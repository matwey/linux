/*
 *    Copyright IBM Corp. 2007
 *    Author(s): Heiko Carstens <heiko.carstens@de.ibm.com>
 */

#define KMSG_COMPONENT "cpu"
#define pr_fmt(fmt) KMSG_COMPONENT ": " fmt

#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/bootmem.h>
#include <linux/sched.h>
#include <linux/workqueue.h>
#include <linux/uaccess.h>
#include <linux/sysctl.h>
#include <linux/cpu.h>
#include <linux/smp.h>
#include <linux/cpuset.h>
#include <asm/delay.h>

#define PTF_HORIZONTAL	(0UL)
#define PTF_VERTICAL	(1UL)
#define PTF_CHECK	(2UL)

enum {
	TOPOLOGY_MODE_HW,
	TOPOLOGY_MODE_SINGLE,
	TOPOLOGY_MODE_PACKAGE,
	TOPOLOGY_MODE_UNINITIALIZED
};

struct mask_info {
	struct mask_info *next;
	unsigned char id;
	cpumask_t mask;
};

static int topology_mode = TOPOLOGY_MODE_UNINITIALIZED;
static void topology_work_fn(struct work_struct *work);
static struct sysinfo_15_1_x *tl_info;
static struct timer_list topology_timer;
static void set_topology_timer(void);
static DECLARE_WORK(topology_work, topology_work_fn);
/* topology_lock protects the core linked list */
static DEFINE_SPINLOCK(topology_lock);

static struct mask_info core_info;
cpumask_t cpu_core_map[NR_CPUS];
unsigned char cpu_core_id[NR_CPUS];

#ifdef CONFIG_SCHED_BOOK
static struct mask_info book_info;
cpumask_t cpu_book_map[NR_CPUS];
unsigned char cpu_book_id[NR_CPUS];
#endif

static cpumask_t cpu_group_map(struct mask_info *info, unsigned int cpu)
{
	cpumask_t mask;

	cpumask_copy(&mask, cpumask_of(cpu));
	switch (topology_mode) {
	case TOPOLOGY_MODE_HW:
		while (info) {
			if (cpumask_test_cpu(cpu, &info->mask)) {
				mask = info->mask;
				break;
			}
			info = info->next;
		}
		if (cpumask_empty(&mask))
			cpumask_copy(&mask, cpumask_of(cpu));
		break;
	case TOPOLOGY_MODE_PACKAGE:
		cpumask_copy(&mask, cpu_present_mask);
		break;
	default:
		/* fallthrough */
	case TOPOLOGY_MODE_SINGLE:
		cpumask_copy(&mask, cpumask_of(cpu));
		break;
	}
	return mask;
}

static struct mask_info *add_cpus_to_mask(struct topology_cpu *tl_cpu,
					  struct mask_info *book,
					  struct mask_info *core,
					  int z10)
{
	unsigned int cpu;

	for (cpu = find_first_bit(&tl_cpu->mask[0], TOPOLOGY_CPU_BITS);
	     cpu < TOPOLOGY_CPU_BITS;
	     cpu = find_next_bit(&tl_cpu->mask[0], TOPOLOGY_CPU_BITS, cpu + 1))
	{
		unsigned int rcpu, lcpu;

		rcpu = TOPOLOGY_CPU_BITS - 1 - cpu + tl_cpu->origin;
		for_each_present_cpu(lcpu) {
			if (cpu_logical_map(lcpu) != rcpu)
				continue;
#ifdef CONFIG_SCHED_BOOK
			cpumask_set_cpu(lcpu, &book->mask);
			cpu_book_id[lcpu] = book->id;
#endif
			cpumask_set_cpu(lcpu, &core->mask);
			if (z10) {
				cpu_core_id[lcpu] = rcpu;
				core = core->next;
			} else {
				cpu_core_id[lcpu] = core->id;
			}
			smp_cpu_polarization[lcpu] = tl_cpu->pp;
		}
	}
	return core;
}

static void clear_masks(void)
{
	struct mask_info *info;

	info = &core_info;
	while (info) {
		cpumask_clear(&info->mask);
		info = info->next;
	}
#ifdef CONFIG_SCHED_BOOK
	info = &book_info;
	while (info) {
		cpumask_clear(&info->mask);
		info = info->next;
	}
#endif
}

static union topology_entry *next_tle(union topology_entry *tle)
{
	if (!tle->nl)
		return (union topology_entry *)((struct topology_cpu *)tle + 1);
	return (union topology_entry *)((struct topology_container *)tle + 1);
}

static void tl_to_cores(struct sysinfo_15_1_x *info)
{
#ifdef CONFIG_SCHED_BOOK
	struct mask_info *book = &book_info;
	struct cpuid cpu_id;
#else
	struct mask_info *book = NULL;
#endif
	struct mask_info *core = &core_info;
	union topology_entry *tle, *end;
	int z10 = 0;

#ifdef CONFIG_SCHED_BOOK
	get_cpu_id(&cpu_id);
	z10 = cpu_id.machine == 0x2097 || cpu_id.machine == 0x2098;
#endif
	spin_lock_irq(&topology_lock);
	clear_masks();
	tle = info->tle;
	end = (union topology_entry *)((unsigned long)info + info->length);
	while (tle < end) {
#ifdef CONFIG_SCHED_BOOK
		if (z10) {
			switch (tle->nl) {
			case 1:
				book = book->next;
				book->id = tle->container.id;
				break;
			case 0:
				core = add_cpus_to_mask(&tle->cpu, book, core, z10);
				break;
			default:
				clear_masks();
				goto out;
			}
			tle = next_tle(tle);
			continue;
		}
#endif
		switch (tle->nl) {
#ifdef CONFIG_SCHED_BOOK
		case 2:
			book = book->next;
			book->id = tle->container.id;
			break;
#endif
		case 1:
			core = core->next;
			core->id = tle->container.id;
			break;
		case 0:
			add_cpus_to_mask(&tle->cpu, book, core, z10);
			break;
		default:
			clear_masks();
			goto out;
		}
		tle = next_tle(tle);
	}
out:
	spin_unlock_irq(&topology_lock);
}

static void topology_update_polarization_simple(void)
{
	int cpu;

	for_each_possible_cpu(cpu)
		smp_cpu_polarization[cpu] = POLARIZATION_HRZ;
}

static int ptf(unsigned long fc)
{
	int rc;

	asm volatile(
		"	.insn	rre,0xb9a20000,%1,%1\n"
		"	ipm	%0\n"
		"	srl	%0,28\n"
		: "=d" (rc)
		: "d" (fc)  : "cc");
	return rc;
}

int topology_set_cpu_management(int fc)
{
	int cpu;
	int rc;

	if (!MACHINE_HAS_TOPOLOGY)
		return -EOPNOTSUPP;
	if (fc)
		rc = ptf(PTF_VERTICAL);
	else
		rc = ptf(PTF_HORIZONTAL);
	if (rc)
		return -EBUSY;
	for_each_possible_cpu(cpu)
		smp_cpu_polarization[cpu] = POLARIZATION_UNKNWN;
	return rc;
}

static void update_cpu_core_map(void)
{
	unsigned long flags;
	int cpu;

	spin_lock_irqsave(&topology_lock, flags);
	for_each_possible_cpu(cpu) {
		cpu_core_map[cpu] = cpu_group_map(&core_info, cpu);
#ifdef CONFIG_SCHED_BOOK
		cpu_book_map[cpu] = cpu_group_map(&book_info, cpu);
#endif
	}
	spin_unlock_irqrestore(&topology_lock, flags);
}

void store_topology(struct sysinfo_15_1_x *info)
{
#ifdef CONFIG_SCHED_BOOK
	int rc;

	rc = stsi(info, 15, 1, 3);
	if (rc != -ENOSYS)
		return;
#endif
	stsi(info, 15, 1, 2);
}

int arch_update_cpu_topology(void)
{
	struct sysinfo_15_1_x *info = tl_info;
	struct sys_device *sysdev;
	int cpu, rc;

	mutex_lock(&smp_cpu_state_mutex);
	if (!MACHINE_HAS_TOPOLOGY) {
		update_cpu_core_map();
		topology_update_polarization_simple();
		rc = 0;
		goto out;
	}
	store_topology(info);
	tl_to_cores(info);
	update_cpu_core_map();
	for_each_online_cpu(cpu) {
		sysdev = get_cpu_sysdev(cpu);
		kobject_uevent(&sysdev->kobj, KOBJ_CHANGE);
	}
	rc = 1;
out:
	mutex_unlock(&smp_cpu_state_mutex);
	return rc;
}

static void topology_work_fn(struct work_struct *work)
{
	rebuild_sched_domains();
}

void topology_schedule_update(void)
{
	schedule_work(&topology_work);
}

static void topology_flush_work(void)
{
	flush_work(&topology_work);
}

static void topology_timer_fn(unsigned long ignored)
{
	if (ptf(PTF_CHECK))
		topology_schedule_update();
	set_topology_timer();
}

static void set_topology_timer(void)
{
	topology_timer.function = topology_timer_fn;
	topology_timer.data = 0;
	topology_timer.expires = jiffies + 60 * HZ;
	add_timer(&topology_timer);
}

static inline int topology_get_mode(int enabled)
{
	if (!enabled)
		return TOPOLOGY_MODE_SINGLE;
	return MACHINE_HAS_TOPOLOGY ? TOPOLOGY_MODE_HW : TOPOLOGY_MODE_PACKAGE;
}

static inline int topology_is_enabled(void)
{
	return topology_mode != TOPOLOGY_MODE_SINGLE;
}

static int __init topology_setup(char *str)
{
	int enabled;

	if (!strncmp(str, "off", 3))
		enabled = 0;
	else if (!strncmp(str, "on", 2))
		enabled = 1;
	else
		return -EINVAL;
	topology_mode = topology_get_mode(enabled);
	return 0;
}
early_param("topology", topology_setup);

static int topology_ctl_handler(struct ctl_table *ctl, int write,
				void __user *buffer, size_t *lenp, loff_t *ppos)
{
	unsigned int len;
	int new_mode;
	char buf[2];

	if (!*lenp || *ppos) {
		*lenp = 0;
		return 0;
	}
	if (!write) {
		strncpy(buf, topology_is_enabled() ? "1\n" : "0\n",
			ARRAY_SIZE(buf));
		len = strnlen(buf, ARRAY_SIZE(buf));
		if (len > *lenp)
			len = *lenp;
		if (copy_to_user(buffer, buf, len))
			return -EFAULT;
		goto out;
	}
	len = *lenp;
	if (copy_from_user(buf, buffer, len > sizeof(buf) ? sizeof(buf) : len))
		return -EFAULT;
	if (buf[0] != '0' && buf[0] != '1')
		return -EINVAL;
	mutex_lock(&smp_cpu_state_mutex);
	new_mode = topology_get_mode(buf[0] == '1');
	if (topology_mode != new_mode) {
		topology_mode = new_mode;
		topology_schedule_update();
	}
	mutex_unlock(&smp_cpu_state_mutex);
	topology_flush_work();
out:
	*lenp = len;
	*ppos += len;
	return 0;
}

static struct ctl_table topology_ctl_table[] = {
	{
		.procname       = "topology",
		.mode           = 0644,
		.proc_handler   = topology_ctl_handler,
	},
	{ },
};

static struct ctl_table topology_dir_table[] = {
	{
		.procname       = "s390",
		.maxlen         = 0,
		.mode           = 0555,
		.child          = topology_ctl_table,
	},
	{ },
};

static int __init init_topology_update(void)
{
	int rc;

	register_sysctl_table(topology_dir_table);
	rc = 0;
	if (!MACHINE_HAS_TOPOLOGY) {
		topology_update_polarization_simple();
		goto out;
	}
	init_timer_deferrable(&topology_timer);
	set_topology_timer();
out:
	update_cpu_core_map();
	return rc;
}
__initcall(init_topology_update);

static void alloc_masks(struct sysinfo_15_1_x *info, struct mask_info *mask,
			int offset)
{
	int i, nr_masks;

	nr_masks = info->mag[TOPOLOGY_NR_MAG - offset];
	for (i = 0; i < info->mnest - offset; i++)
		nr_masks *= info->mag[TOPOLOGY_NR_MAG - offset - 1 - i];
	nr_masks = max(nr_masks, 1);
	for (i = 0; i < nr_masks; i++) {
		mask->next = alloc_bootmem(sizeof(struct mask_info));
		mask = mask->next;
	}
}

void __init s390_init_cpu_topology(void)
{
	struct sysinfo_15_1_x *info;
	int i;

	if (topology_mode == TOPOLOGY_MODE_UNINITIALIZED) {
		if (MACHINE_HAS_TOPOLOGY)
			topology_mode = TOPOLOGY_MODE_HW;
		else
			topology_mode = TOPOLOGY_MODE_SINGLE;
	}
	if (!MACHINE_HAS_TOPOLOGY)
		return;
	tl_info = alloc_bootmem_pages(PAGE_SIZE);
	info = tl_info;
	store_topology(info);
	pr_info("The CPU configuration topology of the machine is:");
	for (i = 0; i < TOPOLOGY_NR_MAG; i++)
		printk(" %d", info->mag[i]);
	printk(" / %d\n", info->mnest);
	alloc_masks(info, &core_info, 1);
#ifdef CONFIG_SCHED_BOOK
	alloc_masks(info, &book_info, 2);
#endif
}

/*
 * Copyright (C) 2010 ST-Ericsson AB
 * Mian Yousaf Kaukab <mian.yousaf.kaukab@stericsson.com>
 *
 * Based on omap2430.c
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/platform_device.h>

#include "musb_core.h"

struct ux500_glue {
	struct device		*dev;
	struct platform_device	*musb;
	struct clk		*clk;
};
#define glue_to_musb(g)	platform_get_drvdata(g->musb)

static irqreturn_t ux500_musb_interrupt(int irq, void *__hci)
{
	unsigned long   flags;
	irqreturn_t     retval = IRQ_NONE;
	struct musb     *musb = __hci;

	spin_lock_irqsave(&musb->lock, flags);

	musb->int_usb = musb_readb(musb->mregs, MUSB_INTRUSB);
	musb->int_tx = musb_readw(musb->mregs, MUSB_INTRTX);
	musb->int_rx = musb_readw(musb->mregs, MUSB_INTRRX);

	if (musb->int_usb || musb->int_tx || musb->int_rx)
		retval = musb_interrupt(musb);

	spin_unlock_irqrestore(&musb->lock, flags);

	return retval;
}

static int ux500_musb_init(struct musb *musb)
{
	musb->xceiv = usb_get_phy(USB_PHY_TYPE_USB2);
	if (IS_ERR_OR_NULL(musb->xceiv)) {
		pr_err("HS USB OTG: no transceiver configured\n");
		return -EPROBE_DEFER;
	}

	musb->isr = ux500_musb_interrupt;

	return 0;
}

static int ux500_musb_exit(struct musb *musb)
{
	usb_put_phy(musb->xceiv);

	return 0;
}

static const struct musb_platform_ops ux500_ops = {
	.init		= ux500_musb_init,
	.exit		= ux500_musb_exit,
};

static struct musb_hdrc_platform_data *
ux500_of_probe(struct platform_device *pdev, struct device_node *np)
{
	struct musb_hdrc_platform_data *pdata;
	const char *mode;
	int strlen;

	pdata = devm_kzalloc(&pdev->dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata)
		return NULL;

	mode = of_get_property(np, "dr_mode", &strlen);
	if (!mode) {
		dev_err(&pdev->dev, "No 'dr_mode' property found\n");
		return NULL;
	}

	if (strlen > 0) {
		if (!strcmp(mode, "host"))
			pdata->mode = MUSB_HOST;
		if (!strcmp(mode, "otg"))
			pdata->mode = MUSB_OTG;
		if (!strcmp(mode, "peripheral"))
			pdata->mode = MUSB_PERIPHERAL;
	}

	return pdata;
}

static int ux500_probe(struct platform_device *pdev)
{
	struct resource musb_resources[2];
	struct musb_hdrc_platform_data	*pdata = pdev->dev.platform_data;
	struct device_node		*np = pdev->dev.of_node;
	struct platform_device		*musb;
	struct ux500_glue		*glue;
	struct clk			*clk;
	int				ret = -ENOMEM;

	if (!pdata) {
		if (np) {
			pdata = ux500_of_probe(pdev, np);
			if (!pdata)
				goto err0;

			pdev->dev.platform_data = pdata;
		} else {
			dev_err(&pdev->dev, "no pdata or device tree found\n");
			goto err0;
		}
	}

	glue = kzalloc(sizeof(*glue), GFP_KERNEL);
	if (!glue) {
		dev_err(&pdev->dev, "failed to allocate glue context\n");
		goto err0;
	}

	musb = platform_device_alloc("musb-hdrc", PLATFORM_DEVID_AUTO);
	if (!musb) {
		dev_err(&pdev->dev, "failed to allocate musb device\n");
		goto err1;
	}

	clk = clk_get(&pdev->dev, "usb");
	if (IS_ERR(clk)) {
		dev_err(&pdev->dev, "failed to get clock\n");
		ret = PTR_ERR(clk);
		goto err3;
	}

	ret = clk_prepare_enable(clk);
	if (ret) {
		dev_err(&pdev->dev, "failed to enable clock\n");
		goto err4;
	}

	musb->dev.parent		= &pdev->dev;
	musb->dev.dma_mask		= pdev->dev.dma_mask;
	musb->dev.coherent_dma_mask	= pdev->dev.coherent_dma_mask;
	musb->dev.of_node		= pdev->dev.of_node;

	glue->dev			= &pdev->dev;
	glue->musb			= musb;
	glue->clk			= clk;

	pdata->platform_ops		= &ux500_ops;

	platform_set_drvdata(pdev, glue);

	memset(musb_resources, 0x00, sizeof(*musb_resources) *
			ARRAY_SIZE(musb_resources));

	musb_resources[0].name = pdev->resource[0].name;
	musb_resources[0].start = pdev->resource[0].start;
	musb_resources[0].end = pdev->resource[0].end;
	musb_resources[0].flags = pdev->resource[0].flags;

	musb_resources[1].name = pdev->resource[1].name;
	musb_resources[1].start = pdev->resource[1].start;
	musb_resources[1].end = pdev->resource[1].end;
	musb_resources[1].flags = pdev->resource[1].flags;

	ret = platform_device_add_resources(musb, musb_resources,
			ARRAY_SIZE(musb_resources));
	if (ret) {
		dev_err(&pdev->dev, "failed to add resources\n");
		goto err5;
	}

	ret = platform_device_add_data(musb, pdata, sizeof(*pdata));
	if (ret) {
		dev_err(&pdev->dev, "failed to add platform_data\n");
		goto err5;
	}

	ret = platform_device_add(musb);
	if (ret) {
		dev_err(&pdev->dev, "failed to register musb device\n");
		goto err5;
	}

	return 0;

err5:
	clk_disable_unprepare(clk);

err4:
	clk_put(clk);

err3:
	platform_device_put(musb);

err1:
	kfree(glue);

err0:
	return ret;
}

static int ux500_remove(struct platform_device *pdev)
{
	struct ux500_glue	*glue = platform_get_drvdata(pdev);

	platform_device_unregister(glue->musb);
	clk_disable_unprepare(glue->clk);
	clk_put(glue->clk);
	kfree(glue);

	return 0;
}

#ifdef CONFIG_PM
static int ux500_suspend(struct device *dev)
{
	struct ux500_glue	*glue = dev_get_drvdata(dev);
	struct musb		*musb = glue_to_musb(glue);

	usb_phy_set_suspend(musb->xceiv, 1);
	clk_disable_unprepare(glue->clk);

	return 0;
}

static int ux500_resume(struct device *dev)
{
	struct ux500_glue	*glue = dev_get_drvdata(dev);
	struct musb		*musb = glue_to_musb(glue);
	int			ret;

	ret = clk_prepare_enable(glue->clk);
	if (ret) {
		dev_err(dev, "failed to enable clock\n");
		return ret;
	}

	usb_phy_set_suspend(musb->xceiv, 0);

	return 0;
}

static const struct dev_pm_ops ux500_pm_ops = {
	.suspend	= ux500_suspend,
	.resume		= ux500_resume,
};

#define DEV_PM_OPS	(&ux500_pm_ops)
#else
#define DEV_PM_OPS	NULL
#endif

static const struct of_device_id ux500_match[] = {
        { .compatible = "stericsson,db8500-musb", },
        {}
};

static struct platform_driver ux500_driver = {
	.probe		= ux500_probe,
	.remove		= ux500_remove,
	.driver		= {
		.name	= "musb-ux500",
		.pm	= DEV_PM_OPS,
		.of_match_table = ux500_match,
	},
};

MODULE_DESCRIPTION("UX500 MUSB Glue Layer");
MODULE_AUTHOR("Mian Yousaf Kaukab <mian.yousaf.kaukab@stericsson.com>");
MODULE_LICENSE("GPL v2");
module_platform_driver(ux500_driver);

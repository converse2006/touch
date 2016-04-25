/*
 * siw_touch_bus.c - SiW touch bus core driver
 *
 * Copyright (C) 2016 Silicon Works - http://www.siliconworks.co.kr
 * Author: Hyunho Kim <kimhh@siliconworks.co.kr>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/async.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/sched.h>
#include <linux/delay.h>
#include <linux/input.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/irqreturn.h>
#include <linux/slab.h>
#include <linux/pm.h>
#include <linux/gpio.h>
#include <linux/string.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/of_device.h>
#include <asm/page.h>
#include <asm/uaccess.h>
#include <asm/irq.h>
#include <asm/io.h>
#include <asm/memory.h>

#include <linux/spi/spi.h>

#include <linux/gpio.h>
#include <linux/regulator/consumer.h>

#include "siw_touch.h"
#include "siw_touch_bus.h"
#include "siw_touch_bus_i2c.h"
#include "siw_touch_bus_spi.h"

#define TOUCH_PINCTRL_ACTIVE	"touch_pin_active"
#define TOUCH_PINCTRL_SLEEP		"touch_pin_sleep"

#if defined(__SIW_SUPPORT_PINCTRL)	//See siw_touch_cfg.h
int siw_touch_bus_pin_get(struct siw_ts *ts)
{
	struct device *dev = ts->dev;
	struct touch_pinctrl *pinctrl = &ts->pinctrl;
	struct pinctrl *pin_ctrl = NULL;
	struct pinctrl_state *pin_active = NULL;
	struct pinctrl_state *pin_suspend = NULL;
	int ret = 0;

	t_dev_dbg_base(dev, "get pinctrl\n");

	pin_ctrl = devm_pinctrl_get(dev);
	if (IS_ERR_OR_NULL(pin_ctrl)) {
		if (PTR_ERR(pin_ctrl) == -EPROBE_DEFER) {
			ret = -EPROBE_DEFER;
			return ret;
		}

		t_dev_info(dev, "pinctrl not used\n");
		goto pin_skip;
	}

	pin_active = pinctrl_lookup_state(pin_ctrl,
								TOUCH_PINCTRL_ACTIVE);
	if (IS_ERR_OR_NULL(pin_active)) {
		t_dev_dbg_gpio(dev, "cannot get pinctrl active\n");
		goto pin_skip;
	}

	pin_suspend = pinctrl_lookup_state(pin_ctrl,
								TOUCH_PINCTRL_SLEEP);
	if (IS_ERR_OR_NULL(pin_suspend)) {
		t_dev_dbg_gpio(dev, "cannot get pinctrl suspend\n");
		goto pin_skip;
	}

	ret = pinctrl_select_state(pin_ctrl, pin_active);
	if (ret) {
		t_dev_dbg_gpio(dev, "cannot set pinctrl active\n");
		goto pin_skip;
	}

	t_dev_info(dev, "pinctrl set active\n");

	pinctrl->ctrl = pin_ctrl;
	pinctrl->active = pin_active;
	pinctrl->suspend = pin_suspend;

pin_skip:
	if (touch_bus_type(ts) == BUS_IF_SPI) {
		ret = spi_setup(to_spi_device(dev));
		if (ret < 0) {
			t_dev_err(dev, "failed to perform SPI setup\n");
		}
	}

	return ret;
}

int siw_touch_bus_pin_put(struct siw_ts *ts)
{
	struct device *dev = ts->dev;
	struct touch_pinctrl *pinctrl = &ts->pinctrl;

	t_dev_dbg_base(dev, "put pinctrl\n");

	if (pinctrl->ctrl && !IS_ERR_OR_NULL(pinctrl->ctrl)) {
		devm_pinctrl_put(ts->pinctrl.ctrl);
		memset((void *)pinctrl, 0, sizeof(struct touch_pinctrl));
	}

	return 0;
}
#else	/* __SIW_SUPPORT_PINCTRL */
int siw_touch_bus_pin_get(struct siw_ts *ts)
{
	struct device *dev = ts->dev;

	t_dev_dbg_base(dev, "get pinctrl, nop ...\n");
	return 0;
}

int siw_touch_bus_pin_put(struct siw_ts *ts)
{
	struct device *dev = ts->dev;

	t_dev_dbg_base(dev, "put pinctrl, nop ...\n");
	return 0;
}
#endif	/* __SIW_SUPPORT_PINCTRL */

int siw_touch_bus_alloc_buffer(struct siw_ts *ts)
{
	struct device *dev = ts->dev;
	int buf_size = touch_buf_size(ts);
	u8 *tx_buf = NULL;
	u8 *rx_buf = NULL;
	struct touch_xfer_msg *xfer = NULL;
	int ret = 0;

	if (!buf_size)
		buf_size = SIW_TOUCH_MAX_XFER_BUF_SIZE;

	t_dev_dbg_base(dev, "allocate touch bus buffer\n");

	tx_buf = devm_kzalloc(dev, buf_size, GFP_KERNEL);
	if (!tx_buf) {
		t_dev_err(dev, "failed to allocate tx_buf\n");
		goto out_tx_buf;
	}
	t_dev_dbg_base(dev, "tx_buf : 0x%08X(0x%X)\n",
				(u32)tx_buf, buf_size);

	rx_buf = devm_kzalloc(dev, buf_size, GFP_KERNEL);
	if (!rx_buf) {
		t_dev_err(dev, "failed to allocate rx_buf\n");
		goto out_rx_buf;
	}
	t_dev_dbg_base(dev, "rx_buf : 0x%08X(0x%X)\n",
				(u32)rx_buf, buf_size);

	xfer = devm_kzalloc(dev, sizeof(struct touch_xfer_msg), GFP_KERNEL);
	if (!xfer) {
		t_dev_err(dev, "failed to allocate xfer\n");
		goto out_xfer;
	}
	t_dev_dbg_base(dev, "xfer   : 0x%08X(0x%X)\n",
				(u32)xfer, sizeof(struct touch_xfer_msg));

	ts->tx_buf = tx_buf;
	ts->rx_buf = rx_buf;
	ts->xfer = xfer;

	return 0;

out_xfer:
	if (rx_buf) {
		devm_kfree(dev, rx_buf);
	}

out_rx_buf:
	if (tx_buf) {
		devm_kfree(dev, tx_buf);
	}

out_tx_buf:

	return ret;
}

int siw_touch_bus_free_buffer(struct siw_ts *ts)
{
	struct device *dev = ts->dev;

	t_dev_dbg_base(dev, "release touch bus buffer\n");

	if (ts->xfer) {
		devm_kfree(dev, ts->xfer);
		ts->xfer = NULL;
	}

	if (ts->rx_buf) {
		devm_kfree(dev, ts->rx_buf);
		ts->rx_buf= NULL;
	}

	if (ts->tx_buf) {
		devm_kfree(dev, ts->tx_buf);
		ts->tx_buf = NULL;
	}

	return 0;
}

int siw_touch_bus_read(struct device *dev,
					struct touch_bus_msg *msg)
{
	struct siw_ts *ts = to_touch_core(dev);

	if (!ts->bus_read) {
		t_dev_err(dev, "no bus_read %s(%d)\n",
			dev_name(dev), touch_bus_type(ts));
		return -EINVAL;
	}

	return ts->bus_read(dev, msg);
}

int siw_touch_bus_write(struct device *dev, struct touch_bus_msg *msg)
{
	struct siw_ts *ts = to_touch_core(dev);

	if (!ts->bus_write) {
		t_dev_err(dev, "no bus_write for %s(%d)\n",
			dev_name(dev), touch_bus_type(ts));
		return -EINVAL;
	}

	return ts->bus_write(dev, msg);
}

int siw_touch_bus_xfer(struct device *dev, struct touch_xfer_msg *xfer)
{
	struct siw_ts *ts = to_touch_core(dev);

	if (!ts->bus_xfer) {
		t_dev_err(dev, "No bus_xfer for %s(%d)\n",
			dev_name(dev), touch_bus_type(ts));
		return -EINVAL;
	}

	return ts->bus_xfer(dev, xfer);
}

enum {
	SIW_BUS_ERR_PRT_BOUNDARY	= 16,
	SIW_BUS_ERR_PRT_BUF_SZ		= 128,
};
void siw_touch_bus_err_dump_data(struct device *dev,
							u8 *buf, int len,
							int idx, char *name)
{
	char __prt_buf[SIW_BUS_ERR_PRT_BUF_SZ + 1] = {0, };
	char *prt_buf;
	int prt_len;
	int prt_pos = 0;
	int prt_idx;
	int cnt, total;
	int i, k;

	if (!buf || !len)
		return;

	cnt = (len + SIW_BUS_ERR_PRT_BOUNDARY - 1)/SIW_BUS_ERR_PRT_BOUNDARY;
	total = cnt;

	prt_idx = 0;
	for (i=0 ; i<cnt ; i++) {
		prt_len = min(len, SIW_BUS_ERR_PRT_BOUNDARY);
		prt_buf = __prt_buf;
		prt_pos = 0;
		for (k=0 ; k<prt_len ; k++) {
			prt_pos += snprintf(prt_buf + prt_pos,
							SIW_BUS_ERR_PRT_BUF_SZ - prt_pos,
							"%02X ",
							buf[prt_idx+k]);
			if (prt_pos >= SIW_BUS_ERR_PRT_BUF_SZ) {
				cnt = 0;
				break;
			}
		}
		t_dev_err(dev,
				" - %s[%d] buf[%3d~%3d] %s\n",
				name, idx,
				prt_idx,
				prt_idx + prt_len - 1,
				__prt_buf);

		len -= prt_len;
		prt_idx += prt_len;
	}
}


static const struct siw_op_dbg siw_bus_init_ops[2][2] = {
	[BUS_IF_I2C] = {
		_SIW_OP_DBG(DRIVER_FREE, siw_touch_i2c_del_driver, NULL, 0),
		_SIW_OP_DBG(DRIVER_INIT, siw_touch_i2c_add_driver, NULL, 0),
	},
	[BUS_IF_SPI] = {
		_SIW_OP_DBG(DRIVER_FREE, siw_touch_spi_del_driver, NULL, 0),
		_SIW_OP_DBG(DRIVER_INIT, siw_touch_spi_add_driver, NULL, 0),
	},
};

#define SIW_BUS_MAX		sizeof(siw_bus_init_ops) / sizeof(siw_bus_init_ops[0])

static int __siw_touch_bus_add_chk(struct siw_touch_pdata *pdata)
{
	if (pdata == NULL) {
		t_pr_err("NULL touch driver\n");
		return -ENODEV;
	}

	if (pdata_bus_type(pdata) >= SIW_BUS_MAX) {
		t_pr_err("Unknown touch interface : %d\n", pdata_bus_type(pdata));
		return -EINVAL;
	}

	return 0;
}

static int __siw_touch_bus_add_op(
					struct siw_touch_pdata *pdata,
					void *op_func)
{
	struct siw_op_dbg *op = op_func;
	int ret = 0;

	ret = __siw_touch_bus_add_chk(pdata);
	if (ret)
		goto out;

//	t_pr_info("%s\n", op->name);
	ret = __siw_touch_op_dbg(op, pdata);
	if (ret) {
		t_pr_err("%s failed, %d\n", op->name, ret);
		goto out;
	}

out:
	return ret;
}

static int __siw_touch_bus_add_driver(struct siw_touch_pdata *pdata, int on_off)
{
	return __siw_touch_bus_add_op(pdata,
			(void *)&siw_bus_init_ops[pdata_bus_type(pdata)][on_off]);
}

int siw_touch_bus_add_driver(struct siw_touch_pdata *pdata)
{
	return __siw_touch_bus_add_driver(pdata, DRIVER_INIT);
}

int siw_touch_bus_del_driver(struct siw_touch_pdata *pdata)
{
	return __siw_touch_bus_add_driver(pdata, DRIVER_FREE);
}



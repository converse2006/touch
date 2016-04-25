/*
 * siw_touch_sysfs.c - SiW touch sysfs driver
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
#include <linux/err.h>
#include <linux/sched.h>
#include <linux/delay.h>
#include <linux/input.h>
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

#include "siw_touch.h"
#include "siw_touch_hal.h"
#include "siw_touch_irq.h"
#include "siw_touch_sys.h"


static const char *siw_ime_str[] = {
	"OFF",
	"ON",
	"SWYPE",
};

static const char *siw_mfts_str[] = {
	"NONE",
	"FOLDER",
	"FLAT",
	"CURVED",
};

#if defined(__SIW_SUPPORT_ASC)
static const char *siw_incoming_call_str[] = {
	"IDLE",
	"RINGING",
	"OFFHOOK",
};

static const char *siw_onhand_str[] = {
	"IN_HAND_ATTN",
	"NOT_IN_HAND",
	"IN_HAND_NO_ATTN",
};
#endif	/* __SIW_SUPPORT_ASC */


#define siw_sysfs_err_invalid_param(_dev)	\
		t_dev_err(_dev, "Invalid param\n");

#define _plat_data_snprintf(_buf, _size, args...)	\
		siw_snprintf(_buf, _size, " %-25s = %d\n", ##args)

static ssize_t _show_plat_data(struct device *dev, char *buf)
{
	struct siw_ts *ts = to_touch_core(dev);
	struct touch_device_caps *caps = &ts->caps;
	int i;
	int size = 0;

	size += siw_snprintf(buf, size, "=== Platform Data ===\n");
	size += _plat_data_snprintf(buf, size,
					"reset_pin", ts->pins.reset_pin);
	size += _plat_data_snprintf(buf, size,
					"irq_pin", ts->pins.irq_pin);
	size += _plat_data_snprintf(buf, size,
					"maker_id_pin", ts->pins.maker_id_pin);

	size += siw_snprintf(buf, size, "power:\n");
	size += _plat_data_snprintf(buf, size,
					"vdd-gpio", ts->pins.vdd_pin);
	size += _plat_data_snprintf(buf, size,
					"vio-gpio", ts->pins.vio_pin);

	size += siw_snprintf(buf, size, "caps:\n");
	size += _plat_data_snprintf(buf, size,
					"max_x", caps->max_x);
	size += _plat_data_snprintf(buf, size,
					"max_y", caps->max_y);
	size += _plat_data_snprintf(buf, size,
					"max_pressure", caps->max_pressure);
	size += _plat_data_snprintf(buf, size,
					"max_width", caps->max_width);
	size += _plat_data_snprintf(buf, size,
					"max_orientation", caps->max_orientation);
	size += _plat_data_snprintf(buf, size,
					"max_id", caps->max_id);
	size += _plat_data_snprintf(buf, size,
					"hw_reset_delay", caps->hw_reset_delay);
	size += _plat_data_snprintf(buf, size,
					"sw_reset_delay", caps->sw_reset_delay);

	size += siw_snprintf(buf, size, "role:\n");
	size += _plat_data_snprintf(buf, size,
					"use_lpwg", ts->role.use_lpwg);
	size += _plat_data_snprintf(buf, size,
					"use_firmware", ts->role.use_firmware);
	size += _plat_data_snprintf(buf, size,
					"use_fw_upgrade", ts->role.use_fw_upgrade);

	size += siw_snprintf(buf, size, "firmware:\n");
	size += _plat_data_snprintf(buf, size,
					"def_fwcnt", ts->def_fwcnt);
	for (i = 0; i < ts->def_fwcnt; i++)
		size += siw_snprintf(buf, size, " %-25s : [%d] %s\n",
						"def_fwpath", i, ts->def_fwpath[i]);

	return size;
}

static ssize_t _store_upgrade(struct device *dev,
				const char *buf, size_t count)
{
	struct siw_ts *ts = to_touch_core(dev);

	if (sscanf(buf, "%255s", &ts->test_fwpath[0]) <= 0) {
		siw_sysfs_err_invalid_param(dev);
		return count;
	}

	ts->force_fwup = 1;

	siw_touch_qd_upgrade_work_now(ts);

	return count;
}

static ssize_t _show_upgrade(struct device *dev, char *buf)
{
	struct siw_ts *ts = to_touch_core(dev);

	ts->test_fwpath[0] = '\0';
	ts->force_fwup = 1;

	siw_touch_qd_upgrade_work_now(ts);

	return 0;
}

static int __show_do_lpwg_data(struct device *dev, char *buf)
{
	struct siw_ts *ts = to_touch_core(dev);
	int i;
	int size = 0;

	for (i = 0; i < MAX_LPWG_CODE; i++) {
		if (ts->lpwg.code[i].x == -1 && ts->lpwg.code[i].y == -1)
			break;
		size += siw_snprintf(buf, size, "%d %d\n",
				ts->lpwg.code[i].x, ts->lpwg.code[i].y);
	}
	memset(ts->lpwg.code, 0, sizeof(struct point) * MAX_LPWG_CODE);

	return size;
}

static ssize_t _show_lpwg_data(struct device *dev, char *buf)
{
	struct siw_ts *ts = to_touch_core(dev);
	int ret = 0;

	if (siw_ops_is_null(ts, lpwg))
		return ret;

	mutex_lock(&ts->lock);
	ret = __show_do_lpwg_data(dev, buf);
	mutex_unlock(&ts->lock);

	return ret;
}

static ssize_t _store_lpwg_data(struct device *dev,
				const char *buf, size_t count)
{
	struct siw_ts *ts = to_touch_core(dev);
	int reply = 0;

	if (sscanf(buf, "%d", &reply) <= 0) {
		siw_sysfs_err_invalid_param(dev);
		return count;
	}

	t_dev_info(dev, "reply %d\n", reply);

	atomic_set(&ts->state.uevent, UEVENT_IDLE);
	wake_unlock(&ts->lpwg_wake_lock);

	return count;
}

static ssize_t _store_lpwg_notify(struct device *dev,
				const char *buf, size_t count)
{
	struct siw_ts *ts = to_touch_core(dev);
	int code = 0;
	int param[4] = {0, };
	int mfts_mode = 0;

	mfts_mode = siw_touch_boot_mode_check(dev);
	if ((mfts_mode >= MINIOS_MFTS_FOLDER) && !ts->role.mfts_lpwg)
		return count;

	if (sscanf(buf, "%d %d %d %d %d",
			&code, &param[0], &param[1], &param[2], &param[3]) <= 0) {
		siw_sysfs_err_invalid_param(dev);
		return count;
	}

	/* only below code notify
		3 active_area
		4 knockcode tap count
		8 knockcode double tap check
		9 update_all
	*/
	if (code == 1 || code == 2 || code == 5 ||
		code == 6 || code == 7)
		return count;

	mutex_lock(&ts->lock);
	siw_ops_lpwg(ts, code, param);
	mutex_unlock(&ts->lock);

	return count;
}

static ssize_t _show_lockscreen_state(struct device *dev, char *buf)
{
	struct siw_ts *ts = to_touch_core(dev);
	int value = 0;
	int size = 0;

	value = atomic_read(&ts->state.lockscreen);

	size += siw_snprintf(buf, size, "Lock-screen : %s(%d)\n",
				value ? "LOCK" : "UNLOCK", value);

	return (ssize_t)size;
}

static ssize_t _store_lockscreen_state(struct device *dev,
				const char *buf, size_t count)
{
	struct siw_ts *ts = to_touch_core(dev);
	int value = 0;

	if (sscanf(buf, "%d", &value) <= 0) {
		siw_sysfs_err_invalid_param(dev);
		return count;
	}

	if (value == LOCKSCREEN_UNLOCK || value == LOCKSCREEN_LOCK) {
		atomic_set(&ts->state.lockscreen, value);
		t_dev_info(dev, "Lock-screen : %s(%d)\n", value ? "LOCK" : "UNLOCK", value);
	} else {
		t_dev_info(dev, "Lock-screen : Unknown state, %d\n", value);
	}

	return count;
}

static ssize_t _show_ime_state(struct device *dev, char *buf)
{
	struct siw_ts *ts = to_touch_core(dev);
	int value = 0;
	int size = 0;

	value = atomic_read(&ts->state.ime);

	size += siw_snprintf(buf, size, "%s(%d)\n",
				siw_ime_str[value], value);

	return (ssize_t)size;
}

static ssize_t _store_ime_state(struct device *dev,
				const char *buf, size_t count)
{
	struct siw_ts *ts = to_touch_core(dev);
	int value = 0;
	int ret = 0;

	if (sscanf(buf, "%d", &value) <= 0) {
		siw_sysfs_err_invalid_param(dev);
		goto out;
	}

	if ((value < IME_OFF) || (value > IME_SWYPE)) {
		t_dev_warn(dev, "IME : Unknown state, %d\n", value);
		goto out;
	}

	if (atomic_read(&ts->state.ime) == value)
		goto out;

	if (touch_test_quirks(ts, CHIP_QUIRK_NOT_SUPPORT_IME)) {
		t_dev_err(dev, "ime control not supporeted in %s\n",
				touch_chip_name(ts));
		goto out;
	}

	atomic_set(&ts->state.ime, value);
	ret = siw_touch_blocking_notifier_call(NOTIFY_IME_STATE,
			&ts->state.ime);
	t_dev_info(dev, "IME : %s(%d), %d\n",
			siw_ime_str[value], value, ret);

out:
	return count;
}

static ssize_t _show_quick_cover_state(struct device *dev, char *buf)
{
	struct siw_ts *ts = to_touch_core(dev);
	int value = 0;
	int size = 0;

	value = atomic_read(&ts->state.quick_cover);

	size += siw_snprintf(buf, size, "Qcover : %s(%d)\n",
				value ? "CLOSE" : "OPEN", value);

	return (ssize_t)size;
}

static ssize_t _store_quick_cover_state(struct device *dev,
				const char *buf, size_t count)
{
	int value = 0;
	struct siw_ts *ts = to_touch_core(dev);

	if (sscanf(buf, "%d", &value) <= 0) {
		siw_sysfs_err_invalid_param(dev);
		return count;
	}

	if (value == QUICKCOVER_CLOSE || value == QUICKCOVER_OPEN) {
		atomic_set(&ts->state.quick_cover, value);
		t_dev_info(dev, "Qcover : %s(%d)\n",
			value ? "CLOSE" : "OPEN", value);
	} else {
		t_dev_info(dev, "Qcover : Unknown state, %d\n", value);
	}

	return count;
}

static ssize_t _show_version_info(struct device *dev, char *buf)
{
	return siw_touch_get(dev, CMD_VERSION, buf);
}

static ssize_t _show_atcmd_version_info(struct device *dev, char *buf)
{
	return siw_touch_get(dev, CMD_ATCMD_VERSION, buf);
}

static ssize_t _show_mfts_state(struct device *dev, char *buf)
{
	struct siw_ts *ts = to_touch_core(dev);
	int value = 0;
	int size = 0;

	value = atomic_read(&ts->state.mfts);

	size += siw_snprintf(buf, size, "%s : %s(%d)\n",
				__func__,
				siw_mfts_str[value], value);

	return (ssize_t)size;
}

static ssize_t _store_mfts_state(struct device *dev,
				const char *buf, size_t count)
{
	struct siw_ts *ts = to_touch_core(dev);
	int value = 0;

	if (sscanf(buf, "%d", &value) <= 0) {
		siw_sysfs_err_invalid_param(dev);
		return count;
	}

	if (value >= MFTS_NONE && value <= MFTS_CURVED) {
		atomic_set(&ts->state.mfts, value);
		t_dev_info(dev, "MFTS : %s(%d)\n", siw_mfts_str[value], value);
	} else {
		t_dev_info(dev, "MFTS : Unknown state, %d\n", value);
	}

	return count;
}

static ssize_t _show_mfts_lpwg(struct device *dev, char *buf)
{
	struct siw_ts *ts = to_touch_core(dev);
	int size = 0;

	size += siw_snprintf(buf, size, "MFTS LPWG : %d\n",
				ts->role.use_lpwg_test);

	return (ssize_t)size;
}

static ssize_t _store_mfts_lpwg(struct device *dev,
				const char *buf, size_t count)
{
	struct siw_ts *ts = to_touch_core(dev);
	int value = 0;

	if (sscanf(buf, "%d", &value) <= 0) {
		siw_sysfs_err_invalid_param(dev);
		return count;
	}

	ts->role.mfts_lpwg = value;
	t_dev_info(dev, "MFTS LPWG : %d\n", ts->role.mfts_lpwg);

	return count;
}

static ssize_t _show_sp_link_touch_off(struct device *dev, char *buf)
{
	struct siw_ts *ts = to_touch_core(dev);
	int size = 0;

	size += siw_snprintf(buf, size, "SP link touch status %d\n",
				atomic_read(&ts->state.sp_link));

	return (ssize_t)size;
}

static ssize_t _store_sp_link_touch_off(struct device *dev,
				const char *buf, size_t count)
{
	struct siw_ts *ts = to_touch_core(dev);
	int value = 0;

	if (sscanf(buf, "%d", &value) <= 0) {
		siw_sysfs_err_invalid_param(dev);
		return count;
	}

	atomic_set(&ts->state.sp_link, value);

	if (atomic_read(&ts->state.sp_link) == SP_CONNECT) {
		siw_touch_irq_control(ts->dev, INTERRUPT_DISABLE);
		t_dev_info(dev, "SP Mirroring Connected\n");
	} else if(atomic_read(&ts->state.sp_link) == SP_DISCONNECT) {
		siw_touch_irq_control(ts->dev, INTERRUPT_ENABLE);
		t_dev_info(dev, "SP Mirroring Disconnected\n");
	}

	return count;
}

static ssize_t _show_debug_tool_state(struct device *dev, char *buf)
{
	struct siw_ts *ts = to_touch_core(dev);
	int value = 0;
	int size = 0;

	value = atomic_read(&ts->state.debug_tool);

	size += siw_snprintf(buf, size, "Debug tool state : %d\n", value);

	return (ssize_t)size;
}

static ssize_t _store_debug_tool_state(struct device *dev,
				const char *buf, size_t count)
{
	struct siw_ts *ts = to_touch_core(dev);
	int data = 0;

	if (sscanf(buf, "%d", &data) <= 0) {
		siw_sysfs_err_invalid_param(dev);
		return count;
	}

	if (siw_ops_is_null(ts, notify))
		return count;

	if (data >= DEBUG_TOOL_DISABLE && data <= DEBUG_TOOL_ENABLE) {
		atomic_set(&ts->state.debug_tool, data);
		ts->notify_event = NOTIFY_DEBUG_TOOL;
		ts->notify_data = data;
		siw_ops_notify(ts, ts->notify_event, (void *)&ts->notify_data);
		t_dev_info(dev, "Debug tool state : %s\n",
				(data == DEBUG_TOOL_ENABLE) ?
				"Debug tool Enabled" : "Debug tool Disabled");
	} else {
		t_dev_info(dev, "Debug tool state : Invalid value, %d\n", data);
	}

	return count;
}

static ssize_t _show_debug_option_state(struct device *dev, char *buf)
{
	return 0;
}

static ssize_t _store_debug_option_state(struct device *dev,
				const char *buf, size_t count)
{
	return count;
}

#if defined(__SIW_SUPPORT_ASC)
static ssize_t _show_incoming_call_state(struct device *dev, char *buf)
{
	struct siw_ts *ts = to_touch_core(dev);
	int value = 0;
	int size = 0;

	value = atomic_read(&ts->state.incoming_call);

	size += siw_snprintf(buf, size, "%s : %s(%d)\n",
				__func__,
				siw_incoming_call_str[value], value);

	return (ssize_t)size;
}

static ssize_t _store_incoming_call_state(struct device *dev,
				const char *buf, size_t count)
{
	struct siw_ts *ts = to_touch_core(dev);
	int value = 0;
	int ret = 0;

	if (sscanf(buf, "%d", &value) <= 0) {
		siw_sysfs_err_invalid_param(dev);
		return count;
	}

	if (value >= INCOMING_CALL_IDLE && value <= INCOMING_CALL_OFFHOOK) {
		if (atomic_read(&ts->state.incoming_call) == value)
			return count;

		atomic_set(&ts->state.incoming_call, value);

		ret = siw_touch_blocking_notifier_call(NOTIFY_CALL_STATE,
					&ts->state.incoming_call);

		if (ts->asc.use_asc == ASC_ON)
			siw_touch_qd_toggle_delta_work_jiffies(ts, 0);

		t_dev_info(dev, "Incoming-call : %s(%d)\n",
				siw_incoming_call_str[value], value);
	} else {
		t_dev_info(dev, "Incoming-call : Unknown %d\n", value);
	}

	return count;
}

static ssize_t _show_asc_param(struct device *dev, char *buf)
{
	struct siw_ts *ts = to_touch_core(dev);
	struct asc_info *asc = &(ts->asc);
	int size = 0;

	size += siw_snprintf(buf, size, "use_asc = %d\n",
				asc->use_asc);
	size += siw_snprintf(buf, size, "low_delta_thres = %d\n",
				asc->low_delta_thres);
	size += siw_snprintf(buf, size, "high_delta_thres = %d\n",
				asc->high_delta_thres);

	return (ssize_t)size;
}

static ssize_t _store_asc_param(struct device *dev,
				const char *buf, size_t count)
{
	struct siw_ts *ts = to_touch_core(dev);
	struct asc_info *asc = &(ts->asc);
	unsigned char string[30] = {0, };
	u32 value = 0;

	if (touch_test_quirks(ts, CHIP_QUIRK_NOT_SUPPORT_ASC)) {
		t_dev_err(dev, "asc control not supporeted in %s\n",
				touch_chip_name(ts));
		goto out;
	}

	if (sscanf(buf, "%s %d", string, &value) <= 0) {
		siw_sysfs_err_invalid_param(dev);
		return count;
	}

	if (!strcmp(string, "use_asc")) {
		if (value == ASC_OFF) {
			asc->use_delta_chk = DELTA_CHK_OFF;
			siw_touch_change_sensitivity(ts, NORMAL_SENSITIVITY);
			asc->use_asc = ASC_OFF;
		} else if (value == ASC_ON) {
			asc->use_asc = ASC_ON;
			mutex_lock(&ts->lock);
			siw_ops_asc(ts, ASC_GET_FW_SENSITIVITY, 0);
			mutex_unlock(&ts->lock);
			siw_touch_qd_toggle_delta_work_jiffies(ts, 0);
		} else {
			t_dev_info(dev, "ASC : Invalid value, %d\n", value);
		}

	} else if (!strcmp(string, "low_delta_thres")) {
		asc->low_delta_thres = value;
	} else if (!strcmp(string, "high_delta_thres")) {
		asc->high_delta_thres = value;
	}

out:
	return count;
}

static ssize_t _show_onhand(struct device *dev, char *buf)
{
	struct siw_ts *ts = to_touch_core(dev);
	int value = 0;
	int size = 0;

	value = atomic_read(&ts->state.onhand);

	t_dev_info(dev, "Hand : %s(%d)\n", siw_onhand_str[value], value);

	size += siw_snprintf(buf, size, "%d\n", value);

	return (ssize_t)size;
}

static ssize_t _store_onhand(struct device *dev,
				const char *buf, size_t count)
{
	struct siw_ts *ts = to_touch_core(dev);
	int value = 0;

	if (sscanf(buf, "%d", &value) <= 0) {
		siw_sysfs_err_invalid_param(dev);
		return count;
	}

	if (value >= IN_HAND_ATTN && value <= IN_HAND_NO_ATTN) {
		atomic_set(&ts->state.onhand, value);

		if (ts->asc.use_asc == ASC_ON)
			siw_touch_qd_toggle_delta_work_jiffies(ts, 0);

		t_dev_info(dev, "Hand : %s(%d)\n", siw_onhand_str[value], value);
	} else {
		t_dev_info(dev, "Hand : Invalid value, %d\n", value);
	}

	return count;
}
#endif	/* __SIW_SUPPORT_ASC */

static ssize_t _show_module_info(struct device *dev, char *buf)
{
	struct siw_ts *ts = to_touch_core(dev);
	int size = 0;

	size += siw_snprintf(buf, size, "%s/%s/%s, %s\n",
			dev_name(dev->parent->parent),
			dev_name(dev->parent),
			dev_name(dev),
			dev_name(&ts->input->dev));

	return (ssize_t)size;
}


#define SIW_TOUCH_ATTR(_name, _show, _store)	\
		TOUCH_ATTR(_name, _show, _store)

#define _SIW_TOUCH_ATTR_T(_name)	\
		touch_attr_##_name


static SIW_TOUCH_ATTR(platform_data,
						_show_plat_data, NULL);
static SIW_TOUCH_ATTR(fw_upgrade,
						_show_upgrade,
						_store_upgrade);
static SIW_TOUCH_ATTR(lpwg_data,
						_show_lpwg_data,
						_store_lpwg_data);
static SIW_TOUCH_ATTR(lpwg_notify, NULL,
						_store_lpwg_notify);
static SIW_TOUCH_ATTR(keyguard,
						_show_lockscreen_state,
						_store_lockscreen_state);
static SIW_TOUCH_ATTR(ime_status,
						_show_ime_state,
						_store_ime_state);
static SIW_TOUCH_ATTR(quick_cover_status,
						_show_quick_cover_state,
						_store_quick_cover_state);
static SIW_TOUCH_ATTR(firmware,
						_show_version_info, NULL);
static SIW_TOUCH_ATTR(version,
						_show_version_info, NULL);
static SIW_TOUCH_ATTR(testmode_ver,
						_show_atcmd_version_info, NULL);
static SIW_TOUCH_ATTR(mfts,
						_show_mfts_state,
						_store_mfts_state);
static SIW_TOUCH_ATTR(mfts_lpwg,
						_show_mfts_lpwg,
						_store_mfts_lpwg);
static SIW_TOUCH_ATTR(sp_link_touch_off,
						_show_sp_link_touch_off,
						_store_sp_link_touch_off);
static SIW_TOUCH_ATTR(debug_tool,
						_show_debug_tool_state,
						_store_debug_tool_state);
static SIW_TOUCH_ATTR(debug_option,
						_show_debug_option_state,
						_store_debug_option_state);
#if defined(__SIW_SUPPORT_ASC)
static SIW_TOUCH_ATTR(incoming_call,
						_show_incoming_call_state,
						_store_incoming_call_state);
static SIW_TOUCH_ATTR(asc,
						_show_asc_param,
						_store_asc_param);
static SIW_TOUCH_ATTR(onhand,
						_show_onhand,
						_store_onhand);
#endif	/* __SIW_SUPPORT_ASC */
static SIW_TOUCH_ATTR(module_info,
						_show_module_info, NULL);

static struct attribute *siw_touch_attribute_list[] = {
	&_SIW_TOUCH_ATTR_T(platform_data).attr,
	&_SIW_TOUCH_ATTR_T(fw_upgrade).attr,
	&_SIW_TOUCH_ATTR_T(lpwg_data).attr,
	&_SIW_TOUCH_ATTR_T(lpwg_notify).attr,
	&_SIW_TOUCH_ATTR_T(keyguard.attr),
	&_SIW_TOUCH_ATTR_T(ime_status).attr,
	&_SIW_TOUCH_ATTR_T(quick_cover_status).attr,
	&_SIW_TOUCH_ATTR_T(firmware).attr,
	&_SIW_TOUCH_ATTR_T(version).attr,
	&_SIW_TOUCH_ATTR_T(testmode_ver).attr,
	&_SIW_TOUCH_ATTR_T(mfts).attr,
	&_SIW_TOUCH_ATTR_T(mfts_lpwg).attr,
	&_SIW_TOUCH_ATTR_T(sp_link_touch_off).attr,
	&_SIW_TOUCH_ATTR_T(debug_tool).attr,
	&_SIW_TOUCH_ATTR_T(debug_option).attr,
#if defined(__SIW_SUPPORT_ASC)
	&_SIW_TOUCH_ATTR_T(incoming_call).attr,
	&_SIW_TOUCH_ATTR_T(asc).attr,
	&_SIW_TOUCH_ATTR_T(onhand).attr,
#endif	/* __SIW_SUPPORT_ASC */
	&_SIW_TOUCH_ATTR_T(module_info).attr,
	NULL,
};

static const struct attribute_group siw_touch_attribute_group = {
	.attrs = siw_touch_attribute_list,
};

static ssize_t siw_touch_attr_show(struct kobject *kobj,
		struct attribute *attr, char *buf)
{
	struct siw_ts *ts =
		container_of(kobj, struct siw_ts, kobj);
	struct siw_touch_attribute *priv =
		container_of(attr, struct siw_touch_attribute, attr);
	ssize_t ret = 0;

	if (priv->show)
		ret = priv->show(ts->dev, buf);

	return ret;
}

static ssize_t siw_touch_attr_store(struct kobject *kobj,
		struct attribute *attr, const char *buf, size_t count)
{
	struct siw_ts *ts =
		container_of(kobj, struct siw_ts, kobj);
	struct siw_touch_attribute *priv =
		container_of(attr, struct siw_touch_attribute, attr);
	ssize_t ret = 0;

	if (priv->store)
		ret = priv->store(ts->dev, buf, count);

	return ret;
}

/*
 * To reegister SiW's unique sysfs functions
 */
static const struct sysfs_ops siw_touch_sysfs_ops = {
	.show	= siw_touch_attr_show,
	.store	= siw_touch_attr_store,
};

static struct kobj_type siw_touch_kobj_type = {
	.sysfs_ops = &siw_touch_sysfs_ops,
};

int siw_touch_init_sysfs(struct siw_ts *ts)
{
	struct device *dev = ts->dev;
	struct device *idev = &ts->input->dev;
	struct kobject *kobj = &ts->kobj;
	int ret = 0;

	ret = kobject_init_and_add(kobj, &siw_touch_kobj_type,
			idev->kobj.parent, "%s", touch_idrv_name(ts));
	if (ret < 0) {
		t_dev_err(dev, "failed to create sysfs entry\n");
		goto out;
	}

	ret = sysfs_create_group(kobj, &siw_touch_attribute_group);
	if (ret < 0) {
		t_dev_err(dev, "failed to create sysfs\n");
		goto out_sys;
	}

	ret = siw_ops_sysfs(ts, DRIVER_INIT);
	if (ret < 0) {
		t_dev_err(dev, "failed to register sysfs\n");
		goto out_sysfs;
	}

	return 0;

out_sysfs:
	sysfs_remove_group(kobj, &siw_touch_attribute_group);

out_sys:
	kobject_del(kobj);

out:
	return ret;
}

void siw_touch_free_sysfs(struct siw_ts *ts)
{
	struct device *dev = ts->dev;
	struct device *idev = &ts->input->dev;

	if (ts->kobj.parent != idev->kobj.parent) {
		t_dev_warn(dev, "Invalid kobject\n");
		return;
	}

	siw_ops_sysfs(ts, DRIVER_FREE);

	sysfs_remove_group(&ts->kobj, &siw_touch_attribute_group);

	kobject_del(&ts->kobj);
}


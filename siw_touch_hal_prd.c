/*
 * siw_touch_hal_prd.c - SiW touch hal driver for PRD
 *
 * Copyright (C) 2016 Silicon Works - http://www.siliconworks.co.kr
 * Author: Sungyeal Park <parksy5@siliconworks.co.kr>
 *         Hyunho Kim <kimhh@siliconworks.co.kr>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 */

#include <linux/version.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/delay.h>
#include <linux/linkage.h>
#include <linux/syscalls.h>
#include <linux/namei.h>
#include <linux/file.h>
#include <linux/workqueue.h>
#include <linux/interrupt.h>
#include <linux/firmware.h>
#include <linux/time.h>
#include <linux/fs.h>

#include "siw_touch.h"
#include "siw_touch_hal.h"
#include "siw_touch_bus.h"
#include "siw_touch_event.h"
#include "siw_touch_gpio.h"
#include "siw_touch_irq.h"
#include "siw_touch_sys.h"

/*
 * [PRD]
 * Touch IC Production Test
 * This supports a method to verify the quality of Touch IC using input/output file test.
 *
 * 1. Input setup file(spec file)
 * 2. Self testing
 * 3. Output result file(self test result)
 * 4. Verify the result
 *
 */


#if defined(__SIW_SUPPORT_PRD)	//See siw_touch_cfg.h

struct siw_hal_prd_test_off {
	u16 offset0;
	u16 offset1;
} __packed;

struct siw_hal_prd_test_off_info {
	struct siw_hal_prd_test_off m1_m2_raw;
	struct siw_hal_prd_test_off frame0_1;
	struct siw_hal_prd_test_off frame2_short;
	struct siw_hal_prd_test_off os_result;
} __packed;


#define TSP_FRAME6_OFFSET				(0x3E18)
#define TSP_FRAME5_OFFSET				(0x3ADB)
#define TSP_FRAME4_OFFSET				(0x3798)
#define TSP_FRAME3_OFFSET				(0x3458)
#define TSP_FRAME2_OFFSET				(0x3118)
#define TSP_FRAME1_OFFSET				(0x2DD8)
#define TSP_FRAME0_OFFSET				(0x2A98)
#define TSP_REPORT						(0x2A30)

#define DELTA_DATA_OFFSET				(0xD95)
#define LABLE_DATA_OFFSET				(0xE83)
#define AIT_RAW_DATA_OFFSET				(0xA8C)
#define AIT_BASE_DATA_EVNE_OFFSET		(0xC0F)
#define AIT_BASE_DATA_ODD_OFFSET		(0xCD2)

#define FILTERED_DELTA_DATA_OFFSET		(0x7FD)

/*
 * define M1_M2_RAWDATA_TEST_CNT
 * if you have odd/even frame setting num 2
 * if you have 1 frame setting num 1
 */
enum {
	M1_RAWDATA_TEST_CNT		= 1,
	M2_RAWDATA_TEST_CNT 	= 2,
	MAX_TEST_CNT			= 2,
};

enum {
	LINE_FILTER_OPTION	= (0x40000),
};

/* tune code */
enum {
	TC_TUNE_CODE_SIZE				= ((1<<8)+4),	//260
	TC_TOTAL_CH_SIZE				= (1<<5),		//32
	TSP_TUNE_CODE_L_GOFT_OFFSET		= 0,
	TSP_TUNE_CODE_L_M1_OFT_OFFSET	= (1<<1),
	TSP_TUNE_CODE_L_G1_OFT_OFFSET	= (TSP_TUNE_CODE_L_M1_OFT_OFFSET + TC_TOTAL_CH_SIZE),
	TSP_TUNE_CODE_L_G2_OFT_OFFSET	= (TSP_TUNE_CODE_L_G1_OFT_OFFSET + TC_TOTAL_CH_SIZE),
	TSP_TUNE_CODE_L_G3_OFT_OFFSET	= (TSP_TUNE_CODE_L_G2_OFT_OFFSET + TC_TOTAL_CH_SIZE),
	TSP_TUNE_CODE_R_GOFT_OFFSET		= (TSP_TUNE_CODE_L_G3_OFT_OFFSET + TC_TOTAL_CH_SIZE),
	TSP_TUNE_CODE_R_M1_OFT_OFFSET	= (TSP_TUNE_CODE_R_GOFT_OFFSET + 2),
	TSP_TUNE_CODE_R_G1_OFT_OFFSET	= (TSP_TUNE_CODE_R_M1_OFT_OFFSET + TC_TOTAL_CH_SIZE),
	TSP_TUNE_CODE_R_G2_OFT_OFFSET	= (TSP_TUNE_CODE_R_G1_OFT_OFFSET + TC_TOTAL_CH_SIZE),
	TSP_TUNE_CODE_R_G3_OFT_OFFSET	= (TSP_TUNE_CODE_R_G2_OFT_OFFSET + TC_TOTAL_CH_SIZE),
};

#if defined(CONFIG_TOUCHSCREEN_SIW_SW1828)
#define __PRD_TYPE_S1
#elif defined(CONFIG_TOUCHSCREEN_SIW_LG4946)
#define __PRD_TYPE_L2
#elif defined(CONFIG_TOUCHSCREEN_SIW_LG4895)
#define __PRD_TYPE_L2
#elif defined(CONFIG_TOUCHSCREEN_SIW_LG4894)
#define __PRD_TYPE_L1
#endif

#if defined(__PRD_TYPE_S1)
#define __PRD_ROW_SIZE	32
#define __PRD_COL_SIZE	20
#elif defined(__PRD_TYPE_L2)
#define __PRD_ROW_SIZE	32
#define __PRD_COL_SIZE	18
#elif defined(__PRD_TYPE_L1)
#define __PRD_ROW_SIZE	26
#define __PRD_COL_SIZE	15
#else
	#error Wrong PRD row and col size! check again!
#define __PRD_ROW_SIZE	-1
#define __PRD_COL_SIZE	1
#endif

enum {
	PRD_DATA_NAME_SZ	= 128,
	/* */
	PRD_LINE_NUM		= 50000,
	PRD_PATH_SIZE		= (1<<6),		//64
	PRD_BURST_SIZE		= (1<<9),		//512
	PRD_RAWDATA_SIZE	= (1<<1),
	PRD_ROW_SIZE		= __PRD_ROW_SIZE,
	PRD_COL_SIZE		= __PRD_COL_SIZE,
	PRD_M1_COL_SIZE		= (1<<1),
	/* */
	PRD_LOG_BUF_SIZE	= (1<<10),		//1K
	PRD_BUF_SIZE		= (PAGE_SIZE<<1),
	/* */
	MAX_LOG_FILE_COUNT	= (4),
	MAX_LOG_FILE_SIZE	= (10 * (1<<20)),	/* 10 M byte */
	/* */
	RAWDATA_OFFSET		= (0xE00),
};

struct siw_hal_prd_data {
	struct device *dev;
	char name[PRD_DATA_NAME_SZ];
	/* */
	char log_buf[PRD_LOG_BUF_SIZE];
	char line[PRD_LINE_NUM];
	char buf_write[PRD_BUF_SIZE];
	int16_t	m1_buf_odd_rawdata[PRD_ROW_SIZE*PRD_M1_COL_SIZE];
	int16_t	m1_buf_even_rawdata[PRD_ROW_SIZE*PRD_M1_COL_SIZE];
	int16_t	m2_buf_odd_rawdata[PRD_ROW_SIZE*PRD_COL_SIZE];
	int16_t	m2_buf_even_rawdata[PRD_ROW_SIZE*PRD_COL_SIZE];
	int16_t	buf_delta[(PRD_ROW_SIZE+2)*(PRD_COL_SIZE+2)];
	u8	buf_label_tmp[(PRD_ROW_SIZE+2)*(PRD_COL_SIZE+2)];
	u8	buf_label[(PRD_ROW_SIZE)*(PRD_COL_SIZE)];
//	int16_t	buf_m2_rawdata[PRD_ROW_SIZE*PRD_COL_SIZE];
//	int16_t buf_m1_rawdata[PRD_ROW_SIZE*PRD_M1_COL_SIZE];
	int16_t buf_m1_rawdata_tmp[PRD_ROW_SIZE*PRD_M1_COL_SIZE];
	int16_t image_lower[PRD_ROW_SIZE][PRD_COL_SIZE];
	int16_t image_upper[PRD_ROW_SIZE][PRD_COL_SIZE];
};

#define siw_prd_buf_snprintf(_buf, _size, _fmt, _args...) \
		__siw_snprintf(_buf, PRD_BUF_SIZE, _size, _fmt, ##_args)

#define siw_prd_log_buf_snprintf(_buf, _size, _fmt, _args...) \
		__siw_snprintf(_buf, PRD_LOG_BUF_SIZE, _size, _fmt, ##_args)


enum {
	CMD_TEST_EXIT = 0,
	CMD_TEST_ENTER,
};

enum {
	CMD_RAWDATA_PRD = 1,
	CMD_RAWDATA_TCM,
	CMD_RAWDATA_AIT,
	CMD_AIT_BASEDATA_EVEN,
	CMD_AIT_BASEDATA_ODD,
	CMD_FILTERED_DELTADATA,
	CMD_DELTADATA,
	CMD_LABELDATA,
	CMD_BLU_JITTER,
	CMD_MAX,
};

#define __PRD_GET_DATA_CMD_SET(_idx)	[_idx] = #_idx

static const char *prd_get_data_cmd_name[] = {
	[0] = "(none)",
	__PRD_GET_DATA_CMD_SET(CMD_RAWDATA_PRD),
	__PRD_GET_DATA_CMD_SET(CMD_RAWDATA_TCM),
	__PRD_GET_DATA_CMD_SET(CMD_RAWDATA_AIT),
	__PRD_GET_DATA_CMD_SET(CMD_AIT_BASEDATA_EVEN),
	__PRD_GET_DATA_CMD_SET(CMD_AIT_BASEDATA_ODD),
	__PRD_GET_DATA_CMD_SET(CMD_FILTERED_DELTADATA),
	__PRD_GET_DATA_CMD_SET(CMD_DELTADATA),
	__PRD_GET_DATA_CMD_SET(CMD_LABELDATA),
	__PRD_GET_DATA_CMD_SET(CMD_BLU_JITTER),
};

enum {
	TIME_INFO_SKIP = 0,
	TIME_INFO_WRITE,
};

enum {
	NO_TEST = 0,
	OPEN_SHORT_ALL_TEST,
	OPEN_NODE_TEST,
	SHORT_NODE_TEST,
	U3_M2_RAWDATA_TEST = 5,
	U3_M1_RAWDATA_TEST,
	U0_M2_RAWDATA_TEST,
	U0_M1_RAWDATA_TEST,
	U3_BLU_JITTER_TEST = 12,
};
enum{
	M1_ODD_DATA = 0,
	M1_EVEN_DATA,
	M2_ODD_DATA ,
	M2_EVEN_DATA,
	LABEL_DATA,
	};

/* PRD RAWDATA test RESULT Save On/Off CMD */
enum {
	RESULT_OFF = 0,
	RESULT_ON,
};

/* TCM Memory Access Select */
//tc_mem_sel(0x0457) RAW : 0 , BASE1 : 1 , BASE2 : 2 , BASE3: 3
enum {
	TCM_MEM_RAW = 0,
	TCM_MEM_BASE1,
	TCM_MEM_BASE2,
	TCM_MEM_BASE3,
};

/* AIT Algorithm Engine HandShake CMD */
enum {
	IT_IMAGE_NONE = 0,
	IT_IMAGE_RAW,
	IT_IMAGE_BASELINE,
	IT_IMAGE_BASELINE_ODD,
	IT_IMAGE_DELTA,
	IT_IMAGE_LABEL,
	IT_IMAGE_FILTERED_DELTA,
	IT_IMAGE_RESERVED,
	IT_IMAGE_DEBUG,
	IT_WAIT = 0xFF
};

/* AIT Algorithm Engine HandShake Status */
enum {
	RS_READY    = 0xA0,
	RS_NONE     = 0x05,
	RS_LOG      = 0x77,
	RS_IMAGE	= 0xAA
};

enum {
	PRD_TIME_STR_SZ = 64,
	PRD_TMP_FILE_NAME_SZ = 256,
};

enum {
	PRD_TUNE_DISPLAY_TYPE_1 = 1,
	PRD_TUNE_DISPLAY_TYPE_2,
};

static char __prd_in_file[PRD_TMP_FILE_NAME_SZ] =	\
			"/sdcard/siw/prd_in.txt";
static char __prd_in_file_m[PRD_TMP_FILE_NAME_SZ] =	\
			"/sdcard/siw/prd_in_mfts.txt";
static char __prd_out_file[PRD_TMP_FILE_NAME_SZ] =	\
			"/sdcard/siw/prd_out.txt";
static char __prd_out_file_mo_aat[PRD_TMP_FILE_NAME_SZ] =	\
			"/sdcard/siw/prd_out_mfts_aat.txt";
static char __prd_out_file_mo_mfo[PRD_TMP_FILE_NAME_SZ] =	\
			"/sdcard/siw/prd_out_mfts_mfo.txt";
static char __prd_out_file_mo_mfl[PRD_TMP_FILE_NAME_SZ] =	\
			"/sdcard/siw/prd_out_mfts_mfl.txt";
static char __prd_out_file_mo_mcv[PRD_TMP_FILE_NAME_SZ] =	\
			"/sdcard/siw/prd_out_mfts_mcv.txt";

#if defined(MODULE)
/* use eg. prd_if=arc1 to change name */
module_param_string(prd_if, __prd_in_file, sizeof(__prd_in_file), 0);

/* use eg. prd_if_m=arc1 to change name */
module_param_string(prd_if_m, __prd_in_file_m, sizeof(__prd_in_file_m), 0);

/* use eg. prd_of=arc1 to change name */
module_param_string(prd_of, __prd_out_file, sizeof(__prd_out_file), 0);

/* use eg. prd_of_mo_aat=arc1 to change name */
module_param_string(prd_of_mo_aat, __prd_out_file_mo_aat, sizeof(__prd_out_file_mo_aat), 0);

/* use eg. prd_of_mo_mfo=arc1 to change name */
module_param_string(prd_of_mo_mfo, __prd_out_file_mo_mfo, sizeof(__prd_out_file_mo_mfo), 0);

/* use eg. prd_of_mo_mfl=arc1 to change name */
module_param_string(prd_of_mo_mfl, __prd_out_file_mo_mfl, sizeof(__prd_out_file_mo_mfl), 0);

/* use eg. prd_of_mo_mcv=arc1 to change name */
module_param_string(prd_of_mo_mcv, __prd_out_file_mo_mcv, sizeof(__prd_out_file_mo_mcv), 0);
#endif


static u32 t_prd_dbg_mask = 0;

/* usage
 * (1) echo <value> > /sys/module/{Siw Touch Module Name}/parameters/s_prd_dbg_mask
 * (2) insmod {Siw Touch Module Name}.ko s_prd_dbg_mask=<value>
 */
module_param_named(s_prd_dbg_mask, t_prd_dbg_mask, int, S_IRUGO|S_IWUSR|S_IWGRP);


#define t_prd_info(_prd, fmt, args...)	\
		__t_dev_info(_prd->dev, "%s : " fmt, _prd->name, ##args)

#define t_prd_err(_prd, fmt, args...)	\
		__t_dev_err(_prd->dev, "%s : " fmt, _prd->name, ##args)

#define t_prd_warn(_prd, fmt, args...)	\
		__t_dev_warn(_prd->dev, "%s : " fmt, _prd->name, ##args)

#define t_prd_dbg(condition, _prd, fmt, args...)	\
		do {	\
			if (unlikely(t_prd_dbg_mask & (condition)))	\
				t_prd_info(_prd, fmt, ##args);	\
		} while (0)

#define t_prd_dbg_base(_prd, fmt, args...)	\
		t_prd_dbg(DBG_BASE, _prd, fmt, ##args)

#define t_prd_dbg_trace(_prd, fmt, args...)	\
		t_prd_dbg(DBG_TRACE, _prd, fmt, ##args)


#define siw_prd_sysfs_err_invalid_param(_prd)	\
		t_prd_err(_prd, "Invalid param\n");

static int prd_chip_reset(struct device *dev)
{
	struct siw_touch_chip *chip = to_touch_chip(dev);
	struct siw_ts *ts = chip->ts;
	int ret;

//	mutex_lock(&ts->lock);
	ret = siw_ops_reset(ts, HW_RESET_SYNC);
//	mutex_unlock(&ts->lock);

	return ret;
}

static int prd_chip_info(struct device *dev)
{
	struct siw_touch_chip *chip = to_touch_chip(dev);
	struct siw_ts *ts = chip->ts;

	return siw_ops_ic_info(ts);
}

static int prd_chip_driving(struct device *dev, int mode)
{
	struct siw_touch_chip *chip = to_touch_chip(dev);
	struct siw_ts *ts = chip->ts;

	return siw_ops_tc_driving(ts, mode);
}

static ssize_t prd_kernel_write(struct file *file,
				const char *buf, size_t count,
			    loff_t *pos)
{
	mm_segment_t old_fs;
	ssize_t res;

	old_fs = get_fs();
	set_fs(get_ds());
	/* The cast to a user pointer is valid due to the set_fs() */
	res = vfs_write(file, (__force const char __user *)buf, count, pos);
	set_fs(old_fs);

	return res;
}

static int __prd_boot_mode_is_err(struct device *dev, int boot_mode)
{
	struct siw_touch_chip *chip = to_touch_chip(dev);
	struct siw_ts *ts = chip->ts;
	struct siw_hal_prd_data *prd = (struct siw_hal_prd_data *)ts->prd;

	if ((boot_mode > MINIOS_MFTS_CURVED) || (boot_mode < NORMAL_BOOT)) {
		t_prd_err(prd, "boot mode(%d) not support mode\n", boot_mode);
		return -EINVAL;
	}
	return 0;
}

static int __used prd_chk_file_mode(struct siw_hal_prd_data *prd,
						struct file *filp,
						char *fname)
{
	struct dentry *fentry = filp->f_path.dentry;

	if (filp->f_flags & (O_RDWR|O_WRONLY)) {
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,19,0))
		if (!(filp->f_mode & FMODE_CAN_WRITE)) {
			t_prd_err(prd, "file not writeable : %s\n", fname);
			return -EINVAL;
		}
#endif
	}

	if (filp->f_flags & (O_RDWR|O_RDONLY)) {
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,19,0))
		if (!(filp->f_mode & FMODE_CAN_READ)) {
			t_prd_err(prd, "file not readable : %s\n", fname);
			return -EINVAL;
		}
#endif

		if (S_ISDIR(fentry->d_inode->i_mode)) {
			t_prd_err(prd, "%s is folder\n", fname);
			return -EOPNOTSUPP;
		}

		if (S_ISLNK(fentry->d_inode->i_mode)) {
			t_prd_err(prd, "%s is link\n", fname);
			return -EOPNOTSUPP;
		}
	}
	return 0;
}

static struct file __used *prd_vfs_file_open(struct siw_hal_prd_data *prd,
				char *fname, int flags, umode_t mode)
{
	struct file *filp;

	filp = filp_open((const char *)fname, flags, mode);
	if (IS_ERR(filp)) {
		int ret = (int)PTR_ERR(filp);

		if ((flags == O_RDONLY) && (ret == -ENOENT)) {
			t_prd_dbg_trace(prd, "file[%s, 0x%X, 0x%X] open failed, %d\n",
					fname, flags, mode, ret);
		} else {
			t_prd_err(prd, "file[%s, 0x%X, 0x%X] open failed, %d\n",
					fname, flags, mode, ret);
		}
		return NULL;
	}

	return filp;
}

static int __used prd_vfs_file_chk(struct siw_hal_prd_data *prd,
				char *fname,
				int flags,
				umode_t mode,
				loff_t *size)
{
	struct file *filp;
	int ret = 0;

	filp = prd_vfs_file_open(prd, fname, flags, mode);
	if (filp == NULL) {
		ret = -ENOENT;
		goto out;
	}

	if (size) {
		*size = vfs_llseek(filp, 0, SEEK_END);
	}

	ret = prd_chk_file_mode(prd, filp, fname);
	if (ret < 0) {
		goto out_close;
	}

out_close:
	filp_close(filp, 0);

out:
	return ret;
}

static void __used prd_vfs_file_close(struct file *filp, fl_owner_t id)
{
	filp_close(filp, id);
}


/*
 * [Safety and Compatibility]
 * Using call_usermodehelper is recommended
 * because direct access to file system in kernel space is not good way.
 */
#define __PRD_USE_USERMODE_HELPER

static inline char *prd_find_name_pos(char *pathname)
{
	char *pos, *tpos;

	pos = strrchr(pathname, '/');
	tpos = (pos)? pos + 1 : pathname;

	return tpos;
}

#if defined(__PRD_USE_USERMODE_HELPER)
#define t_prd_err_uh(_prd, _ret, fmt, args...)	\
		t_prd_err(_prd, "user mode helper" fmt " failed, %d\n",	\
				##args, _ret)

#define t_prd_dbg_uh(_prd, fmt, args...)	\
		t_prd_dbg_trace(_prd, "user mode helper" fmt " done\n", ##args)

static int prd_vfs_uh_remove(struct siw_hal_prd_data *prd,
				char *pathname)
{
	char *argv[4] = { "/bin/rm", NULL, NULL, NULL };
	char *envp[1] = { NULL };
	int ret = 0;

	argv[1] = (char *)pathname;

//	envp[0] = "HOME=/";
//	envp[1] = "PATH=/sbin:/vendor/bin:/system/sbin:/system/bin:/system/xbin";

	ret = call_usermodehelper(argv[0], argv, envp, UMH_WAIT_PROC);
	if (ret < 0) {
		t_prd_err_uh(prd, ret, "[%s %s]", argv[0], argv[1]);
	}
	t_prd_dbg_uh(prd, "[%s %s]", argv[0], argv[1]);

	return ret;
}

static int prd_vfs_uh_rename(struct siw_hal_prd_data *prd,
				char *pathname,
				char *pathname_new)
{
	char *argv[4] = { "/bin/mv", NULL, NULL, NULL };
	char *envp[1] = { NULL };
	int ret = 0;

	argv[1] = (char *)pathname;
	argv[2] = (char *)pathname_new;

//	envp[0] = "HOME=/";
//	envp[1] = "PATH=/sbin:/vendor/bin:/system/sbin:/system/bin:/system/xbin";

	ret = call_usermodehelper(argv[0], argv, envp, UMH_WAIT_PROC);
	if (ret < 0) {
		t_prd_err_uh(prd, ret, "[%s %s %s]", argv[0], argv[1], argv[2]);
	}
	t_prd_dbg_uh(prd, "[%s %s %s]", argv[0], argv[1], argv[2]);

	return ret;
}

static int prd_vfs_remove(struct siw_hal_prd_data *prd,
				char *fname)
{
	struct file *filp = NULL;
	char *tmp = NULL;
	char *pathname = NULL;
	int ret = 0;

	tmp = (char *)kzalloc(PAGE_SIZE, GFP_KERNEL);
	if (!tmp) {
		return -ENOMEM;
	}

	/*
	 * Step 1. Get fentry data
	 */
	filp = prd_vfs_file_open(prd, fname, O_RDONLY, 0);
	if (filp == NULL) {
		ret = -ENOENT;
		goto out;
	}

	pathname = d_path(&filp->f_path, tmp, PAGE_SIZE);
	if (IS_ERR(pathname)) {
		ret = -ENOMEM;
		goto out;
	}

	prd_vfs_file_close(filp, 0);

	if (ret < 0) {
		goto out;
	}

	/*
	 * Step 2. Remove target file
	 */
	ret = prd_vfs_uh_remove(prd, pathname);

out:
	kfree(tmp);
	return ret;
}

static int prd_vfs_rename(struct siw_hal_prd_data *prd,
					char *fname,
					char *fname_new)
{
	struct file *filp = NULL;
	char *tmp = NULL;
	char *tmp_new = NULL;
	char *pathname = NULL;
	char *pathname_new = NULL;
	char *wpos, *rpos;
	int ret = 0;

	tmp = (char *)kzalloc(PAGE_SIZE, GFP_KERNEL);
	if (!tmp) {
		return -ENOMEM;
	}

	tmp_new = (char *)kzalloc(PAGE_SIZE, GFP_KERNEL);
	if (!tmp_new) {
		kfree(tmp);
		return -ENOMEM;
	}

	/*
	 * Step 1. Get fentry data
	 */
	filp = prd_vfs_file_open(prd, fname, O_RDONLY, 0);
	if (filp == NULL) {
		ret = -ENOENT;
		goto out;
	}

	pathname = d_path(&filp->f_path, tmp, PAGE_SIZE);
	if (IS_ERR(pathname)) {
		ret = -ENOMEM;
		goto out;
	}

	prd_vfs_file_close(filp, 0);

	if (ret < 0) {
		goto out;
	}

	/*
	 * Step 2. pathname_new
	 */
	pathname_new = tmp_new;
	strncpy(pathname_new, pathname, strlen(pathname));

	wpos = prd_find_name_pos(pathname_new);
	rpos = prd_find_name_pos(fname_new);

	memcpy(wpos, rpos, strlen(rpos));

	/*
	 * Step 3. Change name
	 */
	ret = prd_vfs_uh_rename(prd, pathname, pathname_new);

out:
	kfree(tmp_new);
	kfree(tmp);
	return ret;
}
#else	/* __PRD_USE_USERMODE_HELPER */
static int prd_vfs_do_remove(struct siw_hal_prd_data *prd,
				struct dentry *fentry,
				char *fname)
{
	struct dentry *dir = NULL;
	int ret = 0;

	dir = dget_parent(fentry);

	mutex_lock(&dir->d_inode->i_mutex);

	spin_lock(&fentry->d_lock);

	t_prd_dbg_trace(prd, "unhashed 0x%X, d_inode %p\n",
			d_unhashed(fentry), fentry->d_inode);

	if (d_unhashed(fentry) && fentry->d_inode) {
		spin_unlock(&fentry->d_lock);
		goto out;
	}

	dget_dlock(fentry);
	__d_drop(fentry);
	spin_unlock(&fentry->d_lock);

	vfs_unlink(dir->d_inode, fentry);

	t_prd_dbg_trace(prd, "vfs_unlink done\n");

out:
	mutex_unlock(&dir->d_inode->i_mutex);
	return ret;
}

static int prd_vfs_do_rename(struct siw_hal_prd_data *prd,
				struct dentry *fentry,
				char *fname_new)
{
	struct dentry *dir = NULL;
	struct inode *d_inode = NULL;
	struct dentry *fentry_new = NULL;
	char *base_name = NULL;
	int ret = 0;

	dir = dget_parent(fentry);
	d_inode = dir->d_inode;

	lock_rename(dir, dir);

	base_name = prd_find_name_pos(fname_new);
	t_prd_dbg_trace(prd, "base name : %s\n", base_name);

	fentry_new = lookup_one_len(base_name, dir, strlen(base_name));
	if (IS_ERR(fentry_new)) {
		ret = PTR_ERR(fentry_new);
		t_prd_err(prd, "lookup_one_len for fentry_new failed, %d\n", ret);
		goto out;
	}

	ret = vfs_rename(d_inode, fentry, d_inode, fentry_new);

out:
	unlock_rename(dir, dir);

	return ret;
}

static int prd_vfs_remove(struct siw_hal_prd_data *prd,
				char *fname)
{
	struct file *filp = NULL;
	struct dentry *fentry = NULL;
	int ret = 0;

	/*
	 * Step 1. Get fentry data
	 */
	filp = prd_vfs_file_open(prd, fname, O_RDONLY, 0);
	if (filp == NULL) {
		return -ENOENT;
	}

	fentry = filp->f_path.dentry;

	prd_vfs_file_close(filp, 0);

	/*
	 * Step 2. Remove target file
	 */
	ret = prd_vfs_do_remove(prd, fentry, fname);

	return ret;
}

static int prd_vfs_rename(struct siw_hal_prd_data *prd,
					char *fname,
					char *fname_new)
{
	struct file *filp = NULL;
	struct dentry *fentry = NULL;
	int ret = 0;

	/*
	 * Step 1. Get fentry data
	 */
	filp = prd_vfs_file_open(prd, fname, O_RDONLY, 0);
	if (filp == NULL) {
		return -ENOENT;
	}

	fentry = filp->f_path.dentry;

	prd_vfs_file_close(filp, 0);

	/*
	 * Step 2. Change name
	 */
	ret = prd_vfs_do_rename(prd, fentry, fname_new);

	return ret;
}

#endif	/* __PRD_USE_USERMODE_HELPER */


#define t_prd_dbg_file_modify(_prd, fmt, args...)	\
		t_prd_dbg_trace(_prd, "try to " fmt "\n", ##args)

#define t_prd_dbg_file_remove(_prd, fmt, args...)	\
		t_prd_dbg_file_modify(_prd, "remove " fmt, ##args)

#define t_prd_dbg_file_rename(_prd, fmt, args...)	\
		t_prd_dbg_file_modify(_prd, "rename " fmt, ##args)

static int __used prd_vfs_file_remove(struct siw_hal_prd_data *prd,
				char *fname)
{
	int ret = 0;

	t_prd_dbg_file_remove(prd, "%s", fname);

	ret = prd_vfs_remove(prd, fname);
	if (ret < 0) {
		if (ret != -ENOENT) {
			t_prd_err(prd, "failed to remove file : %s, %d\n", fname, ret);
		}
		goto out;
	}
	t_prd_info(prd, "file removed : %s\n", fname);

out:
	return 0;
}

static int __used prd_vfs_file_rename(struct siw_hal_prd_data *prd,
				char *fname, char *fname_new)
{
	int ret = 0;

	t_prd_dbg_file_rename(prd, "%s -> %s", fname, fname_new);

	ret = prd_vfs_rename(prd, fname, fname_new);
	if (ret < 0) {
		if (ret != -ENOENT) {
			t_prd_err(prd, "failed to rename file : %s -> %s, %d\n",
					fname, fname_new, ret);
		}
		goto out;
	}
	t_prd_info(prd, "file renamed : %s -> %s\n",
			fname, fname_new);

out:
	return 0;
}

static int prd_log_file_size_check(struct siw_hal_prd_data *prd)
{
	char *prd_out_fname[] = {
		[NORMAL_BOOT]			= __prd_out_file,
		[MINIOS_AAT]			= __prd_out_file_mo_aat,
		[MINIOS_MFTS_FOLDER]	= __prd_out_file_mo_mfo,
		[MINIOS_MFTS_FLAT]		= __prd_out_file_mo_mfl,
		[MINIOS_MFTS_CURVED]	= __prd_out_file_mo_mcv,
	};
	struct device *dev = prd->dev;
	char *fname = NULL;
	loff_t file_size = 0;
	int i = 0;
	char buf1[PRD_TMP_FILE_NAME_SZ] = {0, };
	char buf2[PRD_TMP_FILE_NAME_SZ] = {0, };
	int boot_mode = 0;
	int ret = 0;

	boot_mode = siw_touch_boot_mode_check(dev);

	if (__prd_boot_mode_is_err(dev, boot_mode)) {
		return -EINVAL;
	}

	fname = (char *)prd_out_fname[boot_mode];

	ret = prd_vfs_file_chk(prd, fname, O_RDONLY, 0666, &file_size);
	if (ret < 0) {
		return ret;
	}

	if (file_size <= MAX_LOG_FILE_SIZE) {
		goto out;
	}

	t_prd_info(prd, "file size : %s, %lld > MAX_LOG_FILE_SIZE(%d)\n",
			fname, file_size, MAX_LOG_FILE_SIZE);

	for (i = MAX_LOG_FILE_COUNT - 1; i >= 0; i--) {
		if (i == 0) {
			snprintf(buf1, sizeof(buf1), "%s", fname);
		} else {
			snprintf(buf1, sizeof(buf1), "%s.%d", fname, i);
		}

		if (i == (MAX_LOG_FILE_COUNT - 1)) {
			ret = prd_vfs_file_remove(prd, buf1);
			if (ret < 0) {
				goto out;
			}
			t_prd_info(prd, "file removed : %s\n", buf1);
		} else {
			snprintf(buf2, sizeof(buf2), "%s.%d", fname, (i + 1));

			ret = prd_vfs_file_rename(prd, buf1, buf2);
			if (ret < 0) {
				goto out;
			}
		}
	}

out:
	return ret;
}

static int prd_do_write_file(struct siw_hal_prd_data *prd,
				char *fname,
				char *data,
				int write_time)
{
//	struct device *dev = prd->dev;
	struct file *filp = NULL;
	loff_t offset = 0;
	char time_string[PRD_TIME_STR_SZ] = {0, };
	int twlen = 0;
	int nwlen = 0;
	int ret = 0;

	filp = prd_vfs_file_open(prd, fname,
				O_WRONLY|O_CREAT|O_APPEND, 0666);
	if (filp == NULL) {
		ret = -ENOENT;
		goto out;
	}

	ret = prd_chk_file_mode(prd, filp, fname);
	if (ret < 0) {
		goto out_close;
	}

	if (write_time == TIME_INFO_WRITE) {
		struct timespec my_time;
		struct tm my_date;

		my_time = current_kernel_time();
		time_to_tm(my_time.tv_sec,
				sys_tz.tz_minuteswest * 60 * (-1),
				&my_date);
		snprintf(time_string, 64,
			"\n[%02d-%02d %02d:%02d:%02d.%03lu]\n",
			my_date.tm_mon + 1,
			my_date.tm_mday, my_date.tm_hour,
			my_date.tm_min, my_date.tm_sec,
			(unsigned long) my_time.tv_nsec / 1000000);
		ret = prd_kernel_write(filp, (const char *)time_string,
					strlen(time_string), &offset);
		if (ret < 0) {
			t_prd_err(prd, "failed to write file : %s(%d), %d \n",
					fname, (int)offset, ret);
			goto out_close;
		}
		twlen += strlen(time_string);
		nwlen += ret;
	}
	ret = prd_kernel_write(filp, (const char *)data,
				strlen(data), &offset);
	if (ret < 0) {
		t_prd_err(prd, "failed to write file : %s(%d), %d \n",
				fname, (int)offset, ret);
		goto out_close;
	}
	twlen += strlen(data);
	nwlen += ret;

out_close:
	filp_close(filp, 0);
	if (ret >= 0) {
		t_prd_info(prd, "file write done : %s(%d, %d)\n",
				fname, twlen, nwlen);
	}

out:
	return ret;
}

static int prd_write_file(struct siw_hal_prd_data *prd, char *data, int write_time)
{
	char *prd_out_fname[] = {
		[NORMAL_BOOT]			= __prd_out_file,
		[MINIOS_AAT]			= __prd_out_file_mo_aat,
		[MINIOS_MFTS_FOLDER]	= __prd_out_file_mo_mfo,
		[MINIOS_MFTS_FLAT]		= __prd_out_file_mo_mfl,
		[MINIOS_MFTS_CURVED]	= __prd_out_file_mo_mcv,
	};
	struct device *dev = prd->dev;
	char *fname = NULL;
	int boot_mode = 0;
	int ret = 0;

	boot_mode = siw_touch_boot_mode_check(dev);
	if (__prd_boot_mode_is_err(dev, boot_mode)) {
		return -EINVAL;
	}

	fname = (char *)prd_out_fname[boot_mode];

	ret = prd_do_write_file(prd, fname, data, write_time);

	return ret;
}

static int prd_write_test_mode(struct siw_hal_prd_data *prd, u8 type)
{
	struct device *dev = prd->dev;
	struct siw_touch_chip *chip = to_touch_chip(dev);
	struct siw_hal_reg *reg = chip->reg;
	u32 testmode = 0;
	u8 disp_mode = 0x3;
	int retry = 20;
	u32 rdata = 0x01;
	int waiting_time = 400;
	int ret = 0;

	switch (type) {
	case OPEN_NODE_TEST:
		testmode = (disp_mode << 8) + type;
		waiting_time = 10;
		break;
	case SHORT_NODE_TEST:
		testmode = (disp_mode << 8) + type;
		waiting_time = 1000;
		break;
	case U3_M2_RAWDATA_TEST:
		testmode = (disp_mode << 8) + type;
		break;
	case U0_M1_RAWDATA_TEST:
		type = 0x6;
		testmode = type;
		break;
	case U0_M2_RAWDATA_TEST:
		type = 0x5;
		testmode = type;
		break;
	case U3_BLU_JITTER_TEST:
		testmode = ((disp_mode << 8) + type) | LINE_FILTER_OPTION;
		break;
	}

	/* TestType Set */
	ret = siw_hal_write_value(dev,
				reg->tc_tsp_test_ctl,
				testmode);
	if (ret < 0) {
		return ret;
	}
	t_prd_info(prd, "write testmode = %x\n", testmode);
	touch_msleep(waiting_time);

	/* Check Test Result - wait until 0 is written */
	do {
		touch_msleep(100);
		ret = siw_hal_read_value(dev,
					reg->tc_tsp_test_status,
					&rdata);
		if (ret < 0) {
			return ret;
		}
		if (ret >= 0) {
			t_prd_dbg_base(prd, "rdata = 0x%x\n", rdata);
		}
	} while ((rdata != 0xAA) && retry--);

	if (rdata != 0xAA) {
		t_prd_err(prd, "ProductionTest Type [%d] Time out\n", type);
		goto out;
	}

	return 1;

out:
	t_prd_err(prd, "Write test mode failed\n");
	return 0;
}

static int prd_spec_file_read_ext(struct siw_hal_prd_data *prd)
{
	char *prd_in_fname[] = {
		[0]	= __prd_in_file,
		[1]	= __prd_in_file_m,
	};
	struct device *dev = prd->dev;
	struct file *filp = NULL;
	loff_t offset = 0;
	char *fname;
	int path_idx = 0;
	int ret = 0;

	path_idx = !!(siw_touch_boot_mode_check(dev) >= MINIOS_MFTS_FOLDER);
	fname = (char *)prd_in_fname[path_idx];

	filp = prd_vfs_file_open(prd, fname, O_RDONLY, 0);
	if (filp == NULL) {
		ret = -ENOENT;
		goto out;
	}

	kernel_read(filp, offset, (char *)prd->line, sizeof(prd->line));

	filp_close(filp, 0);

	t_prd_info(prd, "file detected : %s\n", fname);

out:
	return ret;
}

static int prd_spec_file_read(struct siw_hal_prd_data *prd)
{
	struct device *dev = prd->dev;
	struct siw_touch_chip *chip = to_touch_chip(dev);
	struct siw_ts *ts = chip->ts;
	const struct firmware *fwlimit = NULL;
	const char *path[2] = {
		ts->panel_spec,
		ts->panel_spec_mfts
	};
	char *fname;
	int path_idx = 0;
	int ret = 0;

	path_idx = !!(siw_touch_boot_mode_check(dev) >= MINIOS_MFTS_FOLDER);
	fname = (char *)path[path_idx];

	if (ts->panel_spec == NULL || ts->panel_spec_mfts == NULL) {
		t_prd_err(prd, "panel spec file name is null\n");
		ret = -EINVAL;
		goto out;
	}

	ret = request_firmware(&fwlimit, fname, dev);
	if (ret < 0) {
		t_prd_err(prd, "request file is failed in normal mode\n");
		goto out;
	}

	if (fwlimit->data == NULL) {
		ret = -EFAULT;
		t_prd_err(prd, "fwlimit->data is NULL\n");
		goto out;
	}

	strlcpy(prd->line, fwlimit->data, sizeof(prd->line));

out:
	if (fwlimit)
		release_firmware(fwlimit);

	return ret;
}

static int prd_get_limit(struct siw_hal_prd_data *prd,
		char *breakpoint, int16_t (*buf)[PRD_COL_SIZE])
{
	struct device *dev = prd->dev;
	int p = 0;
	int q = 0;
	int r = 0;
	int q_limit;
	int cipher = 1;
	char *found;
	int boot_mode = 0;
	int tx_num = 0;
	int rx_num = 0;
	int ret = 0;

	if (breakpoint == NULL) {
		ret = -EINVAL;
		goto out;
	}

	boot_mode = siw_touch_boot_mode_check(dev);
	if ((boot_mode > MINIOS_MFTS_CURVED) ||
		(boot_mode < NORMAL_BOOT)) {
		ret = -EINVAL;
		goto out;
	}

	ret = prd_spec_file_read_ext(prd);
	if (ret < 0) {
		ret = prd_spec_file_read(prd);
		if (ret < 0)
			goto out;
	}

#if 0
	if (prd->line == NULL) {
		ret = -EFAULT;
		goto out;
	}
#endif

	found = strnstr(prd->line, breakpoint, sizeof(prd->line));
	if (found == NULL) {
		t_prd_err(prd,
			"failed to find breakpoint. The panel spec file is wrong");
		ret = -EFAULT;
		goto out;
	}
	q = found - prd->line;

	memset(buf, 0, (PRD_ROW_SIZE * PRD_COL_SIZE)<<1);

	q_limit = ARRAY_SIZE(prd->line);
	while (q < q_limit) {
		if (prd->line[q] == ',') {
			cipher = 1;
			for (p = 1; (prd->line[q - p] >= '0') &&
					(prd->line[q - p] <= '9'); p++) {
				buf[tx_num][rx_num] += ((prd->line[q - p] - '0') * cipher);
				cipher *= 10;
			}
			r++;
			if (r % (int)PRD_COL_SIZE == 0) {
				rx_num = 0;
				tx_num++;
			} else {
				rx_num++;
			}
		}
		q++;
		if (r == (int)PRD_ROW_SIZE * (int)PRD_COL_SIZE) {
			break;
		}
	}

	if (!r) {
		ret = -EFAULT;
		goto out;
	}

	t_prd_info(prd, "Scanning panel spec file done\n");

out:
	return ret;
}

static int prd_os_result_get(struct siw_hal_prd_data *prd, u32 *buf)
{
	struct device *dev = prd->dev;
	struct siw_touch_chip *chip = to_touch_chip(dev);
	struct siw_hal_reg *reg = chip->reg;
	u32 os_result_offset;
	u32 offset;
	int ret = 0;

	ret = siw_hal_read_value(dev,
				reg->prd_open3_short_offset,
				&os_result_offset);
	if (ret < 0) {
		goto out;
	}

	offset = os_result_offset & 0xFFFF;

	ret = siw_hal_write_value(dev,
				reg->tc_tsp_test_data_offset,
				offset);
	if (ret < 0) {
		goto out;
	}

	ret = siw_hal_reg_read(dev,
				reg->tc_tsp_data_access_addr,
				(void *)buf, sizeof(u32)*PRD_ROW_SIZE);
	if (ret < 0) {
		goto out;
	}

out:
	return ret;
}

static int prd_os_xline_result_read(struct siw_hal_prd_data *prd,
			u8 (*buf)[PRD_COL_SIZE], int type)
{
//	struct device *dev = prd->dev;
	u32 buffer[PRD_ROW_SIZE] = {0,};
	int i = 0;
	int j = 0;
	int ret = 0;
	u8 w_val = 0x0;

	switch (type) {
	case OPEN_NODE_TEST:
		w_val = 0x1;
		break;
	case SHORT_NODE_TEST:
		w_val = 0x2;
		break;
	}

	ret = prd_os_result_get(prd, buffer);

	if (ret == 0) {
		for (i = 0; i < PRD_ROW_SIZE; i++) {
			for (j = 0; j < PRD_COL_SIZE; j++) {
				if ((buffer[i] & (0x1 << j)) != 0)
					buf[i][j] =
						(buf[i][j] | w_val);
			}
		}
	}

	return ret;
}


static int prd_open_short_test(struct siw_hal_prd_data *prd)
{
	struct device *dev = prd->dev;
	struct siw_touch_chip *chip = to_touch_chip(dev);
	struct siw_hal_reg *reg = chip->reg;
	u32 open_result = 0;
	u32 short_result = 0;
	u32 openshort_all_result = 0;
	u8 buf[PRD_ROW_SIZE][PRD_COL_SIZE];
	int type = 0;
	int size = 0;
	int i = 0;
	int j = 0;
	int ret = 0;

	/* Test Type Write */
	ret = prd_write_file(prd, "\n\n[OPEN_SHORT_ALL_TEST]\n", TIME_INFO_SKIP);
	if (ret < 0) {
		return ret;
	}

	memset(&buf, 0x0, sizeof(buf));

	/* 1. open_test */
	type = OPEN_NODE_TEST;
	ret = prd_write_test_mode(prd, type);
	if (ret < 0) {
		return ret;
	}
	if (!ret) {
		t_prd_err(prd, "write test mode failed\n");
		return 0x3;
	}

	ret = siw_hal_read_value(dev,
				reg->tc_tsp_test_pf_result,
				&open_result);
	if (ret < 0) {
		return ret;
	}
	t_prd_info(prd, "open result = %d\n", open_result);

	if (open_result) {
		ret = prd_os_xline_result_read(prd, buf, type);
		if (ret < 0) {
			return ret;
		}
		openshort_all_result |= 0x1;
	}

	/* 2. short_test */
	type = SHORT_NODE_TEST;
	ret = prd_write_test_mode(prd, type);
	if (ret < 0) {
		return ret;
	}
	if (!ret) {
		t_prd_err(prd, "write test mode failed\n");
		return 0x3;
	}

	ret = siw_hal_read_value(dev,
				reg->tc_tsp_test_pf_result,
				&short_result);
	t_prd_info(prd, "short result = %d\n", short_result);

	if (short_result) {
		ret = prd_os_xline_result_read(prd, buf, type);
		openshort_all_result |= 0x2;
	}

	/* fail case */
	if (openshort_all_result != 0) {
		size = siw_prd_buf_snprintf(prd->buf_write, 0, "\n   : ");

		for (i = 0; i < PRD_COL_SIZE; i++)
			size += siw_prd_buf_snprintf(prd->buf_write,
						size,
						" [%2d] ", i);

		for (i = 0; i < PRD_ROW_SIZE; i++) {
			size += siw_prd_buf_snprintf(prd->buf_write,
						size,
						"\n[%2d] ", i);

			for (j = 0; j < PRD_COL_SIZE; j++) {
				size += siw_prd_buf_snprintf(prd->buf_write,
							size, "%5s ",
							((buf[i][j] & 0x3) == 0x3) ?  "O,S" :
							((buf[i][j] & 0x1) == 0x1) ?  "O" :
							((buf[i][j] & 0x2) == 0x2) ?  "S" : "-");
			}
		}
		size += siw_prd_buf_snprintf(prd->buf_write, size, "\n");
	} else
		size = siw_prd_buf_snprintf(prd->buf_write, size,
					"OPEN_SHORT_ALL_TEST : Pass\n");

	ret = prd_write_file(prd, prd->buf_write, TIME_INFO_SKIP);
	if (ret < 0) {
		return ret;
	}

	return openshort_all_result;
}

static int prd_print_u8(struct siw_hal_prd_data *prd, char *buf,
				int size, u8 *rawdata_buf_u8,
				int row_size, int col_size,
				const char *name)
{
	char *log_buf = prd->log_buf;
	int i, j;
	int col_i;
	int min = 9999;
	int max = 0;

	t_prd_info(prd, "-------- %s(%d %d) --------\n",
				name, row_size, col_size);

	size += siw_prd_buf_snprintf(buf, size, "-------- %s(%d %d) --------",
				name, row_size, col_size);

	/* print a frame data */
	size += siw_prd_buf_snprintf(buf, size, "\n   : ");

	for (i = 0; i < col_size; i++) {
		size += siw_prd_buf_snprintf(buf, size, " [%2d] ", i);
	}

	for (i = 0; i < row_size; i++) {
		int log_ret = 0;

		memset(log_buf, 0, sizeof(prd->log_buf));

		size += siw_prd_buf_snprintf(buf, size, "\n[%2d] ", i);

		log_ret += siw_prd_log_buf_snprintf(log_buf,
						log_ret,  "[%2d]  ", i);

		col_i = col_size * i;
		for (j = 0; j < col_size; j++) {
			size += siw_prd_buf_snprintf(buf, size,
						"%5d ", rawdata_buf_u8[col_i+j]);

			log_ret += siw_prd_buf_snprintf(log_buf,
							log_ret,
							"%5d ", rawdata_buf_u8[col_i+j]);

			if (rawdata_buf_u8[col_i+j] != 0 &&
				rawdata_buf_u8[col_i+j] < min) {
				min = rawdata_buf_u8[col_i+j];
			}
			if (rawdata_buf_u8[col_i+j] > max) {
				max = rawdata_buf_u8[col_i+j];
			}
		}
		t_prd_info(prd, "%s\n", log_buf);
	}

	size += siw_prd_buf_snprintf(buf, size, "\n");

	size += siw_prd_buf_snprintf(buf, size,
				"\nRawdata min : %d , max : %d\n\n",
				min, max);
	return size;
}

static int prd_print_16(struct siw_hal_prd_data *prd, char *buf,
				int size, int16_t *rawdata_buf_16,
				int row_size, int col_size,
				const char *name)
{
	char *log_buf = prd->log_buf;
	int i, j;
	int col_i;
	int min = 9999;
	int max = 0;

	t_prd_info(prd, "-------- %s(%d %d) --------\n",
				name, row_size, col_size);

	size += siw_prd_buf_snprintf(buf, size,
				"-------- %s(%d %d) --------\n",
				name, row_size, col_size);

	/* print a frame data */
	size += siw_prd_buf_snprintf(buf, size, "\n   : ");

	for (i = 0; i < col_size; i++) {
		size += siw_prd_buf_snprintf(buf, size, " [%2d] ", i);
	}

	for (i = 0; i < PRD_ROW_SIZE; i++) {
		int log_ret = 0;

		memset(log_buf, 0, sizeof(prd->log_buf));

		size += siw_prd_buf_snprintf(buf, size, "\n[%2d] ", i);

		log_ret += siw_prd_log_buf_snprintf(log_buf,
						log_ret,  "[%2d]  ", i);

		col_i = col_size * i;
		for (j = 0; j < col_size; j++) {
			size += siw_prd_buf_snprintf(buf, size,
						"%5d ", rawdata_buf_16[col_i+j]);

			log_ret += siw_prd_log_buf_snprintf(log_buf,
							log_ret,
							"%5d ", rawdata_buf_16[col_i+j]);
			if (rawdata_buf_16[col_i+j] != 0 &&
				rawdata_buf_16[col_i+j] < min) {
				min = rawdata_buf_16[col_i+j];
			}
			if (rawdata_buf_16[col_i+j] > max) {
				max = rawdata_buf_16[col_i+j];
			}
		}
		t_prd_info(prd, "%s\n", log_buf);
	}

	size += siw_prd_buf_snprintf(buf, size, "\n");

	size += siw_prd_buf_snprintf(buf, size,
				"\nRawdata min : %d , max : %d\n\n",
				min, max);
	return size;
}

static int prd_print_rawdata(struct siw_hal_prd_data *prd, char *buf, int type, int size)
{
	int16_t *rawdata_buf_16 = NULL;
	u8 *rawdata_buf_u8 = NULL;
	const char *name = NULL;
	int col_size = 0;
	int row_size = 0;

	switch (type) {
	case M1_ODD_DATA:
		col_size = PRD_M1_COL_SIZE;
		row_size = PRD_ROW_SIZE;
		rawdata_buf_16 = prd->m1_buf_odd_rawdata;
		name = "ODD Data";
		break;
	case M1_EVEN_DATA:
		col_size = PRD_M1_COL_SIZE;
		row_size = PRD_ROW_SIZE;
		rawdata_buf_16 = prd->m1_buf_even_rawdata;
		name = "EVEN Data";
		break;
	case M2_ODD_DATA:
		col_size = PRD_COL_SIZE;
		row_size = PRD_ROW_SIZE;
		rawdata_buf_16 = prd->m2_buf_odd_rawdata;
		name = "ODD Data";
		break;
	case M2_EVEN_DATA:
		col_size = PRD_COL_SIZE;
		row_size = PRD_ROW_SIZE;
		rawdata_buf_16 = prd->m2_buf_even_rawdata;
		name = "EVEN Data";
		break;
	case LABEL_DATA:
		col_size = PRD_COL_SIZE;
		row_size = PRD_ROW_SIZE;
		rawdata_buf_u8 = prd->buf_label;
		name = "LABEL Data";
		break;
	default:
		t_prd_warn(prd, "prd_print_rawdata: invalud type, %d\n", type);
		break;
	}

	if (rawdata_buf_16) {
		size = prd_print_16(prd, buf, size, rawdata_buf_16,
				row_size, col_size, name);
	} else if (rawdata_buf_u8) {
		size = prd_print_u8(prd, buf, size, rawdata_buf_u8,
				row_size, col_size, name);
	}

	return size;
}

/*
*	return "result Pass:0 , Fail:1"
*/
static int prd_compare_tool(struct siw_hal_prd_data *prd,
				int print_index_cnt, int16_t **buf,
				int row, int col, u8 type)
{
	int16_t *raw_buf;
	int i, j ,k;
	int col_i;
	int	result = 0;
	int size = 0;
	int curr_raw;

	for (k =0 ; k < print_index_cnt ; k++) {
		size += siw_prd_buf_snprintf(prd->buf_write,
					size,
					"-------- Compare[%d/%d] --------\n",
					k, print_index_cnt);
		raw_buf = buf[k];
		col_i = 0;
		for (i = 0 ; i < row ; i++) {
			for (j = 0 ; j < col ; j++) {
				curr_raw = raw_buf[col_i+j];

			#if 0	//for test
				t_prd_info(prd, "%d %d\n",
					curr_raw,
					prd->image_upper[i][j]);
			#endif

				if ((curr_raw < prd->image_lower[i][j]) ||
					(curr_raw > prd->image_upper[i][j]))
				{
					if ((type != U0_M1_RAWDATA_TEST) &&
						((i <= 1) && (j <= 4)))
					{
						if (curr_raw) {
							result = 1;
							size += siw_prd_buf_snprintf(prd->buf_write,
										size,
										"F [%d][%d] = %d\n",
										i, j, curr_raw);
						}
					} else {
						result = 1;
						size += siw_prd_buf_snprintf(prd->buf_write,
									size,
									"F [%d][%d] = %d\n",
									i, j, curr_raw);
					}
				}
			}
			col_i += col;
		}

		if (!result) {
			size += siw_prd_buf_snprintf(prd->buf_write,
						size,
						"(none)\n");
		}
	}

	t_prd_info(prd, "type %d, result %d\n", type, result);

	return result;
}

/* Rawdata compare result
	Pass : reurn 0
	Fail : return 1
*/
static int prd_compare_rawdata(struct siw_hal_prd_data *prd, u8 type)
{
//	struct device *dev = prd->dev;
	/* spec reading */
	char lower_str[64] = {0, };
	char upper_str[64] = {0, };
	int16_t *rawdata_buf[MAX_TEST_CNT] = {NULL,};
	int col_size = 0;
	int row_size = 0;
	int result = 0;

	switch (type) {
	case U0_M1_RAWDATA_TEST:
		snprintf(lower_str, sizeof(lower_str),
				"U0_M1_Lower");
		snprintf(upper_str, sizeof(upper_str),
				"U0_M1_Upper");
		col_size = PRD_M1_COL_SIZE;
		row_size = PRD_ROW_SIZE;
		rawdata_buf[0] = prd->m1_buf_odd_rawdata;
		rawdata_buf[1] = prd->m1_buf_even_rawdata;
		break;
	case U0_M2_RAWDATA_TEST:
		snprintf(lower_str, sizeof(lower_str),
				"U0_M2_Lower");
		snprintf(upper_str, sizeof(upper_str),
				"U0_M2_Upper");
		col_size = PRD_COL_SIZE;
		row_size = PRD_ROW_SIZE;
		rawdata_buf[0] = prd->m2_buf_odd_rawdata;
		rawdata_buf[1] = prd->m2_buf_even_rawdata;
		break;
	case U3_M2_RAWDATA_TEST:
		snprintf(lower_str, sizeof(lower_str),
				"U3_M2_Lower");
		snprintf(upper_str, sizeof(upper_str),
				"U3_M2_Upper");
		col_size = PRD_COL_SIZE;
		row_size = PRD_ROW_SIZE;
		rawdata_buf[0] = prd->m2_buf_odd_rawdata;
		rawdata_buf[1] = prd->m2_buf_even_rawdata;
		break;
	case U3_BLU_JITTER_TEST:
		snprintf(lower_str, sizeof(lower_str),
				"U3_Blu_Jitter_Lower");
		snprintf(upper_str, sizeof(upper_str),
				"U3_Blu_Jitter_Upper");
		col_size = PRD_COL_SIZE;
		row_size = PRD_ROW_SIZE;
		rawdata_buf[0] = prd->m2_buf_odd_rawdata;
		rawdata_buf[1] = prd->m2_buf_even_rawdata;
		break;
	}

	prd_get_limit(prd, lower_str, prd->image_lower);
	prd_get_limit(prd, upper_str, prd->image_upper);

	if (type == U0_M1_RAWDATA_TEST) {
		result = prd_compare_tool(prd, M1_RAWDATA_TEST_CNT,
						rawdata_buf, row_size, col_size,type);
	}
	else if((type == U3_M2_RAWDATA_TEST) ||
			(type == U0_M2_RAWDATA_TEST) ||
			(type == U3_BLU_JITTER_TEST))
	{
		result = prd_compare_tool(prd, M2_RAWDATA_TEST_CNT,
						rawdata_buf, row_size, col_size, type);
	}

	return result;
}

#define IC_AIT_START_REG				(0xC6C) //AIT Algorithm Engine HandShake Reg
#define IC_AIT_DATA_READYSTATUS			(0xC64)

/*
 * SIW TOUCH IC F/W Stop HandShake
 */
static int prd_stop_firmware(struct siw_hal_prd_data *prd, u32 wdata)
{
	struct device *dev = prd->dev;
//	struct siw_touch_chip *chip = to_touch_chip(dev);
//	struct siw_hal_reg *reg = chip->reg;
	u32 read_val;
	u32 check_data = 0;
	int try_cnt = 0;
	int ret = 0;

	/*
	 * STOP F/W to check
	 */
	ret = siw_hal_write_value(dev, IC_AIT_START_REG, wdata);
	if (ret < 0) {
		goto out;
	}
	ret = siw_hal_read_value(dev, IC_AIT_START_REG, &check_data);
	if (ret < 0) {
		goto out;
	}
#if 0
	ret = siw_hal_read_value(dev, IC_AIT_START_REG, &check_data);
	if (ret < 0) {
		goto out;
	}
#endif
	t_prd_info(prd, "check_data : %x\n", check_data);

	try_cnt = 1000;
	do {
		--try_cnt;
		if (try_cnt == 0) {
			t_prd_err(prd, "[ERR] get_data->try_cnt == 0\n");
			ret = -ETIMEDOUT;
			goto out;
		}
		siw_hal_read_value(dev, IC_AIT_DATA_READYSTATUS, &read_val);
		t_prd_info(prd, "Read RS_IMAGE = [%x] , OK RS_IMAGE = [%x]\n",
			read_val, (u32)RS_IMAGE);
		touch_msleep(10);
	} while(read_val != (u32)RS_IMAGE);

out:
	return ret;
}

static int prd_start_firmware(struct siw_hal_prd_data *prd)
{
	struct device *dev = prd->dev;
//	struct siw_touch_chip *chip = to_touch_chip(dev);
//	struct siw_hal_reg *reg = chip->reg;
	u32 const cmd = IT_IMAGE_NONE;
	u32 check_data = 0;
	u32 read_val = 0;
	int ret = 0;

	/* Release F/W to operate */
	ret = siw_hal_write_value(dev,IC_AIT_START_REG, cmd);
	if (ret < 0) {
		goto out;
	}
	ret = siw_hal_read_value(dev, IC_AIT_START_REG, &check_data);
	if (ret < 0) {
		goto out;
	}
#if 0
	ret = siw_hal_read_value(dev, IC_AIT_START_REG, &check_data);
	if (ret < 0) {
		goto out;
	}
#endif
	t_prd_info(prd, "check_data : %x\n", check_data);

	//for test
	ret = siw_hal_read_value(dev, IC_AIT_DATA_READYSTATUS, &read_val);
	if (ret < 0) {
		goto out;
	}
	t_prd_info(prd, "Read RS_IMAGE = [%x]\n", read_val);

out:
	return ret;
}

static int prd_read_rawdata(struct siw_hal_prd_data *prd, u8 type)
{
	struct device *dev = prd->dev;
	struct siw_touch_chip *chip = to_touch_chip(dev);
	struct siw_hal_reg *reg = chip->reg;
	int __m1_frame_size = PRD_ROW_SIZE*PRD_M1_COL_SIZE*PRD_RAWDATA_SIZE;
	int __m2_frame_size = PRD_ROW_SIZE*PRD_COL_SIZE*PRD_RAWDATA_SIZE;
	u32 raw_offset_info = 0;
	u16 raw_data_offset[MAX_TEST_CNT] = {0, };
	int16_t *buf_rawdata[MAX_TEST_CNT] = {NULL, };
	int16_t *raw_buf;
//	u16 raw_data_odd_offset = 0;
//	u16 raw_data_even_offset = 0;
	int i, j = 0;
	int ret = 0;

	if (__m1_frame_size % 4)
		__m1_frame_size = (((__m1_frame_size >> 2) + 1) << 2);
	if (__m2_frame_size % 4)
		__m2_frame_size = (((__m2_frame_size >> 2) + 1) << 2);

	/* Odd/Even Frame Offeset Read */
	ret = siw_hal_read_value(dev,
				reg->prd_m1_m2_raw_offset,
				&raw_offset_info);
	if (ret < 0) {
		goto out;
	}
	raw_data_offset[0] = raw_offset_info & 0xFFFF;
	raw_data_offset[1] = (raw_offset_info >> 16) & 0xFFFF;
	t_prd_info(prd, "Test type=%d Odd Offset=%x Even offset=%x\n",
		type, raw_data_offset[0], raw_data_offset[1]);

	switch (type) {
	case U0_M1_RAWDATA_TEST:
	#if 1
		buf_rawdata[0] = prd->m1_buf_odd_rawdata;
		buf_rawdata[1] = prd->m1_buf_even_rawdata;

		for (i=0 ; i<M1_RAWDATA_TEST_CNT ; i++) {
			raw_buf = buf_rawdata[i];

			/* raw data offset write */
			ret = siw_hal_reg_write(dev,
						reg->tc_tsp_test_data_offset,
						(void *)&raw_data_offset[i], sizeof(raw_data_offset[i]));
			if (ret < 0) {
				goto out;
			}

			/* raw data read */
			memset(raw_buf, 0, sizeof(int16_t)*PRD_ROW_SIZE*PRD_COL_SIZE);
			memset(prd->buf_m1_rawdata_tmp, 0, sizeof(prd->buf_m1_rawdata_tmp));

			ret = siw_hal_reg_read(dev,
						reg->tc_tsp_data_access_addr,
						(u8 *)&prd->buf_m1_rawdata_tmp, __m1_frame_size);

			for (j = 0 ; j < PRD_ROW_SIZE; j++ ) {
				raw_buf[i<<1] 	= prd->buf_m1_rawdata_tmp[PRD_ROW_SIZE+j];
				raw_buf[(j<<1)+1] = prd->buf_m1_rawdata_tmp[j];
			}
		}
	#else
		/* odd raw data offset write */
		ret = siw_hal_reg_write(dev,
					reg->tc_tsp_test_data_offset,
					(void *)&raw_data_offset[0], sizeof(raw_data_offset[0]));
		if (ret < 0) {
			goto out;
		}
		/* odd raw data read */
		memset(prd->m1_buf_odd_rawdata, 0, sizeof(prd->m1_buf_odd_rawdata));
		memset(prd->buf_m1_rawdata_tmp, 0, sizeof(prd->buf_m1_rawdata_tmp));

		ret = siw_hal_reg_read(dev,
					reg->tc_tsp_data_access_addr,
					(void *)&prd->buf_m1_rawdata_tmp, __m1_frame_size);
		if (ret < 0) {
			goto out;
		}
		for (i = 0 ; i < PRD_ROW_SIZE ; i++) {
			prd->m1_buf_odd_rawdata[i<<1] 	= prd->buf_m1_rawdata_tmp[PRD_ROW_SIZE+i];
			prd->m1_buf_odd_rawdata[(i<<1)+1] = prd->buf_m1_rawdata_tmp[i];
		}

		/* even raw data offset write */
		ret = siw_hal_reg_write(dev,
					reg->tc_tsp_test_data_offset,
					(void *)&raw_data_offset[1], sizeof(raw_data_offset[1]));
		if (ret < 0) {
			goto out;
		}
		/* even raw data read */
		memset(prd->m1_buf_even_rawdata, 0, sizeof(prd->m1_buf_even_rawdata));
		memset(prd->buf_m1_rawdata_tmp, 0, sizeof(prd->buf_m1_rawdata_tmp));

		ret = siw_hal_reg_read(dev,
					reg->tc_tsp_data_access_addr,
					(u8 *)&prd->buf_m1_rawdata_tmp, __m1_frame_size);
		if (ret < 0) {
			goto out;
		}
		for(i = 0; i < PRD_ROW_SIZE; i++) {
			prd->m1_buf_even_rawdata[i<<1] 	= prd->buf_m1_rawdata_tmp[PRD_ROW_SIZE+i];
			prd->m1_buf_even_rawdata[(i<<1)+1] = prd->buf_m1_rawdata_tmp[i];
		}
	#endif
		break;

	case U3_M2_RAWDATA_TEST:
	case U0_M2_RAWDATA_TEST:
	case U3_BLU_JITTER_TEST:
		/* Conrtol LCD Backlightness */
		if(type == U3_BLU_JITTER_TEST){
			int backlightness;

		//	LCD brightness ON 742ms -> LCD brightness OFF 278mms -> LCD brightness ON 278ms

			backlightness = siw_touch_sys_get_panel_bl(dev);

			touch_msleep(742);

			siw_touch_sys_set_panel_bl(dev, 0);

			touch_msleep(278);

			siw_touch_sys_set_panel_bl(dev, backlightness);

			touch_msleep(278);
		}

		buf_rawdata[0] = prd->m2_buf_odd_rawdata;
		buf_rawdata[1] = prd->m2_buf_even_rawdata;

		for (i=0 ; i<M2_RAWDATA_TEST_CNT; i++) {
			/* raw data offset write */
			ret = siw_hal_reg_write(dev,
						reg->tc_tsp_test_data_offset,
						(void *)&raw_data_offset[i], sizeof(raw_data_offset[i]));
			if (ret < 0) {
				goto out;
			}
			/* raw data read */
			memset(buf_rawdata[i], 0, sizeof(int16_t)*PRD_ROW_SIZE*PRD_COL_SIZE);
			ret = siw_hal_reg_read(dev,
						reg->tc_tsp_data_access_addr,
						(void *)buf_rawdata[i], sizeof(int16_t)*PRD_ROW_SIZE*PRD_COL_SIZE);
			if (ret < 0) {
				goto out;
			}
		}
		break;
	}

out:
	return ret;
}

static void prd_tune_display(struct siw_hal_prd_data *prd, char *tc_tune_code,
			int offset, int type)
{
//	struct device *dev = prd->dev;
	char log_buf[TC_TUNE_CODE_SIZE] = {0,};
	char temp[TC_TUNE_CODE_SIZE] = {0,};
	int size = 0;
	int i = 0;

	switch (type) {
	case PRD_TUNE_DISPLAY_TYPE_1:
		size = snprintf(log_buf, TC_TUNE_CODE_SIZE,
					"GOFT tune_code_read : ");
		if ((tc_tune_code[offset] >> 4) == 1) {
			temp[offset] = tc_tune_code[offset] - (0x1 << 4);
			size += snprintf(log_buf + size,
						TC_TUNE_CODE_SIZE - size,
						" %d  ", temp[offset]);
		} else {
			size += snprintf(log_buf + size,
						TC_TUNE_CODE_SIZE - size,
						"-%d  ", tc_tune_code[offset]);
		}
		t_prd_info(prd, "%s\n", log_buf);
		size += snprintf(log_buf + size, TC_TUNE_CODE_SIZE - size, "\n");
		prd_write_file(prd, log_buf, TIME_INFO_SKIP);
		break;
	case PRD_TUNE_DISPLAY_TYPE_2:
		size = snprintf(log_buf, TC_TUNE_CODE_SIZE,
					"LOFT tune_code_read : ");
		for (i = 0; i < TC_TOTAL_CH_SIZE; i++) {
			if ((tc_tune_code[offset+i]) >> 5 == 1) {
				temp[offset+i] =
					tc_tune_code[offset+i] - (0x1 << 5);
				size += snprintf(log_buf + size,
							TC_TUNE_CODE_SIZE - size,
							" %d  ", temp[offset+i]);
			} else {
				size += snprintf(log_buf + size,
							TC_TUNE_CODE_SIZE - size,
							"-%d  ",
							tc_tune_code[offset+i]);
			}
		}
		t_prd_info(prd, "%s\n", log_buf);
		size += snprintf(log_buf + size, TC_TUNE_CODE_SIZE - size, "\n");
		prd_write_file(prd, log_buf, TIME_INFO_SKIP);
		break;
	default:
		t_prd_err(prd, "unknown tune type\n");
		break;
	}
}

/*
*	tune code result check
*/
static int prd_read_tune_code(struct siw_hal_prd_data *prd, u8 type)
{
	struct device *dev = prd->dev;
	struct siw_touch_chip *chip = to_touch_chip(dev);
	struct siw_hal_reg *reg = chip->reg;
	u8 tune_code_read_buf[276] = {0,};
	u32 tune_code_offset;
	u32 offset;
	int ret = 0;

	ret = siw_hal_read_value(dev,
				reg->prd_tune_result_offset,
				&tune_code_offset);
	if (ret < 0) {
		goto out;
	}
	offset = (tune_code_offset >> 16) & 0xFFFF;

	t_prd_info(prd, "tune_code_offset = %Xh", offset);
//	t_prd_dbg_base(prd, "tune_code_offset = %Xh", offset);

	ret = siw_hal_write_value(dev,
				reg->tc_tsp_test_data_offset,
				offset);
	if (ret < 0) {
		goto out;
	}
	ret = siw_hal_reg_read(dev,
				reg->tc_tsp_data_access_addr,
				(void *)tune_code_read_buf, TC_TUNE_CODE_SIZE);
	if (ret < 0) {
		goto out;
	}

	prd_write_file(prd, "\n[Read Tune Code]\n", TIME_INFO_SKIP);

	switch (type) {
	case U0_M1_RAWDATA_TEST:
		prd_tune_display(prd, tune_code_read_buf,
					TSP_TUNE_CODE_L_GOFT_OFFSET,
					PRD_TUNE_DISPLAY_TYPE_1);
		prd_tune_display(prd, tune_code_read_buf,
					TSP_TUNE_CODE_R_GOFT_OFFSET,
					PRD_TUNE_DISPLAY_TYPE_1);
		prd_tune_display(prd, tune_code_read_buf,
					TSP_TUNE_CODE_L_M1_OFT_OFFSET,
					PRD_TUNE_DISPLAY_TYPE_2);
		prd_tune_display(prd, tune_code_read_buf,
					TSP_TUNE_CODE_R_M1_OFT_OFFSET,
					PRD_TUNE_DISPLAY_TYPE_2);
		break;
	case U3_M2_RAWDATA_TEST:
	case U0_M2_RAWDATA_TEST:
		prd_tune_display(prd, tune_code_read_buf,
					TSP_TUNE_CODE_L_GOFT_OFFSET + 1,
					PRD_TUNE_DISPLAY_TYPE_1);
		prd_tune_display(prd, tune_code_read_buf,
					TSP_TUNE_CODE_R_GOFT_OFFSET + 1,
					PRD_TUNE_DISPLAY_TYPE_1);
		prd_tune_display(prd, tune_code_read_buf,
					TSP_TUNE_CODE_L_G1_OFT_OFFSET,
					PRD_TUNE_DISPLAY_TYPE_2);
		prd_tune_display(prd, tune_code_read_buf,
					TSP_TUNE_CODE_L_G2_OFT_OFFSET,
					PRD_TUNE_DISPLAY_TYPE_2);
		prd_tune_display(prd, tune_code_read_buf,
					TSP_TUNE_CODE_L_G3_OFT_OFFSET,
					PRD_TUNE_DISPLAY_TYPE_2);
		prd_tune_display(prd, tune_code_read_buf,
					TSP_TUNE_CODE_R_G1_OFT_OFFSET,
					PRD_TUNE_DISPLAY_TYPE_2);
		prd_tune_display(prd, tune_code_read_buf,
					TSP_TUNE_CODE_R_G2_OFT_OFFSET,
					PRD_TUNE_DISPLAY_TYPE_2);
		prd_tune_display(prd, tune_code_read_buf,
					TSP_TUNE_CODE_R_G3_OFT_OFFSET,
					PRD_TUNE_DISPLAY_TYPE_2);
		break;
	}
	prd_write_file(prd, "\n", TIME_INFO_SKIP);

out:
	return ret;
}

/*
 * print Frame Data & Compare rawdata & save result
 * return result Pass:0 Fail:1
 */
static int prd_conrtol_rawdata_result(struct siw_hal_prd_data *prd, int type, int result_on)
{
	int print_type[2] = {0, };
	int i,test_cnt;
	int size = 0;
	int result = 0;

	switch (type) {
	case U3_M2_RAWDATA_TEST:
		print_type[0] = M2_ODD_DATA;
		print_type[1] = M2_EVEN_DATA;
		test_cnt = M2_RAWDATA_TEST_CNT;
		break;
	case U0_M1_RAWDATA_TEST:
		print_type[0] = M1_ODD_DATA;
		print_type[1] = M1_EVEN_DATA;
		test_cnt = M1_RAWDATA_TEST_CNT;
		break;
	case U0_M2_RAWDATA_TEST:
		print_type[0] = M2_ODD_DATA;
		print_type[1] = M2_EVEN_DATA;
		test_cnt = M2_RAWDATA_TEST_CNT;
		break;
	case U3_BLU_JITTER_TEST:
		print_type[0] = M2_ODD_DATA;
		print_type[1] = M2_EVEN_DATA;
		test_cnt = M2_RAWDATA_TEST_CNT;
		break;

	default:
		t_prd_err(prd, "conrtol_rawdata_result Type not defined, %d\n", type);
		return 1;
	}

	/* print Raw Data */
	for (i=0 ; i<test_cnt ; i++) {
		size += prd_print_rawdata(prd, prd->buf_write, print_type[i], size);
	}
	if (size)
		size += siw_prd_buf_snprintf(prd->buf_write, size, "\n\n");

	if (result_on == RESULT_ON) {
		/* Frame Data write to Result File */
		prd_write_file(prd, prd->buf_write, TIME_INFO_SKIP);

		memset(prd->buf_write, 0, PRD_BUF_SIZE);

		/* rawdata compare result(pass : 0 fail : 1) */
		result = prd_compare_rawdata(prd, type);
		prd_write_file(prd, prd->buf_write, TIME_INFO_SKIP);
	}

	return result;
}

/*
 * Index "result_on" -> Save Result File ON: 1 , OFF: 0
 * return - Pass: 0 , Fail: 1
 */
static int prd_do_rawdata_test(struct siw_hal_prd_data *prd,
				int type, int result_on)
{
//	struct device *dev = prd->dev;
	char *info_str = NULL;
	char *sprt_str = NULL;
	char test_type[32] = {0, };
	int ret = 0;

	switch (type) {
	case U3_M2_RAWDATA_TEST:
		info_str = "========U3_M2_RAWDATA_TEST========";
		if (result_on) {
			sprt_str = "[U3_M2_RAWDATA_TEST]";
		}
		break;
	case U0_M1_RAWDATA_TEST:
		info_str = "========U0_M1_RAWDATA_TEST========";
		if (result_on) {
			sprt_str = "[U0_M1_RAWDATA_TEST]";
		}
		break;
	case U0_M2_RAWDATA_TEST:
		info_str = "========U0_M2_RAWDATA_TEST========";
		if (result_on) {
			sprt_str = "[U0_M2_RAWDATA_TEST]";
		}
		break;
	case U3_BLU_JITTER_TEST:
		info_str = "========U3_BLU_JITTER_TEST========";
		if (result_on) {
			sprt_str= "[U3_BLU_JITTER_TEST]";
		}
		break;

	default:
		t_prd_err(prd, "Test Type not defined, %d\n", type);
		return 1;
	}

	if (info_str) {
		t_prd_info(prd, "%s\n", info_str);
	}
	if (sprt_str) {
		snprintf(test_type, sizeof(test_type), "\n\n%s\n", sprt_str);
		/* Test Type Write */
		prd_write_file(prd, test_type, TIME_INFO_SKIP);
	}

	/* Test Start & Finish Check */
	ret = prd_write_test_mode(prd, type);
	if (ret < 0) {
		return ret;
	}
	if (!ret) {
		t_prd_err(prd, "production test couldn't be done\n");
		return 1;
	}

	/* Read Frame Data */
	prd_read_rawdata(prd, type);

	/* Compare Spec & Print & Save result*/
	ret = prd_conrtol_rawdata_result(prd, type, result_on);

	/* tune code result check */
	if (!(type == U3_BLU_JITTER_TEST)) {
		prd_read_tune_code(prd, type);
	}

	return ret;
}

static int prd_rawdata_test(struct siw_hal_prd_data *prd,
				int type, int result_on)
{
	int ret;

	ret = prd_do_rawdata_test(prd, type, result_on);
	if (ret < 0) {
		t_prd_err(prd, "prd_rawdata_test(%d) failed, %d\n",
				type, ret);
	}

	return ret;
}

static void prd_firmware_version_log(struct siw_hal_prd_data *prd)
{
	struct device *dev = prd->dev;
	struct siw_touch_chip *chip = to_touch_chip(dev);
	char *log_buf = prd->log_buf;
	int boot_mode = 0;
	int ret = 0;

	memset(log_buf, 0, sizeof(prd->log_buf));

	boot_mode = siw_touch_boot_mode_check(dev);
	if (boot_mode >= MINIOS_MFTS_FOLDER)
		ret = prd_chip_info(dev);

	ret = snprintf(log_buf, PRD_LOG_BUF_SIZE,
				"======== Firmware Info ========\n");
	ret += snprintf(log_buf + ret, PRD_LOG_BUF_SIZE - ret,
				"version : v%d.%02d\n",
				chip->fw.version[0],
				chip->fw.version[1]);
	ret += snprintf(log_buf + ret, PRD_LOG_BUF_SIZE - ret,
				"revision : %d\n",
				chip->fw.revision);
	ret += snprintf(log_buf + ret, PRD_LOG_BUF_SIZE - ret,
				"product id : %s\n",
				chip->fw.product_id);

	prd_write_file(prd, log_buf, TIME_INFO_SKIP);
}

static void prd_ic_run_info_print(struct siw_hal_prd_data *prd)
{
	struct device *dev = prd->dev;
	struct siw_touch_chip *chip = to_touch_chip(dev);
	struct siw_hal_reg *reg = chip->reg;
	char *log_buf = prd->log_buf;
	int ret = 0;
	u32 rdata[4] = {0};

	memset(log_buf, 0, sizeof(prd->log_buf));

	ret = siw_hal_reg_read(dev,
				reg->info_lot_num,
				(void *)&rdata, sizeof(rdata));

	ret = snprintf(log_buf, PRD_LOG_BUF_SIZE,
				"\n===== Production Info =====\n");
	ret += snprintf(log_buf + ret, PRD_LOG_BUF_SIZE - ret,
				"lot : %d\n", rdata[0]);
	ret += snprintf(log_buf + ret, PRD_LOG_BUF_SIZE - ret,
				"serial : 0x%X\n", rdata[1]);
	ret += snprintf(log_buf + ret, PRD_LOG_BUF_SIZE - ret,
				"date : 0x%X 0x%X\n",
				rdata[2], rdata[3]);
	ret += snprintf(log_buf + ret, PRD_LOG_BUF_SIZE - ret,
				"date : %04d.%02d.%02d %02d:%02d:%02d Site%d\n",
				rdata[2] & 0xFFFF, (rdata[2] >> 16 & 0xFF),
				(rdata[2] >> 24 & 0xFF), rdata[3] & 0xFF,
				(rdata[3] >> 8 & 0xFF),
				(rdata[3] >> 16 & 0xFF),
				(rdata[3] >> 24 & 0xFF));

	prd_write_file(prd, log_buf, TIME_INFO_SKIP);
}


static int prd_ic_exception_check(struct siw_hal_prd_data *prd, char *buf)
{
	struct device *dev = prd->dev;
	int boot_mode = 0;
	int size = 0;

	boot_mode = siw_touch_boot_mode_check(dev);

#if 0
	if (0) {
		/* MINIOS mode, MFTS mode check */
		if (siw_touch_get_boot_mode() || boot_mode > 0) {
			if (chip->fw.revision < 2 ||
					chip->fw.revision > 3 ||
					chip->fw.version[0] != 1) {
				t_prd_info(prd, "ic_revision : %d, fw_version : v%d.%02d\n",
						chip->fw.revision,
						chip->fw.version[0], chip->fw.version[1]);

				size += siw_snprintf(buf, size,
								"========RESULT=======\n");

				if (chip->fw.version[0] != 1) {
					size += siw_snprintf(buf, size,
									"version[v%d.%02d] : Fail\n",
									chip->fw.version[0], chip->fw.version[1]);
				} else {
					size += siw_snprintf(buf, size,
									"version[v%d.%02d] : Pass\n",
									chip->fw.version[0], chip->fw.version[1]);
				}

				if (chip->fw.revision < 2 || chip->fw.revision > 3) {
					size += siw_snprintf(buf, size,
									"revision[%d] : Fail\n",
									chip->fw.revision);
				} else {
					size += siw_snprintf(buf, size,
									"revision[%d] : Pass\n",
									chip->fw.revision);
				}
				prd_write_file(dev, buf, TIME_INFO_SKIP);

				size += siw_snprintf(buf, size,
							"Raw data : Fail\n");
				size += siw_snprintf(buf, size,
							"Channel Status : Fail\n");
				prd_write_file(dev, "", TIME_INFO_WRITE);
			}
		}
	}
#endif

	return size;
}

static int prd_write_test_control(struct siw_hal_prd_data *prd, u32 mode)
{
	struct device *dev = prd->dev;
	struct siw_touch_chip *chip = to_touch_chip(dev);
	struct siw_hal_reg *reg = chip->reg;
	u32 test_mode_enter_cmt = mode;
	u32 test_mode_enter_check = -1;
	unsigned int delay_ms = 30;
	int ret = 0;

	ret = siw_hal_write_value(dev,
				reg->prd_tc_test_mode_ctl,
				test_mode_enter_cmt);
	if (ret < 0) {
		goto out;
	}
	t_prd_info(prd, "write tc_test_mode_ctl = %x\n", test_mode_enter_cmt);

	touch_msleep(delay_ms);

	ret = siw_hal_read_value(dev,
				reg->prd_tc_test_mode_ctl,
				&test_mode_enter_check);
	if (ret < 0) {
		goto out;
	}
	t_prd_info(prd, "read tc_test_mode_ctl= %x\n", test_mode_enter_check);

out:
	return ret;
}

static int prd_show_do_sd(struct siw_hal_prd_data *prd, char *buf)
{
	struct device *dev = prd->dev;
	int rawdata_ret = 0;
	int openshort_ret = 0;
	int blu_jitter_ret = 0;
	int size = 0;
	int ret;

	ret = prd_write_test_control(prd, CMD_TEST_ENTER);
	if (ret < 0) {
		goto out;
	}

	siw_touch_irq_control(dev, INTERRUPT_DISABLE);

	prd_chip_driving(dev, LCD_MODE_STOP);

	/*
	 * U3_M2_RAWDATA_TEST
	 * rawdata - pass : 0, fail : 1
	 * rawdata tunecode - pass : 0, fail : 2
	 */
	rawdata_ret = prd_rawdata_test(prd, U3_M2_RAWDATA_TEST, RESULT_ON);
	if (rawdata_ret < 0) {
		return rawdata_ret;
	}

	/*
	 * U3_BLU_JITTER_TEST
	 * BLU Jitter - pass : 0, fail : 1
	 * : if you want to BLU_JITTER_TEST You should be able to control LCD backlightness
	 *   BLU_JITTER_TEST Start CMD Write ->
	 *   LCD brightness ON 742ms -> LCD brightness OFF 278mms -> LCD brightness ON 278ms ->
	 *   Read RawData & Compare with BLU Jitter Spec
	 */
	blu_jitter_ret = prd_rawdata_test(prd, U3_BLU_JITTER_TEST, RESULT_ON);
	if (blu_jitter_ret < 0) {
		return blu_jitter_ret;
	}

	/*
	 * OPEN_SHORT_ALL_TEST
	 * open - pass : 0, fail : 1
	 * short - pass : 0, fail : 2
	 */
	openshort_ret = prd_open_short_test(prd);
	if (openshort_ret < 0) {
		t_prd_err(prd, "prd_open_short_test failed, %d\n", openshort_ret);
		return openshort_ret;
	}

	size += siw_snprintf(buf, size,
				"\n========RESULT=======\n");

	if (!rawdata_ret) {
		size += siw_snprintf(buf, size,
					"Raw Data : Pass\n");
	} else {
		size += siw_snprintf(buf, size,
					"Raw Data : Fail\n");
	}

	if (!blu_jitter_ret) {
		size += siw_snprintf(buf, size,
					"Blu Jitter Data : Pass\n");
	} else {
		size += siw_snprintf(buf, size,
					"Blu Jitter : Fail\n");
	}

	if (!openshort_ret) {
		size += siw_snprintf(buf, size,
					"Channel Status : Pass\n");
	} else {
		size += siw_snprintf(buf, size,
					"Channel Status : Fail (%d/%d)\n",
					((openshort_ret & 0x1) == 0x1) ? 0 : 1,
					((openshort_ret & 0x2) == 0x2) ? 0 : 1);
	}

	prd_write_file(prd, buf, TIME_INFO_SKIP);

	prd_write_test_control(prd, CMD_TEST_EXIT);

	prd_chip_driving(dev, LCD_MODE_U3);

	prd_chip_reset(dev);

out:
	return size;
}

static ssize_t prd_show_sd(struct device *dev, char *buf)
{
	struct siw_touch_chip *chip = to_touch_chip(dev);
	struct siw_ts *ts = chip->ts;
	struct siw_hal_prd_data *prd = (struct siw_hal_prd_data *)ts->prd;
	int size = 0;

	/* LCD mode check */
	if (chip->lcd_mode != LCD_MODE_U3) {
		size += siw_snprintf(buf, size,
					"LCD mode is not U3. Test Result : Fail\n");
		goto out;
	}

	/* file create , time log */
	prd_write_file(prd, "\nShow_sd Test Start", TIME_INFO_SKIP);
	prd_write_file(prd, "\n", TIME_INFO_WRITE);

	t_prd_info(prd, "show_sd test begins\n");

	/* ic rev check - MINIOS mode, MFTS mode check */
	size = prd_ic_exception_check(prd, buf);
	if (size > 0) {
		t_prd_err(prd, "ic exception detected, test canceled\n");
		goto out;
	}

	prd_firmware_version_log(prd);
	prd_ic_run_info_print(prd);

	mutex_lock(&ts->lock);
	size = prd_show_do_sd(prd, buf);
	mutex_unlock(&ts->lock);

	prd_write_file(prd, "Show_sd Test End\n", TIME_INFO_WRITE);
	prd_log_file_size_check(prd);

	t_prd_info(prd, "show_sd test terminated\n");

out:
	return (ssize_t)size;
}

static int prd_show_prd_get_data_raw_prd(struct device *dev)
{
	struct siw_touch_chip *chip = to_touch_chip(dev);
	struct siw_ts *ts = chip->ts;
//	struct siw_hal_reg *reg = chip->reg;
	struct siw_hal_prd_data *prd = (struct siw_hal_prd_data *)ts->prd;
	int ret = 0;

	t_prd_info(prd, "======== CMD_RAWDATA_PRD ========\n");

	mutex_lock(&ts->lock);

	ret = prd_write_test_control(prd, CMD_TEST_ENTER);
	if (ret < 0) {
		goto out;
	}

	siw_touch_irq_control(dev, INTERRUPT_DISABLE);

	prd_chip_driving(dev, LCD_MODE_STOP);

	ret = prd_rawdata_test(prd, U3_M2_RAWDATA_TEST, RESULT_OFF);

	siw_touch_irq_control(dev, INTERRUPT_ENABLE);

	if (ret < 0) {
		goto out;
	}

	ret = prd_write_test_control(prd, CMD_TEST_EXIT);
	if (ret < 0) {
		goto out;
	}

out:
	mutex_unlock(&ts->lock);

	prd_chip_driving(dev, LCD_MODE_U3);
	prd_chip_reset(dev);

	return ret;
}

static int prd_show_prd_get_data_raw_tcm(struct device *dev)
{
	struct siw_touch_chip *chip = to_touch_chip(dev);
	struct siw_ts *ts = chip->ts;
	struct siw_hal_reg *reg = chip->reg;
	struct siw_hal_prd_data *prd = (struct siw_hal_prd_data *)ts->prd;
	int __m2_frame_size = PRD_ROW_SIZE*PRD_COL_SIZE*PRD_RAWDATA_SIZE;
	int ret = 0;

	/* 	LCD mode check 	*/
#if 0
	if (chip->lcd_mode != LCD_MODE_U3) {
		t_prd_info(prd, "LCD mode is not U3. Test Result : Fail\n");
		goto out;
	}
#endif

	t_prd_info(prd, "======== CMD_RAWDATA_TCM ========\n");

	/* TCM Offset write 0 */
	ret = siw_hal_write_value(dev, reg->prd_serial_tcm_offset, 0);
	if (ret < 0) {
		goto out;
	}

	/*
	 * TCM Memory Access Select
	 * tc_mem_sel(0x0457) "RAW" or "BASE1" or "BASE2" or "BASE3"
	 */
	ret = siw_hal_write_value(dev, reg->prd_tc_mem_sel, TCM_MEM_RAW);
	if(ret < 0 ){
		goto out;
	}

	/* 	Read Rawdata	*/
	memset(prd->m2_buf_odd_rawdata, 0, sizeof(prd->m2_buf_odd_rawdata));
	ret = siw_hal_reg_read(dev,
				reg->prd_tcm_base_addr,
				(void *)prd->m2_buf_odd_rawdata,
				__m2_frame_size);
	if(ret < 0 ){
		goto out;
	}

	/*	Print RawData Buffer	*/
	prd_print_rawdata(prd, prd->buf_write, M2_ODD_DATA, 0);

out:
	return ret;
}

static int prd_show_prd_get_data_raw_ait(struct device *dev)
{
	struct siw_touch_chip *chip = to_touch_chip(dev);
	struct siw_ts *ts = chip->ts;
	struct siw_hal_reg *reg = chip->reg;
	struct siw_hal_prd_data *prd = (struct siw_hal_prd_data *)ts->prd;
	int ret = 0;

	t_prd_info(prd, "======== CMD_RAWDATA_AIT ========\n");

	ret = prd_stop_firmware(prd, IT_IMAGE_RAW);
	if (ret < 0) {
		goto out;
	}

	ret = siw_hal_write_value(dev,
				reg->tc_tsp_test_data_offset,
				AIT_RAW_DATA_OFFSET);
	if (ret < 0) {
		goto out;
	}

	memset(prd->m2_buf_odd_rawdata, 0, sizeof(prd->m2_buf_odd_rawdata));

	ret = siw_hal_reg_read(dev, reg->tc_tsp_data_access_addr,
					(void *)prd->m2_buf_odd_rawdata,
					sizeof(int16_t)*(PRD_ROW_SIZE)*(PRD_COL_SIZE));
	if (ret < 0) {
		goto out;
	}

	prd_print_rawdata(prd, prd->buf_write, M2_ODD_DATA, 0);

	ret = prd_start_firmware(prd);
	if (ret < 0) {
		goto out;
	}

out:
	return ret;
}

static int prd_show_prd_get_data_ait_basedata_even(struct device *dev)
{
	struct siw_touch_chip *chip = to_touch_chip(dev);
	struct siw_ts *ts = chip->ts;
	struct siw_hal_reg *reg = chip->reg;
	struct siw_hal_prd_data *prd = (struct siw_hal_prd_data *)ts->prd;
	int ret = 0;

	t_prd_info(prd, "======== CMD_AIT_BASEDATA_EVEN ========\n");

	ret = prd_stop_firmware(prd, IT_IMAGE_BASELINE);
	if (ret < 0) {
		goto out;
	}

	ret = siw_hal_write_value(dev,
				reg->tc_tsp_test_data_offset,
				AIT_BASE_DATA_EVNE_OFFSET);
	if (ret < 0) {
		goto out;
	}

	memset(prd->m2_buf_even_rawdata, 0, sizeof(prd->m2_buf_even_rawdata));

	ret = siw_hal_reg_read(dev, reg->tc_tsp_data_access_addr,
				(void *)prd->m2_buf_even_rawdata,
				sizeof(int16_t)*(PRD_ROW_SIZE)*(PRD_COL_SIZE));
	if (ret < 0) {
		goto out;
	}

	prd_print_rawdata(prd, prd->buf_write, M2_EVEN_DATA, 0);

	ret = prd_start_firmware(prd);
	if (ret < 0) {
		goto out;
	}

out:
	return ret;
}

static int prd_show_prd_get_data_ait_basedata_odd(struct device *dev)
{
	struct siw_touch_chip *chip = to_touch_chip(dev);
	struct siw_ts *ts = chip->ts;
	struct siw_hal_reg *reg = chip->reg;
	struct siw_hal_prd_data *prd = (struct siw_hal_prd_data *)ts->prd;
	int ret = 0;

	t_prd_info(prd, "======== CMD_AIT_BASEDATA_ODD ========\n");

	ret = prd_stop_firmware(prd, IT_IMAGE_BASELINE_ODD);
	if (ret < 0) {
		goto out;
	}

	ret = siw_hal_write_value(dev,
				reg->tc_tsp_test_data_offset,
				AIT_BASE_DATA_ODD_OFFSET);
	if (ret < 0) {
		goto out;
	}

	memset(prd->m2_buf_odd_rawdata, 0, sizeof(prd->m2_buf_odd_rawdata));

	ret = siw_hal_reg_read(dev,
				reg->tc_tsp_data_access_addr,
				(void *)prd->m2_buf_odd_rawdata,
				sizeof(int16_t)*(PRD_ROW_SIZE)*(PRD_COL_SIZE));
	if (ret < 0) {
		goto out;
	}

	prd_print_rawdata(prd, prd->buf_write, M2_ODD_DATA, 0);

	ret = prd_start_firmware(prd);
	if (ret < 0) {
		goto out;
	}

out:
	return ret;
}

static int prd_show_prd_get_data_filtered_deltadata(struct device *dev)
{
	struct siw_touch_chip *chip = to_touch_chip(dev);
	struct siw_ts *ts = chip->ts;
	struct siw_hal_reg *reg = chip->reg;
	struct siw_hal_prd_data *prd = (struct siw_hal_prd_data *)ts->prd;
	int row, col;
	int i;
	int ret = 0;

	t_prd_info(prd, "======== CMD_FILTERED_DELTADATA ========\n");

	ret = prd_stop_firmware(prd, IT_IMAGE_FILTERED_DELTA);
	if (ret < 0) {
		goto out;
	}

	ret = siw_hal_write_value(dev,
				reg->tc_tsp_test_data_offset,
				FILTERED_DELTA_DATA_OFFSET);
	if (ret < 0) {
		goto out;
	}

	memset(prd->buf_delta, 0, sizeof(prd->buf_delta));
	memset(prd->m2_buf_odd_rawdata, 0, sizeof(prd->m2_buf_odd_rawdata));

	ret = siw_hal_reg_read(dev,
				reg->tc_tsp_data_access_addr,
				(void *)prd->buf_delta,
				sizeof(int16_t)*(PRD_ROW_SIZE+2)*(PRD_COL_SIZE+2));
	if (ret < 0) {
		goto out;
	}

	for(i = 0; i < PRD_ROW_SIZE*PRD_COL_SIZE; i++){
		row = i / PRD_COL_SIZE;
		col = i % PRD_COL_SIZE;
		prd->m2_buf_odd_rawdata[i] = prd->buf_delta[(row + 1)*(PRD_COL_SIZE + 2) + (col + 1)];
	}

	prd_print_rawdata(prd, prd->buf_write, M2_ODD_DATA, 0);

	ret = prd_start_firmware(prd);
	if (ret < 0) {
		goto out;
	}

out:
	return ret;
};

static int prd_show_prd_get_data_deltadata(struct device *dev)
{
	struct siw_touch_chip *chip = to_touch_chip(dev);
	struct siw_ts *ts = chip->ts;
	struct siw_hal_reg *reg = chip->reg;
	struct siw_hal_prd_data *prd = (struct siw_hal_prd_data *)ts->prd;
	int row, col;
	int i;
	int ret = 0;

	t_prd_info(prd, "======== CMD_DELTADATA ========\n");

	ret = prd_stop_firmware(prd, IT_IMAGE_DELTA);
	if (ret < 0) {
		goto out;
	}

	ret = siw_hal_write_value(dev,
				reg->tc_tsp_test_data_offset,
				DELTA_DATA_OFFSET);
	if (ret < 0) {
		goto out;
	}

	memset(prd->buf_delta, 0, sizeof(prd->buf_delta));
	memset(prd->m2_buf_odd_rawdata, 0, sizeof(prd->m2_buf_odd_rawdata));

	ret = siw_hal_reg_read(dev,
				reg->tc_tsp_data_access_addr,
				(void *)prd->buf_delta,
				sizeof(int16_t)*(PRD_ROW_SIZE+2)*(PRD_COL_SIZE+2));
	if (ret < 0) {
		goto out;
	}

	for(i = 0; i < PRD_ROW_SIZE*PRD_COL_SIZE; i++){
		row = i / PRD_COL_SIZE;
		col = i % PRD_COL_SIZE;
		prd->m2_buf_odd_rawdata[i] = prd->buf_delta[(row + 1)*(PRD_COL_SIZE + 2) + (col + 1)];
	}

	prd_print_rawdata(prd, prd->buf_write, M2_ODD_DATA, 0);

	ret = prd_start_firmware(prd);
	if (ret < 0) {
		goto out;
	}

out:
	return ret;

}

static int prd_show_prd_get_data_labeldata(struct device *dev)
{
	struct siw_touch_chip *chip = to_touch_chip(dev);
	struct siw_ts *ts = chip->ts;
	struct siw_hal_reg *reg = chip->reg;
	struct siw_hal_prd_data *prd = (struct siw_hal_prd_data *)ts->prd;
	int row, col;
	int i;
	int ret = 0;

	t_prd_info(prd, "======== CMD_LABELDATA ========\n");

	ret = prd_stop_firmware(prd, IT_IMAGE_LABEL);
	if (ret < 0) {
		goto out;
	}

	ret = siw_hal_write_value(dev,
				reg->tc_tsp_test_data_offset,
				LABLE_DATA_OFFSET);
	if (ret < 0) {
		goto out;
	}

	memset(prd->buf_label, 0, sizeof(prd->buf_label));
	memset(prd->buf_label_tmp, 0, sizeof(prd->buf_label_tmp));

	ret = siw_hal_reg_read(dev,
				reg->tc_tsp_data_access_addr,
				(void *)prd->buf_label_tmp,
				sizeof(u8)*(PRD_ROW_SIZE+2)*(PRD_COL_SIZE+2));
	if (ret < 0) {
		goto out;
	}

	for(i = 0; i < PRD_ROW_SIZE*PRD_COL_SIZE; i++){
		row = i / PRD_COL_SIZE;
		col = i % PRD_COL_SIZE;
		prd->buf_label[i] = prd->buf_label_tmp[(row + 1)*(PRD_COL_SIZE + 2) + (col + 1)];
	}

	prd_print_rawdata(prd, prd->buf_write, LABEL_DATA, 0);

	ret = prd_start_firmware(prd);
	if (ret < 0) {
		goto out;
	}

out:
	return ret;
}

static int prd_show_prd_get_data_blu_jitter(struct device *dev)
{
	struct siw_touch_chip *chip = to_touch_chip(dev);
	struct siw_ts *ts = chip->ts;
//	struct siw_hal_reg *reg = chip->reg;
	struct siw_hal_prd_data *prd = (struct siw_hal_prd_data *)ts->prd;
	int ret = 0;

	t_prd_info(prd, "======== CMD_BLU_JITTER ========\n");

	mutex_lock(&ts->lock);

	ret = prd_write_test_control(prd, CMD_TEST_ENTER);
	if (ret < 0) {
		goto out;
	}

	siw_touch_irq_control(dev, INTERRUPT_DISABLE);

	prd_chip_driving(dev, LCD_MODE_STOP);

	ret = prd_rawdata_test(prd, U3_BLU_JITTER_TEST,RESULT_OFF);

	siw_touch_irq_control(dev, INTERRUPT_ENABLE);

	if (ret < 0) {
		goto out;
	}

	ret = prd_write_test_control(prd, CMD_TEST_EXIT);
	if (ret < 0) {
		goto out;
	}

out:
	mutex_unlock(&ts->lock);

	prd_chip_driving(dev, LCD_MODE_U3);

	prd_chip_reset(dev);

	return ret;
}

static ssize_t prd_show_prd_get_data(struct device *dev, int type)
{
	struct siw_touch_chip *chip = to_touch_chip(dev);
	struct siw_ts *ts = chip->ts;
	struct siw_hal_prd_data *prd = (struct siw_hal_prd_data *)ts->prd;
	int ret = 0;

	switch (type) {
	case CMD_RAWDATA_PRD:
		ret = prd_show_prd_get_data_raw_prd(dev);
		break;
	case CMD_RAWDATA_TCM:
		ret = prd_show_prd_get_data_raw_tcm(dev);
		break;
	case CMD_RAWDATA_AIT:
		ret = prd_show_prd_get_data_raw_ait(dev);
		break;
	case CMD_AIT_BASEDATA_EVEN:
		ret = prd_show_prd_get_data_ait_basedata_even(dev);
		break;
	case CMD_AIT_BASEDATA_ODD:
		ret = prd_show_prd_get_data_ait_basedata_odd(dev);
		break;
	case CMD_FILTERED_DELTADATA:
		ret = prd_show_prd_get_data_filtered_deltadata(dev);
		break;
	case CMD_DELTADATA:
		ret = prd_show_prd_get_data_deltadata(dev);
		break;
	case CMD_LABELDATA:
		ret = prd_show_prd_get_data_labeldata(dev);
		break;
	case CMD_BLU_JITTER:
		ret = prd_show_prd_get_data_blu_jitter(dev);
		break;
	default:
		t_prd_err(prd, "invalid get_data request CMD");
		ret = -EINVAL;
		break;
	};

	return ret;
}

static ssize_t prd_show_get_data_common(struct device *dev, char *buf, int type)
{
	struct siw_touch_chip *chip = to_touch_chip(dev);
	struct siw_ts *ts = chip->ts;
	struct siw_hal_prd_data *prd = (struct siw_hal_prd_data *)ts->prd;
	int size = 0;
	int ret = 0;

	ret = prd_show_prd_get_data(dev, type);
	if (ret < 0){
		t_prd_err(prd, "prd_show_prd_get_data(%d) failed, %d\n",
			type, ret);
	}

	/*
	 * to prepare the response for APP I/F (not fixed)
	 */
	size += siw_snprintf(buf, size, "Get Data[%s] result:\n",
				prd_get_data_cmd_name[type]);
	size += siw_snprintf(buf, size, "%s\n",
				(ret < 0)? "Fail" : "Pass");
	return (ssize_t)size;
}

static ssize_t prd_show_delta(struct device *dev, char *buf)
{
	return (ssize_t)prd_show_get_data_common(dev, buf, CMD_DELTADATA);
}

static ssize_t prd_show_label(struct device *dev, char *buf)
{
	return (ssize_t)prd_show_get_data_common(dev, buf, CMD_LABELDATA);
}

static ssize_t prd_show_rawdata_prd(struct device *dev, char *buf)
{
	return (ssize_t)prd_show_get_data_common(dev, buf, CMD_RAWDATA_PRD);
}

static ssize_t prd_show_rawdata_tcm(struct device *dev, char *buf)
{
	return (ssize_t)prd_show_get_data_common(dev, buf, CMD_RAWDATA_TCM);
}

static ssize_t prd_show_rawdata_ait(struct device *dev, char *buf)
{
	return (ssize_t)prd_show_get_data_common(dev, buf, CMD_RAWDATA_AIT);
}

static ssize_t prd_show_basedata_even(struct device *dev, char *buf)
{
	struct siw_touch_chip *chip = to_touch_chip(dev);
	int size = 0;

	/* LCD mode check */
	if (chip->lcd_mode != LCD_MODE_U3) {
		size = siw_snprintf(buf, size,
					"LCD mode is not U3. Test Result : Fail\n");
		return size;
	}

	return (ssize_t)prd_show_get_data_common(dev, buf, CMD_AIT_BASEDATA_EVEN);
}

static ssize_t prd_show_basedata_odd(struct device *dev, char *buf)
{
	struct siw_touch_chip *chip = to_touch_chip(dev);
	int size = 0;

	/* LCD mode check */
	if (chip->lcd_mode != LCD_MODE_U3) {
		size = siw_snprintf(buf, size,
					"LCD mode is not U3. Test Result : Fail\n");
		return size;
	}

	return (ssize_t)prd_show_get_data_common(dev, buf, CMD_AIT_BASEDATA_ODD);
}

static int prd_show_do_lpwg_sd(struct siw_hal_prd_data *prd, char *buf)
{
	struct device *dev = prd->dev;
	int m1_rawdata_ret = 0;
	int m2_rawdata_ret = 0;
	int size = 0;
	int ret = 0;

	ret = prd_write_test_control(prd, CMD_TEST_ENTER);
	if (ret < 0) {
		return ret;
	}

	siw_touch_irq_control(dev, INTERRUPT_DISABLE);

	prd_chip_driving(dev, LCD_MODE_STOP);

	/*
	 * U0_M2_RAWDATA_TEST & U0_M1_RAWDATA_TEST
	 * rawdata - pass : 0, fail : 1
	 * rawdata tunecode - pass : 0, fail : 2
	 */
	m2_rawdata_ret = prd_rawdata_test(prd, U0_M2_RAWDATA_TEST, RESULT_ON);
	if (m2_rawdata_ret < 0) {
		size = m2_rawdata_ret;
		goto out;
	}
	m1_rawdata_ret = prd_rawdata_test(prd, U0_M1_RAWDATA_TEST, RESULT_ON);
	if (m1_rawdata_ret < 0) {
		size = m1_rawdata_ret;
		goto out;
	}

	size = siw_snprintf(buf, size, "========RESULT=======\n");

	if (!m1_rawdata_ret && !m2_rawdata_ret) {
		size += siw_snprintf(buf, size,
					"LPWG RawData : %s\n", "Pass");
	} else {
		size += siw_snprintf(buf, size,
					"LPWG RawData : %s (%d/%d)\n", "Fail",
					m1_rawdata_ret ? 0 : 1, m2_rawdata_ret ? 0 : 1);
	}

	prd_write_file(prd, buf, TIME_INFO_SKIP);

out:
	prd_chip_driving(dev, LCD_MODE_U3);

	prd_chip_reset(dev);

	return size;
}

static ssize_t prd_show_lpwg_sd(struct device *dev, char *buf)
{
	struct siw_touch_chip *chip = to_touch_chip(dev);
	struct siw_ts *ts = chip->ts;
	struct siw_hal_prd_data *prd = (struct siw_hal_prd_data *)ts->prd;
	int size = 0;

	/* LCD mode check */
	if (chip->lcd_mode != LCD_MODE_U0) {
		size = siw_snprintf(buf, size,
					"LCD mode is not U0. Test Result : Fail\n");
		goto out;
	}

	/* file create , time log */
	prd_write_file(prd, "\nShow_lpwg_sd Test Start", TIME_INFO_SKIP);
	prd_write_file(prd, "\n", TIME_INFO_WRITE);

	t_prd_info(prd, "show_lpwg_sd test begins\n");

	prd_firmware_version_log(prd);
	prd_ic_run_info_print(prd);

	mutex_lock(&ts->lock);
	size = prd_show_do_lpwg_sd(prd, buf);
	mutex_unlock(&ts->lock);

	prd_write_file(prd, "Show_lpwg_sd Test End\n", TIME_INFO_WRITE);
	prd_log_file_size_check(prd);

	t_prd_info(prd, "show_lpwg_sd test terminated\n");

out:
	return (ssize_t)size;
}

static ssize_t prd_show_irq_state(struct device *dev, char *buf)
{
	struct siw_touch_chip *chip = to_touch_chip(dev);
	struct siw_ts *ts = chip->ts;
	int irq_status = atomic_read(&ts->state.irq_enable);
	int size = 0;

	size = siw_snprintf(buf, size,
				"Irq State : %s\n",
				(irq_status)? "Enabled" : "Disabled");

	return size;
}

static ssize_t prd_store_irq_state(struct device *dev,
				const char *buf, size_t count)
{
	struct siw_touch_chip *chip = to_touch_chip(dev);
	struct siw_ts *ts = chip->ts;
	struct siw_hal_prd_data *prd = (struct siw_hal_prd_data *)ts->prd;
	int value = 0;

	if (sscanf(buf, "%d", &value) <= 0) {
		siw_prd_sysfs_err_invalid_param(prd);
		return count;
	}

	siw_touch_irq_control(ts->dev,
				(value)? INTERRUPT_ENABLE : INTERRUPT_DISABLE);

	return (ssize_t)count;
}


#define __PRD_FILE_RW_TEST

#if defined(__PRD_FILE_RW_TEST)

enum {
	PRD_FILE_TEST_OPTION_0	= 0,
	PRD_FILE_TEST_OPTION_1,
	PRD_FILE_TEST_OPTION_2,
	PRD_FILE_TEST_OPTION_MAX,
};

static void __prd_show_file_test_usage(struct siw_hal_prd_data *prd)
{
	t_prd_info(prd, "[Usage]\n");
	t_prd_info(prd, " Check : echo 0 {fname} > file_test\n");
	t_prd_info(prd, " Write : echo 1 {fname} > file_test\n");
	t_prd_info(prd, "       : Remove old file and create new one\n");
	t_prd_info(prd, " Write : echo 2 {fname} > file_test\n");
	t_prd_info(prd, "       : Rename old file and create new one\n");
}

static ssize_t prd_show_file_test(struct device *dev, char *buf)
{
	struct siw_touch_chip *chip = to_touch_chip(dev);
	struct siw_ts *ts = chip->ts;
	struct siw_hal_prd_data *prd = (struct siw_hal_prd_data *)ts->prd;

	__prd_show_file_test_usage(prd);

	return 0;
}

static ssize_t prd_store_file_test(struct device *dev,
				const char *buf, size_t count)
{
	struct siw_touch_chip *chip = to_touch_chip(dev);
	struct siw_ts *ts = chip->ts;
	struct siw_hal_prd_data *prd = (struct siw_hal_prd_data *)ts->prd;
	char fname[PRD_TMP_FILE_NAME_SZ] = {0, };
	int value = 0;
	int ret = 0;

	if (sscanf(buf, "%d %s", &value, fname) <= 0) {
		siw_prd_sysfs_err_invalid_param(prd);
		__prd_show_file_test_usage(prd);
		goto out;
	}
	if (value >= PRD_FILE_TEST_OPTION_MAX) {
		t_prd_err(prd, "invalude option, %d\n", value);
		__prd_show_file_test_usage(prd);
		goto out;
	}
	if (fname == NULL) {
		t_prd_err(prd, "target file name is NULL\n");
		__prd_show_file_test_usage(prd);
		goto out;
	}

	ret = prd_vfs_file_chk(prd, fname, O_RDONLY, 0666, NULL);
	if (ret < 0) {
		/* */
	}

	/* Check mode */
	if (value == PRD_FILE_TEST_OPTION_0) {
		goto out;
	}

	if (!ret) {
		if (value == PRD_FILE_TEST_OPTION_2) {	/* Rename */
			char buf1[PRD_TMP_FILE_NAME_SZ] = {0, };
			char buf2[PRD_TMP_FILE_NAME_SZ] = {0, };
			int i;

			for (i = MAX_LOG_FILE_COUNT - 1; i >= 0; i--) {
				if (i == 0) {
					snprintf(buf1, sizeof(buf1), "%s", fname);
				} else {
					snprintf(buf1, sizeof(buf1), "%s.%d", fname, i);
				}

				if (i == (MAX_LOG_FILE_COUNT - 1)) {
					ret = prd_vfs_file_remove(prd, buf1);
					if (ret < 0) {
						goto out;
					}
				} else {
					snprintf(buf2, sizeof(buf2), "%s.%d", fname, (i + 1));

					ret = prd_vfs_file_rename(prd, buf1, buf2);
					if (ret < 0) {
						goto out;
					}
				}
			}
		} else {	/* Remove */
			ret = prd_vfs_file_remove(prd, fname);
			if (ret < 0) {
				goto out;
			}
		}
	}

	ret = prd_do_write_file(prd, fname, "[File Write Test]\n", TIME_INFO_WRITE);
	if (ret < 0) {
		t_prd_err(prd, "File Write Test failed\n");
	}

out:
	return (ssize_t)count;
}
#else	/* __PRD_FILE_RW_TEST */
static ssize_t prd_show_file_test(struct device *dev, char *buf)
{
	t_dev_info(dev, "Nop ...\n");
	return 0;
}

static ssize_t prd_store_file_test(struct device *dev,
				const char *buf, size_t count)
{
	t_dev_info(dev, "Nop ...\n");
	return (ssize_t)count;
}
#endif	/* __PRD_FILE_RW_TEST */


#define SIW_TOUCH_HAL_PRD_ATTR(_name, _show, _store)	\
		__TOUCH_ATTR(_name, 0664, _show, _store)

#define _SIW_TOUCH_HAL_PRD_T(_name)	\
		touch_attr_##_name

static SIW_TOUCH_HAL_PRD_ATTR(sd, prd_show_sd, NULL);
static SIW_TOUCH_HAL_PRD_ATTR(delta, prd_show_delta, NULL);
static SIW_TOUCH_HAL_PRD_ATTR(label, prd_show_label, NULL);
static SIW_TOUCH_HAL_PRD_ATTR(rawdata_prd, prd_show_rawdata_prd, NULL);
static SIW_TOUCH_HAL_PRD_ATTR(rawdata_tcm, prd_show_rawdata_tcm, NULL);
static SIW_TOUCH_HAL_PRD_ATTR(rawdata_ait, prd_show_rawdata_ait, NULL);
static SIW_TOUCH_HAL_PRD_ATTR(base_even, prd_show_basedata_even, NULL);
static SIW_TOUCH_HAL_PRD_ATTR(base_odd, prd_show_basedata_odd, NULL);
static SIW_TOUCH_HAL_PRD_ATTR(lpwg_sd, prd_show_lpwg_sd, NULL);
static SIW_TOUCH_HAL_PRD_ATTR(irq_state, prd_show_irq_state, prd_store_irq_state);
static SIW_TOUCH_HAL_PRD_ATTR(file_test, prd_show_file_test, prd_store_file_test);

static struct attribute *siw_hal_prd_attribute_list[] = {
	&_SIW_TOUCH_HAL_PRD_T(sd).attr,
	&_SIW_TOUCH_HAL_PRD_T(delta).attr,
	&_SIW_TOUCH_HAL_PRD_T(label).attr,
	&_SIW_TOUCH_HAL_PRD_T(rawdata_prd).attr,
	&_SIW_TOUCH_HAL_PRD_T(rawdata_tcm).attr,
	&_SIW_TOUCH_HAL_PRD_T(rawdata_ait).attr,
	&_SIW_TOUCH_HAL_PRD_T(base_even).attr,
	&_SIW_TOUCH_HAL_PRD_T(base_odd).attr,
	&_SIW_TOUCH_HAL_PRD_T(lpwg_sd).attr,
	&_SIW_TOUCH_HAL_PRD_T(irq_state).attr,
	&_SIW_TOUCH_HAL_PRD_T(file_test).attr,
	NULL,
};

static const struct attribute_group __used siw_hal_prd_attribute_group = {
	.attrs = siw_hal_prd_attribute_list,
};

static struct siw_hal_prd_data *siw_hal_prd_alloc(struct device *dev)
{
	struct siw_touch_chip *chip = to_touch_chip(dev);
	struct siw_ts *ts = chip->ts;
	struct siw_hal_prd_data *prd;

	prd = devm_kzalloc(dev, sizeof(*prd), GFP_KERNEL);
	if (!prd) {
		t_dev_err(dev,
				"failed to allocate memory for prd\n");
		goto out;
	}

	snprintf(prd->name, sizeof(prd->name)-1, "%s-prd", dev_name(dev));

	t_dev_dbg_base(dev, "create prd[%s] (0x%X)\n",
				prd->name, sizeof(*prd));

	prd->dev = ts->dev;

	ts->prd = prd;

out:
	return prd;
}

static void siw_hal_prd_free(struct device *dev)
{
	struct siw_touch_chip *chip = to_touch_chip(dev);
	struct siw_ts *ts = chip->ts;
	struct siw_hal_prd_data *prd = (struct siw_hal_prd_data *)ts->prd;

	if (prd) {
		t_dev_dbg_base(dev, "free prd[%s]\n", prd->name);

		ts->prd = NULL;
		devm_kfree(dev, prd);
	}
}

static int siw_hal_prd_create_sysfs(struct device *dev)
{
	struct siw_touch_chip *chip = to_touch_chip(dev);
	struct siw_ts *ts = chip->ts;
	struct device *idev = &ts->input->dev;
	struct kobject *kobj = &ts->kobj;
	struct siw_hal_prd_data *prd;
	int ret = 0;

	if (kobj->parent != idev->kobj.parent) {
		t_dev_err(dev, "Invalid kobject\n");
		return -EINVAL;
	}

	prd = siw_hal_prd_alloc(dev);
	if (!prd) {
		ret = -ENOMEM;
		goto out;
	}

	ret = sysfs_create_group(kobj, &siw_hal_prd_attribute_group);
	if (ret < 0) {
		t_dev_err(dev, "%s prd sysfs register failed, %d\n",
				touch_chip_name(ts), ret);
		goto out_sysfs;
	}

	t_dev_dbg_base(dev, "%s prd sysfs registered\n",
			touch_chip_name(ts));

	t_prd_dbg_base(prd, "PRD_ROW_SIZE %d, PRD_COL_SIZE %d\n",
			PRD_ROW_SIZE, PRD_COL_SIZE);

	return 0;

out_sysfs:
	siw_hal_prd_free(dev);

out:
	return ret;
}

static void siw_hal_prd_remove_sysfs(struct device *dev)
{
	struct siw_touch_chip *chip = to_touch_chip(dev);
	struct siw_ts *ts = chip->ts;
	struct device *idev = &ts->input->dev;
	struct kobject *kobj = &ts->kobj;

	if (kobj->parent != idev->kobj.parent) {
		t_dev_err(dev, "Invalid kobject\n");
		return;
	}

	sysfs_remove_group(kobj, &siw_hal_prd_attribute_group);

	siw_hal_prd_free(dev);

	t_dev_dbg_base(dev, "%s prd sysfs unregistered\n",
			touch_chip_name(ts));
}

int siw_hal_prd_sysfs(struct device *dev, int on_off)
{
	if (on_off == DRIVER_INIT) {
		return siw_hal_prd_create_sysfs(dev);
	}

	siw_hal_prd_remove_sysfs(dev);
	return 0;
}

int siw_hal_set_prd_file(struct device *dev, const char *path, int idx)
{
	char *prd_file_group[] = {
		[0]	= __prd_in_file,
		[1]	= __prd_in_file_m,
		[2] = __prd_out_file,
		[3] = __prd_out_file_mo_aat,
		[4] = __prd_out_file_mo_mfo,
		[5] = __prd_out_file_mo_mfl,
		[6] = __prd_out_file_mo_mcv,
	};

	if (!path) {
		t_dev_err(dev, "NULL prd path (%d)\n", idx);
		return -EINVAL;
	}

	if (idx >= ARRAY_SIZE(prd_file_group)) {
		t_dev_err(dev, "wrong prd file index, %d\n", idx);
		return -EINVAL;
	}

	t_dev_info(dev, "prd file setup: %s -> %s\n",
			prd_file_group[idx], path);
	memcpy(prd_file_group[idx], path, strlen(path));

	return 0;
}

__siw_setup_str("siw_prd_if=", prd_setup_if, __prd_in_file);
__siw_setup_str("siw_prd_if_m=", prd_setup_if_m, __prd_in_file_m);

__siw_setup_str("siw_prd_of_=", prd_setup_of, __prd_out_file);
__siw_setup_str("siw_prd_of_mo_aat=", prd_setup_of_aat, __prd_out_file_mo_aat);
__siw_setup_str("siw_prd_of_mo_mfo=", prd_setup_of_mfo, __prd_out_file_mo_mfo);
__siw_setup_str("siw_prd_of_mo_mfl=", prd_setup_of_mfl, __prd_out_file_mo_mfl);
__siw_setup_str("siw_prd_of_mo_mcv=", prd_setup_of_mcv, __prd_out_file_mo_mcv);

#endif	/* __SIW_SUPPORT_PRD */



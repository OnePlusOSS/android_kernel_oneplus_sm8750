/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2012-2015, 2017-2019, 2021 The Linux Foundation.
 * All rights reserved.
 * Copyright (c) 2022, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef QPNP_PON_H
#define QPNP_PON_H

#include <dt-bindings/input/qcom,qpnp-power-on.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/version.h>

//#if IS_MODULE(CONFIG_OPLUS_FEATURE_QCOM_PMICWD)
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/of_regulator.h>

struct qpnp_pon_config {
	u32			pon_type;
	u32			support_reset;
	u32			key_code;
	u32			s1_timer;
	u32			s2_timer;
	u32			s2_type;
	bool			pull_up;
	int			state_irq;
	int			bark_irq;
	u16			s2_cntl_addr;
	u16			s2_cntl2_addr;
	bool			old_state;
	bool			use_bark;
	bool			config_reset;
};

struct pon_regulator {
	struct qpnp_pon		*pon;
	struct regulator_dev	*rdev;
	struct regulator_desc	rdesc;
	u32			addr;
	u32			bit;
	bool			enabled;
};

struct qpnp_pon {
	struct device		*dev;
	struct regmap		*regmap;
	struct input_dev	*pon_input;
	struct qpnp_pon_config	*pon_cfg;
	struct pon_regulator	*pon_reg_cfg;
	struct list_head	restore_regs;
	struct list_head	list;
	struct mutex		restore_lock;
	struct delayed_work	bark_work;
	struct dentry		*debugfs;
	u16			base;
	u16			pbs_base;
	u8			subtype;
	u8			pon_ver;
	u8			warm_reset_reason1;
	u8			warm_reset_reason2;
	int			num_pon_config;
	int			num_pon_reg;
	int			pon_trigger_reason;
	int			pon_power_off_reason;
	u32			dbc_time_us;
	u32			uvlo;
	int			warm_reset_poff_type;
	int			hard_reset_poff_type;
	int			shutdown_poff_type;
	int			resin_warm_reset_type;
	int			resin_hard_reset_type;
	int			resin_shutdown_type;
	bool			is_spon;
	bool			store_hard_reset_reason;
	bool			resin_hard_reset_disable;
	bool			resin_shutdown_disable;
	bool			ps_hold_hard_reset_disable;
	bool			ps_hold_shutdown_disable;
	bool			kpdpwr_dbc_enable;
	bool			resin_pon_reset;
	ktime_t			kpdpwr_last_release_time;
	bool			legacy_hard_reset_offset;
};
//#endif

/**
 * enum pon_trigger_source: List of PON trigger sources
 * %PON_SMPL:		PON triggered by Sudden Momentary Power Loss (SMPL)
 * %PON_RTC:		PON triggered by real-time clock (RTC) alarm
 * %PON_DC_CHG:		PON triggered by insertion of DC charger
 * %PON_USB_CHG:	PON triggered by insertion of USB
 * %PON_PON1:		PON triggered by other PMIC (multi-PMIC option)
 * %PON_CBLPWR_N:	PON triggered by power-cable insertion
 * %PON_KPDPWR_N:	PON triggered by long press of the power-key
 */
enum pon_trigger_source {
	PON_SMPL = 1,
	PON_RTC,
	PON_DC_CHG,
	PON_USB_CHG,
	PON_PON1,
	PON_CBLPWR_N,
	PON_KPDPWR_N,
};

/**
 * enum pon_power_off_type: Possible power off actions to perform
 * %PON_POWER_OFF_RESERVED:          Reserved, not used
 * %PON_POWER_OFF_WARM_RESET:        Reset the MSM but not all PMIC peripherals
 * %PON_POWER_OFF_SHUTDOWN:          Shutdown the MSM and PMIC completely
 * %PON_POWER_OFF_HARD_RESET:        Reset the MSM and all PMIC peripherals
 * %PON_POWER_OFF_MAX_TYPE:          Reserved, not used
 */
enum pon_power_off_type {
	PON_POWER_OFF_RESERVED		= 0x00,
	PON_POWER_OFF_WARM_RESET	= PON_POWER_OFF_TYPE_WARM_RESET,
	PON_POWER_OFF_SHUTDOWN		= PON_POWER_OFF_TYPE_SHUTDOWN,
	PON_POWER_OFF_HARD_RESET	= PON_POWER_OFF_TYPE_HARD_RESET,
	PON_POWER_OFF_MAX_TYPE		= 0x10,
};

enum pon_restart_reason {
	PON_RESTART_REASON_UNKNOWN		= 0x00,
	PON_RESTART_REASON_RECOVERY		= 0x01,
	PON_RESTART_REASON_BOOTLOADER		= 0x02,
	PON_RESTART_REASON_RTC			= 0x03,
	PON_RESTART_REASON_DMVERITY_CORRUPTED	= 0x04,
	PON_RESTART_REASON_DMVERITY_ENFORCE	= 0x05,
	PON_RESTART_REASON_KEYS_CLEAR		= 0x06,
	PON_RESTART_REASON_SILENT				= 0x0a,
	PON_RESTART_REASON_NON_SILENT			= 0x0b,
	PON_RESTART_REASON_FORCED_SILENT		= 0x0c,
	PON_RESTART_REASON_FORCED_NON_SILENT	= 0x0d,
};

#if IS_ENABLED(CONFIG_INPUT_QPNP_POWER_ON)
#if (LINUX_VERSION_CODE <= KERNEL_VERSION(6, 6, 0))
int qpnp_pon_system_pwr_off(enum pon_power_off_type type);
#endif
int qpnp_pon_is_warm_reset(void);
int qpnp_pon_trigger_config(enum pon_trigger_source pon_src, bool enable);
#if (LINUX_VERSION_CODE <= KERNEL_VERSION(6, 6, 0))
int qpnp_pon_wd_config(bool enable);
#endif
int qpnp_pon_set_restart_reason(enum pon_restart_reason reason);
bool qpnp_pon_check_hard_reset_stored(void);
int qpnp_pon_modem_pwr_off(enum pon_power_off_type type);

#else

#if (LINUX_VERSION_CODE <= KERNEL_VERSION(6, 6, 0))
static int qpnp_pon_system_pwr_off(enum pon_power_off_type type)
{
	return -ENODEV;
}
#endif
static inline int qpnp_pon_is_warm_reset(void)
{
	return -ENODEV;
}

static inline int qpnp_pon_trigger_config(enum pon_trigger_source pon_src,
							bool enable)
{
	return -ENODEV;
}
#if (LINUX_VERSION_CODE <= KERNEL_VERSION(6, 6, 0))
int qpnp_pon_wd_config(bool enable)
{
	return -ENODEV;
}
#endif
static inline int qpnp_pon_set_restart_reason(enum pon_restart_reason reason)
{
	return -ENODEV;
}

static inline bool qpnp_pon_check_hard_reset_stored(void)
{
	return false;
}

static inline int qpnp_pon_modem_pwr_off(enum pon_power_off_type type)
{
	return -ENODEV;
}

#endif

#endif

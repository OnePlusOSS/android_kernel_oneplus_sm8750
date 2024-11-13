// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2019, 2021 The Linux Foundation. All rights reserved.
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/err.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/reboot.h>
#include <linux/pm.h>
#include <linux/of_address.h>
#include <linux/nvmem-consumer.h>

struct qcom_reboot_reason {
	struct device *dev;
	struct notifier_block reboot_nb;
	struct nvmem_cell *nvmem_cell;
};

struct poweroff_reason {
	const char *cmd;
	unsigned char pon_reason;
};

static struct poweroff_reason reasons[] = {
	{ "recovery",			0x01 },
	{ "bootloader",			0x02 },
	{ "rtc",			0x03 },
	{ "dm-verity device corrupted",	0x04 },
	{ "dm-verity enforcing",	0x05 },
	{ "keys clear",			0x06 },
#ifdef OPLUS_OEM_BOOT_MODE
	{ "silence",			0x21 },//PON_RESTART_REASON_SILENCE= 0x21,
	{ "sau",			0x22 },//PON_RESTART_REASON_SAU= 0x22,
	{ "rf", 				0x23 },//PON_RESTART_REASON_RF= 0x23,
	{ "wlan",			0x24 },//PON_RESTART_REASON_WLAN= 0x24,
#ifdef USE_MOS_MODE
	{ "mos",			0x25 },//PON_RESTART_REASON_MOS= 0x25,
#endif
	{ "ftm",			0x26 },//PON_RESTART_REASON_FACTORY= 0x26,
	{ "kernel", 		0x27 },//PON_RESTART_REASON_KERNEL= 0x27,
	{ "modem",			0x28 },//PON_RESTART_REASON_MODEM= 0x28,
	{ "android",			0x29 },//PON_RESTART_REASON_ANDROID= 0x29,
	{ "safe",			0x2A },//PON_RESTART_REASON_SAFE= 0x2A,
	//#ifdef OPLUS_FEATURE_AGINGTEST
	{ "sbllowmemtest",		0x2B },///PON_RESTART_REASON_SBL_DDRTEST= 0x2B,
	{ "sblmemtest", 		0x2C },///PON_RESTART_REASON_SBL_DDR_CUS= 0x2C,
	{ "usermemaging",		0x2D },///PON_RESTART_REASON_MEM_AGING= 0x2D,
	/*0x2E is SBLTEST FAIL, just happen in ddrtest fail when xbl setup*/
	//#endif/*OPLUS_FEATURE_AGINGTEST*/
	{ "other",			0x3E },//PON_RESTART_REASON_NORMAL= 0x3E,
#endif /*OPLUS_OEM_BOOT_MODE*/
	{}
};

static int qcom_reboot_reason_reboot(struct notifier_block *this,
				     unsigned long event, void *ptr)
{
	char *cmd = ptr;
	struct qcom_reboot_reason *reboot = container_of(this,
		struct qcom_reboot_reason, reboot_nb);
	struct poweroff_reason *reason;

	if (!cmd)
		return NOTIFY_OK;
	for (reason = reasons; reason->cmd; reason++) {
#ifdef OPLUS_OEM_BOOT_MODE
		if ((!strcmp(cmd, reason->cmd))||(!strcmp("other", reason->cmd))) {
#else
		if (!strcmp(cmd, reason->cmd)) {
#endif
			nvmem_cell_write(reboot->nvmem_cell,
					 &reason->pon_reason,
					 sizeof(reason->pon_reason));
			break;
		}
	}

	return NOTIFY_OK;
}

static int qcom_reboot_reason_probe(struct platform_device *pdev)
{
	struct qcom_reboot_reason *reboot;

	reboot = devm_kzalloc(&pdev->dev, sizeof(*reboot), GFP_KERNEL);
	if (!reboot)
		return -ENOMEM;

	reboot->dev = &pdev->dev;

	reboot->nvmem_cell = nvmem_cell_get(reboot->dev, "restart_reason");

	if (IS_ERR(reboot->nvmem_cell))
		return PTR_ERR(reboot->nvmem_cell);

	reboot->reboot_nb.notifier_call = qcom_reboot_reason_reboot;
	reboot->reboot_nb.priority = 255;
	register_reboot_notifier(&reboot->reboot_nb);

	platform_set_drvdata(pdev, reboot);

	return 0;
}

static int qcom_reboot_reason_remove(struct platform_device *pdev)
{
	struct qcom_reboot_reason *reboot = platform_get_drvdata(pdev);

	unregister_reboot_notifier(&reboot->reboot_nb);

	return 0;
}

static const struct of_device_id of_qcom_reboot_reason_match[] = {
	{ .compatible = "qcom,reboot-reason", },
	{},
};
MODULE_DEVICE_TABLE(of, of_qcom_reboot_reason_match);

static struct platform_driver qcom_reboot_reason_driver = {
	.probe = qcom_reboot_reason_probe,
	.remove = qcom_reboot_reason_remove,
	.driver = {
		.name = "qcom-reboot-reason",
		.of_match_table = of_match_ptr(of_qcom_reboot_reason_match),
	},
};

module_platform_driver(qcom_reboot_reason_driver);

MODULE_DESCRIPTION("MSM Reboot Reason Driver");
MODULE_LICENSE("GPL");

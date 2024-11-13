# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the Linux Kernel SOC specific device drivers.
#

obj-y				+= apple/
obj-y				+= aspeed/
obj-$(CONFIG_ARCH_AT91)		+= atmel/
obj-y				+= bcm/
obj-$(CONFIG_SOC_CANAAN)	+= canaan/
obj-$(CONFIG_ARCH_DOVE)		+= dove/
obj-$(CONFIG_MACH_DOVE)		+= dove/
obj-y				+= fsl/
obj-y				+= fujitsu/
obj-$(CONFIG_ARCH_GEMINI)	+= gemini/
obj-y				+= hisilicon/
obj-y				+= imx/
obj-y				+= ixp4xx/
obj-$(CONFIG_SOC_XWAY)		+= lantiq/
obj-$(CONFIG_LITEX_SOC_CONTROLLER) += litex/
obj-y				+= loongson/
obj-y				+= mediatek/
obj-y				+= microchip/
obj-y				+= nuvoton/
obj-y				+= pxa/
obj-y				+= amlogic/
obj-y				+= qcom/
obj-y				+= renesas/
obj-y				+= rockchip/
obj-$(CONFIG_SOC_SAMSUNG)	+= samsung/
obj-y				+= sifive/
obj-y				+= sunxi/
obj-$(CONFIG_ARCH_TEGRA)	+= tegra/
obj-y				+= ti/
obj-$(CONFIG_ARCH_U8500)	+= ux500/
obj-$(CONFIG_PLAT_VERSATILE)	+= versatile/
obj-y				+= xilinx/

#ifdef OPLUS_FEATURE_MM_FEEDBACK
obj-$(CONFIG_OPLUS_FEATURE_MM_FEEDBACK)	+= oplus/multimedia/
#endif /* OPLUS_FEATURE_MM_FEEDBACK */

#ifdef OPLUS_TRACKPOINT_REPORT
obj-$(CONFIG_OPLUS_TRACKPOINT_REPORT)	+= oplus/trackpoint/
#endif /* OPLUS_TRACKPOINT_REPORT */

#ifdef OPLUS_FEATURE_BOOT
obj-y += oplus/boot/
#endif /* OPLUS_FEATURE_BOOT */

#ifdef OPLUS_FEATURE_MODEM_MINIDUMP
obj-$(CONFIG_OPLUS_FEATURE_RECORD_MDMRST)     += oplus/mdmrst/
#endif /*OPLUS_FEATURE_MODEM_MINIDUMP*/

#ifdef OPLUS_FEATURE_TP_BASIC
obj-$(CONFIG_OPLUS_DEVICE_IFNO)  +=  oplus/device_info/
#endif /* OPLUS_FEATURE_TP_BASIC */
obj-$(CONFIG_OPLUS_FEATURE_QCOM_WATCHDOG) += oplus/boot/qcom_watchdog/

#ifdef OPLUS_UFS_DRIVER
obj-$(CONFIG_OPLUS_UFS_DRIVER) += oplus/storage/common/ufs_oplus_dbg/
obj-$(CONFIG_OPLUS_UFS_DRIVER) += oplus/storage/common/oplus_uprobe/
obj-$(CONFIG_OPLUS_UFS_DRIVER) += oplus/storage/common/storage_log/
obj-$(CONFIG_OPLUS_UFS_DRIVER) += oplus/storage/common/oplus_f2fslog_storage/
obj-$(CONFIG_OPLUS_UFS_DRIVER) += oplus/storage/common/wq_dynamic_priority/
#endif

#ifdef CONFIG_OPLUS_FEATURE_STORAGE_IO_METRICS
obj-$(CONFIG_OPLUS_FEATURE_STORAGE_IO_METRICS) += oplus/storage/common/io_metrics/
#endif

#ifdef OPLUS_FEATURE_DFT
obj-y += oplus/dft/
#endif /* OPLUS_FEATURE_DFT */

#ifdef OPLUS_SUBSYS_SLEEP_MONITOR
obj-$(CONFIG_OPLUS_SUBSYS_SLEEP_MONITOR)     += oplus/power/subsys_sleep_monitor/
#endif /* OPLUS_SUBSYS_SLEEP_MONITOR */

#ifdef OPLUS_FEATURE_FPGA
obj-$(CONFIG_OPLUS_FPGA_NOTIFY)     += oplus/fpga_notify/
#endif /* OPLUS_FEATURE_DFT */
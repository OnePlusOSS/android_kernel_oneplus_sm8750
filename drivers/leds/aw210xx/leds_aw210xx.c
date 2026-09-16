/*
 * leds-aw210xx.c
 *
 * Copyright (c) 2021 AWINIC Technology CO., LTD
 *
 *  Author: hushanping <hushanping@awinic.com>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/i2c.h>
#include <linux/of_gpio.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/firmware.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/version.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/debugfs.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>
#include <linux/leds.h>
#include <soc/oplus/system/oplus_project.h>
#include <linux/workqueue.h>
#include <linux/regulator/consumer.h>
#include <soc/oplus/boot/boot_mode.h>
#ifdef CONFIG_OPLUS_CHARGER_MTK
#include <mt-plat/mtk_boot_common.h>
#endif

#include "leds_aw210xx.h"
//#include "leds_aw210xx_reg.h"

#define DLED_NUM 2
#define DLED_RGB_NUM 3

static struct aw210xx *aw210xx_g_chip[DLED_NUM][DLED_RGB_NUM] = {{NULL,NULL,NULL}, {NULL,NULL,NULL}};
struct aw210xx *aw210xx_glo;
static int max_led = 0;
static int workqueue_flag = 0;
//static int dec_flag = 1;
static int ledbri[5] = {0};
//static int led_esd_color[3][4] = {0};
static int load_num = 0;
static int music_rate = 300;
static int music_timer = 0;
static int brightness_ratio = 255;
static int aw210xx_hw_enable(struct aw210xx *aw210xx, bool flag);
static int aw210xx_led_init(struct aw210xx *aw210xx);
static int aw210xx_led_change_mode(struct aw210xx *led, enum AW2023_LED_MODE mode);
static int aw210xx_read_chipid(struct aw210xx *aw210xx);
void reset_led(void);

/******************************************************
 *
 * Marco
 *
 ******************************************************/
#define AW210XX_DRIVER_VERSION "V0.4.0"
#define AW_I2C_RETRIES 5
#define AW_I2C_RETRY_DELAY 1
#define AW_READ_CHIPID_RETRIES 2
#define AW_READ_CHIPID_RETRY_DELAY 1
#define AW_START_TO_BREATH 200
#define LED_ESD_WORK_TIME					3

#define R_ISNK_ON_MASK					0x04
#define G_ISNK_ON_MASK					0x02
#define B_ISNK_ON_MASK					0x01

#define LED_SUPPORT_TYPE					"support"
// #define BLINK_USE_AW210XX
static int workqueue_flow_flag = 0;
struct aw210xx *led_default;

void run_flow_effect(struct aw210xx *aw210xx);
void run_rainbow_effect(struct aw210xx *aw210xx);

static bool oplus_boot_mode_is_power_off_charging(void)
{
#ifdef CONFIG_OPLUS_CHARGER_MTK
	if (get_boot_mode() == KERNEL_POWER_OFF_CHARGING_BOOT) {
		return true;
	} else {
		return false;
	}
#else
	return qpnp_is_power_off_charging();
#endif
}

/******************************************************
 *
 * aw210xx i2c write/read
 *
 ******************************************************/

static int aw210xx_i2c_write(struct aw210xx *aw210xx,
		unsigned char reg_addr, unsigned char reg_data)
{
	int ret = -1;
	unsigned char cnt = 0;
	//AW_ERR("MTC_LOG: enter aw210xx_i2c_write function\n");
	if (aw210xx == NULL) {
		AW_ERR("aw210xx_i2c_write aw210xx is NULL \n");
		return 0;
	}

	while (cnt < AW_I2C_RETRIES) {
		ret = i2c_smbus_write_byte_data(aw210xx->i2c,
				reg_addr, reg_data);
		if (ret < 0)
			AW_ERR("i2c_write fail cnt=%d ret=%d addr=0x%x data=0x%x id:%x rgbid:%d \n", cnt, ret, reg_addr, reg_data, aw210xx->aw210xx_led_id, aw210xx->id);
		else {
			//AW_ERR("i2c_write suc cnt=%d ret=%d addr=0x%x data=0x%x id:%x rgbid:%d \n", cnt, ret, reg_addr, reg_data, aw210xx->aw210xx_led_id, aw210xx->id);
			break;
		}
		cnt++;
		//AW_ERR("MTC_LOG: end i2c_smbus_write_byte_data function\n");
		usleep_range(AW_I2C_RETRY_DELAY * 1000,
				AW_I2C_RETRY_DELAY * 1000 + 500);
	}
	if (ret < 0)
	{
		dump_stack();
	}
	//AW_ERR("MTC_LOG: end aw210xx_i2c_write function\n");
	return ret;
}

static int aw210xx_i2c_read(struct aw210xx *aw210xx,
		unsigned char reg_addr, unsigned char *reg_data)
{
	int ret = -1;
	unsigned char cnt = 0;

	if (aw210xx == NULL) {
		AW_ERR("aw210xx_i2c_read aw210xx is NULL \n");
		return 0;
	}

	while (cnt < AW_I2C_RETRIES) {
		ret = i2c_smbus_read_byte_data(aw210xx->i2c, reg_addr);
		if (ret < 0) {
			AW_ERR("i2c_read fail cnt=%d ret=%d addr=0x%x id:%x \n", cnt, ret, reg_addr, aw210xx->aw210xx_led_id);
		} else {
			*reg_data = ret;
			//AW_ERR("i2c_read suc cnt=%d ret=%d addr=0x%x id:%x \n", cnt, ret, reg_addr, aw210xx->aw210xx_led_id);
			break;
		}
		cnt++;
		usleep_range(AW_I2C_RETRY_DELAY * 1000,
				AW_I2C_RETRY_DELAY * 1000 + 500);
	}

	return ret;
}

static int aw210xx_i2c_write_bits(struct aw210xx *aw210xx,
		unsigned char reg_addr, unsigned int mask,
		unsigned char reg_data)
{
	unsigned char reg_val;

	aw210xx_i2c_read(aw210xx, reg_addr, &reg_val);
	reg_val &= mask;
	reg_val |= (reg_data & (~mask));
	aw210xx_i2c_write(aw210xx, reg_addr, reg_val);

	return 0;
}

void aw210xx_uvlo_set(struct aw210xx *aw210xx, bool flag)
{
	if (flag) {
		aw210xx_i2c_write_bits(aw210xx,
				AW210XX_REG_UVCR,
				AW210XX_BIT_UVPD_MASK,
				AW210XX_BIT_UVPD_DISENA);
		aw210xx_i2c_write_bits(aw210xx,
				AW210XX_REG_UVCR,
				AW210XX_BIT_UVDIS_MASK,
				AW210XX_BIT_UVDIS_DISENA);
	} else {
		aw210xx_i2c_write_bits(aw210xx,
				AW210XX_REG_UVCR,
				AW210XX_BIT_UVPD_MASK,
				AW210XX_BIT_UVPD_ENABLE);
		aw210xx_i2c_write_bits(aw210xx,
				AW210XX_REG_UVCR,
				AW210XX_BIT_UVDIS_MASK,
				AW210XX_BIT_UVDIS_ENABLE);
	}
}

void aw210xx_sbmd_set(struct aw210xx *aw210xx, bool flag)
{
	if (flag) {
		aw210xx_i2c_write_bits(aw210xx,
				AW210XX_REG_GCR2,
				AW210XX_BIT_SBMD_MASK,
				AW210XX_BIT_SBMD_ENABLE);
		aw210xx->sdmd_flag = 1;
	} else {
		aw210xx_i2c_write_bits(aw210xx,
				AW210XX_REG_GCR2,
				AW210XX_BIT_SBMD_MASK,
				AW210XX_BIT_SBMD_DISENA);
		aw210xx->sdmd_flag = 0;
	}
}

void aw210xx_rgbmd_set(struct aw210xx *aw210xx, bool flag)
{
	if (flag) {
		aw210xx_i2c_write_bits(aw210xx,
				AW210XX_REG_GCR2,
				AW210XX_BIT_RGBMD_MASK,
				AW210XX_BIT_RGBMD_ENABLE);
		aw210xx->rgbmd_flag = 1;
	} else {
		aw210xx_i2c_write_bits(aw210xx,
				AW210XX_REG_GCR2,
				AW210XX_BIT_RGBMD_MASK,
				AW210XX_BIT_RGBMD_DISENA);
		aw210xx->rgbmd_flag = 0;
	}
}

void aw210xx_apse_set(struct aw210xx *aw210xx, bool flag)
{
	if (flag) {
		aw210xx_i2c_write_bits(aw210xx,
				AW210XX_REG_GCR,
				AW210XX_BIT_APSE_MASK,
				AW210XX_BIT_APSE_ENABLE);
	} else {
		aw210xx_i2c_write_bits(aw210xx,
				AW210XX_REG_GCR,
				AW210XX_BIT_APSE_MASK,
				AW210XX_BIT_APSE_DISENA);
	}
}

/*****************************************************
* aw210xx led function set
*****************************************************/
int32_t aw210xx_osc_pwm_set(struct aw210xx *aw210xx)
{
	switch (aw210xx->osc_clk) {
	case CLK_FRQ_16M:
		AW_LOG("osc is 16MHz!\n");
		aw210xx_i2c_write_bits(aw210xx,
				AW210XX_REG_GCR,
				AW210XX_BIT_CLKFRQ_MASK,
				AW210XX_BIT_CLKFRQ_16MHz);
		break;
	case CLK_FRQ_8M:
		AW_LOG("osc is 8MHz!\n");
		aw210xx_i2c_write_bits(aw210xx,
				AW210XX_REG_GCR,
				AW210XX_BIT_CLKFRQ_MASK,
				AW210XX_BIT_CLKFRQ_8MHz);
		break;
	case CLK_FRQ_1M:
		AW_LOG("osc is 1MHz!\n");
		aw210xx_i2c_write_bits(aw210xx,
				AW210XX_REG_GCR,
				AW210XX_BIT_CLKFRQ_MASK,
				AW210XX_BIT_CLKFRQ_1MHz);
		break;
	case CLK_FRQ_512k:
		AW_LOG("osc is 512KHz!\n");
		aw210xx_i2c_write_bits(aw210xx,
				AW210XX_REG_GCR,
				AW210XX_BIT_CLKFRQ_MASK,
				AW210XX_BIT_CLKFRQ_512kHz);
		break;
	case CLK_FRQ_256k:
		AW_LOG("osc is 256KHz!\n");
		aw210xx_i2c_write_bits(aw210xx,
				AW210XX_REG_GCR,
				AW210XX_BIT_CLKFRQ_MASK,
				AW210XX_BIT_CLKFRQ_256kHz);
		break;
	case CLK_FRQ_125K:
		AW_LOG("osc is 125KHz!\n");
		aw210xx_i2c_write_bits(aw210xx,
				AW210XX_REG_GCR,
				AW210XX_BIT_CLKFRQ_MASK,
				AW210XX_BIT_CLKFRQ_125kHz);
		break;
	case CLK_FRQ_62_5K:
		AW_LOG("osc is 62.5KHz!\n");
		aw210xx_i2c_write_bits(aw210xx,
				AW210XX_REG_GCR,
				AW210XX_BIT_CLKFRQ_MASK,
				AW210XX_BIT_CLKFRQ_62_5kHz);
		break;
	case CLK_FRQ_31_25K:
		AW_LOG("osc is 31.25KHz!\n");
		aw210xx_i2c_write_bits(aw210xx,
				AW210XX_REG_GCR,
				AW210XX_BIT_CLKFRQ_MASK,
				AW210XX_BIT_CLKFRQ_31_25kHz);
		break;
	default:
		AW_LOG("this clk_pwm is unsupported!\n");
		return -AW210XX_CLK_MODE_UNSUPPORT;
	}

	return 0;
}

int32_t aw210xx_br_res_set(struct aw210xx *aw210xx)
{
	switch (aw210xx->br_res) {
	case BR_RESOLUTION_8BIT:
		AW_LOG("br resolution select 8bit!\n");
		aw210xx_i2c_write_bits(aw210xx,
				AW210XX_REG_GCR,
				AW210XX_BIT_PWMRES_MASK,
				AW210XX_BIT_PWMRES_8BIT);
		break;
	case BR_RESOLUTION_9BIT:
		AW_LOG("br resolution select 9bit!\n");
		aw210xx_i2c_write_bits(aw210xx,
				AW210XX_REG_GCR,
				AW210XX_BIT_PWMRES_MASK,
				AW210XX_BIT_PWMRES_9BIT);
		break;
	case BR_RESOLUTION_12BIT:
		AW_LOG("br resolution select 12bit!\n");
		aw210xx_i2c_write_bits(aw210xx,
				AW210XX_REG_GCR,
				AW210XX_BIT_PWMRES_MASK,
				AW210XX_BIT_PWMRES_12BIT);
		break;
	case BR_RESOLUTION_9_AND_3_BIT:
		AW_LOG("br resolution select 9+3bit!\n");
		aw210xx_i2c_write_bits(aw210xx,
				AW210XX_REG_GCR,
				AW210XX_BIT_PWMRES_MASK,
				AW210XX_BIT_PWMRES_9_AND_3_BIT);
		break;
	default:
		AW_LOG("this br_res is unsupported!\n");
		return -AW210XX_CLK_MODE_UNSUPPORT;
	}

	return 0;
}

/*****************************************************
* aw210xx debug interface set
*****************************************************/
static void aw210xx_update(struct aw210xx *aw210xx)
{
	aw210xx_i2c_write(aw210xx, AW210XX_REG_UPDATE, AW210XX_UPDATE_BR_SL);
}

void aw210xx_global_set(struct aw210xx *aw210xx)
{
	if (aw210xx->light_sensor_state) {
		aw210xx_i2c_write(aw210xx, AW210XX_REG_GCCR, aw210xx->glo_current_max);
	}
	else {
		aw210xx_i2c_write(aw210xx, AW210XX_REG_GCCR, aw210xx->glo_current_min);
	}
}

void aw210xx_current_set(struct aw210xx *aw210xx)
{
	if (aw210xx->light_sensor_state) {
		aw210xx_i2c_write(aw210xx, AW210XX_REG_GCCR, aw210xx->glo_current_min);
	}
	else {
		aw210xx_i2c_write(aw210xx, AW210XX_REG_GCCR, aw210xx->glo_current_max);
	}
}

/*********************************************************
 *
 * light effect
 *
 ********************************************************/
 /*初始化四颗灯，每科灯需要的结构体数据*/
void aw210xx_rgb_multi_breath_init(const AW_MULTI_BREATH_DATA_STRUCT *data, effect_select_t effect)
{
	unsigned char i;

	aw210xx_interface[effect].getBrightnessfunc = aw210xx_get_breath_brightness_algo_func(BREATH_ALGO_GAMMA_CORRECTION);//leds_aw210xx.h定义AW_COLORFUL_INTERFACE_STRUCT aw210xx_interface；aw_breath_algorithm.h中定义了该函数getBrightnessfunc
	algo_data[effect].cur_frame = 0;//aw_lamp_interface.h中定义了algo_data
	algo_data[effect].total_frames = 20;
	for (i = 0; i < 3; i++) {
		algo_data[effect].data_start[i] = 0;
		algo_data[effect].data_end[i] = 0;
	}
	aw210xx_interface[effect].p_algo_data = &algo_data[effect];
	AW_ERR("MTC_LOG: enter %s\n", __func__);

	for (i = 0; i < RGB_NUM; i++) { //RGB_NUM is 4，表示4颗RGB灯
		colorful_cur_frame[effect][i] = 0;//leds_aw210xx.h中定义了
		colorful_total_frames[effect][i] = 20;//leds_aw210xx.h中定义了
		colorful_cur_color_index[effect][i] = 0;//leds_aw210xx.h中定义了
		colorful_cur_phase[effect][i] = 0;//leds_aw210xx.h中定义了
		colorful_phase_nums[effect][i] = 5;
		breath_cur_phase[effect][i] = 0;
		breath_phase_nums[effect][i] = 6;
		aw210xx_algo_data[effect][i].cur_frame = 0;//leds_aw210xx.h中定义ALGO_DATA_STRUCT aw210xx_algo_data[RGB_NUM]，每一颗灯有有一个该变量
		aw210xx_algo_data[effect][i].total_frames = (data[i].time[0] + data[i].frame_factor - 1) / data[i].frame_factor + 1;
		aw210xx_algo_data[effect][i].data_start[0] = data[i].fadel[0].r;
		aw210xx_algo_data[effect][i].data_end[0] = data[i].fadeh[0].r;
		aw210xx_algo_data[effect][i].data_start[1] = data[i].fadel[0].g;
		aw210xx_algo_data[effect][i].data_end[1] = data[i].fadeh[0].g;
		aw210xx_algo_data[effect][i].data_start[2] = data[i].fadel[0].b;
		aw210xx_algo_data[effect][i].data_end[2] = data[i].fadeh[0].b;
		source_color[effect][i].r = 0x00;
		source_color[effect][i].g = 0x00;
		source_color[effect][i].b = 0x00;
		destination_color[effect][i].r = 0x00;
		destination_color[effect][i].g = 0x00;
		destination_color[effect][i].b = 0x00;
		loop_end[effect][i] = 0;
		breath_cur_loop[effect][i]=0;
	}
}

void aw210xx_update_frame_idx( AW_MULTI_BREATH_DATA_STRUCT *data, effect_select_t effect)
{
	unsigned char i;
	int update_frame_idx = 0;
    int index,dest_index;
    
	//AW_ERR("MTC_LOG: enter %s\n", __func__);
	for (i = 0; i < RGB_NUM; i++) {
		update_frame_idx = 1;
		if (loop_end[effect][i] == 1) //leds_aw210xx.h中定义
			continue;

		aw210xx_algo_data[effect][i].cur_frame++;
		if (aw210xx_algo_data[effect][i].cur_frame >= aw210xx_algo_data[effect][i].total_frames) {
			aw210xx_algo_data[effect][i].cur_frame = 0;
			breath_cur_phase[effect][i]++;//leds_aw210xx.h中定义了该变量
			if(data[i].color_nums == 5){
				if(breath_cur_phase[effect][i] == 6){
					if(0 == data[i].repeat_nums){
							breath_cur_phase[effect][i] = 0;
							breath_cur_loop[effect][i] = 0;
						}
						else if(breath_cur_loop[effect][i] < data[i].repeat_nums - 1){
							breath_cur_phase[effect][i] = 0;
							breath_cur_loop[effect][i]++;
						}
				}
			}else{
				if(breath_cur_phase[effect][i] == 5){
					if(0 == data[i].repeat_nums){
						breath_cur_phase[effect][i] = 1;
						breath_cur_loop[effect][i] = 0;//leds_aw210xx.h中定义了该变量
					}
					else if(breath_cur_loop[effect][i] < data[i].repeat_nums - 1){
						breath_cur_phase[effect][i] = 1;
						breath_cur_loop[effect][i]++;
					}
				}
			}
			if (breath_cur_phase[effect][i] >= breath_phase_nums[effect][i]) {
				breath_cur_phase[effect][i] = 0;
				update_frame_idx = 0;
			}

			if (update_frame_idx) {
				aw210xx_algo_data[effect][i].total_frames =
					(data[i].time[breath_cur_phase[effect][i]])/data[i].frame_factor + 1;
				if(aw210xx_algo_data[effect][i].total_frames == 1){
					continue;
				}
				if (breath_cur_phase[effect][i] == 1) {
					aw210xx_algo_data[effect][i].data_start[0] = data[i].fadel[0].r;
					aw210xx_algo_data[effect][i].data_end[0] = data[i].fadeh[0].r;
					aw210xx_algo_data[effect][i].data_start[1] = data[i].fadel[0].g;
					aw210xx_algo_data[effect][i].data_end[1] = data[i].fadeh[0].g;
					aw210xx_algo_data[effect][i].data_start[2] = data[i].fadel[0].b;
					aw210xx_algo_data[effect][i].data_end[2] = data[i].fadeh[0].b;
				} else if (breath_cur_phase[effect][i] == 2) {
					aw210xx_algo_data[effect][i].data_start[0] = data[i].fadeh[0].r;
					aw210xx_algo_data[effect][i].data_end[0] = data[i].fadeh[0].r;
					aw210xx_algo_data[effect][i].data_start[1] = data[i].fadeh[0].g;
					aw210xx_algo_data[effect][i].data_end[1] = data[i].fadeh[0].g;
					aw210xx_algo_data[effect][i].data_start[2] = data[i].fadeh[0].b;
					aw210xx_algo_data[effect][i].data_end[2] = data[i].fadeh[0].b;
				} else if (breath_cur_phase[effect][i] == 3) {
					aw210xx_algo_data[effect][i].data_start[0] = data[i].fadeh[0].r;
					aw210xx_algo_data[effect][i].data_end[0] = data[i].fadel[0].r;
					aw210xx_algo_data[effect][i].data_start[1] = data[i].fadeh[0].g;
					aw210xx_algo_data[effect][i].data_end[1] = data[i].fadel[0].g;
					aw210xx_algo_data[effect][i].data_start[2] = data[i].fadeh[0].b;
					aw210xx_algo_data[effect][i].data_end[2] = data[i].fadel[0].b;
				} else {
					aw210xx_algo_data[effect][i].data_start[0] = data[i].fadel[0].r;
					aw210xx_algo_data[effect][i].data_end[0] = data[i].fadel[0].r;
					aw210xx_algo_data[effect][i].data_start[1] = data[i].fadel[0].g;
					aw210xx_algo_data[effect][i].data_end[1] = data[i].fadel[0].g;
					aw210xx_algo_data[effect][i].data_start[2] = data[i].fadel[0].b;
					aw210xx_algo_data[effect][i].data_end[2] = data[i].fadel[0].b;
				}
				/* breath_cur_phase[i]++; */
			} else {
				aw210xx_algo_data[effect][i].cur_frame = 0;
				aw210xx_algo_data[effect][i].total_frames = 1;
				aw210xx_algo_data[effect][i].data_start[0] = 0;
				aw210xx_algo_data[effect][i].data_end[0] = 0;
				aw210xx_algo_data[effect][i].data_start[1] = 0;
				aw210xx_algo_data[effect][i].data_end[1] = 0;
				aw210xx_algo_data[effect][i].data_start[2] = 0;
				aw210xx_algo_data[effect][i].data_end[2] = 0;
				loop_end[effect][i] = 1;
			}
		}

		if(data[i].color_nums == 1){
			source_color[effect][i].r = destination_color[effect][i].r;
			source_color[effect][i].g = destination_color[effect][i].g;
			source_color[effect][i].b = destination_color[effect][i].b;
			destination_color[effect][i].r = data[i].rgb_color_list[0].r;
			destination_color[effect][i].g = data[i].rgb_color_list[0].g;
			destination_color[effect][i].b = data[i].rgb_color_list[0].b;		
			
		}else if(data[i].color_nums == 9){
			/*source_color[i].r = data[i].rgb_color_list[breath_cur_loop[i]].r;
			source_color[i].g = data[i].rgb_color_list[breath_cur_loop[i]].g;
			source_color[i].b = data[i].rgb_color_list[breath_cur_loop[i]].b;
			destination_color[i].r = data[i].rgb_color_list[breath_cur_loop[i]+1].r;
			destination_color[i].g = data[i].rgb_color_list[breath_cur_loop[i]+1].g;
			destination_color[i].b = data[i].rgb_color_list[breath_cur_loop[i]+1].b;*/
			index = (i+breath_cur_loop[effect][i])% (RGB_NUM);
			dest_index = (index+1)%RGB_NUM;
			source_color[effect][i].r = data[i].rgb_color_list[index].r;
			source_color[effect][i].g = data[i].rgb_color_list[index].g;
			source_color[effect][i].b = data[i].rgb_color_list[index].b;
			destination_color[effect][i].r = data[i].rgb_color_list[dest_index].r;
			destination_color[effect][i].g = data[i].rgb_color_list[dest_index].g;
			destination_color[effect][i].b = data[i].rgb_color_list[dest_index].b;
		}else{
			source_color[effect][i].r = destination_color[effect][i].r;
			source_color[effect][i].g = destination_color[effect][i].g;
			source_color[effect][i].b = destination_color[effect][i].b;
			if(breath_cur_loop[effect][i] < data[i].color_nums)
			{
				destination_color[effect][i].r = data[i].rgb_color_list[breath_cur_loop[effect][i]].r;
				destination_color[effect][i].g = data[i].rgb_color_list[breath_cur_loop[effect][i]].g;
				destination_color[effect][i].b = data[i].rgb_color_list[breath_cur_loop[effect][i]].b;
			}else
			{
				destination_color[effect][i].r = data[i].rgb_color_list[breath_cur_loop[effect][i]%data[i].color_nums].r;
				destination_color[effect][i].g = data[i].rgb_color_list[breath_cur_loop[effect][i]%data[i].color_nums].g;
				destination_color[effect][i].b = data[i].rgb_color_list[breath_cur_loop[effect][i]%data[i].color_nums].b;
			}
			
		}

//		AW_ERR(" rgb = %d, ufi = %d, breath_cur_phase[%d] = %d, breath_cur_loop = %d, \n",i,update_frame_idx, i, breath_cur_phase[i], breath_cur_loop[i]);
//		AW_ERR(" rgb = %d, cur_frame = %d, total_frames = %d, data_start = %d, data_end= %d \n",i,aw210xx_algo_data[i].cur_frame, aw210xx_algo_data[i].total_frames, aw210xx_algo_data[i].data_start, aw210xx_algo_data[i].data_end);
		
	}
}

void aw210xx_frame_display(effect_select_t effect)
{
	unsigned char i = 0;
	unsigned char brightness[3] = {0};
	//AW_ERR("MTC_LOG: enter %s\n", __func__);

	for (i = 0; i < RGB_NUM; i++) {
		aw210xx_interface[effect].p_color_1 = &source_color[effect][i];////aw_lamp_interface.h中定义aw210xx_interface,leds_aw210xx_reg.h中定义soruce_color
		aw210xx_interface[effect].p_color_2 = &destination_color[effect][i];//leds_aw210xx.h中定义
		aw210xx_interface[effect].cur_frame = aw210xx_algo_data[effect][i].cur_frame;
		aw210xx_interface[effect].total_frames = aw210xx_algo_data[effect][i].total_frames;
		if(aw210xx_interface[effect].total_frames > 1){
			aw210xx_set_colorful_rgb_data(i, dim_data[effect], &aw210xx_interface[effect]);//aw_lamp_interface.h中定义
			if (breath_cur_phase[effect][i] == 0)
			{
				brightness[0] = 0;
				brightness[1] = 0;
				brightness[2] = 0;
			}
			else {
				brightness[0] = aw210xx_interface[effect].getBrightnessfunc(&aw210xx_algo_data[effect][i],0);//返回一个start_index，貌似是初始亮度
				brightness[1] = aw210xx_interface[effect].getBrightnessfunc(&aw210xx_algo_data[effect][i],1);//返回一个start_index，貌似是初始亮度
				brightness[2] = aw210xx_interface[effect].getBrightnessfunc(&aw210xx_algo_data[effect][i],2);//返回一个start_index，貌似是初始亮度
			}
			aw210xx_set_rgb_brightness(i, fade_data[effect], brightness);//aw_lamp_interface.h中定义
			//AW_ERR("MTC_LOG:aw210xx_frame_display, num is %d, dim_data is 0x%x, fade_data is 0x%x\n", i, dim_data[i], fade_data[i]);
		}
	}
}

void aw210xx_update_effect(struct aw210xx *aw210xx,effect_select_t effect)
{
	unsigned char i = 0,j=0;
	int ret = -1;

	ret = aw210xx_read_chipid(aw210xx);
	if (ret < 0) {
		reset_led();
		AW_ERR("aw210xx_led retry over\n");
	}

	//AW_ERR("MTC_LOG:pepare to aw210xx_update\n");
	for (i = 0,j=0; i < RGB_NUM; i+=2,j++) {
		/*aw210xx_col_data[j * 3 + 0] = dim_data[i * 3 + 0];
		aw210xx_br_data[j * 3 + 0] = fade_data[i * 3 + 0];
		aw210xx_col_data[j * 3 + 1] = dim_data[i * 3 + 1];
		aw210xx_br_data[j * 3 + 1] = fade_data[i * 3 + 1];
		aw210xx_col_data[j * 3 + 2] = dim_data[i * 3 + 2];
		aw210xx_br_data[j * 3 + 2] = fade_data[i * 3 + 2];*/
		aw210xx_i2c_write(aw210xx, aw210xx_reg_map[i * 6 + 0], dim_data[effect][i * 3 + 0]);
		aw210xx_i2c_write(aw210xx, aw210xx_reg_map[i * 6 + 1], fade_data[effect][i * 3 + 0]);
		aw210xx_i2c_write(aw210xx, aw210xx_reg_map[i * 6 + 2], dim_data[effect][i * 3 + 1]);
		aw210xx_i2c_write(aw210xx, aw210xx_reg_map[i * 6 + 3], fade_data[effect][i * 3 + 1]);
		aw210xx_i2c_write(aw210xx, aw210xx_reg_map[i * 6 + 4], dim_data[effect][i * 3 + 2]);
		aw210xx_i2c_write(aw210xx, aw210xx_reg_map[i * 6 + 5], fade_data[effect][i * 3 + 2]);
	}
	/*
	for (i = 0,j=0; i < RGB_NUM; i+=2,j++)  {
		aw210xx_i2c_write(aw210xx, aw210xx_reg_map[i * 6 + 1], aw210xx_br_data[j * 3 + 0]);
		aw210xx_i2c_write(aw210xx, aw210xx_reg_map[i * 6 + 3], aw210xx_br_data[j * 3 + 1]);
		aw210xx_i2c_write(aw210xx, aw210xx_reg_map[i * 6 + 5], aw210xx_br_data[j * 3 + 2]);
	}

	for (i = 0,j=0; i < RGB_NUM; i+=2,j++) {
		aw210xx_i2c_write(aw210xx, aw210xx_reg_map[i * 6 + 0], aw210xx_col_data[j * 3 + 0]);
		aw210xx_i2c_write(aw210xx, aw210xx_reg_map[i * 6 + 2], aw210xx_col_data[j * 3 + 1]);
		aw210xx_i2c_write(aw210xx, aw210xx_reg_map[i * 6 + 4], aw210xx_col_data[j * 3 + 2]);
	}*/

	for (i = 1,j=0; i < RGB_NUM; i+=2,j++) {
/*		aw210xx_col_data[j * 3 + 0] = dim_data[i * 3 + 0];
		aw210xx_br_data[j * 3 + 0] = fade_data[i * 3 + 0];
		aw210xx_col_data[j * 3 + 1] = dim_data[i * 3 + 1];
		aw210xx_br_data[j * 3 + 1] = fade_data[i * 3 + 1];
		aw210xx_col_data[j * 3 + 2] = dim_data[i * 3 + 2];
		aw210xx_br_data[j * 3 + 2] = fade_data[i * 3 + 2
*/
		aw210xx_i2c_write(aw210xx_g_chip[1][0], aw210xx_reg_map[i * 6 + 0], dim_data[effect][i * 3 + 0]);
		aw210xx_i2c_write(aw210xx_g_chip[1][0], aw210xx_reg_map[i * 6 + 1], fade_data[effect][i * 3 + 0]);
		aw210xx_i2c_write(aw210xx_g_chip[1][0], aw210xx_reg_map[i * 6 + 2], dim_data[effect][i * 3 + 1]);
		aw210xx_i2c_write(aw210xx_g_chip[1][0], aw210xx_reg_map[i * 6 + 3], fade_data[effect][i * 3 + 1]);
		aw210xx_i2c_write(aw210xx_g_chip[1][0], aw210xx_reg_map[i * 6 + 4], dim_data[effect][i * 3 + 2]);
		aw210xx_i2c_write(aw210xx_g_chip[1][0], aw210xx_reg_map[i * 6 + 5], fade_data[effect][i * 3 + 2]);
	}
/*
	for (i = 1,j=0; i < RGB_NUM; i+=2,j++)  {
		aw210xx_i2c_write(aw210xx_g_chip[1][0], aw210xx_reg_map[i * 6 + 1], aw210xx_br_data[j * 3 + 0]);
		aw210xx_i2c_write(aw210xx_g_chip[1][0], aw210xx_reg_map[i * 6 + 3], aw210xx_br_data[j * 3 + 1]);
		aw210xx_i2c_write(aw210xx_g_chip[1][0], aw210xx_reg_map[i * 6 + 5], aw210xx_br_data[j * 3 + 2]);
	}

	for (i = 1,j=0; i < RGB_NUM; i+=2,j++) {
		aw210xx_i2c_write(aw210xx_g_chip[1][0], aw210xx_reg_map[i * 6 + 0], aw210xx_col_data[j * 3 + 0]);
		aw210xx_i2c_write(aw210xx_g_chip[1][0], aw210xx_reg_map[i * 6 + 2], aw210xx_col_data[j * 3 + 1]);
		aw210xx_i2c_write(aw210xx_g_chip[1][0], aw210xx_reg_map[i * 6 + 4], aw210xx_col_data[j * 3 + 2]);
	}
*/
	aw210xx_update(aw210xx);
	aw210xx_update(aw210xx_g_chip[1][0]);
}

int aw210xx_start_next_effect( effect_select_t effect,struct aw210xx *aw210xx){
		int i=0;
		unsigned char size = aw210xx_cfg_array[effect].count/RGB_NUM;
		//AW_ERR("MTC_LOG: enter %s\n", __func__);
		for(i=0;i<RGB_NUM;i++){
			if(loop_end[effect][i]==0){
				break;
			}else{
				effect_stop_val[effect]++;
			}
		}
		if( effect_stop_val[effect] >= RGB_NUM && num[effect] < size){
			num[effect]++;
			if(num[effect] >= size){
				num[effect] = 0;
				return -1;
			}
			effect_data[effect] += RGB_NUM;
			aw210xx_rgb_multi_breath_init(effect_data[effect],effect);
			aw210xx_frame_display(effect);
			aw210xx_update_effect(aw210xx,effect);
		}
		effect_stop_val[effect] = 0;
		return 0;
}

void print_effectdata(effect_select_t effect)
{
	int i =0,j =0;
	#define BUF_SIZE 200
	char buf[BUF_SIZE];
	int len=0;
	AW_ERR("DebugLog: effectindex = %d  \n", effect);
	for(i=0;i<aw210xx_cfg_array[effect].count;i++)
	{
		len=0;
		memset(buf,0,sizeof(buf));
		len += snprintf(buf + len, BUF_SIZE - len,"%d,%d,%d,", effect, i,aw210xx_cfg_array[effect].p[i].frame_factor);
		for(j=0;j<6;j++)
		{
			len += snprintf(buf + len, BUF_SIZE - len, "%d,", aw210xx_cfg_array[effect].p[i].time[j]);
		}
		len += snprintf(buf + len, BUF_SIZE - len, "%d,", aw210xx_cfg_array[effect].p[i].repeat_nums);
		len += snprintf(buf + len, BUF_SIZE - len, "%d,", aw210xx_cfg_array[effect].p[i].color_nums);
		for(j=0;j<aw210xx_cfg_array[effect].p[i].color_nums;j++)
		{
			len += snprintf(buf + len, BUF_SIZE - len, "%d,", aw210xx_cfg_array[effect].p[i].rgb_color_list[j].r);
			len += snprintf(buf + len, BUF_SIZE - len, "%d,", aw210xx_cfg_array[effect].p[i].rgb_color_list[j].g);
			len += snprintf(buf + len, BUF_SIZE - len, "%d,", aw210xx_cfg_array[effect].p[i].rgb_color_list[j].b);
		}
		len += snprintf(buf + len, BUF_SIZE - len, "%d,%d,%d,", aw210xx_cfg_array[effect].p[i].fadeh[0].r, aw210xx_cfg_array[effect].p[i].fadeh[0].g, aw210xx_cfg_array[effect].p[i].fadeh[0].b);
		len += snprintf(buf + len, BUF_SIZE - len, "%d,%d,%d,", aw210xx_cfg_array[effect].p[i].fadel[0].r, aw210xx_cfg_array[effect].p[i].fadel[0].g, aw210xx_cfg_array[effect].p[i].fadel[0].b);
		buf[--len] = 0x00;
		AW_ERR("DebugLog: %s  \n", buf);
	}
//	aw210xx_cfg_array[effect].p[dataindex].time[i]
}

int check_effect_state(effect_select_t effect)
{
	switch(effect) {
		case INCALL_EFFECT:
			if(effect_state.state[AW210XX_LED_INCALL_MODE] == 1){
				return 0;
			}
			if(effect_state.state[AW210XX_LED_INCALL_MODE] == 0){
				return -1;
			}
			break;
		case POWERON_EFFECT:
			if(effect_state.state[AW210XX_LED_POWERON_MODE] == 1){
				return 0;
			}
			if(effect_state.state[AW210XX_LED_POWERON_MODE] == 0){
				return -1;
			}
			while(effect_state.state[AW210XX_LED_POWERON_MODE] == 2)
			{
				usleep_range(100000, 120000);
				AW_ERR(" AW210XX_LED_POWERON_MODE pause\n" );
				if(effect_state.state[AW210XX_LED_CHARGE_MODE] == 1)
				{
					//TBD enable hw
					;
				}
			}
			break;
		case CHARGE_EFFECT:
			if(effect_state.state[AW210XX_LED_CHARGE_MODE] == 1){
				return 0;
			}
			if(effect_state.state[AW210XX_LED_CHARGE_MODE] == 0){
				return -1;
			}
			while(effect_state.state[AW210XX_LED_CHARGE_MODE] == 2)
			{
				usleep_range(100000, 120000);
				AW_ERR(" AW210XX_LED_CHARGE_MODE pause \n" );
				if(effect_state.state[AW210XX_LED_CHARGE_MODE] == 1)
				{
					//TBD enable hw
					;
				}
			}
			return 0;
			break;
		case GAME_ENTER_EFFECT:
			if(effect_state.state[AW210XX_LED_GAMEMODE] == 1){
				return 0;
			}
			if(effect_state.state[AW210XX_LED_GAMEMODE] == 0){
				return -1;
			}
			while(effect_state.state[AW210XX_LED_GAMEMODE] == 2)
			{
				usleep_range(100000, 120000);
				AW_ERR(" AW210XX_LED_GAMEMODE pause\n" );
				if(effect_state.state[AW210XX_LED_GAMEMODE] == 1)
				{
					//TBD enable hw
					;
				}
			}
			break;
		case NOTIFY_EFFECT:
			if(effect_state.state[AW210XX_LED_NOTIFY_MODE] == 1){
				return 0;
			}
			if(effect_state.state[AW210XX_LED_NOTIFY_MODE] == 0){
				return -1;
			}
			while(effect_state.state[AW210XX_LED_NOTIFY_MODE] == 2)
			{
				usleep_range(100000, 120000);
				AW_ERR(" AW210XX_LED_NOTIFY_MODE pause \n" );
				if(effect_state.state[AW210XX_LED_NOTIFY_MODE] == 1)
				{
					//TBD enable hw
					;
				}
			}
			break;
		default:
			break;
	}
	return -1;
}

void brightness_sbmd_setup(br_pwm_t br_pwm, bool enable) {
	static bool br_enable = true;
	int led_groups_num = aw210xx_g_chip[0][0]->pdata->led->led_groups_num;

	//AW_ERR("  entry br_pwm = %d\n", br_pwm);
	if( (aw210xx_g_chip[0][0]->br_res == br_pwm ) && (br_enable == enable) ){
		//AW_ERR(" already set\n");
		return;
	}
	else {
		//AW_ERR(" not set\n");
		br_enable = enable;
	}

	if(br_pwm == BR_RESOLUTION_8BIT) {

		aw210xx_g_chip[0][0]->br_res = BR_RESOLUTION_8BIT;
		aw210xx_br_res_set(aw210xx_g_chip[0][0]);
		/* sbmd disable */
		aw210xx_sbmd_set(aw210xx_g_chip[0][0], enable);

		if (led_groups_num == 8) {
			aw210xx_g_chip[1][0]->br_res = BR_RESOLUTION_8BIT;
			aw210xx_br_res_set(aw210xx_g_chip[1][0]);
				/* sbmd disable */
			aw210xx_sbmd_set(aw210xx_g_chip[1][0], enable);
		}
	}
	else if(br_pwm == BR_RESOLUTION_9_AND_3_BIT){
		aw210xx_g_chip[0][0]->br_res = BR_RESOLUTION_9_AND_3_BIT;
		aw210xx_br_res_set(aw210xx_g_chip[0][0]);
		/* sbmd disable */
		aw210xx_sbmd_set(aw210xx_g_chip[0][0], enable);

		if (led_groups_num == 8) {
			aw210xx_g_chip[1][0]->br_res = BR_RESOLUTION_9_AND_3_BIT;
			aw210xx_br_res_set(aw210xx_g_chip[1][0]);
			aw210xx_sbmd_set(aw210xx_g_chip[1][0], enable);
		}
	}
	else{
		//not yet required
		AW_ERR("br_pwm no requirement");
	}
}

void run_alwayson_effect(struct aw210xx *aw210xx){

	struct aw210xx *aw210xx_id1 = aw210xx_g_chip[1][0];
	struct aw210xx *led = aw210xx_g_chip[0][0];
	int i =0;
	//AW_ERR(" effect_state.data[AW210XX_LED_NEW_ALWAYSON] = %d, last_run_effect = %d", effect_state.data[AW210XX_LED_NEW_ALWAYSON], last_run_effect);
	if( (effect_state.data[AW210XX_LED_NEW_ALWAYSON] == 1) || (last_run_effect != AW210XX_LED_NEW_ALWAYSON)) {
		effect_state.data[AW210XX_LED_NEW_ALWAYSON] = 0;
		for (i=0; i<3; i++)
		{
			AW_ERR(" new_always_on_color = %d,%d,%d,%d,%d,%d,%d,%d,%d",new_always_on_color[i][0],new_always_on_color[i][1],new_always_on_color[i][2],new_always_on_color[i][3],
			new_always_on_color[i][4],new_always_on_color[i][5],new_always_on_color[i][6],new_always_on_color[i][7],new_always_on_color[i][8]);
			AW_ERR(" frame entry \n");
		}

		brightness_sbmd_setup(BR_RESOLUTION_8BIT , false);
		for(i=0;i<3;i++){
			aw210xx_i2c_write(led, AW210XX_REG_BR00L + 2*i, new_always_on_color[i][8]*brightness_ratio/255);
			aw210xx_i2c_write(led, AW210XX_REG_BR03L + 2*i, new_always_on_color[i][8]*brightness_ratio/255);
			aw210xx_i2c_write(led, AW210XX_REG_BR06L + 2*i, new_always_on_color[i][8]*brightness_ratio/255);
			aw210xx_i2c_write(led, AW210XX_REG_BR09L + 2*i, new_always_on_color[i][8]*brightness_ratio/255);

			aw210xx_i2c_write(aw210xx_id1, AW210XX_REG_BR00L + 2*i, new_always_on_color[i][8]);
			aw210xx_i2c_write(aw210xx_id1, AW210XX_REG_BR03L + 2*i, new_always_on_color[i][8]);
			aw210xx_i2c_write(aw210xx_id1, AW210XX_REG_BR06L + 2*i, new_always_on_color[i][8]);
			aw210xx_i2c_write(aw210xx_id1, AW210XX_REG_BR09L + 2*i, new_always_on_color[i][8]);

			aw210xx_i2c_write(led, AW210XX_REG_SL00 + i, new_always_on_color[i][0]);
			aw210xx_i2c_write(led, AW210XX_REG_SL03 + i, new_always_on_color[i][1]);
			aw210xx_i2c_write(led, AW210XX_REG_SL06 + i, new_always_on_color[i][2]);
			aw210xx_i2c_write(led, AW210XX_REG_SL09 + i, new_always_on_color[i][3]);

			aw210xx_i2c_write(aw210xx_id1, AW210XX_REG_SL00 + i, new_always_on_color[i][4]);
			aw210xx_i2c_write(aw210xx_id1, AW210XX_REG_SL03 + i, new_always_on_color[i][5]);
			aw210xx_i2c_write(aw210xx_id1, AW210XX_REG_SL06 + i, new_always_on_color[i][6]);
			aw210xx_i2c_write(aw210xx_id1, AW210XX_REG_SL09 + i, new_always_on_color[i][7]);
		}

		aw210xx_update(led);
		aw210xx_update(aw210xx_id1);
	}
}

void run_music_effect(struct aw210xx *aw210xx){

	struct aw210xx *aw210xx_id1 = aw210xx_g_chip[1][0];
	struct aw210xx *led = aw210xx_g_chip[0][0];
	int i =0,brightness[3];
	//AW_ERR(" effect_state.data[AW210XX_LED_MUSICMODE] = %d, last_run_effect = %d", effect_state.data[AW210XX_LED_MUSICMODE], last_run_effect);
	if((effect_state.data[AW210XX_LED_MUSICMODE] == 1) || (last_run_effect != AW210XX_LED_MUSICMODE)) {
		effect_state.data[AW210XX_LED_MUSICMODE] = 0;
		for (i=0; i<3; i++)
		{
			AW_ERR(" music_color = %d,%d,%d,%d,%d,%d,%d,%d,%d",music_color[i][0],music_color[i][1],music_color[i][2],music_color[i][3],
			music_color[i][4],music_color[i][5],music_color[i][6],music_color[i][7],music_color[i][8]);
			if (music_color[i][8] > 16) {
				brightness[i] = (music_color[i][8] * music_color[i][8]) / 16;
			}
			else {
				brightness[i] = music_color[i][8];
			}
		}
		AW_ERR(" frame entry \n");
		brightness_sbmd_setup(BR_RESOLUTION_9_AND_3_BIT , false);
		for(i=0;i<3;i++){
			aw210xx_i2c_write(led, AW210XX_REG_BR00H + 2 * i, 0xff & (brightness[i] >> 8));
			aw210xx_i2c_write(led, AW210XX_REG_BR00L + 2 * i, 0xff & brightness[i]);
			aw210xx_i2c_write(led, AW210XX_REG_BR03H + 2 * i, 0xff & (brightness[i] >> 8));
			aw210xx_i2c_write(led, AW210XX_REG_BR03L + 2 * i, 0xff & brightness[i]);
			aw210xx_i2c_write(led, AW210XX_REG_BR06H + 2 * i, 0xff & (brightness[i] >> 8));
			aw210xx_i2c_write(led, AW210XX_REG_BR06L + 2 * i, 0xff & brightness[i]);
			aw210xx_i2c_write(led, AW210XX_REG_BR09H + 2 * i, 0xff & (brightness[i] >> 8));
			aw210xx_i2c_write(led, AW210XX_REG_BR09L + 2 * i, 0xff & brightness[i]);
			aw210xx_i2c_write(led, AW210XX_REG_ABMCFG, 0x01);

				aw210xx_i2c_write(aw210xx_id1, AW210XX_REG_BR00H + 2 * i, 0xff & (brightness[i] >> 8));
				aw210xx_i2c_write(aw210xx_id1, AW210XX_REG_BR00L + 2 * i, 0xff & brightness[i]);
				aw210xx_i2c_write(aw210xx_id1, AW210XX_REG_BR03H + 2 * i, 0xff & (brightness[i] >> 8));
				aw210xx_i2c_write(aw210xx_id1, AW210XX_REG_BR03L + 2 * i, 0xff & brightness[i]);
				aw210xx_i2c_write(aw210xx_id1, AW210XX_REG_BR06H + 2 * i, 0xff & (brightness[i] >> 8));
				aw210xx_i2c_write(aw210xx_id1, AW210XX_REG_BR06L + 2 * i, 0xff & brightness[i]);
				aw210xx_i2c_write(aw210xx_id1, AW210XX_REG_BR09H + 2 * i, 0xff & (brightness[i] >> 8));
				aw210xx_i2c_write(aw210xx_id1, AW210XX_REG_BR09L + 2 * i, 0xff & brightness[i]);
				aw210xx_i2c_write(aw210xx_id1, AW210XX_REG_ABMCFG, 0x01);

			aw210xx_i2c_write(led, AW210XX_REG_SL00 + i, music_color[i][0]);
			aw210xx_i2c_write(led, AW210XX_REG_SL03 + i, music_color[i][1]);
			aw210xx_i2c_write(led, AW210XX_REG_SL06 + i, music_color[i][2]);
			aw210xx_i2c_write(led, AW210XX_REG_SL09 + i, music_color[i][3]);
			aw210xx_i2c_write(led, AW210XX_REG_GCFG, 0x40);
			aw210xx_i2c_write(aw210xx_id1, AW210XX_REG_SL00 + i, music_color[i][4]);
			aw210xx_i2c_write(aw210xx_id1, AW210XX_REG_SL03 + i, music_color[i][5]);
			aw210xx_i2c_write(aw210xx_id1, AW210XX_REG_SL06 + i, music_color[i][6]);
			aw210xx_i2c_write(aw210xx_id1, AW210XX_REG_SL09 + i, music_color[i][7]);
			aw210xx_i2c_write(aw210xx_id1, AW210XX_REG_GCFG, 0x40);
		}
		aw210xx_update(led);
		aw210xx_update(aw210xx_id1);
	}
}
void run_notify_effect_autonomous(struct aw210xx *aw210xx){

	struct aw210xx *aw210xx_id1 = aw210xx_g_chip[1][0];
	struct aw210xx *led = aw210xx_g_chip[0][0];
	int i=0;

	int notify_color[3];
	notify_color[0] = rgb_notify_list[0].r;
	notify_color[1] = rgb_notify_list[0].g;
	notify_color[2] = rgb_notify_list[0].b;
	//AW_ERR(" effect_state.data[AW210XX_LED_MUSICMODE] = %d, last_run_effect = %d", effect_state.data[AW210XX_LED_MUSICMODE], last_run_effect);
	if((effect_state.data[AW210XX_LED_NOTIFY_MODE] == 1) || (last_run_effect != AW210XX_LED_NOTIFY_MODE)) {
		effect_state.data[AW210XX_LED_NOTIFY_MODE] = 0;
		AW_ERR(" entry \n");
		brightness_sbmd_setup(BR_RESOLUTION_8BIT , true);

/*		aw210xx_i2c_write(led, AW210XX_REG_GSLR, rgb_notify_list[0].r);
		aw210xx_i2c_write(led, AW210XX_REG_GSLG, rgb_notify_list[0].g);
		aw210xx_i2c_write(led, AW210XX_REG_GSLB, rgb_notify_list[0].b);
*/
        for(i=0;i<3;i++) {
            aw210xx_i2c_write(led, AW210XX_REG_SL00 + i, notify_color[i]);
			aw210xx_i2c_write(led, AW210XX_REG_SL03 + i, notify_color[i]);
			aw210xx_i2c_write(led, AW210XX_REG_SL06 + i, notify_color[i]);
			aw210xx_i2c_write(led, AW210XX_REG_SL09 + i, notify_color[i]);
        }

		aw210xx_i2c_write(led, AW210XX_REG_GBRH, br_notify_fadeh[0].r | br_notify_fadeh[0].g | br_notify_fadeh[0].b);
		aw210xx_i2c_write(led, AW210XX_REG_GBRL, 0x00);

		aw210xx_i2c_write(led, AW210XX_REG_GCFG, 0x4F);
		aw210xx_i2c_write(led, AW210XX_REG_ABMCFG, 0x03);
		aw210xx_i2c_write(led, AW210XX_REG_ABMT0 , notify_breath_cycle[0]);
		aw210xx_i2c_write(led, AW210XX_REG_ABMT1 , notify_breath_cycle[1]);

		aw210xx_update(led);

		if (led->pdata->led->led_groups_num == 8 && aw210xx_id1 != NULL) {
			for(i=0;i<12;i++)
			{
				aw210xx_i2c_write(aw210xx_id1, AW210XX_REG_SL00+i, 0x00);
			}
			aw210xx_update(aw210xx_id1);
		}

		aw210xx_i2c_write(led, AW210XX_REG_ABMGO, 0x01);

/*
		if (led->pdata->led->led_groups_num == 8 && aw210xx_id1 != NULL) {
			aw210xx_i2c_write(aw210xx_id1, AW210XX_REG_ABMGO, 0x01);
		}
*/

	}
}

void initiate_effect(struct aw210xx *aw210xx, effect_select_t effect) {
	//print_effectdata(effect);
	effect_data[effect] = aw210xx_cfg_array[effect].p;
	effect_stop_val[effect] = 0;
	num[effect] = 0;
	aw210xx_rgb_multi_breath_init(effect_data[effect],effect);
	aw210xx_frame_display(effect);
	aw210xx_update_effect(aw210xx,effect);
}

int update_next_frame(struct aw210xx *aw210xx, effect_select_t effect){
		//AW_ERR("MTC_LOG: while %s\n", __func__);
		int ret =0;
		aw210xx_update_frame_idx(effect_data[effect], effect);

		aw210xx_frame_display(effect);

		aw210xx_update_effect(aw210xx, effect);

		ret = aw210xx_start_next_effect(effect,aw210xx);

		if(ret != 0){
			AW_ERR("exit2 \n");
			return 1;
		}
		return 0;
}

void run_poweron_effect(struct aw210xx *aw210xx){
	int ret=0;
	brightness_sbmd_setup(BR_RESOLUTION_8BIT , false);
	//effect_state.state  = 0 stopped, 1 initial, 2 running, 3 pause,
	if (effect_state.state[AW210XX_LED_POWERON_MODE] == 1){
		AW_ERR(" AW210XX_LED_POWERON_MODE enter \n" );
		initiate_effect(aw210xx, POWERON_EFFECT);
		if(effect_state.state[AW210XX_LED_POWERON_MODE] != 0) // TBD protect by lock for syncronization
			effect_state.state[AW210XX_LED_POWERON_MODE] = 2;
	}
	if(effect_state.state[AW210XX_LED_POWERON_MODE] == 2){
		ret = update_next_frame(aw210xx, POWERON_EFFECT);
		if(ret == 1) // current effect cycle is completed
		{
			effect_state.state[AW210XX_LED_POWERON_MODE] = 0;
		}
	}
}

void run_incall_effect(struct aw210xx *aw210xx){
	int ret=0;
	brightness_sbmd_setup(BR_RESOLUTION_8BIT, false);
	//effect_state.state  = 0 stopped, 1 initial, 2 running, 3 pause,
	if (effect_state.state[AW210XX_LED_INCALL_MODE] == 1){
		AW_ERR(" AW210XX_LED_INCALL_MODE enter \n" );
		initiate_effect(aw210xx, INCALL_EFFECT);
		if(effect_state.state[AW210XX_LED_INCALL_MODE] != 0) // TBD protect by lock for syncronization
			effect_state.state[AW210XX_LED_INCALL_MODE] = 2;
	}
	if(effect_state.state[AW210XX_LED_INCALL_MODE] == 2){
		ret = update_next_frame(aw210xx, INCALL_EFFECT);
		if(ret == 1) // current effect cycle is completed
		{
			if(effect_state.state[AW210XX_LED_INCALL_MODE] != 0) // if effect is not stopped then restart
				effect_state.state[AW210XX_LED_INCALL_MODE] = 1;
		}
	}
}

void run_game_effect(struct aw210xx *aw210xx){
	int ret=0;
	brightness_sbmd_setup(BR_RESOLUTION_8BIT, false);
	//effect_state.state  = 0 stopped, 1 initial, 2 running, 3 pause,
	if (effect_state.state[AW210XX_LED_GAMEMODE] == 1){
		AW_ERR(" AW210XX_LED_GAMEMODE enter \n" );
		initiate_effect(aw210xx, GAME_ENTER_EFFECT);
		if(effect_state.state[AW210XX_LED_GAMEMODE] != 0) // TBD protect by lock for syncronization
			effect_state.state[AW210XX_LED_GAMEMODE] = 2;
	}
	if(effect_state.state[AW210XX_LED_GAMEMODE] == 2){
		ret = update_next_frame(aw210xx, GAME_ENTER_EFFECT);
		if(ret == 1) // current effect cycle is completed
		{
			effect_state.state[AW210XX_LED_GAMEMODE] = 0;
		}
	}
}

void run_notify_effect(struct aw210xx *aw210xx){
	int ret=0;
	brightness_sbmd_setup(BR_RESOLUTION_8BIT , false);
	//effect_state.state  = 0 stopped, 1 initial, 2 running, 3 pause,
	if (effect_state.state[AW210XX_LED_NOTIFY_MODE] == 1){
		AW_ERR(" AW210XX_LED_NOTIFY_MODE enter \n" );
		initiate_effect(aw210xx, NOTIFY_EFFECT);
		if(effect_state.state[AW210XX_LED_NOTIFY_MODE] != 0) // TBD protect by lock for syncronization
			effect_state.state[AW210XX_LED_NOTIFY_MODE] = 2;
	}
	if(effect_state.state[AW210XX_LED_NOTIFY_MODE] == 2){
		ret = update_next_frame(aw210xx, NOTIFY_EFFECT);
		if(ret == 1) // current effect cycle is completed
		{
			if(effect_state.state[AW210XX_LED_NOTIFY_MODE] != 0) // if effect is not stopped then restart
				effect_state.state[AW210XX_LED_NOTIFY_MODE] = 1;
		}
	}
}

void run_charger_effect(struct aw210xx *aw210xx){
	int ret=0;
	brightness_sbmd_setup(BR_RESOLUTION_8BIT, false);
	//effect_state.state  = 0 disable, 1 init, 2 running, 3 pause, 4 second stage init (for charger),5 second stage running, 6 100% recover  stage (for charger)
	if (effect_state.state[AW210XX_LED_CHARGE_MODE] == 1){
		AW_ERR(" AW210XX_LED_CHARGE_MODE enter \n" );
		initiate_effect(aw210xx, CHARGE_EFFECT);
		if(effect_state.state[AW210XX_LED_CHARGE_MODE] != 0) // TBD protect by lock for syncronization
			effect_state.state[AW210XX_LED_CHARGE_MODE] = 2;
	}
	if(effect_state.state[AW210XX_LED_CHARGE_MODE] == 2){
		ret = update_next_frame(aw210xx, CHARGE_EFFECT);
		if(ret == 1) // current effect cycle is completed
		{
			if(effect_state.state[AW210XX_LED_CHARGE_MODE] != 0)
			{
				effect_state.state[AW210XX_LED_CHARGE_MODE] = 4;
				blevel = -1;
			}
		}
	}
	if(effect_state.state[AW210XX_LED_CHARGE_MODE] == 4){
		AW_ERR(" AW210XX_LED_CHARGE_MODE stage 2 \n" );
		charge_ongoing_frame = 0;
		initiate_effect(aw210xx, CHARGE_STAGE2_EFFECT);
		if(effect_state.state[AW210XX_LED_CHARGE_MODE] != 0) // TBD protect by lock for syncronization
			effect_state.state[AW210XX_LED_CHARGE_MODE] = 5;
	}
	if(effect_state.state[AW210XX_LED_CHARGE_MODE] == 5){
		if(charge_ongoing_frame < charge_frame_map[effect_state.data[AW210XX_LED_CHARGE_MODE]] ) {
			charge_ongoing_frame++;
			ret = update_next_frame(aw210xx, CHARGE_STAGE2_EFFECT);
			if(ret == 1) // current effect cycle is completed
			{
				effect_state.state[AW210XX_LED_CHARGE_MODE] = 0;
				charge_ongoing_frame =0;
			}
		}
		else if(last_run_effect != AW210XX_LED_CHARGE_MODE) {
			aw210xx_update_effect(aw210xx, CHARGE_STAGE2_EFFECT);
		}
	}

}

void reset_led(void){

	struct aw210xx *led = aw210xx_g_chip[0][0];
/*
	struct aw210xx *aw210xx_id1 = aw210xx_g_chip[1][0];
	aw210xx_i2c_write(led, AW210XX_REG_GBRH, 0x00);
	aw210xx_i2c_write(led, AW210XX_REG_GBRL, 0xff);
	aw210xx_i2c_write(led, AW210XX_REG_ABMCFG, 0x00);
	aw210xx_i2c_write(led, AW210XX_REG_ABMGO, 0x00);
	if (led->pdata->led->led_groups_num == 8 && aw210xx_id1 != NULL) {
		aw210xx_i2c_write(aw210xx_id1, AW210XX_REG_GBRH, 0x00);
		aw210xx_i2c_write(aw210xx_id1, AW210XX_REG_GBRL, 0xff);
		aw210xx_i2c_write(aw210xx_id1, AW210XX_REG_ABMCFG, 0x00);
		aw210xx_i2c_write(aw210xx_id1, AW210XX_REG_ABMGO, 0x00);
	}
*/
	AW_LOG("enter");
	led->pdata->led->power_change_state |= (1 << 0);
	mutex_lock(&led->pdata->led->lock);
	if (aw210xx_hw_enable(led->pdata->led, false)) {
		AW_LOG("aw210xx hw disable failed");
	}
	usleep_range(10000,10500);

	if (aw210xx_hw_enable(led->pdata->led, true)) {
		AW_LOG("aw210xx hw enable failed");
	}
	mutex_unlock(&led->pdata->led->lock);
	led->pdata->led->power_change_state &= ~(1 << 0);
}

void run_effect(struct aw210xx *aw210xx)
{
	while (1)
	{
		if(effect_state.state[AW210XX_LED_POWERON_MODE] != 0) {
			run_poweron_effect(aw210xx);
			last_run_effect = AW210XX_LED_POWERON_MODE;
			usleep_range(20000,20500);
			continue;
		}
		else if (effect_state.state[AW210XX_LED_NEW_ALWAYSON] != 0){
			run_alwayson_effect(aw210xx);
			last_run_effect = AW210XX_LED_NEW_ALWAYSON;
			usleep_range(20000,20500);
			continue;
		}
		else if (effect_state.state[AW210XX_LED_INCALL_MODE] != 0){
			if(last_run_effect == AW210XX_LED_NOTIFY_MODE ){
				reset_led();
			}
			run_incall_effect(aw210xx);
			last_run_effect = AW210XX_LED_INCALL_MODE;
			usleep_range(6500,7000);
			continue;
		}
		else if (effect_state.state[AW210XX_LED_MUSICMODE] != 0){
			run_music_effect(aw210xx);
			last_run_effect = AW210XX_LED_MUSICMODE;
			usleep_range(3000,5000);
			continue;
		}
		else if(effect_state.state[AW210XX_LED_GAMEMODE] != 0) {
			run_game_effect(aw210xx);
			last_run_effect = AW210XX_LED_GAMEMODE;
			usleep_range(4500,5000);
			continue;
		}
		else if(effect_state.state[AW210XX_LED_NOTIFY_MODE] != 0) {
			run_notify_effect_autonomous(aw210xx);
			last_run_effect = AW210XX_LED_NOTIFY_MODE;
			usleep_range(20000,20500);
			continue;
		}
		else if(effect_state.state[AW210XX_LED_CHARGE_MODE] != 0) {
			if(last_run_effect == AW210XX_LED_NOTIFY_MODE ){
				reset_led();
			}
			run_charger_effect(aw210xx);
			last_run_effect = AW210XX_LED_CHARGE_MODE;
			usleep_range(20000,20500);
			continue;
		}
		break; // no effect running
	}
}

static void aw210xx_brightness(struct aw210xx *led)
{
	int i = 0;
	int j = 0;
	int color[DLED_RGB_NUM][LED_MAX_NUM] = {0};
	int led_brightness = 0;
	struct aw210xx *aw210xx_id1 = NULL;
	int led_groups_num = led->pdata->led->led_groups_num;
	int breath_rise_time = 0,breath_fall_time = 0;

	AW_LOG("id = %d brightness = %d, boot_mode = %d \n", led->id, led->cdev.brightness,get_boot_mode());
	if (led->id > 5) {
		AW_LOG("id = %d ,return\n", led->id);
		return ;
	}

	if (oplus_boot_mode_is_power_off_charging()) {
		AW_LOG("boot_mode is power_off_charging");
		return;
	}

	if (led_groups_num == 8) {
		if (aw210xx_g_chip[1][0] == NULL) {
			AW_LOG("aw210xx_g_chip[1][%d] is NULL \n", led->id);
		} else {
			aw210xx_id1 = aw210xx_g_chip[1][0];
		}
	}

	switch(led->id) {
	case AW210xx_LED_RED:
		ledbri[0] = led->cdev.brightness;
		if (led->cdev.brightness > 0)
			led->pdata->led->rgb_isnk_on |= R_ISNK_ON_MASK;
		else
			led->pdata->led->rgb_isnk_on &= ~R_ISNK_ON_MASK;
		break;
	case AW210xx_LED_GREEN:
		ledbri[1] = led->cdev.brightness;
		if (led->cdev.brightness > 0)
			led->pdata->led->rgb_isnk_on |= G_ISNK_ON_MASK;
		else
			led->pdata->led->rgb_isnk_on &= ~G_ISNK_ON_MASK;
		break;
	case AW210xx_LED_BLUE:
		ledbri[2] = led->cdev.brightness;
		if (led->cdev.brightness > 0)
			led->pdata->led->rgb_isnk_on |= B_ISNK_ON_MASK;
		else
			led->pdata->led->rgb_isnk_on &= ~B_ISNK_ON_MASK;
		break;
	default:
		AW_LOG("no define id = %d brightness = %d\n", led->id, led->cdev.brightness);
		return;
	}
	AW_LOG("rgb_isnk_on = 0x%x\n", led->pdata->led->rgb_isnk_on);
	if (led->cdev.brightness > 0) {
		led->pdata->led->power_change_state |= (1 << (led->id));
		mutex_lock(&led->pdata->led->lock);
		if (!led->pdata->led->led_enable && led->pdata->led->rgb_isnk_on) {
			if (aw210xx_hw_enable(led->pdata->led, true)) {
				AW_LOG("aw210xx hw enable failed");
			}
		}
		mutex_unlock(&led->pdata->led->lock);
		led->pdata->led->power_change_state &= ~(1 << (led->id));
	} else {
		led->pdata->led->power_change_state |= (1 << (led->id));
		mutex_lock(&led->pdata->led->lock);
		if (led->pdata->led->led_enable && !(led->pdata->led->rgb_isnk_on)) {
			if (aw210xx_hw_enable(led->pdata->led, false)) {
				AW_LOG("aw210xx hw disable failed");
			}
		}
		mutex_unlock(&led->pdata->led->lock);
		led->pdata->led->power_change_state &= ~(1 << (led->id));
	}

	if (led->cdev.brightness == 0) {
		usleep_range(10000, 10500);
	}

	if (!led->pdata->led->led_enable) {
		AW_LOG("aw210xx hw is disable");
		goto out;
	}
	switch(led->pdata->led_mode) {
		case AW210XX_LED_INCALL_MODE:
		case AW210XX_LED_NOTIFY_MODE:
		case AW210XX_LED_CHARGE_MODE:
		case AW210XX_LED_POWERON_MODE:
		case AW210XX_LED_GAMEMODE:
		case AW210XX_LED_NEW_ALWAYSON:
		case AW210XX_LED_MUSICMODE:
			AW_ERR(" initial effect mode %d enter \n",led->pdata->led_mode );
			led->br_res = BR_RESOLUTION_8BIT;
			aw210xx_br_res_set(led);
			/* sbmd enable */
			aw210xx_sbmd_set(led, false);
			if ((led_groups_num == 8) && (aw210xx_id1 != NULL)) {
				aw210xx_id1->br_res = BR_RESOLUTION_8BIT;
				aw210xx_br_res_set(aw210xx_id1);
				/* sbmd enable */
				aw210xx_sbmd_set(aw210xx_id1, false);
			}
			/*light effect*/
			run_effect(led);
			AW_ERR(" effect mode %d exit \n", led->pdata->led_mode );
			break;
		default:
			break;
	}
	
#if 0
	/*aw210xx led music mode set up*/
	if (led->pdata->led_mode == AW210XX_LED_MUSICMODE ) {
		led->br_res = BR_RESOLUTION_9_AND_3_BIT;
		aw210xx_br_res_set(led);
		/* sbmd disable */
		aw210xx_sbmd_set(led, false);
		
		if (led_groups_num == 8) {
			led->br_res = BR_RESOLUTION_9_AND_3_BIT;
			aw210xx_br_res_set(aw210xx_id1);
			aw210xx_sbmd_set(aw210xx_id1, false);
		}
		if (led->cdev.brightness > 16) {
			led_brightness = (led->cdev.brightness * led->cdev.brightness) / 16;
		} else {
			led_brightness = led->cdev.brightness;
		}

		aw210xx_i2c_write(led, AW210XX_REG_BR00H + 2 * led->id, 0xff & (led_brightness >> 8));
		aw210xx_i2c_write(led, AW210XX_REG_BR00L + 2 * led->id, 0xff & led_brightness);
		aw210xx_i2c_write(led, AW210XX_REG_BR03H + 2 * led->id, 0xff & (led_brightness >> 8));
		aw210xx_i2c_write(led, AW210XX_REG_BR03L + 2 * led->id, 0xff & led_brightness);
		aw210xx_i2c_write(led, AW210XX_REG_BR06H + 2 * led->id, 0xff & (led_brightness >> 8));
		aw210xx_i2c_write(led, AW210XX_REG_BR06L + 2 * led->id, 0xff & led_brightness);
		aw210xx_i2c_write(led, AW210XX_REG_BR09H + 2 * led->id, 0xff & (led_brightness >> 8));
		aw210xx_i2c_write(led, AW210XX_REG_BR09L + 2 * led->id, 0xff & led_brightness);
		aw210xx_i2c_write(led, AW210XX_REG_ABMCFG, 0x01);

		if (led_groups_num == 8 && aw210xx_id1 != NULL) {
			aw210xx_i2c_write(aw210xx_id1, AW210XX_REG_BR00H + 2 * led->id, 0xff & (led_brightness >> 8));
			aw210xx_i2c_write(aw210xx_id1, AW210XX_REG_BR00L + 2 * led->id, 0xff & led_brightness);
			aw210xx_i2c_write(aw210xx_id1, AW210XX_REG_BR03H + 2 * led->id, 0xff & (led_brightness >> 8));
			aw210xx_i2c_write(aw210xx_id1, AW210XX_REG_BR03L + 2 * led->id, 0xff & led_brightness);
			aw210xx_i2c_write(aw210xx_id1, AW210XX_REG_BR06H + 2 * led->id, 0xff & (led_brightness >> 8));
			aw210xx_i2c_write(aw210xx_id1, AW210XX_REG_BR06L + 2 * led->id, 0xff & led_brightness);
			aw210xx_i2c_write(aw210xx_id1, AW210XX_REG_BR09H + 2 * led->id, 0xff & (led_brightness >> 8));
			aw210xx_i2c_write(aw210xx_id1, AW210XX_REG_BR09L + 2 * led->id, 0xff & led_brightness);
			aw210xx_i2c_write(aw210xx_id1, AW210XX_REG_ABMCFG, 0x01);
		}


		/*red led, green led max current is 20ma, blue led max current is 10ma*/
		
		for (i = 0; i < led_groups_num; i++) {
			if (led->id == 0) {
				color[i] = (led->pdata->color[i]) / 3;
				AW_LOG("id = %d set color[%d] = %d\n", led->id, i, color[i]);
			} else if (led->id == 1) {
				color[i] = (led->pdata->color[i]) / 3;
				AW_LOG("id = %d set color[%d] = %d\n", led->id, i, color[i]);
			} else if (led->id == 2) {
				color[i] = (led->pdata->color[i]) / 3;
				AW_LOG("id = %d set color[%d] = %d\n", led->id, i, color[i]);
			}
		}
		if (led_groups_num == 2) {
			aw210xx_i2c_write(led, AW210XX_REG_SL00 + led->id, color[0]);
			aw210xx_i2c_write(led, AW210XX_REG_SL03 + led->id, color[1]);
			aw210xx_i2c_write(led, AW210XX_REG_GCFG, 0x40);
			AW_LOG("id = %d led_groups_num = %d \n", led->id, led_groups_num);
		} else if (led_groups_num == 4) {
			aw210xx_i2c_write(led, AW210XX_REG_SL00 + led->id, color[0]);
			aw210xx_i2c_write(led, AW210XX_REG_SL03 + led->id, color[1]);
			aw210xx_i2c_write(led, AW210XX_REG_SL06 + led->id, color[2]);
			aw210xx_i2c_write(led, AW210XX_REG_SL09 + led->id, color[3]);
			aw210xx_i2c_write(led, AW210XX_REG_GCFG, 0x40);
			AW_LOG("id = %d led_groups_num = %d \n", led->id, led_groups_num);
		} else if (led_groups_num == 8 && aw210xx_id1 != NULL) {
			aw210xx_i2c_write(led, AW210XX_REG_SL00 + led->id, color[0]);
			aw210xx_i2c_write(led, AW210XX_REG_SL03 + led->id, color[1]);
			aw210xx_i2c_write(led, AW210XX_REG_SL06 + led->id, color[2]);
			aw210xx_i2c_write(led, AW210XX_REG_SL09 + led->id, color[3]);
			aw210xx_i2c_write(led, AW210XX_REG_GCFG, 0x40);
			aw210xx_i2c_write(aw210xx_id1, AW210XX_REG_SL00 + led->id, color[4]);
			aw210xx_i2c_write(aw210xx_id1, AW210XX_REG_SL03 + led->id, color[5]);
			aw210xx_i2c_write(aw210xx_id1, AW210XX_REG_SL06 + led->id, color[6]);
			aw210xx_i2c_write(aw210xx_id1, AW210XX_REG_SL09 + led->id, color[7]);
			aw210xx_i2c_write(aw210xx_id1, AW210XX_REG_GCFG, 0x40);
			AW_LOG("id = %d led_groups_num = %d \n", led->id, led_groups_num);
		} else {
			aw210xx_i2c_write(led, AW210XX_REG_SL00 + led->id, color[0]);
			aw210xx_i2c_write(led, AW210XX_REG_SL03 + led->id, color[1]);
			aw210xx_i2c_write(led, AW210XX_REG_SL06 + led->id, color[2]);
			aw210xx_i2c_write(led, AW210XX_REG_SL09 + led->id, color[3]);
			aw210xx_i2c_write(led, AW210XX_REG_GCFG, 0x40);
			AW_LOG("id = %d led_groups_num = %d \n", led->id, led_groups_num);
		}
	}
	else
#endif
	if (led->pdata->led_mode == AW210XX_LED_INDIVIDUAL_CTL_BREATH) {
		/*red led, green led max current is 20ma, blue led max current is 10ma*/
		for (j = 0; j < led_groups_num; j++) {
			for (i = 0; i < DLED_RGB_NUM; i++) {
				color[i][j] = ((led->pdata->color[j] >> 8*(DLED_RGB_NUM - 1 - i)) & 0xff);
				AW_LOG("id = %d set color[%d][%d] = %x\n", led->id, i, j, color[i][j]);
			}
		}

		led->br_res = BR_RESOLUTION_8BIT;
		aw210xx_br_res_set(led);
		/* sbmd enable */
		aw210xx_sbmd_set(led, true);

		if (led_groups_num == 8 && aw210xx_id1 != NULL) {
			aw210xx_id1->br_res = BR_RESOLUTION_8BIT;
			aw210xx_br_res_set(aw210xx_id1);
			aw210xx_sbmd_set(aw210xx_id1, true);
		}

		for (i = 0; i < DLED_RGB_NUM; i++) {
			if (led_groups_num == 2) {
				aw210xx_i2c_write(led, AW210XX_REG_SL00 + i, color[i][0]);
				aw210xx_i2c_write(led, AW210XX_REG_SL03 + i, color[i][1]);
				aw210xx_i2c_write(led, AW210XX_REG_GCFG, 0x47);
				AW_LOG("id = %d led_groups_num = %d \n", led->id, led_groups_num);
			} else if (led_groups_num == 4) {
				aw210xx_i2c_write(led, AW210XX_REG_SL00 + i, color[i][0]);
				aw210xx_i2c_write(led, AW210XX_REG_SL03 + i, color[i][1]);
				aw210xx_i2c_write(led, AW210XX_REG_SL06 + i, color[i][2]);
				aw210xx_i2c_write(led, AW210XX_REG_SL09 + i, color[i][3]);
				aw210xx_i2c_write(led, AW210XX_REG_GCFG, 0x4F);
				AW_LOG("id = %d led_groups_num = %d \n", led->id, led_groups_num);
			} else if (led_groups_num == 8 && aw210xx_id1 != NULL) {
				aw210xx_i2c_write(led, AW210XX_REG_SL00 + i, color[i][0]);
				aw210xx_i2c_write(led, AW210XX_REG_SL03 + i, color[i][1]);
				aw210xx_i2c_write(led, AW210XX_REG_SL06 + i, color[i][2]);
				aw210xx_i2c_write(led, AW210XX_REG_SL09 + i, color[i][3]);
				aw210xx_i2c_write(led, AW210XX_REG_GCFG, 0x4F);
				aw210xx_i2c_write(aw210xx_id1, AW210XX_REG_SL00 + i, color[i][4]);
				aw210xx_i2c_write(aw210xx_id1, AW210XX_REG_SL03 + i, color[i][5]);
				aw210xx_i2c_write(aw210xx_id1, AW210XX_REG_SL06 + i, color[i][6]);
				aw210xx_i2c_write(aw210xx_id1, AW210XX_REG_SL09 + i, color[i][7]);
				aw210xx_i2c_write(aw210xx_id1, AW210XX_REG_GCFG, 0x4F);
				AW_LOG("id = %d led_groups_num = %d \n", led->id, led_groups_num);
			} else {
				aw210xx_i2c_write(led, AW210XX_REG_SL00 + i, color[i][0]);
				aw210xx_i2c_write(led, AW210XX_REG_SL03 + i, color[i][1]);
				aw210xx_i2c_write(led, AW210XX_REG_SL06 + i, color[i][2]);
				aw210xx_i2c_write(led, AW210XX_REG_SL09 + i, color[i][3]);
				aw210xx_i2c_write(led, AW210XX_REG_GCFG, 0x4F);
				AW_LOG("id = %d led_groups_num = %d \n", led->id, led_groups_num);
			}
		}
	} else {
		if (led->id == 0) {
			led_brightness = (led->cdev.brightness) * led->color_ratio[0] / led->color_ratio[1];
			AW_LOG("id = %d set brightness = %d\n", led->id, led_brightness);
		} else if (led->id == 1) {
			led_brightness = (led->cdev.brightness) * led->color_ratio[0] / led->color_ratio[1];
			AW_LOG("id = %d set brightness = %d\n", led->id, led_brightness);
		} else if (led->id == 2) {
			led_brightness = (led->cdev.brightness) * led->color_ratio[0] / led->color_ratio[1];
			AW_LOG("id = %d set brightness = %d\n", led->id, led_brightness);
		}
		led->br_res = BR_RESOLUTION_8BIT;
		aw210xx_br_res_set(led);
		/* sbmd enable */
		aw210xx_sbmd_set(led, true);

		if (led_groups_num == 8 && aw210xx_id1 != NULL) {
			aw210xx_id1->br_res = BR_RESOLUTION_8BIT;
			aw210xx_br_res_set(aw210xx_id1);
			aw210xx_sbmd_set(aw210xx_id1, true);
		}

		if (led_groups_num == 2) {
			aw210xx_i2c_write(led, AW210XX_REG_SL00 + led->id, led_brightness);
			aw210xx_i2c_write(led, AW210XX_REG_SL03 + led->id, led_brightness);
			aw210xx_i2c_write(led, AW210XX_REG_GCFG, 0x47);
			AW_LOG("id = %d led_groups_num = %d\n", led->id, led_groups_num);
		} else if (led_groups_num == 4) {
			aw210xx_i2c_write(led, AW210XX_REG_SL00 + led->id, led_brightness);
			aw210xx_i2c_write(led, AW210XX_REG_SL03 + led->id, led_brightness);
			aw210xx_i2c_write(led, AW210XX_REG_SL06 + led->id, led_brightness);
			aw210xx_i2c_write(led, AW210XX_REG_SL09 + led->id, led_brightness);
			aw210xx_i2c_write(led, AW210XX_REG_GCFG, 0x4F);
			AW_LOG("id = %d led_groups_num = %d\n", led->id, led_groups_num);
		} else if (led_groups_num == 8 && aw210xx_id1 != NULL) {
			aw210xx_i2c_write(led, AW210XX_REG_SL00 + led->id, led_brightness);
			aw210xx_i2c_write(led, AW210XX_REG_SL03 + led->id, led_brightness);
			aw210xx_i2c_write(led, AW210XX_REG_SL06 + led->id, led_brightness);
			aw210xx_i2c_write(led, AW210XX_REG_SL09 + led->id, led_brightness);
			aw210xx_i2c_write(led, AW210XX_REG_GCFG, 0x4F);
			aw210xx_i2c_write(aw210xx_id1, AW210XX_REG_SL00 + led->id, led_brightness);
			aw210xx_i2c_write(aw210xx_id1, AW210XX_REG_SL03 + led->id, led_brightness);
			aw210xx_i2c_write(aw210xx_id1, AW210XX_REG_SL06 + led->id, led_brightness);
			aw210xx_i2c_write(aw210xx_id1, AW210XX_REG_SL09 + led->id, led_brightness);
			aw210xx_i2c_write(aw210xx_id1, AW210XX_REG_GCFG, 0x4F);
			AW_LOG("id = %d led_groups_num = %d\n", led->id, led_groups_num);
		} else {
			aw210xx_i2c_write(led, AW210XX_REG_SL00 + led->id, led_brightness);
			aw210xx_i2c_write(led, AW210XX_REG_SL03 + led->id, led_brightness);
			aw210xx_i2c_write(led, AW210XX_REG_SL06 + led->id, led_brightness);
			aw210xx_i2c_write(led, AW210XX_REG_SL09 + led->id, led_brightness);
			aw210xx_i2c_write(led, AW210XX_REG_GCFG, 0x4F);
			AW_LOG("id = %d led_groups_num = %d\n", led->id, led_groups_num);
		}
	}

	/*aw210xx led cc mode set up*/
	if (led->pdata->led_mode == AW210XX_LED_CCMODE) {
		aw210xx_i2c_write(led, AW210XX_REG_GBRH, 0x00);
		aw210xx_i2c_write(led, AW210XX_REG_GBRL, 0xff);
		aw210xx_i2c_write(led, AW210XX_REG_ABMCFG, 0x00);
		if (led_groups_num == 8 && aw210xx_id1 != NULL) {
			aw210xx_i2c_write(aw210xx_id1, AW210XX_REG_GBRH, 0x00);
			aw210xx_i2c_write(aw210xx_id1, AW210XX_REG_GBRL, 0xff);
			aw210xx_i2c_write(aw210xx_id1, AW210XX_REG_ABMCFG, 0x00);
		}
	}

	/*aw210xx led breath set up*/
	if (led->pdata->led_mode == AW210XX_LED_BREATHMODE || led->pdata->led_mode == AW210XX_LED_INDIVIDUAL_CTL_BREATH) {
		if(led->pdata->led_mode == AW210XX_LED_BREATHMODE){
			aw210xx_i2c_write(led, AW210XX_REG_ABMT0,
					(led->pdata->rise_time_ms << 4 | led->pdata->hold_time_ms));
			aw210xx_i2c_write(led, AW210XX_REG_ABMT1,
					(led->pdata->fall_time_ms << 4 | led->pdata->off_time_ms));
				aw210xx_i2c_write(led, AW210XX_REG_GBRH, 0xff);
		} else {
			if (led->pdata->rise_time_ms*0xff / brightness_ratio > 0xf) {
				breath_rise_time = 0x0f;
			} else {
				breath_rise_time = led->pdata->rise_time_ms*0xff / brightness_ratio;
			}
			if (led->pdata->fall_time_ms*0xff / brightness_ratio > 0xf) {
				breath_fall_time = 0x0f;
			} else {
				breath_fall_time = led->pdata->fall_time_ms*0xff / brightness_ratio;
			}

			aw210xx_i2c_write(led, AW210XX_REG_ABMT0, (breath_rise_time << 4 | led->pdata->hold_time_ms));
			aw210xx_i2c_write(led, AW210XX_REG_ABMT1, (breath_fall_time << 4 | led->pdata->hold_time_ms));
			aw210xx_i2c_write(led, AW210XX_REG_GBRH, 0xff*brightness_ratio / 255);
			AW_LOG("%s:breath_rise_time=%d, breath_fall_time=%d\n", __func__, breath_rise_time, breath_fall_time);
		}
		aw210xx_i2c_write(led, AW210XX_REG_GBRL, 0x00);
		aw210xx_i2c_write(led, AW210XX_REG_ABMCFG, 0x03);

		if (led_groups_num == 8 && aw210xx_id1 != NULL) {
			aw210xx_i2c_write(aw210xx_id1, AW210XX_REG_ABMT0,
					(led->pdata->rise_time_ms << 4 | led->pdata->hold_time_ms));
			aw210xx_i2c_write(aw210xx_id1, AW210XX_REG_ABMT1,
					(led->pdata->fall_time_ms << 4 | led->pdata->off_time_ms));

			aw210xx_i2c_write(aw210xx_id1, AW210XX_REG_GBRH, 0xff);
			aw210xx_i2c_write(aw210xx_id1, AW210XX_REG_GBRL, 0x00);
			aw210xx_i2c_write(aw210xx_id1, AW210XX_REG_ABMCFG, 0x03);
		}

		for (i = 0; i < max_led; i++)
			cancel_delayed_work(&(&aw210xx_glo[i])->breath_work);

		schedule_delayed_work(&led->breath_work, msecs_to_jiffies(AW_START_TO_BREATH));
	}

	/*aw210xx led blink time*/
	if (led->pdata->led_mode == AW210XX_LED_BLINKMODE) {
		#ifdef BLINK_USE_AW210XX
			aw210xx_i2c_write(led, AW210XX_REG_ABMT0 , 0x04);
			aw210xx_i2c_write(led, AW210XX_REG_ABMT1 , 0x04);

			aw210xx_i2c_write(led, AW210XX_REG_GBRH, 0xff);
			aw210xx_i2c_write(led, AW210XX_REG_GBRL, 0x00);

			aw210xx_i2c_write(led, AW210XX_REG_ABMCFG, 0x03);

			if (led_groups_num == 8 && aw210xx_id1 != NULL) {
				aw210xx_i2c_write(aw210xx_id1, AW210XX_REG_ABMT0 , 0x04);
				aw210xx_i2c_write(aw210xx_id1, AW210XX_REG_ABMT1 , 0x04);

				aw210xx_i2c_write(aw210xx_id1, AW210XX_REG_GBRH, 0xff);
				aw210xx_i2c_write(aw210xx_id1, AW210XX_REG_GBRL, 0x00);

				aw210xx_i2c_write(aw210xx_id1, AW210XX_REG_ABMCFG, 0x03);
			}

			for (i = 0; i < max_led; i++)
				cancel_delayed_work(&(&aw210xx_glo[i])->breath_work);

			schedule_delayed_work(&led->breath_work, msecs_to_jiffies(AW_START_TO_BREATH));
		#else
			aw210xx_i2c_write(led, AW210XX_REG_GBRH, 0x00);
			aw210xx_i2c_write(led, AW210XX_REG_GBRL, 0xff);
			aw210xx_i2c_write(led, AW210XX_REG_ABMCFG, 0x00);
			if (led_groups_num == 8 && aw210xx_id1 != NULL) {
				aw210xx_i2c_write(aw210xx_id1, AW210XX_REG_GBRH, 0x00);
				aw210xx_i2c_write(aw210xx_id1, AW210XX_REG_GBRL, 0xff);
				aw210xx_i2c_write(aw210xx_id1, AW210XX_REG_ABMCFG, 0x00);
			}
		#endif
	}
	/* update */
	aw210xx_update(led);
	if (led_groups_num == 8 && aw210xx_id1 != NULL) {
		aw210xx_update(aw210xx_id1);
	}

	AW_LOG("%s:  brightness[%d]=%x led_mode[%d]=%x \n",__func__,led->id,led_brightness,led->id,led->pdata->led_mode);

out:
	if((effect_state.state[AW210XX_LED_FLOW_CTL_MODE]==1 || effect_state.state[AW210XX_LED_RAINBOW_CTL_MODE]==1)&& !workqueue_flow_flag){
		AW_LOG("workqueue_flow_flag=1\n");
		workqueue_flow_flag = 1;
		queue_delayed_work(led_default->aw210_led_flow, &led_default->flow_led_work,0);
	} else if (((effect_state.state[AW210XX_LED_FLOW_CTL_MODE]==0 &&effect_state.state[AW210XX_LED_RAINBOW_CTL_MODE]==0)||led->cdev.brightness == 0)&&workqueue_flow_flag){
		AW_LOG("workqueue_flow_flag=0\n");
		workqueue_flow_flag = 0;
		cancel_delayed_work(&led_default->flow_led_work);
	}
	if (led->cdev.brightness > 0 && !workqueue_flag) {
		queue_delayed_work(led_default->aw210_led_wq, &led_default->aw210_led_work,LED_ESD_WORK_TIME * HZ);
		workqueue_flag = 1;
		AW_LOG("%s: queue_delayed_work brightness[%d]\n",__func__,led->id);
	} else if ((led->cdev.brightness == 0) && (!led_default->esd_flag) && (workqueue_flag == 1)) {
		if ((ledbri[0] == 0) && (ledbri[1] == 0) && (ledbri[2] == 0)) {
			cancel_delayed_work(&led_default->aw210_led_work);
			workqueue_flag = 0;
			AW_LOG("%s: cancel_delayed_work brightness[%d]\n",__func__,led->id);
		}
	}
}

static void aw210_work_func(struct work_struct *aw210_work)
{
	u8 ret = 0,val = 0;
	int i = 0;
	struct aw210xx *led = container_of(aw210_work, struct aw210xx,aw210_led_work.work);

	AW_LOG("aw210_work_func enter\n");

	if (!led->pdata->led->power_change_state && led->pdata->led->led_enable) {
		ret = aw210xx_i2c_read(led, AW210XX_REG_GCCR, &val);
		if(!val) {
			dev_notice(&led->i2c->dev, "%s AW210_REG_STATUS[%d]:[0x%x]\n",__func__,led->id,val);
			led_default->esd_flag = true;
			AW_LOG("aw210xx_led_init enter\n");
			ret = aw210xx_led_init(led_default);
			if (ret) {
				dev_err(&led->i2c->dev, "%s reset failed :[%d]\n",__func__,ret);
			}
			for (i = 0; i < 3; i++) {
				aw210xx_brightness(aw210xx_g_chip[0][i]);
			}
			if (led_default->led_groups_num == 8) {
				for (i = 0; i < 3; i++) {
					aw210xx_brightness(aw210xx_g_chip[1][i]);
				}
			}
		} else {
			led_default->esd_flag = false;
		}
	} else {
		AW_LOG("led is power off: power_change_state = 0x%x led_enable = %d, brightness= %d, esd_flag=%d, wq_flag = %d\n", led->pdata->led->power_change_state, led->pdata->led->led_enable, led->cdev.brightness, led_default->esd_flag, workqueue_flag);	
	}

	queue_delayed_work(led->aw210_led_wq, &led->aw210_led_work,3 * HZ);
}

static void flow_work_func(struct work_struct *aw210_work)
{
	struct aw210xx *led = container_of(aw210_work, struct aw210xx,flow_led_work.work);

	AW_LOG("flow_work_func enter music_rate=%d\n",music_rate);
	if(effect_state.state[AW210XX_LED_FLOW_CTL_MODE]==1) {
		AW_LOG("flow_work_func enter AW210XX_LED_FLOW_CTL_MODE\n");
		run_flow_effect(led);
	} else if(effect_state.state[AW210XX_LED_RAINBOW_CTL_MODE]==1) {
		AW_LOG("flow_work_func enter AW210XX_LED_RAINBOW_CTL_MODE\n");
		run_rainbow_effect(led);
	}
	aw210xx_update(led);
	queue_delayed_work(led->aw210_led_flow, &led->flow_led_work,msecs_to_jiffies(music_rate));
}
static void aw210xx_breath_func(struct work_struct *work)
{
	struct aw210xx *led = container_of(work, struct aw210xx, breath_work.work);
	struct aw210xx *aw210xx_id1 = NULL;
	int led_groups_num = led->pdata->led->led_groups_num;

	if (led_groups_num == 8) {
		if (aw210xx_g_chip[1][0] == NULL) {
			AW_LOG("aw210xx_g_chip[1][0] is NULL \n");
		} else {
			aw210xx_id1 = aw210xx_g_chip[1][0];
			aw210xx_i2c_write(aw210xx_id1, AW210XX_REG_ABMGO, 0x01);
		}
	}
	aw210xx_i2c_write(led, AW210XX_REG_ABMGO, 0x00);
	aw210xx_i2c_write(led, AW210XX_REG_ABMGO, 0x01);
}

static void aw210xx_brightness_work(struct work_struct *work)
{
	struct aw210xx *aw210xx = container_of(work, struct aw210xx,
			brightness_work);
	int i = 0;

	/*
	 * Cancel/drain all breath_work before this brightness run so a delayed
	 * 0x81 (ABMGO) cannot run concurrently with or after schedule but before
	 * the new config. Complements mutex in breath_func / aw210xx_brightness.
	 * Do not flush_work() sibling brightness_work here: parallel R/G/B
	 * brightness_work could deadlock if each flushes the others.
	 */
	if (aw210xx_glo && max_led > 0) {
		for (i = 0; i < max_led; i++) {
			cancel_delayed_work_sync(&aw210xx_glo[i].breath_work);
		}
		AW_LOG("cancel_delayed_work_sync breath_work enter\n");
	}
	AW_LOG("aw210xx_brightness_work enter\n");
	aw210xx_brightness(aw210xx);
}

static void aw210xx_set_brightness(struct led_classdev *cdev,
		enum led_brightness brightness)
{
	struct aw210xx *aw210xx = container_of(cdev, struct aw210xx, cdev);

	aw210xx->cdev.brightness = brightness;

	if(aw210xx->cdev.trigger != NULL)
	{
		if(strcmp(aw210xx->cdev.trigger->name,"timer") == 0)
		{
			aw210xx_led_change_mode(aw210xx, AW210XX_LED_BLINKMODE);
			AW_LOG("%s[%d]:  trigger = %s\n",__func__,aw210xx->id,aw210xx->cdev.trigger->name);
		}
	}

	if (brightness == 0) {
		cancel_work_sync(&aw210xx->brightness_work);
	}
	schedule_work(&aw210xx->brightness_work);
}

/*****************************************************
* aw210xx basic function set
*****************************************************/
void aw210xx_chipen_set(struct aw210xx *aw210xx, bool flag)
{
	if (flag) {
		aw210xx_i2c_write_bits(aw210xx,
				AW210XX_REG_GCR,
				AW210XX_BIT_CHIPEN_MASK,
				AW210XX_BIT_CHIPEN_ENABLE);
	} else {
		aw210xx_i2c_write_bits(aw210xx,
				AW210XX_REG_GCR,
				AW210XX_BIT_CHIPEN_MASK,
				AW210XX_BIT_CHIPEN_DISENA);
	}
}

static int aw210xx_hw_enable(struct aw210xx *aw210xx, bool flag)
{
	int ret = 0;
	int led_groups_num = 0;
	struct aw210xx *aw210xx_id1 = NULL;

	if (aw210xx == NULL){
		AW_LOG("aw210xx is NULL\n");
		return -1;
	}

	AW_LOG("enter\n");

	led_groups_num = aw210xx->led_groups_num;
	if (led_groups_num == 8) {
		if (aw210xx_g_chip[1][0] == NULL) {
			AW_LOG("aw210xx_g_chip[1][0] is NULL \n");
		} else {
			aw210xx_id1 = aw210xx_g_chip[1][0];
		}
	}

	if (gpio_is_valid(aw210xx->enable_gpio) && gpio_is_valid(aw210xx->vbled_enable_gpio)) {
		if (flag) {
			gpio_set_value_cansleep(aw210xx->vbled_enable_gpio, 1);
			usleep_range(50, 60);
			gpio_set_value_cansleep(aw210xx->enable_gpio, 1);
			if (aw210xx->led_groups_num == 8 && aw210xx_id1 != NULL) {
				gpio_set_value_cansleep(aw210xx_id1->enable_gpio, 1);
			}
			aw210xx_i2c_write(aw210xx, AW210XX_REG_RESET, 0x00);
			if (aw210xx->led_groups_num == 8 && aw210xx_id1 != NULL) {
				aw210xx_i2c_write(aw210xx_id1, AW210XX_REG_RESET, 0x00);
			}
			usleep_range(2000, 2500);
			aw210xx_led_init(aw210xx);
			if (aw210xx->led_groups_num == 8 && aw210xx_id1 != NULL) {
				aw210xx_led_init(aw210xx_id1);
			}
			aw210xx->led_enable = true;
		} else {
			gpio_set_value_cansleep(aw210xx->enable_gpio, 0);
			if (aw210xx->led_groups_num == 8 && aw210xx_id1 != NULL) {
				gpio_set_value_cansleep(aw210xx_id1->enable_gpio, 0);
			}
			gpio_set_value_cansleep(aw210xx->vbled_enable_gpio, 0);
			aw210xx->led_enable = false;
		}
	} else if (gpio_is_valid(aw210xx->enable_gpio) && (!IS_ERR_OR_NULL(aw210xx->vbled))) {
		if (flag) {
			gpio_set_value_cansleep(aw210xx->enable_gpio, 1);
			if (aw210xx->led_groups_num == 8 && aw210xx_id1 != NULL) {
				gpio_set_value_cansleep(aw210xx_id1->enable_gpio, 1);
			}
			AW_ERR("Enable the Regulator vbled.\n");
			ret = regulator_enable(aw210xx->vbled);
			if (ret) {
				AW_ERR("Regulator vbled enable failed ret = %d\n", ret);
			}
			aw210xx_i2c_write(aw210xx, AW210XX_REG_RESET, 0x00);
			if (aw210xx->led_groups_num == 8 && aw210xx_id1 != NULL) {
				aw210xx_i2c_write(aw210xx_id1, AW210XX_REG_RESET, 0x00);
			}
			usleep_range(2000, 2500);
			aw210xx_led_init(aw210xx);
			if (aw210xx->led_groups_num == 8 && aw210xx_id1 != NULL) {
				aw210xx_led_init(aw210xx_id1);
			}
			aw210xx->led_enable = true;
		} else {
			gpio_set_value_cansleep(aw210xx->enable_gpio, 0);
			if (aw210xx->led_groups_num == 8 && aw210xx_id1 != NULL) {
				gpio_set_value_cansleep(aw210xx_id1->enable_gpio, 0);
			}
			AW_ERR("Disable the Regulator vbled.\n");
			ret = regulator_disable(aw210xx->vbled);
			if (ret) {
				AW_ERR("Regulator vbled disable failed ret = %d\n", ret);
			}
			aw210xx->led_enable = false;
		}
	} else {
		AW_ERR("failed\n");
	}
	return 0;
}

static int aw210xx_led_init(struct aw210xx *aw210xx)
{
	AW_LOG("enter\n");

	aw210xx->sdmd_flag = 0;
	aw210xx->rgbmd_flag = 0;
	/* chip enable */
	aw210xx_chipen_set(aw210xx, true);
	/* sbmd enable */
	aw210xx_sbmd_set(aw210xx, true);
	/* rgbmd enable */
	aw210xx_rgbmd_set(aw210xx, false);
	/* clk_pwm selsect */
	aw210xx_osc_pwm_set(aw210xx);
	/* br_res select */
	aw210xx_br_res_set(aw210xx);
	/* 2. check id */
	/*aw_i2c_read_one_byte(REG_RESET, &val);*/ /* 0x7f 0x18 */
	/*if (val != AW210XX_CHIPID)
	AW_LOG("%s:read chip id failed. val = %#x\n", __func__, val);*/

	/* global set */
	aw210xx_global_set(aw210xx);
	/* under voltage lock out */
	aw210xx_uvlo_set(aw210xx, false);
	/* apse enable */
	aw210xx_apse_set(aw210xx, false);
	return 0;
}

static int32_t aw210xx_group_gcfg_set(struct aw210xx *aw210xx, bool flag)
{
	if (flag) {
		switch (aw210xx->chipid) {
		case AW21018_CHIPID:
			aw210xx_i2c_write(aw210xx, AW210XX_REG_GCFG,
					  AW21018_GROUP_ENABLE);
			return 0;
		case AW21012_CHIPID:
			aw210xx_i2c_write(aw210xx, AW210XX_REG_GCFG,
					  AW21012_GROUP_ENABLE);
			return 0;
		case AW21009_CHIPID:
			aw210xx_i2c_write(aw210xx, AW210XX_REG_GCFG,
					  AW21009_GROUP_ENABLE);
			return 0;
		default:
			AW_LOG("%s: chip is unsupported device!\n",
			       __func__);
			return -AW210XX_CHIPID_FAILD;
		}
	} else {
		switch (aw210xx->chipid) {
		case AW21018_CHIPID:
			aw210xx_i2c_write(aw210xx, AW210XX_REG_GCFG,
					  AW21018_GROUP_DISABLE);
			return 0;
		case AW21012_CHIPID:
			aw210xx_i2c_write(aw210xx, AW210XX_REG_GCFG,
					  AW21012_GROUP_DISABLE);
			return 0;
		case AW21009_CHIPID:
			aw210xx_i2c_write(aw210xx, AW210XX_REG_GCFG,
					  AW21009_GROUP_DISABLE);
			return 0;
		default:
			AW_LOG("%s: chip is unsupported device!\n",
			       __func__);
			return -AW210XX_CHIPID_FAILD;
		}
	}
}

void aw210xx_singleled_set(struct aw210xx *aw210xx,
		uint32_t rgb_reg,
		uint32_t rgb_sl,
		uint32_t rgb_br)
{
	/* chip enable */
	aw210xx_chipen_set(aw210xx, true);
	/* global set */
	aw210xx->set_current = rgb_br;
	aw210xx_current_set(aw210xx);
	/* group set disable */
	aw210xx_group_gcfg_set(aw210xx, false);

	aw210xx_sbmd_set(aw210xx, true);
	aw210xx_rgbmd_set(aw210xx, false);
	aw210xx_uvlo_set(aw210xx, true);

	/* set sl */
	aw210xx->rgbcolor = rgb_sl & 0xff;
	aw210xx_i2c_write(aw210xx, AW210XX_REG_SL00 + rgb_reg,
			  aw210xx->rgbcolor);

	/* br set */
	if (aw210xx->sdmd_flag == 1) {
		if (aw210xx->rgbmd_flag == 1) {
			aw210xx_i2c_write(aw210xx,
					  AW210XX_REG_BR00L + rgb_reg,
					  rgb_br);
		} else {
			aw210xx_i2c_write(aw210xx,
					  AW210XX_REG_BR00L + rgb_reg,
					  rgb_br);
		}
	} else {
		if (aw210xx->rgbmd_flag == 1) {
			aw210xx_i2c_write(aw210xx,
					  AW210XX_REG_BR00L + rgb_reg,
					  rgb_br);
			aw210xx_i2c_write(aw210xx,
					  AW210XX_REG_BR00H + rgb_reg,
					  rgb_br);
		} else {
			aw210xx_i2c_write(aw210xx,
					  AW210XX_REG_BR00L + rgb_reg,
					  rgb_br);
			aw210xx_i2c_write(aw210xx,
					  AW210XX_REG_BR00H + rgb_reg,
					  rgb_br);
		}
	}
	/* update */
	aw210xx_update(aw210xx);
}

/*****************************************************
* open short detect
*****************************************************/
void aw210xx_open_detect_cfg(struct aw210xx *aw210xx)
{
	/*enable open detect*/
	aw210xx_i2c_write(aw210xx, AW210XX_REG_OSDCR, AW210XX_OPEN_DETECT_EN);
	/*set DCPWM = 1*/
	aw210xx_i2c_write_bits(aw210xx, AW210XX_REG_SSCR,
							AW210XX_DCPWM_SET_MASK,
							AW210XX_DCPWM_SET);
	/*set Open threshold = 0.2v*/
	aw210xx_i2c_write_bits(aw210xx,
							AW210XX_REG_OSDCR,
							AW210XX_OPEN_THRESHOLD_SET_MASK,
							AW210XX_OPEN_THRESHOLD_SET);
}

void aw210xx_short_detect_cfg(struct aw210xx *aw210xx)
{

	/*enable short detect*/
	aw210xx_i2c_write(aw210xx, AW210XX_REG_OSDCR, AW210XX_SHORT_DETECT_EN);
	/*set DCPWM = 1*/
	aw210xx_i2c_write_bits(aw210xx, AW210XX_REG_SSCR,
							AW210XX_DCPWM_SET_MASK,
							AW210XX_DCPWM_SET);
	/*set Short threshold = 1v*/
	aw210xx_i2c_write_bits(aw210xx,
							AW210XX_REG_OSDCR,
							AW210XX_SHORT_THRESHOLD_SET_MASK,
							AW210XX_SHORT_THRESHOLD_SET);
}

void aw210xx_open_short_dis(struct aw210xx *aw210xx)
{
	aw210xx_i2c_write(aw210xx, AW210XX_REG_OSDCR, AW210XX_OPEN_SHORT_DIS);
	/*SET DCPWM = 0*/
	aw210xx_i2c_write_bits(aw210xx, AW210XX_REG_SSCR,
							AW210XX_DCPWM_SET_MASK,
							AW210XX_DCPWM_CLEAN);
}

void aw210xx_open_short_detect(struct aw210xx *aw210xx,
										int32_t detect_flg, u8 *reg_val)
{
	/*config for open shor detect*/
	if (detect_flg == AW210XX_OPEN_DETECT)
		aw210xx_open_detect_cfg(aw210xx);
	else if (detect_flg == AW210XX_SHORT_DETECT)
		aw210xx_short_detect_cfg(aw210xx);
	/*read detect result*/
	aw210xx_i2c_read(aw210xx, AW210XX_REG_OSST0, &reg_val[0]);
	aw210xx_i2c_read(aw210xx, AW210XX_REG_OSST1, &reg_val[1]);
	aw210xx_i2c_read(aw210xx, AW210XX_REG_OSST2, &reg_val[2]);
	/*close for open short detect*/
	aw210xx_open_short_dis(aw210xx);
}

/******************************************************
 *
 * sys group attribute: reg
 *
 ******************************************************/
static ssize_t aw210xx_reg_store(struct device *dev,
		struct device_attribute *attr, const char *buf,
		size_t len)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct aw210xx *aw210xx = container_of(led_cdev, struct aw210xx, cdev);
	uint32_t databuf[2] = { 0, 0 };

	if (sscanf(buf, "%x %x", &databuf[0], &databuf[1]) == 2) {
		if (aw210xx_reg_access[(uint8_t)databuf[0]] & REG_WR_ACCESS)
			aw210xx_i2c_write(aw210xx, (uint8_t)databuf[0],
					(uint8_t)databuf[1]);
	}

	return len;
}

static ssize_t aw210xx_reg_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct aw210xx *aw210xx = container_of(led_cdev, struct aw210xx, cdev);
	ssize_t len = 0;
	unsigned int i = 0;
	unsigned char reg_val = 0;
	uint8_t br_max = 0;
	uint8_t sl_val = 0;

	aw210xx_i2c_read(aw210xx, AW210XX_REG_GCR, &reg_val);
	len += snprintf(buf + len, PAGE_SIZE - len,
			"reg:0x%02x=0x%02x\n", AW210XX_REG_GCR, reg_val);
	switch (aw210xx->chipid) {
	case AW21018_CHIPID:
		br_max = AW210XX_REG_BR17H;
		sl_val = AW210XX_REG_SL17;
		break;
	case AW21012_CHIPID:
		br_max = AW210XX_REG_BR11H;
		sl_val = AW210XX_REG_SL11;
		break;
	case AW21009_CHIPID:
		br_max = AW210XX_REG_BR08H;
		sl_val = AW210XX_REG_SL08;
		break;
	default:
		AW_LOG("chip is unsupported device!\n");
		return len;
	}

	for (i = AW210XX_REG_BR00L; i <= br_max; i++) {
		if (!(aw210xx_reg_access[i] & REG_RD_ACCESS))
			continue;
		aw210xx_i2c_read(aw210xx, i, &reg_val);
		len += snprintf(buf + len, PAGE_SIZE - len,
				"reg:0x%02x=0x%02x\n", i, reg_val);
	}
	for (i = AW210XX_REG_SL00; i <= sl_val; i++) {
		if (!(aw210xx_reg_access[i] & REG_RD_ACCESS))
			continue;
		aw210xx_i2c_read(aw210xx, i, &reg_val);
		len += snprintf(buf + len, PAGE_SIZE - len,
				"reg:0x%02x=0x%02x\n", i, reg_val);
	}
	for (i = AW210XX_REG_GCCR; i <= AW210XX_REG_GCFG; i++) {
		if (!(aw210xx_reg_access[i] & REG_RD_ACCESS))
			continue;
		aw210xx_i2c_read(aw210xx, i, &reg_val);
		len += snprintf(buf + len, PAGE_SIZE - len,
				"reg:0x%02x=0x%02x\n", i, reg_val);
	}

	return len;
}

static ssize_t aw210xx_hwen_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t len)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct aw210xx *aw210xx = container_of(led_cdev, struct aw210xx, cdev);
	int rc;
	unsigned int val = 0;

	rc = kstrtouint(buf, 0, &val);
	if (rc < 0)
		return rc;

	if (val > 0)
		aw210xx_hw_enable(aw210xx, true);
	else
		aw210xx_hw_enable(aw210xx, false);

	return len;
}

static ssize_t aw210xx_hwen_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct aw210xx *aw210xx = container_of(led_cdev, struct aw210xx, cdev);
	ssize_t len = 0;

	len += snprintf(buf + len, PAGE_SIZE - len, "hwen=%d\n",
			gpio_get_value(aw210xx->enable_gpio));
	return len;
}

static ssize_t aw210xx_rgbcolor_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t len)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct aw210xx *aw210xx = container_of(led_cdev, struct aw210xx, cdev);
	uint32_t rgb_num = 0;
	uint32_t rgb_data = 0;

	if (sscanf(buf, "%x %x", &rgb_num, &rgb_data) == 2) {
		aw210xx_chipen_set(aw210xx, true);
		aw210xx_sbmd_set(aw210xx, true);
		aw210xx_rgbmd_set(aw210xx, true);
		aw210xx_global_set(aw210xx);
		aw210xx_uvlo_set(aw210xx, true);

		/* set sl */
		aw210xx->rgbcolor = (rgb_data & 0xff0000) >> 16;
		aw210xx_i2c_write(aw210xx,
				AW210XX_REG_SL00 + (uint8_t)rgb_num * 3,
				aw210xx->rgbcolor);

		aw210xx->rgbcolor = (rgb_data & 0x00ff00) >> 8;
		aw210xx_i2c_write(aw210xx,
				AW210XX_REG_SL00 + (uint8_t)rgb_num * 3 + 1,
				aw210xx->rgbcolor);

		aw210xx->rgbcolor = (rgb_data & 0x0000ff);
		aw210xx_i2c_write(aw210xx,
				AW210XX_REG_SL00 + (uint8_t)rgb_num * 3 + 2,
				aw210xx->rgbcolor);

		/* br set */
		aw210xx_i2c_write(aw210xx,
				AW210XX_REG_BR00L + (uint8_t)rgb_num,
				AW210XX_GLOBAL_DEFAULT_SET);

		/* update */
		aw210xx_update(aw210xx);
	}

	return len;
}

static ssize_t aw210xx_singleled_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t len)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct aw210xx *aw210xx = container_of(led_cdev, struct aw210xx, cdev);
	uint32_t led_num = 0;
	uint32_t rgb_data = 0;
	uint32_t rgb_brightness = 0;

	if (sscanf(buf, "%x %x %x", &led_num, &rgb_data, &rgb_brightness) == 3) {
		if (aw210xx->chipid == AW21018_CHIPID) {
			if (led_num > AW21018_LED_NUM)
				led_num = AW21018_LED_NUM;
		} else if (aw210xx->chipid == AW21012_CHIPID) {
			if (led_num > AW21012_LED_NUM)
				led_num = AW21012_LED_NUM;
		} else {
			if (led_num > AW21009_LED_NUM)
				led_num = AW21009_LED_NUM;
		}
		aw210xx_singleled_set(aw210xx, led_num, rgb_data, rgb_brightness);
	}

	return len;
}

static ssize_t aw210xx_opdetect_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct aw210xx *aw210xx = container_of(led_cdev, struct aw210xx, cdev);
	ssize_t len = 0;
	int i = 0;
	uint8_t reg_val[3] = {0};

	aw210xx_open_short_detect(aw210xx, AW210XX_OPEN_DETECT, reg_val);
	for (i = 0; i < sizeof(reg_val); i++)
		len += snprintf(buf + len, PAGE_SIZE - len,
					"OSST%d:%#x\n", i, reg_val[i]);

	return len;
}

static ssize_t aw210xx_stdetect_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct aw210xx *aw210xx = container_of(led_cdev, struct aw210xx, cdev);
	ssize_t len = 0;
	int i = 0;
	uint8_t reg_val[3] = {0};

	aw210xx_open_short_detect(aw210xx, AW210XX_SHORT_DETECT, reg_val);
	for (i = 0; i < sizeof(reg_val); i++)
		len += snprintf(buf + len, PAGE_SIZE - len,
					"OSST%d:%#x\n", i, reg_val[i]);
	return len;
}

static ssize_t aw210xx_led_br_attr_show (struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct aw210xx *led = container_of(led_cdev, struct aw210xx, cdev);

	return snprintf(buf, PAGE_SIZE, "%d,%d,%d,%d (max:4095)\n",led->pdata->br_brightness[0],led->pdata->br_brightness[1],
			led->pdata->br_brightness[2], led->pdata->br_brightness[3]);
}

static ssize_t aw210xx_led_br_attr_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t cnt)
{
	unsigned long data = 0;
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct aw210xx *led = container_of(led_cdev, struct aw210xx, cdev);
	char *delim = ",";
	char *str = NULL;
	char *p_buf, *tmp_buf;
	int i = 0, led_br[4] = {0}, led_order[4] = {0};
	ssize_t ret = -EINVAL;

	AW_LOG("br input is %s\n", buf);

	p_buf = kstrdup(buf, GFP_KERNEL);
	tmp_buf = p_buf;
	led_order[0] = led->pdata->led->led_allocation_order[0];
	led_order[1] = led->pdata->led->led_allocation_order[1];
	led_order[2] = led->pdata->led->led_allocation_order[2];
	led_order[3] = led->pdata->led->led_allocation_order[3];
	AW_LOG("[%d]: led_order=%d,%d,%d,%d.\n",
		led->id, led_order[0], led_order[1], led_order[2], led_order[3]);

	for (i = 0; i < 4; i++) {
		str = strsep(&tmp_buf, delim);
		if (str == NULL) {
			break;
		}
		ret = kstrtoul(str, 10, &data);
		if (ret) {
			kfree(p_buf);
			return ret;
		}
		AW_LOG("[%d]: led_color=%lu, i=%d.\n",led->id ,data, i);
		led_br[i] = data;
	}

	if (i == 4) {
		AW_LOG("[%d]: led_color=%d,%d,%d,%d.\n",
			led->id, led_br[0], led_br[1], led_br[2], led_br[3]);
		mutex_lock(&led->pdata->led->lock);
		led->pdata->br_brightness[led_order[0]] = led_br[0];
		led->pdata->br_brightness[led_order[1]] = led_br[1];
		led->pdata->br_brightness[led_order[2]] = led_br[2];
		led->pdata->br_brightness[led_order[3]] = led_br[3];
		mutex_unlock(&led->pdata->led->lock);
		AW_LOG("[%d]: led_br_brightness=%d,%d,%d,%d\n",led->id, led->pdata->br_brightness[0], led->pdata->br_brightness[1],
			led->pdata->br_brightness[2], led->pdata->br_brightness[3]);
	} else {
		AW_LOG("[%d]: input error.\n",led->id);
	}
	kfree(p_buf);
	return cnt;
}

static ssize_t aw210xx_led_color_attr_show (struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct aw210xx *led = container_of(led_cdev, struct aw210xx, cdev);

	return snprintf(buf, PAGE_SIZE, "%d,%d,%d,%d,%d,%d,%d,%d (max:255)\n",led->pdata->color[0], led->pdata->color[1], led->pdata->color[2],
			led->pdata->color[3], led->pdata->color[4], led->pdata->color[5], led->pdata->color[6], led->pdata->color[7]);
}

static ssize_t aw210xx_led_color_attr_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t cnt)
{
	unsigned long data = 0;
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct aw210xx *led = container_of(led_cdev, struct aw210xx, cdev);
	char *delim = ",";
	char *str = NULL;
	char *p_buf, *tmp_buf;
	int i = 0, j = 0, led_color[LED_MAX_NUM] = {0};
	int *led_order;
	int led_groups_num = led->pdata->led->led_groups_num;
	ssize_t ret = -EINVAL;

	AW_LOG("color input is %s\n", buf);

	p_buf = kstrdup(buf, GFP_KERNEL);
	tmp_buf = p_buf;
	led_order = led->pdata->led->led_allocation_order;

	AW_LOG("[%d]: led_order=%d,%d,%d,%d,%d,%d,%d,%d.\n",
		led->id, led_order[0], led_order[1], led_order[2], led_order[3], led_order[4], led_order[5], led_order[6], led_order[7]);

	for (i = 0; i < LED_MAX_NUM; i++) {
		str = strsep(&tmp_buf, delim);
		if (str == NULL) {
			break;
		}
		ret = kstrtoul(str, 16, &data);
		if (ret) {
			kfree(p_buf);
			return ret;
		}

		led_color[i] = data;
	}

	if (i == LED_MAX_NUM) {
		AW_LOG("[%d]: led_color=%x,%x,%x,%x,%x,%x,%x,%x.\n",
			led->id, led_color[0], led_color[1], led_color[2], led_color[3], led_color[4], led_color[5], led_color[6], led_color[7]);
		mutex_lock(&led->pdata->led->lock);
		for (j = 0; j < led_groups_num; j++) {
			led->pdata->color[led_order[j]] = led_color[j];
		}
		mutex_unlock(&led->pdata->led->lock);
		AW_LOG("[%d]: led_color=%x,%x,%x,%x,%x,%x,%x,%x (max:ffffff)\n", led->id, led->pdata->color[0], led->pdata->color[1], led->pdata->color[2],
				led->pdata->color[3], led->pdata->color[4], led->pdata->color[5], led->pdata->color[6], led->pdata->color[7]);
	} else {
		AW_LOG("[%d]: input error.\n",led->id);
	}
	kfree(p_buf);
	return cnt;
}

void store_effect_data(char *tmp_buf)
{
	unsigned long data = 0;
	char *delim = ",";
	char *str = NULL;
	int i = 0;
	int effectindex=0, dataindex=0;
	int ret =0;

	str = strsep(&tmp_buf, delim);
	if (str == NULL) {
		return;
	}
	ret = kstrtoul(str, 10, &data);
	if (ret) {
		return;
	}
	effectindex = data;
	AW_LOG("effectindex = %d \n",effectindex);
	if(effectindex > MAX_EFFECT)
	{
		return;
	}

	str = strsep(&tmp_buf, delim);
	if (str == NULL) {
		return;
	}
	ret = kstrtoul(str, 10, &data);
	if (ret) {
		return;
	}
	dataindex=data;
	AW_LOG("dataindex = %d \n",dataindex);
	if(dataindex >= aw210xx_cfg_array[effectindex].count)
	{
		return;
	}
	str = strsep(&tmp_buf, delim);
	if (str == NULL) {
		return;
	}
	ret = kstrtoul(str, 10, &data);
	if (ret) {
		return;
	}
	if(data>=1)
	{
		aw210xx_cfg_array[effectindex].p[dataindex].frame_factor = data;
	}
	for (i = 0; i < 6; i++) {
		str = strsep(&tmp_buf, delim);
		if (str == NULL) {
			break;
		}
		ret = kstrtoul(str, 10, &data);
		if (ret) {
			return;
		}
		aw210xx_cfg_array[effectindex].p[dataindex].time[i] = data;
		AW_LOG("time[%d] = %lu \n",i, data);
	}
	str = strsep(&tmp_buf, delim);
	if (str == NULL) {
		return;
	}
	ret = kstrtoul(str, 10, &data);
	if (ret) {
		return;
	}
	aw210xx_cfg_array[effectindex].p[dataindex].repeat_nums = data;
	AW_LOG("repeat_nums = %lu \n", data);

	str = strsep(&tmp_buf, delim);
	if (str == NULL) {
		return;
	}
	ret = kstrtoul(str, 10, &data);
	if (ret) {
		return;
	}
	aw210xx_cfg_array[effectindex].p[dataindex].color_nums = data;
	AW_LOG("color_nums = %lu \n", data);
	for (i=0;i<aw210xx_cfg_array[effectindex].p[dataindex].color_nums;i++){
		str = strsep(&tmp_buf, delim);
		if (str == NULL) {
			return;
		}
		ret = kstrtoul(str, 10, &data);
		if (ret) {
			return;
		}
		aw210xx_cfg_array[effectindex].p[dataindex].rgb_color_list[i].r = data;
		AW_LOG("rgb_color_list[%d].r = %lu \n",i,data);
		str = strsep(&tmp_buf, delim);
		if (str == NULL) {
			return;
		}
		ret = kstrtoul(str, 10, &data);
		if (ret) {
			return;
		}
		aw210xx_cfg_array[effectindex].p[dataindex].rgb_color_list[i].g = data;
		AW_LOG("rgb_color_list[%d].g = %lu \n", i,data);
		str = strsep(&tmp_buf, delim);
		if (str == NULL) {
			return;
		}
		ret = kstrtoul(str, 10, &data);
		if (ret) {
			return;
		}
		aw210xx_cfg_array[effectindex].p[dataindex].rgb_color_list[i].b = data;
		AW_LOG("rgb_color_list[%d].b = %lu \n",i, data);
	}

	str = strsep(&tmp_buf, delim);
	if (str == NULL) {
		return;
	}
	ret = kstrtoul(str, 10, &data);
	if (ret) {
		return;
	}
	aw210xx_cfg_array[effectindex].p[dataindex].fadeh[0].r = data;

	str = strsep(&tmp_buf, delim);
	if (str == NULL) {
		return;
	}
	ret = kstrtoul(str, 10, &data);
	if (ret) {
		return;
	}
	aw210xx_cfg_array[effectindex].p[dataindex].fadeh[0].g = data;

	str = strsep(&tmp_buf, delim);
	if (str == NULL) {
		return;
	}
	ret = kstrtoul(str, 10, &data);
	if (ret) {
		return;
	}
	aw210xx_cfg_array[effectindex].p[dataindex].fadeh[0].b = data;
	AW_LOG("fadeh.r = %d, fadeh.g =%d, fadeh.b = %d \n", aw210xx_cfg_array[effectindex].p[dataindex].fadeh[0].r, aw210xx_cfg_array[effectindex].p[dataindex].fadeh[0].g, aw210xx_cfg_array[effectindex].p[dataindex].fadeh[0].b);

	str = strsep(&tmp_buf, delim);
	if (str == NULL) {
		return;
	} 
	ret = kstrtoul(str, 10, &data);
	if (ret) {
		return;
	}
	aw210xx_cfg_array[effectindex].p[dataindex].fadel[0].r = data;

	str = strsep(&tmp_buf, delim);
	if (str == NULL) {
		return;
	} 
	ret = kstrtoul(str, 10, &data);
	if (ret) {
		return;
	}
	aw210xx_cfg_array[effectindex].p[dataindex].fadel[0].g = data;

	str = strsep(&tmp_buf, delim);
	if (str == NULL) {
		return;
	} 
	ret = kstrtoul(str, 10, &data);
	if (ret) {
		return;
	}
	aw210xx_cfg_array[effectindex].p[dataindex].fadel[0].b = data;
	AW_LOG("fadel.r = %d, fadel.g = %d, fadel.b = %d \n", aw210xx_cfg_array[effectindex].p[dataindex].fadel[0].r, aw210xx_cfg_array[effectindex].p[dataindex].fadel[0].g, aw210xx_cfg_array[effectindex].p[dataindex].fadel[0].b);

}

void store_effect_color_and_brightness(char *tmp_buf)
{
	unsigned long data = 0;
	char *delim = ",";
	char *str = NULL;
	int i = 0,ret=0;
	int gli=0;
	int effect_index=0;
	unsigned int br_rgb = 0;
	unsigned int rgb = 0,prev_rgb = 0;

	int *led_order = aw210xx_g_chip[0][0]->pdata->led->led_allocation_order;
	/*
	store datatype,effect_index , br_rgb(in hexadecimal), rgb(in hexadecimal),...
	0,0,br_rgb,rgb    // incall
	0,1,br_rgb,rgb    // poweron
	0,2,br_rgb,rgb,rgb,rgb,rgb,rgb,rgb,rgb,rgb  // music mode
	0,3,br_rgb,rgb,rgb,rgb,rgb,rgb,rgb,rgb,rgb  // game mode
	0,4,br_rgb,rgb    // notify
	0,5,br_rgb,rgb,rgb,rgb,rgb,rgb,rgb,rgb,rgb // always on mode
	*/

	str = strsep(&tmp_buf, delim);
	if (str == NULL) {
		AW_ERR("error in parsing 1.1");
		return;
	}
	ret = kstrtoul(str, 10, &data);
	if (ret) {
		AW_ERR("error in parsing 1.2");
		return;
	}
	effect_index = data;

	str = strsep(&tmp_buf, delim);
	if (str == NULL) {
		AW_ERR("error in parsing 1.1");
		return;
	}
	ret = kstrtoul(str, 16, &data);
	if (ret) {
		AW_ERR("error in parsing 1.2");
		return;
	}
	br_rgb = data;

	str = strsep(&tmp_buf, delim);
	if (str == NULL) {
		AW_ERR("error in parsing 1.1");
		return;
	}
	ret = kstrtoul(str, 16, &data);
	if (ret) {
		AW_ERR("error in parsing 1.2");
		return;
	}
	rgb = data;

	switch(effect_index) {
		case INCALL_EFFECT:
			rgb_incall_list[0].r = (rgb >> 16) & 0xff; rgb_incall_list[0].g = (rgb >> 8) & 0xff; rgb_incall_list[0].b = rgb & 0xff;
			br_incall_fadeh[0].r = (br_rgb >> 16) & 0xff; br_incall_fadeh[0].g = (br_rgb >> 8) & 0xff; br_incall_fadeh[0].b = br_rgb & 0xff;
			break;
		case POWERON_EFFECT:
			rgb_poweron_list[0].r = (rgb >> 16) & 0xff; rgb_poweron_list[0].g = (rgb >> 8) & 0xff; rgb_poweron_list[0].b = rgb & 0xff;
			br_poweron_fadeh[0].r = (br_rgb >> 16) & 0xff; br_poweron_fadeh[0].g = (br_rgb >> 8) & 0xff; br_poweron_fadeh[0].b = br_rgb & 0xff;
			break;
		case CHARGE_EFFECT:
			rgb_green_list[0].r = (rgb >> 16) & 0xff; rgb_green_list[0].g = (rgb >> 8) & 0xff; rgb_green_list[0].b = rgb & 0xff;
			br_green_fadeh[0].r = (br_rgb >> 16) & 0xff; br_green_fadeh[0].g = (br_rgb >> 8) & 0xff; br_green_fadeh[0].b = br_rgb & 0xff;
			break;
		case GAME_ENTER_EFFECT:
			br_game_fadeh[0].r = (br_rgb >> 16) & 0xff; br_game_fadeh[0].g = (br_rgb >> 8) & 0xff; br_game_fadeh[0].b = br_rgb & 0xff;
			rgb_multi_game_racing[0].r = (rgb >> 16) & 0xff; rgb_multi_game_racing[0].g = (rgb >> 8) & 0xff; rgb_multi_game_racing[0].b = rgb & 0xff;
			rgb_multi_game_lap[0].r = (rgb >> 16) & 0xff; rgb_multi_game_lap[0].g = (rgb >> 8) & 0xff; rgb_multi_game_lap[0].b = rgb & 0xff;
			gli = 1;
			prev_rgb = rgb;
			for(i=1;i<8;i++)
			{
				str = strsep(&tmp_buf, delim);
				if (str == NULL) {
					AW_ERR("error in parsing 1.1");
					return;
				}
				ret = kstrtoul(str, 16, &data);
				if (ret) {
					AW_ERR("error in parsing 1.2");
					return;
				}
				rgb = data;
				rgb_multi_game_racing[i].r = (rgb >> 16) & 0xff; rgb_multi_game_racing[i].g = (rgb >> 8) & 0xff; rgb_multi_game_racing[i].b = rgb & 0xff;
				if ((rgb != prev_rgb) && (gli < sizeof(rgb_multi_game_lap)/sizeof(AW_COLOR_STRUCT) ))
				{
					rgb_multi_game_lap[gli].r = (rgb >> 16) & 0xff; rgb_multi_game_lap[gli].g = (rgb >> 8) & 0xff; rgb_multi_game_lap[gli].b = rgb & 0xff;
					gli++;
					prev_rgb = rgb;
				}
			}
			for(i=gli;i< sizeof(rgb_multi_game_lap)/sizeof(AW_COLOR_STRUCT); i++)
			{
				rgb_multi_game_lap[i].r = rgb_multi_game_lap[i%gli].r; 
				rgb_multi_game_lap[i].g = rgb_multi_game_lap[i%gli].g;
				rgb_multi_game_lap[i].b = rgb_multi_game_lap[i%gli].b;
			}
			break;
		case NOTIFY_EFFECT:
			effect_state.data[AW210XX_LED_NOTIFY_MODE] = 1;
			rgb_notify_list[0].r = (rgb >> 16) & 0xff; rgb_notify_list[0].g = (rgb >> 8) & 0xff; rgb_notify_list[0].b = rgb & 0xff;
			br_notify_fadeh[0].r = (br_rgb >> 16) & 0xff; br_notify_fadeh[0].g = (br_rgb >> 8) & 0xff; br_notify_fadeh[0].b = br_rgb & 0xff;
			break;
		case NEWALWAYSON_EFFECT:
			effect_state.data[AW210XX_LED_NEW_ALWAYSON] = 1;
			new_always_on_color[0][8] = (br_rgb >> 16) & 0xff; new_always_on_color[1][8]= (br_rgb >> 8) & 0xff; new_always_on_color[2][8] = br_rgb & 0xff;
			new_always_on_color[0][led_order[0]] = (rgb >> 16) & 0xff; 
			new_always_on_color[1][led_order[0]] = (rgb >>8) & 0xff; 
			new_always_on_color[2][led_order[0]] = rgb & 0xff; 
			for(i=1;i<8;i++)
			{
				str = strsep(&tmp_buf, delim);
				if (str == NULL) {
					AW_ERR("error in parsing 1.1");
					return;
				}
				ret = kstrtoul(str, 16, &data);
				if (ret) {
					AW_ERR("error in parsing 1.2");
					return;
				}
				rgb = data;
				if(led_order[i] <8) {
					new_always_on_color[0][led_order[i]] = (rgb >> 16) & 0xff;
					new_always_on_color[1][led_order[i]] = (rgb >>8) & 0xff;
					new_always_on_color[2][led_order[i]] = rgb & 0xff;
				}
				else {
					AW_ERR("led_order[i]  is out of index");
				}
			}
			break;
		case FLOW_EFFECT:
			effect_state.data[AW210XX_LED_FLOW_CTL_MODE] = 1;
			flow_color[0][8] = (br_rgb >> 16) & 0xff; flow_color[1][8]= (br_rgb >> 8) & 0xff; flow_color[2][8] = br_rgb & 0xff;
			flow_color[0][led_order[0]] = (rgb >> 16) & 0xff;
			flow_color[1][led_order[0]] = (rgb >>8) & 0xff;
			flow_color[2][led_order[0]] = rgb & 0xff;
			for(i=1;i<8;i++)
			{
				str = strsep(&tmp_buf, delim);
				if (str == NULL) {
					AW_ERR("error in parsing 1.1");
					return;
				}
				ret = kstrtoul(str, 16, &data);
				if (ret) {
					AW_ERR("error in parsing 1.2");
					return;
				}
				rgb = data;
				if(led_order[i] <8) {
					flow_color[0][led_order[i]] = (rgb >> 16) & 0xff;
					flow_color[1][led_order[i]] = (rgb >>8) & 0xff;
					flow_color[2][led_order[i]] = rgb & 0xff;
				}
				else {
					AW_ERR("led_order[i]  is out of index");
				}
			}
			break;
		case RAINBOW_EFFECT:
			effect_state.data[AW210XX_LED_RAINBOW_CTL_MODE] = 1;
			bainbow_color[0][8] = (br_rgb >> 16) & 0xff; bainbow_color[1][8]= (br_rgb >> 8) & 0xff; bainbow_color[2][8] = br_rgb & 0xff;
			bainbow_color[0][led_order[0]] = (rgb >> 16) & 0xff;
			bainbow_color[1][led_order[0]] = (rgb >>8) & 0xff;
			bainbow_color[2][led_order[0]] = rgb & 0xff;
			for(i=1;i<8;i++)
			{
				str = strsep(&tmp_buf, delim);
				if (str == NULL) {
					AW_ERR("error in parsing 1.1");
					return;
				}
				ret = kstrtoul(str, 16, &data);
				if (ret) {
					AW_ERR("error in parsing 1.2");
					return;
				}
				rgb = data;
				if(led_order[i] <8) {
					bainbow_color[0][led_order[i]] = (rgb >> 16) & 0xff;
					bainbow_color[1][led_order[i]] = (rgb >>8) & 0xff;
					bainbow_color[2][led_order[i]] = rgb & 0xff;
				}
				else {
					AW_ERR("led_order[i]  is out of index");
				}
			}
			break;
		case MUSIC_EFFECT:
			effect_state.data[AW210XX_LED_MUSICMODE] = 1;
			music_color[0][8] = (br_rgb >> 16) & 0xff; music_color[1][8]= (br_rgb >> 8) & 0xff; music_color[2][8] = br_rgb & 0xff;
			music_color[0][led_order[0]] = (rgb >> 16) & 0xff;
			music_color[1][led_order[0]] = (rgb >>8) & 0xff;
			music_color[2][led_order[0]] = rgb & 0xff;
			for(i=1;i<8;i++)
			{
				str = strsep(&tmp_buf, delim);
				if (str == NULL) {
					AW_ERR("error in parsing 1.1");
					return;
				}
				ret = kstrtoul(str, 16, &data);
				if (ret) {
					AW_ERR("error in parsing 1.2");
					return;
				}
				rgb = data;
				if(led_order[i] <8) {
					music_color[0][led_order[i]] = (rgb >> 16) & 0xff;
					music_color[1][led_order[i]] = (rgb >>8) & 0xff;
					music_color[2][led_order[i]] = rgb & 0xff;
				}
				else {
					AW_ERR("led_order[i]  is out of index");
				}
			}
			break;
		default:
			break;
	}
}

static ssize_t aw210xx_effectdata_attr_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t cnt)
{
	unsigned long data = 0;
	char *delim = ",";
	char *str = NULL;
	char *p_buf, *tmp_buf;

	ssize_t ret = -EINVAL;

	AW_LOG("effectdata raw %s\n", buf);

	p_buf = kstrdup(buf, GFP_KERNEL);
	tmp_buf = p_buf;

	str = strsep(&tmp_buf, delim);
	if (str == NULL) {
		goto errout;
	}
	ret = kstrtoul(str, 10, &data);
	if (ret) {
		goto errout;
	}

	if(data == 0){
		store_effect_color_and_brightness(tmp_buf);
	}
	else {
		store_effect_data(tmp_buf);
	}

errout:
	kfree(p_buf);
	return cnt;
}

static ssize_t aw210xx_l_type_attr_show (struct device *dev,
	struct device_attribute *attr, char *buf)
{
	int i=0;
	int len =0;

	for(i=0;i<AW210XX_LED_MAXMODE; i++)
	{
		len += snprintf(buf, PAGE_SIZE - len, " %d = %d, ",i, effect_state.state[i]);
	}
	len += snprintf(buf, PAGE_SIZE - len, " (max:%d)", AW210XX_LED_MAXMODE -1 );
	return len;
}

static ssize_t aw210xx_l_type_attr_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t cnt)
{
	unsigned long data = 0;
	char *delim = ",";
	char *str = NULL;
	char *p_buf, *tmp_buf;
	//int i = 0;
	int lmode=0,val=0;

	ssize_t ret = -EINVAL;

	AW_LOG("l_type raw %s\n", buf);

	p_buf = kstrdup(buf, GFP_KERNEL);
	tmp_buf = p_buf;

	str = strsep(&tmp_buf, delim);
	if (str == NULL) {
		goto errout;
	}
	ret = kstrtoul(str, 10, &data);
	if (ret) {
		goto errout;
	}
	lmode = data;

	str = strsep(&tmp_buf, delim);
	if (str == NULL) {
		goto errout;
	}
	ret = kstrtoul(str, 10, &data);
	if (ret) {
		goto errout;
	}
	val = data;

	if(lmode >=0 &&  lmode<AW210XX_LED_MAXMODE)
	{
		str = strsep(&tmp_buf, delim);
		if (str == NULL) {
			goto errout;
		}
		ret = kstrtoul(str, 10, &data);
		if (ret) {
			goto errout;
		}

		if( lmode == AW210XX_LED_CHARGE_MODE ){
			if( data <= 4 ) {  /* remove (data >=0) as data is unsigned so always >=0 to avoid coverity issue  */
				effect_state.data[AW210XX_LED_CHARGE_MODE] = data;
			}
			if( val == 0)
				effect_state.state[lmode] = val;

			if(effect_state.state[lmode] != 0) {
				AW_ERR("skip charger state setting state = %d\n", effect_state.state[lmode] );
				goto errout;
			}
		}
		effect_state.state[lmode] = val;
	}

errout:
	kfree(p_buf);
	return cnt;
}

static ssize_t aw210xx_light_sensor_attr_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t cnt)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct aw210xx *aw210xx = container_of(led_cdev, struct aw210xx, cdev);
	u32 value;
	aw210xx = aw210xx->pdata->led;

	if (sscanf(buf, "%d", &value) != 1) {
		AW_ERR("invaild arguments\n");
		return cnt;
	}
	aw210xx->light_sensor_state = value;
	aw210xx_global_set(aw210xx);
	if ((aw210xx->led_groups_num == 8) && (aw210xx_g_chip[1][0] != NULL)) {
		aw210xx_g_chip[1][0]->light_sensor_state = value;
		aw210xx_global_set(aw210xx_g_chip[1][0]);
	}

	AW_ERR("light_sensor value %d\n", value);
	return cnt;
}

static ssize_t aw210xx_light_sensor_attr_show (struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct aw210xx *aw210xx = container_of(led_cdev, struct aw210xx, cdev);
	ssize_t len = 0;
	aw210xx = aw210xx->pdata->led;
	len += snprintf(buf + len, PAGE_SIZE - len, "light_sensor: %d\n", aw210xx->light_sensor_state);
	return len;
}

static ssize_t aw210xx_global_current_attr_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t cnt)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct aw210xx *aw210xx = container_of(led_cdev, struct aw210xx, cdev);

	unsigned long data = 0;
	char *delim = ",";
	char *str = NULL;
	char *p_buf, *tmp_buf;
	ssize_t ret = -EINVAL;

	aw210xx = aw210xx->pdata->led;

	p_buf = kstrdup(buf, GFP_KERNEL);
	tmp_buf = p_buf;

	str = strsep(&tmp_buf, delim);
	if (str == NULL) {
		goto errout;
	}
	ret = kstrtoul(str, 16, &data);
	if (ret) {
		goto errout;
	}
	if(data < 0x100) {
		aw210xx->glo_current_min = data;
	}
	else {
		aw210xx->glo_current_min = 0xff;
	}

	str = strsep(&tmp_buf, delim);
	if (str == NULL) {
		goto errout;
	}
	ret = kstrtoul(str, 16, &data);
	if (ret) {
		goto errout;
	}
	if(data < 0x100) {
		aw210xx->glo_current_max = data;
	}
	else {
		aw210xx->glo_current_max = 0xff;
	}

	aw210xx_global_set(aw210xx);
	if ((aw210xx->led_groups_num == 8) && (aw210xx_g_chip[1][0] != NULL)) {
		aw210xx_g_chip[1][0]->glo_current_min = aw210xx->glo_current_min;
		aw210xx_g_chip[1][0]->glo_current_max = aw210xx->glo_current_max;
		aw210xx_global_set(aw210xx_g_chip[1][0]);
	}

	AW_ERR("gccr values min = %x max = %x\n", aw210xx->glo_current_min, aw210xx->glo_current_max);

errout:
	kfree(p_buf);
	return cnt;
}

static ssize_t aw210xx_global_current_attr_show (struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct aw210xx *aw210xx = container_of(led_cdev, struct aw210xx, cdev);
	ssize_t len = 0;
	aw210xx = aw210xx->pdata->led;
	len += snprintf(buf + len, PAGE_SIZE - len, "gccr values min = %x max = %x\n", aw210xx->glo_current_min, aw210xx->glo_current_max);
	return len;
}

static ssize_t aw210xx_led_num_attr_show (struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct aw210xx *led = container_of(led_cdev, struct aw210xx, cdev);

	return snprintf(buf, PAGE_SIZE, "%d\n",led->pdata->led->led_groups_num);
}

static ssize_t aw210xx_led_color_ratio_attr_show (struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct aw210xx *led = container_of(led_cdev, struct aw210xx, cdev);

	return snprintf(buf, PAGE_SIZE, "%d,%d\n",led->color_ratio[0], led->color_ratio[1]);
}

static ssize_t aw210xx_led_ton_attr_show (struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct aw210xx *led = container_of(led_cdev, struct aw210xx, cdev);

	return snprintf(buf, PAGE_SIZE, "%d (max:15)\n",led->pdata->hold_time_ms);
}

static ssize_t aw210xx_led_ton_attr_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t cnt)
{
	unsigned long data = 0;
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct aw210xx *led = container_of(led_cdev, struct aw210xx, cdev);

	ssize_t ret = -EINVAL;

	ret = kstrtoul(buf, 10, &data);
	if (ret)
		return ret;

	mutex_lock(&led->pdata->led->lock);
	led->pdata->hold_time_ms = data;
	mutex_unlock(&led->pdata->led->lock);
	AW_LOG("[%d]: hold_time_ms=%d (max:15)\n",led->id,led->pdata->hold_time_ms);

	return cnt;
}

static ssize_t aw210xx_led_tr1_attr_show (struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct aw210xx *led = container_of(led_cdev, struct aw210xx, cdev);

	return snprintf(buf, PAGE_SIZE, "%d (max:15)\n",led->pdata->rise_time_ms);
}

static ssize_t aw210xx_led_tr1_attr_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t cnt)
{
	unsigned long data = 0;
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct aw210xx *led = container_of(led_cdev, struct aw210xx, cdev);

	ssize_t ret = -EINVAL;

	ret = kstrtoul(buf, 10, &data);
	if (ret)
		return ret;

	mutex_lock(&led->pdata->led->lock);
	led->pdata->rise_time_ms = data;
	mutex_unlock(&led->pdata->led->lock);

	AW_LOG("[%d]: rise_time_ms=%d (max:15)\n",led->id,led->pdata->rise_time_ms);

	return cnt;
}

static ssize_t aw210xx_led_tf1_attr_show (struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct aw210xx *led = container_of(led_cdev, struct aw210xx, cdev);

	return snprintf(buf, PAGE_SIZE, "%d (max:15)\n",led->pdata->fall_time_ms);

}

static ssize_t aw210xx_led_tf1_attr_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t cnt)
{
	unsigned long data = 0;
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct aw210xx *led = container_of(led_cdev, struct aw210xx, cdev);

	ssize_t ret = -EINVAL;

	ret = kstrtoul(buf, 10, &data);
	if (ret)
		return ret;

	mutex_lock(&led->pdata->led->lock);
	led->pdata->fall_time_ms = data;
	mutex_unlock(&led->pdata->led->lock);

	AW_LOG("[%d]: fall_time_ms=%d (max:15)\n",led->id,led->pdata->fall_time_ms);

	return cnt;
}

static ssize_t aw210xx_led_toff_attr_show (struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct aw210xx *led = container_of(led_cdev, struct aw210xx, cdev);

	return snprintf(buf, PAGE_SIZE, "%d (max:15)\n",led->pdata->off_time_ms);
}

static ssize_t aw210xx_led_toff_attr_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t cnt)
{
	unsigned long data = 0;
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct aw210xx *led = container_of(led_cdev, struct aw210xx, cdev);

	ssize_t ret = -EINVAL;

	ret = kstrtoul(buf, 10, &data);
	if (ret)
		return ret;

	mutex_lock(&led->pdata->led->lock);
	led->pdata->off_time_ms = data;
	mutex_unlock(&led->pdata->led->lock);
	AW_LOG("[%d]: off_time_ms=%d (max:15)\n",led->id,led->pdata->off_time_ms);

	return cnt;
}

static ssize_t aw210xx_led_brightness_ratio_attr_show (struct device *dev,
	struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%x\n",brightness_ratio);

}

static ssize_t aw210xx_led_brightness_ratio_attr_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t cnt)
{
	unsigned long data = 0;
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct aw210xx *led = container_of(led_cdev, struct aw210xx, cdev);
	int i = 0;

	ssize_t ret = -EINVAL;

	ret = kstrtoul(buf, 10, &data);
	if (ret)
		return ret;

	mutex_lock(&led->pdata->led->lock);
	brightness_ratio = data;
	for(i=0;i<3;i++){
		aw210xx_i2c_write(led, AW210XX_REG_BR00L + 2*i, brightness_ratio);
		aw210xx_i2c_write(led, AW210XX_REG_BR03L + 2*i, brightness_ratio);
		aw210xx_i2c_write(led, AW210XX_REG_BR06L + 2*i, brightness_ratio);
	}
	aw210xx_update(led);
	mutex_unlock(&led->pdata->led->lock);

	AW_LOG("[%d]: brightness_ratio=%x\n",led->id,brightness_ratio);

	return cnt;
}

static ssize_t aw210xx_led_music_rate_attr_show (struct device *dev,
	struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%x\n",music_rate);

}

static ssize_t aw210xx_led_music_rate_attr_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t cnt)
{
	unsigned long data = 0;
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct aw210xx *led = container_of(led_cdev, struct aw210xx, cdev);

	ssize_t ret = -EINVAL;

	ret = kstrtoul(buf, 10, &data);
	if (ret)
		return ret;

	mutex_lock(&led->pdata->led->lock);
	if(music_timer){
		music_timer--;
	}else {
		if(data > 4){
			music_rate = 50;
			music_timer = 5;
		}else if(data > 2 && data < 4){
			music_rate = 200;
		}else {
			music_rate = 400;
		}
	}
	mutex_unlock(&led->pdata->led->lock);

	AW_LOG("[%d]: music_rate=%d\n",led->id,music_rate);

	return cnt;
}
void run_flow_effect(struct aw210xx *aw210xx)
{
	int i = 0;
	int Rotation_color[3][9];

	if(effect_state.data[AW210XX_LED_FLOW_CTL_MODE] == 1&&aw210xx->cdev.brightness != 0) {
		music_rate = 300;
		effect_state.data[AW210XX_LED_FLOW_CTL_MODE] = 0;
		aw210xx_i2c_write(aw210xx, AW210XX_REG_GCFG, 0x00);
		aw210xx_i2c_write(aw210xx,AW210XX_REG_GCR2, 0x0c);
		for(i=0;i<3;i++){
			aw210xx_i2c_write(aw210xx, AW210XX_REG_BR00L + 2*i, flow_color[i][8]*brightness_ratio/255);
			aw210xx_i2c_write(aw210xx, AW210XX_REG_BR03L + 2*i, flow_color[i][8]*brightness_ratio/255);
			aw210xx_i2c_write(aw210xx, AW210XX_REG_BR06L + 2*i, flow_color[i][8]*brightness_ratio/255);

			AW_LOG("[%d]: flow_color[0]= %x,flow_color[1]= %x,flow_color[2]= %x \n",i,flow_color[i][0],flow_color[i][1],flow_color[i][2]);
			aw210xx_i2c_write(aw210xx, AW210XX_REG_SL00 + i, flow_color[i][0]);
			aw210xx_i2c_write(aw210xx, AW210XX_REG_SL03 + i, flow_color[i][1]);
			aw210xx_i2c_write(aw210xx, AW210XX_REG_SL06 + i, flow_color[i][2]);
		}
		for(i=0;i<3;i++){
			temp_color[0][i] = flow_color[0][i];
			temp_color[1][i] = flow_color[1][i];
			temp_color[2][i] = flow_color[2][i];
		}
	}

	for(i=0;i<3;i++){
		Rotation_color[0][i] = temp_color[0][i];
		Rotation_color[1][i] = temp_color[1][i];
		Rotation_color[2][i] = temp_color[2][i];
	}

	for(i=0;i<3;i++){
		aw210xx_i2c_write(aw210xx,AW210XX_REG_SL00+i,Rotation_color[i][0]);
		aw210xx_i2c_write(aw210xx,AW210XX_REG_SL03+i,Rotation_color[i][1]);
		aw210xx_i2c_write(aw210xx,AW210XX_REG_SL06+i,Rotation_color[i][2]);
	}

	for(i=0;i<3;i++){
		Rotation_color[0][i] = temp_color[0][(i+1)%3];
		Rotation_color[1][i] = temp_color[1][(i+1)%3];
		Rotation_color[2][i] = temp_color[2][(i+1)%3];
	}
	for(i=0;i<3;i++){
		temp_color[0][i] = Rotation_color[0][i];
		temp_color[1][i] = Rotation_color[1][i];
		temp_color[2][i] = Rotation_color[2][i];
	}

}


void run_rainbow_effect(struct aw210xx *aw210xx)
{
	int i = 0;
	int Rotation_color[3][9];

	if(effect_state.data[AW210XX_LED_RAINBOW_CTL_MODE] == 1&&aw210xx->cdev.brightness != 0) {
		effect_state.data[AW210XX_LED_RAINBOW_CTL_MODE] = 0;
		aw210xx_i2c_write(aw210xx, AW210XX_REG_GCFG, 0x00);
		aw210xx_i2c_write(aw210xx,AW210XX_REG_GCR2, 0x0c);
		for(i=0;i<3;i++){
			aw210xx_i2c_write(aw210xx, AW210XX_REG_BR00L + 2*i, bainbow_color[i][8]*brightness_ratio/255);
			aw210xx_i2c_write(aw210xx, AW210XX_REG_BR03L + 2*i, bainbow_color[i][8]*brightness_ratio/255);
			aw210xx_i2c_write(aw210xx, AW210XX_REG_BR06L + 2*i, bainbow_color[i][8]*brightness_ratio/255);

			aw210xx_i2c_write(aw210xx, AW210XX_REG_SL00 + i, bainbow_color[i][0]);
			aw210xx_i2c_write(aw210xx, AW210XX_REG_SL03 + i, bainbow_color[i][1]);
			aw210xx_i2c_write(aw210xx, AW210XX_REG_SL06 + i, bainbow_color[i][2]);
		}
		for(i=0;i<8;i++){
			temp_color[0][i] = bainbow_color[0][i];
			temp_color[1][i] = bainbow_color[1][i];
			temp_color[2][i] = bainbow_color[2][i];
		}
	}

	for(i=0;i<8;i++){
		Rotation_color[0][i] = temp_color[0][i];
		Rotation_color[1][i] = temp_color[1][i];
		Rotation_color[2][i] = temp_color[2][i];
	}

	for(i=0;i<3;i++){
		aw210xx_i2c_write(aw210xx,AW210XX_REG_SL00+i,Rotation_color[i][0]);
		aw210xx_i2c_write(aw210xx,AW210XX_REG_SL03+i,Rotation_color[i][1]);
		aw210xx_i2c_write(aw210xx,AW210XX_REG_SL06+i,Rotation_color[i][2]);
	}

	for(i=0;i<8;i++){
		Rotation_color[0][i] = temp_color[0][(i+1)%8];
		Rotation_color[1][i] = temp_color[1][(i+1)%8];
		Rotation_color[2][i] = temp_color[2][(i+1)%8];
	}

	for(i=0;i<8;i++){
		temp_color[0][i] = Rotation_color[0][i];
		temp_color[1][i] = Rotation_color[1][i];
		temp_color[2][i] = Rotation_color[2][i];
	}

}
static ssize_t aw210xx_led_support_attr_show (struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct aw210xx *led = container_of(led_cdev, struct aw210xx, cdev);

//	return snprintf(buf, PAGE_SIZE, "%s-%d-white\n", LED_SUPPORT_TYPE, (int)led->cdev.max_brightness);
	return snprintf(buf, PAGE_SIZE, "%s-%d\n",LED_SUPPORT_TYPE,led->cdev.max_brightness);
}

static DEVICE_ATTR(support, 0664, aw210xx_led_support_attr_show, NULL);
static DEVICE_ATTR(reg, 0664, aw210xx_reg_show, aw210xx_reg_store);
static DEVICE_ATTR(hwen, 0664, aw210xx_hwen_show, aw210xx_hwen_store);
static DEVICE_ATTR(rgbcolor, 0664, NULL, aw210xx_rgbcolor_store);
static DEVICE_ATTR(singleled, 0664, NULL, aw210xx_singleled_store);
static DEVICE_ATTR(opdetect, 0664, aw210xx_opdetect_show, NULL);
static DEVICE_ATTR(stdetect, 0664, aw210xx_stdetect_show, NULL);
static DEVICE_ATTR(br, 0664, aw210xx_led_br_attr_show, aw210xx_led_br_attr_store);
static DEVICE_ATTR(color, 0664, aw210xx_led_color_attr_show, aw210xx_led_color_attr_store);
static DEVICE_ATTR(effectdata, 0664, NULL, aw210xx_effectdata_attr_store);
static DEVICE_ATTR(l_type, 0664, aw210xx_l_type_attr_show, aw210xx_l_type_attr_store);
static DEVICE_ATTR(light_sensor, 0664, aw210xx_light_sensor_attr_show, aw210xx_light_sensor_attr_store);
static DEVICE_ATTR(global_current, 0664, aw210xx_global_current_attr_show, aw210xx_global_current_attr_store);
static DEVICE_ATTR(led_num, 0664, aw210xx_led_num_attr_show, NULL);
static DEVICE_ATTR(color_ratio, 0664, aw210xx_led_color_ratio_attr_show, NULL);
static DEVICE_ATTR(ton, 0664, aw210xx_led_ton_attr_show, aw210xx_led_ton_attr_store);
static DEVICE_ATTR(toff, 0664, aw210xx_led_toff_attr_show, aw210xx_led_toff_attr_store);
static DEVICE_ATTR(tr1, 0664, aw210xx_led_tr1_attr_show, aw210xx_led_tr1_attr_store);
static DEVICE_ATTR(tf1, 0664, aw210xx_led_tf1_attr_show, aw210xx_led_tf1_attr_store);
static DEVICE_ATTR(brightness_ratio, 0664, aw210xx_led_brightness_ratio_attr_show, aw210xx_led_brightness_ratio_attr_store);
static DEVICE_ATTR(music_rate, 0664, aw210xx_led_music_rate_attr_show, aw210xx_led_music_rate_attr_store);

static struct attribute *aw210xx_attributes[] = {
	&dev_attr_support.attr,
	&dev_attr_reg.attr,
	&dev_attr_hwen.attr,
	&dev_attr_rgbcolor.attr,
	&dev_attr_singleled.attr,
	&dev_attr_opdetect.attr,
	&dev_attr_stdetect.attr,
	&dev_attr_br.attr,
	&dev_attr_color.attr,
	&dev_attr_effectdata.attr,
	&dev_attr_l_type.attr,
	&dev_attr_light_sensor.attr,
	&dev_attr_global_current.attr,
	&dev_attr_led_num.attr,
	&dev_attr_color_ratio.attr,
	&dev_attr_ton.attr,
	&dev_attr_toff.attr,
	&dev_attr_tr1.attr,
	&dev_attr_tf1.attr,
	&dev_attr_brightness_ratio.attr,
	&dev_attr_music_rate.attr,
	NULL,
};

static struct attribute_group aw210xx_attribute_group = {
	.attrs = aw210xx_attributes
};

static int aw210xx_led_err_handle(struct aw210xx *led_array,
		int parsed_leds)
{
	int i=0;
	/*
	* If probe fails, cannot free resource of all LEDs, only free
	* resources of LEDs which have allocated these resource really.
	*/
	for (i = 0; i < parsed_leds; i++) {
		sysfs_remove_group(&led_array[i].cdev.dev->kobj,
				&aw210xx_attribute_group);
		led_classdev_unregister(&led_array[i].cdev);
		cancel_work_sync(&led_array[i].brightness_work);
		devm_kfree(&led_array->i2c->dev, led_array[i].pdata);
		led_array[i].pdata = NULL;
	}
	return i;
}

/******************************************************
 *
 * led class dev
 ******************************************************/
static int aw210xx_parse_led_cdev(struct aw210xx *aw210xx,
		struct device_node *node)
{
	struct aw210xx *led;
	struct aw210xx_platform_data *pdata;
	int rc = -1, parsed_leds = 0;
	int color_ratio[2] = {0};
	int driver_ic_id = 0;
	struct device_node *temp;

	AW_LOG("enter\n");
	driver_ic_id = aw210xx->aw210xx_led_id;
	if (driver_ic_id == 0) {
		aw210xx_glo = aw210xx;
	}
	for_each_child_of_node(node, temp) {
		led = &aw210xx[parsed_leds];
		aw210xx_g_chip[driver_ic_id][parsed_leds] = led;
		led->i2c = aw210xx->i2c;

		pdata = devm_kzalloc(&led->i2c->dev,
				sizeof(struct aw210xx_platform_data),
				GFP_KERNEL);
		if (!pdata) {
			dev_err(&led->i2c->dev,
				"Failed to allocate memory\n");
			goto free_err;
		}
		pdata->led = aw210xx;
		led->pdata = pdata;

		rc = of_property_read_string(temp, "aw210xx,name",
			&led->cdev.name);
		if (rc < 0) {
			dev_err(&led->i2c->dev,
				"Failure reading led name, rc = %d\n", rc);
			goto free_pdata;
		}

		rc = of_property_read_u32(temp, "aw210xx,id",
			&led->id);
		if (rc < 0) {
			dev_err(&led->i2c->dev,
				"Failure reading id, rc = %d\n", rc);
			goto free_pdata;
		}

		rc = of_property_read_u32_array(temp, "aw210xx,color_ratio", color_ratio, 2);
		if (rc) {
			dev_err(&led->i2c->dev,
				"Failure reading color_ratio, rc = %d\n", rc);
			if (led->id < 2) {
				led->color_ratio[0] = 2;
				led->color_ratio[1] = 3;
			} else {
				led->color_ratio[0] = 1;
				led->color_ratio[1] = 3;
			}
		} else {
			led->color_ratio[0] = color_ratio[0];
			led->color_ratio[1] = color_ratio[1];
		}

		rc = of_property_read_u32(temp, "aw210xx,imax",
			&led->pdata->imax);
		if (rc < 0) {
			dev_err(&led->i2c->dev,
				"Failure reading imax, rc = %d\n", rc);
			goto free_pdata;
		}

		rc = of_property_read_string(temp, "aw210xx,led_default_trigger",
									&led->pdata->led_default_trigger);
		if (rc < 0) {
			dev_err(&led->i2c->dev,
				"Failure led_default_trigger, rc = %d\n", rc);
			goto free_pdata;
		}

		rc = of_property_read_u32(temp, "aw210xx,brightness",
			&led->cdev.brightness);
		if (rc < 0) {
			dev_err(&led->i2c->dev,
				"Failure reading brightness, rc = %d\n", rc);
			goto free_pdata;
		}

		rc = of_property_read_u32(temp, "aw210xx,max-brightness",
			&led->cdev.max_brightness);
		if (rc < 0) {
			dev_err(&led->i2c->dev,
				"Failure reading max-brightness, rc = %d\n",
				rc);
			goto free_pdata;
		}

		rc = of_property_read_u32(temp, "aw210xx,rise-time-ms",
			&led->pdata->rise_time_ms);
		if (rc < 0) {
			dev_err(&led->i2c->dev,
				"Failure reading rise-time-ms, rc = %d\n", rc);
			goto free_pdata;
		}

		rc = of_property_read_u32(temp, "aw210xx,hold-time-ms",
			&led->pdata->hold_time_ms);
		if (rc < 0) {
			dev_err(&led->i2c->dev,
				"Failure reading hold-time-ms, rc = %d\n", rc);
			goto free_pdata;
		}

		rc = of_property_read_u32(temp, "aw210xx,fall-time-ms",
			&led->pdata->fall_time_ms);
		if (rc < 0) {
			dev_err(&led->i2c->dev,
				"Failure reading fall-time-ms, rc = %d\n", rc);
			goto free_pdata;
		}

		rc = of_property_read_u32(temp, "aw210xx,off-time-ms",
			&led->pdata->off_time_ms);
		if (rc < 0) {
			dev_err(&led->i2c->dev,
				"Failure reading off-time-ms, rc = %d\n", rc);
			goto free_pdata;
		}

		if (aw210xx->aw210xx_led_id == 0) {
			INIT_WORK(&led->brightness_work, aw210xx_brightness_work);
			INIT_DELAYED_WORK(&led->breath_work, aw210xx_breath_func);

			led->cdev.brightness_set = aw210xx_set_brightness;
	//		led->cdev.default_trigger = pdata->led_default_trigger;


			rc = led_classdev_register(&led->i2c->dev, &led->cdev);
			if (rc) {
				dev_err(&led->i2c->dev,
					"unable to register led %d,rc=%d\n",
					led->id, rc);
				goto free_pdata;
			}
			rc = sysfs_create_group(&led->cdev.dev->kobj,
					&aw210xx_attribute_group);
			if (rc) {
				dev_err(&led->i2c->dev, "led sysfs rc: %d, %d, %d\n", rc, aw210xx->aw210xx_led_id, parsed_leds);
				goto free_class;
			}
		}

		parsed_leds++;
	}
	max_led = parsed_leds;
	return 0;

free_class:
	aw210xx_led_err_handle(aw210xx, parsed_leds);
	led_classdev_unregister(&aw210xx[parsed_leds].cdev);
	cancel_work_sync(&aw210xx[parsed_leds].brightness_work);
	devm_kfree(&led->i2c->dev, aw210xx[parsed_leds].pdata);
	aw210xx[parsed_leds].pdata = NULL;
	return rc;

free_pdata:
	aw210xx_led_err_handle(aw210xx, parsed_leds);
	devm_kfree(&led->i2c->dev, aw210xx[parsed_leds].pdata);
	return rc;

free_err:
	aw210xx_led_err_handle(aw210xx, parsed_leds);
	return rc;
}

static int aw210xx_led_change_mode(struct aw210xx *led,
		enum AW2023_LED_MODE mode)
{
	int ret=0;
	switch(mode) {
		case AW210XX_LED_CCMODE:
			led->pdata->led_mode = AW210XX_LED_CCMODE;
			break;
		case AW210XX_LED_NEW_ALWAYSON:
			led->pdata->led_mode = AW210XX_LED_NEW_ALWAYSON;
			break;
		case AW210XX_LED_FLOW_CTL_MODE:
			led->pdata->led_mode = AW210XX_LED_FLOW_CTL_MODE;
			break;
		case AW210XX_LED_RAINBOW_CTL_MODE:
			led->pdata->led_mode = AW210XX_LED_RAINBOW_CTL_MODE;
			break;
		case AW210XX_LED_BLINKMODE:
			led->pdata->hold_time_ms = 4;
			led->pdata->off_time_ms =  4;
			led->pdata->rise_time_ms = 0;
			led->pdata->fall_time_ms = 0;
			led->pdata->led_mode = AW210XX_LED_BLINKMODE;
			break;
		case AW210XX_LED_BREATHMODE:
			led->pdata->hold_time_ms = 0;
			led->pdata->off_time_ms =  0;
			led->pdata->rise_time_ms = 6;
			led->pdata->fall_time_ms = 6;
			led->pdata->led_mode = AW210XX_LED_BREATHMODE;
			break;
		case AW210XX_LED_INDIVIDUAL_CTL_BREATH:
			led->pdata->hold_time_ms = 0;
			led->pdata->off_time_ms =  0;
			led->pdata->rise_time_ms = 6;
			led->pdata->fall_time_ms = 6;
			led->pdata->led_mode = AW210XX_LED_INDIVIDUAL_CTL_BREATH;
			break;
		case AW210XX_LED_MUSICMODE:
			led->pdata->led_mode = AW210XX_LED_MUSICMODE;
			break;
		case AW210XX_LED_INCALL_MODE:
			led->pdata->led_mode = AW210XX_LED_INCALL_MODE;
			break;
		case AW210XX_LED_NOTIFY_MODE:
			led->pdata->led_mode = AW210XX_LED_NOTIFY_MODE;
			break;
		case AW210XX_LED_CHARGE_MODE:
			led->pdata->led_mode = AW210XX_LED_CHARGE_MODE;
			break;
		case AW210XX_LED_POWERON_MODE:
			led->pdata->led_mode = AW210XX_LED_POWERON_MODE;
			break;
		case AW210XX_LED_GAMEMODE:
			led->pdata->led_mode = AW210XX_LED_GAMEMODE;
			break;
		default:
			led->pdata->led_mode = AW210XX_LED_NONE;
			break;
	}
	return ret;
}

/*
static struct attribute *aw210xx_led_cc_mode_attrs[] = {
};

static struct attribute *aw210xx_led_blink_mode_attrs[] = {
};

static struct attribute *aw210xx_led_breath_mode_attrs[] = {
};

ATTRIBUTE_GROUPS(aw210xx_led_cc_mode);
ATTRIBUTE_GROUPS(aw210xx_led_blink_mode);
ATTRIBUTE_GROUPS(aw210xx_led_breath_mode);
*/
static int	aw210xx_led_cc_activate(struct led_classdev *cdev)
{
	int ret = 0;
	struct aw210xx *led = container_of(cdev, struct aw210xx, cdev);

	AW_LOG("[%d]: activate",led->id);

	ret = aw210xx_led_change_mode(led, AW210XX_LED_CCMODE);
	if (ret < 0) {
		dev_err(led->cdev.dev, "%s: aw210xx_led_change_mode fail\n", __func__);
		return ret;
	}

	return ret;
}

static void  aw210xx_led_cc_deactivate(struct led_classdev *cdev)
{
	struct aw210xx *led = container_of(cdev, struct aw210xx, cdev);
	aw210xx_led_change_mode(led, AW210XX_LED_NONE);
	AW_LOG("[%d]: deactivate",led->id);
}

static int aw210xx_led_new_always_on_activate(struct led_classdev *cdev)
{
	int ret = 0;
	struct aw210xx *led = container_of(cdev, struct aw210xx, cdev);

	AW_LOG("[%d]: activate",led->id);

	ret = aw210xx_led_change_mode(led, AW210XX_LED_NEW_ALWAYSON);
	if (ret < 0) {
		dev_err(led->cdev.dev, "%s: aw210xx_led_change_mode fail\n", __func__);
		return ret;
	}

	return ret;
}

static void  aw210xx_led_new_always_on_deactivate(struct led_classdev *cdev)
{
	struct aw210xx *led = container_of(cdev, struct aw210xx, cdev);
	aw210xx_led_change_mode(led, AW210XX_LED_NONE);
	AW_LOG("[%d]: new_always_on deactivate",led->id);
}

static int	aw210xx_led_blink_activate(struct led_classdev *cdev)
{
	int ret = 0;
	struct aw210xx *led = container_of(cdev, struct aw210xx, cdev);

	AW_LOG("[%d]: activate",led->id);

	ret = aw210xx_led_change_mode(led, AW210XX_LED_BLINKMODE);
	if (ret < 0) {
		dev_err(led->cdev.dev, "%s: aw210xx_led_change_mode fail\n", __func__);
		return ret;
	}

	return ret;
}

static void aw210xx_led_blink_deactivate(struct led_classdev *cdev)
{
	struct aw210xx *led = container_of(cdev, struct aw210xx, cdev);
	aw210xx_led_change_mode(led, AW210XX_LED_NONE);
	AW_LOG("[%d]: deactivate",led->id);
}

static int aw210xx_led_breath_activate(struct led_classdev *cdev)
{
	int ret = 0;
	struct aw210xx *led = container_of(cdev, struct aw210xx, cdev);

	AW_LOG("[%d]: activate",led->id);

	ret = aw210xx_led_change_mode(led, AW210XX_LED_BREATHMODE);
	if (ret < 0) {
		dev_err(led->cdev.dev, "%s: aw210xx_led_change_mode fail\n", __func__);
		return ret;
	}
	return ret;
}

static void aw210xx_led_breath_deactivate(struct led_classdev *cdev)
{
	struct aw210xx *led = container_of(cdev, struct aw210xx, cdev);
	aw210xx_led_change_mode(led, AW210XX_LED_NONE);
	AW_LOG("[%d]: deactivate",led->id);
}

static int aw210xx_led_individual_ctl_breath_activate(struct led_classdev *cdev)
{
	int ret = 0;
	struct aw210xx *led = container_of(cdev, struct aw210xx, cdev);

	AW_LOG("[%d]: activate",led->id);

	ret = aw210xx_led_change_mode(led, AW210XX_LED_INDIVIDUAL_CTL_BREATH);
	if (ret < 0) {
		dev_err(led->cdev.dev, "%s: aw210xx_led_change_mode fail\n", __func__);
		return ret;
	}
	return ret;
}

static void aw210xx_led_individual_ctl_breath_deactivate(struct led_classdev *cdev)
{
	struct aw210xx *led = container_of(cdev, struct aw210xx, cdev);
	aw210xx_led_change_mode(led, AW210XX_LED_NONE);
	AW_LOG("[%d]: individual_ctl_breath deactivate",led->id);
}

static int aw210xx_led_flow_ctl_mode_activate(struct led_classdev *cdev)
{
	int ret = 0;
	struct aw210xx *led = container_of(cdev, struct aw210xx, cdev);

	AW_LOG("[%d]: activate",led->id);
	ret = aw210xx_led_change_mode(led, AW210XX_LED_FLOW_CTL_MODE);
	if (ret < 0) {
		dev_err(led->cdev.dev, "%s: aw210xx_led_change_mode fail\n", __func__);
		return ret;
	}
	return ret;
}

static void aw210xx_led_flow_ctl_mode_deactivate(struct led_classdev *cdev)
{
	struct aw210xx *led = container_of(cdev, struct aw210xx, cdev);

	aw210xx_led_change_mode(led, AW210XX_LED_NONE);
	AW_LOG("[%d]: multicolor_ctl_breath deactivate",led->id);
}

static int aw210xx_led_rainbow_ctl_mode_activate(struct led_classdev *cdev)
{
	int ret = 0;
	struct aw210xx *led = container_of(cdev, struct aw210xx, cdev);

	AW_LOG("[%d]: activate",led->id);

	ret = aw210xx_led_change_mode(led, AW210XX_LED_RAINBOW_CTL_MODE);
	if (ret < 0) {
		dev_err(led->cdev.dev, "%s: aw210xx_led_change_mode fail\n", __func__);
		return ret;
	}
	return ret;
}

static void aw210xx_led_rainbow_ctl_mode_deactivate(struct led_classdev *cdev)
{
	struct aw210xx *led = container_of(cdev, struct aw210xx, cdev);
	aw210xx_led_change_mode(led, AW210XX_LED_NONE);
	AW_LOG("[%d]: multicolor_ctl_breath deactivate",led->id);
}

static int aw210xx_led_music_activate(struct led_classdev *cdev)
{
	int ret = 0;
	struct aw210xx *led = container_of(cdev, struct aw210xx, cdev);

	AW_LOG("[%d]: activate",led->id);

	ret = aw210xx_led_change_mode(led, AW210XX_LED_MUSICMODE);
	if (ret < 0) {
		dev_err(led->cdev.dev, "%s: aw210xx_led_change_mode fail\n", __func__);
		return ret;
	}
	return ret;
}

static void aw210xx_led_music_deactivate(struct led_classdev *cdev)
{
	struct aw210xx *led = container_of(cdev, struct aw210xx, cdev);
	aw210xx_led_change_mode(led, AW210XX_LED_NONE);
	AW_LOG("[%d]: music deactivate",led->id);
}

static int aw210xx_led_incall_activate(struct led_classdev *cdev)
{
	int ret = 0;
	struct aw210xx *led = container_of(cdev, struct aw210xx, cdev);

	AW_LOG("[%d]: activate",led->id);

	ret = aw210xx_led_change_mode(led, AW210XX_LED_INCALL_MODE);
	if (ret < 0) {
		dev_err(led->cdev.dev, "%s: aw210xx_led_change_mode fail\n", __func__);
		return ret;
	}
	return ret;
}

static void aw210xx_led_incall_deactivate(struct led_classdev *cdev)
{
	struct aw210xx *led = container_of(cdev, struct aw210xx, cdev);
	aw210xx_led_change_mode(led, AW210XX_LED_NONE);
	AW_LOG("[%d]: %s\n", led->id, __func__);
}

static int aw210xx_led_notify_activate(struct led_classdev *cdev)
{
	int ret = 0;
	struct aw210xx *led = container_of(cdev, struct aw210xx, cdev);

	AW_LOG("[%d]: activate",led->id);

	ret = aw210xx_led_change_mode(led, AW210XX_LED_NOTIFY_MODE);
	if (ret < 0) {
		dev_err(led->cdev.dev, "%s: aw210xx_led_change_mode fail\n", __func__);
		return ret;
	}
	return ret;
}

static void aw210xx_led_notify_deactivate(struct led_classdev *cdev)
{
	struct aw210xx *led = container_of(cdev, struct aw210xx, cdev);
	aw210xx_led_change_mode(led, AW210XX_LED_NONE);
	AW_LOG("[%d]: %s\n", led->id, __func__);
}

static int aw210xx_led_charge_activate(struct led_classdev *cdev)
{
	int ret = 0;
	struct aw210xx *led = container_of(cdev, struct aw210xx, cdev);

	AW_LOG("[%d]: activate",led->id);

	ret = aw210xx_led_change_mode(led, AW210XX_LED_CHARGE_MODE);
	if (ret < 0) {
		dev_err(led->cdev.dev, "%s: aw210xx_led_change_mode fail\n", __func__);
		return ret;
	}
	return ret;
}

static void aw210xx_led_charge_deactivate(struct led_classdev *cdev)
{
	struct aw210xx *led = container_of(cdev, struct aw210xx, cdev);
	aw210xx_led_change_mode(led, AW210XX_LED_NONE);
	AW_LOG("[%d]: %s\n", led->id, __func__);
}

static int aw210xx_led_poweron_activate(struct led_classdev *cdev)
{
	int ret = 0;
	struct aw210xx *led = container_of(cdev, struct aw210xx, cdev);

	AW_LOG("[%d]: activate",led->id);

	ret = aw210xx_led_change_mode(led, AW210XX_LED_POWERON_MODE);
	if (ret < 0) {
		dev_err(led->cdev.dev, "%s: aw210xx_led_change_mode fail\n", __func__);
		return ret;
	}
	return ret;
}

static void aw210xx_led_poweron_deactivate(struct led_classdev *cdev)
{
	struct aw210xx *led = container_of(cdev, struct aw210xx, cdev);
	aw210xx_led_change_mode(led, AW210XX_LED_NONE);
	AW_LOG("[%d]: %s\n", led->id, __func__);
}

static int aw210xx_led_game_activate(struct led_classdev *cdev)
{
	int ret = 0;
	struct aw210xx *led = container_of(cdev, struct aw210xx, cdev);

	AW_LOG("[%d]: activate",led->id);

	ret = aw210xx_led_change_mode(led, AW210XX_LED_GAMEMODE);
	if (ret < 0) {
		dev_err(led->cdev.dev, "%s: aw210xx_led_change_mode fail\n", __func__);
		return ret;
	}
	return ret;
}

static void aw210xx_led_game_deactivate(struct led_classdev *cdev)
{
	struct aw210xx *led = container_of(cdev, struct aw210xx, cdev);
	aw210xx_led_change_mode(led, AW210XX_LED_NONE);
	AW_LOG("[%d]: %s\n", led->id, __func__);
}


static struct led_trigger aw210xx_led_trigger[LEDMODE_MAX_NUM] = {
	{
		.name = "cc_mode",
		.activate = aw210xx_led_cc_activate,
		.deactivate = aw210xx_led_cc_deactivate,
//		.groups = aw210xx_led_cc_mode_groups,
	},
	{
		.name = "new_always_on_mode",
		.activate = aw210xx_led_new_always_on_activate,
		.deactivate = aw210xx_led_new_always_on_deactivate,
//		.groups = aw210xx_led_new_always_on_mode_groups,
	},
	{
		.name = "flow_ctl_mode",
		.activate = aw210xx_led_flow_ctl_mode_activate,
		.deactivate = aw210xx_led_flow_ctl_mode_deactivate,
//		.groups = aw210xx_led_flow_ctl_mode_groups,
	},
	{
		.name = "rainbow_ctl_mode",
		.activate = aw210xx_led_rainbow_ctl_mode_activate,
		.deactivate = aw210xx_led_rainbow_ctl_mode_deactivate,
//		.groups = aw210xx_led_rainbow_ctl_mode_mode_groups,
	},
	{
		.name = "blink_mode",
		.activate = aw210xx_led_blink_activate,
		.deactivate = aw210xx_led_blink_deactivate,
//		.groups = aw210xx_led_blink_mode_groups,
	},
	{
		.name = "breath_mode",
		.activate = aw210xx_led_breath_activate,
		.deactivate = aw210xx_led_breath_deactivate,
//		.groups = aw210xx_led_breath_mode_groups,
	},
	{
		.name = "individual_ctl_breath",
		.activate = aw210xx_led_individual_ctl_breath_activate,
		.deactivate = aw210xx_led_individual_ctl_breath_deactivate,
//		.groups = aw210xx_led_individual_ctl_breath_mode_groups,
	},
	{
		.name = "music_mode",
		.activate = aw210xx_led_music_activate,
		.deactivate = aw210xx_led_music_deactivate,
//		.groups = aw210xx_led_music_mode_groups,
	},
	{
		.name = "incall_mode",
		.activate = aw210xx_led_incall_activate,
		.deactivate = aw210xx_led_incall_deactivate,
//		.groups = aw210xx_led_music_mode_groups,
	},
	{
		.name = "notify_mode",
		.activate = aw210xx_led_notify_activate,
		.deactivate = aw210xx_led_notify_deactivate,
//		.groups = aw210xx_led_music_mode_groups,
	},
	{
		.name = "charge_mode",
		.activate = aw210xx_led_charge_activate,
		.deactivate = aw210xx_led_charge_deactivate,
//		.groups = aw210xx_led_music_mode_groups,
	},
	{
		.name = "poweron_mode",
		.activate = aw210xx_led_poweron_activate,
		.deactivate = aw210xx_led_poweron_deactivate,
//		.groups = aw210xx_led_music_mode_groups,
	},
	{
		.name = "game_mode",
		.activate = aw210xx_led_game_activate,
		.deactivate = aw210xx_led_game_deactivate,
//		.groups = aw210xx_led_music_mode_groups,
	},
};

/*****************************************************
 *
 * check chip id and version
 *
 *****************************************************/
static int aw210xx_read_chipid(struct aw210xx *aw210xx)
{
	int ret = -1;
	unsigned char cnt = 0;
	unsigned char chipid = 0;

	while (cnt < AW_READ_CHIPID_RETRIES) {
		ret = aw210xx_i2c_read(aw210xx, AW210XX_REG_RESET, &chipid);
		if (ret < 0) {
			AW_ERR("failed to read chipid: %d\n", ret);
		} else {
			aw210xx->chipid = chipid;
			switch (aw210xx->chipid) {
			case AW21018_CHIPID:
				AW_LOG("AW21018, read chipid = 0x%02x!!\n",
						chipid);
				return 0;
			case AW21012_CHIPID:
				AW_LOG("AW21012, read chipid = 0x%02x!!\n",
						chipid);
				return 0;
			case AW21009_CHIPID:
				AW_LOG("AW21009, read chipid = 0x%02x!!\n",
						chipid);
				return 0;
			default:
				AW_LOG("chip is unsupported device id = %x\n",
						chipid);
				break;
			}
		}
		cnt++;
		usleep_range(AW_READ_CHIPID_RETRY_DELAY * 1000,
				AW_READ_CHIPID_RETRY_DELAY * 1000 + 500);
	}

	return -EINVAL;
}


/*****************************************************
 *
 * device tree
 *
 *****************************************************/
static int aw210xx_parse_dt(struct device *dev, struct aw210xx *aw210xx,
		struct device_node *np)
{
	int ret = -EINVAL;
	int led_order[LED_MAX_NUM], i = 0;

	aw210xx->enable_gpio = of_get_named_gpio(np, "enable-gpio", 0);
	if (aw210xx->enable_gpio < 0) {
		aw210xx->enable_gpio = -1;
		AW_ERR("no enable gpio provided, HW enable unsupported\n");
//		return ret;
	}

	aw210xx->vbled_enable_gpio = of_get_named_gpio(np, "vbled-enable-gpio", 0);
	if (aw210xx->vbled_enable_gpio < 0) {
		aw210xx->vbled_enable_gpio = -1;
		AW_ERR("no vbled enable gpio provided, HW enable unsupported\n");
//		return ret;
	}

	ret = of_property_read_u32(np, "aw210xx_led_id",
			&aw210xx->aw210xx_led_id);
	if (ret < 0) {
		AW_ERR("led_groups_num is not set, set led_groups_num = 2\n");
		aw210xx->aw210xx_led_id = 0;
	}

	ret = of_property_read_u32(np, "led_groups_num",
			&aw210xx->led_groups_num);
	if (ret < 0) {
		AW_ERR("led_groups_num is not set, set led_groups_num = 2\n");
		aw210xx->led_groups_num = 2;
	}

	ret = of_property_read_u32_array(np, "led_allocation_order", led_order, aw210xx->led_groups_num);
	for (i = 0; i < LED_MAX_NUM; i++) {
		aw210xx->led_allocation_order[i] = i;
	}
	if (ret) {
		for (i = 0; i < aw210xx->led_groups_num; i++) {
			aw210xx->led_allocation_order[i] = i;
		}
		AW_ERR("led_allocation_order is not set, set led_allocation_order = 1,2,3,4\n");
	} else {
		for (i = 0; i < aw210xx->led_groups_num; i++) {
			aw210xx->led_allocation_order[i] = led_order[i];
		}
		AW_ERR("led_allocation_order is %d,%d,%d,%d,%d,%d,%d,%d\n", aw210xx->led_allocation_order[0] + 1, aw210xx->led_allocation_order[1] + 1,
			aw210xx->led_allocation_order[2] + 1, aw210xx->led_allocation_order[3] + 1,aw210xx->led_allocation_order[4] + 1,aw210xx->led_allocation_order[5] + 1,
			aw210xx->led_allocation_order[6] + 1,aw210xx->led_allocation_order[7] + 1);
	}

	ret = of_property_read_u32(np, "vbled_volt",
			&aw210xx->vbled_volt);
	if (ret < 0) {
		AW_ERR("vbled_volt is not set\n");
		aw210xx->vbled_volt = 0;
	}

	ret = of_property_read_u32(np, "osc_clk",
			&aw210xx->osc_clk);
	if (ret < 0) {
		AW_ERR("no osc_clk provided, osc clk unsupported\n");
		return ret;
	}

	ret = of_property_read_u32(np, "br_res",
			&aw210xx->br_res);
	if (ret < 0) {
		AW_ERR("brightness resolution unsupported\n");
		return ret;
	}

	ret = of_property_read_u32(np, "global_current_max",
			&aw210xx->glo_current_max);
	if (ret < 0) {
		AW_ERR("global current max resolution unsupported\n");
		return ret;
	}

	ret = of_property_read_u32(np, "global_current_min",
			&aw210xx->glo_current_min);
	if (ret < 0) {
		AW_ERR("global current min resolution unsupported\n");
		return ret;
	}

	ret = of_property_read_u32(np, "chipid",
			&aw210xx->chipid);
	if (ret < 0) {
		AW_ERR("chipid is not set, chip = 0 \n");
		aw210xx->chipid = 0;
	} else {
		aw210xx->chipid = ((aw210xx->chipid) & 0xFF);
		AW_ERR("chipid set in dts: %02x \n", aw210xx->chipid);
	}

	return 0;
}




/******************************************************
 *
 * i2c driver
 *
 ******************************************************/
static int aw210xx_i2c_probe(struct i2c_client *i2c)
{
	struct aw210xx *aw210xx;
	struct device_node *np = i2c->dev.of_node;
	int ret, num_leds = 0, i = 0;

	if (oplus_boot_mode_is_power_off_charging()) {
		AW_LOG("boot_mode is power_off_charging skip probe");
		return 0;
	}

	AW_LOG("enter\n");

	num_leds = of_get_child_count(np);

	if (!num_leds)
		return -EINVAL;

	if (!i2c_check_functionality(i2c->adapter, I2C_FUNC_I2C)) {
		AW_ERR("check_functionality failed\n");
		return -EIO;
	}

	aw210xx = devm_kzalloc(&i2c->dev,
			(sizeof(struct aw210xx) * num_leds), GFP_KERNEL);
	if (aw210xx == NULL)
		return -ENOMEM;

	load_num ++;
	aw210xx->dev = &i2c->dev;
	aw210xx->i2c = i2c;
	aw210xx->num_leds = num_leds;
	i2c_set_clientdata(i2c, aw210xx);
	aw210xx->rgb_isnk_on = 0;
	aw210xx->power_change_state = 0;
	mutex_init(&aw210xx->lock);
	// aw210xx->boot_mode = get_boot_mode();

	/* aw210xx parse device tree */
	if (np) {
		ret = aw210xx_parse_dt(&i2c->dev, aw210xx, np);
		if (ret) {
			AW_ERR("failed to parse device tree node\n");
			goto err_parse_dt;
		}
	}


	if (gpio_is_valid(aw210xx->enable_gpio)) {
		ret = devm_gpio_request_one(&i2c->dev, aw210xx->enable_gpio,
				GPIOF_OUT_INIT_LOW, "aw210xx_en");
		if (ret) {
			AW_ERR("enable gpio request failed\n");
			goto err_gpio_request;
		}
	}

	if (aw210xx->aw210xx_led_id == 0) {
		if (gpio_is_valid(aw210xx->vbled_enable_gpio)) {
			ret = devm_gpio_request_one(&i2c->dev, aw210xx->vbled_enable_gpio,
					GPIOF_OUT_INIT_LOW, "aw210xx_vbled_en");
			if (ret) {
				AW_ERR("vbled enable gpio request failed\n");
				goto err_gpio_request;
			}
		}

		/* vdd 3.3v*/
		aw210xx->vbled = regulator_get(aw210xx->dev, "vbled");

		if (!aw210xx->vbled_volt || IS_ERR_OR_NULL(aw210xx->vbled)) {
			AW_ERR("Regulator vdd3v3 get failed\n");

		} else {
			AW_ERR("Regulator vbled volt set %u \n", aw210xx->vbled_volt);
			if (aw210xx->vbled_volt) {
				ret = regulator_set_voltage(aw210xx->vbled, aw210xx->vbled_volt,
								aw210xx->vbled_volt);

			} else {
				ret = regulator_set_voltage(aw210xx->vbled, 3300000, 3300000);
			}

			if (ret) {
				AW_ERR("Regulator set_vtg failed vdd rc = %d\n", ret);
			}
		}

		/* hardware enable */
		aw210xx_hw_enable(aw210xx, true);
	}

	/* aw210xx identify */
	if (aw210xx->chipid == 0) {
		ret = aw210xx_read_chipid(aw210xx);
		if (ret < 0) {
			AW_ERR("aw210xx_read_chipid failed ret=%d\n", ret);
			goto err_id;
		}
	}

	dev_set_drvdata(&i2c->dev, aw210xx);

	aw210xx_parse_led_cdev(aw210xx, np);
	if (ret < 0) {
		AW_ERR("error creating led class dev\n");
		goto err_sysfs;
	}

	/* aw210xx led trigger register */
	if (aw210xx->aw210xx_led_id == 0) {
		for (i = 0; i < LEDMODE_MAX_NUM; i++) {
			ret = led_trigger_register(&aw210xx_led_trigger[i]);
			if (ret < 0) {
				dev_err(&i2c->dev, "register %d trigger fail\n", i);
				goto fail_led_trigger;
			}
		}

		led_default = aw210xx;
	}

//	aw210xx_led_init(aw210xx);
	aw210xx->light_sensor_state = 1;
	if (aw210xx->aw210xx_led_id == 0) {
		aw210xx_hw_enable(aw210xx, false);

		aw210xx->aw210_led_wq = create_singlethread_workqueue("aw210_led_workqueue");
		if (!aw210xx->aw210_led_wq) {
			dev_err(&i2c->dev, "aw210_led_workqueue error\n");
			goto err_parse_dt;
		}
		aw210xx->aw210_led_flow = create_singlethread_workqueue("flow_led_workqueue");
		if (!aw210xx->aw210_led_flow) {
			dev_err(&i2c->dev, "flow_led_workqueue error\n");
			goto err_parse_dt;
		}
		INIT_DELAYED_WORK(&aw210xx->aw210_led_work, aw210_work_func);
		INIT_DELAYED_WORK(&aw210xx->flow_led_work, flow_work_func);
	}

	effect_state.cur_effect = -1;
	effect_state.prev_effect = -1;
	for(i=0;i<AW210XX_LED_MAXMODE;i++)
	{
		effect_state.state[i]=0;
		effect_state.data[i]=0;
	}
	AW_LOG("led[%d]:probe completed!\n", aw210xx->aw210xx_led_id);
	return 0;

fail_led_trigger:
	while (--i >= 0)
		led_trigger_register(&aw210xx_led_trigger[i]);
	aw210xx_led_err_handle(aw210xx, num_leds);
err_sysfs:
err_id:
	if (aw210xx->aw210xx_led_id == 0)
		aw210xx_hw_enable(aw210xx, false);
err_gpio_request:
err_parse_dt:
	devm_kfree(&i2c->dev, aw210xx);
	aw210xx = NULL;
	return ret;
}

static void aw210xx_i2c_remove(struct i2c_client *i2c)
{
	struct aw210xx *aw210xx = i2c_get_clientdata(i2c);

	AW_LOG("enter\n");
	sysfs_remove_group(&aw210xx->cdev.dev->kobj, &aw210xx_attribute_group);
	led_classdev_unregister(&aw210xx->cdev);
	if (gpio_is_valid(aw210xx->enable_gpio))
		gpio_free(aw210xx->enable_gpio);
	if (gpio_is_valid(aw210xx->vbled_enable_gpio))
		gpio_free(aw210xx->vbled_enable_gpio);
	mutex_destroy(&aw210xx->pdata->led->lock);
	devm_kfree(&i2c->dev, aw210xx);
	aw210xx = NULL;
	return;
}

static const struct i2c_device_id aw210xx_i2c_id[] = {
	{AW210XX_I2C_NAME, 0},
	{}
};

MODULE_DEVICE_TABLE(i2c, aw210xx_i2c_id);

static const struct of_device_id aw210xx_dt_match[] = {
	{.compatible = "awinic,aw210xx_led"},
	{}
};

static struct i2c_driver aw210xx_i2c_driver = {
	.driver = {
		.name = AW210XX_I2C_NAME,
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(aw210xx_dt_match),
		},
	.probe = aw210xx_i2c_probe,
	.remove = aw210xx_i2c_remove,
	.id_table = aw210xx_i2c_id,
};

static int __init aw210xx_i2c_init(void)
{
	int ret = 0;
	AW_LOG("enter, aw210xx driver version %s\n", AW210XX_DRIVER_VERSION);

	ret = i2c_add_driver(&aw210xx_i2c_driver);
	if (ret) {
		AW_ERR("failed to register aw210xx driver!\n");
		return ret;
	}

	return 0;
}
module_init(aw210xx_i2c_init);

static void __exit aw210xx_i2c_exit(void)
{
	i2c_del_driver(&aw210xx_i2c_driver);
}
module_exit(aw210xx_i2c_exit);

MODULE_DESCRIPTION("AW210XX LED Driver");
MODULE_LICENSE("GPL v2");

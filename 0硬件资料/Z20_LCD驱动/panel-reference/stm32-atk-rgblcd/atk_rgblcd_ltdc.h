/**
 ****************************************************************************************************
 * @file        atk_rgblcd_ltdc.h
 * @author      正点原子团队(ALIENTEK)
 * @version     V1.0
 * @date        2023-02-24
 * @brief       正点原子RGB LCD模块LTDC接口驱动代码
 * @license     Copyright (c) 2020-2032, 广州市星翼电子科技有限公司
 ****************************************************************************************************
 * @attention
 *
 * 实验平台:正点原子 阿波罗 H743开发板
 * 在线视频:www.yuanzige.com
 * 技术论坛:www.openedv.com
 * 公司网址:www.alientek.com
 * 购买地址:openedv.taobao.com
 *
 ****************************************************************************************************
 */

#ifndef __ATK_RGBLCD_LTDC_H
#define __ATK_RGBLCD_LTDC_H

#include "./SYSTEM/sys/sys.h"
#include "./BSP/ATK_RGBLCD/atk_rgblcd.h"

/* RGB LCD模块LTDC接口定义 */
#define ATK_RGBLCD_LTDC_PLL3M           5                                           /* PLL3输入分频系数 */
#define ATK_RGBLCD_LTDC_PLL3N           160                                         /* PLL3倍频系数 */
#define ATK_RGBLCD_LTDC_PLL3R           24                                          /* PLL3分频系数 */
#define ATK_RGBLCD_LTDC_CLK_ENABLE()    do{ __HAL_RCC_LTDC_CLK_ENABLE(); }while(0)  /* RGB LCD模块所接LTDC时钟使能 */
#define ATK_RGBLCD_LTDC_LAYER_FB_ADDR   ((uint32_t)0xC0000000)

/* 引脚定义 */
#define ATK_RGBLCD_LTDC_DE_GPIO_PORT            GPIOF
#define ATK_RGBLCD_LTDC_DE_GPIO_PIN             GPIO_PIN_10
#define ATK_RGBLCD_LTDC_DE_GPIO_AF              GPIO_AF14_LTDC
#define ATK_RGBLCD_LTDC_DE_GPIO_CLK_ENABLE()    do{ __HAL_RCC_GPIOF_CLK_ENABLE(); }while(0)
#define ATK_RGBLCD_LTDC_CLK_GPIO_PORT           GPIOG
#define ATK_RGBLCD_LTDC_CLK_GPIO_PIN            GPIO_PIN_7
#define ATK_RGBLCD_LTDC_CLK_GPIO_AF             GPIO_AF14_LTDC
#define ATK_RGBLCD_LTDC_CLK_GPIO_CLK_ENABLE()   do{ __HAL_RCC_GPIOG_CLK_ENABLE(); }while(0)
#define ATK_RGBLCD_LTDC_HSYNC_GPIO_PORT         GPIOI
#define ATK_RGBLCD_LTDC_HSYNC_GPIO_PIN          GPIO_PIN_10
#define ATK_RGBLCD_LTDC_HSYNC_GPIO_AF           GPIO_AF14_LTDC
#define ATK_RGBLCD_LTDC_HSYNC_GPIO_CLK_ENABLE() do{ __HAL_RCC_GPIOI_CLK_ENABLE(); }while(0)
#define ATK_RGBLCD_LTDC_VSYNC_GPIO_PORT         GPIOI
#define ATK_RGBLCD_LTDC_VSYNC_GPIO_PIN          GPIO_PIN_9
#define ATK_RGBLCD_LTDC_VSYNC_GPIO_AF           GPIO_AF14_LTDC
#define ATK_RGBLCD_LTDC_VSYNC_GPIO_CLK_ENABLE() do{ __HAL_RCC_GPIOI_CLK_ENABLE(); }while(0)
#define ATK_RGBLCD_LTDC_R3_GPIO_PORT            GPIOH
#define ATK_RGBLCD_LTDC_R3_GPIO_PIN             GPIO_PIN_9
#define ATK_RGBLCD_LTDC_R3_GPIO_AF              GPIO_AF14_LTDC
#define ATK_RGBLCD_LTDC_R3_GPIO_CLK_ENABLE()    do{ __HAL_RCC_GPIOH_CLK_ENABLE(); }while(0)
#define ATK_RGBLCD_LTDC_R4_GPIO_PORT            GPIOH
#define ATK_RGBLCD_LTDC_R4_GPIO_PIN             GPIO_PIN_10
#define ATK_RGBLCD_LTDC_R4_GPIO_AF              GPIO_AF14_LTDC
#define ATK_RGBLCD_LTDC_R4_GPIO_CLK_ENABLE()    do{ __HAL_RCC_GPIOH_CLK_ENABLE(); }while(0)
#define ATK_RGBLCD_LTDC_R5_GPIO_PORT            GPIOH
#define ATK_RGBLCD_LTDC_R5_GPIO_PIN             GPIO_PIN_11
#define ATK_RGBLCD_LTDC_R5_GPIO_AF              GPIO_AF14_LTDC
#define ATK_RGBLCD_LTDC_R5_GPIO_CLK_ENABLE()    do{ __HAL_RCC_GPIOH_CLK_ENABLE(); }while(0)
#define ATK_RGBLCD_LTDC_R6_GPIO_PORT            GPIOH
#define ATK_RGBLCD_LTDC_R6_GPIO_PIN             GPIO_PIN_12
#define ATK_RGBLCD_LTDC_R6_GPIO_AF              GPIO_AF14_LTDC
#define ATK_RGBLCD_LTDC_R6_GPIO_CLK_ENABLE()    do{ __HAL_RCC_GPIOH_CLK_ENABLE(); }while(0)
#define ATK_RGBLCD_LTDC_R7_GPIO_PORT            GPIOG
#define ATK_RGBLCD_LTDC_R7_GPIO_PIN             GPIO_PIN_6
#define ATK_RGBLCD_LTDC_R7_GPIO_AF              GPIO_AF14_LTDC
#define ATK_RGBLCD_LTDC_R7_GPIO_CLK_ENABLE()    do{ __HAL_RCC_GPIOG_CLK_ENABLE(); }while(0)
#define ATK_RGBLCD_LTDC_G2_GPIO_PORT            GPIOH
#define ATK_RGBLCD_LTDC_G2_GPIO_PIN             GPIO_PIN_13
#define ATK_RGBLCD_LTDC_G2_GPIO_AF              GPIO_AF14_LTDC
#define ATK_RGBLCD_LTDC_G2_GPIO_CLK_ENABLE()    do{ __HAL_RCC_GPIOH_CLK_ENABLE(); }while(0)
#define ATK_RGBLCD_LTDC_G3_GPIO_PORT            GPIOH
#define ATK_RGBLCD_LTDC_G3_GPIO_PIN             GPIO_PIN_14
#define ATK_RGBLCD_LTDC_G3_GPIO_AF              GPIO_AF14_LTDC
#define ATK_RGBLCD_LTDC_G3_GPIO_CLK_ENABLE()    do{ __HAL_RCC_GPIOH_CLK_ENABLE(); }while(0)
#define ATK_RGBLCD_LTDC_G4_GPIO_PORT            GPIOH
#define ATK_RGBLCD_LTDC_G4_GPIO_PIN             GPIO_PIN_15
#define ATK_RGBLCD_LTDC_G4_GPIO_AF              GPIO_AF14_LTDC
#define ATK_RGBLCD_LTDC_G4_GPIO_CLK_ENABLE()    do{ __HAL_RCC_GPIOH_CLK_ENABLE(); }while(0)
#define ATK_RGBLCD_LTDC_G5_GPIO_PORT            GPIOI
#define ATK_RGBLCD_LTDC_G5_GPIO_PIN             GPIO_PIN_0
#define ATK_RGBLCD_LTDC_G5_GPIO_AF              GPIO_AF14_LTDC
#define ATK_RGBLCD_LTDC_G5_GPIO_CLK_ENABLE()    do{ __HAL_RCC_GPIOI_CLK_ENABLE(); }while(0)
#define ATK_RGBLCD_LTDC_G6_GPIO_PORT            GPIOI
#define ATK_RGBLCD_LTDC_G6_GPIO_PIN             GPIO_PIN_1
#define ATK_RGBLCD_LTDC_G6_GPIO_AF              GPIO_AF14_LTDC
#define ATK_RGBLCD_LTDC_G6_GPIO_CLK_ENABLE()    do{ __HAL_RCC_GPIOI_CLK_ENABLE(); }while(0)
#define ATK_RGBLCD_LTDC_G7_GPIO_PORT            GPIOI
#define ATK_RGBLCD_LTDC_G7_GPIO_PIN             GPIO_PIN_2
#define ATK_RGBLCD_LTDC_G7_GPIO_AF              GPIO_AF14_LTDC
#define ATK_RGBLCD_LTDC_G7_GPIO_CLK_ENABLE()    do{ __HAL_RCC_GPIOI_CLK_ENABLE(); }while(0)
#define ATK_RGBLCD_LTDC_B3_GPIO_PORT            GPIOG
#define ATK_RGBLCD_LTDC_B3_GPIO_PIN             GPIO_PIN_11
#define ATK_RGBLCD_LTDC_B3_GPIO_AF              GPIO_AF14_LTDC
#define ATK_RGBLCD_LTDC_B3_GPIO_CLK_ENABLE()    do{ __HAL_RCC_GPIOG_CLK_ENABLE(); }while(0)
#define ATK_RGBLCD_LTDC_B4_GPIO_PORT            GPIOI
#define ATK_RGBLCD_LTDC_B4_GPIO_PIN             GPIO_PIN_4
#define ATK_RGBLCD_LTDC_B4_GPIO_AF              GPIO_AF14_LTDC
#define ATK_RGBLCD_LTDC_B4_GPIO_CLK_ENABLE()    do{ __HAL_RCC_GPIOI_CLK_ENABLE(); }while(0)
#define ATK_RGBLCD_LTDC_B5_GPIO_PORT            GPIOI
#define ATK_RGBLCD_LTDC_B5_GPIO_PIN             GPIO_PIN_5
#define ATK_RGBLCD_LTDC_B5_GPIO_AF              GPIO_AF14_LTDC
#define ATK_RGBLCD_LTDC_B5_GPIO_CLK_ENABLE()    do{ __HAL_RCC_GPIOI_CLK_ENABLE(); }while(0)
#define ATK_RGBLCD_LTDC_B6_GPIO_PORT            GPIOI
#define ATK_RGBLCD_LTDC_B6_GPIO_PIN             GPIO_PIN_6
#define ATK_RGBLCD_LTDC_B6_GPIO_AF              GPIO_AF14_LTDC
#define ATK_RGBLCD_LTDC_B6_GPIO_CLK_ENABLE()    do{ __HAL_RCC_GPIOI_CLK_ENABLE(); }while(0)
#define ATK_RGBLCD_LTDC_B7_GPIO_PORT            GPIOI
#define ATK_RGBLCD_LTDC_B7_GPIO_PIN             GPIO_PIN_7
#define ATK_RGBLCD_LTDC_B7_GPIO_AF              GPIO_AF14_LTDC
#define ATK_RGBLCD_LTDC_B7_GPIO_CLK_ENABLE()    do{ __HAL_RCC_GPIOI_CLK_ENABLE(); }while(0)

/* 操作函数 */
void atk_rgblcd_ltdc_init(uint16_t width, uint16_t height, const atk_rgblcd_timing_t *timing);  /* RGB LCD模块LTDC接口初始化 */
void atk_rgblcd_ltdc_enable(void);                                                              /* 使能RGB LCD模块LTDC接口 */
void atk_rgblcd_ltdc_disable(void);                                                             /* 禁用RGB LCD模块LTDC接口 */

#endif

/**
 ****************************************************************************************************
 * @file        atk_rgblcd_touch_iic.h
 * @author      正点原子团队(ALIENTEK)
 * @version     V1.0
 * @date        2023-02-24
 * @brief       正点原子RGB LCD模块触摸IIC接口驱动代码
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

#ifndef __ATK_RGBLCD_TOUCH_IIC_H
#define __ATK_RGBLCD_TOUCH_IIC_H

#include "./BSP/ATK_RGBLCD/atk_rgblcd.h"

#if (ATK_RGBLCD_USING_TOUCH != 0)

/* 引脚定义 */
#define ATK_RGBLCD_TOUCH_IIC_SCL_GPIO_PORT          GPIOH
#define ATK_RGBLCD_TOUCH_IIC_SCL_GPIO_PIN           GPIO_PIN_6
#define ATK_RGBLCD_TOUCH_IIC_SCL_GPIO_CLK_ENABLE()  do{ __HAL_RCC_GPIOH_CLK_ENABLE(); }while(0)
#define ATK_RGBLCD_TOUCH_IIC_SDA_GPIO_PORT          GPIOI
#define ATK_RGBLCD_TOUCH_IIC_SDA_GPIO_PIN           GPIO_PIN_3
#define ATK_RGBLCD_TOUCH_IIC_SDA_GPIO_CLK_ENABLE()  do{ __HAL_RCC_GPIOI_CLK_ENABLE(); }while(0)

/* IO操作 */
#define ATK_RGBLCD_TOUCH_IIC_SCL(x)                 do{ x ?                                                                                                         \
                                                        HAL_GPIO_WritePin(ATK_RGBLCD_TOUCH_IIC_SCL_GPIO_PORT, ATK_RGBLCD_TOUCH_IIC_SCL_GPIO_PIN, GPIO_PIN_SET) :    \
                                                        HAL_GPIO_WritePin(ATK_RGBLCD_TOUCH_IIC_SCL_GPIO_PORT, ATK_RGBLCD_TOUCH_IIC_SCL_GPIO_PIN, GPIO_PIN_RESET);   \
                                                    }while(0)

#define ATK_RGBLCD_TOUCH_IIC_SDA(x)                 do{ x ?                                                                                                         \
                                                        HAL_GPIO_WritePin(ATK_RGBLCD_TOUCH_IIC_SDA_GPIO_PORT, ATK_RGBLCD_TOUCH_IIC_SDA_GPIO_PIN, GPIO_PIN_SET) :    \
                                                        HAL_GPIO_WritePin(ATK_RGBLCD_TOUCH_IIC_SDA_GPIO_PORT, ATK_RGBLCD_TOUCH_IIC_SDA_GPIO_PIN, GPIO_PIN_RESET);   \
                                                    }while(0)

#define ATK_RGBLCD_TOUCH_IIC_READ_SDA()             HAL_GPIO_ReadPin(ATK_RGBLCD_TOUCH_IIC_SDA_GPIO_PORT, ATK_RGBLCD_TOUCH_IIC_SDA_GPIO_PIN)

/* 控制IIC通讯是读操作还是写操作 */
#define ATK_RGBLCD_TOUCH_IIC_WRITE                  0
#define ATK_RGBLCD_TOUCH_IIC_READ                   1

/* 错误代码 */
#define ATK_RGBLCD_TOUCH_IIC_EOK                    0   /* 没有错误 */
#define ATK_RGBLCD_TOUCH_IIC_ERROR                  1   /* 错误 */

/* 操作函数 */
void atk_rgblcd_touch_iic_init(void);                   /* 初始化IIC接口 */
void atk_rgblcd_touch_iic_start(void);                  /* 产生IIC起始信号 */
void atk_rgblcd_touch_iic_stop(void);                   /* 产生IIC停止信号 */
uint8_t atk_rgblcd_touch_iic_wait_ack(void);            /* 等待IIC应答信号 */
void atk_rgblcd_touch_iic_ack(void);                    /* 产生ACK应答信号 */
void atk_rgblcd_touch_iic_nack(void);                   /* 不产生ACK应答信号 */
void atk_rgblcd_touch_iic_send_byte(uint8_t dat);       /* IIC发送一个字节 */
uint8_t atk_rgblcd_touch_iic_recv_byte(uint8_t ack);    /* IIC接收一个字节 */

#endif

#endif

/**
 ****************************************************************************************************
 * @file        atk_rgblcd_ltdc.c
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

#include "./BSP/ATK_RGBLCD/atk_rgblcd_ltdc.h"

/* RGB LCD模块LTDC句柄 */
static LTDC_HandleTypeDef g_ltdc_handle = {0};

/**
 * @brief       RGB LCD模块LTDC接口时钟配置
 * @note        LCD_CLK frequency = HSE / PLL3M * PLL3N / PLL3R
 * @param       无
 * @retval      无
 */
static void atk_rgblcd_ltdc_clock_config(void)
{
    RCC_PeriphCLKInitTypeDef ltdc_clk_init_struct;

    ltdc_clk_init_struct.PeriphClockSelection   = RCC_PERIPHCLK_LTDC;
    ltdc_clk_init_struct.PLL3.PLL3M             = ATK_RGBLCD_LTDC_PLL3M;
    ltdc_clk_init_struct.PLL3.PLL3N             = ATK_RGBLCD_LTDC_PLL3N;
    ltdc_clk_init_struct.PLL3.PLL3R             = ATK_RGBLCD_LTDC_PLL3R;
    HAL_RCCEx_PeriphCLKConfig(&ltdc_clk_init_struct);
}

/**
 * @brief       RGB LCD模块LTDC接口初始化
 * @param       无
 * @retval      无
 */
void atk_rgblcd_ltdc_init(uint16_t width, uint16_t height, const atk_rgblcd_timing_t *timing)
{
    GPIO_InitTypeDef gpio_init_struct = {0};
    LTDC_LayerCfgTypeDef layer_init_struct = {0};

    /* 使能时钟 */
    ATK_RGBLCD_LTDC_CLK_ENABLE();
    ATK_RGBLCD_LTDC_DE_GPIO_CLK_ENABLE();
    ATK_RGBLCD_LTDC_CLK_GPIO_CLK_ENABLE();
    ATK_RGBLCD_LTDC_HSYNC_GPIO_CLK_ENABLE();
    ATK_RGBLCD_LTDC_VSYNC_GPIO_CLK_ENABLE();
    ATK_RGBLCD_LTDC_R3_GPIO_CLK_ENABLE();
    ATK_RGBLCD_LTDC_R4_GPIO_CLK_ENABLE();
    ATK_RGBLCD_LTDC_R5_GPIO_CLK_ENABLE();
    ATK_RGBLCD_LTDC_R6_GPIO_CLK_ENABLE();
    ATK_RGBLCD_LTDC_R7_GPIO_CLK_ENABLE();
    ATK_RGBLCD_LTDC_G2_GPIO_CLK_ENABLE();
    ATK_RGBLCD_LTDC_G3_GPIO_CLK_ENABLE();
    ATK_RGBLCD_LTDC_G4_GPIO_CLK_ENABLE();
    ATK_RGBLCD_LTDC_G5_GPIO_CLK_ENABLE();
    ATK_RGBLCD_LTDC_G6_GPIO_CLK_ENABLE();
    ATK_RGBLCD_LTDC_G7_GPIO_CLK_ENABLE();
    ATK_RGBLCD_LTDC_B3_GPIO_CLK_ENABLE();
    ATK_RGBLCD_LTDC_B4_GPIO_CLK_ENABLE();
    ATK_RGBLCD_LTDC_B5_GPIO_CLK_ENABLE();
    ATK_RGBLCD_LTDC_B6_GPIO_CLK_ENABLE();
    ATK_RGBLCD_LTDC_B7_GPIO_CLK_ENABLE();

    /* 初始化DE引脚 */
    gpio_init_struct.Pin        = ATK_RGBLCD_LTDC_DE_GPIO_PIN;
    gpio_init_struct.Mode       = GPIO_MODE_AF_PP;
    gpio_init_struct.Speed      = GPIO_SPEED_FREQ_HIGH;
    gpio_init_struct.Alternate  = ATK_RGBLCD_LTDC_DE_GPIO_AF;
    HAL_GPIO_Init(ATK_RGBLCD_LTDC_DE_GPIO_PORT, &gpio_init_struct);

    /* 初始化CLK引脚 */
    gpio_init_struct.Pin        = ATK_RGBLCD_LTDC_CLK_GPIO_PIN;
    gpio_init_struct.Mode       = GPIO_MODE_AF_PP;
    gpio_init_struct.Speed      = GPIO_SPEED_FREQ_HIGH;
    gpio_init_struct.Alternate  = ATK_RGBLCD_LTDC_CLK_GPIO_AF;
    HAL_GPIO_Init(ATK_RGBLCD_LTDC_CLK_GPIO_PORT, &gpio_init_struct);

    /* 初始化HSYNC引脚 */
    gpio_init_struct.Pin        = ATK_RGBLCD_LTDC_HSYNC_GPIO_PIN;
    gpio_init_struct.Mode       = GPIO_MODE_AF_PP;
    gpio_init_struct.Speed      = GPIO_SPEED_FREQ_HIGH;
    gpio_init_struct.Alternate  = ATK_RGBLCD_LTDC_HSYNC_GPIO_AF;
    HAL_GPIO_Init(ATK_RGBLCD_LTDC_HSYNC_GPIO_PORT, &gpio_init_struct);

    /* 初始化VSYNC引脚 */
    gpio_init_struct.Pin        = ATK_RGBLCD_LTDC_VSYNC_GPIO_PIN;
    gpio_init_struct.Mode       = GPIO_MODE_AF_PP;
    gpio_init_struct.Speed      = GPIO_SPEED_FREQ_HIGH;
    gpio_init_struct.Alternate  = ATK_RGBLCD_LTDC_VSYNC_GPIO_AF;
    HAL_GPIO_Init(ATK_RGBLCD_LTDC_VSYNC_GPIO_PORT, &gpio_init_struct);

    /* 初始化R3引脚 */
    gpio_init_struct.Pin        = ATK_RGBLCD_LTDC_R3_GPIO_PIN;
    gpio_init_struct.Mode       = GPIO_MODE_AF_PP;
    gpio_init_struct.Speed      = GPIO_SPEED_FREQ_HIGH;
    gpio_init_struct.Alternate  = ATK_RGBLCD_LTDC_R3_GPIO_AF;
    HAL_GPIO_Init(ATK_RGBLCD_LTDC_R3_GPIO_PORT, &gpio_init_struct);

    /* 初始化R4引脚 */
    gpio_init_struct.Pin        = ATK_RGBLCD_LTDC_R4_GPIO_PIN;
    gpio_init_struct.Mode       = GPIO_MODE_AF_PP;
    gpio_init_struct.Speed      = GPIO_SPEED_FREQ_HIGH;
    gpio_init_struct.Alternate  = ATK_RGBLCD_LTDC_R4_GPIO_AF;
    HAL_GPIO_Init(ATK_RGBLCD_LTDC_R4_GPIO_PORT, &gpio_init_struct);

    /* 初始化R5引脚 */
    gpio_init_struct.Pin        = ATK_RGBLCD_LTDC_R5_GPIO_PIN;
    gpio_init_struct.Mode       = GPIO_MODE_AF_PP;
    gpio_init_struct.Speed      = GPIO_SPEED_FREQ_HIGH;
    gpio_init_struct.Alternate  = ATK_RGBLCD_LTDC_R5_GPIO_AF;
    HAL_GPIO_Init(ATK_RGBLCD_LTDC_R5_GPIO_PORT, &gpio_init_struct);

    /* 初始化R6引脚 */
    gpio_init_struct.Pin        = ATK_RGBLCD_LTDC_R6_GPIO_PIN;
    gpio_init_struct.Mode       = GPIO_MODE_AF_PP;
    gpio_init_struct.Speed      = GPIO_SPEED_FREQ_HIGH;
    gpio_init_struct.Alternate  = ATK_RGBLCD_LTDC_R6_GPIO_AF;
    HAL_GPIO_Init(ATK_RGBLCD_LTDC_R6_GPIO_PORT, &gpio_init_struct);

    /* 初始化R7引脚 */
    gpio_init_struct.Pin        = ATK_RGBLCD_LTDC_R7_GPIO_PIN;
    gpio_init_struct.Mode       = GPIO_MODE_AF_PP;
    gpio_init_struct.Speed      = GPIO_SPEED_FREQ_HIGH;
    gpio_init_struct.Alternate  = ATK_RGBLCD_LTDC_R7_GPIO_AF;
    HAL_GPIO_Init(ATK_RGBLCD_LTDC_R7_GPIO_PORT, &gpio_init_struct);

    /* 初始化G2引脚 */
    gpio_init_struct.Pin        = ATK_RGBLCD_LTDC_G2_GPIO_PIN;
    gpio_init_struct.Mode       = GPIO_MODE_AF_PP;
    gpio_init_struct.Speed      = GPIO_SPEED_FREQ_HIGH;
    gpio_init_struct.Alternate  = ATK_RGBLCD_LTDC_G2_GPIO_AF;
    HAL_GPIO_Init(ATK_RGBLCD_LTDC_G2_GPIO_PORT, &gpio_init_struct);

    /* 初始化G3引脚 */
    gpio_init_struct.Pin        = ATK_RGBLCD_LTDC_G3_GPIO_PIN;
    gpio_init_struct.Mode       = GPIO_MODE_AF_PP;
    gpio_init_struct.Speed      = GPIO_SPEED_FREQ_HIGH;
    gpio_init_struct.Alternate  = ATK_RGBLCD_LTDC_G3_GPIO_AF;
    HAL_GPIO_Init(ATK_RGBLCD_LTDC_G3_GPIO_PORT, &gpio_init_struct);

    /* 初始化G4引脚 */
    gpio_init_struct.Pin        = ATK_RGBLCD_LTDC_G4_GPIO_PIN;
    gpio_init_struct.Mode       = GPIO_MODE_AF_PP;
    gpio_init_struct.Speed      = GPIO_SPEED_FREQ_HIGH;
    gpio_init_struct.Alternate  = ATK_RGBLCD_LTDC_G4_GPIO_AF;
    HAL_GPIO_Init(ATK_RGBLCD_LTDC_G4_GPIO_PORT, &gpio_init_struct);

    /* 初始化G5引脚 */
    gpio_init_struct.Pin        = ATK_RGBLCD_LTDC_G5_GPIO_PIN;
    gpio_init_struct.Mode       = GPIO_MODE_AF_PP;
    gpio_init_struct.Speed      = GPIO_SPEED_FREQ_HIGH;
    gpio_init_struct.Alternate  = ATK_RGBLCD_LTDC_G5_GPIO_AF;
    HAL_GPIO_Init(ATK_RGBLCD_LTDC_G5_GPIO_PORT, &gpio_init_struct);

    /* 初始化G6引脚 */
    gpio_init_struct.Pin        = ATK_RGBLCD_LTDC_G6_GPIO_PIN;
    gpio_init_struct.Mode       = GPIO_MODE_AF_PP;
    gpio_init_struct.Speed      = GPIO_SPEED_FREQ_HIGH;
    gpio_init_struct.Alternate  = ATK_RGBLCD_LTDC_G6_GPIO_AF;
    HAL_GPIO_Init(ATK_RGBLCD_LTDC_G6_GPIO_PORT, &gpio_init_struct);

    /* 初始化G7引脚 */
    gpio_init_struct.Pin        = ATK_RGBLCD_LTDC_G7_GPIO_PIN;
    gpio_init_struct.Mode       = GPIO_MODE_AF_PP;
    gpio_init_struct.Speed      = GPIO_SPEED_FREQ_HIGH;
    gpio_init_struct.Alternate  = ATK_RGBLCD_LTDC_G7_GPIO_AF;
    HAL_GPIO_Init(ATK_RGBLCD_LTDC_G7_GPIO_PORT, &gpio_init_struct);

    /* 初始化B3引脚 */
    gpio_init_struct.Pin        = ATK_RGBLCD_LTDC_B3_GPIO_PIN;
    gpio_init_struct.Mode       = GPIO_MODE_AF_PP;
    gpio_init_struct.Speed      = GPIO_SPEED_FREQ_HIGH;
    gpio_init_struct.Alternate  = ATK_RGBLCD_LTDC_B3_GPIO_AF;
    HAL_GPIO_Init(ATK_RGBLCD_LTDC_B3_GPIO_PORT, &gpio_init_struct);

    /* 初始化B4引脚 */
    gpio_init_struct.Pin        = ATK_RGBLCD_LTDC_B4_GPIO_PIN;
    gpio_init_struct.Mode       = GPIO_MODE_AF_PP;
    gpio_init_struct.Speed      = GPIO_SPEED_FREQ_HIGH;
    gpio_init_struct.Alternate  = ATK_RGBLCD_LTDC_B4_GPIO_AF;
    HAL_GPIO_Init(ATK_RGBLCD_LTDC_B4_GPIO_PORT, &gpio_init_struct);

    /* 初始化B5引脚 */
    gpio_init_struct.Pin        = ATK_RGBLCD_LTDC_B5_GPIO_PIN;
    gpio_init_struct.Mode       = GPIO_MODE_AF_PP;
    gpio_init_struct.Speed      = GPIO_SPEED_FREQ_HIGH;
    gpio_init_struct.Alternate  = ATK_RGBLCD_LTDC_B5_GPIO_AF;
    HAL_GPIO_Init(ATK_RGBLCD_LTDC_B5_GPIO_PORT, &gpio_init_struct);

    /* 初始化B6引脚 */
    gpio_init_struct.Pin        = ATK_RGBLCD_LTDC_B6_GPIO_PIN;
    gpio_init_struct.Mode       = GPIO_MODE_AF_PP;
    gpio_init_struct.Speed      = GPIO_SPEED_FREQ_HIGH;
    gpio_init_struct.Alternate  = ATK_RGBLCD_LTDC_B6_GPIO_AF;
    HAL_GPIO_Init(ATK_RGBLCD_LTDC_B6_GPIO_PORT, &gpio_init_struct);

    /* 初始化B7引脚 */
    gpio_init_struct.Pin        = ATK_RGBLCD_LTDC_B7_GPIO_PIN;
    gpio_init_struct.Mode       = GPIO_MODE_AF_PP;
    gpio_init_struct.Speed      = GPIO_SPEED_FREQ_HIGH;
    gpio_init_struct.Alternate  = ATK_RGBLCD_LTDC_B7_GPIO_AF;
    HAL_GPIO_Init(ATK_RGBLCD_LTDC_B7_GPIO_PORT, &gpio_init_struct);

    /* LTDC时钟配置 */
    atk_rgblcd_ltdc_clock_config();

    /* LTDC配置 */
    g_ltdc_handle.Instance                  = LTDC;
    g_ltdc_handle.Init.HSPolarity           = LTDC_HSPOLARITY_AL;
    g_ltdc_handle.Init.VSPolarity           = LTDC_VSPOLARITY_AL;
    g_ltdc_handle.Init.DEPolarity           = LTDC_DEPOLARITY_AL;
    g_ltdc_handle.Init.PCPolarity           = (timing->pixel_clock_polarity == 0) ? LTDC_PCPOLARITY_IIPC : LTDC_PCPOLARITY_IPC;
    g_ltdc_handle.Init.HorizontalSync       = timing->hsync_len - 1;
    g_ltdc_handle.Init.VerticalSync         = timing->vsync_len - 1;
    g_ltdc_handle.Init.AccumulatedHBP       = timing->hsync_len + timing->hback_porch - 1;
    g_ltdc_handle.Init.AccumulatedVBP       = timing->vsync_len + timing->vback_porch - 1;
    g_ltdc_handle.Init.AccumulatedActiveW   = timing->hsync_len + timing->hback_porch + timing->hactive - 1;
    g_ltdc_handle.Init.AccumulatedActiveH   = timing->vsync_len + timing->vback_porch + timing->vactive - 1;
    g_ltdc_handle.Init.TotalWidth           = timing->hsync_len + timing->hback_porch + timing->hactive + timing->hfront_porch - 1;
    g_ltdc_handle.Init.TotalHeigh           = timing->vsync_len + timing->vback_porch + timing->vactive + timing->vfront_porch - 1;
    g_ltdc_handle.Init.Backcolor.Blue       = 0;
    g_ltdc_handle.Init.Backcolor.Green      = 0;
    g_ltdc_handle.Init.Backcolor.Red        = 0;
    HAL_LTDC_Init(&g_ltdc_handle);

    /* LTDC层配置 */
    layer_init_struct.WindowX0          = 0;
    layer_init_struct.WindowX1          = width;
    layer_init_struct.WindowY0          = 0;
    layer_init_struct.WindowY1          = height;
    layer_init_struct.PixelFormat       = LTDC_PIXEL_FORMAT_RGB565;
    layer_init_struct.Alpha             = 255;
    layer_init_struct.Alpha0            = 0;
    layer_init_struct.BlendingFactor1   = LTDC_BLENDING_FACTOR1_PAxCA;
    layer_init_struct.BlendingFactor2   = LTDC_BLENDING_FACTOR2_PAxCA;
    layer_init_struct.FBStartAdress     = ATK_RGBLCD_LTDC_LAYER_FB_ADDR;
    layer_init_struct.ImageWidth        = width;
    layer_init_struct.ImageHeight       = height;
    layer_init_struct.Backcolor.Blue    = 0;
    layer_init_struct.Backcolor.Green   = 0;
    layer_init_struct.Backcolor.Red     = 0;
    HAL_LTDC_ConfigLayer(&g_ltdc_handle, &layer_init_struct, LTDC_LAYER_1);
}

/**
 * @brief       使能RGB LCD模块LTDC接口
 * @param       无
 * @retval      无
 */
void atk_rgblcd_ltdc_enable(void)
{
    __HAL_LTDC_ENABLE(&g_ltdc_handle);
}

/**
 * @brief       禁用RGB LCD模块LTDC接口
 * @param       无
 * @retval      无
 */
void atk_rgblcd_ltdc_disable(void)
{
    __HAL_LTDC_DISABLE(&g_ltdc_handle);
}

/**
 ****************************************************************************************************
 * @file        atk_rgblcd.c
 * @author      正点原子团队(ALIENTEK)
 * @version     V1.0
 * @date        2023-02-24
 * @brief       正点原子RGB LCD模块驱动代码
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

#include "./BSP/ATK_RGBLCD/atk_rgblcd.h"
#include "./BSP/ATK_RGBLCD/atk_rgblcd_ltdc.h"
#include "./BSP/ATK_RGBLCD/atk_rgblcd_font.h"
#include "./SYSTEM/usart/usart.h"

#define ATK_RGBLCD_LCD_RAW_WIDTH    g_atk_rgblcd_sta.param->width
#define ATK_RGBLCD_LCD_RAW_HEIGHT   g_atk_rgblcd_sta.param->height
#define ATK_RGBLCD_LCD_WIDTH        g_atk_rgblcd_sta.width
#define ATK_RGBLCD_LCD_HEIGHT       g_atk_rgblcd_sta.height
#define ATK_RGBLCD_FB               g_atk_rgblcd_sta.fb

/* RGB LCD模块参数结构体 */
typedef struct
{
    uint8_t id;
    uint16_t pid;
    uint16_t width;
    uint16_t height;
    atk_rgblcd_timing_t timing;
    atk_rgblcd_touch_type_t touch_type;
} atk_rgblcd_param_t;

/* RGB LCD模块参数匹配表 */
static const atk_rgblcd_param_t atk_rgblcd_param[] = {
    {0, ATK_RGBLCD_PID_4342,  480, 272, { 9000000, 1,  480,  40,  5,   1, 272,  8,  8, 1}, ATK_RGBLCD_TOUCH_TYPE_GTXX}, // ATK-MD0430R-480272
    {1, ATK_RGBLCD_PID_7084,  800, 480, {33000000, 1,  800,  46, 210,  1, 480, 23, 22, 1}, ATK_RGBLCD_TOUCH_TYPE_FTXX}, // ATK-MD0700R-800480
    {2, ATK_RGBLCD_PID_7016, 1024, 600, {45000000, 1, 1024, 140, 160, 20, 600, 20, 12, 3}, ATK_RGBLCD_TOUCH_TYPE_GTXX}, // ATK-MD0700R-1024600
    {3, ATK_RGBLCD_PID_7018, 1280, 800, {       0, 1, 1280,   0,   0,  0, 800,  0,  0, 0}, ATK_RGBLCD_TOUCH_TYPE_GTXX}, // ATK-MD0700R-1280800
    {4, ATK_RGBLCD_PID_4384,  800, 480, {33000000, 1,  800,  88,  40, 48, 480, 32, 13, 3}, ATK_RGBLCD_TOUCH_TYPE_GTXX}, // ATK-MD0430R-800480
    {5, ATK_RGBLCD_PID_1018, 1280, 800, {45000000, 0, 1280, 140,  10, 10, 800, 10, 10, 3}, ATK_RGBLCD_TOUCH_TYPE_GTXX}, // ATK-MD1010R-1280800
};

/* RGB LCD模块状态数据结构体 */
static struct
{
    const atk_rgblcd_param_t *param;
    uint16_t width;
    uint16_t height;
    atk_rgblcd_lcd_disp_dir_t disp_dir;
    uint16_t *fb;
} g_atk_rgblcd_sta = {0};

/**
 * @brief       RGB LCD模块ID获取
 * @param       无
 * @retval      RGB LCD模块ID
 */
static uint8_t atk_rgblcd_get_id(void)
{
    GPIO_InitTypeDef gpio_init_struct = {0};
    uint8_t id;

    ATK_RGBLCD_M0_GPIO_CLK_ENABLE();
    ATK_RGBLCD_M1_GPIO_CLK_ENABLE();
    ATK_RGBLCD_M2_GPIO_CLK_ENABLE();

    gpio_init_struct.Pin = ATK_RGBLCD_M0_GPIO_PIN;
    gpio_init_struct.Mode = GPIO_MODE_INPUT;
    gpio_init_struct.Pull = GPIO_NOPULL;
    gpio_init_struct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(ATK_RGBLCD_M0_GPIO_PORT, &gpio_init_struct);

    gpio_init_struct.Pin = ATK_RGBLCD_M1_GPIO_PIN;
    HAL_GPIO_Init(ATK_RGBLCD_M1_GPIO_PORT, &gpio_init_struct);

    gpio_init_struct.Pin = ATK_RGBLCD_M2_GPIO_PIN;
    HAL_GPIO_Init(ATK_RGBLCD_M2_GPIO_PORT, &gpio_init_struct);

    id = ATK_RGBLCD_READ_M0();
    id |= ATK_RGBLCD_READ_M1() << 1;
    id |= ATK_RGBLCD_READ_M2() << 2;

    return id;
}

/**
 * @brief       RGB LCD模块参数初始化
 * @param       通过RGB LCD的ID确定RGB LCD的尺寸和时序
 * @retval      ATK_RGBLCD_EOK   : RGB LCD模块参数初始化成功
 *              ATK_RGBLCD_EINVAL: 输入ID无效
 */
static uint8_t atk_rgblcd_setup_param_by_id(uint8_t id)
{
    uint8_t index;

    for (index=0; index < (sizeof(atk_rgblcd_param) / sizeof(atk_rgblcd_param[0])); index++)
    {
        if (id == atk_rgblcd_param[index].id)
        {
            g_atk_rgblcd_sta.param = &atk_rgblcd_param[index];
            return ATK_RGBLCD_EOK;
        }
    }

    return ATK_RGBLCD_EINVAL;
}

/**
 * @brief       RGB LCD模块硬件初始化
 * @param       无
 * @retval      ATK_RGBLCD_EOK  : RGB LCD模块初始化成功
 *              ATK_RGBLCD_ERROR: RGB LCD模块初始化失败
 */
static void atk_rgblcd_hw_init(void)
{
    GPIO_InitTypeDef gpio_init_struct = {0};

    ATK_RGBLCD_BL_GPIO_CLK_ENABLE();

    gpio_init_struct.Pin = ATK_RGBLCD_BL_GPIO_PIN;
    gpio_init_struct.Mode = GPIO_MODE_OUTPUT_PP;
    gpio_init_struct.Pull = GPIO_PULLUP;
    gpio_init_struct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(ATK_RGBLCD_BL_GPIO_PORT, &gpio_init_struct);
}

/**
 * @brief       平方函数，x^y
 * @param       x: 底数
 *              y: 指数
 * @retval      x^y
 */
static uint32_t atk_rgblcd_pow(uint8_t x, uint8_t y)
{
    uint8_t loop;
    uint32_t res = 1;

    for (loop=0; loop<y; loop++)
    {
        res *= x;
    }

    return res;
}

/**
 * @brief       根据RGB LCD模块显示方向换算坐标值
 * @param       x: X坐标值
 *              y: Y坐标值
 * @retval      无
 */
static inline void atk_rgblcd_pos_transform(uint16_t *x, uint16_t *y)
{
    uint16_t x_target;
    uint16_t y_target;

    switch (g_atk_rgblcd_sta.disp_dir)
    {
        case ATK_RGBLCD_LCD_DISP_DIR_0:
        {
            x_target = *x;
            y_target = *y;
            break;
        }
        case ATK_RGBLCD_LCD_DISP_DIR_90:
        {
            x_target = ATK_RGBLCD_LCD_RAW_WIDTH - *y - 1;
            y_target = *x;
            break;
        }
        case ATK_RGBLCD_LCD_DISP_DIR_180:
        {
            x_target = ATK_RGBLCD_LCD_RAW_WIDTH - *x - 1;
            y_target = ATK_RGBLCD_LCD_RAW_HEIGHT - *y - 1;
            break;
        }
        case ATK_RGBLCD_LCD_DISP_DIR_270:
        {
            x_target = *y;
            y_target = ATK_RGBLCD_LCD_RAW_HEIGHT - *x - 1;
            break;
        }
    }

    *x = x_target;
    *y = y_target;
}

#if (ATK_RGBLCD_USING_DMA2D != 0)
/**
 * @brief       DMA2D初始化
 * @param       无
 * @retval      无
 */
static void atk_rgblcd_dma2d_init(void)
{
    RCC->AHB3ENR |= RCC_AHB3ENR_DMA2DEN;    /* 使能DMA2D时钟 */
    DMA2D->CR &= ~DMA2D_CR_START;           /* 停止DMA2D */
    DMA2D->CR &= ~DMA2D_CR_MODE_Msk;        /* 寄存器到存储器模式 */
    DMA2D->CR |= DMA2D_R2M;
    DMA2D->OPFCCR &= ~DMA2D_OPFCCR_CM_Msk;  /* RGB565模式 */
    DMA2D->OPFCCR |= DMA2D_OUTPUT_RGB565;
}

/**
 * @brief       DMA2D LCD区域填充
 * @param       xs   : 区域起始X坐标
 *              ys   : 区域起始Y坐标
 *              xe   : 区域终止X坐标
 *              ye   : 区域终止Y坐标
 *              color: 区域填充颜色
 * @retval      无
 */
static inline void atk_rgblcd_dma2d_fill(uint16_t xs, uint16_t ys, uint16_t xe, uint16_t ye, uint16_t color)
{
    atk_rgblcd_pos_transform(&xs, &ys);
    atk_rgblcd_pos_transform(&xe, &ye);

    if (xs > xe)
    {
        xs = xs ^ xe;
        xe = xs ^ xe;
        xs = xs ^ xe;
    }
    if (ys > ye)
    {
        ys = ys ^ ye;
        ye = ys ^ ye;
        ys = ys ^ ye;
    }

    DMA2D->CR &= ~DMA2D_CR_START;                                               /* 停止DMA2D */
    DMA2D->OOR = ATK_RGBLCD_LCD_RAW_WIDTH - (xe - xs + 1);                      /* 行偏移 */
    DMA2D->OMAR = (uint32_t)&ATK_RGBLCD_FB[ys * ATK_RGBLCD_LCD_RAW_WIDTH + xs]; /* 存储器地址 */
    DMA2D->NLR &= ~DMA2D_NLR_PL_Msk;                                            /* 每行的像素数 */
    DMA2D->NLR |= ((xe - xs + 1) << DMA2D_NLR_PL_Pos);
    DMA2D->NLR &= ~DMA2D_NLR_NL_Msk;                                            /* 总的行数 */
    DMA2D->NLR |= ((ye - ys + 1) << DMA2D_NLR_NL_Pos);
    DMA2D->OCOLR = color;                                                       /* 输出颜色 */
    DMA2D->CR |= DMA2D_CR_START;                                                /* 开启DMA2D */
    while ((DMA2D->ISR & DMA2D_ISR_TCIF) != DMA2D_ISR_TCIF);                    /* 等待DMA2D传输完成 */
    DMA2D->IFCR |= DMA2D_IFCR_CTCIF;                                            /* 清除传输完成标志 */
}
#endif

/**
 * @brief       RGB LCD模块初始化
 * @param       无
 * @retval      ATK_RGBLCD_EOK  : RGB LCD模块初始化成功
 *              ATK_RGBLCD_ERROR: RGB LCD模块初始化失败
 */
uint8_t atk_rgblcd_init(void)
{
    uint8_t id;
    uint8_t ret;

    id = atk_rgblcd_get_id();                                       /* RGB LCD模块ID获取 */
    ret = atk_rgblcd_setup_param_by_id(id);                         /* RGB LCD模块参数初始化 */
    if (ret != ATK_RGBLCD_EOK)
    {
        return ATK_RGBLCD_ERROR;
    }
    atk_rgblcd_hw_init();                                           /* RGB LCD模块硬件初始化 */
    atk_rgblcd_ltdc_init(   ATK_RGBLCD_LCD_RAW_WIDTH,
                            ATK_RGBLCD_LCD_RAW_HEIGHT,
                            &g_atk_rgblcd_sta.param->timing);       /* RGB LCD模块LTDC接口初始化 */
    g_atk_rgblcd_sta.fb = (uint16_t *)ATK_RGBLCD_LTDC_LAYER_FB_ADDR;
#if (ATK_RGBLCD_USING_DMA2D != 0)
    atk_rgblcd_dma2d_init();                                        /* 初始化DMA2D */
#endif
    atk_rgblcd_set_disp_dir(ATK_RGBLCD_LCD_DISP_DIR_0);
    atk_rgblcd_clear(ATK_RGBLCD_WHITE);
    atk_rgblcd_backlight_on();                                      /* 开启RGB LCD模块LCD背光 */
#if (ATK_RGBLCD_USING_TOUCH != 0)
    ret = atk_rgblcd_touch_init(g_atk_rgblcd_sta.param->touch_type);
    if (ret != ATK_RGBLCD_TOUCH_EOK)
    {
        return ATK_RGBLCD_ERROR;
    }
#endif

    return ATK_RGBLCD_EOK;
}

/**
 * @brief       获取RGB LCD模块PID
 * @param       无
 * @retval      0     : RGB LCD模块未初始化
 *              其他值: RGB LCD模块PID
 */
uint16_t atk_rgblcd_get_pid(void)
{
    return g_atk_rgblcd_sta.param->pid;
}

/**
 * @brief       获取RGB LCD模块LCD宽度
 * @param       无
 * @retval      0     : RGB LCD模块未初始化
 *              其他值: RGB LCD模块LCD宽度
 */
uint16_t atk_rgblcd_get_lcd_width(void)
{
    return g_atk_rgblcd_sta.width;
}

/**
 * @brief       获取RGB LCD模块LCD高度
 * @param       无
 * @retval      0     : RGB LCD模块未初始化
 *              其他值: RGB LCD模块LCD高度
 */
uint16_t atk_rgblcd_get_lcd_height(void)
{
    return g_atk_rgblcd_sta.height;
}

/**
 * @brief       开启RGB LCD模块LCD背光
 * @param       无
 * @retval      无
 */
void atk_rgblcd_backlight_on(void)
{
    ATK_RGBLCD_BL(1);
}

/**
 * @brief       关闭RGB LCD模块LCD背光
 * @param       无
 * @retval      无
 */
void atk_rgblcd_backlight_off(void)
{
    ATK_RGBLCD_BL(0);
}

/**
 * @brief       开启RGB LCD模块LCD显示
 * @param       无
 * @retval      无
 */
void atk_rgblcd_display_on(void)
{
    atk_rgblcd_ltdc_enable();
}

/**
 * @brief       关闭RGB LCD模块LCD显示
 * @param       无
 * @retval      无
 */
void atk_rgblcd_display_off(void)
{
    atk_rgblcd_ltdc_disable();
}

/**
 * @brief       设置RGB LCD模块LCD显示方向
 * @param       disp_dir: ATK_RGBLCD_LCD_DISP_DIR_0  : LCD顺时针旋转0°显示内容
 *                        ATK_RGBLCD_LCD_DISP_DIR_90 : LCD顺时针旋转90°显示内容
 *                        ATK_RGBLCD_LCD_DISP_DIR_180: LCD顺时针旋转180°显示内容
 *                        ATK_RGBLCD_LCD_DISP_DIR_270: LCD顺时针旋转270°显示内容
 * @retval      ATK_RGBLCD_EOK   : 设置RGB LCD模块LCD显示方向成功
 *              ATK_RGBLCD_EINVAL: 传入参数错误
 */
uint8_t atk_rgblcd_set_disp_dir(atk_rgblcd_lcd_disp_dir_t disp_dir)
{
    switch (disp_dir)
    {
        case ATK_RGBLCD_LCD_DISP_DIR_0:
        case ATK_RGBLCD_LCD_DISP_DIR_180:
        {
            g_atk_rgblcd_sta.width = g_atk_rgblcd_sta.param->width;
            g_atk_rgblcd_sta.height = g_atk_rgblcd_sta.param->height;
            break;
        }
        case ATK_RGBLCD_LCD_DISP_DIR_90:
        case ATK_RGBLCD_LCD_DISP_DIR_270:
        {
            g_atk_rgblcd_sta.width = g_atk_rgblcd_sta.param->height;
            g_atk_rgblcd_sta.height = g_atk_rgblcd_sta.param->width;
            break;
        }
        default:
        {
            return ATK_RGBLCD_EINVAL;
        }
    }

    g_atk_rgblcd_sta.disp_dir = disp_dir;

    return ATK_RGBLCD_EOK;
}

/**
 * @brief       获取RGB LCD模块LCD扫描方向
 * @param       无
 * @retval      RGB LCD模块LCD扫描方向
 */
atk_rgblcd_lcd_disp_dir_t atk_rgblcd_get_disp_dir(void)
{
    return g_atk_rgblcd_sta.disp_dir;
}

/**
 * @brief       RGB LCD模块LCD区域填充
 * @param       xs   : 区域起始X坐标
 *              ys   : 区域起始Y坐标
 *              xe   : 区域终止X坐标
 *              ye   : 区域终止Y坐标
 *              color: 区域填充颜色
 * @retval      无
 */
void atk_rgblcd_fill(uint16_t xs, uint16_t ys, uint16_t xe, uint16_t ye, uint16_t color)
{
    if (xe >= ATK_RGBLCD_LCD_WIDTH)
    {
        xe = ATK_RGBLCD_LCD_WIDTH - 1;
    }
    if (ye >= ATK_RGBLCD_LCD_HEIGHT)
    {
        ye = ATK_RGBLCD_LCD_HEIGHT - 1;
    }

#if (ATK_RGBLCD_USING_DMA2D != 0)
    atk_rgblcd_dma2d_fill(xs, ys, xe, ye, color);
#else
    uint16_t x_index;
    uint16_t y_index;

    for (y_index=ys; y_index<ye + 1; y_index++)
    {
        for (x_index=xs; x_index<xe + 1; x_index++)
        {
            atk_rgblcd_pos_transform(&x_index, &y_index);
            ATK_RGBLCD_FB[y_index * ATK_RGBLCD_LCD_RAW_WIDTH + x_index] = color;
        }
    }
#endif
}

/**
 * @brief       RGB LCD模块LCD清屏
 * @param       color: 清屏颜色
 * @retval      无
 */
void atk_rgblcd_clear(uint16_t color)
{
    atk_rgblcd_fill(0, 0, ATK_RGBLCD_LCD_WIDTH - 1, ATK_RGBLCD_LCD_HEIGHT - 1, color);
}

/**
 * @brief       RGB LCD模块LCD画点
 * @param       x    : 待画点的X坐标
 *              y    : 待画点的Y坐标
 *              color: 待画点的颜色
 * @retval      无
 */
void atk_rgblcd_draw_point(uint16_t x, uint16_t y, uint16_t color)
{
    atk_rgblcd_pos_transform(&x, &y);
    ATK_RGBLCD_FB[y * ATK_RGBLCD_LCD_RAW_WIDTH + x] = color;
}

/**
 * @brief       RGB LCD模块LCD读点
 * @param       x    : 待读点的X坐标
 *              y    : 待读点的Y坐标
 * @retval      待读点的颜色
 */
uint16_t atk_rgblcd_read_point(uint16_t x, uint16_t y)
{
    atk_rgblcd_pos_transform(&x, &y);
    return ATK_RGBLCD_FB[y * ATK_RGBLCD_LCD_RAW_WIDTH + x];
}

/**
 * @brief       RGB LCD模块LCD画线段
 * @param       x1   : 待画线段端点1的X坐标
 *              y1   : 待画线段端点1的Y坐标
 *              x2   : 待画线段端点2的X坐标
 *              y2   : 待画线段端点2的Y坐标
 *              color: 待画线段的颜色
 * @retval      无
 */
void atk_rgblcd_draw_line(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t color)
{
    uint16_t x_delta;
    uint16_t y_delta;
    int16_t x_sign;
    int16_t y_sign;
    int16_t error;
    int16_t error2;

    x_delta = (x1 < x2) ? (x2 - x1) : (x1 - x2);
    y_delta = (y1 < y2) ? (y2 - y1) : (y1 - y2);
    x_sign = (x1 < x2) ? 1 : -1;
    y_sign = (y1 < y2) ? 1 : -1;
    error = x_delta - y_delta;

    atk_rgblcd_draw_point(x2, y2, color);

    while ((x1 != x2) || (y1 != y2))
    {
        atk_rgblcd_draw_point(x1, y1, color);

        error2 = error << 1;
        if (error2 > -y_delta)
        {
            error -= y_delta;
            x1 += x_sign;
        }

        if (error2 < x_delta)
        {
            error += x_delta;
            y1 += y_sign;
        }
    }
}

/**
 * @brief       RGB LCD模块LCD画矩形框
 * @param       x1   : 待画矩形框端点1的X坐标
 *              y1   : 待画矩形框端点1的Y坐标
 *              x2   : 待画矩形框端点2的X坐标
 *              y2   : 待画矩形框端点2的Y坐标
 *              color: 待画矩形框的颜色
 * @retval      无
 */
void atk_rgblcd_draw_rect(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t color)
{
    atk_rgblcd_draw_line(x1, y1, x2, y1, color);
    atk_rgblcd_draw_line(x1, y2, x2, y2, color);
    atk_rgblcd_draw_line(x1, y1, x1, y2, color);
    atk_rgblcd_draw_line(x2, y1, x2, y2, color);
}

/**
 * @brief       RGB LCD模块LCD画圆形框
 * @param       x    : 待画圆形框原点的X坐标
 *              y    : 待画圆形框原点的Y坐标
 *              r    : 待画圆形框的半径
 *              color: 待画圆形框的颜色
 * @retval      无
 */
void atk_rgblcd_draw_circle(uint16_t x, uint16_t y, uint16_t r, uint16_t color)
{
    int32_t x_t;
    int32_t y_t;
    int32_t error;
    int32_t error2;

    x_t = -r;
    y_t = 0;
    error = 2 - 2 * r;

    do {
        atk_rgblcd_draw_point(x - x_t, y + y_t, color);
        atk_rgblcd_draw_point(x + x_t, y + y_t, color);
        atk_rgblcd_draw_point(x + x_t, y - y_t, color);
        atk_rgblcd_draw_point(x - x_t, y - y_t, color);

        error2 = error;
        if (error2 <= y_t)
        {
            y_t++;
            error = error + (y_t * 2 + 1);
            if ((-x_t == y_t) && (error2 <= x_t))
            {
                error2 = 0;
            }
        }

        if (error2 > x_t)
        {
            x_t++;
            error = error + (x_t * 2 + 1);
        }
    } while (x_t <= 0);
}

/**
 * @brief       RGB LCD模块LCD显示1个字符
 * @param       x    : 待显示字符的X坐标
 *              y    : 待显示字符的Y坐标
 *              ch   : 待显示字符
 *              font : 待显示字符的字体
 *              color: 待显示字符的颜色
 * @retval      无
 */
void atk_rgblcd_show_char(uint16_t x, uint16_t y, char ch, atk_rgblcd_lcd_font_t font, uint16_t color)
{
    const uint8_t *ch_code;
    uint8_t ch_width;
    uint8_t ch_height;
    uint8_t ch_size;
    uint8_t ch_offset;
    uint8_t byte_index;
    uint8_t byte_code;
    uint8_t bit_index;
    uint8_t width_index = 0;
    uint8_t height_index = 0;

    ch_offset = ch - ' ';

    switch (font)
    {
#if (ATK_RGBLCD_FONT_12 != 0)
        case ATK_RGBLCD_LCD_FONT_12:
        {
            ch_code = atk_rgblcd_font_1206[ch_offset];
            ch_width = ATK_RGBLCD_FONT_12_CHAR_WIDTH;
            ch_height = ATK_RGBLCD_FONT_12_CHAR_HEIGHT;
            ch_size = ATK_RGBLCD_FONT_12_CHAR_SIZE;
            break;
        }
#endif
#if (ATK_RGBLCD_FONT_16 != 0)
        case ATK_RGBLCD_LCD_FONT_16:
        {
            ch_code = atk_rgblcd_font_1608[ch_offset];
            ch_width = ATK_RGBLCD_FONT_16_CHAR_WIDTH;
            ch_height = ATK_RGBLCD_FONT_16_CHAR_HEIGHT;
            ch_size = ATK_RGBLCD_FONT_16_CHAR_SIZE;
            break;
        }
#endif
#if (ATK_RGBLCD_FONT_24 != 0)
        case ATK_RGBLCD_LCD_FONT_24:
        {
            ch_code = atk_rgblcd_font_2412[ch_offset];
            ch_width = ATK_RGBLCD_FONT_24_CHAR_WIDTH;
            ch_height = ATK_RGBLCD_FONT_24_CHAR_HEIGHT;
            ch_size = ATK_RGBLCD_FONT_24_CHAR_SIZE;
            break;
        }
#endif
#if (ATK_RGBLCD_FONT_32 != 0)
        case ATK_RGBLCD_LCD_FONT_32:
        {
            ch_code = atk_rgblcd_font_3216[ch_offset];
            ch_width = ATK_RGBLCD_FONT_32_CHAR_WIDTH;
            ch_height = ATK_RGBLCD_FONT_32_CHAR_HEIGHT;
            ch_size = ATK_RGBLCD_FONT_32_CHAR_SIZE;
            break;
        }
#endif
        default:
        {
            return;
        }
    }

    if ((x + ch_width > ATK_RGBLCD_LCD_WIDTH) || (y + ch_height > ATK_RGBLCD_LCD_HEIGHT))
    {
        return;
    }

    for (byte_index=0; byte_index<ch_size; byte_index++)
    {
        byte_code = ch_code[byte_index];
        for (bit_index=0; bit_index<8; bit_index++)
        {
            if ((byte_code & 0x80) != 0)
            {
                atk_rgblcd_draw_point(x + width_index, y + height_index, color);
            }
            height_index++;
            if (height_index == ch_height)
            {
                height_index = 0;
                width_index++;
                break;
            }
            byte_code <<= 1;
        }
    }
}

/**
 * @brief       RGB LCD模块LCD显示字符串
 * @note        会自动换行和换页
 * @param       x     : 待显示字符串的X坐标
 *              y     : 待显示字符串的Y坐标
 *              width : 待显示字符串的显示高度
 *              height: 待显示字符串的显示宽度
 *              str   : 待显示字符串
 *              font  : 待显示字符串的字体
 *              color : 待显示字符串的颜色
 * @retval      无
 */
void atk_rgblcd_show_string(uint16_t x, uint16_t y, uint16_t width, uint16_t height, char *str, atk_rgblcd_lcd_font_t font, uint16_t color)
{
    uint8_t ch_width;
    uint8_t ch_height;
    uint16_t x_raw;
    uint16_t y_raw;
    uint16_t x_limit;
    uint16_t y_limit;

    switch (font)
    {
#if (ATK_RGBLCD_FONT_12 != 0)
        case ATK_RGBLCD_LCD_FONT_12:
        {
            ch_width = ATK_RGBLCD_FONT_12_CHAR_WIDTH;
            ch_height = ATK_RGBLCD_FONT_12_CHAR_HEIGHT;
            break;
        }
#endif
#if (ATK_RGBLCD_FONT_16 != 0)
        case ATK_RGBLCD_LCD_FONT_16:
        {
            ch_width = ATK_RGBLCD_FONT_16_CHAR_WIDTH;
            ch_height = ATK_RGBLCD_FONT_16_CHAR_HEIGHT;
            break;
        }
#endif
#if (ATK_RGBLCD_FONT_24 != 0)
        case ATK_RGBLCD_LCD_FONT_24:
        {
            ch_width = ATK_RGBLCD_FONT_24_CHAR_WIDTH;
            ch_height = ATK_RGBLCD_FONT_24_CHAR_HEIGHT;
            break;
        }
#endif
#if (ATK_RGBLCD_FONT_32 != 0)
        case ATK_RGBLCD_LCD_FONT_32:
        {
            ch_width = ATK_RGBLCD_FONT_32_CHAR_WIDTH;
            ch_height = ATK_RGBLCD_FONT_32_CHAR_HEIGHT;
            break;
        }
#endif
        default:
        {
            return;
        }
    }

    x_raw = x;
    y_raw = y;
    x_limit = ((x + width + 1) > ATK_RGBLCD_LCD_WIDTH) ? ATK_RGBLCD_LCD_WIDTH : (x + width + 1);
    y_limit = ((y + height + 1) > ATK_RGBLCD_LCD_HEIGHT) ? ATK_RGBLCD_LCD_HEIGHT : (y + height + 1);

    while ((*str >= ' ') && (*str <= '~'))
    {
        if (x + ch_width >= x_limit)
        {
            x = x_raw;
            y += ch_height;
        }

        if (y + ch_height >= y_limit)
        {
            y = x_raw;
            x = y_raw;
        }

        atk_rgblcd_show_char(x, y, *str, font, color);

        x += ch_width;
        str++;
    }
}

/**
 * @brief       RGB LCD模块LCD显示数字，可控制显示高位0
 * @param       x    : 待显示数字的X坐标
 *              y    : 待显示数字的Y坐标
 *              num  : 待显示数字
 *              len  : 待显示数字的位数
 *              mode : ATK_RGBLCD_NUM_SHOW_NOZERO: 数字高位0不显示
 *                     ATK_RGBLCD_NUM_SHOW_ZERO  : 数字高位0显示
 *              font : 待显示数字的字体
 *              color: 待显示数字的颜色
 * @retval      无
 */
void atk_rgblcd_show_xnum(uint16_t x, uint16_t y, uint32_t num, uint8_t len, atk_rgblcd_num_mode_t mode, atk_rgblcd_lcd_font_t font, uint16_t color)
{
    uint8_t ch_width;
    uint8_t len_index;
    uint8_t num_index;
    uint8_t first_nozero = 0;
    char pad;

    switch (font)
    {
#if (ATK_RGBLCD_FONT_12 != 0)
        case ATK_RGBLCD_LCD_FONT_12:
        {
            ch_width = ATK_RGBLCD_FONT_12_CHAR_WIDTH;
            break;
        }
#endif
#if (ATK_RGBLCD_FONT_16 != 0)
        case ATK_RGBLCD_LCD_FONT_16:
        {
            ch_width = ATK_RGBLCD_FONT_16_CHAR_WIDTH;
            break;
        }
#endif
#if (ATK_RGBLCD_FONT_24 != 0)
        case ATK_RGBLCD_LCD_FONT_24:
        {
            ch_width = ATK_RGBLCD_FONT_24_CHAR_WIDTH;
            break;
        }
#endif
#if (ATK_RGBLCD_FONT_32 != 0)
        case ATK_RGBLCD_LCD_FONT_32:
        {
            ch_width = ATK_RGBLCD_FONT_32_CHAR_WIDTH;
            break;
        }
#endif
        default:
        {
            return;
        }
    }

    switch (mode)
    {
        case ATK_RGBLCD_NUM_SHOW_NOZERO:
        {
            pad = ' ';
            break;
        }
        case ATK_RGBLCD_NUM_SHOW_ZERO:
        {
            pad = '0';
            break;
        }
        default:
        {
            return;
        }
    }

    for (len_index=0; len_index<len; len_index++)
    {
        num_index = (num / atk_rgblcd_pow(10, len - len_index - 1)) % 10;
        if ((first_nozero == 0) && (len_index < (len - 1)))
        {
            if (num_index == 0)
            {
                atk_rgblcd_show_char(x + ch_width * len_index, y, pad, font, color);
                continue;
            }
            else
            {
                first_nozero = 1;
            }
        }

        atk_rgblcd_show_char(x + ch_width * len_index, y, num_index + '0', font, color);
    }
}

/**
 * @brief       RGB LCD模块LCD显示数字，不显示高位0
 * @param       x    : 待显示数字的X坐标
 *              y    : 待显示数字的Y坐标
 *              num  : 待显示数字
 *              len  : 待显示数字的位数
 *              font : 待显示数字的字体
 *              color: 待显示数字的颜色
 * @retval      无
 */
void atk_rgblcd_show_num(uint16_t x, uint16_t y, uint32_t num, uint8_t len, atk_rgblcd_lcd_font_t font, uint16_t color)
{
    atk_rgblcd_show_xnum(x, y, num, len, ATK_RGBLCD_NUM_SHOW_NOZERO, font, color);
}

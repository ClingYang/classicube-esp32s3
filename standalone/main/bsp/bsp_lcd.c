// SPDX-License-Identifier: MIT
#include "bsp_lcd.h"

#include <string.h>
#include "bsp_pins.h"
#include "bsp_pca9557.h"
#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_memory_utils.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_st7789.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "bsp_lcd";

#define LCD_LEDC_MODE       LEDC_LOW_SPEED_MODE
#define LCD_LEDC_TIMER      LEDC_TIMER_0
#define LCD_LEDC_CHANNEL    LEDC_CHANNEL_0
#define LCD_LEDC_RES        LEDC_TIMER_10_BIT
#define LCD_LEDC_FREQ_HZ    5000
#define LCD_LEDC_FULL_DUTY  1023

static esp_lcd_panel_handle_t s_panel;
static esp_lcd_panel_io_handle_t s_io;
static SemaphoreHandle_t s_trans_done;
static bool s_inited;

/* One band worth of pixels: 320 * 40 * 2 = 25600 bytes. */
static uint16_t *s_band_buf;

static bool lcd_on_color_trans_done(esp_lcd_panel_io_handle_t panel_io,
                                    esp_lcd_panel_io_event_data_t *edata,
                                    void *user_ctx)
{
    (void)panel_io;
    (void)edata;
    BaseType_t woken = pdFALSE;
    if (s_trans_done) {
        xSemaphoreGiveFromISR(s_trans_done, &woken);
    }
    return woken == pdTRUE;
}

static esp_err_t backlight_init(void)
{
    /* Drive the backlight as a plain GPIO first: this board's backlight is
     * active-low, and a plain output removes LEDC from the diagnosis path. */
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << BSP_LCD_BACKLIGHT,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&io), TAG, "backlight gpio");
    return gpio_set_level(BSP_LCD_BACKLIGHT, 1);  /* start OFF (high) */
}

esp_err_t bsp_lcd_set_backlight(int percent)
{
    if (percent < 0) {
        percent = 0;
    }
    if (percent > 100) {
        percent = 100;
    }
    /* Active-low: percent > 0 -> drive low -> lit. PWM dimming comes later. */
    return gpio_set_level(BSP_LCD_BACKLIGHT, percent > 0 ? 0 : 1);
}

esp_err_t bsp_lcd_init(void)
{
    if (s_inited) {
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(backlight_init(), TAG, "backlight init");

    s_trans_done = xSemaphoreCreateCounting(16, 0);
    ESP_RETURN_ON_FALSE(s_trans_done != NULL, ESP_ERR_NO_MEM, TAG, "sem alloc");

    spi_bus_config_t bus_cfg = {
        .sclk_io_num = BSP_LCD_SPI_CLK,
        .mosi_io_num = BSP_LCD_SPI_MOSI,
        .miso_io_num = -1,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        /* Upper bound only: big enough for a full-frame transfer from PSRAM. */
        .max_transfer_sz = BSP_LCD_H_RES * BSP_LCD_V_RES * (int)sizeof(uint16_t),
    };
    ESP_RETURN_ON_ERROR(spi_bus_initialize(BSP_LCD_SPI_HOST, &bus_cfg, SPI_DMA_CH_AUTO), TAG, "spi bus");

    esp_lcd_panel_io_spi_config_t io_cfg = {
        .cs_gpio_num = -1,      /* PCA9557 keeps CS low */
        .dc_gpio_num = BSP_LCD_DC,
        .spi_mode = 2,   /* panel needs CPOL=1 on this board */
        .pclk_hz = BSP_LCD_PIXEL_CLOCK_HZ,
        .trans_queue_depth = 10,
        .on_color_trans_done = lcd_on_color_trans_done,
        .user_ctx = NULL,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .flags.psram_dma_direct = 1,   /* allow DMA straight out of PSRAM */
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)BSP_LCD_SPI_HOST, &io_cfg, &s_io),
                        TAG, "panel io");

    esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = -1,   /* no reset pin on this board */
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .data_endian = LCD_RGB_DATA_ENDIAN_LITTLE,
        .bits_per_pixel = 16,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_st7789(s_io, &panel_cfg, &s_panel), TAG, "new panel");

    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(s_panel), TAG, "panel reset");

    /* Panel is selected permanently; make sure the expander bit really is low. */
    ESP_RETURN_ON_ERROR(bsp_pca9557_set(BSP_PCA_BIT_LCD_CS, false), TAG, "lcd cs low");

    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(s_panel), TAG, "panel init");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_invert_color(s_panel, true), TAG, "invert");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_swap_xy(s_panel, true), TAG, "swap xy");     /* landscape */
    ESP_RETURN_ON_ERROR(esp_lcd_panel_mirror(s_panel, true, false), TAG, "mirror");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(s_panel, true), TAG, "disp on");

    s_band_buf = heap_caps_malloc(BSP_LCD_H_RES * BSP_LCD_BAND_LINES * sizeof(uint16_t),
                                  MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    ESP_RETURN_ON_FALSE(s_band_buf != NULL, ESP_ERR_NO_MEM, TAG, "band buffer alloc");

    s_inited = true;
    ESP_LOGI(TAG, "ST7789 %dx%d @ %dMHz ready", BSP_LCD_H_RES, BSP_LCD_V_RES,
             BSP_LCD_PIXEL_CLOCK_HZ / 1000000);
    return ESP_OK;
}

esp_lcd_panel_handle_t bsp_lcd_panel(void)
{
    return s_panel;
}

esp_err_t bsp_lcd_blit(int x_start, int y_start, int x_end, int y_end, const uint16_t *data)
{
    ESP_RETURN_ON_FALSE(s_inited, ESP_ERR_INVALID_STATE, TAG, "not inited");
    if(esp_ptr_external_ram(data))return bsp_lcd_blit_staged(x_start,y_start,x_end,y_end,data);

    esp_err_t err = esp_lcd_panel_draw_bitmap(s_panel, x_start, y_start, x_end, y_end, data);
    if (err != ESP_OK) {
        return err;
    }
    /* Wait for this transfer so the caller can reuse the buffer. */
    if (xSemaphoreTake(s_trans_done, pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGW(TAG, "transfer timeout (%d,%d)-(%d,%d)", x_start, y_start, x_end, y_end);
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

esp_err_t bsp_lcd_blit_async(int x_start, int y_start, int x_end, int y_end, const uint16_t *data)
{
    ESP_RETURN_ON_FALSE(s_inited, ESP_ERR_INVALID_STATE, TAG, "not inited");
    return esp_lcd_panel_draw_bitmap(s_panel, x_start, y_start, x_end, y_end, data);
}

esp_err_t bsp_lcd_blit_staged(int x,int y,int x_end,int y_end,const uint16_t *data)
{
    int width=x_end-x,height=y_end-y;
    if(!s_inited || !data || x<0 || y<0 || width<=0 || height<=0 || x_end>BSP_LCD_H_RES || y_end>BSP_LCD_V_RES)return ESP_ERR_INVALID_ARG;
    for(int row=0;row<height;row+=BSP_LCD_BAND_LINES) {
        int rows=height-row;if(rows>BSP_LCD_BAND_LINES)rows=BSP_LCD_BAND_LINES;
        memcpy(s_band_buf,data+(size_t)row*width,(size_t)width*rows*sizeof(uint16_t));
        esp_err_t err=bsp_lcd_blit(x,y+row,x_end,y+row+rows,s_band_buf);
        if(err!=ESP_OK)return err;
    }
    return ESP_OK;
}

bool bsp_lcd_wait_transfer(uint32_t timeout_ms)
{
    if (!s_inited) {
        return false;
    }
    return xSemaphoreTake(s_trans_done, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}

void bsp_lcd_flush_pending(void)
{
    while (s_inited && xSemaphoreTake(s_trans_done, 0) == pdTRUE) {
    }
}

void bsp_lcd_draw_rect(int x, int y, int w, int h, uint16_t color)
{
    if (!s_inited || w <= 0 || h <= 0) {
        return;
    }
    if (x < 0) {
        w += x;
        x = 0;
    }
    if (y < 0) {
        h += y;
        y = 0;
    }
    if (x + w > BSP_LCD_H_RES) {
        w = BSP_LCD_H_RES - x;
    }
    if (y + h > BSP_LCD_V_RES) {
        h = BSP_LCD_V_RES - y;
    }
    if (w <= 0 || h <= 0) {
        return;
    }

    /* Split into row bands: the scratch buffer only holds BSP_LCD_BAND_LINES rows. */
    int rows_left = h;
    int row = 0;
    while (rows_left > 0) {
        int rows = rows_left > BSP_LCD_BAND_LINES ? BSP_LCD_BAND_LINES : rows_left;
        for (int i = 0; i < w * rows; i++) {
            s_band_buf[i] = color;
        }
        esp_lcd_panel_draw_bitmap(s_panel, x, y + row, x + w, y + row + rows, s_band_buf);
        xSemaphoreTake(s_trans_done, pdMS_TO_TICKS(1000));
        row += rows;
        rows_left -= rows;
    }
}

void bsp_lcd_fill(uint16_t color)
{
    bsp_lcd_draw_rect(0, 0, BSP_LCD_H_RES, BSP_LCD_V_RES, color);
}

void bsp_lcd_color_bars(void)
{
    static const uint16_t bars[8] = {
        0xFFFF, 0xFFE0, 0x07FF, 0x07E0, 0xF81F, 0xF800, 0x001F, 0x0000,
    };
    int bar_w = BSP_LCD_H_RES / 8;

    for (int b = 0; b < 8; b++) {
        int x = b * bar_w;
        int w = (b == 7) ? (BSP_LCD_H_RES - x) : bar_w;
        bsp_lcd_draw_rect(x, 0, w, BSP_LCD_V_RES, bars[b]);
    }
}

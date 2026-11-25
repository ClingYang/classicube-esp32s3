// SPDX-License-Identifier: MIT
#include "bsp_audio.h"

#include "bsp_i2c.h"
#include "bsp_pca9557.h"
#include "bsp_pins.h"
#include "driver/i2s_std.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "esp_log.h"

#define MUSIC_VOLUME            84
#define ES8311_DAC_CONTROL_REG  0x31
#define ES8311_DAC_VOLUME_REG   0x32
#define ES8311_DAC_MUTE_MASK    0x60
#define PCA9557_OUTPUT_REG     0x01

static const char *TAG = "ridge_audio";

static i2s_chan_handle_t s_tx;
static esp_codec_dev_handle_t s_codec;
static const audio_codec_ctrl_if_t *s_control;
static const audio_codec_data_if_t *s_data;
static const audio_codec_gpio_if_t *s_gpio;
static const audio_codec_if_t *s_interface;
static bool s_tx_enabled;

static esp_err_t set_amplifier(bool on)
{
    esp_err_t err = bsp_pca9557_set(BSP_PCA_BIT_PA_EN, on);
    if (err != ESP_OK) {
        return err;
    }

    uint8_t reg = PCA9557_OUTPUT_REG;
    uint8_t latch = 0;
    err = bsp_i2c_write_read(BSP_PCA9557_ADDR, &reg, 1, &latch, 1);
    if (err != ESP_OK) {
        return err;
    }
    if (((latch >> BSP_PCA_BIT_PA_EN) & 1u) != on) {
        ESP_LOGE(TAG, "PA enable latch 0x%02x did not reach %d", (unsigned)latch, on);
        return ESP_ERR_INVALID_STATE;
    }
    ESP_LOGI(TAG, "PA_EN latch %s", on ? "high" : "low");
    return ESP_OK;
}

static esp_err_t set_dac_muted(bool muted)
{
    int control = 0;
    int level = 0;
    if (esp_codec_dev_set_out_mute(s_codec, muted) != ESP_CODEC_DEV_OK ||
        esp_codec_dev_read_reg(s_codec, ES8311_DAC_CONTROL_REG, &control) != ESP_CODEC_DEV_OK ||
        esp_codec_dev_read_reg(s_codec, ES8311_DAC_VOLUME_REG, &level) != ESP_CODEC_DEV_OK) {
        return ESP_FAIL;
    }

    if ((control & ES8311_DAC_MUTE_MASK) != (muted ? ES8311_DAC_MUTE_MASK : 0) || level == 0) {
        ESP_LOGE(TAG, "unexpected DAC state: control=0x%02x volume=0x%02x", (unsigned)control, (unsigned)level);
        return ESP_ERR_INVALID_STATE;
    }
    ESP_LOGI(TAG, "DAC %s, volume register 0x%02x", muted ? "muted" : "on", (unsigned)level);
    return ESP_OK;
}

static void release_audio(void)
{
    if (s_codec) esp_codec_dev_delete(s_codec);
    if (s_interface) audio_codec_delete_codec_if(s_interface);
    if (s_gpio) audio_codec_delete_gpio_if(s_gpio);
    if (s_data) audio_codec_delete_data_if(s_data);
    if (s_control) audio_codec_delete_ctrl_if(s_control);
    if (s_tx_enabled) i2s_channel_disable(s_tx);
    if (s_tx) i2s_del_channel(s_tx);
    s_tx = NULL;
    s_codec = NULL;
    s_control = NULL;
    s_data = NULL;
    s_gpio = NULL;
    s_interface = NULL;
    s_tx_enabled = false;
}

static esp_err_t configure_audio(void)
{
    i2s_chan_config_t channel = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    channel.auto_clear = true;
    channel.dma_desc_num = BSP_AUDIO_DMA_DESCRIPTORS;
    channel.dma_frame_num = BSP_AUDIO_DMA_FRAMES;
    esp_err_t err = i2s_new_channel(&channel, &s_tx, NULL);
    if (err != ESP_OK) return err;

    i2s_std_config_t config = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(BSP_AUDIO_SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = BSP_I2S_MCLK, .bclk = BSP_I2S_BCLK, .ws = BSP_I2S_LRCK,
            .dout = BSP_I2S_DOUT, .din = I2S_GPIO_UNUSED,
        },
    };
    config.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;
    err = i2s_channel_init_std_mode(s_tx, &config);
    if (err != ESP_OK) return err;
    err = i2s_channel_enable(s_tx);
    if (err != ESP_OK) return err;
    s_tx_enabled = true;

    audio_codec_i2c_cfg_t i2c = {
        .port = BSP_I2C_PORT_NUM,
        .addr = ES8311_CODEC_DEFAULT_ADDR,
        .bus_handle = bsp_i2c_bus(),
    };
    audio_codec_i2s_cfg_t i2s = {.port = I2S_NUM_0, .tx_handle = s_tx};
    s_control = audio_codec_new_i2c_ctrl(&i2c);
    s_data = audio_codec_new_i2s_data(&i2s);
    s_gpio = audio_codec_new_gpio();
    if (!s_control || !s_data || !s_gpio) return ESP_ERR_NO_MEM;

    es8311_codec_cfg_t codec = {
        .ctrl_if = s_control, .gpio_if = s_gpio,
        .codec_mode = ESP_CODEC_DEV_WORK_MODE_DAC,
        .use_mclk = true, .pa_pin = -1,
        .hw_gain = {.pa_voltage = 5.0f, .codec_dac_voltage = 3.3f},
        .mclk_div = 256,
    };
    s_interface = es8311_codec_new(&codec);
    if (!s_interface) return ESP_FAIL;
    esp_codec_dev_cfg_t output = {
        .dev_type = ESP_CODEC_DEV_TYPE_OUT, .codec_if = s_interface, .data_if = s_data,
    };
    s_codec = esp_codec_dev_new(&output);
    if (!s_codec) return ESP_ERR_NO_MEM;

    esp_codec_dev_sample_info_t format = {
        .bits_per_sample = 16, .channel = 2, .channel_mask = 0x03,
        .sample_rate = BSP_AUDIO_SAMPLE_RATE,
    };
    if (esp_codec_dev_open(s_codec, &format) != ESP_CODEC_DEV_OK ||
        esp_codec_dev_set_out_vol(s_codec, MUSIC_VOLUME) != ESP_CODEC_DEV_OK) {
        return ESP_FAIL;
    }
    return set_dac_muted(true);
}

esp_err_t bsp_audio_init(void)
{
    esp_err_t err = bsp_audio_speaker(false);
    if (err == ESP_OK) {
        err = configure_audio();
        if (err != ESP_OK) release_audio();
    }
    return err;
}

esp_err_t bsp_audio_speaker(bool on)
{
    if (!on) {
        esp_err_t err = set_amplifier(false);
        if (s_codec && set_dac_muted(true) != ESP_OK) {
            ESP_LOGW(TAG, "could not verify DAC mute");
        }
        return err;
    }
    if (!s_codec) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = set_dac_muted(false);
    if (err == ESP_OK) {
        err = set_amplifier(true);
    }
    if (err != ESP_OK) {
        set_amplifier(false);
        set_dac_muted(true);
    }
    return err;
}

esp_err_t bsp_audio_write(const int16_t *stereo, size_t frames)
{
    size_t bytes = frames * 2 * sizeof(*stereo);
    size_t written = 0;
    esp_err_t err = i2s_channel_write(s_tx, stereo, bytes, &written, 100);
    return err != ESP_OK ? err : (written == bytes ? ESP_OK : ESP_FAIL);
}

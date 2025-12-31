#include "sense_acs758.h"

#include "esp_log.h"
#include "esp_adc/adc_oneshot.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "ACS758";

// ВНИМАНИЕ: для ESP32-S3 GPIO4 обычно соответствует ADC1_CH3.
// Если у тебя будет другой SoC/маппинг, правится здесь.
#define ACS_ADC_UNIT     ADC_UNIT_1
#define ACS_ADC_CH       ADC_CHANNEL_3
#define ACS_ADC_ATTEN    ADC_ATTEN_DB_12
#define ACS_SAMPLES      8

static adc_oneshot_unit_handle_t s_unit = NULL;
static int32_t s_zero_raw = 0;
static uint16_t s_seq = 0;

static esp_err_t read_raw_avg(int32_t *out_raw)
{
    if (!s_unit || !out_raw) return ESP_ERR_INVALID_STATE;

    int64_t acc = 0;
    for (int i = 0; i < ACS_SAMPLES; i++) {
        int raw = 0;
        const esp_err_t err = adc_oneshot_read(s_unit, ACS_ADC_CH, &raw);
        if (err != ESP_OK) return err;
        acc += raw;
    }
    *out_raw = (int32_t)(acc / ACS_SAMPLES);
    return ESP_OK;
}

esp_err_t acs758_init(gpio_num_t gpio_adc)
{
    (void)gpio_adc;

    if (s_unit) return ESP_OK;

    adc_oneshot_unit_init_cfg_t ucfg = {
        .unit_id = ACS_ADC_UNIT,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    esp_err_t err = adc_oneshot_new_unit(&ucfg, &s_unit);
    if (err != ESP_OK) return err;

    adc_oneshot_chan_cfg_t ccfg = {
        .atten = ACS_ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    err = adc_oneshot_config_channel(s_unit, ACS_ADC_CH, &ccfg);
    if (err != ESP_OK) return err;

    // Небольшая пауза после старта питания/ADC
    vTaskDelay(pdMS_TO_TICKS(50));

    err = acs758_zero_calibrate();
    if (err != ESP_OK) return err;

    ESP_LOGI(TAG, "init ok: unit=%d ch=%d zero_raw=%ld",
             (int)ACS_ADC_UNIT, (int)ACS_ADC_CH, (long)s_zero_raw);

    return ESP_OK;
}

esp_err_t acs758_zero_calibrate(void)
{
    int32_t raw = 0;
    const esp_err_t err = read_raw_avg(&raw);
    if (err != ESP_OK) return err;

    s_zero_raw = raw;
    return ESP_OK;
}

esp_err_t acs758_get_raw4(j_cur_raw4_t *out)
{
    if (!out) return ESP_ERR_INVALID_ARG;

    int32_t raw = 0;
    esp_err_t err = read_raw_avg(&raw);
    if (err != ESP_OK) return err;

    // Пока отдаём "сырое" dv в кодах АЦП, но в поле dv_mV.
    // Чтобы не врать: это "delta raw". Пульт всё равно хотел сырые данные.
    // Когда добавим adc_cali и перевод в mV, просто заменим на реальные mV.
    int32_t dv = raw - s_zero_raw;
    if (dv > 32767) dv = 32767;
    if (dv < -32768) dv = -32768;

    out->seq = s_seq++;
    out->dv_mV = (int16_t)dv;
    return ESP_OK;
}

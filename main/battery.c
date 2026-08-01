#include "battery.h"

#include <stdbool.h>
#include "esp_log.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"

static const char *TAG = "battery";

/* Su ESP32-S3, GPIO1 corrisponde al canale 0 dell'unita' ADC1 (vedi
 * "ESP32-S3 Technical Reference Manual", capitolo ADC, tabella di
 * mappatura pin/canale). */
#define BATTERY_ADC_UNIT    ADC_UNIT_1
#define BATTERY_ADC_CHANNEL ADC_CHANNEL_0

/* Attenuazione massima disponibile: estende il fondo scala dell'ADC fino
 * a circa 3.3V in ingresso, necessario perche' anche dopo il partitore
 * (vedi battery.h) la tensione sul pin puo' avvicinarsi ai 2.1V con la
 * batteria carica. */
#define BATTERY_ADC_ATTEN ADC_ATTEN_DB_12

/* Rapporto del partitore resistivo VBAT->GPIO1 sullo schematico della
 * board (due resistenze da 100k(ohm) in serie, R7 e R24): la tensione sul
 * pin e' meta' di quella reale della batteria. Vedi il commento in
 * battery.h per come validare/correggere questo valore sull'hardware
 * reale. */
#define BATTERY_DIVIDER_RATIO 2.0f

/* Soglie tipiche di una singola cella Li-ion, usate per stimare la
 * percentuale di carica residua (vedi battery_read_percent()). */
#define BATTERY_VOLTAGE_EMPTY 3.3f
#define BATTERY_VOLTAGE_FULL  4.2f

static adc_oneshot_unit_handle_t s_adc_handle = NULL;
static adc_cali_handle_t s_cali_handle = NULL;
static bool s_calibrated = false;

/*
 * L'ESP32-S3 supporta due schemi di calibrazione ADC (curve fitting e line
 * fitting): proviamo prima il piu' preciso (curve fitting, usa dati di
 * calibrazione di fabbrica specifici per chip) e ripieghiamo sul secondo
 * se il primo non fosse disponibile. Entrambi gli schemi sono opzionali a
 * livello di build (dipendono da CONFIG_IDF_TARGET), da cui le macro
 * ADC_CALI_SCHEME_*_SUPPORTED generate automaticamente da ESP-IDF: il
 * codice fra #if resta quindi portabile anche su chip che non
 * supportassero uno dei due schemi.
 */
static bool adc_calibration_init(adc_cali_handle_t *out_handle) {
    adc_cali_handle_t handle = NULL;
    esp_err_t err = ESP_FAIL;

#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    adc_cali_curve_fitting_config_t curve_config = {
        .unit_id = BATTERY_ADC_UNIT,
        .chan = BATTERY_ADC_CHANNEL,
        .atten = BATTERY_ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    err = adc_cali_create_scheme_curve_fitting(&curve_config, &handle);
#endif

#if ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    if (err != ESP_OK) {
        adc_cali_line_fitting_config_t line_config = {
            .unit_id = BATTERY_ADC_UNIT,
            .atten = BATTERY_ADC_ATTEN,
            .bitwidth = ADC_BITWIDTH_DEFAULT,
        };
        err = adc_cali_create_scheme_line_fitting(&line_config, &handle);
    }
#endif

    *out_handle = handle;
    return err == ESP_OK;
}

void battery_init(void) {
    adc_oneshot_unit_init_cfg_t init_config = {
        .unit_id = BATTERY_ADC_UNIT,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config, &s_adc_handle));

    adc_oneshot_chan_cfg_t chan_config = {
        .atten = BATTERY_ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(s_adc_handle, BATTERY_ADC_CHANNEL, &chan_config));

    s_calibrated = adc_calibration_init(&s_cali_handle);
    if (!s_calibrated) {
        ESP_LOGW(TAG, "Calibrazione ADC non disponibile su questo chip: la tensione letta sara' meno precisa");
    }

    ESP_LOGI(TAG, "ADC batteria inizializzato (GPIO1, ADC1 canale %d)", BATTERY_ADC_CHANNEL);
}

float battery_read_voltage(void) {
    int raw = 0;
    ESP_ERROR_CHECK(adc_oneshot_read(s_adc_handle, BATTERY_ADC_CHANNEL, &raw));

    int millivolts;
    if (s_calibrated) {
        ESP_ERROR_CHECK(adc_cali_raw_to_voltage(s_cali_handle, raw, &millivolts));
    } else {
        // Ripiego senza calibrazione: stima lineare sul fondo scala
        // nominale dell'attenuazione scelta (~3300 mV) e sulla risoluzione
        // di default dell'ADC (12 bit su ESP32-S3, cioe' 0-4095).
        millivolts = (raw * 3300) / 4095;
    }

    return (millivolts / 1000.0f) * BATTERY_DIVIDER_RATIO;
}

uint8_t battery_read_percent(void) {
    float voltage = battery_read_voltage();

    if (voltage <= BATTERY_VOLTAGE_EMPTY) {
        return 0;
    }
    if (voltage >= BATTERY_VOLTAGE_FULL) {
        return 100;
    }
    float fraction = (voltage - BATTERY_VOLTAGE_EMPTY) / (BATTERY_VOLTAGE_FULL - BATTERY_VOLTAGE_EMPTY);
    return (uint8_t)(fraction * 100.0f);
}

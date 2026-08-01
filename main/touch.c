#include "touch.h"

#include "driver/gpio.h"
#include "esp_lcd_io_i2c.h"
#include "esp_lcd_touch.h"
#include "esp_lcd_touch_cst816s.h"
#include "esp_log.h"
#include "display.h"
#include "i2c_bus.h"

static const char *TAG = "touch";

/* Pin di reset del touch (vedi tabella in CLAUDE.md /
 * docs/datasheet/ESP32-S3-Touch-LCD-1.69-Pinout.webp). SCL/SDA non sono
 * piu' gestiti qui: il bus I2C, condiviso anche con RTC e IMU, e' di
 * competenza del modulo i2c_bus (vedi i2c_bus_get_handle()). */
#define PIN_TOUCH_RST 13

/* Handle del driver touch, salvato staticamente dentro il modulo: chi usa
 * touch_get_point() non ha bisogno di conoscere ne' passare questo handle,
 * lo stesso spirito di incapsulamento gia' usato in display.c. */
static esp_lcd_touch_handle_t s_touch_handle = NULL;

void touch_init(void) {
    /* --- 1. Bus I2C ---
     * Il bus e' condiviso con RTC e IMU: lo richiediamo al modulo i2c_bus,
     * che lo crea al primo utilizzo (qui, se touch_init() e' il primo
     * modulo driver inizializzato) o restituisce quello gia' esistente. */
    i2c_master_bus_handle_t bus_handle = i2c_bus_get_handle();

    /* --- 2. Panel IO: incapsula il bus I2C con l'indirizzo del controller
     * touch (0x15) e i dettagli del protocollo di trasferimento, seguendo
     * la configurazione raccomandata dal driver stesso. --- */
    esp_lcd_panel_io_i2c_config_t io_config = ESP_LCD_TOUCH_IO_I2C_CST816S_CONFIG();
    esp_lcd_panel_io_handle_t io_handle = NULL;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c(bus_handle, &io_config, &io_handle));

    /* --- 3. Driver del controller touch ---
     * x_max/y_max sono la risoluzione del display (0..239, 0..279): il
     * driver usa questi valori per scartare eventuali letture fuori range.
     * Non usiamo il pin INT (interrupt "dati pronti"): interroghiamo il
     * controller a polling da touch_get_point(), chiamata a intervalli
     * regolari dalla callback di lettura del lv_indev di LVGL, il che e'
     * gia' sufficientemente reattivo per un'interfaccia touch. */
    esp_lcd_touch_config_t touch_config = {
        .x_max = DISPLAY_WIDTH,
        .y_max = DISPLAY_HEIGHT,
        .rst_gpio_num = PIN_TOUCH_RST,
        .int_gpio_num = GPIO_NUM_NC,
        .levels = {
            .reset = 0,
        },
    };
    ESP_ERROR_CHECK(esp_lcd_touch_new_i2c_cst816s(io_handle, &touch_config, &s_touch_handle));

    ESP_LOGI(TAG, "Touch CST816T inizializzato (reset GPIO%d)", PIN_TOUCH_RST);
}

bool touch_get_point(uint16_t *x, uint16_t *y) {
    esp_lcd_touch_read_data(s_touch_handle);

    esp_lcd_touch_point_data_t point;
    uint8_t touch_count = 0;

    esp_err_t err = esp_lcd_touch_get_data(s_touch_handle, &point, &touch_count, 1);

    if (err == ESP_OK && touch_count > 0) {
        *x = point.x;
        *y = point.y;
        return true;
    }
    return false;
}

#include "touch.h"

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_lcd_io_i2c.h"
#include "esp_lcd_touch.h"
#include "esp_lcd_touch_cst816s.h"
#include "esp_log.h"
#include "display.h"

static const char *TAG = "touch";

/* Pin della board (vedi tabella in CLAUDE.md / docs/datasheet/ESP32-S3-Touch-LCD-1.69-Pinout.webp).
 * SCL/SDA sono lo stesso bus I2C che in futuro ospitera' anche RTC e IMU. */
#define PIN_I2C_SCL   10
#define PIN_I2C_SDA   11
#define PIN_TOUCH_RST 13

#define I2C_PORT_NUM I2C_NUM_0

/* Handle del driver touch, salvato staticamente dentro il modulo: chi usa
 * touch_get_point() non ha bisogno di conoscere ne' passare questo handle,
 * lo stesso spirito di incapsulamento gia' usato in display.c. */
static esp_lcd_touch_handle_t s_touch_handle = NULL;

void touch_init(void) {
    /* --- 1. Bus I2C ---
     * Usiamo il driver "nuovo" di ESP-IDF 5.x (driver/i2c_master.h): il bus
     * si crea una sola volta con i2c_new_master_bus() e resta disponibile
     * per aggiungere altri device (in futuro RTC e IMU) con i loro
     * indirizzi, senza dover reinizializzare i pin SCL/SDA. */
    i2c_master_bus_config_t bus_config = {
        .i2c_port = I2C_PORT_NUM,
        .sda_io_num = PIN_I2C_SDA,
        .scl_io_num = PIN_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    i2c_master_bus_handle_t bus_handle = NULL;
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_config, &bus_handle));

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

    ESP_LOGI(TAG, "Touch CST816T inizializzato (I2C SCL=%d SDA=%d, reset GPIO%d)", PIN_I2C_SCL, PIN_I2C_SDA,
             PIN_TOUCH_RST);
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

#include "i2c_bus.h"

#include "esp_log.h"

static const char *TAG = "i2c_bus";

/* Pin del bus I2C condiviso (vedi tabella in CLAUDE.md). */
#define PIN_I2C_SCL 10
#define PIN_I2C_SDA 11

#define I2C_PORT_NUM I2C_NUM_0

/* Handle statico del bus: NULL finche' nessuno lo ha ancora richiesto. */
static i2c_master_bus_handle_t s_bus_handle = NULL;

i2c_master_bus_handle_t i2c_bus_get_handle(void) {
    if (s_bus_handle != NULL) {
        /* Il bus esiste gia' (creato da una chiamata precedente, magari da
         * un altro modulo): lo restituiamo cosi' com'e', senza rifare la
         * configurazione hardware. */
        return s_bus_handle;
    }

    i2c_master_bus_config_t bus_config = {
        .i2c_port = I2C_PORT_NUM,
        .sda_io_num = PIN_I2C_SDA,
        .scl_io_num = PIN_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_config, &s_bus_handle));

    ESP_LOGI(TAG, "Bus I2C condiviso creato (SCL=GPIO%d, SDA=GPIO%d)", PIN_I2C_SCL, PIN_I2C_SDA);

    return s_bus_handle;
}

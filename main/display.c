#include "display.h"

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_lcd_io_spi.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"

static const char *TAG = "display";

/* Pin della board (vedi tabella in CLAUDE.md / docs/datasheet/ESP32-S3-Touch-LCD-1.69-Pinout.webp). */
#define PIN_LCD_DC   4
#define PIN_LCD_CS   5
#define PIN_LCD_CLK  6
#define PIN_LCD_DIN  7
#define PIN_LCD_RST  8
#define PIN_LCD_BL   15

/* Bus SPI dedicato al display. La board ha un secondo bus I2C condiviso
 * (touch/RTC/IMU) del tutto separato da questo: SPI e I2C sono periferiche
 * diverse, non c'e' contesa tra i due. */
#define LCD_SPI_HOST SPI2_HOST

/* Frequenza del clock SPI verso il pannello. 40MHz e' un valore comune e
 * sicuro per l'ST7789V2; si potrebbe salire fino a 80MHz in un secondo
 * momento se servisse un refresh piu' rapido, verificando la stabilita'
 * del segnale sui fili verso il display. */
#define LCD_PIXEL_CLOCK_HZ (40 * 1000 * 1000)

esp_lcd_panel_handle_t display_init(esp_lcd_panel_io_color_trans_done_cb_t on_color_trans_done, void *user_ctx) {
    /* --- 1. Configurazione del pin del backlight come uscita digitale ---
     * Lo configuriamo subito ma lo lasciamo spento (livello basso): verra'
     * acceso esplicitamente da display_backlight_set(true) solo dopo che
     * il resto dell'inizializzazione e' andato a buon fine. */
    gpio_config_t bl_config = {
        .pin_bit_mask = 1ULL << PIN_LCD_BL,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&bl_config));
    gpio_set_level(PIN_LCD_BL, 0);

    /* --- 2. Bus SPI ---
     * MISO non e' usato (il display non manda dati indietro in questa
     * configurazione), quindi si lascia a -1. max_transfer_sz limita la
     * dimensione massima di un singolo trasferimento DMA: lo dimensioniamo
     * su un quarto di frame, abbondante sia per i test con pattern grezzo
     * sia per i buffer parziali che userà LVGL (in genere 1/10 di frame). */
    spi_bus_config_t bus_config = {
        .sclk_io_num = PIN_LCD_CLK,
        .mosi_io_num = PIN_LCD_DIN,
        .miso_io_num = -1,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(uint16_t) / 4,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_SPI_HOST, &bus_config, SPI_DMA_CH_AUTO));

    /* --- 3. Panel IO: incapsula il bus SPI con i dettagli del protocollo
     * comando/dati (linea DC) usato dai controller LCD tipo ST7789. --- */
    esp_lcd_panel_io_handle_t io_handle = NULL;
    esp_lcd_panel_io_spi_config_t io_config = {
        .cs_gpio_num = PIN_LCD_CS,
        .dc_gpio_num = PIN_LCD_DC,
        .spi_mode = 0,
        .pclk_hz = LCD_PIXEL_CLOCK_HZ,
        .trans_queue_depth = 10,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .on_color_trans_done = on_color_trans_done,
        .user_ctx = user_ctx,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_SPI_HOST, &io_config, &io_handle));

    /* --- 4. Panel handle: il driver specifico per il controller ST7789V2,
     * che sopra il panel IO conosce la sequenza di inizializzazione, il
     * formato colore, e i comandi per gap/mirror/invert. --- */
    esp_lcd_panel_handle_t panel_handle = NULL;
    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = PIN_LCD_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        /* Il nostro buffer e' un array di uint16_t in RAM: su ESP32 (CPU
         * little-endian) ogni pixel vi e' memorizzato byte-basso-prima.
         * Bisogna dirlo esplicitamente al driver ST7789, che configura di
         * default il controller per aspettarsi byte-alto-prima ("big
         * endian"): senza questo campo i canali di colore arrivano
         * permutati (verificato sperimentalmente: rosso sul pannello
         * appariva blu, verde appariva rosso, blu appariva verde). */
        .data_endian = LCD_RGB_DATA_ENDIAN_LITTLE,
        .bits_per_pixel = 16,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(io_handle, &panel_config, &panel_handle));

    /* --- 5. Sequenza di avvio del pannello --- */
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));

    /* Molti pannelli ST7789 restituiscono i colori con polarita' invertita
     * rispetto a quanto ci si aspetterebbe dal formato RGB565: invert_color
     * corregge questo comportamento. Se durante il test con pattern grezzo
     * (fase 5 del piano) i colori dovessero risultare scambiati o "negativi",
     * e' il primo parametro da rivedere. */
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel_handle, true));

    /* Offset verticale di 20 pixel: il controller ha una GRAM interna da
     * 240x320, ma il vetro montato su questa board e' alto solo 280 pixel,
     * centrato con un margine di 20 righe in alto (valore confermato dai
     * parametri di init usati nella board da Waveshare). Senza questo gap
     * l'immagine apparirebbe traslata verso l'alto e "tagliata" in fondo. */
    ESP_ERROR_CHECK(esp_lcd_panel_set_gap(panel_handle, 0, 20));

    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true));

    ESP_LOGI(TAG, "Pannello ST7789V2 inizializzato (%dx%d)", DISPLAY_WIDTH, DISPLAY_HEIGHT);

    return panel_handle;
}

void display_backlight_set(bool on) {
    gpio_set_level(PIN_LCD_BL, on ? 1 : 0);
}

/**
 * 動作確認スケッチ: 自己診断 + LVGL Hello World
 * - ESP32_Display_Panel (JingCai ESP32-4848S040C_I_Y_3 ボード定義) で ST7701 + GT911 を初期化
 * - シリアルモニタ (COM5, 115200) にスペックを出力
 *
 * 公式例 examples/arduino/gui/lvgl_v8/simple_port をベースに改変
 */
#include <Arduino.h>
#include <esp_display_panel.hpp>
#include <lvgl.h>
#include "lvgl_v8_port.h"
#include <esp_mac.h>

using namespace esp_panel::drivers;
using namespace esp_panel::board;

static void printSpecs()
{
    uint8_t mac[6] = {0};
    char macStr[18] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    Serial.println("---------- device specs ----------");
    Serial.printf("Chip model      : %s (rev %d)\r\n", ESP.getChipModel(), ESP.getChipRevision());
    Serial.printf("CPU freq        : %d MHz\r\n", ESP.getCpuFreqMHz());
    Serial.printf("Flash size      : %d MB\r\n", ESP.getFlashChipSize() / 1024 / 1024);
    Serial.printf("Flash speed     : %d MHz\r\n", ESP.getFlashChipSpeed() / 1000000);
    Serial.printf("Free app space  : %d MB\r\n", ESP.getFreeSketchSpace() / 1024 / 1024);
    Serial.printf("PSRAM size      : %d MB\r\n", ESP.getPsramSize() / 1024 / 1024);
    Serial.printf("Free PSRAM      : %d KB\r\n", ESP.getFreePsram() / 1024);
    Serial.printf("Free heap       : %d KB\r\n", ESP.getFreeHeap() / 1024);
    Serial.printf("MAC             : %s\r\n", macStr);
    Serial.println("----------------------------------");
}

void setup()
{
    Serial.begin(115200);

    Serial.println("Initializing board (JingCai ESP32-4848S040C_I_Y_3)");
    Board *board = new Board();
    board->init();
#if LVGL_PORT_AVOID_TEARING_MODE
    auto lcd = board->getLCD();
    // When avoid tearing function is enabled, the frame buffer number should be set in the board driver
    lcd->configFrameBufferNumber(LVGL_PORT_DISP_BUFFER_NUM);
#if ESP_PANEL_DRIVERS_BUS_ENABLE_RGB && CONFIG_IDF_TARGET_ESP32S3
    auto lcd_bus = lcd->getBus();
    /**
     * As the anti-tearing feature typically consumes more PSRAM bandwidth, for the ESP32-S3, we need to utilize the
     * "bounce buffer" functionality to enhance the RGB data bandwidth.
     * This feature will consume `bounce_buffer_size * bytes_per_pixel * 2` of SRAM memory.
     */
    if (lcd_bus->getBasicAttributes().type == ESP_PANEL_BUS_TYPE_RGB) {
        static_cast<BusRGB *>(lcd_bus)->configRGB_BounceBufferSize(lcd->getFrameWidth() * 10);
    }
#endif
#endif
    assert(board->begin());

    printSpecs();

    Serial.println("Initializing LVGL");
    lvgl_port_init(board->getLCD(), board->getTouch());

    Serial.println("Creating UI");
    /* Lock the mutex due to the LVGL APIs are not thread-safe */
    lvgl_port_lock(-1);

    lv_obj_t *label_1 = lv_label_create(lv_scr_act());
    lv_label_set_text(label_1, "Hello World!");
    lv_obj_set_style_text_font(label_1, &lv_font_montserrat_30, 0);
    lv_obj_align(label_1, LV_ALIGN_CENTER, 0, -40);

    lv_obj_t *label_2 = lv_label_create(lv_scr_act());
    lv_label_set_text_fmt(
        label_2, "ESP32_Display_Panel (%d.%d.%d)",
        ESP_PANEL_VERSION_MAJOR, ESP_PANEL_VERSION_MINOR, ESP_PANEL_VERSION_PATCH
    );
    lv_obj_set_style_text_font(label_2, &lv_font_montserrat_16, 0);
    lv_obj_align_to(label_2, label_1, LV_ALIGN_OUT_BOTTOM_MID, 0, 10);

    lv_obj_t *label_3 = lv_label_create(lv_scr_act());
    lv_label_set_text_fmt(label_3, "LVGL (%d.%d.%d)", LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR, LVGL_VERSION_PATCH);
    lv_obj_set_style_text_font(label_3, &lv_font_montserrat_16, 0);
    lv_obj_align_to(label_3, label_2, LV_ALIGN_OUT_BOTTOM_MID, 0, 10);

    /* Release the mutex */
    lvgl_port_unlock();

    Serial.println("Setup done");
}

void loop()
{
    Serial.println("IDLE loop");
    delay(1000);
}

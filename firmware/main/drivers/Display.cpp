#include "Display.hpp"

#include "bsp/display.h"
#include "bsp/esp-bsp.h"
#include "bsp/touch.h"
#include "esp_check.h"
#include "esp_lcd_ek79007.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_panel_ops.h"
#include "esp_ldo_regulator.h"
#include "esp_lv_adapter.h"

static const char* TAG = "Display";
static constexpr uint32_t LVGL_TASK_STACK_BYTES = 16 * 1024;

/**
 * Creates the MIPI-DSI bus and the EK79007 panel, resets and initialises it: the BSP's bsp_display_new_with_handles()
 * without its backlight set-up.
 *
 * The board lights the backlight at power-up. The BSP sets up its PWM (off) and turns it on again, which shows as a
 * flicker (bench 2026-10-11); leaving it alone keeps it on throughout.
 *
 * @param panel Receives the panel
 * @param io Receives the command IO
 * @return ESP_OK, or the error of the step that failed
 */
static esp_err_t createPanel(esp_lcd_panel_handle_t& panel, esp_lcd_panel_io_handle_t& io) {
    // Power the DSI PHY from LDO 3
    static esp_ldo_channel_handle_t phyPowerChannel = nullptr;
    esp_ldo_channel_config_t ldoConfig = {};
    ldoConfig.chan_id = BSP_MIPI_DSI_PHY_PWR_LDO_CHAN;
    ldoConfig.voltage_mv = BSP_MIPI_DSI_PHY_PWR_LDO_VOLTAGE_MV;
    ESP_RETURN_ON_ERROR(esp_ldo_acquire_channel(&ldoConfig, &phyPowerChannel), TAG, "DSI PHY power");

    // DSI bus and the command channel
    esp_lcd_dsi_bus_handle_t bus = nullptr;
    esp_lcd_dsi_bus_config_t busConfig = {};
    busConfig.bus_id = 0;
    busConfig.num_data_lanes = BSP_LCD_MIPI_DSI_LANE_NUM;
    busConfig.lane_bit_rate_mbps = BSP_LCD_MIPI_DSI_LANE_BITRATE_MBPS;
    ESP_RETURN_ON_ERROR(esp_lcd_new_dsi_bus(&busConfig, &bus), TAG, "DSI bus");

    esp_lcd_dbi_io_config_t ioConfig = {};
    ioConfig.virtual_channel = 0;
    ioConfig.lcd_cmd_bits = 8;
    ioConfig.lcd_param_bits = 8;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_dbi(bus, &ioConfig, &io), TAG, "Panel IO");

    // Panel: RGB565, the BSP's frame buffer count, the driver's default init commands
    esp_lcd_dpi_panel_config_t dpiConfig = EK79007_1024_600_PANEL_60HZ_CONFIG(LCD_COLOR_PIXEL_FORMAT_RGB565);
    dpiConfig.num_fbs = CONFIG_BSP_LCD_DPI_BUFFER_NUMS;

    ek79007_vendor_config_t vendorConfig = {};
    vendorConfig.mipi_config.dsi_bus = bus;
    vendorConfig.mipi_config.dpi_config = &dpiConfig;

    esp_lcd_panel_dev_config_t panelConfig = {};
    panelConfig.bits_per_pixel = 16;
    panelConfig.rgb_ele_order = BSP_LCD_COLOR_SPACE;
    panelConfig.reset_gpio_num = BSP_LCD_RST;
    panelConfig.vendor_config = &vendorConfig;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_ek79007(io, &panelConfig, &panel), TAG, "Panel");

    // The reset is needed: without it the panel stays in reset and LVGL never gets a frame out (bench 2026-10-11)
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(panel), TAG, "Panel reset");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(panel), TAG, "Panel init");

    return ESP_OK;
}

lv_display_t* Display::start() {
    // LVGL through Espressif's adapter, as the BSP's bsp_display_start_with_config() does, minus its backlight set-up
    esp_lv_adapter_config_t adapterConfig = ESP_LV_ADAPTER_DEFAULT_CONFIG();

    // LVGL draws nested objects recursively; the adapter's 8 KB overflowed on the Parameters list, about 10 levels deep
    // (stack protection fault in lv_obj_draw, 2026-10-12)
    adapterConfig.task_stack_size = LVGL_TASK_STACK_BYTES;

    if (esp_lv_adapter_init(&adapterConfig) != ESP_OK) {
        return nullptr;
    }

    esp_lcd_panel_handle_t panel = nullptr;
    esp_lcd_panel_io_handle_t io = nullptr;

    if (createPanel(panel, io) != ESP_OK) {
        return nullptr;
    }

    // Rotation is Waveshare's; the BSP's buffer settings
    esp_lv_adapter_display_config_t displayConfig = {};
    displayConfig.panel = panel;
    displayConfig.panel_io = io;
    displayConfig.profile.interface = ESP_LV_ADAPTER_PANEL_IF_MIPI_DSI;
    displayConfig.profile.rotation = ESP_LV_ADAPTER_ROTATE_180;
    displayConfig.profile.hor_res = BSP_LCD_H_RES;
    displayConfig.profile.ver_res = BSP_LCD_V_RES;
    displayConfig.profile.buffer_height = 50;
    displayConfig.profile.use_psram = false;
    displayConfig.profile.enable_ppa_accel = false;
    displayConfig.profile.require_double_buffer = false;
    displayConfig.tear_avoid_mode = ESP_LV_ADAPTER_TEAR_AVOID_MODE_TRIPLE_PARTIAL;
    lv_display_t* display = esp_lv_adapter_register_display(&displayConfig);

    if (display == nullptr) {
        return nullptr;
    }

    // Touch: no mirroring here, because the adapter already rotates touch with the display (tested 2026-10-08)
    esp_lcd_touch_handle_t touch = nullptr;
    bsp_touch_config_t touchConfig = {};

    if (bsp_touch_new(&touchConfig, &touch) != ESP_OK) {
        return nullptr;
    }

    esp_lv_adapter_touch_config_t touchAdapterConfig = ESP_LV_ADAPTER_TOUCH_DEFAULT_CONFIG(display, touch);

    if (esp_lv_adapter_register_touch(&touchAdapterConfig) == nullptr || esp_lv_adapter_start() != ESP_OK) {
        return nullptr;
    }

    return display;
}

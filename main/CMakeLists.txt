# ESP-IDF Main Component CMakeLists.txt
# ESP32-S3 无线 1602A LCD 显示屏项目

idf_component_register(
    SRCS
        # 主程序
        "main.cpp"

        # 硬件驱动层
        "hardware/lcd_driver.cpp"
        "hardware/button.cpp"
        "hardware/buzzer.cpp"
        "hardware/rgb_led.cpp"
        "hardware/fuel_gauge.cpp"
        "hardware/opt3001.cpp"

        # 应用层
        "applications/clock.cpp"
        "applications/weather.cpp"
        "applications/pomodoro.cpp"
        "applications/badappleplayer.cpp"
        "applications/setting.cpp"
        "applications/about.cpp"

        # 网络连接层
        "connectivity/wifi_config.cpp"
        "connectivity/network.cpp"
        "connectivity/jwt_auth.cpp"
        "connectivity/wifi_esp32.cpp"
        "connectivity/http_server_wrapper.cpp"

        # 服务层
        "services/protocol.cpp"
        "services/playbuffer.cpp"
        "services/config_manager.cpp"
        "services/wifi_config_manager.cpp"
        "services/qweather_auth_config_manager.cpp"
        "services/ota_manager.cpp"
        "services/time_manager.cpp"
        "services/sleep_manager.cpp"
        "services/auto_brightness.cpp"
        "services/kanamap.cpp"
        "services/web_setting.cpp"

        # 菜单系统
        "menu/menu.cpp"
        "menu/menu_item.cpp"
        "menu/menu_navigator.cpp"
        "menu/status_bar_renderer.cpp"

        # UI 组件
        "ui/animetion.cpp"
        "ui/icons.cpp"
        "ui/hold_progress.cpp"

        # 工具类
        "utils/logger.cpp"
        "utils/memory_utils.cpp"

    INCLUDE_DIRS
        # 当前目录（main）
        "."

        # 子目录
        "hardware"
        "applications"
        "connectivity"
        "services"
        "menu"
        "ui"
        "utils"

        # 头文件目录
        "../include"
        "../include/hardware"
        "../include/applications"
        "../include/connectivity"
        "../include/services"
        "../include/menu"
        "../include/ui"
        "../include/utils"

    REQUIRES
        # ESP-IDF 核心组件
        driver
        esp_wifi
        esp_http_client
        esp_https_ota
        esp_http_server
        esp_timer
        esp_event
        esp_netif
        esp_ota
        nvs_flash
        spiffs
        json
        mbedtls

        # FreeRTOS
        freertos

        # 网络协议
        lwip

    PRIV_REQUIRES
        # 可选依赖
        esp_psram
)

# 抑制 ESP-IDF LEDC 宏的 implicit-fallthrough 警告（来自 esp-idf 内部宏）
target_compile_options(${COMPONENT_LIB} PUBLIC -Wno-implicit-fallthrough)

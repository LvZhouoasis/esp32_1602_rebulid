# ESP-IDF Main Component CMakeLists.txt
# ESP32-S3 无线 1602A LCD 显示屏项目

idf_component_register(
    SRCS
        # 主程序
        "../src/main.cpp"

        # 硬件驱动层
        "../src/hardware/lcd_driver.cpp"
        "../src/hardware/button.cpp"
        "../src/hardware/buzzer.cpp"
        "../src/hardware/rgb_led.cpp"
        "../src/hardware/fuel_gauge.cpp"
        "../src/hardware/opt3001.cpp"

        # 应用层
        "../src/applications/clock.cpp"
        "../src/applications/weather.cpp"
        "../src/applications/pomodoro.cpp"
        "../src/applications/badappleplayer.cpp"
        "../src/applications/setting.cpp"
        "../src/applications/about.cpp"

        # 网络连接层
        "../src/connectivity/wifi_config.cpp"
        "../src/connectivity/network.cpp"
        "../src/connectivity/jwt_auth.cpp"

        # 服务层
        "../src/services/protocol.cpp"
        "../src/services/playbuffer.cpp"
        "../src/services/config_manager.cpp"
        "../src/services/wifi_config_manager.cpp"
        "../src/services/qweather_auth_config_manager.cpp"
        "../src/services/ota_manager.cpp"
        "../src/services/time_manager.cpp"
        "../src/services/sleep_manager.cpp"
        "../src/services/auto_brightness.cpp"
        "../src/services/kanamap.cpp"
        "../src/services/web_setting.cpp"

        # 菜单系统
        "../src/menu/menu.cpp"
        "../src/menu/menu_item.cpp"
        "../src/menu/menu_navigator.cpp"
        "../src/menu/status_bar_renderer.cpp"

        # UI 组件
        "../src/ui/animetion.cpp"
        "../src/ui/icons.cpp"
        "../src/ui/hold_progress.cpp"

        # 工具类
        "../src/utils/logger.cpp"
        "../src/utils/memory_utils.cpp"

    INCLUDE_DIRS
        # 源代码目录
        "../src"
        "../src/hardware"
        "../src/applications"
        "../src/connectivity"
        "../src/services"
        "../src/menu"
        "../src/ui"
        "../src/utils"

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
        esp_timer
        esp_event
        esp_netif
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

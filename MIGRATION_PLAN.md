# ESP32 1602A 项目分析与 ESP-IDF 迁移计划

> 文档版本：1.0
> 创建日期：2026-08-15
> 当前分支：1.1.0（基于 1.0.1，已完成方案B：millis/delay 替换）

---

## 一、项目代码全面分析

### 1.1 项目概览

基于 **ESP32-S3** 的无线 1602A LCD 显示屏固件，通过 TCP 协议接收数据帧并在 1602A LCD 上高速播放（最高约 83 FPS）。内置 WiFi 配网、OTA 升级、天气显示、番茄钟等功能。

### 1.2 硬件架构

| 组件 | 型号/规格 | 通信方式 | 驱动文件 |
|------|----------|---------|---------|
| 主控 | ESP32-S3-N8R8（16MB Flash + 8MB OPI PSRAM） | - | - |
| LCD | 1602A（4-bit 并行接口） | GPIO 直接控制 | `lcd_driver.cpp` |
| RGB LED | WS2812（单颗） | FastLED 库 | `rgb_led.cpp` |
| 蜂鸣器 | 有源/无源 | GPIO PWM（LEDC） | `buzzer.cpp` |
| 电量计 | BQ27421 | I2C（Wire 库） | `fuel_gauge.cpp` |
| 光传感器 | OPT3001 | I2C（Wire 库） | `opt3001.cpp` |
| 按键 | 5个（上/下/左/右/中） | GPIO 输入 | `button.cpp` |

### 1.3 GPIO 引脚分配

```
LCD:    RS=1, E=2, D4=3, D5=4, D6=5, D7=6, BLA=7, CTL=18
按键:   LEFT=9, CENTER=10, RIGHT=11, SIDE/POWER=12
RGB:    WS2812=8
蜂鸣器: BUZZER=13
I2C:    SDA=38, SCL=21
其他:   FUEL_WARN=17, DC_PLUG=14
```

### 1.4 软件架构

```
second/
├── src/                          # 源代码
│   ├── main.cpp                  # 主程序入口 + 功耗管理
│   ├── hardware/                 # 硬件驱动层（6个文件）
│   │   ├── lcd_driver.cpp        # LCD 驱动（差分刷新核心）
│   │   ├── button.cpp            # 按键扫描（FreeRTOS 双任务）
│   │   ├── buzzer.cpp            # 蜂鸣器（队列式非阻塞播放）
│   │   ├── rgb_led.cpp           # WS2812 RGB LED
│   │   ├── fuel_gauge.cpp        # BQ27421 电量计
│   │   └── opt3001.cpp           # OPT3001 光传感器
│   ├── applications/             # 应用层（6个文件）
│   │   ├── clock.cpp             # 时钟（NTP 同步）
│   │   ├── weather.cpp           # 天气（和风天气 API）
│   │   ├── pomodoro.cpp          # 番茄钟
│   │   ├── badappleplayer.cpp    # Bad Apple 播放器
│   │   ├── setting.cpp           # 设置界面
│   │   └── about.cpp             # 关于信息
│   ├── connectivity/             # 网络连接层（3个文件）
│   │   ├── wifi_config.cpp       # WiFi 配网（AP + Web）
│   │   ├── network.cpp           # TCP 服务器 + 帧缓存
│   │   └── jwt_auth.cpp          # JWT 认证（Ed25519）
│   ├── services/                 # 服务层（11个文件）
│   │   ├── protocol.cpp          # 自定义协议解析
│   │   ├── playbuffer.cpp        # 帧播放缓冲
│   │   ├── config_manager.cpp    # 配置管理器基类
│   │   ├── wifi_config_manager.cpp
│   │   ├── qweather_auth_config_manager.cpp
│   │   ├── ota_manager.cpp       # OTA 升级
│   │   ├── time_manager.cpp      # NTP 时间同步
│   │   ├── sleep_manager.cpp     # 深度睡眠管理
│   │   ├── auto_brightness.cpp   # 自动亮度
│   │   ├── kanamap.cpp           # 假名映射（已禁用）
│   │   └── web_setting.cpp       # Web 设置服务器
│   ├── menu/                     # 菜单系统（4个文件）
│   │   ├── menu.cpp              # 菜单核心逻辑
│   │   ├── menu_item.cpp         # 菜单项定义
│   │   ├── menu_navigator.cpp    # 菜单导航
│   │   └── status_bar_renderer.cpp # 状态栏渲染
│   ├── ui/                       # UI 组件（3个文件）
│   │   ├── animetion.cpp         # 动画系统
│   │   ├── icons.cpp             # 图标定义
│   │   └── hold_progress.cpp     # 长按进度条
│   └── utils/                    # 工具类（2个文件）
│       ├── logger.cpp            # 多模块日志系统
│       └── memory_utils.cpp      # 内存工具
├── include/                      # 头文件（与 src 对应）
├── test/                         # 单元测试（11个测试文件）
├── data/                         # SPIFFS 数据（badapple.bin）
├── script/                       # 构建脚本
├── boards/                       # 自定义板级配置
└── partitions.csv                # Flash 分区表
```

### 1.5 核心功能模块详解

#### 1.5.1 LCD 驱动（lcd_driver.cpp）

- **接口**：4-bit 并行接口（RS, E, D4-D7）
- **刷新算法**：差分刷新（lcdRenderDiff），仅更新变化的字符和 CGRAM
- **性能**：最高约 83 FPS（1602A 硬件极限）
- **自定义字符**：8 个 CGRAM 槽位，自动循环分配
- **PWM 控制**：背光亮度（LEDC 通道 0）、对比度（LEDC 通道 2）

#### 1.5.2 网络通信（network.cpp + protocol.cpp）

- **TCP 服务器**：端口 13000，接收数据帧
- **协议格式**：`[AA 55] [LEN] [帧间隔H] [帧间隔L] [数据体...]`
- **帧缓存**：双端队列（std::deque），最大 16 帧
- **心跳检测**：15 秒超时断开
- **功耗管理**：动态 CPU 频率、WiFi 省电模式

#### 1.5.3 WiFi 配网（wifi_config.cpp）

- **AP 模式**：SSID "1602A_Config"
- **强制门户**：DNS 劫持，自动弹出配网页面
- **Web 服务器**：端口 80，提供配网页面
- **状态机**：IDLE → CONNECTING → CONNECTED/FAILED

#### 1.5.4 Web 设置（web_setting.cpp）

- **端口**：80（与配网服务器复用）
- **功能**：
  - 设备信息查看
  - 亮度调节
  - 自动亮度开关
  - 音效开关
  - 和风天气配置
  - 城市搜索
  - OTA 升级（URL / 文件上传）

#### 1.5.5 电源管理（main.cpp + sleep_manager.cpp）

- **功耗模式**：
  - Performance：240MHz，全功率
  - ConnectedBalanced：80MHz，推流省电
  - Standby：80MHz，light sleep
- **深度睡眠**：GPIO 唤醒，RTC 时间保持
- **PM 锁**：NO_LIGHT_SLEEP、CPU_FREQ_MAX

#### 1.5.6 菜单系统（menu/）

- **结构**：主菜单 → 设置/WiFi配置/关于
- **导航**：上下键移动，中键确认，侧键返回
- **状态栏**：电池/WiFi/时间/动画
- **应用接口**：统一的 enterAppInterface/exitAppInterface

### 1.6 第三方库依赖

| 库名 | 用途 | Arduino 依赖程度 |
|------|------|-----------------|
| FastLED | WS2812 RGB LED 控制 | 低（支持 ESP-IDF） |
| ArduinoJson | JSON 解析/生成 | 低（支持 ESP-IDF） |
| libsodium | Ed25519 签名（JWT） | 无（纯 C 库） |
| zlib_turbo | Gzip 解压 | 无（纯 C 库） |

### 1.7 Arduino API 使用统计

| API 类别 | 使用次数 | 涉及文件数 | 说明 |
|---------|---------|-----------|------|
| String 类 | 286 处 | 24 个文件 | 最大改动量 |
| SPIFFS | 143 处 | 18 个文件 | 文件系统操作 |
| WiFi.* | 61 处 | 11 个文件 | WiFi 连接管理 |
| WebServer | 6 个文件 | - | HTTP 服务器 |
| HTTPClient | 5 个文件 | - | HTTP 客户端 |
| Wire.* | 5 个文件 | - | I2C 通信 |
| digitalRead/Write | 5 个文件 | - | GPIO 操作 |
| Serial | 15 个文件 | - | 串口输出 |
| ESP.* | 7 个文件 | - | 系统信息 |
| Update.* | 3 个文件 | - | OTA 升级 |
| constrain | 4 个文件 | - | 数值限制 |
| PROGMEM | 7 个文件 | - | Flash 存储 |
| IPAddress | 3 个文件 | - | IP 地址 |

---

## 二、迁移历史记录

### 2.1 方案B：替换 millis/delay（已完成）

- **分支**：1.0.1
- **内容**：将 142 处 Arduino `millis()` 和 `delay()` 替换为 ESP-IDF 等效方法
- **替换宏**：
  - `GET_MS()` → `esp_timer_get_time() / 1000`
  - `WAIT_MS(ms)` → `vTaskDelay(pdMS_TO_TICKS(ms))`
- **涉及文件**：22 个
- **状态**：✅ 已完成

### 2.2 日语假名功能删除（已完成）

- **分支**：1.0.0
- **内容**：删除所有日语片假名显示功能
- **涉及文件**：kanamap.cpp、lcd_driver.cpp、protocol.cpp、badappleplayer.cpp
- **状态**：✅ 已完成

---

## 三、方案C：完全迁移到 ESP-IDF 框架

### 3.1 迁移目标

| 指标 | 当前（Arduino+ESP-IDF） | 目标（纯 ESP-IDF） |
|------|----------------------|------------------|
| 固件大小 | ~1.5 MB | ~0.8-1.0 MB |
| RAM 使用 | ~80 KB | ~60-70 KB |
| 启动时间 | ~2-3 秒 | ~1-2 秒 |
| 编译速度 | 较慢 | 较快 |
| 调试能力 | 有限 | 完整 JTAG 支持 |

### 3.2 迁移策略

采用 **8 个阶段**，每阶段完成后可编译测试，确保功能不回退。

---

### 阶段 1：构建系统迁移（CMakeLists.txt）

**目标**：创建 ESP-IDF 构建系统，保持 Arduino-as-component 暂时可用

**预计工时**：2-3 天

**步骤**：

1. 创建项目根目录 `CMakeLists.txt`：
```cmake
cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(esp32_1602)
```

2. 创建 `main/CMakeLists.txt`：
```cmake
idf_component_register(
    SRCS 
        "main.cpp"
        "hardware/lcd_driver.cpp"
        "hardware/button.cpp"
        "hardware/buzzer.cpp"
        "hardware/rgb_led.cpp"
        "hardware/fuel_gauge.cpp"
        "hardware/opt3001.cpp"
        "applications/clock.cpp"
        "applications/weather.cpp"
        "applications/pomodoro.cpp"
        "applications/badappleplayer.cpp"
        "applications/setting.cpp"
        "applications/about.cpp"
        "connectivity/wifi_config.cpp"
        "connectivity/network.cpp"
        "connectivity/jwt_auth.cpp"
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
        "menu/menu.cpp"
        "menu/menu_item.cpp"
        "menu/menu_navigator.cpp"
        "menu/status_bar_renderer.cpp"
        "ui/animetion.cpp"
        "ui/icons.cpp"
        "ui/hold_progress.cpp"
        "utils/logger.cpp"
        "utils/memory_utils.cpp"
    INCLUDE_DIRS 
        "."
        "hardware"
        "applications"
        "connectivity"
        "services"
        "menu"
        "ui"
        "utils"
    REQUIRES
        driver
        esp_wifi
        esp_http_client
        esp_https_ota
        esp_timer
        nvs_flash
        spiffs
        json
        libsodium
)
```

3. 创建 `sdkconfig.defaults`：
```
CONFIG_ESP32S3_DEFAULT_CPU_FREQ_240=y
CONFIG_SPIRAM=y
CONFIG_SPIRAM_MODE_OCT=y
CONFIG_SPIRAM_SPEED_80M=y
CONFIG_ESP_TIMER_TASK_STACK_SIZE=4096
```

**验证**：`idf.py build` 能找到所有文件

---

### 阶段 2：核心头文件重构（mydefine.h + logger.h）

**目标**：移除 `#include <Arduino.h>`，替换为 ESP-IDF 原生头文件

**预计工时**：1 天

**步骤**：

#### 2.1 mydefine.h
```cpp
// 移除
#include <Arduino.h>

// 替换为
#include <stdint.h>
#include <stdbool.h>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include "driver/gpio.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
```

#### 2.2 logger.h
```cpp
// 移除
#include <Arduino.h>

// 替换为
#include <stdint.h>
#include <stdbool.h>
#include <cstdarg>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"
```

#### 2.3 logger.cpp
```cpp
// 移除 Serial.begin() / Serial.println()
// 替换为 ESP_LOGI / ESP_LOGW / ESP_LOGE

// 移除
Serial.begin(115200);
while (!Serial && GET_MS() < 3000) { ... }
Serial.println();

// 替换为
esp_log_level_set("*", ESP_LOG_INFO);
ESP_LOGI("LOGGER", "=== Logger System Initialized ===");
```

**验证**：logger 模块独立编译通过

---

### 阶段 3：String 类替换（最大改动）

**目标**：将所有 Arduino `String` 替换为 C 风格字符串或自定义轻量封装

**预计工时**：5-7 天

**涉及文件**：24 个文件，286 处

**替换模式**：

```cpp
// 模式1：简单字符串拼接
// Arduino
String json = "{";
json += "\"brightness\":" + String(brightness) + ",";
json += "\"autoBrightness\":" + String(isAutoBrightnessActive() ? "true" : "false");
json += "}";

// ESP-IDF (使用 snprintf)
char json[256];
snprintf(json, sizeof(json), 
    "{\"brightness\":%d,\"autoBrightness\":%s}",
    brightness, 
    isAutoBrightnessActive() ? "true" : "false");

// 模式2：字符串比较
// Arduino
if (ssid == "") { ... }

// ESP-IDF
if (strlen(ssid) == 0) { ... }

// 模式3：字符串方法
// Arduino
ssid.trim();
int idx = s.indexOf("lo");
String sub = s.substring(0, 5);
s.toUpperCase();

// ESP-IDF
// trim: 自己写循环去除首尾空格
// indexOf: 使用 strstr()
// substring: 使用 strncpy()
// toUpperCase: 使用 toupper() 循环
```

**优先级**：
1. 先处理 `mydefine.h`（已无 String）
2. 处理 `logger.h/cpp`（已无 String）
3. 处理 `config_manager.h/cpp`（基础服务）
4. 处理 `wifi_config_manager.h/cpp`
5. 处理 `qweather_auth_config_manager.h/cpp`
6. 处理其他文件

---

### 阶段 4：GPIO 和硬件抽象层

**目标**：替换 Arduino GPIO API

**预计工时**：1-2 天

**涉及文件**：
- `src/hardware/button.cpp`（digitalRead）
- `src/hardware/buzzer.cpp`（ledcAttachPin, ledcWrite, ledcWriteTone）
- `src/hardware/lcd_driver.cpp`（已部分使用 ESP-IDF）
- `src/connectivity/wifi_config.cpp`（digitalRead）

**替换**：

```cpp
// Arduino
pinMode(17, OUTPUT);
digitalWrite(17, HIGH);
int val = digitalRead(9);

// ESP-IDF
gpio_set_direction(GPIO_NUM_17, GPIO_MODE_OUTPUT);
gpio_set_level(GPIO_NUM_17, 1);
int val = gpio_get_level(GPIO_NUM_9);
```

**buzzer.cpp 特殊处理**：
```cpp
// Arduino LEDC
ledcAttachPin(BUZZER_PIN, channel);
ledcWriteTone(channel, frequency);
ledcWrite(channel, duty);

// ESP-IDF LEDC (v5.x API)
ledc_timer_config_t timer_conf = {
    .speed_mode = LEDC_LOW_SPEED_MODE,
    .duty_resolution = LEDC_TIMER_8_BIT,
    .timer_num = LEDC_TIMER_0,
    .freq_hz = frequency,
    .clk_cfg = LEDC_AUTO_CLK
};
ledc_timer_config(&timer_conf);

ledc_channel_config_t channel_conf = {
    .gpio_num = BUZZER_PIN,
    .speed_mode = LEDC_LOW_SPEED_MODE,
    .channel = LEDC_CHANNEL_0,
    .timer_sel = LEDC_TIMER_0,
    .duty = duty,
    .hpoint = 0
};
ledc_channel_config(&channel_conf);
```

---

### 阶段 5：I2C 通信迁移

**目标**：替换 Arduino Wire 库为 ESP-IDF I2C 驱动

**预计工时**：2-3 天

**涉及文件**：
- `src/hardware/fuel_gauge.cpp`（~30 处 Wire 调用）
- `src/hardware/opt3001.cpp`（~20 处 Wire 调用）

**替换**：

```cpp
// Arduino Wire
Wire.beginTransmission(ADDR);
Wire.write(reg);
Wire.endTransmission(false);
Wire.requestFrom(ADDR, (uint8_t)2);
uint8_t msb = Wire.read();
uint8_t lsb = Wire.read();

// ESP-IDF I2C
i2c_cmd_handle_t cmd = i2c_cmd_link_create();
i2c_master_start(cmd);
i2c_master_write_byte(cmd, (ADDR << 1) | I2C_MASTER_WRITE, true);
i2c_master_write_byte(cmd, reg, true);
i2c_master_start(cmd);
i2c_master_write_byte(cmd, (ADDR << 1) | I2C_MASTER_READ, true);
i2c_master_read_byte(cmd, &msb, I2C_MASTER_ACK);
i2c_master_read_byte(cmd, &lsb, I2C_MASTER_NACK);
i2c_master_stop(cmd);
i2c_master_cmd_begin(I2C_NUM_0, cmd, pdMS_TO_TICKS(100));
i2c_cmd_link_delete(cmd);
```

**封装建议**：创建 `I2CDevice` 类封装常用操作

---

### 阶段 6：SPIFFS 文件系统迁移

**目标**：替换 Arduino SPIFFS 库为 ESP-IDF VFS

**预计工时**：2-3 天

**涉及文件**：
- `src/services/config_manager.cpp`（~42 处 SPIFFS 调用）
- `src/applications/badappleplayer.cpp`（File 操作）

**替换**：

```cpp
// Arduino SPIFFS
SPIFFS.begin(false);
File file = SPIFFS.open("/config.json", "r");
String content = file.readString();
file.close();
SPIFFS.exists("/config.json");
SPIFFS.remove("/config.json");
SPIFFS.totalBytes();
SPIFFS.usedBytes();

// ESP-IDF VFS
esp_vfs_spiffs_conf_t spiffs_conf = {
    .base_path = "/spiffs",
    .partition_label = NULL,
    .max_files = 10,
    .format_if_mount_failed = true
};
esp_vfs_spiffs_register(&spiffs_conf);

FILE* f = fopen("/spiffs/config.json", "r");
char buf[512];
size_t bytesRead = fread(buf, 1, sizeof(buf) - 1, f);
buf[bytesRead] = '\0';
fclose(f);

// 检查文件存在
struct stat st;
bool exists = (stat("/spiffs/config.json", &st) == 0);

// 删除文件
unlink("/spiffs/config.json");

// 获取存储信息
size_t total = 0, used = 0;
esp_spiffs_info(NULL, &total, &used);
```

---

### 阶段 7：WiFi 和网络迁移

**目标**：替换 Arduino WiFi/WebServer/HTTPClient 库

**预计工时**：5-7 天

**涉及文件**：
- `src/connectivity/wifi_config.cpp`（WiFi、WebServer、DNSServer）
- `src/connectivity/network.cpp`（WiFiServer、WiFiClient）
- `src/services/web_setting.cpp`（WebServer、HTTPClient）
- `src/services/ota_manager.cpp`（HTTPClient、Update）
- `src/applications/weather.cpp`（HTTPClient）
- `src/services/time_manager.cpp`（WiFi）

#### 7.1 WiFi 连接
```cpp
// Arduino
WiFi.mode(WIFI_STA);
WiFi.begin(ssid, password);
WiFi.status();
WiFi.localIP();
WiFi.disconnect();

// ESP-IDF
wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
esp_wifi_init(&cfg);
esp_wifi_set_mode(WIFI_MODE_STA);
wifi_config_t wifi_config = { ... };
esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
esp_wifi_start();
esp_wifi_connect();

// 获取 IP
esp_netif_ip_info_t ip_info;
esp_netif_get_ip_info(esp_netif_get_handle_from_ifkey("WIFI_STA_DEF"), &ip_info);
```

#### 7.2 WebServer
```cpp
// Arduino WebServer
WebServer server(80);
server.on("/api/info", HTTP_GET, handleInfo);
server.begin();
server.handleClient();

// ESP-IDF httpd
httpd_config_t config = HTTPD_DEFAULT_CONFIG();
httpd_handle_t server = NULL;
httpd_start(&server, &config);

httpd_uri_t uri_info = {
    .uri = "/api/info",
    .method = HTTP_GET,
    .handler = handleInfo,
    .user_ctx = NULL
};
httpd_register_uri_handler(server, &uri_info);
```

#### 7.3 HTTPClient
```cpp
// Arduino HTTPClient
HTTPClient http;
http.begin(url);
http.addHeader("Authorization", "Bearer " + token);
int code = http.GET();
WiFiClient* stream = http.getStreamPtr();

// ESP-IDF esp_http_client
esp_http_client_config_t config = {
    .url = url,
};
esp_http_client_handle_t client = esp_http_client_open(&client, HTTP_METHOD_GET);
esp_http_client_set_header(client, "Authorization", bearer);
esp_http_client_fetch_headers(client);
int code = esp_http_client_get_status_code(client);
```

#### 7.4 OTA
```cpp
// Arduino Update
Update.begin(UPDATE_SIZE_UNKNOWN);
Update.write(buf, len);
Update.end(true);

// ESP-IDF esp_ota
const esp_partition_t* update_partition = esp_ota_get_next_update_partition(NULL);
esp_ota_handle_t ota_handle;
esp_ota_begin(update_partition, OTA_SIZE_UNKNOWN, &ota_handle);
esp_ota_write(ota_handle, buf, len);
esp_ota_end(ota_handle);
esp_ota_set_boot_partition(update_partition);
```

---

### 阶段 8：第三方库适配和清理

**目标**：适配剩余第三方库，清理所有 Arduino 依赖

**预计工时**：2-3 天

#### 8.1 FastLED
FastLED 支持 ESP-IDF，无需大改。确认 `platformio.ini` 中 `lib_deps` 保留。

#### 8.2 ArduinoJson
ArduinoJson 支持 ESP-IDF，无需大改。确认 `lib_deps` 保留。

#### 8.3 libsodium
libsodium 是纯 C 库，无需修改。

#### 8.4 zlib_turbo
zlib_turbo 是纯 C 库，无需修改。

#### 8.5 PROGMEM
```cpp
// Arduino
const char data[] PROGMEM = "...";

// ESP-IDF
// ESP32-S3 统一地址空间，PROGMEM 不需要
// 直接使用 const char data[] = "...";
```

#### 8.6 ESP.restart()
```cpp
// Arduino
ESP.restart();

// ESP-IDF
esp_restart();
```

#### 8.7 ESP.getFreeHeap()
```cpp
// Arduino
ESP.getFreeHeap();
ESP.getHeapSize();
ESP.getFreePsram();
ESP.getPsramSize();

// ESP-IDF
esp_get_free_heap_size();
esp_get_minimum_free_heap_size();
heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
```

#### 8.8 setCpuFrequencyMhz()
```cpp
// Arduino
setCpuFrequencyMhz(240);
getCpuFrequencyMhz();

// ESP-IDF
esp_pm_config_esp32s3_t pm_config = {
    .max_freq_mhz = 240,
    .min_freq_mhz = 80,
    .light_sleep_enable = true
};
esp_pm_configure(&pm_config);
```

---

## 四、迁移执行计划

### 4.1 建议执行顺序

```
阶段1 → 阶段2 → 阶段4 → 阶段5 → 阶段6 → 阶段3 → 阶段7 → 阶段8
```

**理由**：
- 阶段1-2 是基础，必须先做
- 阶段4-6 是独立模块，改动量小，建立信心
- 阶段3（String）改动量最大，放后面集中处理
- 阶段7（WiFi/网络）最复杂，放最后
- 阶段8 是收尾清理

### 4.2 工时估算

| 阶段 | 内容 | 工时 | 难度 | 状态 |
|------|------|------|------|------|
| 1 | 构建系统迁移 | 2-3 天 | ★★☆ | ⏳ 待执行 |
| 2 | 核心头文件重构 | 1 天 | ★☆☆ | ⏳ 待执行 |
| 3 | String 类替换 | 5-7 天 | ★★★ | ⏳ 待执行 |
| 4 | GPIO 和硬件抽象层 | 1-2 天 | ★★☆ | ⏳ 待执行 |
| 5 | I2C 通信迁移 | 2-3 天 | ★★☆ | ⏳ 待执行 |
| 6 | SPIFFS 文件系统迁移 | 2-3 天 | ★★☆ | ⏳ 待执行 |
| 7 | WiFi 和网络迁移 | 5-7 天 | ★★★ | ⏳ 待执行 |
| 8 | 第三方库适配和清理 | 2-3 天 | ★★☆ | ⏳ 待执行 |
| **总计** | - | **20-30 天** | - | - |

### 4.3 风险和注意事项

#### 高风险区域
1. **String 替换**（阶段3）：改动量最大，容易引入 bug
2. **WiFi/WebServer**（阶段7）：API 差异大，需要重写逻辑
3. **OTA 升级**（阶段7）：涉及固件安全性

#### 缓解措施
1. 每阶段完成后提交 Git
2. 每阶段编译测试
3. 关键模块写单元测试
4. 保留 Arduino-as-component 作为回退方案

### 4.4 验证方案

#### 每阶段验证
1. `idf.py build` 编译通过
2. `idf.py flash` 烧录成功
3. 串口日志输出正常
4. 基本功能测试

#### 最终验证
1. 全功能回归测试
2. 性能对比（固件大小、启动时间、内存占用）
3. 稳定性测试（连续运行 24 小时）
4. OTA 升级测试

---

## 五、Git 分支管理

### 5.1 当前分支结构

```
main
├── 1.0.0（删除日语假名功能）
│   └── 1.0.1（方案B：millis/delay 替换）
│       └── 1.1.0（当前分支，待执行方案C）
```

### 5.2 建议分支策略

- **1.1.0**：方案C 开发分支
- 每个阶段完成后合并到 **1.1.0**
- 方案C 完成后合并到 **main**

---

## 六、附录

### 6.1 通信协议格式

| 字节位置 | 字段名称 | 长度 | 说明 |
|---------|---------|------|------|
| 0-1 | 协议头 | 2 字节 | 固定值：`0xAA 0x55` |
| 2 | 数据长度 | 1 字节 | 数据体长度（不含头部） |
| 3-4 | 帧间隔 | 2 字节 | 帧率间隔（毫秒），高字节在前 |
| 5-N | 数据体 | 可变 | 显示内容数据 |

### 6.2 Flash 分区表

```
# Name,   Type, SubType, Offset,    Size
nvs,      data, nvs,     0x9000,    0x5000
otadata,  data, ota,     0xe000,    0x2000
ota_0,    app,  ota_0,   0x10000,   0x200000
ota_1,    app,  ota_1,   0x210000,  0x200000
spiffs,   data, spiffs,  0x410000,  0x100000
coredump, data, coredump,0x510000,  0x10000
```

### 6.3 相关文档

- `README.md`：项目说明和编译指南
- `PROJECT.md`：项目详细文档
- `MIGRATION_PLAN.md`：本文档

---

> **文档维护者**：Claude
> **最后更新**：2026-08-15

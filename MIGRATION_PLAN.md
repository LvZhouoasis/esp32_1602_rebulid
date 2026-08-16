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

### ✅ 阶段 1 完成记录

**完成日期**：2026-08-15

**已创建文件**：
1. `CMakeLists.txt` - 项目根目录 CMake 配置
2. `main/CMakeLists.txt` - 主组件配置，使用本地路径
3. `sdkconfig.defaults` - ESP-IDF 默认配置（PSRAM、Flash、WiFi、电源管理等）

**项目结构调整**：
- 采用**纯 ESP-IDF 标准结构**
- 将 `src/` 目录下的所有源文件移动到 `main/` 目录
- 删除原 `src/` 目录
- 源代码现在位于 `main/` 目录，符合 ESP-IDF 规范

**目录结构**：
```
second/
├── CMakeLists.txt              # 根目录配置
├── main/                       # 主组件目录（源代码在这里）
│   ├── CMakeLists.txt          # 主组件配置
│   ├── main.cpp
│   ├── hardware/
│   ├── applications/
│   ├── connectivity/
│   ├── services/
│   ├── menu/
│   ├── ui/
│   └── utils/
├── include/                    # 公共头文件
├── sdkconfig.defaults          # ESP-IDF 默认配置
└── partitions.csv              # 分区表
```

**待验证**：
- [ ] 安装 ESP-IDF 工具链后执行 `idf.py build` 验证编译

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

### ✅ 阶段 2 完成记录

**完成日期**：2026-08-15

**已修改文件**：
1. `include/mydefine.h` - 移除 `#include <Arduino.h>`，添加 ESP-IDF 原生头文件
2. `include/utils/logger.h` - 移除 `#include <Arduino.h>`，添加 ESP-IDF 原生头文件
3. `main/utils/logger.cpp` - 移除 Serial，替换为 ESP_LOGI/printf

**主要修改**：
- **mydefine.h**：添加 `stdint.h`, `stdbool.h`, `cstring`, `cstdio`, `cstdlib`, `driver/gpio.h`
- **logger.h**：添加 `stdint.h`, `stdbool.h`, `cstdarg`, `cstring`, `cstdio`, `esp_log.h`
- **logger.cpp**：
  - 移除 `Serial.begin(115200)` 和所有 Serial 调用
  - 替换为 `ESP_LOGI()` 和 `printf()`
  - 替换 `ESP.getFreeHeap()` 等为 ESP-IDF 的 `esp_get_free_heap_size()`, `heap_caps_get_total_size()`
  - 注释掉 String 类型的 log 函数（待阶段3处理）

**待处理**：
- [ ] logger.h 中的 `String` 类型函数声明（第101行）- 待阶段3处理
- [ ] 验证所有文件编译通过

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

### ✅ 阶段 3：String 类替换（最大改动）

**完成日期**：2026-08-15

**已处理文件**（27个，全部完成）：
1. ✅ `include/utils/logger.h` - 移除 String 类型函数声明
2. ✅ `main/utils/logger.cpp` - 移除 String 类型函数实现
3. ✅ `include/services/kanamap.h` - String 数组改为 const char*
4. ✅ `main/services/kanamap.cpp` - String 数组改为 const char*
5. ✅ `include/hardware/lcd_driver.h` - 函数参数改为 const char*
6. ✅ `main/hardware/lcd_driver.cpp` - 函数参数改为 const char*
7. ✅ `include/ui/icons.h` - 函数参数改为 const char*
8. ✅ `main/ui/icons.cpp` - 函数参数改为 const char*，strcmp 替换 ==
9. ✅ `main/applications/about.cpp` - String 拼接改为 snprintf
10. ✅ `main/applications/setting.cpp` - String 拼接改为 snprintf
11. ✅ `main/hardware/button.cpp` - String 拼接改为 snprintf
12. ✅ `main/menu/menu.cpp` - String 改为 const char*
13. ✅ `main/main.cpp` - String 拼接改为 snprintf
14. ✅ `include/services/config_manager.h` - 函数参数改为 const char*
15. ✅ `main/services/config_manager.cpp` - readFile/writeFile 重写为 char 缓冲区
16. ✅ `include/services/wifi_config_manager.h` - 成员改为 char 数组
17. ✅ `main/services/wifi_config_manager.cpp` - 使用 strlcpy/char 缓冲区
18. ✅ `include/services/qweather_auth_config_manager.h` - 成员改为 char 数组
19. ✅ `main/services/qweather_auth_config_manager.cpp` - 使用 strlcpy/char 缓冲区
20. ✅ `include/connectivity/jwt_auth.h` - generate_jwt 改为输出缓冲区模式
21. ✅ `main/connectivity/jwt_auth.cpp` - 完全重写为 C 风格字符串
22. ✅ `include/services/ota_manager.h` - 函数参数改为 const char*
23. ✅ `main/services/ota_manager.cpp` - 使用 snprintf/char 缓冲区
24. ✅ `main/connectivity/wifi_config.cpp` - 使用 snprintf/char 缓冲区
25. ✅ `main/applications/weather.cpp` - 全局变量改为 char 数组，JSON 解析改为 const char*
26. ✅ `main/services/web_setting.cpp` - 99处全部改为 snprintf/char 缓冲区

**替换模式总结**：
- `String` → `char[]` + `strlcpy()`/`snprintf()`
- `String.concat()` → `snprintf()`
- `String.indexOf()` → `strstr()`/`strchr()`
- `String.substring()` → `strncpy()` + 手动偏移
- `String.trim()` → 自定义 lambda 去除首尾空格
- `String.c_str()` → 直接使用 `const char*`
- `generate_jwt()` → 输出缓冲区模式，返回 `size_t`

---

### ✅ 阶段 4：GPIO 和硬件抽象层

**完成日期**：2026-08-15

**实现方式**：在 `include/mydefine.h` 中添加内联辅助函数，封装 ESP-IDF GPIO/LEDC API，保持 Arduino 兼容接口

**新增辅助函数**：
1. `digitalRead(int pin)` → 封装 `gpio_get_level()`
2. `digitalWrite(int pin, int level)` → 封装 `gpio_set_level()`
3. `ledcSetup(channel, freq, resolution)` → 封装 `ledc_timer_config()`
4. `ledcAttachPin(pin, channel)` → 封装 `ledc_channel_config()`
5. `ledcWrite(channel, duty)` → 封装 `ledc_set_duty()` + `ledc_update_duty()`
6. `ledcWriteTone(channel, freq)` → 封装 `ledc_timer_config()` + `ledcWrite()`
7. `ledcDetachPin(pin)` → 封装 `gpio_set_direction(DISABLE)`
8. `map(x, in_min, in_max, out_min, out_max)` → 数值范围映射
9. `constrain(x, a, b)` → 数值范围限制

**涉及文件**：
- `include/mydefine.h` - 新增9个辅助函数
- `main/hardware/button.cpp` - 使用 `digitalRead`
- `main/hardware/buzzer.cpp` - 使用 LEDC 函数
- `main/hardware/lcd_driver.cpp` - 使用 LEDC 函数
- `main/connectivity/wifi_config.cpp` - 使用 `digitalRead`
- `main/applications/setting.cpp` - 使用 `digitalRead`
- `main/applications/pomodoro.cpp` - 使用 `digitalRead`

**说明**：
- 通过内联辅助函数封装，保持 Arduino API 兼容性
- 底层实现使用 ESP-IDF 原生 API
- 无需修改各硬件驱动文件的调用代码

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

### ✅ 阶段 5：I2C 通信迁移

**完成日期**：2026-08-15

**实现方式**：在 `include/mydefine.h` 中添加 `I2CDevice` 辅助类，封装 ESP-IDF I2C 驱动

**新增辅助函数/类**：
1. `i2cInit(port, sda_pin, scl_pin, freq)` - 初始化 I2C 总线
2. `I2CDevice` 类 - 封装 I2C 设备操作
   - `isPresent()` - 检测设备是否存在
   - `readRegister(reg, data, len)` - 读取寄存器
   - `writeRegister(reg, data, len)` - 写入寄存器
   - `readRegister16BE(reg)` - 读取16位寄存器（大端序）
   - `readRegister16LE(reg)` - 读取16位寄存器（小端序）
   - `writeRegister16BE(reg, value)` - 写入16位寄存器（大端序）
   - `writeRegister16LE(reg, value)` - 写入16位寄存器（小端序）
   - `readByte(reg)` - 读取单字节
   - `writeByte(reg, value)` - 写入单字节

**涉及文件**：
- `include/mydefine.h` - 新增 `i2cInit()` 函数和 `I2CDevice` 类
- `main/hardware/opt3001.cpp` - 使用 `I2CDevice` 替换 Arduino Wire
- `main/hardware/fuel_gauge.cpp` - 使用 `I2CDevice` 替换 Arduino Wire

**说明**：
- 通过 `I2CDevice` 类封装，保持代码简洁
- 底层实现使用 ESP-IDF 原生 I2C 驱动
- 支持大小端序读写，适配不同设备

---

### ✅ 阶段 6：SPIFFS 文件系统迁移

**完成日期**：2026-08-15

**目标**：替换 Arduino SPIFFS 库为 ESP-IDF VFS

**涉及文件**：
- `include/mydefine.h` - 新增 `spiffsInit()` 和 `spiffsInfo()` 辅助函数
- `main/services/config_manager.cpp` - 使用 ESP-IDF VFS POSIX API
- `include/services/config_manager.h` - 移除 `#include <SPIFFS.h>`
- `main/applications/badappleplayer.cpp` - 使用 ESP-IDF VFS POSIX API
- `include/applications/badappleplayer.h` - 移除 `#include <SPIFFS.h>`，改用 `<cstdio>`
- `main/applications/setting.cpp` - 使用 `unlink()` 替换 `SPIFFS.remove()`
- `include/services/web_pages.h` - 移除 `PROGMEM` 属性（ESP32-S3 统一地址空间不需要）
- `include/applications/bad_apple_melody.h` - 移除 `PROGMEM` 属性

**主要修改**：

1. **mydefine.h** - 新增辅助函数：
   - `spiffsInit(basePath, max_files)` - 封装 `esp_vfs_spiffs_register()`
   - `spiffsInfo(total, used)` - 封装 `esp_spiffs_info()`

2. **config_manager.cpp** - 重写文件操作：
   - `initSPIFFS()` - 使用 `spiffsInit()` 初始化
   - `readFile()` - 使用 `stat()` + `fopen()` + `fread()` + `fclose()`
   - `writeFile()` - 使用 `stat()` + `unlink()` + `fopen()` + `fwrite()` + `fclose()`
   - `listDir()` - 使用 `opendir()` + `readdir()` + `closedir()`
   - `saveAutoBrightnessEnabled()` / `loadAutoBrightnessEnabled()` - 更新路径格式
   - `saveSoundEffectsEnabled()` / `loadSoundEffectsEnabled()` - 更新路径格式

3. **badappleplayer.cpp** - 重写文件操作：
   - `File file` → `FILE* file`
   - `SPIFFS.exists()` → `stat()`
   - `SPIFFS.open()` → `fopen()`
   - `file.size()` → `stat.st_size`
   - `file.read()` → `fread()`
   - `file.seek()` → `fseek()`
   - `file.available()` → `ftell()` 计算剩余
   - `file.close()` → `fclose()`

4. **setting.cpp** - 更新文件操作：
   - `SPIFFS.remove("/wifi.txt")` → `unlink("/spiffs/wifi.txt")`
   - `ESP.restart()` → `esp_restart()`

5. **PROGMEM 移除**：
   - ESP32-S3 统一地址空间，PROGMEM 不需要
   - 移除 `#include <pgmspace.h>`
   - 移除所有 `PROGMEM` 属性

6. **路径格式更新**：
   - Arduino SPIFFS: `/config.json`
   - ESP-IDF VFS: `/spiffs/config.json`
   - 所有文件路径添加 `/spiffs` 前缀

**替换模式总结**：
- `SPIFFS.begin()` → `spiffsInit()` / `esp_vfs_spiffs_register()`
- `SPIFFS.open(path, "r")` → `fopen("/spiffs" + path, "r")`
- `File file` → `FILE* file`
- `file.read()` → `fread()`
- `file.write()` → `fwrite()`
- `file.close()` → `fclose()`
- `file.size()` → `stat.st_size`
- `file.seek()` → `fseek()`
- `SPIFFS.exists()` → `stat() == 0`
- `SPIFFS.remove()` → `unlink()`
- `SPIFFS.totalBytes()` / `SPIFFS.usedBytes()` → `spiffsInfo()` / `esp_spiffs_info()`
- `PROGMEM` → 直接使用 const 数组（ESP32-S3 不需要）
- `ESP.restart()` → `esp_restart()`

**额外清理工作（阶段6附加）**：

在完成SPIFFS迁移的同时，还清理了以下Arduino依赖：

1. **移除 `#include <Arduino.h>`**：
   - `include/hardware/lcd_driver.h` - 已有 `mydefine.h` 覆盖
   - `include/ui/icons.h` - 改用 `<cstdint>`
   - `include/services/kanamap.h` - 直接移除
   - `include/services/sleep_manager.h` - 已有 `mydefine.h` 覆盖
   - `include/ui/animations.h` - 改用 `<cstdint>`
   - `include/ui/hold_progress.h` - 改用 `<cstdint>`
   - `include/services/auto_brightness.h` - 改用 `<cstdint>`
   - `include/hardware/buzzer.h` - 已有 `mydefine.h` 覆盖

2. **移除 `#include <SPIFFS.h>`**：
   - `include/services/config_manager.h`
   - `include/applications/badappleplayer.h`

3. **移除 `#include <pgmspace.h>`**：
   - `include/services/web_pages.h`

4. **替换 `ESP.restart()`**：
   - `main/main.cpp` - `ESP.restart()` → `esp_restart()`
   - `main/services/web_setting.cpp` - `ESP.restart()` → `esp_restart()`
   - `main/services/ota_manager.cpp` - `ESP.restart()` → `esp_restart()`
   - `main/applications/setting.cpp` - `ESP.restart()` → `esp_restart()`

5. **替换 `ESP.getFreeHeap()` 等**：
   - `main/utils/memory_utils.cpp` - 使用 `esp_get_free_heap_size()`, `heap_caps_get_largest_free_block()`, `esp_get_minimum_free_heap_size()`
   - `include/utils/memory_utils.h` - 移除 `#include <Arduino.h>`，添加 ESP-IDF 头文件

---

### 阶段 7：WiFi 和网络迁移（进行中）

**目标**：替换 Arduino WiFi/WebServer/HTTPClient/WiFiServer/WiFiClient 库为 ESP-IDF 原生 API

**预计工时**：9-14 天（最复杂阶段）

**迁移范围**：113 处 Arduino WiFi 相关 API 调用，分布在 18 个文件中

#### 需要替换的 Arduino 库

| Arduino 库 | ESP-IDF 替代 | 使用文件数 |
|-----------|------------|-----------|
| `WiFi.h` | `esp_wifi` + `esp_netif` | 6 个 |
| `WebServer.h` | `esp_http_server` | 2 个 |
| `DNSServer.h` | 自定义实现 | 1 个 |
| `HTTPClient.h` | `esp_http_client` | 3 个 |
| `WiFiServer.h` | BSD Socket API | 2 个 |
| `WiFiClient.h` | BSD Socket API | 3 个 |
| `WiFiClientSecure.h` | `esp_http_client` (HTTPS) | 1 个 |
| `Update.h` | `esp_ota` | 2 个 |

#### 涉及文件清单

**核心网络层（必须重写）：**
1. `include/connectivity/wifi_config.h` - WebServer, DNSServer
2. `main/connectivity/wifi_config.cpp` - WiFi 连接、AP 模式、DNS、WebServer 配网
3. `include/connectivity/network.h` - WiFiServer, WiFiClient
4. `main/connectivity/network.cpp` - TCP 服务器、客户端管理

**服务层（需要适配）：**
5. `main/services/web_setting.cpp` - WebServer 路由、HTTPClient
6. `include/services/ota_manager.h` - HTTPClient, WiFiClientSecure, Update
7. `main/services/ota_manager.cpp` - OTA 下载、写入
8. `main/services/time_manager.cpp` - WiFi.status(), WiFi.localIP()

**应用层（需要适配）：**
9. `main/applications/weather.cpp` - HTTPClient, WiFi.status()
10. `main/applications/setting.cpp` - WiFi.status(), WiFi.localIP()
11. `main/menu/menu.cpp` - WiFi.status(), WiFi.localIP()

**系统层（需要适配）：**
12. `main/main.cpp` - WiFi.setSleep(), WiFi.status(), WiFi.localIP(), WiFi.setTxPower()
13. `main/services/sleep_manager.cpp` - WiFi.status(), WiFi.disconnect(), WiFi.mode()

**头文件依赖：**
14. `include/hardware/button.h` - extern WiFiClient client

---

#### ✅ 阶段 7.1：WiFi 基础连接封装（已完成）

**完成日期**：2026-08-15

**新建文件**：
- `include/connectivity/wifi_esp32.h` - WiFi 封装类声明
- `main/connectivity/wifi_esp32.cpp` - ESP-IDF WiFi 实现

**封装的 API（兼容 Arduino WiFi）**：
```cpp
WiFi.init()              // 初始化 WiFi 子系统
WiFi.begin(ssid, pwd)    // 连接到 WiFi 网络
WiFi.softAP(ssid)        // 启动软 AP 模式
WiFi.status()            // 获取连接状态（WL_CONNECTED 等）
WiFi.localIP()           // 获取本地 IP 地址
WiFi.softAPIP()          // 获取软 AP 的 IP 地址
WiFi.SSID()              // 获取已连接的 SSID
WiFi.RSSI()              // 获取信号强度
WiFi.macAddress()        // 获取 MAC 地址
WiFi.scanNetworks()      // 扫描可用网络
WiFi.SSID(index)         // 获取扫描结果的 SSID
WiFi.RSSI(index)         // 获取扫描结果的 RSSI
WiFi.encryptionType(index) // 获取扫描结果的加密类型
WiFi.setSleep(enable)    // 设置睡眠模式
WiFi.setTxPower(dbm)     // 设置发射功率
WiFi.disconnect(wifiOff) // 断开连接
WiFi.setMode(mode)       // 设置 WiFi 模式
```

**ESP-IDF 实现要点**：
- 使用 `esp_wifi_init()` 初始化
- 使用 `esp_wifi_set_mode()` 设置模式
- 使用 `esp_wifi_set_config()` 配置 SSID/密码
- 使用 `esp_wifi_connect()` 连接
- 使用 `esp_netif_get_ip_info()` 获取 IP
- 使用 `esp_wifi_scan_start()` 扫描
- 使用 `esp_event_handler_instance_register()` 注册事件处理

**更新文件**：
- `main/CMakeLists.txt` - 添加 wifi_esp32.cpp、esp_http_server、esp_ota 依赖
- `include/connectivity/wifi_config.h` - 使用 wifi_esp32.h 替代 Arduino WiFi

---

#### ✅ 阶段 7.2：HTTP 服务器封装（已完成）

**完成日期**：2026-08-15

**新建文件**：
- `include/connectivity/http_server_wrapper.h` - HttpServer 类声明
- `main/connectivity/http_server_wrapper.cpp` - ESP-IDF httpd 实现

**封装的 API（兼容 Arduino WebServer）**：
```cpp
HttpServer(port)                    // 构造函数
on(uri, handler)                    // 注册路由
on(uri, method, handler)            // 注册指定方法路由
onNotFound(handler)                 // 注册 404 处理
begin()                             // 启动服务器
handleClient()                      // 处理请求（兼容 Arduino）
send(code, type, content)           // 发送响应
sendHeader(name, value)             // 发送响应头
setContentLength(len)               // 设置内容长度
sendContent_P(content, len)         // 发送内容块
sendContent(content)                // 发送响应内容
arg(name)                           // 获取查询参数
argPlain()                          // 获取 POST 请求体
hasArg(name)                        // 检查参数是否存在
method()                            // 获取请求方法
uri()                               // 获取请求 URI
header(name)                        // 获取请求头
hasHeader(name)                     // 检查请求头是否存在
```

**ESP-IDF 实现要点**：
- 使用 `httpd_start()` 启动服务器
- 使用 `httpd_register_uri_handler()` 注册 URI 处理
- 使用 `httpd_resp_send()` 发送响应
- 使用 `httpd_resp_send_chunk()` 发送分块响应
- 使用 `httpd_req_get_url_query_str()` 获取查询参数
- 使用 `httpd_req_recv()` 读取请求体

**更新文件**：
- `include/connectivity/wifi_config.h` - 使用 HttpServer 替代 WebServer
- `main/connectivity/wifi_config.cpp` - 使用 HttpServer apServer(80)
- `main/services/web_setting.cpp` - 使用 HttpServer settingServer(80)
- `include/connectivity/wifi_esp32.h` - 添加 WIFI_STA/WIFI_AP/WIFI_OFF 兼容常量
- `main/CMakeLists.txt` - 添加 http_server_wrapper.cpp

**验证**：编译通过，0 个错误

---

#### 阶段 7.3：DNS 服务器实现（待执行）

**目标**：替换 DNSServer.h 的调用，实现强制门户功能

**实现方案**：
- 使用 ESP-IDF 的 DNS 服务器实现，或移植一个轻量级 DNS 服务器
- 主要用于强制门户（captive portal）功能

**需要修改的文件**：
- `main/connectivity/wifi_config.cpp` - 替换 dnsServer 的所有调用

**预计工时**：1 天

---

#### 阶段 7.4：TCP 服务器和客户端封装（待执行）

**目标**：替换 WiFiServer.h 和 WiFiClient.h 的所有调用

**封装类设计**：
```cpp
// include/connectivity/tcp_server.h
class TcpServer {
public:
    TcpServer(int port);
    void begin();
    void end();
    TcpClient accept();
};

class TcpClient {
public:
    TcpClient();
    bool connected();
    int available();
    int read(uint8_t* buffer, size_t length);
    void stop();
    void setNoDelay(bool enable);
    IPAddress remoteIP();
    int remotePort();
    operator bool();
};
```

**ESP-IDF 实现要点**：
- 使用 BSD Socket API（`socket()`, `bind()`, `listen()`, `accept()`）
- 使用 `setsockopt()` 设置 TCP_NODELAY
- 使用 `getpeername()` 获取远程 IP/端口

**需要修改的文件**：
- `include/connectivity/network.h` - 替换 WiFiServer, WiFiClient 声明
- `main/connectivity/network.cpp` - 替换 server.accept(), client 的所有调用
- `include/hardware/button.h` - 替换 extern WiFiClient client
- `main/services/ota_manager.cpp` - 替换 WiFiClient 使用

**预计工时**：2-3 天

---

#### 阶段 7.5：HTTP 客户端封装（待执行）

**目标**：替换 HTTPClient.h 的所有调用

**封装类设计**：
```cpp
// include/connectivity/http_client_wrapper.h
class HttpClientWrapper {
public:
    HttpClientWrapper();
    ~HttpClientWrapper();
    bool begin(const char* url);
    bool begin(TcpClient& client, const char* url);
    void end();
    void addHeader(const char* name, const char* value);
    int GET();
    int getSize();
    TcpClient* getStreamPtr();
    String getString();
    bool connected();
};
```

**ESP-IDF 实现要点**：
- 使用 `esp_http_client_init()` 初始化
- 使用 `esp_http_client_perform()` 执行请求
- 使用 `esp_http_client_read()` 读取响应
- 使用 `esp_http_client_set_header()` 设置头

**需要修改的文件**：
- `main/services/web_setting.cpp` - 替换 HTTPClient 用于城市搜索
- `main/applications/weather.cpp` - 替换 HTTPClient 用于天气 API
- `main/services/ota_manager.cpp` - 替换 HTTPClient 用于 OTA 下载

**预计工时**：1-2 天

---

#### 阶段 7.6：OTA 升级迁移（待执行）

**目标**：替换 Update.h 和 WiFiClientSecure.h 的调用

**封装设计**：
```cpp
// 使用 esp_ota_ops.h
bool otaBegin(size_t size);
bool otaWrite(const uint8_t* data, size_t length);
bool otaEnd();
void otaAbort();
```

**ESP-IDF 实现要点**：
- 使用 `esp_ota_get_next_update_partition()` 获取分区
- 使用 `esp_ota_begin()` 开始 OTA
- 使用 `esp_ota_write()` 写入数据
- 使用 `esp_ota_end()` 完成 OTA
- 使用 `esp_ota_set_boot_partition()` 设置启动分区

**需要修改的文件**：
- `include/services/ota_manager.h` - 移除 Arduino OTA 头文件
- `main/services/ota_manager.cpp` - 替换 Update 类的所有调用

**预计工时**：1-2 天

---

#### 阶段 7.7：清理和测试（待执行）

**目标**：移除所有 Arduino WiFi 相关头文件，确保编译通过

**需要移除的头文件**：
- `#include <WiFi.h>`
- `#include <WebServer.h>`
- `#include <DNSServer.h>`
- `#include <HTTPClient.h>`
- `#include <WiFiClientSecure.h>`
- `#include <Update.h>`
- `#include <WiFiServer.h>`
- `#include <WiFiClient.h>`

**功能验证**：
1. WiFi 连接功能
2. AP 配网模式
3. TCP 服务器连接
4. Web 设置页面
5. OTA 升级
6. 天气 API 调用
7. 深度睡眠唤醒后 WiFi 恢复

**预计工时**：1 天

---

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
| 1 | 构建系统迁移 | 2-3 天 | ★★☆ | ✅ 已完成 |
| 2 | 核心头文件重构 | 1 天 | ★☆☆ | ✅ 已完成 |
| 3 | String 类替换 | 5-7 天 | ★★★ | ✅ 已完成 |
| 4 | GPIO 和硬件抽象层 | 1-2 天 | ★★☆ | ✅ 已完成 |
| 5 | I2C 通信迁移 | 2-3 天 | ★★☆ | ✅ 已完成 |
| 6 | SPIFFS 文件系统迁移 | 2-3 天 | ★★☆ | ✅ 已完成 |
| 7 | WiFi 和网络迁移 | 9-14 天 | ★★★★ | 🔄 进行中（7.1-7.2已完成） |
| 8 | 第三方库适配和清理 | 2-3 天 | ★★☆ | ⏳ 待执行 |
| **总计** | - | **25-37 天** | - | 6.1/8 完成 |

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

# ESP32 1602A 无线显示屏 - 项目文档

> 基于 kuilb/esp32_1602 的个人定制版本
> GitHub: https://github.com/LvZhouoasis/esp32_1602_rebulid
> 当前分支: `1.1.2`（纯 ESP-IDF 框架，已完全移除 Arduino 依赖）

---

## 一、项目概览

基于 **ESP32-S3** 的无线 1602A LCD 显示屏固件。通过 TCP 协议接收数据帧，在 1602A LCD 上高速播放，最高约 83 FPS。内置 WiFi 配网、OTA 升级、天气显示、番茄钟等功能。

### 核心能力
- 1602A LCD 高速显示（4-bit 并行接口，差分刷新算法）
- TCP 推流接收并播放数据帧
- WiFi 配网（AP 模式 + 强制门户）
- Web 配置页面（端口 80，内嵌 HTML）
- OTA 固件升级（URL / 文件上传）
- 和风天气 API 集成（JWT 认证）
- 多页面菜单系统
- 自动亮度调节（OPT3001 光传感器）
- 电源管理（深度睡眠、动态 CPU 频率）

---

## 二、硬件需求

| 组件 | 型号/规格 | 说明 |
|------|----------|------|
| 主控 | ESP32-S3-N8R8 | 16MB Flash + 8MB OPI PSRAM |
| LCD | 1602A | 4-bit 并行接口，日文字符集版本 |
| RGB LED | WS2812 | 单颗，状态指示 |
| 蜂鸣器 | 有源/无源 | GPIO 13 驱动 |
| 电量计 | BQ27421 | I2C 通信，电池电压/电流/SOC |
| 光传感器 | OPT3001 | I2C 通信，环境光照度 |
| 按键 | 5个 | 上/下/左/右/中（含电源键） |
| 电池 | 1000mAh | 设计容量 |

### GPIO 引脚分配

```
LCD:  RS=1, E=2, D4=3, D5=4, D6=5, D7=6, BLA=7, CTL=18
按键: LEFT=9, CENTER=10, RIGHT=11, SIDE/POWER=12
RGB:  WS2812=8
蜂鸣器: BUZZER=13
I2C:  SDA=38, SCL=21
其他: FUEL_WARN=17, DC_PLUG=14
```

---

## 三、项目结构

```
second/
├── CMakeLists.txt              # ESP-IDF 项目根配置
├── main/                       # 主组件目录（ESP-IDF 标准结构）
│   ├── CMakeLists.txt          # 主组件配置
│   ├── main.cpp                # 主程序入口 + 功耗管理
│   ├── hardware/               # 硬件驱动层
│   │   ├── lcd_driver.cpp      # LCD 驱动（差分刷新核心）
│   │   ├── button.cpp          # 按键扫描（FreeRTOS 双任务）
│   │   ├── buzzer.cpp          # 蜂鸣器（队列式非阻塞播放）
│   │   ├── rgb_led.cpp         # WS2812 RGB LED
│   │   ├── fuel_gauge.cpp      # BQ27421 电量计
│   │   └── opt3001.cpp         # OPT3001 光传感器
│   ├── applications/           # 应用层
│   │   ├── clock.cpp           # 时钟（NTP 同步）
│   │   ├── weather.cpp         # 天气（和风天气 API）
│   │   ├── pomodoro.cpp        # 番茄钟
│   │   ├── badappleplayer.cpp  # Bad Apple 播放器
│   │   ├── setting.cpp         # 设置界面
│   │   └── about.cpp           # 关于信息
│   ├── connectivity/           # 网络连接层（ESP-IDF 封装）
│   │   ├── wifi_esp32.cpp      # WiFi 封装（兼容 Arduino WiFi API）
│   │   ├── wifi_config.cpp     # WiFi 配网（AP + Web）
│   │   ├── network.cpp         # TCP 服务器 + 帧缓存
│   │   ├── tcp_server.cpp      # TCP Server/Client 封装
│   │   ├── http_server_wrapper.cpp # HTTP 服务器封装
│   │   ├── http_client_wrapper.cpp # HTTP 客户端封装
│   │   ├── dns_server.cpp      # DNS 服务器（强制门户）
│   │   └── jwt_auth.cpp        # JWT 认证（Ed25519）
│   ├── services/               # 服务层
│   │   ├── protocol.cpp        # 自定义协议解析
│   │   ├── playbuffer.cpp      # 帧播放缓冲
│   │   ├── config_manager.cpp  # 配置管理器基类
│   │   ├── wifi_config_manager.cpp
│   │   ├── qweather_auth_config_manager.cpp
│   │   ├── ota_manager.cpp     # OTA 升级
│   │   ├── ota_esp32.cpp       # OTA 封装（ESP-IDF 原生）
│   │   ├── time_manager.cpp    # NTP 时间同步
│   │   ├── sleep_manager.cpp   # 深度睡眠管理
│   │   ├── auto_brightness.cpp # 自动亮度
│   │   ├── kanamap.cpp         # 假名映射（已禁用）
│   │   └── web_setting.cpp     # Web 设置服务器
│   ├── menu/                   # 菜单系统
│   │   ├── menu.cpp            # 菜单核心逻辑
│   │   ├── menu_item.cpp       # 菜单项定义
│   │   ├── menu_navigator.cpp  # 菜单导航
│   │   └── status_bar_renderer.cpp # 状态栏渲染
│   ├── ui/                     # UI 组件
│   │   ├── animetion.cpp       # 动画系统
│   │   ├── icons.cpp           # 图标定义
│   │   └── hold_progress.cpp   # 长按进度条
│   └── utils/                  # 工具类
│       ├── logger.cpp          # 多模块日志系统
│       └── memory_utils.cpp    # 内存工具
├── include/                    # 公共头文件
├── sdkconfig.defaults          # ESP-IDF 默认配置
└── partitions.csv              # Flash 分区表
├── test/                         # 单元测试
├── data/                         # SPIFFS 数据（badapple.bin）
├── website/                      # Web 前端源文件
├── script/                       # 版本管理脚本
├── boards/                       # 自定义板定义
├── platformio.ini                # PlatformIO 配置
├── partitions.csv                # 分区表
├── version.txt                   # 版本号（自动生成）
└── PROJECT.md                    # 本文档
```

---

## 四、架构分层

```
┌─────────────────────────────────────────────┐
│              main.cpp (主循环)                │
│  setup() → 初始化全部 → loop() → 功耗管理     │
├──────────┬──────────┬──────────┬─────────────┤
│ 应用层    │ 菜单系统  │ 连接层    │  硬件层     │
│ apps/    │ menu/    │ conn/    │  hardware/  │
├──────────┴──────────┴──────────┴─────────────┤
│              服务层 services/                  │
├──────────────────────────────────────────────┤
│              UI + 工具层                       │
└──────────────────────────────────────────────┘
```

### 数据流

```
客户端App → TCP:13000 → network.cpp(接收/缓存)
    → playbuffer.cpp(帧调度) → protocol.cpp(协议解析)
    → lcd_driver.cpp(差分刷新) → 1602A LCD
```

---

## 五、关键设计

### 1. LCD 差分刷新 (lcd_driver.cpp)

维护两帧缓冲：
- `s_hwFrame`：当前硬件状态
- `s_pendingFrame`：目标状态

每帧只写变化的 DDRAM 位置和 CGRAM 槽位，大幅减少 I/O 操作。

### 2. TCP 帧缓存 (network.cpp)

- `std::deque<FramePacket>` 双端队列
- 重复帧检测：连续相同帧直接丢弃
- 菜单模式丢帧：不消费流媒体帧
- 断连去抖：持续 1.2s 判定断开

### 3. 功耗管理 (main.cpp)

三种模式动态切换：

| 模式 | CPU | WiFi | Light Sleep | 场景 |
|------|-----|------|-------------|------|
| Performance | 240MHz | 全功率 | 禁止 | 推流/配网 |
| ConnectedBalanced | 80MHz | 低功耗 | 禁止 | 有连接空闲 |
| Standby | 80MHz | 深度省电 | 允许 | 菜单空闲 |

### 4. 菜单系统 (menu.cpp)

- 状态机 + 历史栈（最多 4 层）
- `InterfaceHandler` 函数指针注册子界面
- 主菜单第 0 项动态显示（WiFi 状态）

### 5. Web 配置 (web_setting.cpp)

- HTML/CSS/JS 内嵌在 `web_pages.h`（PROGMEM）
- 端口 80，阻塞式服务
- 路由：主页、OTA、城市搜索、设备信息/设置

### 6. 协议格式 (protocol.cpp)

```
[AA 55] [LEN] [帧间隔H] [帧间隔L] [数据体...]
```

数据体：
- `0x00` + 数据：ASCII 字符（非 ASCII 已跳过）
- `0x01` + 8字节：自定义点阵字符

---

## 六、编译环境

### 框架说明

本项目使用 **Arduino 框架**，代码中混合使用了 Arduino API 和 ESP-IDF API：

| API 来源 | 使用的 API |
|---------|-----------|
| Arduino | `String` 类、`WiFi`、`WebServer`、`HTTPClient`、`SPIFFS`、`FastLED`、`ArduinoJson` |
| ESP-IDF | `esp_pm`、`esp_timer`、`esp_sleep`、`esp_rom_delay_us`、FreeRTOS、GPIO 寄存器、LEDC |

> **注意**：构建工具（PlatformIO `pio` 或 ESP-IDF `idf.py`）只是编译器，不影响代码。
> 换构建工具不需要改代码，换框架才需要。

### PlatformIO 配置

```ini
[env:esp32s3-1602]
platform = espressif32@6.9.0
board = esp32-1602
framework = arduino
board_upload.flash_size = 16MB
board_build.psram_type = opi
```

### 依赖库

| 库 | 用途 | 类型 |
|---|------|------|
| FastLED | WS2812 RGB LED | Arduino 库 |
| ArduinoJson | JSON 解析 | Arduino 库 |
| libsodium | Ed25519 JWT 签名 | C 库（ESP-IDF 兼容） |
| zlib_turbo | Gzip 解压 | C 库 |

### 编译命令

```bash
# Release 版本
python script/set_version.py release
pio run -e esp32s3-1602

# Debug 版本
python script/set_version.py debug
pio run -e esp32s3-1602-dev

# 快速编译上传
.\run.bat    # 交互式菜单
.\up.bat     # 快速编译+上传
```

---

## 七、Git 分支

```
main   ← 初始版本（v1.0.4 基线）
  └── 1.0.0  ← 删除日文假名显示功能
       └── 1.0.1  ← 方案B：替换 millis/delay 为 ESP-IDF API
```

### 分支修改记录

| 分支 | 修改内容 |
|------|---------|
| `1.0.0` | 删除日文假名显示功能（7 个文件） |
| `1.0.1` | 替换 millis/delay 为 ESP-IDF API（22 个文件，142 处修改） |

---

## 八、已做修改记录

### 1.0.0 分支：删除日文假名显示功能

**原因**：项目使用 1602A 日文字符集 LCD 显示片假名，用户不需要此功能。

**修改文件**：

| 文件 | 修改内容 |
|------|---------|
| `src/services/kanamap.cpp` | 清空假名映射表，`initKanaMap()` 改为空实现 |
| `include/services/kanamap.h` | 精简注释 |
| `src/hardware/lcd_driver.cpp` | 删除 `convertUTF8ToKana()` 函数；`lcdText()` 和 `lcdPrint()` 直接使用原始字符串 |
| `src/services/protocol.cpp` | TCP 协议层：只接受 ASCII 字符（<0x80），UTF-8 多字节字符直接跳过 |
| `src/applications/badappleplayer.cpp` | 删除 `_utf8ToUnicode()` 和 `_convertUtf8ToKana()`；`_lyricDisplay()` 直接显示英文 |
| `include/applications/badappleplayer.h` | 清空日文歌词，只保留英文标题和致谢 |
| `include/services/protocol.h` | 清理假名相关注释 |

**保留兼容**：
- `main.cpp` 中 `initKanaMap()` 调用保留（空函数，无副作用）
- `kanamap.h` 声明保留（避免其他文件编译报错）

---

## 九、Web 页面

内嵌在 `web_pages.h`，通过 ESP32 IP 地址访问（端口 80）。

| 页面 | 路由 | 功能 |
|------|------|------|
| 主设置页 | `/` | 搜索城市、OTA、天气密钥、设备信息、设备设置 |
| OTA 升级 | `/ota` | URL 升级 + 文件上传 |
| 城市搜索 | `/citysearch` | 搜索并设置天气城市 |
| WiFi 配网 | AP 模式 192.168.4.1 | 扫描 + 连接 WiFi |

访问方式：LCD 菜单 Settings → Web Settings → 显示 IP 地址 → 浏览器打开

---

## 十、测试

```bash
# 运行所有测试
pio test

# 运行特定测试
pio test -f test_config_manager
pio test -f test_wifi_config
pio test -f test_qweather_config
pio test -f test_lcd_fps
```

---

## 十一、注意事项

1. **首次烧录**必须执行 `pio run --target uploadfs` 上传文件系统
2. **分区表**在 `partitions.csv`，Flash 16MB
3. **版本号**由 `script/genrate_version.py` 自动生成，写入 `version.txt`
4. **日志级别**在 `platformio.ini` 中通过 `-D DEFAULT_LOG_LEVEL` 控制
5. **web_preview/** 目录已删除，如需预览 Web 页面可从 `web_pages.h` 提取 HTML

---

## 十二、方案 B 实施记录：替换 Arduino 特有 API ✅ 已完成

### 目标

将 Arduino 框架中的 `millis()` 和 `delay()` 替换为 ESP-IDF 原生 API，减少 Arduino 依赖，提升代码的可移植性和一致性。保持 Arduino 框架不变（String 类、WiFi、WebServer 等保留）。

### 新增 API（mydefine.h）

```cpp
#define GET_MS()    ((uint32_t)(esp_timer_get_time() / 1000))  // 替代 millis()
#define WAIT_MS(ms) vTaskDelay(pdMS_TO_TICKS(ms))              // 替代 delay()
```

### 替换统计

| 类别 | 数量 | 状态 |
|------|------|------|
| `millis()` → `GET_MS()` | 77 处 | ✅ |
| `delay()` → `WAIT_MS()` | 65 处 | ✅ |
| **总计** | **142 处** | ✅ |

### 修改文件（22 个）

| 模块 | 文件 | 修改数 |
|------|------|--------|
| 硬件层 | `buzzer.cpp` / `button.cpp` / `fuel_gauge.cpp` / `lcd_driver.cpp` / `opt3001.cpp` | 55 |
| 服务层 | `config_manager.cpp` / `ota_manager.cpp` / `playbuffer.cpp` / `protocol.cpp` / `sleep_manager.cpp` / `time_manager.cpp` / `web_setting.cpp` | 29 |
| 应用层 | `clock.cpp` / `pomodoro.cpp` / `setting.cpp` / `weather.cpp` | 26 |
| 其他 | `main.cpp` / `menu.cpp` / `status_bar_renderer.cpp` / `animetion.cpp` / `logger.cpp` / `network.cpp` / `wifi_config.cpp` | 32 |

### 替换规则

| Arduino API | ESP-IDF 替代 | 说明 |
|-------------|-------------|------|
| `millis()` | `esp_timer_get_time() / 1000` | 返回 uint64_t 毫秒 |
| `delay(ms)` | `vTaskDelay(pdMS_TO_TICKS(ms))` | FreeRTOS 延时，让出 CPU |

### 需要修改的文件（按优先级排序）

#### 阶段 1：高频模块（影响最大）

| 文件 | millis 次数 | delay 次数 | 合计 | 优先级 |
|------|-----------|-----------|------|--------|
| `hardware/buzzer.cpp` | 12 | 0 | 12 | P0 |
| `hardware/button.cpp` | 4 | 0 | 4 | P0 |
| `connectivity/network.cpp` | 9 | 0 | 9 | P0 |
| `connectivity/wifi_config.cpp` | 5 | 0 | 5 | P0 |
| `hardware/fuel_gauge.cpp` | 0 | 26 | 26 | P1 |
| `hardware/lcd_driver.cpp` | 0 | 11 | 11 | P1 |

#### 阶段 2：服务层

| 文件 | millis 次数 | delay 次数 | 合计 | 优先级 |
|------|-----------|-----------|------|--------|
| `services/web_setting.cpp` | 3 | 4 | 7 | P1 |
| `services/sleep_manager.cpp` | 0 | 6 | 6 | P1 |
| `services/playbuffer.cpp` | 3 | 0 | 3 | P2 |
| `services/ota_manager.cpp` | 2 | 1 | 3 | P2 |
| `services/time_manager.cpp` | 4 | 1 | 5 | P2 |
| `services/protocol.cpp` | 2 | 0 | 2 | P2 |
| `services/config_manager.cpp` | 0 | 3 | 3 | P2 |

#### 阶段 3：应用层

| 文件 | millis 次数 | delay 次数 | 合计 | 优先级 |
|------|-----------|-----------|------|--------|
| `applications/weather.cpp` | 9 | 2 | 11 | P2 |
| `applications/setting.cpp` | 4 | 5 | 9 | P2 |
| `applications/clock.cpp` | 3 | 0 | 3 | P3 |
| `applications/pomodoro.cpp` | 3 | 0 | 3 | P3 |

#### 阶段 4：其他模块

| 文件 | millis 次数 | delay 次数 | 合计 | 优先级 |
|------|-----------|-----------|------|--------|
| `menu/status_bar_renderer.cpp` | 8 | 0 | 8 | P3 |
| `menu/menu.cpp` | 1 | 0 | 1 | P3 |
| `ui/animetion.cpp` | 2 | 0 | 2 | P3 |
| `utils/logger.cpp` | 2 | 1 | 3 | P3 |
| `main.cpp` | 1 | 3 | 4 | P3 |
| `hardware/opt3001.cpp` | 0 | 2 | 2 | P3 |

### 修改示例

#### millis() 替换

```cpp
// 修改前
#include <Arduino.h>
unsigned long now = millis();

// 修改后
#include <esp_timer.h>
uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
```

#### delay() 替换

```cpp
// 修改前
#include <Arduino.h>
delay(500);

// 修改后
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
vTaskDelay(pdMS_TO_TICKS(500));
```

### 特殊情况处理

1. **delay(1) 或 delay(0)**：使用 `esp_rom_delay_us(1000)` 替代（微秒级延时）
2. **lcd_driver.cpp 中的 delay(5)**：已有 `esp_rom_delay_us` 使用，保持不变即可
3. **fuel_gauge.cpp 中的 delay(5~10)**：I2C 通信延时，可改为 `esp_rom_delay_us(5000)`
4. **全局变量类型**：`unsigned long millis_xxx` → `uint32_t millis_xxx`

### 统计汇总

| 类别 | 数量 |
|------|------|
| 需修改文件总数 | 22 个 |
| millis() 替换总数 | 77 处 |
| delay() 替换总数 | 65 处 |
| **总计修改点** | **142 处** |
| 预计工作量 | 1-2 天 |
| 风险等级 | 低（纯机械替换，行为一致） |

### 验证方案

每个阶段修改完成后执行：

```bash
# 1. 编译检查
pio run -e esp32s3-1602

# 2. 运行测试
pio test -e esp32s3-1602-test

# 3. 功能验证清单
- [ ] LCD 显示正常
- [ ] 按键响应正常
- [ ] WiFi 连接正常
- [ ] TCP 推流正常
- [ ] 蜂鸣器音效正常
- [ ] Web 页面可访问
- [ ] OTA 升级正常
- [ ] 深度睡眠/唤醒正常
- [ ] 电池信息显示正常
- [ ] 自动亮度调节正常
```

### 执行顺序建议

```
阶段 1（硬件层）→ 阶段 2（服务层）→ 阶段 3（应用层）→ 阶段 4（其他）
    ↓                  ↓                  ↓                  ↓
  编译验证            编译验证            编译验证            编译验证
```

每个阶段修改后必须编译通过再进入下一阶段，避免错误累积。

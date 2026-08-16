#ifndef MYDEFINE_H
#define MYDEFINE_H

// ==============================
// ESP-IDF 原生头文件（替代 Arduino.h）
// ==============================
#include <stdint.h>
#include <stdbool.h>
#include <cstring>
#include <cstdio>
#include <cstdlib>

// ESP-IDF 驱动
#include "driver/gpio.h"

// ESP-IDF 系统
#include <esp_timer.h>

// FreeRTOS
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// ==============================
// ESP-IDF 时间工具宏（替代 Arduino millis/delay）
// ==============================

/// 获取当前毫秒数（替代 millis()）
#define GET_MS() ((uint32_t)(esp_timer_get_time() / 1000))

/// 毫秒级延时（替代 delay(ms)），让出 CPU 给其他任务
#define WAIT_MS(ms) vTaskDelay(pdMS_TO_TICKS(ms))

/**
 * @brief 将一个值从一个范围映射到另一个范围（替代 Arduino map）
 *
 * @param[in] x 要映射的值
 * @param[in] in_min 输入范围最小值
 * @param[in] in_max 输入范围最大值
 * @param[in] out_min 输出范围最小值
 * @param[in] out_max 输出范围最大值
 * @return long �射后的值
 */
inline long map(long x, long in_min, long in_max, long out_min, long out_max) {
    return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

/**
 * @brief 将值限制在指定范围内（替代 Arduino constrain）
 *
 * @param[in] x 要限制的值
 * @param[in] a 范围边界1
 * @param[in] b 范围边界2
 * @return T 限制后的值
 */
template<typename T>
inline T constrain(T x, T a, T b) {
    return (x < a) ? a : (x > b) ? b : x;
}

/**
 * @brief 将指定 GPIO 引脚配置为输出模式
 * 
 * @param[in] pin GPIO 引脚号
 */
inline void setOutput(int pin) {
    gpio_set_direction((gpio_num_t)pin, GPIO_MODE_OUTPUT);
}

/**
 * @brief 将指定 GPIO 引脚配置为输入模式（带内部下拉）
 * 
 * @param[in] pin GPIO 引脚号
 * 
 * @note 启用内部下拉，禁用上拉电阻，用于按钮等输入
 */
inline void setInputPullDown(int pin) {
    gpio_config_t io_conf = {};
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pin_bit_mask = 1ULL << pin;
    io_conf.pull_down_en = GPIO_PULLDOWN_ENABLE;   // 启用内部下拉
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;      // 禁用上拉
    gpio_config(&io_conf);
}

/**
 * @brief 将指定 GPIO 引脚配置为输入模式（带内部上拉）
 * 
 * @param[in] pin GPIO 引脚号
 * 
 * @note 启用内部上拉，禁用下拉电阻，用于按钮等输入
 */
inline void setInputPullUp(int pin) {
    gpio_config_t io_conf = {};
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pin_bit_mask = 1ULL << pin;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;  // 禁用内部下拉
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE;       // 启用内部上拉
    gpio_config(&io_conf);
}

/**
 * @brief 读取 GPIO 引脚电平（替代 digitalRead）
 *
 * @param[in] pin GPIO 引脚号
 * @return int 引脚电平（0 或 1）
 */
inline int digitalRead(int pin) {
    return gpio_get_level((gpio_num_t)pin);
}

/**
 * @brief 设置 GPIO 引脚电平（替代 digitalWrite）
 *
 * @param[in] pin GPIO 引脚号
 * @param[in] level 电平值（LOW=0, HIGH=1）
 */
inline void digitalWrite(int pin, int level) {
    gpio_set_level((gpio_num_t)pin, level);
}

/**
 * @brief 设置 LEDC 通道（替代 Arduino ledcSetup）
 *
 * @param[in] channel LEDC 通道号
 * @param[in] freq 频率（Hz）
 * @param[in] resolution 分辨率（位数）
 */
inline void ledcSetup(uint8_t channel, uint32_t freq, uint8_t resolution) {
    ledc_timer_config_t timer_conf = {};
    timer_conf.speed_mode = LEDC_LOW_SPEED_MODE;
    timer_conf.duty_resolution = static_cast<ledc_timer_bit_t>(resolution);
    timer_conf.timer_num = static_cast<ledc_timer_t>(channel % 4);
    timer_conf.freq_hz = freq;
    timer_conf.clk_cfg = LEDC_AUTO_CLK;
    ledc_timer_config(&timer_conf);
}

/**
 * @brief 将 GPIO 引脚附加到 LEDC 通道（替代 Arduino ledcAttachPin）
 *
 * @param[in] pin GPIO 引脚号
 * @param[in] channel LEDC 通道号
 */
inline void ledcAttachPin(uint8_t pin, uint8_t channel) {
    ledc_channel_config_t channel_conf = {};
    channel_conf.gpio_num = pin;
    channel_conf.speed_mode = LEDC_LOW_SPEED_MODE;
    channel_conf.channel = static_cast<ledc_channel_t>(channel % 8);
    channel_conf.timer_sel = static_cast<ledc_timer_t>(channel % 4);
    channel_conf.duty = 0;
    channel_conf.hpoint = 0;
    ledc_channel_config(&channel_conf);
}

/**
 * @brief 设置 LEDC 通道占空比（替代 Arduino ledcWrite）
 *
 * @param[in] channel LEDC 通道号
 * @param[in] duty 占空比值
 */
inline void ledcWrite(uint8_t channel, uint32_t duty) {
    ledc_set_duty(LEDC_LOW_SPEED_MODE, static_cast<ledc_channel_t>(channel % 8), duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, static_cast<ledc_channel_t>(channel % 8));
}

/**
 * @brief 设置 LEDC 通道频率并输出方波（替代 Arduino ledcWriteTone）
 *
 * @param[in] channel LEDC 通道号
 * @param[in] freq 频率（Hz），0 表示停止输出
 */
inline void ledcWriteTone(uint8_t channel, uint32_t freq) {
    if (freq == 0) {
        ledcWrite(channel, 0);
        return;
    }
    ledc_timer_config_t timer_conf = {};
    timer_conf.speed_mode = LEDC_LOW_SPEED_MODE;
    timer_conf.duty_resolution = LEDC_TIMER_8_BIT;
    timer_conf.timer_num = static_cast<ledc_timer_t>(channel % 4);
    timer_conf.freq_hz = freq;
    timer_conf.clk_cfg = LEDC_AUTO_CLK;
    ledc_timer_config(&timer_conf);
    ledcWrite(channel, 128);  // 50% 占空比
}

/**
 * @brief 分离 GPIO 引脚与 LEDC 通道（替代 Arduino ledcDetachPin）
 *
 * @param[in] pin GPIO 引脚号
 */
inline void ledcDetachPin(uint8_t pin) {
    gpio_set_direction((gpio_num_t)pin, GPIO_MODE_DISABLE);
}

// ==============================
// I2C 辅助类（替代 Arduino Wire）
// ==============================

/**
 * @brief 初始化 I2C 总线（替代 Arduino Wire.begin）
 *
 * @param[in] port I2C 端口号
 * @param[in] sda_pin SDA 引脚号
 * @param[in] scl_pin SCL 引脚号
 * @param[in] freq 时钟频率（Hz）
 * @return true 初始化成功，false 初始化失败
 */
inline bool i2cInit(i2c_port_t port, int sda_pin, int scl_pin, uint32_t freq = 100000) {
    i2c_config_t conf = {};
    conf.mode = I2C_MODE_MASTER;
    conf.sda_io_num = sda_pin;
    conf.scl_io_num = scl_pin;
    conf.sda_pullup_en = GPIO_PULLUP_ENABLE;
    conf.scl_pullup_en = GPIO_PULLUP_ENABLE;
    conf.master.clk_speed = freq;
    esp_err_t err = i2c_param_config(port, &conf);
    if (err != ESP_OK) {
        return false;
    }
    err = i2c_driver_install(port, conf.mode, 0, 0, 0);
    return (err == ESP_OK);
}

/**
 * @brief I2C 设备操作辅助类
 *
 * 封装 ESP-IDF I2C 驱动，提供类似 Arduino Wire 的接口
 */
class I2CDevice {
private:
    i2c_port_t _port;
    uint8_t _addr;

public:
    /**
     * @brief 构造函数
     * @param[in] port I2C 端口号
     * @param[in] addr 设备地址
     */
    I2CDevice(i2c_port_t port, uint8_t addr) : _port(port), _addr(addr) {}

    /**
     * @brief 检测设备是否存在
     * @return true 设备存在，false 设备不存在
     */
    bool isPresent() {
        i2c_cmd_handle_t cmd = i2c_cmd_link_create();
        i2c_master_start(cmd);
        i2c_master_write_byte(cmd, (_addr << 1) | I2C_MASTER_WRITE, true);
        i2c_master_stop(cmd);
        esp_err_t ret = i2c_master_cmd_begin(_port, cmd, pdMS_TO_TICKS(100));
        i2c_cmd_link_delete(cmd);
        return (ret == ESP_OK);
    }

    /**
     * @brief 读取寄存器
     * @param[in] reg 寄存器地址
     * @param[out] data 读取的数据缓冲区
     * @param[in] len 要读取的字节数
     * @return true 读取成功，false 读取失败
     */
    bool readRegister(uint8_t reg, uint8_t* data, size_t len) {
        i2c_cmd_handle_t cmd = i2c_cmd_link_create();
        i2c_master_start(cmd);
        i2c_master_write_byte(cmd, (_addr << 1) | I2C_MASTER_WRITE, true);
        i2c_master_write_byte(cmd, reg, true);
        i2c_master_start(cmd);
        i2c_master_write_byte(cmd, (_addr << 1) | I2C_MASTER_READ, true);
        if (len > 1) {
            i2c_master_read(cmd, data, len - 1, I2C_MASTER_ACK);
        }
        i2c_master_read_byte(cmd, data + len - 1, I2C_MASTER_NACK);
        i2c_master_stop(cmd);
        esp_err_t ret = i2c_master_cmd_begin(_port, cmd, pdMS_TO_TICKS(100));
        i2c_cmd_link_delete(cmd);
        return (ret == ESP_OK);
    }

    /**
     * @brief 写入寄存器
     * @param[in] reg 寄存器地址
     * @param[in] data 要写入的数据
     * @param[in] len 数据长度
     * @return true 写入成功，false 写入失败
     */
    bool writeRegister(uint8_t reg, const uint8_t* data, size_t len) {
        i2c_cmd_handle_t cmd = i2c_cmd_link_create();
        i2c_master_start(cmd);
        i2c_master_write_byte(cmd, (_addr << 1) | I2C_MASTER_WRITE, true);
        i2c_master_write_byte(cmd, reg, true);
        i2c_master_write(cmd, data, len, true);
        i2c_master_stop(cmd);
        esp_err_t ret = i2c_master_cmd_begin(_port, cmd, pdMS_TO_TICKS(100));
        i2c_cmd_link_delete(cmd);
        return (ret == ESP_OK);
    }

    /**
     * @brief 读取16位寄存器（大端序）
     * @param[in] reg 寄存器地址
     * @return uint16_t 读取的值
     */
    uint16_t readRegister16BE(uint8_t reg) {
        uint8_t data[2];
        if (readRegister(reg, data, 2)) {
            return (data[0] << 8) | data[1];
        }
        return 0;
    }

    /**
     * @brief 读取16位寄存器（小端序）
     * @param[in] reg 寄存器地址
     * @return uint16_t 读取的值
     */
    uint16_t readRegister16LE(uint8_t reg) {
        uint8_t data[2];
        if (readRegister(reg, data, 2)) {
            return (data[1] << 8) | data[0];
        }
        return 0;
    }

    /**
     * @brief 写入16位寄存器（大端序）
     * @param[in] reg 寄存器地址
     * @param[in] value 要写入的值
     * @return true 写入成功，false 写入失败
     */
    bool writeRegister16BE(uint8_t reg, uint16_t value) {
        uint8_t data[2] = { static_cast<uint8_t>(value >> 8), static_cast<uint8_t>(value & 0xFF) };
        return writeRegister(reg, data, 2);
    }

    /**
     * @brief 写入16位寄存器（小端序）
     * @param[in] reg 寄存器地址
     * @param[in] value 要写入的值
     * @return true 写入成功，false 写入失败
     */
    bool writeRegister16LE(uint8_t reg, uint16_t value) {
        uint8_t data[2] = { static_cast<uint8_t>(value & 0xFF), static_cast<uint8_t>(value >> 8) };
        return writeRegister(reg, data, 2);
    }

    /**
     * @brief 读取单字节
     * @param[in] reg 寄存器地址
     * @return uint8_t 读取的值
     */
    uint8_t readByte(uint8_t reg) {
        uint8_t data;
        if (readRegister(reg, &data, 1)) {
            return data;
        }
        return 0;
    }

    /**
     * @brief 写入单字节
     * @param[in] reg 寄存器地址
     * @param[in] value 要写入的值
     * @return true 写入成功，false 写入失败
     */
    bool writeByte(uint8_t reg, uint8_t value) {
        return writeRegister(reg, &value, 1);
    }
};

// ==============================
// SPIFFS 辅助函数（替代 Arduino SPIFFS）
// ==============================

/**
 * @brief 初始化 SPIFFS 文件系统（替代 Arduino SPIFFS.begin）
 *
 * @param[in] basePath 挂载点路径
 * @param[in] max_files 最大打开文件数
 * @return true 初始化成功，false 初始化失败
 */
inline bool spiffsInit(const char* basePath = "/spiffs", size_t max_files = 10) {
    esp_vfs_spiffs_conf_t conf = {};
    conf.base_path = basePath;
    conf.partition_label = NULL;
    conf.max_files = max_files;
    conf.format_if_mount_failed = true;
    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    return (ret == ESP_OK);
}

/**
 * @brief 获取 SPIFFS 文件系统信息
 *
 * @param[out] total 总字节数
 * @param[out] used 已使用字节数
 * @return true 获取成功，false 获取失败
 */
inline bool spiffsInfo(size_t* total, size_t* used) {
    return (esp_spiffs_info(NULL, total, used) == ESP_OK);
}

// ==============================
// LCD 引脚定义
// ==============================

#define LCD_RS              1   ///< LCD RS
#define LCD_E               2  ///< LCD E
#define LCD_D4              3  ///< LCD D4
#define LCD_D5              4  ///< LCD D5
#define LCD_D6              5  ///< LCD D6
#define LCD_D7              6  ///< LCD D7
#define LCD_BLA             7  ///< LCD 背光
#define LCD_CTL            18  ///< LCD 对比度控制引脚（ADC）

// ==============================
// PWM 参数定义（LCD 背光）
// ==============================

#define LCD_BLA_PWM_PIN         LCD_BLA       ///< LCD 背光控制引脚
#define LCD_BLA_PWM_CHANNEL     0             ///< PWM 通道（0~7）
#define LCD_BLA_PWM_FREQ        5000          ///< PWM 频率（Hz）
#define LCD_BLA_PWM_RESOLUTION  8             ///< 分辨率（位数）
#define LCD_BLA_PWM_MAX_DUTY    ((1 << LCD_BLA_PWM_RESOLUTION) - 1) ///< 最大占空比

// ==============================
// PWM 参数定义（LCD 对比度）
// ==============================

#define LCD_CTL_PWM_PIN         LCD_CTL       ///< LCD 对比度控制引脚
#define LCD_CTL_PWM_CHANNEL     2             ///< PWM 通道（0~7）
#define LCD_CTL_PWM_FREQ        5000          ///< PWM 频率（Hz）
#define LCD_CTL_PWM_RESOLUTION  8             ///< 分辨率（位数）
#define LCD_CTL_PWM_MAX_DUTY    ((1 << LCD_CTL_PWM_RESOLUTION) - 1) ///< 最大占空比

// ==============================
// 按键引脚定义
// ==============================
#define BUTTON_LEFT_PIN         9  ///< 左按键
#define BUTTON_RIGHT_PIN        11  ///< 右按键
#define BUTTON_CENTER_PIN       10   ///< 中按键
#define BUTTON_SIDE_PIN         12  ///< 侧按键

#define BUTTON_POWER_PIN        12   ///< 电源按键

// ==============================
// 燃料计IC引脚定义
// ==============================
#define FUEL_GAUGE_WARNING_PIN  17  ///< 燃料计警告引脚

//i2c
#define SDA_PIN     38   ///< SDA 引脚
#define SCL_PIN     21   ///< SCL 引脚

// ==============================
// 板载外设
// ==============================

#define RGB_PIN             8  ///< 板载 RGB 灯
#define BUZZER_PIN         13  ///< 板载 蜂鸣器
#define DC_PLUG_PIN        14  ///< 板载 DC 插入检测

// ==============================
// 系统参数设置
// ==============================
#define BATTERY_DESIGN_CAPACITY_MAH 1000  ///< 电池设计容量 (mAh)
#define VISIBLE_LINES       2           ///< LCD 行数

#define BaudRate            115200      ///< 串口通信波特率
#define MAX_CACHE_SIZE      16          ///< 最大缓存帧数量
#define MAX_RECV_BUFFER_SIZE 1024       ///< 最大接收缓冲区大小（字节）
#define MAX_LATENCY_MS      1000        ///< 最大缓存延迟（单位：毫秒）
#define IMMEDIATE_FRAME_CACHE_KEEP 3    ///< frameInterval=0 时保留最近帧数量
#define CONNECT_PORT        13000       ///< TCP/UDP 通信端口号
#define CONNECT_TIMEOUT_MS  15000       ///< 连接超时时间（单位：毫秒）

// 推流功耗策略（全局）
#define STREAM_GLOBAL_LOW_POWER 1       ///< 1=有连接时优先低功耗；0=保持性能优先
#define STREAM_CONNECTED_CPU_MHZ 80    ///< 有连接时目标CPU频率（建议 120/160）
#define STREAM_CONNECTED_LOW_TX_POWER 1 ///< 有连接时使用低发射功率
#define STREAM_CONNECTED_ULTRA_LOW_TX_POWER 1 ///< 连接态优先使用超低发射功率（若芯片支持）
#define STREAM_CONNECTED_ALWAYS_POWER_SAVE 1 ///< 连接态全程保持省电（1=开启）
#define STREAM_CONNECTED_WIFI_PS_LEVEL 1    ///< 连接态省电级别：1=MIN_MODEM, 2=MAX_MODEM

// 待机监听占空比（降低待机功耗）
#define STANDBY_LISTEN_WINDOW_ENABLE 0      ///< 1=启用待机监听窗口；0=待机时常驻监听
#define STANDBY_LISTEN_CYCLE_MS 5000       ///< 待机监听周期（毫秒）
#define STANDBY_LISTEN_WINDOW_MS 1000        ///< 待机监听窗口（毫秒）
#define STANDBY_ACCEPT_POLL_INTERVAL_MS 700 ///< 待机/断联空闲时 accept 轮询间隔（毫秒）

// 应用界面射频门控（非无线应用）
#define APP_INTERFACE_RF_GATE_ENABLE 1      ///< 1=进入非无线应用界面后默认关闭监听并进入省电
#define APP_INTERFACE_DEFAULT_NETWORK_REQUIRED 0 ///< 应用界面默认是否需要联网（可运行时覆盖）

#define DEBOUNCE_TIME       20          ///< 按钮扫描消抖时间（单位：毫秒）
#define BUTTON_DEBOUNCE_DELAY 150       ///< 按钮软件消抖延迟（单位：毫秒）
#define FIRST_TIME_DELAY    300         ///< 首次启动延迟时间（单位：毫秒）

#define TIME_SYNC_TIMEOUT   30000       ///< 时间同步超时时间（单位：毫秒）
#define TIME_SYNC_RETRY_INTERVAL 1000   ///< 时间同步重试间隔（单位：毫秒）
#define GMT_OFFSET_HOUR     8           ///< GMT 偏移（时间），北京时间为 UTC+8

#endif
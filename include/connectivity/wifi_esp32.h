/**
 * @file wifi_esp32.h
 * @brief ESP-IDF WiFi封装层，提供与Arduino WiFi兼容的API
 *
 * 封装ESP-IDF的WiFi API，保持与Arduino WiFi库相似的接口，
 * 便于从Arduino迁移到纯ESP-IDF框架。
 */

#ifndef WIFI_ESP32_H
#define WIFI_ESP32_H

#include <cstdint>
#include <cstring>
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_log.h"

// WiFi连接状态（兼容Arduino WL_常量）
#define WL_IDLE_STATUS      0
#define WL_NO_SSID_AVAIL    1
#define WL_SCAN_COMPLETED   2
#define WL_CONNECTED        3
#define WL_CONNECT_FAILED   4
#define WL_CONNECTION_LOST  5
#define WL_DISCONNECTED     6

// WiFi加密类型（兼容Arduino WIFI_AUTH_常量）
#define WIFI_AUTH_OPEN      0
#define WIFI_AUTH_WEP       1
#define WIFI_AUTH_WPA_PSK   2
#define WIFI_AUTH_WPA2_PSK  3
#define WIFI_AUTH_WPA_WPA2_PSK 4

// WiFi模式（兼容Arduino WIFI_常量）
#define WIFI_STA    WIFI_MODE_STA
#define WIFI_AP     WIFI_MODE_AP
#define WIFI_AP_STA WIFI_MODE_APSTA
#define WIFI_OFF    WIFI_MODE_NULL

/**
 * @brief IP地址封装类（兼容Arduino IPAddress）
 */
class IPAddress {
private:
    uint32_t _address;
    mutable char _strBuffer[16];  // 用于toString()的缓冲区
public:
    IPAddress();
    IPAddress(uint8_t first, uint8_t second, uint8_t third, uint8_t fourth);
    IPAddress(uint32_t address);
    const char* toString() const;
    operator uint32_t() const { return _address; }
    bool operator==(const IPAddress& other) const { return _address == other._address; }
    bool operator!=(const IPAddress& other) const { return _address != other._address; }
    uint8_t operator[](int index) const;
};

/**
 * @brief WiFi扫描结果结构
 */
struct WiFiScanResult {
    char ssid[33];
    int32_t rssi;
    uint8_t encryptionType;
};

/**
 * @brief ESP-IDF WiFi管理器（兼容Arduino WiFi API）
 */
class WiFiClass {
private:
    static bool _initialized;
    static bool _apMode;
    static int _status;
    static esp_netif_t* _staNetif;
    static esp_netif_t* _apNetif;
    static char _connectedSSID[33];
    static int _scanCount;
    static WiFiScanResult* _scanResults;
    static char _macBuffer[18];  // 用于macAddress()的缓冲区
    static bool _autoReconnect;  // 是否自动重连
    static bool _userDisconnect;  // 用户主动断开标志

    static void _eventHandler(void* arg, esp_event_base_t eventBase,
                             int32_t eventId, void* eventData);

public:
    /**
     * @brief 初始化WiFi子系统
     * @return true 成功，false 失败
     */
    static bool init();

    /**
     * @brief 连接到WiFi网络
     * @param ssid 网络名称
     * @param password 密码
     * @return WL_CONNECTED 等状态码
     */
    static int begin(const char* ssid, const char* password = nullptr);

    /**
     * @brief 断开WiFi连接
     * @param wifiOff 是否关闭WiFi射频
     */
    static void disconnect(bool wifiOff = false);

    /**
     * @brief 启动软AP模式
     * @param ssid AP名称
     * @param password 密码（可选）
     * @return true 成功，false 失败
     */
    static bool softAP(const char* ssid, const char* password = nullptr);

    /**
     * @brief 获取当前连接状态
     * @return WL_CONNECTED 等状态码
     */
    static int status();

    /**
     * @brief 获取本地IP地址
     * @return IP地址
     */
    static IPAddress localIP();

    /**
     * @brief 获取软AP的IP地址
     * @return IP地址
     */
    static IPAddress softAPIP();

    /**
     * @brief 获取已连接的SSID
     * @return SSID字符串
     */
    static const char* SSID();

    /**
     * @brief 获取信号强度
     * @return RSSI值
     */
    static int RSSI();

    /**
     * @brief 获取MAC地址
     * @return MAC地址字符串
     */
    static const char* macAddress();

    /**
     * @brief 设置WiFi睡眠模式
     * @param enable 是否启用睡眠
     */
    static void setSleep(bool enable);

    /**
     * @brief 设置发射功率
     * @param power 功率值（dBm）
     * @return true 成功
     */
    static bool setTxPower(int power);

    /**
     * @brief 设置自动重连
     * @param autoReconnect 是否自动重连
     */
    static void setAutoReconnect(bool autoReconnect);

    /**
     * @brief 设置持久化配置
     * @param persistent 是否持久化
     */
    static void persistent(bool persistent);

    /**
     * @brief 设置WiFi模式
     * @param mode WIFI_STA, WIFI_AP, WIFI_AP_STA
     */
    static void setMode(wifi_mode_t mode);

    /**
     * @brief 开始WiFi扫描
     * @return 扫描到的网络数量
     */
    static int scanNetworks();

    /**
     * @brief 获取扫描到的SSID
     * @param index 索引
     * @return SSID字符串
     */
    static const char* SSID(int index);

    /**
     * @brief 获取扫描到的网络RSSI
     * @param index 索引
     * @return RSSI值
     */
    static int RSSI(int index);

    /**
     * @brief 获取扫描到的网络加密类型
     * @param index 索引
     * @return 加密类型
     */
    static int encryptionType(int index);
};

// 全局WiFi实例（兼容Arduino风格）
extern WiFiClass WiFi;

#endif // WIFI_ESP32_H

/**
 * @file wifi_esp32.cpp
 * @brief ESP-IDF WiFi封装层实现
 */

#include "./connectivity/wifi_esp32.h"
#include <cstring>
#include <cstdio>

static const char* TAG = "WiFi";

// 静态成员初始化
bool WiFiClass::_initialized = false;
bool WiFiClass::_apMode = false;
int WiFiClass::_status = WL_IDLE_STATUS;
esp_netif_t* WiFiClass::_staNetif = nullptr;
esp_netif_t* WiFiClass::_apNetif = nullptr;
char WiFiClass::_connectedSSID[33] = "";
int WiFiClass::_scanCount = 0;
WiFiScanResult* WiFiClass::_scanResults = nullptr;

// 全局WiFi实例
WiFiClass WiFi;

// IPAddress实现
IPAddress::IPAddress() : _address(0) {}

IPAddress::IPAddress(uint8_t first, uint8_t second, uint8_t third, uint8_t fourth)
    : _address((uint32_t)first | ((uint32_t)second << 8) |
               ((uint32_t)third << 16) | ((uint32_t)fourth << 24)) {}

IPAddress::IPAddress(uint32_t address) : _address(address) {}

String IPAddress::toString() const {
    char buf[16];
    snprintf(buf, sizeof(buf), "%d.%d.%d.%d",
             (_address & 0xFF),
             ((_address >> 8) & 0xFF),
             ((_address >> 16) & 0xFF),
             ((_address >> 24) & 0xFF));
    return String(buf);
}

uint8_t IPAddress::operator[](int index) const {
    if (index < 0 || index > 3) return 0;
    return (_address >> (index * 8)) & 0xFF;
}

// WiFi事件处理
void WiFiClass::_eventHandler(void* arg, esp_event_base_t eventBase,
                              int32_t eventId, void* eventData) {
    if (eventBase == WIFI_EVENT) {
        switch (eventId) {
            case WIFI_EVENT_STA_START:
                ESP_LOGI(TAG, "STA started");
                break;

            case WIFI_EVENT_STA_CONNECTED:
                ESP_LOGI(TAG, "Connected to AP");
                _status = WL_CONNECTED;
                break;

            case WIFI_EVENT_STA_DISCONNECTED: {
                wifi_event_sta_disconnected_t* event =
                    (wifi_event_sta_disconnected_t*)eventData;
                ESP_LOGW(TAG, "Disconnected, reason=%d", event->reason);
                _status = WL_DISCONNECTED;
                // 尝试重连
                esp_wifi_connect();
                break;
            }

            case WIFI_EVENT_AP_START:
                ESP_LOGI(TAG, "AP started");
                _apMode = true;
                break;

            case WIFI_EVENT_AP_STOP:
                ESP_LOGI(TAG, "AP stopped");
                _apMode = false;
                break;

            case WIFI_EVENT_SCAN_DONE:
                ESP_LOGI(TAG, "Scan completed");
                break;

            default:
                break;
        }
    } else if (eventBase == IP_EVENT) {
        switch (eventId) {
            case IP_EVENT_STA_GOT_IP: {
                ip_event_got_ip_t* event = (ip_event_got_ip_t*)eventData;
                ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
                _status = WL_CONNECTED;
                break;
            }

            case IP_EVENT_STA_LOST_IP:
                ESP_LOGW(TAG, "Lost IP");
                _status = WL_DISCONNECTED;
                break;

            default:
                break;
        }
    }
}

// 初始化WiFi
bool WiFiClass::init() {
    if (_initialized) return true;

    ESP_LOGI(TAG, "Initializing WiFi...");

    // 初始化网络接口
    esp_netif_init();

    // 创建默认事件循环
    esp_event_loop_create_default();

    // 创建默认网络接口
    _staNetif = esp_netif_create_default_wifi_sta();
    _apNetif = esp_netif_create_default_wifi_ap();

    // 初始化WiFi
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_err_t err = esp_wifi_init(&cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "WiFi init failed: %s", esp_err_to_name(err));
        return false;
    }

    // 注册事件处理
    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                        &_eventHandler, nullptr, nullptr);
    esp_event_handler_instance_register(IP_EVENT, ESP_EVENT_ANY_ID,
                                        &_eventHandler, nullptr, nullptr);

    // 启动WiFi
    err = esp_wifi_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "WiFi start failed: %s", esp_err_to_name(err));
        return false;
    }

    _initialized = true;
    ESP_LOGI(TAG, "WiFi initialized successfully");
    return true;
}

// 连接到WiFi
int WiFiClass::begin(const char* ssid, const char* password) {
    if (!_initialized) {
        if (!init()) return WL_CONNECT_FAILED;
    }

    ESP_LOGI(TAG, "Connecting to: %s", ssid);

    // 设置为STA模式
    esp_wifi_set_mode(WIFI_MODE_STA);

    // 配置连接参数
    wifi_config_t wifiConfig = {};
    strlcpy((char*)wifiConfig.sta.ssid, ssid, sizeof(wifiConfig.sta.ssid));
    if (password) {
        strlcpy((char*)wifiConfig.sta.password, password,
                sizeof(wifiConfig.sta.password));
    }

    esp_wifi_set_config(WIFI_IF_STA, &wifiConfig);

    // 保存SSID
    strlcpy(_connectedSSID, ssid, sizeof(_connectedSSID));

    // 开始连接
    _status = WL_IDLE_STATUS;
    esp_err_t err = esp_wifi_connect();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Connect failed: %s", esp_err_to_name(err));
        return WL_CONNECT_FAILED;
    }

    return WL_IDLE_STATUS;
}

// 断开连接
void WiFiClass::disconnect(bool wifiOff) {
    ESP_LOGI(TAG, "Disconnecting...");
    esp_wifi_disconnect();
    _status = WL_DISCONNECTED;
    _connectedSSID[0] = '\0';

    if (wifiOff) {
        esp_wifi_stop();
        _initialized = false;
    }
}

// 启动软AP
bool WiFiClass::softAP(const char* ssid, const char* password) {
    if (!_initialized) {
        if (!init()) return false;
    }

    ESP_LOGI(TAG, "Starting AP: %s", ssid);

    // 设置为AP模式
    esp_wifi_set_mode(WIFI_MODE_AP);

    // 配置AP参数
    wifi_config_t wifiConfig = {};
    strlcpy((char*)wifiConfig.ap.ssid, ssid, sizeof(wifiConfig.ap.ssid));
    wifiConfig.ap.ssid_len = strlen(ssid);
    wifiConfig.ap.max_connection = 4;

    if (password) {
        strlcpy((char*)wifiConfig.ap.password, password,
                sizeof(wifiConfig.ap.password));
        wifiConfig.ap.authmode = WIFI_AUTH_WPA2_PSK;
    } else {
        wifiConfig.ap.authmode = WIFI_AUTH_OPEN;
    }

    esp_wifi_set_config(WIFI_IF_AP, &wifiConfig);

    // 启动WiFi
    esp_err_t err = esp_wifi_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "AP start failed: %s", esp_err_to_name(err));
        return false;
    }

    _apMode = true;
    return true;
}

// 获取连接状态
int WiFiClass::status() {
    return _status;
}

// 获取本地IP
IPAddress WiFiClass::localIP() {
    if (!_staNetif) return IPAddress(0, 0, 0, 0);

    esp_netif_ip_info_t ipInfo;
    if (esp_netif_get_ip_info(_staNetif, &ipInfo) != ESP_OK) {
        return IPAddress(0, 0, 0, 0);
    }

    return IPAddress(ipInfo.ip.addr);
}

// 获取软AP的IP
IPAddress WiFiClass::softAPIP() {
    if (!_apNetif) return IPAddress(192, 168, 4, 1);

    esp_netif_ip_info_t ipInfo;
    if (esp_netif_get_ip_info(_apNetif, &ipInfo) != ESP_OK) {
        return IPAddress(192, 168, 4, 1);
    }

    return IPAddress(ipInfo.ip.addr);
}

// 获取SSID
String WiFiClass::SSID() {
    return String(_connectedSSID);
}

// 获取RSSI
int WiFiClass::RSSI() {
    wifi_ap_record_t apInfo;
    if (esp_wifi_sta_get_ap_info(&apInfo) != ESP_OK) {
        return 0;
    }
    return apInfo.rssi;
}

// 获取MAC地址
String WiFiClass::macAddress() {
    uint8_t mac[6];
    esp_wifi_get_mac(WIFI_IF_STA, mac);
    char buf[18];
    snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return String(buf);
}

// 设置睡眠模式
void WiFiClass::setSleep(bool enable) {
    if (enable) {
        esp_wifi_set_ps(WIFI_PS_MODEM);
    } else {
        esp_wifi_set_ps(WIFI_PS_NONE);
    }
}

// 设置发射功率
bool WiFiClass::setTxPower(int power) {
    return esp_wifi_set_max_tx_power(power * 4) == ESP_OK;  // ESP-IDF使用0.25dBm单位
}

// 设置自动重连
void WiFiClass::setAutoReconnect(bool autoReconnect) {
    // ESP-IDF默认会自动重连，这里不需要特别处理
    (void)autoReconnect;
}

// 设置持久化
void WiFiClass::persistent(bool persistent) {
    // ESP-IDF默认使用NVS存储配置
    (void)persistent;
}

// 设置WiFi模式
void WiFiClass::setMode(wifi_mode_t mode) {
    esp_wifi_set_mode(mode);
}

// 扫描网络
int WiFiClass::scanNetworks() {
    if (!_initialized) {
        if (!init()) return 0;
    }

    // 释放之前的扫描结果
    if (_scanResults) {
        free(_scanResults);
        _scanResults = nullptr;
    }
    _scanCount = 0;

    // 开始扫描
    wifi_scan_config_t scanConfig = {};
    scanConfig.show_hidden = true;

    esp_err_t err = esp_wifi_scan_start(&scanConfig, true);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Scan failed: %s", esp_err_to_name(err));
        return 0;
    }

    // 获取扫描结果数量
    uint16_t count = 0;
    esp_wifi_scan_get_ap_num(&count);
    _scanCount = count;

    if (count == 0) return 0;

    // 分配内存存储结果
    wifi_ap_record_t* apRecords = (wifi_ap_record_t*)malloc(count * sizeof(wifi_ap_record_t));
    if (!apRecords) {
        ESP_LOGE(TAG, "Failed to allocate memory for scan results");
        return 0;
    }

    // 获取扫描结果
    esp_wifi_scan_get_ap_records(&count, apRecords);

    // 转换为我们的格式
    _scanResults = (WiFiScanResult*)malloc(count * sizeof(WiFiScanResult));
    if (!_scanResults) {
        free(apRecords);
        return 0;
    }

    for (int i = 0; i < count; i++) {
        strlcpy(_scanResults[i].ssid, (const char*)apRecords[i].ssid,
                sizeof(_scanResults[i].ssid));
        _scanResults[i].rssi = apRecords[i].rssi;
        _scanResults[i].encryptionType = apRecords[i].authmode;
    }

    free(apRecords);
    return _scanCount;
}

// 获取扫描到的SSID
String WiFiClass::SSID(int index) {
    if (index < 0 || index >= _scanCount || !_scanResults) {
        return String("");
    }
    return String(_scanResults[index].ssid);
}

// 获取扫描到的RSSI
int WiFiClass::RSSI(int index) {
    if (index < 0 || index >= _scanCount || !_scanResults) {
        return 0;
    }
    return _scanResults[index].rssi;
}

// 获取加密类型
int WiFiClass::encryptionType(int index) {
    if (index < 0 || index >= _scanCount || !_scanResults) {
        return WIFI_AUTH_OPEN;
    }
    return _scanResults[index].encryptionType;
}

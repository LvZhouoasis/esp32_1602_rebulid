#ifndef OTA_ESP32_H
#define OTA_ESP32_H

#include <cstdint>
#include <cstddef>
#include "esp_ota_ops.h"

/**
 * @brief OTA 升级封装类，兼容 Arduino Update API
 *
 * 使用 ESP-IDF esp_ota_ops 实现
 */
class OTAUpdate {
public:
    OTAUpdate();
    ~OTAUpdate();

    /**
     * @brief 开始 OTA 升级
     * @param size 固件大小（如果未知，使用 OTA_SIZE_UNKNOWN）
     * @return true 成功，false 失败
     */
    bool begin(size_t size = OTA_SIZE_UNKNOWN);

    /**
     * @brief 写入固件数据
     * @param data 数据指针
     * @param length 数据长度
     * @return 实际写入的字节数，-1 表示错误
     */
    size_t write(const uint8_t* data, size_t length);

    /**
     * @brief 结束 OTA 升级
     * @param restart 是否立即重启
     * @return true 成功，false 失败
     */
    bool end(bool restart = true);

    /**
     * @brief 中止 OTA 升级
     */
    void abort();

    /**
     * @brief 获取错误代码
     * @return 错误代码
     */
    int getError() const;

    /**
     * @brief 获取错误信息字符串
     * @return 错误信息
     */
    const char* errorString() const;

    /**
     * @brief 获取已写入的字节数
     * @return 已写入字节数
     */
    size_t written() const { return _written; }

    /**
     * @brief 检查 OTA 是否已开始
     * @return true 已开始，false 未开始
     */
    bool isRunning() const { return _running; }

private:
    esp_ota_handle_t _handle;
    const esp_partition_t* _partition;
    size_t _size;
    size_t _written;
    bool _running;
    esp_err_t _lastError;

    void cleanup();
};

#endif // OTA_ESP32_H

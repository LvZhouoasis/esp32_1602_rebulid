#include "./services/ota_esp32.h"
#include <cstring>
#include "esp_log.h"

static const char* TAG = "OTA_ESP32";

OTAUpdate::OTAUpdate()
    : _handle(0)
    , _partition(nullptr)
    , _size(0)
    , _written(0)
    , _running(false)
    , _lastError(ESP_OK) {
}

OTAUpdate::~OTAUpdate() {
    if (_running) {
        abort();
    }
}

bool OTAUpdate::begin(size_t size) {
    if (_running) {
        abort();
    }

    _size = size;
    _written = 0;
    _lastError = ESP_OK;

    // 获取下一个可用的 OTA 分区
    _partition = esp_ota_get_next_update_partition(nullptr);
    if (_partition == nullptr) {
        ESP_LOGE(TAG, "No OTA partition found");
        _lastError = ESP_ERR_NOT_FOUND;
        return false;
    }

    ESP_LOGI(TAG, "OTA partition: %s, offset: 0x%lx, size: 0x%lx",
             _partition->label,
             static_cast<unsigned long>(_partition->address),
             static_cast<unsigned long>(_partition->size));

    // 开始 OTA
    _lastError = esp_ota_begin(_partition, size, &_handle);
    if (_lastError != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin failed: %d", _lastError);
        return false;
    }

    _running = true;
    ESP_LOGI(TAG, "OTA started, size: %zu", size);
    return true;
}

size_t OTAUpdate::write(const uint8_t* data, size_t length) {
    if (!_running) {
        ESP_LOGE(TAG, "OTA not started");
        return -1;
    }

    _lastError = esp_ota_write(_handle, data, length);
    if (_lastError != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_write failed: %d", _lastError);
        return -1;
    }

    _written += length;

    // 定期更新进度日志
    if (_size > 0 && (_written % 4096 == 0 || _written == _size)) {
        int progress = (_written * 100) / _size;
        ESP_LOGI(TAG, "OTA progress: %d%% (%zu/%zu)", progress, _written, _size);
    }

    return length;
}

bool OTAUpdate::end(bool restart) {
    if (!_running) {
        ESP_LOGE(TAG, "OTA not started");
        return false;
    }

    // 验证写入完整性
    if (_size > 0 && _written != _size) {
        ESP_LOGE(TAG, "OTA incomplete: %zu/%zu", _written, _size);
        abort();
        return false;
    }

    // 结束 OTA
    _lastError = esp_ota_end(_handle);
    if (_lastError != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_end failed: %d", _lastError);
        cleanup();
        return false;
    }

    // 设置启动分区
    _lastError = esp_ota_set_boot_partition(_partition);
    if (_lastError != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition failed: %d", _lastError);
        cleanup();
        return false;
    }

    _running = false;
    ESP_LOGI(TAG, "OTA completed successfully, written: %zu bytes", _written);

    // 重启
    if (restart) {
        ESP_LOGI(TAG, "Rebooting...");
        esp_restart();
    }

    return true;
}

void OTAUpdate::abort() {
    if (_running) {
        esp_ota_abort(_handle);
        ESP_LOGW(TAG, "OTA aborted");
    }
    cleanup();
}

int OTAUpdate::getError() const {
    return static_cast<int>(_lastError);
}

const char* OTAUpdate::errorString() const {
    switch (_lastError) {
        case ESP_OK:
            return "Success";
        case ESP_ERR_INVALID_STATE:
            return "Invalid state";
        case ESP_ERR_INVALID_ARG:
            return "Invalid argument";
        case ESP_ERR_NOT_FOUND:
            return "Partition not found";
        case ESP_ERR_FLASH_OP_TIMEOUT:
            return "Flash operation timeout";
        case ESP_ERR_FLASH_OP_FAIL:
            return "Flash operation failed";
        case ESP_ERR_OTA_VALIDATE_FAILED:
            return "OTA validation failed";
        default:
            return "Unknown error";
    }
}

void OTAUpdate::cleanup() {
    _handle = 0;
    _partition = nullptr;
    _running = false;
}

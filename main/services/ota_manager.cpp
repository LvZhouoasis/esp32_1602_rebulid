#include "services/ota_manager.h"

namespace {
int g_otaProgress = 0;
char g_otaLastError[128] = "";
volatile OTAStatus g_otaCurrentStatus = OTA_IDLE;
volatile OTAResult g_otaCurrentResult = OTA_IN_PROGRESS;

bool otaDownloadFirmware(HTTPClient& http, size_t contentLength) {
    WiFiClient* stream = http.getStreamPtr();       //使用传入的http客户端获取流
    
    if (!stream) {
        strlcpy(g_otaLastError, "Stream pointer is null", sizeof(g_otaLastError));
        LOG_SYSTEM_ERROR("OTA: %s", g_otaLastError);
        return false;
    }
    
    uint8_t buff[512];
    size_t written = 0;
    int lastDisplayedProgress = -1;
    uint32_t lastProgressTime = GET_MS();
    const uint32_t PROGRESS_UPDATE_INTERVAL = 50;
    
    while (http.connected() && written < contentLength) {
        size_t available = stream->available();
        if (available) {
            // 写入流数据到缓冲区
            int currentSize = stream->readBytes(buff, min(available, sizeof(buff)));
            
            if (currentSize <= 0) {       // 未找到有效数据，等待流
                vTaskDelay(1);
                continue;
            }
            
            // 写入缓冲区数据到Flash
            if (Update.write(buff, currentSize) != currentSize) {
                snprintf(g_otaLastError, sizeof(g_otaLastError), "Write failed at %zu", written);
                LOG_SYSTEM_ERROR("OTA write error: %s", g_otaLastError);
                return false;
            }
            
            written += currentSize;
            
            // 定期更新进度显示
            uint32_t now = GET_MS();
            if (now - lastProgressTime >= PROGRESS_UPDATE_INTERVAL) {
                g_otaProgress = (written * 100) / contentLength;
                if (g_otaProgress != lastDisplayedProgress) {
                    LOG_SYSTEM_DEBUG("OTA Progress: %d%% (%d/%d bytes)", 
                                    g_otaProgress, written, contentLength);
                    char lcdBuf[17];
                    snprintf(lcdBuf, sizeof(lcdBuf), "Updating: %d%%", g_otaProgress);
                    lcdText(lcdBuf, 1);
                    snprintf(lcdBuf, sizeof(lcdBuf), "%zu/%zu KB", written/1024, contentLength/1024);
                    lcdText(lcdBuf, 2);
                    lastDisplayedProgress = g_otaProgress;
                }
                lastProgressTime = now;
            }
        } else {
            vTaskDelay(1);
        }
    }
    
    // 验证下载完整性
    if (written != contentLength) {
        snprintf(g_otaLastError, sizeof(g_otaLastError), "Download incomplete: %zu/%zu", written, contentLength);
        LOG_SYSTEM_ERROR("OTA: %s", g_otaLastError);
        lcdText("OTA Failed!", 1);
        lcdText("Incomplete DL", 2);
        return false;
    }
    
    LOG_SYSTEM_INFO("OTA firmware download complete: %d bytes", written);
    g_otaProgress = 100;
    return true;
}
}

void otaInit() {
    LOG_SYSTEM_INFO("OTA Manager initialized");
    g_otaCurrentStatus = OTA_IDLE;
    g_otaCurrentResult = OTA_IN_PROGRESS;
}

OTAResult otaUpdateFromURL(const char* url, bool useHTTPS) {
    g_otaProgress = 0;
    g_otaLastError[0] = '\0';
    g_otaCurrentStatus = OTA_RUNNING;
    g_otaCurrentResult = OTA_IN_PROGRESS;
    
    WiFiClient* client;
    WiFiClientSecure secureClient;
    
    if (useHTTPS) {
        secureClient.setInsecure(); // 跳过证书验证,或使用setCACert()
        client = &secureClient;
    } else {
        static WiFiClient normalClient;
        client = &normalClient;
    }
    
    HTTPClient http;
    http.begin(*client, url);
    
    LOG_SYSTEM_INFO("Starting OTA from URL: %s", url);
    lcdText("OTA Starting...", 1);
    lcdText("Connecting...", 2);
    updateColor(CRGB::Orange);
    
    int httpCode = http.GET();
    
    if (httpCode != HTTP_CODE_OK) {
        snprintf(g_otaLastError, sizeof(g_otaLastError), "HTTP Error: %d", httpCode);
        LOG_SYSTEM_ERROR("OTA HTTP failed: %d", httpCode);
        http.end();
        lcdText("OTA Failed!", 1);
        lcdText("HTTP Error", 2);
        updateColor(CRGB::Red);
        g_otaCurrentStatus = OTA_COMPLETED_FAILED;
        g_otaCurrentResult = OTA_FAIL_DOWNLOAD;
        return OTA_FAIL_DOWNLOAD;
    }
    
    size_t contentLength = http.getSize();
    if (contentLength == 0) {
        strlcpy(g_otaLastError, "Content-Length is 0", sizeof(g_otaLastError));
        LOG_SYSTEM_ERROR("OTA: Invalid content length");
        lcdText("OTA Failed!", 1);
        lcdText("No Content", 2);
        http.end();
        g_otaCurrentStatus = OTA_COMPLETED_FAILED;
        g_otaCurrentResult = OTA_FAIL_DOWNLOAD;
        return OTA_FAIL_DOWNLOAD;
    }
    
    LOG_SYSTEM_INFO("Firmware size: %d bytes", contentLength);
    lcdText("Downloading...", 1);
    char lcdBuf[17];
    snprintf(lcdBuf, sizeof(lcdBuf), "Size: %zu B", contentLength);
    lcdText(lcdBuf, 2);
    
    if (!Update.begin(contentLength)) {
        snprintf(g_otaLastError, sizeof(g_otaLastError), "Not enough space: %s", Update.errorString());
        LOG_SYSTEM_ERROR("OTA begin failed: %s", g_otaLastError);
        lcdText("OTA Failed!", 1);
        lcdText("No Space", 2);
        http.end();
        g_otaCurrentStatus = OTA_COMPLETED_FAILED;
        g_otaCurrentResult = OTA_FAIL_WRITE;
        return OTA_FAIL_WRITE;
    }
    
    // 下载并写入固件
    bool result = otaDownloadFirmware(http, contentLength);
    http.end();
    
    if (!result) {
        LOG_SYSTEM_ERROR("OTA firmware download failed: %s", g_otaLastError);
        Update.abort();
        lcdText("OTA Failed!", 1);
        lcdText("Download Error", 2);
        updateColor(CRGB::Red);
        g_otaCurrentStatus = OTA_COMPLETED_FAILED;
        g_otaCurrentResult = OTA_FAIL_WRITE;
        return OTA_FAIL_WRITE;
    }
    
    if (Update.end(false)) {  // false = 不立即重启,先返回状态
        LOG_SYSTEM_INFO("OTA Update Success! Rebooting...");
        lcdText("OTA Success!", 1);
        lcdText("Rebooting...", 2);
        updateColor(CRGB::Green);
        g_otaProgress = 100;
        g_otaCurrentStatus = OTA_COMPLETED_SUCCESS;
        g_otaCurrentResult = OTA_SUCCESS;
        WAIT_MS(2000);
        esp_restart();
        return OTA_SUCCESS;
    } else {
        snprintf(g_otaLastError, sizeof(g_otaLastError), "%d: %s", Update.getError(), Update.errorString());
        LOG_SYSTEM_ERROR("OTA Update failed: %s", g_otaLastError);
        Update.abort();
        lcdText("OTA Failed!", 1);
        lcdText(g_otaLastError, 2);  // lcdText handles up to 16 chars
        updateColor(CRGB::Red);
        g_otaCurrentStatus = OTA_COMPLETED_FAILED;
        g_otaCurrentResult = OTA_FAIL_WRITE;
        return OTA_FAIL_WRITE;
    }
}

OTAResult otaUpdateFromFile(uint8_t* data, size_t length) {
    (void)data;
    (void)length;
    strlcpy(g_otaLastError, "OTA from file not implemented", sizeof(g_otaLastError));
    g_otaCurrentStatus = OTA_COMPLETED_FAILED;
    g_otaCurrentResult = OTA_FAIL_WRITE;
    return OTA_FAIL_WRITE;
}

int otaGetProgress() {
    LOG_SYSTEM_DEBUG("Got OTA Progress: %d%%", g_otaProgress);
    return g_otaProgress;
}

const char* otaGetErrorString() {
    return g_otaLastError;
}

OTAStatus otaGetStatus() {
    return g_otaCurrentStatus;
}

bool otaIsInProgress() {
    return g_otaCurrentStatus == OTA_RUNNING;
}

void otaCheckForUpdate(const char* versionCheckURL) {
    // 可选: 实现版本检查逻辑
    // 从服务器获取最新版本号并比较
    (void)versionCheckURL;
}
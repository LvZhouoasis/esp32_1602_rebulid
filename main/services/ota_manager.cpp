#include "services/ota_manager.h"

namespace {
int g_otaProgress = 0;
char g_otaLastError[128] = "";
volatile OTAStatus g_otaCurrentStatus = OTA_IDLE;
volatile OTAResult g_otaCurrentResult = OTA_IN_PROGRESS;
OTAUpdate otaUpdate;  // 使用新的 OTAUpdate 类
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

    LOG_SYSTEM_INFO("Starting OTA from URL: %s", url);
    lcdText("OTA Starting...", 1);
    lcdText("Connecting...", 2);
    updateColor(CRGB::Orange);

    // 使用 HttpClientWrapper 下载固件
    HttpClientWrapper http;
    http.begin(url);

    int httpCode = http.GET();

    if (httpCode != 200) {
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

    // 开始 OTA
    if (!otaUpdate.begin(contentLength)) {
        snprintf(g_otaLastError, sizeof(g_otaLastError), "OTA begin failed: %s", otaUpdate.errorString());
        LOG_SYSTEM_ERROR("OTA begin failed: %s", g_otaLastError);
        lcdText("OTA Failed!", 1);
        lcdText("No Space", 2);
        http.end();
        g_otaCurrentStatus = OTA_COMPLETED_FAILED;
        g_otaCurrentResult = OTA_FAIL_WRITE;
        return OTA_FAIL_WRITE;
    }

    // 获取响应数据并写入 OTA
    String response = http.getString();
    http.end();

    if (response.length() == 0) {
        strlcpy(g_otaLastError, "No data received", sizeof(g_otaLastError));
        LOG_SYSTEM_ERROR("OTA: No data received");
        otaUpdate.abort();
        lcdText("OTA Failed!", 1);
        lcdText("No Data", 2);
        updateColor(CRGB::Red);
        g_otaCurrentStatus = OTA_COMPLETED_FAILED;
        g_otaCurrentResult = OTA_FAIL_DOWNLOAD;
        return OTA_FAIL_DOWNLOAD;
    }

    // 写入固件数据
    size_t written = otaUpdate.write((const uint8_t*)response.c_str(), response.length());
    if (written != response.length()) {
        snprintf(g_otaLastError, sizeof(g_otaLastError), "Write failed: %s", otaUpdate.errorString());
        LOG_SYSTEM_ERROR("OTA write error: %s", g_otaLastError);
        otaUpdate.abort();
        lcdText("OTA Failed!", 1);
        lcdText("Write Error", 2);
        updateColor(CRGB::Red);
        g_otaCurrentStatus = OTA_COMPLETED_FAILED;
        g_otaCurrentResult = OTA_FAIL_WRITE;
        return OTA_FAIL_WRITE;
    }

    // 更新进度
    g_otaProgress = 100;
    lcdText("Updating: 100%", 1);
    lcdText("Complete", 2);

    // 结束 OTA（不立即重启）
    if (otaUpdate.end(false)) {
        LOG_SYSTEM_INFO("OTA Update Success! Rebooting...");
        lcdText("OTA Success!", 1);
        lcdText("Rebooting...", 2);
        updateColor(CRGB::Green);
        g_otaCurrentStatus = OTA_COMPLETED_SUCCESS;
        g_otaCurrentResult = OTA_SUCCESS;
        WAIT_MS(2000);
        esp_restart();
        return OTA_SUCCESS;
    } else {
        snprintf(g_otaLastError, sizeof(g_otaLastError), "OTA end failed: %s", otaUpdate.errorString());
        LOG_SYSTEM_ERROR("OTA Update failed: %s", g_otaLastError);
        otaUpdate.abort();
        lcdText("OTA Failed!", 1);
        lcdText(g_otaLastError, 2);
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
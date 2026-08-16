#include "./services/web_setting.h"

#include "./hardware/opt3001.h"
#include "./hardware/fuel_gauge.h"
#include "./services/auto_brightness.h"
#include "./services/config_manager.h"
#include "./hardware/buzzer.h"
#include "./applications/weather.h"
#include "./connectivity/wifi_esp32.h"
#include "./connectivity/http_server_wrapper.h"
#include <cstring>
#include "esp_system.h"
#include "esp_clk_tree.h"

extern QWeatherAuthConfigManager qweatherAuthConfigManager;

HttpServer settingServer(80);
volatile bool isConfigDone = false;
volatile bool isKeyDone = false;
volatile bool otaUploadSuccess = false;
static size_t otaExpectedSize = 0;      // 预期的OTA文件大小
static bool s_otaModuleInitialized = false;

static const char* _wifiStateToStr(WiFiConnectionState state) {
    switch (state) {
        case WIFI_IDLE: return "idle";
        case WIFI_CONNECTING: return "connecting";
        case WIFI_CONNECTED: return "connected";
        case WIFI_DISCONNECTED: return "disconnected";
        case WIFI_FAILED: return "failed";
        default: return "unknown";
    }
}

void webSettingHandleDeviceStatus() {
    char json[128];
    snprintf(json, sizeof(json), "{\"brightness\":%d,\"autoBrightness\":%s,\"soundEffects\":%s}",
        brightness, isAutoBrightnessActive() ? "true" : "false", buzzerIsUiSoundEnabled() ? "true" : "false");
    settingServer.send(200, "application/json; charset=utf-8", json);
}

void webSettingHandleCitySearchReady() {
    const bool ready = qweatherAuthConfigManager.checkApiConfigValid();
    char json[192];
    snprintf(json, sizeof(json), "{\"ready\":%s,\"message\":\"%s\"}",
        ready ? "true" : "false", ready ? "配置完整，可进行城市搜索" : "和风天气密钥未完整配置，无法搜索");
    settingServer.send(200, "application/json; charset=utf-8", json);
}

void webSettingHandleDeviceBasicInfo() {
    const unsigned long uptimeMs = GET_MS();
    const size_t freeHeap = esp_get_free_heap_size();
    const size_t totalHeap = heap_caps_get_total_size(MALLOC_CAP_DEFAULT);
    const size_t freePsram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    const size_t totalPsram = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);

    char json[384];
    snprintf(json, sizeof(json),
        "{\"projectVersion\":\"%s\",\"buildVersion\":\"%s\",\"buildTimestamp\":\"%s\","
        "\"uptimeMs\":%lu,\"cpuFreqMHz\":%u,\"freeHeap\":%u,\"totalHeap\":%u,"
        "\"freePsram\":%u,\"totalPsram\":%u,\"resetReason\":%d}",
        PROJECT_VERSION, BUILD_VERSION, BUILD_TIMESTAMP,
        uptimeMs, (unsigned)(esp_clk_cpu_freq() / 1000000), freeHeap, totalHeap,
        freePsram, totalPsram, (int)esp_reset_reason());

    settingServer.send(200, "application/json; charset=utf-8", json);
}

void webSettingHandleAlsRealtimeInfo() {
    const float lux = isOPT3001Connected ? readLux() : -1.0f;
    const float smoothedLux = getCurrentLux(true);

    char json[128];
    snprintf(json, sizeof(json), "{\"connected\":%s,\"lux\":%.2f,\"smoothedLux\":%.2f}",
        isOPT3001Connected ? "true" : "false", lux, smoothedLux);

    settingServer.send(200, "application/json; charset=utf-8", json);
}

void webSettingHandleFuelGaugeRealtimeInfo() {
    const uint16_t voltage = readVoltage();
    const int16_t current = readAverageCurrent();
    const uint8_t soc = readStateOfCharge();

    char json[128];
    snprintf(json, sizeof(json), "{\"connected\":%s,\"voltageMv\":%u,\"currentMa\":%d,\"soc\":%u}",
        isfuelICConnected ? "true" : "false", voltage, current, soc);

    settingServer.send(200, "application/json; charset=utf-8", json);
}

void webSettingHandleWifiInfo() {
    char json[256];
    snprintf(json, sizeof(json),
        "{\"state\":\"%s\",\"wlStatus\":%d,\"ssid\":\"%s\",\"ip\":\"%s\",\"rssi\":%d,\"mac\":\"%s\"}",
        _wifiStateToStr(wifiConnectionState), (int)WiFi.status(),
        WiFi.SSID().c_str(), WiFi.localIP().toString().c_str(),
        WiFi.RSSI(), WiFi.macAddress().c_str());

    settingServer.send(200, "application/json; charset=utf-8", json);
}

void webSettingHandleSetBrightness() {
    if (settingServer.method() != HTTP_POST) {
        settingServer.send(405, "application/json; charset=utf-8", "{\"error\":\"请使用 POST 方法\"}");
        return;
    }

    if (!settingServer.hasArg("value")) {
        settingServer.send(400, "application/json; charset=utf-8", "{\"error\":\"缺少 value 参数\"}");
        return;
    }

    int value = atoi(settingServer.arg("value").c_str());
    if (value < 0) value = 0;
    if (value > 255) value = 255;

    disableAutoBrightness();
    setLcdBrightness((uint8_t)value);
    ConfigManager::saveAutoBrightnessEnabled(false);

    char json[96];
    snprintf(json, sizeof(json), "{\"ok\":true,\"brightness\":%d,\"autoBrightness\":false}", brightness);
    settingServer.send(200, "application/json; charset=utf-8", json);
}

void webSettingHandleToggleAutoBrightness() {
    if (settingServer.method() != HTTP_POST) {
        settingServer.send(405, "application/json; charset=utf-8", "{\"error\":\"请使用 POST 方法\"}");
        return;
    }

    const bool enabled = toggleAutoBrightness();
    ConfigManager::saveAutoBrightnessEnabled(enabled);

    char json[96];
    snprintf(json, sizeof(json), "{\"ok\":true,\"autoBrightness\":%s,\"brightness\":%d}",
        enabled ? "true" : "false", brightness);
    settingServer.send(200, "application/json; charset=utf-8", json);
}

void webSettingHandleToggleSoundEffects() {
    if (settingServer.method() != HTTP_POST) {
        settingServer.send(405, "application/json; charset=utf-8", "{\"error\":\"请使用 POST 方法\"}");
        return;
    }

    const bool enabled = !buzzerIsUiSoundEnabled();
    buzzerSetUiSoundEnabled(enabled);
    ConfigManager::saveSoundEffectsEnabled(enabled);
    if (enabled) {
        buzzerPlaySelectSound();
    }

    char json[64];
    snprintf(json, sizeof(json), "{\"ok\":true,\"soundEffects\":%s}", enabled ? "true" : "false");
    settingServer.send(200, "application/json; charset=utf-8", json);
}

// OTA页面处理
void webSettingHandleOTA() {
    // 计算总长度
    unsigned int len1 = strlen_P(webComponent);
    unsigned int len2 = strlen_P(ota_html);
    unsigned int totalLen = len1 + len2;

    // 设置 Content-Length 并发送头（空 body）
    settingServer.setContentLength(totalLen);
    settingServer.send(200, "text/html; charset=utf-8", "");

    // 直接发送 PROGMEM 内容块
    settingServer.sendContent_P(webComponent, len1);
    settingServer.sendContent_P(ota_html, len2);
}

// OTA URL处理
void webSettingHandleOTAURL() {
    // 只处理GET请求
    if (settingServer.method() != HTTP_GET) {
        settingServer.send(405, "application/json; charset=utf-8", "{\"error\":\"请使用 GET 方法上传 URL\"}");
        LOG_WEB_WARN("Received non-GET request for OTA URL");
        return;
    }

    // 如果发送的请求不含URL那么 HTTP400 Bad Request
    if (!settingServer.hasArg("url")) {
        settingServer.send(400, "application/json", "{\"success\":false,\"error\":\"请提供 URL\"}");
        return;
    }
    
    // 如果已有 OTA 在进行那么 HTTP409 Conflict
    if (otaIsInProgress()) {
        settingServer.send(409, "application/json", 
            "{\"success\":false,\"error\":\"当前正在进行 OTA\"}");
        return;
    }
    
    const char* url = settingServer.arg("url").c_str();
    LOG_SYSTEM_INFO("OTA from URL: %s", url);

    // 先响应前端,告诉它 OTA 已开始 (HTTP 202 Accepted)
    settingServer.send(202, "application/json",
        "{\"success\":true,\"message\":\"OTA started\"}");

    // 创建独立任务执行 OTA - 复制URL到堆上
    char* urlCopy = strdup(url);
    xTaskCreate([](void* param) {
        char* urlStr = (char*)param;
        bool useHTTPS = (strncmp(urlStr, "https://", 8) == 0);
        OTAResult result = otaUpdateFromURL(urlStr, useHTTPS);
        if (result != OTA_SUCCESS) {
            LOG_SYSTEM_ERROR("OTA failed: %s", otaGetErrorString());
        }
        free(urlStr);
        vTaskDelete(NULL);
    }, "OTA_Task", 8192, urlCopy, 5, NULL);
}

// OTA进度查询
void webSettingHandleOTAProgress() {
    int progress = otaGetProgress();
    OTAStatus status = otaGetStatus();
    const char* statusStr;

    switch(status) {
        case OTA_IDLE: statusStr = "\"idle\""; break;
        case OTA_RUNNING: statusStr = "\"in_progress\""; break;
        case OTA_COMPLETED_SUCCESS: statusStr = "\"success\""; break;
        case OTA_COMPLETED_FAILED: statusStr = "\"failed\""; break;
        default: statusStr = "\"unknown\""; break;
    }

    char json[256];
    const char* errorStr = otaGetErrorString();

    // 有错误信息时提示客户端
    if(strlen(errorStr) > 0){
        snprintf(json, sizeof(json), "{\"progress\":\"0\",\"status\":\"failed\",\"error\":\"%s\"}", errorStr);
        LOG_SYSTEM_DEBUG("OTA Progress queried with error: %s", json);
    }
    // 无错误信息时正常返回进度和状态
    else{
        snprintf(json, sizeof(json), "{\"progress\":%d,\"status\":%s}", progress, statusStr);
        LOG_SYSTEM_DEBUG("OTA Progress queried: %s", json);
    }

    settingServer.send(200, "application/json", json);
}

// OTA文件上传处理
void webSettingHandleOTAUpload() {
    if(settingServer.method() != HTTP_POST) {
        settingServer.send(405, "application/json; charset=utf-8", "{\"error\":\"请使用 POST 方法上传固件\"}");
        LOG_WEB_WARN("Received non-POST request for OTA upload");
        return;
    }

    HTTPUpload& upload = settingServer.upload();

    // 只处理三种合法状态，如果没有文件那么 HTTP400 Bad Request
    if (upload.status != UPLOAD_FILE_START && upload.status != UPLOAD_FILE_WRITE && upload.status != UPLOAD_FILE_END) {
        LOG_WEB_WARN("OTA upload: unexpected upload.status=%d", upload.status);
        settingServer.send(400, "application/json; charset=utf-8", "{\"error\":\"无效的上传状态\"}");
        return;
    }
    
    // 请求上传阶段
    if (upload.status == UPLOAD_FILE_START){
        // 有文件名 => 确认是文件上传
        if (upload.filename && upload.filename.length() > 0) {
            LOG_SYSTEM_INFO("OTA Upload Start: %s", upload.filename.c_str());
            lcdText("Uploading...", 1);
            lcdText(upload.filename.c_str(), 2);
            updateColor(CRGB::Orange);
            otaExpectedSize = 0;  // 重置预期大小
            
            // http上传无法提前获取文件大小，让update库使用未知大小模式
            if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
                LOG_SYSTEM_ERROR("OTA begin failed");
                lcdText("OTA Begin Fail", 1);
                lcdText("", 2);
                char errBuf[128];
                snprintf(errBuf, sizeof(errBuf), "{\"success\":false,\"error\":\"%s\"}", Update.errorString());
                settingServer.send(500, "application/json", errBuf);
                return;
            }
        }
    }

    // 分片上传阶段
    else if (upload.status == UPLOAD_FILE_WRITE) {
        // 如果写入字节不匹配
        if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
            LOG_SYSTEM_ERROR("OTA write failed");
            Update.abort();  // abort 回滚
            settingServer.send(500, "application/json", 
                "{\"success\":false,\"error\":\"写入失败\"}");
            lcdText("OTA Write Fail", 1);
            lcdText("", 2);
            return;
        }
        
        // 显示已写入的字节数
        static size_t lastReported = 0;
        size_t written = Update.progress();
        // 每100KB显示一次进度
        if (written - lastReported >= 102400 || (written >= 10240 && lastReported == 0)) {
            LOG_SYSTEM_INFO("OTA uploading:%u B (%u KB) written", written, written / 1024);
            lastReported = written;
        }
    }

    // 上传结束阶段
    else if (upload.status == UPLOAD_FILE_END) {
        LOG_SYSTEM_INFO("OTA Upload End: %u bytes (%.2f KB)", upload.totalSize, upload.totalSize / 1024.0);
        otaExpectedSize = upload.totalSize;  // 保存最终大小
        if (Update.end(true)) {
            LOG_SYSTEM_INFO("OTA Success! Firmware size: %u", upload.totalSize);
            lcdText("OTA Success!", 1);
            lcdText("Rebooting...", 2);
            updateColor(CRGB::Green);
            otaUploadSuccess = true;  // 标记上传成功
            
            // 创建后台重启任务，等待结束响应发送完成
            xTaskCreate([](void*){
                WAIT_MS(2000);
                esp_restart();
            }, "Restart_Task", 2048, NULL, 1, NULL);
        } 
        else {  // 如果结束时出错
            LOG_SYSTEM_ERROR("OTA End failed: %s", Update.errorString());
            otaUploadSuccess = false;
            return;
        }
    }
}

// 主页处理函数
void webSettingHandleRoot() {
    // 计算总长度
    unsigned int len1 = strlen_P(webComponent);
    unsigned int len2 = strlen_P(index_html);
    unsigned int totalLen = len1 + len2;

    // 设置 Content-Length 并发送头（空 body）
    settingServer.setContentLength(totalLen);
    settingServer.send(200, "text/html; charset=utf-8", "");

    // 直接发送 PROGMEM 内容块
    settingServer.sendContent_P(webComponent, len1);
    settingServer.sendContent_P(index_html, len2);
}

// 处理上传的 JWT 配置信息 POST + application/json
void webSettingHandleSet() {
    if (settingServer.method() != HTTP_POST) {
        settingServer.send(405, "application/json; charset=utf-8", "{\"error\":\"不允许的请求方法\"}");
        LOG_WEB_WARN("Received non-POST request for JWT config");
        return;
    }

    if (!settingServer.hasArg("plain")) {
        settingServer.send(400, "application/json; charset=utf-8", "{\"error\":\"请求体为空\"}");
        LOG_WEB_WARN("Received empty request body for JWT config");
        return;
    }

    const char* body = settingServer.arg("plain").c_str();
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, body);
    if (err) {
        settingServer.send(400, "application/json; charset=utf-8", "{\"error\":\"JSON解析失败\"}");
        LOG_WEB_WARN("Failed to parse JSON for JWT config: %s", err.c_str());
        return;
    }
    if (doc.overflowed()) {
        settingServer.send(413, "application/json; charset=utf-8", "{\"error\":\"JSON大小超过限制\"}");
        LOG_WEB_WARN("JSON size exceeded limit for JWT config");
        return;
    }

    char apiHost[64], kid[64], project[64], privateKey[128];
    strlcpy(apiHost, doc["apiHost"] | doc["host"] | "", sizeof(apiHost));
    strlcpy(kid, doc["kID"] | doc["kid"] | "", sizeof(kid));
    strlcpy(project, doc["projectID"] | doc["project"] | "", sizeof(project));
    strlcpy(privateKey, doc["privateKey"] | doc["key"] | "", sizeof(privateKey));

    // 去除首尾空格
    auto trimStr = [](char* str) {
        char* start = str;
        while (*start == ' ') start++;
        if (start != str) memmove(str, start, strlen(start) + 1);
        size_t len = strlen(str);
        while (len > 0 && str[len - 1] == ' ') {
            str[--len] = '\0';
        }
    };
    trimStr(apiHost);
    trimStr(kid);
    trimStr(project);
    trimStr(privateKey);

    // Validate base64 PKCS#8 Ed25519 private key using jwt_auth helper
    if (!validate_base64_ed25519_key(privateKey)) {
        settingServer.send(400, "application/json; charset=utf-8", "{\"error\":\"无效的 privateKey\"}");
        LOG_WEB_WARN("Invalid privateKey base64 PKCS#8");
        return;
    }

    if(!qweatherAuthConfigManager.setAuth(apiHost, kid, project, privateKey)){
        char errBuf[128];
        snprintf(errBuf, sizeof(errBuf), "{\"error\":\"%s\"}", qweatherAuthConfigManager.getLastQWeatherErrorString());
        settingServer.send(500, "application/json; charset=utf-8", errBuf);
        LOG_WEB_ERROR("Failed to save JWT config");
        return;
    }
    loadJwtConfig();

    // 打印到串口DEBUG等级日志
    LOG_WEATHER_DEBUG("==== Configuration received ====");
    LOG_WEATHER_DEBUG("API Host: %s", apiHost);
    LOG_WEATHER_DEBUG("kid: %s", kid);
    LOG_WEATHER_DEBUG("projectID: %s", project);
    LOG_WEATHER_DEBUG("private key length: %zu", strlen(privateKey));

    // 保存成功
    settingServer.send(200, "application/json; charset=utf-8", "{\"ok\":true}");
}

void webSettingHandleGetApiInfo() {
    char json[256];
    snprintf(json, sizeof(json),
        "{\"apiHost\":\"%s\",\"kID\":\"%s\",\"projectID\":\"%s\",\"privateKey\":%s}",
        qweatherAuthConfigManager.getApiHost(), qweatherAuthConfigManager.getKId(),
        qweatherAuthConfigManager.getProjectID(), isKeyDone ? "true" : "false");
    settingServer.send(200, "application/json; charset=utf-8", json);
}

void webSettingHandleFavicon() {
    LOG_WEB_INFO("Favicon requested, serving /favicon.ico, size=%u", favicon_ico_len);
    // Set cache headers so browsers don't repeatedly request the icon
    settingServer.sendHeader("Cache-Control", "public, max-age=86400");
    settingServer.setContentLength(favicon_ico_len);
    settingServer.send(200, "image/x-icon", "");
    settingServer.sendContent_P(reinterpret_cast<const char*>(favicon_ico), favicon_ico_len);
}

void webSettingHandleCitySearch() {
    // 计算总长度
    unsigned int len1 = strlen_P(webComponent);
    unsigned int len2 = strlen_P(city_search_html);
    unsigned int totalLen = len1 + len2;

    // 设置 Content-Length 并发送头（空 body）
    settingServer.setContentLength(totalLen);
    settingServer.send(200, "text/html; charset=utf-8", "");

    // 直接发送 PROGMEM 内容块
    settingServer.sendContent_P(webComponent, len1);
    settingServer.sendContent_P(city_search_html, len2);
}

void fetchCitySearchResult(char* location) {
    // 去除首尾空格
    char* start = location;
    while (*start == ' ') start++;
    if (start != location) memmove(location, start, strlen(start) + 1);
    size_t locLen = strlen(location);
    while (locLen > 0 && location[locLen - 1] == ' ') {
        location[--locLen] = '\0';
    }

    if(!qweatherAuthConfigManager.checkApiConfigValid()){
        LOG_WEATHER_ERROR("API configuration missing for city search");
        settingServer.send(500, "text/html; charset=utf-8", "{\"error\":\"API配置缺失，无法进行城市搜索\"}");
        return;
    }

    const char* varApiHost = qweatherAuthConfigManager.getApiHost();
    const char* varKid = qweatherAuthConfigManager.getKId();
    const char* varProjectID = qweatherAuthConfigManager.getProjectID();

    // 确保先生成seed32
    generateSeed32();

    // 生成JWT
    char jwtToken[512];
    generate_jwt(varKid, varProjectID, seed32, jwtToken, sizeof(jwtToken));
    LOG_WEATHER_DEBUG("City search JWT token generated, length: %zu", strlen(jwtToken));

    if (strlen(varApiHost) == 0 || strlen(jwtToken) == 0) {
        LOG_WEATHER_ERROR("API configuration missing for city search (host or token empty)");
        settingServer.send(500, "text/html; charset=utf-8", "{\"error\":\"API配置缺失，无法进行城市搜索\"}");
        return;
    }

    // 请求城市搜索API
    char url[256];
    snprintf(url, sizeof(url), "https://%s/geo/v2/city/lookup?location=%s&number=10", varApiHost, location);
    LOG_WEATHER_INFO("City search request URL: %s", url);

    HTTPClient http;
    http.begin(url);                            // 让HTTPClient自动处理HTTPS和DNS
    http.addHeader("Accept-Encoding", "gzip");
    char authHeader[560];
    snprintf(authHeader, sizeof(authHeader), "Bearer %s", jwtToken);
    http.addHeader("Authorization", authHeader);
    LOG_WEATHER_DEBUG("City search authorization header set");

    int httpCode = http.GET();
    LOG_WEATHER_INFO("City search HTTP response code: %d", httpCode);
    if (httpCode != 200) {
        http.end();
        char errBuf[96];
        snprintf(errBuf, sizeof(errBuf), "{\"error\":\"请求失败，HTTP代码：%d\"}", httpCode);
        settingServer.send(500, "text/html; charset=utf-8", errBuf);
        return;
    }
    int payloadSize = http.getSize();
    
    // 使用RAII内存管理
    MemoryManager::SafeBuffer compressedBuffer(payloadSize + 8, "CitySearch_Response");
    if (!compressedBuffer.isValid()) {
        http.end();
        settingServer.send(500, "text/html; charset=utf-8", "{\"error\":\"内存分配失败\"}");
        return " ";
    }
    
    WiFiClient *stream = http.getStreamPtr();
    long startMillis = GET_MS();
    int iCount = 0;
    while (iCount < payloadSize && (GET_MS() - startMillis) < 4000) {
        if (stream->available()) {
            compressedBuffer.get()[iCount++] = stream->read();
        } else {
            vTaskDelay(5);
        }
    }
    http.end();
    
    char* jsonData = nullptr;
    size_t jsonDataLen = 0;
    zlib_turbo zt;
    if (iCount >= 2 && compressedBuffer.get()[0] == 0x1f && compressedBuffer.get()[1] == 0x8b) {
        int uncompSize = zt.gzip_info(compressedBuffer.get(), iCount);
        if (uncompSize <= 0) {
            settingServer.send(500, "text/html; charset=utf-8", "{\"error\":\"Gzip解压失败\"}");
            return;
        }

        MemoryManager::SafeBuffer uncompressedBuffer(uncompSize + 8, "CitySearch_Decompressed");
        if (!uncompressedBuffer.isValid()) {
            settingServer.send(500, "text/html; charset=utf-8", "{\"error\":\"内存分配失败\"}");
            return;
        }

        int rc = zt.gunzip(compressedBuffer.get(), iCount, uncompressedBuffer.get());
        if (rc != ZT_SUCCESS) {
            char errBuf[96];
            snprintf(errBuf, sizeof(errBuf), "{\"error\":\"Gzip解压失败，错误代码：%d\"}", rc);
            settingServer.send(500, "text/html; charset=utf-8", errBuf);
            return;
        }
        jsonData = (char*)uncompressedBuffer.get();
        jsonDataLen = uncompSize;
        // uncompressedBuffer 会在作用域结束时自动释放
    } else {
        jsonData = (char*)compressedBuffer.get();
        jsonDataLen = iCount;
    }
    // compressedBuffer 会在作用域结束时自动释放

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, jsonData, jsonDataLen);
    if (error) {
        LOG_WEATHER_ERROR("City search JSON parse failed: %s", error.c_str());
        settingServer.send(500, "text/html; charset=utf-8", "{\"error\":\"JSON解析失败\"}");
        return;
    }

    LOG_WEATHER_DEBUG("City search JSON parsed successfully");

    // 检查API响应状态
    const char* codeStr = doc["code"].as<const char*>("");
    if (strcmp(codeStr, "200") != 0) {
        LOG_WEATHER_WARN("City search API error code: %s", codeStr);
        char errBuf[96];
        snprintf(errBuf, sizeof(errBuf), "{\"error\":\"API错误，错误代码：%s\"}", codeStr);
        settingServer.send(500, "text/html; charset=utf-8", errBuf);
        return;
    }

    // 检查location字段是否存在且为数组
    if (!doc["location"].is<JsonArray>()) {
        LOG_WEATHER_WARN("City search response: location field missing or not array");
        settingServer.send(500, "text/html; charset=utf-8", "{\"error\":\"API响应格式错误，缺少 location 字段\"}");
        return;
    }
    JsonArray locArr = doc["location"].as<JsonArray>();
    LOG_WEATHER_INFO("City search found %u cities", locArr.size());

    if (locArr.size() == 0) {
        settingServer.send(200, "application/json; charset=utf-8", "{\"success\":true,\"results\":[]}");
        return;
    }

    // 发送响应JSON - 使用大缓冲区
    static char jsonBuf[2048];
    size_t pos = 0;
    pos += snprintf(jsonBuf + pos, sizeof(jsonBuf) - pos, "{\"success\":true,\"results\":[");

    for (size_t i = 0; i < locArr.size() && pos < sizeof(jsonBuf) - 1; ++i) {
        JsonObject obj = locArr[i];
        const char* name = obj["name"].as<const char*>("");
        const char* adm1 = obj["adm1"].as<const char*>("");
        const char* country = obj["country"].as<const char*>("");
        const char* locid = obj["id"].as<const char*>("");
        const char* fxlink = obj["fxLink"].as<const char*>("");

        pos += snprintf(jsonBuf + pos, sizeof(jsonBuf) - pos,
            "{\"name\":\"%s\",\"adm1\":\"%s\",\"country\":\"%s\",\"locid\":\"%s\",\"fxlink\":\"%s\"}%s",
            name, adm1, country, locid, fxlink,
            (i != locArr.size() - 1) ? "," : "");
    }

    pos += snprintf(jsonBuf + pos, sizeof(jsonBuf) - pos, "]}");

    LOG_WEATHER_DEBUG("City search result JSON: %s", jsonBuf);
    // 复制到全局结果缓冲区
    strlcpy(citySearchResultJson, jsonBuf, sizeof(citySearchResultJson));
}

enum FetchCitySearchState {
    IDLE,
    FETCHING,
    COMPLETED
};
FetchCitySearchState citySearchState = IDLE;
char citySearchResultJson[2048] = "";

void webSettingHandleCitySearchResult() {
    if(citySearchState == IDLE){
        if (!settingServer.hasArg("location")) {
            settingServer.send(400, "application/json; charset=utf-8", "{\"error\":\"缺少 location 参数\"}");
            return;
        }
        citySearchState = FETCHING;
        settingServer.send(202, "application/json; charset=utf-8", "{\"status\":\"processing\"}");
        // 复制location到堆上
        char* locCopy = strdup(settingServer.arg("location").c_str());
        xTaskCreate([](void* param) {
            char* loc = (char*)param;
            LOG_WEATHER_DEBUG("City search task started for location: %s", loc);
            fetchCitySearchResult(loc);
            citySearchState = COMPLETED;
            free(loc);
            vTaskDelete(NULL);
        }, "CitySearchTask", 16384, locCopy, 2, NULL);
    }
    else if(citySearchState == COMPLETED){
        settingServer.send(200, "application/json; charset=utf-8", citySearchResultJson);
        citySearchState = IDLE;
        citySearchResultJson[0] = '\0';
    }
    else if(citySearchState == FETCHING){
        settingServer.send(202, "application/json; charset=utf-8", "{\"status\":\"processing\"}");
    }
    else {
        settingServer.send(429, "application/json; charset=utf-8", "{\"error\":\"正在处理另一个请求，请稍后再试\"}");
    }
}

void webSettingHandleSetLocation() {
    if (settingServer.method() != HTTP_POST) {
        settingServer.send(405, "application/json; charset=utf-8", "{\"error\":\"请使用 POST 方法\"}");
        return;
    }
    if (!settingServer.hasArg("locid")) {
        settingServer.send(400, "text/html; charset=utf-8", "参数错误");
        return;
    }
    char locid[32];
    strlcpy(locid, settingServer.arg("locid").c_str(), sizeof(locid));
    // 去除首尾空格
    char* start = locid;
    while (*start == ' ') start++;
    if (start != locid) memmove(locid, start, strlen(start) + 1);
    size_t len = strlen(locid);
    while (len > 0 && locid[len - 1] == ' ') {
        locid[--len] = '\0';
    }

    char cityname[32] = "";
    if (settingServer.hasArg("fxlink")) {
        const char* fxlink = settingServer.arg("fxlink").c_str();
        const char* weatherStart = strstr(fxlink, "/weather/");
        const char* lastDash = strrchr(fxlink, '-');
        if (weatherStart && lastDash && lastDash > weatherStart + 9) {
            size_t nameLen = lastDash - (weatherStart + 9);
            if (nameLen < sizeof(cityname)) {
                strncpy(cityname, weatherStart + 9, nameLen);
                cityname[nameLen] = '\0';
            }
        }
    }
    else{
        LOG_WEATHER_INFO("No fxlink provided, using locid as city name");
        strlcpy(cityname, locid, sizeof(cityname));
    }

    // 保存到配置文件（追加或覆盖）
    qweatherAuthConfigManager.setLocation(locid, cityname);

    loadJwtConfig();

    weatherSynced = false;
    isReadyToDisplay = false;

    LOG_WEATHER_INFO("Location set to ID: %s, Name: %s",
        qweatherAuthConfigManager.getLocation(), qweatherAuthConfigManager.getCityName());
    char resp[96];
    snprintf(resp, sizeof(resp), "{\"ok\":true,\"locid\":\"%s\"}", qweatherAuthConfigManager.getLocation());
    settingServer.send(200, "application/json; charset=utf-8", resp);
}

void webSettingSetupWebServer() {
    if(wifiConnectionState != WIFI_CONNECTED) {
        LOG_SYSTEM_WARN("Web setting server setup called but WiFi not connected");
        lcdText("WiFi Not Conn", 1);
        lcdText(" ", 2);
        WAIT_MS(1000);
        return;
    }

    if (!s_otaModuleInitialized) {
        otaInit();
        s_otaModuleInitialized = true;
    }

    isConfigDone=false;

    settingServer.onNotFound([](){ settingServer.send(404, "text/plain", "404 Not Found"); });

    // 主页相关路由w
    settingServer.on("/", webSettingHandleRoot);                        // 主页
    settingServer.on("/favicon.ico", webSettingHandleFavicon);          // 网站图标
    settingServer.on("/get_api_info", webSettingHandleGetApiInfo);      // 获取当前API信息
    settingServer.on("/set", HTTP_POST, webSettingHandleSet);           // 接收POST JSON格式的API配置信息
    settingServer.on("/exit", [](){                                     // 退出设置页面
        isConfigDone = true;
        settingServer.send(200, "text/plain", "Exiting configuration...");
    });

    // OTA相关路由
    settingServer.on("/ota", webSettingHandleOTA);                         // OTA页面
    settingServer.on("/ota/url", webSettingHandleOTAURL);                  // OTA URL处理
    settingServer.on("/ota/progress", webSettingHandleOTAProgress);        // OTA进度查询
    settingServer.on("/ota/upload", HTTP_POST,
        []() {
            // 处理完成后的回调
            if (otaUploadSuccess) {
                settingServer.send(200, "application/json", "{\"success\":true}");
            } else {
                char errBuf[128];
                snprintf(errBuf, sizeof(errBuf), "{\"success\":false,\"error\":\"%s\"}",
                    Update.hasError() ? Update.errorString() : "上传失败");
                settingServer.send(500, "application/json", errBuf);
            }
            otaUploadSuccess = false;  // 重置标志
        },
        webSettingHandleOTAUpload  // 上传处理函数
    );

    // 城市搜索相关路由
    settingServer.on("/citysearch", webSettingHandleCitySearch);    // 城市搜索界面
    settingServer.on("/citysearch_result", webSettingHandleCitySearchResult);
    settingServer.on("/set_location", webSettingHandleSetLocation);
    settingServer.on("/settings/city_search_ready", webSettingHandleCitySearchReady);

    // 设置菜单相关路由
    settingServer.on("/settings/status", webSettingHandleDeviceStatus);
    settingServer.on("/settings/basic_info", webSettingHandleDeviceBasicInfo);
    settingServer.on("/settings/als", webSettingHandleAlsRealtimeInfo);
    settingServer.on("/settings/fuel", webSettingHandleFuelGaugeRealtimeInfo);
    settingServer.on("/settings/wifi", webSettingHandleWifiInfo);
    settingServer.on("/settings/brightness", HTTP_POST, webSettingHandleSetBrightness);
    settingServer.on("/settings/auto_brightness/toggle", HTTP_POST, webSettingHandleToggleAutoBrightness);
    settingServer.on("/settings/sound_effects/toggle", HTTP_POST, webSettingHandleToggleSoundEffects);
    
    settingServer.begin();
    LOG_WEATHER_INFO("Web configuration server started, access via IP address");

    // 获取当前IP地址并显示
    lcdText("Config Mode", 1);
    lcdText(WiFi.localIP().toString().c_str(), 2);
    while(isConfigDone == false){
        settingServer.handleClient();
        WAIT_MS(1);
    }

    lcdText("Config Done", 1);
    lcdText("Exiting...", 2);
    WAIT_MS(500);
}
#include "./applications/weather.h"
#include "./hardware/buzzer.h"
#include "./connectivity/http_client_wrapper.h"
#include <cstring>

// 天气服务，API接口通过ESP32向云端获取JSON数据
extern QWeatherAuthConfigManager qweatherAuthConfigManager;

bool weatherSynced = false;
char currentWeather[16] = "N/A";
char currentTemp[8] = "--C";
char currentCity[16] = "N/A";
char weatherUpdateTime[16] = "--/-- --:--";
char feelsLike[8] = "--C";
char windDir[16] = "";
char windScale[4] = "";
char humidity[8] = "";
char pressure[8] = "";
char obsTime[16] = "";
unsigned long lastWeatherUpdate = 0;
unsigned int interface_num = 0; // 当前显示的界面编号
static bool s_weatherIsNewInterface = false;
static bool s_weatherReadyToDisplay = false;
static unsigned long s_lastWeatherFail = 0;
static unsigned long s_lastWeatherFailSoundMs = 0;

static void _exitWeatherToMenu(unsigned long delayMs = FIRST_TIME_DELAY) {
    exitAppInterface(delayMs);
}

static void _playWeatherFailSoundThrottled(unsigned long intervalMs = 2500) {
    const unsigned long now = GET_MS();
    if (now - s_lastWeatherFailSoundMs < intervalMs) {
        return;
    }
    s_lastWeatherFailSoundMs = now;
    buzzerPlayError();
}

static bool _ensureWeatherTimeSynced() {
    if (!(timeSyncState == TIME_SYNC_SUCCESS) && !(getRtcTime().tv_sec > 1765967312)) { // 2025-12-17 18:40 GMT+8
        lcdText("Try time sync", 1);
        lcdText("Please wait", 2);
        updateTimeSync();
        if (timeSyncState != TIME_SYNC_SUCCESS) {
            LOG_WEATHER_WARN("Time not synced yet, cannot display weather");
            lcdText("Time not synced", 1);
            lcdText("", 2);
            _playWeatherFailSoundThrottled();
            return false;
        }
    }
    return true;
}

void enterWeatherInterface() {
    s_weatherIsNewInterface = true;
    enterAppInterface(handleWeatherInterface, true);
}

void handleWeatherInterface() {
    if (!_ensureWeatherTimeSynced()) {
        _exitWeatherToMenu();
        return;
    }

    // 进入天气界面时先做本地前置校验，避免被失败重试冷却窗口“卡住”。
    if (WiFi.status() != WL_CONNECTED) {
        lcdText("No WiFi", 1);
        lcdText(" ", 2);
        _playWeatherFailSoundThrottled();
        _exitWeatherToMenu();
        return;
    }
    if (!qweatherAuthConfigManager.checkApiConfigValid()) {
        lcdText("No API config", 1);
        lcdText("Use web config", 2);
        _playWeatherFailSoundThrottled();
        _exitWeatherToMenu();
        return;
    }
    if (!qweatherAuthConfigManager.checkLocationConfigValid()) {
        lcdText("No City Set", 1);
        lcdText("Use Web Config", 2);
        _playWeatherFailSoundThrottled();
        _exitWeatherToMenu();
        return;
    }

    // 每隔10分钟更新一次天气数据
    if (!s_weatherReadyToDisplay || GET_MS() - lastWeatherUpdate > 10 * 60 * 1000) {
        if (GET_MS() - s_lastWeatherFail > 15 * 1000) { // 失败后15秒再试
            loadJwtConfig();
            LOG_WEATHER_INFO("Fetching weather data...");
            if (fetchWeatherData()) {
                s_weatherReadyToDisplay = true;
                lastWeatherUpdate = GET_MS();
            } else {
                LOG_WEATHER_WARN("Failed to fetch weather data");
                _playWeatherFailSoundThrottled(4000);
                s_weatherReadyToDisplay = false;
                s_lastWeatherFail = GET_MS();
                _exitWeatherToMenu();
                return;
            }
        }
    }

    if (s_weatherIsNewInterface) {
        s_weatherIsNewInterface = false;
        if (s_weatherReadyToDisplay) {
            updateWeatherScreen();
        }
    }

    if (isButtonReadyToRespond(CENTER, BUTTON_DEBOUNCE_DELAY)) {
        LOG_WEATHER_INFO("Exit weather interface to menu");
        _exitWeatherToMenu();
        return;
    }

    if (!s_weatherReadyToDisplay) {
        // 失败时已显示具体错误信息，并在上方直接返回菜单，这里不再二次覆盖为 No Data。
        return;
    }

    if (isButtonReadyToRespond(LEFT, BUTTON_DEBOUNCE_DELAY)) {
        interface_num = (interface_num + 3) % 4; // 切换到上一个界面
        updateWeatherScreen();
    }
    if (isButtonReadyToRespond(RIGHT, BUTTON_DEBOUNCE_DELAY)) {
        interface_num = (interface_num + 1) % 4; // 切换到下一个界面
        updateWeatherScreen();
    }
}

// 只负责显示天气信息
void updateWeatherScreen() {
    if(interface_num == 0){
        lcdResetCursor();
        if (strcmp(currentCity, "N/A") != 0 && strlen(currentCity) > 0) {
            lcdText(currentCity, 1); // 第一行显示配置地名
        } else {
            lcdText("N/A", 1);
        }

        lcdSetCursor(16); 
        
        lcdCreateCharAuto(WeatherIcons::getLeftIcon(currentWeather));
        lcdCreateCharAuto(WeatherIcons::getRightIcon(currentWeather));
        lcdCreateCharAuto(SystemIcons::tempIcon);

        lcdPrint(currentTemp);
        lcdCreateCharAuto(SystemIcons::celsius);
        char feelsBuf[16];
        snprintf(feelsBuf, sizeof(feelsBuf), " Fel %s", feelsLike);
        lcdPrint(feelsBuf);
        lcdCreateCharAuto(SystemIcons::celsius);


        for(int i=lcdCursor;i<32;i++) lcdDisChar(' '); // 清除剩余部分
        
    } else if(interface_num == 1){
        lcdClear();
        
        lcdPrint("Wind ");
        lcdCreateCharAuto(WindIcons::getIcon(windDir));
        lcdPrint(windScale);

        lcdSetCursor(16); 
        lcdPrint("Humi:"); // 第二行显示湿度
        lcdPrint(humidity);

    } else if(interface_num == 2){
        lcdClear();
        lcdPrint("Pres:");
        lcdPrint(pressure);
        lcdText(" ",2);
        
    } else if(interface_num == 3){
        lcdClear();
        lcdPrint("Obs:");
        lcdPrint(obsTime);

        lcdSetCursor(16); 
        lcdPrint("Upd:"); // 第二行显示观测时间
        lcdPrint(weatherUpdateTime);
    }
}

// 负责网络请求和数据解析（HTTPS + gzip解压）
bool fetchWeatherData() {
    // 打印内存使用情况
    MemoryManager::printMemoryInfo("Weather fetch start");
    
    if (WiFi.status() != WL_CONNECTED) {
        LOG_WEATHER_ERROR("WiFi not connected");
        lcdText("No WiFi", 1);
        lcdText(" ", 2);
        _playWeatherFailSoundThrottled();
        return false;
    }

    // 检查 API 配置是否完整
    if (!qweatherAuthConfigManager.checkApiConfigValid()) {
        LOG_WEATHER_ERROR("Missing API configuration");
        lcdText("No API config", 1);
        lcdText("Use web config", 2);
        _playWeatherFailSoundThrottled();
        WAIT_MS(1000);
        return false;
    }

    // 检查城市配置是否为空
    if (!qweatherAuthConfigManager.checkLocationConfigValid()) {
        LOG_WEATHER_ERROR("Missing city/location configuration");
        lcdText("No City Set", 1);
        lcdText("Use Web Config", 2);
        _playWeatherFailSoundThrottled();
        WAIT_MS(1000);
        return false;
    }

    generateSeed32(); // 生成Seed32

    LOG_WEATHER_INFO("API config OK, fetching weather...");

    lcdText("Updating weather", 1);
    lcdText("Please wait...", 2);

    // 生成 JWT
    char jwtToken[512];
    generate_jwt(qweatherAuthConfigManager.getKId(), qweatherAuthConfigManager.getProjectID(), seed32, jwtToken, sizeof(jwtToken));
    LOG_WEATHER_DEBUG("JWT token: %s", jwtToken);

    // 拼接 URL
    char url[256];
    snprintf(url, sizeof(url), "https://%s/v7/weather/now?location=%s",
        qweatherAuthConfigManager.getApiHost(), qweatherAuthConfigManager.getLocation());
    LOG_WEATHER_DEBUG("Final Request URL: %s", url);

    HttpClientWrapper http;
    http.begin(url);
    http.addHeader("Accept-Encoding", "gzip");                  // 请求 gzip 压缩响应
    char authHeader[560];
    snprintf(authHeader, sizeof(authHeader), "Bearer %s", jwtToken);
    http.addHeader("Authorization", authHeader);      // 使用 Bearer 令牌进行授权

    int httpCode = http.GET();

    if (httpCode == 200)
        LOG_WEATHER_DEBUG("HTTP code: %d", httpCode);
    else {
        LOG_WEATHER_ERROR("HTTP error: %d", httpCode);
        lcdText("HTTP error", 1);
        char httpCodeStr[8];
        snprintf(httpCodeStr, sizeof(httpCodeStr), "%d", httpCode);
        lcdText(httpCodeStr, 2);
        _playWeatherFailSoundThrottled();
        http.end();
        return false; // 直接返回，避免解析空数据
    }

    // 使用 getResponseData() 获取完整响应（支持二进制数据如gzip）
    size_t responseLen = 0;
    const uint8_t* responseData = http.getResponseData(&responseLen);
    http.end();

    const char* compressedData = (const char*)responseData;
    int compressedSize = responseLen;

    if (compressedSize == 0 || responseData == nullptr) {
        LOG_WEATHER_ERROR("No data received");
        lcdText("No data received", 1);
        lcdText("", 2);
        _playWeatherFailSoundThrottled();
        return false;
    }

    char* jsonData = nullptr;
    size_t jsonDataLen = 0;
    zlib_turbo zturbo;      // zlib_turbo 实例

    // 检查是否为 gzip 格式（检查前两个字节 0x1f 0x8b）
    if (compressedSize >= 2 && compressedData[0] == 0x1f && compressedData[1] == 0x8b) {
        int uncompSize = zturbo.gzip_info(compressedData, compressedSize); // 获取解压后大小
        if (uncompSize <= 0) {
            LOG_WEATHER_ERROR("get gzip_info failed");
            lcdText("Gzip info fail", 1);
            lcdText("", 2);
            _playWeatherFailSoundThrottled();
            return false;
        }

        // 使用RAII解压缓冲区
        MemoryManager::SafeBuffer uncompressedBuffer(uncompSize + 8, "Gzip_Decompressed");
        if (!uncompressedBuffer.isValid()) {
            LOG_WEATHER_ERROR("malloc failed for uncompressed buffer");
            lcdText("Mem fail", 1);
            lcdText("", 2);
            _playWeatherFailSoundThrottled();
            return false;
        }

        // 执行解压
        int unzipResult = zturbo.gunzip(compressedData, compressedSize, uncompressedBuffer.get());
        if (unzipResult != ZT_SUCCESS) {
            LOG_WEATHER_ERROR("Gzip decompress failed");
            lcdText("Gzip failed", 1);
            lcdText("", 2);
            _playWeatherFailSoundThrottled();
            return false;
        }
        jsonData = (char*)uncompressedBuffer.get();
        jsonDataLen = uncompSize;
        // uncompressedBuffer 会在作用域结束时自动释放
    } else {
        jsonData = (char*)compressedData;
        jsonDataLen = compressedSize;
    }

    if (jsonDataLen == 0 || jsonData == nullptr) {
        LOG_WEATHER_ERROR("Empty response");
        lcdText("Empty response", 1);
        lcdText("", 2);
        _playWeatherFailSoundThrottled();
        return false;
    }

    // 解析Json
    JsonDocument doc;       // 自动选择合适的内存分配器
    DeserializationError error = deserializeJson(doc, jsonData, jsonDataLen);    // 反序列化JSON
    if (error) {
        LOG_WEATHER_ERROR("JSON parse failed: %s", error.c_str());
        lcdText("JSON failed", 1);
        lcdText("", 2);
        _playWeatherFailSoundThrottled();
        return false;
    }

    // 成功解析，赋值天气数据
    strlcpy(currentCity, qweatherAuthConfigManager.getCityName(), sizeof(currentCity));

    // 安全地获取天气数据，避免空值
    if (doc["now"]["text"].is<const char*>()) {
        strlcpy(currentWeather, doc["now"]["text"].as<const char*>(), sizeof(currentWeather));
        if (strlen(currentWeather) == 0) {
            LOG_WEATHER_WARN("Empty weather text");
            strlcpy(currentWeather, "Unknown", sizeof(currentWeather));
        }
    } else {
        LOG_WEATHER_WARN("unknown weather text");
        strlcpy(currentWeather, "Unknown", sizeof(currentWeather));
    }

    // 安全地获取其他数据
    snprintf(currentTemp, sizeof(currentTemp), "%sC",
        doc["now"]["temp"].is<const char*>() ? doc["now"]["temp"].as<const char*>() : "?");
    snprintf(feelsLike, sizeof(feelsLike), "%sC",
        doc["now"]["feelsLike"].is<const char*>() ? doc["now"]["feelsLike"].as<const char*>() : "?");
    strlcpy(windDir,
        doc["now"]["windDir"].is<const char*>() ? doc["now"]["windDir"].as<const char*>() : "", sizeof(windDir));
    strlcpy(windScale,
        doc["now"]["windScale"].is<const char*>() ? doc["now"]["windScale"].as<const char*>() : "?", sizeof(windScale));
    snprintf(humidity, sizeof(humidity), "%s%%",
        doc["now"]["humidity"].is<const char*>() ? doc["now"]["humidity"].as<const char*>() : "?");
    snprintf(pressure, sizeof(pressure), "%shPa",
        doc["now"]["pressure"].is<const char*>() ? doc["now"]["pressure"].as<const char*>() : "?");

    // 格式化 obsTime 为 MM/DD HH:MM
    const char* rawObsTime = doc["now"]["obsTime"].is<const char*>() ? doc["now"]["obsTime"].as<const char*>() : "";
    if (strlen(rawObsTime) >= 16) {
        // 格式为ISO8601：2023-11-01T14:30:00+08:00
        snprintf(obsTime, sizeof(obsTime), "%.*s/%.*s %.*s:%.*s",
            2, rawObsTime + 5, 2, rawObsTime + 8, 2, rawObsTime + 11, 2, rawObsTime + 14);
    } else {
        strlcpy(obsTime, "--/-- --:--", sizeof(obsTime));
    }

    // 格式化 weatherUpdateTime 为 MM/DD HH:MM
    const char* rawUpdateTime = doc["updateTime"].is<const char*>() ? doc["updateTime"].as<const char*>() : "";
    if (strlen(rawUpdateTime) >= 16) {
        // 格式为ISO8601
        snprintf(weatherUpdateTime, sizeof(weatherUpdateTime), "%.*s/%.*s %.*s:%.*s",
            2, rawUpdateTime + 5, 2, rawUpdateTime + 8, 2, rawUpdateTime + 11, 2, rawUpdateTime + 14);
    } else {
        strlcpy(weatherUpdateTime, "--/-- --:--", sizeof(weatherUpdateTime));
    }
    
    weatherSynced = true;
    lastWeatherUpdate = GET_MS();

    // 打印内存使用情况
    MemoryManager::printMemoryInfo("Weather fetch complete");
    
    LOG_WEATHER_INFO("Weather updated: %s, %s", currentWeather, currentTemp);
    updateWeatherScreen();
    return true;
}
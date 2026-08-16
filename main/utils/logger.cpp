#include "./utils/logger.h"
#include <esp_timer.h>
#include <esp_system.h>

// 静态成员变量定义
LogLevel Logger::globalLogLevel = LOG_LEVEL_INFO;
LogLevel Logger::moduleLogLevels[LOG_MODULE_MAX];
bool Logger::initialized = false;
SemaphoreHandle_t Logger::logMutex = nullptr;

// 模块前缀定义
const char* Logger::modulePrefixes[LOG_MODULE_MAX] = {
    "SYSTEM",
    "WIFI",
    "TIME_SYNC",
    "WEATHER",
    "MENU",
    "LCD",
    "BUTTON",
    "JWT",
    "WEB",
    "NETWORK",
    "MEMORY",
    "RGB",
    "DISPLAY",
    "CONFIG",
    "BATTERY",
    "SLEEP",
    "ALS"
};

// 日志级别前缀定义
const char* Logger::levelPrefixes[6] = {
    "NONE",
    "ERROR",
    "WARN",
    "INFO",
    "DEBUG",
    "VERBOSE"
};

// ESP-IDF 日志标签
static const char* TAG = "LOGGER";

void Logger::init(LogLevel defaultLevel) {
    if (initialized) return;

    globalLogLevel = defaultLevel;

    // 初始化所有模块的日志级别为默认级别
    for (int i = 0; i < LOG_MODULE_MAX; i++) {
        moduleLogLevels[i] = defaultLevel;
    }

    initialized = true;
    logMutex = xSemaphoreCreateMutex();

    // 使用 ESP-IDF 日志系统
    esp_log_level_set("*", ESP_LOG_INFO);

    ESP_LOGI(TAG, "=== Logger System Initialized ===");
    ESP_LOGI(TAG, "Global log level: %s", levelPrefixes[defaultLevel]);
    ESP_LOGI(TAG, "==================================");
}

void Logger::setGlobalLevel(LogLevel level) {
    globalLogLevel = level;
    if (xPortInIsrContext()) {
        return;
    }
    if (logMutex != nullptr && xSemaphoreTake(logMutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        ESP_LOGI(TAG, "Global log level set to: %s", levelPrefixes[level]);
        xSemaphoreGive(logMutex);
    }
}

void Logger::setModuleLevel(LogModule module, LogLevel level) {
    if (module < LOG_MODULE_MAX) {
        moduleLogLevels[module] = level;
        if (xPortInIsrContext()) {
            return;
        }
        if (logMutex != nullptr && xSemaphoreTake(logMutex, pdMS_TO_TICKS(20)) == pdTRUE) {
            ESP_LOGI(TAG, "Module %s log level set to: %s",
                     modulePrefixes[module], levelPrefixes[level]);
            xSemaphoreGive(logMutex);
        }
    }
}

bool Logger::shouldLog(LogModule module, LogLevel level) {
    if (!initialized || level == LOG_LEVEL_NONE) return false;

    // 检查全局级别
    if (level > globalLogLevel) return false;

    // 检查模块级别
    if (module < LOG_MODULE_MAX && level > moduleLogLevels[module]) {
        return false;
    }

    return true;
}

void Logger::printTimestamp() {
    uint32_t now = GET_MS();
    uint32_t seconds = now / 1000;
    uint32_t milliseconds = now % 1000;

    uint32_t hours = seconds / 3600;
    uint32_t minutes = (seconds % 3600) / 60;
    seconds = seconds % 60;

    printf("[%02lu:%02lu:%02lu.%03lu] ",
           (unsigned long)hours, (unsigned long)minutes,
           (unsigned long)seconds, (unsigned long)milliseconds);
}

const char* Logger::getModulePrefix(LogModule module) {
    if (module < LOG_MODULE_MAX) {
        return modulePrefixes[module];
    }
    return "UNKNOWN";
}

const char* Logger::getLevelPrefix(LogLevel level) {
    if (level >= 0 && level < 6) {
        return levelPrefixes[level];
    }
    return "UNKNOWN";
}

void Logger::log(LogModule module, LogLevel level, const char* format, ...) {
    if (!shouldLog(module, level)) return;
    if (xPortInIsrContext()) return;
    if (logMutex == nullptr) return;
    if (xSemaphoreTake(logMutex, pdMS_TO_TICKS(20)) != pdTRUE) return;

    // 打印时间戳
    printTimestamp();

    // 打印级别和模块
    printf("[%s][%s] ", getLevelPrefix(level), getModulePrefix(module));

    // 打印消息内容
    va_list args;
    va_start(args, format);

    char buffer[512];
    vsnprintf(buffer, sizeof(buffer), format, args);
    printf("%s", buffer);

    va_end(args);

    // 如果消息没有以换行符结尾，添加一个
    if (strlen(buffer) > 0 && buffer[strlen(buffer) - 1] != '\n') {
        printf("\n");
    }

    xSemaphoreGive(logMutex);
}

// TODO: 阶段3处理 - String 类型参数
// void Logger::log(LogModule module, LogLevel level, const String& message) {
//     log(module, level, "%s", message.c_str());
// }

void Logger::logMemoryInfo(LogModule module) {
    if (!shouldLog(module, LOG_LEVEL_INFO)) return;

    size_t freeHeap = esp_get_free_heap_size();
    size_t totalHeap = heap_caps_get_total_size(MALLOC_CAP_DEFAULT);
    size_t usedHeap = totalHeap - freeHeap;

    // 如果有PSRAM，也显示PSRAM信息
    size_t totalPsram = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    if (totalPsram > 0) {
        size_t freePsram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
        size_t usedPsram = totalPsram - freePsram;

        log(module, LOG_LEVEL_INFO,
            "Memory - Heap: %u/%u bytes (%.1f%%), PSRAM: %u/%u bytes (%.1f%%)",
            usedHeap, totalHeap, (float)usedHeap * 100.0 / totalHeap,
            usedPsram, totalPsram, (float)usedPsram * 100.0 / totalPsram);
    } else {
        log(module, LOG_LEVEL_INFO,
            "Memory - Heap: %u/%u bytes (%.1f%%)",
            usedHeap, totalHeap, (float)usedHeap * 100.0 / totalHeap);
    }
}

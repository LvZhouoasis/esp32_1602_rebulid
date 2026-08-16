#include "./services/config_manager.h"

static const char* kAutoBrightnessConfigPath = "/auto_brightness_config.txt";
static const char* kSoundEffectsConfigPath = "/sound_effects_config.txt";

bool ConfigManager::isSPIFFSInitialized = false;

ConfigManager::ConfigManager(const char* configFilePath) {
    strncpy(this->configFilePath, configFilePath, sizeof(this->configFilePath) - 1);
    this->configFilePath[sizeof(this->configFilePath) - 1] = '\0';
    LOG_CONFIG_INFO("ConfigManager initialized");
    LOG_CONFIG_DEBUG("Config file path: %s", this->configFilePath);
};

ConfigManager::~ConfigManager(){
    LOG_CONFIG_INFO("ConfigManager destroyed");
};

bool ConfigManager::readFile(char* configContent, size_t bufferSize) {
    // 构建完整路径（ESP-IDF VFS 需要 /spiffs 前缀）
    char fullPath[128];
    snprintf(fullPath, sizeof(fullPath), "/spiffs%s", configFilePath);

    // 检查文件是否存在
    struct stat st;
    if (stat(fullPath, &st) != 0) {
        LOG_CONFIG_WARN("Config file not found: %s", fullPath);
        lastError = Error::FileNotFound;
        listDir("/spiffs", 0); // 列出根目录以帮助调试
        return false;
    }

    FILE* file = fopen(fullPath, "r");
    if (!file) {
        lastError = Error::ReadError;
        LOG_CONFIG_ERROR("Failed to open config file: %s", fullPath);
        return false;
    }

    size_t fileSize = st.st_size;
    LOG_CONFIG_DEBUG("Reading file: %s, size: %d bytes", fullPath, fileSize);

    if (fileSize == 0) {
        LOG_CONFIG_WARN("Config file is empty: %s", fullPath);
        fclose(file);
        configContent[0] = '\0';
        lastError = Error::ReadError;
        return false;
    }

    // 检查缓冲区大小
    if (fileSize >= bufferSize) {
        LOG_CONFIG_ERROR("Buffer too small: need %d, have %d", fileSize + 1, bufferSize);
        fclose(file);
        lastError = Error::ReadError;
        return false;
    }

    // 读取文件内容
    size_t bytesRead = fread(configContent, 1, fileSize, file);
    configContent[bytesRead] = '\0';
    fclose(file);

    LOG_CONFIG_VERBOSE("Config read (%d bytes): %s", bytesRead, configContent);

    if (bytesRead == 0) {
        LOG_CONFIG_ERROR("Failed to read content from file: %s", fullPath);
        lastError = Error::ReadError;
        return false;
    }

    return true;
}

bool ConfigManager::writeFile(const char* configContent) {
    size_t contentLen = strlen(configContent);
    LOG_CONFIG_VERBOSE("Writing config (%d bytes): %s", contentLen, configContent);

    // 删除旧文件(如果存在)
    if(SPIFFS.exists(configFilePath)) {
        LOG_CONFIG_DEBUG("Removing existing file: %s", configFilePath);
        SPIFFS.remove(configFilePath);
        WAIT_MS(50);  // 等待Flash完成删除操作
    }

    File file = SPIFFS.open(configFilePath, "w", true);  // create if not exists
    if (!file) {
        lastError = Error::WriteError;
        LOG_CONFIG_ERROR("Failed to open config file for writing: %s", configFilePath);
        return false;
    }

    size_t written = file.print(configContent);
    file.flush();  // 确保数据完全写入Flash
    file.close();

    WAIT_MS(100);  // 等待Flash完成写入操作

    if (written != contentLen) {
        lastError = Error::WriteError;
        LOG_CONFIG_ERROR("Failed to write complete config to file: %s (wrote %d/%d bytes)",
                        configFilePath, written, contentLen);
        return false;
    }

    // 验证写入
    if (!SPIFFS.exists(configFilePath)) {
        lastError = Error::WriteError;
        LOG_CONFIG_ERROR("File verification failed: file not found after write: %s", configFilePath);
        return false;
    }

    // 验证文件大小
    File verifyFile = SPIFFS.open(configFilePath, "r");
    if (verifyFile) {
        size_t actualSize = verifyFile.size();
        verifyFile.close();
        if (actualSize != contentLen) {
            lastError = Error::WriteError;
            LOG_CONFIG_ERROR("File size mismatch: expected %d, got %d", contentLen, actualSize);
            return false;
        }
    }

    LOG_CONFIG_INFO("Config saved successfully: %s (%d bytes)", configFilePath, written);
    return true;
}

// 打印目录
void ConfigManager::listDir(const char* dirname, uint8_t levels) {
    LOG_SYSTEM_DEBUG("Listing directory: %s", dirname);

    File root = SPIFFS.open(dirname);
    if(!root) {
        LOG_SYSTEM_ERROR("Failed to open directory: %s", dirname);
        return;
    }
    if(!root.isDirectory()) {
        LOG_SYSTEM_ERROR("Not a directory: %s", dirname);
        root.close();
        return;
    }

    File file = root.openNextFile();
    while(file) {
        const char* fileName = file.name();
        if (!fileName) {
            LOG_SYSTEM_WARN("File has no name, skipping");
            file.close();
            file = root.openNextFile();
            continue;
        }
        
        if(file.isDirectory()) {
            LOG_SYSTEM_DEBUG("  DIR : %s", fileName);
            if(levels) {
                listDir(fileName, levels - 1);
            }
        } else {
            LOG_SYSTEM_DEBUG("  FILE: %s\tSIZE: %d", fileName, file.size());
        }
        file.close();
        file = root.openNextFile();
    }
    root.close();
}

// 初始化 SPIFFS
bool ConfigManager::initSPIFFS() {
    if(isSPIFFSInitialized) {
        LOG_CONFIG_INFO("SPIFFS already initialized");
        return true;
    }

    LOG_CONFIG_INFO("Initializing SPIFFS...");

    // 使用 ESP-IDF VFS 初始化 SPIFFS
    if (!spiffsInit()) {
        LOG_CONFIG_ERROR("SPIFFS initialization failed");
        return false;
    }

    LOG_CONFIG_DEBUG("SPIFFS mounted successfully");

    // 获取存储信息
    size_t total = 0, used = 0;
    if (spiffsInfo(&total, &used)) {
        LOG_CONFIG_DEBUG("Total: %d bytes, Used: %d bytes", total, used);
    }

    // 打印文件列表
    LOG_CONFIG_DEBUG("Checking SPIFFS files...");
    listDir("/spiffs", 0);

    isSPIFFSInitialized = true;
    return true;
}

ConfigManager::Error ConfigManager::getLastError() const {
    return lastError;
}

const char* ConfigManager::getLastErrorString(Error error) const {
    switch (error) {
        case Error::None:
            return "No error";
        case Error::FileNotFound:
            return "File not found";
        case Error::ReadError:
            return "Read error";
        case Error::WriteError:
            return "Write error";
        case Error::InvalidData:
            return "Invalid data";
        default:
            return "Unknown error";
    }
}

void ConfigManager::clearLastError() {
    lastError = Error::None;
}

void ConfigManager::setLastError(Error error) {
    lastError = error;
}

bool ConfigManager::saveAutoBrightnessEnabled(bool enabled) {
    if (!isSPIFFSInitialized && !initSPIFFS()) {
        LOG_CONFIG_ERROR("Failed to init SPIFFS before saving auto brightness config");
        return false;
    }

    JsonDocument doc;
    doc["enabled"] = enabled;

    char jsonString[64];
    serializeJson(doc, jsonString, sizeof(jsonString));

    if (SPIFFS.exists(kAutoBrightnessConfigPath)) {
        SPIFFS.remove(kAutoBrightnessConfigPath);
    }

    File file = SPIFFS.open(kAutoBrightnessConfigPath, "w", true);
    if (!file) {
        LOG_CONFIG_ERROR("Failed to open auto brightness config for write: %s", kAutoBrightnessConfigPath);
        return false;
    }

    size_t written = file.print(jsonString);
    file.flush();
    file.close();

    if (written != strlen(jsonString)) {
        LOG_CONFIG_ERROR("Failed to write full auto brightness config (%u/%u)",
            static_cast<unsigned int>(written),
            static_cast<unsigned int>(strlen(jsonString)));
        return false;
    }

    LOG_CONFIG_INFO("Auto brightness config saved: enabled=%s", enabled ? "true" : "false");
    return true;
}

bool ConfigManager::loadAutoBrightnessEnabled(bool& enabled) {
    if (!isSPIFFSInitialized && !initSPIFFS()) {
        LOG_CONFIG_ERROR("Failed to init SPIFFS before loading auto brightness config");
        return false;
    }

    if (!SPIFFS.exists(kAutoBrightnessConfigPath)) {
        LOG_CONFIG_INFO("Auto brightness config not found, keep current default");
        return false;
    }

    File file = SPIFFS.open(kAutoBrightnessConfigPath, "r");
    if (!file) {
        LOG_CONFIG_ERROR("Failed to open auto brightness config for read: %s", kAutoBrightnessConfigPath);
        return false;
    }

    char content[128];
    size_t bytesRead = file.read((uint8_t*)content, sizeof(content) - 1);
    content[bytesRead] = '\0';
    file.close();

    if (bytesRead == 0) {
        LOG_CONFIG_WARN("Auto brightness config is empty");
        return false;
    }

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, content);
    if (error) {
        LOG_CONFIG_ERROR("Failed to parse auto brightness config: %s", error.c_str());
        return false;
    }

    if (!doc["enabled"].is<bool>()) {
        LOG_CONFIG_WARN("Auto brightness config missing boolean field: enabled");
        return false;
    }

    enabled = doc["enabled"].as<bool>();
    LOG_CONFIG_INFO("Auto brightness config loaded: enabled=%s", enabled ? "true" : "false");
    return true;
}

bool ConfigManager::saveSoundEffectsEnabled(bool enabled) {
    if (!isSPIFFSInitialized && !initSPIFFS()) {
        LOG_CONFIG_ERROR("Failed to init SPIFFS before saving sound effects config");
        return false;
    }

    JsonDocument doc;
    doc["enabled"] = enabled;

    char jsonString[64];
    serializeJson(doc, jsonString, sizeof(jsonString));

    if (SPIFFS.exists(kSoundEffectsConfigPath)) {
        SPIFFS.remove(kSoundEffectsConfigPath);
    }

    File file = SPIFFS.open(kSoundEffectsConfigPath, "w", true);
    if (!file) {
        LOG_CONFIG_ERROR("Failed to open sound effects config for write: %s", kSoundEffectsConfigPath);
        return false;
    }

    size_t written = file.print(jsonString);
    file.flush();
    file.close();

    if (written != strlen(jsonString)) {
        LOG_CONFIG_ERROR("Failed to write full sound effects config (%u/%u)",
            static_cast<unsigned int>(written),
            static_cast<unsigned int>(strlen(jsonString)));
        return false;
    }

    LOG_CONFIG_INFO("Sound effects config saved: enabled=%s", enabled ? "true" : "false");
    return true;
}

bool ConfigManager::loadSoundEffectsEnabled(bool& enabled) {
    if (!isSPIFFSInitialized && !initSPIFFS()) {
        LOG_CONFIG_ERROR("Failed to init SPIFFS before loading sound effects config");
        return false;
    }

    if (!SPIFFS.exists(kSoundEffectsConfigPath)) {
        LOG_CONFIG_INFO("Sound effects config not found, keep current default");
        return false;
    }

    File file = SPIFFS.open(kSoundEffectsConfigPath, "r");
    if (!file) {
        LOG_CONFIG_ERROR("Failed to open sound effects config for read: %s", kSoundEffectsConfigPath);
        return false;
    }

    char content[128];
    size_t bytesRead = file.read((uint8_t*)content, sizeof(content) - 1);
    content[bytesRead] = '\0';
    file.close();

    if (bytesRead == 0) {
        LOG_CONFIG_WARN("Sound effects config is empty");
        return false;
    }

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, content);
    if (error) {
        LOG_CONFIG_ERROR("Failed to parse sound effects config: %s", error.c_str());
        return false;
    }

    if (!doc["enabled"].is<bool>()) {
        LOG_CONFIG_WARN("Sound effects config missing boolean field: enabled");
        return false;
    }

    enabled = doc["enabled"].as<bool>();
    LOG_CONFIG_INFO("Sound effects config loaded: enabled=%s", enabled ? "true" : "false");
    return true;
}
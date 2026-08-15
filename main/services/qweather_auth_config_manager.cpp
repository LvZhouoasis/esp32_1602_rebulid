#include "./services/qweather_auth_config_manager.h"

QWeatherAuthConfigManager::QWeatherAuthConfigManager(const char* configFilePath)
    : ConfigManager(configFilePath)
    {
    apiHost[0] = '\0';
    kId[0] = '\0';
    projectID[0] = '\0';
    base64Key[0] = '\0';
    location[0] = '\0';
    cityName[0] = '\0';
    LOG_CONFIG_DEBUG("QWeatherAuthConfigManager initialized with config file: %s", configFilePath);
}

QWeatherAuthConfigManager::~QWeatherAuthConfigManager() {
    LOG_CONFIG_DEBUG("QWeatherAuthConfigManager destroyed");
}

const char* QWeatherAuthConfigManager::getApiHost() {return apiHost;}
const char* QWeatherAuthConfigManager::getKId() {return kId;}
const char* QWeatherAuthConfigManager::getProjectID() {return projectID;}
const char* QWeatherAuthConfigManager::getBase64Key() {return base64Key;}
const char* QWeatherAuthConfigManager::getLocation() {return location;}
const char* QWeatherAuthConfigManager::getCityName() {return cityName;}

bool QWeatherAuthConfigManager::init() {
    if(!loadConfig()){
        if(lastError == Error::FileNotFound){
            LOG_CONFIG_WARN("Config file not found, creating default config");
            if(saveConfig()) {
                setLastQWeatherError(QWeatherError::None);
                return true;
            }
            setLastQWeatherError(QWeatherError::ConfigFileError);
            return false;
        }
        else{
            LOG_CONFIG_ERROR("Failed to load QWeather auth config with error: %s", getLastErrorString(lastError));
            setLastQWeatherError(QWeatherError::ConfigFileError);
            return false;
        }
    }
    return true;
}

bool QWeatherAuthConfigManager::loadConfig() {
    LOG_CONFIG_DEBUG("Loading QWeather auth config from file: %s", configFilePath);
    char configContent[512];
    if (!readFile(configContent, sizeof(configContent))) {
        LOG_CONFIG_WARN("Failed to read QWeather auth config file");
        setLastQWeatherError(QWeatherError::ConfigFileError);
        return false;
    }

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, configContent);
    if (error) {
        LOG_CONFIG_ERROR("Failed to parse QWeather auth config JSON: %s", error.c_str());
        setLastQWeatherError(QWeatherError::ConfigFileError);
        return false;
    }

    strlcpy(apiHost, doc["apiHost"] | "", sizeof(apiHost));
    strlcpy(kId, doc["kId"] | "", sizeof(kId));
    strlcpy(projectID, doc["projectID"] | "", sizeof(projectID));
    strlcpy(base64Key, doc["base64Key"] | "", sizeof(base64Key));
    strlcpy(location, doc["location"] | "", sizeof(location));
    strlcpy(cityName, doc["cityName"] | "", sizeof(cityName));

    return true;
}

bool QWeatherAuthConfigManager::saveConfig() {
    LOG_CONFIG_INFO("Saving QWeather auth config to file: %s", configFilePath);
    JsonDocument doc;
    doc["apiHost"] = apiHost;
    doc["kId"] = kId;
    doc["projectID"] = projectID;
    doc["base64Key"] = base64Key;
    doc["location"] = location;
    doc["cityName"] = cityName;

    char jsonString[512];
    serializeJson(doc, jsonString, sizeof(jsonString));

    if(writeFile(jsonString)){
        LOG_CONFIG_INFO("QWeather auth config saved successfully: %s", jsonString);
        return true;
    }
    else{
        LOG_CONFIG_ERROR("Failed to write QWeather auth config to file");
        setLastQWeatherError(QWeatherError::ConfigFileError);
        return false;
    }
}

bool QWeatherAuthConfigManager::resetConfig() {
    LOG_CONFIG_INFO("Resetting QWeather auth config to defaults");
    apiHost[0] = '\0';
    kId[0] = '\0';
    projectID[0] = '\0';
    base64Key[0] = '\0';
    location[0] = '\0';
    cityName[0] = '\0';

    if(saveConfig()) return true;
    return false;
}

bool QWeatherAuthConfigManager::setAuth(const char* apiHost, const char* kId, const char* projectID, const char* base64Key) {
    strlcpy(this->apiHost, apiHost, sizeof(this->apiHost));
    strlcpy(this->kId, kId, sizeof(this->kId));
    strlcpy(this->projectID, projectID, sizeof(this->projectID));
    strlcpy(this->base64Key, base64Key, sizeof(this->base64Key));

    if(strlen(this->apiHost) == 0 || strlen(this->kId) == 0 ||
       strlen(this->projectID) == 0 || strlen(this->base64Key) == 0){
        LOG_CONFIG_WARN("One or more auth parameters are empty");
        setLastQWeatherError(QWeatherError::EmptyArguments);
        return false;
    }

    // 去除首尾空格
    auto trimStr = [](char* str) {
        // 去除前导空格
        char* start = str;
        while (*start == ' ') start++;
        if (start != str) memmove(str, start, strlen(start) + 1);
        // 去除尾部空格
        size_t len = strlen(str);
        while (len > 0 && str[len - 1] == ' ') {
            str[--len] = '\0';
        }
    };
    trimStr(this->apiHost);
    trimStr(this->kId);
    trimStr(this->projectID);
    trimStr(this->base64Key);

    auto isValidApiHost = [](const char* host)->bool{
        // 简单检测：必须包含点且不含空格
        if (strchr(host, ' ') != NULL) return false;
        if (strchr(host, '.') == NULL) return false;
        return true;
    };

    if(isValidApiHost(this->apiHost) == false){
        LOG_CONFIG_WARN("invalid apiHost format");
        setLastQWeatherError(QWeatherError::InvalidApiHost);
        return false;
    }

    auto isValidID = [](const char* s)->bool{
        if (strlen(s) != 10) return false;
        for (size_t i = 0; i < strlen(s); ++i) {
            char c = s[i];
            if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'))) return false;
        }
        return true;
    };

    if(!isValidID(this->kId)){
        LOG_CONFIG_WARN("invalid kId format");
        setLastQWeatherError(QWeatherError::InvalidKid);
        return false;
    }

    if(!isValidID(this->projectID)){
        LOG_CONFIG_WARN("invalid projectID format");
        setLastQWeatherError(QWeatherError::InvalidProjectID);
        return false;
    }

    return saveConfig();
}

bool QWeatherAuthConfigManager::setLocation(const char* location, const char* cityName) {
    auto isValidLocation = [](const char* loc)->bool{
        for(size_t i = 0; i < strlen(loc); ++i){
            char c = loc[i];
            if(!(c >= '0' && c <= '9')){
                return false;
            }
        }
        return true;
    };
    if(!isValidLocation(location)){
        LOG_CONFIG_WARN("invalid location format");
        setLastQWeatherError(QWeatherError::InvalidLocation);
        return false;
    }

    auto isValidCityName = [](const char* name)->bool{
        for(size_t i = 0; i < strlen(name); ++i){
            char c = name[i];
            if(!( (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c == ' ') )){
                return false;
            }
        }
        return true;
    };
    if(!isValidCityName(cityName)){
        LOG_CONFIG_WARN("invalid cityName format");
        setLastQWeatherError(QWeatherError::InvalidCityName);
        return false;
    }

    strlcpy(this->location, location, sizeof(this->location));
    strlcpy(this->cityName, cityName, sizeof(this->cityName));
    return saveConfig();
}

bool QWeatherAuthConfigManager::checkApiConfigValid() {
    if (strlen(apiHost) == 0 || strlen(kId) == 0 ||
        strlen(projectID) == 0 || strlen(base64Key) == 0) {
        setLastQWeatherError(QWeatherError::EmptyArguments);
        return false;
    }
    setLastQWeatherError(QWeatherError::None);
    return true;
}

bool QWeatherAuthConfigManager::checkLocationConfigValid() {
    if (strlen(location) == 0) {
        setLastQWeatherError(QWeatherError::EmptyArguments);
        return false;
    }
    setLastQWeatherError(QWeatherError::None);
    return true;
}

QWeatherAuthConfigManager::QWeatherError QWeatherAuthConfigManager::getLastQWeatherError() {
    return lastQWeatherError;
}

void QWeatherAuthConfigManager::setLastQWeatherError(QWeatherError error) {
    lastQWeatherError = error;
}

const char* QWeatherAuthConfigManager::getLastQWeatherErrorString() {
    switch (lastQWeatherError) {
        case QWeatherError::None:
            return "No Error";
        case QWeatherError::InvalidApiHost:
            return "Invalid API Host";
        case QWeatherError::InvalidKid:
            return "Invalid Key ID";
        case QWeatherError::InvalidProjectID:
            return "Invalid Project ID";
        case QWeatherError::InvalidBase64Key:
            return "Invalid Base64 Key";
        case QWeatherError::EmptyArguments:
            return "One or more arguments are empty";
        case QWeatherError::ConfigFileError:
            return "Configuration file error";
        default:
            return "Unknown Error";
    }
}
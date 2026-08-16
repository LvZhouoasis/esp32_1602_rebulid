/**
 * @file qweather_auth_config_manager.h
 * @brief 和风天气认证配置管理器
 * 
 * 管理和风天气API的认证信息和位置配置，继承自ConfigManager基类
 */

#ifndef QWEATHER_AUTH_CONFIG_MANAGER_H
#define QWEATHER_AUTH_CONFIG_MANAGER_H

#include "./services/config_manager.h"
#include "./utils/logger.h"

/**
 * @class QWeatherAuthConfigManager
 * @brief 和风天气认证配置管理器类
 * 
 * 管理和风天气API的认证参数和位置信息，包括：
 * - API主机地址
 * - 认证密钥信息(KId, ProjectID, Base64Key)
 * - 位置信息(地点ID和城市名称)
 */
class QWeatherAuthConfigManager : public ConfigManager {
public:
    enum class QWeatherError {
        None = 0,
        InvalidApiHost,
        InvalidKid,
        InvalidProjectID,
        InvalidBase64Key,
        InvalidLocation,
        InvalidCityName,
        EmptyArguments,
        ConfigFileError
    };
private:
    char apiHost[64];     ///< API主机地址
    char kId[33];         ///< 和风天气Key ID
    char projectID[33];   ///< 项目ID
    char base64Key[129];  ///< Base64编码的密钥
    char location[33];    ///< 位置坐标(地点ID)
    char cityName[33];    ///< 城市名称

    QWeatherError lastQWeatherError;  ///< 最后一次和风天气错误类型

    /**
     * @brief 设置最后一次和风天气错误类型
     * @param error 要设置的错误类型
     */
    void setLastQWeatherError(QWeatherError error);

public:
    /**
     * @brief 构造函数
     * @param configFilePath 和风天气配置文件的完整路径
     */
    QWeatherAuthConfigManager(const char* configFilePath);

    /**
     * @brief 析构函数
     */
    ~QWeatherAuthConfigManager() override;

    /**
     * @brief 获取API主机地址
     * @return const char* API主机地址
     */
    const char* getApiHost();

    /**
     * @brief 获取Key ID
     * @return const char* 和风天气Key ID
     */
    const char* getKId();

    /**
     * @brief 获取项目ID
     * @return const char* 项目ID
     */
    const char* getProjectID();

    /**
     * @brief 获取Base64密钥
     * @return const char* Base64编码的密钥
     */
    const char* getBase64Key();

    /**
     * @brief 获取位置坐标
     * @return const char* 位置坐标(经纬度格式)
     */
    const char* getLocation();

    /**
     * @brief 获取城市名称
     * @return const char* 城市名称
     */
    const char* getCityName();

    /**
     * @brief 设置认证信息
     * @param apiHost API主机地址
     * @param kId 和风天气Key ID
     * @param projectID 项目ID
     * @param base64Key Base64编码的密钥
     * @return true 设置成功，false 设置失败
     */
    bool setAuth(const char* apiHost, const char* kId, const char* projectID, const char* base64Key);

    /**
     * @brief 设置位置信息
     * @param location 位置坐标(经纬度)
     * @param cityName 城市名称
     * @return true 设置成功，false 设置失败
     */
    bool setLocation(const char* location, const char* cityName);

    /**
     * @brief 初始化和风天气配置管理器
     * @return true 初始化成功，false 初始化失败
     */
    bool init() override;
    
    /**
     * @brief 从文件加载和风天气配置
     * @return true 加载成功，false 加载失败
     */
    bool loadConfig() override;
    
    /**
     * @brief 保存和风天气配置到文件
     * @return true 保存成功，false 保存失败
     */
    bool saveConfig() override;
    
    /**
     * @brief 重置和风天气配置为默认值
     * @return true 重置成功，false 重置失败
     */
    bool resetConfig() override;

    bool checkApiConfigValid();

    bool checkLocationConfigValid();

    /**
     * @brief 获取最后一次和风天气错误类型
     * @return QWeatherError 错误类型枚举值
     */
    QWeatherError getLastQWeatherError();

    /**
     * @brief 获取和风天气错误类型的字符串描述
     * @param error 错误类型
     * @return const char* 错误描述字符串
     */
    const char* getLastQWeatherErrorString();
};

#endif  // QWEATHER_AUTH_CONFIG_MANAGER_H
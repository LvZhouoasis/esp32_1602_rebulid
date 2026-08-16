#ifndef HTTP_CLIENT_WRAPPER_H
#define HTTP_CLIENT_WRAPPER_H

#include <cstdint>
#include "esp_http_client.h"
#include "wifi_esp32.h"
#include "tcp_server.h"

/**
 * @brief HTTP 客户端封装类，兼容 Arduino HTTPClient API
 *
 * 使用 ESP-IDF esp_http_client 实现
 */
class HttpClientWrapper {
public:
    HttpClientWrapper();
    ~HttpClientWrapper();

    /**
     * @brief 开始 HTTP 请求
     * @param url 请求 URL
     * @return true 成功，false 失败
     */
    bool begin(const char* url);

    /**
     * @brief 开始 HTTP 请求（使用指定的 TCP 客户端）
     * @param client TCP 客户端对象
     * @param url 请求 URL
     * @return true 成功，false 失败
     */
    bool begin(TcpClient& client, const char* url);

    /**
     * @brief 结束 HTTP 请求
     */
    void end();

    /**
     * @brief 添加请求头
     * @param name 头名称
     * @param value 头值
     */
    void addHeader(const char* name, const char* value);

    /**
     * @brief 发送 GET 请求
     * @return HTTP 状态码，-1 表示错误
     */
    int GET();

    /**
     * @brief 获取响应体大小
     * @return 响应体大小（字节），-1 表示未知
     */
    int getSize();

    /**
     * @brief 获取响应流指针
     * @return TcpClient 指针，用于流式读取响应体
     */
    TcpClient* getStreamPtr();

    /**
     * @brief 获取响应体字符串
     * @return 响应体字符串
     */
    String getString();

    /**
     * @brief 检查是否已连接
     * @return true 已连接，false 未连接
     */
    bool connected();

private:
    esp_http_client_handle_t _client;
    bool _initialized;
    int _httpCode;
    int64_t _contentLength;
    char* _responseBuffer;
    size_t _responseBufferSize;

    // 流式读取支持
    TcpClient* _streamClient;
    bool _streamOwned;

    void cleanup();
    static esp_err_t _http_event_handler(esp_http_client_event_t* evt);
};

#endif // HTTP_CLIENT_WRAPPER_H

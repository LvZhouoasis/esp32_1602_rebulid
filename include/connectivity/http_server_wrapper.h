/**
 * @file http_server_wrapper.h
 * @brief ESP-IDF HTTP服务器封装层，提供与Arduino WebServer兼容的API
 *
 * 封装ESP-IDF的httpd API，保持与Arduino WebServer库相似的接口，
 * 便于从Arduino迁移到纯ESP-IDF框架。
 */

#ifndef HTTP_SERVER_WRAPPER_H
#define HTTP_SERVER_WRAPPER_H

#include <cstdint>
#include <cstring>
#include <functional>
#include "esp_http_server.h"
#include "esp_log.h"

// HTTP方法定义（兼容Arduino WebServer）
#define HTTP_GET    0
#define HTTP_POST   1
#define HTTP_PUT    2
#define HTTP_DELETE 3

// 最大路由数量
#define MAX_ROUTES 32

// 最大查询参数数量
#define MAX_ARGS 16

/**
 * @brief 查询参数结构
 */
struct HttpArg {
    char key[64];
    char value[256];
};

/**
 * @brief HTTP请求上下文
 */
struct HttpContext {
    httpd_req_t* req;
    char queryBuffer[512];
    HttpArg args[MAX_ARGS];
    int argCount;
    int method;
};

/**
 * @brief ESP-IDF HTTP服务器封装类（兼容Arduino WebServer）
 */
class HttpServer {
private:
    httpd_handle_t _server;
    int _port;

    // 路由处理
    struct Route {
        char uri[128];
        int method;  // -1 表示所有方法
        std::function<void()> handler;
    };

    Route _routes[MAX_ROUTES];
    int _routeCount;
    std::function<void()> _notFoundHandler;

    // 当前请求上下文（用于在handler中访问请求信息）
    static HttpContext _currentContext;

    // 静态handler包装器
    static esp_err_t _handleRequest(httpd_req_t* req);

    // 解析查询字符串
    static void _parseQueryString(const char* query, HttpContext& ctx);

public:
    /**
     * @brief 构造函数
     * @param port 服务器端口（默认80）
     */
    HttpServer(int port = 80);

    /**
     * @brief 析构函数
     */
    ~HttpServer();

    /**
     * @brief 注册路由处理函数
     * @param uri URI路径
     * @param handler 处理函数
     */
    void on(const char* uri, std::function<void()> handler);

    /**
     * @brief 注册指定方法的路由处理函数
     * @param uri URI路径
     * @param method HTTP方法（HTTP_GET, HTTP_POST等）
     * @param handler 处理函数
     */
    void on(const char* uri, int method, std::function<void()> handler);

    /**
     * @brief 注册404处理函数
     * @param handler 处理函数
     */
    void onNotFound(std::function<void()> handler);

    /**
     * @brief 启动服务器
     */
    void begin();

    /**
     * @brief 停止服务器
     */
    void stop();

    /**
     * @brief 处理客户端请求（非阻塞，ESP-IDF自动处理）
     */
    void handleClient();

    /**
     * @brief 发送HTTP响应
     * @param code HTTP状态码
     * @param contentType 内容类型
     * @param content 响应内容
     */
    void send(int code, const char* contentType, const char* content);

    /**
     * @brief 发送HTTP响应头
     * @param name 头名称
     * @param value 头值
     * @param first 是否为第一个头
     */
    void sendHeader(const char* name, const char* value, bool first = false);

    /**
     * @brief 设置响应内容长度
     * @param length 内容长度
     */
    void setContentLength(int length);

    /**
     * @brief 发送PROGMEM内容块
     * @param content 内容指针
     * @param length 内容长度
     */
    void sendContent_P(const char* content, int length);

    /**
     * @brief 发送响应内容
     * @param content 内容
     */
    void sendContent(const char* content);

    /**
     * @brief 获取查询参数值
     * @param name 参数名
     * @return 参数值
     */
    String arg(const char* name);

    /**
     * @brief 获取POST请求体
     * @return 请求体内容
     */
    String argPlain();

    /**
     * @brief 检查是否存在查询参数
     * @param name 参数名
     * @return true 存在，false 不存在
     */
    bool hasArg(const char* name);

    /**
     * @brief 获取请求方法
     * @return HTTP方法（HTTP_GET, HTTP_POST等）
     */
    int method();

    /**
     * @brief 获取请求URI
     * @return URI字符串
     */
    String uri();

    /**
     * @brief 获取请求头
     * @param name 头名称
     * @return 头值
     */
    String header(const char* name);

    /**
     * @brief 检查是否存在请求头
     * @param name 头名称
     * @return true 存在，false 不存在
     */
    bool hasHeader(const char* name);
};

#endif // HTTP_SERVER_WRAPPER_H

/**
 * @file http_server_wrapper.cpp
 * @brief ESP-IDF HTTP服务器封装层实现
 */

#include "./connectivity/http_server_wrapper.h"
#include <cstring>
#include <cstdio>

static const char* TAG = "HttpServer";

// 静态成员初始化
HttpContext HttpServer::_currentContext = {};

// 构造函数
HttpServer::HttpServer(int port)
    : _server(nullptr), _port(port), _routeCount(0), _notFoundHandler(nullptr) {
    memset(_routes, 0, sizeof(_routes));
}

// 析构函数
HttpServer::~HttpServer() {
    stop();
}

// 解析查询字符串
void HttpServer::_parseQueryString(const char* query, HttpContext& ctx) {
    ctx.argCount = 0;
    if (!query || strlen(query) == 0) return;

    char queryCopy[512];
    strlcpy(queryCopy, query, sizeof(queryCopy));

    char* token = strtok(queryCopy, "&");
    while (token && ctx.argCount < MAX_ARGS) {
        char* eq = strchr(token, '=');
        if (eq) {
            *eq = '\0';
            strlcpy(ctx.args[ctx.argCount].key, token,
                    sizeof(ctx.args[ctx.argCount].key));
            strlcpy(ctx.args[ctx.argCount].value, eq + 1,
                    sizeof(ctx.args[ctx.argCount].value));
            ctx.argCount++;
        }
        token = strtok(nullptr, "&");
    }
}

// 静态handler包装器
esp_err_t HttpServer::_handleRequest(httpd_req_t* req) {
    HttpServer* server = (HttpServer*)req->user_ctx;
    if (!server) return ESP_FAIL;

    // 查找匹配的路由
    for (int i = 0; i < server->_routeCount; i++) {
        if (strcmp(server->_routes[i].uri, req->uri) == 0) {
            // 检查方法是否匹配
            if (server->_routes[i].method != -1) {
                int reqMethod = HTTP_GET;
                if (req->method == HTTP_POST) reqMethod = HTTP_POST;
                else if (req->method == HTTP_PUT) reqMethod = HTTP_PUT;
                else if (req->method == HTTP_DELETE) reqMethod = HTTP_DELETE;

                if (server->_routes[i].method != reqMethod) {
                    continue;
                }
            }

            // 设置当前请求上下文
            _currentContext.req = req;
            _currentContext.method = (req->method == HTTP_POST) ? HTTP_POST : HTTP_GET;

            // 解析查询字符串
            size_t queryLen = httpd_req_get_url_query_str(req, nullptr, 0);
            if (queryLen > 0 && queryLen < sizeof(_currentContext.queryBuffer)) {
                httpd_req_get_url_query_str(req, _currentContext.queryBuffer,
                                            sizeof(_currentContext.queryBuffer));
                _parseQueryString(_currentContext.queryBuffer, _currentContext);
            } else {
                _currentContext.queryBuffer[0] = '\0';
                _currentContext.argCount = 0;
            }

            // 调用处理函数
            server->_routes[i].handler();
            return ESP_OK;
        }
    }

    // 未找到匹配的路由
    if (server->_notFoundHandler) {
        _currentContext.req = req;
        _currentContext.method = (req->method == HTTP_POST) ? HTTP_POST : HTTP_GET;
        server->_notFoundHandler();
        return ESP_OK;
    }

    // 返回404
    httpd_resp_set_status(req, "404 Not Found");
    httpd_resp_send(req, "404 Not Found", 13);
    return ESP_OK;
}

// 注册路由
void HttpServer::on(const char* uri, std::function<void()> handler) {
    if (_routeCount >= MAX_ROUTES) {
        ESP_LOGE(TAG, "Maximum routes reached");
        return;
    }

    strlcpy(_routes[_routeCount].uri, uri, sizeof(_routes[_routeCount].uri));
    _routes[_routeCount].method = -1;  // 所有方法
    _routes[_routeCount].handler = handler;
    _routeCount++;
}

// 注册指定方法的路由
void HttpServer::on(const char* uri, int method, std::function<void()> handler) {
    if (_routeCount >= MAX_ROUTES) {
        ESP_LOGE(TAG, "Maximum routes reached");
        return;
    }

    strlcpy(_routes[_routeCount].uri, uri, sizeof(_routes[_routeCount].uri));
    _routes[_routeCount].method = method;
    _routes[_routeCount].handler = handler;
    _routeCount++;
}

// 注册404处理函数
void HttpServer::onNotFound(std::function<void()> handler) {
    _notFoundHandler = handler;
}

// 启动服务器
void HttpServer::begin() {
    if (_server) {
        ESP_LOGW(TAG, "Server already started");
        return;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = _port;
    config.max_uri_handlers = _routeCount + 1;  // +1 for not found handler
    config.stack_size = 8192;

    esp_err_t err = httpd_start(&_server, &config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start server: %s", esp_err_to_name(err));
        return;
    }

    // 注册所有路由
    for (int i = 0; i < _routeCount; i++) {
        httpd_uri_t uriConfig = {};
        uriConfig.uri = _routes[i].uri;
        uriConfig.method = HTTP_GET;  // 默认GET，后面会处理方法匹配
        uriConfig.handler = _handleRequest;
        uriConfig.user_ctx = this;

        // 根据注册的方法设置HTTP方法
        if (_routes[i].method == HTTP_POST) {
            uriConfig.method = HTTP_POST;
        } else if (_routes[i].method == HTTP_PUT) {
            uriConfig.method = HTTP_PUT;
        } else if (_routes[i].method == HTTP_DELETE) {
            uriConfig.method = HTTP_DELETE;
        } else {
            // 注册为所有方法
            uriConfig.method = HTTP_GET;
            httpd_register_uri_handler(_server, &uriConfig);
            uriConfig.method = HTTP_POST;
        }

        err = httpd_register_uri_handler(_server, &uriConfig);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to register URI %s: %s",
                     _routes[i].uri, esp_err_to_name(err));
        }
    }

    ESP_LOGI(TAG, "Server started on port %d with %d routes", _port, _routeCount);
}

// 停止服务器
void HttpServer::stop() {
    if (_server) {
        httpd_stop(_server);
        _server = nullptr;
        ESP_LOGI(TAG, "Server stopped");
    }
}

// 处理客户端请求（ESP-IDF自动处理，这里只是空操作）
void HttpServer::handleClient() {
    // ESP-IDF的httpd服务器在后台自动处理请求
    // 这里只是兼容Arduino的API
    vTaskDelay(pdMS_TO_TICKS(1));
}

// 发送HTTP响应
void HttpServer::send(int code, const char* contentType, const char* content) {
    if (!_currentContext.req) return;

    // 设置状态码
    char status[32];
    snprintf(status, sizeof(status), "%d", code);
    httpd_resp_set_status(_currentContext.req, status);

    // 设置内容类型
    httpd_resp_set_type(_currentContext.req, contentType);

    // 发送响应
    httpd_resp_send(_currentContext.req, content, strlen(content));
}

// 发送HTTP响应头
void HttpServer::sendHeader(const char* name, const char* value, bool first) {
    if (!_currentContext.req) return;

    httpd_resp_set_hdr(_currentContext.req, name, value);
}

// 设置响应内容长度
void HttpServer::setContentLength(int length) {
    if (!_currentContext.req) return;

    char lenStr[16];
    snprintf(lenStr, sizeof(lenStr), "%d", length);
    httpd_resp_set_hdr(_currentContext.req, "Content-Length", lenStr);
}

// 发送PROGMEM内容块
void HttpServer::sendContent_P(const char* content, int length) {
    if (!_currentContext.req) return;

    httpd_resp_send_chunk(_currentContext.req, content, length);
}

// 发送响应内容
void HttpServer::sendContent(const char* content) {
    if (!_currentContext.req) return;

    httpd_resp_send_chunk(_currentContext.req, content, strlen(content));
}

// 获取查询参数值
String HttpServer::arg(const char* name) {
    for (int i = 0; i < _currentContext.argCount; i++) {
        if (strcmp(_currentContext.args[i].key, name) == 0) {
            return String(_currentContext.args[i].value);
        }
    }
    return String("");
}

// 获取POST请求体
String HttpServer::argPlain() {
    if (!_currentContext.req) return String("");

    // 读取请求体
    char buffer[1024];
    int ret = httpd_req_recv(_currentContext.req, buffer, sizeof(buffer) - 1);
    if (ret <= 0) return String("");

    buffer[ret] = '\0';
    return String(buffer);
}

// 检查是否存在查询参数
bool HttpServer::hasArg(const char* name) {
    for (int i = 0; i < _currentContext.argCount; i++) {
        if (strcmp(_currentContext.args[i].key, name) == 0) {
            return true;
        }
    }
    return false;
}

// 获取请求方法
int HttpServer::method() {
    return _currentContext.method;
}

// 获取请求URI
String HttpServer::uri() {
    if (!_currentContext.req) return String("");
    return String(_currentContext.req->uri);
}

// 获取请求头
String HttpServer::header(const char* name) {
    if (!_currentContext.req) return String("");

    char buffer[256];
    size_t len = httpd_req_get_hdr_value_str(_currentContext.req, name,
                                              buffer, sizeof(buffer));
    if (len > 0) {
        return String(buffer);
    }
    return String("");
}

// 检查是否存在请求头
bool HttpServer::hasHeader(const char* name) {
    if (!_currentContext.req) return false;

    char buffer[1];
    size_t len = httpd_req_get_hdr_value_str(_currentContext.req, name,
                                              buffer, 0);
    return len > 0;
}

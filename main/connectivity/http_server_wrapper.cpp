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
    : _server(nullptr), _port(port), _routeCount(0), _uploadRouteCount(0), _notFoundHandler(nullptr) {
    memset(_routes, 0, sizeof(_routes));
    memset(_uploadRoutes, 0, sizeof(_uploadRoutes));
    memset(&_currentContext.upload, 0, sizeof(_currentContext.upload));
    _currentContext.isUpload = false;
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

// 注册文件上传处理函数
void HttpServer::onUpload(const char* uri, std::function<void()> handler) {
    if (_uploadRouteCount >= MAX_ROUTES) {
        ESP_LOGE(TAG, "Maximum upload routes reached");
        return;
    }

    strlcpy(_uploadRoutes[_uploadRouteCount].uri, uri, sizeof(_uploadRoutes[_uploadRouteCount].uri));
    _uploadRoutes[_uploadRouteCount].handler = handler;
    _uploadRouteCount++;
}

// 上传处理handler
esp_err_t HttpServer::_handleUpload(httpd_req_t* req) {
    HttpServer* server = (HttpServer*)req->user_ctx;
    if (!server) return ESP_FAIL;

    // 查找匹配的上传路由
    for (int i = 0; i < server->_uploadRouteCount; i++) {
        if (strcmp(server->_uploadRoutes[i].uri, req->uri) == 0) {
            // 设置当前请求上下文
            _currentContext.req = req;
            _currentContext.method = HTTP_POST;
            _currentContext.isUpload = true;

            // 获取Content-Type并解析boundary
            char contentType[256] = {0};
            size_t contentTypeLen = httpd_req_get_hdr_value_str(req, "Content-Type", contentType, sizeof(contentType));
            if (contentTypeLen == 0) {
                ESP_LOGE(TAG, "Missing Content-Type header");
                httpd_resp_set_status(req, "400 Bad Request");
                httpd_resp_send(req, "Missing Content-Type", 19);
                return ESP_FAIL;
            }

            // 解析boundary
            char boundary[128] = {0};
            if (_parseMultipartBoundary(contentType, boundary, sizeof(boundary)) != 0) {
                ESP_LOGE(TAG, "Failed to parse boundary from Content-Type: %s", contentType);
                httpd_resp_set_status(req, "400 Bad Request");
                httpd_resp_send(req, "Invalid Content-Type", 20);
                return ESP_FAIL;
            }

            // 读取并处理整个请求体
            size_t totalReceived = 0;
            uint8_t* buffer = (uint8_t*)malloc(UPLOAD_BUF_SIZE);
            if (!buffer) {
                ESP_LOGE(TAG, "Failed to allocate upload buffer");
                httpd_resp_set_status(req, "500 Internal Server Error");
                httpd_resp_send(req, "Memory allocation failed", 24);
                return ESP_FAIL;
            }

            // 累积缓冲区用于边界检测
            uint8_t* accumBuffer = (uint8_t*)malloc(UPLOAD_BUF_SIZE * 2);
            size_t accumLen = 0;
            if (!accumBuffer) {
                free(buffer);
                ESP_LOGE(TAG, "Failed to allocate accum buffer");
                httpd_resp_set_status(req, "500 Internal Server Error");
                httpd_resp_send(req, "Memory allocation failed", 24);
                return ESP_FAIL;
            }

            // 初始化上传状态
            _currentContext.upload.filename = "";
            _currentContext.upload.contentType = "";
            _currentContext.upload.buf = nullptr;
            _currentContext.upload.currentSize = 0;
            _currentContext.upload.totalSize = 0;
            _currentContext.upload.status = UPLOAD_FILE_START;

            bool inBody = false;
            bool headerParsed = false;
            size_t headerEndPos = 0;

            // 触发 FILE_START 回调
            server->_uploadRoutes[i].handler();

            while (true) {
                int received = httpd_req_recv(req, (char*)buffer, UPLOAD_BUF_SIZE);
                if (received <= 0) {
                    if (received == HTTPD_SOCK_ERR_TIMEOUT) {
                        continue;  // 超时重试
                    }
                    break;  // 错误或连接关闭
                }

                totalReceived += received;

                // 将新数据追加到累积缓冲区
                if (accumLen + received > UPLOAD_BUF_SIZE * 2) {
                    // 处理累积缓冲区中的数据
                    if (inBody && headerParsed) {
                        _currentContext.upload.buf = accumBuffer;
                        _currentContext.upload.currentSize = accumLen;
                        _currentContext.upload.totalSize += accumLen;
                        _currentContext.upload.status = UPLOAD_FILE_WRITE;
                        server->_uploadRoutes[i].handler();
                    }
                    accumLen = 0;
                }
                memcpy(accumBuffer + accumLen, buffer, received);
                accumLen += received;

                // 如果还没找到头部结束标记，尝试查找
                if (!headerParsed) {
                    // 查找 \r\n\r\n（头部结束）
                    for (size_t j = 0; j < accumLen - 3; j++) {
                        if (accumBuffer[j] == '\r' && accumBuffer[j+1] == '\n' &&
                            accumBuffer[j+2] == '\r' && accumBuffer[j+3] == '\n') {
                            headerParsed = true;
                            headerEndPos = j + 4;

                            // 解析头部获取filename
                            char header[512] = {0};
                            size_t headerLen = j < sizeof(header) - 1 ? j : sizeof(header) - 1;
                            memcpy(header, accumBuffer, headerLen);

                            // 查找filename
                            const char* fnStart = strstr(header, "filename=\"");
                            if (fnStart) {
                                fnStart += 10;  // 跳过 filename="
                                const char* fnEnd = strchr(fnStart, '"');
                                if (fnEnd) {
                                    static char filenameBuf[128];
                                    size_t fnLen = fnEnd - fnStart;
                                    if (fnLen >= sizeof(filenameBuf)) fnLen = sizeof(filenameBuf) - 1;
                                    memcpy(filenameBuf, fnStart, fnLen);
                                    filenameBuf[fnLen] = '\0';
                                    _currentContext.upload.filename = filenameBuf;
                                }
                            }

                            // 查找Content-Type
                            const char* ctStart = strstr(header, "Content-Type: ");
                            if (ctStart) {
                                ctStart += 14;
                                const char* ctEnd = strstr(ctStart, "\r\n");
                                if (ctEnd) {
                                    static char ctBuf[128];
                                    size_t ctLen = ctEnd - ctStart;
                                    if (ctLen >= sizeof(ctBuf)) ctLen = sizeof(ctBuf) - 1;
                                    memcpy(ctBuf, ctStart, ctLen);
                                    ctBuf[ctLen] = '\0';
                                    _currentContext.upload.contentType = ctBuf;
                                }
                            }

                            // 移动body数据到缓冲区开头
                            size_t bodyLen = accumLen - headerEndPos;
                            memmove(accumBuffer, accumBuffer + headerEndPos, bodyLen);
                            accumLen = bodyLen;
                            inBody = true;
                            break;
                        }
                    }
                }

                // 如果在body中，处理数据
                if (inBody && headerParsed && accumLen > 0) {
                    // 检查是否包含boundary结束标记
                    char endBoundary[128];
                    snprintf(endBoundary, sizeof(endBoundary), "\r\n--%s--", boundary);
                    size_t endBoundaryLen = strlen(endBoundary);

                    // 查找结束boundary
                    bool foundEnd = false;
                    for (size_t j = 0; j < accumLen; j++) {
                        if (j + endBoundaryLen <= accumLen &&
                            memcmp(accumBuffer + j, endBoundary, endBoundaryLen) == 0) {
                            // 找到结束标记，发送剩余数据
                            if (j > 0) {
                                _currentContext.upload.buf = accumBuffer;
                                _currentContext.upload.currentSize = j;
                                _currentContext.upload.totalSize += j;
                                _currentContext.upload.status = UPLOAD_FILE_WRITE;
                                server->_uploadRoutes[i].handler();
                            }
                            foundEnd = true;
                            break;
                        }
                    }

                    if (foundEnd) {
                        break;  // 上传完成
                    }

                    // 没有找到结束标记，发送数据（保留可能的boundary前缀）
                    if (accumLen > 128) {  // 保留一些数据用于boundary检测
                        size_t sendLen = accumLen - 128;
                        _currentContext.upload.buf = accumBuffer;
                        _currentContext.upload.currentSize = sendLen;
                        _currentContext.upload.totalSize += sendLen;
                        _currentContext.upload.status = UPLOAD_FILE_WRITE;
                        server->_uploadRoutes[i].handler();

                        memmove(accumBuffer, accumBuffer + sendLen, accumLen - sendLen);
                        accumLen -= sendLen;
                    }
                }
            }

            // 触发 FILE_END 回调
            _currentContext.upload.status = UPLOAD_FILE_END;
            _currentContext.upload.buf = nullptr;
            _currentContext.upload.currentSize = 0;
            server->_uploadRoutes[i].handler();

            free(buffer);
            free(accumBuffer);
            return ESP_OK;
        }
    }

    // 未找到匹配的上传路由
    httpd_resp_set_status(req, "404 Not Found");
    httpd_resp_send(req, "Upload endpoint not found", 25);
    return ESP_OK;
}

// 解析multipart boundary
int HttpServer::_parseMultipartBoundary(const char* contentType, char* boundary, size_t boundaryLen) {
    const char* boundaryStart = strstr(contentType, "boundary=");
    if (!boundaryStart) {
        return -1;
    }
    boundaryStart += 9;  // 跳过 "boundary="

    // 处理可能的引号
    if (*boundaryStart == '"') {
        boundaryStart++;
        const char* boundaryEnd = strchr(boundaryStart, '"');
        if (!boundaryEnd) {
            return -1;
        }
        size_t len = boundaryEnd - boundaryStart;
        if (len >= boundaryLen) {
            len = boundaryLen - 1;
        }
        memcpy(boundary, boundaryStart, len);
        boundary[len] = '\0';
    } else {
        // 没有引号，取到分号或字符串结尾
        const char* boundaryEnd = strchr(boundaryStart, ';');
        if (!boundaryEnd) {
            boundaryEnd = boundaryStart + strlen(boundaryStart);
        }
        size_t len = boundaryEnd - boundaryStart;
        if (len >= boundaryLen) {
            len = boundaryLen - 1;
        }
        memcpy(boundary, boundaryStart, len);
        boundary[len] = '\0';
    }

    return 0;
}

// 获取上传状态
HTTPUpload& HttpServer::upload() {
    return _currentContext.upload;
}

// 启动服务器
void HttpServer::begin() {
    if (_server) {
        ESP_LOGW(TAG, "Server already started");
        return;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = _port;
    config.max_uri_handlers = _routeCount + _uploadRouteCount + 1;  // +1 for not found handler
    config.stack_size = 8192;
    config.lru_purge_enable = true;  // 启用LRU清理

    esp_err_t err = httpd_start(&_server, &config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start server: %s", esp_err_to_name(err));
        return;
    }

    // 注册所有路由
    for (int i = 0; i < _routeCount; i++) {
        httpd_uri_t uriConfig = {};
        uriConfig.uri = _routes[i].uri;
        uriConfig.handler = _handleRequest;
        uriConfig.user_ctx = this;

        // 根据注册的方法设置HTTP方法
        if (_routes[i].method == -1) {
            // 注册为所有方法
            const httpd_method_t methods[] = {HTTP_GET, HTTP_POST, HTTP_PUT, HTTP_DELETE};
            for (int m = 0; m < 4; m++) {
                uriConfig.method = methods[m];
                err = httpd_register_uri_handler(_server, &uriConfig);
                if (err != ESP_OK) {
                    ESP_LOGE(TAG, "Failed to register URI %s: %s",
                             _routes[i].uri, esp_err_to_name(err));
                }
            }
        } else {
            // 注册为指定方法
            uriConfig.method = (httpd_method_t)_routes[i].method;
            err = httpd_register_uri_handler(_server, &uriConfig);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to register URI %s: %s",
                         _routes[i].uri, esp_err_to_name(err));
            }
        }
    }

    // 注册上传路由
    for (int i = 0; i < _uploadRouteCount; i++) {
        httpd_uri_t uriConfig = {};
        uriConfig.uri = _uploadRoutes[i].uri;
        uriConfig.method = HTTP_POST;  // 上传只接受POST
        uriConfig.handler = _handleUpload;
        uriConfig.user_ctx = this;

        err = httpd_register_uri_handler(_server, &uriConfig);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to register upload URI %s: %s",
                     _uploadRoutes[i].uri, esp_err_to_name(err));
        }
    }

    ESP_LOGI(TAG, "Server started on port %d with %d routes, %d upload routes",
             _port, _routeCount, _uploadRouteCount);
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
const char* HttpServer::arg(const char* name) {
    for (int i = 0; i < _currentContext.argCount; i++) {
        if (strcmp(_currentContext.args[i].key, name) == 0) {
            return _currentContext.args[i].value;
        }
    }
    return "";
}

// 获取POST请求体
const char* HttpServer::argPlain() {
    if (!_currentContext.req) return "";

    // 读取请求体到静态缓冲区
    static char buffer[1024];
    int ret = httpd_req_recv(_currentContext.req, buffer, sizeof(buffer) - 1);
    if (ret <= 0) return "";

    buffer[ret] = '\0';
    return buffer;
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
const char* HttpServer::uri() {
    if (!_currentContext.req) return "";
    return _currentContext.req->uri;
}

// 获取请求头
const char* HttpServer::header(const char* name) {
    if (!_currentContext.req) return "";

    static char buffer[256];
    size_t len = httpd_req_get_hdr_value_str(_currentContext.req, name,
                                              buffer, sizeof(buffer));
    if (len > 0) {
        return buffer;
    }
    return "";
}

// 检查是否存在请求头
bool HttpServer::hasHeader(const char* name) {
    if (!_currentContext.req) return false;

    char buffer[1];
    size_t len = httpd_req_get_hdr_value_str(_currentContext.req, name,
                                              buffer, 0);
    return len > 0;
}

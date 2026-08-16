#include "./connectivity/http_client_wrapper.h"
#include <cstring>
#include "esp_log.h"

static const char* TAG = "HTTP_CLIENT";

HttpClientWrapper::HttpClientWrapper()
    : _client(nullptr)
    , _initialized(false)
    , _httpCode(-1)
    , _contentLength(-1)
    , _responseBuffer(nullptr)
    , _responseBufferSize(0)
    , _streamClient(nullptr)
    , _streamOwned(false) {
}

HttpClientWrapper::~HttpClientWrapper() {
    end();
}

bool HttpClientWrapper::begin(const char* url) {
    if (_initialized) {
        end();
    }

    esp_http_client_config_t config = {};
    config.url = url;
    config.event_handler = _http_event_handler;
    config.user_data = this;
    config.buffer_size = 4096;
    config.buffer_size_tx = 1024;

    _client = esp_http_client_init(&config);
    if (_client == nullptr) {
        ESP_LOGE(TAG, "Failed to initialize HTTP client");
        return false;
    }

    _initialized = true;
    _httpCode = -1;
    _contentLength = -1;

    ESP_LOGD(TAG, "HTTP client initialized for URL: %s", url);
    return true;
}

bool HttpClientWrapper::begin(TcpClient& client, const char* url) {
    // ESP-IDF http_client 不直接支持外部 socket
    // 这里简化实现，使用 esp_http_client 自己管理连接
    (void)client;  // 忽略外部 client
    return begin(url);
}

void HttpClientWrapper::end() {
    cleanup();
}

void HttpClientWrapper::cleanup() {
    if (_client != nullptr) {
        esp_http_client_cleanup(_client);
        _client = nullptr;
    }

    if (_responseBuffer != nullptr) {
        free(_responseBuffer);
        _responseBuffer = nullptr;
    }

    if (_streamOwned && _streamClient != nullptr) {
        delete _streamClient;
        _streamClient = nullptr;
    }

    _initialized = false;
    _httpCode = -1;
    _contentLength = -1;
    _responseBufferSize = 0;
    _streamClient = nullptr;
    _streamOwned = false;
}

void HttpClientWrapper::addHeader(const char* name, const char* value) {
    if (!_initialized || _client == nullptr) {
        return;
    }

    esp_http_client_set_header(_client, name, value);
}

int HttpClientWrapper::GET() {
    if (!_initialized || _client == nullptr) {
        return -1;
    }

    esp_http_client_set_method(_client, HTTP_METHOD_GET);

    esp_err_t err = esp_http_client_open(_client, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP GET failed: %d", err);
        return -1;
    }

    int64_t content_length = esp_http_client_fetch_headers(_client);
    _contentLength = content_length;
    _httpCode = esp_http_client_get_status_code(_client);

    ESP_LOGD(TAG, "HTTP GET status: %d, content_length: %lld", _httpCode, _contentLength);

    return _httpCode;
}

int HttpClientWrapper::getSize() {
    if (!_initialized) {
        return -1;
    }

    return static_cast<int>(_contentLength);
}

TcpClient* HttpClientWrapper::getStreamPtr() {
    // ESP-IDF http_client 使用不同的流式读取方式
    // 这里返回 nullptr，调用者应使用 getString() 或直接读取
    ESP_LOGW(TAG, "getStreamPtr() not fully supported, use getString() instead");
    return nullptr;
}

const char* HttpClientWrapper::getString() {
    if (!_initialized || _client == nullptr) {
        return "";
    }

    // 如果还没有发送请求，先发送 GET
    if (_httpCode == -1) {
        if (GET() < 0) {
            return "";
        }
    }

    // 释放之前的缓冲区
    if (_responseBuffer) {
        free(_responseBuffer);
        _responseBuffer = nullptr;
    }

    // 读取响应体
    int content_length = esp_http_client_get_content_length(_client);
    if (content_length <= 0) {
        content_length = 4096;  // 默认缓冲区大小
    }

    _responseBuffer = (char*)malloc(content_length + 1);
    if (_responseBuffer == nullptr) {
        ESP_LOGE(TAG, "Failed to allocate response buffer");
        return "";
    }

    int read_len = esp_http_client_read_response(_client, _responseBuffer, content_length);
    if (read_len < 0) {
        free(_responseBuffer);
        _responseBuffer = nullptr;
        ESP_LOGE(TAG, "Failed to read response");
        return "";
    }

    _responseBuffer[read_len] = '\0';
    _responseBufferSize = read_len;  // 保存实际长度
    return _responseBuffer;
}

const uint8_t* HttpClientWrapper::getResponseData(size_t* len) {
    if (!_initialized || _client == nullptr) {
        if (len) *len = 0;
        return nullptr;
    }

    // 如果还没有发送请求，先发送 GET
    if (_httpCode == -1) {
        if (GET() < 0) {
            if (len) *len = 0;
            return nullptr;
        }
    }

    // 释放之前的缓冲区
    if (_responseBuffer) {
        free(_responseBuffer);
        _responseBuffer = nullptr;
    }

    // 读取响应体
    int content_length = esp_http_client_get_content_length(_client);
    if (content_length <= 0) {
        content_length = 4096;  // 默认缓冲区大小
    }

    _responseBuffer = (char*)malloc(content_length);
    if (_responseBuffer == nullptr) {
        ESP_LOGE(TAG, "Failed to allocate response buffer");
        if (len) *len = 0;
        return nullptr;
    }

    int read_len = esp_http_client_read_response(_client, _responseBuffer, content_length);
    if (read_len < 0) {
        free(_responseBuffer);
        _responseBuffer = nullptr;
        ESP_LOGE(TAG, "Failed to read response");
        if (len) *len = 0;
        return nullptr;
    }

    _responseBufferSize = read_len;
    if (len) *len = read_len;
    return (const uint8_t*)_responseBuffer;
}

bool HttpClientWrapper::connected() {
    if (!_initialized || _client == nullptr) {
        return false;
    }

    // ESP-IDF http_client 没有直接的 connected() 方法
    // 这里简化实现，假设已连接
    return true;
}

esp_err_t HttpClientWrapper::_http_event_handler(esp_http_client_event_t* evt) {
    HttpClientWrapper* self = static_cast<HttpClientWrapper*>(evt->user_data);

    switch (evt->event_id) {
        case HTTP_EVENT_ERROR:
            ESP_LOGD(TAG, "HTTP_EVENT_ERROR");
            break;
        case HTTP_EVENT_ON_CONNECTED:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_CONNECTED");
            break;
        case HTTP_EVENT_HEADER_SENT:
            ESP_LOGD(TAG, "HTTP_EVENT_HEADER_SENT");
            break;
        case HTTP_EVENT_ON_HEADER:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
            break;
        case HTTP_EVENT_ON_DATA:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
            break;
        case HTTP_EVENT_ON_FINISH:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_FINISH");
            break;
        case HTTP_EVENT_DISCONNECTED:
            ESP_LOGD(TAG, "HTTP_EVENT_DISCONNECTED");
            break;
        default:
            break;
    }

    return ESP_OK;
}

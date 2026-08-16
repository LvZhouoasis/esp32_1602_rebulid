#include "./connectivity/dns_server.h"
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include "esp_log.h"

static const char* TAG = "DNS_SERVER";

// DNS 协议常量
#define DNS_HEADER_SIZE 12
#define DNS_FLAG_RESPONSE 0x8000
#define DNS_FLAG_AUTHORITATIVE 0x0400
#define DNS_CLASS_IN 1
#define DNS_TYPE_A 1
#define DNS_OFFSET_ERROR 0x0003

DNSServer::DNSServer()
    : _socket(-1)
    , _port(53)
    , _running(false) {
    memset(_domain, 0, sizeof(_domain));
    memset(&_responseIP, 0, sizeof(_responseIP));
    memset(_buffer, 0, sizeof(_buffer));
}

DNSServer::~DNSServer() {
    stop();
}

bool DNSServer::start(uint16_t port, const char* domain, esp_ip4_addr_t ip) {
    if (_running) {
        stop();
    }

    _port = port;
    _responseIP = ip;
    strncpy(_domain, domain, sizeof(_domain) - 1);

    // 创建 UDP socket
    _socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (_socket < 0) {
        ESP_LOGE(TAG, "Failed to create socket: errno %d", errno);
        return false;
    }

    // 设置为非阻塞模式
    int flags = fcntl(_socket, F_GETFL, 0);
    if (flags < 0 || fcntl(_socket, F_SETFL, flags | O_NONBLOCK) < 0) {
        ESP_LOGE(TAG, "Failed to set non-blocking: errno %d", errno);
        close(_socket);
        _socket = -1;
        return false;
    }

    // 绑定地址
    struct sockaddr_in serverAddr;
    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = htonl(INADDR_ANY);
    serverAddr.sin_port = htons(_port);

    if (bind(_socket, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
        ESP_LOGE(TAG, "Failed to bind socket: errno %d", errno);
        close(_socket);
        _socket = -1;
        return false;
    }

    _running = true;
    ESP_LOGI(TAG, "DNS server started on port %d, domain: %s, IP: %s",
             _port, _domain, ip4addr_ntoa((const ip4_addr_t*)&_responseIP));
    return true;
}

void DNSServer::stop() {
    if (_socket >= 0) {
        close(_socket);
        _socket = -1;
    }
    _running = false;
    ESP_LOGI(TAG, "DNS server stopped");
}

void DNSServer::processNextRequest() {
    if (!_running || _socket < 0) {
        return;
    }

    struct sockaddr_in clientAddr;
    socklen_t clientAddrLen = sizeof(clientAddr);

    ssize_t len = recvfrom(_socket, _buffer, sizeof(_buffer), 0,
                           (struct sockaddr*)&clientAddr, &clientAddrLen);

    if (len > 0 && len >= DNS_HEADER_SIZE) {
        handleDNSQuery(_buffer, len, (struct sockaddr*)&clientAddr, clientAddrLen);
    }
}

void DNSServer::handleDNSQuery(uint8_t* data, size_t len, struct sockaddr* clientAddr, socklen_t clientAddrLen) {
    // 解析 DNS 头部
    // uint16_t transactionId = (data[0] << 8) | data[1];  // 未使用，保留用于调试
    uint16_t flags = (data[2] << 8) | data[3];
    uint16_t questionCount = (data[4] << 8) | data[5];

    // 只处理查询请求
    if ((flags & DNS_FLAG_RESPONSE) || questionCount == 0) {
        return;
    }

    // 解析查询的域名
    char queryDomain[256];
    size_t offset = DNS_HEADER_SIZE;
    size_t domainLen = 0;

    while (offset < len && data[offset] != 0) {
        uint8_t labelLen = data[offset];
        if (labelLen > 63) {
            return;  // 无效的标签长度
        }
        offset++;
        if (offset + labelLen > len) {
            return;  // 超出缓冲区
        }
        if (domainLen > 0 && domainLen < sizeof(queryDomain) - 1) {
            queryDomain[domainLen++] = '.';
        }
        for (uint8_t i = 0; i < labelLen && domainLen < sizeof(queryDomain) - 1; i++) {
            queryDomain[domainLen++] = data[offset + i];
        }
        offset += labelLen;
    }
    queryDomain[domainLen] = '\0';

    ESP_LOGD(TAG, "DNS query for: %s from %s:%d",
             queryDomain,
             inet_ntoa(((struct sockaddr_in*)clientAddr)->sin_addr),
             ntohs(((struct sockaddr_in*)clientAddr)->sin_port));

    // 检查域名是否匹配（"*" 匹配所有）
    bool match = (strcmp(_domain, "*") == 0) || (strcmp(_domain, queryDomain) == 0);

    // 构建响应
    uint8_t response[512];
    size_t responseLen = 0;

    if (match) {
        buildDNSResponse(data, len, response, &responseLen);
    } else {
        // 对于不匹配的域名，返回 NXDOMAIN
        memcpy(response, data, len);
        responseLen = len;
        // 设置响应标志和 RCODE=3 (NXDOMAIN)
        response[2] = 0x81;  // QR=1, RD=1
        response[3] = 0x83;  // RA=1, RCODE=3
        // 设置 ANCOUNT=0
        response[6] = 0;
        response[7] = 0;
    }

    // 发送响应
    sendto(_socket, response, responseLen, 0, clientAddr, clientAddrLen);
}

void DNSServer::buildDNSResponse(uint8_t* query, size_t queryLen, uint8_t* response, size_t* responseLen) {
    // 复制查询作为响应基础
    memcpy(response, query, queryLen);
    *responseLen = queryLen;

    // 设置响应标志
    response[2] = 0x85;  // QR=1, RD=1, RA=1
    response[3] = 0x80;  // RCODE=0

    // 设置 ANCOUNT=1
    response[6] = 0;
    response[7] = 1;

    // 添加回答记录
    // 名称指针（指向查询中的域名）
    response[(*responseLen)++] = 0xC0;
    response[(*responseLen)++] = 0x0C;

    // 类型 A
    response[(*responseLen)++] = 0x00;
    response[(*responseLen)++] = DNS_TYPE_A;

    // 类 IN
    response[(*responseLen)++] = 0x00;
    response[(*responseLen)++] = DNS_CLASS_IN;

    // TTL (60 秒)
    response[(*responseLen)++] = 0x00;
    response[(*responseLen)++] = 0x00;
    response[(*responseLen)++] = 0x00;
    response[(*responseLen)++] = 0x3C;

    // 数据长度 (4 字节 IPv4 地址)
    response[(*responseLen)++] = 0x00;
    response[(*responseLen)++] = 0x04;

    // IP 地址
    uint32_t ip = _responseIP.addr;
    response[(*responseLen)++] = (ip >> 0) & 0xFF;
    response[(*responseLen)++] = (ip >> 8) & 0xFF;
    response[(*responseLen)++] = (ip >> 16) & 0xFF;
    response[(*responseLen)++] = (ip >> 24) & 0xFF;
}

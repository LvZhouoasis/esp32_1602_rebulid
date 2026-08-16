#ifndef DNS_SERVER_H
#define DNS_SERVER_H

#include <cstdint>
#include "esp_netif.h"

/**
 * @brief 简单的 DNS 服务器，用于强制门户（Captive Portal）
 *
 * 监听 UDP 53 端口，将所有域名查询响应为指定的 IP 地址
 * 当用户连接到 AP 并尝试访问任何网站时，会被重定向到配网页面
 */
class DNSServer {
public:
    DNSServer();
    ~DNSServer();

    /**
     * @brief 启动 DNS 服务器
     * @param port 监听端口（默认 53）
     * @param domain 拦截的域名（"*" 表示所有域名）
     * @param ip 响应的 IP 地址
     * @return true 启动成功，false 启动失败
     */
    bool start(uint16_t port, const char* domain, esp_ip4_addr_t ip);

    /**
     * @brief 停止 DNS 服务器
     */
    void stop();

    /**
     * @brief 处理 DNS 请求（非阻塞）
     * 应在主循环中调用
     */
    void processNextRequest();

    /**
     * @brief 检查服务器是否正在运行
     */
    bool isRunning() const { return _running; }

private:
    int _socket;
    uint16_t _port;
    char _domain[64];
    esp_ip4_addr_t _responseIP;
    bool _running;
    uint8_t _buffer[512];

    void handleDNSQuery(uint8_t* data, size_t len, struct sockaddr* clientAddr, socklen_t clientAddrLen);
    void buildDNSResponse(uint8_t* query, size_t queryLen, uint8_t* response, size_t* responseLen);
};

#endif // DNS_SERVER_H

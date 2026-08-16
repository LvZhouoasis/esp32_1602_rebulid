#ifndef TCP_SERVER_H
#define TCP_SERVER_H

#include <cstdint>
#include "esp_netif.h"
#include "wifi_esp32.h"

/**
 * @brief TCP 客户端封装类，兼容 Arduino WiFiClient API
 *
 * 使用 ESP-IDF BSD Socket API 实现
 */
class TcpClient {
public:
    TcpClient();
    TcpClient(int socket);
    ~TcpClient();

    /**
     * @brief 检查客户端是否已连接
     * @return true 已连接，false 未连接
     */
    bool connected();

    /**
     * @brief 获取可读取的数据字节数
     * @return 可读字节数，-1 表示无连接
     */
    int available();

    /**
     * @brief 读取数据
     * @param buffer 数据缓冲区
     * @param length 缓冲区长度
     * @return 实际读取的字节数，-1 表示错误
     */
    int read(uint8_t* buffer, size_t length);

    /**
     * @brief 停止连接
     */
    void stop();

    /**
     * @brief 设置 TCP_NODELAY 选项
     * @param enable true 启用，false 禁用
     */
    void setNoDelay(bool enable);

    /**
     * @brief 获取远程 IP 地址
     * @return IPAddress 对象
     */
    IPAddress remoteIP();

    /**
     * @brief 获取远程端口
     * @return 端口号
     */
    int remotePort();

    /**
     * @brief 检查客户端是否有效
     * @return true 有效，false 无效
     */
    operator bool();

    /**
     * @brief 获取底层 socket
     * @return socket 文件描述符
     */
    int getSocket() const { return _socket; }

private:
    int _socket;
    bool _connected;
    IPAddress _remoteIP;
    int _remotePort;
};

/**
 * @brief TCP 服务器封装类，兼容 Arduino WiFiServer API
 *
 * 使用 ESP-IDF BSD Socket API 实现
 */
class TcpServer {
public:
    TcpServer(int port);
    ~TcpServer();

    /**
     * @brief 启动服务器
     * @return true 成功，false 失败
     */
    bool begin();

    /**
     * @brief 停止服务器
     */
    void end();

    /**
     * @brief 接受客户端连接（非阻塞）
     * @return TcpClient 对象，如果无连接则返回无效的 TcpClient
     */
    TcpClient accept();

    /**
     * @brief 检查服务器是否正在监听
     * @return true 正在监听，false 未监听
     */
    bool isListening() const { return _listening; }

private:
    int _socket;
    int _port;
    bool _listening;
};

#endif // TCP_SERVER_H

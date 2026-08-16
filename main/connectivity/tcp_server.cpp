#include "./connectivity/tcp_server.h"
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <poll.h>
#include <sys/ioctl.h>
#include "esp_log.h"

static const char* TAG = "TCP_SERVER";

// ============================================================================
// TcpClient 实现
// ============================================================================

TcpClient::TcpClient()
    : _socket(-1)
    , _connected(false)
    , _remotePort(0) {
}

TcpClient::TcpClient(int socket)
    : _socket(socket)
    , _connected(true)
    , _remotePort(0) {
    if (_socket >= 0) {
        // 获取远程地址信息
        struct sockaddr_in addr;
        socklen_t addrLen = sizeof(addr);
        if (getpeername(_socket, (struct sockaddr*)&addr, &addrLen) == 0) {
            _remoteIP = IPAddress(addr.sin_addr.s_addr);
            _remotePort = ntohs(addr.sin_port);
        }
    }
}

TcpClient::~TcpClient() {
    // 不在析构函数中关闭 socket，避免重复关闭
    // 调用者应显式调用 stop()
}

bool TcpClient::connected() {
    if (_socket < 0) {
        _connected = false;
        return false;
    }

    // 使用 poll 检查连接状态
    struct pollfd pfd;
    pfd.fd = _socket;
    pfd.events = POLLIN;
    pfd.revents = 0;

    int ret = poll(&pfd, 1, 0);
    if (ret < 0) {
        // 错误
        _connected = false;
        return false;
    }

    if (ret > 0 && (pfd.revents & POLLERR)) {
        // 连接错误
        _connected = false;
        return false;
    }

    // 如果有数据可读或无事件，认为连接正常
    return _connected;
}

int TcpClient::available() {
    if (_socket < 0) {
        return 0;
    }

    // 使用 ioctl 获取可读字节数
    int bytesAvailable = 0;
    if (ioctl(_socket, FIONREAD, &bytesAvailable) < 0) {
        return 0;
    }

    return bytesAvailable;
}

int TcpClient::read(uint8_t* buffer, size_t length) {
    if (_socket < 0 || !_connected) {
        return -1;
    }

    ssize_t bytesRead = recv(_socket, buffer, length, 0);
    if (bytesRead < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            // 非阻塞模式下无数据
            return 0;
        }
        // 其他错误
        _connected = false;
        return -1;
    }

    if (bytesRead == 0) {
        // 对端关闭连接
        _connected = false;
        return -1;
    }

    return static_cast<int>(bytesRead);
}

void TcpClient::stop() {
    if (_socket >= 0) {
        close(_socket);
        _socket = -1;
    }
    _connected = false;
}

void TcpClient::setNoDelay(bool enable) {
    if (_socket < 0) {
        return;
    }

    int flag = enable ? 1 : 0;
    setsockopt(_socket, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));
}

IPAddress TcpClient::remoteIP() {
    return _remoteIP;
}

int TcpClient::remotePort() {
    return _remotePort;
}

TcpClient::operator bool() {
    return _socket >= 0 && _connected;
}

// ============================================================================
// TcpServer 实现
// ============================================================================

TcpServer::TcpServer(int port)
    : _socket(-1)
    , _port(port)
    , _listening(false) {
}

TcpServer::~TcpServer() {
    end();
}

bool TcpServer::begin() {
    if (_listening) {
        return true;
    }

    // 创建 socket
    _socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (_socket < 0) {
        ESP_LOGE(TAG, "Failed to create socket: errno %d", errno);
        return false;
    }

    // 设置 SO_REUSEADDR
    int reuseAddr = 1;
    setsockopt(_socket, SOL_SOCKET, SO_REUSEADDR, &reuseAddr, sizeof(reuseAddr));

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

    // 开始监听
    if (listen(_socket, 1) < 0) {
        ESP_LOGE(TAG, "Failed to listen: errno %d", errno);
        close(_socket);
        _socket = -1;
        return false;
    }

    _listening = true;
    ESP_LOGI(TAG, "TCP server started on port %d", _port);
    return true;
}

void TcpServer::end() {
    if (_socket >= 0) {
        close(_socket);
        _socket = -1;
    }
    _listening = false;
    ESP_LOGI(TAG, "TCP server stopped");
}

TcpClient TcpServer::accept() {
    if (!_listening || _socket < 0) {
        return TcpClient();
    }

    struct sockaddr_in clientAddr;
    socklen_t clientAddrLen = sizeof(clientAddr);

    int clientSocket = ::accept(_socket, (struct sockaddr*)&clientAddr, &clientAddrLen);
    if (clientSocket < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            // 非阻塞模式下无连接
            return TcpClient();
        }
        ESP_LOGE(TAG, "Failed to accept: errno %d", errno);
        return TcpClient();
    }

    // 设置客户端 socket 为非阻塞模式
    int flags = fcntl(clientSocket, F_GETFL, 0);
    if (flags >= 0) {
        fcntl(clientSocket, F_SETFL, flags | O_NONBLOCK);
    }

    ESP_LOGI(TAG, "Client connected from %s:%d",
             inet_ntoa(clientAddr.sin_addr),
             ntohs(clientAddr.sin_port));

    return TcpClient(clientSocket);
}

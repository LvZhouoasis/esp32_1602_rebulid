#include "./connectivity/wifi_config.h"
#include "./connectivity/dns_server.h"
#include <cstring>

extern WifiConfigManager wifiConfigManager;

// 配网模式使用的 Web 服务器（监听端口 80）
HttpServer apServer(80);

// DNS 服务器用于强制门户
DNSServer dnsServer;
const uint16_t DNS_PORT = 53;

// 当前是否处于配网模式的标志位
bool inConfigMode = false;

// 待执行重启标志（在回调中不能直接调用 ESP.restart()，否则会在 WiFi 事件回调中卡死）
volatile bool pendingRestart = false;

// WiFi连接状态
WiFiConnectionState wifiConnectionState = WIFI_IDLE;

// 防止重复创建连接任务 / 时间同步任务导致资源耗尽和状态抖动
static TaskHandle_t wifiConnectTaskHandle = nullptr;
static TaskHandle_t timeSyncTaskHandle = nullptr;

void ensureTimeSyncTaskRunning() {
	if (timeSyncTaskHandle != nullptr) {
		return;
	}

	BaseType_t rc = xTaskCreatePinnedToCore(
		timeSyncTask,
		"TimeSyncTask",
		6144,
		&timeSyncTaskHandle,
		1,
		&timeSyncTaskHandle,
		0
	);

	if (rc != pdPASS) {
		LOG_WIFI_ERROR("Failed to create TimeSyncTask (rc=%ld)", static_cast<long>(rc));
		timeSyncTaskHandle = nullptr;
	} else {
		LOG_WIFI_INFO("TimeSyncTask started");
	}
}

// 扫描状态
WifiScanState wifiScanState = WIFI_SCAN_IDLE;
char scanResult[1024] = "";

// 保存WiFi信息
void _saveWiFiCredentials(const char* ssid, const char* password) {
	wifiConfigManager.setSSID(ssid);
	wifiConfigManager.setPassword(password);

	LOG_WIFI_INFO("WiFi config saved, restarting...");
	lcdText("Config Saved", 1);
	lcdText("Restarting...", 2);
}

void wifiConfigHandler(){
	// 计算总长度
    unsigned int len1 = strlen_P(webComponent);
    unsigned int len2 = strlen_P(wifiConfigHtml);
    unsigned int totalLen = len1 + len2;

    // 设置 Content-Length 并发送头（空 body）
    apServer.setContentLength(totalLen);
    apServer.send(200, "text/html; charset=utf-8", "");

    // 直接发送 PROGMEM 内容块
    apServer.sendContent_P(webComponent, len1);
    apServer.sendContent_P(wifiConfigHtml, len2);
}

void wifiScanhandler(){
	if (wifiScanState == WIFI_SCAN_IDLE) {
        wifiScanState = WIFI_SCAN_SCANNING;
        apServer.send(202, "application/json", "{\"status\":\"scanning\"}");
        
        // 创建任务进行WiFi扫描
        xTaskCreate([](void*){
            int n = WiFi.scanNetworks();
            LOG_NETWORK_INFO("Find %d WiFi!", n);

            size_t pos = 0;
            pos += snprintf(scanResult + pos, sizeof(scanResult) - pos, "{\"status\":\"done\",\"networks\":[");

            for (int i = 0; i < n && pos < sizeof(scanResult) - 1; ++i) {
                char ssidBuf[33];
                strlcpy(ssidBuf, WiFi.SSID(i).c_str(), sizeof(ssidBuf));
                pos += snprintf(scanResult + pos, sizeof(scanResult) - pos,
                    "{\"ssid\":\"%s\",\"rssi\":%d,\"secure\":%s}%s",
                    ssidBuf, WiFi.RSSI(i),
                    (WiFi.encryptionType(i) != WIFI_AUTH_OPEN) ? "true" : "false",
                    (i != n - 1) ? "," : "");
            }

            pos += snprintf(scanResult + pos, sizeof(scanResult) - pos, "]}");
            wifiScanState = WIFI_SCAN_DONE;
			LOG_NETWORK_DEBUG("%s", scanResult);
            vTaskDelete(NULL);
        }, "ScanTask", 4096, NULL, 1, NULL);
    } 

	else {
        if (wifiScanState != WIFI_SCAN_DONE) {
			LOG_NETWORK_DEBUG("send HTTP 202 scanning");
            apServer.send(202, "application/json", "{\"status\":\"scanning\"}");
        } else if (scanResult[0] != '\0') {
            apServer.send(200, "application/json", scanResult);
            scanResult[0] = '\0';
        } else {
			LOG_NETWORK_DEBUG("send HTTP 500 (no results)");
            apServer.send(500, "application/json", "{\"error\":\"no result\"}");
			wifiScanState = WIFI_SCAN_IDLE;
        }
    }
}

void wifiSethandler(){
	const char* ssid = apServer.arg("ssid").c_str();
	const char* password = apServer.arg("password").c_str();
	LOG_NETWORK_INFO("access /wifi_set");
	LOG_NETWORK_INFO("ssid: %s", ssid);
	_saveWiFiCredentials(ssid, password);
	apServer.send(200, "application/json", "{\"success\":true}");
	// 不在回调内直接 restart，否则 WiFi AP/STA 事件会在重启过程中乱序触发导致卡死
	pendingRestart = true;
}

// 进入配网
void enterConfigMode() {
	inConfigMode = true;  // 先置位，让正在运行的 wifiConnectTask 感知并跳过 WiFi.mode(WIFI_OFF)

	updateColor(CRGB::Purple);  // 配网紫灯

	WiFi.softAP("1602A_Config");

    LOG_WIFI_INFO("Entering config mode");
	LOG_WIFI_INFO("Config webpage started at IP: %s", WiFi.softAPIP().toString().c_str());

	// 启动DNS服务器，将所有域名请求劫持到ESP32的IP
	IPAddress softIP = WiFi.softAPIP();
	esp_ip4_addr_t ipAddr;
	ipAddr.addr = softIP;  // IPAddress 可以隐式转换为 uint32_t
	dnsServer.start(DNS_PORT, "*", ipAddr);

	// 捕获所有DNS请求并重定向到配网页面
	apServer.onNotFound([](){
		char location[32];
		snprintf(location, sizeof(location), "http://%s", WiFi.softAPIP().toString().c_str());
		apServer.sendHeader("Location", location, true);
		apServer.send(302, "text/plain", "");
	});

    // 配网页面
	apServer.on("/", wifiConfigHandler);

    // 扫描WiFi并展示列表
	apServer.on("/wifi_scan", wifiScanhandler);

	// 处理WiFi信息提交
	apServer.on("/wifi_set", wifiSethandler);

	// 常见的强制门户检测端点
	// Android 设备检测
	apServer.on("/generate_204", [](){
		char location[32];
		snprintf(location, sizeof(location), "http://%s", WiFi.softAPIP().toString().c_str());
		apServer.sendHeader("Location", location, true);
		apServer.send(302, "text/plain", "");
	});

	// iOS 设备检测
	apServer.on("/hotspot-detect.html", [](){
		char location[32];
		snprintf(location, sizeof(location), "http://%s", WiFi.softAPIP().toString().c_str());
		apServer.sendHeader("Location", location, true);
		apServer.send(302, "text/plain", "");
	});

	// Windows 设备检测
	apServer.on("/ncsi.txt", [](){
		char location[32];
		snprintf(location, sizeof(location), "http://%s", WiFi.softAPIP().toString().c_str());
		apServer.sendHeader("Location", location, true);
		apServer.send(302, "text/plain", "");
	});

	// 通用重定向端点
	apServer.on("/redirect", [](){
		char location[32];
		snprintf(location, sizeof(location), "http://%s", WiFi.softAPIP().toString().c_str());
		apServer.sendHeader("Location", location, true);
		apServer.send(302, "text/plain", "");
	});

	apServer.begin();

	// 在屏幕上显示ip
	lcdText("Connect to AP",1);
	char ipBuf[17];
	snprintf(ipBuf, sizeof(ipBuf), "IP:%s", WiFi.softAPIP().toString().c_str());
	lcdText(ipBuf, 2);
}

// WiFi连接后台任务
void wifiConnectTask(void* parameter) {
	LOG_WIFI_DEBUG("WiFi connection task started");
	
	// 等待一小段时间，确保网络栈完全初始化
	vTaskDelay(100 / portTICK_PERIOD_MS);
	
	updateColor(CRGB::Blue);  // 连接中蓝灯

	// 避免底层自动重连导致“超时失败后仍在后台不断重试”，这里交由上层逻辑控制重连
	WiFi.persistent(false);
	WiFi.setAutoReconnect(false);

	// 连接期间关闭省电，避免连接抖动/状态不同步
	WiFi.setSleep(false);
	
	WiFi.begin(wifiConfigManager.getSSID(), wifiConfigManager.getPassword());
	
	unsigned long startTime = GET_MS();
	int fadeStep = 2;
	uint8_t brightness = 64;
	
	// 连接中蓝灯闪烁
	uint32_t lastDotMs = 0;
	while (GET_MS() - startTime < 15000 && !shouldExitTasks && !inConfigMode) {
		const IPAddress ipNow = WiFi.localIP();
		if (WiFi.status() == WL_CONNECTED || ipNow != IPAddress(0, 0, 0, 0)) {
			break;
		}

		brightness += fadeStep;

		if (brightness == 0 || brightness == 192) {
				fadeStep = -fadeStep;
		}

		const uint32_t nowMs = GET_MS();
		if ((uint32_t)(nowMs - lastDotMs) >= 500) {
			lastDotMs = nowMs;
			LOG_WIFI_DEBUG(".");
		}
		updateBrightness(brightness);
		vTaskDelay(5 / portTICK_PERIOD_MS);
	}
	updateBrightness(128);
	
	// 检查是否因睡眠退出
	if (shouldExitTasks) {
		LOG_WIFI_INFO("WiFi connect task exiting due to sleep request");
		vTaskDelay(pdMS_TO_TICKS(5));
		vTaskDelete(NULL);
		return;
	}

	const IPAddress ipAfter = WiFi.localIP();
	if (inConfigMode) {
		// 配网模式已启动，跳过连接结果处理，不修改 WiFi 状态
		LOG_WIFI_INFO("WiFi connect task: config mode active, aborting without touching WiFi mode");
	} else if (WiFi.status() == WL_CONNECTED || ipAfter != IPAddress(0, 0, 0, 0)) {
		wifiConnectionState = WIFI_CONNECTED;
		LOG_WIFI_DEBUG("WiFi connected successfully");
		
		updateColor(CRGB::Green);  	// 连接成功绿灯
		updateBrightness(10);
		
		// 等待 DHCP 分配到有效 IP
		unsigned long ipWaitStart = GET_MS();
		IPAddress ip = WiFi.localIP();
		while (ip == IPAddress(0, 0, 0, 0) && (GET_MS() - ipWaitStart) < 5000 && !shouldExitTasks) {
			vTaskDelay(pdMS_TO_TICKS(50));
			ip = WiFi.localIP();
		}
		LOG_WIFI_DEBUG("STA IP after connect: %s", ip.toString().c_str());

		// 启动TCP服务器（放在 IP/DNS 完成后）
		LOG_WIFI_DEBUG("Starting TCP server...");
		server.begin();
		LOG_WIFI_INFO("TCP server started on port %d", CONNECT_PORT);

		LOG_WIFI_INFO("connected: %s", wifiConfigManager.getSSID());
		LOG_WIFI_INFO("IP: %s", WiFi.localIP().toString().c_str());
		LOG_WIFI_DEBUG("starting background time sync...");

		// 稳定性优先：保持关闭 WiFi 睡眠，避免在当前固件中再次触发 pm/idle 相关异常。
		WiFi.setSleep(false);

		// 创建后台时间同步任务（initNtpTimeSync 移到后台任务中，避免阻塞）
		ensureTimeSyncTaskRunning();
	} else {
			wifiConnectionState = WIFI_FAILED;
			// 关闭射频，防止 WiFi 底层在后台继续自动尝试连接
			// 仅在非配网模式下关闭，避免把已启动的 AP 一并关掉
			if (!inConfigMode) {
				WiFi.disconnect(true);
				WiFi.mode(WIFI_OFF);
			}
			LOG_WIFI_ERROR("can't connect to WiFi");
			updateColor(CRGB::Red);  // 失败变红
	}
	
	// 任务完成，删除任务自身
	vTaskDelay(pdMS_TO_TICKS(5));	//延迟5MS确保RGB灯状态更新
	wifiConnectTaskHandle = nullptr;
	vTaskDelete(NULL);
}

void connectToWiFi() {
		if(strlen(wifiConfigManager.getSSID()) == 0){
			LOG_WIFI_WARN("config not found");
			wifiConnectionState = WIFI_FAILED;
			updateColor(CRGB::Red);  		// 无配置红灯
			return;
		}

		// 避免重复创建连接任务
		if (wifiConnectionState == WIFI_CONNECTING || wifiConnectTaskHandle != nullptr) {
			LOG_WIFI_DEBUG("WiFi connect already in progress, skip.");
			return;
		}

		LOG_WIFI_INFO("will connect to: %s", wifiConfigManager.getSSID());

		// 连接前做一次硬断开，清理底层状态/停止可能存在的后台重连
		WiFi.persistent(false);
		WiFi.setAutoReconnect(false);
		WiFi.disconnect(true);
		WiFi.mode(WIFI_OFF);
		vTaskDelay(pdMS_TO_TICKS(50));

		// 设置连接中状态
		wifiConnectionState = WIFI_CONNECTING;
		
		// 先设置WiFi模式，确保网络栈已初始化
		WiFi.mode(WIFI_STA);
		vTaskDelay(50 / portTICK_PERIOD_MS);  // 给WiFi栈一点时间初始化
		
		// 创建后台任务进行WiFi连接，不阻塞主线程
		xTaskCreatePinnedToCore(wifiConnectTask, "WiFiConnectTask", 6144, NULL, 1, &wifiConnectTaskHandle, 0);
}

// 初始化wifi
void wifiinit(){
	if (digitalRead(BUTTON_CENTER_PIN)) {
			LOG_WIFI_INFO("Entering config mode by button");
			enterConfigMode();
	} else {
			connectToWiFi();
	}
}
#include "./services/protocol.h"

#include "./services/frame_stats.h"

static unsigned int frameCount = 0;
static float currentFPS = 0.0f;
static uint32_t lastStatsMs = 0;
static uint32_t lastEnqueued = 0;
static uint32_t lastDropped = 0;

// 解码函数
void processIncoming(const uint8_t* raw, unsigned int fullLen) {
    if (inMenuMode) return;
    
    LOG_DISPLAY_VERBOSE("Processing incoming data frame of length %u bytes", fullLen);

    // LOG_DISPLAY_VERBOSE("Raw bytes: ");
    // for (unsigned int i = 0; i < fullLen; i++) {
    //     LOG_DISPLAY_VERBOSE("0x" + String(raw[i], HEX));
    //     LOG_DISPLAY_VERBOSE(" ");
    // }

    const uint32_t now = GET_MS();
    if (lastStatsMs == 0) {
        lastStatsMs = now;
        lastEnqueued = gFramesEnqueued;
        lastDropped = gFramesDropped;
    }

    if (fullLen < 5) {  // 头(2) + 长度(1) + 帧率(2) + 最少1字节数据
        LOG_DISPLAY_WARN("数据包太短，无法解析");
        return;
    }

    // 检查协议头
    if (raw[0] != 0xAA || raw[1] != 0x55) {
        LOG_DISPLAY_WARN("无效数据头");
        return;
    }

    // 读取帧率字段（两字节，高字节先发）
    // uint16_t frameInterval = (raw[3] << 8) | raw[4];
    // Serial.print("帧率 (ms): ");
    // Serial.println(frameInterval);

    // 构建 32 字节帧缓冲（DDRAM） + 8 槽自定义字符（CGRAM）
    uint8_t frame[32];
    for (int k = 0; k < 32; k++) frame[k] = ' ';

    uint8_t cgram[8][8] = {{0}};
    bool cgramUsed[8] = {false};

    unsigned int i = 5;          // 从数据体开始
    uint8_t customCharIndex = 0; // 自定义字符编号（0~7）
    uint8_t cursor = 0;          // 帧缓冲光标 0~31
    
    // 添加安全计数器，防止无限循环
    unsigned int loopCounter = 0;
    const unsigned int maxLoops = 1000; // 最大循环次数限制

    while (i < fullLen && cursor < 32 && loopCounter < maxLoops) {
        loopCounter++; // 增加循环计数
        
        // 每处理10个字符就喂一次狗
        if (loopCounter % 10 == 0) {
            esp_task_wdt_reset();
        }
        uint8_t flag = raw[i++];
        if (flag == 0x00) {
            if (i >= fullLen) break;
            uint8_t c0 = raw[i++];

            // ASCII 字符直接写入帧缓冲
            if (c0 < 0x80) {
                frame[cursor++] = static_cast<uint8_t>(c0);
            }
            // 非 ASCII 字符（含 UTF-8 多字节）跳过对应字节数
            else if ((c0 & 0xE0) == 0xC0) {
                // 2字节 UTF-8，跳过1个后续字节
                if (i < fullLen) i++;
            }
            else if ((c0 & 0xF0) == 0xE0) {
                // 3字节 UTF-8，跳过2个后续字节
                if (i + 1 < fullLen) i += 2;
                else break;
            }
            else if ((c0 & 0xF8) == 0xF0) {
                // 4字节 UTF-8，跳过3个后续字节
                if (i + 2 < fullLen) i += 3;
                else break;
            }
            // 其他情况忽略
        } 

        // 自定义字符
        else if (flag == 0x01) {
            // 添加边界检查
            if (i + 8 > fullLen) {
                LOG_DISPLAY_WARN("自定义字符数据不足");
                break;
            }

            // 记录 CGRAM 定义并在帧缓冲中写入该槽位字符码
            for (int j = 0; j < 8; j++) {
                cgram[customCharIndex][j] = raw[i++];
            }
            cgramUsed[customCharIndex] = true;
            frame[cursor++] = customCharIndex;

            // 循环使用 slot
            customCharIndex = (customCharIndex + 1) % 8;
        } 

        else {
            LOG_DISPLAY_WARN("未知标志: 0x%02X", static_cast<unsigned int>(flag));
            break;
        }
    }

    // 检查是否因为安全限制退出循环
    if (loopCounter >= maxLoops) {
        LOG_DISPLAY_WARN("处理循环达到安全限制，可能存在数据异常");
    }

    // 差分刷新：只写变化的位置
    const uint8_t updatedCells = lcdRenderDiff(frame, cgram, cgramUsed);
    if (updatedCells > 0) {
        frameCount++;
    }

    // 统计输出：基于“实际发生 LCD 写入”的帧
    const uint32_t elapsed = now - lastStatsMs;
    if (elapsed >= 1000) {
        const uint32_t enqueuedNow = gFramesEnqueued;
        const uint32_t droppedNow = gFramesDropped;

        const uint32_t enqDelta = enqueuedNow - lastEnqueued;
        const uint32_t dropDelta = droppedNow - lastDropped;
        const uint32_t recvDelta = enqDelta + dropDelta;

        currentFPS = (elapsed > 0) ? (frameCount * 1000.0f / elapsed) : 0.0f;
        const float dropRate = (recvDelta > 0) ? (100.0f * static_cast<float>(dropDelta) / static_cast<float>(recvDelta)) : 0.0f;

        LOG_DISPLAY_INFO("FPS=%.1f, dropped=%u, dropRate=%.1f%% (recv=%u)",
            currentFPS,
            static_cast<unsigned int>(dropDelta),
            dropRate,
            static_cast<unsigned int>(recvDelta));

        frameCount = 0;
        lastStatsMs = now;
        lastEnqueued = enqueuedNow;
        lastDropped = droppedNow;
    }

    // Serial.print("Process end:");
    // Serial.println(GET_MS());
}
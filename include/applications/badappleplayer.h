
/**
 * @file badappleplayer.h
 * @brief Bad Apple播放器相关函数与歌词结构体定义
 *
 * 提供Bad Apple动画播放、歌词显示等功能的接口声明。
 * 适用于ESP32 1602 LCD项目。
 *
 * @author kulib
 * @date 2025-11-04
 */

#ifndef BADAPPLEPLAYER_H
#define BADAPPLEPLAYER_H

#include <cstdio>
#include "./applications/bad_apple_melody.h"

#include "./menu/menu.h"
#include "./hardware/button.h"
#include "./services/kanamap.h"
#include "./utils/logger.h"

/**
 * @struct LyricLine
 * @brief 歌词行结构体，描述每一帧对应的歌词内容。
 */
struct LyricLine {
    int frameIndex;             /**< 歌词出现的起始帧号 */
    const char* text_line1;     /**< 歌词第一行 */
    const char* text_line2;     /**< 歌词第二行 */
};

/**
 * @brief 歌词数组，存储所有歌词及其对应帧号。
 */
const LyricLine lyrics[] = {
    {0,     "Bad Apple!!",          "in 1602A"},
    {60,    "Code by",              "Kulib"},
    {120,   "Vocals:",              "nomico"},
    {180,   "Lyrics:",              "Haruka"},
    {240,   "Composition",          "ZUN"},
    {6320,  "Thanks for",           "Watching!"}
};


/**
 * @brief 歌词数组元素数量
 */
const int lyricCount = sizeof(lyrics) / sizeof(lyrics[0]);

/**
 * @brief 进入 Bad Apple 界面（非阻塞，注册到状态机）
 */
void enterBadAppleInterface();

/**
 * @brief 处理 Bad Apple 界面状态机，每帧调用一次
 */
void handleBadAppleInterface();


#endif
# ESP-IDF Project CMakeLists.txt
# ESP32-S3 无线 1602A LCD 显示屏项目

cmake_minimum_required(VERSION 3.16)

# 设置额外组件目录（如果需要）
# set(EXTRA_COMPONENT_DIRS "")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(esp32_1602)

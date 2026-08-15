#include "./ui/icons.h"

// 系统图标定义
namespace SystemIcons {
    uint8_t wifiIcon[] = { 0x0E, 0x11, 0x00, 0x04, 0x0A, 0x00, 0x00, 0x04 };;
    uint8_t wifiOffIcon[] = {0x0E, 0x1F, 0x1F, 0x1F, 0x0E, 0x04, 0x0E, 0x11};
    uint8_t bluetoothIcon[] = {0x04, 0x0A, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x0A};
    uint8_t dcIconLeft[] = {0x00, 0x00, 0x00, 0x00, 0x0F, 0x00, 0x00, 0x00};
    uint8_t dcIconRight[] = {0x00, 0x00, 0x0C, 0x1F, 0x1C, 0x1F, 0x0C, 0x00};

    uint8_t batteryChargingIcon[] = {0x00, 0x02, 0x04, 0x0C, 0x1F, 0x06, 0x04, 0x08};

    uint8_t batteryLeft0Icon[] = {0x00, 0x0F, 0x10, 0x10, 0x10, 0x10, 0x0F, 0x00};
    uint8_t batteryLeft1Icon[] = {0x00, 0x0F, 0x18, 0x18, 0x18, 0x18, 0x0F, 0x00};
    uint8_t batteryLeft2Icon[] = {0x00, 0x0F, 0x1C, 0x1C, 0x1C, 0x1C, 0x0F, 0x00};
    uint8_t batteryLeft3Icon[] = {0x00, 0x0F, 0x1E, 0x1E, 0x1E, 0x1E, 0x0F, 0x00};
    uint8_t batteryLeft4Icon[] = {0x00, 0x0F, 0x1F, 0x1F, 0x1F, 0x1F, 0x0F, 0x00};
     
    uint8_t batteryRight0Icon[] = {0x00, 0x18, 0x04, 0x05, 0x05, 0x04, 0x18, 0x00}; 
    uint8_t batteryRight1Icon[] = {0x00, 0x18, 0x14, 0x15, 0x15, 0x14, 0x18, 0x00}; 
    uint8_t batteryRight2Icon[] = {0x00, 0x18, 0x1C, 0x1D, 0x1D, 0x1C, 0x18, 0x00}; 

    uint8_t clockIcon[] = { 0x0E, 0x11, 0x15, 0x15, 0x13, 0x11, 0x0E, 0x00 }; // 时间图标
    uint8_t outDatedIcon[] = {0x00, 0x00, 0x17, 0x15, 0x17, 0x15, 0x17, 0x00}; // 过期图标

    uint8_t tempIcon[] = {0x04, 0x0A, 0x0E, 0x0A, 0x0E, 0x11, 0x11, 0x0E}; // 温度图标
    uint8_t celsius[] = {0x1C, 0x14, 0x1C, 0x00, 0x00, 0x00, 0x00, 0x00}; // ℃ 图标

    uint8_t unknowIcon[] = {0x0E, 0x11, 0x01, 0x02, 0x04, 0x00, 0x04, 0x00}; // 未知图标

    uint8_t* getIcon(const char* iconName) {
        if (strcmp(iconName, "wifi") == 0) return wifiIcon;
        if (strcmp(iconName, "wifi_off") == 0) return wifiOffIcon;
        if (strcmp(iconName, "bluetooth") == 0) return bluetoothIcon;
        if (strcmp(iconName, "clock") == 0) return clockIcon;
        if (strcmp(iconName, "temperature") == 0) return tempIcon;
        if (strcmp(iconName, "celsius") == 0) return celsius;
        if (strcmp(iconName, "battery_charging") == 0) return batteryChargingIcon;
        if (strcmp(iconName, "outdated") == 0) return outDatedIcon;
        if (strcmp(iconName, "dc_left") == 0) return dcIconLeft;
        if (strcmp(iconName, "dc_right") == 0) return dcIconRight;
        if (strcmp(iconName, "unknown") == 0) return unknowIcon;

        return nullptr;
    }

    uint8_t* getBatteryLeftIcon(const uint8_t& soc) {
        if (soc >= 56) return batteryLeft4Icon;
        else if (soc >= 42) return batteryLeft3Icon;
        else if (soc >= 28) return batteryLeft2Icon;
        else if (soc >= 14) return batteryLeft1Icon;
        else return batteryLeft0Icon;
    }

    uint8_t* getBatteryRightIcon(const uint8_t& soc) {
        if (soc >= 84) return batteryRight2Icon;
        else if (soc >= 70) return batteryRight1Icon;
        else return batteryRight0Icon;
    }
}

// 风向图标定义
namespace WindIcons {
    uint8_t northWindIcon [] = {0x00, 0x04, 0x04, 0x04, 0x15, 0x0E, 0x04, 0x00};
    uint8_t southWindIcon [] = {0x00, 0x04, 0x0E, 0x15, 0x04, 0x04, 0x04, 0x00};
    uint8_t eastWindIcon  [] = {0x00, 0x04, 0x08, 0x1F, 0x08, 0x04, 0x00, 0x00};
    uint8_t westWindIcon  [] = {0x00, 0x04, 0x02, 0x1F, 0x02, 0x04, 0x00, 0x00};
    uint8_t northEastWindIcon [] = {0x00, 0x01, 0x12, 0x14, 0x18, 0x1E, 0x00, 0x00};
    uint8_t southEastWindIcon [] = {0x00, 0x1E, 0x18, 0x14, 0x12, 0x01, 0x00, 0x00};
    uint8_t southWestWindIcon [] = {0x00, 0x0F, 0x03, 0x05, 0x09, 0x10, 0x00, 0x00};
    uint8_t northWestWindIcon [] = {0x00, 0x10, 0x09, 0x05, 0x03, 0x0F, 0x00, 0x00};

    uint8_t* getIcon(const char* direction) {
        if (strcmp(direction, "N") == 0 || strcmp(direction, "北风") == 0) return northWindIcon;
        if (strcmp(direction, "S") == 0 || strcmp(direction, "南风") == 0) return southWindIcon;
        if (strcmp(direction, "E") == 0 || strcmp(direction, "东风") == 0) return eastWindIcon;
        if (strcmp(direction, "W") == 0 || strcmp(direction, "西风") == 0) return westWindIcon;
        if (strcmp(direction, "NE") == 0 || strcmp(direction, "东北风") == 0) return northEastWindIcon;
        if (strcmp(direction, "SE") == 0 || strcmp(direction, "东南风") == 0) return southEastWindIcon;
        if (strcmp(direction, "SW") == 0 || strcmp(direction, "西南风") == 0) return southWestWindIcon;
        if (strcmp(direction, "NW") == 0 || strcmp(direction, "西北风") == 0) return northWestWindIcon;
        return SystemIcons::unknowIcon;
    }
}

// 天气图标定义
namespace WeatherIcons {
    // 晴 (Sunny)
    uint8_t sunnyLeftIcon[] = {0x08, 0x05, 0x03, 0x1B, 0x03, 0x05, 0x08, 0x00}; 
    uint8_t sunnyRightIcon[] = {0x02, 0x14, 0x18, 0x1B, 0x18, 0x14, 0x02, 0x00};

    // 多云 (Cloudy)
    uint8_t cloudyLeftIcon[] = {0x0C, 0x12, 0x13, 0x0C, 0x10, 0x10, 0x0F, 0x00};  
    uint8_t cloudyRightIcon[] = {0x00, 0x08, 0x14, 0x02, 0x02, 0x01, 0x1E, 0x00};

    // 阴 (Overcast)
    uint8_t overcastLeftIcon[] = {0x00, 0x00, 0x03, 0x0C, 0x10, 0x10, 0x0F, 0x00};  
    uint8_t overcastRightIcon[] = {0x00, 0x08, 0x14, 0x02, 0x02, 0x01, 0x1E, 0x00};

    // 小雨 (Light Rain)
    uint8_t lightRainLeftIcon[] = {0x00, 0x03, 0x0C, 0x08, 0x07, 0x00, 0x01, 0x01};  
    uint8_t lightRainRightIcon[] = {0x08, 0x14, 0x02, 0x02, 0x1C, 0x00, 0x10, 0x10};

    // 中雨 (Moderate Rain)
    uint8_t moderateRainLeftIcon[] = {0x00, 0x03, 0x0C, 0x08, 0x07, 0x00, 0x05, 0x05};
    uint8_t moderateRainRightIcon[] = {0x08, 0x14, 0x02, 0x02, 0x1C, 0x00, 0x10, 0x10};

    // 大雨 (Heavy Rain)
    uint8_t heavyRainLeftIcon[] = {0x00, 0x03, 0x0C, 0x08, 0x07, 0x00, 0x05, 0x05};
    uint8_t heavyRainRightIcon[] = {0x08, 0x14, 0x02, 0x02, 0x1C, 0x00, 0x14, 0x14};

    // 暴雨 (Storm)
    uint8_t stormLeftIcon[] = {0x00, 0x03, 0x1C, 0x10, 0x0F, 0x00, 0x15, 0x15};
    uint8_t stormRightIcon[] = {0x08, 0x14, 0x02, 0x01, 0x1E, 0x00, 0x15, 0x15};

    // 雾 (Fog)
    uint8_t fogLeftIcon[] = {0x17, 0x00, 0x1D, 0x00, 0x1F, 0x00, 0x17, 0x00};
    uint8_t fogRightIcon[] = {0x1D, 0x00, 0x1F, 0x00, 0x1B, 0x00, 0x1D, 0x00};

    // 雪 (Snow)
    uint8_t snowLeftIcon[] = {0x14, 0x0A, 0x01, 0x17, 0x01, 0x0A, 0x04, 0x11};
    uint8_t snowRightIcon[] = {0x05, 0x0A, 0x10, 0x1D, 0x10, 0x0A, 0x04, 0x11};

    // 雷阵雨 (Thunder)
    uint8_t thunderLeftIcon[] = {0x01, 0x0E, 0x10, 0x0F, 0x00, 0x0A, 0x0A, 0x0A};
    uint8_t thunderRightIcon[] = {0x00, 0x1E, 0x01, 0x06, 0x18, 0x0D, 0x09, 0x11};

    // 沙尘暴 (Dust Storm)
    uint8_t dustStormLeftIcon[] = {0x00, 0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E};
    uint8_t dustStormRightIcon[] = {0x00, 0x04, 0x02, 0x1F, 0x02, 0x04, 0x00, 0x00};


    uint8_t* getLeftIcon(const char* weather) {
        // 中文
        if (strcmp(weather, "晴") == 0) return sunnyLeftIcon;
        if (strcmp(weather, "多云") == 0) return cloudyLeftIcon;
        if (strcmp(weather, "阴") == 0) return overcastLeftIcon;
        if (strcmp(weather, "小雨") == 0) return lightRainLeftIcon;
        if (strcmp(weather, "中雨") == 0) return moderateRainLeftIcon;
        if (strcmp(weather, "大雨") == 0) return heavyRainLeftIcon;
        if (strcmp(weather, "暴雨") == 0) return stormLeftIcon;
        if (strcmp(weather, "雾") == 0) return fogLeftIcon;
        if (strcmp(weather, "雪") == 0) return snowLeftIcon;
        if (strcmp(weather, "雷阵雨") == 0) return thunderLeftIcon;
        if (strcmp(weather, "沙尘暴") == 0) return dustStormLeftIcon;

        // 日语
        if (strcmp(weather, "晴れ") == 0) return sunnyLeftIcon;
        if (strcmp(weather, "曇り") == 0) return cloudyLeftIcon;
        if (strcmp(weather, "驟雨") == 0) return lightRainLeftIcon;
        if (strcmp(weather, "雷雨") == 0) return thunderLeftIcon;
        if (strcmp(weather, "霧") == 0) return fogLeftIcon;

        // 英文
        if (strcmp(weather, "Sunny") == 0 || strcmp(weather, "Clear") == 0) return sunnyLeftIcon;
        if (strcmp(weather, "Cloudy") == 0 || strcmp(weather, "Partly Cloudy") == 0) return cloudyLeftIcon;
        if (strcmp(weather, "Overcast") == 0) return overcastLeftIcon;
        if (strcmp(weather, "Rain") == 0 || strcmp(weather, "Light Rain") == 0) return lightRainLeftIcon;
        if (strcmp(weather, "Heavy Rain") == 0) return heavyRainLeftIcon;
        if (strcmp(weather, "Snow") == 0) return snowLeftIcon;
        if (strcmp(weather, "Thunderstorm") == 0) return thunderLeftIcon;
        if (strcmp(weather, "Fog") == 0) return fogLeftIcon;

        return sunnyLeftIcon;
    }

    uint8_t* getRightIcon(const char* weather) {
        // 中文
        if (strcmp(weather, "晴") == 0) return sunnyRightIcon;
        if (strcmp(weather, "多云") == 0) return cloudyRightIcon;
        if (strcmp(weather, "阴") == 0) return overcastRightIcon;
        if (strcmp(weather, "小雨") == 0) return lightRainRightIcon;
        if (strcmp(weather, "中雨") == 0) return moderateRainRightIcon;
        if (strcmp(weather, "大雨") == 0) return heavyRainRightIcon;
        if (strcmp(weather, "暴雨") == 0) return stormRightIcon;
        if (strcmp(weather, "雾") == 0) return fogRightIcon;
        if (strcmp(weather, "雪") == 0) return snowRightIcon;
        if (strcmp(weather, "雷阵雨") == 0) return thunderRightIcon;
        if (strcmp(weather, "沙尘暴") == 0) return dustStormRightIcon;

        // 日语
        if (strcmp(weather, "晴れ") == 0) return sunnyRightIcon;
        if (strcmp(weather, "曇り") == 0) return cloudyRightIcon;
        if (strcmp(weather, "驟雨") == 0) return lightRainRightIcon;
        if (strcmp(weather, "雷雨") == 0) return thunderRightIcon;
        if (strcmp(weather, "霧") == 0) return fogRightIcon;

        // 英文
        if (strcmp(weather, "Sunny") == 0 || strcmp(weather, "Clear") == 0) return sunnyRightIcon;
        if (strcmp(weather, "Cloudy") == 0 || strcmp(weather, "Partly Cloudy") == 0) return cloudyRightIcon;
        if (strcmp(weather, "Overcast") == 0) return overcastRightIcon;
        if (strcmp(weather, "Rain") == 0 || strcmp(weather, "Light Rain") == 0) return lightRainRightIcon;
        if (strcmp(weather, "Heavy Rain") == 0) return heavyRainRightIcon;
        if (strcmp(weather, "Snow") == 0) return snowRightIcon;
        if (strcmp(weather, "Thunderstorm") == 0) return thunderRightIcon;
        if (strcmp(weather, "Fog") == 0) return fogRightIcon;

        return sunnyRightIcon;
    }
}
# esp8266-led-strip-controller

ESP8266 驱动的 LED 灯带控制器。一块 ESP8266、一条 WS2812 灯带，刷上这套固件，手机连上它自带的网页就能玩——不装 App，浏览器就是遥控器。

## 它能做什么

- **灯光模式**：彩虹、剧院、彩纸、呼吸、星光、极光、蹦迪、心跳、烟花、单色、单控（指定灯珠区间上色）等十几种
- **单色动画**：全局渐亮（开机默认）、随机溶解、顺序点亮、中心扩散、两端向内、无动画
- **五套网页皮肤**：新粗野 / 黏土 / 赛博朋克 / 极光网格 / 像素街机，点一下切换，选择记在浏览器里
- **定时关闭**、**灯珠数量**（1~200 颗，写 EEPROM）、**反向显示**
- **WiFi 管理**：多组网络按优先级自动重连，断网自动开热点进配网模式
- **巴法云 MQTT**：接入微信小程序、语音音箱之类的外部控制
- **OTA 一键升级**：网页里粘贴固件下载链接直接升级，不用拆机接数据线
- **红外遥控**：支持常见 NEC 遥控器调模式 / 调亮度

## 硬件

| | |
|---|---|
| 主控 | ESP8266（Generic，4MB Flash） |
| 灯带 | WS2812B，默认 60 颗（网页里可改 1~200） |
| 灯带数据脚 | GPIO2（`config.h` 里可改） |
| 红外接收头 | 引脚见 `config.h` 的 `kRecvPin` |

## 怎么烧录

### 方式一：直接烧现成固件（不想配编译环境就看这里）

`firmware/LAND_LED.ino.bin` 是编译好的成品。用 `tools/` 里的烧录工具（ESP8266Flasher）+ 对应的串口驱动（CH340 或 CP210x，看你板子上的串口芯片）：

1. 装驱动，插上板子
2. 打开烧录工具，选好 COM 口
3. 固件选 `firmware/LAND_LED.ino.bin`，**烧录地址填 0x0**，波特率 115200
4. 擦除后烧录，完了复位

### 方式二：自己编译

Arduino IDE 装好 ESP8266 core（3.1.2 版本测试通过），开发板选 **Generic ESP8266 Module**，Flash Size 选 **4MB (FS:1MB OTA:~1019KB)**。

这个布局得选对。官方文档里讲 flash layout 的部分说得很清楚：OTA 需要两块等大的固件槽，1MB 的老布局每槽只有 ~470KB，而这固件有 542KB——串口能烧进去，OTA 却会装不下。第一次用数据线烧，之后就能走网页里的 OTA 卡片升级了。

## 目录结构

```
LAND_LED/    固件（Arduino 工程，网页以 index_html 形式内嵌在其中）
firmware/    编译好的成品固件（对应上面的 4MB 布局）
tools/       烧录工具 + CH340 / CP210x 串口驱动
```

固件里内联了 ESP8266httpUpdate（源码直接放工程里，免得每台电脑库索引不一样编译不过），MQTT 用的 PubSubClient。

配置（WiFi、巴法云、灯珠数）都存在 EEPROM，断电不丢；代码里没有硬编码的密码，凭据全是你自己在网页里填的。

## 用到的东西

[FastLED](https://github.com/FastLED/FastLED)、[ESPAsyncWebServer](https://github.com/me-no-dev/ESPAsyncWebServer)、[PubSubClient](https://github.com/knolleary/pubsubclient)、[IRremoteESP8266](https://github.com/IRremoteESP8266/IRremoteESP8266)、[ArduinoJson](https://github.com/bblanchon/ArduinoJson)，云端是[巴法云](https://bemfa.com)。感谢这些库的作者，站在他们肩膀上省了太多事。

## 许可证

MIT，随便用，用了觉得不错回来点个 star 就行。

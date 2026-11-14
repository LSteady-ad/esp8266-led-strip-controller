# esp8266-led-strip-controller

ESP8266 驱动的 LED 灯带控制器。一块 ESP8266、一条 WS2812 灯带，刷上这套固件，手机连上它自带的网页就能玩——不装 App，浏览器就是遥控器。

## 它能做什么

- **灯光模式**：彩虹、剧院、彩纸、呼吸、星光、极光、蹦迪、心跳、烟花、单色、单控（指定灯珠区间上色）等十几种
- **单色动画**：全局渐亮（开机默认）、随机溶解、顺序点亮、中心扩散、两端向内、无动画
- **五套网页皮肤**：新粗野 / 黏土 / 赛博朋克 / 极光网格 / 像素街机，点一下切换，选择记在浏览器里
- **定时关闭**、**灯珠数量**（1~200 颗，写 EEPROM）、**反向显示**
- **WiFi 管理**：多组网络按优先级自动重连，断网自动开热点进配网模式
- **巴法云 MQTT**：接入微信小程序、语音音箱之类的外部控制
- **OTA 升级两种方式**：网页"一键升级"手动升级；或开启"自动模式"——向 MQTT 订阅主题发送固件下载链接（http 开头），设备收到立即升级。同一固件只刷一次，重复推送安全
- **红外遥控**：支持常见 NEC 遥控器调模式 / 调亮度
- **DHT11 温湿度**：网页实时显示，另有两个纯数字接口方便自己写小工具轮询

## 硬件接线

所有引脚都在 `LAND_LED/config.h` 里定义，想换脚改那里就行（网页上标注的引脚名是跟着代码走的，改完记得同步改网页文案）。

| 部件 | 板子丝印 | GPIO | 代码里的名字 | 备注 |
|---|---|---|---|---|
| WS2812 灯带数据线 | **D1** | GPIO5 | `LED_PIN` | 颜色顺序 GRB |
| DHT11 数据线（可选） | **D5** | GPIO14 | `DHT_PIN` | VCC 接 3.3V，GND 共地 |
| 红外接收头 OUT（可选） | **D2** | GPIO4 | `kRecvPin` | 常见 NEC 一体化接收头 |
| 灯带电源 | — | — | — | WS2812 用 5V 供电 |

两个外设默认都不碍事：DHT11 默认关、红外默认开，都可以在网页「⚙ 灯带设置」的"外设开关"里随时开或关，不用重新烧录。

供电方面多说一句：灯带别从板子的 3.3V 脚取电（带不动），直接接 5V；数据线串个 330Ω 电阻、5V 电源并个 1000µF 电容，是 WS2812 的通用保命做法。灯带长、亮度高的话记得算一下电流。

## 怎么烧录

### 方式一：直接烧现成固件（不想配编译环境就看这里）

去 [Releases 页面](https://github.com/LSteady-ad/esp8266-led-strip-controller/releases) 下载 `LAND_LED.ino.bin` 和烧录工具（ESP8266Flasher.exe + 串口驱动）：

1. 装驱动（CH340 或 CP210x，看你板子上的串口芯片），插上板子
2. 打开烧录工具，选好 COM 口
3. 固件选 `LAND_LED.ino.bin`，**烧录地址填 0x0**，波特率 115200
4. 擦除后烧录，完了复位

### 方式二：自己编译

Arduino IDE 装好 ESP8266 core（3.1.2 版本测试通过），开发板选 **Generic ESP8266 Module**，Flash Size 选 **4MB (FS:1MB OTA:~1019KB)**。

这个布局得选对。官方文档里讲 flash layout 的部分说得很清楚：OTA 需要两块等大的固件槽，1MB 的老布局每槽只有 ~470KB，而这固件有 560KB+——串口能烧进去，OTA 却会装不下。第一次用数据线烧，之后就能走网页里的 OTA 卡片升级了。

第三方库（FastLED、ESPAsyncWebServer 等）已经全部内置在工程的 `LAND_LED/src/` 里，**不需要在库管理器装任何库**，clone 下来直接编译。

## HTTP 接口

设备联网后，所有功能都有对应的 GET 接口，直接浏览器或脚本就能调。返回要么是 JSON，要么是纯文本，`-1` 一般表示"该功能未启用"。

### 页面与状态

| 接口 | 返回 |
|---|---|
| `GET /` | 控制网页本体 |
| `GET /state` | 全量状态 JSON（见下） |
| `GET /scanwifi` | 触发 WiFi 扫描，返回扫描到的网络 |
| `GET /wifilist` | 已保存网络列表 JSON（含优先级顺序） |
| `GET /favicon.ico` | 网页图标（SVG） |

`/state` 里的主要字段：`mode`（当前模式）、`color`、`brightness`（0~100）、`speed`、`ledCount`、`reverse`、`solidanim`、`timerActive` / `timerRemaining`、`dhtEnabled` / `temperature` / `humidity`、`irEnabled`、`otaAuto`、`otaInterval`、`otaAutoUrl`、`wifiConnected` / `apModeActive` / `ipAddress`、`wifiNets`（已保存网络数组）、巴法云的 `mqttEnabled` / `mqttConnected` / `mqttLastMsg` 等。

### 灯光控制（都是 GET /control 加参数）

| 参数 | 取值 | 说明 |
|---|---|---|
| `mode` | 见下方模式表 | 切换灯光模式 |
| `color` | `#RRGGBB` | 颜色 |
| `brightness` | 0~100 | 亮度 |
| `speed` | 数值 | 动效速度 |
| `solidanim` | `random` / `sequential` / `center` / `ends` / `fade` / `none` | 单色动画（fade 为开机默认） |
| `timer` | 秒数，0=取消 | 定时关闭 |
| `ledcount` | 1~200 | 灯珠数量（写 EEPROM） |
| `reverse` | 0 / 1 | 反向显示 |
| `restart` | 1 | 重启设备 |

单色模式下的"单控"（指定区间上色）：`/control?mode=single&range=起-止&color=#RRGGBB`，灯珠编号从 1 开始，比如 `range=1-10` 点亮前 10 颗。

`mode` 的全部取值：`rainbow`（彩虹）、`rainbowstring`（彩虹流水）、`theater`（剧院）、`confetti`（彩纸）、`breathing`（呼吸）、`sparkle`（星光）、`sparksparks`（闪星）、`aurora`（极光）、`disco`（蹦迪）、`heartbeat`（心跳）、`firework`（烟花）、`step`（阶梯）、`random`（随机渐变）、`randomjump`（随机跳变）、`solid`（单色）、`single`（单控）、`off`（关闭）。

### WiFi 管理（GET /control 加参数）

| 参数 | 说明 |
|---|---|
| `addwifi=1&ssid=名称&password=密码` | 新增网络（追加到优先级末尾） |
| `saveconfig=1` | 保存配置 |
| `connectwifi=下标` | 立即连接指定网络 |
| `setactive=下标` | 设为当前使用（显示用） |
| `movewifi=下标&dir=1或-1` | 上移 / 下移优先级 |
| `setpw=下标&password=新密码` | 修改已存网络的密码 |
| `delwifi=下标` | 删除网络 |
| `setmode=1&staEnabled=0或1&apEnabled=0或1` | STA / AP 模式开关 |

下标从 0 开始，顺序就是连接优先级。

### 外设与 OTA（GET /control 加参数）

| 参数 | 说明 |
|---|---|
| `dhtenable=0或1` | DHT11 开关（接 D5/GPIO14） |
| `irenable=0或1` | 红外接收开关（接 D2/GPIO4） |
| `otaauto=0或1` | 每天定时自动升级开关（链接见下） |
| `otainterval=分钟` | 自动检查间隔（分钟，1~65535，默认 240） |
| `otaautourl=链接` | 自动升级用的固件链接（留空 = 内置巴法云链接） |

### OTA 升级

| 接口 | 说明 |
|---|---|
| `GET /ota?url=固件.bin的链接` | 手动升级：提交后设备下载写入，成功自动重启并切回手动模式，失败回退 |
| `GET /otastatus` | 纯数字升级进度：`-2`=失败已回退，`-1`=空闲，`0~99`=下载中，`100`=写入 Flash |


### 其它小接口

| 接口 | 说明 |
|---|---|
| `GET /temperature` | 纯数字温度（如 `25.4`），DHT11 未启用返回 `-1` |
| `GET /humidity` | 纯数字湿度（如 `60`），未启用返回 `-1` |
| `GET /update?power=0~100` | 把手机电量推给灯带显示（电量模式） |
| `GET /music?volume=0~100` | 音乐律动模式的音量输入 |

## 巴法云 MQTT 指令

在巴法云控制台（或接了它的小程序 / 语音音箱）往订阅主题发**文本指令**就行，不用 JSON：

- `on` / `off` —— 开灯 / 关灯
- `http://…bin 固件下载链接` —— **OTA 推送升级**：自动模式下收到即升级（见上）
- `get mode`、`get light`、`get speed`、`get R`、`get G`、`get B`、`get reverse` —— 查询当前状态
- `字段:数值` 格式设置，比如 `R:255`、`G:100`、`B:80`、`light:80`（亮度）、`speed:5`、`mode:solid`、`reverse:1`

## 目录结构

```
LAND_LED/    固件（Arduino 工程，网页以 index_html 形式内嵌在其中）
```

固件里内联了 ESP8266httpUpdate（源码直接放工程里，免得每台电脑库索引不一样编译不过），MQTT 用的 PubSubClient。第三方库（FastLED、ESPAsyncWebServer、ArduinoJson、IRremoteESP8266 等）全部内置在 `LAND_LED/src/`，换电脑不用装库。

配置（WiFi、巴法云、灯珠数、外设开关）都存在 EEPROM，断电不丢；代码里没有硬编码的密码，凭据全是你自己在网页里填的。

## 用到的东西

[FastLED](https://github.com/FastLED/FastLED)、[ESPAsyncWebServer](https://github.com/me-no-dev/ESPAsyncWebServer)、[PubSubClient](https://github.com/knolleary/pubsubclient)、[IRremoteESP8266](https://github.com/IRremoteESP8266/IRremoteESP8266)、[ArduinoJson](https://github.com/bblanchon/ArduinoJson)，云端是[巴法云](https://bemfa.com)。感谢这些库的作者，站在他们肩膀上省了太多事。

## 许可证

MIT，随便用，用了觉得不错回来点个 star 就行。

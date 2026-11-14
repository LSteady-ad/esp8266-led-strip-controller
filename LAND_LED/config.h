/**
 * @file config.h
 * @brief LAND_LED 灯光控制系统 - 全局配置头文件
 *
 * 包含：
 *  - 硬件引脚 / 灯带参数宏定义
 *  - MQTT 连接参数宏定义
 *  - 电量显示相关宏定义
 *  - WiFiConfig / MqttConfig 结构体
 *  - 所有模块共享的全局变量 extern 声明
 *
 * 使用方式：在 LAND_LED.ino 顶部 #include "config.h"，
 * 其余 .ino 模块文件由 Arduino IDE 自动合并，无需重复包含。
 */

#ifndef CONFIG_H
#define CONFIG_H

// ============================================================
// IRAM 优化：减小红外解码缓冲区
// 【必须在 include IRremoteESP8266.h 之前定义】
// 默认 RECV_BUF_LENGTH=512，可减小到 256（节省约 2KB IRAM）
// 对于常见 21 键遥控器的 NEC 协议信号，256 字节足够
// ============================================================
#define RECV_BUF_LENGTH 256

// ============================================================
// 头文件依赖（放在此处，避免各 .ino 重复 include）
// ============================================================
#include <ESP8266WiFi.h>
// 以下第三方库全部内置在 sketch 的 src/ 子目录（见 src/README.md），
// 用 "" + 相对路径包含，换电脑不需要在"库管理器"里安装任何库。
// 注意：ESP8266 开发板 core 仍需安装 3.1.2，它提供 WiFi/EEPROM/flash_hal 等底层头文件。
#include "src/ESPAsyncTCP/ESPAsyncTCP.h"
#include "src/ESPAsyncWebServer/ESPAsyncWebServer.h"
#include "src/FastLED/FastLED.h"
#include "src/ArduinoJson/ArduinoJson.h"
#include <EEPROM.h>
#include "PubSubClient.h"
#include <Arduino.h>
#include <stddef.h>
#include "src/IRremoteESP8266/IRrecv.h"
#include "src/IRremoteESP8266/IRremoteESP8266.h"
#include "src/IRremoteESP8266/IRutils.h"
// OTA 升级库。用 "" 而不是 <>：库源码（ESP8266httpUpdate.h/.cpp）已放进 sketch 文件夹，
// 和 PubSubClient 一样内联进来 —— 避免不同机器上 Arduino 库索引差异导致"找不到头文件"。
#include "ESP8266httpUpdate.h"

// ============================================================
// 硬件引脚定义
// ============================================================
/** WS2812 灯带数据引脚（GPIO5 / D1） */
#define LED_PIN 5

/** 红外接收器引脚（GPIO4 / D2） */
static const uint16_t kRecvPin = 4;
// DHT11 温湿度传感器数据脚：丝印 D5 = GPIO14（VCC 接 3.3V，GND 共地）
// 读取协议为手写单总线（peripherals.ino），不依赖第三方库
#define DHT_PIN 14

// ============================================================
// 灯带参数
// ============================================================
/** 灯带最大支持灯珠数量（影响 leds 数组大小，勿随意增大） */
#define MAX_LEDS 200

/**
 * 首次上电、EEPROM 无效或执行"恢复出厂设置"后的默认灯珠数量。
 *
 * 【重要】该值必须 ≤ 实际灯带的最小灯珠数（多台设备时取最小值）。
 * 原因：启动动画的三段范围（后 30% 蓝 / 中间 50% / 前 20% 紫）都按
 * "灯珠数量"换算成数组下标。若该值大于实际灯珠数，前两段就会落在
 * 实际不存在的灯珠上，表现为"前两段不亮、最后一段整条亮起"的错位。
 * 取较小值只会让灯带末尾少数几颗不参与动画，问题一眼可见、不会误导。
 */
#define DEFAULT_LEDS 60

/** 默认亮度（0-255，对应约 60%） */
#define BRIGHTNESS 153

// ============================================================
// EEPROM 存储布局
// （具体值在 MqttConfig 结构体定义后计算，见下方）

/**
 * @brief 连续快速断电后触发"恢复出厂设置"的次数阈值。
 *
 * 若上电后不到 BOOT_STABLE_MS 毫秒就断电，则断电计数 +1；
 * 连续达到此次数时，清空整片 EEPROM（WiFi 配置、灯珠数量、反向显示、
 * MQTT 配置等全部清除），并自动重启，以默认参数重新启动。
 *
 * 即：连续快速断电 4 次后，第 5 次上电即触发恢复出厂设置。
 * （计数在每次上电时先读取再判断，故触发发生在上电瞬间。）
 */
#define BOOT_RESET_COUNT  4

/**
 * @brief 系统稳定运行时间阈值（毫秒）。
 *
 * 系统上电后持续运行超过此时间，则认为本次启动"稳定"，
 * 自动将快速断电计数器清零。
 */
#define BOOT_STABLE_MS    5000UL

/**
 * @brief STA 重新连接后保持稳定的时间阈值（毫秒）。
 *
 * 动态 AP 管理中，STA 连接后持续稳定超过此时长，
 * 才关闭临时 AP 热点（默认 60 秒）。
 */
#define WIFI_STABLE_MS    60000UL

/**
 * @brief 上电后"强制开启 AP 热点"的窗口时长（毫秒）。
 *
 * 每次通电后的前 5 分钟内，**无论网页上的「AP 模式」开关是否勾选**，
 * 热点都保持开启：
 *   - 刚上电时 STA 可能还没连上、或路由器还没起来，这时必须保证
 *     一定能通过 192.168.4.1 进配置页把设备救回来；
 *   - 也避免了"AP 开关忘了勾、STA 又连不上"导致设备彻底失联的情况。
 *
 * 窗口结束后，热点开关完全交回「AP 模式」开关决定
 * （STA 断开时的临时热点兜底仍然保留，见 manageWiFiConnection()）。
 */
#define AP_FORCE_BOOT_MS  300000UL   // 5 分钟

// ============================================================
// MQTT 连接参数
// ============================================================
/** 巴法云 MQTT 服务器地址 */
static const char* mqtt_server      = "bemfa.com";

/** 巴法云 MQTT 服务器端口 */
static const int   mqtt_server_port = 9501;

/**
 * "上一次接收内容"展示缓冲长度（含 '\0'）。
 *
 * 巴法云下发的消息可能很频繁，用固定长度 char 缓冲存放最近一条消息，
 * 避免用 String 反复分配/释放把 ESP8266 的堆打碎。
 */
#define MQTT_LAST_MSG_LEN 80

/** 巴法云默认 Client ID（默认为空：由用户在网页端自行填写） */
#define DEFAULT_MQTT_CLIENT_ID ""

/** 巴法云默认订阅主题（默认为空：由用户在网页端自行填写） */
#define DEFAULT_MQTT_TOPIC ""

/**
 * 旧版本固件遗留在 EEPROM 中的示例占位值。
 *
 * loadMqttConfig() 检测到这两个值时会自动清空，避免升级后网页上
 * 仍显示无意义的 "please in" / "put topic"。
 */
#define LEGACY_MQTT_CLIENT_ID "please in"
#define LEGACY_MQTT_TOPIC     "put topic"

// ============================================================
// 电量显示相关参数
// ============================================================
/** 充满电（100%）后保持显示的时长（ms） */
#define BATTERY_FULL_TIMEOUT     5000

/** 充电中保持显示的时长（ms），超时后恢复上一个模式 */
#define BATTERY_CHARGING_TIMEOUT 20000

/** 电量渐变色相起始值（红色） */
#define BATTERY_HUE_START        0

/** 电量渐变色相结束值（绿色） */
#define BATTERY_HUE_END          96

/** 充电指示灯闪烁间隔（ms） */
#define BATTERY_BLINK_INTERVAL   500

// ============================================================
// 音乐可视化参数
// ============================================================
/** 音乐模式超时时间（ms）：超过此时长无新音量数据则退出音乐模式 */
#define MUSIC_TIMEOUT 1001

// ============================================================
// 数据结构定义
// ============================================================

/**
 * @brief WiFi 配置结构体，存储在 EEPROM 地址 0 起始处。
 */
struct WiFiConfig {
  char ssid[32];           ///< WiFi SSID（最长31字节+终止符）
  char password[64];       ///< WiFi 密码（最长63字节+终止符）
  bool staEnabled = true;  ///< 是否开启 STA 模式（连接路由器）
  bool apEnabled  = true;  ///< 是否开启 AP 模式（自建热点）
  bool configured = false; ///< 是否已完成过 WiFi 配置
};

/**
 * @brief 巴法云 MQTT 配置结构体，存储在 EEPROM 中。
 *
 * 数据紧接 WiFiConfig + num_leds(2) + reverse(1) + bootCount(1) 之后。
 * clientId / topic 默认为空字符串，由用户在网页端填写。
 */
struct MqttConfig {
  char clientId[48];      ///< 巴法云 Client ID（最长 47 字节，默认空）
  char topic[64];         ///< 巴法云订阅主题（最长 63 字节，默认空）
  bool enabled = false;   ///< 巴法云 MQTT 总开关（默认关闭，关闭时不执行任何云功能）
};

/** 可保存的 WiFi 网络数量上限（家中 / 公司 / 手机热点等） */
#define MAX_WIFI_NETS 6

/**
 * @brief 单条已保存的 WiFi 网络（SSID + 密码）。
 */
struct WiFiNet {
  char ssid[32];          ///< WiFi 名称（最长 31 字节 + 终止符）
  char password[64];      ///< WiFi 密码（最长 63 字节 + 终止符）
};

/**
 * @brief 多网络 WiFi 列表在 EEPROM 中的魔数。
 *
 * 用于区分「这块区域从未写过（空白 Flash 全 0xFF）」与「已有有效数据」。
 * 空白 Flash 读出 0xFFFFFFFF，与本魔数不等 → 判为未初始化 → 触发迁移。
 */
#define WIFI_LIST_MAGIC 0x4E444C57UL

/**
 * @brief 多网络 WiFi 列表（存储在 EEPROM 独立区域）。
 *
 * 用途：保存多组 WiFi 凭据。
 *  - **数组顺序 = 连接优先级**：nets[0] 最高。开机从 nets[0] 开始依次尝试；
 *    断网后也按这个顺序**逐个轮询重试**（每轮只试一个，下一个等下一轮）。
 *  - active 只表示"最后一次成功连上的网络下标"，仅用于网页显示"当前"徽标，
 *    不参与连接顺序（顺序由数组本身决定）。
 *
 * 注意：结构体内无隐式填充字节（magic 4B + count/active/reserved 4B +
 *       6 × 96B = 584B），因此可以直接用 memcmp 做落盘回读校验。
 */
struct WiFiList {
  uint32_t magic;                 ///< 固定为 WIFI_LIST_MAGIC，标记本区域已初始化
  uint8_t  count;                 ///< 已保存网络数量（0 ~ MAX_WIFI_NETS）
  uint8_t  active;                ///< 最后一次成功连接的网络下标（仅用于显示"当前"）
  uint8_t  reserved[2];           ///< 保留（对齐用，恒为 0）
  WiFiNet  nets[MAX_WIFI_NETS];   ///< 网络条目，**顺序即优先级**
};

/**
 * 断网后先"原地重连"的宽限时间（毫秒）。
 *
 * 路由器重启 / 短暂抖动时，ESP8266 SDK 自己就能把当前网络接回来，
 * 这段时间内不换网络，避免不必要的切换（切换本身要断开重连，代价更高）。
 */
#define WIFI_RETRY_GRACE_MS 30000UL

/**
 * 轮询重试时，每个网络等待结果的时间（毫秒）。
 *
 * 宽限期过后进入轮询：每 WIFI_RETRY_STEP_MS 试列表里的下一个网络，
 * 一轮只试一个 —— 这样"路由器重启中"和"换了地点"两种情况都能覆盖，
 * 又不会同时把多个网络搅在一起。
 */
#define WIFI_RETRY_STEP_MS  15000UL

/**
 * 轮询间隔的退避上限（毫秒）。
 *
 * 列表里每个网络各试一次算"一轮"。若一整轮都没连上（多半是都不在范围内、
 * 或路由器还没起来），就没必要一直按 15 秒的节奏折腾射频与 CPU：
 * 每完成一整轮把间隔翻倍（15s → 30s → 60s），到此为止，之后固定 60 秒一轮。
 * 一旦连上，退避立即归零，下次断网重新从 15 秒起步。
 */
#define WIFI_RETRY_MAX_MS   60000UL

/**
 * "连续失败"标记阈值。
 *
 * 某条网络连续失败达到该次数时，网页会在它的条目上标一个灰点
 * （鼠标悬停显示次数），提示"这条可能密码填错了或信号太差"。
 * 计数只存在 RAM 里，不落盘 —— 重启即清零，不污染 EEPROM 布局。
 */
#define WIFI_FAIL_MARK      2

// ============================================================
// EEPROM 地址计算（依赖前文结构体定义，须放在结构体之后）
// ============================================================
/** 灯珠数量在 EEPROM 中的存储地址（紧随 WiFiConfig 之后） */
static const int NUM_LEDS_EEPROM_ADDR  = sizeof(WiFiConfig);

/** 反向显示状态在 EEPROM 中的存储地址 */
static const int REVERSE_EEPROM_ADDR   = NUM_LEDS_EEPROM_ADDR + sizeof(uint16_t);

/**
 * 快速断电计数器在 EEPROM 中的存储地址（紧随反向显示字节之后）。
 *
 * 存储格式：1 字节无符号整数（0-255）。
 *  - 每次上电时 +1 写入；
 *  - 稳定运行 BOOT_STABLE_MS 后清零；
 *  - 达到 BOOT_RESET_COUNT 时清空整片 EEPROM 恢复出厂设置并自动重启。
 */
static const int BOOT_COUNT_EEPROM_ADDR = REVERSE_EEPROM_ADDR + 1;

/**
 * MQTT 配置在 EEPROM 中的存储地址（紧随快速断电计数器之后）。
 *
 * 存储格式：MqttConfig 结构体（48 + 64 + 1 = 113 字节）。
 *  - clientId / topic 默认为空字符串，由用户在网页端填写；
 *  - enabled 为总开关，空白 Flash（0xFF）读入后会被规范化为 false（关闭），
 *    因此新设备与旧版本升级后默认都不启用巴法云功能。
 */
static const int MQTT_CONFIG_EEPROM_ADDR = BOOT_COUNT_EEPROM_ADDR + 1;

/**
 * 多网络 WiFi 列表在 EEPROM 中的存储地址（紧随 MQTT 配置之后）。
 *
 * 存储格式：WiFiList 结构体（4 + 1 + 1 + 2 + 6×96 = 584 字节）。
 *  - 首次上电 / 升级自旧版本时 magic 不匹配，会从偏移 0 处的
 *    WiFiConfig（旧版单条配置）自动迁移一条记录进来；
 *  - 保持"旧地址完全不变、新数据追加在后面"的布局，升级不会破坏
 *    灯珠数量、反向显示、断电计数、MQTT 配置等既有数据。
 */
static const int WIFI_LIST_EEPROM_ADDR = MQTT_CONFIG_EEPROM_ADDR + sizeof(MqttConfig);

/**
 * @brief MQTT 服务器地址 / 端口 在 EEPROM 中的魔数。
 *
 * 与 WiFiList 同样的思路：用魔数区分"这块区域从未写过（全 0xFF）"与"已有有效数据"。
 */
#define MQTT_SERVER_MAGIC 0x52564553UL   /* 'SERV' */

/** 默认 MQTT 服务器地址（巴法云）与端口 */
#define DEFAULT_MQTT_SERVER "bemfa.com"
#define DEFAULT_MQTT_PORT   9501

/** OTA 自动升级使用的默认固件链接（巴法云）。把新版本 bin 传到巴法云覆盖它，
 *  开启"开机自动升级"的设备联网后就会自动拉取这个链接升级。 */
#define OTA_AUTO_URL "http://bin.bemfa.com/b/253904/1BcOTllNTViNDMwN2U3NDA2ZDhjOGVjZjM5MTIyYmRkODk=xuexiaoden002.bin"

/**
 * @brief MQTT 服务器地址与端口（存储在 EEPROM 独立区域）。
 *
 * 存储位置：**追加在 WiFiList 之后**。按项目铁律「旧地址一律不动、新数据追加在后面」，
 * 这样加这个功能不会移动 WiFiList 等既有数据的位置，升级不丢配置。
 *
 * 注意事项：结构体内无隐式填充字节（4 + 48 + 2 + 2 = 56，为 4 的倍数），
 * 因此可以安全地用 memcmp 做落盘回读校验。
 */
struct MqttServerConfig {
  uint32_t magic;          ///< 固定为 MQTT_SERVER_MAGIC，标记本区域已初始化
  char     server[48];     ///< 服务器地址（域名或 IP，最长 47 字节）
  uint16_t port;           ///< 端口（默认 9501）
  uint16_t reserved;       ///< 保留（对齐用，恒为 0）
};

/** MQTT 服务器配置在 EEPROM 中的存储地址（紧随 WiFi 列表之后） */
static const int MQTT_SERVER_EEPROM_ADDR = WIFI_LIST_EEPROM_ADDR + sizeof(WiFiList);

/**
 * 可选外设开关 / OTA 定时自动升级 的存储区域（追加在 MQTT 服务器配置之后）。
 * 布局（共 164 字节）：
 *   [0]      魔数 0xA7
 *   [1]      位标志：bit0=DHT11  bit1=红外接收  bit2=定时自动检查升级
 *   [2~3]    自动检查间隔分钟数（小端 uint16，默认 OTA_AUTO_INTERVAL_DEFAULT）
 *   [4~35]   巴法云用户私钥 openID（32B，网页可填，用于调用 firmwareVersion 接口）
 *   [36~39]  当前固件版本号（uint32，升级成功后记录）
 *   [40~43]  当前固件的"上传时间"戳（uint32，升级成功后记录）
 */
static const int OTA_FLAGS_EEPROM_ADDR = MQTT_SERVER_EEPROM_ADDR + sizeof(MqttServerConfig);
#define OTA_FLAGS_MAGIC    0xA7   /* v3：接入巴法云 OTA API（openID+版本号+上传时间），与 v2(0xA6) 不兼容 */
#define OTA_FLAGS_MAGIC_V2 0xA6   /* 旧布局魔数：迁移时识别用 */
#define PERIPH_FLAG_DHT    0x01
#define PERIPH_FLAG_IR     0x02
#define PERIPH_FLAG_OTA    0x04
#define OTA_FLAGS_INTERVAL_OFF  2
#define OTA_FLAGS_INTERVAL_LEN  2
#define OTA_FLAGS_OID_OFF   4
#define OTA_FLAGS_OID_LEN   32
#define OTA_FLAGS_UNIX_OFF  36
#define OTA_FLAGS_VER_OFF   40
#define OTA_FLAGS_URL_LEN  128
#define OTA_FLAGS_TOTAL    164
#define OTA_AUTO_INTERVAL_DEFAULT 240

/** EEPROM 总大小（WiFi配置 + 灯珠 + 反向 + 断电计数 + MQTT配置 + WiFi列表 + MQTT服务器） */
#define EEPROM_SIZE (sizeof(WiFiConfig) + sizeof(uint16_t) + 1 + 1 + sizeof(MqttConfig) + sizeof(WiFiList) + sizeof(MqttServerConfig) + OTA_FLAGS_TOTAL)

// ============================================================
// 调试串口开关
// ============================================================
/**
 * @brief 编译期串口调试开关。
 *
 * ESP8266 的 UART 硬件即使没有连接串口监视器也会驱动 TX 引脚发送数据，
 * 在灯带持续循环的场景下会产生不必要的 CPU 开销。
 *
 * - 设为 1：启用所有串口调试输出（开发/调试时使用）
 * - 设为 0：编译时完全移除串口代码，零 CPU 开销（生产/日常使用）
 */
#define SERIAL_DEBUG 1

#if SERIAL_DEBUG
  #define DEBUG_BEGIN(baud)    Serial.begin(baud)
  #define DEBUG_PRINT(x)       Serial.print(x)
  #define DEBUG_PRINTLN(x)     Serial.println(x)
  #define DEBUG_PRINTF(...)    Serial.printf(__VA_ARGS__)
#else
  #define DEBUG_BEGIN(baud)    ((void)0)
  #define DEBUG_PRINT(x)       ((void)0)
  #define DEBUG_PRINTLN(x)     ((void)0)
  #define DEBUG_PRINTF(...)    ((void)0)
#endif

// ============================================================
// 全局变量 extern 声明
// （实际定义在 LAND_LED.ino；各模块 .ino 通过此头文件引用）
// ============================================================

// --- FastLED ---
extern CRGB     leds[];       ///< LED 灯珠颜色数组
extern uint16_t num_leds;     ///< 当前有效灯珠数量
extern bool     reverseDisplay; ///< 是否反向显示（true=反向）

// --- 灯效状态 ---
extern String   currentMode;          ///< 当前灯效模式名称
extern uint8_t  gHue;                 ///< 全局色相偏移（彩虹/彩纸等）
extern CRGB     solidColor;           ///< 当前单色模式颜色
extern String   solidAnimMode;        ///< 单色动画模式: fade(默认)/random/sequential/center/ends/none
extern bool     modelbianhua;        ///< 单色渐入动画触发标志
extern uint8_t  brightness;           ///< 当前亮度（0-255）
extern int      effectSpeed;          ///< 当前效果速度（1-100）
extern unsigned long lastEffectUpdate; ///< 上次灯效刷新时间戳（ms）

// --- 呼吸/扫描/星光/极光效果内部状态 ---
extern uint8_t  breathingValue;   ///< 呼吸效果当前亮度值
extern bool     breathingUp;      ///< 呼吸效果方向（true=变亮）
extern uint8_t  scannerPosition;  ///< 扫描/剧院效果当前偏移
extern bool     scannerDirection; ///< 扫描方向
extern uint8_t  auroraPosition;   ///< 极光效果位置偏移

// --- 蹦迪模式 ---
extern uint8_t       discoHue;           ///< 蹦迪模式当前色相
extern bool          strobeOn;           ///< 蹦迪模式频闪状态
extern unsigned long lastStrobeChange;   ///< 蹦迪频闪上次切换时间

// --- 定时关闭 ---
extern unsigned long timerEndTime; ///< 定时关闭的目标时间戳（ms）
extern bool          timerActive;  ///< 定时器是否已激活

// --- 电量显示 ---
extern int           batteryLevel;       ///< 当前接收到的手机电量（0-100）
extern unsigned long lastBatteryUpdate;  ///< 上次收到电量数据的时间戳
extern String        previousMode;       ///< 进入电量模式前的模式名称
extern bool          inBatteryMode;      ///< 当前是否处于电量显示模式

// --- 音乐可视化 ---
extern int           musicVolume;            ///< 当前接收到的音量值（0-100）
extern unsigned long lastMusicUpdate;        ///< 上次收到音量数据的时间戳
extern bool          inMusicMode;            ///< 当前是否处于音乐可视化模式
extern String        previousModeBeforeMusic; ///< 进入音乐模式前的模式名称

// --- WiFi / MQTT ---
extern WiFiConfig    wifiConfig;     ///< 当前生效的 WiFi 配置（多网络列表中 active 那一条的镜像）
extern WiFiList      wifiList;       ///< 已保存的多组 WiFi 网络（从 EEPROM 加载）
extern MqttConfig    mqttConfig;     ///< MQTT 配置（从 EEPROM 加载）
extern MqttServerConfig mqttServerConfig; ///< MQTT 服务器地址/端口（从 EEPROM 加载）
extern bool          dhtEnabled;      ///< DHT11 温湿度传感器是否启用（网页"灯带设置"里开关）
extern bool          irEnabled;       ///< 红外接收是否启用
extern bool          otaAutoEnabled;  ///< OTA 定时自动检查升级是否启用
extern float         dhtTemperature;  ///< 最近一次成功读取的温度（℃）
extern float         dhtHumidity;     ///< 最近一次成功读取的湿度（%）
extern volatile int  otaProgress;     ///< OTA 进度：-1空闲 -2失败 0~99下载中 100写入Flash（/otastatus 返回）
extern uint32_t      otaLastUnix;       ///< 当前固件的"上传时间"戳（升级成功后记录）
extern uint32_t      otaLastVersion;    ///< 当前固件版本号（升级成功后记录）
extern char          otaLastDate[24];   ///< 当前固件的上传日期（显示用）
extern uint32_t      otaRemoteVer;      ///< 远程固件版本号（最近一次检查）
extern char          otaRemoteDate[24]; ///< 远程固件上传日期（显示用）
extern uint32_t      otaRemoteSize;     ///< 远程固件大小（字节）
extern bool          otaRemoteOk;       ///< 最近一次检查是否成功
extern bool          otaHasNew;         ///< 远程是否有新版本
extern bool          otaCheckNow;       ///< 网页"立即检查并升级"触发的标志（loop 消费）
extern bool          otaCheckReq;       ///< 网页查看检查信息触发的标志（loop 消费）
extern bool          apTempEnabled;  ///< 热点当前是"自动/强制"开的（非用户勾选「AP模式」）
extern bool          wifiSwitchNow;  ///< 置位后 loop 立即切换到当前选中(active)的 WiFi 网络
extern WiFiClient    espClient;      ///< WiFi TCP 客户端
extern PubSubClient  client;         ///< MQTT 客户端
extern bool          mqttReconnectNow; ///< 置位后 loop 会立即尝试一次 MQTT 重连
extern char          mqttLastMsg[MQTT_LAST_MSG_LEN]; ///< 最近一次收到的 MQTT 消息（供网页显示）
extern uint32_t      mqttMsgCount;     ///< 累计收到的 MQTT 消息条数（0 = 从未收到）
extern uint32_t      mqttLastMsgAt;    ///< 上次收到 MQTT 消息的 millis()（网页算接收时间）
extern String        otaPendingUrl;    ///< 待执行的 OTA 升级链接（非空时 loop 执行升级）
extern int           wifiRetryIdx;     ///< 断网轮询时"正在尝试"的网络下标（-1 = 未在尝试）
extern uint8_t       wifiFailCount[MAX_WIFI_NETS]; ///< 每条网络"连续连接失败"计数（仅 RAM，不落盘）

// --- Web 服务器 ---
extern AsyncWebServer server; ///< 异步 HTTP 服务器（端口 80）

// --- SSE 状态推送 ---
/** 状态版本号：每次状态发生变化时 +1，供 SSE 推送检测使用 */
extern volatile uint32_t stateVersion;

// --- 红外接收 ---
extern IRrecv        irrecv;   ///< 红外接收器
extern decode_results results; ///< 红外解码结果


// ============================================================
// 函数前向声明
// （各 .ino 模块中定义，在其他模块中调用时需要此声明）
// ============================================================

// --- LAND_LED.ino 中的辅助函数 ---
void fanxiang(bool a, CRGB leds[], uint16_t num_leds);
void fill_solid_new(CRGB* ledArray, int numToFill, const CRGB& color);
inline void solid_cut(CRGB leds[], uint16_t num_leds, CRGB solidColor);
inline void applySolidColor(CRGB color, bool animate);
CRGB decimalToCRGB(unsigned long decimal);
CRGB hexToCRGB(const String& hexStr);
void resetEffectState();

// --- wifi_mqtt.ino 中的函数 ---
bool checkInternet();
void reconnect();
void callback(char* topic, byte* payload, unsigned int length);
void getsend(String str);
void extractStringParts(const String& str);
void loadWiFiConfig();
bool saveWiFiConfig();   ///< 保存 WiFi 配置 + 多网络列表到 EEPROM 并回读校验
bool saveWiFiList();     ///< 仅保存多网络列表（用于连接成功后回写 active 项）
bool isWiFiConfigValid(const WiFiConfig &cfg); ///< 判断 WiFiConfig 是否有效可用

// --- 多网络 WiFi 列表操作（均在返回前不做落盘，需调用方自行 saveWiFiConfig）---
bool wifiListAdd(const char* ssid, const char* password);        ///< 添加/更新一条网络（不改连接状态）
bool wifiListSetPassword(int idx, const char* password);         ///< 修改指定网络的密码
bool wifiListDelete(int idx);                                    ///< 删除指定网络
bool wifiListSetActive(int idx);                                 ///< 记录"当前已连接"的网络下标
bool wifiListMove(int idx, int dir);                             ///< 调整优先级顺序（dir: -1 上移 / +1 下移）
void wifiSyncActiveToConfig();                                   ///< 把 active 条目同步到 wifiConfig
void loadMqttConfig();   ///< 从 EEPROM 加载 MQTT 配置（首次用默认值初始化）
bool saveMqttConfig();   ///< 将当前 MQTT 配置写入 EEPROM 并回读校验，返回是否成功
void loadMqttServerConfig(); ///< 从 EEPROM 加载 MQTT 服务器地址/端口（未初始化则写入默认值）
bool saveMqttServerConfig(); ///< 将 MQTT 服务器地址/端口写入 EEPROM 并回读校验
void loadPeriphFlags();  ///< 加载外设/OTA自动升级开关（未初始化时写默认值）
bool savePeriphFlags();  ///< 将外设/OTA自动升级开关写入 EEPROM 并回读校验
void periphTick();       ///< 外设轮询（loop 里调用：DHT11 每 2 秒读一次）
void otaAutoTick();           ///< 定时自动检查升级：间隔到点调巴法云 API，有新版本直接刷入
void otaCheckRequest();       ///< 网页"立即检查并升级"：登记请求（loop 里执行）
void otaSyncVersionFromApi(); ///< 升级成功后同步一次版本号/上传时间到 EEPROM
String otaFormatUnixStr(uint32_t ux); ///< unix 时间戳 → 北京时间显示串
void otaCheckBemfa(bool allowUpgrade); ///< 调巴法云 firmwareVersion 接口比对版本，allowUpgrade=true 时直接刷入
int  connectToWiFi(int wifiStartLed, int wifiEndLed);
void setupWebServer();
void manageWiFiConnection();  ///< 运行时动态 AP 管理 + 断网多网络轮询重连（loop 中调用）
void wifiRetryRestart();      ///< 重置断网轮询排程（手动切换网络后调用，重新给宽限期）
void apForceWindowStart();    ///< 开始"上电强制开 AP"计时（setup 开头调用一次）
bool apForceWindowActive();   ///< 当前是否还在"上电强制开 AP"窗口内
unsigned long apForceWindowRemainMs(); ///< 强制开 AP 窗口剩余毫秒（0 = 已结束）
void notifyStateChanged();    ///< 状态变化通知：stateVersion++ 并触发 SSE 推送

// --- effects.ino 中的灯效函数 ---
void rainbow();
void theaterChase();
void confetti();
void breathing();
void sparkle();
void aurora();
void discoMode();
void randomColorJump();
void stepLighting();
void sparkSparks();
void rainbowString();
void heartbeatPulse();
void ICACHE_FLASH_ATTR fireworkExplosion();  // 存 Flash 节省 IRAM
void musicVisualization();
void showBatteryLevel();

// --- ir_control.ino 中的函数 ---
void hongwai(uint32_t aa);

// --- HTML 网页内容（在 LAND_LED.ino 中定义，存于 Flash） ---
extern const char index_html[] PROGMEM;

#endif // CONFIG_H

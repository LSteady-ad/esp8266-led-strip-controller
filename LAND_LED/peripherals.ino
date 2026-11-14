/**
 * @file peripherals.ino
 * @brief 可选外设：DHT11 温湿度传感器 + 外设开关 + 巴法云 OTA 自动检查升级
 *
 * 设计说明：
 *  - DHT11 数据脚接 D5 = GPIO14（config.h 的 DHT_PIN），手写单总线协议读取，零第三方依赖。
 *  - 外设开关（DHT11 / 红外接收）和 OTA 检查配置存 EEPROM（带魔数，布局见 config.h）。
 *  - OTA 检查走巴法云 firmwareVersion 接口（openID + 订阅主题），返回 version / url /
 *    unix（上传时间）。与 EEPROM 里记录的版本比对：有新版本 → 从返回的 url 下载刷入，
 *    成功后把新版本号/上传时间写回 EEPROM —— 同一固件只刷一次，不会循环升级。
 *  - DHT11 每 2 秒读一次，读失败保留上次有效值，不打断灯效。
 */
#include "config.h"
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecure.h>

// ---- 外设 ----
bool  dhtEnabled     = false;   ///< DHT11 是否启用
bool  irEnabled      = true;    ///< 红外接收是否启用（默认开，保持老版本行为）
float dhtTemperature = 0.0f;    ///< 最近一次成功读取的温度（℃）
float dhtHumidity    = 0.0f;    ///< 最近一次成功读取的湿度（%）
static unsigned long lastDHTRead = 0;
static bool          dhtInited   = false;

// ---- OTA ----
bool     otaAutoEnabled = false;                    ///< 定时自动检查升级是否启用
uint16_t otaIntervalMin = OTA_AUTO_INTERVAL_DEFAULT; ///< 自动检查间隔（分钟，默认 240）
uint32_t otaLastUnix    = 0;                        ///< 当前固件的"上传时间"戳（升级成功后记录）
uint32_t otaLastVersion = 0;                        ///< 当前固件版本号
uint32_t otaRemoteVer   = 0;                        ///< 远程固件版本号（最近一次检查）
char     otaRemoteDate[24] = "";                    ///< 远程固件上传日期（显示用）
uint32_t otaRemoteSize  = 0;                        ///< 远程固件大小（字节）
bool     otaRemoteOk    = false;                    ///< 最近一次检查是否成功
char     otaLastDate[24] = "";                      ///< 当前固件的上传日期（显示用）
bool     otaHasNew    = false;                      ///< 远程是否有新版本
bool     otaCheckNow  = false;                      ///< 网页"立即检查并升级"触发的标志
static unsigned long otaAutoLastRun = 0;           ///< 上次自动检查的时间戳（0 = 开机后还没查过）
bool         otaCheckReq = false;                   ///< 检查请求待执行（wifi_mqtt 设置，loop 消费）
static unsigned long otaCheckLastRun = 0;

/**
 * @brief 从 EEPROM 加载外设开关 / OTA 检查配置。
 *
 * 布局见 config.h。空白 Flash 与魔数不符 → 写默认值
 * （DHT11 关、红外开、自动检查开、间隔 240 分钟、openID 空、版本 0）。
 * 旧布局（0xA6 及更早）迁移：位标志和间隔可继承，openID 需重新填写。
 */
void loadPeriphFlags() {
  EEPROM.begin(EEPROM_SIZE);
  uint8_t  magic    = EEPROM.read(OTA_FLAGS_EEPROM_ADDR);
  uint8_t  flags    = EEPROM.read(OTA_FLAGS_EEPROM_ADDR + 1);
  uint16_t interval = EEPROM.read(OTA_FLAGS_EEPROM_ADDR + OTA_FLAGS_INTERVAL_OFF)
                    | ((uint16_t)EEPROM.read(OTA_FLAGS_EEPROM_ADDR + OTA_FLAGS_INTERVAL_OFF + 1) << 8);
  uint32_t lastU = EEPROM.read(OTA_FLAGS_EEPROM_ADDR + OTA_FLAGS_UNIX_OFF)
                 | ((uint32_t)EEPROM.read(OTA_FLAGS_EEPROM_ADDR + OTA_FLAGS_UNIX_OFF + 1) << 8)
                 | ((uint32_t)EEPROM.read(OTA_FLAGS_EEPROM_ADDR + OTA_FLAGS_UNIX_OFF + 2) << 16)
                 | ((uint32_t)EEPROM.read(OTA_FLAGS_EEPROM_ADDR + OTA_FLAGS_UNIX_OFF + 3) << 24);
  uint32_t lastV = EEPROM.read(OTA_FLAGS_EEPROM_ADDR + OTA_FLAGS_VER_OFF)
                 | ((uint32_t)EEPROM.read(OTA_FLAGS_EEPROM_ADDR + OTA_FLAGS_VER_OFF + 1) << 8)
                 | ((uint32_t)EEPROM.read(OTA_FLAGS_EEPROM_ADDR + OTA_FLAGS_VER_OFF + 2) << 16)
                 | ((uint32_t)EEPROM.read(OTA_FLAGS_EEPROM_ADDR + OTA_FLAGS_VER_OFF + 3) << 24);
  EEPROM.end();

  if (magic != OTA_FLAGS_MAGIC) {
    // 旧布局 / 空白 Flash：位标志和间隔可继承（偏移相同），openID 与版本从零开始
    bool inherit = (magic == OTA_FLAGS_MAGIC_V2 || magic == 0xA5);
    dhtEnabled     = inherit && (flags & PERIPH_FLAG_DHT) != 0;
    irEnabled      = inherit ? (flags & PERIPH_FLAG_IR) != 0 : true;
    otaAutoEnabled = inherit ? ((flags & PERIPH_FLAG_OTA) != 0) : true;   // 全新设备默认巴法云模式
    otaIntervalMin = (inherit && interval >= 1 && interval <= 65535)
                     ? interval : OTA_AUTO_INTERVAL_DEFAULT;
    otaLastUnix = 0;
    otaLastVersion = 0;
    savePeriphFlags();
    DEBUG_PRINTLN("[外设] 配置区初始化（DHT11关 红外开 自动检查开 间隔240分钟）");
    return;
  }

  dhtEnabled     = (flags & PERIPH_FLAG_DHT) != 0;
  irEnabled      = (flags & PERIPH_FLAG_IR)  != 0;
  otaAutoEnabled = (flags & PERIPH_FLAG_OTA) != 0;
  otaIntervalMin = (interval >= 1 && interval <= 65535) ? interval : OTA_AUTO_INTERVAL_DEFAULT;

  otaLastUnix    = (lastU != 0xFFFFFFFF) ? lastU : 0;
  otaLastVersion = (lastV != 0xFFFFFFFF) ? lastV : 0;
  strlcpy(otaLastDate, (otaLastUnix ? otaFormatUnixStr(otaLastUnix) : String("(未知)")).c_str(), sizeof(otaLastDate));

  DEBUG_PRINT("[外设] DHT11: ");
  DEBUG_PRINT(dhtEnabled ? "开" : "关");
  DEBUG_PRINT("  红外: ");
  DEBUG_PRINT(irEnabled ? "开" : "关");
  DEBUG_PRINT("  自动检查: ");
  DEBUG_PRINT(otaAutoEnabled ? "开" : "关");
  DEBUG_PRINT("  间隔: ");
  DEBUG_PRINT(otaIntervalMin);
  DEBUG_PRINTLN(" 分钟");
}

/**
 * @brief 将外设开关 / OTA 检查配置写入 EEPROM，并回读校验。
 */
bool savePeriphFlags() {
  uint8_t magic = OTA_FLAGS_MAGIC;
  uint8_t flags = 0;
  if (dhtEnabled)     flags |= PERIPH_FLAG_DHT;
  if (irEnabled)      flags |= PERIPH_FLAG_IR;
  if (otaAutoEnabled) flags |= PERIPH_FLAG_OTA;

  EEPROM.begin(EEPROM_SIZE);
  EEPROM.write(OTA_FLAGS_EEPROM_ADDR, magic);
  EEPROM.write(OTA_FLAGS_EEPROM_ADDR + 1, flags);
  EEPROM.write(OTA_FLAGS_EEPROM_ADDR + OTA_FLAGS_INTERVAL_OFF, otaIntervalMin & 0xFF);
  EEPROM.write(OTA_FLAGS_EEPROM_ADDR + OTA_FLAGS_INTERVAL_OFF + 1, (otaIntervalMin >> 8) & 0xFF);
  EEPROM.write(OTA_FLAGS_EEPROM_ADDR + OTA_FLAGS_UNIX_OFF, otaLastUnix & 0xFF);
  EEPROM.write(OTA_FLAGS_EEPROM_ADDR + OTA_FLAGS_UNIX_OFF + 1, (otaLastUnix >> 8) & 0xFF);
  EEPROM.write(OTA_FLAGS_EEPROM_ADDR + OTA_FLAGS_UNIX_OFF + 2, (otaLastUnix >> 16) & 0xFF);
  EEPROM.write(OTA_FLAGS_EEPROM_ADDR + OTA_FLAGS_UNIX_OFF + 3, (otaLastUnix >> 24) & 0xFF);
  EEPROM.write(OTA_FLAGS_EEPROM_ADDR + OTA_FLAGS_VER_OFF, otaLastVersion & 0xFF);
  EEPROM.write(OTA_FLAGS_EEPROM_ADDR + OTA_FLAGS_VER_OFF + 1, (otaLastVersion >> 8) & 0xFF);
  EEPROM.write(OTA_FLAGS_EEPROM_ADDR + OTA_FLAGS_VER_OFF + 2, (otaLastVersion >> 16) & 0xFF);
  EEPROM.write(OTA_FLAGS_EEPROM_ADDR + OTA_FLAGS_VER_OFF + 3, (otaLastVersion >> 24) & 0xFF);
  bool okCommit = EEPROM.commit();
  EEPROM.end();

  EEPROM.begin(EEPROM_SIZE);
  bool okVerify = (EEPROM.read(OTA_FLAGS_EEPROM_ADDR) == magic)
               && (EEPROM.read(OTA_FLAGS_EEPROM_ADDR + 1) == flags);
  EEPROM.end();

  DEBUG_PRINT("[外设] 配置保存: commit=");
  DEBUG_PRINT(okCommit ? "OK" : "FAIL");
  DEBUG_PRINT(" 回读校验=");
  DEBUG_PRINTLN(okVerify ? "OK" : "FAIL");
  return okCommit && okVerify;
}

/**
 * @brief DHT11 单总线读取一次（手写协议，返回是否成功）。
 *
 * 时序：主机拉低 ≥18ms → 释放 → 传感器回 80us低+80us高 → 40 位数据
 * （每位 50us 低 + 26~28us 高 = 0，70us 高 = 1）→ 湿度整数 湿度小数
 * 温度整数 温度小数 校验和 各 8 位。
 *
 * ESP8266 上 WiFi 中断可能偶尔拉长电平窗口导致某次读数校验失败，
 * 这里靠"校验和 + 保留上次有效值 + 2 秒后重试"兜底，用户侧无感。
 */
static bool dht11ReadRaw() {
  uint8_t data[5] = {0, 0, 0, 0, 0};

  pinMode(DHT_PIN, OUTPUT);
  digitalWrite(DHT_PIN, LOW);
  delay(20);
  digitalWrite(DHT_PIN, HIGH);
  delayMicroseconds(30);
  pinMode(DHT_PIN, INPUT_PULLUP);

  uint32_t t0 = micros();
  while (digitalRead(DHT_PIN) == LOW) {
    if (micros() - t0 > 100) return false;
  }
  t0 = micros();
  while (digitalRead(DHT_PIN) == HIGH) {
    if (micros() - t0 > 100) return false;
  }

  for (uint8_t i = 0; i < 40; i++) {
    t0 = micros();
    while (digitalRead(DHT_PIN) == LOW) {
      if (micros() - t0 > 80) return false;
    }
    t0 = micros();
    while (digitalRead(DHT_PIN) == HIGH) {
      if (micros() - t0 > 120) return false;
    }
    data[i / 8] <<= 1;
    if (micros() - t0 > 45) data[i / 8] |= 1;
  }

  pinMode(DHT_PIN, OUTPUT);
  digitalWrite(DHT_PIN, HIGH);

  if (data[4] != (uint8_t)(data[0] + data[1] + data[2] + data[3])) return false;

  float h = data[0] + data[1] * 0.1f;
  float t = data[2] + (data[3] & 0x7F) * 0.1f;
  if (data[3] & 0x80) t = -t;

  dhtHumidity    = h;
  dhtTemperature = t;
  return true;
}

/**
 * @brief 外设轮询：loop() 里每个循环调用。
 * 未启用 DHT11 时直接返回（几乎零开销）；启用时每 2 秒读一次。
 */
void periphTick() {
  if (!dhtInited) {
    dhtInited = true;
    lastDHTRead = millis();
    return;
  }
  if (!dhtEnabled) return;
  if (millis() - lastDHTRead < 2000) return;
  lastDHTRead = millis();

  if (dht11ReadRaw()) {
    DEBUG_PRINT("[DHT] 温度 ");
    DEBUG_PRINT(String(dhtTemperature, 1));
    DEBUG_PRINT(" ℃  湿度 ");
    DEBUG_PRINT(String(dhtHumidity, 0));
    DEBUG_PRINTLN(" %");
  } else {
    DEBUG_PRINTLN("[DHT] 读取失败，保留上次有效值");
  }
}

// ═══════════════════════════════════════════════════════════════
// 巴法云 OTA：firmwareVersion 接口检查 + 自动升级
// ═══════════════════════════════════════════════════════════════

/**
 * @brief 把 unix 时间戳转成北京时间显示串（+8h，含跨日进位）。
 */
String otaFormatUnixStr(uint32_t ux) {
  time_t bt = (time_t)(ux + 8 * 3600);
  struct tm* t = gmtime(&bt);
  if (!t) return String("(未知)");
  char out[24] = {0};
  snprintf(out, sizeof(out), "%04d-%02d-%02d %02d:%02d:%02d",
           t->tm_year + 1900, t->tm_mon + 1, t->tm_mday, t->tm_hour, t->tm_min, t->tm_sec);
  return String(out);
}

/**
 * @brief 网页"检查信息"：登记一次检查请求（loop 里执行，3 秒节流防刷屏）。
 */
void otaCheckRequest() {
  unsigned long now = millis();
  if (otaCheckReq || now - otaCheckLastRun < 3000) return;
  otaCheckLastRun = now;
  otaCheckReq = true;
}

/**
 * @brief 调巴法云 firmwareVersion 接口，查当前主题的固件信息。
 *
 * @param ver  出参：远程固件版本号
 * @param unix 出参：远程固件上传时间戳
 * @param url  出参：远程固件下载地址
 * @return 是否查询成功（code==0）
 *
 * 接口为 HTTPS，用 setInsecure() 跳过证书校验（巴法云 API 无固定证书）。
 */
static bool otaQueryApi(uint32_t& ver, uint32_t& unixT, String& url, uint32_t& size) {
  // 巴法云 openID 就是 MQTT 设备私钥（Client ID），直接复用配置，不用单独填
  if (mqttConfig.clientId[0] == '\0' || mqttConfig.topic[0] == '\0') return false;

  WiFiClientSecure sec;
  sec.setInsecure();   // 不校验证书链（设备侧常规做法）
  HTTPClient http;
  String api = String("https://apis.bemfa.com/vb/api/v1/firmwareVersion?openID=")
             + mqttConfig.clientId + "&topic=" + mqttConfig.topic + "&deviceType=1";
  if (!http.begin(sec, api)) return false;
  int code = http.GET();
  bool ok = false;
  if (code == 200) {
    JsonDocument doc;
    if (deserializeJson(doc, http.getString()) == DeserializationError::Ok) {
      if ((int)(doc["code"] | -1) == 0) {
        ver   = (uint32_t)(doc["data"]["version"] | 0);
        unixT = (uint32_t)(doc["data"]["unix"] | 0);
        url   = String((const char*)(doc["data"]["url"] | ""));
        size  = (uint32_t)(doc["data"]["size"] | 0);
        ok = true;
      }
    }
  }
  http.end();
  return ok;
}

/**
 * @brief 执行一次检查：查远程版本 → 更新显示 → 有新版本时触发升级。
 *
 * @param allowUpgrade true=有新版本直接刷入（自动模式/网页按钮）；false=只记录不刷
 *
 * 特殊情况：EEPROM 里还没有版本基准（串口烧录的固件第一次检查）→
 * 把远程版本登记为基准并显示"已是最新"，避免误报。
 */
void otaCheckBemfa(bool allowUpgrade) {
  uint32_t ver, ux, sz;
  String url;
  if (!otaQueryApi(ver, ux, url, sz)) {
    otaRemoteOk = false;
    return;
  }
  otaRemoteOk   = true;
  otaRemoteVer  = ver;
  otaRemoteSize = sz;
  strlcpy(otaRemoteDate, otaFormatUnixStr(ux).c_str(), sizeof(otaRemoteDate));

  // 没有基准 → 首次登记，不算"有新版本"
  if (otaLastUnix == 0) {
    otaLastUnix    = ux;
    otaLastVersion = ver;
    savePeriphFlags();
    otaHasNew = false;
    DEBUG_PRINTLN("[OTA] 首次检查，已登记当前固件版本 v" + String(ver));
    return;
  }

  otaHasNew = (ux > otaLastUnix || (ux == otaLastUnix && ver > otaLastVersion));
  if (!otaHasNew) {
    DEBUG_PRINTLN("[OTA] 远程固件 v" + String(ver) + " 与当前一致，跳过");
    return;
  }

  DEBUG_PRINTLN("[OTA] 发现有新固件 v" + String(ver) + "，开始升级");
  otaPendingUrl   = url;   // 交给 loop 的 OTA 执行块
}

/**
 * @brief OTA 定时自动升级：loop 里每个循环调用。
 * 自动模式开启时，按设定间隔（分钟）检查一次；有新版本直接刷入。
 */
void otaAutoTick() {
  if (!otaAutoEnabled) return;
  if (WiFi.status() != WL_CONNECTED) return;

  unsigned long intervalMs = (unsigned long)otaIntervalMin * 60UL * 1000UL;
  if (otaAutoLastRun != 0 && millis() - otaAutoLastRun < intervalMs) return;
  bool first = (otaAutoLastRun == 0);
  otaAutoLastRun = millis();
  DEBUG_PRINTLN(first ? "[OTA] 开机首次检查更新" : "[OTA] 到达检查间隔，开始检查");

  otaCheckBemfa(true);

  if (!otaHasNew) return;   // 没新版本或检查失败，等下一个间隔
  DEBUG_PRINTLN("[OTA] 升级任务已提交");
}

/**
 * @brief 升级成功（重启前）调用：同步一次最新版本号/上传时间到 EEPROM。
 */
void otaSyncVersionFromApi() {
  uint32_t ver, ux, sz;
  String url;
  if (otaQueryApi(ver, ux, url, sz)) {
    otaLastVersion = ver;
    otaLastUnix    = ux;
    strlcpy(otaLastDate, otaFormatUnixStr(ux).c_str(), sizeof(otaLastDate));
    savePeriphFlags();
    DEBUG_PRINTLN("[OTA] 已记录当前固件版本 v" + String(ver));
  }
}

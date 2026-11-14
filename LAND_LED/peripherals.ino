/**
 * @file peripherals.ino
 * @brief 可选外设：DHT11 温湿度传感器 + 外设开关 + OTA 定时自动升级
 *
 * 设计说明：
 *  - DHT11 数据脚接 D5 = GPIO14（config.h 的 DHT_PIN），用**手写单总线协议**读取，
 *    不依赖任何第三方库 —— 与本项目"第三方库全部内置在 src/、工程零外部依赖"的思路一致。
 *  - 外设开关（DHT11 / 红外接收）和 OTA 定时自动升级的配置存在 EEPROM 里（带魔数，
 *    未初始化时默认：DHT11 关、红外开、自动升级关 —— 与老版本行为一致）。
 *  - DHT11 最快 1 秒读一次，每 2 秒读一次；读失败（接线松、校验错）时
 *    保留上一次的有效值，不打断灯效。
 */
#include "config.h"
#include <ESP8266HTTPClient.h>
#include <time.h>

// ---- 外设 ----
bool  dhtEnabled     = false;   ///< DHT11 是否启用
bool  irEnabled      = true;    ///< 红外接收是否启用（默认开，保持老版本行为）
float dhtTemperature = 0.0f;    ///< 最近一次成功读取的温度（℃）
float dhtHumidity    = 0.0f;    ///< 最近一次成功读取的湿度（%）
static unsigned long lastDHTRead = 0;   ///< 上次读取时间戳
static bool          dhtInited   = false;

// ---- OTA 定时自动升级 ----
bool         otaAutoEnabled = false;                          ///< 定时自动升级是否启用
uint16_t     otaIntervalMin = OTA_AUTO_INTERVAL_DEFAULT;      ///< 自动检查间隔（分钟，1~65535）
char         otaAutoUrl[129] = "";                            ///< 自动升级链接（空 = 内置 OTA_AUTO_URL）
char         otaRemoteTime[33] = "";                          ///< 远程固件上传时间（显示用）
char         otaLocalTime[33] = "";                           ///< 当前固件上传时间（显示用）
bool         otaCheckSame  = false;                           ///< 远程与当前是否同一版本
unsigned int otaCheckCount = 0;                               ///< 完成的检查次数（网页轮询用）
static char  otaLocalLM[33] = "";                             ///< 当前固件的 Last-Modified 原始串（比对用）
static unsigned long otaAutoLastRun = 0;                      ///< 上次自动检查的时间戳（0 = 开机后还没查过）
static bool  otaCheckReq = false;                             ///< 网页请求了一次检查
static unsigned long otaCheckLastRun = 0;

/**
 * @brief 从 EEPROM 加载外设开关 / OTA 定时自动升级配置。
 *
 * 存储区域布局（OTA_FLAGS_EEPROM_ADDR 起，共 OTA_FLAGS_TOTAL 字节）：
 *   [0] 魔数 0xA5   [1] 位标志   [2~3] 检查间隔分钟
 *   [3~34] 当前固件的 Last-Modified 原始串   [35~162] 自动升级链接
 * 空白 Flash 与魔数不符 → 写入默认值（DHT11 关、红外开、自动升级关，与老版本一致）。
 */
void loadPeriphFlags() {
  EEPROM.begin(EEPROM_SIZE);
  uint8_t magic    = EEPROM.read(OTA_FLAGS_EEPROM_ADDR);
  uint8_t flags    = EEPROM.read(OTA_FLAGS_EEPROM_ADDR + 1);
  uint16_t interval = EEPROM.read(OTA_FLAGS_EEPROM_ADDR + OTA_FLAGS_INTERVAL_OFF)
                    | ((uint16_t)EEPROM.read(OTA_FLAGS_EEPROM_ADDR + OTA_FLAGS_INTERVAL_OFF + 1) << 8);
  char    lmBuf[OTA_FLAGS_LM_LEN + 1]  = {0};
  for (int i = 0; i < OTA_FLAGS_LM_LEN; i++)  lmBuf[i] = (char)EEPROM.read(OTA_FLAGS_EEPROM_ADDR + OTA_FLAGS_LM_OFF + i);
  char    urlBuf[OTA_FLAGS_URL_LEN + 1] = {0};
  for (int i = 0; i < OTA_FLAGS_URL_LEN; i++) urlBuf[i] = (char)EEPROM.read(OTA_FLAGS_EEPROM_ADDR + OTA_FLAGS_URL_OFF + i);
  EEPROM.end();

  if (magic != OTA_FLAGS_MAGIC) {
    // 布局迁移：0xA5 = 旧布局（间隔是"每天X点"，[2] 存小时，[3~] 存上传时间）。
    // 新布局（0xA6）把 [2~3] 当分钟 uint16 读，会读到 4+'S'<<8 之类的垃圾值
    // （比如 21252），所以旧区域不能直接沿用 —— 只继承位标志字节（偏移相同），
    // 间隔 / 上传时间戳 / 升级链接全部回默认重新登记。
    // 全新设备（空白 Flash）→ 直接默认值。
    if (magic == OTA_FLAGS_MAGIC_V1) {
      dhtEnabled     = (flags & PERIPH_FLAG_DHT) != 0;
      irEnabled      = (flags & PERIPH_FLAG_IR)  != 0;
      otaAutoEnabled = (flags & PERIPH_FLAG_OTA) != 0;
      otaIntervalMin = OTA_AUTO_INTERVAL_DEFAULT;
      strlcpy(otaLocalLM, "", sizeof(otaLocalLM));
      strlcpy(otaLocalTime, "(未知)", sizeof(otaLocalTime));
      strlcpy(otaAutoUrl, "", sizeof(otaAutoUrl));
      savePeriphFlags();
      DEBUG_PRINTLN("[外设] 检测到旧布局(0xA5)，开关已迁移、其余重新登记");
      return;
    }
    // 首次上电（空白 Flash）：默认值
    dhtEnabled     = false;
    irEnabled      = true;
    otaAutoEnabled = false;
    savePeriphFlags();
    DEBUG_PRINTLN("[外设] 开关区首次初始化（DHT11关 红外开 定时自动升级关）");
    return;
  }

  dhtEnabled     = (flags & PERIPH_FLAG_DHT) != 0;
  irEnabled      = (flags & PERIPH_FLAG_IR)  != 0;
  otaAutoEnabled = (flags & PERIPH_FLAG_OTA) != 0;
  // 间隔合理上限 43200 分钟（30 天）；0 / 0xFFFF / 超上限 → 回默认
  otaIntervalMin = (interval >= 1 && interval <= 43200) ? interval : OTA_AUTO_INTERVAL_DEFAULT;

  // Last-Modified 原始串：要求全部为可打印字符，否则视为未知
  bool lmOk = (lmBuf[0] != '\0' && lmBuf[0] != (char)0xFF);
  for (int i = 0; lmOk && i < OTA_FLAGS_LM_LEN && lmBuf[i]; i++) {
    if ((uint8_t)lmBuf[i] < 0x20 || (uint8_t)lmBuf[i] > 0x7E) lmOk = false;
  }
  strlcpy(otaLocalLM, lmOk ? lmBuf : "", sizeof(otaLocalLM));
  strlcpy(otaLocalTime, otaFormatLMStr(otaLocalLM).c_str(), sizeof(otaLocalTime));
  if (!lmOk) strlcpy(otaLocalTime, "(未知)", sizeof(otaLocalTime));

  // 自动升级链接：要求为空或以 http:// / https:// 开头，否则恢复内置默认
  bool urlOk = (urlBuf[0] == '\0' ||
                strncmp(urlBuf, "http://", 7) == 0 ||
                strncmp(urlBuf, "https://", 8) == 0);
  strlcpy(otaAutoUrl, urlOk ? urlBuf : "", sizeof(otaAutoUrl));

  DEBUG_PRINT("[外设] DHT11: ");
  DEBUG_PRINT(dhtEnabled ? "开" : "关");
  DEBUG_PRINT("  红外: ");
  DEBUG_PRINT(irEnabled ? "开" : "关");
  DEBUG_PRINT("  自动升级: ");
  DEBUG_PRINT(otaAutoEnabled ? "开" : "关");
  DEBUG_PRINT("  间隔: ");
  DEBUG_PRINT(otaIntervalMin);
  DEBUG_PRINTLN(" 分钟");
}

/**
 * @brief 将外设开关 / OTA 定时自动升级配置写入 EEPROM，并回读校验。
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
  for (int i = 0; i < OTA_FLAGS_LM_LEN; i++) {
    char c = (i < (int)strlen(otaLocalLM)) ? otaLocalLM[i] : 0;
    EEPROM.write(OTA_FLAGS_EEPROM_ADDR + OTA_FLAGS_LM_OFF + i, c);
  }
  for (int i = 0; i < OTA_FLAGS_URL_LEN; i++) {
    char c = (i < (int)strlen(otaAutoUrl)) ? otaAutoUrl[i] : 0;
    EEPROM.write(OTA_FLAGS_EEPROM_ADDR + OTA_FLAGS_URL_OFF + i, c);
  }
  bool okCommit = EEPROM.commit();
  EEPROM.end();

  EEPROM.begin(EEPROM_SIZE);
  uint8_t vm = EEPROM.read(OTA_FLAGS_EEPROM_ADDR);
  uint8_t vf = EEPROM.read(OTA_FLAGS_EEPROM_ADDR + 1);
  uint16_t vh = EEPROM.read(OTA_FLAGS_EEPROM_ADDR + OTA_FLAGS_INTERVAL_OFF)
              | ((uint16_t)EEPROM.read(OTA_FLAGS_EEPROM_ADDR + OTA_FLAGS_INTERVAL_OFF + 1) << 8);
  EEPROM.end();
  bool okVerify = (vm == magic && vf == flags && vh == otaIntervalMin);

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

  // 1. 主机发起：拉低 20ms
  pinMode(DHT_PIN, OUTPUT);
  digitalWrite(DHT_PIN, LOW);
  delay(20);
  digitalWrite(DHT_PIN, HIGH);
  delayMicroseconds(30);
  pinMode(DHT_PIN, INPUT_PULLUP);

  // 2. 传感器响应（80us 低 + 80us 高），带超时保护
  uint32_t t0 = micros();
  while (digitalRead(DHT_PIN) == LOW) {
    if (micros() - t0 > 100) return false;
  }
  t0 = micros();
  while (digitalRead(DHT_PIN) == HIGH) {
    if (micros() - t0 > 100) return false;
  }

  // 3. 读 40 位
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
    if (micros() - t0 > 45) data[i / 8] |= 1;   // 高电平 >45us = 1
  }

  // 4. 恢复空闲（推高电平，方便下一次发起）
  pinMode(DHT_PIN, OUTPUT);
  digitalWrite(DHT_PIN, HIGH);

  // 5. 校验和
  if (data[4] != (uint8_t)(data[0] + data[1] + data[2] + data[3])) return false;

  float h = data[0] + data[1] * 0.1f;
  float t = data[2] + (data[3] & 0x7F) * 0.1f;
  if (data[3] & 0x80) t = -t;   // DHT11 量程 0~50℃，按协议兜个底

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
    lastDHTRead = millis();   // 上电先等 2 秒再读第一针（给传感器上电稳定时间）
    return;
  }
  if (!dhtEnabled) return;
  if (millis() - lastDHTRead < 2000) return;
  lastDHTRead = millis();

  bool ok = dht11ReadRaw();
  if (ok) {
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
// OTA 定时自动升级 / 检查更新
// ═══════════════════════════════════════════════════════════════

/**
 * @brief 解析 HTTP 的 Last-Modified 头（RFC1123 格式，
 *        如 "Sat, 14 Nov 2026 11:44:29 GMT"）→ "2026-11-14 19:44:29"（北京时间）。
 *        解析失败返回 ""。+8 小时直接换算成北京时间，与巴法云 OTA 页面显示一致。
 */
String otaFormatLMStr(const String& raw) {
  // Last-Modified 原始格式："Sat, 14 Nov 2026 11:44:29 GMT"
  // 注意：星期后面跟着逗号（"Sat,"），用 %*s 整个跳过；
  // 若用 %*3s 只吃掉 "Sat"，逗号会留给后面的 %d 导致解析失败返回空。
  int d, y, hh, mm, ss;
  char monStr[4] = {0};
  if (sscanf(raw.c_str(), "%*s %d %3s %d %d:%d:%d", &d, monStr, &y, &hh, &mm, &ss) != 6) return "";
  static const char* months[] = {"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"};
  int mon = -1;
  for (int i = 0; i < 12; i++) {
    if (strncmp(monStr, months[i], 3) == 0) { mon = i; break; }
  }
  if (mon < 0) return "";

  // GMT → 北京时间（+8 小时）。用天数算法先算 UTC 纪元秒再整体平移，
  // 避免"直接在小时上加 8"造成的跨日/跨月溢出（比如 20:00 GMT 加 8 应该是次日 4 点）。
  y -= (mon + 1 <= 2) ? 1 : 0;
  long era = (y >= 0 ? y : y - 399) / 400;
  long yoe = y - era * 400;
  long doy = (153 * (mon + 1 + (mon + 1 > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  long epochUTC = (era * 146097 + doe - 719468) * 86400L + hh * 3600L + mm * 60L + ss;
  time_t beijing = (time_t)(epochUTC + 8 * 3600);
  struct tm* bt = gmtime(&beijing);
  if (!bt) return "";
  char out[24] = {0};
  snprintf(out, sizeof(out), "%04d-%02d-%02d %02d:%02d:%02d",
           bt->tm_year + 1900, bt->tm_mon + 1, bt->tm_mday, bt->tm_hour, bt->tm_min, bt->tm_sec);
  return String(out);
}

/**
 * @brief 取"实际用于自动升级的链接"：网页里存了就用存的，没存用内置巴法云链接。
 */
String otaAutoUrlEffective() {
  return (otaAutoUrl[0] != '\0') ? String(otaAutoUrl) : String(OTA_AUTO_URL);
}

/**
 * @brief HEAD 一次固件链接，取 Last-Modified（即巴法云 OTA 页面的"上传时间"）。
 *        只拿响应头不下载文件，一两秒内完成。失败 / 无该头返回 ""。
 */
String otaFetchRemoteLM(const String& url) {
  WiFiClient c;
  HTTPClient http;
  http.begin(c, url);
  const char* keys[] = {"Last-Modified"};
  http.collectHeaders(keys, 1);
  int code = http.sendRequest("HEAD");
  String lm = "";
  if (code > 0) lm = http.header("Last-Modified");
  http.end();
  return lm;
}

/** @brief 当前固件上传时间的显示串（EEPROM 里没记录时返回 "(未知)"）。 */
String otaGetLocalTimeStr() {
  return (otaLocalTime[0] != '\0') ? String(otaLocalTime) : String("(未知)");
}

/**
 * @brief 网页"检查更新"：登记一次检查请求（loop 里执行），3 秒节流防刷屏。
 */
void otaCheckRequest() {
  unsigned long now = millis();
  if (otaCheckReq || now - otaCheckLastRun < 3000) return;
  otaCheckLastRun = now;
  otaCheckReq = true;
}

/**
 * @brief 执行一次"检查更新"：HEAD 自动升级链接 → 记录远程上传时间 → 比对版本。
 *        只比对、不刷机（刷机由 otaAutoTick 触发）。
 */
void otaCheckRun() {
  String lm = otaFetchRemoteLM(otaAutoUrlEffective());
  if (lm.length() == 0) {
    strlcpy(otaRemoteTime, "检查失败", sizeof(otaRemoteTime));
    otaCheckSame = false;
    otaCheckCount++;
    return;
  }
  strlcpy(otaRemoteTime, otaFormatLMStr(lm).c_str(), sizeof(otaRemoteTime));
  // 本地没有基准（串口烧录后第一次检查）：把远程上传时间登记为基准，
  // 这样检查更新才不会一直报"有新版本"。
  if (strlen(otaLocalLM) == 0) {
    otaSetLocalLM(lm);
    otaCheckSame = true;
  } else {
    otaCheckSame = (lm == String(otaLocalLM));
  }
  otaCheckCount++;
}

/**
 * @brief 记录"当前固件对应 bin 的上传时间"（升级成功后调用）。
 *        写入 EEPROM，供每天定时检查时比对；空串表示未知
 *        （下次定时检查会多刷一次并记录，属正常现象）。
 */
void otaSetLocalLM(const String& lm) {
  strlcpy(otaLocalLM, lm.c_str(), sizeof(otaLocalLM));
  strlcpy(otaLocalTime, otaFormatLMStr(lm).c_str(), sizeof(otaLocalTime));
  savePeriphFlags();
  DEBUG_PRINTLN("[OTA] 已记录当前固件上传时间: " + otaGetLocalTimeStr());
}

/**
 * @brief OTA 定时自动升级：loop 里每个循环调用。
 *
 * 逻辑：每间隔设定的小时数（默认 24 小时）检查一次 ——
 *  1. HEAD 自动升级链接，读 Last-Modified（= 巴法云页面的"上传时间"）
 *  2. 与 EEPROM 里存的"当前固件的上传时间"比对
 *  3. 相同 → 什么都不做（这就是不会循环刷机的关键）
 *     不同 → 触发升级（otaPendingUrl），升级成功后把新的上传时间写入 EEPROM
 *
 * 开机后的第一次检查会立即执行，之后按间隔进行。
 */
void otaAutoTick() {
  if (!otaAutoEnabled) return;
  if (WiFi.status() != WL_CONNECTED) return;

  unsigned long intervalMs = (unsigned long)otaIntervalMin * 60UL * 1000UL;
  if (otaAutoLastRun != 0 && millis() - otaAutoLastRun < intervalMs) return;
  bool first = (otaAutoLastRun == 0);      // 开机后的第一次检查
  otaAutoLastRun = millis();
  DEBUG_PRINTLN(first ? "[OTA] 开机首次检查更新" : "[OTA] 到达检查间隔，开始检查");

  String lm = otaFetchRemoteLM(otaAutoUrlEffective());
  if (lm.length() == 0) {
    DEBUG_PRINTLN("[OTA] 读取远端上传时间失败，1 分钟后重试");
    otaAutoLastRun = millis() - intervalMs + 60000UL;   // 1 分钟后重试
    return;
  }

  strlcpy(otaRemoteTime, otaFormatLMStr(lm).c_str(), sizeof(otaRemoteTime));
  otaCheckCount++;

  // 本地还没有基准（串口烧录的固件第一次检查）：把远程上传时间登记为基准，
  // 否则之后每次检查都会误报"有新版本"。
  if (strlen(otaLocalLM) == 0) {
    otaSetLocalLM(lm);
    otaCheckSame = true;
    DEBUG_PRINTLN("[OTA] 首次检查，已记录当前固件上传时间: " + otaGetLocalTimeStr());
    return;
  }

  otaCheckSame = (lm == String(otaLocalLM));
  if (otaCheckSame) {
    DEBUG_PRINTLN("[OTA] 远程固件与当前一致，跳过升级");
    return;
  }

  DEBUG_PRINTLN(String("[OTA] 发现有新固件（远端上传时间 ") + otaRemoteTime + "），开始升级");
  otaPendingUrl = otaAutoUrlEffective();   // 交给 loop 的 OTA 执行块
}

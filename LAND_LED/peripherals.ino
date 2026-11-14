/**
 * @file peripherals.ino
 * @brief 可选外设：DHT11 温湿度传感器 + 外设开关 + OTA 升级链接基线
 *
 * 设计说明：
 *  - DHT11 数据脚接 D5 = GPIO14（config.h 的 DHT_PIN），用**手写单总线协议**读取，
 *    不依赖任何第三方库 —— 与本项目"第三方库全部内置在 src/、工程零外部依赖"的思路一致。
 *  - 外设开关（DHT11 / 红外接收）和 OTA 升级配置存在 EEPROM 里（带魔数，
 *    未初始化时默认：DHT11 关、红外开、手动模式 —— 与老版本行为一致）。
 *  - DHT11 最快 1 秒读一次，每 2 秒读一次；读失败（接线松、校验错）时
 *    保留上一次的有效值，不打断灯效。
 *  - OTA 升级方式：自动模式 = 监听 MQTT 订阅主题推送的固件链接（wifi_mqtt.ino 的
 *    callback 处理）；手动模式 = 网页"一键升级"。升级成功后用 otaSetLocalLM 记录
 *    该固件文件的上传时间（Last-Modified），再次收到相同文件时自动跳过。
 */
#include "config.h"
#include <ESP8266HTTPClient.h>

// ---- 外设 ----
bool  dhtEnabled     = false;   ///< DHT11 是否启用
bool  irEnabled      = true;    ///< 红外接收是否启用（默认开，保持老版本行为）
float dhtTemperature = 0.0f;    ///< 最近一次成功读取的温度（℃）
float dhtHumidity    = 0.0f;    ///< 最近一次成功读取的湿度（%）
static unsigned long lastDHTRead = 0;   ///< 上次读取时间戳
static bool          dhtInited   = false;

// ---- OTA ----
bool otaAutoEnabled = false;      ///< 升级模式：true=自动（监听主题推送）false=手动
char otaAutoUrl[129] = "";        ///< 升级固件链接（空 = 内置 OTA_AUTO_URL）
char otaLocalLM[33] = "";         ///< 当前固件对应 bin 的 Last-Modified 原始串（比对用）
static bool periphInited = false; ///< 开关区是否已加载

/**
 * @brief 从 EEPROM 加载外设开关 / OTA 升级配置。
 *
 * 存储区域布局（OTA_FLAGS_EEPROM_ADDR 起，共 OTA_FLAGS_TOTAL 字节）：
 *   [0] 魔数 0xA6   [1] 位标志（bit0=DHT11 bit1=红外 bit2=自动模式）
 *   [2~3] 预留      [4~35] 当前固件的 Last-Modified 原始串   [36~163] 升级链接
 * 空白 Flash 与魔数不符 → 写入默认值（DHT11 关、红外开、手动模式，与老版本一致）。
 */
void loadPeriphFlags() {
  EEPROM.begin(EEPROM_SIZE);
  uint8_t magic = EEPROM.read(OTA_FLAGS_EEPROM_ADDR);
  uint8_t flags = EEPROM.read(OTA_FLAGS_EEPROM_ADDR + 1);
  char    lmBuf[OTA_FLAGS_LM_LEN + 1]  = {0};
  for (int i = 0; i < OTA_FLAGS_LM_LEN; i++)  lmBuf[i] = (char)EEPROM.read(OTA_FLAGS_EEPROM_ADDR + OTA_FLAGS_LM_OFF + i);
  char    urlBuf[OTA_FLAGS_URL_LEN + 1] = {0};
  for (int i = 0; i < OTA_FLAGS_URL_LEN; i++) urlBuf[i] = (char)EEPROM.read(OTA_FLAGS_EEPROM_ADDR + OTA_FLAGS_URL_OFF + i);
  EEPROM.end();

  if (magic != OTA_FLAGS_MAGIC) {
    // 首次上电 / 旧版本升级上来：写默认值（与老版本行为一致）
    dhtEnabled     = false;
    irEnabled      = true;
    otaAutoEnabled = false;
    savePeriphFlags();
    DEBUG_PRINTLN("[外设] 开关区首次初始化（DHT11关 红外开 手动模式）");
    return;
  }

  dhtEnabled     = (flags & PERIPH_FLAG_DHT) != 0;
  irEnabled      = (flags & PERIPH_FLAG_IR)  != 0;
  otaAutoEnabled = (flags & PERIPH_FLAG_OTA) != 0;

  // Last-Modified 原始串：要求全部为可打印字符，否则视为未知
  bool lmOk = (lmBuf[0] != '\0' && lmBuf[0] != (char)0xFF);
  for (int i = 0; lmOk && i < OTA_FLAGS_LM_LEN && lmBuf[i]; i++) {
    if ((uint8_t)lmBuf[i] < 0x20 || (uint8_t)lmBuf[i] > 0x7E) lmOk = false;
  }
  strlcpy(otaLocalLM, lmOk ? lmBuf : "", sizeof(otaLocalLM));

  // 升级链接：要求为空或以 http:// / https:// 开头，否则恢复内置默认
  bool urlOk = (urlBuf[0] == '\0' ||
                strncmp(urlBuf, "http://", 7) == 0 ||
                strncmp(urlBuf, "https://", 8) == 0);
  strlcpy(otaAutoUrl, urlOk ? urlBuf : "", sizeof(otaAutoUrl));

  DEBUG_PRINT("[外设] DHT11: ");
  DEBUG_PRINT(dhtEnabled ? "开" : "关");
  DEBUG_PRINT("  红外: ");
  DEBUG_PRINT(irEnabled ? "开" : "关");
  DEBUG_PRINT("  OTA: ");
  DEBUG_PRINTLN(otaAutoEnabled ? "自动" : "手动");
}

/**
 * @brief 将外设开关 / OTA 升级配置写入 EEPROM，并回读校验。
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
  char    vl[OTA_FLAGS_LM_LEN + 1] = {0};
  for (int i = 0; i < OTA_FLAGS_LM_LEN; i++) vl[i] = (char)EEPROM.read(OTA_FLAGS_EEPROM_ADDR + OTA_FLAGS_LM_OFF + i);
  EEPROM.end();
  bool okVerify = (vm == magic && vf == flags && strcmp(vl, otaLocalLM) == 0);

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

/**
 * @brief 取"实际用于升级的链接"：网页里存了就用存的，没存用内置巴法云链接。
 */
String otaAutoUrlEffective() {
  return (otaAutoUrl[0] != '\0') ? String(otaAutoUrl) : String(OTA_AUTO_URL);
}

/**
 * @brief HEAD 一次固件链接，取 Last-Modified（即文件服务器显示的"上传时间"）。
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

/**
 * @brief 记录"当前固件对应 bin 的上传时间"（升级成功后调用）。
 *        写入 EEPROM；再次收到相同文件（上传时间没变）的主题推送时自动跳过。
 *        空串表示未知（下次收到推送会多刷一次并记录，属正常现象）。
 */
void otaSetLocalLM(const String& lm) {
  strlcpy(otaLocalLM, lm.c_str(), sizeof(otaLocalLM));
  savePeriphFlags();
  DEBUG_PRINTLN("[OTA] 已记录当前固件上传时间: " + (lm.length() ? lm : String("(未知)")));
}

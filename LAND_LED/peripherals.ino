/**
 * @file peripherals.ino
 * @brief 可选外设：DHT11 温湿度传感器 + 外设/OTA自动升级开关的存取
 *
 * 设计说明：
 *  - DHT11 数据脚接 D5 = GPIO14（config.h 的 DHT_PIN），用**手写单总线协议**读取，
 *    不依赖任何第三方库 —— 与本项目"第三方库全部内置在 src/、工程零外部依赖"的思路一致，
 *    任何电脑 clone 下来都能直接编译。
 *  - 外设开关（DHT11 / 红外接收）和"OTA 开机自动升级"开关存在 EEPROM 里（带魔数，
 *    未初始化时默认：DHT11 关、红外开、自动升级关 —— 与老版本行为一致）。
 *  - DHT11 最快 1 秒读一次，按测试代码的节奏每 2 秒读一次；
 *    读失败（接线松、校验错）时保留上一次的有效值，不打断灯效。
 */
#include "config.h"

bool  dhtEnabled     = false;   ///< DHT11 是否启用
bool  irEnabled      = true;    ///< 红外接收是否启用（默认开，保持老版本行为）
bool  otaAutoEnabled = false;   ///< OTA 开机自动升级是否启用
float dhtTemperature = 0.0f;    ///< 最近一次成功读取的温度（℃）
float dhtHumidity    = 0.0f;    ///< 最近一次成功读取的湿度（%）

static unsigned long lastDHTRead = 0;   ///< 上次读取时间戳
static bool          dhtInited   = false;

/**
 * @brief 从 EEPROM 加载外设 / OTA自动升级 开关。
 *
 * 存储格式（2 字节，追加在 MQTT 服务器配置之后）：[魔数 0xA5][位标志]。
 * 空白 Flash 读出 0xFF 0xFF，与魔数不符 → 视为未初始化 → 写入默认值并落盘。
 */
void loadPeriphFlags() {
  EEPROM.begin(EEPROM_SIZE);
  uint8_t magic = EEPROM.read(OTA_FLAGS_EEPROM_ADDR);
  uint8_t flags = EEPROM.read(OTA_FLAGS_EEPROM_ADDR + 1);
  EEPROM.end();

  if (magic != OTA_FLAGS_MAGIC) {
    // 首次上电 / 旧版本升级上来：写默认值（与老版本行为一致）
    dhtEnabled     = false;
    irEnabled      = true;
    otaAutoEnabled = false;
    savePeriphFlags();
    DEBUG_PRINTLN("[外设] 开关区首次初始化（DHT11关 红外开 OTA自动升级关）");
    return;
  }

  dhtEnabled     = (flags & PERIPH_FLAG_DHT) != 0;
  irEnabled      = (flags & PERIPH_FLAG_IR)  != 0;
  otaAutoEnabled = (flags & PERIPH_FLAG_OTA) != 0;

  DEBUG_PRINT("[外设] DHT11: ");
  DEBUG_PRINT(dhtEnabled ? "开" : "关");
  DEBUG_PRINT("  红外: ");
  DEBUG_PRINT(irEnabled ? "开" : "关");
  DEBUG_PRINT("  OTA自动升级: ");
  DEBUG_PRINTLN(otaAutoEnabled ? "开" : "关");
}

/**
 * @brief 将外设 / OTA自动升级 开关写入 EEPROM，并回读校验。
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
  bool okCommit = EEPROM.commit();
  EEPROM.end();

  EEPROM.begin(EEPROM_SIZE);
  uint8_t vm = EEPROM.read(OTA_FLAGS_EEPROM_ADDR);
  uint8_t vf = EEPROM.read(OTA_FLAGS_EEPROM_ADDR + 1);
  EEPROM.end();
  bool okVerify = (vm == magic && vf == flags);

  DEBUG_PRINT("[外设] 开关保存: commit=");
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

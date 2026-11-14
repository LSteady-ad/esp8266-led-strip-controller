/**
 * @file wifi_mqtt.ino
 * @brief LAND_LED 灯光控制系统 - WiFi / MQTT / Web 服务器模块
 *
 * 包含：
 *  - WiFi 配置加载 / 保存（EEPROM）
 *  - WiFi 连接（STA + AP 双模式）
 *  - MQTT 连接与回调（巴法云）
 *  - AsyncWebServer 路由注册（/、/control、/scanwifi、/wifilist、/state、/events、/update、/music）
 *
 * 此文件由 Arduino IDE 自动与主文件合并编译，无需额外 include。
 */

// ============================================================
// 各网络"连续连接失败"计数（**仅 RAM，不落盘**）
// ============================================================
//
// 与 wifiList.nets[] 下标一一对应：
//   - 新增网络 → 该位清零；改了密码 → 该位清零（用户已介入，重新观察）
//   - 删除网络 → 高位左移补齐；调整顺序 → 跟着一起交换
//   - 成功连上 → 该位清零
// 之所以不写进 WiFiList 结构体落盘：改结构体会让老设备 EEPROM 里的列表
// 校验失败（memcmp 回读校验），不值得为一个诊断用的计数冒丢配置的风险。
uint8_t wifiFailCount[MAX_WIFI_NETS] = {0};

/**
 * @brief 判断 EEPROM 中读出的字符串字段是否"合法"。
 *
 * 空白 Flash 为全 0xFF，读进 char 数组后既没有 '\0' 终止符，
 * 又全是 0xFF 非法字节。若直接当作字符串使用（WiFi.begin / JSON 序列化 /
 * Serial 打印），会发生越界读取，产生上百字节乱码，进而导致：
 *   - WiFi 协议栈收到非法 SSID，STA 连接逻辑异常
 *   - 网页 /state 返回乱码，前端解析失败、字段显示空白
 *
 * 判定规则：
 *   - 在 cap 字节内遇到 '\0'        → 合法（允许空字符串）
 *   - 遇到 0xFF                     → 非法（空白 Flash 垃圾）
 *   - 遇到控制字符(<0x20 或 0x7F)   → 非法
 *   - cap 字节内始终没有 '\0'        → 非法（无终止符）
 *
 * 注意：允许 >=0x80 的字节，因为 SSID / 密码可能包含 UTF-8 中文。
 */
static bool isSaneField(const char* s, size_t cap) {
  for (size_t i = 0; i < cap; i++) {
    uint8_t c = (uint8_t)s[i];
    if (c == '\0')          return true;   // 正常终止
    if (c == 0xFF)          return false;  // 空白 Flash 垃圾
    if (c < 0x20 || c == 0x7F) return false; // 控制字符
  }
  return false;                            // 无终止符
}

/**
 * @brief 判断当前 WiFiConfig 是否为"有效且可用的配置"。
 *
 * 要求 ssid / password 字段合法，且 ssid 非空。
 * 供 loadWiFiConfig() 使用：数据非法（空白 Flash / 损坏 / 恢复出厂后的
 * 0xFF）时回退为"未配置"状态。
 */
bool isWiFiConfigValid(const WiFiConfig &cfg) {
  if (!isSaneField(cfg.ssid,     sizeof(cfg.ssid)))     return false;
  if (!isSaneField(cfg.password, sizeof(cfg.password))) return false;
  return (cfg.ssid[0] != '\0');   // 必须存在实际 SSID
}

/**
 * @brief 把多网络列表中的 active 条目同步到 wifiConfig（当前生效配置）。
 *
 * wifiConfig 是"当前使用的那个网络"的镜像，供 WiFi.begin() / 网页状态显示
 * 等既有代码直接读取；真正的数据源是 wifiList。
 */
void wifiSyncActiveToConfig() {
  if (wifiList.count == 0) {
    memset(wifiConfig.ssid,     0, sizeof(wifiConfig.ssid));
    memset(wifiConfig.password, 0, sizeof(wifiConfig.password));
    wifiConfig.configured = false;
    return;
  }
  if (wifiList.active >= wifiList.count) wifiList.active = 0;

  memset(wifiConfig.ssid,     0, sizeof(wifiConfig.ssid));
  memset(wifiConfig.password, 0, sizeof(wifiConfig.password));
  strncpy(wifiConfig.ssid,     wifiList.nets[wifiList.active].ssid,
          sizeof(wifiConfig.ssid) - 1);
  strncpy(wifiConfig.password, wifiList.nets[wifiList.active].password,
          sizeof(wifiConfig.password) - 1);
  wifiConfig.ssid[sizeof(wifiConfig.ssid) - 1]         = '\0';
  wifiConfig.password[sizeof(wifiConfig.password) - 1] = '\0';
  wifiConfig.configured = true;
}

/**
 * @brief 逐条校验多网络列表：丢弃空白/损坏条目、压缩数组、修正 active 下标。
 *
 * 空白 Flash（全 0xFF）读出的字符数组既无终止符又是非法字节，直接使用会
 * 越界读取（同 WiFiConfig 的老问题），因此必须先校验再使用。
 */
static void wifiListSanitize() {
  uint8_t oldActive = wifiList.active;
  int w = 0;
  int newActive = -1;

  for (int i = 0; i < (int)wifiList.count && i < MAX_WIFI_NETS; i++) {
    bool ok = isSaneField(wifiList.nets[i].ssid,     sizeof(wifiList.nets[i].ssid))
           && isSaneField(wifiList.nets[i].password, sizeof(wifiList.nets[i].password))
           && wifiList.nets[i].ssid[0] != '\0';
    if (!ok) continue;

    if (i == (int)oldActive) newActive = w;
    if (w != i) wifiList.nets[w] = wifiList.nets[i];

    wifiList.nets[w].ssid[sizeof(wifiList.nets[w].ssid) - 1]         = '\0';
    wifiList.nets[w].password[sizeof(wifiList.nets[w].password) - 1] = '\0';
    w++;
  }

  for (int i = w; i < MAX_WIFI_NETS; i++) {
    memset(&wifiList.nets[i], 0, sizeof(WiFiNet));
  }

  wifiList.count  = (uint8_t)w;
  wifiList.active = (newActive >= 0) ? (uint8_t)newActive : 0;
}

/**
 * @brief 从 EEPROM 加载 WiFi 配置、多网络列表和灯珠数量。
 *
 * - 偏移 0：WiFiConfig 结构体（当前生效网络 + 模式开关）
 * - 偏移 NUM_LEDS_EEPROM_ADDR：uint16_t 灯珠数量
 * - 偏移 WIFI_LIST_EEPROM_ADDR：WiFiList 结构体（多网络列表）
 * - 若灯珠数量超出 [1, MAX_LEDS]，则重置为默认值并重新写入
 *
 * 升级兼容：旧版本只有偏移 0 处的单条 WiFiConfig，新区域的 magic 不匹配，
 * 此时会把这条配置自动迁移成列表中的第 1 条（用户的 WiFi 不会丢）。
 */
void loadWiFiConfig() {
  EEPROM.begin(EEPROM_SIZE);
  EEPROM.get(0, wifiConfig);
  EEPROM.get(NUM_LEDS_EEPROM_ADDR, num_leds);
  EEPROM.get(WIFI_LIST_EEPROM_ADDR, wifiList);

  // ============================================================
  // 【核心修复】WiFiConfig 合法性校验与修复
  //
  // 根因：新设备 EEPROM 为空白（全 0xFF）时：
  //   1. ssid[32] / password[64] 全是 0xFF 且没有 '\0' 终止符，
  //      直接当字符串用会越界读取，产生上百字节乱码；
  //   2. configured 读出 0xFF，bool 规范化后为 true，
  //      系统误判"已配置过 WiFi"，用非法凭据调用 WiFi.begin()。
  // 两者叠加就是"新设备保存 WiFi 后重启显示不出来的根本原因"。
  //
  // 注意：不能用 `bool != true/false` 来校验 —— bool 读入 0xFF 后
  // 一定被规范化成 true，永远查不出问题，必须读取原始字节 + 字符校验。
  // ============================================================
  uint8_t rawConfigured = EEPROM.read(offsetof(WiFiConfig, configured));

  bool cfgOk = isWiFiConfigValid(wifiConfig)
            && (rawConfigured == 0 || rawConfigured == 1);

  bool wifiDirty = false;

  // ============================================================
  // 多网络列表：首次上电 / 旧版本升级 / 数据损坏 → 初始化，并把旧的
  // 单条配置迁移为列表中的第 1 条（老用户的 WiFi 不会丢）。
  // ============================================================
  if (wifiList.magic != WIFI_LIST_MAGIC) {
    memset(&wifiList, 0, sizeof(wifiList));
    wifiList.magic  = WIFI_LIST_MAGIC;
    wifiList.count  = 0;
    wifiList.active = 0;

    if (cfgOk) {
      strncpy(wifiList.nets[0].ssid,     wifiConfig.ssid,
              sizeof(wifiList.nets[0].ssid) - 1);
      strncpy(wifiList.nets[0].password, wifiConfig.password,
              sizeof(wifiList.nets[0].password) - 1);
      wifiList.count = 1;

      DEBUG_PRINT("[WiFi] 已把旧版单条配置迁移到多网络列表: ");
      DEBUG_PRINTLN(wifiList.nets[0].ssid);
    } else {
      DEBUG_PRINTLN("[WiFi] 多网络列表首次初始化（无历史配置）");
    }
    wifiDirty = true;
  }

  // ---- 逐条校验（丢空白/损坏条目）+ 修正 active ----
  {
    uint8_t beforeCount = wifiList.count;
    uint8_t beforeActive = wifiList.active;
    wifiListSanitize();
    if (wifiList.count != beforeCount || wifiList.active != beforeActive) {
      wifiDirty = true;
    }
  }

  // ---- 完全没有可用网络 → 回到"未配置"状态（模式开关复位为默认） ----
  if (wifiList.count == 0) {
    if (wifiConfig.staEnabled != true || wifiConfig.apEnabled != true ||
        rawConfigured != 0) {
      wifiDirty = true;
    }
    wifiConfig.staEnabled = true;
    wifiConfig.apEnabled  = true;
    DEBUG_PRINTLN("[WiFi] 无已保存网络，WiFi 处于未配置状态（需在网页配网）");
  }

  // ---- 用 active 条目覆盖 wifiConfig，保证二者始终一致 ----
  wifiSyncActiveToConfig();

  if (wifiDirty) {
    EEPROM.put(0, wifiConfig);
    EEPROM.put(WIFI_LIST_EEPROM_ADDR, wifiList);
    EEPROM.commit();
  }

  // 校验灯珠数量合法性
  if (num_leds < 1 || num_leds > MAX_LEDS) {
    num_leds = DEFAULT_LEDS;
    EEPROM.put(NUM_LEDS_EEPROM_ADDR, num_leds);
    EEPROM.commit();
    // 新设备 EEPROM 为空白（0xFFFF）时会走到这里，使用编译期的默认值。
    // 若该默认值与实际灯珠数不符，启动动画的三段范围会整体错位
    // （在物理灯带上表现为"前两段看不见、最后一段整条亮起"）。
    DEBUG_PRINTLN("[配置] 灯珠数量无效或未配置，已回退为默认值 DEFAULT_LEDS");
  }
  EEPROM.end();

  DEBUG_PRINTLN("加载WiFi配置:");
  DEBUG_PRINT("  SSID: ");      DEBUG_PRINTLN(wifiConfig.ssid[0] ? wifiConfig.ssid : "(空)");
  DEBUG_PRINT("  STA模式: ");   DEBUG_PRINTLN(wifiConfig.staEnabled ? "开启" : "关闭");
  DEBUG_PRINT("  AP模式: ");    DEBUG_PRINTLN(wifiConfig.apEnabled  ? "开启" : "关闭");
  DEBUG_PRINT("  配置状态: ");  DEBUG_PRINTLN(wifiConfig.configured  ? "已配置" : "未配置");
  DEBUG_PRINT("  灯珠数量: ");  DEBUG_PRINTLN(num_leds);
  DEBUG_PRINT("  已保存网络: "); DEBUG_PRINT(wifiList.count);
  DEBUG_PRINT(" 个（数组顺序即连接优先级，第 1 个最先试；当前使用的是第 ");
  DEBUG_PRINT(wifiList.active + 1);
  DEBUG_PRINTLN(" 个）");
  for (int i = 0; i < wifiList.count; i++) {
    DEBUG_PRINT("    ["); DEBUG_PRINT(i); DEBUG_PRINT("] ");
    DEBUG_PRINT(wifiList.nets[i].ssid);
    DEBUG_PRINTLN(i == wifiList.active ? "  <-- 当前" : "");
  }
}

/**
 * @brief 将当前 WiFi 配置 + 多网络列表写入 EEPROM，并回读校验。
 *
 * 两个区域一起写：
 *  - 偏移 0：WiFiConfig（当前生效网络 + 模式开关，兼容旧版本布局）
 *  - 偏移 WIFI_LIST_EEPROM_ADDR：WiFiList（多网络列表）
 *
 * 写入后重新从 Flash 读回逐字节比对，确保数据真正落盘。
 * 两个结构体都无隐式填充字节，因此 memcmp 全量比对是可靠的。
 *
 * @return true 写入且回读校验通过；false 失败
 */
bool saveWiFiConfig() {
  // ---- 防御性规范化 ----
  if (wifiList.magic != WIFI_LIST_MAGIC) wifiList.magic = WIFI_LIST_MAGIC;
  if (wifiList.count > MAX_WIFI_NETS)    wifiList.count = MAX_WIFI_NETS;
  if (wifiList.count == 0)               wifiList.active = 0;
  else if (wifiList.active >= wifiList.count) wifiList.active = 0;

  EEPROM.begin(EEPROM_SIZE);
  EEPROM.put(0, wifiConfig);
  EEPROM.put(WIFI_LIST_EEPROM_ADDR, wifiList);
  bool okCommit = EEPROM.commit();
  EEPROM.end();

  // ---- 回读校验：重新从 Flash 读回，确认真正落盘 ----
  WiFiConfig vCfg;
  WiFiList   vList;
  EEPROM.begin(EEPROM_SIZE);
  EEPROM.get(0, vCfg);
  EEPROM.get(WIFI_LIST_EEPROM_ADDR, vList);
  EEPROM.end();

  bool okVerify = (memcmp(&vCfg, &wifiConfig, sizeof(WiFiConfig)) == 0)
               && (memcmp(&vList, &wifiList, sizeof(WiFiList)) == 0);

  DEBUG_PRINT("[WiFi] 保存到EEPROM: commit=");
  DEBUG_PRINT(okCommit ? "OK" : "FAIL");
  DEBUG_PRINT(" 回读校验=");
  DEBUG_PRINTLN(okVerify ? "OK" : "FAIL");
  DEBUG_PRINT("  当前网络: ");
  DEBUG_PRINT(wifiConfig.ssid[0] ? wifiConfig.ssid : "(空)");
  DEBUG_PRINT("  共 ");
  DEBUG_PRINT(wifiList.count);
  DEBUG_PRINTLN(" 个已保存网络");

  return okCommit && okVerify;
}

/**
 * @brief 仅写入多网络列表区域（用于连接成功后回写 active 项）。
 *
 * 与 saveWiFiConfig() 的区别：不触碰偏移 0 的 WiFiConfig，也不做整片写的
 * 额外开销。仅有一个区域需要落盘时使用。
 */
bool saveWiFiList() {
  if (wifiList.magic != WIFI_LIST_MAGIC) wifiList.magic = WIFI_LIST_MAGIC;

  EEPROM.begin(EEPROM_SIZE);
  EEPROM.put(WIFI_LIST_EEPROM_ADDR, wifiList);
  bool okCommit = EEPROM.commit();
  EEPROM.end();

  WiFiList vList;
  EEPROM.begin(EEPROM_SIZE);
  EEPROM.get(WIFI_LIST_EEPROM_ADDR, vList);
  EEPROM.end();

  return okCommit && (memcmp(&vList, &wifiList, sizeof(WiFiList)) == 0);
}

/**
 * @brief 判断多网络列表里是否已存在同名网络。
 *
 * @return 已存在返回下标（>=0），否则返回 -1
 */
static int wifiListFind(const char* ssid) {
  if (!ssid) return -1;
  for (int i = 0; i < wifiList.count; i++) {
    if (strcmp(wifiList.nets[i].ssid, ssid) == 0) return i;
  }
  return -1;
}

/**
 * @brief 添加（或更新）一条 WiFi 网络。
 *
 * 同名 SSID 视为同一网络：只更新密码，不新增条目。
 *
 * **本函数不改动连接状态**（既不设 active、也不触发重连）：
 * 网页上「保存」只是把凭据存进列表，连不连由用户点列表里的"连接"决定。
 * 唯一的例外是"更新的是当前正在使用的网络"——此时必须把新密码同步进
 * wifiConfig，否则下次重连会用旧密码。
 *
 * @return true 成功；false 参数非法或列表已满
 */
bool wifiListAdd(const char* ssid, const char* password) {
  if (!ssid || ssid[0] == '\0') return false;

  char s[32];
  char p[64];
  memset(s, 0, sizeof(s));
  memset(p, 0, sizeof(p));
  strncpy(s, ssid, sizeof(s) - 1);
  strncpy(p, password ? password : "", sizeof(p) - 1);

  int exist = wifiListFind(s);
  if (exist >= 0) {
    memcpy(wifiList.nets[exist].password, p, sizeof(p));
    // 用户重填了密码 → 之前的失败记录作废，重新观察
    wifiFailCount[exist] = 0;
    // 改的正好是当前在用网络 → 同步镜像，避免后续重连用旧密码
    if (exist == wifiList.active) wifiSyncActiveToConfig();
    return true;
  }

  if (wifiList.count >= MAX_WIFI_NETS) return false;

  int i = wifiList.count;
  memset(&wifiList.nets[i], 0, sizeof(WiFiNet));
  memcpy(wifiList.nets[i].ssid,     s, sizeof(s));
  memcpy(wifiList.nets[i].password, p, sizeof(p));
  wifiFailCount[i] = 0;
  wifiList.count++;

  // 列表原本是空的 → 这条就是唯一选择，直接记为当前（wifiConfig 需要它兜底）
  if (wifiList.count == 1) {
    wifiList.active = 0;
    wifiSyncActiveToConfig();
  }
  return true;
}

/**
 * @brief 修改指定网络的密码。
 *
 * 若修改的是当前生效网络，会同步刷新 wifiConfig（下次连接即用新密码）。
 */
bool wifiListSetPassword(int idx, const char* password) {
  if (idx < 0 || idx >= wifiList.count) return false;

  memset(wifiList.nets[idx].password, 0, sizeof(wifiList.nets[idx].password));
  strncpy(wifiList.nets[idx].password, password ? password : "",
          sizeof(wifiList.nets[idx].password) - 1);

  if (idx == wifiList.active) wifiSyncActiveToConfig();
  return true;
}

/**
 * @brief 删除指定网络。
 *
 * 删除后自动修正 active 下标，并把新的 active 同步到 wifiConfig。
 */
bool wifiListDelete(int idx) {
  if (idx < 0 || idx >= wifiList.count) return false;

  for (int i = idx; i < wifiList.count - 1; i++) {
    wifiList.nets[i] = wifiList.nets[i + 1];
    wifiFailCount[i] = wifiFailCount[i + 1];   // 失败计数跟着条目一起左移
  }
  wifiList.count--;
  memset(&wifiList.nets[wifiList.count], 0, sizeof(WiFiNet));
  wifiFailCount[wifiList.count] = 0;

  if (wifiList.count == 0) {
    wifiList.active = 0;
  } else if (wifiList.active > idx) {
    wifiList.active--;
  } else if (wifiList.active >= wifiList.count) {
    wifiList.active = wifiList.count - 1;
  }

  wifiSyncActiveToConfig();
  return true;
}

/**
 * @brief 把指定网络设为"当前已连接"（仅记录，不触发重连）。
 */
bool wifiListSetActive(int idx) {
  if (idx < 0 || idx >= wifiList.count) return false;
  wifiList.active = (uint8_t)idx;
  wifiSyncActiveToConfig();
  return true;
}

/**
 * @brief 调整某条网络的优先级顺序（数组顺序 = 连接优先级，nets[0] 最高）。
 *
 * 网络列表的顺序决定：开机从谁开始试、断网轮询先试谁。
 *
 * @param idx 要移动的条目下标
 * @param dir -1 = 上移（提高优先级）、+1 = 下移（降低优先级）
 * @return true 成功；false 下标非法或已在首/末位
 *
 * @note active 是"当前连的是谁"，与顺序无关，但要跟着下标一起搬，
 *       否则调顺序后网页上的"当前"徽标会跑偏。
 */
bool wifiListMove(int idx, int dir) {
  if (idx < 0 || idx >= wifiList.count) return false;
  if (dir != -1 && dir != 1) return false;

  int to = idx + dir;
  if (to < 0 || to >= wifiList.count) return false;

  // 用 static 缓冲做交换：本函数运行在 AsyncWebServer 回调里，
  // 回调的栈很紧，不要放 96 字节的 WiFiNet 临时对象。
  static char          tmpSsid[sizeof(wifiList.nets[0].ssid)];
  static char          tmpPw[sizeof(wifiList.nets[0].password)];

  memcpy(tmpSsid, wifiList.nets[idx].ssid,     sizeof(tmpSsid));
  memcpy(tmpPw,   wifiList.nets[idx].password, sizeof(tmpPw));

  memcpy(wifiList.nets[idx].ssid,     wifiList.nets[to].ssid,     sizeof(tmpSsid));
  memcpy(wifiList.nets[idx].password, wifiList.nets[to].password, sizeof(tmpPw));

  memcpy(wifiList.nets[to].ssid,     tmpSsid, sizeof(tmpSsid));
  memcpy(wifiList.nets[to].password, tmpPw,   sizeof(tmpPw));

  // active 跟着条目一起换位置
  if (wifiList.active == idx)      wifiList.active = (uint8_t)to;
  else if (wifiList.active == to)  wifiList.active = (uint8_t)idx;

  // 失败计数也跟着条目走，否则调完顺序灰点会标到别的网络头上
  uint8_t tmpFail = wifiFailCount[idx];
  wifiFailCount[idx] = wifiFailCount[to];
  wifiFailCount[to]  = tmpFail;

  return true;
}

/**
 * @brief 从 EEPROM 加载 MQTT 配置（巴法云 Client ID、订阅主题与总开关）。
 *
 * 合法性处理：
 *  - enabled 字段：空白 Flash（全 0xFF）读出的字节不是 0/1，一律规范化
 *    为 false（默认关闭）。旧设备 EEPROM 中没有该字段，升级后即为"关闭"。
 *  - clientId / topic：非法（0xFF / 控制字符 / 无终止符）则清空为 ""，
 *    默认不再填入任何示例数据，需用户在网页端自行填写。
 *  - 凭据为空时不允许开关保持开启：若 EEPROM 中 enabled 为 true 但
 *    clientId / topic 为空，则自动关闭开关（与保存接口行为一致）。
 *  - 以上任一项被修正时，回写 EEPROM 使数据落盘。
 *
 * 注意：不能用 `(bool)` 直接读取 —— bool 读入 0xFF 后必被规范化成 true，
 * 必须先用 memcpy 取原始字节判断。
 */
void loadMqttConfig() {
  EEPROM.begin(EEPROM_SIZE);
  EEPROM.get(MQTT_CONFIG_EEPROM_ADDR, mqttConfig);
  EEPROM.end();

  bool dirty = false;

  // ---- 1) 开关字段规范化：只有明确读到 0x01 才算开启 ----
  // 空白 Flash（0xFF）或旧版本 EEPROM（无此字段）都视为"关闭"。
  uint8_t rawEnabled = 0;
  memcpy(&rawEnabled, &mqttConfig.enabled, sizeof(rawEnabled));

  bool enabledValid = (rawEnabled == 0x00 || rawEnabled == 0x01);
  mqttConfig.enabled = enabledValid && (rawEnabled == 0x01);
  if (!enabledValid) dirty = true;   // 非法字节 → 已规范化为"关闭"，需落盘

  // ---- 2) 字符串字段校验：非法则清空（默认数据为空） ----
  if (!isSaneField(mqttConfig.clientId, sizeof(mqttConfig.clientId))) {
    memset(mqttConfig.clientId, 0, sizeof(mqttConfig.clientId));
    dirty = true;
  }
  if (!isSaneField(mqttConfig.topic, sizeof(mqttConfig.topic))) {
    memset(mqttConfig.topic, 0, sizeof(mqttConfig.topic));
    dirty = true;
  }

  // ---- 2.5) 清除旧版本遗留的示例占位值（"please in" / "put topic"） ----
  if (strcmp(mqttConfig.clientId, LEGACY_MQTT_CLIENT_ID) == 0) {
    mqttConfig.clientId[0] = '\0';
    dirty = true;
  }
  if (strcmp(mqttConfig.topic, LEGACY_MQTT_TOPIC) == 0) {
    mqttConfig.topic[0] = '\0';
    dirty = true;
  }

  // ---- 2.8) 凭据为空则不允许保持开启：自动关闭开关并落盘 ----
  // 与保存接口一致（保存为空时会自动关开关），保证冷启动后状态自洽，
  // 避免出现"开关显示开启、实际永远连不上"的矛盾状态。
  if (mqttConfig.enabled &&
      (mqttConfig.clientId[0] == '\0' || mqttConfig.topic[0] == '\0')) {
    mqttConfig.enabled = false;
    dirty = true;
  }

  // ---- 3) 合法数据也强制补齐终止符，防止越界读取 ----
  mqttConfig.clientId[sizeof(mqttConfig.clientId) - 1] = '\0';
  mqttConfig.topic[sizeof(mqttConfig.topic) - 1]       = '\0';

  // ---- 4) 有修正则回写 EEPROM ----
  if (dirty) {
    EEPROM.begin(EEPROM_SIZE);
    EEPROM.put(MQTT_CONFIG_EEPROM_ADDR, mqttConfig);
    EEPROM.commit();
    EEPROM.end();
    DEBUG_PRINTLN("MQTT配置已规范化并写回 EEPROM");
  }

  DEBUG_PRINT("  MQTT 开关: ");
  DEBUG_PRINTLN(mqttConfig.enabled ? "开启" : "关闭");
  DEBUG_PRINT("  Client ID: ");
  DEBUG_PRINTLN(mqttConfig.clientId[0] ? mqttConfig.clientId : "(空)");
  DEBUG_PRINT("  Topic: ");
  DEBUG_PRINTLN(mqttConfig.topic[0] ? mqttConfig.topic : "(空)");
}

/**
 * @brief 将当前 MQTT 配置写入 EEPROM，并回读校验。
 */
bool saveMqttConfig() {
  // 强制补齐终止符，防止超长/脏数据导致越界读取
  mqttConfig.clientId[sizeof(mqttConfig.clientId) - 1] = '\0';
  mqttConfig.topic[sizeof(mqttConfig.topic) - 1]       = '\0';

  EEPROM.begin(EEPROM_SIZE);
  EEPROM.put(MQTT_CONFIG_EEPROM_ADDR, mqttConfig);
  bool okCommit = EEPROM.commit();
  EEPROM.end();

  MqttConfig verify;
  EEPROM.begin(EEPROM_SIZE);
  EEPROM.get(MQTT_CONFIG_EEPROM_ADDR, verify);
  EEPROM.end();
  bool okVerify = (memcmp(&verify, &mqttConfig, sizeof(MqttConfig)) == 0);

  DEBUG_PRINT("[MQTT] 保存到EEPROM: commit=");
  DEBUG_PRINT(okCommit ? "OK" : "FAIL");
  DEBUG_PRINT(" 回读校验=");
  DEBUG_PRINTLN(okVerify ? "OK" : "FAIL");
  DEBUG_PRINT("  Client ID: ");
  DEBUG_PRINTLN(mqttConfig.clientId);
  DEBUG_PRINT("  Topic: ");
  DEBUG_PRINTLN(mqttConfig.topic);

  return okCommit && okVerify;
}

/**
 * @brief 从 EEPROM 加载 MQTT 服务器地址/端口。
 *
 * 该区域以 magic 标记是否初始化过：空白 Flash（全 0xFF）或旧版本升级上来时
 * magic 不匹配，此时写入默认值（巴法云 bemfa.com:9501）并落盘。
 * 默认值就是原来的硬编码地址，所以升级后行为与以前完全一致。
 */
void loadMqttServerConfig() {
  EEPROM.begin(EEPROM_SIZE);
  EEPROM.get(MQTT_SERVER_EEPROM_ADDR, mqttServerConfig);
  EEPROM.end();

  bool sane = (mqttServerConfig.magic == MQTT_SERVER_MAGIC)
              && (mqttServerConfig.server[0] != '\0')
              && (mqttServerConfig.port != 0);

  if (!sane) {
    mqttServerConfig.magic = MQTT_SERVER_MAGIC;
    strncpy(mqttServerConfig.server, DEFAULT_MQTT_SERVER, sizeof(mqttServerConfig.server) - 1);
    mqttServerConfig.server[sizeof(mqttServerConfig.server) - 1] = '\0';
    mqttServerConfig.port     = DEFAULT_MQTT_PORT;
    mqttServerConfig.reserved = 0;

    EEPROM.begin(EEPROM_SIZE);
    EEPROM.put(MQTT_SERVER_EEPROM_ADDR, mqttServerConfig);
    EEPROM.commit();
    EEPROM.end();
    DEBUG_PRINTLN("[MQTT] 服务器配置首次初始化（默认巴法云）");
  }

  DEBUG_PRINT("  MQTT 服务器: ");
  DEBUG_PRINT(mqttServerConfig.server);
  DEBUG_PRINT(":");
  DEBUG_PRINTLN(mqttServerConfig.port);
}

/**
 * @brief 将 MQTT 服务器地址/端口写入 EEPROM 并回读校验。
 */
bool saveMqttServerConfig() {
  mqttServerConfig.server[sizeof(mqttServerConfig.server) - 1] = '\0';
  mqttServerConfig.magic    = MQTT_SERVER_MAGIC;
  mqttServerConfig.reserved = 0;

  EEPROM.begin(EEPROM_SIZE);
  EEPROM.put(MQTT_SERVER_EEPROM_ADDR, mqttServerConfig);
  bool okCommit = EEPROM.commit();
  EEPROM.end();

  MqttServerConfig verify;
  EEPROM.begin(EEPROM_SIZE);
  EEPROM.get(MQTT_SERVER_EEPROM_ADDR, verify);
  EEPROM.end();
  bool okVerify = (memcmp(&verify, &mqttServerConfig, sizeof(MqttServerConfig)) == 0);

  DEBUG_PRINT("[MQTT] 服务器配置保存: commit=");
  DEBUG_PRINT(okCommit ? "OK" : "FAIL");
  DEBUG_PRINT(" 回读校验=");
  DEBUG_PRINTLN(okVerify ? "OK" : "FAIL");
  return okCommit && okVerify;
}

// ============================================================
// WiFi 连接
// ============================================================// ============================================================
// "上电后强制开启 AP"窗口（见 config.h 的 AP_FORCE_BOOT_MS）
// ============================================================

/** 窗口截止时刻（毫秒，基于 millis()；0 = 尚未开始计时） */
static unsigned long apForceUntil = 0;

/**
 * @brief 开始计时：setup() 开头调用一次，以"上电时刻"为基准。
 *
 * 必须尽早调用（loadWiFiConfig / connectToWiFi 之前），
 * 否则启动动画等耗时会让 5 分钟窗口白白少掉一截。
 */
void apForceWindowStart() {
  apForceUntil = millis() + AP_FORCE_BOOT_MS;
  DEBUG_PRINT("[WiFi] 上电强制开热点窗口: ");
  DEBUG_PRINT(AP_FORCE_BOOT_MS / 1000);
  DEBUG_PRINTLN(" 秒");
}

/** @brief 当前是否还在强制开 AP 的窗口内。 */
bool apForceWindowActive() {
  if (apForceUntil == 0) return false;
  // 用有符号差值比较，避免 millis() 回绕（约 49.7 天）时判断错误
  return (long)(millis() - apForceUntil) < 0;
}

/** @brief 强制开 AP 窗口剩余毫秒（0 = 已结束或未开始）。 */
unsigned long apForceWindowRemainMs() {
  if (apForceUntil == 0) return 0;
  long rem = (long)(apForceUntil - millis());
  return (rem > 0) ? (unsigned long)rem : 0UL;
}

/**
 * @brief 根据 wifiConfig 设置 WiFi 模式并尝试连接。
 *
 * 连接过程中逐颗点亮 [wifiEndLed, wifiStartLed] 范围内的 LED
 * 作为进度指示（颜色由调用方决定）。
 *
 * @param wifiStartLed 进度指示起始 LED 索引（从此向下推进）
 * @param wifiEndLed   进度指示结束 LED 索引（最小到此为止）
 * @return 连接结束后 wifiStartLed 的最新值
 */
int connectToWiFi(int wifiStartLed, int wifiEndLed) {
  // 保证至少开启一种模式
  if (!wifiConfig.staEnabled && !wifiConfig.apEnabled) {
    wifiConfig.apEnabled = true;
  }

  // 上电后前 AP_FORCE_BOOT_MS 内，热点一律开启（不管「AP 模式」开关怎么勾）：
  // 这时 STA 多半还没连上，必须保证 192.168.4.1 一定进得去。
  bool apForce = apForceWindowActive();
  bool apWant  = wifiConfig.apEnabled || apForce;

  // 设置 WiFi 工作模式
  if (wifiConfig.staEnabled && apWant) {
    WiFi.mode(WIFI_AP_STA);
    DEBUG_PRINTLN(apForce && !wifiConfig.apEnabled ? "模式: STA+AP（上电强制开热点）"
                                                   : "模式: STA+AP");
  } else if (wifiConfig.staEnabled) {
    WiFi.mode(WIFI_STA);
    DEBUG_PRINTLN("模式: STA");
  } else {
    WiFi.mode(WIFI_AP);
    DEBUG_PRINTLN("模式: AP");
  }

  // STA 模式：按列表顺序（=优先级）逐个尝试已保存的网络。
  //
  // **开机一律从 nets[0]（第一个网络）开始**，不再从上次连上的那个开始 ——
  // 顺序由用户在网页上通过上移/下移调整，所见即所得。
  //   - 第 1 个候选：最多 20 次 × 500ms = 10s（给慢速路由器留足 DHCP 时间）
  //   - 后续候选  ：最多 10 次 × 500ms = 5s（避免开机等待过久）
  // 注意：ssid 必须非空，避免用垃圾/空凭据调用 WiFi.begin()。
  bool staConnected = false;
  if (wifiConfig.staEnabled && wifiConfig.configured && wifiConfig.ssid[0] != '\0') {
    int total = (wifiList.count > 0) ? wifiList.count : 1;

    for (int k = 0; k < total && !staConnected; k++) {
      int idx = (wifiList.count > 0) ? k : 0;   // 从第一个网络开始，按顺序往下试

      const char* ssid = (wifiList.count > 0) ? wifiList.nets[idx].ssid
                                             : wifiConfig.ssid;
      const char* pw   = (wifiList.count > 0) ? wifiList.nets[idx].password
                                             : wifiConfig.password;
      if (ssid[0] == '\0') continue;

      DEBUG_PRINT("尝试连接WiFi");
      if (total > 1) {
        DEBUG_PRINT(" (");
        DEBUG_PRINT(k + 1);
        DEBUG_PRINT("/");
        DEBUG_PRINT(total);
        DEBUG_PRINT(")");
      }
      DEBUG_PRINT(": ");
      DEBUG_PRINTLN(ssid);

      WiFi.disconnect();          // 换网络前先断开上一次的尝试
      delay(50);
      WiFi.begin(ssid, pw);

      int maxAttempts = (k == 0) ? 20 : 10;
      int attempts = 0;
      while (WiFi.status() != WL_CONNECTED && attempts < maxAttempts) {
        delay(500);
        if (wifiStartLed >= wifiEndLed) {
          leds[wifiStartLed] = CRGB::Green;
          fanxiang(!reverseDisplay, leds, num_leds);
          wifiStartLed--;
        }
        DEBUG_PRINT(".");
        attempts++;
      }

      if (WiFi.status() == WL_CONNECTED) {
        staConnected = true;
        DEBUG_PRINTLN("\n连接成功");
        DEBUG_PRINT("IP: ");
        DEBUG_PRINTLN(WiFi.localIP());

        // 记录"当前用的是谁"，供网页显示"当前"徽标（不改变顺序）
        if (wifiList.count > 0 && idx != wifiList.active) {
          wifiList.active = (uint8_t)idx;
          saveWiFiList();
        }
        wifiSyncActiveToConfig();
        wifiRetryIdx = -1;
      } else {
        DEBUG_PRINTLN("\n连接失败!");
      }
    }
  }

  // AP 模式：开启热点（用户勾选了「AP 模式」，或还在上电强制窗口内）
  if (apWant) {
    DEBUG_PRINTLN(apForce && !wifiConfig.apEnabled
                    ? "开启AP模式（上电强制窗口内，与开关无关）"
                    : "开启AP模式");
    WiFi.softAP("LAND_LED", "12345678");
    DEBUG_PRINT("AP IP: ");
    DEBUG_PRINTLN(WiFi.softAPIP());
    // 不是用户勾选开起来的热点 → 记为"自动/强制开启"，
    // 窗口结束或 STA 稳定连通后由 manageWiFiConnection() 收掉。
    apTempEnabled = !wifiConfig.apEnabled;
  } else if (!staConnected) {
    // STA 失败且未配置 AP 时强制开启 AP 兜底
    DEBUG_PRINTLN("STA连接失败且未开启AP模式，强制开启临时AP热点");

    if (wifiConfig.configured) {
      // 有保存过WiFi凭据但本次连接失败：保持 STA+AP 双模式，
      // 让 ESP8266 SDK 在后台持续尝试重连 STA，
      // 同时 AP 作为热点供临时访问和重新配网
      WiFi.mode(WIFI_AP_STA);
    } else {
      // 从未配置过WiFi：没有凭据可重连，AP 单模式即可
      WiFi.mode(WIFI_AP);
    }

    WiFi.softAP("LAND_LED", "12345678");
    DEBUG_PRINT("临时AP IP: ");
    DEBUG_PRINTLN(WiFi.softAPIP());
    apTempEnabled = true;
  }

  return wifiStartLed;
}

// ============================================================
// MQTT
// ============================================================

/**
 * @brief 检测是否有互联网连接（最多等待 1500ms）。
 *
 * 超时设为 1500ms，给 WiFi 重连后的 ARP/路由初始化留足时间，
 * 避免 STA 刚重连时 TCP 尚未稳定导致误判为无网络。
 *
 * @return true 若连接成功，false 若超时
 */
bool checkInternet() {
  WiFiClient testClient;
  testClient.setTimeout(1500);
  bool connected = testClient.connect("180.101.50.188", 80);
  if (connected) testClient.stop();
  return connected;
}

/**
 * @brief 非阻塞 MQTT 重连。
 *
 * 若巴法云总开关关闭（mqttConfig.enabled == false），直接返回，
 * 不进行任何 DNS 解析 / TCP 连接 / 订阅操作 —— 即"不执行巴法云功能"。
 *
 * 先检测互联网可达性，再用 WiFiClient 直连巴法云服务器。
 * 连接前打印 DNS 解析结果，方便诊断 rc=-2 问题。
 * 若连接失败，打印错误码后直接返回，不阻塞主循环。
 */
void reconnect() {
  if (!mqttConfig.enabled) return;   ///< 开关关闭 → 不执行任何云功能
  if (client.connected()) return;

  // 缺少 Client ID 或订阅主题时无需连接（避免用空凭据连接被拒）
  if (mqttConfig.clientId[0] == '\0' || mqttConfig.topic[0] == '\0') {
    DEBUG_PRINTLN("[MQTT] 开关已开启但 Client ID / 主题为空，跳过连接");
    return;
  }

  // 确保服务器地址与回调已配置（开关在运行时被打开时，setup() 可能跳过了初始化）
  // 地址/端口取自 mqttServerConfig（网页可改，默认巴法云 bemfa.com:9501）
  espClient.setTimeout(8000);
  client.setServer(mqttServerConfig.server, mqttServerConfig.port);
  client.setCallback(callback);

  if (!checkInternet()) {
    DEBUG_PRINTLN("[MQTT] 互联网不可达，跳过重连");
    return;
  }

  // 诊断：DNS 解析服务器域名
  IPAddress bemfaIP;
  if (WiFi.hostByName(mqttServerConfig.server, bemfaIP)) {
    DEBUG_PRINT("[MQTT] DNS 解析成功: ");
    DEBUG_PRINT(mqttServerConfig.server);
    DEBUG_PRINT(" -> ");
    DEBUG_PRINTLN(bemfaIP);
  } else {
    DEBUG_PRINT("[MQTT] DNS 解析失败: ");
    DEBUG_PRINTLN(mqttServerConfig.server);
  }

  // 连接前确保 socket 已清理
  espClient.stop();

  DEBUG_PRINT("[MQTT] 正在连接 ");
  DEBUG_PRINT(mqttServerConfig.server);
  DEBUG_PRINT(":");
  DEBUG_PRINT(mqttServerConfig.port);
  DEBUG_PRINT(" (Client ID: ");
  DEBUG_PRINT(mqttConfig.clientId);
  DEBUG_PRINT(")...");

  if (client.connect(mqttConfig.clientId)) {
    DEBUG_PRINTLN(" 已连接!");
    client.subscribe(mqttConfig.topic);
    DEBUG_PRINT("[MQTT] 已订阅主题: ");
    DEBUG_PRINTLN(mqttConfig.topic);
    notifyStateChanged();   // 立刻把"已连接"推给网页，不用等下一次 2 秒轮询
  } else {
    DEBUG_PRINT(" 失败, rc=");
    DEBUG_PRINT(client.state());
    switch (client.state()) {
      case -4: DEBUG_PRINTLN(" (连接超时)"); break;
      case -3: DEBUG_PRINTLN(" (连接丢失)"); break;
      case -2: DEBUG_PRINTLN(" (TCP连接失败，请检查网络/防火墙/服务器地址)"); break;
      case  1: DEBUG_PRINTLN(" (协议版本不匹配)"); break;
      case  2: DEBUG_PRINTLN(" (Client ID 被拒绝)"); break;
      case  3: DEBUG_PRINTLN(" (服务器不可用)"); break;
      case  4: DEBUG_PRINTLN(" (用户名/密码错误)"); break;
      case  5: DEBUG_PRINTLN(" (未授权)"); break;
      default: DEBUG_PRINTLN(" (未知错误)"); break;
    }
  }
}

/**
 * @brief MQTT 消息接收回调。
 *
 * 支持以下消息格式：
 *  - "on"  → 切换到单色模式（solidColor）
 *  - "off" → 关闭灯带
 *  - 包含 "get" → 调用 getsend() 上报状态
 *  - 其他 → 调用 extractStringParts() 解析 "模式#亮度#颜色#速度" 格式
 *
 * @param topic   消息来源主题
 * @param payload 消息字节数组
 * @param length  消息长度
 */
void callback(char* topic, byte* payload, unsigned int length) {
  DEBUG_PRINT("Message arrived [");
  DEBUG_PRINT(topic);
  DEBUG_PRINT("] ");

  String Mqtt_Buff;
  Mqtt_Buff.reserve(length);        // 预分配，避免多次 realloc
  for (unsigned int i = 0; i < length; i++) {
    Mqtt_Buff += (char)payload[i];
  }
  DEBUG_PRINTLN(Mqtt_Buff);

  // 记录"上一次接收内容"，供网页显示（超长直接截断，避免溢出固定缓冲）
  strncpy(mqttLastMsg, Mqtt_Buff.c_str(), MQTT_LAST_MSG_LEN - 1);
  mqttLastMsg[MQTT_LAST_MSG_LEN - 1] = '\0';
  mqttMsgCount++;
  mqttLastMsgAt = millis();   // 收到消息的时刻，网页据此显示"上一次接收"的时间

  if (Mqtt_Buff == "on") {
    if (currentMode != "solid") {
      currentMode = "solid";
      modelbianhua = true;
    }
  } else if (Mqtt_Buff == "off") {
    if (currentMode != "off") {
      currentMode = "off";
      modelbianhua = true;
    }
  } else if (Mqtt_Buff.indexOf("get") != -1) {
    getsend(Mqtt_Buff);
  } else {
    extractStringParts(Mqtt_Buff);
  }

  // MQTT 改变状态后，通知网页刷新
  notifyStateChanged();
}

/**
 * @brief 处理包含 "get" 的 MQTT 消息，将对应状态值发布回主题。
 *
 * 支持查询：mode / light（亮度百分比）/ speed / R / G / B / reverse
 *
 * @param str 完整的 MQTT 消息字符串，如 "get mode"
 *
 * @note Bug修复：原代码使用 `if (msg != NULL)` 对 String 对象判断，
 *       始终为 true（String 不可为 NULL）。已改为 `if (!msg.isEmpty())`。
 */
void getsend(String str) {
  String msg = "";
  if (str == "get mode") {
    msg = currentMode;
  } else if (str == "get light") {
    msg = map(brightness, 1, 255, 0, 100);
  } else if (str == "get speed") {
    msg = effectSpeed;
  } else if (str == "get R") {
    msg = solidColor.r;
  } else if (str == "get G") {
    msg = solidColor.g;
  } else if (str == "get B") {
    msg = solidColor.b;
  } else if (str == "get reverse") {
    msg = reverseDisplay ? "1" : "0";
  }

  // Bug修复：对 String 对象应使用 isEmpty() 而非 != NULL
  if (!msg.isEmpty()) {
    client.publish(mqttConfig.topic, msg.c_str());
  }
}

/**
 * @brief 解析 "模式#亮度[#颜色[#速度]]" 格式的 MQTT 命令字符串。
 *
 * 格式说明：
 *  - `letters` : 模式名称（字母，如 "rainbow"、"solid"）
 *  - `num1`    : 亮度百分比（0-100）
 *  - `num2`    : 颜色十进制值（可选，通过 decimalToCRGB 转换）
 *  - `num3`    : 速度（可选，1-100）
 *
 * @param str 完整命令字符串
 */
void extractStringParts(const String& str) {
  String letters = "";
  int num1 = 0, num2 = 0, num3 = 0, numCount = 0;

  // 查找第一个 '#'
  int firstPos = str.indexOf('#');
  if (firstPos == -1) {
    DEBUG_PRINTLN("Error: No '#' found");
    currentMode = str;
    return;
  }

  // 提取模式名称
  letters = str.substring(0, firstPos);
  if (letters == "on") letters = "solid";

  if (letters.length() == 0) {
    DEBUG_PRINTLN("Error: No letters before first '#'");
    return;
  }
  // 验证模式名为纯字母（防止格式错误）
  for (unsigned int i = 0; i < letters.length(); i++) {
    if (!isAlpha(letters[i])) {
      DEBUG_PRINTLN("Error: 模式名中含非字母字符");
      return;
    }
  }
  currentMode = letters;

  // 提取第一个数字（亮度）
  int secondPos = str.indexOf('#', firstPos + 1);
  String num1Str = (secondPos == -1)
    ? str.substring(firstPos + 1)
    : str.substring(firstPos + 1, secondPos);

  if (num1Str.length() == 0) { DEBUG_PRINTLN("Error: Missing first number"); return; }
  num1 = num1Str.toInt();
  numCount = 1;
  modelbianhua = true;

  // 提取第二个数字（颜色，可选）
  if (secondPos != -1) {
    int thirdPos = str.indexOf('#', secondPos + 1);
    String num2Str = (thirdPos == -1)
      ? str.substring(secondPos + 1)
      : str.substring(secondPos + 1, thirdPos);

    if (num2Str.length() == 0) { DEBUG_PRINTLN("Error: Missing second number"); return; }
    num2 = num2Str.toInt();
    numCount = 2;

    // 提取第三个数字（速度，可选）
    if (thirdPos != -1) {
      String num3Str = str.substring(thirdPos + 1);
      if (num3Str.indexOf('#') != -1) {
        DEBUG_PRINTLN("Error: More than three '#' found");
        return;
      }
      if (num3Str.length() == 0) { DEBUG_PRINTLN("Error: Missing third number"); return; }
      num3 = num3Str.toInt();
      numCount = 3;
    }
  }

  // 应用解析结果
  int a = map(num1, 0, 100, 1, 255);
  FastLED.setBrightness(a);
  brightness = a;
  currentMode = letters;

  if (numCount >= 2) {
    applySolidColor(decimalToCRGB(num2), true);
  } else {
    // 只发了亮度（如 on#23），没有颜色参数：
    // 确保当前 solidColor 生效，防止灯带处于关闭/其他状态时不亮
    fill_solid_new(leds, num_leds, solidColor);
  }
  if (numCount >= 3) {
    effectSpeed = num3;
  }

  // 立即刷新LED显示，确保亮度/颜色变化立即生效
  fanxiang(reverseDisplay, leds, num_leds);
}

// ============================================================
// SSE（Server-Sent Events）状态推送
// ============================================================

/** SSE 事件源对象：管理所有客户端长连接 */
AsyncEventSource events("/events");

/**
 * @brief 通知所有 SSE 客户端状态已发生变化。
 *
 * 自增 stateVersion，并向已连接的网页客户端推送 "update" 事件。
 * 网页收到事件后仅调用一次 fetchCurrentState()，实现精准按需刷新。
 *
 * 调用时机：
 *  - /control 接口处理完任何参数后
 *  - MQTT callback 改变状态后
 *  - 红外遥控 hongwai() 改变状态后（在 loop() 末尾检测）
 */
void notifyStateChanged() {
  stateVersion++;
  // 向所有已连接的 SSE 客户端发送 "update" 事件，携带版本号
  events.send(String(stateVersion).c_str(), "update", millis());
}

/**
 * 最近一次 WiFi 扫描活动的时刻。
 *
 * 扫描时射频要离开当前信道，STA 链路会短暂抖动，
 * manageWiFiConnection() 若正好在这段时间轮询，会把"抖动"误判成"断线"，
 * 进而重新初始化 softAP（会踢掉正在配置的客户端）并开启临时 AP。
 * 因此扫描前后用一个时间窗抑制该判断。
 */
static unsigned long lastScanActivityMs = 0;

/**
 * @brief 把字符串转义成 JSON 字符串的"内容部分"（不含首尾引号）。
 *
 * 扫描到的 SSID 里可能出现引号、反斜杠或控制字符（部分路由 / 手机热点会有），
 * 直接拼接会破坏 JSON 结构导致网页解析失败。
 * 中文等多字节 UTF-8 原样透传即可，网页按 UTF-8 解析能正确显示。
 */
static String jsonEscape(const String& in) {
  String out;
  out.reserve(in.length() + 8);
  for (size_t i = 0; i < in.length(); i++) {
    uint8_t c = (uint8_t)in[i];
    switch (c) {
      case '"':  out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n";  break;
      case '\r': out += "\\r";  break;
      case '\t': out += "\\t";  break;
      default:
        if (c < 0x20) {
          char buf[7];
          snprintf(buf, sizeof(buf), "\\u%04x", c);
          out += buf;
        } else {
          out += (char)c;
        }
    }
  }
  return out;
}

// ============================================================
// Web 服务器路由
// ============================================================

/**
 * @brief 注册所有 AsyncWebServer 路由。
 *
 * 路由列表：
 *  - GET /           → 返回 HTML 控制界面（PROGMEM）
 *  - GET /control    → 处理灯效控制参数（mode/color/brightness/speed/timer/ledcount/reverse/saveconfig/restart）
 *  - GET /scanwifi   → 启动异步扫描附近 WiFi（立即返回，结果由 /wifilist 取）
 *  - GET /wifilist   → 返回扫描结果 JSON（去重 + 按信号强度降序）
 *  - GET /state      → 返回当前系统状态 JSON
 *  - GET /events     → SSE 长连接，状态变化时推送 "update" 事件
 *  - GET /update     → 接收手机电量并切换到电量显示模式
 *  - GET /music      → 接收音量值并切换到音乐可视化模式
 */
// ---------------------------------------------------------------------------
// 网页图标（favicon）
//
// 用内联 SVG：不占额外烧录的图片资源，浏览器标签页/收藏夹也能正常显示。
// 与网页 <head> 里那段 data:image/svg+xml 是同一个图标（深色圆角底 + 发光灯泡）。
// /favicon.ico 路由是给不认 SVG 图标的老浏览器兜底的。
// ---------------------------------------------------------------------------
static const char FAVICON_SVG[] PROGMEM =
  "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 32 32'><defs><linearGradient id='g' x1="
  "'.1' y1='0' x2='.7' y2='1'><stop offset='0' stop-color='#FFE600'/><stop offset='.62' stop-co"
  "lor='#FF9A00'/><stop offset='1' stop-color='#FF4D8D'/></linearGradient></defs><rect width='3"
  "2' height='32' rx='7.5' fill='#1B1B22'/><circle cx='16' cy='12.6' r='8.6' fill='#FFE600' opa"
  "city='.13'/><circle cx='16' cy='12.6' r='6.4' fill='url(#g)'/><rect x='12.6' y='18.4' width="
  "'6.8' height='2.4' rx='1.2' fill='#FFC400'/><rect x='13.7' y='22.2' width='4.6' height='2.6'"
  " rx='1.3' fill='#FF9A00'/></svg>";

void setupWebServer() {
  // ---- GET / ----
  server.on("/", HTTP_GET, [](AsyncWebServerRequest* request) {
    request->send_P(200, "text/html; charset=utf-8", index_html);
    DEBUG_PRINTLN("已发送网页界面");
  });

  // ---- GET /events（SSE 长连接推送）----
  events.onConnect([](AsyncEventSourceClient* client) {
    // 客户端首次连接时，立即推送当前版本号，触发一次状态同步
    client->send(String(stateVersion).c_str(), "update", millis(), 1000);
    DEBUG_PRINTLN("[SSE] 客户端已连接");
  });
  server.addHandler(&events);

  // ---- GET /update（电量接口）----
  server.on("/update", HTTP_GET, [](AsyncWebServerRequest* request) {
    if (request->hasParam("power")) {
      lastBatteryUpdate = millis();
      batteryLevel = request->getParam("power")->value().toInt();

      DEBUG_PRINT("收到手机电量: ");
      DEBUG_PRINT(batteryLevel);
      DEBUG_PRINTLN("%");

      if (!inBatteryMode) {
        previousMode  = currentMode;
        currentMode   = "battery";
        inBatteryMode = true;
        resetEffectState();
        DEBUG_PRINTLN("切换到电量模式");
      }
      request->send(200, "text/plain", "Battery level updated: " + String(batteryLevel) + "%");
    } else {
      request->send(400, "text/plain", "Missing power parameter");
    }
  });

  // ---- GET /control（主控制接口）----
  server.on("/control", HTTP_GET, [](AsyncWebServerRequest* request) {

    // 处理模式切换
    if (request->hasParam("mode")) {
      String newMode = request->getParam("mode")->value();
      DEBUG_PRINTLN("模式设置为: " + newMode);

      if (newMode == "battery") {
        // 手动切换电量模式
        if (!inBatteryMode) {
          previousMode  = currentMode;
          inBatteryMode = true;
        }
        currentMode = newMode;
        showBatteryLevel();
      } else {
        // 只有模式真正切换时才重置灯效状态、触发单色渐入。
        // 重复下发同一个 mode（轮询式控制 / 重复点已选中的模式按钮）
        // 不应让渐亮动画重新从黑开始。
        bool modeChanged = (currentMode != newMode);
        inBatteryMode = false;
        if (modeChanged) {
          resetEffectState();
          currentMode = newMode;
          modelbianhua = true;
          if (newMode == "disco") {
            fill_solid_new(leds, num_leds, CRGB::Black);
          }
        }
      }
    }

    // 处理单控模式（mode=single + range + color）
    if (request->hasParam("mode")
        && request->getParam("mode")->value() == "single"
        && request->hasParam("range")
        && request->hasParam("color")) {

      String rangeStr = request->getParam("range")->value();
      String colorStr = request->getParam("color")->value();

      // 解析范围 "start-end" 或 "index"
      int dashIndex = rangeStr.indexOf('-');
      int start, end;
      if (dashIndex != -1) {
        start = rangeStr.substring(0, dashIndex).toInt() - 1;
        end   = rangeStr.substring(dashIndex + 1).toInt() - 1;
      } else {
        start = rangeStr.toInt() - 1;
        end   = start;
      }

      // 解析十六进制颜色字符串
      CRGB targetColor = hexToCRGB(colorStr);

      // 设置指定范围 LED 颜色，其余保持原样
      for (int i = 0; i < num_leds; i++) {
        if (i >= start && i <= end) {
          leds[i] = targetColor;
        }
      }
      currentMode = "single";
      fanxiang(reverseDisplay, leds, num_leds);
      DEBUG_PRINTF("单控模式: 设置灯珠 %d-%d 为指定颜色\n", start + 1, end + 1);
    }

    // 处理颜色参数
    if (request->hasParam("color")) {
      String colorStr = request->getParam("color")->value();
      DEBUG_PRINTLN("颜色设置为: " + colorStr);

      if (colorStr.length() > 0) {
        CRGB newColor = hexToCRGB(colorStr);

        if (currentMode == "solid") {
          // 颜色相近时跳过动画（RGB分量差之和 < 150），直接变色
          int rDiff = abs((int)newColor.r - (int)solidColor.r);
          int gDiff = abs((int)newColor.g - (int)solidColor.g);
          int bDiff = abs((int)newColor.b - (int)solidColor.b);
          applySolidColor(newColor, (rDiff + gDiff + bDiff) >= 150);
        } else {
          // 其他模式下只更新颜色值，不触发动画
          solidColor = newColor;
        }
      }
    }

    // 处理亮度参数（0-100 → 1-255）
    if (request->hasParam("brightness")) {
      int brightnessValue = request->getParam("brightness")->value().toInt();
      brightness = map(brightnessValue, 0, 100, 1, 255);
      FastLED.setBrightness(brightness);
      DEBUG_PRINT("亮度设置为: ");
      DEBUG_PRINTLN(brightness);
      fanxiang(reverseDisplay, leds, num_leds);
    }

    // 处理速度参数（1-100）
    if (request->hasParam("speed")) {
      effectSpeed = request->getParam("speed")->value().toInt();
      DEBUG_PRINT("速度设置为: ");
      DEBUG_PRINTLN(effectSpeed);
    }

    // 处理定时关闭参数（秒）
    if (request->hasParam("timer")) {
      int totalSeconds = request->getParam("timer")->value().toInt();
      if (totalSeconds > 0) {
        timerEndTime = millis() + totalSeconds * 1000UL;
        timerActive  = true;
        DEBUG_PRINT("定时关闭设置为: ");
        DEBUG_PRINT(totalSeconds);
        DEBUG_PRINTLN(" 秒");
      } else {
        timerActive = false;
        DEBUG_PRINTLN("定时关闭已取消");
      }
    }

    // 处理灯珠数量参数（1-200），保存到 EEPROM 并重新初始化灯带
    if (request->hasParam("ledcount")) {
      uint16_t newCount = request->getParam("ledcount")->value().toInt();
      if (newCount >= 1 && newCount <= MAX_LEDS) {
        num_leds = newCount;

        EEPROM.begin(EEPROM_SIZE);
        EEPROM.put(NUM_LEDS_EEPROM_ADDR, num_leds);
        EEPROM.commit();
        EEPROM.end();

        // 重新注册控制器以应用新的灯珠数量。
        // 说明：不能用 FastLED[0].setLeds() 就地改长度 —— ESP8266 的灯带驱动
        // 按【注册时】的长度准备输出，运行期改长度不会生效，必须重新 addLeds。
        FastLED.addLeds<WS2812, LED_PIN, GRB>(leds, num_leds);
        FastLED.setBrightness(brightness);
        // 清屏走项目自带的 fill_solid_new()：内部 fanxiang() 会连续 show 两次，
        // 单次 FastLED.show() 在这块板子上是不显示的（见 fanxiang 注释）。
        fill_solid_new(leds, num_leds, CRGB::Black);
        resetEffectState();

        DEBUG_PRINT("灯珠数量设置为: ");
        DEBUG_PRINTLN(num_leds);
        request->send(200, "text/plain", "灯珠数量已更新为: " + String(num_leds));
      } else {
        request->send(400, "text/plain", "无效的灯珠数量");
      }
      return;  // ledcount 处理完即返回，避免继续发送 "OK"
    }

    // 处理反向显示参数（0/1），保存到 EEPROM
    if (request->hasParam("reverse")) {
      reverseDisplay = request->getParam("reverse")->value().toInt();

      EEPROM.begin(EEPROM_SIZE);
      EEPROM.write(REVERSE_EEPROM_ADDR, reverseDisplay);
      EEPROM.commit();
      EEPROM.end();

      DEBUG_PRINT("反向显示设置已保存: ");
      DEBUG_PRINTLN(reverseDisplay ? "开启" : "关闭");
      fanxiang(reverseDisplay, leds, num_leds);
    }

    // 处理单色动画模式
    if (request->hasParam("solidanim")) {
      String animMode = request->getParam("solidanim")->value();
      if (animMode == "random" || animMode == "sequential" || animMode == "center"
          || animMode == "ends" || animMode == "fade" || animMode == "none") {
        solidAnimMode = animMode;
        DEBUG_PRINT("单色动画模式设置为: ");
        DEBUG_PRINTLN(solidAnimMode);
      }
    }

    // ============================================================
    // WiFi 多网络管理
    //
    // 支持的操作（可单独调用，也可组合）：
    //   addwifi=1&ssid=&password=          只保存一条网络（不改连接状态）
    //   connectwifi=idx                    立即连接第 idx 条网络
    //   movewifi=idx&dir=0|1               调整优先级顺序（0=上移、1=下移）
    //   delwifi=idx                        删除第 idx 条
    //   setpw=idx&password=                修改第 idx 条的密码
    //   setmode=1&staEnabled=&apEnabled=   保存 STA / AP 模式开关
    //   saveconfig=1&…                     （兼容旧版网页，语义同 addwifi + setmode）
    // ============================================================
    bool   wifiChanged    = false;   // 有变化需要落盘
    bool   wifiNeedSwitch = false;   // 需要用新配置立即重连
    String wifiMsg        = "";

    if (request->hasParam("addwifi") || request->hasParam("saveconfig")) {
      String ssid     = request->hasParam("ssid")     ? request->getParam("ssid")->value()     : String("");
      String password = request->hasParam("password") ? request->getParam("password")->value() : String("");
      ssid.trim();

      if (ssid.length() > 0) {
        if (!wifiListAdd(ssid.c_str(), password.c_str())) {
          request->send(400, "text/plain",
                        "添加失败：最多保存 " + String(MAX_WIFI_NETS) + " 个网络，请先删除一个");
          return;
        }
        wifiChanged = true;
        // 只保存，不连接：连不连、连哪个由用户在"已保存的网络"里点「连接」决定
        wifiMsg = "已保存 " + ssid + "，点它右侧的「连接」即可使用";
        DEBUG_PRINT("[WiFi] 已保存网络: ");
        DEBUG_PRINTLN(ssid);
      } else {
        wifiMsg = "未填写 WiFi 名称，仅保存模式设置";
      }
    }

    if (request->hasParam("connectwifi")) {
      int idx = request->getParam("connectwifi")->value().toInt();
      if (!wifiListSetActive(idx)) {
        request->send(400, "text/plain", "无效的网络序号");
        return;
      }
      wifiChanged    = true;
      wifiNeedSwitch = true;
      wifiMsg        = "正在连接 " + String(wifiList.nets[idx].ssid) + " …";
      DEBUG_PRINT("[WiFi] 手动连接: ");
      DEBUG_PRINTLN(wifiList.nets[idx].ssid);
    }

    if (request->hasParam("movewifi")) {
      int idx = request->getParam("movewifi")->value().toInt();
      int dir = request->hasParam("dir")
                  ? (request->getParam("dir")->value().toInt() != 0 ? 1 : -1)
                  : -1;
      if (!wifiListMove(idx, dir)) {
        request->send(400, "text/plain", "无法移动（已在首/末位或序号无效）");
        return;
      }
      wifiChanged = true;
      wifiMsg     = "已调整顺序，" + String(wifiList.nets[idx + dir].ssid) + " 现在是第 "
                    + String(idx + dir + 1) + " 位";
      DEBUG_PRINT("[WiFi] 顺序已调整: ");
      DEBUG_PRINTLN(wifiMsg);
    }

    if (request->hasParam("setactive")) {
      // 兼容旧版网页：语义等同于"连接该网络"
      int idx = request->getParam("setactive")->value().toInt();
      if (!wifiListSetActive(idx)) {
        request->send(400, "text/plain", "无效的网络序号");
        return;
      }
      wifiChanged    = true;
      wifiNeedSwitch = true;
      wifiMsg        = "正在连接 " + String(wifiList.nets[idx].ssid) + " …";
    }

    if (request->hasParam("delwifi")) {
      int idx = request->getParam("delwifi")->value().toInt();
      if (idx < 0 || idx >= wifiList.count) {
        request->send(400, "text/plain", "无效的网络序号");
        return;
      }
      String gone = String(wifiList.nets[idx].ssid);
      bool   wasActive = (idx == wifiList.active);
      wifiListDelete(idx);
      wifiChanged    = true;
      wifiNeedSwitch = (wifiList.count > 0 && wasActive);
      wifiMsg        = "已删除网络 " + gone;
      DEBUG_PRINT("[WiFi] 已删除网络: ");
      DEBUG_PRINTLN(gone);
    }

    if (request->hasParam("setpw")) {
      int idx = request->getParam("setpw")->value().toInt();
      String password = request->hasParam("password") ? request->getParam("password")->value() : String("");
      if (!wifiListSetPassword(idx, password.c_str())) {
        request->send(400, "text/plain", "无效的网络序号");
        return;
      }
      wifiChanged    = true;
      wifiNeedSwitch = (idx == wifiList.active);
      wifiMsg        = "已更新 " + String(wifiList.nets[idx].ssid) + " 的密码";
      DEBUG_PRINTLN("[WiFi] 网络密码已更新");
    }

    // 模式开关：随 addwifi / saveconfig 一起提交时也应生效，因此只要带上
    // staEnabled / apEnabled 参数就处理。
    if (request->hasParam("setmode") || request->hasParam("saveconfig") ||
        request->hasParam("staEnabled") || request->hasParam("apEnabled")) {
      if (request->hasParam("staEnabled")) {
        wifiConfig.staEnabled = (request->getParam("staEnabled")->value().toInt() != 0);
      }
      if (request->hasParam("apEnabled")) {
        wifiConfig.apEnabled  = (request->getParam("apEnabled")->value().toInt() != 0);
      }
      // 至少保留一种模式，否则设备无法访问
      if (!wifiConfig.staEnabled && !wifiConfig.apEnabled) {
        wifiConfig.apEnabled = true;
      }
      // 上电后的强制窗口内，就算把「AP 模式」取消勾选也关不掉热点 —— 说清楚，
      // 免得用户以为开关坏了。
      if (!wifiConfig.apEnabled && apForceWindowActive()) {
        unsigned long rem = apForceWindowRemainMs() / 1000UL;
        String hint = "（上电后前 5 分钟内热点强制开启，约 " + String(rem / 60)
                    + " 分钟 " + String(rem % 60) + " 秒后按此设置执行）";
        wifiMsg = wifiMsg.length() ? (wifiMsg + hint) : ("已保存" + hint);
      }
      wifiChanged = true;
    }

    if (wifiChanged) {
      if (!saveWiFiConfig()) {
        request->send(500, "text/plain", "保存失败：EEPROM 写入/校验不通过，请重试");
        return;
      }
      // 需要换网/换密码时，交给 loop 执行实际重连：
      // 在 AsyncTCP 回调里直接调用 WiFi.disconnect()/begin() 会影响正在返回的
      // HTTP 响应，因此这里只置标志位。
      if (wifiNeedSwitch && wifiConfig.staEnabled && wifiConfig.ssid[0] != '\0') {
        wifiSwitchNow = true;
      }
      notifyStateChanged();
      request->send(200, "text/plain", wifiMsg.length() ? wifiMsg : "已保存");
      return;
    }

    // 处理 MQTT 配置保存（巴法云 Client ID + 订阅主题）
    //
    // 允许保存为空：清空 Client ID / 订阅主题也算合法操作。
    // 但凭据为空时不可能连上云平台，因此若此时开关处于开启状态，
    // 会顺手把总开关关掉并落盘（避免"开关开着却永远连不上"）。
    // ---- 可选外设开关（DHT11 / 红外接收）----
    if (request->hasParam("dhtenable")) {
      dhtEnabled = (request->getParam("dhtenable")->value().toInt() != 0);
      savePeriphFlags();
      DEBUG_PRINTLN("[外设] DHT11: " + String(dhtEnabled ? "开" : "关"));
      request->send(200, "text/plain", dhtEnabled ? "DHT11 已开启（接 D5/GPIO14）" : "DHT11 已关闭");
      return;
    }
    if (request->hasParam("irenable")) {
      irEnabled = (request->getParam("irenable")->value().toInt() != 0);
      savePeriphFlags();
      request->send(200, "text/plain", irEnabled ? "红外接收已开启（接 D2/GPIO4）" : "红外接收已关闭");
      return;
    }
    // ---- OTA 定时自动升级：开关 + 每天几点 + 升级链接 ----
    // ---- OTA 定时自动检查升级开关 ----
    if (request->hasParam("otaauto")) {
      otaAutoEnabled = (request->getParam("otaauto")->value().toInt() != 0);
      savePeriphFlags();
      request->send(200, "text/plain", otaAutoEnabled
        ? "已开启定时自动检查升级（固件有新版本会直接刷入）"
        : "已关闭定时自动检查升级");
      return;
    }
    // ---- OTA 自动检查间隔（分钟）----
    if (request->hasParam("otainterval")) {
      int v = request->getParam("otainterval")->value().toInt();
      if (v < 1 || v > 65535) { request->send(400, "text/plain", "间隔须为 1~65535 分钟"); return; }
      otaIntervalMin = (uint16_t)v;
      savePeriphFlags();
      request->send(200, "text/plain", "自动检查间隔已设为每 " + String(v) + " 分钟");
      return;
    }
    // ---- 立即检查并升级（有新版本自动刷入）----
    if (request->hasParam("otachecknow")) {
      otaCheckNow = true;
      request->send(200, "text/plain", "开始检查，若有新固件将自动升级...");
      return;
    }
    if (request->hasParam("savemqtt")) {
      String mqttId    = request->hasParam("mqttid")    ? request->getParam("mqttid")->value()    : String("");
      String mqttTopic = request->hasParam("mqtttopic") ? request->getParam("mqtttopic")->value() : String("");
      // 可选：服务器地址与端口（不传则保持原值，兼容老网页）
      bool serverChanged = false;
      if (request->hasParam("mqttserver")) {
        String srv = request->getParam("mqttserver")->value();
        srv.trim();
        if (srv.length() > 0) {
          strncpy(mqttServerConfig.server, srv.c_str(), sizeof(mqttServerConfig.server) - 1);
          mqttServerConfig.server[sizeof(mqttServerConfig.server) - 1] = '\0';
          serverChanged = true;
        }
      }
      if (request->hasParam("mqttport")) {
        int p = request->getParam("mqttport")->value().toInt();
        if (p > 0 && p <= 65535) { mqttServerConfig.port = (uint16_t)p; serverChanged = true; }
      }
      // 可选：一同提交开关状态（不传则保持当前内存中的值）
      if (request->hasParam("mqttenabled")) {
        mqttConfig.enabled = (request->getParam("mqttenabled")->value().toInt() != 0);
      }

      strncpy(mqttConfig.clientId, mqttId.c_str(),    sizeof(mqttConfig.clientId) - 1);
      strncpy(mqttConfig.topic,    mqttTopic.c_str(), sizeof(mqttConfig.topic)    - 1);
      mqttConfig.clientId[sizeof(mqttConfig.clientId) - 1] = '\0';
      mqttConfig.topic[sizeof(mqttConfig.topic) - 1]       = '\0';

      // 凭据为空 → 不允许保持开启，顺手关闭总开关
      bool credsEmpty   = (mqttConfig.clientId[0] == '\0' || mqttConfig.topic[0] == '\0');
      bool autoDisabled = false;
      if (credsEmpty && mqttConfig.enabled) {
        mqttConfig.enabled = false;
        autoDisabled = true;
      }

      bool ok = saveMqttConfig();
      if (serverChanged && ok) ok = saveMqttServerConfig();   // 服务器地址/端口有变才落盘
      if (ok) {
        if (autoDisabled) {
          // 立即断开已有连接，此后不再执行任何巴法云功能
          if (client.connected()) client.disconnect();
        } else if (mqttConfig.enabled) {
          // 凭据变化后若开关已开启，断开旧连接并请求立即用新凭据重连
          if (client.connected()) client.disconnect();
          mqttReconnectNow = true;
        }
        notifyStateChanged();
        if (autoDisabled) {
          request->send(200, "text/plain", "配置已保存（内容为空），巴法云开关已自动关闭");
        } else {
          request->send(200, "text/plain", "MQTT配置已保存并校验通过");
        }
      } else {
        request->send(500, "text/plain", "保存失败：EEPROM 写入/校验不通过，请重试");
      }
      return;
    }

    // 处理巴法云 MQTT 总开关（切换后立即生效，无需重启）
    if (request->hasParam("mqttenable")) {
      bool want = (request->getParam("mqttenable")->value().toInt() != 0);

      // 凭据为空时不允许开启，避免用空 Client ID 反复连接失败
      if (want && (mqttConfig.clientId[0] == '\0' || mqttConfig.topic[0] == '\0')) {
        request->send(400, "text/plain", "请先填写并保存 Client ID 与订阅主题");
        return;
      }

      mqttConfig.enabled = want;
      if (!saveMqttConfig()) {
        request->send(500, "text/plain", "开关保存失败：EEPROM 写入/校验不通过，请重试");
        return;
      }

      if (want) {
        mqttReconnectNow = true;   // 请求 loop 立即建立连接
        notifyStateChanged();
        request->send(200, "text/plain", "巴法云已开启（连接状态见上方「云平台连接」）");
      } else {
        // 关闭：立即断开已有连接，此后不再执行任何巴法云功能
        if (client.connected()) client.disconnect();
        notifyStateChanged();
        request->send(200, "text/plain", "巴法云已关闭");
      }
      return;
    }

    // 处理重启设备
    if (request->hasParam("restart")) {
      request->send(200, "text/plain", "设备即将重启...");
      delay(1000);
      ESP.restart();
      return;
    }

    // 所有状态变更操作完成后，通知 SSE 客户端刷新
    notifyStateChanged();

    // 根据本次请求携带的参数，返回具体的结果文案（而非笼统的 "OK"），
    // 这样发送端的状态栏能显示「亮度已调整为 50%」这类明确反馈。
    String result;
    if (request->hasParam("brightness")) {
      result = "亮度已调整为 " + request->getParam("brightness")->value() + "%";
    } else if (request->hasParam("speed")) {
      result = "速度已调整为 " + request->getParam("speed")->value();
    } else if (request->hasParam("color") && request->hasParam("mode")) {
      result = "颜色已设置";
    } else if (request->hasParam("color")) {
      result = "颜色已设置";
    } else if (request->hasParam("mode")) {
      result = "模式已切换为 " + request->getParam("mode")->value();
    } else if (request->hasParam("timer")) {
      result = "定时关闭已设置";
    } else if (request->hasParam("reverse")) {
      result = "反向显示已设置";
    } else {
      result = "OK";
    }
    request->send(200, "text/plain", result);
  });

  // ---- GET /scanwifi（启动扫描附近 WiFi）----
  //
  // 【为什么必须用异步扫描】
  // ESP8266 的 wifi_station_scan 在同步模式下会一直阻塞到扫描结束（约 2~4 秒），
  // HTTP 请求要等这么久才响应；而扫描期间链路本来就会短暂中断，同步等待会让
  // 网页看起来像卡死。所以本接口只负责"启动异步扫描"后立即返回，
  // 网页再轮询 /wifilist 取结果。
  server.on("/scanwifi", HTTP_GET, [](AsyncWebServerRequest* request) {
    if (WiFi.scanComplete() == WIFI_SCAN_RUNNING) {
      request->send(200, "application/json", "{\"status\":\"running\"}");
      return;
    }

    // 异步扫描；show_hidden=true 让隐藏 SSID 也能被扫到（虽然无名称无法选择）
    lastScanActivityMs = millis();
    int8_t ret = WiFi.scanNetworks(true /* async */, true /* show hidden */);
    DEBUG_PRINT("[WiFi扫描] 启动结果: ");
    DEBUG_PRINTLN(ret);

    request->send(200, "application/json",
                  (ret == WIFI_SCAN_RUNNING || ret >= 0)
                    ? "{\"status\":\"started\"}"
                    : "{\"status\":\"failed\"}");
  });

  // ---- GET /wifilist（读取扫描结果）----
  server.on("/wifilist", HTTP_GET, [](AsyncWebServerRequest* request) {
    int n = WiFi.scanComplete();

    if (n == WIFI_SCAN_RUNNING) {
      request->send(200, "application/json", "{\"status\":\"running\"}");
      return;
    }
    if (n < 0) {
      // WIFI_SCAN_FAILED：从未扫描过，或结果已被上一次请求取走
      request->send(200, "application/json", "{\"status\":\"idle\",\"networks\":[]}");
      return;
    }

    // 去重（同一 SSID 的多个 AP 只保留信号最强的）+ 按信号强度降序。
    // 用 static 固定缓冲区，避免在 AsyncTCP 任务较小的栈上分配大数组。
    const int MAX_LIST = 24;
    struct NetEntry { char ssid[33]; int8_t rssi; bool open; };
    static NetEntry nets[MAX_LIST];

    int count = 0;
    for (int i = 0; i < n; i++) {
      String ssid = WiFi.SSID(i);
      if (ssid.length() == 0) continue;           // 隐藏网络无名称，下拉框里没法选
      if (ssid.length() > 32) ssid = ssid.substring(0, 32);

      int8_t rssi = (int8_t)WiFi.RSSI(i);
      bool  open_  = (WiFi.encryptionType(i) == ENC_TYPE_NONE);

      int found = -1;
      for (int k = 0; k < count; k++) {
        if (strcmp(nets[k].ssid, ssid.c_str()) == 0) { found = k; break; }
      }
      if (found >= 0) {
        if (rssi > nets[found].rssi) nets[found].rssi = rssi;   // 保留更强信号
        continue;
      }
      if (count >= MAX_LIST) continue;

      memset(nets[count].ssid, 0, sizeof(nets[count].ssid));
      strncpy(nets[count].ssid, ssid.c_str(), sizeof(nets[count].ssid) - 1);
      nets[count].rssi = rssi;
      nets[count].open = open_;
      count++;
    }

    // 插入排序：信号强的排前面
    for (int i = 1; i < count; i++) {
      NetEntry tmp = nets[i];
      int j = i - 1;
      while (j >= 0 && nets[j].rssi < tmp.rssi) {
        nets[j + 1] = nets[j];
        j--;
      }
      nets[j + 1] = tmp;
    }

    String json;
    json.reserve(48 + count * 64);
    json = "{\"status\":\"done\",\"networks\":[";
    for (int i = 0; i < count; i++) {
      if (i) json += ',';
      json += "{\"ssid\":\"";
      json += jsonEscape(String(nets[i].ssid));
      json += "\",\"rssi\":";
      json += String((int)nets[i].rssi);
      json += ",\"open\":";
      json += (nets[i].open ? "true" : "false");
      json += '}';
    }
    json += "]}";

    // 结果已取走，释放 SDK 内部缓冲区
    WiFi.scanDelete();
    lastScanActivityMs = millis();   // 延长抑制窗口，覆盖链路恢复过程

    DEBUG_PRINT("[WiFi扫描] 返回网络数: ");
    DEBUG_PRINTLN(count);
    request->send(200, "application/json", json);
  });

  // ---- GET /state（状态查询接口）----
  server.on("/state", HTTP_GET, [](AsyncWebServerRequest* request) {
    // ArduinoJson 7.x：StaticJsonDocument 已废弃且忽略容量参数（内部走堆分配），
    // 直接使用 JsonDocument 即可，不再需要手工估算容量。
    JsonDocument doc;

    doc["mode"]       = currentMode;

    char colorHex[7];
    sprintf(colorHex, "%02X%02X%02X", solidColor.r, solidColor.g, solidColor.b);
    doc["color"]      = colorHex;

    doc["brightness"] = map(brightness, 1, 255, 0, 100);
    doc["speed"]      = effectSpeed;
    doc["ledCount"]   = num_leds;
    doc["reverse"]    = reverseDisplay;
    doc["solidAnim"]  = solidAnimMode;
    doc["dhtEnabled"]   = dhtEnabled;
    doc["irEnabled"]    = irEnabled;
    doc["otaAuto"]      = otaAutoEnabled;
    doc["temperature"]  = dhtEnabled ? String(dhtTemperature, 1) : String("-1");
    doc["humidity"]     = dhtEnabled ? String((int)(dhtHumidity + 0.5f)) : String("-1");
    doc["otaLastVersion"] = otaLastVersion;
    doc["otaLastDate"]    = otaLastDate;
    doc["otaRemoteVer"]   = otaRemoteVer;
    doc["otaRemoteDate"]  = otaRemoteDate;
    doc["otaRemoteSize"]  = otaRemoteSize;
    doc["otaRemoteOk"]    = otaRemoteOk;
    doc["otaHasNew"]      = otaHasNew;

    doc["timerActive"] = timerActive;
    doc["timerRemaining"] = timerActive
      ? (unsigned long)((timerEndTime - millis()) / 1000)
      : 0UL;

    doc["batteryLevel"]  = batteryLevel;
    doc["wifiConnected"] = (WiFi.status() == WL_CONNECTED);
    doc["apModeActive"]  = (bool)(WiFi.getMode() & WIFI_AP);
    doc["staEnabled"]    = wifiConfig.staEnabled;
    doc["apEnabled"]     = wifiConfig.apEnabled;
    doc["apTempEnabled"] = apTempEnabled;
    // 上电强制开热点窗口（网页据此显示倒计时提示）
    doc["apForced"]        = apForceWindowActive();
    doc["apForceRemainMs"] = apForceWindowRemainMs();

    // ---- 当前网络：**已连接时以实际连上的为准** ----
    // wifiConfig 只是 wifiList.active 的镜像；若断线后自动切到了别的网络，
    // 而 active 还没同步过来（manageWiFiConnection 每 5 秒才对一次），
    // 这里直接读 WiFi.SSID()，网页就不会出现"实际用网络2、却显示网络1"。
    int liveNetIdx = -1;   // 实际连上的网络在列表里的下标（-1 = 未知/未连接）
    {
      String liveSsid = (WiFi.status() == WL_CONNECTED) ? WiFi.SSID() : String("");
      if (liveSsid.length() > 0) {
        for (int i = 0; i < wifiList.count; i++) {
          if (liveSsid == wifiList.nets[i].ssid) { liveNetIdx = i; break; }
        }
        doc["ssid"]     = liveSsid;
        // 密码跟着取那一家的（网页在密码框里回填时要对得上号）
        doc["password"] = (liveNetIdx >= 0) ? String(wifiList.nets[liveNetIdx].password)
                                            : String(wifiConfig.password);
      } else {
        doc["ssid"]     = wifiConfig.ssid;
        doc["password"] = wifiConfig.password;
      }
    }

    // 已保存的多组 WiFi 网络（供网页渲染"已保存的网络"列表）
    // 密码一并下发：网页在下拉框里切回某个已保存网络时要能自动填回密码，
    // 否则一换网络密码就被清空。配置页只在本机/热点内可访问，
    // 与 doc["password"]（当前网络密码）属于同一暴露级别。
    // 断网轮询时"正在尝试"的网络（网页显示用；未在轮询时为空字符串）
    doc["wifiTrying"] = (wifiRetryIdx >= 0 && wifiRetryIdx < wifiList.count)
      ? String(wifiList.nets[wifiRetryIdx].ssid)
      : String("");
    // "当前"徽标同样以实际连上的为准，避免列表里标错人
    int effActive = (liveNetIdx >= 0) ? liveNetIdx : (int)wifiList.active;
    doc["wifiActive"] = effActive;
    doc["wifiMax"]    = MAX_WIFI_NETS;
    {
      JsonArray arr = doc["wifiNets"].to<JsonArray>();
      for (int i = 0; i < wifiList.count; i++) {
        JsonObject o = arr.add<JsonObject>();
        o["ssid"]     = wifiList.nets[i].ssid;
        o["active"]   = (i == effActive);
        o["password"] = wifiList.nets[i].password;
        // 连续连接失败次数：网页在达到 WIFI_FAIL_MARK 时标灰点（诊断用）
        o["fails"]    = wifiFailCount[i];
      }
    }
    doc["mqttClientId"]  = mqttConfig.clientId;
    doc["mqttTopic"]     = mqttConfig.topic;
    doc["mqttServer"]    = mqttServerConfig.server;
    doc["mqttPort"]      = mqttServerConfig.port;
    doc["mqttEnabled"]   = mqttConfig.enabled;
    doc["mqttConnected"] = client.connected();
    // 网页据此显示"已连接 / 已开启，正在连接…"以及"上一次接收内容"
    doc["mqttLastMsg"]   = mqttLastMsg;
    doc["mqttMsgCount"]  = mqttMsgCount;
    doc["mqttLastMsgAgo"] = (mqttMsgCount > 0) ? (millis() - mqttLastMsgAt) : 0;

    doc["ipAddress"]   = (WiFi.status() == WL_CONNECTED)
      ? WiFi.localIP().toString()
      : String("未连接");
    doc["apIpAddress"] = (WiFi.getMode() & WIFI_AP)
      ? WiFi.softAPIP().toString()
      : String("未开启");

    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
    DEBUG_PRINTLN("已发送状态信息");
  });

  // ---- GET /music（音乐可视化接口）----
  server.on("/music", HTTP_GET, [](AsyncWebServerRequest* request) {
    if (request->hasParam("volume")) {
      lastMusicUpdate = millis();
      musicVolume     = constrain(request->getParam("volume")->value().toInt(), 0, 100);

      if (!inMusicMode) {
        previousModeBeforeMusic = currentMode;
        currentMode  = "music";
        inMusicMode  = true;
        resetEffectState();
      }
      request->send(200, "text/plain", "Volume updated: " + String(musicVolume) + "%");
    } else {
      request->send(400, "text/plain", "Missing volume parameter");
    }
  });

  // ---- GET /ota（一键 HTTP 升级）----
  // 参数 url = 固件 .bin 下载链接。立即返回"开始升级"，随后在后台执行升级；
  // 升级成功会自动重启到新固件，失败则回到当前固件继续运行。
  // ---- GET /temperature（纯数字：当前温度，如 25.4；未启用返回 -1）----
  server.on("/temperature", HTTP_GET, [](AsyncWebServerRequest* request) {
    request->send(200, "text/plain", dhtEnabled ? String(dhtTemperature, 1) : String("-1"));
  });

  // ---- GET /humidity（纯数字：当前湿度整数，如 60；未启用返回 -1）----
  server.on("/humidity", HTTP_GET, [](AsyncWebServerRequest* request) {
    request->send(200, "text/plain", dhtEnabled ? String((int)(dhtHumidity + 0.5f)) : String("-1"));
  });

  // ---- GET /otastatus（纯数字：OTA 进度。-2=失败 -1=空闲 0~99=下载中 100=写入Flash）----
  server.on("/otastatus", HTTP_GET, [](AsyncWebServerRequest* request) {
    request->send(200, "text/plain", String(otaProgress));
  });

  // ---- GET /otacheck（OTA 检查信息：当前固件版本 vs 巴法云远程固件）----
  server.on("/otacheck", HTTP_GET, [](AsyncWebServerRequest* request) {
    otaCheckRequest();   // 登记检查请求（loop 里执行，避免在异步回调里做网络请求）
    String j = String("{\"local\":") + otaLastVersion
             + ",\"remote\":" + otaRemoteVer
             + ",\"remoteDate\":\"" + otaRemoteDate
             + "\",\"remoteSize\":" + otaRemoteSize
             + ",\"ok\":" + (otaRemoteOk ? "true" : "false")
             + ",\"hasNew\":" + (otaHasNew ? "true" : "false") + "}";
    request->send(200, "application/json", j);
  });

  // ---- GET /ota（手动指定固件链接升级）----
  server.on("/ota", HTTP_GET, [](AsyncWebServerRequest* request) {
    if (!request->hasParam("url")) {
      request->send(400, "text/plain", "缺少 url 参数");
      return;
    }
    String url = request->getParam("url")->value();
    url.trim();
    if (url.length() < 8 || !(url.startsWith("http://") || url.startsWith("https://"))) {
      request->send(400, "text/plain", "下载链接无效，需以 http:// 或 https:// 开头");
      return;
    }
    DEBUG_PRINTLN("OTA 升级链接: " + url);
    request->send(200, "text/plain", "开始升级，设备即将重启...");

    // 升级过程会阻塞且可能重启，放在延时后执行，确保上面的响应已发出
    static String otaUrl;   // static 避免栈上大对象
    otaUrl = url;
    // 用一个标志交给 loop() 执行，避免在 AsyncWebServer 回调里做耗时/重启操作
    otaPendingUrl = otaUrl;
  });

  // ---- GET /favicon.ico（网页图标，内联 SVG）----
  server.on("/favicon.ico", HTTP_GET, [](AsyncWebServerRequest* request) {
    request->send_P(200, "image/svg+xml", FAVICON_SVG);
  });

  // 启动 HTTP 服务器
  server.begin();
  DEBUG_PRINTLN("HTTP服务器已启动");
}

// ============================================================
// 动态 WiFi / AP 管理
// ============================================================

/**
 * @brief 运行时动态管理 STA 连接与临时 AP 热点。
 *
 * **触发条件**：仅当用户配置为 STA 模式且未开启 AP 模式时生效
 *             （即 wifiConfig.staEnabled=true 且 wifiConfig.apEnabled=false）。
 *
 * **状态机逻辑**：
 *  - STA 断开 → 立即开临时 AP（WIFI_AP_STA，SDK 继续后台重连）
 *  - STA 重新连通 → 开始计时
 *  - 稳定连通 WIFI_STABLE_MS（默认60秒）→ 关闭临时 AP（切回 WIFI_STA）
 *  - 再次断开 → 重新开 AP，循环往复
 *
 * @note 每 5 秒轮询一次，不阻塞主循环。
 *       static 变量在函数内部保持跨 loop() 调用的状态。
 */
// ============================================================
// 断网重连轮询的排程状态（文件作用域，便于 wifiRetryRestart() 重置）
// ============================================================
static unsigned long wifiRetryNextAt = 0;   ///< 下一次轮询尝试的时间点（0 = 未排程）
static int           wifiRetryCursor = 0;   ///< 下一个要尝试的网络下标（顺序轮询游标）

/**
 * @brief 重置断网轮询排程。
 *
 * 用户在网页上手动"连接"某个网络后调用：重新开始计时宽限期，
 * 让 SDK 专心连这个网络；轮询游标也归零，之后仍从第一个网络按顺序试。
 */
void wifiRetryRestart() {
  wifiRetryNextAt = 0;
  wifiRetryCursor = 0;
  wifiRetryIdx    = -1;
}

/**
 * @brief 计算"下一次轮询之前的等待时间"（含整轮失败的退避）。
 *
 * 每完成一整轮（列表里每个网络各试一次）就把 WIFI_RETRY_STEP_MS 翻倍：
 *   15s → 30s → 60s（封顶，见 WIFI_RETRY_MAX_MS）
 * 一旦连上，游标在"情形 A"里归零，退避自然回到 15 秒。
 *
 * @note 调用时机：游标已自增完，所以"已完成轮数 = 游标 / 网络数"。
 */
static unsigned long wifiRetryStepMs() {
  if (wifiList.count == 0) return WIFI_RETRY_STEP_MS;   // 理论上到不了
  int rounds = wifiRetryCursor / wifiList.count;
  unsigned long step = WIFI_RETRY_STEP_MS;
  for (int r = 0; r < rounds && step < WIFI_RETRY_MAX_MS; r++) step *= 2;
  return (step > WIFI_RETRY_MAX_MS) ? WIFI_RETRY_MAX_MS : step;
}

/**
 * @brief 运行时 WiFi 管理：临时 AP 兜底 + 断网后的多网络轮询重连。
 *
 * 只要开了 STA 本函数就会介入（AP 开不开都一样）：
 *  - ESP8266 的 `WiFi.disconnect()/begin()` 只影响 STA 侧，
 *    不会踢掉正在通过热点配网的客户端，所以 AP+STA 模式下也能安全换网。
 *  - 有 WIFI_RETRY_GRACE_MS 的宽限期：刚断网时先交给 SDK 原地重连当前网络
 *    （路由器重启这类情况最多，没必要立刻换网）。
 *  - 宽限期过后进入**严格顺序轮询**：每次只尝试一个网络，下一个等下一轮
 *    （游标 +1 取模），游标顺序就是列表顺序（=用户排好的优先级）。
 *  - 间隔带**退避**：一整轮全部失败后间隔翻倍（15s→30s→60s 封顶），
 *    一旦连上立即归零。见 wifiRetryStepMs()。
 *  - 每个网络的"连续失败次数"记在 wifiFailCount[]（仅 RAM），网页据此标灰点。
 *  - 热点开关在这里统一"协调"：上电强制窗口 / 「AP 模式」开关 / STA 是否稳定，
 *    三者取或决定该不该开着热点。
 *  - 实际连上的网络若和 wifiList.active 不一致（断线后自动切到了别的网络），
 *    **以实际为准**更新 active 并落盘，否则网页会一直显示旧网络的名字。
 *
 * 注意：轮询**尝试**过程中不改 wifiList.active（否则网页上的"当前网络/SSID
 * 下拉框"会每十几秒跳一次，用户根本没法在上面操作）；只有真正连上了才改。
 */
void manageWiFiConnection() {
  // 没开 STA 就没有"按顺序重连"这回事，AP 交给用户自己管
  if (!wifiConfig.staEnabled) return;

  // ---- 静态状态变量（仅初始化一次）----
  static bool     initialized        = false;   ///< 是否已完成首次状态同步
  static bool     staWasConnected    = false;   ///< 上次轮询时的 STA 连接状态
  static unsigned long stableStartMs = 0;        ///< STA 持续连通的开始时间（0=未计时）
  static unsigned long lastCheckMs   = 0;        ///< 上次轮询时间戳
  static bool     apForceWas         = false;    ///< 上次进入时是否还在上电强制窗口内

  unsigned long now = millis();

  // 上电强制开热点窗口（见 AP_FORCE_BOOT_MS）
  bool apForce = apForceWindowActive();

  // ---- 首次调用：同步当前真实状态，避免误判 ----
  if (!initialized) {
    initialized     = true;
    staWasConnected = (WiFi.status() == WL_CONNECTED);
    stableStartMs   = staWasConnected ? now : 0;
    apForceWas      = apForce;
    DEBUG_PRINT("[WiFi管理] 初始化完成，STA状态: ");
    DEBUG_PRINTLN(staWasConnected ? "已连接" : "未连接");
    return;
  }

  // ---- 强制窗口刚结束 ----
  // 之后热点开关完全交给「AP 模式」勾选状态（以及断线兜底）决定，
  // 这里只需推一次状态，让网页上的倒计时提示消失。
  if (apForceWas && !apForce) {
    apForceWas = false;
    DEBUG_PRINTLN("[WiFi管理] 上电强制开热点窗口已结束，热点开关交回「AP模式」设置");
    notifyStateChanged();
  }

  // ---- 扫描抑制窗口 ----
  // WiFi 扫描期间射频离开当前信道，STA 状态会短暂抖动。若此时照常判断，会
  // 把抖动误判成"断线"，进而重新初始化 softAP（踢掉正在配置的客户端）并
  // 开启临时 AP。因此扫描进行中、以及扫描结束后的一小段恢复期内跳过判断。
  // 注意：这里不消耗轮询时间戳，恢复后能立刻按原节奏继续判断。
  if (WiFi.scanComplete() == WIFI_SCAN_RUNNING ||
      (lastScanActivityMs != 0 && now - lastScanActivityMs < 5000UL)) {
    return;
  }

  // ---- 每 5 秒轮询一次 ----
  if (now - lastCheckMs < 5000UL) return;
  lastCheckMs = now;

  bool currentlyConnected = (WiFi.status() == WL_CONNECTED);

  // ================================================================
  // AP 热点协调：决定"现在该不该开着热点"，然后一次性对齐
  // ================================================================
  // 期望状态 = 下面三者取或：
  //   ① 上电后的强制窗口（AP_FORCE_BOOT_MS）—— 保证刚通电时一定进得了配置页，
  //      且不受「AP 模式」开关影响；
  //   ② 用户勾选了「AP 模式」—— 长期开启；
  //   ③ STA 还没"稳定连通"（断线中 / 刚连上不足 WIFI_STABLE_MS）——
  //      既是断线兜底，也给"正通过热点配置的人"留出完成操作的时间，
  //      避免 STA 刚一连上就把 AP 上的浏览器踢下去。
  bool staSettled   = currentlyConnected && stableStartMs > 0 &&
                      (now - stableStartMs >= WIFI_STABLE_MS);
  bool apShouldBeOn = apForce || wifiConfig.apEnabled || !staSettled;
  bool apIsOn       = (WiFi.getMode() & WIFI_AP) != 0;

  if (apShouldBeOn && !apIsOn) {
    // 【注意】不要对"已经开着"的热点重复调用 softAP()：那会重新初始化热点
    // 并踢掉所有已连接客户端，所以只在确实关着的时候才开。
    WiFi.mode(WIFI_AP_STA);        // 保留 STA 侧，便于在热点上直接配网/切网
    WiFi.softAP("LAND_LED", "12345678");
    apTempEnabled = !wifiConfig.apEnabled;
    DEBUG_PRINT("[WiFi管理] 已开启热点（");
    DEBUG_PRINT(apForce ? "上电强制窗口" : (wifiConfig.apEnabled ? "用户已勾选AP模式" : "STA未稳定"));
    DEBUG_PRINT("），IP: ");
    DEBUG_PRINTLN(WiFi.softAPIP());
    notifyStateChanged();
  } else if (!apShouldBeOn && apIsOn) {
    WiFi.softAPdisconnect(true);   // 关闭热点
    WiFi.mode(WIFI_STA);           // 切回纯 STA
    apTempEnabled = false;
    DEBUG_PRINTLN("[WiFi管理] STA已稳定连通且未勾选「AP模式」，热点已关闭。");
    notifyStateChanged();
  } else if (apIsOn) {
    // 热点状态没变，但"是不是自动开的"可能变了（用户改了开关）→ 保持标记同步
    apTempEnabled = !wifiConfig.apEnabled;
  }

  // ================================================================
  // 情形 A：当前 STA 已连接
  // ================================================================
  if (currentlyConnected) {
    // 先取出"上次尝试的网络"：下面要把轮询状态清零，清完就取不到了
    int triedIdx = wifiRetryIdx;

    // 连接恢复 → 清空轮询排程与退避（游标归零 = 下次断网从 15 秒重新起步，
    // 而不是接着上一次的高间隔继续）
    wifiRetryNextAt = 0;
    wifiRetryCursor = 0;
    wifiRetryIdx    = -1;

    // ---- 以"实际连上的网络"为准，纠正网页显示 ----
    // 典型场景：正在用网络1，网络1 突然断开，断网轮询连上了网络2 ——
    // 此时 wifiList.active 还停在网络1，网页就会一直显示"已连接至 网络1"。
    // 这里用 WiFi.SSID() 反查真实下标并同步（顺带落盘，重启后"当前"徽标也不会错）。
    int realIdx = -1;
    {
      String curSsid = WiFi.SSID();
      for (int i = 0; i < wifiList.count; i++) {
        if (curSsid.length() && curSsid == wifiList.nets[i].ssid) { realIdx = i; break; }
      }
    }
    if (realIdx >= 0 && (int)wifiList.active != realIdx) {
      DEBUG_PRINT("[WiFi管理] 实际连接的网络与记录不符，同步为: ");
      DEBUG_PRINTLN(wifiList.nets[realIdx].ssid);
      wifiList.active = (uint8_t)realIdx;
      wifiSyncActiveToConfig();   // wifiConfig 是 active 的镜像，必须一起更新
      saveWiFiList();
      notifyStateChanged();       // 立刻推给网页，不用等下一次 2 秒推送
    }

    if (!staWasConnected) {
      // ↑ 状态跳变：断开 → 连接
      staWasConnected = true;
      stableStartMs   = now;
      DEBUG_PRINTLN("[WiFi管理] STA已重新连接，开始1分钟稳定性计时...");

      // 连上了 → 把它之前的"连续失败"计数清零（网页上的灰点消失）。
      // realIdx 上面已按 WiFi.SSID() 算好，别把"试过的那个"和"实际连上的"搞混。
      int okIdx = realIdx;
      if (okIdx < 0 && triedIdx >= 0 && triedIdx < wifiList.count) {
        okIdx = triedIdx;   // 兜底：SDK 没报出 SSID 时用"刚试过的那个"
      }
      if (okIdx >= 0 && wifiFailCount[okIdx] != 0) {
        DEBUG_PRINT("[WiFi管理] 已连上，清除失败计数: ");
        DEBUG_PRINTLN(wifiList.nets[okIdx].ssid);
        wifiFailCount[okIdx] = 0;
        notifyStateChanged();
      }

      // WiFi 恢复后立即尝试重连 MQTT，不等 10 秒轮询
      // 稍作延迟让 TCP 协议栈初始化完成
      if (mqttConfig.enabled) {
        delay(200);
        reconnect();
      }
    }

  // ================================================================
  // 情形 B：当前 STA 未连接
  // ================================================================
  } else {
    if (staWasConnected) {
      // ↓ 状态跳变：连接 → 断开
      staWasConnected = false;
      stableStartMs   = 0;
      DEBUG_PRINTLN("[WiFi管理] STA连接断开。");
    }

    // ---- 断网后的多网络轮询重连 ----
    if (wifiList.count == 0) {   // 没有任何已保存凭据：只能靠 SDK 原地重连
      wifiRetryIdx = -1;
      return;
    }

    // 1) 宽限期：先让 SDK 自己把当前网络接回来（路由器重启等最常见情况）
    if (wifiRetryNextAt == 0) {
      wifiRetryNextAt = now + WIFI_RETRY_GRACE_MS;
      wifiRetryCursor = 0;
      DEBUG_PRINT("[WiFi管理] 断网：先原地重连 ");
      DEBUG_PRINT(WIFI_RETRY_GRACE_MS / 1000);
      DEBUG_PRINTLN(" 秒，之后开始按顺序轮询各网络");
      return;
    }
    if ((long)(now - wifiRetryNextAt) < 0) return;   // 还在宽限/等待期内

    // 2) 结算上一次尝试：能走到这里说明"给它的整个等待窗口"都过完了还没连上，
    //    这才算一次真正的失败。放在窗口末尾判定（而不是刚 WiFi.begin() 几秒后），
    //    是因为有些网络要 5~10 秒才能连上，判早了会冤枉它。
    if (wifiRetryIdx >= 0 && wifiRetryIdx < wifiList.count) {
      if (wifiFailCount[wifiRetryIdx] < 250) wifiFailCount[wifiRetryIdx]++;
      DEBUG_PRINT("[WiFi管理] 连接失败 +1 (");
      DEBUG_PRINT(WIFI_FAIL_MARK);
      DEBUG_PRINT(" 次标灰点): ");
      DEBUG_PRINT(wifiList.nets[wifiRetryIdx].ssid);
      DEBUG_PRINT(" → ");
      DEBUG_PRINTLN(wifiFailCount[wifiRetryIdx]);
      wifiRetryIdx = -1;
      notifyStateChanged();   // 让网页上的灰点立刻更新，不用等下一次推送
    }

    // 3) 轮询：这一轮只试一个网络，下一个等下一轮
    //    间隔带退避：一整轮（列表里每个网络各试一次）全失败后翻倍，
    //    15s → 30s → 60s 封顶，避免长时间没网时一直高频折腾射频。
    int idx = wifiRetryCursor % wifiList.count;
    wifiRetryCursor++;
    unsigned long step = wifiRetryStepMs();
    wifiRetryNextAt = now + step;

    const char* ssid = wifiList.nets[idx].ssid;
    if (ssid[0] == '\0') { wifiRetryIdx = -1; return; }

    wifiRetryIdx = idx;   // 仅用于网页显示"正在尝试"，不改 active
    DEBUG_PRINT("[WiFi管理] 轮询尝试 (");
    DEBUG_PRINT(idx + 1);
    DEBUG_PRINT("/");
    DEBUG_PRINT(wifiList.count);
    DEBUG_PRINT(")，下次间隔 ");
    DEBUG_PRINT(step / 1000);
    DEBUG_PRINTLN(" 秒: ");
    DEBUG_PRINT("  ");
    DEBUG_PRINTLN(ssid);

    WiFi.disconnect();    // 只影响 STA 侧，不会踢掉 AP 上的配网客户端
    delay(50);
    WiFi.begin(ssid, wifiList.nets[idx].password);
  }
}

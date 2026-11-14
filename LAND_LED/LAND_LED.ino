/**
 * @file LAND_LED.ino
 * @brief LAND_LED 灯光控制系统 - 主文件
 *
 * 负责：
 *  - 全局变量实际定义（config.h 中为 extern 声明）
 *  - 辅助工具函数（fanxiang / fill_solid_new / solid_cut /
 *                  decimalToCRGB / hexToCRGB / applySolidColor / resetEffectState）
 *  - HTML 网页内容（存于 Flash PROGMEM）
 *  - setup() 初始化流程
 *  - loop() 主循环
 *
 * 其余功能见各模块文件：
 *  - effects.ino     — 全部灯效函数
 *  - wifi_mqtt.ino   — WiFi / MQTT / Web 服务器路由
 *  - ir_control.ino  — 红外遥控解析
 *  - config.h        — 宏定义 / 结构体 / extern 声明
 */

#include "config.h"

// ============================================================
// 全局变量定义
// （config.h 中已通过 extern 向各模块暴露）
// ============================================================

// --- FastLED ---
CRGB     leds[MAX_LEDS];           ///< LED 灯珠颜色数组（使用最大数量分配）
uint16_t num_leds    = DEFAULT_LEDS; ///< 当前有效灯珠数量
bool     reverseDisplay = true;    ///< 反向显示开关（true=反向）

// --- 灯效状态 ---
String       currentMode       = "off"; ///< 当前灯效模式名称
uint8_t      gHue              = 0;     ///< 全局色相偏移
CRGB         solidColor        = CRGB::White; ///< 单色模式颜色
uint8_t      brightness        = BRIGHTNESS;  ///< 当前亮度
int          effectSpeed       = 66;    ///< 效果速度（1-100）
unsigned long lastEffectUpdate = 0;     ///< 上次灯效刷新时间戳（ms）

// --- SSE 状态推送 ---
volatile uint32_t stateVersion = 0;   ///< 状态版本号（每次变化 +1 触发 SSE）

// --- 呼吸 / 扫描 / 星光 / 极光 内部状态 ---
uint8_t  breathingValue  = 0;
bool     breathingUp     = true;
uint8_t  scannerPosition = 0;
bool     scannerDirection = true;
uint8_t  auroraPosition  = 0;

// --- 蹦迪模式 ---
uint8_t       discoHue        = 0;
bool          strobeOn        = false;
unsigned long lastStrobeChange = 0;

// --- 定时关闭 ---
unsigned long timerEndTime = 0;
bool          timerActive  = false;

// --- 电量显示 ---
int           batteryLevel      = 0;
unsigned long lastBatteryUpdate = 0;
String        previousMode      = "off";
bool          inBatteryMode     = false;

// --- 音乐可视化 ---
int           musicVolume            = 0;
unsigned long lastMusicUpdate        = 0;
bool          inMusicMode            = false;
String        previousModeBeforeMusic = "";

// --- WiFi / MQTT ---
WiFiConfig    wifiConfig;                 ///< 当前生效网络（wifiList 中 active 项的镜像）
WiFiList      wifiList;                   ///< 已保存的多组 WiFi 网络
MqttConfig    mqttConfig;
MqttServerConfig mqttServerConfig;        ///< MQTT 服务器地址/端口（网页可改，默认巴法云）
bool          apTempEnabled = false;
WiFiClient    espClient;
PubSubClient  client(espClient);
bool          mqttReconnectNow = false;   ///< 置位后 loop 立即重连（开关打开时用）
bool          wifiSwitchNow    = false;   ///< 置位后 loop 立即切换到 active 网络

// 最近一次收到的 MQTT 消息：网页端"上一次接收内容"用。
// 用固定长度 char 缓冲而不是 String —— MQTT 消息可能很频繁，
// 反复重建 String 会把堆打碎。
char          mqttLastMsg[MQTT_LAST_MSG_LEN] = ""; ///< extern 声明见 config.h
uint32_t      mqttMsgCount     = 0;       ///< 0 表示从未收到过消息
uint32_t      mqttLastMsgAt    = 0;       ///< 上次收到 MQTT 消息的 millis()，网页算接收时间用
int           wifiRetryIdx     = -1;      ///< 断网轮询"正在尝试"的网络下标（-1=未在尝试）

// --- OTA 升级 ---
String        otaPendingUrl    = "";      ///< 待执行的升级链接；非空时 loop() 会执行升级
volatile int  otaProgress      = -1;      ///< OTA 进度：-1空闲 -2失败 0~99下载中 100写入Flash（/otastatus 返回）
bool          otaAutoDone      = false;   ///< 本次上电是否已触发过自动升级（防止循环刷机）

// --- Web 服务器 ---
AsyncWebServer server(80);

// --- 红外接收 ---
IRrecv        irrecv(kRecvPin);
decode_results results;

// --- 单色渐入动画 ---
bool   modelbianhua  = true;
String solidAnimMode = "fade"; ///< 单色动画模式: fade(默认=全局渐亮)/random/sequential/center/ends/none

// 随机模式可用灯效列表（14种，字符串字面量自动存 Flash）
static const char* const modeList[] = {
  "rainbow","solid","theater","confetti","breathing",
  "sparkle","aurora","disco","randomjump","step",
  "sparksparks","rainbowstring","heartbeat","firework"
};

// ============================================================
// HTML 网页（存于 Flash，节省 RAM）
// ============================================================
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html lang="zh"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>LAND 灯光系统</title>
<link rel="icon" type="image/svg+xml" href="data:image/svg+xml;base64,PHN2ZyB4bWxucz0naHR0cDovL3d3dy53My5vcmcvMjAwMC9zdmcnIHZpZXdCb3g9JzAgMCAzMiAzMic+PGRlZnM+PGxpbmVhckdyYWRpZW50IGlkPSdnJyB4MT0nLjEnIHkxPScwJyB4Mj0nLjcnIHkyPScxJz48c3RvcCBvZmZzZXQ9JzAnIHN0b3AtY29sb3I9JyNGRkU2MDAnLz48c3RvcCBvZmZzZXQ9Jy42Micgc3RvcC1jb2xvcj0nI0ZGOUEwMCcvPjxzdG9wIG9mZnNldD0nMScgc3RvcC1jb2xvcj0nI0ZGNEQ4RCcvPjwvbGluZWFyR3JhZGllbnQ+PC9kZWZzPjxyZWN0IHdpZHRoPSczMicgaGVpZ2h0PSczMicgcng9JzcuNScgZmlsbD0nIzFCMUIyMicvPjxjaXJjbGUgY3g9JzE2JyBjeT0nMTIuNicgcj0nOC42JyBmaWxsPScjRkZFNjAwJyBvcGFjaXR5PScuMTMnLz48Y2lyY2xlIGN4PScxNicgY3k9JzEyLjYnIHI9JzYuNCcgZmlsbD0ndXJsKCNnKScvPjxyZWN0IHg9JzEyLjYnIHk9JzE4LjQnIHdpZHRoPSc2LjgnIGhlaWdodD0nMi40JyByeD0nMS4yJyBmaWxsPScjRkZDNDAwJy8+PHJlY3QgeD0nMTMuNycgeT0nMjIuMicgd2lkdGg9JzQuNicgaGVpZ2h0PScyLjYnIHJ4PScxLjMnIGZpbGw9JyNGRjlBMDAnLz48L3N2Zz4=">
<link rel="alternate icon" href="/favicon.ico">
<link rel="apple-touch-icon" href="data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAALQAAAC0CAMAAAAKE/YAAAAAflBMVEX96AABAQL+qSb+zQ3/j07/bm0mJSAhICD/VYYhICH92AVlXReckRD+1gn/kFDEeQj4tCXftReiZw1LNhr/eWL/i1JAPB3/bmz/tChuSRX/pkaJfRS3mwxvbxVsXRYaGyIAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAD2C2s4AAAAIHRSTlMgAf7+/v6e2f1jW1I1o2H/jy7//2umeJlX/zs9fDCo/kvwNHAAAAUqSURBVHja7d3peqsgEAZgqg0EURuTNOnu/d/lSdNNUFyYGWSek/ndNu9DP4gLi7ibLC2lUm2UUkpKPS0SU+BIXIs+BRej4na1GnX70SuKv92L0auTx9giXbKfPYTWbUKl56Flm1TJGWjdJld6Cp2gua8WSUfDExHBweyqLbRqky3lQ8s24ZLD6KTNllpwMXfVIumxzjPy/aJbBuWiJQe0tNG6ZVHaQrdMqouWXNCygya4qf4q9Nv4PzReQ1+0pmxEp5rSSMRbevmLRruDNsJTBu3O/geN8vfUqRSjVZ4UVlMLnIaWRswoI3GaWmCM0bIUM6uEs/UVLaORUdjyigZm2YiFZRQ0HwKYDi0CCviRFzTk36VeRVC9KlA+BOTGUIrgAjSVuqDDf9sIQBlAqAH5GjUX16JR62C0d9QoqirvVFUV2KOIDg2X8onzgfK5VWhXUnjmYfGYO/TDFVaei3yiCqxcK4FlrvLJqrDUAsc8gzzMNrHQ/e+UfHahfMsIhE5Y5AuqQOiMAegSYu6ryxhoHRZnf7A1PVpBzX21IkcbsLmnNtRoCcqzJ9eSGF1imF11SYuWoePz+HgtSdElPNBDsS4p0RInHP2ASEK0QQpHLyCGDq2ccGSAcgKiyNAnKxwZsKyAnMjQVjfc5TBzvgvuiiK4G2bgCu6KwWhoQ7tNTYU2uA1tN7WhQSvUbtjriooEbacDAx2aj1B0lmHngwZtaNGGBN0dpZ9w0E9hI/UCdIMdaTvUDQVaCVr0guEjEJ3dY5jvM2q0pEZLavQ9Ut3QN/QNfUOniVbUaPJvxEcc8yM12rpgqnHQNfUFk31pioOmvzQ1+KGmvwmQ6KF+jHyPWGNHmghtDR8NBrqhf4Rgh/rxfgMsOx1ED2vcfIDRdfRneWIDrigPIO1HvdB82Okge9RrP1QXqA1N91Ddfn1Rw8x1pNcXzosixIYmfFHkdMUar6EpX8k5Lz/P4eZzvJef7mtmpHAQv2Z2mvol1PwS84W+29Q1RqDJp064k1SOIeZj5EkqvelAR7A5wnSg3sSrI9QcYeJVf4pbvdnOF2/dPMeZ4tafTPgyX711xo1okwkHpm2et9vNdrIuP3Jea9rm0ATZ+ps10safVa83QXZIfd7OqM2aU5EH1FfT2E3sZqilAxczBKLdzthcRNn4nKXswj7COyEA7aqbSfOlNi46dPlFKNpZMtLMMOe5jQ5feBaMdnL9tHgGIWBJEQBtqaulc/EAZhC6+y2zdNojaCklCN1Z2rcM/QpbRwlD/13zLUJDV/VC0T+jyIJMG/BSbDD6e2FwMXf0QFjPjIH+YlezGrpEWViPgr4udq+m29kg7S2AhL5kW+/GzbsT2jYOaOjP5v54Kzz19oG5hQgmur3uOWnerZvI8t2g736JjP6Sdzclodj6iwJNXjf0DR2/I3aKBfr5wZnnuFepow9D0zOf00YfhieVPieN9k2FVQmjn33ofcLohyxKU6OilX+q9OGG/u/R7Z5hpn3DdNqjh7epUx6nfak+tCmjWzXQ1uhXTPhXeYf9g1X7A/pH8LwJUPzMiida8kNLofmhNU/0HT80bDvFlfohcOPKdfoheIvQNSIN34x1hUhjbHsbPR04GwxHTgfWVs5R04G3aXbEdKBuTx6robE3go/S0DRb7hM3NOfDDVgeI8HzwA6WR6PwPISG53E/LA9W4nmEFc/Dwngey8bzADyeRw3yPNSR5/GZPA8qZXokLM/Dd5kec8z0QGmmR3czPSQ92ePo/wEbOTVAlMXVnwAAAABJRU5ErkJggg==">
<meta name="theme-color" content="#1B1B22">
<style> .skinoff{display:none!important}.pb.accordion-body{overflow:hidden}.pb.accordion-body:not(.open){max-height:0!important;opacity:0!important}.p:not(.open) .ar{transform:rotate(-90deg)}.skbar{display:grid!important;grid-template-columns:repeat(auto-fit,minmax(106px,1fr));gap:9px}.skt{display:flex;flex-direction:column;gap:6px;padding:8px;border-radius:12px;cursor:pointer;border:1.5px solid rgba(128,128,128,.42);background:rgba(128,128,128,.1);color:inherit;font:inherit;text-align:left;opacity:.72;transition:transform .18s,opacity .18s,border-color .18s,box-shadow .18s}.skt:hover{opacity:1;transform:translateY(-2px);border-color:currentColor}.skt.on{opacity:1;border-color:currentColor;box-shadow:0 0 0 2px rgba(128,128,128,.45)}.skt .pv{display:block;border-radius:7px;padding:6px;min-height:44px;box-shadow:inset 0 0 0 1px rgba(0,0,0,.14)}.skt .pvb{display:block;height:6px;border-radius:3px;margin-bottom:5px;opacity:.9}.skt .pvr{display:block;height:4px;border-radius:3px;margin-bottom:3px;opacity:.75}.skt .pvr:last-child{margin-bottom:0;width:62%}.skt .pvs{display:flex;gap:3px;margin-top:6px}.skt .pvs i{width:12px;height:5px;border-radius:2px;display:block}.skt .nm{font-size:12.5px;font-weight:650;line-height:1.3;display:flex;align-items:center;gap:6px;padding-left:5px}.skt .nm b{font-size:9.5px;font-weight:600;padding:1px 6px;border-radius:20px;border:1px solid currentColor;opacity:.7}.skt .ds{font-size:10.5px;opacity:.6;line-height:1.45;padding-left:5px}.sktip{font-size:11px;opacity:.6;margin-top:10px;line-height:1.65}#wifiSavedList .ssid-hint{font-size:11.5px;opacity:.62;margin:2px 0 0}.saved-list{margin:4px 0 0}.saved-row{display:flex;align-items:center;gap:6px;flex-wrap:wrap;padding:7px 9px;margin:5px 0;border-radius:9px;background:rgba(128,128,128,.12);border:1.5px solid rgba(128,128,128,.3);color:inherit}.saved-row.cur{border-color:#4f6ef7;background:rgba(79,110,247,.16)}.saved-pri{flex:0 0 auto;font-size:11px;font-weight:700;width:19px;height:19px;line-height:19px;text-align:center;border-radius:50%;background:rgba(128,128,128,.3);font-variant-numeric:tabular-nums}.saved-row.cur .saved-pri{background:#4f6ef7;color:#fff}.saved-ssid{flex:1 1 70px;min-width:0;overflow:hidden;text-overflow:ellipsis;white-space:nowrap;font-size:12.5px;font-weight:600}.saved-ssid.ssid-dim{opacity:.5;font-weight:400}.saved-fail{flex:0 0 auto;font-size:10.5px;padding:1px 6px;border-radius:9px;background:rgba(128,128,128,.22);border:1px solid rgba(128,128,128,.32);cursor:help}.saved-badge{flex:0 0 auto;font-size:10.5px;padding:1px 7px;border-radius:9px;background:#4f6ef7;color:#fff}.mini-btn{flex:0 0 auto;font:inherit;font-size:11.5px;line-height:1.5;padding:2px 7px;border-radius:6px;border:1.5px solid rgba(128,128,128,.42);background:rgba(128,128,128,.16);color:inherit;cursor:pointer;transition:background .2s,border-color .2s}.mini-btn:hover{background:rgba(128,128,128,.3);border-color:currentColor}.mini-btn:disabled{opacity:.32;cursor:default}.mini-btn.del{border-color:rgba(217,72,59,.6);color:#d9483b;background:rgba(217,72,59,.1)}.mini-btn.del:hover{background:rgba(217,72,59,.28);color:#fff}.saved-pw{flex:1 1 auto;min-width:0;font:inherit;font-size:13px;padding:5px 9px;border-radius:7px;border:1.5px solid rgba(128,128,128,.42);background:rgba(128,128,128,.1);color:inherit}[data-mode="off"]{grid-column:1/-1!important}.saved-row{gap:5px;padding:6px 8px}.saved-ssid{flex:1 1 40px}.saved-pri{width:18px;height:18px;line-height:18px}.mini-btn{padding:2px 6px;font-size:11px;line-height:1.45}.saved-badge,.saved-fail{padding:1px 6px;font-size:10px}.mstamp{font-weight:400;opacity:.7}body[data-skin] .ws,body[data-skin] .cs{margin-bottom:9px}body[data-skin] .hint{margin-bottom:8px}body[data-skin] #singleControl{margin-bottom:10px}body[data-skin] .chk{cursor:pointer}body[data-skin] .chk:not(.on) .bx::after{display:none}body[data-skin] .chk:not(.on) .bx{opacity:.5}#colorControl{display:block}
/* 固定滚动条占位：否则展开面板→出现滚动条→可用宽度变小→列数跳变→面板乱跳 */
html{scrollbar-gutter:stable}
/* 全局重置（5 套皮肤本来各有一份 *{...}，这里只留一份且不加皮肤前缀，避免特异性打架） */
*,*::before,*::after{box-sizing:border-box;margin:0;padding:0}
/* 瀑布流容器：用 !important 压过各皮肤自己的 .grid 规则（它们写的是 display:grid） */
.grid{display:flex!important;gap:14px;align-items:flex-start!important}
.mcol{flex:1 1 0;min-width:0;display:flex;flex-direction:column;gap:14px}
.mcol section.p{width:100%}
.msd .p{animation:none!important}
body.sk0{--k:#000;--y:#FFE600;--p:#FF4D8D;--c:#00D9FF;--o:#FF7A00;--cr:#FFF3D6;--pa:#FFF9E8;--g:#8CC63F}body.sk0{background:var(--cr) radial-gradient(circle at 1px 1px,rgba(0,0,0,.07) 1px,transparent 0) 0 0/14px 14px;color:var(--k);font-family:"Arial Black",Impact,"Helvetica Neue",system-ui,sans-serif;-webkit-text-size-adjust:100%;padding:14px 12px 40px;font-weight:900}body.sk0 .top{border:4px solid var(--k);box-shadow:8px 8px 0 var(--k);background:var(--y);padding:14px 16px;margin-bottom:18px;animation:pop_sk0 .45s cubic-bezier(.34,1.56,.64,1)}@keyframes pop_sk0{from{opacity:0;transform:translateY(-16px) rotate(-1.2deg)}}body.sk0 .band{display:flex;height:9px;border-bottom:4px solid var(--k);margin-bottom:12px}body.sk0 .band i{flex:1}body.sk0 .band i:nth-child(1){background:var(--y)}body.sk0 .band i:nth-child(2){background:var(--p)}body.sk0 .band i:nth-child(3){background:var(--c);flex:1.6}body.sk0 .band i:nth-child(4){background:var(--g)}body.sk0 .band i:nth-child(5){background:var(--o)}body.sk0 h1{font-size:clamp(22px,4.6vw,34px);letter-spacing:1px;line-height:1.15}body.sk0 h1 em{font-style:normal;background:var(--p);color:#fff;padding:0 8px;box-shadow:4px 4px 0 var(--k);display:inline-block}body.sk0 #status{margin-top:12px;border:3px solid var(--k);box-shadow:5px 5px 0 var(--c);background:#fff;padding:8px 11px;font-size:12.5px;display:flex;gap:8px;align-items:center}body.sk0 #status .d{width:12px;height:12px;background:var(--g);flex:0 0 12px;animation:bk_sk0 .9s steps(1) infinite}@keyframes bk_sk0{50%{background:var(--p)}}body.sk0 .grid{display:flex;gap:16px;align-items:flex-start}body.sk0 .mcol{flex:1 1 0;min-width:0;display:flex;flex-direction:column;gap:16px}body.sk0 .mcol .p{width:100%}body.sk0 .grid.msd .p{animation:none!important}body.sk0 .p{border:4px solid var(--k);box-shadow:8px 8px 0 var(--k);background:var(--pa);animation:up_sk0 .5s backwards;transition:box-shadow .18s,transform .18s}body.sk0 .p:nth-child(1){animation-delay:.05s}body.sk0 .p:nth-child(2){animation-delay:.1s}body.sk0 .p:nth-child(3){animation-delay:.15s}body.sk0 .p:nth-child(4){animation-delay:.2s}body.sk0 .p:nth-child(5){animation-delay:.25s}body.sk0 .p:nth-child(6){animation-delay:.3s}body.sk0 .p:nth-child(7){animation-delay:.35s}@keyframes up_sk0{from{opacity:0;transform:translateY(22px)}}body.sk0 .ph{display:flex;align-items:center;gap:9px;padding:12px 13px;cursor:pointer;user-select:none;border-bottom:4px solid var(--k);background:var(--y);transition:background .2s}body.sk0 .ph:hover{background:var(--o)}body.sk0 .ph:active{transform:translate(2px,2px)}body.sk0 .ar{flex:none;width:16px;text-align:center;font-size:11px;transition:transform .28s steps(4)}body.sk0 .p.cl .ar{transform:rotate(-90deg)}body.sk0 .ti{flex:1;font-size:14px;letter-spacing:.5px}body.sk0 .tg{font-size:10.5px;background:var(--k);color:var(--y);padding:3px 8px;flex:none;max-width:44%;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}body.sk0 .tg.i{background:#fff;color:#666}body.sk0 .tg.on{background:var(--g);color:var(--k)}body.sk0 .pb{overflow:hidden;max-height:1600px;transition:max-height .32s cubic-bezier(.4,0,.6,1),opacity .25s}body.sk0 .p.cl .pb{max-height:0;opacity:0}body.sk0 .pb>.in{padding:13px}body.sk0 .st{font-size:11px;font-weight:900;background:var(--c);border:3px solid var(--k);padding:4px 8px;display:inline-block;margin-bottom:10px}body.sk0 .mg{display:grid;grid-template-columns:repeat(auto-fit,minmax(78px,1fr));gap:8px}body.sk0 .mode-btn,body.sk0 .anim-btn{position:relative;overflow:hidden;border:3px solid var(--k);box-shadow:4px 4px 0 var(--k);padding:11px 4px;font:inherit;font-size:11px;cursor:pointer;background:#fff;transition:transform .09s,box-shadow .09s,filter .2s}body.sk0 .mode-btn::after,body.sk0 .anim-btn::after,body.sk0 .btn::after{content:'';position:absolute;top:-40%;left:-60%;width:40%;height:180%;background:linear-gradient(90deg,transparent,rgba(255,255,255,.7),transparent);transform:rotate(18deg);animation:sw_sk0 2.8s linear infinite}@keyframes sw_sk0{0%{left:-60%}52%,100%{left:150%}}body.sk0 .mode-btn:active,body.sk0 .anim-btn:active,body.sk0 .btn:active{transform:translate(4px,4px);box-shadow:0 0 0 var(--k)}body.sk0 .mode-btn.active,body.sk0 .anim-btn.active{box-shadow:0 0 0 var(--k);transform:translate(4px,4px);filter:saturate(1.5)}body.sk0 .mode-btn.active::before,body.sk0 .anim-btn.active::before{content:'●';position:absolute;top:1px;right:3px;font-size:8px}body.sk0 .mode-btn:nth-child(6n+1){background:var(--y)}body.sk0 .mode-btn:nth-child(6n+2){background:var(--p);color:#fff}body.sk0 .mode-btn:nth-child(6n+3){background:var(--c)}body.sk0 .mode-btn:nth-child(6n+4){background:var(--g)}body.sk0 .mode-btn:nth-child(6n+5){background:var(--o)}body.sk0 .mode-btn:nth-child(6n+6){background:#C9A7FF}body.sk0 .anim-btn{background:#fff}body.sk0 .row{display:flex;align-items:center;gap:8px;margin-bottom:10px;flex-wrap:wrap}body.sk0 .lb{font-size:11.5px;min-width:38px}body.sk0 input[type=range]{-webkit-appearance:none;appearance:none;flex:1;min-width:110px;height:20px;background:transparent;cursor:pointer}body.sk0 input[type=range]::-webkit-slider-runnable-track{height:12px;background:#fff;border:3px solid var(--k);box-shadow:2px 2px 0 var(--k)}body.sk0 input[type=range]::-webkit-slider-thumb{-webkit-appearance:none;width:20px;height:24px;margin-top:-9px;background:var(--p);border:3px solid var(--k);box-shadow:2px 2px 0 var(--k)}body.sk0 input[type=range]::-moz-range-track{height:12px;background:#fff;border:3px solid var(--k)}body.sk0 input[type=range]::-moz-range-thumb{width:18px;height:22px;background:var(--p);border:3px solid var(--k);border-radius:0}body.sk0 .rv{font-size:13px;background:var(--k);color:var(--y);padding:2px 7px;min-width:46px;text-align:center}body.sk0 input[type=text],body.sk0 input[type=password],body.sk0 input[type=number],body.sk0 select{font:inherit;font-size:12px;border:3px solid var(--k);background:#fff;padding:7px 9px;box-shadow:3px 3px 0 var(--k);outline:0;color:var(--k)}body.sk0 input[type=text]:focus,body.sk0 input[type=password]:focus,body.sk0 input[type=number]:focus,body.sk0 select:focus{background:var(--y)}body.sk0 select{flex:1;min-width:120px}body.sk0 input[type=color]{width:52px;height:36px;padding:0;border:3px solid var(--k);box-shadow:3px 3px 0 var(--k);background:#fff;cursor:pointer}body.sk0 input[type=color]::-webkit-color-swatch-wrapper{padding:3px}body.sk0 input[type=color]::-webkit-color-swatch{border:0}body.sk0 .btn{position:relative;overflow:hidden;border:3px solid var(--k);box-shadow:4px 4px 0 var(--k);background:var(--c);padding:9px 14px;font:inherit;font-size:12px;cursor:pointer;transition:transform .09s,box-shadow .09s}body.sk0 .btn.g{background:var(--g)}body.sk0 .btn.r{background:var(--p);color:#fff}body.sk0 .sw{display:inline-flex;align-items:center;gap:7px;cursor:pointer;font-size:11.5px}body.sk0 .sw input{display:none}body.sk0 .tr{width:46px;height:24px;border:3px solid var(--k);background:#fff;position:relative;flex:none;transition:background .2s}body.sk0 .tr::after{content:'';position:absolute;top:2px;left:2px;width:16px;height:14px;background:var(--k);transition:transform .22s cubic-bezier(.34,1.56,.64,1),background .2s}body.sk0 .sw input:checked+.tr{background:var(--g)}body.sk0 .sw input:checked+.tr::after{transform:translateX(20px);background:var(--p)}body.sk0 .chk{display:inline-flex;align-items:center;gap:7px;cursor:pointer;font-size:11.5px;border:3px solid var(--k);background:#fff;padding:5px 9px;box-shadow:3px 3px 0 var(--k);user-select:none}body.sk0 .bx{width:16px;height:16px;border:3px solid var(--k);background:#fff;flex:none;position:relative}body.sk0 .chk.on .bx::after{content:'';position:absolute;inset:1px;background:var(--p)}body.sk0 .chk.on{background:var(--y)}body.sk0 .lst{border:3px solid var(--k);background:#fff}body.sk0 .it{padding:8px 9px;border-bottom:3px solid var(--k);display:flex;align-items:center;gap:6px;flex-wrap:wrap;animation:jin_sk0 .4s backwards}body.sk0 .it:nth-child(1){animation-delay:.1s}body.sk0 .it:nth-child(2){animation-delay:.2s}body.sk0 .it:nth-child(3){animation-delay:.3s}@keyframes jin_sk0{from{opacity:0;transform:translateX(-14px)}}body.sk0 .it:last-child{border-bottom:0}body.sk0 .it.cur{background:var(--y)}body.sk0 .it.cur:hover{background:var(--y)}body.sk0 .nm{flex:1;min-width:74px;font-size:11.5px;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}body.sk0 .mb{border:3px solid var(--k);background:#fff;padding:3px 6px;font:inherit;font-size:10px;cursor:pointer}body.sk0 .mb:active{background:var(--p);color:#fff}body.sk0 .mb.d{background:var(--p);color:#fff}body.sk0 .hint{font-size:10px;font-weight:900;line-height:1.65;margin-top:9px;color:#444}body.sk0 .ws{font-size:11.5px;border:3px solid var(--k);background:#fff;padding:7px 9px;box-shadow:3px 3px 0 var(--k);line-height:1.7;word-break:break-all}body.sk0 .cs{font-size:11px;min-height:16px;margin-top:8px;font-weight:900}body.sk0 #singleControl{display:none}@media(max-width:520px){body.sk0 .mg{grid-template-columns:repeat(3,1fr)}}body.sk1{--bg:#FDF3F8;--c1:#FFD9E8;--c2:#D6F0FF;--c3:#FFF2C9;--c4:#DFF5E1;--c5:#E8DBFF;--tx:#6B5A7A;--pk:#FF8FB1;--bl:#7EC8F5;--yw:#FFC94A;--gn:#8FD9A0;--pu:#B49BE8;--wh:#fff}body.sk1{background:var(--bg);color:var(--tx);font-family:"Trebuchet MS","Comic Sans MS",system-ui,sans-serif;-webkit-text-size-adjust:100%;padding:12px 10px 34px;position:relative;font-weight:700}body.sk1::before{content:'';position:fixed;inset:0;z-index:0;pointer-events:none;background:radial-gradient(38% 34% at 12% 14%,var(--c1),transparent 62%),radial-gradient(36% 32% at 88% 20%,var(--c2),transparent 62%),radial-gradient(34% 30% at 78% 78%,var(--c3),transparent 62%),radial-gradient(36% 34% at 18% 82%,var(--c4),transparent 62%),radial-gradient(30% 28% at 50% 52%,var(--c5),transparent 62%);opacity:.75}body.sk1 .wrap{max-width:1900px;margin:0 auto;position:relative;z-index:1}body.sk1 .top{background:var(--wh);border-radius:26px;padding:12px 16px;margin-bottom:12px;box-shadow:0 14px 34px -10px rgba(120,90,140,.28),inset 0 -4px 0 rgba(0,0,0,.05),inset 0 4px 0 rgba(255,255,255,.9);animation:pop_sk1 .5s cubic-bezier(.34,1.56,.64,1)}@keyframes pop_sk1{from{opacity:0;transform:translateY(-14px) scale(.97)}}body.sk1 h1{font-size:clamp(20px,3.6vw,27px);color:#7A5F8F;letter-spacing:1px}body.sk1 h1 em{font-style:normal;color:var(--pk)}body.sk1 #status{margin-top:8px;background:var(--c2);border-radius:18px;padding:7px 12px;font-size:11.5px;color:#4E7A96;box-shadow:inset 0 -3px 0 rgba(0,0,0,.05),inset 0 3px 0 rgba(255,255,255,.8);display:flex;gap:8px;align-items:center}body.sk1 #status .d{width:10px;height:10px;border-radius:50%;background:var(--gn);flex:0 0 10px;animation:bb_sk1 1.1s ease-in-out infinite}@keyframes bb_sk1{50%{transform:scale(.65);opacity:.5}}body.sk1 .grid{display:flex;gap:12px;align-items:flex-start}body.sk1 .mcol{flex:1 1 0;min-width:0;display:flex;flex-direction:column;gap:12px}body.sk1 .mcol .p{width:100%}body.sk1 .grid.msd .p{animation:none!important}body.sk1 .p{background:var(--wh);border-radius:26px;overflow:hidden;box-shadow:0 16px 38px -12px rgba(120,90,140,.25),inset 0 -4px 0 rgba(0,0,0,.05),inset 0 4px 0 rgba(255,255,255,.9);animation:up_sk1 .5s cubic-bezier(.34,1.56,.64,1) backwards}body.sk1 .p:nth-child(1){animation-delay:.04s}body.sk1 .p:nth-child(2){animation-delay:.08s}body.sk1 .p:nth-child(3){animation-delay:.12s}body.sk1 .p:nth-child(4){animation-delay:.16s}body.sk1 .p:nth-child(5){animation-delay:.2s}body.sk1 .p:nth-child(6){animation-delay:.24s}body.sk1 .p:nth-child(7){animation-delay:.28s}@keyframes up_sk1{from{opacity:0;transform:translateY(20px) scale(.96)}}body.sk1 .ph{display:flex;align-items:center;gap:8px;padding:10px 13px;cursor:pointer;user-select:none;background:linear-gradient(135deg,var(--c1),var(--c5));transition:filter .2s}body.sk1 .ph:hover{filter:brightness(1.05)}body.sk1 .ar{flex:none;font-size:9px;color:#8C6FA6;transition:transform .34s cubic-bezier(.34,1.56,.64,1)}body.sk1 .p.cl .ar{transform:rotate(-90deg)}body.sk1 .ti{flex:1;font-size:13px;color:#6B4F82}body.sk1 .tg{font-size:10px;background:var(--wh);color:#8C6FA6;border-radius:14px;padding:3px 9px;flex:none;max-width:42%;overflow:hidden;text-overflow:ellipsis;white-space:nowrap;box-shadow:0 3px 8px -2px rgba(120,90,140,.22)}body.sk1 .tg.i{background:#F1EBF5;color:#A293B0}body.sk1 .tg.on{background:var(--c3);color:#95772B}body.sk1 .pb{max-height:1700px;overflow:hidden;transition:max-height .42s cubic-bezier(.34,1.4,.64,1),opacity .3s}body.sk1 .p.cl .pb{max-height:0;opacity:0}body.sk1 .in{padding:11px 13px 13px}body.sk1 .st{font-size:10.5px;color:#A08CB2;margin-bottom:7px;letter-spacing:.5px}body.sk1 .mg{display:grid;grid-template-columns:repeat(auto-fit,minmax(74px,1fr));gap:7px}body.sk1 .mode-btn,body.sk1 .anim-btn{position:relative;overflow:hidden;font:inherit;font-size:10.5px;font-weight:700;padding:9px 3px;border:0;border-radius:16px;cursor:pointer;color:#7A6288;background:var(--wh);box-shadow:0 6px 16px -6px rgba(120,90,140,.3),inset 0 -3px 0 rgba(0,0,0,.06),inset 0 3px 0 rgba(255,255,255,.9);transition:transform .18s cubic-bezier(.34,1.56,.64,1),box-shadow .18s}body.sk1 .mode-btn::after,body.sk1 .anim-btn::after,body.sk1 .btn::after{content:'';position:absolute;top:0;left:-60%;width:40%;height:100%;background:linear-gradient(90deg,transparent,rgba(255,255,255,.65),transparent);animation:sw_sk1 2.8s ease-in-out infinite}@keyframes sw_sk1{0%{left:-60%}55%,100%{left:160%}}body.sk1 .mode-btn:active,body.sk1 .anim-btn:active,body.sk1 .btn:active{transform:translateY(3px) scale(.97);box-shadow:inset 0 4px 0 rgba(0,0,0,.12),inset 0 -2px 0 rgba(255,255,255,.6)}body.sk1 .mode-btn.active,body.sk1 .anim-btn.active{color:#fff;background:linear-gradient(135deg,var(--pk),#FFB3CB);transform:translateY(2px);box-shadow:0 3px 10px -3px rgba(255,143,177,.6),inset 0 -3px 0 rgba(0,0,0,.1)}body.sk1 .mode-btn:nth-child(6n+1){background:var(--c2);color:#3E6F8C}body.sk1 .mode-btn:nth-child(6n+2){background:var(--c3);color:#8A6C1C}body.sk1 .mode-btn:nth-child(6n+3){background:var(--c4);color:#4A7D5A}body.sk1 .mode-btn:nth-child(6n+4){background:var(--c5);color:#6A55A0}body.sk1 .mode-btn:nth-child(6n+5){background:#FFE0D0;color:#96603A}body.sk1 .mode-btn:nth-child(6n+6){background:#E6F7F2;color:#3E7C6C}body.sk1 .anim-btn{background:var(--wh)}body.sk1 .row{display:flex;align-items:center;gap:8px;margin-bottom:8px;flex-wrap:wrap}body.sk1 .lb{font-size:10.5px;color:#A08CB2;min-width:32px}body.sk1 input[type=range]{-webkit-appearance:none;appearance:none;flex:1;min-width:96px;height:20px;background:transparent;cursor:pointer}body.sk1 input[type=range]::-webkit-slider-runnable-track{height:12px;border-radius:10px;background:#F0E8F5;box-shadow:inset 0 2px 4px rgba(120,90,140,.18)}body.sk1 input[type=range]::-webkit-slider-thumb{-webkit-appearance:none;width:22px;height:22px;margin-top:-5px;border-radius:50%;background:var(--pk);border:0;box-shadow:0 4px 10px -2px rgba(255,143,177,.7),inset 0 -3px 0 rgba(0,0,0,.12),inset 0 3px 0 rgba(255,255,255,.6)}body.sk1 input[type=range]::-moz-range-track{height:12px;border-radius:10px;background:#F0E8F5}body.sk1 input[type=range]::-moz-range-thumb{width:20px;height:20px;border-radius:50%;background:var(--pk);border:0;box-shadow:0 4px 10px -2px rgba(255,143,177,.7)}body.sk1 .rv{font-size:11.5px;background:var(--c3);color:#95772B;border-radius:12px;padding:2px 8px;min-width:42px;text-align:center;font-variant-numeric:tabular-nums}body.sk1 input[type=text],body.sk1 input[type=password],body.sk1 input[type=number],body.sk1 select{font:inherit;font-size:11.5px;font-weight:700;color:var(--tx);background:#FBF6FE;border:0;border-radius:14px;padding:8px 11px;outline:0;box-shadow:inset 0 2px 5px rgba(120,90,140,.14),inset 0 -2px 0 rgba(255,255,255,.9)}body.sk1 input:focus,body.sk1 select:focus{box-shadow:inset 0 2px 5px rgba(255,143,177,.35),0 0 0 3px rgba(255,143,177,.25)}body.sk1 select{flex:1;min-width:106px}body.sk1 input[type=color]{width:46px;height:32px;padding:0;border:0;border-radius:12px;background:#fff;cursor:pointer;box-shadow:0 4px 10px -3px rgba(120,90,140,.3)}body.sk1 input[type=color]::-webkit-color-swatch-wrapper{padding:3px}body.sk1 input[type=color]::-webkit-color-swatch{border:0;border-radius:9px}body.sk1 .btn{position:relative;overflow:hidden;font:inherit;font-size:11.5px;font-weight:700;border:0;border-radius:18px;padding:9px 15px;cursor:pointer;color:#fff;background:linear-gradient(135deg,var(--bl),#A5DBFB);box-shadow:0 6px 16px -6px rgba(126,200,245,.7),inset 0 -3px 0 rgba(0,0,0,.1),inset 0 3px 0 rgba(255,255,255,.45);transition:transform .18s cubic-bezier(.34,1.56,.64,1),box-shadow .18s}body.sk1 .btn.g{background:linear-gradient(135deg,var(--gn),#B4E8C2);box-shadow:0 6px 16px -6px rgba(143,217,160,.7),inset 0 -3px 0 rgba(0,0,0,.1),inset 0 3px 0 rgba(255,255,255,.45)}body.sk1 .btn.r{background:linear-gradient(135deg,#FF7B7B,#FFAFAF);box-shadow:0 6px 16px -6px rgba(255,123,123,.7),inset 0 -3px 0 rgba(0,0,0,.1),inset 0 3px 0 rgba(255,255,255,.45)}body.sk1 .sw{display:inline-flex;align-items:center;gap:7px;cursor:pointer;font-size:10.5px}body.sk1 .sw input{display:none}body.sk1 .tr{width:44px;height:24px;border-radius:20px;background:#F0E8F5;position:relative;flex:none;box-shadow:inset 0 2px 5px rgba(120,90,140,.2);transition:background .3s}body.sk1 .tr::after{content:'';position:absolute;top:2px;left:2px;width:20px;height:20px;border-radius:50%;background:#fff;box-shadow:0 3px 7px -2px rgba(120,90,140,.4);transition:transform .34s cubic-bezier(.34,1.56,.64,1),background .3s}body.sk1 .sw input:checked+.tr{background:linear-gradient(135deg,var(--gn),#B4E8C2)}body.sk1 .sw input:checked+.tr::after{transform:translateX(20px);background:#fff}body.sk1 .chk{display:inline-flex;align-items:center;gap:6px;cursor:pointer;font-size:10.5px;color:#A08CB2;background:#FBF6FE;border-radius:16px;padding:5px 11px;user-select:none;box-shadow:inset 0 2px 4px rgba(120,90,140,.12)}body.sk1 .bx{width:13px;height:13px;border-radius:50%;background:#EDE4F3;flex:none;position:relative;transition:background .25s}body.sk1 .chk.on .bx{background:var(--gn)}body.sk1 .chk.on .bx::after{content:'';position:absolute;inset:3px;border-radius:50%;background:#fff}body.sk1 .chk.on{color:#4A7D5A;background:var(--c4)}body.sk1 .lst{background:#FBF6FE;border-radius:18px;padding:4px;box-shadow:inset 0 2px 5px rgba(120,90,140,.1)}body.sk1 .it{padding:7px 9px;border-radius:14px;display:flex;align-items:center;gap:5px;flex-wrap:wrap;animation:jin_sk1 .42s cubic-bezier(.34,1.56,.64,1) backwards}body.sk1 .it:nth-child(1){animation-delay:.08s}body.sk1 .it:nth-child(2){animation-delay:.18s}body.sk1 .it:nth-child(3){animation-delay:.28s}@keyframes jin_sk1{from{opacity:0;transform:translateY(12px) scale(.94)}}body.sk1 .it.cur{background:var(--c3);box-shadow:0 3px 8px -3px rgba(255,201,74,.5)}body.sk1 .nm{flex:1;min-width:66px;font-size:10.5px;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}body.sk1 .mb{font:inherit;font-size:9.5px;font-weight:700;border:0;border-radius:11px;padding:4px 7px;cursor:pointer;color:#7A6288;background:#fff;box-shadow:0 3px 7px -3px rgba(120,90,140,.35)}body.sk1 .mb:active{transform:translateY(2px)}body.sk1 .mb.d{color:#fff;background:#FF9B9B}body.sk1 .hint{font-size:9.5px;line-height:1.6;margin-top:7px;color:#B0A0BE;font-weight:700}body.sk1 .ws{font-size:10.5px;background:var(--c2);border-radius:14px;padding:7px 10px;line-height:1.75;word-break:break-all;color:#4E7A96}body.sk1 .cs{font-size:10px;min-height:14px;margin-top:6px;color:#95772B}body.sk1 #singleControl{display:none}@media(max-width:520px){body.sk1 .mg{grid-template-columns:repeat(3,1fr)}}body.sk2{--bg:#05070A;--pn:#0A0F15;--g:#00FF9C;--a:#FFB000;--m:#FF2E88;--w:#E6F2EE;--d:#5C7480;--ln:#16222D;--mono:"SF Mono",Consolas,"Courier New",monospace}body.sk2{background:var(--bg);color:var(--w);font-family:var(--mono);-webkit-text-size-adjust:100%;padding:12px 10px 34px;font-size:12px;position:relative;overflow-x:hidden}body.sk2::before{content:'';position:fixed;inset:0;z-index:9;pointer-events:none;background:repeating-linear-gradient(0deg,rgba(0,0,0,.28) 0 1px,transparent 1px 3px);mix-blend-mode:multiply}body.sk2::after{content:'';position:fixed;inset:0;z-index:8;pointer-events:none;background:radial-gradient(120% 90% at 50% 50%,transparent 55%,rgba(0,0,0,.6) 100%)}body.sk2 .wrap{max-width:1900px;margin:0 auto;position:relative;z-index:1}body.sk2 .top{border:1px solid var(--ln);background:linear-gradient(180deg,#0B1218,#070B0F);padding:12px 14px;margin-bottom:14px;position:relative;clip-path:polygon(0 0,calc(100% - 14px) 0,100% 14px,100% 100%,14px 100%,0 calc(100% - 14px));animation:boot_sk2 .6s steps(6)}@keyframes boot_sk2{from{opacity:0;transform:scaleY(.02)}}body.sk2 .top::before{content:'';position:absolute;top:0;left:0;width:34px;height:2px;background:var(--g);box-shadow:0 0 10px var(--g)}body.sk2 .top::after{content:'';position:absolute;bottom:0;right:0;width:34px;height:2px;background:var(--m);box-shadow:0 0 10px var(--m)}body.sk2 h1{font-size:clamp(17px,3.4vw,25px);color:var(--g);letter-spacing:3px;text-shadow:0 0 12px rgba(0,255,156,.6);font-weight:700}body.sk2 .cur{color:var(--g);animation:bl_sk2 1s steps(1) infinite}@keyframes bl_sk2{50%{opacity:0}}body.sk2 .sub{font-size:9.5px;color:var(--d);letter-spacing:2px;margin-top:5px}body.sk2 #status{margin-top:9px;border:1px solid var(--ln);background:#04070A;padding:7px 10px;font-size:11px;color:var(--g);display:flex;gap:8px;align-items:center;box-shadow:inset 0 0 22px rgba(0,255,156,.06)}body.sk2 #status .d{width:7px;height:7px;background:var(--g);flex:0 0 7px;box-shadow:0 0 9px var(--g);animation:bk_sk2 .7s steps(1) infinite}@keyframes bk_sk2{50%{background:var(--a)}}body.sk2 .grid{display:flex;gap:12px;align-items:flex-start}body.sk2 .mcol{flex:1 1 0;min-width:0;display:flex;flex-direction:column;gap:12px}body.sk2 .mcol .p{width:100%}body.sk2 .grid.msd .p{animation:none!important}body.sk2 .p{border:1px solid var(--ln);background:var(--pn);position:relative;animation:up_sk2 .4s steps(5) backwards}body.sk2 .p:nth-child(1){animation-delay:.04s}body.sk2 .p:nth-child(2){animation-delay:.08s}body.sk2 .p:nth-child(3){animation-delay:.12s}body.sk2 .p:nth-child(4){animation-delay:.16s}body.sk2 .p:nth-child(5){animation-delay:.2s}body.sk2 .p:nth-child(6){animation-delay:.24s}body.sk2 .p:nth-child(7){animation-delay:.28s}@keyframes up_sk2{from{opacity:0;transform:translateY(16px)}}body.sk2 .p::before,body.sk2 .p::after{content:'';position:absolute;width:10px;height:10px;pointer-events:none}body.sk2 .p::before{top:-1px;left:-1px;border-top:2px solid var(--g);border-left:2px solid var(--g);opacity:.75}body.sk2 .p::after{bottom:-1px;right:-1px;border-bottom:2px solid var(--g);border-right:2px solid var(--g);opacity:.75}body.sk2 .ph{display:flex;align-items:center;gap:8px;padding:9px 11px;cursor:pointer;user-select:none;background:#0C131A;border-bottom:1px solid var(--ln)}body.sk2 .ph:hover{background:#101A23}body.sk2 .ar{flex:none;font-size:8px;color:var(--g);transition:transform .2s steps(3)}body.sk2 .p.cl .ar{transform:rotate(-90deg)}body.sk2 .ti{flex:1;font-size:11.5px;color:var(--w);letter-spacing:1px}body.sk2 .tg{font-size:9.5px;background:#0B1218;border:1px solid var(--ln);color:var(--a);padding:3px 7px;flex:none;max-width:40%;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}body.sk2 .tg.i{color:var(--d)}body.sk2 .tg.on{color:var(--g);border-color:rgba(0,255,156,.4);text-shadow:0 0 8px rgba(0,255,156,.5)}body.sk2 .pb{max-height:1700px;overflow:hidden;transition:max-height .3s steps(9),opacity .22s}body.sk2 .p.cl .pb{max-height:0;opacity:0}body.sk2 .in{padding:11px}body.sk2 .st{font-size:9.5px;letter-spacing:2px;color:var(--d);margin-bottom:8px;border-bottom:1px solid var(--ln);padding-bottom:4px}body.sk2 .mg{display:grid;grid-template-columns:repeat(auto-fit,minmax(74px,1fr));gap:5px}body.sk2 .mode-btn,body.sk2 .anim-btn{position:relative;overflow:hidden;font-family:var(--mono);font-size:10px;letter-spacing:.5px;padding:9px 2px;color:var(--d);background:#080C11;border:1px solid var(--ln);cursor:pointer;clip-path:polygon(6px 0,100% 0,100% calc(100% - 6px),calc(100% - 6px) 100%,0 100%,0 6px);transition:color .15s,background .15s,border-color .15s}body.sk2 .mode-btn::after,body.sk2 .anim-btn::after,body.sk2 .btn::after{content:'';position:absolute;top:0;left:-60%;width:45%;height:100%;background:linear-gradient(90deg,transparent,rgba(0,255,156,.34),transparent);animation:sw_sk2 2.6s linear infinite}@keyframes sw_sk2{0%{left:-60%}55%,100%{left:130%}}body.sk2 .mode-btn:hover,body.sk2 .anim-btn:hover{color:var(--w);border-color:rgba(0,255,156,.35);background:#0C141C}body.sk2 .mode-btn:active,body.sk2 .anim-btn:active{transform:translateX(1px);filter:brightness(1.6)}body.sk2 .mode-btn.active,body.sk2 .anim-btn.active{color:var(--g);border-color:rgba(0,255,156,.55);background:rgba(0,255,156,.09);text-shadow:0 0 9px rgba(0,255,156,.6);animation:gl_sk2 .28s steps(2)}@keyframes gl_sk2{50%{transform:translate(1px,-1px)}}body.sk2 .row{display:flex;align-items:center;gap:8px;margin-bottom:8px;flex-wrap:wrap}body.sk2 .lb{font-size:10px;color:var(--d);min-width:32px;letter-spacing:1px}body.sk2 input[type=range]{-webkit-appearance:none;appearance:none;flex:1;min-width:96px;height:16px;background:transparent;cursor:pointer}body.sk2 input[type=range]::-webkit-slider-runnable-track{height:3px;background:#152430;border:0}body.sk2 input[type=range]::-webkit-slider-thumb{-webkit-appearance:none;width:9px;height:16px;margin-top:-6.5px;background:var(--g);border:0;box-shadow:0 0 8px rgba(0,255,156,.85),0 0 18px rgba(0,255,156,.35)}body.sk2 input[type=range]::-moz-range-track{height:3px;background:#152430}body.sk2 input[type=range]::-moz-range-thumb{width:9px;height:16px;border-radius:0;background:var(--g);border:0;box-shadow:0 0 8px rgba(0,255,156,.85)}body.sk2 .rd{font-size:13px;color:var(--g);text-shadow:0 0 7px rgba(0,255,156,.7),0 0 18px rgba(0,255,156,.3);min-width:48px;text-align:right;font-weight:700;font-variant-numeric:tabular-nums}body.sk2 input[type=text],body.sk2 input[type=password],body.sk2 input[type=number],body.sk2 select{font-family:var(--mono);font-size:11px;color:var(--w);background:#04070A;border:1px solid var(--ln);padding:7px 9px;outline:0}body.sk2 input:focus,body.sk2 select:focus{border-color:rgba(0,255,156,.5);box-shadow:0 0 12px rgba(0,255,156,.15)}body.sk2 select{flex:1;min-width:106px}body.sk2 select option{background:#04070A}body.sk2 input[type=color]{width:44px;height:30px;padding:0;border:1px solid var(--ln);background:#04070A;cursor:pointer}body.sk2 input[type=color]::-webkit-color-swatch-wrapper{padding:2px}body.sk2 input[type=color]::-webkit-color-swatch{border:0}body.sk2 .btn{position:relative;overflow:hidden;font-family:var(--mono);font-size:10.5px;letter-spacing:1px;padding:8px 13px;color:var(--w);background:#0B1218;border:1px solid var(--ln);cursor:pointer;clip-path:polygon(7px 0,100% 0,100% calc(100% - 7px),calc(100% - 7px) 100%,0 100%,0 7px);transition:color .15s,background .15s}body.sk2 .btn:active{background:var(--g);color:#04120C}body.sk2 .btn.p{color:var(--g);border-color:rgba(0,255,156,.42)}body.sk2 .btn.a{color:var(--a);border-color:rgba(255,176,0,.42)}body.sk2 .btn.a::after{background:linear-gradient(90deg,transparent,rgba(255,176,0,.32),transparent)}body.sk2 .dgr{color:#FF4D4D;border-color:rgba(255,46,88,.55);background:rgba(255,46,88,.09);animation:dbl_sk2 1.1s steps(2) infinite}@keyframes dbl_sk2{50%{background:rgba(255,46,88,.3);color:#fff}}body.sk2 .sw{display:inline-flex;align-items:center;gap:7px;cursor:pointer}body.sk2 .sw input{display:none}body.sk2 .tr{width:40px;height:16px;background:#0B1218;border:1px solid var(--ln);position:relative;flex:none;transition:background .2s,border-color .2s}body.sk2 .tr::after{content:'';position:absolute;top:1px;left:1px;width:16px;height:12px;background:#3A5468;transition:transform .2s,background .2s}body.sk2 .sw input:checked+.tr{border-color:var(--g);background:rgba(0,255,156,.12)}body.sk2 .sw input:checked+.tr::after{transform:translateX(20px);background:var(--g);box-shadow:0 0 9px rgba(0,255,156,.8)}body.sk2 .sw em{font-style:normal;font-size:10px;color:var(--a);letter-spacing:1px}body.sk2 .sw input:checked~em{color:var(--g);text-shadow:0 0 7px rgba(0,255,156,.5)}body.sk2 .chk{display:inline-flex;align-items:center;gap:6px;cursor:pointer;font-size:10.5px;color:var(--d)}body.sk2 .bx{width:14px;height:14px;border:1px solid var(--a);display:inline-block;position:relative;flex:none;background:#04070A}body.sk2 .chk.on .box::after,body.sk2 .chk.on .bx::after{content:'';position:absolute;inset:2px;background:var(--a);box-shadow:0 0 7px rgba(255,176,0,.8)}body.sk2 .chk.on .bx{border-color:var(--a)}body.sk2 .chk.on{color:var(--a)}body.sk2 .lst{border:1px solid var(--ln);background:#04070A}body.sk2 .it{padding:7px 8px;border-bottom:1px solid #101A23;display:flex;align-items:center;gap:6px;flex-wrap:wrap;animation:tw_sk2 .34s steps(7) backwards;transition:background .2s,transform .2s,padding-left .2s}body.sk2 .it:nth-child(1){animation-delay:.06s}body.sk2 .it:nth-child(2){animation-delay:.26s}body.sk2 .it:nth-child(3){animation-delay:.46s}@keyframes tw_sk2{from{opacity:0;transform:translateX(-12px)}}body.sk2 .it:last-child{border-bottom:0}body.sk2 .it:hover{background:rgba(0,255,156,.07);padding-left:12px}body.sk2 .it.cur{background:rgba(0,255,156,.06);box-shadow:inset 2px 0 0 var(--g)}body.sk2 .dot{width:6px;height:6px;background:#22333F;flex:none}body.sk2 .dot.g{background:var(--g);box-shadow:0 0 7px var(--g)}body.sk2 .nm{font-size:11px;color:var(--w);flex:1;min-width:66px;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}body.sk2 .nm small{color:var(--d);font-size:9px;margin-left:4px}body.sk2 .ab{font-family:var(--mono);font-size:9px;letter-spacing:1px;padding:3px 5px;background:#0D151C;border:1px solid var(--ln);color:var(--a);cursor:pointer}body.sk2 .ab:active{background:var(--a);color:#100C00}body.sk2 .ab.g{color:var(--g)}body.sk2 .ab.r{color:#FF4D4D;border-color:rgba(255,46,88,.4)}body.sk2 .hint{font-size:9.5px;line-height:1.7;margin-top:8px;color:var(--d)}body.sk2 .ws{font-size:10.5px;background:#04070A;border:1px solid var(--ln);padding:7px 9px;line-height:1.8;word-break:break-all;color:var(--g)}body.sk2 .cs{font-size:10px;min-height:14px;margin-top:7px;color:var(--a)}body.sk2 #singleControl{display:none}@media(max-width:520px){body.sk2 .mg{grid-template-columns:repeat(3,1fr)}}body.sk3{--cy:#7DD3FC;--pu:#C4B5FD;--pk:#F9A8D4;--gn:#86EFAC;--or:#FDBA74;--w:#F2F6FF;--d:#B9C3DE;--gl:rgba(255,255,255,.09);--ln:rgba(255,255,255,.16)}body.sk3,body.sk3{min-height:100%}body.sk3{background:#0A0E1A;color:var(--w);font-family:"Segoe UI",system-ui,-apple-system,sans-serif;-webkit-text-size-adjust:100%;padding:14px 12px 38px;position:relative;overflow-x:hidden;min-height:100vh}body.sk3 .aur{position:fixed;inset:0;z-index:0;overflow:hidden;pointer-events:none}body.sk3 .aur i{position:absolute;display:block;border-radius:50%;filter:blur(72px);opacity:.55}body.sk3 .aur i:nth-child(1){width:52vw;height:52vw;background:#2563EB;top:-14vw;left:-8vw;animation:d1_sk3 26s ease-in-out infinite alternate}body.sk3 .aur i:nth-child(2){width:46vw;height:46vw;background:#7C3AED;top:8vw;right:-10vw;animation:d2_sk3 32s ease-in-out infinite alternate}body.sk3 .aur i:nth-child(3){width:44vw;height:44vw;background:#DB2777;bottom:-12vw;left:12vw;animation:d3_sk3 29s ease-in-out infinite alternate}body.sk3 .aur i:nth-child(4){width:40vw;height:40vw;background:#0891B2;bottom:-6vw;right:6vw;animation:d4_sk3 35s ease-in-out infinite alternate}body.sk3 .aur i:nth-child(5){width:34vw;height:34vw;background:#F59E0B;top:38%;left:38%;animation:d5_sk3 23s ease-in-out infinite alternate;opacity:.35}@keyframes d1_sk3{to{transform:translate(14vw,10vw) scale(1.18)}}@keyframes d2_sk3{to{transform:translate(-12vw,14vw) scale(1.14)}}@keyframes d3_sk3{to{transform:translate(10vw,-12vw) scale(1.2)}}@keyframes d4_sk3{to{transform:translate(-9vw,-9vw) scale(1.16)}}@keyframes d5_sk3{to{transform:translate(-8vw,8vw) scale(.86)}}body.sk3 .wrap{max-width:1900px;margin:0 auto;position:relative;z-index:1}body.sk3 .top{background:var(--gl);backdrop-filter:blur(22px);-webkit-backdrop-filter:blur(22px);border:1px solid var(--ln);border-radius:20px;padding:15px 17px;margin-bottom:14px;box-shadow:0 8px 32px rgba(0,0,0,.28),inset 0 1px 0 rgba(255,255,255,.18);animation:up_sk3 .6s both}@keyframes up_sk3{from{opacity:0;transform:translateY(18px)}}body.sk3 h1{font-size:clamp(21px,4.2vw,31px);font-weight:650;letter-spacing:.5px;background:linear-gradient(96deg,var(--cy),var(--pk) 45%,var(--pu));-webkit-background-clip:text;background-clip:text;-webkit-text-fill-color:transparent}body.sk3 .sub{font-size:10.5px;letter-spacing:4px;color:var(--d);margin-top:5px;text-transform:uppercase}body.sk3 #status{margin-top:10px;background:rgba(255,255,255,.1);border:1px solid var(--ln);border-radius:12px;padding:8px 12px;font-size:11.5px;color:var(--w);display:flex;gap:9px;align-items:center}body.sk3 #status .d{width:9px;height:9px;border-radius:50%;background:var(--gn);flex:0 0 9px;box-shadow:0 0 12px var(--gn);animation:bl_sk3 1.5s ease-in-out infinite}@keyframes bl_sk3{50%{opacity:.3;transform:scale(.75)}}body.sk3 .grid{display:flex;gap:14px;align-items:flex-start}body.sk3 .mcol{flex:1 1 0;min-width:0;display:flex;flex-direction:column;gap:14px}body.sk3 .mcol .p{width:100%}body.sk3 .grid.msd .p{animation:none!important}body.sk3 .p{background:var(--gl);backdrop-filter:blur(20px);-webkit-backdrop-filter:blur(20px);border:1px solid var(--ln);border-radius:20px;overflow:hidden;box-shadow:0 8px 30px rgba(0,0,0,.25),inset 0 1px 0 rgba(255,255,255,.16);transition:transform .3s,box-shadow .3s,border-color .3s;animation:up_sk3 .55s both}body.sk3 .p:nth-child(1){animation-delay:.05s}body.sk3 .p:nth-child(2){animation-delay:.1s}body.sk3 .p:nth-child(3){animation-delay:.15s}body.sk3 .p:nth-child(4){animation-delay:.2s}body.sk3 .p:nth-child(5){animation-delay:.25s}body.sk3 .p:nth-child(6){animation-delay:.3s}body.sk3 .p:nth-child(7){animation-delay:.35s}body.sk3 .p:hover{transform:translateY(-2px);border-color:rgba(255,255,255,.3);box-shadow:0 14px 40px rgba(0,0,0,.32),inset 0 1px 0 rgba(255,255,255,.2)}body.sk3 .ph{display:flex;align-items:center;gap:9px;padding:12px 14px;cursor:pointer;user-select:none;background:linear-gradient(96deg,rgba(125,211,252,.16),rgba(196,181,253,.12));border-bottom:1px solid var(--ln)}body.sk3 .ph:hover{background:linear-gradient(96deg,rgba(125,211,252,.24),rgba(196,181,253,.18))}body.sk3 .ar{flex:none;font-size:9px;color:var(--cy);transition:transform .3s}body.sk3 .p.cl .ar{transform:rotate(-90deg)}body.sk3 .ti{flex:1;font-size:13.5px;font-weight:600}body.sk3 .tg{font-size:10px;color:var(--cy);background:rgba(125,211,252,.16);border:1px solid rgba(125,211,252,.32);border-radius:20px;padding:3px 9px;flex:none;max-width:40%;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}body.sk3 .tg.i{color:var(--d);background:rgba(255,255,255,.08);border-color:var(--ln)}body.sk3 .tg.on{color:var(--gn);background:rgba(134,239,172,.16);border-color:rgba(134,239,172,.36)}body.sk3 .pb{max-height:1700px;overflow:hidden;transition:max-height .4s cubic-bezier(.4,0,.2,1),opacity .3s,filter .3s}body.sk3 .p.cl .pb{max-height:0;opacity:0}body.sk3 .in{padding:13px}body.sk3 .st{font-size:10px;letter-spacing:2.5px;color:var(--d);text-transform:uppercase;margin-bottom:9px;border-bottom:1px solid var(--ln);padding-bottom:5px}body.sk3 .mg{display:grid;grid-template-columns:repeat(auto-fit,minmax(76px,1fr));gap:8px}body.sk3 .mode-btn,body.sk3 .anim-btn,body.sk3 .btn{position:relative;overflow:hidden;font:inherit;font-size:10.5px;padding:10px 4px;cursor:pointer;color:var(--w);background:rgba(255,255,255,.1);border:1px solid var(--ln);border-radius:12px;transition:transform .18s,background .25s,box-shadow .25s,border-color .25s}body.sk3 .mode-btn::after,body.sk3 .anim-btn::after,body.sk3 .btn::after{content:'';position:absolute;top:0;left:-70%;width:45%;height:100%;background:linear-gradient(90deg,transparent,rgba(255,255,255,.4),transparent);animation:sw_sk3 3.2s ease-in-out infinite}@keyframes sw_sk3{0%{left:-70%}55%,100%{left:170%}}body.sk3 .mode-btn:hover,body.sk3 .anim-btn:hover,body.sk3 .btn:hover{background:rgba(255,255,255,.2);box-shadow:0 0 22px rgba(125,211,252,.3);transform:translateY(-1px)}body.sk3 .mode-btn:active,body.sk3 .anim-btn:active,body.sk3 .btn:active{transform:scale(.96)}body.sk3 .mode-btn.active,body.sk3 .anim-btn.active{color:#0B1024;font-weight:700;background:linear-gradient(120deg,var(--cy),var(--pu));border-color:transparent;box-shadow:0 0 24px rgba(196,181,253,.5)}body.sk3 .btn.g{color:#0B1024;font-weight:700;background:linear-gradient(120deg,var(--gn),var(--cy));border-color:transparent}body.sk3 .btn.g:hover{box-shadow:0 0 24px rgba(134,239,172,.45)}body.sk3 .btn.r{color:#fff;background:rgba(249,168,212,.2);border-color:rgba(249,168,212,.45)}body.sk3 .btn.r:hover{background:rgba(249,168,212,.32);box-shadow:0 0 20px rgba(249,168,212,.35)}body.sk3 .row{display:flex;align-items:center;gap:9px;margin-bottom:10px;flex-wrap:wrap}body.sk3 .lb{font-size:11px;color:var(--d);min-width:34px}body.sk3 input[type=range]{-webkit-appearance:none;appearance:none;flex:1;min-width:98px;height:18px;background:transparent;cursor:pointer}body.sk3 input[type=range]::-webkit-slider-runnable-track{height:6px;border-radius:6px;background:linear-gradient(90deg,var(--cy),var(--pu),var(--pk));box-shadow:0 0 14px rgba(196,181,253,.35)}body.sk3 input[type=range]::-webkit-slider-thumb{-webkit-appearance:none;width:17px;height:17px;border-radius:50%;margin-top:-5.5px;background:#fff;border:0;box-shadow:0 0 12px rgba(255,255,255,.8),0 2px 8px rgba(0,0,0,.3)}body.sk3 input[type=range]::-moz-range-track{height:6px;border-radius:6px;background:linear-gradient(90deg,var(--cy),var(--pu),var(--pk))}body.sk3 input[type=range]::-moz-range-thumb{width:15px;height:15px;border-radius:50%;background:#fff;border:0;box-shadow:0 0 12px rgba(255,255,255,.8)}body.sk3 .rv{font-size:12px;color:var(--cy);min-width:44px;text-align:right;font-weight:650;font-variant-numeric:tabular-nums}body.sk3 input[type=text],body.sk3 input[type=password],body.sk3 input[type=number],body.sk3 select{font:inherit;font-size:11.5px;color:var(--w);background:rgba(255,255,255,.1);border:1px solid var(--ln);border-radius:11px;padding:8px 11px;outline:0;transition:border-color .25s,box-shadow .25s}body.sk3 input:focus,body.sk3 select:focus{border-color:var(--cy);box-shadow:0 0 14px rgba(125,211,252,.28)}body.sk3 select{flex:1;min-width:106px}body.sk3 select option{background:#141A2E;color:#fff}body.sk3 input[type=color]{width:48px;height:34px;padding:0;background:rgba(255,255,255,.1);border:1px solid var(--ln);border-radius:10px;cursor:pointer}body.sk3 input[type=color]::-webkit-color-swatch-wrapper{padding:3px}body.sk3 input[type=color]::-webkit-color-swatch{border:0;border-radius:7px}body.sk3 .sw{display:inline-flex;align-items:center;gap:7px;cursor:pointer;font-size:11px}body.sk3 .sw input{display:none}body.sk3 .tr{width:44px;height:24px;border-radius:20px;background:rgba(255,255,255,.14);border:1px solid var(--ln);position:relative;flex:none;transition:background .3s,border-color .3s,box-shadow .3s}body.sk3 .tr::after{content:'';position:absolute;top:2px;left:2px;width:18px;height:18px;border-radius:50%;background:#fff;box-shadow:0 2px 6px rgba(0,0,0,.3);transition:transform .3s cubic-bezier(.34,1.4,.64,1)}body.sk3 .sw input:checked+.tr{background:linear-gradient(90deg,var(--gn),var(--cy));border-color:transparent;box-shadow:0 0 14px rgba(134,239,172,.4)}body.sk3 .sw input:checked+.tr::after{transform:translateX(20px)}body.sk3 .chk{display:inline-flex;align-items:center;gap:7px;cursor:pointer;font-size:11px;color:var(--d);background:rgba(255,255,255,.09);border:1px solid var(--ln);border-radius:20px;padding:5px 12px;user-select:none;transition:color .25s,border-color .25s,background .25s}body.sk3 .bx{width:13px;height:13px;border-radius:50%;border:1px solid var(--d);flex:none;position:relative;transition:border-color .25s}body.sk3 .chk.on .bx{border-color:var(--cy)}body.sk3 .chk.on .bx::after{content:'';position:absolute;inset:2px;border-radius:50%;background:var(--cy);box-shadow:0 0 8px var(--cy)}body.sk3 .chk.on{color:var(--cy);background:rgba(125,211,252,.14);border-color:rgba(125,211,252,.35)}body.sk3 .lst{background:rgba(255,255,255,.07);border:1px solid var(--ln);border-radius:14px;overflow:hidden}body.sk3 .it{padding:9px 10px;border-bottom:1px solid rgba(255,255,255,.09);display:flex;align-items:center;gap:6px;flex-wrap:wrap;animation:jin_sk3 .5s both;transition:background .25s,transform .25s}body.sk3 .it:nth-child(1){animation-delay:.12s}body.sk3 .it:nth-child(2){animation-delay:.26s}body.sk3 .it:nth-child(3){animation-delay:.4s}@keyframes jin_sk3{from{opacity:0;transform:translateY(12px)}}body.sk3 .it:last-child{border-bottom:0}body.sk3 .it:hover{background:rgba(255,255,255,.12);transform:translateX(3px)}body.sk3 .it.cur{background:rgba(134,239,172,.13);box-shadow:inset 3px 0 0 var(--gn)}body.sk3 .nm{flex:1;min-width:66px;font-size:11.5px;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}body.sk3 .mb{font:inherit;font-size:9.5px;background:rgba(255,255,255,.12);color:var(--w);border:1px solid var(--ln);border-radius:8px;padding:4px 8px;cursor:pointer;transition:background .2s,box-shadow .2s}body.sk3 .mb:hover{background:rgba(255,255,255,.26);box-shadow:0 0 12px rgba(255,255,255,.2)}body.sk3 .mb.d{background:rgba(249,168,212,.22);border-color:rgba(249,168,212,.42)}body.sk3 .hint{font-size:9.5px;line-height:1.75;margin-top:9px;color:var(--d)}body.sk3 .ws{font-size:11px;background:rgba(255,255,255,.09);border:1px solid var(--ln);border-radius:12px;padding:8px 11px;line-height:1.8;word-break:break-all;color:var(--cy)}body.sk3 .cs{font-size:10.5px;min-height:15px;margin-top:8px;color:var(--pk)}body.sk3 #singleControl{display:none}@media(max-width:520px){body.sk3 .mg{grid-template-columns:repeat(3,1fr)}}body.sk4{--bg:#1A1B26;--pn:#252739;--r:#E94560;--b:#0F4C81;--y:#FFD369;--cy:#00FFF5;--g:#6BCB77;--w:#EAEAEA;--d:#8B8FA3}body.sk4{background:var(--bg);color:var(--w);font-family:"Courier New",Consolas,monospace;-webkit-text-size-adjust:100%;padding:12px 10px 34px;font-weight:700;letter-spacing:.6px;background-image:linear-gradient(rgba(255,255,255,.022) 1px,transparent 1px),linear-gradient(90deg,rgba(255,255,255,.022) 1px,transparent 1px);background-size:8px 8px}body.sk4 .wrap{max-width:1900px;margin:0 auto;padding:0 10px}body.sk4 .top{border:4px solid #000;background:var(--pn);box-shadow:0 0 0 4px #4a4d66,8px 8px 0 0 #000;padding:12px 14px;margin-bottom:16px;animation:pk_sk4 .5s steps(4)}@keyframes pk_sk4{from{opacity:0;transform:translateY(-10px)}}body.sk4 h1{font-size:clamp(19px,3.8vw,28px);color:var(--y);text-shadow:3px 3px 0 #000,0 0 14px rgba(255,211,105,.5);letter-spacing:2px;animation:fl_sk4 2.4s steps(1) infinite}@keyframes fl_sk4{0%,96%{opacity:1}97%,99%{opacity:.72}}body.sk4 .sub{font-size:10.5px;color:var(--cy);margin-top:6px;letter-spacing:1px}body.sk4 #status{margin-top:10px;border:3px solid #000;background:#0D0E18;padding:8px 10px;font-size:11px;color:var(--g);box-shadow:inset 0 0 0 2px #2c2f45;display:flex;gap:8px;align-items:center}body.sk4 #status .d{width:9px;height:9px;background:var(--g);flex:0 0 9px;box-shadow:0 0 0 2px #000;animation:bk_sk4 .8s steps(1) infinite}@keyframes bk_sk4{50%{background:var(--r)}}body.sk4 .grid{display:flex;gap:14px;align-items:flex-start}body.sk4 .mcol{flex:1 1 0;min-width:0;display:flex;flex-direction:column;gap:14px}body.sk4 .mcol .p{width:100%}body.sk4 .grid.msd .p{animation:none!important}body.sk4 .p{background:var(--pn);border:4px solid #000;box-shadow:0 0 0 4px #4a4d66,6px 6px 0 0 #000;animation:up_sk4 .45s steps(5) backwards}body.sk4 .p:nth-child(1){animation-delay:.04s}body.sk4 .p:nth-child(2){animation-delay:.08s}body.sk4 .p:nth-child(3){animation-delay:.12s}body.sk4 .p:nth-child(4){animation-delay:.16s}body.sk4 .p:nth-child(5){animation-delay:.2s}body.sk4 .p:nth-child(6){animation-delay:.24s}body.sk4 .p:nth-child(7){animation-delay:.28s}@keyframes up_sk4{from{opacity:0;transform:translateY(18px)}}body.sk4 .ph{display:flex;align-items:center;gap:8px;padding:10px 11px;cursor:pointer;user-select:none;background:#33364d;border-bottom:4px solid #000}body.sk4 .ph:hover{background:#3e4260}body.sk4 .ph:active{transform:translate(2px,2px)}body.sk4 .ar{flex:none;font-size:9px;color:var(--cy);transition:transform .2s steps(3)}body.sk4 .p.cl .ar{transform:rotate(-90deg)}body.sk4 .ti{flex:1;font-size:12.5px;color:var(--y);text-shadow:2px 2px 0 #000}body.sk4 .tg{font-size:9.5px;background:#000;color:var(--cy);padding:3px 7px;flex:none;max-width:40%;overflow:hidden;text-overflow:ellipsis;white-space:nowrap;box-shadow:0 0 0 2px #4a4d66}body.sk4 .tg.i{color:var(--d)}body.sk4 .tg.on{color:var(--g)}body.sk4 .pb{max-height:1700px;overflow:hidden;transition:max-height .3s steps(8),opacity .2s}body.sk4 .p.cl .pb{max-height:0;opacity:0}body.sk4 .in{padding:12px}body.sk4 .st{font-size:10px;color:var(--cy);letter-spacing:2px;margin-bottom:9px;border-bottom:2px dashed #4a4d66;padding-bottom:5px}body.sk4 .mg{display:grid;grid-template-columns:repeat(auto-fit,minmax(76px,1fr));gap:7px}body.sk4 .mode-btn,body.sk4 .anim-btn{position:relative;overflow:hidden;font:inherit;font-size:10.5px;font-weight:700;letter-spacing:.5px;padding:10px 3px;color:var(--w);background:#3a3e58;border:0;cursor:pointer;box-shadow:0 0 0 3px #000,4px 4px 0 0 #000,inset 0 3px 0 rgba(255,255,255,.12);transition:transform .07s,box-shadow .07s}body.sk4 .mode-btn::after,body.sk4 .anim-btn::after,body.sk4 .btn::after{content:'';position:absolute;top:0;left:-60%;width:38%;height:100%;background:linear-gradient(90deg,transparent,rgba(255,255,255,.3),transparent);animation:sw_sk4 2.6s steps(22) infinite}@keyframes sw_sk4{0%{left:-60%}55%,100%{left:150%}}body.sk4 .mode-btn:active,body.sk4 .anim-btn:active,body.sk4 .btn:active{transform:translate(4px,4px);box-shadow:0 0 0 3px #000}body.sk4 .mode-btn.active,body.sk4 .anim-btn.active{background:var(--r);color:#fff;transform:translate(4px,4px);box-shadow:0 0 0 3px #000;animation:act_sk4 1s steps(2) infinite}@keyframes act_sk4{50%{background:#ff5f78}}body.sk4 .mode-btn:nth-child(6n+1){background:#0F4C81}body.sk4 .mode-btn:nth-child(6n+2){background:#5C3A7A}body.sk4 .mode-btn:nth-child(6n+3){background:#2A7F6F}body.sk4 .mode-btn:nth-child(6n+4){background:#7A5C2A}body.sk4 .mode-btn:nth-child(6n+5){background:#7A2A4C}body.sk4 .mode-btn:nth-child(6n+6){background:#3A5C7A}body.sk4 .anim-btn{background:#3a3e58}body.sk4 .row{display:flex;align-items:center;gap:8px;margin-bottom:9px;flex-wrap:wrap}body.sk4 .lb{font-size:10px;color:var(--d);min-width:34px;letter-spacing:1px}body.sk4 input[type=range]{-webkit-appearance:none;appearance:none;flex:1;min-width:100px;height:18px;background:transparent;cursor:pointer}body.sk4 input[type=range]::-webkit-slider-runnable-track{height:14px;background:#11131f;border:0;box-shadow:0 0 0 3px #000,inset 0 0 0 2px #2c3048;background-image:repeating-linear-gradient(90deg,#2c3048 0 6px,#11131f 6px 8px)}body.sk4 input[type=range]::-webkit-slider-thumb{-webkit-appearance:none;width:16px;height:20px;margin-top:-4px;background:var(--g);border:0;box-shadow:0 0 0 3px #000,inset 0 3px 0 rgba(255,255,255,.4)}body.sk4 input[type=range]::-moz-range-track{height:14px;background:#11131f;box-shadow:0 0 0 3px #000}body.sk4 input[type=range]::-moz-range-thumb{width:14px;height:18px;background:var(--g);border:0;border-radius:0;box-shadow:0 0 0 3px #000}body.sk4 .rv{font-size:12px;color:var(--y);background:#0D0E18;padding:3px 7px;min-width:44px;text-align:center;box-shadow:0 0 0 2px #000;font-variant-numeric:tabular-nums}body.sk4 input[type=text],body.sk4 input[type=password],body.sk4 input[type=number],body.sk4 select{font:inherit;font-size:11px;font-weight:700;color:var(--w);background:#11131f;border:0;padding:7px 9px;outline:0;box-shadow:0 0 0 3px #000,inset 0 0 0 2px #2c3048}body.sk4 input:focus,body.sk4 select:focus{box-shadow:0 0 0 3px #000,inset 0 0 0 2px var(--cy)}body.sk4 select{flex:1;min-width:110px}body.sk4 input[type=color]{width:48px;height:32px;padding:0;background:#11131f;border:0;box-shadow:0 0 0 3px #000;cursor:pointer}body.sk4 input[type=color]::-webkit-color-swatch-wrapper{padding:3px}body.sk4 input[type=color]::-webkit-color-swatch{border:0}body.sk4 .btn{position:relative;overflow:hidden;font:inherit;font-size:11px;font-weight:700;padding:9px 13px;color:#12131c;background:var(--y);border:0;cursor:pointer;box-shadow:0 0 0 3px #000,4px 4px 0 0 #000;transition:transform .07s,box-shadow .07s}body.sk4 .btn.g{background:var(--g)}body.sk4 .btn.c{background:var(--cy)}body.sk4 .btn.r{background:var(--r);color:#fff}body.sk4 .sw{display:inline-flex;align-items:center;gap:7px;cursor:pointer;font-size:10.5px}body.sk4 .sw input{display:none}body.sk4 .tr{width:44px;height:20px;background:#11131f;position:relative;flex:none;box-shadow:0 0 0 3px #000,inset 0 0 0 2px #2c3048}body.sk4 .tr::after{content:'';position:absolute;top:2px;left:2px;width:16px;height:16px;background:#5a5e78;transition:transform .18s steps(4),background .18s}body.sk4 .sw input:checked+.tr{box-shadow:0 0 0 3px #000,inset 0 0 0 2px var(--g)}body.sk4 .sw input:checked+.tr::after{transform:translateX(20px);background:var(--g)}body.sk4 .chk{display:inline-flex;align-items:center;gap:7px;cursor:pointer;font-size:10.5px;background:#11131f;padding:5px 9px;box-shadow:0 0 0 3px #000,inset 0 0 0 2px #2c3048;user-select:none}body.sk4 .bx{width:14px;height:14px;background:#11131f;flex:none;position:relative;box-shadow:0 0 0 2px #4a4d66}body.sk4 .chk.on .bx::after{content:'';position:absolute;inset:2px;background:var(--cy)}body.sk4 .chk.on{color:var(--cy);box-shadow:0 0 0 3px #000,inset 0 0 0 2px var(--cy)}body.sk4 .lst{background:#11131f;box-shadow:0 0 0 3px #000}body.sk4 .it{padding:8px 9px;border-bottom:2px solid #2c3048;display:flex;align-items:center;gap:5px;flex-wrap:wrap;animation:tw_sk4 .3s steps(6) backwards}body.sk4 .it:nth-child(1){animation-delay:.05s}body.sk4 .it:nth-child(2){animation-delay:.22s}body.sk4 .it:nth-child(3){animation-delay:.39s}@keyframes tw_sk4{from{opacity:0;transform:translateX(-10px)}}body.sk4 .it:last-child{border-bottom:0}body.sk4 .it.cur{background:#1c2036;box-shadow:inset 4px 0 0 var(--g)}body.sk4 .nm{flex:1;min-width:70px;font-size:10.5px;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}body.sk4 .mb{font:inherit;font-size:9px;background:#3a3e58;color:var(--w);border:0;padding:4px 6px;cursor:pointer;box-shadow:0 0 0 2px #000}body.sk4 .mb:active{background:var(--r);color:#fff}body.sk4 .mb.d{background:var(--r);color:#fff}body.sk4 .hint{font-size:9.5px;line-height:1.7;margin-top:9px;color:var(--d);font-weight:700}body.sk4 .ws{font-size:10.5px;background:#11131f;padding:7px 9px;line-height:1.8;word-break:break-all;box-shadow:0 0 0 3px #000,inset 0 0 0 2px #2c3048}body.sk4 .cs{font-size:10px;min-height:15px;margin-top:8px;color:var(--y)}body.sk4 #singleControl{display:none}body.sk4 .pix{display:flex;gap:3px;margin-bottom:9px}body.sk4 .pix i{width:7px;height:7px;background:#3a3e58;box-shadow:0 0 0 2px #000}body.sk4 .pix i:nth-child(3n+1){background:var(--r)}body.sk4 .pix i:nth-child(3n+2){background:var(--cy)}@media(max-width:520px){body.sk4 .mg{grid-template-columns:repeat(3,1fr)}}</style>
<style>/* ---- 温湿度卡片 ---- */
.dhtgrid{display:grid;grid-template-columns:1fr 1fr;gap:10px;margin:4px 0 2px}
.dhtcell{text-align:center;padding:10px 6px;border-radius:12px;border:1.5px solid rgba(128,128,128,.42);background:rgba(128,128,128,.1)}
.dhtnum{font-size:26px;font-weight:800;line-height:1.15}
.dhtlb{font-size:11px;opacity:.65;margin-top:2px}
/* ---- OTA 进度条 ---- */
.opwrap{height:10px;border-radius:6px;background:rgba(128,128,128,.28);overflow:hidden;margin:8px 0 2px}
.opbar{height:100%;width:0;background:linear-gradient(90deg,#FFE600,#FF9A00,#FF4D8D);border-radius:6px;transition:width .4s ease}
</style>
</head>
<body>
<script>
try{var _s=localStorage.getItem('land_led_skin'),_n=_s?+_s:0;
document.body.setAttribute('data-skin',_n);document.body.classList.add('sk'+_n);}catch(e){}
</script>
<div data-sk="3" class="aur"><i></i><i></i><i></i><i></i><i></i></div>
<div class="wrap">
<div class="top">
  <div data-sk="0" class="band"><i></i><i></i><i></i><i></i><i></i></div>
  <h1 data-sk="4">▚ LAND 灯光系统 ▞</h1>
  <h1 data-sk="3">LAND 灯光系统</h1>
  <h1 data-sk="2">&gt; LAND_LIGHT_SYS<span class="cur">▊</span></h1>
  <h1 data-sk="0 1">LAND <em>灯光系统</em></h1>
  <div class="sub">LIGHT · COLOR · RHYTHM</div>
  <div id="status"><span class="d"></span>状态已加载 · 灯带运行中</div>

</div>
<div class="grid">
<section class="p accordion-panel open"><div class="ph accordion-header"><span class="ar">▼</span><span class="ti">🔆 灯光模式</span>
<div class="sw" onclick="event.stopPropagation()"><label class="sw"><input type="checkbox" id="reverseToggle"><span class="tr"></span></label>
<span id="reverseStatus">反向关</span></div></div>
<div class="pb accordion-body open"><div class="in">
  <div data-sk="2" class="st">18 MODES</div>
  <div data-sk="4" class="pix"><i></i><i></i><i></i><i></i><i></i><i></i><i></i><i></i></div>
  <div class="mg">
    <button class="mode-btn" data-mode="solid">🔵单色</button>
    <button class="mode-btn" data-mode="single">🔧单控</button>
    <button class="mode-btn" data-mode="rainbow">🌈彩虹</button>
    <button class="mode-btn" data-mode="theater">🎭剧院</button>
    <button class="mode-btn" data-mode="confetti">🎉彩纸</button>
    <button class="mode-btn" data-mode="breathing">💫呼吸</button>
    <button class="mode-btn" data-mode="sparkle">✨星光</button>
    <button class="mode-btn" data-mode="aurora">🌌极光</button>
    <button class="mode-btn" data-mode="disco">🎵蹦迪</button>
    <button class="mode-btn" data-mode="randomjump">🎺跳变</button>
    <button class="mode-btn" data-mode="step">🕹踏步</button>
    <button class="mode-btn" data-mode="sparksparks">⚡火花</button>
    <button class="mode-btn" data-mode="rainbowstring">🌁彩跑</button>
    <button class="mode-btn" data-mode="heartbeat">❤️心跳</button>
    <button class="mode-btn" data-mode="firework">🎆烟花</button>
    <button class="mode-btn" data-mode="random">🎲随机</button>
    <button class="mode-btn active" data-mode="off">⏹关闭</button>
  </div>
</div></div></section>
<section class="p accordion-panel open"><div class="ph accordion-header"><span class="ar">▼</span><span class="ti">🎨 颜色与效果</span><span class="tg i">60%</span></div>
<div class="pb accordion-body open"><div class="in">
<div id="colorControl">
  <div data-sk="0 1" class="st">🎨 颜色设置</div>
  <div data-sk="4" class="st">■ COLOR SETTING</div>
  <div data-sk="3" class="st">Color</div>
  <div data-sk="2" class="st">COLOR</div>
  <div class="row"><span class="lb">颜色</span><input type="color" id="colorPicker" value="#ffffff"></div>
</div>
  <div id="singleControl">
    <div data-sk="0 1" class="st">🔧 单控设置</div>
    <div data-sk="4" class="st">■ SINGLE CONTROL</div>
    <div data-sk="3" class="st">Single Control</div>
    <div data-sk="2" class="st">SINGLE CTRL</div>
    <div class="row"><span class="lb">范围</span><input type="text" id="singleRange" placeholder="如: 1-20" style="width:110px"></div>
    <div class="row"><span class="lb">颜色</span><input type="color" id="singleColor" value="#ffffff"></div>
    <button class="btn g p" id="applySingle" style="width:100%">应用设置</button>
  </div>
  <div data-sk="0 1" class="st">💡 亮度调节</div>
  <div data-sk="4" class="st">■ BRIGHTNESS</div>
  <div data-sk="3" class="st">Brightness</div>
  <div data-sk="2" class="st">BRIGHTNESS</div>
  <div data-sk="4" class="st">■ SPEED</div>
  <div class="row"><span class="lb">亮度</span><input type="range" id="brightness" min="1" max="100" value="25"><span class="rv rd" id="brightnessValue">25%</span></div>
  <div data-sk="3" class="st">Speed</div>
  <div data-sk="0 1" class="st">⏱ 速度调节</div>
  <div data-sk="2" class="st">SPEED</div>
  <div class="row"><span class="lb">速度</span><input type="range" id="speed" min="1" max="100" value="50"><span class="rv rd" id="speedValue">50%</span></div>
</div></div></section>
<section class="p accordion-panel"><div class="ph accordion-header"><span class="ar">▼</span><span class="ti">🎬 单色动画</span><span class="tg i" id="animModeTag">随机溶解</span></div>
<div class="pb accordion-body"><div class="in">
  <div data-sk="0 1" class="st">🌐 切换颜色时的动画效果</div>
  <div data-sk="4" class="st">■ TRANSITION FX</div>
  <div data-sk="3" class="st">Transitions</div>
  <div data-sk="2" class="st">6 TRANSITIONS</div>
  <div class="mg">
    <button class="anim-btn active" data-anim="fade">🔊全局渐亮</button>
    <button class="anim-btn" data-anim="random">🎲随机溶解</button>
    <button class="anim-btn" data-anim="sequential">➜顺序点亮</button>
    <button class="anim-btn" data-anim="center">🌐中心扩散</button>
    <button class="anim-btn" data-anim="ends">↔两端向内</button>
    <button class="anim-btn" data-anim="none">❌无动画</button>
  </div>
</div></div></section>
<section class="p accordion-panel"><div class="ph accordion-header"><span class="ar">▼</span><span class="ti">⏰ 定时关闭</span><span class="tg on" id="timerDisplay">01:23:45</span></div>
<div class="pb accordion-body"><div class="in">
  <div data-sk="0 1" class="st">⏳ 持续时间</div>
  <div data-sk="4" class="st">■ COUNTDOWN TIMER</div>
  <div data-sk="3" class="st">Countdown</div>
  <div data-sk="2" class="st">COUNTDOWN</div>
  <div class="row">
    <input type="number" id="timerHours" min="0" max="23" value="1" style="width:66px"><span class="lb" style="min-width:auto">时</span>
    <input type="number" id="timerMinutes" min="0" max="59" value="0" style="width:66px"><span class="lb" style="min-width:auto">分</span>
    <input type="number" id="timerSeconds" min="0" max="59" value="0" style="width:66px"><span class="lb" style="min-width:auto">秒</span>
  </div>
  <div class="row"><button class="btn g p" id="startTimer">▶ 开始定时</button><button class="btn a c" id="stopTimer">■ 停止定时</button></div>
</div></div></section>
<section class="p accordion-panel"><div class="ph accordion-header"><span class="ar">▼</span><span class="ti">🌡 温湿度</span><span class="tg i" id="dhtTag">未启用</span></div>
<div class="pb accordion-body"><div class="in">
  <div class="dhtgrid">
    <div class="dhtcell"><div class="dhtnum" id="dhtTemp">--</div><div class="dhtlb">温度 ℃</div></div>
    <div class="dhtcell"><div class="dhtnum" id="dhtHumi">--</div><div class="dhtlb">湿度 %</div></div>
  </div>
  <div class="hint" id="dhtHint">在「⚙ 灯带设置」里开启 DHT11（接 D5 / GPIO14）后，这里每 2 秒刷新一次。</div>
</div></div></section>
<section class="p accordion-panel"><div class="ph accordion-header"><span class="ar">▼</span><span class="ti">⚙ 灯带设置</span><span class="tg i" id="ledCountTag">60 颗</span></div>
<div class="pb accordion-body"><div class="in">
  <div data-sk="0 1" class="st">📦 灯珠数量</div>
  <div data-sk="4" class="st">■ LED COUNT</div>
  <div data-sk="3" class="st">LED Count</div>
  <div data-sk="2" class="st">LED COUNT</div>
  <div class="row"><input type="number" id="ledCount" min="1" max="200" value="60" style="width:96px"><span style="font-size:11px">颗 (1-200)</span>
  <button class="btn g p" id="saveLedCount">💾 保存</button></div>
  <div class="cs" id="ledCountStatus"></div>
  <div class="st">🔌 外设开关（可选，按需开启）</div>
  <div class="row"><span style="flex:1">🌡 DHT11 温湿度<small style="opacity:.6"> 接 D5 / GPIO14</small></span>
  <div class="sw" onclick="event.stopPropagation()"><label class="sw"><input type="checkbox" id="dhtToggle"><span class="tr"></span></label></div></div>
  <div class="row"><span style="flex:1">📡 红外接收<small style="opacity:.6"> 接 D2 / GPIO4</small></span>
  <div class="sw" onclick="event.stopPropagation()"><label class="sw"><input type="checkbox" id="irToggle"><span class="tr"></span></label></div></div>
  <div class="cs" id="periphStatus"></div>
</div></div></section>
<section class="p accordion-panel"><div class="ph accordion-header"><span class="ar">▼</span><span class="ti">🌐 WiFi 配置</span><span class="tg on ssid" id="wifiSsidTag">LAND_5G</span></div>
<div class="pb accordion-body"><div class="in">
  <div data-sk="0 1" class="st">📡 连接状态</div>
  <div data-sk="4" class="st">■ LINK STATUS</div>
  <div data-sk="3" class="st">Link Status</div>
  <div data-sk="2" class="st">LINK STATUS</div>
  <div class="ws" id="wifiStatus">已连接 LAND_5G · IP 192.168.1.108<br>热点 ESP-8266 · 192.168.4.1</div>
  <div data-sk="0 1" class="st">🔒 网络设置</div>
  <div data-sk="4" class="st">■ NETWORK SETUP</div>
  <div data-sk="3" class="st">Network Setup</div>
  <div data-sk="2" class="st">NETWORK SETUP</div>
  <div class="row">
    <select id="wifiSSID">
      <option>LAND_5G</option><option>ChinaNet-hK7x</option><option>CMCC-9f2a</option>
      <option>TP-LINK_8888</option><option>-- 手动输入 --</option>
    </select>
    <button class="btn a c" id="scanWifi" title="扫描">🔄</button>
  </div>
  <input type="text" id="wifiSSIDManual" placeholder="手动输入WiFi名称" style="display:none;width:100%;margin-bottom:10px">
  <div class="row"><input type="password" id="wifiPassword" placeholder="WiFi密码" style="flex:1">
    <button class="btn" id="togglePw">👁</button></div>
  <div class="hint" id="wifiScanHint">扫描到 5 个网络，选择后填写密码保存</div>
  <div class="row">
    <div class="chk on" id="staOption"><span class="bx" id="staCheckbox"></span><span>STA模式</span></div>
    <div class="chk on" id="apOption"><span class="bx" id="apCheckbox"></span><span>AP模式</span></div>
  </div>
  <div class="row"><button class="btn g p" id="saveConfig">💾 保存</button><button class="btn r dgr" id="restartDevice">🔄 重启设备</button></div>
  <div class="cs" id="wifiConfigStatus"></div>
  <div class="st">📁 已保存的网络 <span id="wifiNetCount">3/6</span></div>
  <div class="lst" id="wifiSavedList">
    <div class="it cur"><span class="dot g"></span><span class="nm">LAND_5G</span><span class="mb ab g">当前</span><button class="mb ab" data-act="up" disabled title="上移（提高优先级）">↑</button><button class="mb ab" data-act="down" title="下移（降低优先级）">↓</button><button class="mb ab" data-act="pw" title="修改密码">🔑</button><button class="mb ab d r" data-act="del" title="删除">✕</button></div>
    <div class="it"><span class="dot"></span><span class="nm">ChinaNet-hK7x <small>● 2</small></span><button class="mb ab" data-act="use">连接</button><button class="mb ab" data-act="up" title="上移（提高优先级）">↑</button><button class="mb ab" data-act="down" title="下移（降低优先级）">↓</button><button class="mb ab" data-act="pw" title="修改密码">🔑</button><button class="mb ab d r" data-act="del" title="删除">✕</button></div>
    <div class="it"><span class="dot"></span><span class="nm">CMCC-9f2a</span><button class="mb ab" data-act="use">连接</button><button class="mb ab" data-act="up" title="上移（提高优先级）">↑</button><button class="mb ab" data-act="down" disabled title="下移（降低优先级）">↓</button><button class="mb ab" data-act="pw" title="修改密码">🔑</button><button class="mb ab d r" data-act="del" title="删除">✕</button></div>
  </div>
</div></div></section>
<section class="p accordion-panel"><div class="ph accordion-header"><span class="ar">▼</span><span class="ti">☁ 巴法云 MQTT</span>
<div class="sw" onclick="event.stopPropagation()"><label class="sw"><input type="checkbox" id="mqttToggle" checked><span class="tr"></span></label>
<span id="mqttToggleStatus">开</span></div></div>
<div class="pb accordion-body"><div class="in">
  <div data-sk="0 1" class="st">👥 云平台连接</div>
  <div data-sk="4" class="st">■ CLOUD LINK</div>
  <div data-sk="3" class="st">Cloud Link</div>
  <div data-sk="2" class="st">CLOUD LINK</div>
  <div class="ws" id="mqttConnStatus">已连接 · 最近消息 {"on":true}</div>
  <div class="st">服务器地址</div>
  <div class="row"><input type="text" id="mqttServer" style="flex:1" value="bemfa.com" placeholder="bemfa.com" inputmode="url"></div>
  <div class="st">端口</div>
  <div class="row"><input type="number" id="mqttPort" style="flex:1" value="9501" placeholder="9501" inputmode="numeric"></div>
  <div class="st">Client ID</div>
  <div class="row"><input type="text" id="mqttClientId" value="abc123def456" style="flex:1" placeholder="巴法云 Client ID"></div>
  <div class="st">订阅主题</div>
  <div class="row"><input type="text" id="mqttTopic" value="light002" style="flex:1" placeholder="订阅主题"></div>
  <button class="btn g p" id="saveMqttConfig" style="width:100%">💾 保存 MQTT 配置</button>
  <div class="cs" id="mqttConfigStatus"></div>
</div></div></section>
<section class="p accordion-panel"><div class="ph accordion-header"><span class="ar">▼</span><span class="ti">🔄 OTA 升级</span><span class="tg i" id="otaVersionTag">固件</span></div>
<div class="pb accordion-body"><div class="in">
  <div class="st">固件下载链接</div>
  <div class="row"><input type="text" id="otaUrl" style="flex:1" placeholder="粘贴 .bin 固件下载链接"></div>
  <div class="row"><span style="flex:1">⏰ 每天定时自动升级<small style="opacity:.6"> 固件有更新才会刷入</small></span>
  <input type="number" id="otaHour" min="0" max="23" value="4" style="width:56px"><span class="lb" style="min-width:auto">点</span>
  <div class="sw" onclick="event.stopPropagation()"><label class="sw"><input type="checkbox" id="otaAutoToggle"><span class="tr"></span></label></div></div>
  <div class="st">自动升级固件链接（留空 = 巴法云内置链接）</div>
  <div class="row"><input type="text" id="otaAutoUrl" style="flex:1" placeholder="http://bin.bemfa.com/..."></div>
  <div class="row"><button class="btn" id="otaCheckBtn" style="width:100%">🔍 检查更新</button></div>
  <div class="cs" id="otaCheckInfo">尚未检查</div>
  <button class="btn g p" id="otaUpgrade" style="width:100%">⬆️ 一键升级</button>
  <div class="cs" id="otaStatus"></div>
  <div class="opwrap" id="otaBarWrap" style="display:none"><div class="opbar" id="otaBar"></div></div>
  <div class="hint">升级时会自动下载固件并重启设备，过程中请勿断电；失败会自动回退到当前固件。</div>
</div></div></section>
<section class="p accordion-panel skin-panel"><div class="ph accordion-header"><span class="ar">▼</span><span class="ti">🎨 界面皮肤</span><span class="tg" id="skinName">新粗野</span></div>
<div class="pb accordion-body"><div class="in">
  <div class="skbar" id="skinBar"></div>
  <div class="sktip">点一下立即切换外观，选择会保存在这台设备上。也可以用快捷键 1~5 切换。</div>
</div></div></section>
</div>
</div>
<script>
    /* 设备给的是"距上次收到消息多久"，换算成本机时钟的绝对时刻 */
    function mqttTimeText(agoMs){
      agoMs=parseInt(agoMs||0,10);
      if(!agoMs)return '';
      var d=new Date(Date.now()-agoMs);
      function p(n){return (n<10?'0':'')+n}
      return ' <span class="mstamp">\uFF08'+p(d.getHours())+':'+p(d.getMinutes())+':'+p(d.getSeconds())+'\uFF09</span>';
    }
    /* 皮肤的选中样式挂在 .chk.on 上，原网页 JS 切换的是 .checked，两边对不上 →
       这里把状态同时写到父元素的 .on，外观才会跟着变（否则点了没反应） */
    function syncModeBox(id,on){
      var el=document.getElementById(id);
      if(!el)return;
      el.classList.toggle('checked',!!on);
      if(el.parentNode&&el.parentNode.classList)el.parentNode.classList.toggle('on',!!on);
    }

    let timerInterval = null;
    let remainingTime = 0;
    let staEnabled = true;
    let apEnabled = true;
    let reverseState = false;
    /* 竞态修复：节流与活跃控件跟踪，防止拖动滑块时被滞后的SSE状态返回覆盖 */
    let activeControl = null;
    let activeTimeout = null;
    let throttleTimer = null;
    let pendingData = null;

    /* ======= 折叠面板切换 ======= */
    document.querySelectorAll('.accordion-header').forEach(function(hdr){
      hdr.addEventListener('click',function(e){
        var panel=this.parentElement;
        var body=panel.querySelector('.accordion-body');
        if(body.classList.contains('open')){
          body.classList.remove('open');
          panel.classList.remove('open');
        }else{
          body.classList.add('open');
          panel.classList.add('open');
        }
      });
    });

    function updateReverseStatus(){
      document.getElementById('reverseStatus').textContent=reverseState?"反向开":"反向关";
    }

    /* 巴法云总开关右侧的状态文字：不止"开/关"，还要反映真实连接状态 */
    /* 温湿度卡片显示：未启用时显示 --，启用时跟随 /state 的温度湿度 */
    function updateDhtDisplay(data){
      var tg=document.getElementById('dhtTag');
      var tEl=document.getElementById('dhtTemp');
      var hEl=document.getElementById('dhtHumi');
      if(!tg||!tEl||!hEl)return;
      if(!data.dhtEnabled){
        tg.textContent='未启用';
        tEl.textContent='--';hEl.textContent='--';
        return;
      }
      tg.textContent=(data.temperature||'--')+'℃ / '+(data.humidity||'--')+'%';
      tEl.textContent=data.temperature||'--';
      hEl.textContent=data.humidity||'--';
    }

    function updateMqttToggleStatus(on,connected){
      var el=document.getElementById('mqttToggleStatus');
      if(!el)return;
      if(!on){el.textContent="\u5173";return;}
      el.textContent=connected?"\u5DF2\u8FDE\u63A5":"\u8FDE\u63A5\u4E2D";
    }

    /* 云平台连接状态（面板内那一行，随 /state 每 2 秒刷新）
       —— 已连接时显示"已连接 + 上一次接收内容"，不再一直停在"正在连接…" */
    function updateMqttConnStatus(enabled,connected,lastMsg,msgCount,agoMs){
      var el=document.getElementById('mqttConnStatus');
      if(!el)return;
      if(!enabled){
        el.innerHTML='<div class="wifi-disconnected">\u2716 \u5DF2\u5173\u95ED</div>';
        return;
      }
      var h=connected
        ?'<div class="wifi-connected">\u2714 \u5DF2\u8FDE\u63A5</div>'
        :'<div class="wifi-temp">\u23F3 \u5DF2\u5F00\u542F\uFF0C\u6B63\u5728\u8FDE\u63A5\u2026</div>';
      h+='<div>\u4E0A\u4E00\u6B21\u63A5\u6536\u5185\u5BB9: '
        +(msgCount?('<b>'+esc(lastMsg||'\uFF08\u7A7A\u6D88\u606F\uFF09')+'</b>'+mqttTimeText(agoMs)):'\u6682\u65E0')
        +'</div>';
      el.innerHTML=h;
    }

    function updateActiveButton(mode){
      document.querySelectorAll('.mode-btn').forEach(function(btn){
        btn.classList.toggle('active',btn.dataset.mode===mode);
      });
      var cc=document.getElementById('colorControl');
      var showColor=(mode==='solid'||mode==='theater'||mode==='breathing'||mode==='step'||mode==='heartbeat');
      cc.style.display=showColor?'block':'none';
      document.getElementById('singleControl').style.display=(mode==='single')?'block':'none';
    }

    function updateStatus(message){
      var el=document.getElementById('status');
      el.textContent=message;
      el.style.color='#ff8c00';
      setTimeout(function(){
        if(el.textContent===message){
          el.textContent='\u5C31\u7EEA';
          el.style.color='#8899bb';
        }
      },1000);
    }

    function updateTimerDisplay(hours,minutes,seconds){
      var d=document.getElementById('timerDisplay');
      if(hours===0&&minutes===0&&seconds===0){
        d.textContent='\u5DF2\u5B8C\u6210';
        d.className='accordion-tag idle';
      }else if(hours===0&&minutes===0&&seconds===0){
        d.textContent='\u672A\u8BBE\u7F6E';
        d.className='accordion-tag idle';
      }else{
        d.textContent=[hours,minutes,seconds].map(function(v){return String(v).padStart(2,'0')}).join(':');
        d.className='accordion-tag';
      }
      if(!hours&&!minutes&&!seconds&&d.textContent==='\u672A\u8BBE\u7F6E'){
        d.className='accordion-tag idle';
      }
    }

    /* ===== 上电强制开 AP 窗口：剩余时间按本地时钟每秒自减 =====
       设备端只在窗口"结束"的那一刻推一次状态，中间不会推；
       若直接用 /state 的值渲染，倒计时就是静态的（必须刷新网页才变）。 */
    function apForceRemainNow(){
      if(apForceBaseSec<=0)return 0;
      var left=apForceBaseSec-Math.floor((Date.now()-apForceBaseAt)/1000);
      return left>0?left:0;
    }
    function apForceHintText(sec){
      return '\u23F1 \u4E0A\u7535\u540E\u524D 5 \u5206\u949FAP\u5F3A\u5236\u5F00\u542F\uFF08\u5269\u4F59 '
        +Math.floor(sec/60)+' \u5206 '+(sec%60)+' \u79D2\uFF09';
    }
    /* 每秒只改那一行文字，不重建整个状态区（否则会打断用户其他操作） */
    setInterval(function(){
      var el=document.getElementById('apForceHint');
      if(!el)return;
      var left=apForceRemainNow();
      el.textContent=left>0
        ?apForceHintText(left)
        :'\u23F1 \u4E0A\u7535\u540E\u524D 5 \u5206\u949FAP\u5F3A\u5236\u5F00\u542F\u5DF2\u7ED3\u675F';
    },1000);

    function updateWiFiStatus(connected,apMode,ip,apIp,ssid,apTemp,trying){
      var s=document.getElementById('wifiStatus');
      /* 卡片右上角 tag：显示当前实际连上的 SSID；没连上就说明当前处于哪种状态 */
      var tag=document.getElementById('wifiSsidTag');
      if(tag){
        if(connected&&ssid){
          tag.textContent=ssid;
          tag.className='accordion-tag ssid-tag';       /* 橙底 = 已连上 */
          tag.title='\u5F53\u524D\u8FDE\u63A5: '+ssid;
        }else{
          tag.textContent=apMode?'\u70ED\u70B9\u6A21\u5F0F':'\u672A\u8FDE\u63A5';
          tag.className='accordion-tag idle ssid-tag';  /* 灰底 = 未连上 */
          tag.title=apMode?'\u672A\u8FDE\u63A5WiFi\uFF0C\u5F53\u524D\u4EC5\u70ED\u70B9\u53EF\u7528':'\u672A\u8FDE\u63A5WiFi';
        }
      }
      /* 断网轮询期间，告诉用户设备正在试哪个网络 */
      var extra=trying?('<div class="wifi-temp">\u23F3 \u6B63\u5728\u5C1D\u8BD5\u8FDE\u63A5: '+esc(trying)+'</div>'):'';
      /* 上电后前 5 分钟 AP 强制开（与「AP模式」开关无关）；剩余时间本地每秒自减 */
      var forceLeft=apForceRemainNow();
      var forceHint=(apMode&&forceLeft>0)
        ?'<div class="wifi-temp" id="apForceHint">'+apForceHintText(forceLeft)+'</div>'
        :'';
      if(connected){
        s.innerHTML='<div class="wifi-connected">\u2714 \u5DF2\u8FDE\u63A5\u81F3WiFi</div><div>SSID: '+esc(ssid)+'</div><div>IP\u5730\u5740: '+ip+'</div>'+(apMode?'<div>\u70ED\u70B9IP: '+apIp+'</div>':'')+forceHint;
      }else if(apMode){
        s.innerHTML=(apTemp
          ?'<div class="wifi-disconnected">\u2716 \u672A\u8FDE\u63A5WiFi</div><div class="wifi-temp">\u26A0 \u5DF2\u4E34\u65F6\u5F00\u542F\u70ED\u70B9</div>'
          :'<div class="wifi-disconnected">\u2716 \u672A\u8FDE\u63A5WiFi</div><div>\u5DF2\u5F00\u542F\u70ED\u70B9\u6A21\u5F0F</div>')
          +'<div>\u70ED\u70B9IP: '+apIp+'</div><div>SSID: LAND_LED</div><div>\u5BC6\u7801: 12345678</div>'+forceHint+extra;
      }else{
        s.innerHTML='<div class="wifi-disconnected">\u2716 \u672A\u8FDE\u63A5WiFi</div><div>\u8BF7\u914D\u7F6EWiFi</div>'+extra;
      }
    }

    function updateModeCheckboxes(sta,ap){
      staEnabled=sta;apEnabled=ap;
      syncModeBox('staCheckbox',sta);
      syncModeBox('apCheckbox',ap);
      if(!sta&&!ap){staEnabled=true;syncModeBox('staCheckbox',true);}
    }

    /* ==================== WiFi 网络配置 ====================
     * - 打开网页时自动异步扫描一次，扫描期间下拉框显示"正在加载…"；
     * - 扫描结果按信号强度排序追加，末尾保留"手动输入"项；
     * - 下方"已保存的网络"列表支持：设为当前 / 改密码 / 删除；
     * - 设备当前使用的网络在下拉框中标注"当前使用"。
     * ====================================================== */
    var knownSsid='';                  /* 设备当前使用的 SSID */
    var MANUAL_VALUE='__manual__';
    var LOADING_VALUE='__loading__';
    var savedNets=[];                  /* 已保存网络 [{ssid,active,fails}] */
    var wifiMaxNets=6;                 /* 可保存网络数量上限（由设备下发） */
    var FAIL_MARK=2;                   /* 连续失败达到该次数就在列表里标灰点（与固件 WIFI_FAIL_MARK 一致） */
    var editPwIdx=-1;                  /* 正在改密码的条目下标，-1=无 */
    var ssidLabels={};                 /* SSID -> 下拉项原始文案（用于还原"当前使用"标记） */
    var ssidPinned=false;              /* 用户手动选过网络后，不再让状态同步把下拉框抢回去 */

    /* 上电强制开 AP 窗口的倒计时基准：
       /state 是**事件驱动**的（只在设备状态变化时才推），所以剩余时间不会自己变。
       这里以最近一次 /state 下发的剩余秒数为基准，用本地时钟每秒自减显示。 */
    var apForceBaseSec=0;              /* 基准剩余秒数（来自 /state 的 apForceRemainMs） */
    var apForceBaseAt=0;               /* 取到基准值时的本地时间戳(ms) */

    /* 密码按网络各记一份：在下拉框里换网络时密码不会被清空，
       换回来还是原来那个密码（手动输入过的也记在当前网络名下）。 */
    var pwMap={};                      /* SSID -> 密码 */
    var pwUserEdit={};                 /* SSID -> true 表示用户手动改过，不被 /state 覆盖 */
    var pwCurSsid='';                  /* 密码框当前对应的 SSID */

    /* HTML 转义：SSID 里可能含 & < > " 等字符，直接拼进 innerHTML 会破坏结构 */
    function esc(s){
      return String(s==null?'':s)
        .replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;')
        .replace(/"/g,'&quot;').replace(/'/g,'&#39;');
    }

    /* 下拉框当前选中的真实 SSID（"手动输入…"/"正在加载…"不算） */
    function selSsidValue(){
      var sel=document.getElementById('wifiSSID');
      if(!sel)return '';
      var v=sel.value;
      return (v===MANUAL_VALUE||v===LOADING_VALUE)?'':v;
    }

    /* 切换网络时按 SSID 存取密码框内容：
       切走时把框里的密码存进 pwMap，切回时原样还回来，绝不无脑清空。
       只有"从没见过的网络"才显示为空。选到"手动输入…"时保持原样，
       交给用户自己填。 */
    function switchPwContext(){
      var el=document.getElementById('wifiPassword');
      if(!el)return;
      var now=selSsidValue();
      if(now===pwCurSsid)return;
      if(pwCurSsid)pwMap[pwCurSsid]=el.value;   /* 记住刚才那个网络的输入 */
      pwCurSsid=now;
      if(!now)return;                            /* 手动输入：不动密码框 */
      el.value=(pwMap[now]!==undefined)?pwMap[now]:'';
    }

    function onSsidChange(){
      var sel=document.getElementById('wifiSSID');
      var man=document.getElementById('wifiSSIDManual');
      if(!sel||!man)return;
      if(sel.value===MANUAL_VALUE){
        man.style.display='';
      }else{
        man.style.display='none';
        man.value='';
      }
      switchPwContext();
    }

    /* 扫描进行中：下拉框显示"正在加载…" */
    function showSsidLoading(){
      var sel=document.getElementById('wifiSSID');
      if(!sel)return;
      sel.innerHTML='';
      var o=document.createElement('option');
      o.value=LOADING_VALUE;
      o.textContent='\u6B63\u5728\u52A0\u8F7D\u2026';
      sel.appendChild(o);
      sel.value=LOADING_VALUE;
      var man=document.getElementById('wifiSSIDManual');
      if(man){man.style.display='none';man.value='';}
    }

    /* 彻底重建下拉列表（扫描完成后调用） */
    function rebuildSsidSelect(networks){
      var sel=document.getElementById('wifiSSID');
      if(!sel)return;
      var cur=sel.value;
      /* 用户之前选中的真实 SSID；处于手动/加载态时回落到设备当前 SSID */
      var want=(cur&&cur!==MANUAL_VALUE&&cur!==LOADING_VALUE)?cur:knownSsid;

      sel.innerHTML='';
      ssidLabels={};
      var seen={};

      function add(v,label){
        if(!v||seen[v])return;
        seen[v]=1;
        ssidLabels[v]=label;
        var o=document.createElement('option');
        o.value=v;o.textContent=label;
        sel.appendChild(o);
      }

      if(knownSsid){
        ssidLabels[knownSsid]=knownSsid;
        add(knownSsid,knownSsid+'  (\u5F53\u524D\u4F7F\u7528)');
      }
      if(networks&&networks.length){
        for(var i=0;i<networks.length;i++){
          var n=networks[i];
          if(!n.ssid)continue;
          add(n.ssid,n.ssid+(n.open?' \uD83D\uDD13':'\uD83D\uDD12')+' '+n.rssi+'dBm');
        }
      }

      /* 用户当前选中的网络即使不在本次扫描结果里也保留一项：
         否则重建列表会把选择丢掉，密码框也跟着切走。 */
      if(want)add(want,want);

      var m=document.createElement('option');
      m.value=MANUAL_VALUE;
      m.textContent='\u270E \u624B\u52A8\u8F93\u5165\u2026';
      sel.appendChild(m);

      sel.value=seen[want]?want:(knownSsid?knownSsid:MANUAL_VALUE);
      onSsidChange();
    }

    /* 状态同步：只保证"设备当前 SSID"在列表里并被选中，不重建整个列表
       （/state 会被 SSE 频繁推送，重建列表会打断用户正在做的选择） */
    function syncSsidSelect(ssid){
      var newSsid=ssid||'';
      /* 设备"真的换网了"（例：网络1 突然断开，断网轮询连上了网络2）：
         此时必须解除用户的手动选择，让下拉框跟上实际在用的网络，
         否则下拉框会一直停在旧网络、和上面的连接状态互相矛盾。
         注意只在 SSID 发生变化时解除 —— 平时 2 秒一次的推送不打扰用户。 */
      if(newSsid!==knownSsid&&newSsid!=='')ssidPinned=false;
      knownSsid=newSsid;

      /* 扫描进行中：只记录当前 SSID，不动下拉框 —— 否则会把
         "正在加载…" 顶掉（/state 每 2 秒推送一次）。扫描结束后
         rebuildSsidSelect() 会统一重建。 */
      if(scanPolling)return;

      var sel=document.getElementById('wifiSSID');
      if(!sel)return;

      if(!knownSsid){
        /* 未配置：列表为空（或只有加载占位）时建一个基础列表 */
        if(sel.options.length<=1)rebuildSsidSelect(null);
        return;
      }

      /* 先把旧的"当前使用"标记还原成原始文案 */
      for(var j=0;j<sel.options.length;j++){
        var op=sel.options[j];
        if(op.value===MANUAL_VALUE||op.value===LOADING_VALUE)continue;
        if(op.value===knownSsid)continue;
        if(op.textContent.indexOf('(\u5F53\u524D\u4F7F\u7528)')>=0&&ssidLabels[op.value]){
          op.textContent=ssidLabels[op.value];
        }
      }

      var found=null;
      for(var i=0;i<sel.options.length;i++){
        if(sel.options[i].value===knownSsid){found=sel.options[i];break;}
      }
      if(!found){
        var o=document.createElement('option');
        o.value=knownSsid;
        sel.insertBefore(o,sel.firstChild);
        found=o;
      }
      found.textContent=knownSsid+'  (\u5F53\u524D\u4F7F\u7528)';

      /* 用户已经手动选定某个网络：不要把它抢回设备当前网络。
         （"保存"现在只保存不连接，抢回去会让用户刚选好就被刷新掉）
         一旦选中的正是设备当前网络，说明用户也不再需要保持选择了。 */
      if(ssidPinned&&knownSsid&&selSsidValue()===knownSsid)ssidPinned=false;

      /* 用户正在手动挑选时不要抢回选中项 */
      if(activeControl!=='wifiSSID'&&!ssidPinned){
        sel.value=knownSsid;
        switchPwContext();   /* 选中项变了，密码框跟着切到对应网络 */
      }
    }

    /* ---------------- 已保存的网络列表 ---------------- */
    function renderSavedList(){
      var box=document.getElementById('wifiSavedList');
      var cnt=document.getElementById('wifiNetCount');
      if(!box)return;
      if(cnt)cnt.textContent=savedNets.length?('('+savedNets.length+'/'+wifiMaxNets+')'):'';

      /* 正在改密码时不要重建列表：/state 每 2 秒推送一次，重建会把输入框
         的内容和焦点清掉。等用户点 ✓ / ✕ 后再刷新。 */
      if(editPwIdx>=0 && box.querySelector('#savedPwInput'))return;

      if(!savedNets.length){
        box.innerHTML='<div class="ssid-hint">\u6682\u65E0\u5DF2\u4FDD\u5B58\u7684\u7F51\u7EDC\uFF0C\u5728\u4E0A\u9762\u9009\u62E9\u5E76\u4FDD\u5B58\u5373\u53EF\u6DFB\u52A0</div>';
        return;
      }

      var h='';
      for(var i=0;i<savedNets.length;i++){
        var n=savedNets[i]||{};
        h+='<div class="saved-row'+(n.active?' cur':'')+'" data-idx="'+i+'">';
        h+='<span class="saved-pri">'+(i+1)+'</span>';
        /* 连续失败次数达到阈值 → 标一个灰点，提示"密码可能填错/信号太差" */
        var fails=parseInt(n.fails||0);
        var dotOK=(fails>=FAIL_MARK);
        h+='<span class="saved-ssid'+(dotOK?' ssid-dim':'')+'">'+esc(n.ssid)+'</span>';
        if(dotOK){
          h+='<span class="saved-fail" title="\u8FDE\u7EED '+fails+' \u6B21\u8FDE\u63A5\u5931\u8D25\uFF0C\u8BF7\u68C0\u67E5\u5BC6\u7801\u6216\u4FE1\u53F7">\u25CF '+fails+'</span>';
        }
        if(editPwIdx===i){
          h+='<input type="password" class="saved-pw" id="savedPwInput" placeholder="\u65B0\u5BC6\u7801">';
          h+='<button type="button" class="mini-btn" data-act="savepw">\u2713</button>';
          h+='<button type="button" class="mini-btn" data-act="cancel">\u2715</button>';
        }else{
          if(n.active){
            h+='<span class="saved-badge">\u5F53\u524D</span>';
          }else{
            h+='<button type="button" class="mini-btn" data-act="use">\u8FDE\u63A5</button>';
          }
          /* 顺序 = 优先级：↑ 提高、↓ 降低 */
          h+='<button type="button" class="mini-btn" data-act="up"'
            +(i===0?' disabled':'')+' title="\u4E0A\u79FB\uFF08\u63D0\u9AD8\u4F18\u5148\u7EA7\uFF09">\u2191</button>';
          h+='<button type="button" class="mini-btn" data-act="down"'
            +(i===savedNets.length-1?' disabled':'')+' title="\u4E0B\u79FB\uFF08\u964D\u4F4E\u4F18\u5148\u7EA7\uFF09">\u2193</button>';
          h+='<button type="button" class="mini-btn" data-act="pw" title="\u4FEE\u6539\u5BC6\u7801">\uD83D\uDD11</button>';
          h+='<button type="button" class="mini-btn del" data-act="del" title="\u5220\u9664">\u2715</button>';
        }
        h+='</div>';
      }
      box.innerHTML=h;
    }

    /* 统一的网络操作请求：/control?<action>=<idx>[&其它参数] */
    function wifiNetAction(action,idx,extra){
      var st=document.getElementById('wifiConfigStatus');
      var q={};
      q[action]=idx;
      if(extra){for(var k in extra)q[k]=extra[k];}
      if(st)st.textContent='\u6B63\u5728\u5904\u7406\u2026';
      fetch('/control?'+new URLSearchParams(q).toString())
        .then(function(r){return r.text().then(function(t){return {ok:r.ok,txt:t};});})
        .then(function(res){
          if(st)st.textContent=res.txt||(res.ok?'\u5DF2\u5B8C\u6210':'\u64CD\u4F5C\u5931\u8D25');
          editPwIdx=-1;
          fetchCurrentState();
          renderSavedList();
        })
        .catch(function(e){
          if(st)st.textContent='\u64CD\u4F5C\u5931\u8D25: '+e.message;
        });
    }

    /* 扫描附近 WiFi：/scanwifi 只负责启动，随后轮询 /wifilist 取结果 */
    var scanPolling=false;
    function startWifiScan(){
      var btn=document.getElementById('scanWifi');
      var hint=document.getElementById('wifiScanHint');
      if(scanPolling||!btn)return;
      scanPolling=true;
      btn.classList.add('scanning');
      if(hint)hint.textContent='\u6B63\u5728\u52A0\u8F7D\u2026';
      showSsidLoading();
      fetch('/scanwifi').then(function(r){return r.json();}).then(function(res){
        if(res.status==='failed')throw new Error('\u542F\u52A8\u5931\u8D25');
        pollWifiList(0);
      }).catch(function(e){
        scanPolling=false;
        btn.classList.remove('scanning');
        /* 通过设备热点访问时，扫描瞬间可能让页面短暂断开 —— 点了刷新按钮重试即可 */
        if(hint)hint.textContent='\u626B\u63CF\u5931\u8D25: '+e.message+'\uFF0C\u53EF\u70B9\u53F3\u4FA7 \uD83D\uDD04 \u91CD\u8BD5';
        rebuildSsidSelect(null);
      });
    }

    function pollWifiList(tries){
      var btn=document.getElementById('scanWifi');
      var hint=document.getElementById('wifiScanHint');
      if(tries>25){        /* 25 × 700ms ≈ 17 秒超时 */
        scanPolling=false;
        if(btn)btn.classList.remove('scanning');
        if(hint)hint.textContent='\u626B\u63CF\u8D85\u65F6\uFF0C\u8BF7\u70B9\u53F3\u4FA7\u5237\u65B0\u6309\u94AE\u91CD\u8BD5';
        rebuildSsidSelect(null);
        return;
      }
      setTimeout(function(){
        fetch('/wifilist').then(function(r){return r.json();}).then(function(res){
          if(res.status==='running'){pollWifiList(tries+1);return;}
          var nets=res.networks||[];
          rebuildSsidSelect(nets);
          scanPolling=false;
          if(btn)btn.classList.remove('scanning');
          if(hint)hint.textContent=nets.length
            ?('\u626B\u63CF\u5230 '+nets.length+' \u4E2A\u7F51\u7EDC\uFF0C\u9009\u62E9\u540E\u586B\u5199\u5BC6\u7801\u4FDD\u5B58')
            :'\u672A\u626B\u63CF\u5230\u7F51\u7EDC\uFF0C\u53EF\u9009"\u624B\u52A8\u8F93\u5165"';
        }).catch(function(){
          scanPolling=false;
          if(btn)btn.classList.remove('scanning');
          if(hint)hint.textContent='\u83B7\u53D6\u626B\u63CF\u7ED3\u679C\u5931\u8D25\uFF0C\u8BF7\u91CD\u8BD5';
        });
      },700);
    }

    /* 单色动画标签映射和按钮高亮（全局作用域，供 fetchCurrentState 调用） */
    var animLabelMap={
      random:'随机溶解',
      sequential:'顺序点亮',
      center:'中心扩散',
      ends:'两端向内',
      fade:'全局渐亮',
      none:'无动画'
    };
    function updateAnimButtons(mode){
      var btns=document.querySelectorAll('.anim-btn');
      if(!btns.length)return;
      btns.forEach(function(btn){
        btn.classList.toggle('active',btn.dataset.anim===mode);
      });
      var tag=document.getElementById('animModeTag');
      if(tag)tag.textContent=animLabelMap[mode]||mode;
    }

    /* \u6807\u8BB0\u7528\u6237\u6B63\u5728\u64CD\u4F5C\u67D0\u4E2A\u63A7\u4EF6\uFF0C\u72B6\u6001\u8FD4\u56DE\u65F6\u8DF3\u8FC7\u5B83\u7684\u66F4\u65B0 */
    function markActive(controlId){
      activeControl=controlId;
      if(activeTimeout)clearTimeout(activeTimeout);
      activeTimeout=setTimeout(function(){activeControl=null;},2000);
    }

    /* \u53D1\u9001\u547D\u4EE4\uFF08\u65E0\u8282\u6D41\uFF0C\u7528\u4E8E\u6309\u94AE/\u4E00\u6B21\u6027\u64CD\u4F5C\uFF09 */
    function sendCommand(data){
      doSend(data);
    }

    /* \u53D1\u9001\u547D\u4EE4\uFF08\u5E26\u8282\u6D41\uFF0C\u7528\u4E8E\u6ED1\u5757/\u989C\u8272\u8FDE\u7EED\u62D6\u52A8\uFF09 */
    function sendCommandThrottled(data,controlId){
      markActive(controlId);
      pendingData=data;
      if(throttleTimer)return;
      doSend(data);
      throttleTimer=setTimeout(function(){
        throttleTimer=null;
        if(pendingData){
          var d=pendingData;pendingData=null;
          doSend(d);
        }
      },50);
    }

    function doSend(data){
      updateStatus('\u53D1\u9001\u547D\u4EE4...');
      var url='/control?'+new URLSearchParams(data).toString();
      fetch(url).then(function(r){
        if(r.ok)return r.text();
        throw new Error('\u7F51\u7EDC\u9519\u8BEF: '+r.status);
      }).then(function(t){
        updateStatus(t);
        /* 本机主动改的，标记签名为「未知」，下一个轮询不弹「状态已加载」 */
        _lastStateSig='__local__';
      }).catch(function(e){
        updateStatus('\u9519\u8BEF: '+e.message);
      });
    }

    /* 灯带设置卡片右上角的灯珠数量 tag（只读展示，跟服务端生效值走） */
    function updateLedCountTag(n){
      var t=document.getElementById('ledCountTag');
      if(t)t.textContent=((n===0||n)?n:'--')+'\u9897';
    }

    var _lastStateSig=null;
    var OTA_DEFAULT_URL='http://bin.bemfa.com/b/253904/1BcOTllNTViNDMwN2U3NDA2ZDhjOGVjZjM5MTIyYmRkODk=xuexiaoden002.bin';
    function fetchCurrentState(){
      fetch('/state').then(function(r){
        if(!r.ok)throw new Error('\u7F51\u7EDC\u9519\u8BEF: '+r.status);
        return r.json();
      }).then(function(data){
        updateActiveButton(data.mode);
        /* 竞态修复：用户正在拖动的控件跳过更新，防止滞后SSE覆盖当前值 */
        if(activeControl!=='colorPicker'){
          document.getElementById('colorPicker').value='#'+data.color;
        }
        if(activeControl!=='brightness'){
          document.getElementById('brightness').value=data.brightness;
          document.getElementById('brightnessValue').textContent=data.brightness+'%';
        }
        if(activeControl!=='speed'){
          document.getElementById('speed').value=data.speed;
          document.getElementById('speedValue').textContent=data.speed+'%';
        }
        /* 灯珠数量输入框在卡片内，用户可能正在改，所以和密码框一样：
           获得焦点时不覆盖 */
        var lcEl=document.getElementById('ledCount');
        if(lcEl&&document.activeElement!==lcEl)lcEl.value=data.ledCount;
        /* 卡片右上角的 tag 只做展示，永远跟服务端的生效值走 */
        updateLedCountTag(data.ledCount);

        if(data.timerActive){
          var h=Math.floor(data.timerRemaining/3600);
          var m=Math.floor((data.timerRemaining%3600)/60);
          var s=data.timerRemaining%60;
          updateTimerDisplay(h,m,s);
          if(timerInterval)clearInterval(timerInterval);
          remainingTime=data.timerRemaining;
          timerInterval=setInterval(function(){
            remainingTime--;
            if(remainingTime<=0){clearInterval(timerInterval);updateTimerDisplay(0,0,0);}
            else{updateTimerDisplay(Math.floor(remainingTime/3600),Math.floor((remainingTime%3600)/60),remainingTime%60);}
          },1000);
        }else{
          var td=document.getElementById('timerDisplay');
          td.textContent='\u672A\u8BBE\u7F6E';
          td.className='accordion-tag idle';
          if(timerInterval)clearInterval(timerInterval);
        }

        /* 强制开 AP 窗口：以设备下发的剩余时间为基准，之后由本地每秒自减
           （0 = 窗口已结束，状态区里那一行会自动消失） */
        apForceBaseSec=Math.floor((data.apForceRemainMs||0)/1000);
        apForceBaseAt=Date.now();
        updateWiFiStatus(data.wifiConnected,data.apModeActive,data.ipAddress,data.apIpAddress,data.ssid,data.apTempEnabled,data.wifiTrying||'');
        syncSsidSelect(data.ssid||'');
        updateModeCheckboxes(data.staEnabled,data.apEnabled);

        /* 已保存的多组网络（供下方列表渲染） */
        savedNets=data.wifiNets||[];
        if(data.wifiMax)wifiMaxNets=data.wifiMax;

        /* 密码记忆：每个网络各记一份（用户手动改过的以用户输入为准）。
           这样在下拉框里换网络，密码不会丢，换回来还在。 */
        for(var ni=0;ni<savedNets.length;ni++){
          var sn=savedNets[ni];
          if(sn&&sn.ssid&&!pwUserEdit[sn.ssid])pwMap[sn.ssid]=sn.password||'';
        }
        if(data.ssid&&!pwUserEdit[data.ssid])pwMap[data.ssid]=data.password||'';

        /* 密码框：只有"下拉框选中的正是设备当前网络"时才回填，
           且用户正在输入（获得焦点）时不动 —— 否则 /state 每 2 秒推一次，
           会把用户为别的网络准备的密码冲掉。 */
        var pwEl=document.getElementById('wifiPassword');
        if(pwEl&&document.activeElement!==pwEl&&selSsidValue()===data.ssid){
          pwEl.value=(pwMap[data.ssid]!==undefined)?pwMap[data.ssid]:(data.password||'');
        }
        renderSavedList();
        var _mc=document.getElementById('mqttClientId');
        if(_mc&&document.activeElement!==_mc)_mc.value=data.mqttClientId||'';
        var _mt=document.getElementById('mqttTopic');
        if(_mt&&document.activeElement!==_mt)_mt.value=data.mqttTopic||'';
        var _ms=document.getElementById('mqttServer');
        if(_ms&&document.activeElement!==_ms)_ms.value=data.mqttServer||'';
        var _mp=document.getElementById('mqttPort');
        if(_mp&&document.activeElement!==_mp)_mp.value=data.mqttPort||'';
        document.getElementById('dhtToggle').checked=!!data.dhtEnabled;
        document.getElementById('irToggle').checked=!!data.irEnabled;
        var _at=document.getElementById('otaAutoToggle');
        if(_at)_at.checked=!!data.otaAuto;
        var _oh=document.getElementById('otaHour');
        if(_oh&&document.activeElement!==_oh)_oh.value=data.otaHour||4;
        var _ou2=document.getElementById('otaAutoUrl');
        if(_ou2&&document.activeElement!==_ou2)_ou2.value=data.otaAutoUrl||'';
        updateDhtDisplay(data);
        document.getElementById('mqttToggle').checked=!!data.mqttEnabled;
        updateMqttToggleStatus(!!data.mqttEnabled,!!data.mqttConnected);
        updateMqttConnStatus(!!data.mqttEnabled,!!data.mqttConnected,data.mqttLastMsg||'',data.mqttMsgCount||0,data.mqttLastMsgAgo||0);
        reverseState=data.reverse;
        document.getElementById('reverseToggle').checked=reverseState;
        updateReverseStatus();
        if(data.solidAnim)updateAnimButtons(data.solidAnim);
        var _sig=[data.mode,data.color,data.brightness,data.speed,data.timerActive,data.ledCount,data.reverse,data.mqttLastMsg,data.mqttConnected].join('|');
        if(_lastStateSig!==null&&_lastStateSig!=='__local__'&&_sig!==_lastStateSig)updateStatus('\u72B6\u6001\u5DF2\u52A0\u8F7D');
        _lastStateSig=_sig;
      }).catch(function(e){
        console.error(e);
        updateStatus('\u52A0\u8F7D\u72B6\u6001\u5931\u8D25: '+e.message);
        setTimeout(fetchCurrentState,3000);
      });
    }

    document.addEventListener('DOMContentLoaded',function(){
      /* 打开网页即进入"加载中"状态，随后自动异步扫描一次，
         扫描完成后下拉列表自动填充（无需手动点刷新按钮）。 */
      showSsidLoading();
      renderSavedList();

      fetchCurrentState();

      setTimeout(startWifiScan,400);

      
    /* ---- OTA 一键升级（真实进度条：固件通过 /otastatus 上报进度）----
       流程：提交 /ota → 轮询 /otastatus（-1=等待/已重启 0~99=下载 100=写入Flash -2=失败）
       → 看到 100 后设备写入完自动重启（/otastatus 回 -1 且 /state 恢复）→ 显示完成。 */
    var otaUrlEl=document.getElementById('otaUrl');
    var otaBtn=document.getElementById('otaUpgrade');
    var otaSt=document.getElementById('otaStatus');
    var otaBarWrap=document.getElementById('otaBarWrap');
    var otaBar=document.getElementById('otaBar');
    var otaPollTimer=null,otaStartAt=0,otaSaw100=false;
    function otaBarSet(pct){ if(otaBar)otaBar.style.width=pct+'%'; }
    function otaBarReset(ok){
      if(otaPollTimer){clearInterval(otaPollTimer);otaPollTimer=null;}
      if(otaBarWrap)otaBarWrap.style.display='none';
      if(otaBar)otaBar.style.width='0';
      if(otaBtn)otaBtn.disabled=false;
      otaSaw100=false;
      if(ok)setTimeout(fetchCurrentState,1500);
    }
    if(otaUrlEl&&otaBtn){
      if(!otaUrlEl.value)otaUrlEl.value=OTA_DEFAULT_URL;
      otaBtn.addEventListener('click',function(){
        var u=(otaUrlEl.value||'').trim();
        if(!/^https?:\/\//i.test(u)){
          if(otaSt){otaSt.textContent='链接无效，需以 http:// 或 https:// 开头';otaSt.style.color='#e05';}
          return;
        }
        if(otaSt){otaSt.textContent='正在提交升级请求...';otaSt.style.color='';}
        otaBtn.disabled=true;
        otaSaw100=false;
        fetch('/ota?'+new URLSearchParams({url:u}))
          .then(function(r){return r.text();})
          .then(function(t){
            if(otaBarWrap)otaBarWrap.style.display='';
            otaBarSet(0);
            if(otaSt){otaSt.textContent=t+' 等待设备响应…';otaSt.style.color='';}
            otaStartAt=Date.now();
            otaPollTimer=setInterval(function(){
              if(Date.now()-otaStartAt>300000){  // 5 分钟兜底超时
                if(otaSt){otaSt.textContent='✗ 等待超时，请检查固件链接或设备状态';otaSt.style.color='#e05';}
                otaBarReset(false);
                return;
              }
              fetch('/otastatus',{cache:'no-store'})
                .then(function(r){return r.text();})
                .then(function(v){
                  var p=parseInt(v,10);
                  if(p===-2){
                    if(otaSt){otaSt.textContent='✗ 升级失败，设备已回退到当前固件';otaSt.style.color='#e05';}
                    otaBarReset(false);
                    return;
                  }
                  if(p>=100){
                    otaSaw100=true;
                    otaBarSet(100);
                    if(otaSt){otaSt.textContent='下载完成，正在写入 Flash（约 30 秒，请勿断电）…';otaSt.style.color='';}
                    return;
                  }
                  if(p>=0){
                    otaBarSet(Math.max(p,3));
                    var el=Math.round((Date.now()-otaStartAt)/1000);
                    if(otaSt){otaSt.textContent='正在下载固件 '+p+'%（已用时 '+el+' 秒）…';otaSt.style.color='';}
                    return;
                  }
                  /* p===-1：写入完成重启，或还没开始 */
                  if(otaSaw100){
                    fetch('/state',{cache:'no-store'}).then(function(r){
                      if(!r.ok)throw 0;
                      return r.json();
                    }).then(function(){
                      otaBarSet(100);
                      if(otaSt){otaSt.textContent='✓ 升级完成，设备已重启并恢复连接';otaSt.style.color='#3a5';}
                      otaBarReset(true);
                    }).catch(function(){/* 还没起来，下一轮再试 */});
                  }
                }).catch(function(){/* 升级/重启期间无响应属正常，继续轮询 */});
            },1000);
          })
          .catch(function(e){
            if(otaSt){otaSt.textContent='升级请求失败: '+e.message;otaSt.style.color='#e05';}
            otaBarReset(false);
          });
      });
    }

    /* ---- 外设开关（灯带设置）---- */
    var dhtT=document.getElementById('dhtToggle');
    if(dhtT)dhtT.addEventListener('change',function(){
      var st=document.getElementById('periphStatus');
      fetch('/control?dhtenable='+(dhtT.checked?1:0))
        .then(function(r){return r.text();})
        .then(function(t){if(st){st.textContent=t;st.style.color='#3a5';}})
        .catch(function(e){if(st){st.textContent='操作失败: '+e.message;st.style.color='#e05';}});
    });
    var irT=document.getElementById('irToggle');
    if(irT)irT.addEventListener('change',function(){
      var st=document.getElementById('periphStatus');
      fetch('/control?irenable='+(irT.checked?1:0))
        .then(function(r){return r.text();})
        .then(function(t){if(st){st.textContent=t;st.style.color='#3a5';}})
        .catch(function(e){if(st){st.textContent='操作失败: '+e.message;st.style.color='#e05';}});
    });

    /* ---- OTA 定时自动升级：开关 / 每天几点 / 升级链接 ---- */
    var oaT=document.getElementById('otaAutoToggle');
    var oh=document.getElementById('otaHour');
    var ou=document.getElementById('otaAutoUrl');
    function otaSaveSettings(){
      var p='/control?otaauto='+(oaT&&oaT.checked?1:0)+
            '&otahour='+encodeURIComponent((oh&&oh.value)?oh.value:'4')+
            '&otaautourl='+encodeURIComponent((ou&&ou.value)?ou.value.trim():'');
      return fetch(p).then(function(r){return r.text();});
    }
    function otaShowStatus(t,ok){
      var st=document.getElementById('otaStatus');
      if(st){st.textContent=t;st.style.color=ok?'#3a5':'#e05';}
    }
    if(oaT)oaT.addEventListener('change',function(){
      otaSaveSettings().then(function(t){otaShowStatus(t,true);})
        .catch(function(e){otaShowStatus('操作失败: '+e.message,false);oaT.checked=!oaT.checked;});
    });
    if(oh)oh.addEventListener('change',function(){
      var v=parseInt(oh.value,10);
      if(isNaN(v)||v<0||v>23){v=4;oh.value='4';}
      otaSaveSettings().then(function(t){otaShowStatus(t,true);})
        .catch(function(e){otaShowStatus('操作失败: '+e.message,false);});
    });
    if(ou)ou.addEventListener('change',function(){
      otaSaveSettings().then(function(t){otaShowStatus(t,true);})
        .catch(function(e){otaShowStatus('操作失败: '+e.message,false);});
    });

    /* ---- 检查更新（对比巴法云固件的"上传时间"）----
       每次点按钮 → 固件 HEAD 巴法云链接读 Last-Modified → 网页轮询 /otacheck 拿结果。 */
    var ocb=document.getElementById('otaCheckBtn');
    var oci=document.getElementById('otaCheckInfo');
    if(ocb&&oci){
      var otaCheckBase=-1;
      var renderCheck=function(d){
        oci.innerHTML='远程固件上传时间: '+d.remote+'<br>当前固件: '+d.local+'<br>'+
          (d.remote==='检查失败'?'⚠ 检查失败，稍后再试':
           (d.same?'✓ 巴法云固件与当前一致':'⬆ 巴法云有新固件，可点「一键升级」'));
      };
      ocb.addEventListener('click',function(){
        ocb.disabled=true;
        oci.textContent='检查中...';
        var t=setInterval(function(){
          fetch('/otacheck',{cache:'no-store'}).then(function(r){return r.json();}).then(function(d){
            if(otaCheckBase<0)otaCheckBase=d.count;
            renderCheck(d);
            if(d.count>otaCheckBase){clearInterval(t);ocb.disabled=false;}
          }).catch(function(){/* 忽略，下一轮再试 */});
        },1000);
      });
    }


      document.querySelectorAll('.mode-btn').forEach(function(btn){
        btn.addEventListener('click',function(){
          var mode=btn.dataset.mode;
          updateActiveButton(mode);
          sendCommand({mode:mode});
        });
      });

      /* ---- 颜色选择器：input节流发送 + change最终确认 ---- */
      document.getElementById('colorPicker').addEventListener('input',function(e){
        sendCommandThrottled({color:e.target.value.replace('#','')},'colorPicker');
      });
      document.getElementById('colorPicker').addEventListener('change',function(e){
        activeControl=null;
        sendCommand({color:e.target.value.replace('#','')});
      });

      /* ---- 亮度滑块：input节流发送 + change最终确认 ---- */
      document.getElementById('brightness').addEventListener('input',function(e){
        document.getElementById('brightnessValue').textContent=e.target.value+'%';
        sendCommandThrottled({brightness:e.target.value},'brightness');
      });
      document.getElementById('brightness').addEventListener('change',function(e){
        activeControl=null;
        document.getElementById('brightnessValue').textContent=e.target.value+'%';
        sendCommand({brightness:e.target.value});
      });

      /* ---- 速度滑块：input节流发送 + change最终确认 ---- */
      document.getElementById('speed').addEventListener('input',function(e){
        document.getElementById('speedValue').textContent=e.target.value+'%';
        sendCommandThrottled({speed:e.target.value},'speed');
      });
      document.getElementById('speed').addEventListener('change',function(e){
        activeControl=null;
        document.getElementById('speedValue').textContent=e.target.value+'%';
        sendCommand({speed:e.target.value});
      });

      document.getElementById('applySingle').addEventListener('click',function(){
        var ri=document.getElementById('singleRange').value;
        var cl=document.getElementById('singleColor').value.replace('#','');
        var start,end;
        if(ri.includes('-')){
          var p=ri.split('-').map(Number);
          start=p[0];end=p[1];
        }else{
          start=Number(ri);end=start;
        }
        if(isNaN(start)||isNaN(end)||start<1||end<1){updateStatus('\u9519\u8BEF: \u8BF7\u8F93\u5165\u6709\u6548\u8303\u56F4');return;}
        if(start>end){var t=start;start=end;end=t;}
        sendCommand({mode:'single',range:start+'-'+end,color:cl});
        updateStatus('\u5DF2\u8BBE\u7F6E\u706F\u73E0 '+start+'-'+end+' \u7684\u989C\u8272');
      });

      document.getElementById('startTimer').addEventListener('click',function(){
        var h=parseInt(document.getElementById('timerHours').value)||0;
        var m=parseInt(document.getElementById('timerMinutes').value)||0;
        var s=parseInt(document.getElementById('timerSeconds').value)||0;
        var total=h*3600+m*60+s;
        if(total>0){
          sendCommand({timer:total});
          remainingTime=total;
          updateTimerDisplay(h,m,s);
          if(timerInterval)clearInterval(timerInterval);
          timerInterval=setInterval(function(){
            remainingTime--;
            if(remainingTime<=0){clearInterval(timerInterval);updateTimerDisplay(0,0,0);}
            else{updateTimerDisplay(Math.floor(remainingTime/3600),Math.floor((remainingTime%3600)/60),remainingTime%60);}
          },1000);
        }
      });

      document.getElementById('stopTimer').addEventListener('click',function(){
        sendCommand({timer:0});
        if(timerInterval)clearInterval(timerInterval);
        var td=document.getElementById('timerDisplay');
        td.textContent='\u672A\u8BBE\u7F6E';
        td.className='accordion-tag idle';
      });

      document.getElementById('reverseToggle').addEventListener('change',function(){
        reverseState=this.checked;
        updateReverseStatus();
        sendCommand({reverse:reverseState?1:0});
      });

      document.getElementById('staOption').addEventListener('click',function(){
        staEnabled=!staEnabled;
        updateModeCheckboxes(staEnabled,apEnabled);
      });

      document.getElementById('apOption').addEventListener('click',function(){
        apEnabled=!apEnabled;
        updateModeCheckboxes(staEnabled,apEnabled);
      });

      /* ---- WiFi：SSID 下拉选择 / 扫描 / 密码显隐 ---- */
      document.getElementById('wifiSSID').addEventListener('change',function(){
        markActive('wifiSSID');   /* 2 秒内不让状态同步抢回选中项 */
        ssidPinned=true;          /* 用户选定了网络，之后一直保持这个选择 */
        onSsidChange();
      });
      document.getElementById('scanWifi').addEventListener('click',startWifiScan);
      /* 在密码框里打的字，实时记到"当前选中的网络"名下 */
      document.getElementById('wifiPassword').addEventListener('input',function(){
        var s=selSsidValue();
        if(s){pwMap[s]=this.value;pwUserEdit[s]=true;}
      });
      document.getElementById('togglePw').addEventListener('click',function(){
        var p=document.getElementById('wifiPassword');
        var show=(p.type==='password');
        p.type=show?'text':'password';
        this.innerHTML=show?'&#128584;':'&#128065;';
        this.title=show?'\u9690\u85CF\u5BC6\u7801':'\u663E\u793A\u5BC6\u7801';
      });

      /* ---- 保存：只把凭据存进"已保存的网络"，不切换/不连接 ---- */
      document.getElementById('saveConfig').addEventListener('click',function(){
        /* SSID 来源：下拉选择 或（选中"手动输入"时）文本框 */
        var selVal=document.getElementById('wifiSSID').value;
        var ssid=(selVal===MANUAL_VALUE||selVal===LOADING_VALUE)
          ?document.getElementById('wifiSSIDManual').value.trim()
          :selVal;
        var pw=document.getElementById('wifiPassword').value;
        var st=document.getElementById('wifiConfigStatus');
        if(!staEnabled&&!apEnabled){st.textContent='\u9519\u8BEF: \u5FC5\u987B\u81F3\u5C11\u5F00\u542F\u4E00\u79CD\u6A21\u5F0F!';return;}
        if(staEnabled&&ssid.length===0){st.textContent='\u9519\u8BEF: \u8BF7\u5148\u9009\u62E9WiFi\u6216\u624B\u52A8\u8F93\u5165WiFi\u540D\u79F0!';return;}
        /* 真实等待服务器返回，而不是前端假装成功 */
        st.textContent='\u6B63\u5728\u4FDD\u5B58...';
        var u='/control?'+new URLSearchParams({addwifi:1,ssid:ssid,password:pw,staEnabled:staEnabled?1:0,apEnabled:apEnabled?1:0}).toString();
        fetch(u).then(function(r){
          return r.text().then(function(t){return {ok:r.ok,txt:t};});
        }).then(function(res){
          st.textContent=res.txt||(res.ok?'\u5DF2\u4FDD\u5B58':'\u4FDD\u5B58\u5931\u8D25');
          /* 保存成功：服务端值即权威，交回给 /state 同步（失败则保留用户输入） */
          if(res.ok&&ssid){pwUserEdit[ssid]=false;pwMap[ssid]=pw;}
          ssidPinned=true;   /* 保持用户选中的网络，别让状态同步把下拉框抢回去 */
          fetchCurrentState();
        }).catch(function(e){
          st.textContent='\u4FDD\u5B58\u5931\u8D25: '+e.message;
        });
      });

      /* ---- 已保存网络列表：连接 / 调顺序 / 改密码 / 删除 ---- */
      document.getElementById('wifiSavedList').addEventListener('click',function(e){
        var t=e.target;
        while(t&&t!==this&&!(t.dataset&&t.dataset.act))t=t.parentNode;
        if(!t||t===this||!t.dataset)return;

        var row=t.parentNode;
        while(row&&row!==this&&!(row.dataset&&row.dataset.idx!==undefined))row=row.parentNode;
        var idx=row&&row.dataset?parseInt(row.dataset.idx,10):-1;
        if(isNaN(idx)||idx<0)return;

        var act=t.dataset.act;
        if(act==='pw'){
          editPwIdx=idx;
          renderSavedList();
          var inp=document.getElementById('savedPwInput');
          if(inp)inp.focus();
        }else if(act==='cancel'){
          editPwIdx=-1;
          renderSavedList();
        }else if(act==='savepw'){
          var inp2=document.getElementById('savedPwInput');
          var np=inp2?inp2.value:'';
          /* 改密码成功后台：该网络的密码记忆以服务端为准 */
          var snm=(savedNets[idx]&&savedNets[idx].ssid)?savedNets[idx].ssid:'';
          if(snm){pwUserEdit[snm]=false;pwMap[snm]=np;}
          wifiNetAction('setpw',idx,{password:np});
        }else if(act==='use'){
          /* 点「连接」：设备会切到该网络；下拉框也同步选中它，
             这样能顺便看到/修改它的密码 */
          var uSel=document.getElementById('wifiSSID');
          var uSsid=(savedNets[idx]&&savedNets[idx].ssid)?savedNets[idx].ssid:'';
          if(uSel&&uSsid){
            for(var k=0;k<uSel.options.length;k++){
              if(uSel.options[k].value===uSsid){
                uSel.value=uSsid;ssidPinned=true;onSsidChange();break;
              }
            }
          }
          wifiNetAction('connectwifi',idx);
        }else if(act==='up'){
          wifiNetAction('movewifi',idx,{dir:0});
        }else if(act==='down'){
          wifiNetAction('movewifi',idx,{dir:1});
        }else if(act==='del'){
          var nm=(savedNets[idx]&&savedNets[idx].ssid)?savedNets[idx].ssid:'';
          if(confirm('\u786E\u5B9A\u5220\u9664\u7F51\u7EDC "'+nm+'" \u5417\uFF1F')){
            wifiNetAction('delwifi',idx);
          }
        }
      });

      document.getElementById('restartDevice').addEventListener('click',function(){
        if(confirm('\u786E\u5B9A\u8981\u91CD\u542F\u8BBE\u5907\u5417\uFF1F')){
          sendCommand({restart:1});
        }
      });

      /* ---- 巴法云总开关：切换后立即保存并生效 ---- */
      document.getElementById('mqttToggle').addEventListener('change',function(){
        var want=this.checked;
        var self=this;
        var st=document.getElementById('mqttConfigStatus');
        if(want){
          var cid=document.getElementById('mqttClientId').value.trim();
          var top=document.getElementById('mqttTopic').value.trim();
        var srvEl=document.getElementById('mqttServer');
        var prtEl=document.getElementById('mqttPort');
        var srv=srvEl?srvEl.value.trim():'';
        var prt=prtEl?prtEl.value.trim():'';
          if(cid.length===0||top.length===0){
            self.checked=false;
            updateMqttToggleStatus(false);
            st.textContent='\u9519\u8BEF: \u8BF7\u5148\u586B\u5199\u5E76\u4FDD\u5B58 Client ID \u4E0E\u8BA2\u9605\u4E3B\u9898';
            return;
          }
        }
        st.textContent='\u6B63\u5728\u4E0B\u53D1...';
        fetch('/control?mqttenable='+(want?1:0)).then(function(r){
          return r.text().then(function(t){return {ok:r.ok,txt:t};});
        }).then(function(res){
          if(!res.ok){
            self.checked=!want;
            updateMqttToggleStatus(!want);
            st.textContent=res.txt||'\u64CD\u4F5C\u5931\u8D25';
          }else{
            updateMqttToggleStatus(want);
            st.textContent=res.txt||(want?'\u5DF2\u5F00\u542F':'\u5DF2\u5173\u95ED');
          }
          fetchCurrentState();
        }).catch(function(e){
          self.checked=!want;
          updateMqttToggleStatus(!want);
          st.textContent='\u64CD\u4F5C\u5931\u8D25: '+e.message;
        });
      });

      document.getElementById('saveMqttConfig').addEventListener('click',function(){
        // 允许保存为空：留空即清空巴法云配置（若开关开着，服务端会自动关闭它）
        var cid=document.getElementById('mqttClientId').value.trim();
        var top=document.getElementById('mqttTopic').value.trim();
        var srvEl=document.getElementById('mqttServer');
        var prtEl=document.getElementById('mqttPort');
        var srv=srvEl?srvEl.value.trim():'';
        var prt=prtEl?prtEl.value.trim():'';
        var st=document.getElementById('mqttConfigStatus');
        st.textContent='\u6B63\u5728\u4FDD\u5B58...';
        var u='/control?'+new URLSearchParams({savemqtt:1,mqttid:cid,mqtttopic:top,mqttserver:srv,mqttport:prt}).toString();
        fetch(u).then(function(r){
          return r.text().then(function(t){return {ok:r.ok,txt:t};});
        }).then(function(res){
          st.textContent=res.txt||(res.ok?'\u5DF2\u4FDD\u5B58':'\u4FDD\u5B58\u5931\u8D25');
          fetchCurrentState();
        }).catch(function(e){
          st.textContent='\u4FDD\u5B58\u5931\u8D25: '+e.message;
        });
      });

      /* 保存灯珠数量（输入框 + 保存按钮都在卡片内，回车即可保存） */
      function saveLedCount(){
        var el=document.getElementById('ledCount');
        var lc=parseInt(el.value);
        if(isNaN(lc)||lc<1||lc>200){
          document.getElementById('ledCountStatus').textContent='\u9519\u8BEF: \u706F\u73E0\u6570\u91CF\u5FC5\u987B\u57281-200\u4E4B\u95F4!';
          updateStatus('\u9519\u8BEF: \u706F\u73E0\u6570\u91CF\u5FC5\u987B\u57281-200\u4E4B\u95F4!');
          return;
        }
        el.value=lc;
        sendCommand({ledcount:lc});
        updateLedCountTag(lc);
        document.getElementById('ledCountStatus').textContent='\u5DF2\u4FDD\u5B58: '+lc+' \u9897';
        /* 面板默认折叠，所以顺带在顶部状态栏也提示一次 */
        updateStatus('\u5DF2\u4FDD\u5B58\u706F\u73E0\u6570\u91CF: '+lc+' \u9897');
      }
      document.getElementById('saveLedCount').addEventListener('click',saveLedCount);
      document.getElementById('ledCount').addEventListener('keydown',function(e){
        if(e.key==='Enter'){e.preventDefault();saveLedCount();this.blur();}
      });

      /* ---- 单色动画模式按钮事件绑定 ---- */
      document.querySelectorAll('.anim-btn').forEach(function(btn){
        btn.addEventListener('click',function(){
          var mode=btn.dataset.anim;
          updateAnimButtons(mode);
          sendCommand({solidanim:mode});
        });
      });

      /* ---- SSE ---- */
      /* 状态刷新：操作（点按钮/拖动）后立即拉一次 + 短暂连补几拍，
         空闲时降到低频（8 秒）保底 —— 有操作时越刷越快、没操作几乎不刷 */
      var _pollIdleMs=1000,_pollActiveMs=1000,_pollTimer=null,_pollBurst=0;
      function schedulePoll(delay){clearTimeout(_pollTimer);_pollTimer=setTimeout(pollTick,delay);}
      function pollTick(){
        /* 用户正在输入（手机键盘弹着）时跳过这一拍：轮询会重写状态区/下拉框，
           重排会把移动端键盘顶掉。用 activeElement 判断，自愈、不会卡死。 */
        var _ae=document.activeElement;
        if(_ae&&/^(INPUT|SELECT|TEXTAREA)$/i.test(_ae.tagName||'')){schedulePoll(_pollIdleMs);return;}
        fetchCurrentState();
        if(_pollBurst>0){_pollBurst--;schedulePoll(_pollActiveMs);}
        else{schedulePoll(_pollIdleMs);}
      }
      function kickPoll(){_pollBurst=6;schedulePoll(_pollActiveMs);}  // 操作后连补 6 拍
      document.addEventListener('click',kickPoll);
      document.addEventListener('input',kickPoll);
      document.addEventListener('change',kickPoll);
      schedulePoll(_pollIdleMs);
    });
  </script>
<script>
/* ── 皮肤切换 ── */
(function(){
 var KEY='land_led_skin',N=5;
 var NAMES=['新粗野','黏土','终端','流光','像素'];
 var bar=document.getElementById('skinBar');
 if(bar){
  bar.innerHTML=[{n:'新粗野',tag:'NEO',d:'粗黑边框 · 奶油底 · 撞色色块',bg:'#FFF3D6',c:['#FFE600','#FF4D8D','#00D9FF','#8CC63F']},{n:'黏土',tag:'CLAY',d:'马卡龙软糖 · 圆角 · 果冻感',bg:'#FDF3F8',c:['#FF8FB1','#B49BE8','#7EC8F5','#8FD9A0']},{n:'终端',tag:'HUD',d:'CRT 扫描线 · 霓虹绿 · 等宽字',bg:'#05070A',c:['#00FF9C','#FFB000','#FF2E88','#5C7480']},{n:'流光',tag:'AURORA',d:'漂移色块 · 毛玻璃卡片',bg:'#0B1020',c:['#7DD3FC','#C4B5FD','#F9A8D4','#86EFAC']},{n:'像素',tag:'8-BIT',d:'8-bit 描边 · NES 配色 · 计分板',bg:'#1A1B26',c:['#E94560','#FFD369','#00FFF5','#6BCB77']}].map(function(p,i){
   var sw=p.c.map(function(c){return '<i style="background:'+c+'"></i>'}).join('');
   var rows=p.c.slice(1).map(function(c,ri){
     return '<span class="pvr" style="background:'+c+';'+(ri?'width:62%':'')+'"></span>'}).join('');
   return '<button class="skt" data-i="'+i+'">'+
     '<span class="pv" style="background:'+p.bg+'">'+
       '<span class="pvb" style="background:'+p.c[0]+'"></span>'+rows+
       '<span class="pvs">'+sw+'</span>'+
     '</span>'+
     '<span class="nm">'+p.n+'<b>'+p.tag+'</b></span>'+
     '<span class="ds">'+p.d+'</span></button>';
  }).join('');
 }
 function apply(n){
  n=Math.max(0,Math.min(N-1,n|0));
  for(var i=0;i<N;i++)document.body.classList.remove('sk'+i);
  document.body.classList.add('sk'+n);
  document.body.setAttribute('data-skin',n);
  var els=document.querySelectorAll('[data-sk]');
  for(var j=0;j<els.length;j++){
   var a=(els[j].getAttribute('data-sk')||'').split(/\s+/);
   els[j].classList.toggle('skinoff',a.indexOf(String(n))<0);
  }
  var bs=document.querySelectorAll('.skt');
  for(var k=0;k<bs.length;k++)bs[k].classList.toggle('on',+bs[k].dataset.i===n);
  var nm=document.getElementById('skinName');
  if(nm)nm.textContent=NAMES[n];
  try{localStorage.setItem(KEY,n)}catch(e){}
  /* 皮肤变了布局可能变，重排一次瀑布流 */
  if(window.__relayout)window.__relayout(true);
 }
 window.__applySkin=apply;
 var btns=document.querySelectorAll('.skt');
 for(var b=0;b<btns.length;b++){(function(x){x.addEventListener('click',function(){apply(+x.dataset.i)})})(btns[b])}
 document.addEventListener('keydown',function(e){
  var t=e.target;if(t&&/input|textarea|select/i.test(t.tagName))return;
  var n=parseInt(e.key,10);if(n>=1&&n<=N)apply(n-1);
 });
 /* 优先级：地址栏 ?skin=N > 本机记忆 > 默认第一套 */
 var saved=0;try{saved=parseInt(localStorage.getItem(KEY)||'0',10)||0}catch(e){}
 var qp=null;try{qp=new URLSearchParams(location.search).get('skin')}catch(e){}
 apply((qp===null||qp==='')?saved:+qp);
})();
</script>
<script>
/* ── 布局：按顺序**连续分组**成 N 列，并让各列高度尽量对齐 ──
   规则：
   1. 顺序 = 原始顺序（灯光模式 → … → 界面皮肤），阅读顺序是「先第 1 竖排从上到下，
      再第 2 竖排」，列内顺序固定不会跳来跳去；
   2. 列数 = min(4, 可用宽度允许的列数)，手机竖屏就是 1 列；
   3. 展开/收起后重新分组，让各列高度尽量对齐（1 列时不动，手机上位置永远不变）。 */
(function(){
 var grid=document.querySelector('.grid');
 var MINW=320,GAP=14,MAXCOL=4,ANIM_MIN=780,rt=null,laid=false;
 function cols(){return [].slice.call(grid.querySelectorAll('.mcol'));}
 function mkcols(n){
  cols().forEach(function(c){c.remove()});
  var cs=[];
  for(var i=0;i<n;i++){var c=document.createElement('div');c.className='mcol';grid.appendChild(c);cs.push(c);}
  return cs;
 }
 /* 把 hs 顺序切成 n 段，让最高的那段尽量矮（并兼顾方差），返回每段结束下标 */
 function bestCuts(hs,n){
  var m=hs.length;
  if(n<2||m<=n)return null;
  var bestCuts=null,bestKey=null;
  function sum(a,b){var s=0;for(var i=a;i<b;i++)s+=hs[i];return s}
  (function rec(start,left,acc,cuts){
   if(left===1){
    var all=acc.concat([sum(start,m)]);
    var mx=Math.max.apply(null,all);
    var avg=all.reduce(function(a,b){return a+b},0)/all.length;
    var cost=all.reduce(function(a,b){return a+(b-avg)*(b-avg)},0);
    if(bestKey===null||mx<bestKey.mx||(mx===bestKey.mx&&cost<bestKey.cost)){
     bestKey={mx:mx,cost:cost};bestCuts=cuts.concat([m]);
    }
    return;
   }
   for(var i=start+1;i<=m-left+1;i++)rec(i,left-1,acc.concat([sum(start,i)]),cuts.concat([i]));
  })(0,n,[],[]);
  return bestCuts;
 }
 function masonry(anim){
  if(!grid)return;
  /* 【关键】有输入框聚焦时绝不重排。
     masonry 会把面板 appendChild 到各列 —— 移动节点会让输入框失焦（手机键盘
     立刻收起），还会让所有 CSS 动画重新开始（按钮扫光卡顿重来）。
     手机点输入框 → 键盘弹出 → 视口 resize → 这里被触发，正是"键盘刚出就下去"的元凶。 */
  var _ae=document.activeElement;
  if(_ae&&/^(INPUT|SELECT|TEXTAREA)$/i.test(_ae.tagName||''))return;
  if(anim&&window.innerWidth<ANIM_MIN)anim=false;   /* 窄屏不做位移动画 */
  var ps=[].slice.call(grid.querySelectorAll('section.p'));
  if(!ps.length)return;
  var first=null;
  if(anim){first={};ps.forEach(function(p){first[p]=p.getBoundingClientRect();});}
  var w=grid.clientWidth;
  var n=Math.max(1,Math.min(MAXCOL,ps.length,Math.floor((w+GAP)/(MINW+GAP))));
  /* 首次运行时面板还挤在 flex 行里、宽度不对，先全塞进第 1 列量准高度 */
  if(!laid){laid=true;var c0=mkcols(n)[0];ps.forEach(function(p){c0.appendChild(p)});}
  var hs=ps.map(function(p){return p.offsetHeight+GAP;});
  var cuts=bestCuts(hs,n);
  var cs=mkcols(n),at=0;
  for(var ci=0;ci<n;ci++){
   var end=cuts?cuts[ci]:(ci===n-1?ps.length:at+Math.ceil((ps.length-at)/(n-ci)));
   for(var k=at;k<end;k++)cs[ci].appendChild(ps[k]);
   at=end;
  }
  if(anim){ps.forEach(function(p){
   var f=first[p],l=p.getBoundingClientRect(),dx=f.left-l.left,dy=f.top-l.top;
   if(dx||dy){p.style.transition='none';p.style.transform='translate('+dx+'px,'+dy+'px)';
    requestAnimationFrame(function(){requestAnimationFrame(function(){
     p.style.transition='transform .3s cubic-bezier(.22,1,.36,1)';p.style.transform='';});});}
  });}
 }
 window.__relayout=masonry;
 masonry(false);
 /* 只在「宽度变了」时重排：列数只跟宽度有关。手机弹/收键盘只改高度，
    以前会误触发重排 → 丢焦点 + 收键盘 + 动画重来。 */
 var lastW=window.innerWidth;
 window.addEventListener('resize',function(){
  if(window.innerWidth===lastW)return;
  lastW=window.innerWidth;
  clearTimeout(rt);rt=setTimeout(function(){masonry(true)},160);
 });
 /* 展开/收起后重新分组对齐高度（手机只有 1 列时不会发生任何位移） */
 [].slice.call(document.querySelectorAll('.accordion-header')).forEach(function(h){
  h.addEventListener('click',function(){setTimeout(function(){masonry(true)},420)});
 });
 window.addEventListener('load',function(){masonry(false)});
 setTimeout(function(){if(grid)grid.classList.add('msd')},1000);
})();
</script>
</body></html>

)rawliteral";

// ============================================================
// 辅助工具函数
// ============================================================

/**
 * @brief 反向显示辅助函数。
 *
 * 若 a=true：先将 leds 数组原地翻转，调用 FastLED.show() 输出，
 * 再翻转回来恢复原始顺序，保证后续逻辑不受影响。
 * 若 a=false：直接 FastLED.show()。
 *
 * @note FastLED.show() 调用两次是为了防止部分 WS2812 驱动的显示 bug，
 *       请勿删除。
 *
 * @param a        是否反向显示
 * @param leds     LED 颜色数组
 * @param num_leds 有效灯珠数量
 */
inline void fanxiang(bool a, CRGB leds[], uint16_t num_leds) {
  if (a) {
    // 原地翻转
    for (int i = 0; i < num_leds / 2; i++) {
      CRGB t              = leds[i];
      leds[i]             = leds[num_leds - i - 1];
      leds[num_leds - i - 1] = t;
    }
    FastLED.show();
    FastLED.show();  // 重复一次防止 WS2812 显示 bug，请勿删除！
    // 恢复原始顺序
    for (int i = 0; i < num_leds / 2; i++) {
      CRGB t              = leds[i];
      leds[i]             = leds[num_leds - i - 1];
      leds[num_leds - i - 1] = t;
    }
  } else {
    FastLED.show();
    FastLED.show();  // 重复一次防止 WS2812 显示 bug，请勿删除！
  }
}

/**
 * @brief 填充指定数量的 LED 为同一颜色，并立即输出（含反向处理）。
 *
 * @param ledArray   目标 LED 数组
 * @param numToFill  要填充的 LED 数量
 * @param color      目标颜色
 */
inline void fill_solid_new(CRGB* ledArray, int numToFill, const CRGB& color) {
  for (int i = 0; i < numToFill; i++) {
    ledArray[i] = color;
  }
  fanxiang(reverseDisplay, leds, num_leds);
}


/**
 * @brief 将 24 位十进制颜色值转换为 CRGB 对象。
 *
 * @param decimal 24 位颜色值（高 8 位=R，中 8 位=G，低 8 位=B）
 * @return CRGB 颜色对象
 */
inline CRGB decimalToCRGB(unsigned long decimal) {
  decimal &= 0xFFFFFF;
  return CRGB(
    (decimal >> 16) & 0xFF,
    (decimal >>  8) & 0xFF,
     decimal        & 0xFF
  );
}

/**
 * @brief 将十六进制颜色字符串（如 "FF8800"）转换为 CRGB 对象。
 *
 * @param hexStr 6 位十六进制颜色字符串
 * @return CRGB 颜色对象（解析失败返回 Black）
 */
inline CRGB hexToCRGB(const String& hexStr) {
  long color = strtol(hexStr.c_str(), NULL, 16);
  return CRGB(
    (color >> 16) & 0xFF,
    (color >>  8) & 0xFF,
     color        & 0xFF
  );
}

/**
 * @brief 统一设置单色颜色的入口函数。
 *
 * 所有需要改变 solidColor 的地方统一调用此函数：
 *  - animate=true  → 设置 modelbianhua=true，solid_cut 重新开始动画
 *  - animate=false → 直接全部填充完跳过动画
 *
 * @param color   目标颜色
 * @param animate 是否启用渐入动画
 */
inline void applySolidColor(CRGB color, bool animate) {
  bool changed = (color.r != solidColor.r || color.g != solidColor.g || color.b != solidColor.b);
  solidColor = color;
  if (animate) {
    // 颜色没变就不重新触发渐入：重复点同一个颜色（红外/网页/MQTT）
    // 不应让灯带再从黑亮一遍。
    if (changed) modelbianhua = true;
  } else {
    fill_solid_new(leds, num_leds, solidColor);
  }
}

/**
 * @brief 单色渐入动画，支持 5 种动画模式（由 solidAnimMode 控制）：
 *
 *  - random     : 随机溶解 — 每次随机点亮一颗未亮灯珠（默认）
 *  - sequential : 顺序点亮 — 从第一颗依次向后
 *  - center     : 中心扩散 — 从灯带中心向两端交替
 *  - ends       : 两端向内 — 从两端同时向中间靠拢
 *  - fade       : 全局渐亮 — 所有灯珠同时从当前颜色渐变到目标颜色
 *                 （关闭状态点亮时从黑开始；已点亮时换色则是交叉渐变）
 *
 * 智能跳帧：颜色切换时若 RGB 分量差之和 < 80，直接全量填充跳过动画。
 *
 * @param leds      LED 颜色数组
 * @param num_leds  有效灯珠数量
 * @param solidColor 目标颜色
 */
inline void solid_cut(CRGB leds[], uint16_t num_leds, CRGB solidColor) {
  //上一个模式的颜色
  static CRGB last_solidColor = solidColor;
  static CRGB fadeFrom        = CRGB::Black;  // fade 动画起点（动画开始那一帧屏幕上的颜色）
  static uint16_t num = 1;
  static uint32_t random_seed = 1;

  if (!modelbianhua && last_solidColor == solidColor && num <= num_leds) {
    num++;
  } else if (modelbianhua || last_solidColor != solidColor) {
    // 计算前后颜色差异（RGB 分量绝对差值之和，最大 765）
    int rDiff = abs((int)solidColor.r - (int)last_solidColor.r);
    int gDiff = abs((int)solidColor.g - (int)last_solidColor.g);
    int bDiff = abs((int)solidColor.b - (int)last_solidColor.b);
    int totalDiff = rDiff + gDiff + bDiff;

    // modelbianhua=true（显式切换）→ 始终走动画
    // modelbianhua=false（连续拖动）→ 阈值判断是否跳过动画
    if (!modelbianhua && totalDiff < 100) {
      // 颜色相近且非显式触发：直接全部填充，跳过渐入动画
      fill_solid_new(leds, num_leds, solidColor);
      num = num_leds + 1;   // 标记已完成，后续帧跳过
    } else {
      // 颜色相差大 或 显式触发：走动画
      num = 0;
      random_seed = millis() | 1;  // 确保不为0

      // 记录渐入动画的起点 = 动画开始那一刻屏幕上实际显示的代表色（取所有
      // 已点亮灯珠的平均色）：
      //   - 关闭状态点亮：阵列全黑，起点为黑 → 仍然"从黑渐亮"；
      //   - 已点亮时重复下发同一颜色/模式：所有灯珠已等于目标 → 跳过动画，
      //     不会再出现"从黑重新亮一遍"的闪烁；
      //   - 已点亮时换色：从当前颜色交叉渐变 → 不会先闪黑。
      uint32_t sumR = 0, sumG = 0, sumB = 0;
      uint16_t litCount = 0;
      bool alreadyTarget = true;
      for (uint16_t i = 0; i < num_leds; i++) {
        if (leds[i] != solidColor) alreadyTarget = false;
        if (leds[i].r || leds[i].g || leds[i].b) {
          sumR += leds[i].r;
          sumG += leds[i].g;
          sumB += leds[i].b;
          litCount++;
        }
      }
      fadeFrom = litCount
        ? CRGB((uint8_t)(sumR / litCount), (uint8_t)(sumG / litCount), (uint8_t)(sumB / litCount))
        : CRGB::Black;
      if (alreadyTarget) num = num_leds + 1;  // 无需动画，交给下面统一 return
    }
    modelbianhua = false;
    last_solidColor = solidColor;
  }
  if (num > num_leds) return;  // 已全部点亮或已跳过，跳过重复填充

  // ---- 动画模式分发 ----
  if (solidAnimMode == "none") {
    // 无动画：直接全部填充目标颜色并跳过后续帧
    fill_solid_new(leds, num_leds, solidColor);
    num = num_leds + 1;
    return;
  } else if (solidAnimMode == "sequential") {
    // 顺序点亮：从第一颗依次往后
    int pos = num - 1;
    if (pos >= 0 && pos < (int)num_leds) {
      leds[pos] = solidColor;
      fanxiang(reverseDisplay, leds, num_leds);
    }
  } else if (solidAnimMode == "center") {
    // 中心扩散：从灯带中心向两端交替点亮
    int center = num_leds / 2;
    int offset;
    if (num % 2 == 1) {
      offset = num / 2;         // 奇数：向右
    } else {
      offset = -(num / 2);      // 偶数：向左
    }
    int pos = center + offset;
    if (pos >= 0 && pos < (int)num_leds) {
      leds[pos] = solidColor;
      fanxiang(reverseDisplay, leds, num_leds);
    }
  } else if (solidAnimMode == "ends") {
    // 两端向内：从灯带两端交替向中间靠拢
    int half = (num - 1) / 2;
    int pos;
    if (num % 2 == 1) {
      pos = half;               // 奇数：左端
    } else {
      pos = num_leds - 1 - half;  // 偶数：右端
    }
    if (pos >= 0 && pos < (int)num_leds) {
      leds[pos] = solidColor;
      fanxiang(reverseDisplay, leds, num_leds);
    }
  } else if (solidAnimMode == "fade") {
    // 全局渐亮/渐暗：所有灯珠同时从动画起点交叉渐变到目标颜色。
    // 起点 fadeFrom 在动画开始那一帧采样（见上方），因此：
    //   - 从关闭状态点亮：起点为黑，仍是"从黑渐亮"；
    //   - 已点亮时换色 / 重复下发同一颜色：从当前颜色渐变，不会闪黑。
    uint8_t scale = (uint8_t)((uint16_t)num * 255 / num_leds);
    CRGB displayColor = fadeFrom;
    if (scale >= 255) {
      displayColor = solidColor;        // 最后一帧精确落到目标色
    } else {
      nblend(displayColor, solidColor, scale);
    }
    fill_solid_new(leds, num_leds, displayColor);
  } else {
    // 默认随机溶解：用简单的线性同余生成器生成伪随机序列
    if (num < num_leds) {
      // LCG参数：类似glibc的LCG
      random_seed = random_seed * 1103515245UL + 12345UL;
      int pos = (random_seed >> 16) % num_leds;

      // 简单的去重：如果已经亮了，就找下一个没亮的
      int start_pos = pos;
      for (int i = 0; i < num_leds; i++) {
        pos = (start_pos + i) % num_leds;
        // 检查是否已经是目标颜色（已点亮）
        if (leds[pos].r != solidColor.r || leds[pos].g != solidColor.g || leds[pos].b != solidColor.b) {
          leds[pos] = solidColor;
          fanxiang(reverseDisplay, leds, num_leds);
          break;
        }
      }
    }
  }
}

/**
 * @brief 重置所有灯效的内部状态变量。
 *
 * 在切换模式时调用，防止上一个模式的状态残留影响新模式。
 */
void resetEffectState() {
  gHue              = 0;
  breathingValue    = 0;
  breathingUp       = true;
  scannerPosition   = 0;
  scannerDirection  = true;
  auroraPosition    = 0;
  discoHue          = 0;
  strobeOn          = false;
  lastEffectUpdate  = millis();
  lastStrobeChange  = millis();
}

// ============================================================
// setup() - 系统初始化
// ============================================================

/**
 * @brief 系统初始化，按以下顺序执行：
 *  1. 串口、红外、EEPROM 初始化
 *  2. 从 EEPROM 加载 WiFi 配置、灯珠数量、反向显示设置
 *  3. 初始化 FastLED 灯带
 *  4. LED 进度条动画（分 3 阶段）
 *     - 蓝色：基础初始化（后 30%）
 *     - 绿/黄/红：WiFi 连接（中间 50%）
 *     - 紫色：服务器初始化（前 20%）
 *  5. 若 WiFi 连接成功则启动 MQTT
 *  6. 注册 Web 服务器路由并启动
 */
// ============================================================
// 快速断电计数器：辅助函数
// ============================================================

/**
 * @brief 读取当前快速断电计数器值（EEPROM 单字节，0xFF 视为 0）。
 * @note 调用前须已 EEPROM.begin()。
 */
static inline uint8_t readBootCount() {
  uint8_t v = EEPROM.read(BOOT_COUNT_EEPROM_ADDR);
  return (v == 0xFF) ? 0 : v;  // 未初始化的 Flash 位为 0xFF，视为 0
}

/**
 * @brief 将快速断电计数器写入 EEPROM 并提交。
 * @note 调用前须已 EEPROM.begin()。
 */
static inline void writeBootCount(uint8_t count) {
  EEPROM.write(BOOT_COUNT_EEPROM_ADDR, count);
  EEPROM.commit();
}

// ============================================================
// setup() - 系统初始化
// ============================================================

void setup() {
  DEBUG_BEGIN(115200);
  DEBUG_PRINTLN("系统启动中...");

  // 禁止 WiFi 库在每次连接时把凭据写入 Flash 的 wifi 扇区。
  // 本项目的 WiFi 配置完全由 EEPROM 管理，关闭 SDK 自动持久化可以：
  //   1. 避免与配置读写产生额外的 Flash 擦写；
  //   2. 避免 SDK 用旧的/垃圾的保存凭据在启动时抢连。
  WiFi.persistent(false);

  // 开始"上电强制开 AP"计时（前 AP_FORCE_BOOT_MS 内热点必开，与开关无关）。
  // 必须尽量早调用：后面的启动动画 + 联网尝试可能要十几秒，
  // 晚了这段耗时会白白吃掉 5 分钟窗口。
  apForceWindowStart();

  irrecv.enableIRIn();  // 启动红外接收

  // ============================================================
  // 【启动顺序】先加载配置（拿到灯珠数量）→ 立刻初始化灯带 → 再做其它初始化。
  //
  // 灯带必须尽早初始化：这样上电后立刻开始播放"三段式进度动画"
  // （联网前 1/3 → 联网中 1/3 → 配置收尾 1/3），而不是等所有配置读取、
  // 网络初始化都做完之后才亮。
  //
  // 灯带也必须在 WiFi.mode()/WiFi.begin() 之前准备好：ESP8266 的 WiFi
  // 中断会打断 WS2812 的精确时序，联网期间灯带可能显示异常。
  // ============================================================
  loadWiFiConfig();   // 加载 WiFi 配置 + 灯珠数量（num_leds）
  loadMqttConfig();   // 加载 MQTT 配置
  loadMqttServerConfig(); // 加载 MQTT 服务器地址/端口（默认巴法云 bemfa.com:9501）
  loadPeriphFlags();  // 加载外设开关 / OTA 定时自动升级配置（DHT11、红外、检查时间、升级链接）
  configTime(8 * 3600, 0, "ntp.aliyun.com", "ntp1.aliyun.com");  // SNTP 对时——定时自动升级判断"每天几点"要用

  // 立刻初始化灯带（此时 num_leds 已确定）
  FastLED.addLeds<WS2812, LED_PIN, GRB>(leds, num_leds);
  FastLED.setBrightness(brightness);

  // ============================================================
  // 恢复出厂设置：连续 BOOT_RESET_COUNT 次"快速断电"
  // （上电后不到 BOOT_STABLE_MS 毫秒就断电）即清空整片 EEPROM，
  // 恢复到出厂状态，然后自动重启。
  //
  // 清除内容：WiFi 配置、灯珠数量、反向显示、MQTT 配置 —— 全部清除，
  // 重启后按默认参数（DEFAULT_LEDS 等）重新启动。
  //
  // 提示：清除期间全部灯珠会点亮为白色并持续 2 秒，然后自动重启。
  //       本机制不区分"是否已配置过"，连续 4 次快速断电后第 5 次上电即触发。
  // ============================================================
  EEPROM.begin(EEPROM_SIZE);
  uint8_t bootCount = readBootCount();
  DEBUG_PRINT("快速断电计数: ");
  DEBUG_PRINTLN(bootCount);

  if (bootCount >= BOOT_RESET_COUNT) {
    // ---- 触发恢复出厂设置 ----
    DEBUG_PRINTLN("检测到连续快速断电！正在恢复出厂设置...");

    // 1) 全部灯珠点亮为白色，持续 2 秒。
    //
    // 【为什么之前不亮】这里原来用的是 FastLED 原生的 fill_solid() + show()，
    // 而项目自带的 fanxiang() 里明确注释着：
    //     FastLED.show();
    //     FastLED.show();  // 重复一次防止 WS2812 显示 bug，请勿删除！
    // 这块板子上【单次 FastLED.show() 不显示】，必须连续调用两次。
    // 所以这里改用项目自带的 fill_solid_new()（内部走 fanxiang，自带双次 show）。
    //
    // 填充数量取 MAX_LEDS：无论实际灯带多长、当前配置的灯珠数是多少，
    // 整条灯带都会全白（多发的数据由灯带自行丢弃）。
    // 长度按 MAX_LEDS 重新注册，保证输出覆盖整条灯带。
    FastLED.addLeds<WS2812, LED_PIN, GRB>(leds, MAX_LEDS);
    FastLED.setBrightness(brightness);
    fill_solid_new(leds, MAX_LEDS, CRGB::White);
    delay(2000);

    // 2) 清空整片 EEPROM：全部写入 0xFF（等同出厂空白 Flash）。
    //    重启后各加载函数会把这些 0xFF 判为"非法/未配置"，回退到默认值，
    //    从而实现 WiFi、灯珠数量、反向显示、MQTT 配置的彻底重置。
    for (int i = 0; i < EEPROM_SIZE; i++) {
      EEPROM.write(i, 0xFF);
    }
    // 计数器单独写 0：确保重启后不会立刻再次满足触发条件。
    EEPROM.write(BOOT_COUNT_EEPROM_ADDR, 0);
    EEPROM.commit();
    EEPROM.end();

    // 3) 熄灯，然后自动重启（新参数将在重启后生效）
    fill_solid_new(leds, MAX_LEDS, CRGB::Black);
    delay(200);

    DEBUG_PRINTLN("EEPROM 已恢复出厂设置，设备即将重启...");
    delay(200);
    ESP.restart();
  } else {
    // 未触发：计数器 +1，标记本次为"未稳定上电"
    writeBootCount(bootCount + 1);
    EEPROM.end();
    DEBUG_PRINT("计数器已更新为: ");
    DEBUG_PRINTLN(bootCount + 1);
  }

  // 灯带已在上面初始化完毕，这里只做清屏，随后立刻播放启动动画
  fill_solid_new(leds, num_leds, CRGB::Black);  // 全灭

  // 加载反向显示设置
  EEPROM.begin(EEPROM_SIZE);
  byte storedReverse = EEPROM.read(REVERSE_EEPROM_ADDR);
  EEPROM.end();

  reverseDisplay = (storedReverse == 0 || storedReverse == 1) ? storedReverse : true;
  DEBUG_PRINT("反向显示设置: ");
  DEBUG_PRINTLN(reverseDisplay ? "开启" : "关闭");

  // 启动动画单步延时：灯珠越多单步越快，但保证最小可见节奏（≥5ms），
  // 避免灯珠数接近 MAX_LEDS 时延时归零、启动动画"一闪而过"看不出进度。
  short int stepDelay = map(num_leds, 1, MAX_LEDS, 90, 5);

  DEBUG_PRINTLN("[启动动画] 第一阶段：蓝色（后 30%）");
  // ---- 第一阶段：蓝色（后 30%）----
  for (int i = num_leds - 1; i >= (int)(num_leds * 0.7) - 1; i--) {
    leds[i] = CRGB::Blue;
    fanxiang(!reverseDisplay, leds, num_leds);
    delay(stepDelay);
  }

  int wifiStartLed = (int)(num_leds * 0.7) - 1;
  int wifiEndLed   = (int)(num_leds * 0.2);

  DEBUG_PRINTLN("[启动动画] 第二阶段：连接网络（中间 50%）");
  // ---- 第二阶段：WiFi 连接（中间 50%）----
  wifiStartLed = connectToWiFi(wifiStartLed, wifiEndLed);

  // MQTT 初始化：仅在巴法云开关开启时配置服务器地址与回调。
  // 开关关闭时不设置、不连接 —— 完全不执行巴法云功能。
  // （开关由网页动态打开时，reconnect() 内部也会补设，保证可用。）
  if (mqttConfig.enabled) {
    espClient.setTimeout(8000);  ///< TCP 连接/读写超时 8 秒，留足 DNS+握手时间
    client.setServer(mqtt_server, mqtt_server_port);
    client.setCallback(callback);
    mqttReconnectNow = true;     ///< 启动后 loop 立即尝试连接，不等 10 秒轮询
    DEBUG_PRINT("MQTT 服务器: ");
    DEBUG_PRINT(mqtt_server);
    DEBUG_PRINT(":");
    DEBUG_PRINTLN(mqtt_server_port);
    DEBUG_PRINT("MQTT Client ID: ");
    DEBUG_PRINTLN(mqttConfig.clientId[0] ? mqttConfig.clientId : "(空)");
    DEBUG_PRINT("MQTT Topic: ");
    DEBUG_PRINTLN(mqttConfig.topic[0] ? mqttConfig.topic : "(空)");
  } else {
    DEBUG_PRINTLN("巴法云 MQTT 功能已关闭（可在网页中开启）");
  }

  if (WiFi.status() == WL_CONNECTED) {
    // STA 连接成功：绿色
    while (wifiStartLed >= wifiEndLed) {
      leds[wifiStartLed] = CRGB::Green;
      fanxiang(!reverseDisplay, leds, num_leds);
      wifiStartLed--;
      delay(stepDelay / 2);
    }
  } else if (WiFi.getMode() & WIFI_AP) {
    // 仅 AP 模式：黄色
    while (wifiStartLed >= wifiEndLed) {
      leds[wifiStartLed] = CRGB::Yellow;
      fanxiang(!reverseDisplay, leds, num_leds);
      wifiStartLed--;
      delay(stepDelay / 2);
    }
  } else {
    // 连接失败：红色
    while (wifiStartLed >= wifiEndLed) {
      leds[wifiStartLed] = CRGB::Red;
      fanxiang(!reverseDisplay, leds, num_leds);
      wifiStartLed--;
      delay(stepDelay / 2);
    }
  }

  DEBUG_PRINTLN("[启动动画] 第三阶段：配置收尾（前 20%）");
  // ---- 第三阶段：紫色（前 20%）----
  for (int i = wifiEndLed - 1; i >= 0; i--) {
    leds[i] = CRGB::Purple;
    fanxiang(!reverseDisplay, leds, num_leds);
    delay(stepDelay);
  }

  // 注册 Web 服务器路由并启动
  setupWebServer();

  delay(500);
  fill_solid_new(leds, num_leds, CRGB::Black);  // 初始化完成后全灭
}

// ============================================================
// loop() - 主循环
// ============================================================

/**
 * @brief 主循环，依次处理：
 *  1. 红外遥控输入
 *  2. MQTT 非阻塞重连与消息处理
 *  3. 电量模式超时检测
 *  4. 音乐模式超时检测
 *  5. 定时关闭检测
 *  6. 状态变更检测与 SSE 推送
 *  7. 当前模式对应灯效执行（hash→switch O(1) 分发）
 */
void loop() {

  // ---- 稳定运行计数器清零（仅执行一次）----
  // 系统启动完成后再稳定运行超过 BOOT_STABLE_MS，则认为本次上电"稳定"，
  // 将快速断电计数器清零，避免正常使用时累积而误触发"恢复出厂设置"。
  //
  // 【重要】计时基准必须是「setup 结束之后」，而不是「上电时刻」。
  // 原因：setup() 里要播放三段启动进度动画并等待 WiFi 连接（最长约 10 秒），
  // 这段耗时本身就可能超过 BOOT_STABLE_MS。若写成 millis() >= BOOT_STABLE_MS，
  // 一进入 loop 就会立刻把计数器清零 —— 计数器永远累积不到阈值，
  // "连续快速断电恢复出厂设置"就永远无法触发（表现为全白提示不出现）。
  // 改为从 loop 首次执行时刻起算：语义也更准确 —— 设备正常启动完成后，
  // 再稳定运行满 BOOT_STABLE_MS 才算"稳定上电"。
  static bool bootCountCleared = false;
  static const unsigned long loopStartMs = millis();  // loop 首次执行时刻（≈ setup 结束）
  if (!bootCountCleared && (millis() - loopStartMs) >= BOOT_STABLE_MS) {
    bootCountCleared = true;
    EEPROM.begin(EEPROM_SIZE);
    writeBootCount(0);
    EEPROM.end();
    DEBUG_PRINTLN("系统稳定运行超过5秒，快速断电计数器已清零。");
  }

  // ---- OTA 定时自动升级（网页 OTA 卡片里开关 + 时间 + 链接）----
  // 每天在设定的小时检查一次巴法云固件的"上传时间"（Last-Modified 头），
  // 和当前固件记录的比对：没变 → 跳过（不会循环刷机）；变了 → 触发升级。
  otaAutoTick();

  // ---- 可选外设：DHT11 温湿度（网页"灯带设置"里开关）----
  periphTick();

  // ---- OTA 升级执行 ----
  // 网页点「一键升级」后置 otaPendingUrl，这里在 loop 中真正执行：
  // 调用阻塞式 httpUpdate，成功自动重启，失败打印原因并清空标志继续运行。
  if (otaPendingUrl.length() > 0) {
    String url = otaPendingUrl;
    otaPendingUrl = "";   // 先清标志，避免失败后反复重试
    // 记下这次要刷的固件文件的上传时间（Last-Modified）——升级成功后写入 EEPROM，
    // 之后每天定时检查时与巴法云文件头比对，没变就跳过（防循环刷机）。
    String remoteLM = otaFetchRemoteLM(url);
    if (remoteLM.length()) DEBUG_PRINTLN("[OTA] 远端固件上传时间: " + otaFormatLMStr(remoteLM));
    DEBUG_PRINTLN("[OTA] 开始下载固件: " + url);

    WiFiClient otaClient;
    ESPhttpUpdate.rebootOnUpdate(false);   // 成功后不自动重启，便于打印提示
    // 进度回调：网页轮询 /otastatus 读取 otaProgress，显示真实下载进度
    ESPhttpUpdate.onStart([]() { otaProgress = 0; });
    ESPhttpUpdate.onProgress([](int cur, int total) { otaProgress = (total > 0) ? (cur * 100 / total) : 0; });
    ESPhttpUpdate.onEnd([]() { otaProgress = 100; });
    t_httpUpdate_return ret = ESPhttpUpdate.update(otaClient, url);

    switch (ret) {
      case HTTP_UPDATE_FAILED:
        DEBUG_PRINT("[OTA] 升级失败: ");
        DEBUG_PRINTLN(ESPhttpUpdate.getLastError());
        DEBUG_PRINT("  原因: ");
        DEBUG_PRINTLN(ESPhttpUpdate.getLastErrorString().c_str());
        otaProgress = -2;   // 告诉网页：升级失败
        break;
      case HTTP_UPDATE_NO_UPDATES:
        DEBUG_PRINTLN("[OTA] 无可用更新（服务器返回 304 或内容相同）");
        otaProgress = -2;
        break;
      case HTTP_UPDATE_OK:
        DEBUG_PRINTLN("[OTA] 升级成功，即将重启...");
        otaSetLocalLM(remoteLM);   // 记录新固件的上传时间，之后定时检查据此判断"有无新版本"
        delay(500);
        ESP.restart();
        break;
    }
  }

  // ---- 红外遥控（网页"灯带设置"里可关闭；关闭时不扫描，省 CPU）----
  if (irEnabled && irrecv.decode(&results)) {
    hongwai(results.value);
    irrecv.resume();
  }

  // ---- WiFi 网络切换（网页里"新增网络 / 设为当前 / 改密码"后触发）----
  // 不在 HTTP 回调里直接动 WiFi（会影响正在返回的响应），改为在 loop 执行。
  if (wifiSwitchNow) {
    wifiSwitchNow = false;
    if (wifiConfig.staEnabled && wifiConfig.ssid[0] != '\0') {
      DEBUG_PRINT("[WiFi] 正在切换到网络: ");
      DEBUG_PRINTLN(wifiConfig.ssid);
      WiFi.disconnect();          // 先断开当前连接，再按新凭据重连
      delay(50);
      WiFi.begin(wifiConfig.ssid, wifiConfig.password);
      wifiRetryRestart();         // 重新开始宽限期，别让断网轮询马上把它切走
      // 标记"正在尝试这一条"：网页会显示"⏳ 正在尝试连接: xxx"；
      // 若连宽限期（30 秒）都过了还没连上，manageWiFiConnection 会给它记一次失败。
      if (wifiList.active < wifiList.count) wifiRetryIdx = wifiList.active;
    } else {
      DEBUG_PRINTLN("[WiFi] 未开启 STA 或无有效网络，跳过切换");
    }
  }

  // ---- 动态 WiFi / AP 管理（每5秒轮询）----
  // 在「STA 开、用户未配置 AP」时，自动管理临时 AP 热点：
  //   断线→立刻开AP，重连稳定1分钟→关AP
  manageWiFiConnection();

  // ---- MQTT：仅在巴法云开关开启时执行（每 10 秒重连一次，非阻塞）----
  // 开关关闭时不执行任何 MQTT 动作（不重连、不收发消息）。
  if (mqttConfig.enabled) {
    if (!client.connected()) {
      static unsigned long lastReconnectAttempt = 0;
      unsigned long now = millis();
      // 开关刚打开时（mqttReconnectNow）立即尝试一次，否则每 10 秒一次
      if (mqttReconnectNow || (now - lastReconnectAttempt > 10000)) {
        lastReconnectAttempt = now;
        mqttReconnectNow = false;
        reconnect();
      }
    } else {
      client.loop();
    }
  }

  // ---- 电量模式超时检测 ----
  if (inBatteryMode) {
    unsigned long currentTime = millis();
    bool fullTimeout    = (batteryLevel == 100  && currentTime - lastBatteryUpdate > BATTERY_FULL_TIMEOUT);
    bool chargingTimeout = (batteryLevel < 100  && currentTime - lastBatteryUpdate > BATTERY_CHARGING_TIMEOUT);
    if (fullTimeout || chargingTimeout) {
      currentMode   = previousMode;
      inBatteryMode = false;
      resetEffectState();
      DEBUG_PRINTLN("退出电量模式，恢复至: " + previousMode);
    }
  }

  // ---- 音乐模式超时检测 ----
  if (inMusicMode) {
    if (millis() - lastMusicUpdate > MUSIC_TIMEOUT) {
      currentMode = previousModeBeforeMusic;
      inMusicMode = false;
      resetEffectState();
      DEBUG_PRINTLN("退出音乐模式，恢复至: " + previousModeBeforeMusic);
    }
  }
  if (inMusicMode && currentMode != "music") {
    inMusicMode = false;
  }

  // ---- 定时关闭 ----
  if (!inBatteryMode && timerActive && millis() >= timerEndTime) {
    timerActive = false;
    currentMode = "off";
    modelbianhua = true;
    DEBUG_PRINTLN("定时器结束，已关闭灯带");
  }

  // ---- 状态变更检测（必须在灯效分发之前执行）----
  // 检测红外/网页/MQTT 是否改变了模式、颜色、亮度、速度或定时器
  {
    static uint8_t  lastBr   = brightness;
    static int      lastSp   = effectSpeed;
    static CRGB     lastCol  = solidColor;
    static bool     lastTim  = timerActive;
    static String   lastMode = currentMode;

    bool modeChg = (lastMode != currentMode);
    bool colChg  = (lastCol.r != solidColor.r || lastCol.g != solidColor.g || lastCol.b != solidColor.b);

    if (modeChg || colChg || lastBr != brightness || lastSp != effectSpeed || lastTim != timerActive) {
      if (modeChg) {
        DEBUG_PRINTLN("当前模式: " + currentMode);
      }
      if (modeChg || colChg) {
        // 计算颜色差异（RGB 分量差之和）
        int rDiff = abs((int)solidColor.r - (int)lastCol.r);
        int gDiff = abs((int)solidColor.g - (int)lastCol.g);
        int bDiff = abs((int)solidColor.b - (int)lastCol.b);
        int totalDiff = rDiff + gDiff + bDiff;

        // 只有显式模式切换，或颜色差异超过阈值，才执行动画
        if (modeChg || totalDiff > 100) {
          modelbianhua = true;  // 通知 solid_cut 重新开始动画
        }
        // 否则保持 false，solid_cut 会跳过动画
      }
      notifyStateChanged();
      lastBr   = brightness;
      lastSp   = effectSpeed;
      lastCol  = solidColor;
      lastTim  = timerActive;
      lastMode = currentMode;
    }
  }

  // ---- 灯效分发（hash→switch O(1) 跳转）----
  // 哈希算法: firstChar*256+length，已验证20种模式名无碰撞
  {
    uint16_t hash = (currentMode.length() > 0)
      ? ((uint8_t)currentMode[0] * 256U + currentMode.length())
      : 0;

    switch (hash) {
      // ---- 直接跳转型 ----
      case 29191: rainbow();              break;  // "rainbow"     ('r'*256+7)
      case 29703: theaterChase();         break;  // "theater"     ('t'*256+7)
      case 25352: confetti();             break;  // "confetti"    ('c'*256+8)
      case 25097: breathing();            break;  // "breathing"   ('b'*256+9)
      case 29447: sparkle();              break;  // "sparkle"     ('s'*256+7)
      case 24838: aurora();               break;  // "aurora"      ('a'*256+6)
      case 25605: discoMode();            break;  // "disco"       ('d'*256+5)
      case 29194: randomColorJump();      break;  // "randomjump"  ('r'*256+10)
      case 29444: stepLighting();         break;  // "step"        ('s'*256+4)
      case 29451: sparkSparks();          break;  // "sparksparks" ('s'*256+11)
      case 29197: rainbowString();        break;  // "rainbowstring"('r'*256+13)
      case 26633: heartbeatPulse();       break;  // "heartbeat"   ('h'*256+9)
      case 26120: fireworkExplosion();    break;  // "firework"    ('f'*256+8)
      case 25095: showBatteryLevel();     break;  // "battery"     ('b'*256+7)
      case 27909: musicVisualization();   break;  // "music"       ('m'*256+5)
      case 29446: /* single: 保持不动 */ break;  // "single"      ('s'*256+6)

      // ---- 带内联逻辑型 ----
      case 29445: {  // "solid" ('s'*256+5)
        int minDelay = (solidAnimMode == "fade") ? 120 : 300;
        if (millis() - lastEffectUpdate > (map(effectSpeed, 1, 100, 1500, minDelay)/num_leds)) {
          lastEffectUpdate = millis();
          solid_cut(leds, num_leds, solidColor);
        }
      } break;

      case 28419: {  // "off" ('o'*256+3)
        int minDelayOff = (solidAnimMode == "fade") ? 120 : 300;
        if (millis() - lastEffectUpdate > (map(effectSpeed, 1, 100, 1500, minDelayOff)/num_leds)) {
          lastEffectUpdate = millis();
          solid_cut(leds, num_leds, CRGB::Black);
        }
      } break;

      case 29190:  // "random" ('r'*256+6)
        randomSeed(micros());
        currentMode = modeList[random(14)];
        DEBUG_PRINT("随机选中: ");
        DEBUG_PRINTLN(currentMode);
        resetEffectState();
        break;

      case 31239:  // "zanting" ('z'*256+7)
        delay(50);
        break;

      default:  // 未知模式 → 关闭
        currentMode = "off";
        break;
    }
  }

  delay(1);  // 释放 CPU，让 AsyncWebServer 有机会处理请求
}

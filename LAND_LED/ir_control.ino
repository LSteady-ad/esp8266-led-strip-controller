/**
 * @file ir_control.ino
 * @brief LAND_LED 灯光控制系统 - 红外遥控模块
 *
 * 包含红外码解析函数 hongwai()。
 * 使用的遥控器为常见 21 键 IR 遥控器，布局如下：
 *
 *  行/列  | 1（左）          | 2（中）          | 3（右）
 *  -------|-----------------|-----------------|------------------
 *   1     | 电源 开/关切换   | 白色单色         | 暖白色单色
 *   2     | 暂停/恢复 切换   | 速度 -3          | 速度 +3
 *   3     | 定时5分钟        | 亮度 -（非线性） | 亮度 +（非线性）
 *   4     | 纯红色           | 纯绿色           | 纯蓝色
 *   5     | 彩虹模式         | 彩纸模式         | 随机跳色模式
 *   6     | 蹦迪模式         | 剧院模式         | 呼吸模式
 *   7     | 极光模式         | 心跳模式         | 星光模式
 *
 * 所有 case 值均为接收到的 NEC 协议十进制数值。
 * 长按时遥控器发送 0xFFFFFFFF（十进制 4294967295），
 * 此时用 last 保存上次有效码并重复执行（速度/亮度调节支持长按）。
 *
 * 此文件由 Arduino IDE 自动与主文件合并编译，无需额外 include。
 */

// ---- 视觉反馈闪烁辅助函数 ----
// longFlash=true: 黑→色→黑三拍 (约410ms)
// longFlash=false: 仅黑闪一拍 (约90ms)，不改变LED数组
// restoreSolid: 闪烁后是否在 solid 模式下恢复颜色
static void flashFeedback(CRGB color, bool longFlash, bool restoreSolid) {
  if (longFlash) {
    // 长闪：改变LED数组实现黑→色→黑
    fill_solid_new(leds, num_leds, CRGB::Black);
    delay(90);
    fill_solid_new(leds, num_leds, color);
    delay(230);
    fill_solid_new(leds, num_leds, CRGB::Black);
    delay(90);
    if (restoreSolid && currentMode == "solid") {
      fill_solid_new(leds, num_leds, solidColor);
    }
  } else {
    // 短闪：临时把亮度设为0实现黑闪，不改变LED数组
    uint8_t savedBrightness = FastLED.getBrightness();
    FastLED.setBrightness(0);
    FastLED.show();
    FastLED.show();  // 重复一次防止WS2812显示bug
    delay(90);
    FastLED.setBrightness(savedBrightness);
    // LED数组内容不变，调用者需要负责刷新显示
  }
}

/**
 * @brief 红外码解析与功能执行。
 *
 * @param aa 接收到的红外十进制值（长按时为 4294967295）
 */
void hongwai(uint32_t aa) {
  DEBUG_PRINTLN(aa);

  static uint32_t last = 0;
  uint32_t lastjilu = last;
  bool biaoji = true;  // true=短按新按键, false=长按重复

  if (aa == 4294967295U) {
    aa = last;
    biaoji = false;
  } else {
    last = aa;
  }

  switch (aa) {
    default:
      last = lastjilu;
      break;

    // ===== 第 1 行 =====

    case 16753245:  // [1/1] 电源开关切换（短按有效，长按忽略）
      if (biaoji) {
        static String lastmode = "solid";
        if (currentMode == "off") {
          resetEffectState();
          currentMode = lastmode;
        } else {
          lastmode    = currentMode;
          currentMode = "off";
        }
      }
      break;

    case 16736925:  // [1/2] 白色单色
      resetEffectState();
      currentMode = "solid";
      applySolidColor(CRGB::White, true);
      break;

    case 16769565:  // [1/3] 暖白色单色
      resetEffectState();
      currentMode = "solid";
      applySolidColor(CRGB(255, 190, 130), true);
      break;

    // ===== 第 2 行 =====

    case 16720605:  // [2/1] 暂停/恢复切换（短按有效）
      if (biaoji) {
        static String lastmode = "";
        if (currentMode == "zanting") {
          currentMode = lastmode;
        } else {
          lastmode    = currentMode;
          currentMode = "zanting";
        }
      }
      break;

    case 16712445:  // [2/2] 速度 -3（支持长按，最小 1）
      effectSpeed -= 3;
      if (effectSpeed < 1) {
        effectSpeed = 1;
        if (biaoji) flashFeedback(CRGB::Black, false, true);
      }
      break;

    case 16761405:  // [2/3] 速度 +3（支持长按，最大 100）
      effectSpeed += 3;
      if (effectSpeed > 100) {
        effectSpeed = 100;
        if (biaoji) flashFeedback(CRGB::Black, false, true);
      }
      break;

    // ===== 第 3 行 =====

    case 16769055:  // [3/1] 定时 5 分钟开/关（附带闪烁确认）
      if (!timerActive) {
        flashFeedback(CRGB::Green, true, true);
        timerEndTime = millis() + 5UL * 60UL * 1000UL;
        timerActive  = true;
      } else {
        flashFeedback(CRGB::Red, true, true);
        timerActive  = false;
      }
      break;

    case 16754775:  // [3/2] 亮度 -（非线性步进，短按有效）
      if (biaoji) {
        int step = map(brightness, 1, 255, 2, 45);
        if (brightness > step) {
          brightness -= step;
        } else {
          brightness = 1;
          flashFeedback(CRGB::Black, false, false);
        }
        FastLED.setBrightness(brightness);
        // 立即刷新显示，让亮度变化立即生效
        fanxiang(reverseDisplay, leds, num_leds);
      }
      break;

    case 16748655:  // [3/3] 亮度 +（非线性步进，短按有效）
      if (biaoji) {
        int step = map(brightness, 1, 255, 2, 45);
        if (brightness > 255 - step) {
          brightness = 255;
          flashFeedback(CRGB::Black, false, false);
        } else {
          brightness += step;
        }
        FastLED.setBrightness(brightness);
        // 立即刷新显示，让亮度变化立即生效
        fanxiang(reverseDisplay, leds, num_leds);
      }
      break;

    // ===== 第 4 行（颜色直选）=====

    case 16738455:  // [4/1] 纯红色
      resetEffectState();
      currentMode = "solid";
      applySolidColor(CRGB::Red, true);
      break;

    case 16750695:  // [4/2] 纯绿色
      resetEffectState();
      currentMode = "solid";
      applySolidColor(CRGB::Green, true);
      break;

    case 16756815:  // [4/3] 纯蓝色
      resetEffectState();
      currentMode = "solid";
      applySolidColor(CRGB::Blue, true);
      break;

    // ===== 第 5 行（灯效 1）=====

    case 16724175:  // [5/1] 彩虹流动
      currentMode = "rainbow";
      break;

    case 16718055:  // [5/2] 彩纸随机点亮
      currentMode = "confetti";
      break;

    case 16743045:  // [5/3] 随机色跳变
      currentMode = "randomjump";
      break;

    // ===== 第 6 行（灯效 2）=====

    case 16716015:  // [6/1] 蹦迪闪光
      currentMode = "disco";
      break;

    case 16726215:  // [6/2] 剧院追逐
      currentMode = "theater";
      break;

    case 16734885:  // [6/3] 呼吸灯
      currentMode = "breathing";
      break;

    // ===== 第 7 行（灯效 3）=====

    case 16728765:  // [7/1] 极光
      currentMode = "aurora";
      break;

    case 16730805:  // [7/2] 心跳脉冲
      currentMode = "heartbeat";
      break;

    case 16732845:  // [7/3] 星光闪烁
      currentMode = "sparkle";
      break;
  }
}

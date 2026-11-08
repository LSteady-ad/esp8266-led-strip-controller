/**
 * @file effects.ino
 * @brief LAND_LED 灯光控制系统 - 灯效模块
 *
 * 包含所有 LED 灯效函数：
 *   - 14 种基础灯效（彩虹、单色、剧院、彩纸、呼吸、星光、极光、
 *                    蹦迪、随机跳色、踏步、火花、彩跑、心跳、烟花）
 *   - 音乐可视化（music，通过 HTTP /music 接收音量数据）
 *   - 电量显示（battery）
 *
 * 所有函数依赖 config.h 中的全局变量，由 LAND_LED.ino 提供实际定义。
 * 此文件由 Arduino IDE 自动与主文件合并编译，无需额外 include。
 */

// ============================================================
// 电量显示
// ============================================================

/**
 * @brief 显示手机电量进度条。
 *
 * - 点亮数量 = batteryLevel * num_leds / 100
 * - 颜色从红（0%）渐变到绿（100%）
 * - 充电中：最后一颗灯每 500ms 闪一次白色
 * - 充满电：全部绿色每 500ms 闪灭一次（实现真正的闪烁）
 *
 * @note Bug修复：原代码充满电分支 if/else 两路都写 fill_solid Green，
 *       改为 else 时熄灭，实现真正的交替闪烁。
 */
void showBatteryLevel() {
  int ledsOn = map(batteryLevel, 0, 100, 0, num_leds);
  fill_solid(leds, num_leds, CRGB::Black);

  // 绘制红→绿渐变进度条
  for (int i = 0; i < ledsOn; i++) {
    uint8_t hue = map(i * 100 / num_leds, 0, 100, BATTERY_HUE_START, BATTERY_HUE_END);
    leds[i] = CHSV(hue, 255, 255);
  }

  // 统一闪烁状态机（充电闪烁白灯 / 满电闪烁绿灯，互斥共用一对 static）
  static bool blinkState = false;
  static unsigned long lastBlinkTime = 0;
  if (millis() - lastBlinkTime > BATTERY_BLINK_INTERVAL) {
    blinkState = !blinkState;
    lastBlinkTime = millis();
  }

  if (batteryLevel < 100) {
    if (blinkState && ledsOn > 0) leds[ledsOn - 1] = CRGB::White;
  } else {
    fill_solid(leds, num_leds, blinkState ? CRGB::Green : CRGB::Black);
  }

  fanxiang(reverseDisplay, leds, num_leds);
}

// ============================================================
// 14 种基础灯效
// ============================================================

/**
 * @brief 彩虹流动效果。
 *
 * 使用 FastLED fill_rainbow 填充整条灯带，色相每帧 +3，
 * 速度越大帧间隔越小（越快）。
 */
void rainbow() {
  unsigned long delayTime = map(effectSpeed, 1, 100, 50, 2);
  unsigned long now = millis();
  if (now - lastEffectUpdate > delayTime) {
    lastEffectUpdate = now;
    fill_rainbow(leds, num_leds, gHue, 7);
    gHue += 3;
    fanxiang(reverseDisplay, leds, num_leds);
  }
}

/**
 * @brief 剧院追逐灯效果。
 *
 * 每 3 颗灯中点亮 1 颗（固定颜色），偏移量每帧 +1，
 * 形成追逐流动感。颜色随 solidColor 设置变化。
 */
void theaterChase() {
  unsigned long delayTime = map(effectSpeed, 1, 100, 250, 25);
  unsigned long now = millis();
  if (now - lastEffectUpdate > delayTime) {
    lastEffectUpdate = now;
    for (int i = 0; i < num_leds; i++) {
      leds[i] = ((i + scannerPosition) % 3 == 0) ? solidColor : CRGB::Black;
    }
    scannerPosition = (scannerPosition + 1) % 3;
    fanxiang(reverseDisplay, leds, num_leds);
  }
}

/**
 * @brief 彩纸（Confetti）随机点亮效果。
 *
 * 每帧随机点亮一颗 LED，同时对全部 LED 做衰减，
 * 产生星星点点逐渐消散的彩纸效果。
 */
void confetti() {
  unsigned long delayTime = map(effectSpeed, 1, 100, 150, 15);
  unsigned long now = millis();
  if (now - lastEffectUpdate > delayTime) {
    lastEffectUpdate = now;
    fadeToBlackBy(leds, num_leds, 10);
    int pos = random16(num_leds);
    leds[pos] = CHSV(gHue + random8(64), 200, 255);
    gHue++;
    fanxiang(reverseDisplay, leds, num_leds);
  }
}

/**
 * @brief 呼吸灯效果。
 *
 * 亮度在 5~250 之间平滑往返，颜色跟随 solidColor 设置。
 * 速度越大，呼吸频率越快。
 */
void breathing() {
  unsigned long delayTime = map(effectSpeed, 1, 100, 20, 2);
  unsigned long now = millis();
  if (now - lastEffectUpdate > delayTime) {
    lastEffectUpdate = now;
    if (breathingUp) {
      breathingValue++;
      if (breathingValue >= 250) breathingUp = false;
    } else {
      breathingValue--;
      if (breathingValue <= 5) breathingUp = true;
    }
    CRGB color = solidColor;
    color.fadeToBlackBy(255 - breathingValue);
    fill_solid_new(leds, num_leds, color);
  }
}

/**
 * @brief 星光闪烁效果。
 *
 * 每帧随机点亮 0~1 颗随机颜色的 LED，同时所有 LED 持续衰减，
 * 形成星光随机闪烁的效果。
 */
void sparkle() {
  unsigned long delayTime = map(effectSpeed, 1, 100, 100, 5);
  unsigned long now = millis();
  if (now - lastEffectUpdate > delayTime) {
    lastEffectUpdate = now;
    fadeToBlackBy(leds, num_leds, 40);
    if (random8() < 80) {
      int pos = random16(num_leds);
      leds[pos] = CHSV(random8(), random8(150, 255), random8(180, 255));
    }
    fanxiang(reverseDisplay, leds, num_leds);
  }
}

/**
 * @brief 极光（Aurora Borealis）模拟效果。
 *
 * 利用多个正弦/余弦波叠加计算每颗 LED 的色相、饱和度和亮度，
 * 偶尔在随机位置叠加一颗白色闪光模拟极光粒子。
 */
void aurora() {
  unsigned long delayTime = map(effectSpeed, 1, 100, 70, 7);
  unsigned long now = millis();
  if (now - lastEffectUpdate > delayTime) {
    lastEffectUpdate = now;
    auroraPosition++;
    for (int i = 0; i < num_leds; i++) {
      uint8_t wave1    = sin8(i * 3 + auroraPosition * 2);
      uint8_t wave2    = cos8(i * 4 - auroraPosition * 3);
      uint8_t wave3    = sin8(i * 2 + auroraPosition * 4);
      uint8_t combined = (wave1 + wave2 + wave3) / 3;
      uint8_t hue      = 140 + sin8(combined) / 8;
      uint8_t sat      = 200 + cos8(i * 10) / 2;
      uint8_t bri      = combined / 2 + 128;
      leds[i] = CHSV(hue, sat, bri);
    }
    // 随机极光粒子
    for (int j = 0; j < 3; j++) {
      if (random8() < 50) {
        int pos = random16(num_leds);
        leds[pos] = CRGB::White;
        leds[pos].fadeToBlackBy(random8(100, 200));
      }
    }
    fanxiang(reverseDisplay, leds, num_leds);
  }
}

/**
 * @brief 蹦迪（Disco）闪光效果。
 *
 * 随机颜色快速交替白光频闪与彩色填充，
 * 每次彩色帧额外在随机位置叠加 5 颗高饱和度色彩。
 * 速度越大频闪越快。
 */
void discoMode() {
  unsigned long now = millis();

  // 每 10 秒重新随机化种子，防止长时间运行模式单调
  if (now - lastStrobeChange > 10000) {
    randomSeed(micros());
  }

  if (now - lastStrobeChange > (unsigned long)map(effectSpeed, 1, 100, 500, 0)) {
    lastStrobeChange = now;
    strobeOn  = !strobeOn;
    discoHue  = random(0, 256);

    if (strobeOn) {
      fill_solid_new(leds, num_leds, CRGB::White);  // 白色频闪帧
    } else {
      fill_solid_new(leds, num_leds, CHSV(discoHue, 255, 255));
      // 额外 5 颗随机高亮点
      for (int i = 0; i < 5; i++) {
        int pos  = random16(num_leds);
        leds[pos] = CHSV(discoHue + random8(64), 255, 255);
      }
      fanxiang(reverseDisplay, leds, num_leds);
    }
    lastEffectUpdate = now;
  }
}

/**
 * @brief 随机色跳变效果。
 *
 * 每帧随机改变若干颗 LED 的颜色（数量随速度增加），
 * 产生随机跳动的彩色效果。
 */
void randomColorJump() {
  unsigned long delayTime = map(effectSpeed, 1, 100, 200, 10);
  unsigned long now = millis();
  if (now - lastEffectUpdate > delayTime) {
    lastEffectUpdate = now;
    int changeCount = map(effectSpeed, 1, 100, 2, num_leds / 2);
    for (int i = 0; i < changeCount; i++) {
      int pos  = random16(num_leds);
      leds[pos] = CHSV(random8(), 255, 255);
    }
    fanxiang(reverseDisplay, leds, num_leds);
  }
}

/**
 * @brief 踏步式依次点亮/熄灭效果。
 *
 * 将灯带按每组 1 颗分组，先依次向前点亮（所有点亮的 LED 保持），
 * 全亮后再依次向后熄灭，循环往复，颜色跟随 solidColor。
 * 速度越大每步保持时间越短。
 */
void stepLighting() {
  static int  currentStep  = 0;         // 当前LED索引
  static bool lightingUp   = true;      // true=点亮阶段，false=熄灭阶段
  static unsigned long stepStartTime = 0;

  unsigned long holdTime  = map(effectSpeed, 1, 100, 500, 0);
  unsigned long now       = millis();

  if (now - stepStartTime > holdTime) {
    stepStartTime = now;
    if (lightingUp) {
      currentStep++;
      if (currentStep >= (int)num_leds) {
        lightingUp  = false;
        currentStep = num_leds - 1;
      }
    } else {
      currentStep--;
      if (currentStep < 0) {
        lightingUp  = true;
        currentStep = 0;
      }
    }
  }

  fill_solid(leds, num_leds, CRGB::Black);

  if (lightingUp) {
    // 点亮阶段：当前步及之前的全亮
    for (int i = 0; i <= currentStep; i++) leds[i] = solidColor;
  } else {
    // 熄灭阶段：当前步及之后的保持亮
    for (int i = currentStep; i < (int)num_leds; i++) leds[i] = solidColor;
  }

  fanxiang(reverseDisplay, leds, num_leds);
}

/**
 * @brief 随机爆闪火花效果。
 *
 * 在随机位置瞬间点亮高亮度白光，随后快速衰减；
 * 速度越大爆闪频率越高。
 */
void sparkSparks() {
  unsigned long flashInterval = map(effectSpeed, 1, 100, 500, 50);
  static unsigned long lastFlashTime = 0;
  unsigned long now = millis();

  if (now - lastFlashTime > flashInterval) {
    lastFlashTime = now;
    int sparkCount = map(effectSpeed, 1, 100, 1, 2);
    for (int i = 0; i < sparkCount; i++) {
      int pos  = random16(num_leds);
      leds[pos] = CRGB::White;
      leds[pos].maximizeBrightness();  // 强制最大亮度
    }
  }

  // 每帧衰减 50%，模拟瞬间熄灭
  fadeToBlackBy(leds, num_leds, 128);
  fanxiang(reverseDisplay, leds, num_leds);
}

/**
 * @brief 彩虹跑马串效果。
 *
 * 5 颗彩虹色光点（红橙黄绿青）均匀间隔，循环向前跑动。
 * 速度越大移动间隔越短。
 */
void rainbowString() {
  // 固定彩虹颜色序列（static const 避免每帧栈分配）
  static const CRGB rainbowColors[] = {
    CRGB::Red,
    CRGB::Orange,
    CRGB::Yellow,
    CRGB::Green,
    CRGB::Cyan
  };
  static const int colorCount = sizeof(rainbowColors) / sizeof(CRGB);

  unsigned long moveInterval = map(effectSpeed, 1, 100, 200, 0);
  static unsigned long lastMoveTime = 0;
  static int startPos = 0;

  unsigned long now = millis();
  if (now - lastMoveTime > moveInterval) {
    lastMoveTime = now;
    startPos = (startPos + 1) % num_leds;
  }

  fill_solid(leds, num_leds, CRGB::Black);

  int spacing = max(2, num_leds / (colorCount + 2));
  for (int i = 0; i < colorCount; i++) {
    int pos  = (startPos + i * spacing) % num_leds;
    leds[pos] = rainbowColors[i];
  }

  fanxiang(reverseDisplay, leds, num_leds);
}

/**
 * @brief 心跳脉冲效果。
 *
 * 颜色跟随 solidColor，快速亮起（约 200ms）→ 缓慢变暗，
 * 模拟心跳节奏。速度越大心跳周期越短。
 */
void heartbeatPulse() {
  unsigned long cycleTime = map(effectSpeed, 1, 100, 800, 300);
  static unsigned long lastBeatTime = 0;
  static bool isBeating = false;

  unsigned long now = millis();
  if (now - lastBeatTime > (isBeating ? 200UL : cycleTime - 200UL)) {
    lastBeatTime = now;
    isBeating    = !isBeating;
  }

  uint8_t beatBrightness = isBeating ? brightness : brightness / 5;
  CRGB beatColor = solidColor;
  beatColor.fadeToBlackBy(255 - beatBrightness);
  fill_solid_new(leds, num_leds, beatColor);
}

/**
 * @brief 烟花绽放模拟效果。
 *
 * 随机时间间隔触发一次烟花（随机中心位置），
 * 向两侧扩散，颜色从白色过渡到红色，到达最大半径后结束。
 * 速度越大扩散越快、触发间隔越短。
 *
 * @note ICACHE_FLASH_ATTR：将此函数代码存放在 Flash 以节省 IRAM。
 *       由于烟花效果是间歇性触发的，放 Flash 对性能影响可忽略。
 */
void ICACHE_FLASH_ATTR fireworkExplosion() {
  static int fireworkPos    = -1;  // -1 表示当前无烟花
  static int explosionSize  = 0;
  static unsigned long lastUpdate   = 0;
  static unsigned long nextFirework = 0;
  unsigned long now = millis();

  // 随机触发新烟花
  if (fireworkPos == -1 && now > nextFirework) {
    fireworkPos   = random16(num_leds);
    explosionSize = 0;
    lastUpdate    = now;
    nextFirework  = now + random16(map(effectSpeed, 1, 100, 2000, 0));
  }

  if (fireworkPos != -1) {
    unsigned long explosionInterval = map(effectSpeed, 1, 100, 100, 30);
    if (now - lastUpdate > explosionInterval) {
      lastUpdate = now;
      explosionSize++;

      fill_solid(leds, num_leds, CRGB::Black);

      // 绘制从中心向外扩散的渐变
      for (int i = 0; i <= explosionSize; i++) {
        int leftPos  = fireworkPos - i;
        int rightPos = fireworkPos + i;
        uint8_t hue  = map(i, 0, min(explosionSize, 10), 0, 10);
        CRGB color   = CHSV(hue, 255, 255);
        if (leftPos >= 0)                          leds[leftPos]  = color;
        if (rightPos < num_leds && rightPos != leftPos) leds[rightPos] = color;
      }

      // 达到最大扩散范围后结束
      if (explosionSize >= min(num_leds / 3, 10)) {
        fireworkPos = -1;
      }

      fanxiang(reverseDisplay, leds, num_leds);
    }
  } else {
    // 烟花间隙保持全暗
    fill_solid_new(leds, num_leds, CRGB::Black);
  }
}

// ============================================================
// 音乐可视化
// ============================================================

/**
 * @brief 音乐可视化效果（柔和闪烁点）。
 *
 * 根据 musicVolume（0-100）动态计算：
 *  - 点亮数量：从左向右展开，音量越大延伸越远
 *  - 颜色：低音量偏蓝，高音量偏红
 *  - 亮度：非线性映射，音量越大越亮
 *  - 闪烁点：仅在音量 > 40 时随机生成，带短拖尾
 */
void musicVisualization() {
  static float   smoothedVolume  = 0;
  static float   prevSmoothed    = 0;
  static uint8_t hue             = 0;
  static uint8_t brightnessLocal = 0;
  static uint8_t saturation      = 0;
  static float   ledFactor       = 0;
  static int     ledsToLight     = 0;

  // 三段加权平滑，减小音量抖动
  smoothedVolume = 0.3f * musicVolume + 0.5f * smoothedVolume + 0.2f * prevSmoothed;
  prevSmoothed   = smoothedVolume;

  // 颜色映射：<40 蓝区，>40 渐变到红
  if (smoothedVolume < 40) {
    hue = map(smoothedVolume, 0, 40, 160, 192);
  } else {
    hue = map(smoothedVolume, 40, 100, 192, 0);
  }

  // 非线性亮度映射
  brightnessLocal = map(smoothedVolume, 0, 100, 50, 255);
  brightnessLocal = constrain(brightnessLocal * brightnessLocal / 200, 50, 255);
  saturation      = 255 - constrain((uint8_t)(smoothedVolume / 3), 0, 50);

  // 点亮数量（指数增长，使中低音量变化更敏感）
  ledFactor   = pow(smoothedVolume / 100.0f, 1.3f);
  ledsToLight = 1 + ledFactor * (num_leds - 1);
  ledsToLight = constrain(ledsToLight, 1, num_leds);

  // 余辉效果
  fadeToBlackBy(leds, num_leds, 20);

  // 绘制渐变柱状（从左向右展开）
  for (int i = 0; i < ledsToLight; i++) {
    float distanceFactor   = (ledsToLight > 1) ? (float)i / (ledsToLight - 1.0f) : 0;
    uint8_t pixelBrightness = brightnessLocal * (1.0f - distanceFactor * distanceFactor);
    int pos = i;

    if (pos < num_leds) {
      if (smoothedVolume > 70) {
        CRGB redBoost = CHSV(hue, saturation, pixelBrightness);
        redBoost.r = min(255, redBoost.r + 50);
        redBoost.g = max(0,   redBoost.g - 30);
        leds[pos] += blend(
          CHSV(hue, saturation, pixelBrightness),
          redBoost,
          map(i, 0, ledsToLight - 1, 0, 200)
        );
      } else {
        leds[pos] += CHSV(hue, saturation, pixelBrightness);
      }
      if (distanceFactor > 0.8f) {
        uint8_t highlight = 150 + constrain((uint8_t)(smoothedVolume - 70), 0, 30) * 3;
        leds[pos] += CHSV(hue - 10, 200, highlight);
      }
    }
  }

  // 闪烁点（仅在音量 > 40 时产生）
  static uint8_t sparkCooldown  = 0;
  static int     sparkPos       = 0;
  static CRGB    sparkColor;
  static uint8_t sparkBrightness = 0;

  uint8_t sparkChance = map(smoothedVolume, 30, 90, 1, 25);
  if (sparkCooldown > 0) sparkCooldown--;

  if (smoothedVolume > 40
      && random8() < sparkChance
      && (millis() - lastMusicUpdate) < 500
      && sparkCooldown == 0) {
    sparkCooldown = 15;

    if (smoothedVolume > 70) {
      int topRegion = max(1, ledsToLight / 4);
      sparkPos = random(ledsToLight - topRegion, ledsToLight);
    } else {
      sparkPos = random(0, ledsToLight);
    }
    sparkPos       = constrain(sparkPos, 0, num_leds - 1);
    sparkBrightness = 140 + map(smoothedVolume, 40, 100, 0, 60);
    sparkColor     = CHSV(hue - 20, 200, sparkBrightness);

    leds[sparkPos] = blend(leds[sparkPos], sparkColor, 180);
    for (int j = 1; j <= 2; j++) {
      CRGB trailColor = sparkColor;
      trailColor.fadeToBlackBy(200 + j * 60);
      if (sparkPos + j < num_leds) leds[sparkPos + j] = blend(leds[sparkPos + j], trailColor, 100);
      if (sparkPos - j >= 0)       leds[sparkPos - j] = blend(leds[sparkPos - j], trailColor, 100);
    }
  }

  fanxiang(reverseDisplay, leds, num_leds);
}

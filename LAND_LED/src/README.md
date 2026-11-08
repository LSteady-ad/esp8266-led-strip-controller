# 内置第三方库

本目录存放工程用到的第三方库源码。Arduino 会把 sketch 下的 `src/` **递归编译**，所以这些库会随工程一起编译，**换电脑不需要在「库管理器」里安装任何库**。

## 目录与版本

| 目录 | 库 | 版本 | 上游 |
|---|---|---|---|
| `FastLED/` | FastLED | 3.10.4 | https://github.com/FastLED/FastLED |
| `ESPAsyncTCP/` | ESPAsyncTCP | 1.2.4 | https://github.com/me-no-dev/ESPAsyncTCP |
| `ESPAsyncWebServer/` | ESP Async WebServer | 3.6.0 | https://github.com/ESP32Async/ESPAsyncWebServer |
| `IRremoteESP8266/` | IRremoteESP8266 | 2.9.0 | https://github.com/crankyoldgit/IRremoteESP8266 |
| `ArduinoJson/` | ArduinoJson | 7.4.3 | https://github.com/bblanchon/ArduinoJson |

`PubSubClient`（2.8）和 `ESP8266httpUpdate` 没有放在这里，它们在 sketch 根目录（`LAND_LED/PubSubClient.*`、`LAND_LED/ESP8266httpUpdate.*`），一直是内联的。

## 还需要的环境

**ESP8266 开发板 core 3.1.2 仍然必须安装**（Arduino IDE → 开发板管理器）。它提供的是芯片底层头文件和编译工具，不是普通库，无法放进 `src/`：

- `ESP8266WiFi.h`、`EEPROM.h`、`ESP8266HTTPClient.h`
- `flash_hal.h`、`Updater.h`、`StreamString.h`、`MD5Builder.h`
- `Hash.h`、`SHA1Builder.h`、`libb64/cencode.h`
- xtensa 编译器、esptool 等

## 为什么库里的 include 都是引号相对路径

arduino-cli / Arduino IDE 并**不会**把 sketch 根目录或 `src/` 加入头文件搜索路径（`-I`），只按文件自身所在目录解析引号包含。因此：

- 工程里包含这些库时使用 `#include "src/FastLED/FastLED.h"` 这种相对路径（见 `config.h`）；
- 各库内部原本指向同库/其他内置库的 `#include <xxx.h>`，已全部改成引号相对路径（例如 `#include "../ESPAsyncTCP/ESPAsyncTCP.h"`）；
- 指向 core 的头文件（`<Arduino.h>`、`<Hash.h>`、`<libb64/cencode.h>` 等）保持尖括号不变。

已核对：与本目录库的上游源码相比，**只有 `#include` 行不同**，其余字节完全一致。

## 关于 FastLED 的文件改名

Arduino IDE 会校验工程内所有文件名，**不允许 `+`、也不允许以 `.` 开头**（否则打开工程时弹「无效的项目文件名」）。因此：

- FastLED 的 unity build 聚合文件 `fl/build/*+.cpp` 已统一改名为 `fl/build/*_plus.cpp`（如 `extras+.cpp` → `extras_plus.cpp`）。这些文件没有被任何 `#include` 引用（Arduino 是把它们当独立翻译单元编译的），改名不影响功能。
- `platforms/wasm/compiler/.eslintrc.cjs` 已改名为 `eslintrc.cjs`（该文件不参与编译）。

升级 FastLED 后如果又出现 `+` 文件名，需要重复这一步改名。

## 升级某个库时

1. 用上游源码覆盖对应目录；
2. 把所有「指向本工程内某个库」的尖括号 include 改成引号相对路径（可用脚本扫描：若 `<x>` 在当前库根目录下存在，就改成相对路径）；
3. 注意别把系统头文件误改，例如 `<math.h>`、`<string.h>`、`<Arduino.h>`、`<new.h>` 必须保持尖括号；
4. 重新编译验证。

## 编译验证

```powershell
arduino-cli compile --fqbn "esp8266:esp8266:generic:eesz=4M1M,xtal=80" <本工程路径>
```

已在「开发板 core 只有 3.1.2、用户库目录为空」的环境下验证通过：Flash 占用 519228 字节（49%），IRAM 93%。

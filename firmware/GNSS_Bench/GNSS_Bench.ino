// ============================================================================
//  GNSS 台架演示固件 —— 单文件版 v4(板载 RGB 灯 + 触摸屏)
// ----------------------------------------------------------------------------
//  一台"看得见状态翻转"的桌面验证台架,不是产品样机。
//
//  硬件:ESP32-S3 开发板(用板载 WS2812 彩色灯) + ATGM336H-5N + 2.4" ILI9341 触摸屏
//        + SHT30-D 温湿度(I2C:SCL=GPIO7 / SDA=GPIO8,走核心自带的 Wire,不用装库)
//          ★ 屏的 BLK 本项目直接接 3.3V 常亮,不占 IO —— GPIO7 就是这么空出来的
//  环境:Arduino IDE,开发板选 "ESP32S3 Dev Module",库只装 TFT_eSPI
//        板载彩灯走 Arduino-ESP32 核心内置函数,不用再装 Adafruit NeoPixel
//        (核心 2.x 老版本才需要,见文件顶部 LED_USE_BUILTIN 开关)
//  用法:
//    1) 先按 User_Setup_GNSS_Bench.h 把屏的引脚挂进 TFT_eSPI —— 那一步不做,屏不亮
//    2) 把这个 .ino 放进一个同名文件夹,直接编译烧录
//
//  引脚:全部照抄自已交付项目 RF_Source_Controller 的实测接线,不是猜的。
//        固件里有编译期自检,User_Setup 引脚对不上会直接编译报错,不用烧进去试。
//
//  指示 (★★ v1.7.0 主界面整块重排 —— 下面是**现行**口径,别照旧版注释去找色带/边框):
//    屏上  240x320 竖屏,三段式:
//      y   0..239  星空图区 —— 圆盘 R80(30/60° 细分圈 + 十字准线;星点按仰角/方位角落点,
//                  点径与颜色由 C/N0 和星座定)+ **最外圈 2px 状态光环**(R83/84)
//                  + 四角 HUD 块 92x32(左上 日期/时间、右上 纬度/经度、
//                    左下 温度/湿度、右下 漂移/HDOP)+ 顶部正中 WiFi 图标(4 段弧)
//                  ★ 状态色**只体现在那一圈光环**和数据条的状态词上(绿=INTEG OK /
//                    黄=DEGRADED / 橙=SIG-BAD / 红=POS-BAD / 红闪=LOST / 灰=SEARCH·NO-DATA /
//                    **紫=HPL-HIGH** ★ v1.9.0 新增的第三态 /
//                    **琥珀=DRIFT** ★ v1.10.1 新增的"慢漂移"态)
//                    —— 原来那圈"顶部 100px 色带 + 全屏 3px 边框"v1.7.0 已删除
//      y 240..273  按键条 —— 4 键等宽 60px:BASE / CLR-BS / CHARTS / LOG(★ v1.9.0 高 40->34)
//      y 274..319  数据条 —— 左:FONT4 状态词(居中);右三行:HPL / CN0 / SAT(★ v1.9.0 40->46)
//      ★ 报文窗**不再常驻**:在数据条上长按 ≥800ms 切出 8 行报文窗(再长按切回)
//      ★ 深色主题:页底 #0C0F13,卡面 / 面板 / 按键三级深灰,文字两级灰白 —— 一处常量定义全局引用
//      ★ 全英文界面:内置 FONT4(Font32rle)是唯一含完整 96 ASCII 的大字模,
//        原来的 32x32 中文字模(cnfont)v1.7.0 已整段删除(省 ~2.7KB flash + 2KB RAM)
//    板上  那颗 WS2812 同步同色 —— 远处看灯,近处看屏
//          ★ 灯和光环取的是同一个 statusColor565(),所以改配色只改那一处,
//            灯自动跟着变,不存在"屏改了灯没改"的可能。
//          ★ 它挂在 **GPIO48**,是**占掉一个脚**的。"板载"不等于"免 IO"。
//            好在你台架的脚位(GNSS 16/17/18、屏与触摸 4/6/9/10/11/12/13、I2C 7/8)
//            一个都没用 48,所以对你来说等于白捡一颗灯。
//
//  六个页面 (★ v1.7.0 从 2 页扩到 4 页;★ v1.10.0 加 PARAMS / 编辑两项;
//             各页 sprite 互斥,**峰值只占一份** 153,600 B):
//    主界面    星空图 + 按键条 + 数据条                       (两块 sprite:240x240 + 240x80)
//    第二界面  CHARTS 动图页 —— 每星 C/N0 竖条 / C/N0 中位曲线 / INTEGRITY 保护级 /
//              DETECTORS(CUSUM + 多径)(四张 234x64/64/76/79;★ v1.9.0 删掉定位漂移盘;
//              ★ v1.10.1 按内容重新配平:两张图各让 16px 给下面两张算法卡,
//              INTEGRITY 卡新增一行 `OFF .. / LIM ..` —— 现场能直接看出是"距离越线"
//              还是"慢漂移",而不是只看到一个 POS-BAD 干瞪眼)
//    第三界面  EVENT LOG —— 事件列表 10 行 x 22px + 底栏统计摘要,可上下滑动翻页
//    第四界面  事件详情 —— 单条事件的类型/时刻/持续/偏离 + 事件开始那条 GGA 原文
//    第五界面  ★ v1.10.0 PARAMS —— 八个可调阈值(判据类六个 + 迟滞两个),点行进编辑
//    第六界面  ★ v1.10.0 单项编辑 —— 大字当前值 + 刻度条(只指示)+ `-5/-1/+1/+5` 四键
//    ★ BACK 按钮几何**全局一处**(BACK_X/Y/W/H),第二~六界面共用同一组;
//      第四界面 BACK 回**第三界面**,第六界面(BACK/DONE)回**第五界面**;
//      第五界面 BACK 回**主界面**(不回来路 —— 调完参数就是想看主界面反应)。
//
//  控制(两条路并存,互不影响):
//    点屏  星空区 y 0..239  -> 无动作(误触安全 —— 比旧版"点顶部就改基准"安全得多)
//          按键条 y 240..273 -> x 四等分:BASE / CLR-BS / CHARTS / LOG
//          ★ v1.8.3:第 2 键原来叫 INJECT,**按住 5 s** 才生效(短按只打一行日志)——
//            防误触。生效后该键亮绿,表示"当前没有基准"(与 BASE 的 SET 绿互斥:
//            同一时刻只有一个键是绿的)。见 touch::poll / disp::btnState。
//          ★ v1.10.0:第 3 键 CHARTS **按住 1.5 s → 参数页**(短按仍是第二界面)。
//            进度条与 CLR-BS 同一套观感,但阈值只有 1.5 s —— 它不具破坏性。
//          数据条 y 274..319 -> **长按 ≥800ms** 切报文窗 / 切回(短按无动作)
//          ★ v1.7.0 起**动作一律在"抬起"时执行** —— 只有抬起才能分辨"点击"与"滑动";
//            旧版在"刚按下"时就动作,分不出滑动。
//    串口  115200,输入单字符回车
//      b = 把当前坐标存为基准(★ v1.7.0 起**会落盘**:写 NVS `gnsjudge/base`,重启自动恢复)
//      c = 清除基准(连 flash 里那份一起擦)
//      ★ v1.8.3:"注入"功能**整套删除**(用户 2026-10-17 定 —— 不再做演示)。
//        原来这里还有 `i = 切换注入偏移:比对坐标偏离基准 120 m`,连同判据里的
//        偏移坐标、屏上第 2 键的 INJECT 态、C3 菜单的 INJECT 项一起删干净。
//        屏上第 2 键改为 **CLR-BS(清基准),按住 5 s 才生效**。
//      s = 立即打一行状态
//      t = 立刻读一次温湿度(不等自动的 2 秒节拍,现场查接线用)
//      g = PPS 观测:两行 —— 第 1 行是最近一拍(仅参考),第 2 行是**自开机起的积分平均**,
//          积分那行才是能当结论的 ppm(单拍只有 1us 分辨率 = ±1ppm 噪声)。
//          未定位时模块不出 PPS。
//      (原 1/2/3/0 = 模拟信源开关 —— ★ v1.7.0 整段删除,见下面"第 1.5 段"的说明)
//      v = S3 日志页开关(★ v1.8.0 改义 —— 原来是串口日志开关;C3 的 LOG 键要的是屏上日志页)
//      d = 切第二界面「动图页」(与"点按键条 CHARTS"完全等价)
//      w = WiFi 存档:wadd <ssid> <pass> / wseed <ssid> <pass> / wtry [n] / wlist / wdel <n> / wconn [n] / wscan
//          ★ `wtry` —— C3 的 PUSH 收尾发它:从 #1 起挨个试连,连上即停、失败跳下一条。
//          ★ `wscan` —— **异步**扫一遍周边热点,结果分条直发 C3 命令板,
//            是"WIFI / S3 / SCAN"的服务端。自己屏上不显示列表(那是 C3 的活)。
//      r = 清触摸校准(清的是 **S3 这台** 的触摸;C3 命令板有它自己那套)
//      p = ★ v1.10.0 **参数表**:打印 PARAMS 页那八个数(值 / def / 范围 / 步长 / 改过哪几项)
//      k = ★ v1.10.2 **演示注入**(电脑替手指按屏,演示录屏用):
//            kt <x> <y>                  点一下(120 ms)
//            kh <x> <y> <ms>             长按 ms 毫秒(kh 150 257 1800 = 进参数页)
//            ks <x1> <y1> <x2> <y2> [ms] 划动(默认 400 ms;翻页手势)
//          坐标 = 屏绘图坐标(0..239 / 0..319)。合成值在 poll() 里**替代** getTouch 的
//          返回值 ⇒ 下游全是原路径,按键高亮 / 长按走条 / 滑动都是真的。
//          **不进链路白名单**,C3 发不进来(只有电脑 USB 串口能敲)。
//      ? = 帮助 —— 共 **17 条**
//      ★ 已删除(别找了):l = 试灯(选脚任务早已完成,且会让画面停 8 秒)、
//                        i = 注入偏移(v1.8.3 整套删)。
//      ★ 注:`p` 这个字母在 v1.8.3 曾随"坐标链路打印"一起删掉,v1.10.0 拿回来
//        改作参数表 —— 语义完全无关,只是复用字母。
//      ★ 第三/四/五/六界面(事件列表 / 详情 / 参数 / 编辑)**没有专用串口入口** ——
//        `d` / `v` 只能切到第二 / 三界面;要进更深的那几层,用 `k` 注入坐标
//        (v1.10.2 起):长按 CHARTS → 参数页,点列表行 → 详情。
//
// ----------------------------------------------------------------------------
//  ★ 版本沿革(v1.5.10 → v1.5.25 的**完整**改动记录,含每次为什么改)见
//    docs/变更记录-S3.md —— 文件头只留**现在有效**的口径,历史过程不再堆这里。
// ----------------------------------------------------------------------------
//  ★★ 现行口径(改代码前先读这一段)★★
//   ① **与 C3 的链路走 `UART2(Serial2)`**,脚位 **GPIO44(RX) / GPIO43(TX)**
//      (芯片官方 `U0RXD`/`U0TXD`),与 C3 的 GPIO20/21 配对。
//      ⚠ **不能用 `Serial1`** —— 那是 UART1,被 GNSS 模块的 `port(1)` 占着(16/17):
//        换脚会让 GNSS 收不到数据、C3 的命令被 gnss::poll() 当 NMEA 吃掉(v1.5.11 踩过)。
//      ⚠ 43/44 曾被判"被板载 CP2102N 顶着、做不了" —— **该结论已推翻**(真因是面包板
//        孔松);38/39 那段历史一并作废,别再照旧注释接线。
//   ② **链路协议**:收 `@k <payload>`、回 `@s ok <payload>`。S3 只认 `@k ` 前缀;
//      收到后取**首字符**交给 `consoleKey()` —— 与电脑串口**同一份分派**(链路侧有
//      白名单 `linkKeyAllowed()`,新增可远程触发的命令要同步登记,见 benchlink::poll)。
//   ③ **编译必须带 `PSRAM=opi`**,且本目录 `build_opt.h`(一行 `-DCONFIG_SPIRAM_SUPPORT`)
//      必须保留 —— 漏任一个,动图页的全屏 sprite 会落回内部 RAM 而进不去。
//      另:`User_Setup_GNSS_Bench.h` 里 **USE_DMA 必须保持关闭**(开了就走不了 PSRAM 分支)。
//   ④ 命令共 **17 条**(★ v1.10.2 加 `k` 演示注入:16 -> 17;★ v1.10.0 加 `p` 参数表:15 -> 16;
//      ★ v1.8.3 删 `i`:16 -> 15。敲 `?` 打印;与 README 命令表同一份清单)。
//   ⑤ **双轨版本号**:升版依据唯一 = "完成一轮用户确认的改动 → 号 +1";
//      MAJOR 只在换硬件/改协议时动。规则见 AGENTS.md「版本号规则」。
//   ⑥ 板载 WS2812 彩灯挂 **GPIO48** —— "板载"不等于"免 IO",换板子要重新确认。
//   ⑦ **NVS 命名空间**(v1.7.0 起共四个,**互相独立、别合并**):
//        `wifi10`    WiFi 台账(最多 10 条,FIFO 覆盖最旧)  ← 沿用旧名,别改(一改台账即丢)
//        `gnstouch`  触摸校准 5 点 + cal_flag
//        `gnsjudge`  判据基准 `base`(28 字节 blob,带 magic 'GSJ1' + 版本 + 范围校验)
//        `gnsevt`    事件日志 ev00..ev499 + `stat` + `seq`
//      ⚠ 判据基准和触摸校准**曾经共用一个命名空间**,而 judge::clearBase() 里是
//        prefs.clear() —— 会连校准值一起抹掉。分开就是因为这个。
//      ⚠ 新增命名空间时:判断一下有没有别的 `prefs.clear()` 会扫到它。
//   ⑧ **★★ 整块 sprite 的绘制入口,第一件事必须是「填页底色」** ——
//      `skySpr->fillSprite(CLR_PAPER)` / `botSpr->fillSprite(CLR_PAPER)`。
//      这类函数只覆盖"它自己关心的那几块",**没覆盖到的像素留的是上一帧/上一页的内容**:
//        · 报文窗与"按键条+数据条"**共用 botSpr** ⇒ 切回按键条不填底 = 8 行 NMEA 原文
//          横贯整条数据条(就压在状态大英文上下);
//        · 四个按键块之间那 2px 页底缝、以及数据条"只画字不铺底" ⇒ 上一次更长的状态词
//          (DEGRADED -> NORMAL)会在两侧留下字尾。
//      症状统称"屏上有不干净的显示",而且**不报错、不崩、串口一切正常** —— 只能靠眼睛。
//      v1.7.2 修过一次(paintBot 漏了 fillSprite);`paintSky` 从 v1.7.0 起就是对的。
// ----------------------------------------------------------------------------

//
//  完整交接说明见项目根目录:README-交接（笔记本继续）.md
//
//  代码分段,按顺序读就是完整的数据流:
//    ★ 配置区 -> 1 收 -> 2 解 -> [状态词表] -> 3 判 -> 灯 -> 4 显(主界面)
//       -> 4.2 动图页(CHARTS) -> 4.3 事件日志(EVENT LOG) -> 页面切换(ui)
//       -> 触摸 -> 4.5 屏面镜像报文 -> 5 主循环
// ============================================================================

// ---- 板载 WS2812 彩灯的驱动方式 —— 必须在下面 #include 之前定下来 ----
//   1 = 用 Arduino-ESP32 核心内置的 rgbLedWrite(),**一个库都不用装**(核心 3.x 走这条,推荐)
//       官方头文件 cores/esp32/esp32-hal-rgb-led.h 里:
//         void rgbLedWrite(uint8_t pin, uint8_t r, uint8_t g, uint8_t b);
//         void neopixelWrite(...);   // 旧名,已标 deprecated,仅作向后兼容保留
//       所以核心 3.x 用 rgbLedWrite,核心 2.x 用 neopixelWrite,两条都在下面的代码里写好了。
//   0 = 用 Adafruit NeoPixel 库,核心 2.x 老版本可走这条,需要在库管理器里装库
#define LED_USE_BUILTIN  1

#include <Arduino.h>
#include <TFT_eSPI.h>
#if !LED_USE_BUILTIN
  #include <Adafruit_NeoPixel.h>
#endif
#include <Wire.h>            // SHT30 温湿度走 I2C,核心自带,不用装库
#include <esp_system.h>      // esp_reset_reason():查"这次开机是被谁弄醒的"(见第 5 段)
#include <WiFi.h>             // v1.5.6:WiFi 存档/连接(核心自带)
#include <esp_heap_caps.h>   // v1.5.16:heap_caps_get_largest_free_block —— 临时内存探针用
#include <esp_wifi.h>        // v1.5.16:esp_wifi_deinit() —— 扫描收摊时真正释放射频驱动内存
#include <Preferences.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---- RTC 慢时钟源探针(2026-10-01 加)---------------------------------------
//  ESP32 内部有**两套互不相干**的时钟,这是回答"板子上到底有没有晶振"的关键:
//    · 40MHz 主晶振 —— 在 WROOM 模组的**金属屏蔽罩里面**,板子表面根本看不见。
//      它驱动 CPU / micros() / millis(),精度 ±10~30ppm(1 小时最多差 0.1 秒)。
//    · RTC 慢时钟 —— 驱动 time() / gettimeofday() / 深度睡眠计时。
//      **默认走片上内部 RC(~136kHz,±5% ≈ 5 万 ppm,1 小时可差 3 分钟!)**。
//      只有外挂了 32.768kHz 晶振才会读到 ~32768Hz。
//  两者差 4 倍,一读就能分清,不用拆板子、不用猜。
//  用 __has_include 兜底:核心若没有这两个头文件就自动跳过,绝不拖累编译。
#if __has_include("esp_clk_tree.h") && __has_include("soc/clk_tree_defs.h")
  #include "esp_clk_tree.h"
  #include "soc/clk_tree_defs.h"
  #define HAVE_CLK_TREE 1
#else
  #define HAVE_CLK_TREE 0
#endif

// ESP_ARDUINO_VERSION_MAJOR 用来在 rgbLedWrite(核心 3.x)与 neopixelWrite(核心 2.x)之间二选一。
// 这个宏由核心的 esp_arduino_version.h 提供,并非所有版本都会从 Arduino.h 透出来,这里显式带上。
// 万一某个分支没有这个头文件也不影响 —— 退回 2.x 分支,而 neopixelWrite 在 3.x 里同样还在。
#if defined(__has_include)
  #if __has_include(<esp_arduino_version.h>)
    #include <esp_arduino_version.h>
  #endif
#endif

// ============================================================================
//  ★ 配置区 —— 要改的东西全在这里
// ============================================================================

// ---------------- 引脚 ----------------
// 接法(2026-09-30 接真模块,按用户实物接线):
//   模块 TXD --> ESP32 GPIO16 (RX1)   模块 RXD --> ESP32 GPIO17 (TX1)
//   模块 PPS --> ESP32 GPIO18         模块 VCC/GND 另接
// ★ 交叉接线:模块的"发"接芯片的"收"。名字容易绕,以"信号流"为准:
//   GPS 数据流方向  TXD(模块) ──> GPIO16 ──> UART 接收
constexpr int PIN_GNSS_RX    = 16;   // ESP32-S3 RX1  <-- 模块 TXD
constexpr int PIN_GNSS_TX    = 17;   // ESP32-S3 TX1  --> 模块 RXD(只收不发可不接)
constexpr int PIN_GNSS_PPS   = 18;   // 模块 PPS(1Hz 秒脉冲) --> 用于校时/驯服,见 pps 段

// ---- 屏幕 ----
// ★ 这里不写死脚号,直接引用 User_Setup.h 里的宏(已随上面的 <TFT_eSPI.h> include 进来)。
//   TFT_eSPI 初始化读的就是那几个宏,固件跟着用同一份 —— 从根上不可能不一致。
//   所以原来那串 "#ifdef TFT_xx + static_assert(两份拷贝是否相等)" 全部删了:
//   与其写 8 条代码去守"两份拷贝相等",不如压根只留一份。
//
//   屏引脚(仅供人读 —— 真值在 User_Setup 里,固件不再重复定义一份):
//     CLK=12  MOSI=11  MISO=13  CS=10  DC=6  RES=4   BLK->3.3V
//
//   ★ 2026-10-15 删除:PIN_TFT_SCLK / MOSI / MISO / CS / DC / RST 六个**镜像常量**。
//     它们值永远对(就是 TFT_eSPI 的宏),但**全文没人引用** —— 真引脚在
//     `TFT_eSPI/User_Setups/Setup_*.h` 里,读代码的人却会以为"改这里就能改引脚"
//     (本项目正踩过"改了定义、杜邦线还插在旧孔上")。**引脚清单只留上面那行 +
//     `docs/接线表-双板与彩色杜邦线配色.md` 那一份。** PIN_TFT_BL 例外(它真被用)。

// ---- 背光 ----
// ★ 本项目实测:屏的 BLK 直接接 3.3V 常亮,**不占 IO**,所以默认不驱动任何脚。
//   这份引脚表是从另一个项目沿用的,那边背光走 GPIO7;本项目 GPIO7 空着,
//   已经给下面的 SHT30 当 I2C 的 SCL —— 哪天改成 GPIO 控背光,务必先看这条冲突。
#define TFT_BL_ON_GPIO    0          // 1=背光由 GPIO 控制(需在 User_Setup 里定义 TFT_BL)
                                     // 0=背光直连 3.3V 常亮(本项目)
#if TFT_BL_ON_GPIO
  #ifndef TFT_BL
    #error "TFT_BL_ON_GPIO=1,但 User_Setup 里没有 #define TFT_BL —— 背光脚无处可寻。补上它,或把本开关改回 0。"
  #endif
  constexpr int PIN_TFT_BL = TFT_BL;
#else
  constexpr int PIN_TFT_BL = -1;   // 不驱动。disp::begin() 里的背光段整体被 #if 关掉
#endif

// ---- SHT30-D 数字温湿度(I2C) ----
// 驱动走 ESP32 核心自带的 Wire,**不用装任何库**。
// 接线:VCC->3.3V   GND->GND   SCL->GPIO7   SDA->GPIO8
// 市面上卖的基本都是模块,板上已带 4.7k 上拉;若是裸芯片,SDA/SCL 各要一只 4.7k 上拉到 3.3V。
constexpr int PIN_SHT_SCL    =  7;
constexpr int PIN_SHT_SDA    =  8;

// ---- 触摸(XPT2046,屏上直接点,省得敲笔记本) ----
// 片选 TOUCH_CS / 中断 TOUCH_PEN 由 User_Setup 定义、TFT_eSPI 自己用,固件不碰,故不在这里重列。
// 当前接线:T_CS=GPIO9,T_IRQ=GPIO5(轮询方式,不接也行)。
// 注意:这个开关必须是宏(#define),不能用 constexpr —— 下面要拿它做 #if 判断。
#define ENABLE_TOUCH  1              // 1=开触屏控制  0=只用串口命令

// ---------------- 编译期自检 ----------------
// 思路换了一下,所以条数从 8 条降到 3 条:
//   原来的 8 条守的是"固件里抄的引脚表 == User_Setup 里的引脚表"(两份拷贝是否相等)。
//   现在固件不抄了、直接引用宏,那两个值天然相等,断言没意义 —— 删。
//   但还有一件真正要守的事:**当前生效的 User_Setup 是不是实物对应的那一份**。
//   挂错配置(或没在 User_Setup_Select.h 里选中)的后果是 GPIO 全不对、屏根本不亮,
//   看起来却像固件坏了,极难查。台架接线是焊死的物理事实,所以照它查一次,一次管全部。
static_assert(TFT_SCLK == 12 && TFT_MOSI == 11 && TFT_MISO == 13 &&
              TFT_CS   == 10 && TFT_DC   ==  6 && TFT_RST  ==  4,
              "当前生效的 User_Setup 引脚不是台架接线(应为 CLK12 / MOSI11 / MISO13 / CS10 / DC6 / RES4)。"
              "多半是 User_Setup 没挂对或没选中 —— 见 User_Setup_GNSS_Bench.h 顶部的三步说明。");

#if ENABLE_TOUCH && !defined(TOUCH_CS)
#error "ENABLE_TOUCH=1,但 User_Setup 里没有 #define TOUCH_CS —— 触摸驱动根本没编进来。要么在 User_Setup 里打开 TOUCH_CS,要么把 ENABLE_TOUCH 改成 0。"
#elif ENABLE_TOUCH
static_assert(TOUCH_CS == 9, "触摸片选不是 GPIO9 —— 与台架接线不符,同上,先检查 User_Setup 挂对没有。");
#endif

#if TFT_BL_ON_GPIO && defined(TFT_BL)
static_assert(TFT_BL != PIN_SHT_SCL,
              "背光脚和 SHT30 的 SCL 分到了同一个脚 —— 一个脚不能既驱动背光又跑 I2C。"
              "改 User_Setup 里的 TFT_BL,或者给 SHT30 的 SCL 换一个脚。");
#endif

// PPS 是中断输入脚,和 I2C 的两条线不能撞(撞了秒脉冲会打断 I2C 时序,读数开始随机失败)。
static_assert(PIN_GNSS_PPS != PIN_SHT_SCL && PIN_GNSS_PPS != PIN_SHT_SDA,
              "PPS 脚和 SHT30 的 I2C 线(SCL=7 / SDA=8)撞了 —— 秒脉冲会打乱 I2C 时序。换个脚接 PPS。");

// ---------------- 板载 WS2812 彩色灯 ----------------
// ★ 先纠正一个常见误解:板载彩灯**是占一个 GPIO 的**,不是"不占 IO"。
//    它是 WS2812(单总线可寻址灯),数据线必须挂到一个脚上:
//      官方 ESP32-S3-DevKitC-1  v1.1 → GPIO48
//      官方 ESP32-S3-DevKitC-1  v1.0 → GPIO38
//      国产 N16R8 / 克隆板常见        → GPIO48(也有 38 / 21 / 47)
//    "板载"只代表不用外接线,不代表不花脚位。好在你台架的脚位表里没有 48,等于白捡。
//
// ⚠ 有些板子的 RGB 数据线出厂是**断开**的:灯旁边有一对丝印为 "RGB" 的小焊盘,
//    要用锡短接后才连到 GPIO48(少数板子出厂已短接)。没短接的表现是:
//    程序烧录正常、屏也正常,就是灯不亮 —— 别怀疑代码,先看那对焊盘。
//
// 灯的实际脚位本项目**已经查清**:本板是 ESP32-S3-DevKitC-1 v1.1 → **GPIO48**,
// 代码里就是 LED_RGB_PIN = 48。以前那条 'l' 试灯命令(v1.5.10 已删)是当初"不知道
// 灯在哪"时的临时手段,现在脚位是已知事实,不再需要。若哪天换了板子要重新找,
// 若颜色反了(该显红却显绿),说明这颗灯的色序不是 GRB,见下面 led::writePix 里的注释。
constexpr int LED_RGB_PIN = 48;
constexpr int LED_BRIGHT  = 60;      // 0~255,WS2812 全亮很刺眼,60 足够看清

// ---------------- 串口 ----------------
constexpr long GNSS_BAUD      = 9600;  // ATGM336H 出厂默认 9600 8N1
constexpr int  NMEA_LINE_MAX  = 100;   // 单行最大字符数
constexpr int  NMEA_POOL      = 24;    // 环形缓冲保留行数

// ---------------- 判据阈值(对齐参赛方案 6.2 双阈值 + 7.10 定点一致性)----------------
constexpr int      TH_MIN_SATS_USED  = 4;      // 参与定位星数下限
constexpr float    TH_MAX_HDOP       = 5.0f;   // HDOP 上限
constexpr float    TH_MIN_CN0_MEDIAN = 25.0f;  // C/N0 中位数下限 (dB-Hz)
constexpr float    TH_MAX_OFFSET_M   = 30.0f;  // 偏离基准坐标上限(米)
constexpr uint32_t TH_HOLD_MS        = 5000;   // 迟滞:异常连续 5 s 才报警
constexpr uint32_t TH_RECOVER_MS     = 8000;   // 迟滞:正常连续 8 s 才解除
constexpr uint32_t TH_STALE_MS       = 3000;   // 数据超时视为无数据
constexpr uint32_t TH_COLDSTART_MS   = 90000;  // 冷启动宽限:没定过位且不到 90 s 只提示不报警

// ★ v1.8.3:原来这里有一个「注入量」常量(INJECT_OFFSET_M = 120 m),注入功能整体删除,
//   常量一并去掉 —— 判据现在**只比对真实坐标**,固件里没有任何改写输入的口子。

// ============================================================================
//  ★★ v1.9.0 新增段:完好性(integrity)+ 慢漂移(CUSUM)+ 多径(仰角模板)
// ----------------------------------------------------------------------------
//  这三个算法共用一条**前提**,答辩时必须主动说清,别含糊:
//    我们手里**没有原始伪距 / 星历 / 载波相位** —— NMEA 只给"算完的结果"。
//    所以做不了测量域 RAIM(那要在伪距上做残差检验),这里做的是**解域**
//    (solution domain)完好性:用 GGA 的 HDOP/星数 + GSA 的 PDOP/VDOP/参与星表
//    + GSV 的仰角/方位/C-N0 去**估计**当前解的可信度。
//    这个分支在文献里是站得住的(SBAS/GBAS 的保护级判据就是解域的),
//    但它**不是** RAIM,也不是认证级参数 —— 见下面 kHplK 的注解。
// ============================================================================

// ---- ① 单星测距噪声模型:σ(C/N0) = kSigRefM · 10^((kCn0Ref − C/N0)/20) ----
//  推导:信号**幅度** ∝ 10^(C/N0/20),而测距(码相位)抖动的标准差 ∝ 1/幅度
//       ⇒ σ ∝ 10^(−C/N0/20) ⇒ C/N0 每降 6 dB,测距噪声翻一倍。
//  一个参考点 + 一条固定斜率,不需要标定表 —— 现场想整体挪,只改 kSigRefM 一个数。
constexpr float kSigRefM = 0.6f;    // C/N0 = 45 dB-Hz 时的单星测距噪声(米)
constexpr float kCn0Ref  = 45.0f;   // 上面那个参考点(dB-Hz)
constexpr float kSigMinM = 0.25f;   // 下限:再好的星也不许算出 0(否则 HPL=0 像"绝对准")
constexpr float kSigMaxM = 40.0f;   // 上限:半边天被挡时防止 HPL 溢成天文数字

// ---- ② 保护级:HPL = kHplK · HDOP · σ_UERE ----
//  kHplK = 5.33 取自航空界的常用量级(单侧虚警 ~1e-7 / 漏检 ~1e-3 量级的多重覆盖因子),
//  **是量级对标、不是认证参数**。所以屏上和文档里一律写 "HPL(est)" ——
//  这个"est"字不能省,省了就等于宣称自己是适航级。
constexpr float kHplK = 5.33f;

// ---- ③ 告警限 AL ----
//  ★ v1.10.0:AL 与 POS LIM **不再是同一个常量**。
//    原来这里写着 `constexpr float kAlertM = TH_MAX_OFFSET_M;` + 一条 static_assert
//    把两者锁死同值(都 30 m)。加参数页时必须拆开,而且**拆开本身更正确**:
//      AL      = "我这句保证负责到什么精度"(完好性框架里的 alert limit)
//      POS LIM = "偏多少算异常"(业务判据)
//    两者碰巧都是 30 m 是因为这一版想让"报警"和"保证"用同一把尺子,
//    不是因为它们语义相同。航空界 AL 本来就是**必须现场可配**的:
//    LPV-200 = 35 m / APV-I = 40 m / 海事 556 m。可配置不是妥协,是设计意图。
//    默认值仍是 30 m ⇒ 出厂观感一个字都没变。
//    真正不能动的是 kHplK 与 σ 模型 —— 那是**设备标定项**,不是现场配置项
//    (理由写在下面 ⑦ 段的段头)。

// ---- ④ HPL 迟滞:★ v1.10.0 起与判据**共用** tune::holdMs / tune::recoverMs ----
//  原来这里是 kHplHoldMs / kHplRecoverMs 两个 constexpr(= TH_HOLD_MS / TH_RECOVER_MS)。
//  已删除:一组时间常数只能有**一个真源**,否则"屏上把 HOLD 改成 20 s、HPL 迟滞
//  还在用 5 s"这种错从代码上根本看不出来 —— 现场只会觉得"改了没用"。

// ---- ⑤ CUSUM(单侧、向上):抓"慢慢挪",阈值法抓不到的那一类 ----
//  为什么需要它:真实的位置欺骗往往**不是瞬移**,而是一分钟拉偏几米。
//  阈值法(off > 30 m)要等到 30 m 才响;CUSUM 累积"每拍超出正常起伏的那一点点",
//  配合下面 8 m 的绝对闸,能在 8 m 量级就给出"这是持续趋势"的判据 —— 这段时间
//  足够人去做处置。
//
//  ★ 公式(教科书列表型 CUSUM):
//        S ← max(0, S + (off_filt − μ) − k)
//    μ 是**慢基线**(τ≈5.5 min),代表"平时稳定在哪儿"。
//    ⚠ 这一项不能省!第一版我写成 S += (off − k),等于拿 **0** 当基线 ——
//      而 off 是**距离、恒 ≥ 0**,基准本身差 1~2 m 是常态,于是 S 每秒都涨,
//      十来秒后就永久报"在漂"。**恒定偏差不是漂移**,漂移是"相对自己以前的偏离"。
//      这类错误的形态很难看:设备明明一动不动,却说它一直在漂。
//  归一化:不做。留物理单位(m / m·s),现场改参数时不用换算,也便于对着串口读。
constexpr float kCusumAlpha = 0.05f;   // 一阶低通 α(1 Hz ⇒ τ ≈ 20 s):压掉逐拍噪声
constexpr float kCusumMuA   = 0.003f;  // 慢基线 α(τ ≈ 5.5 min):"平时在哪儿"
constexpr float kCusumSlack = 1.0f;    // 允许的正常起伏(m):偏差小于它就不累积
constexpr float kCusumH     = 20.0f;   // 判决门限(m·s):约等于"多出 1 m 持续 20 s"
constexpr float kCusumMinM  = 8.0f;    // ★ 绝对闸:偏离还没到 8 m 就不许判"漂移"
                                       //   (基准本身差几米时,靠它把噪声挡在门外)
//  ★ v1.9.0 补丁(实机上抓出来的):**开机预热期**。
//    起因:烧完一上电,`off` 稳定在 3 m 不动,统计量却逐拍 +1.25 线性涨,
//    2.8 -> 3.9 -> ... -> 13.0,眼看就要撞 20 —— 设备一动不动,却在"累积漂移量"。
//    根因:μ 的初值取"进入本段时那一拍的 off"。那一拍并不可靠 —— 基准刚从 NVS
//    恢复、首解刚出、off 还在 0 附近,μ 于是从 0 起步;而 μ 的 α 只有 0.003
//    (τ≈5.5 min),要爬好几分钟才追上真实的 3 m。这段爬升期里 dev 恒为正,
//    减去 k 后仍有 1~2 m 的余量,逐拍累积 ⇒ "一开机就在漂"。
//    这与上面那条"恒定偏差不是漂移"是**同一个错的两个入口**:上次是拿 0 当基线,
//    这次是拿"还没稳下来的第一个样本"当基线。
//    治法:进入本段后的前 90 s 只**建基线**、不**做判决** —— μ 用快 α(τ≈6.5 s)
//    快速收敛到真实工作点,统计量锁 0。90 s 后切回慢 α,才开始积累。
//    (现场本来也要等基准稳下来,这 90 s 不占用任何判据窗口。)
constexpr uint32_t kCusumWarmMs = 90000;  // 预热 90 s:只建 μ,不判决
constexpr float    kCusumWarmA  = 0.15f;  // 预热期 μ 的 α(1 Hz ⇒ τ ≈ 6.5 s)

// ---- ⑥ 多径:CN0–仰角模板残差 ----
//  正常天空里 CN0 随仰角**单调上升**(穿过的大气少、路径损耗小)。低仰角星却
//  比模板**明显更强**,说明除了直射还叠了一路反射 —— 那就是多径的指纹。
//
//  ★ 阈值为什么是 10 而不是拍脑袋的 6(2026-10-17 实测定):
//    6 dB 在这个点位(3 楼窗边)**长期误报** —— 现场实测低仰角残差常态就在
//    7.1~9.3 dB 之间,屏上会一直挂着"MP n1 max +8dB",等于把"永远有多径"当默认。
//    这种"常年亮着"的告警比不报警更糟:看的人很快就学会无视它。
//    ⇒ 取 10 dB 只报**强**多径(一倍功率以上的反射),宁可漏报不误报。
//    真要更灵敏,把 kMpThrDb 降到 7 即可(会开始报常态),现场一眼能看出区别。
constexpr float kMpThrDb    = 10.0f;   // 残差高于模板多少 dB 算可疑
constexpr float kMpMaxElDeg = 45.0f;   // 只在这个仰角以下判(高仰角本来就强)
constexpr float kMpForget   = 0.999f;  // RLS 遗忘因子(≈ 最近 1000 个样本)

// ============================================================================
//  ⑦ 可调阈值 —— 屏上「PARAMS」页 / 串口 p 的**运行时真源**(★ v1.10.0 新增)
// ----------------------------------------------------------------------------
//  为什么要这一层(用户 2026-10-17 原话):
//    "城市范围内很多地方有高楼、金属物,会产生多径效应,有时候判断依据不太好……
//     我们需要有一个入口去设置一些参与运算的参数。"
//  这个需求是**真的**:同一个固件放在 3 楼窗边和放在街边峡谷,合理的门限不一样;
//  写死 30 m 在一种场景下太松、在另一种下太紧。
//
//  ★ 分界线必须划清:**判据阈值可调,模型系数锁死。**
//    可调 = "我认什么算异常"(AL / POS LIM / 门限 / 迟滞)—— 这是**配置项**,
//           航空界本来就要求现场可配(LPV-200 = 35 m / APV-I = 40 m / 海事 556 m)。
//    锁死 = "我怎么算出这个异常"(kHplK = 5.33、σ(C/N0) 的 0.6 m@45 dB-Hz、
//           CUSUM 的 α/μA/k/H、多径模板的 RLS 遗忘因子)—— 这是**设备标定项**。
//    为什么后者不能开放:`tools/cusum_replay.py` 是把这些系数**逐值复制**过去做的
//    离线回放,结论是"0.02~0.4 m/s 斜坡下 8 m 绝对闸先到、CUSUM 再确认 0 s"。
//    一旦系数能在屏上改,那份回放证据就和手上这台机器对不上了 ——
//    答辩时说"我们验证过它不会误报"当场失去支撑。
//
//  ★ 存储:NVS 命名空间 **gnstune**,与校准(gnstouch)/ 基准(gnsbase)/ WiFi 台账
//    完全隔离。只烧 app(0x10000)不会丢;屏上 DEFAULTS 一键回出厂。
//  ★ 生效:改完立即生效,不重启。唯一例外是 HDOP MAX / SAT MIN ——
//    它们只在"按 BASE 设基准"那一刻被读,对已存好的基准没有追溯效力(也不该有)。
// ============================================================================
namespace tune {

// ---- 出厂默认值(★ 唯一真源:恢复出厂、串口 p 的 "def" 列都读这里)----
//  每个都注明它的"前身"是哪个 constexpr —— 这一轮是把常量搬到运行时,
//  不是重新定标,所以默认值一个都没改(用户 2026-10-17:"默认值先不变")。
constexpr float kDefPosLim  = TH_MAX_OFFSET_M;                 // 30 m
constexpr float kDefAlert   = 30.0f;                            // 原 kAlertM
constexpr float kDefHdop    = 2.0f;                             // 原 kBaseMaxHdop
constexpr float kDefSat     = 6.0f;                             // 原 kBaseMinSats
constexpr float kDefCn0     = TH_MIN_CN0_MEDIAN;                // 25 dB-Hz
constexpr float kDefMpThr   = kMpThrDb;                         // 10 dB
constexpr float kDefHold    = (float)(TH_HOLD_MS / 1000u);      // 5 s
constexpr float kDefRecover = (float)(TH_RECOVER_MS / 1000u);   // 8 s

struct Item {
  const char* key;      // NVS 键(Preferences 上限 15 字符)
  const char* name;     // 屏上名(FONT2,最长 9 字符)
  const char* unit;     // 单位后缀(空串 = 无单位)
  float  def, lo, hi, step;
  uint8_t dec;          // 显示小数位:0 = 整数,1 = 0.1
  float* p;             // 运行时值
  const char* note;     // 编辑页底部说明(FONT1 6px,≤30 字符 ≈180px)
};

// ---- 运行时值:全部先落出厂默认,load() 再用 NVS 覆盖 ----
static float vPos   = kDefPosLim,
             vAlert = kDefAlert,
             vHdop  = kDefHdop,
             vSat   = kDefSat,
             vCn0   = kDefCn0,
             vMp    = kDefMpThr,
             vHold  = kDefHold,
             vRec   = kDefRecover;

// 顺序 = 屏上列表顺序。**判据类在前、显示类在后**,因为前六个会改状态词。
static Item items[] = {
  { "poslim",  "POS LIM",  "m",     kDefPosLim,  10.0f, 100.0f, 5.0f, 0, &vPos,
    "offset over this -> POS-BAD" },
  { "al",      "HPL LIM",  "m",     kDefAlert,   10.0f, 100.0f, 5.0f, 0, &vAlert,
    "protection level limit (AL)" },
  { "hdopmax", "HDOP MAX", "",      kDefHdop,     1.0f,   5.0f, 0.1f, 1, &vHdop,
    "gate when saving a base" },
  { "satmin",  "SAT MIN",  "",      kDefSat,      4.0f,  12.0f, 1.0f, 0, &vSat,
    "gate when saving a base" },
  { "cn0min",  "CN0 MIN",  "dB-Hz", kDefCn0,     15.0f,  40.0f, 1.0f, 0, &vCn0,
    "median under this -> DEGRADED" },
  { "mpthr",   "MP THR",   "dB",    kDefMpThr,    4.0f,  20.0f, 1.0f, 0, &vMp,
    "multipath residual (readout only)" },
  { "hold",    "HOLD",     "s",     kDefHold,     1.0f,  30.0f, 1.0f, 0, &vHold,
    "alarm needs bad this long" },
  { "recover", "RECOVER",  "s",     kDefRecover,  2.0f,  60.0f, 1.0f, 0, &vRec,
    "clear needs good this long" },
};
constexpr int kN = (int)(sizeof(items) / sizeof(items[0]));
static_assert(kN == 8, "参数表项数必须等于 PARAMS 页行数 8(见 tune::TP_ROWS)");

// ---- 访问器:所有判据**只从这里**取值 ----
//  加 inline:这几个每圈都调,不值得走函数调用。
inline float    posLim()    { return vPos; }
inline float    alertM()    { return vAlert; }
inline float    hdopMax()   { return vHdop; }
inline int      satMin()    { return (int)(vSat + 0.5f); }
inline float    cn0Min()    { return vCn0; }
inline float    mpThrDb()   { return vMp; }
inline uint32_t holdMs()    { return (uint32_t)(vHold    + 0.5f) * 1000u; }
inline uint32_t recoverMs() { return (uint32_t)(vRec     + 0.5f) * 1000u; }

inline void clampAll() {
  for (int i = 0; i < kN; ++i) {
    if (*items[i].p < items[i].lo) *items[i].p = items[i].lo;
    if (*items[i].p > items[i].hi) *items[i].p = items[i].hi;
  }
}

inline int changed() {          // 与出厂默认不同的项数(串口/状态行标记用)
  int n = 0;
  for (int i = 0; i < kN; ++i)
    if (fabsf(*items[i].p - items[i].def) > 1e-3f) ++n;
  return n;
}

// ---- NVS ----
static Preferences prefs;
constexpr const char* kNvsName = "gnstune";

inline void save() {
  prefs.begin(kNvsName, false);
  for (int i = 0; i < kN; ++i) prefs.putFloat(items[i].key, *items[i].p);
  prefs.end();
}

inline void load() {
  prefs.begin(kNvsName, true);
  for (int i = 0; i < kN; ++i) *items[i].p = prefs.getFloat(items[i].key, items[i].def);
  prefs.end();
  clampAll();                   // 换过固件、改过范围也不会读出非法值
  Serial.printf("[bench] tune: %d/%d non-default (nvs gnstune)\n", changed(), kN);
}

inline void resetAll() {
  for (int i = 0; i < kN; ++i) *items[i].p = items[i].def;
  save();
}

inline void fmtVal(char* out, size_t n, int i) {
  const Item& it = items[i];
  char num[12];
  snprintf(num, sizeof(num), "%.*f", (int)it.dec, *it.p);
  if (it.unit[0]) snprintf(out, n, "%s %s", num, it.unit);
  else            snprintf(out, n, "%s", num);
}

// dir = +1 / -1;mult = 1 或 5(屏上四个键就是 ±1×step / ±5×step)
inline void bump(int i, int dir, int mult) {
  Item& it = items[i];
  *it.p += (float)dir * (float)mult * it.step;
  if (*it.p < it.lo) *it.p = it.lo;
  if (*it.p > it.hi) *it.p = it.hi;
}

}  // namespace tune


// ---------------- 屏参数(★★ v1.7.0 主界面重排:三段式) ----------------
// 版式(240x320 竖屏):
//   y   0..239   星空图区 240x240 —— 圆盘 + 外圈状态光环 + 四角 HUD 块
//   y 240..273   按键条   240x34  —— BASE / CLR-BS / CHARTS / LOG,每键 60x34
//   y 274..319   数据条   240x46  —— 左 状态词(FONT4) + 右 HPL/CN0/SAT 三行
//  ★ v1.9.0(用户 2026-10-17 定):"上面四个按键的高度再缩小一点,多留点空间给数据条"。
//    按键 40 -> 34(省 6),数据条 40 -> 46(得 6),两者之和仍是 80 ——
//    **底部 sprite 尺寸、报文窗 8 行、BOT_Y 全都不变**,所以这轮只动两个数。
//    按键文字是 FONT2(16px),在 34px 格里内缩 2px = 30px 可用,16px 字居中后
//    上下各留 7px;长按进度条从 BTN_H-9 挪到 BTN_H-7(=27),不再贴字底。
//    数据条从 40 涨到 46,右侧才放得下**三行** 8px(HPL / CN0 / SAT)。
// 原来那套「顶部 100px 状态色带 + 四周 3px 彩框 + 报文窗 + 指标区」**整体作废**:
// 状态色改由**星空图外圈 2px 光环**唯一承载(见 statusColor565),
// 原先散在指标区的信息(时间/坐标/温湿度/漂移/HDOP)全部归位到四角块与数据条。
// 报文窗不再常驻 —— 长按数据条 800ms 切出来(见 touch::poll)。
constexpr int SCR_W    = 240;
constexpr int SCR_H    = 320;

constexpr int SKY_H    = 240;                          // 星空图区高
constexpr int BTN_Y    = 240, BTN_H = 34, BTN_W = 60;  // 按键条(4 键)
constexpr int DATA_Y   = 274, DATA_H = 46;             // 数据条
static_assert(BTN_Y + BTN_H + DATA_H == SCR_H, "按键条+数据条必须正好铺满 240..319");

// ---- 星空图几何:圆心在 240x240 区正中 ----
constexpr int SKY_CX      = SCR_W / 2;    // 120
constexpr int SKY_CY      = SKY_H / 2;    // 120
// ★ v1.7.1(用户 2026-10-16 拍板"星空图再大一圈 + 外圈色环也大 3px"):圆盘 80 -> 87、
//   环 83/84 -> 90/91 = 各放大 7px。上限是**算术结果、不是感觉**:
//   块高缩到 24 后四角块最内角 = (HUD_M+HUD_W, HUD_M+HUD_H) = (94, 26),
//   到圆心 (120,120) 距离 = sqrt(26²+94²) = 97.53 ⇒ 环外沿最多 97(圆盘 93 / +13)。
//   这里取 +7 —— 与旧版(+5 时余量 5.85px)留同样宽的 6.5px 余量,想用满就改这三个数。
constexpr int R_STAR      = 87;           // 圆盘半径(圆占 x/y 33..207,直径 174)
constexpr int R_RING_IN   = 90;           // 状态光环内沿
constexpr int R_RING_OUT  = 91;           // 状态光环外沿 -> 2px 实心环
// 环与圆盘之间(88..89 这两像素)留**页底色** -> 光环"悬浮"在圆外,不与星点混淆

// ---- 四角 HUD 块 92x24,边距 2 ----
// ★ v1.7.1:"四角字体小一号" —— 内置字模只有 6x8(字体1) / 16px(FONT2) / 26px(FONT4)
//   三档(setTextSize 只吃整数倍),16px 的下一档就是 6x8 ⇒ 字模 FONT2 -> 字体1,
//   行距 16 -> 9、块高 32 -> 24(不缩块的话两行 8px 字在 32px 里上下各空 8px,很散)。
//   块高一缩,星空图上限从 +5px 抬到 +13px,所以同时做了上面那轮放大。
// 块宽 92 的依据(v1.7.0 立;换字模后余量更宽):"2026-10-16" 用字体1(6px/字符)= 60px,
//   加左右各 4px 内边距 = 68px ⇒ 92 富余 24px,**右上块右端那 22px 正好让给 WiFi 图标**。
constexpr int HUD_W = 92, HUD_H = 24, HUD_M = 2;
constexpr int HUD_LH = 9;                  // 块内两行 8px 字的行距(上下各留 ~3.5px)
static_assert(HUD_H >= HUD_LH + 8 + 2, "四角块高装不下两行 8px 字 —— 调大 HUD_H 或调小 HUD_LH");
// 四块的左上角坐标(星空区内,1..239 / 2..205 之间)
constexpr int HUD_XL = HUD_M;                      // 左列 x = 2
constexpr int HUD_XR = SCR_W - HUD_M - HUD_W;      // 右列 x = 146
constexpr int HUD_YT = HUD_M;                      // 上行 y = 2
constexpr int HUD_YB = SKY_H - HUD_M - HUD_H;      // 下行 y = 206

// ★ 编译期几何自检(改 R_* / HUD_* 任一项都必须重过这三条)
// ① 光圈外沿不许碰四角块最内角:块内角 = (HUD_M+HUD_W, HUD_M+HUD_H) = (94,26),
//    到圆心 (120,120) 距离² = 26² + 94² = 9512 > R_RING_OUT² = 8281,余量 6.5px。
static_assert((SKY_CX - (HUD_M + HUD_W)) * (SKY_CX - (HUD_M + HUD_W)) +
              (SKY_CY - (HUD_M + HUD_H)) * (SKY_CY - (HUD_M + HUD_H)) >
              R_RING_OUT * R_RING_OUT,
              "星空图光圈会压到四角 HUD 块 —— 调小 R_* 或调小 HUD_*");
// ② 圆盘与光环之间至少留 3px(含 2px 页底缝)
static_assert(R_RING_IN - R_STAR >= 3, "圆盘与状态光环之间要留 2px 页底缝");
// ③ 按键条必须四等分正好铺满
static_assert(BTN_W * 4 == SCR_W, "按键条必须四等分铺满 240px");

// ---- 报文窗(★ v1.7.0:长按数据条 800ms 切出/收回)—— 占 y 240..319 整块 240x80 ----
constexpr int MSG_ROWS  = 8;      // 8 行 x 10px = 80
constexpr int MSG_LH    = 10;     // 报文行高
constexpr int MSG_CHARS = 38;     // 字体 1 每字符 6px -> 240px 内放 38 字符(228px)

// ---- BACK 按钮几何(第二/三/四界面共用同一组;touch::poll 的判定也按它比对) ----
constexpr int BACK_W = 64, BACK_H = 24, BACK_Y = 2;
constexpr int BACK_X = SCR_W - 6 - BACK_W;   // 170(右对齐,右边距 6)

// ---- WiFi 信息卡(★ v1.7.2 新增)---------------------------------------------
// 为什么要有它:S3 自己的 IP **屏上一直没有落脚点** —— 它只作为回执发给 C3
//   (`@s ok ip 192.168.4.40`),想在地面看一眼得凑到 C3 的输出卡上。
//   而主界面三段式已经排满:四角块只有 92px 宽、数据条右侧只剩 56px 领地,
//   11 字符的 IP(66px)塞不进任何一个固定位置。
//   ⇒ 做成**按需弹卡**:点右上角那颗 WiFi 图标 -> 弹 4 秒 -> 自动收。
//   零侵入:不去点它,屏上一个像素都不变(所以不破坏已定稿的版式)。
// 落点:CARD_Y = 90 在星空图区内、圆盘正中偏上 —— 不压上排四角块(y 2..25),
//   也不压下排(y 206..229);圆盘自身的星点被临时盖住 4 秒,可接受。
constexpr int      CARD_W = 176, CARD_H = 60;
constexpr int      CARD_X = (SCR_W - CARD_W) / 2;   // 32 水平居中
constexpr int      CARD_Y = 90;
constexpr uint32_t kCardMs = 4000;                  // 显示时长(再点一下可提前收)
constexpr int      CARD_TX = 10;                    // 卡内左边距
constexpr int      CARD_TY = 10;                    // 首行距卡顶
constexpr int      CARD_LH = 16;                    // 卡内行距(8px 字 + 8px 空)
// 三行:10 / 26 / 42 -> 末行底 50,卡高 60,上下各留 10px
static_assert(CARD_TY + CARD_LH * 2 + 8 <= CARD_H - 10, "WiFi 卡三行装不下 —— 调 CARD_H 或 CARD_LH");
static_assert(CARD_X >= 4 && CARD_X + CARD_W <= SCR_W - 4, "WiFi 卡会贴到屏边 —— 调 CARD_W");
// 卡内文字用**字体1(6px/字符)**。值的列起点 = CARD_X + CARD_TX + 36(固定 6 字符宽的标签列),
// 右边再留 CARD_TX ⇒ 值那一列最多 (CARD_W - CARD_TX*2 - 36)/6 = 20 字符 —— 够放 IPv4(15)。
static_assert((CARD_W - CARD_TX * 2 - 36) / 6 >= 20, "WiFi 卡的值列放不下 20 字符 —— 调 CARD_W 或 CARD_TX");

// ---------------- 报文窗画法(2026-10-01 定型;★ v1.7.0 尺寸改 240x80) ----------------
// **内存合成(sprite)是唯一画法,已定型,不再提供切换。**
//
// 为什么必须是它 —— 老的"直接画"每次重绘是一段几十毫秒的"亮度脉冲":
//    ① fillRect 整块擦底      ② 逐行 drawString,字一行行长回来
//   节拍踩在人眼对大面积闪烁最敏感的那一档,所以看着特别刺眼。
//   内存合成后,那块区域在 pushSprite 之前**一个像素都不动**,
//   上屏变成一次连续的整块替换,亮度全程基本恒定 ⇒ 闪感消失。
//   (代价:一次性占 38,400 字节 RAM,即 240x80x2。S3 完全够。)
//
// 2026-10-02 把 "串口敲 w 切换画法" 那条临时通道撤了 —— 屏的效果已确认满意,
//   留着一个能改画法的命令只是演示现场的风险(误敲一下画面就变回闪的)。
//   现在老画法只作为**兜底**保留:万一 sprite 申请失败,自动退回它,
//   宁可稍微闪一点,也绝不能黑屏。
//
// 报文窗刷新节拍(ms)。400ms = 2.5Hz:屏上照样看得出"在走",
// 但闪烁频率离开敏感区,SPI 占用也不高。
constexpr uint32_t MSG_REFRESH_MS = 400;

// ---------------- 屏配色:深色主题(★★ v1.7.0 定稿) ----------------
// 根因与改法:原来 CLR_PAPER = TFT_WHITE,整屏铺白,刺眼。现在整屏深色,
// 状态色由**星空图外圈 2px 光环**唯一承载 —— 屏上不再有任何整块颜色。
// ★ 等号右边是屏真正要的 RGB565(uint16_t);注释给回 #RRGGBB 设计值方便核对。
//   换算规则 R565=round(R*31/255) / G565=round(G*63/255) / B565=round(B*31/255),
//   **别用 >>3 截断**(深色值截断会整体偏暗一档)。
// 改配色只动下面这一组常量 —— 别处一律不要再写死颜色。
constexpr uint16_t CLR_PAPER = 0x0882;   // #0C0F13 页底(原 TFT_WHITE)
                                         // ★ v1.7.1 核对过:C3 的 CLR_BG 也是 0x0882,一字不差
// ★★ v1.7.1(用户 2026-10-16 拍板"参照 C3 配色,每个显示块底色有点灰"):块底**三个名字合一**。
//   旧的 C_BLOCK(0x2125) / C_BLOCK(0x0862) / C_BLOCK(0x2146) 整段删除 ——
//   全屏每一个"块"(图卡 / 星图圆盘 / 四角 HUD / 按键 / 三页的顶栏底栏)同色,
//   块与块之间只靠**页底细缝**分隔(照 C3 的 `fillRoundRect(x,y,SCR_W-8,h,4,CLR_BAR)` 手法),
//   块本身**一律不描边**(C3 的规律:边框只留给危险态,如告警弹窗与删除键)。
constexpr uint16_t C_BLOCK   = 0x18E4;   // #1A2027 块底(= C3 的 CLR_BAR,同一值)
constexpr uint16_t C_DATA    = 0x07FF;   // #00FFFF 数据青(= C3 屏上 [S3] 消息行色)
                                         // 用途:四角 HUD 八行 + 数据条 CN0/SAT
constexpr uint16_t CLR_RULE  = 0x3A09;   // #39414A 参考线 / 分隔线(★ v1.7.1 起不再描块的边)
constexpr uint16_t CLR_INK   = 0xE75D;   // #E6EBF0 主文字(原 TFT_BLACK)
constexpr uint16_t CLR_INK2  = 0x9515;   // #98A3AD 次级文字(原 0x4208)
constexpr uint16_t C_GPS     = 0x3FE2;   // #39FF14 GPS 荧光绿(原 #185FA5 蓝)
constexpr uint16_t C_BDS     = 0x069F;   // #00D4FF 北斗青蓝(原 #BA7517 橙)
// 状态色(星空图光圈 + 数据条状态词 + 板载 WS2812 灯,三处共用 statusColor565)
constexpr uint16_t C_ST_OK   = 0x3EAF;   // #37D67A 绿  NORMAL
constexpr uint16_t C_ST_DEG  = 0xEDE7;   // #F2C037 黄  DEGRADED
constexpr uint16_t C_ST_SIG  = 0xEBC5;   // #F2792B 橙  SIG-BAD
constexpr uint16_t C_ST_POS  = 0xFA69;   // #FF4D4D 红  POS-BAD / LOST(闪相=页底色)
constexpr uint16_t C_ST_GREY = 0x7C32;   // #7A8794 灰  SEARCH / NO-DATA
// ★ v1.9.0 新增:HPL-HIGH 用**紫**,是刻意避开已有五色的语义区 ——
//   绿=正常 / 黄=信号弱 / 橙=信号坏 / 红=位置坏 / 灰=还没数据。
//   "保证不足"不属于上面任何一类:它是"我不敢下结论",所以给一个**不在色阶上**的颜色。
//   #A78BFA -> 565:(167>>3)<<11 | (139>>2)<<5 | (250>>3) = 0xA45F
constexpr uint16_t C_ST_HPL  = 0xA45F;   // #A78BFA 紫  HPL-HIGH(完好性不足)
// ★ v1.10.1 新增:慢漂移用**琥珀** #FFB000 -> 565:(255>>3)<<11 | (176>>2)<<5 | 0 = 0xFD80
//   为什么不用"橙"(SIG-BAD)也不用"红"(POS-BAD):DRIFT 的证据强度只到"趋势",
//   没到"当场就偏了 30 m"。给红等于把推测说成事实 —— 2026-10-17 那次困惑就是这么来的。
//   用琥珀而不复用黄(DEGRADED):黄说的是"信号弱到影响精度",与"坐标在漂"不是一回事;
//   两者在事件列表里会并排出现,颜色分开才看得出"这段时间发生过两类事"。
constexpr uint16_t C_ST_DRIFT = 0xFD80;  // #FFB000 琥珀 DRIFT(慢漂移)
// 图表页辅助色(第二界面与主界面共用同一套,不再各定义一份)
constexpr uint16_t C_BG   = CLR_PAPER;   // 页面底色
constexpr uint16_t C_WARN = 0xF4B2;      // #F09595 阈值线
constexpr uint16_t C_DIM  = 0x5AEB;      // #5F5E5A 淡灰:历史点 / 没信号的星
constexpr uint16_t C_NAV  = TFT_RED;     // 亮红:星空图方位标注 N/S/W/E(深底上仍醒目)
constexpr uint16_t C_SATS = TFT_GREEN;   // 亮绿:星数角标(已随 n sats 取消,留作备用)

// ---- 原「指标区四格数值色 + 格位」(v1.5.5)已随 v1.7.0 主界面重排整体作废 ----
// 那四格(USED/VIEW、HDOP、CN0、OFFSET)连同 CLR_M_* 四色、MET_X_* 四个格位一并删除:
// 数值改由**数据条右侧**(CN0 / SAT)与**右下角 HUD 块**(DRIFT / HDOP)承载;
// 配色统一走上面的深色主题常量,不再为四格单独定义颜色。

// ============================================================================
//  第 1 段:GNSS 串口接收
//  只负责把 NMEA 行收下来存进环形缓冲,不解析、不判断。
// ============================================================================
namespace gnss {

static HardwareSerial port(1);
static char           lineBuf[NMEA_LINE_MAX];
static int            lineLen    = 0;
static char           pool[NMEA_POOL][NMEA_LINE_MAX];
static int            headIdx    = 0;
static int            stored     = 0;
static uint32_t       totalLines = 0;
static uint32_t       byteCount  = 0;

// ★ v1.7.0(用户 2026-10-16 20:47 拍板):**模拟信源整段删除**。
//   这里原来有个 `recvEnabled` 开关 —— 模拟信源(1/2/3 档)接管时置 false,
//   让真实 UART 只排空、不入池,好让"开模拟 = 纯模拟"、"结果与场地信号无关"。
//   模拟段走了,这个开关失去唯一的使用者 ⇒ **变量与那条排空分支一起删掉**,
//   报文池从此只有真实模块一个来源(这也正是它本来的语义)。
//   历史坑(留档):2026-10-07 实测发现 sim::commit() 与真报文写**同一个池子**,
//   按到达顺序谁后到谁赢 ⇒ 开着模拟时真模块的强信号会实时盖掉模拟,
//   "场景2 信号差"9 秒内星数仍是真实的 10~12、完全不转红 ⇒ 演示结果不可复现。
//   当年靠 recvEnabled 解决;现在靠"删掉模拟段"从根上不再有这个问题。

inline void begin() {
  port.begin(GNSS_BAUD, SERIAL_8N1, PIN_GNSS_RX, PIN_GNSS_TX);
}

inline void poll() {
  // ★ v1.7.0:原来这里有一条"模拟信源接管 ⇒ 只排空不入池"的分支(配 recvEnabled),
  //   已随模拟段一起删除 —— 见上方 begin() 之前的注释。

  // ★ 2026-10-01 晚改:while 加圈数上限(原来是 while (port.available()) 裸循环)。
  //   9600 波特每秒才来 960 字节,512 这个上限正常永远碰不到;
  //   加它只为兜一个底:**万一** UART 驱动报出一个假的巨大"可用字节数",
  //   主循环也不会被永远锁在这个循环里(那才是真正的死机)。
  for (int guard = 0; guard < 512 && port.available(); ++guard) {
    char c = (char)port.read();
    byteCount++;

    if (c == '\n') {
      if (lineLen > 0) {
        if (lineBuf[lineLen - 1] == '\r') lineLen--;
        lineBuf[lineLen] = '\0';
        if (lineLen >= 6 && lineBuf[0] == '$') {
          memcpy(pool[headIdx], lineBuf, lineLen + 1);
          headIdx = (headIdx + 1) % NMEA_POOL;
          if (stored < NMEA_POOL) stored++;
          totalLines++;
        }
        lineLen = 0;
      }
    } else if (lineLen < NMEA_LINE_MAX - 1) {
      lineBuf[lineLen++] = c;
    } else {
      lineLen = 0;                 // 超长行丢弃,重新对齐
    }
  }
}

// back = 0 取最新一行;取不到返回 nullptr
inline const char* recent(int back) {
  if (back < 0 || back >= stored) return nullptr;
  int idx = (headIdx - 1 - back + NMEA_POOL * 2) % NMEA_POOL;
  return pool[idx];
}

}  // namespace gnss

// ============================================================================
//  第 1.2 段:PPS 秒脉冲捕获(模块 PPS --> GPIO18,上升沿中断)
//  PPS = Pulse Per Second:模块定位后,每秒在 PPS 脚上打一个上升沿,该沿的
//  时刻就是"整秒"的硬件基准(精度是 GPS 级的,比板载晶振好几个数量级)。
//
//  ★ 先把一个常见误解讲清:1Hz 的 PPS **不能"替代"40MHz 晶振**。
//    晶振要每秒输出 4000 万个周期,PPS 每秒只给 1 个沿,数量差 4000 万倍,
//    任何电路也变不出那些不存在的脉冲。PPS 能做的是**每秒对一次表**:
//    用它的沿去量"本地时钟走快了还是慢了",再反过来修正本地计时 —— 这才是
//    可行的驯服(tame/discipline)方案,本次先把"量"的部分做出来。
//
//  本段只做**观测**,不动系统时间:
//    · 数脉冲 —— 每秒 +1 就说明 PPS 线接通了(模块未定位时多不出脉冲,属正常)
//    · 量间隔 —— 相邻两沿的间隔理论值 1000000us;与它的差就是本地时钟的
//                相对误差,单位 us/s 即 ppm(1us/s = 1ppm = 86400us/天)
//    · 积分   —— 单拍只有 1us 分辨率 = ±1ppm 量化噪声,读不出真值。
//                把自开机起的每一拍累加再平均,误差摊薄到 ±1/N ppm:
//                3600 拍(1 小时)= ±0.0003ppm,那时才是真值。(2026-10-01 加)
//  观测数据:串口发 'g' 打印两行(瞬时 + 积分);开日志('v')时每 5 秒附带积分行。
// ============================================================================
namespace pps {

static volatile uint32_t edgeCount  = 0;   // 累计上升沿(中断里递增)
static volatile uint32_t lastEdgeUs = 0;   // 最近一次上升沿的本地时刻
static volatile uint32_t lastGapUs  = 0;   // 最近一次相邻间隔(理论 1000000us)
static volatile uint32_t minGapUs   = 0;   // 本次开机见过的最小间隔(0=还没量到)
static volatile uint32_t maxGapUs   = 0;   // 最大间隔(看抖动)

// ---- 积分累加器(2026-10-01 加,方案 A)--------------------------------------
//  为什么必须积分:micros() 分辨率 1us,单次间隔的量化误差就是 **±1ppm**。
//  实测报出来的 "+1.00 ppm" 恰好是个 1us 的整数跳,落在噪声里,不能当结论。
//  把 N 拍累加再平均,误差摊薄到 ±1/N ppm —— 3600 拍(1 小时)就到 ±0.0003ppm,
//  那时读出来的数才是 ESP32 时基相对 GNSS 的**真值**。
//  integSpanUs 必须是 64 位:micros() 自己 71.6 分钟就回绕一次,32 位累加器撑不住。
//  读这些量一律在 noInterrupts() 里做(64 位读不是原子的)。
static volatile bool     integOn      = false;  // 是否已建立积分起点
static volatile uint64_t integSpanUs  = 0;      // 自起点累计的本地 us
static volatile uint32_t integSecs    = 0;      // 累计的"GNSS 秒数"(丢沿时按整数秒补足)
static volatile uint32_t integSkipped = 0;      // 整段作废的次数(间隔 >4s,判为断线/失锁)

// 中断服务:越短越好 —— 只记时刻、算间隔,不做任何打印/浮点。
// 注意不要加 IRAM_ATTR:它把函数搬到 IRAM,但函数里引用的这几个 static 变量在
// DRAM,l32r 取址的字面量会落到 .literal 段后面 —— 链接期直接报
// "dangerous relocation: l32r: literal placed after use",整个工程编译不过。
// 保持默认(函数在 Flash)即可;本项目已无运行期 NVS 写入(基准改会话制),
// 不存在"写 Flash 时中断被屏蔽"的场景。
inline void onEdge() {
  uint32_t now = micros();
  if (lastEdgeUs) {
    uint32_t gap = now - lastEdgeUs;     // 无符号减法,天然处理 micros() 回绕
    if (gap > 1000) {                    // 滤掉抖动产生的伪沿(<1ms 不可能是一个新秒)
      lastGapUs = gap;
      if (minGapUs == 0 || gap < minGapUs) minGapUs = gap;
      if (gap > maxGapUs) maxGapUs = gap;

      // ---- 积分累加 ----
      // 先把 gap 折算成"最近的整数秒":正常是 1;偶发丢 1~3 个沿时会读到 2~4。
      // 按 2~4 计入分母是**有意的** —— 否则丢沿会被误当成"本地表走慢了",把平均值拉偏。
      uint32_t secs = (gap + 500000UL) / 1000000UL;
      if (secs >= 1 && secs <= 4) {
        if (!integOn) { integOn = true; integSpanUs = 0; integSecs = 0; }
        // ★ 2026-10-15:`+=` 一并展开成 `= ... + ...`。C++20 起 `volatile` 上的**所有**复合赋值
        //   (含 += -= ++ --)都被弃用;这两行 GCC 当前还没报,但没理由留个半新半旧的样子。
        //   这两句都在 ISR 里、变量也只有本 ISR 写,展开后是同一个读-改-写,行为不变。
        integSpanUs = integSpanUs + gap;
        integSecs   = integSecs   + secs;
      } else {
        // ★ 2026-10-15:`++` 改写成 `= ... + 1` —— C++20 起在 `volatile` 上自增已被弃用
        //   (GCC 报 -Wvolatile)。语义完全一样,只是写法不再踩新标准。
        integSkipped = integSkipped + 1;   // >4 秒 = 断线/失锁,整段作废,不污染平均值
      }
    }
  }
  lastEdgeUs = now;
  edgeCount = edgeCount + 1;               // ★ 同上:volatile 上不用 `++`
}

inline void begin() {
  pinMode(PIN_GNSS_PPS, INPUT);          // 模块 PPS 是推挽输出,不用内部上拉
  attachInterrupt(digitalPinToInterrupt(PIN_GNSS_PPS), onEdge, RISING);
}

inline uint32_t pulses() { return edgeCount; }

// 积分结果 —— **这才是能当结论的 ppm**。
// 单次间隔只有 1us 分辨率(±1ppm 量化噪声),把自开机以来所有拍累加起来平均,
// 误差摊薄到 ±1/N ppm。跑满 1 小时 = 3600 拍 → ±0.0003ppm,足够看清真实值。
inline void reportIntegral() {
  uint64_t span;
  uint32_t secs, skipped;
  bool     on;
  noInterrupts();
  span = integSpanUs; secs = integSecs;
  skipped = integSkipped; on = integOn;
  interrupts();

  if (!on || secs < 2) {
    Serial.println("[bench] pps 积分: 样本太少 —— 让它继续跑"
                   "(跑满 1 小时 = 3600 拍,分辨率才到 0.0003ppm)");
    return;
  }
  // 平均每"N 个 GNSS 秒"本地走了多少 us。GNSS 侧就是整秒(纳秒级误差,可忽略),
  // 所以这个平均数减 1000000 就是本地时基的真实 ppm。
  double avg = (double)span / (double)secs;
  double ppm = avg - 1000000.0;
  Serial.printf("[bench] pps 积分: %lu 拍 | 本地 %.6f s / GNSS %lu s | 平均 %.4f us | %+.3f ppm | 日漂移 %+.1f ms | 作废 %lu 段\n",
                (unsigned long)secs, (double)span / 1e6, (unsigned long)secs,
                avg, ppm, ppm * 86.4, (unsigned long)skipped);
}

// 打印一行观测结果。cnt=0 说明一个沿都没收到 —— 先查线,再查模块是否已定位。
inline void report() {
  uint32_t cnt, gap, gmin, gmax;
  noInterrupts();
  cnt  = edgeCount; gap = lastGapUs;
  gmin = minGapUs;  gmax = maxGapUs;
  interrupts();

  if (cnt == 0) {
    Serial.println("[bench] pps: 尚无脉冲 —— 检查 PPS 是否接到 GPIO18;"
                   "另注意多数模块未定位时 PPS 不出脉冲(等定位转绿再看)");
    return;
  }
  if (gap == 0) {
    Serial.printf("[bench] pps: 已收到 %lu 个脉冲,等第二个沿才能量间隔…\n",
                  (unsigned long)cnt);
    return;
  }
  // gap 与 1s 之差:正=本地表走慢了(实际间隔被量成更长),负=走快了。
  // 单位 us/s 数值上就等于 ppm。日漂移 = ppm × 86.4 ms。
  // ⚠ 这**只是最近一拍**,量化误差 ±1ppm,别拿它当结论 —— 看下面那行积分。
  double ppm = (double)((int32_t)gap - 1000000);
  Serial.printf("[bench] pps: %lu 脉冲 | 间隔 %luus (%+.2f ppm) | 抖动 min %lu / max %lu us | 日漂移 %.1f ms\n",
                (unsigned long)cnt, (unsigned long)gap, ppm,
                (unsigned long)gmin, (unsigned long)gmax, ppm * 86.4);

  reportIntegral();            // 紧跟一行积分平均 —— 那个才是结论
}

// ★ v1.5.20:C3 命令板要的是**一行短文本**(它输出卡一行 34 列,还要让出 "* " 前缀)。
//   原来 'g' 只把两行中文长句打在 S3 自己的串口上,C3 那边一个字都看不到 ——
//   用户 2026-10-12 23:5x 报的"命令发过去没反馈"就是这一类。纯 ASCII、≤32 字符。
inline void brief(char* b, size_t n) {
  uint32_t cnt, gap;
  noInterrupts();
  cnt = edgeCount; gap = lastGapUs;
  interrupts();
  if (!cnt) { snprintf(b, n, "pps no pulse"); return; }
  if (!gap) { snprintf(b, n, "pps %lu pulses", (unsigned long)cnt); return; }
  const double ppm = (double)((int32_t)gap - 1000000);
  snprintf(b, n, "pps %lu gap %luus %+.1fppm",
           (unsigned long)cnt, (unsigned long)gap, ppm);
}

}  // namespace pps

// ============================================================================
//  第 1.5 段:模拟信源 —— ★ v1.7.0(2026-10-16 用户拍板)**整段已删除**。
//
//  原来是"手头没 GNSS 模块时也能跑全状态"的演示用信源:把带合法校验和的 NMEA 行
//  按与真模块完全相同的路径写进 gnss 报文池,解析/判定/显示/灯全链路不区分真假。
//  串口 1/2/3 选正常/信号差/漂移,0 关闭;屏上色带右上角有 "SIM" 角标,
//  串口报表行首有 [SIM] 前缀,`@MIR` 第 9 字段也报模拟态。
//
//  删它的理由:项目未进复赛、转自用,"造异常演示"的需求消失(用户原话:
//  "我不想演示了,干脆把演示这部分全部都删掉")。C3 侧同名菜单项也一并删除,
//  S3 白名单里的 "0123" 同步移除。
//
//  ⚠ 删掉之后**两边都没有模拟信源了**:没有接 GNSS 模块时,报文池就是空的,
//    屏上会停在 SEARCH / NO-DATA。要造"位置异常"只能靠真实 GNSS 或外部干扰。
//  ⚠ 连带消失的东西(别再去找):recvEnabled(gnss 段的接收开关)、
//    stSim(drawStatus 的去重键)、屏上 "SIM" 角标、报表 [SIM] 前缀、
//    以及 nmea::st 里那几个只被模拟段写过的字段的"模拟语义"。
// ============================================================================

// ============================================================================
//  第 2 段:NMEA 解析
//  校验和 + 拆字段,只提取演示要用的:星数 / HDOP / C/N0 / 经纬度。
//  GSV 按 talker(GP / BD / GL / GA)分桶累加 —— 北斗和 GPS 是两套独立的
//  句序列,不分桶会互相冲掉,最后只剩一个星座。
// ============================================================================
namespace nmea {

constexpr int kMaxFld       = 32;   // GSV 最坏 4 星 x 4 字段 + 句名 + 校验和 = 22,留余量
constexpr int kMaxCn0       = 40;
constexpr int kMaxTalker    = 4;
constexpr int kCn0PerTalker = 24;
constexpr int kSatPerTalker = 24;   // 每星座最多记几颗星的位置(与 cn0 同容量)

// 单颗卫星的"位置 + 强度"(★ 2026-10-06 新增,给第二界面星空图用)
//   az/el 用 -1 表示"这句没上报" —— GSV 里这两个字段可能是空串,不能拿 0
//   当"在地平线上正北方",那是编出来的位置,星空图上会画出假点。
//   方位角本来就是相对**真北**的(NMEA 定义),所以画星空图不需要磁力计。
struct SatFix {
  uint8_t prn = 0;      // 星号
  int16_t az  = -1;     // 方位角 0..359 度
  int16_t el  = -1;     // 仰角 0..90 度(90 = 天顶)
  uint8_t cn0 = 0;      // 载噪比 dB-Hz;0 = 看得见但没信号
};

struct Talker {
  char  id[3] = {0};
  int   total = 0;
  int   seen  = 0;
  int   view  = 0;       // 该星座本次报的可见星数;全星座求和才是真正的 VIEW
  int   cn0N  = 0;
  float cn0[kCn0PerTalker] = {0};
  int     satN = 0;                  // 本帧已收到几颗星的位置
  SatFix  sat[kSatPerTalker] = {};   // ★ 2026-10-06:每颗星的位置
};

struct State {
  // ---- 来自 GGA ----
  int      quality   = 0;        // 0=无效 1=单点 2=差分 4=RTK
  int      satsUsed  = 0;        // 参与定位的星数
  // 0.0 起步:屏上干净(之前 99.9 是"没数据"哨兵,但判据已有 !fixValid 兜底,
  // 哨兵值纯属显示层噪音;首条 GGA 一到就被真实值覆盖)
  float    hdop      = 0.0f;
  double   lat       = 0;        // 十进制度
  double   lon       = 0;
  bool     fixValid  = false;
  uint32_t fixMs     = 0;

  // ---- 来自 GSV ----
  int      satsView  = 0;        // 可见星数
  float    cn0[kMaxCn0] = {0};   // 已排序(升序),仅供取中位数
  int      cn0N      = 0;
  float    cn0Median = 0;
  uint32_t cn0Ms     = 0;

  // ---- 来自 ZDA(UTC 日期与时间)★ 2026-10-01 新增 ----
  // 这是全项目**唯一**的"现在几点"来源。三个要点先说清:
  //   ① 只取**整秒**:ZDA 的时间字段是 hhmmss.ss,而 .ss 在本模块上恒为 .000
  //      (秒内相位根本不报文里,只能靠 PPS),所以小数部分直接丢。
  //   ② 日期用 ZDA 的 **4 位年**。RMC 也带日期,但只有 2 位年(ddmmyy),要自己补世纪,
  //      不如 ZDA 干净 —— 取年份一律只用 ZDA。
  //   ③ 它是 **UTC**,不是北京时间。时区换算(+8、还有跨日)是显示层的事,
  //      本命名空间不换算,免得"到底存的哪个时区"将来扯不清。
  bool     utcValid = false;   // 收到过至少一条合法 ZDA
  uint32_t utcMs    = 0;       // 最近一次更新时间(ms),用于判"超时无数据"
  int      uY = 0, uMo = 0, uD = 0;
  int      uH = 0, uMi = 0, uS = 0;
  uint32_t zdaCount = 0;       // 诊断:本句收了几条(为 0 说明模块不吐 ZDA)

  // ---- 来自 GSA(★ v1.9.0:PDOP/VDOP + 参与解算的星表)----
  //   GSA 只在"有解"时才有意义;定点解算类型(fix type)3 = 3D,2 = 2D。
  //   ★ 星表**必须是全局并集,不能按 talker 分桶** —— 实测这台模块吐的是
  //     多句 `$GNGSA`(句名一律 GN,一条对应一个星座系统)。按 talker 分桶时
  //     后一句会**覆盖**前一句,最后只剩最后一个星座的星表,其余星全被判成
  //     "没参与解算" ⇒ σ 统计的星数为 0 ⇒ HPL 永远算不出来(屏上恒 `HPL ---`)。
  //     这是 2026-10-17 首次上板实测抓到的,不是推断。
  //     代价:GPS 的 PRN 1..32 与北斗 1..63 **重号**,并集分不清谁是谁。
  //     所以 sigmaUere() 里做两级回退:并集过滤后不足 4 颗就退回"全部可见星"。
  float    pdop      = 0.0f;
  float    vdop      = 0.0f;
  int      gsaFix    = 0;        // GSA 的 fix type(1/2/3)
  uint32_t gsaCount  = 0;        // 诊断:本上电收了几条 GSA(为 0 = 模块不吐 GSA)
  // ★ 曾经想在这里存"参与解算的 PRN 并集"(uint64 位掩码)给 σ 用,
  //   实测发现本模块的 GNGSA 跨星座重号、并集无法归因,已删除 —— 见 sigmaUere()。

  // ---- 计数(排查用)----
  uint32_t sentences = 0;
  uint32_t ggaCount  = 0;
  uint32_t gsvCount  = 0;
  uint32_t badCksum  = 0;
};

static State    st;
static Talker   talkers[kMaxTalker];
static int      talkerN  = 0;
static char*    fld[kMaxFld];
static uint32_t consumed = 0;      // 已解析到第几行

inline void reset() {
  st = State();
  for (int i = 0; i < kMaxTalker; ++i) talkers[i] = Talker();
  talkerN  = 0;
  consumed = 0;
}

static uint8_t hexVal(char c) {
  if (c >= '0' && c <= '9') return (uint8_t)(c - '0');
  if (c >= 'A' && c <= 'F') return (uint8_t)(c - 'A' + 10);
  if (c >= 'a' && c <= 'f') return (uint8_t)(c - 'a' + 10);
  return 255;
}

// NMEA 校验和:* 后两位 = 前 $ 之后到 * 之前所有字符的异或
static bool checksumOk(const char* s) {
  if (s[0] != '$') return false;
  const char* star = strchr(s, '*');
  if (!star || star[1] == '\0' || star[2] == '\0') return false;
  uint8_t x = 0;
  for (const char* p = s + 1; p < star; ++p) x ^= (uint8_t)(*p);
  uint8_t hi = hexVal(star[1]), lo = hexVal(star[2]);
  if (hi == 255 || lo == 255) return false;
  return x == (uint8_t)((hi << 4) | lo);
}

// 就地拆字段,入参是 '$' 之后的位置。fld[0] 是句子名(如 "GNGGA")
static int split(char* s) {
  int n = 0;
  fld[n++] = s;
  for (char* p = s; *p; ++p) {
    if (*p == ',' || *p == '*') {
      char sep = *p;
      *p = '\0';
      if (n < kMaxFld) fld[n++] = p + 1;
      if (sep == '*') break;
    }
  }
  return n;
}

// ddmm.mmmm / dddmm.mmmm -> 十进制度
static double toDeg(const char* v, char hemi) {
  if (!v || !*v) return 0.0;
  double raw = atof(v);
  int    deg = (int)(raw / 100.0);
  double min = raw - deg * 100.0;
  double d   = deg + min / 60.0;
  return (hemi == 'S' || hemi == 'W') ? -d : d;
}

static void handleGGA(int n) {
  if (n < 9) return;
  st.ggaCount++;
  st.quality  = atoi(fld[6]);
  st.satsUsed = atoi(fld[7]);
  st.hdop     = (float)atof(fld[8]);

  if (st.quality > 0 && fld[2][0] && fld[4][0]) {
    st.lat      = toDeg(fld[2], fld[3][0]);
    st.lon      = toDeg(fld[4], fld[5][0]);
    st.fixValid = true;
    st.fixMs    = millis();
  } else {
    st.fixValid = false;
  }
}

// 严格取数字:空字段、占位符、缺字符一律拒绝。
// 为什么不能直接用 atoi —— 未定位时模块会给**空字段**,atoi("") = 0,
// 会静悄悄变成"0 年 0 月 0 日 00:00:00"写进屏里,比不显示更糟。
static bool digN(const char* p, int n, int& v) {
  if (!p) return false;
  v = 0;
  for (int i = 0; i < n; ++i) {
    if (p[i] < '0' || p[i] > '9') return false;
    v = v * 10 + (p[i] - '0');
  }
  return true;
}

// ---- ZDA:UTC 日期 + 时间 ----
// 原文示例(2026-10-01 农村现场实采):
//   $GNZDA,100614.000,01,10,2026,00,00*4C
//   字段: 0=句名  1=hhmmss.ss  2=日 dd  3=月 mm  4=年 yyyy  5/6=本地时区
// 月份定长两位、年份定长四位,所以逐位验数字是可行的、也是必要的。
static void handleZDA(int n) {
  if (n < 5) return;
  st.zdaCount++;

  int h, mi, s, d, mo, y;
  if (!digN(fld[1],     2, h)  || !digN(fld[1] + 2, 2, mi) ||
      !digN(fld[1] + 4, 2, s))  return;         // 时间字段不合法
  if (!digN(fld[2], 2, d) || !digN(fld[3], 2, mo) ||
      !digN(fld[4], 4, y))      return;         // 日期字段不合法

  if (h > 23 || mi > 59 || s > 60) return;      // 60 = 闰秒(留个口子,不判死)
  if (mo < 1 || mo > 12 || d < 1 || d > 31) return;
  if (y < 2020 || y > 2199) return;             // 明显是没定上位的垃圾值

  st.uH = h; st.uMi = mi; st.uS = s;
  st.uY = y; st.uMo = mo; st.uD = d;
  st.utcValid = true;
  st.utcMs    = millis();                       // 喂给显示层的"数据还新不新"
}

static Talker* talkerOf(const char* name) {
  char id[3] = { name[0], name[1], '\0' };
  for (int i = 0; i < talkerN; ++i)
    if (strcmp(talkers[i].id, id) == 0) return &talkers[i];
  if (talkerN < kMaxTalker) {
    Talker* t = &talkers[talkerN++];
    memset(t, 0, sizeof(Talker));
    strcpy(t->id, id);
    return t;
  }
  return nullptr;
}

static int cmpFloat(const void* a, const void* b) {
  float x = *(const float*)a, y = *(const float*)b;
  return (x < y) ? -1 : ((x > y) ? 1 : 0);
}

// 所有星座的 C/N0 汇总后取中位数
static void commitCn0() {
  int n = 0;
  for (int i = 0; i < talkerN; ++i)
    for (int k = 0; k < talkers[i].cn0N && n < kMaxCn0; ++k)
      st.cn0[n++] = talkers[i].cn0[k];
  if (n == 0) return;

  st.cn0N = n;
  qsort(st.cn0, n, sizeof(float), cmpFloat);
  st.cn0Median = (n & 1) ? st.cn0[n / 2]
                         : 0.5f * (st.cn0[n / 2 - 1] + st.cn0[n / 2]);
  st.cn0Ms = millis();
}

// ---- 只读查询口(★ 2026-10-06 新增,专供第二界面"动图页")----
// 和 talkerOf 的区别:这里**只找不建桶**,返回 const 指针 —— 显示层不该有副作用。
inline const Talker* readBucket(const char* id) {
  for (int i = 0; i < talkerN; ++i)
    if (strcmp(talkers[i].id, id) == 0) return &talkers[i];
  return nullptr;
}

// 某星座当前 C/N0 中位数(不在场或没数据 -> 0)。第二界面趋势曲线的数据源。
// 每次现排一遍序 —— 最多 24 个元素、一秒一次,开销可以忽略。
inline float medianOf(const char* id) {
  const Talker* t = readBucket(id);
  if (!t || t->cn0N <= 0) return 0.0f;
  float tmp[kCn0PerTalker];
  int   n = t->cn0N;
  for (int i = 0; i < n; ++i) tmp[i] = t->cn0[i];
  qsort(tmp, n, sizeof(float), cmpFloat);
  return (n & 1) ? tmp[n / 2] : 0.5f * (tmp[n / 2 - 1] + tmp[n / 2]);
}

// 北斗的 talker:老写法 "BD",新标准 "GB",两个都认。
inline float bdsMedian() {
  float a = medianOf("BD");
  return (a > 0.0f) ? a : medianOf("GB");
}

static void handleGSV(const char* name, int n) {
  if (n < 4) return;
  st.gsvCount++;

  Talker* t = talkerOf(name);
  if (!t) return;

  int total = atoi(fld[1]);
  int num   = atoi(fld[2]);
  int view  = atoi(fld[3]);

  if (num <= 1) { t->total = total; t->seen = 0; t->cn0N = 0; t->satN = 0; }
  t->seen++;
  // 可见星数按星座分别记,求和后再入 st —— 原来直接覆盖,多星座时
  // 显示的是"最后一个星座"的可见数,会出现 USED 9 / VIEW 8 的矛盾
  if (view > 0) {
    t->view = view;
    int sum = 0;
    for (int i = 0; i < talkerN; ++i) sum += talkers[i].view;
    st.satsView = sum;
  }

  // 每颗星占 4 个字段:PRN, 仰角, 方位角, C/N0
  for (int k = 4; k + 3 < n; k += 4) {
    if (!fld[k] || !fld[k][0]) continue;
    float c = (float)atof(fld[k + 3]);
    if (c > 0 && t->cn0N < kCn0PerTalker) t->cn0[t->cn0N++] = c;

    // ★ 2026-10-06:位置另存一份,给第二界面的星空图。
    //   只有 el/az 都落在合法区间才记 —— 空的、超界的一律跳过。
    //   注意这里**不要求 c > 0**:看得见但还没锁上的星(CN0=0)也是有效信息,
    //   星空图会把它画成最小的灰点,代表"在视野里但信号弱"。
    int el = -1, az = -1;
    if (fld[k + 1] && fld[k + 1][0]) el = atoi(fld[k + 1]);
    if (fld[k + 2] && fld[k + 2][0]) az = atoi(fld[k + 2]);
    if (el >= 0 && el <= 90 && az >= 0 && az < 360 && t->satN < kSatPerTalker) {
      SatFix& sf = t->sat[t->satN++];
      sf.prn = (uint8_t)atoi(fld[k]);
      sf.el  = (int16_t)el;
      sf.az  = (int16_t)az;
      sf.cn0 = (c > 0 && c < 250) ? (uint8_t)(c + 0.5f) : 0;
    }
  }

  if (t->total > 0 && t->seen >= t->total) commitCn0();
}

// ★ v1.9.0 新增:GSA —— "这套解算到底用了哪几颗星、几何因子多少"
//   $GNGSA,A,3,23,24,07,08,09,27,,,,,,1.8,1.2,1.3*3A
//     [1] 模式 A=自动/M=手动   [2] fix type 1/2/3
//     [3..14] 12 个 PRN(空位留空)   [15] PDOP  [16] HDOP  [17] VDOP
//   两点说明:
//     · 只认 **1..64** 的 PRN —— GSA 的 12 个槽位里空槽是**空串**,atoi("")=0,
//       所以"prn>0"这一条同时把空槽挡掉了,不需要额外判空指针。
//     · HDOP 还是用 GGA 的(它按"参与定位的星"算);GSA 的 HDOP 这里只做交叉对照,
//       不覆盖 —— 改口径会让 P1 门限的语义在版本之间漂移。
static void handleGSA(const char* name, int n) {
  if (n < 18) return;                    // 少了 PDOP/HDOP/VDOP 就不是完整 GSA
  (void)name;                            // ★ 不分星座:PRN 槽位不解析,见 sigmaUere() 的注释

  st.gsaCount++;
  st.gsaFix = atoi(fld[2]);
  // [3..14] 那 12 个 PRN 槽位**不解析**:本模块的多句 GNGSA 跨星座重号,
  // 归不到具体哪颗星(见 sigmaUere 的注释)。这里只取句级字段 PDOP/VDOP。

  float p = (float)atof(fld[15]);
  float v = (float)atof(fld[17]);
  if (p > 0.0f && p < 100.0f) st.pdop = p;
  if (v > 0.0f && v < 100.0f) st.vdop = v;
}

static void feed(const char* sentence) {
  if (!sentence || sentence[0] != '$') return;
  int len = (int)strlen(sentence);
  if (len < 7 || len >= 120) return;
  if (!checksumOk(sentence)) { st.badCksum++; return; }

  char buf[120];
  memcpy(buf, sentence, len + 1);

  int n = split(buf + 1);
  if (n < 1) return;

  const char* name = fld[0];
  if (strlen(name) < 5) return;     // 句子名至少 5 字符(如 GNGGA),防止越界取 talker
  st.sentences++;

  if      (strstr(name, "GGA")) handleGGA(n);
  else if (strstr(name, "GSV")) handleGSV(name, n);
  else if (strstr(name, "GSA")) handleGSA(name, n);   // ★ v1.9.0:PDOP/VDOP + 参与星表
  else if (strstr(name, "ZDA")) handleZDA(n);   // ★ 2026-10-01:UTC 日期 + 时间
}

// 换信源/场景时清掉旧星座的 GSV 累积 —— 否则上个场景的强星 C/N0 和
// 可见数会留在桶里,新场景的"信号差"显示会自相矛盾(实测踩过:cn0 38)。
inline void flushConstellations() {
  for (int i = 0; i < talkerN; ++i) {
    talkers[i].total = 0; talkers[i].seen = 0;
    talkers[i].view  = 0; talkers[i].cn0N  = 0;
    talkers[i].satN  = 0;                 // ★ 2026-10-06:位置也一起清,别留上个场景的星
  }
  st.satsView = 0; st.cn0N = 0; st.cn0Median = 0; st.cn0Ms = 0;
}

// 把第 1 段缓冲里"还没解析过"的行喂进来。每个 loop 都要调。
inline void drain() {
  while (consumed < gnss::totalLines) {
    uint32_t back = gnss::totalLines - 1 - consumed;
    if (back >= (uint32_t)NMEA_POOL) {          // 积压过多,丢掉最早的一段
      consumed = gnss::totalLines - NMEA_POOL;
      continue;
    }
    const char* l = gnss::recent((int)back);
    if (!l) break;
    feed(l);
    consumed++;
  }
}

}  // namespace nmea

// ----------------------------------------------------------------------------
//  状态词表(全英文)—— v1.7.0 起替代原来的 32x32 中文点阵字模(cnfont)
//  ★ 为什么要换:内置字模里只有 FONT4(Font32rle)含完整的 96 个可打印 ASCII,
//    而中文字模要给每个词单独备 128 字节点阵、还要配生成脚本。8 个状态词
//    用 ASCII 拼出来就能覆盖,省约 2.7KB flash + 2KB RAM,也顺手把"中文/英文
//    两套显示口径"这件事彻底消掉。
//  ⚠ 别用 FONT6/FONT8 画这些词:它们只含 `[space] 0-9 : - . a p m` ——
//    打 "NORMAL" 会输出一片空白(不报错、不崩,只是屏上什么都没有)。
// ----------------------------------------------------------------------------
namespace phrase {

// ★ 状态词枚举 —— 两处用途,缺一不可:
//   ① judge::Result.ph 的类型;
//   ② **状态色的唯一输入**:statusColor565(phrase, blink) 按它取色。
//   为什么不能用 judge::Level 代替:同一个 LV_LOST 对应两个不同的词 ——
//   开机从未定位 = NO-DATA(灰,正常等星),定位过又丢 = LOST(红闪,真的丢了)。
//   按 Level 取色会让"刚开机还没定位"红屏闪,看着像坏了。
enum PhraseId {
  PH_WAIT = 0, PH_SEARCH, PH_OK, PH_DEGRADED, PH_SIGBAD, PH_POSBAD, PH_LOST, PH_NODATA,
  PH_RECOVER,   // ★ v1.8.2:**只给事件日志用** —— "恢复正常"是一条独立的事件记录,
                //   不参与主界面判据(judge 永远不会产出这个值)。
  PH_HPLHIGH,   // ★ v1.9.0:完好性不足(HPL > AL)。
                //   为什么它是**独立的一个词**而不是并进 DEGRADED:
                //   两者说的不是一件事 —— DEGRADED 是"信号弱"(能测,只是不准);
                //   HPL-HIGH 是"**连'准不准'都给不出保证**"(保护级已超出告警限)。
                //   把这两个混成一个词,三态判据就退化成两态,那一整套就白做了。
  PH_DRIFT,     // ★ v1.10.1:**慢漂移**(CUSUM 积满),与 POS-BAD 分开。
                //   为什么必须分:两者的证据强度差一个数量级 ——
                //     POS-BAD = "坐标**此刻**就离基准 30 m 以上"(当场可量,红)
                //     DRIFT   = "坐标还没到 30 m,但已比 5 分钟前持续偏了 1~3 m"(趋势判断,琥珀)
                //   2026-10-17 实测:一段 POS-BAD 持续 12 分 23 秒,而两端 off 只有 19.3 / 17.3 m
                //   —— 用户看到红屏 + "数据都很好",完全不知道是被哪条点着的。
                //   合成一个词等于把"趋势"说成"事实",这就是那次困惑的来源。
  PH_COUNT
};

// 屏上显示词(实际只出现 8 个)。PH_WAIT 与 PH_NODATA 同显示 "NO-DATA" ——
// 两者都是"还没有可用数据",屏上不必区分(串口的 tag 会区分 WAIT / NO-DATA)。
// 最宽 "DEGRADED" / "INTEG OK" / "HPL-HIGH" 都是 8 字符,FONT4 下约 152px
// < 数据条左侧 158px 领地 —— **再加长就不行了**,改词前先量宽度。
static const char* const kText[PH_COUNT] = {
  "NO-DATA",    // PH_WAIT     等待数据(一个字都没收到 / 冷启动宽限内)
  "SEARCH",     // PH_SEARCH   搜星中(有数据但还没定位)
  "INTEG OK",   // PH_OK       ★ v1.9.0:原 "NORMAL"。三态里的"保证成立"那一态
  "DEGRADED",   // PH_DEGRADED 质量降级(CN0 中位偏低)
  "SIG-BAD",    // PH_SIGBAD   信号异常(星少 / HDOP 差 / 无定位)
  "POS-BAD",    // PH_POSBAD   位置异常(坐标偏离基准 —— 疑似欺骗)
  "LOST",       // PH_LOST     信号丢失(定位过之后就没数据了)
  "NO-DATA",    // PH_NODATA   无数据
  "NORMAL",     // PH_RECOVER  恢复正常(★ 只出现在事件日志里:异常 -> 正常的转变事件)
                //   刻意**不跟着改**成 "INTEG OK":它记的是"回到正常"这个动作,
                //   而不是"当前完好性成立"这个断言。
  "HPL-HIGH",   // PH_HPLHIGH  ★ v1.9.0:保护级超出告警限 —— 此刻**报警本身不可信**
  "DRIFT",      // PH_DRIFT    ★ v1.10.1:坐标在慢慢漂(还没到 POS LIM)。
                //   5 个字符,FONT4 下约 95px —— 比 "DEGRADED"(8 字符/152px)短,宽度无忧。
};

inline const char* text(PhraseId p) {
  return (p < PH_COUNT) ? kText[p] : kText[PH_NODATA];
}

}  // namespace phrase


// ============================================================================
//  第 3 段:判据
// ----------------------------------------------------------------------------
//  ★ v1.8.3:段名里原来的"+ 注入"已删 —— 演示用的人为偏移坐标**整套移除**
//  (用户 2026-10-17 定:不再做演示)。判据现在只用**真实收到的坐标**与基准比对。
//    仍然保留"位置异常"(POS-ANOMALY)这个判据与事件类型 —— 真实世界里坐标被
//    冒名顶替时,报文看着毫无问题、只有坐标是假的,那才是它该起作用的场合。
// ============================================================================
// ★ v1.8.6:前置声明 —— 采样收尾是**异步**的(10 s 后由 update() 收),那一刻
//   也要给 C3 一条回执,所以 judge 段里要调 wifi10::rptText(定义在 3800 行之后)。
//   只此一处跨段调用,声明放这儿比把采样状态机搬出 judge 段更清爽。
namespace wifi10 { static void rptText(const char* s); }

// ★ v1.9.0:完好性 / 慢漂移 / 多径 三件套的**前置声明**。
//   定义排在 judge 之后(它要读 judge::baseSet),而 judge::update() 又要读它的结论 ——
//   互相依赖一圈,用前置声明断开即可,不必把两段揉成一坨。
namespace integ {
  void  tick(float off);     // 1 Hz 推进一次(内部自己限速)
  void  resetBaseline();     // ★ 换尺子(设/清基准)后重建 μ,旧漂移历史作废
  bool  hplBadOn();          // HPL > AL(已过迟滞)
  bool  slowOn();            // CUSUM 判定"在慢慢漂"
  bool  valid();             // 这一拍 HPL 算得出来吗
  float hpl();               // 水平保护级(米)
}

namespace judge {

enum Level  { LV_OK = 0, LV_DEGRADED, LV_ABNORMAL, LV_LOST };
enum Reason { R_NONE = 0, R_SIGNAL, R_POSITION, R_NODATA };

struct Result {
  Level  level  = LV_LOST;
  Reason reason = R_NODATA;
  phrase::PhraseId ph = phrase::PH_WAIT;       // ★ v1.7.0:屏上状态词(英文,见 phrase::kText)
                                               //   注:字段名用 ph 不用 phrase —— 与命名空间
                                               //   `phrase` 同名会被同名遮蔽规则盯上,可读性也差
  const char* tag = "WAIT";                    // 串口报表用的 ASCII 标签
  float offsetM = 0;
};

static Result      res;
static double      baseLat   = 0, baseLon = 0;
static bool        baseSet   = false;
static uint32_t    badSince  = 0, goodSince = 0;
static bool        alarm     = false;
static uint32_t    bootMs    = 0;
static bool        everFixed = false;      // 是否成功定位过
static Preferences prefs;

// ---- ★ v1.8.6:设基准的 P1/P2 状态(定义排在这里,因为 clearBase() 要中断采样)----
// 详见下面 saveBase() 之后的整段说明。
// ★ v1.10.0:原来这里有两个 constexpr(kBaseMinSats = 6 / kBaseMaxHdop = 2.0),
//   **已搬进 tune 参数表**(PARAMS 页的 SAT MIN / HDOP MAX)。搬的理由很具体:
//   这两个是现场最想调的门 —— 2026-10-17 实测 HDOP 在 2.1~2.4 晃了 3 分钟才等到
//   1.4 的窗口,而 2.0 的闸一卡就是一整轮。现在可以在屏上当场放宽,不用改代码重烧。
//   默认值仍是 6 / 2.0 ⇒ 不动它就是原来那个行为。
constexpr uint32_t kBaseSmpMs     = 10000;   // P2 采样窗口(10 s)
constexpr int      kBaseSmpMinN   = 8;       // 窗口内至少要有 8 个有效样本
constexpr float    kBaseSmpMaxSpr = 25.0f;   // 样本极差上限(米)

static bool     smpOn    = false;
static uint32_t smpT0    = 0, smpLast = 0;
static int      smpN     = 0;
static double   smpSumLat = 0, smpSumLon = 0;
static double   smpMinLat = 0, smpMaxLat = 0, smpMinLon = 0, smpMaxLon = 0;

// ---------------- 工具 ----------------
static float distM(double lat1, double lon1, double lat2, double lon2) {
  const double R = 6371000.0, D2R = 0.017453292519943295;
  double p1 = lat1 * D2R, p2 = lat2 * D2R;
  double dp = (lat2 - lat1) * D2R, dl = (lon2 - lon1) * D2R;
  double a  = sin(dp / 2) * sin(dp / 2) + cos(p1) * cos(p2) * sin(dl / 2) * sin(dl / 2);
  if (a > 1.0) a = 1.0;
  return (float)(R * 2 * atan2(sqrt(a), sqrt(1 - a)));
}

inline void begin() {
  bootMs = millis();
}

// ---------------- 基准坐标(★★ v1.7.0:改回**持久化**,带三条防护) ----------------
// 2026-09-29 曾按用户要求改成"会话制"(不落盘);2026-10-16 用户拍板改回落盘。
// 当年踩的坑**不是落盘本身,而是"落了分不清"** —— 没有来源标记,看不出这个 SET
// 是刚设的还是上次残留的。所以恢复 NVS 的同时必须配防护,缺一不可:
//   ① **来源标签**  屏上 BASE 键三态:--- 无 / SET 本次设 / SAV 从 flash 恢复
//   ② **合法性校验** magic + ver + 坐标范围 + 非 (0,0) —— 空 NVS / flash 垃圾不会被当基准
//   ③ ~~inject 不落盘~~ ★ v1.8.3 作废:注入功能已整体删除,不再有"重启后忘了关
//      导致屏上直接 POS-BAD"的可能。这条防护随之退休(只留 ① ② 两条)。
// 附加收益:串口工具一开端口会拉 RTS/DTR 复位芯片(本项目老坑),复位后基准不再丢,
// 只有串口 'c' 能清。写入频率也无需担心:saveBase 只在人主动设基准那一刻写,
// 一场演示约 20 次 vs NVS 页擦写寿命约 10 万次 —— 差 3 个数量级。
constexpr const char* kNvsName  = "gnsjudge";    // 独立命名空间(不与 gnstouch / gnsevt / wifi10 混)
constexpr uint32_t    kBaseMagic = 0x47534A31u;  // 'GSJ1'
constexpr uint16_t    kBaseVer   = 1;

// 基准来源。屏上/串口都按它显示三态。
enum BaseSrc : uint8_t { SRC_NONE = 0, SRC_SET = 1, SRC_SAV = 2 };
static uint8_t baseSrc = SRC_NONE;

struct BaseBlob {
  uint32_t magic; uint16_t ver; uint16_t flags;   // flags: bit0 = utcOk
  double   lat, lon; uint32_t setUtc;             // setUtc = 设基准那一刻的**当天秒**(UTC)
};
static_assert(sizeof(BaseBlob) == 32, "BaseBlob 布局变了 —— NVS 里的旧基准会读不出来");
constexpr int kBlobSize = 28;                     // 只写前 28 字节,已含全部字段(尾部 4B 是对齐填充)

inline bool blobOk(const BaseBlob& b) {
  if (b.magic != kBaseMagic || b.ver != kBaseVer) return false;
  if (b.lat < -90.0 || b.lat > 90.0)   return false;   // 纬度范围
  if (b.lon < -180.0 || b.lon > 180.0) return false;   // 经度范围
  if (b.lat == 0.0 && b.lon == 0.0)    return false;   // (0,0) 在几内亚湾,不可能是台架
  return true;
}

inline void storeBase() {
  BaseBlob b;
  b.magic  = kBaseMagic;
  b.ver    = kBaseVer;
  b.flags  = nmea::st.utcValid ? 0x0001 : 0x0000;
  b.lat    = baseLat;
  b.lon    = baseLon;
  b.setUtc = (uint32_t)nmea::st.uH * 3600u + (uint32_t)nmea::st.uMi * 60u + (uint32_t)nmea::st.uS;
  prefs.putBytes("base", &b, kBlobSize);
}

// setup() 里在 judge::begin() 之后调用。★ 这里自己 begin 命名空间 —— 之后
// saveBase / clearBase 直接可用(它们只可能在 setup 跑完之后被触摸/串口触发)。
inline void loadBase() {
  prefs.begin(kNvsName, false);
  if (prefs.getBytesLength("base") != kBlobSize) return;    // 没存过 / 长度不对 -> 保持无基准
  BaseBlob b;
  if (prefs.getBytes("base", &b, kBlobSize) != kBlobSize) return;
  if (!blobOk(b)) return;                                   // 垃圾数据 -> 忽略(防护 ②)
  baseLat = b.lat; baseLon = b.lon;
  baseSet = true;
  baseSrc = SRC_SAV;                                        // ★ 关键:来源是"恢复",不是"本次设"
  Serial.printf("[bench] base restored from NVS (SAV %.5f,%.5f)\n", baseLat, baseLon);
}

inline void saveBase(double lat, double lon) {
  baseLat = lat;
  baseLon = lon;
  baseSet = true;
  baseSrc = SRC_SET;                                        // ★ 来源是"本次设"
  storeBase();                                              // ★ v1.7.0:落盘
  // ★ v1.9.0:换基准 = 换尺子 ⇒ CUSUM 的 μ 必须重建。
  //   不重建的后果不是误报而是**漏报**:μ 还记着旧基准下那个偏了 200 m 的
  //   off,新的 off 从 0 起算,dev 长期为负,统计量永远压在 0,"真漂了也不报"。
  integ::resetBaseline();
}

inline void clearBase() {
  baseLat = 0; baseLon = 0;
  baseSet = false;
  baseSrc = SRC_NONE;
  smpOn   = false; smpN = 0;                                // ★ v1.8.6:清基准同时中断采样
  prefs.remove("base");                                     // ★ v1.7.0:擦掉 flash 里那份
  integ::resetBaseline();                                   // ★ v1.9.0:同上
}

// ============================================================================
//  ★ v1.8.6:设基准的两道新闸 —— P1 质量门限 + P2 10 s 采样平均
//  (用户 2026-10-17 拍板:"P1+P2 一起做")
// ----------------------------------------------------------------------------
//  **为什么必须加**(2026-10-17 现场实证,不是猜测):
//    NVS 里那份基准与文档记录的同一台架坐标相差 203.6 m,且**几乎纯纬向**(203.7 m)。
//    当时现场抓到的却是 `sat 8/20 | hdop 1.9 | cn0 31`(好定位)却恒报 POS-ANOMALY off=248m。
//    按 248 m 反推当前点,与文档那份只差 39 m(正常噪声)
//    ⇒ 收进来的坐标是对的,**错的是尺子**:存基准那一刻的定位是坏的。
//  根因:老 saveBase() 的准入只有 `fixValid` —— **"有定位"不等于"定位准"**。
//    冷启动首解、半边天遮挡、窗边/墙边多径,都能给出 fixValid=true 而偏几百米的
//    解;按一下 BASE 就把它写进 flash,之后每次开机都拿它当基准,
//    表现出来就像"设备永久漂移 200 米",而且断电重启也甩不掉。
//  两道闸:
//    P1 质量门限:fixValid + 星数 >= 6 + HDOP <= 2.0 + 定位新鲜(< TH_STALE_MS),
//       任一不满足**直接拒绝**,不落盘,并把原因回执给 C3(产线工装看得见)。
//    P2 采样平均:门限过了也**不当场落盘**,先 1 Hz 采 10 s,要求样本极差 <= 25 m,
//       通过才把**均值**写进 flash。这样连"HDOP 好看但位置还在跳"也能挡住。
//  采样期间屏上第 1 键变 `SET 10..0` 倒计时 + 底部绿进度条;取消/重设/清基准
//  都会中断采样(见 clearBase / beginBaseSample)。
// ============================================================================
inline bool baseSmpOn() { return smpOn; }

// 0..10 = 十等分进度(给 btnState/paintBot 画倒计时与进度条)
inline uint8_t baseSmpStep() {
  if (!smpOn) return 0;
  const uint32_t h = millis() - smpT0;
  return (uint8_t)(h >= kBaseSmpMs ? 10 : (h * 10u) / kBaseSmpMs);
}

// P1 门限。返回 nullptr = 通过;否则是一句 ASCII 短标签(串口与 C3 回执都用它)。
// ★ v1.10.0:后两条读 tune::satMin() / tune::hdopMax() —— PARAMS 页改完**立即生效**,
//   但只对"下一次按 BASE"生效(已存好的基准不会被追溯判废,那样等于自己把自己推翻)。
inline const char* baseGateFail() {
  if (!nmea::st.fixValid)                        return "no fix";
  if ((millis() - nmea::st.fixMs) > TH_STALE_MS) return "fix stale";
  if (nmea::st.satsUsed < tune::satMin())        return "too few sats";
  if (nmea::st.hdop > tune::hdopMax())           return "hdop too high";
  return nullptr;
}

// 开始采样。返回 false = 已经在采样中(重复按 BASE 不清零重来)。
inline bool beginBaseSample() {
  if (smpOn) return false;
  smpOn = true; smpT0 = millis(); smpLast = 0; smpN = 0;
  smpSumLat = smpSumLon = 0;
  return true;
}

// P2 采样一拍:1 Hz 收一个点;到 10 s 收尾判极差,通过就把均值落盘。
// 由 update() 每圈调用 —— 全程非阻塞,GNSS 主循环不受影响。
static void sampleStep(uint32_t now) {
  if (smpN == 0 || (now - smpLast) >= 1000) {
    if (nmea::st.fixValid && (now - nmea::st.fixMs) <= TH_STALE_MS) {
      const double la = nmea::st.lat, lo = nmea::st.lon;
      if (smpN == 0) { smpMinLat = smpMaxLat = la; smpMinLon = smpMaxLon = lo; }
      else {
        if (la < smpMinLat) smpMinLat = la;
        if (la > smpMaxLat) smpMaxLat = la;
        if (lo < smpMinLon) smpMinLon = lo;
        if (lo > smpMaxLon) smpMaxLon = lo;
      }
      smpSumLat += la; smpSumLon += lo; ++smpN;
      smpLast = now;
    }
  }
  if ((now - smpT0) < kBaseSmpMs) return;      // 窗口还没满

  smpOn = false;
  const float spread = (smpN > 1) ? distM(smpMinLat, smpMinLon, smpMaxLat, smpMaxLon) : 9999.0f;

  if (smpN < kBaseSmpMinN) {
    Serial.printf("[bench] base NOT saved: only %d/%d samples in %us (hold still, open sky)\n",
                  smpN, kBaseSmpMinN, (unsigned)(kBaseSmpMs / 1000u));
    wifi10::rptText("base NOT saved: too few samples");
    return;
  }
  if (spread > kBaseSmpMaxSpr) {
    Serial.printf("[bench] base NOT saved: unstable, spread %.0fm > %.0fm (%d samples)\n",
                  spread, kBaseSmpMaxSpr, smpN);
    wifi10::rptText("base NOT saved: unstable");
    return;
  }

  saveBase(smpSumLat / smpN, smpSumLon / smpN);
  // 换尺子 ⇒ 旧报警作废:否则屏上还要再红 8 秒(TH_RECOVER_MS)才转绿,
  // 现场会误以为"刚设完还是漂"。
  alarm = false; badSince = 0; goodSince = now;
  Serial.printf("[bench] base saved: avg of %d samples, spread %.1fm (sat %d, hdop %.1f)\n",
                smpN, spread, nmea::st.satsUsed, nmea::st.hdop);
  wifi10::rptText("base saved (10s avg)");
}

// 统一入口 —— 触摸第 1 键 / 串口 'b' / C3 下发 'b' 三个入口都只走这一条路。
// 返回 true = 已开始采样(10 s 后自动落盘或自动放弃)。
// msgOut 收一条**给 C3 的短回执**(34 列以内),成功/失败都有 —— 三个调用点
// 只管把它丢给 wifi10::rptText(),不各自拼文案(避免三份文案漂移)。
inline bool requestBase(char* msgOut, size_t msgN) {
  if (smpOn) {                                  // 采样中再按:不重来,只提示
    if (msgN) snprintf(msgOut, msgN, "base: sampling...");
    Serial.println("[bench] base: sampling already in progress");
    return false;
  }
  const char* why = baseGateFail();
  if (why) {
    if (msgN) {
      if (!strcmp(why, "too few sats") || !strcmp(why, "hdop too high"))
        snprintf(msgOut, msgN, "weak fix sat%d hdop%.1f need 6/2.0",
                 nmea::st.satsUsed, nmea::st.hdop);
      else
        snprintf(msgOut, msgN, "%s - base NOT saved", why);
    }
    Serial.printf("[bench] base NOT saved: %s (sat %d/%d, hdop %.1f)\n",
                  why, nmea::st.satsUsed, nmea::st.satsView, nmea::st.hdop);
    return false;
  }
  beginBaseSample();
  if (msgN) snprintf(msgOut, msgN, "base: sampling %us hold still", (unsigned)(kBaseSmpMs / 1000u));
  Serial.printf("[bench] base: sampling %us, hold still (sat %d, hdop %.1f)\n",
                (unsigned)(kBaseSmpMs / 1000u), nmea::st.satsUsed, nmea::st.hdop);
  return true;
}

// ---------------- 主判据 ----------------
inline void update() {
  uint32_t now = millis();

  if (smpOn) sampleStep(now);      // ★ v1.8.6:P2 采样(10 s 后在这里收尾落盘)

  if (nmea::st.fixValid) everFixed = true;

  // 一个字都没收到 -> "等待数据"(多半是接线/波特率问题,不是信号异常)
  // ★ 2026-10-01:补 stored 兜底。原来只看 totalLines == 0,但它是 uint32 计数,
  //   连续跑约 5 天回绕时恰好经过 0,那一瞬会被误判成"无数据"(屏闪一下)。
  //   池里还留着历史行(stored > 0)就说明数据一直在来,不该判无数据。
  bool noData = (gnss::totalLines == 0 && gnss::stored == 0);

  // 冷启动宽限:从没定过位、且开机不到 90 s —— 这段时间只提示"搜星中",不报警。
  // 否则上电头几秒必然"无定位",会当场报警,现场看着像设备坏了。
  bool warming = noData || ((!everFixed) && ((now - bootMs) < TH_COLDSTART_MS));

  bool stale = (now - nmea::st.fixMs) > TH_STALE_MS;

  // 参与比对的坐标:**就是收到的真实坐标**(★ v1.8.3 起没有"注入偏移"这条岔路了,
  // 原来这里有一段 `if (inject && baseSet) { cmpLat = injLat; ... }`)
  float off = 0;
  if (baseSet) off = distM(baseLat, baseLon, nmea::st.lat, nmea::st.lon);

  // ★ v1.9.0:推进三件套(完好性 / CUSUM / 多径)。内部自己限速到 1 Hz,
  //   所以放在这里每圈调用是安全的 —— 它的输入本来就只有 1 Hz 的 NMEA。
  integ::tick(off);

  const bool hplBad = integ::hplBadOn();

  bool sigBad  = stale
              || (!nmea::st.fixValid)
              || (nmea::st.satsUsed < TH_MIN_SATS_USED)
              || (nmea::st.hdop > TH_MAX_HDOP);
  // ★ v1.9.0 三态判据(用户 2026-10-17 定):**位置异常要先过完好性这一关**。
  //   ① off > 30 m          = 坐标对不上基准(老判据)
  //   ② integ::slowOn()     = CUSUM 抓到"还没到 30 m 但在持续拉偏"(新判据)
  //   ③ !hplBad             = "此刻我有资格下这个结论"(新判据)
  //   为什么 ③ 是"与"、不是"或":HPL > AL 时,连"现在这组坐标的误差上限"都给不出保证,
  //   这时候报 POS-BAD 等于**把系统误差说成攻击** —— 那才是真正的误报。
  //   代价必须说清:这段时间里真出事也不会报 POS-BAD,状态词会停在 HPL-HIGH。
  //   这是**有意**的取舍(航空界也是这么做的:HPL > AL ⇒ 完好性不可用)。
  // ★ v1.10.0:30 m 这个数改成 tune::posLim() —— 屏上 PARAMS 页可调,**改完立即生效**
  //   (下一个 1 Hz 判据拍就用新值)。默认 30,与 v1.9.0 完全一致。
  // ★★ v1.10.1:把原来的 `posBad` **拆成两条**,各给各的词:
  //   overLim = 坐标**此刻**就离基准超过 POS LIM —— 当场可量的事实 ⇒ POS-BAD(红)
  //   driftOn = 还没过线,但 CUSUM 认为在持续拉偏   —— 趋势判断   ⇒ DRIFT(琥珀)
  //   拆的依据是 2026-10-17 的实测:一段 POS-BAD 持续 12 分 23 秒,而它两端的 off
  //   只有 19.3 / 17.3 m(门槛 30)。也就是说**红屏亮了 12 分钟,却没有一分钟真的越线**
  //   —— 用户当场就问"数据都很好,为什么是 POS-BAD"。词和证据对不上,就是这个后果。
  //   排序仍是**距离优先**:同时成立时报 POS-BAD,因为事实强于趋势。
  const bool overLim = baseSet && !hplBad && (off > tune::posLim());
  const bool driftOn = baseSet && !hplBad && !overLim && integ::slowOn();
  bool qualBad = (nmea::st.cn0Ms != 0)
              && ((now - nmea::st.cn0Ms) < TH_STALE_MS)
              && (nmea::st.cn0Median < tune::cn0Min());

  bool bad = (sigBad || overLim || driftOn || hplBad) && !warming;

  if (bad) { if (badSince == 0) badSince = now; goodSince = 0; }
  else     { if (goodSince == 0) goodSince = now; badSince = 0; }

  // ★ v1.10.0:HOLD / RECOVER 也从 tune 读(屏上单位是秒,这里换算成 ms)。
  //   它们同时管主状态机和 HPL 迟滞 —— 一组时间常数只能有一个真源。
  if (alarm) {
    if (!bad && goodSince && (now - goodSince >= tune::recoverMs())) alarm = false;
  } else {
    if (bad && badSince && (now - badSince >= tune::holdMs())) alarm = true;
  }

  if (!alarm) {
    if (noData) {
      res.level = LV_DEGRADED; res.reason = R_NODATA;
      res.ph = phrase::PH_WAIT;    res.tag = "WAIT";
    } else if (warming) {
      res.level = LV_DEGRADED; res.reason = R_NODATA;
      res.ph = phrase::PH_SEARCH;  res.tag = "SEARCH";
    } else if (stale) {
      res.level = LV_LOST;     res.reason = R_NODATA;
      res.ph = phrase::PH_NODATA;  res.tag = "NO-DATA";
    } else if (hplBad) {
      // ★ v1.9.0:信号还在、坐标也没偏,但**保护级已经超出告警限** ——
      //   这一态在老固件里没有落脚处(会被说成 NORMAL),是这次新增的第三态。
      res.level = LV_DEGRADED; res.reason = R_SIGNAL;
      res.ph = phrase::PH_HPLHIGH; res.tag = "HPL-HIGH";
    } else if (qualBad) {
      res.level = LV_DEGRADED; res.reason = R_SIGNAL;
      res.ph = phrase::PH_DEGRADED; res.tag = "DEGRADED";
    } else {
      res.level = LV_OK;       res.reason = R_NONE;
      // ★ v1.9.0:tag 跟屏上一致改成 INTEG-OK(原来叫 NORMAL)。
      //   改的理由很实际:**串口和屏必须是同一套口径** —— 现场排障时一边看屏
      //   一边敲 s,两个词对不上就会怀疑是不是又出了岔子。连字符而非空格,
      //   与 SIG-BAD / POS-BAD / HPL-HIGH 保持同一种写法。
      res.ph = phrase::PH_OK;      res.tag = "INTEG-OK";
    }
  } else {
    // 判断顺序有讲究:
    //   信号本身失真(星少 / HDOP 差 / 无定位)-> "信号异常",证据直接
    //   信号看着完好、只有坐标对不上          -> "位置异常",这才是疑似欺骗的特征
    if (stale) {
      res.level = LV_LOST;     res.reason = R_NODATA;
      res.ph = phrase::PH_LOST;    res.tag = "LOST";
    } else if (sigBad) {
      res.level = LV_ABNORMAL; res.reason = R_SIGNAL;
      res.ph = phrase::PH_SIGBAD;  res.tag = "SIG-ABNORMAL";
    } else if (overLim) {
      res.level = LV_ABNORMAL; res.reason = R_POSITION;
      res.ph = phrase::PH_POSBAD;  res.tag = "POS-ANOMALY";
    } else if (driftOn) {
      // ★ v1.10.1:坐标**还没**越 POS LIM,但 CUSUM 认定在持续拉偏。
      //   等级给 DEGRADED(与 HPL-HIGH 同档),词单独一个 DRIFT —— 因为它的证据
      //   只到"趋势",不像 POS-BAD 那样"当场就能量给你看"。
      res.level = LV_DEGRADED; res.reason = R_POSITION;
      res.ph = phrase::PH_DRIFT;   res.tag = "DRIFT";
    } else if (hplBad) {
      // 到这一步说明:sigBad / overLim / driftOn 都不成立,只有完好性不足。
      // 它是唯一一个"进了报警状态、却只算 DEGRADED 等级"的词 —— 因为
      // 设备本身没坏,是**外部条件**让它给不出保证。
      res.level = LV_DEGRADED; res.reason = R_SIGNAL;
      res.ph = phrase::PH_HPLHIGH; res.tag = "HPL-HIGH";
    } else {
      // ★ v1.9.0:走到这里 = 报警还挂着(等它 8 s 自然解除),但 sigBad / posBad /
      //   hplBad **四条都已经不成立**了。此时**什么词都不改**,沿用上一拍的 —— 
      //   它们还留在 judge::res 里(命名空间级结构,跨调用保活)。
      //   以前这里是硬写 SIG-ABNORMAL,加了 HPL-HIGH 之后就露馅了:
      //   HPL 一降回 AL 以下,屏上会冒出一次**假的"信号异常"**(实测 T15 那拍),
      //   设备明明好好的却说自己信号坏了 —— 这种假词比不说更难解释。
      res.level = LV_ABNORMAL; res.reason = R_SIGNAL;
    }
  }

  res.offsetM = off;
}

}  // namespace judge

// ============================================================================
//  ★★ v1.9.0:完好性 / 慢漂移 / 多径 —— 三个"能报名字"的算法
// ----------------------------------------------------------------------------
//  为什么要单开一段、而不是塞进 judge:
//    judge 干的是"按阈值下结论"(有解/无解、漂没漂);
//    这一段算的是"**这些结论本身有多可信**"(保护级 / 趋势 / 信号质量)。
//    两者混在一起,以后动一个必碰另一个,所以分开。
//  一并声明边界(答辩会被问,先说清):
//    · 输入只有 NMEA 的**结果量**(GGA 的 HDOP/星数、GSA 的 PDOP/VDOP/参与星表、
//      GSV 的仰角/方位/C-N0),**没有伪距/星历/载波** ⇒ 做的是解域完好性,
//      不是测量域 RAIM。三者都不是认证级参数,一律标 "est"。
//    · 全部在 ESP32-S3 的单精度 FPU 上跑,1 Hz 一拍,实测开销可忽略
//      (每拍 ~30 次 powf + 2x2 矩阵几次乘加)。
// ============================================================================
namespace integ {

struct State {
  bool  valid  = false;   // 这一拍 HPL 算出来了吗
  float hpl    = 0;       // 水平保护级(米,est)
  float vpl    = 0;       // 垂直保护级(米,est)
  float sigma  = 0;       // σ_UERE 代表值(米)
  int   nSig   = 0;       // 参与 σ 统计的星数
  float cusum  = 0;       // 单侧(向上)CUSUM 统计量(m·s)
  bool  slow   = false;   // 已判定"在慢慢漂"
  int   mpN    = 0;       // 多径可疑星数
  float mpMax  = 0;       // 最大正残差(dB)
  int   mpPrn  = 0;       // 残差最大的那颗(给屏上写 PRN)
  int   mpEl   = -1;
  bool  hplBad = false;   // HPL > AL(已过迟滞)
};
static State g;

// ---------------- ① 单星测距噪声 ----------------
inline float sigmaOf(float cn0) {
  float s = kSigRefM * powf(10.0f, (kCn0Ref - cn0) / 20.0f);
  if (s < kSigMinM) s = kSigMinM;
  if (s > kSigMaxM) s = kSigMaxM;
  return s;
}

// σ_UERE = 参与解算那批星的 σ² 均方根。
// 为什么不是"按 1/σ² 加权":那样会几乎只由最强的一两颗星决定,
// 而保护级要回答的是"**最坏情况下**我能保证多少",用平均值更保守。
//
// ★ "参与解算那批星"怎么取 —— 这里踩过一个坑,记录在案:
//   第一版想用 GSA 的星表过滤,上板实测发现**不行**:这台模块吐的是多句 `$GNGSA`,
//   句名一律 "GN",而 PRN 跨星座重号(GPS 1..32 / 北斗 1..63)。并集一 OR,
//   几乎每颗星都"命中",连 CN0≈20 的弱星也算进来 ⇒ σ 被拉到 8.7 m、
//   HPL 常年 40 m、屏上一直 HPL-HIGH(实测 2026-10-17,不是推测)。
//   ⇒ 改用**按 CN0 取最强的 k 颗**(k = GGA 的 satsUsed)。接收机选星本来就是
//     优先强星,这个代理在物理上站得住,而且不依赖任何跨星座编号。
//   GSA 仍然有用 —— PDOP/VDOP/定点类型照拿(那三个是句级字段,不涉及重号)。
static float sigmaUere(int* nOut) {
  const int have = nmea::st.cn0N;          // commitCn0() 已按升序排好
  if (have <= 0) { if (nOut) *nOut = 0; return 0.0f; }

  int k = nmea::st.satsUsed;               // 参与定位的星数
  if (k <= 0 || k > have) k = have;
  double sum = 0.0;
  for (int i = have - k; i < have; ++i) {  // 升序数组的末尾 = 最强的 k 颗
    const float s = sigmaOf(nmea::st.cn0[i]);
    sum += (double)s * s;
  }
  if (nOut) *nOut = k;
  return (float)sqrt(sum / (double)k);
}

// ---------------- ② CN0–仰角模板(2 参数递推最小二乘 RLS)----------------
//  模型:CN0 ≈ a + b·sin(el)。选 sin(el) 而不是 el 本身,是因为穿过大气层的
//  斜路径长度 ∝ 1/sin(el) —— 用 sin 恰好把"路径越长、衰减越大"这件事写进模型。
//  RLS 的三个好处:不需要存历史样本、每拍只做几次乘加、遗忘因子让它自动跟季节/天线变化。
struct Fit {
  float a   = 22.0f;      // 地平线附近的先验值(dB-Hz)
  float b   = 16.0f;      // 天顶增量:22 + 16 = 38 dB-Hz,与实测的开阔天空量级相符
  float p00 = 400.0f, p01 = 0.0f, p11 = 400.0f;   // 协方差初值(大 = 起步快、先信数据)
  bool  seed = false;
};
static Fit fits[nmea::kMaxTalker];

inline float cn0Expect(const Fit& f, float elDeg) {
  return f.a + f.b * sinf(elDeg * 3.14159265f / 180.0f);
}

// 稳健化:残差超过 12 dB 的样本**不参与拟合** —— 否则一颗真的被多径抬高的星
// 会把自己"学"成正常,检测器就把自己的证据吃掉了(这是自适应检测器的经典陷阱)。
inline void fitUpdate(Fit& f, float elDeg, float y) {
  const float x    = sinf(elDeg * 3.14159265f / 180.0f);
  const float pred = f.a + f.b * x;
  if (f.seed && fabsf(y - pred) > 12.0f) return;

  const float s0  = f.p00 + f.p01 * x;
  const float s1  = f.p01 + f.p11 * x;
  const float den = kMpForget + s0 + s1 * x;
  if (den <= 1e-6f) return;
  const float k0 = s0 / den, k1 = s1 / den;
  const float e  = y - pred;
  f.a += k0 * e;
  f.b += k1 * e;

  const float q00 = f.p00 - k0 * s0, q01 = f.p01 - k0 * s1;
  const float q11 = f.p11 - k1 * s1;
  f.p00 = q00 / kMpForget; f.p01 = q01 / kMpForget;
  f.p11 = q11 / kMpForget;
  f.seed = true;
}

// ---------------- 主推进:1 Hz ----------------
// CUSUM 的滤波器状态从 tick() 的局部 static 提到命名空间级 —— 因为
// ★ v1.9.0 resetBaseline() 要在**换基准**那一刻把它们清掉,函数外的代码
//   访问不到局部 static。`static` 在此处表示内部链接,单文件里等价于私有。
static float    sOffFilt  = 0;     // off 的一阶低通(τ = 20 s)
static float    sMu       = 0;     // CUSUM 的慢基线(τ = 5.5 min)
static bool     sFiltInit = false;
static uint32_t sWarmSince = 0;    // 预热期起点,见 kCusumWarmMs

void resetBaseline() {
  sFiltInit = false; sOffFilt = 0; sMu = 0; sWarmSince = 0;
  g.cusum = 0; g.slow = false;
}

void tick(float off) {
  const uint32_t now = millis();
  static uint32_t lastMs = 0, hplSince = 0, hplOkSince = 0;

  // 换信源/清统计时 nmea 会把 talkerN 归零 —— 模板跟着重置,
  // 否则上一个场景学到的 a/b 会套到新场地上。
  if (nmea::talkerN == 0)
    for (int i = 0; i < nmea::kMaxTalker; ++i) fits[i] = Fit();

  if (lastMs != 0 && (now - lastMs) < 1000) return;
  lastMs = now;

  // ---- ① σ 与保护级 ----
  int n = 0;
  const float sig = sigmaUere(&n);
  g.sigma = sig;
  g.nSig  = n;

  if (nmea::st.fixValid && n >= 4 && nmea::st.hdop > 0.05f) {
    g.hpl   = kHplK * nmea::st.hdop * sig;
    g.vpl   = (nmea::st.vdop > 0.05f) ? kHplK * nmea::st.vdop * sig : 0.0f;
    g.valid = true;
  } else {
    g.hpl = 0; g.vpl = 0; g.valid = false;
  }

  // ---- ② HPL > AL 的迟滞(与判据同一组时间常数,★ v1.10.0 起都读 tune)----
  const bool over = g.valid && (g.hpl > tune::alertM());
  if (over) { if (hplSince == 0) hplSince = now; hplOkSince = 0; }
  else      { if (hplOkSince == 0) hplOkSince = now; hplSince = 0; }

  if (!g.hplBad) { if (over && hplSince && (now - hplSince >= tune::holdMs())) g.hplBad = true; }
  else           { if (!over && hplOkSince && (now - hplOkSince >= tune::recoverMs())) g.hplBad = false; }

  // ---- ③ CUSUM:单侧、向上 ----
  //  off 是**距离**(恒 ≥ 0),所以"往下漂"没有意义 —— 只做上侧这一条,别装成双侧。
  //  两级时间常数各司其职:
  //    offFilt(τ=20 s) 压噪声;mu(τ=5.5 min) 记住"平时在哪儿";
  //    两者之差才是"漂",差值减去允许起伏 k 后逐拍累积。
  if (judge::baseSet && nmea::st.fixValid) {
    if (!sFiltInit) {
      sOffFilt = off; sMu = off; sFiltInit = true;
      sWarmSince = now; g.cusum = 0; g.slow = false;
    }
    sOffFilt += kCusumAlpha * (off - sOffFilt);
    const float dev = sOffFilt - sMu;
    if ((now - sWarmSince) < kCusumWarmMs) {
      // 预热期:μ 快追,统计量锁 0(理由见 kCusumWarmMs 的注释)
      sMu += kCusumWarmA * dev;
      g.cusum = 0;
      g.slow  = false;
    } else {
      sMu += kCusumMuA * dev;
      g.cusum = fmaxf(0.0f, g.cusum + dev - kCusumSlack);
      g.slow  = (g.cusum > kCusumH) && (sOffFilt > kCusumMinM);
    }
  } else {
    sFiltInit = false; sOffFilt = 0; sMu = 0; sWarmSince = 0;
    g.cusum = 0; g.slow = false;
  }

  // ---- ④ 多径:先更新模板,再算残差 ----
  //  两遍走,是刻意的:如果把"更新"和"判定"混在一遍里,同一拍里先被更新的星
  //  会影响后面的判定顺序 —— 结果和遍历顺序有关,那就不是算法了。
  for (int ti = 0; ti < nmea::talkerN; ++ti) {
    const nmea::Talker& t = nmea::talkers[ti];
    for (int si = 0; si < t.satN; ++si) {
      const nmea::SatFix& f = t.sat[si];
      if (f.cn0 < 15 || f.el < 0) continue;
      fitUpdate(fits[ti], (float)f.el, (float)f.cn0);
    }
  }

  g.mpN = 0; g.mpMax = 0; g.mpPrn = 0; g.mpEl = -1;
  for (int ti = 0; ti < nmea::talkerN; ++ti) {
    const nmea::Talker& t = nmea::talkers[ti];
    if (!fits[ti].seed) continue;
    for (int si = 0; si < t.satN; ++si) {
      const nmea::SatFix& f = t.sat[si];
      if (f.cn0 == 0 || f.el < 0 || f.el > kMpMaxElDeg) continue;
      const float res = (float)f.cn0 - cn0Expect(fits[ti], (float)f.el);
      if (res <= tune::mpThrDb()) continue;      // ★ v1.10.0:阈值改从 tune 读(默认仍 10 dB)
      ++g.mpN;
      if (res > g.mpMax) { g.mpMax = res; g.mpPrn = f.prn; g.mpEl = f.el; }
    }
  }
}

// 这四个**不加 inline**:它们在前置声明里是普通声明,定义处再补 inline 会让
// "同一实体的声明必须一致"这条规则心里没底。函数体各一行,不在乎那点调用开销。
bool  hplBadOn() { return g.hplBad; }
bool  slowOn()   { return g.slow; }
bool  valid()    { return g.valid; }
float hpl()      { return g.hpl; }

}  // namespace integ

// ============================================================================
//  状态色唯一出处 —— 屏(星空图外圈光环 + 数据条状态词)和板载灯都从这里取色,
//  三处想不同色都难。以前屏、灯各写一份 switch,改配色漏一处就不同步,已合并。
//  ★★ v1.7.0:输入从 judge::Level 改成**状态词 phrase::PhraseId**。
//    为什么必须按词取色、不能按 Level:同一个 LV_LOST 对应两个不同的词 ——
//      开机从未定位 = NO-DATA(灰,正常等星) / 定位过又丢 = LOST(红闪,真的丢了)。
//    按 Level 取色会让"刚开机还没定位"红屏闪,看着像设备坏了。
//    函数名保持不变、只换第一个参数类型 —— 编译器会把所有调用点全报出来,漏不掉。
//  blink:红闪相位(仅 LOST 用,1 = 灭相,用页底色画)。
// ============================================================================
inline uint16_t statusColor565(phrase::PhraseId ph, uint8_t blink) {
  switch (ph) {
    case phrase::PH_OK:       return C_ST_OK;                      // #37D67A 绿 NORMAL
    case phrase::PH_RECOVER:  return C_ST_OK;                      // #37D67A 绿 恢复正常(事件日志专用)
    case phrase::PH_DEGRADED: return C_ST_DEG;                     // #F2C037 黄 DEGRADED
    case phrase::PH_SIGBAD:   return C_ST_SIG;                     // #F2792B 橙 SIG-BAD
    case phrase::PH_POSBAD:   return C_ST_POS;                     // #FF4D4D 红 POS-BAD
    case phrase::PH_LOST:     return blink ? CLR_PAPER : C_ST_POS; // 红闪 LOST(灭相 = 页底色)
    case phrase::PH_HPLHIGH:  return C_ST_HPL;                     // 紫 HPL-HIGH(★ v1.9.0)
    case phrase::PH_DRIFT:    return C_ST_DRIFT;                   // 琥珀 DRIFT(★ v1.10.1)
    case phrase::PH_WAIT:                                          // 等待数据 -> 与 SEARCH 同档
    case phrase::PH_SEARCH:   return C_ST_GREY;                    // #7A8794 灰 SEARCH
    case phrase::PH_NODATA:   return C_ST_GREY;                    // #7A8794 灰 NO-DATA
    default:                  return C_ST_GREY;
  }
}

// ============================================================================
//  灯:板载 WS2812 彩色灯,与屏上色带同一套判据
//  远处看灯、近处看屏 —— 两颗指示冗余,现场哪个角度都能看清。
// ============================================================================
namespace led {

#if !LED_USE_BUILTIN
static Adafruit_NeoPixel strip(1, LED_RGB_PIN, NEO_GRB + NEO_KHZ800);
#endif

static uint32_t lastPacked = 0xFFFFFFFF;

// 把颜色推给一颗 WS2812。
// 两条驱动路径都吃 uint8_t 的 R/G/B,亮度统一在这里按 LED_BRIGHT 缩放
// (核心内置函数没有"亮度"参数,只能自己缩;Adafruit 那条路就不重复 setBrightness 了,
//  否则会被缩两次,灯暗得看不见)。
//
// 色序:核心内置的 rgbLedWrite() 默认按 GRB 发(WS2812B 的标准色序),绝大多数板子正确。
//       若你的灯红绿反了,改用 rgbLedWriteOrdered(pin, LED_COLOR_ORDER_RGB, r, g, b);
inline void writePix(uint8_t pin, uint8_t r, uint8_t g, uint8_t b) {
  r = (uint8_t)((uint16_t)r * LED_BRIGHT / 255);
  g = (uint8_t)((uint16_t)g * LED_BRIGHT / 255);
  b = (uint8_t)((uint16_t)b * LED_BRIGHT / 255);
#if LED_USE_BUILTIN
  #if defined(ESP_ARDUINO_VERSION_MAJOR) && (ESP_ARDUINO_VERSION_MAJOR >= 3)
    rgbLedWrite(pin, r, g, b);          // 核心 3.x:官方推荐名
  #else
    neopixelWrite(pin, r, g, b);        // 核心 2.x:同一功能,旧名(3.x 仍在,仅标 deprecated)
  #endif
#else
  (void)pin;                            // Adafruit 路径只有一颗灯,脚位绑在对象里
  strip.setPixelColor(0, strip.Color(r, g, b));
  strip.show();
#endif
}

inline void setColor(uint8_t r, uint8_t g, uint8_t b) {
  uint32_t packed = ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
  if (packed == lastPacked) return;          // 颜色没变就别动硬件
  lastPacked = packed;
  writePix((uint8_t)LED_RGB_PIN, r, g, b);
}

inline void begin() {
#if !LED_USE_BUILTIN
  strip.begin();
#endif
  lastPacked = 0xFFFFFFFF;
  writePix((uint8_t)LED_RGB_PIN, 0, 0, 0);   // 上电先灭,等判据出来再亮
}

inline void apply() {
  // 与屏取同一个 statusColor565() —— 灯和星空图光环永远同色(565 展开成 888 而已)
  // ★ v1.7.0:改传**状态词**(原来传 judge::Level)—— 与光圈、数据条状态词同一入口,
  //   灯也随 5 档换色:SEARCH/NO-DATA 档灯是**灰**,不再是原来的蓝。
  uint8_t  blink = (millis() / 300) % 2;
  uint16_t c     = statusColor565(judge::res.ph, blink);
  setColor((uint8_t)((c >> 8) & 0xF8),
           (uint8_t)((c >> 3) & 0xFC),
           (uint8_t)((c << 3) & 0xF8));
}

// ★ 2026-10-11 删除 led::probe() 与串口命令 'l' —— 用户定的。
//   它当年只是"找灯脚"的一次性排查工具:候选脚挨个点 1 秒红,看哪个亮就把
//   LED_RGB_PIN 改成它。2026-09-28 已经实测确认本板灯脚 = 48(候选表第一个),
//   任务完成;而且它内部是阻塞 delay,一敲命令画面就停约 8 秒,演示现场容易被
//   误判成"死机"。留着只有风险,故连同候选脚数组一起删除。
//   (将来真换了板子、灯不亮,把 LED_RGB_PIN 改成新脚重烧即可,不需要这个探针。)

}  // namespace led

// ============================================================================
//  环境温湿度 —— SHT30-D(I2C)
//  台架上为什么要放它:晶振频率会随温度漂,机箱内的温升直接影响授时/定位的长期
//  稳定性。所以"终端自身所处的环境"本来就是这套监测系统该记的一项,不是装饰。
//  驱动直接用核心自带的 Wire —— SHT30 的 I2C 协议很短,不值得为它引入外部库。
// ============================================================================
namespace sht {

static const uint8_t kAddrA = 0x44;    // ADDR 脚接地(模块出厂默认)
static const uint8_t kAddrB = 0x45;    // ADDR 脚接 VCC
static uint8_t       addr   = kAddrA;

static float    tempC      = 0.0f;
static float    rhPct      = 0.0f;
static bool     ok         = false;  // 传感器最近是否读得通(串口报表/诊断用)
static bool     haveVal    = false;  // 是否读到过至少一次好值 —— 显示层只看这个
static bool     stale      = false;  // 数据陈旧:>6 秒没读到新值(屏上变灰提示,不消失)
static uint32_t lastGoodMs = 0;
static uint32_t lastMs     = 0;
static int      failCnt    = 0;      // 连续失败次数(驱动层,用于触发总线恢复)

// SHT30 每 2 字节数据后面跟 1 字节 CRC-8:多项式 0x31,初值 0xFF。
// 不做校验也能出数,但线一长、杜邦头一松就会读出垃圾值还看不出来,所以照手册做。
inline uint8_t crc8(const uint8_t* d, int n) {
  uint8_t c = 0xFF;
  for (int i = 0; i < n; ++i) {
    c ^= d[i];
    for (int b = 0; b < 8; ++b)
      c = (c & 0x80) ? (uint8_t)((c << 1) ^ 0x31) : (uint8_t)(c << 1);
  }
  return c;
}

inline bool addrAlive(uint8_t a) {          // 一次空写:只为看从机有没有 ACK
  Wire.beginTransmission(a);
  return Wire.endTransmission() == 0;
}

inline void busRecover() {
  // I2C 总线卡死恢复:从机把 SDA 摁低不放时,标准救法是手动给 SCL 打 9 个时钟,
  // 让它把没发完的位吐完、自己释放 SDA,然后重新初始化 Wire 控制器。
  // 触发条件在 poll():连续 3 次读取失败时做一次。杜邦线 I2C 出现"时好时坏"多半是它。
  Wire.end();
  pinMode(PIN_SHT_SCL, OUTPUT);
  digitalWrite(PIN_SHT_SCL, HIGH);
  pinMode(PIN_SHT_SDA, INPUT_PULLUP);
  for (int i = 0; i < 9; ++i) {
    digitalWrite(PIN_SHT_SCL, LOW);  delayMicroseconds(5);
    digitalWrite(PIN_SHT_SCL, HIGH); delayMicroseconds(5);
  }
  Wire.begin(PIN_SHT_SDA, PIN_SHT_SCL);
  Wire.setClock(50000);
  Serial.println("[bench] sht30 i2c bus recover");
}

// 单次测量:发命令 -> 等测量完成 -> 读 6 字节(温度 高/低/CRC + 湿度 高/低/CRC)。
// 一次 readOnce() 内部最多试 3 遍:杜邦线+内部上拉的 I2C 偶发 NACK/CRC 错很正常,
// 重试能把单次成功率从"看运气"拉到接近 100%。3 遍都错才向上报失败。
inline bool readOnce() {
  for (int attempt = 0; attempt < 3; ++attempt) {
    if (attempt) delay(5);                   // 失败后缓一下再试,给总线喘口气

    Wire.beginTransmission(addr);
    Wire.write(0x24); Wire.write(0x00);      // 高重复性、无时钟拉伸
    if (Wire.endTransmission() != 0) continue;

    delay(20);                                 // 高重复性典型 15ms。2 秒才走一次,这 20ms 无所谓

    if (Wire.requestFrom((int)addr, 6) != 6) continue;
    uint8_t d[6];
    for (int i = 0; i < 6; ++i) d[i] = (uint8_t)Wire.read();

    if (crc8(d, 2) != d[2]) continue;
    if (crc8(d + 3, 2) != d[5]) continue;

    // 手册换算:T = -45 + 175 * S_T / 65535      RH = 100 * S_RH / 65535
    uint16_t tRaw = (uint16_t)(((uint16_t)d[0] << 8) | d[1]);
    uint16_t hRaw = (uint16_t)(((uint16_t)d[3] << 8) | d[4]);
    tempC = -45.0f + 175.0f * (float)tRaw / 65535.0f;
    rhPct = 100.0f * (float)hRaw / 65535.0f;
    return true;
  }
  return false;
}

inline void begin() {
  // 必须显式给脚号:S3 的默认 I2C 是 GPIO8/9,而 9 已经被触摸 CS 占了。
  Wire.begin(PIN_SHT_SDA, PIN_SHT_SCL);
  // 50kHz:内部上拉(~45k)+杜邦线的边沿本来就慢,100kHz 是踩线的;
  // 降一半换可靠性。6 字节 @50k 约 2ms,2 秒才读一次,慢这点毫无感。
  Wire.setClock(50000);

  if      (addrAlive(kAddrA)) addr = kAddrA;
  else if (addrAlive(kAddrB)) addr = kAddrB;
  else {
    ok = false;
    Serial.println("[bench] sht30 not found on 0x44/0x45 -- 屏上温湿度显示 --");
    return;
  }

  ok = readOnce();
  if (ok) { haveVal = true; lastGoodMs = millis(); }
  Serial.printf("[bench] sht30 @0x%02X  %.1fC  %.0f%%RH  (%s)\n",
                addr, tempC, rhPct, ok ? "ok" : "read failed");
}

inline void poll() {
  uint32_t now = millis();
  if (now - lastMs < 2000) return;   // 温湿度变化慢,2 秒一次足够,不占主循环
  lastMs = now;

  if (readOnce()) {
    failCnt    = 0;
    ok         = true;
    haveVal    = true;
    lastGoodMs = now;
  } else {
    ++failCnt;
    if (failCnt == 3) busRecover();          // 6 秒读不到 -> 大概率总线卡死,救一把
    if (failCnt >= 5) ok = false;            // 串口报表里如实反映"最近读不通"
  }
  // ★ 显示层永远不翻黑:只要 haveVal 为真,屏上一直显示最后一次的好值。
  //   陈旧与否由 stale 时间判据决定(>6 秒没新值),画的时候变灰 —— 是"固定界面元素",
  //   不是"读得到才出现的浮层"。用户看到的应该是常驻的 T/RH,不该闪来闪去。
  stale = haveVal && (now - lastGoodMs > 6000);
}

inline void pollNow() {              // 串口 t:不等那 2 秒节拍,立刻读一次(现场查接线用)
  lastMs = millis();
  if (readOnce()) {
    failCnt = 0; ok = true; haveVal = true; lastGoodMs = millis();
  } else {
    ++failCnt;
  }
}

}  // namespace sht

// ============================================================================
//  第 4 段:屏显示
//  上  状态带  y 0..99     —— 状态色大色块 + 居中 32x32 中文状态词
//  边  状态框  全屏 3px    —— 左右下三边同状态色,拉开可视距离
//  中  报文窗  y 103..242  —— 最近几句 NMEA 原样滚动(自动折行,句子看全) 白底黑字
//  下  指标区  y 235..319  —— 星数 / HDOP / C/N0 中位数 / 偏离基准距离 / IP  白底黑字
//          (★ v1.5.15:顶边 245 -> 235,报文窗让出一行,最底多一行 IP)
//  配色:色带与边框 = 状态色(绿=正常 / 蓝=搜星·等待 / 红=异常 / 红闪=丢失),
//        框内一律白纸黑字 —— 像一张贴在状态灯上的白纸。
// ============================================================================
// ============================================================================
//  按星座分色 —— 主界面星空图与第二界面所有图**共用这一份**
//  (★ v1.7.0:从 namespace charts 里提出来放全局;主界面现在也画星空,两边必须同色)
// ============================================================================
inline uint16_t constellationColor(const char* id) {
  if (id[0] == 'G' && id[1] == 'P') return C_GPS;   // GPS             荧光绿
  if (id[0] == 'G' && id[1] == 'B') return C_BDS;   // 北斗(新标准)     青蓝
  if (id[0] == 'B' && id[1] == 'D') return C_BDS;   // 北斗(老写法)     同上
  return CLR_INK2;                                  // GLONASS / Galileo 用灰,不抢视觉
}

static void heapDump(const char* tag);   // 定义在下面(disp::ensureMain 要用)

// 前置声明:WiFi 台账的只读出口(定义在下面的 namespace wifi10 里)。
// ★ 为什么要单独前置 —— 单文件 .ino 的自动原型**只覆盖函数、不覆盖 namespace 成员**;
//   而 WiFi 信息卡在 disp 段(文件靠前)、wifi10 段在文件靠后,不前置编不过。
//   做成函数而不是"直接引用 wifi10::cnt / wifi10::kMax":那两个名字这里根本看不见,
//   若在前置声明里重复写一遍 kMax 就成了**两处手抄同一个常量** —— 正是本项目最忌讳的错。
namespace wifi10 { inline int savedCount(); inline int capacity(); }

namespace disp {

static TFT_eSPI tft;

// ============================================================================
//  ★★ v1.7.0 主界面整体重排 —— 读代码前先看这四条:
//  ① 版式三段式:y 0..239 星空图区 / y 240..279 按键条 / y 280..319 数据条。
//  ② **两块 sprite**:星空区 240x240(115,200B) + 底部条 240x80(38,400B),
//     合计 153,600B(= 旧 charts 全屏 sprite 同量级);进 charts 时两块都释放(互斥纪律)。
//  ③ **状态色不再铺整块**:改由星空图外圈 2px 光环承载(statusColor565 取色),
//     LOST 只让那一圈 300ms 闪 —— 旧写法每 300ms 重铺 240x100 = 24,000px 色带,省 >95%。
//  ④ **报文窗不再常驻**:长按数据条 800ms 把底部 240..319 整块换成 8 行报文(再长按切回)。
//  旧的「顶部 100px 色带 + 四周 3px 彩框 + 指标区四格 + 顶部时钟/坐标条」整套作废。
// ============================================================================

constexpr int BOT_Y = BTN_Y;                     // 底部条在屏上的 y = 240
constexpr int BOT_H = DATA_Y + DATA_H - BTN_Y;   // 240..319 = 80

static TFT_eSprite* skySpr = nullptr;            // 240x240 星空区
static TFT_eSprite* botSpr = nullptr;            // 240x80  按键条+数据条 / 报文窗
static bool msgMode = false;                     // true = 底部显示报文窗

// ---- "上次画出的内容"快照:内容没变就一个像素都不动(防闪屏纪律,沿用旧版) ----
static uint16_t cRingCol = 0xFFFF;               // 状态光圈颜色(0xFFFF 首帧必不命中)
static int      cRingPh  = -1;
static uint8_t  cRingBl  = 0xFF;
struct HudTxt { char l1[16], l2[16]; };
static HudTxt   cHud[4];                         // 四角块两行文本
static char     cWord[16]  = "";                 // 数据条状态词
static uint16_t cWordCol   = 0xFFFF;
static char     cDat[32]   = "";                 // 数据条右侧 HPL/CN0/SAT 合一串('|' 分隔三行)
                                                 // ★ v1.9.0 长度算准:"HPL 248m"=8 + '|' +
                                                 //   "CN0 32"=6 + '|' + "SAT 11/26"=10 = 26 字节,
                                                 //   留到 32 是给 "HPL 1km+" / 负号等最坏情形。手写常量
                                                 //   一旦变长就会**静默截断**(snprintf 不报错),所以留余量。
                                                 //   ⇒ 最宽 17 字符 + NUL = 18。原来给 16 ——
                                                 //   snprintf 会**静默截掉最后一位**(SAT 的颗数显示不全)。
static uint8_t  cBtnSt[4]  = {0xFF,0xFF,0xFF,0xFF};
static uint32_t cMsgSig    = 0xFFFFFFFF;
static uint8_t  cWifiSt    = 0xFF;
static uint32_t tHud = 0, tDat = 0, tWifi = 0;
static uint8_t  madctlNow  = 0;

// 报文窗行缓冲(与旧版同机制,宽度/行数随新常量走)
static char lines[MSG_ROWS][MSG_CHARS + 1];

inline void resetPaintCaches();                  // 定义在本段末尾,这里先声明

// ---------------- 屏初始化 ----------------
inline void begin() {
  tft.init();
  tft.setRotation(0);                // 若显示方向不对,改成 2
  tft.setSwapBytes(true);
  // setRotation 刚写完 MADCTL,把那个值记下来当"影子"(仅供将来排查色序问题时对照)。
  // rotation 0 非 M5STACK 的取值见 TFT_eSPI 的 ILI9341_Rotation.h case 0。
  // 色序/反显不再有运行时命令,要改请动 User_Setup.h 的 TFT_RGB_ORDER / TFT_INVERSION_ON。
  madctlNow = TFT_MAD_MX | TFT_MAD_COLOR_ORDER;

  // 背光:本项目把 BLK 直接接 3.3V,常亮,代码不用管(见配置区 TFT_BL_ON_GPIO)。
  // GPIO7 已经给了 SHT30 当 I2C 的 SCL,再往它写高电平就是和 I2C 抢线,所以不能写。
#if TFT_BL_ON_GPIO
  pinMode(PIN_TFT_BL, OUTPUT);
  digitalWrite(PIN_TFT_BL, HIGH);
#endif

  tft.fillScreen(CLR_PAPER);         // ★ v1.7.0:铺**深色**页底(原来铺白,是全屏刺眼的根源)
}

// ---------------- 两块主界面 sprite 的申请 / 释放(互斥纪律) ----------------
// 懒创建 + 互斥:主界面两块合计 153,600B,第二界面全屏 sprite 也是 153,600B,
// 两块不同时在场,峰值内存才压得住(照抄旧版 charts::enter/leave 的模式)。
inline bool ensureMain() {
  if (!skySpr) {
    skySpr = new TFT_eSprite(&tft);
    if (skySpr) {
      skySpr->setColorDepth(16);
      if (!skySpr->createSprite(SCR_W, SKY_H)) { skySpr->deleteSprite(); delete skySpr; skySpr = nullptr; }
    }
  }
  if (!botSpr) {
    botSpr = new TFT_eSprite(&tft);
    if (botSpr) {
      botSpr->setColorDepth(16);
      if (!botSpr->createSprite(SCR_W, BOT_H)) { botSpr->deleteSprite(); delete botSpr; botSpr = nullptr; }
    }
  }
  if (!skySpr || !botSpr) {
    heapDump("disp ensureMain FAIL");
    Serial.println("[bench] disp: main sprites alloc FAILED (sky/bot)");
    return false;
  }
  return true;
}

inline void releaseMain() {
  if (skySpr) { skySpr->deleteSprite(); delete skySpr; skySpr = nullptr; }
  if (botSpr) { botSpr->deleteSprite(); delete botSpr; botSpr = nullptr; }
  resetPaintCaches();
}

// ---------------- 时钟:UTC -> 本地(北京时),拆成日期 / 时间 ----------------
// 时区换算是**显示层**的责任:ZDA 只给 UTC,屏上显示北京时(否则现场第一反应是
// "这块表坏了")。改时区只动下面 CLK_TZ_HOURS 一个数(西五区写 -5),跨日会自己进位。
// 串口的 utc 字段、报文窗里的 ZDA 一律仍是**原始 UTC** —— 两边一对就知道换算对不对。
constexpr int CLK_TZ_HOURS = 8;
static bool isLeapYear(int y) { return (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0); }
static int  daysInMonth(int y, int m) {
  static const int dm[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
  if (m == 2 && isLeapYear(y)) return 29;
  return dm[m - 1];
}

// 取不到时间(还没收到 ZDA / 超过 3 秒没更新)时 ok = false。
static void localYmdHms(int& Y, int& Mo, int& D, int& h, int& mi, int& s, bool& ok) {
  bool stale = (nmea::st.utcMs == 0) || (millis() - nmea::st.utcMs > 3000);
  ok = nmea::st.utcValid && !stale;

  Y = nmea::st.uY; Mo = nmea::st.uMo; D = nmea::st.uD;
  long sod = (long)nmea::st.uH * 3600 + (long)nmea::st.uMi * 60 + (long)nmea::st.uS
           + (long)CLK_TZ_HOURS * 3600;
  while (sod < 0) {                 // 西时区:往昨天借
    sod += 86400;
    if (--D < 1) { if (--Mo < 1) { Mo = 12; --Y; } D = daysInMonth(Y, Mo); }
  }
  while (sod >= 86400) {            // 东时区:往明天进
    sod -= 86400;
    if (++D > daysInMonth(Y, Mo)) { D = 1; if (++Mo > 12) { Mo = 1; ++Y; } }
  }
  h  = (int)(sod / 3600);
  mi = (int)((sod / 60) % 60);
  s  = (int)(sod % 60);
}

// 只读出口:与屏上四角块**同源**,不另算一遍 ——
// 免得屏和串口各算各的、对不上还以为是换算错了。
inline void clockText(char* out, size_t n) {
  int Y, Mo, D, h, mi, s; bool ok;
  localYmdHms(Y, Mo, D, h, mi, s, ok);
  if (!ok) { snprintf(out, n, "****-**-** **:**:**"); return; }
  snprintf(out, n, "%04d-%02d-%02d %02d:%02d:%02d", Y, Mo, D, h, mi, s);
}

// ---------------- 四角 HUD 的文本 ----------------
// 占位串与真值**等宽**,不会因"串变短"在块里留残影(沿用旧版时钟条/坐标条那条规矩)。
static void buildHud(HudTxt o[4]) {
  // ① 左上:日期 + 时间
  int Y, Mo, D, h, mi, s; bool ok;
  localYmdHms(Y, Mo, D, h, mi, s, ok);
  if (ok) {
    snprintf(o[0].l1, sizeof(o[0].l1), "%04d-%02d-%02d", Y, Mo, D);
    snprintf(o[0].l2, sizeof(o[0].l2), "%02d:%02d:%02d", h, mi, s);
  } else {
    strcpy(o[0].l1, "****-**-**");
    strcpy(o[0].l2, "**:**:**");
  }

  // ② 右上:温度 + 湿度(★ v1.7.1:与经纬度**互换位置** —— 用户 2026-10-16 要求。
  //   附带好处:这块右端要腾 22px 给 WiFi 图标,而温湿度两行最长 8 字符 = 48px,
  //   正好不打架;经纬度 9 字符 = 54px 放到左下角同样宽松。)
  if (sht::haveVal) {
    snprintf(o[1].l1, sizeof(o[1].l1), "T %.1f C", sht::tempC);
    snprintf(o[1].l2, sizeof(o[1].l2), "RH %.0f %%", sht::rhPct);
  } else {
    strcpy(o[1].l1, "T --");
    strcpy(o[1].l2, "RH --");
  }

  // ③ 左下:纬度 + 经度(与上面的温湿度互换后落到这里)
  bool fx = nmea::st.fixValid && nmea::st.fixMs && (millis() - nmea::st.fixMs <= 3000);
  if (fx) {
    snprintf(o[2].l1, sizeof(o[2].l1), "%.4f%c", fabs(nmea::st.lat), nmea::st.lat >= 0 ? 'N' : 'S');
    snprintf(o[2].l2, sizeof(o[2].l2), "%.4f%c", fabs(nmea::st.lon), nmea::st.lon >= 0 ? 'E' : 'W');
  } else {
    strcpy(o[2].l1, "--.----");
    strcpy(o[2].l2, "---.----");
  }

  // ④ 右下:漂移 + HDOP
  if (judge::baseSet && fx) {
    float off = judge::res.offsetM;
    if (off < 1000.0f) snprintf(o[3].l1, sizeof(o[3].l1), "DRIFT %.0fm", off);
    else               snprintf(o[3].l1, sizeof(o[3].l1), "DRIFT %.0fkm", off / 1000.0f);
  } else {
    strcpy(o[3].l1, "DRIFT ---");
  }
  if (fx) {
    // 没定位时模块会把 HDOP 填成占位值 25.5(不是真实几何精度) —— 按"还没有数据"处理,
    // 与旧版指标区同一条规矩。判据层有 !fixValid 兜底。
    snprintf(o[3].l2, sizeof(o[3].l2), "HDOP %.1f", nmea::st.hdop);
  } else {
    strcpy(o[3].l2, "HDOP ---");
  }
}

static const int kHudX[4] = { HUD_XL, HUD_XR, HUD_XL, HUD_XR };
static const int kHudY[4] = { HUD_YT, HUD_YT, HUD_YB, HUD_YB };

// ---------------- WiFi 图标(★ v1.7.1:搬到**右上角 HUD 块内的右端**) ----------------
// 为什么搬:原来孤零零挂在星空区**顶部正中**那条空带(x 110..130)上,与页底同层;
//   现在四角块底色统一成 C_BLOCK,块内右端腾出 22px 空位(见 HUD_W 那条注释的依据),
//   图标嵌进块里 —— 归了位,且不占用任何新地方。
// 大小**一个字不改**(用户 2026-10-16 明确"wifi 符号按原大小,不要缩小了"):
//   仍是 4 段同心"朝上弧",最大半径 11 ⇒ 占 22x11。半径从 11 改大的话,
//   必须回头核对 HUD_W 的余量与温湿度两行文字(当前文字占 x 150..198,图标起于 212)。
// ★ 调用顺序硬约束:必须在四角块**填充之后**画,否则被块的填充整块盖掉;
//   擦除色也是因此从页底 CLR_PAPER 换成块底 C_BLOCK(画错底色会在块上留一条黑弧痕)。
static void wifiIconPaint() {
  const uint8_t  st  = (WiFi.status() == WL_CONNECTED) ? 1 : 0;
  const uint16_t col = st ? 0x064A : 0x4208;     // 0x064A = #00CB52 翠绿 / 深灰
  // 右上块 = x HUD_XR..HUD_XR+HUD_W-1 / y HUD_YT..HUD_YT+HUD_H-1
  // 水平:图标右端贴块右边距 4px -> cx = HUD_XR + HUD_W - 4 - 11 = 223(图标占 212..234)
  // 竖直:135°..225° 的弧只在上半,弧心即图标**底端** -> cy = HUD_YT + (HUD_H+11)/2 = 19
  const int16_t  cx = HUD_XR + HUD_W - 4 - 11;
  const int16_t  cy = HUD_YT + (HUD_H + 11) / 2;
  const int16_t  radii[4] = {2, 5, 8, 11};
  for (int i = 0; i < 4; ++i)
    skySpr->drawArc(cx, cy, radii[i], radii[i] - 1, 135, 225, col, C_BLOCK);
}

// 前置声明(定义在下面):收卡时要立刻重画星空 —— 见 hideWifiCard。
inline void paintSky();

// ---------------- WiFi 信息卡(★ v1.7.2:给 S3 自己的 IP 一个落脚点) ----------------
// 背景:用户 2026-10-16 深夜问"S3 的 IP 地址显示在哪里?" —— 答案是**屏上一直没有**,
//   它只作为回执发给 C3(`@s ok ip 192.168.4.40`),要看只能凑到 C3 的输出卡上。
//   而主界面三段式已排满:四角块只有 92px 宽、数据条右侧只剩 56px 领地,
//   11 字符的 IP(66px)塞不进任何固定位置 ⇒ 改成**按需弹卡**(几何见 CARD_* 常量)。
// 触发:点**右上角那颗 WiFi 图标** —— 整个右上四角块(92x24)都是热点,好按不误触。
// 三行内容(字体1,与四角块同号):
//   `SSID  <名>` / `IP    <地址>` / `SAVED n/10  RSSI -58`  (未连网时后两行给 `--` / `STA off`)
// 收卡:再点屏上任意处,或 4 秒自动。
// ★ 画法约束:**必须在 paintSky() 的 pushSprite 之前调**,直接合成进 skySpr(整块一次上屏,
//   不闪)。卡的外观照"块不描边"的纪律 —— 先铺一圈 2px 页底当"缝"再填块底色,
//   这样它压在星空圆盘上也能一眼看出是"一张卡",而不需要画边框。
static uint32_t cardUntil = 0;      // 0 = 不显示;否则是到期时刻(millis)
static bool     cardShown = false;  // 本张卡是否已经画上屏(防每拍重复标脏)

inline bool cardUp() { return cardUntil != 0; }

// 卡内一行:左侧固定 6 字符宽的"标签列"用次级灰,右边"值"用数据青。
//   标签列固定 36px 是**算出来的**:SSID / IP / SAVED 最长 5 字符 = 30px,取 6 字符对齐;
//   值那列起点 = CARD_X+CARD_TX+36,右边留 CARD_TX ⇒ 正好 20 字符(与常量区那条 static_assert 同一算术)。
static void wifiCardRow(int i, const char* label, const char* val, bool dim) {
  const int ty = CARD_Y + CARD_TY + i * CARD_LH;
  skySpr->setTextColor(CLR_INK2, C_BLOCK);
  skySpr->drawString(label, CARD_X + CARD_TX, ty);
  skySpr->setTextColor(dim ? CLR_INK2 : C_DATA, C_BLOCK);
  skySpr->drawString(val, CARD_X + CARD_TX + 36, ty);
}

static void paintWifiCard() {
  skySpr->fillRoundRect(CARD_X - 2, CARD_Y - 2, CARD_W + 4, CARD_H + 4, 7, CLR_PAPER);  // 页底缝
  skySpr->fillRoundRect(CARD_X,     CARD_Y,     CARD_W,     CARD_H,     5, C_BLOCK);    // 卡面

  skySpr->setTextFont(1);
  skySpr->setTextSize(1);
  skySpr->setTextDatum(TL_DATUM);

  const bool up = (WiFi.status() == WL_CONNECTED);
  char v1[24], v2[24], v3[24];
  if (up) {
    // SSID 最长 32 字符,卡内值列只放得下 20 ⇒ 截到 19(截断只是看不全,不报错、不崩)
    snprintf(v1, sizeof(v1), "%.19s", WiFi.SSID().c_str());
    snprintf(v2, sizeof(v2), "%s",    WiFi.localIP().toString().c_str());
    snprintf(v3, sizeof(v3), "%d/%d  RSSI %d", wifi10::savedCount(), wifi10::capacity(), (int)WiFi.RSSI());
  } else {
    strcpy(v1, "--");
    strcpy(v2, "--");
    snprintf(v3, sizeof(v3), "%d/%d  STA off", wifi10::savedCount(), wifi10::capacity());
  }
  wifiCardRow(0, "SSID",  v1, !up);
  wifiCardRow(1, "IP",    v2, !up);
  wifiCardRow(2, "SAVED", v3, false);
}

inline void hideWifiCard() {
  if (!cardUp()) return;
  cardUntil = 0;
  cardShown = false;
  Serial.println("[bench] ui: wifi card OFF");
  paintSky();                       // 立刻收卡:不能等下一拍,tick 可能半天不标脏
}

// 点右上角 WiFi 图标 -> 弹卡 / 收卡。
inline void toggleWifiCard() {
  if (cardUp()) { hideWifiCard(); return; }
  cardUntil = millis() + kCardMs;
  cardShown = false;                // 交给下一拍 tick 去画(保证与其它重绘走同一条路)
  Serial.printf("[bench] ui: wifi card ON  (ssid=%s ip=%s saved=%d/%d)\n",
                WiFi.SSID().c_str(), WiFi.localIP().toString().c_str(),
                wifi10::savedCount(), wifi10::capacity());
}

// ---------------- 星空区整帧合成 + 一次推上屏 ----------------
inline void paintSky() {
  if (!skySpr) return;
  const int cx = SKY_CX, cy = SKY_CY;

  skySpr->fillSprite(CLR_PAPER);                      // 页底
  skySpr->fillCircle(cx, cy, R_STAR, C_BLOCK);         // 圆盘(深卡面,星点在上面更跳)
  skySpr->drawCircle(cx, cy, R_STAR,         CLR_RULE);
  skySpr->drawCircle(cx, cy, R_STAR * 2 / 3, CLR_RULE);
  skySpr->drawCircle(cx, cy, R_STAR / 3,     CLR_RULE);
  skySpr->drawFastHLine(cx - R_STAR, cy, 2 * R_STAR + 1, CLR_RULE);
  skySpr->drawFastVLine(cx, cy - R_STAR, 2 * R_STAR + 1, CLR_RULE);

  // 方位标注:北在上、顺时针 东-南-西。★ 落点按 R_STAR = 87 重算 ——
  // 贴圆内侧 4px(N 在 y = cy-R_STAR+4 = 37),**不碰 88..91 那条状态光环带**。
  skySpr->setTextFont(1);
  skySpr->setTextSize(1);
  skySpr->setTextColor(C_NAV, C_BLOCK);
  skySpr->setTextDatum(TC_DATUM); skySpr->drawString("N", cx, cy - R_STAR + 4);
  skySpr->setTextDatum(BC_DATUM); skySpr->drawString("S", cx, cy + R_STAR - 4);
  skySpr->setTextDatum(ML_DATUM); skySpr->drawString("W", cx - R_STAR + 4, cy);
  skySpr->setTextDatum(MR_DATUM); skySpr->drawString("E", cx + R_STAR - 4, cy);
  skySpr->setTextDatum(TL_DATUM);

  // 每颗星:半径由仰角定、角度由方位角定、点径/颜色由 C/N0 定(算法与旧 charts 一致)。
  // ★ 北斗目前一个都不画 —— 实机 59/59 颗北斗的 GSV 不带 el/az(el<0)是**数据事实**;
  //   若日后北斗有星历了会自动出现,无需改码。
  for (int ti = 0; ti < nmea::talkerN; ++ti) {
    const nmea::Talker& tk = nmea::talkers[ti];
    uint16_t col = constellationColor(tk.id);
    for (int si = 0; si < tk.satN; ++si) {
      const nmea::SatFix& f = tk.sat[si];
      if (f.el < 0 || f.az < 0) continue;             // 该句没上报位置 -> 不画
      int   r  = R_STAR * (90 - (int)f.el) / 90;
      float a  = (float)f.az * PI / 180.0f;
      float sa = sinf(a), ca = cosf(a);
      int   x  = cx + (int)(r * sa + (sa >= 0 ? 0.5f : -0.5f));
      int   y  = cy - (int)(r * ca + (ca >= 0 ? 0.5f : -0.5f));
      int   pr = (f.cn0 >= 40) ? 3 : ((f.cn0 >= 30) ? 2 : 1);
      skySpr->fillCircle(x, y, pr, (f.cn0 == 0) ? C_DIM : col);
    }
  }

  // 四角 HUD 块(文字来自 cHud,由 tick 算好)
  // ★ v1.7.1:圆角 2 -> 4、**去掉那一圈描边**(照 C3 的规律:块只填充,靠页底缝与别处分开),
  //   字模 FONT2(16px) -> 字体1(6x8) = 用户要的"四角字体小一号",两行统一 C_DATA 青。
  //   块的**字号/行距**改了,块高就必须跟着缩(见 HUD_H 那条注释),三者是一组。
  skySpr->setTextFont(1);
  skySpr->setTextSize(1);
  for (int i = 0; i < 4; ++i) {
    const int x = kHudX[i], y = kHudY[i];
    skySpr->fillRoundRect(x, y, HUD_W, HUD_H, 4, C_BLOCK);
    skySpr->setTextColor(C_DATA, C_BLOCK);
    skySpr->drawString(cHud[i].l1, x + 4, y + 4);
    skySpr->drawString(cHud[i].l2, x + 4, y + 4 + HUD_LH);
  }

  // WiFi 图标
  wifiIconPaint();

  // ★ 状态光圈:2px 实心环 = 两圈同心,画在**最后**、压在页底上。
  //   圆盘(半径 87)与光环(90 / 91)之间那两像素(88 / 89)是页底色 ——
  //   于是光环"悬浮"在圆外,既显眼又不会和星点混淆。
  skySpr->drawCircle(cx, cy, R_RING_OUT, cRingCol);
  skySpr->drawCircle(cx, cy, R_RING_IN,  cRingCol);

  // ★ v1.7.2:WiFi 信息卡**合成在这一步之后**(压在最上层)、pushSprite 之前。
  //   它盖住的是星点与圆盘 —— 只在"点了右上角图标"的 4 秒里存在,平时一个像素都不画。
  if (cardUp() && cardShown) paintWifiCard();

  skySpr->pushSprite(0, 0);
}

// ---------------- 底部条:按键条 + 数据条 ----------------
// ★ v1.8.3:CLR-BS 长按进度(touch::poll 写,btnState/paintBot 读)。
//   0 = 没在按;1..7 = 按住计时档,每档约 714 ms,满 5 s 触发清基准。
static uint8_t gClrHoldStep = 0;
// ★ v1.10.0:CHARTS 长按进度(进参数页),同一套观感,每档约 214 ms(1.5 s 走满)。
static uint8_t gChartsHoldStep = 0;

// 按键态编码(参与"变才重画"的比对):
//   0x00 常态 / 0x01 BASE 本次设(SET,绿底) / 0x02 BASE 从 flash 恢复(SAV,青蓝底)
//   0x10 CLR-BS 常态(base 已设,键灰) / 0x11 CLR-BS 亮绿(base 为空)
//   0x21..0x27 长按清基准进行中(键灰 + 底部绿色进度条,低 3 位 = 档位)
//   0x30..0x3A ★ v1.8.6:BASE 采样中(键底中性灰 + 底部绿进度条,低 4 位 = 10 等分进度)
//   ★ BASE 与 CLR-BS 的绿**互斥**(用户 2026-10-17 明确):base 已设 -> BASE 绿、
//     CLR-BS 灰;base 为空 -> BASE 灰、CLR-BS 绿。同一时刻屏上只有一个绿键。
//     采样中的 BASE 是**中性灰** —— 那一刻基准还没定,不能提前亮绿。
static uint8_t btnState(int i) {
  switch (i) {
    case 0:
      if (judge::baseSmpOn()) return (uint8_t)(0x30 | (judge::baseSmpStep() & 0x0F));
      return judge::baseSet ? ((judge::baseSrc == judge::SRC_SAV) ? 0x02 : 0x01) : 0x00;
    case 1:
      if (!judge::baseSet) return 0x11;              // 绿:当前没有基准(提示去按 BASE)
      if (gClrHoldStep)    return (uint8_t)(0x20 | (gClrHoldStep & 0x07));
      return 0x10;                                   // 灰:有基准,可长按清
    case 2:
      // ★ v1.10.0:CHARTS 长按进参数页。低 3 位 = 档位(1..7),高 4 位 = 0x40 标记。
      //   只在长按过程中非零,所以常态恒 0x00,不影响"变才重画"的判定。
      return gChartsHoldStep ? (uint8_t)(0x40 | (gChartsHoldStep & 0x07)) : 0x00;
    default: return 0x00;
  }
}

// 数据条右侧三行(HPL / CN0 / SAT),用 '|' 拼成一条便于比对;画时拆开。
// ★ 用**字体 1(6x8)**而不是 FONT2:右侧只有 ~82px 领地,
//   FONT2 的 "SAT 15/07"(9 字符 x 8px = 72px)虽然勉强放得下,但 FONT4 状态词
//   最长 8 字符(~152px)已经把左边吃满,右侧用 6x8 才留得住余量。
// ★ v1.9.0:从两行涨到三行 —— 数据条高 40 -> 46 换来的空间正好塞下 HPL。
static void buildData(char* out, size_t n) {
  // 第一段 = HPL(完好性)。--- 表示"还算不出来"(没定位 / 参与星不足 4 颗)
  char hb[12];
  if (!integ::valid())                strcpy(hb, "HPL ---");
  else if (integ::hpl() < 1000.0f)    snprintf(hb, sizeof(hb), "HPL %.0fm", integ::hpl());
  else                                strcpy(hb, "HPL 1km+");

  if (nmea::st.cn0Ms == 0 || (millis() - nmea::st.cn0Ms) > TH_STALE_MS) {
    snprintf(out, n, "%s|CN0 --|SAT --", hb);
    return;
  }
  snprintf(out, n, "%s|CN0 %2d|SAT %2d/%2d", hb,
           (int)(nmea::st.cn0Median + 0.5f), nmea::st.satsView, nmea::st.satsUsed);
}

inline void paintBot() {
  if (!botSpr) return;

  // ★★ v1.7.2 修 —— **整块 sprite 的绘制入口必须自填页底**(与 paintSky 对齐,见文件头 ⑧)。
  //   报修(用户 2026-10-16 深夜):"状态大英文显示区域有不干净的显示,是不是显示了别的东西?"
  //   —— 是。根因就是本函数原来只在报文窗分支里 fillSprite,按键条/数据条分支**一个底都不铺**:
  //     ① 报文窗与"按键条+数据条"**共用 botSpr**:长按切报文窗、再长按切回,8 行 NMEA 原文
  //        (x 1..228 / 行高 10 / 铺满 y 0..79)**原样留着**,正好横贯数据条的状态词上下;
  //     ② 四个按键块按 fillRoundRect(x+2, 2, 56, 36) 画 ⇒ 块与块之间那 2px 页底缝、
  //        以及 y 0..1 与 38..39 两条横缝**从来没被填过**;
  //     ③ 数据条整块"只画字、不铺底" ⇒ 上一次更长的状态词(DEGRADED -> NORMAL)
  //        会在新词两侧留下字尾。
  //   代价:fillSprite 是**内存里**的一次填充(240x80 = 19,200 像素,约 0.2ms),
  //   一个字节的 SPI 流量都不增加(pushSprite 本来就是整块 240x80)
  //   ⇒ 修复后**不会变慢,也不会更闪**。
  botSpr->fillSprite(CLR_PAPER);

  // ======== 报文窗模式:底部 80px 整块让给报文 ========
  if (msgMode) {
    botSpr->setTextFont(1);
    botSpr->setTextSize(1);
    botSpr->setTextColor(CLR_INK, CLR_PAPER);
    for (int i = 0; i < MSG_ROWS; ++i) {
      if (!lines[i][0]) continue;
      botSpr->drawString(lines[i], 1, i * MSG_LH + 1);
    }
    botSpr->pushSprite(0, BOT_Y);
    return;
  }

  // ======== 按键条 y 0..39(屏 240..279)========
  // ★ v1.7.1:直角 fillRect + 一圈描边 -> **圆角 4 的填充块 + 四周 2px 页底缝**(照 C3 手法),
  //   描边整段去掉。"块贴块 + 各描一圈边"是旧观感发脏的根源(四条线挨在一起像框套框)。
  //   连带一处语义改动:BASE 的两个态原来靠**描边颜色**承载(绿框/蓝框),现在描边没了,
  //   改由**底色**承载 —— 绿 = 本次设 / 青蓝 = 上电恢复,配**深色字**(照 C3 的 MENU 键 bg=强调色+fg=页底色)。
  //   四个键几何完全一致(60x40 的格子里内缩 2px),只有底色不同,所以仍是一条规则的按钮带。
  char nmBuf[8];                                     // ★ v1.8.6:采样倒计时文字落脚处
  for (int i = 0; i < 4; ++i) {
    const int x = i * BTN_W;
    uint16_t bg = C_BLOCK, fg = CLR_INK;
    const char* nm = "";
    switch (i) {
      case 0:
        // ★ v1.8.6:P1/P2 采样中 -> 键面显示 `SET 10..0` 倒计时,键底保持中性灰
        //   (基准还没定,DON'T 提前亮绿;绿 = 已落盘这个语义不能混)。
        if (judge::baseSmpOn()) {
          const uint8_t st = judge::baseSmpStep();
          snprintf(nmBuf, sizeof(nmBuf), "SET %u",
                   (unsigned)(st > 10 ? 0u : 10u - st));
          nm = nmBuf;
        } else {
          nm = "BASE";
          if (judge::baseSet) {
            bg = (judge::baseSrc == judge::SRC_SAV) ? C_BDS : C_ST_OK;   // 青蓝=上电恢复 / 绿=本次设
            fg = CLR_PAPER;                                              // 亮底配深字
          }
        }
        break;
      case 1: nm = "CLR-BS";
        // ★ v1.8.3:INJECT 已删,这一格改为 **长按 5 s 清基准**(用户定)。
        //   绿 = 当前没有基准(与 BASE 的 SET 绿互斥,同一时刻只有一个绿键);
        //   灰 = 有基准,可以长按清。长按过程中键底保持灰,底部一条绿进度条逐档长满。
        if (!judge::baseSet) { bg = C_ST_OK; fg = CLR_PAPER; }
        break;
      case 2: nm = "CHARTS"; break;
      case 3: nm = "LOG";    break;
    }
    botSpr->fillRoundRect(x + 2, 2, BTN_W - 4, BTN_H - 4, 4, bg);
    botSpr->setTextFont(2);                            // FONT2(16px)
    botSpr->setTextSize(1);
    botSpr->setTextColor(fg, bg);
    botSpr->setTextDatum(MC_DATUM);
    botSpr->drawString(nm, x + BTN_W / 2, BTN_H / 2);
    botSpr->setTextDatum(TL_DATUM);
    // ★ v1.8.3:长按进度条 —— 52px 分 7 档,档位由 touch::poll 写进 gClrHoldStep。
    //   画在文字下方,**不压字**:★ v1.9.0 按键条 40->34 后,字占 9..25,
    //   进度条从 BTN_H-9(=25,正好贴字底)挪到 BTN_H-7(=27),留给字 2px 余量。
    if (i == 1) {
      const uint8_t st = btnState(1);
      if (st & 0x20) botSpr->fillRect(x + 4, BTN_H - 7, 52 * (st & 0x07) / 7, 3, C_ST_OK);
    }
    // ★ v1.10.0:CHARTS 长按进参数页 —— 同一几何(52px / 3px),同一绿色。
    //   1.5 s 走满,所以肉眼看是"一按就长满",暗示这一格能长按。
    if (i == 2) {
      const uint8_t st = btnState(2);
      if (st & 0x40) botSpr->fillRect(x + 4, BTN_H - 7, 52 * (st & 0x07) / 7, 3, C_ST_OK);
    }
    // ★ v1.8.6:BASE 采样进度条 —— 与 CLR-BS 那条同几何(52px / 3px 高),
    //   0..10 等分;只在采样中画。倒计时文字每秒变一次,靠 btnState(0) 的比对触发重画。
    if (i == 0 && judge::baseSmpOn()) {
      botSpr->fillRect(x + 4, BTN_H - 7, 52 * judge::baseSmpStep() / 10, 3, C_ST_OK);
    }
  }
  botSpr->drawFastHLine(0, BTN_H, SCR_W, CLR_RULE);    // 按键条与数据条的分界

  // ======== 数据条 y 34..79(屏 274..319)========
  // 左 0..152:状态词,字体 4(FONT4/Font32rle,字形高约 26px),居中
  //   ★ v1.9.0:中心 79 -> 76。"INTEG OK" / "HPL-HIGH" 都是 8 字符(~152px),
  //     中心 79 时右端到 155,与右列 156 只差 1px;挪到 76 才有 4px 余量。
  botSpr->setTextFont(4);
  botSpr->setTextSize(1);
  botSpr->setTextColor(cWordCol, CLR_PAPER);
  botSpr->setTextDatum(MC_DATUM);
  botSpr->drawString(cWord, 76, BTN_H + DATA_H / 2);
  botSpr->setTextDatum(TL_DATUM);

  // 右 156..:HPL / CN0 / SAT 三行(字体 1,行距 10)
  //   ★ v1.9.0:第一行 HPL 按**完好性**取色 —— 绿=保证成立 / 紫=超限 / 灰=还算不出。
  //     后两行维持 C_DATA 青。为什么 HPL 单独取色:它是这一屏唯一一个
  //     "说结论可不可信"的数字,和 CN0/SAT 那种纯读数不是一类东西。
  botSpr->setTextFont(1);
  botSpr->setTextSize(1);
  {
    char tmp[40], l1[16], l2[16], l3[16];
    static_assert(sizeof(tmp) >= sizeof(cDat), "tmp 装不下 cDat");
    strncpy(tmp, cDat, sizeof(tmp) - 1); tmp[sizeof(tmp) - 1] = '\0';
    char* p1 = strchr(tmp, '|');
    char* p2 = p1 ? strchr(p1 + 1, '|') : nullptr;
    if (p1) *p1 = '\0';
    if (p2) *p2 = '\0';
    snprintf(l1, sizeof(l1), "%s", tmp);
    snprintf(l2, sizeof(l2), "%s", p1 ? p1 + 1 : "--");
    snprintf(l3, sizeof(l3), "%s", p2 ? p2 + 1 : "--");

    uint16_t hc = C_ST_GREY;                                  // 还算不出来 = 灰
    if (integ::valid()) hc = integ::hplBadOn() ? C_ST_HPL : C_ST_OK;
    botSpr->setTextColor(hc,     CLR_PAPER); botSpr->drawString(l1, 156, BTN_H + 8);
    botSpr->setTextColor(C_DATA, CLR_PAPER); botSpr->drawString(l2, 156, BTN_H + 18);
    botSpr->setTextColor(C_DATA, CLR_PAPER); botSpr->drawString(l3, 156, BTN_H + 28);
  }

  botSpr->pushSprite(0, BOT_Y);
}

// ---------------- 报文窗内容(自动折行,句子看全) ----------------
static void buildLines() {
  // 从最新一句开始,按 MSG_CHARS 字符一截拆开,从屏底往上摆
  int out = MSG_ROWS;
  for (int back = 0; back < gnss::stored && out > 0; ++back) {
    const char* l = gnss::recent(back);
    if (!l) break;
    int len = (int)strlen(l);
    int nch = (len + MSG_CHARS - 1) / MSG_CHARS;
    if (nch == 0) nch = 1;
    for (int c = nch - 1; c >= 0 && out > 0; --c) {
      --out;
      int start = c * MSG_CHARS;
      int n     = len - start;
      if (n > MSG_CHARS) n = MSG_CHARS;
      memcpy(lines[out], l + start, n);
      lines[out][n] = '\0';
    }
  }
  for (int i = 0; i < out; ++i) lines[i][0] = '\0';
}

// 收到新句子才重画。签名 = totalLines 低 16 位 + stored 低 16 位(无乘法、无溢出;
// 旧版用 totalLines * 1000003u 会在 uint32 里溢出撞签名值,已改)。
inline void drawMessages() {
  uint32_t sig = ((gnss::totalLines & 0xFFFFu) << 16) | (gnss::stored & 0xFFFFu);
  if (sig == cMsgSig) return;
  cMsgSig = sig;
  buildLines();
  if (msgMode) paintBot();      // 只有报文窗开着才需要立刻上屏;关着时留给 tick 一起画
}

// 长按数据条 -> 在「按键条+数据条」与「报文窗」之间切换(触摸层调)。
inline void toggleMsg() {
  msgMode = !msgMode;
  if (msgMode) {
    buildLines();
    cMsgSig = ((gnss::totalLines & 0xFFFFu) << 16) | (gnss::stored & 0xFFFFu);
  }
  resetPaintCaches();
  if (!ensureMain()) return;
  paintSky();
  paintBot();
  Serial.printf("[bench] 底部条 -> %s\n", msgMode ? "报文窗(8 行)" : "按键条 + 数据条");
}

inline bool inMsgMode() { return msgMode; }

// ---------------- 主界面刷新入口(主循环每 200ms 调一次) ----------------
inline void tick() {
  if (!ensureMain()) return;
  const uint32_t now = millis();
  bool sky = false, bot = false;

  // ---- ① 状态光圈:变色 / 换词 / 闪相变化才重画那一圈(2 圈同心 ≈ 1056 px)----
  //   稳态 0 次/秒;LOST 红闪时 300ms 一次。去重纪律与旧版色带一致。
  const uint8_t  blink = (judge::res.ph == phrase::PH_LOST) ? ((now / 300) % 2) : 0;
  const uint16_t rc    = statusColor565(judge::res.ph, blink);
  if (rc != cRingCol || (int)judge::res.ph != cRingPh || blink != cRingBl) {
    cRingCol = rc; cRingPh = (int)judge::res.ph; cRingBl = blink;
    sky = true;
  }

  // ---- ② 数据条状态词(与光圈**同一个取色函数** -> 词与环永远同色)----
  const char*    w  = phrase::text(judge::res.ph);
  const uint16_t wc = statusColor565(judge::res.ph, 0);
  if (strcmp(w, cWord) != 0 || wc != cWordCol) {
    strncpy(cWord, w, sizeof(cWord) - 1); cWord[sizeof(cWord) - 1] = '\0';
    cWordCol = wc;
    bot = true;
  }

  // ---- ③ 四角 HUD ----
  //   首帧 / 缓存作废后先算一次(否则第一帧会画出四个空块);之后 1Hz 刷新。
  if (cHud[0].l1[0] == '\0') {
    HudTxt h[4]; buildHud(h);
    if (memcmp(h, cHud, sizeof(h)) != 0) { memcpy(cHud, h, sizeof(h)); sky = true; }
    tHud = now;
  } else if (now - tHud >= 1000) {
    tHud = now;
    HudTxt h[4]; buildHud(h);
    if (memcmp(h, cHud, sizeof(h)) != 0) { memcpy(cHud, h, sizeof(h)); sky = true; }
  }

  // ---- ④ 数据条右侧 CN0 / SAT(1Hz;报文窗开着时不画)----
  if (!msgMode && (now - tDat >= 1000)) {
    tDat = now;
    char d[sizeof(cDat)]; buildData(d, sizeof(d));
    if (strcmp(d, cDat) != 0) { strcpy(cDat, d); bot = true; }
  }

  // ---- ⑤ 按键态(BASE 的 SET/SAV、CLR-BS 的"绿=无基准"、两处长按进度)----
  //   ★ v1.10.0:从 2 项扩到 4 项 —— CHARTS 也有长按进度了(btnState(2))。
  //     数组本来就是 4 字节,只是以前只比前 2 个;现在四个都参与比对。
  //     漏改的后果很典型:长按时进度条**不长**(值变了但没人通知重画)。
  if (!msgMode) {
    uint8_t bs[4] = { btnState(0), btnState(1), btnState(2), btnState(3) };
    if (memcmp(bs, cBtnSt, 4) != 0) { memcpy(cBtnSt, bs, 4); bot = true; }
  }

  // ---- ⑥ WiFi 图标(1Hz 巡检,状态真的变了才重画)----
  if (now - tWifi >= 1000) {
    tWifi = now;
    const uint8_t ws = (WiFi.status() == WL_CONNECTED) ? 1 : 0;
    if (ws != cWifiSt) { cWifiSt = ws; sky = true; }
  }

  // ---- ⑦ WiFi 信息卡(★ v1.7.2:点右上角图标弹出,到点自动收)----
  //   两条状态合在这里管:① "刚弹出还没画" -> 标脏一次把它合成上屏;
  //   ② "到点了" -> 清状态 + 标脏一次把它擦掉(下一拍 paintSky 不带卡,自然就没了)。
  if (cardUp()) {
    if ((int32_t)(millis() - cardUntil) >= 0) {
      cardUntil = 0; cardShown = false; sky = true;
      Serial.println("[bench] ui: wifi card OFF (timeout)");
    } else if (!cardShown) {
      cardShown = true; sky = true;
    }
  }

  if (sky) paintSky();
  if (bot) paintBot();
}

// ---------------- 缓存全作废(让下一拍整屏重画一遍) ----------------
inline void resetPaintCaches() {
  cRingCol = 0xFFFF; cRingPh = -1; cRingBl = 0xFF;
  memset(cHud, 0, sizeof(cHud));
  cWord[0]  = '\0'; cWordCol = 0xFFFF;
  cDat[0]   = '\0';
  cBtnSt[0] = cBtnSt[1] = cBtnSt[2] = cBtnSt[3] = 0xFF;
  cMsgSig   = 0xFFFFFFFF;
  cWifiSt   = 0xFF;
  tHud = tDat = tWifi = 0;
  // ★ v1.7.2:WiFi 信息卡也一起收掉 —— 本函数的语义是"下一拍整屏重画一遍",
  //   留着 cardUntil / cardShown 会让卡片在"切页来回一趟"之后莫名其妙又冒出来。
  cardUntil = 0; cardShown = false;
}

}  // namespace disp

// ============================================================================
//  第二界面「动图页」(★ 2026-10-06 新增)
// ----------------------------------------------------------------------------
//  为什么要"换页":主屏 240x320 已经排满 —— 色带 100px(时钟 / 状态大字 / 坐标条)、
//  报文窗 130px(13 行)、指标区 85px。再加动图只能砍掉别的东西,砍哪个都是损失。
//  改成换页:点报文区 -> 清屏进这一页**独占全屏**,动图全放这儿;点任意处回主界面。
//  两块界面不同时在场,连内存都能错峰 —— 这是这个设计最大的好处。
//
//  画法:整页一块 240x320 的 16bpp sprite(153,600 B),1Hz 全帧内存合成后
//  一次 pushSprite 推上屏。为什么敢用"全屏一块":
//    · 合成期间屏幕一个像素都不动 -> 绝无撕裂、无闪烁(和报文窗同一个原理);
//    · 布局随便排,不用操心"哪块压着哪块";
//    · 153,600 B 的代价用"进页前先归还报文窗那块 65,520 B"抵消 ——
//      两块 sprite 不同时存在,峰值内存比"同时开几块小的"还低。
//  刷新率 1Hz:和模块 GSV/GGA 的出数节奏一致,再快也没有新数据。
//
//  四张图(都是会动的,答辩时一眼能看懂):
//    ① 星空图     —— 方位 x 仰角极坐标,一颗星一个点,色 = C/N0 强弱
//    ② 每星竖条   —— 一颗星一根条,按星座分组,高低起伏最直观
//    ③ 漂移散点   —— 相对基准的点云,看"这套设备稳不稳"
//    ④ 双星座趋势 —— GPS / 北斗两条中位线 + 25 dB-Hz 阈值线(主界面搬来的升级版)
//
//  朝向约定(星空图):方位角 0° 在正上方(北),顺时针增大 —— 这就是 NMEA 的
//  定义,它本来就是相对**真北**的,所以画星空图**不需要磁力计**。
//  仰角 90° 落在圆心(天顶),0° 落在圆周(地平线)。
//
//  屏上文字一律用英文:内置字体只有 ASCII,中文得走 cnfont 的 32x32 字模,
//  那是给顶部状态大字专用的,这里不占用。
// ============================================================================
// ============================================================================
// ★ v1.5.16 内存探针 —— **只在 charts 全屏 sprite 申请失败那一刻**打一行。
//   正常路径一次都不调,平时串口里看不到它;一旦真出问题,这行能立刻区分
//   "总量不够"还是"连续块不够(=碎片)" —— 见文件头 v1.5.16 的 ②。
//   largest = 当前**最大连续**可用于 malloc 的内部 RAM 块;charts 要 153,600 B,
//   所以这个数才是关键,光看 free 会误判。
// ============================================================================
static void heapDump(const char* tag) {
  Serial.printf("[heap] %-20s free=%u  largest=%u\n", tag,
                (unsigned)ESP.getFreeHeap(),
                (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
}

namespace charts {

constexpr int      CW = SCR_W;         // 240
constexpr int      CH = SCR_H;         // 320
constexpr uint32_t PERIOD_MS = 1000;   // 刷新率 1Hz

// ---- 布局(★ v1.9.0:漂移散点整块删掉,换成两张算法卡)----
//   y   0..27    顶条 28px:左 "CHARTS",右 BACK 按钮 64x24
//   y  28..91    ① 每星 C/N0 竖条   (234 x 64)
//   y  94..157   ② C/N0 中位曲线     (234 x 64)
//   y 160..235   ③ INTEGRITY 保护级  (234 x 76)
//   y 238..316   ④ DETECTORS  趋势/多径(234 x 79)
//  ★ 为什么删 ③' 定位漂移散点(用户 2026-10-17 原话:"那种看起来不是很直观,
//    而且平时也不会漂移,那个作用不大")—— 它把 ±5 m 的抖动画成圆心附近一团,
//    唯一能读的信息是"没漂",而这件事状态词早就说了。空间让给**能报名字**的两个算法。
//  ★★ v1.10.1 重新配平(用户原话:"上面柱状图和曲线图占的位置很多,中间的间隔很开,
//    下面新加进去的东西却挤在一个很小的空间里")。这一版把**高度**按内容重分:
//      两张图 80 → 64 (各让 16)     ③ 61 → 76 (+15)     ④ 61 → 79 (+18)
//    改前 ④ 的 6 行字挤在 61px 里(行距 9~10),最后一行离卡底只剩 2px;
//    改后行距 11~13px。**不是"整体缩小",是按各行实际需要分配**。
//    另外 ③ 多了一行 `OFF .. / LIM ..` —— 为了回答"为什么报 POS-BAD"这件事:
//    原来卡上只有 HPL 对 AL,没有"距离对门限",现场看不出是哪条判据在报警。
constexpr int TOP_H  = 28;
constexpr int CARD_X = 3;
constexpr int CARD_W = SCR_W - 6;                 // 234
constexpr int BAR_Y  = 28,  BAR_H = 64;
constexpr int TRD_Y  = 94,  TRD_H = 64;
constexpr int INT_Y  = 160, INT_H = 76;
constexpr int DET_Y  = 238, DET_H = 79;
// 四条互不重叠、且不压屏底 —— 改任何一个数都要先过这两条
static_assert(BAR_Y + BAR_H <= TRD_Y - 2,      "① 与 ② 之间至少要留 2px 卡片缝");
static_assert(TRD_Y + TRD_H <= INT_Y - 2,      "② 与 ③ 之间至少要留 2px 卡片缝");
static_assert(INT_Y + INT_H <= DET_Y - 2,      "③ 与 ④ 之间至少要留 2px 卡片缝");
static_assert(DET_Y + DET_H < SCR_H, "第二界面最下面那张卡压到屏底了");
// BACK 按钮几何已提到配置区(BACK_X / BACK_Y / BACK_W / BACK_H)——
// 第二/三/四界面共用同一组,触摸判定(touch::poll)同源比对,改配置区一处即可。
constexpr int BACK_TOP = BACK_Y;                  // 顶栏里 BACK 的 y(三处一致)

// ---- 配色:★ v1.7.0 全部改用**全局深色主题常量**(主界面/第二界面同一套,不再各定义一份)----
//   用到的:CLR_PAPER(页底) / C_BLOCK(卡面) / CLR_RULE(参考线) / CLR_INK(主文字)
//           CLR_INK2(次级文字) / C_GPS(绿) / C_BDS(青蓝) / C_WARN(阈值线) / C_DIM(淡点)

// ---- 星座图例:两张卡共用同一套写法与落点 ----
constexpr int LEG_RX = CARD_X + CARD_W - 5;       // 图例右端 x = 232
constexpr int LEG_DX = 24;                        // "BDS" 宽 18px + 间隙 6px

// ---- 历史缓冲:只留趋势曲线用的 120 拍(★ v1.9.0:散点的 2x60 个 int16 已删,省 240 B)----
constexpr int TRD_N = 120;             // 趋势曲线 120 拍 ≈ 2 分钟
static uint8_t trdGps[TRD_N];
static uint8_t trdBds[TRD_N];
static int     trdHead = 0;
static bool    trdFull = false;

static TFT_eSprite* spr = nullptr;

// (按星座分色 constellationColor 已提到全局 —— 主界面星空图与这里共用一份)

inline void histReset() {
  for (int i = 0; i < TRD_N; ++i) { trdGps[i] = 0; trdBds[i] = 0; }
  trdHead = 0; trdFull = false;
}

// ---- 进出页(★ v1.7.0:与主界面两块 sprite 互斥 —— 154KB 只有一份)----
inline bool enter() {
  if (spr) return true;
  disp::releaseMain();                   // 先腾地方(主界面两块 153,600 B)
  spr = new TFT_eSprite(&disp::tft);
  if (spr) {
    spr->setColorDepth(16);
    if (!spr->createSprite(CW, CH)) { spr->deleteSprite(); delete spr; spr = nullptr; }
  }
  if (!spr) {                            // 申请不到:把主界面还回去,留在主界面
    disp::ensureMain();
    heapDump("charts enter FAIL");
    Serial.println("[bench] charts: full-screen sprite alloc FAILED -> stay on main page");
    return false;
  }
  spr->setTextSize(1);
  histReset();
  return true;
}

inline void leave() {
  if (spr) { spr->deleteSprite(); delete spr; spr = nullptr; }
  disp::ensureMain();                    // 主界面两块建回来(tick 会自己整屏重画)
  disp::resetPaintCaches();
}

// ---- 1Hz 采样:把这一拍的双星座中位 C/N0 塞进环形缓冲 ----
//  ★ v1.9.0:散点那半段(东/北偏移)已删 —— 图没了,数据也不用算了。
//    完好性/CUSUM/多径**不在这里采样**:它们跑在 judge::update() 里,
//    与页面无关地一直推进(否则"只在第二页才算"就成了另一种 bug)。
inline void sample() {
  float g = nmea::medianOf("GP");
  if (g <= 0.0f) g = nmea::st.cn0Median;  // 兜底:模块若发 GNGSV(混合),就用总体中位
  float b = nmea::bdsMedian();
  trdGps[trdHead] = (g <= 0.0f) ? 0 : (uint8_t)(g >= 99.0f ? 99 : (g + 0.5f));
  trdBds[trdHead] = (b <= 0.0f) ? 0 : (uint8_t)(b >= 99.0f ? 99 : (b + 0.5f));
  trdHead = (trdHead + 1) % TRD_N;
  if (trdHead == 0) trdFull = true;
}

// ---- ① 每星 C/N0 竖条(按星座分组,不排序 —— 分组本身就是要看的东西)----
inline void drawBars() {
  spr->fillRoundRect(CARD_X, BAR_Y, CARD_W, BAR_H, 3, C_BLOCK);

  const int pad   = 8;
  const int baseY = BAR_Y + BAR_H - 12;      // 基线(底部留 12px 写轴标)
  const int maxH  = baseY - (BAR_Y + 5);

  // 25 dB-Hz 参考线 —— 判据 TH_MIN_CN0_MEDIAN 用的就是这条线
  spr->drawFastHLine(CARD_X + pad, baseY - 25 * maxH / 50, CARD_W - 2 * pad, C_WARN);

  int total = 0;
  for (int ti = 0; ti < nmea::talkerN; ++ti) total += nmea::talkers[ti].satN;

  if (total > 0) {
    const int inner = CARD_W - 2 * pad;
    int pitch = inner / total;
    if (pitch > 14) pitch = 14;              // 星少时别把条拉得太胖
    int bw = pitch - 2;
    if (bw < 2) bw = 2;

    int x = CARD_X + pad;
    for (int ti = 0; ti < nmea::talkerN; ++ti) {
      const nmea::Talker& t = nmea::talkers[ti];
      uint16_t col = constellationColor(t.id);
      for (int si = 0; si < t.satN; ++si) {
        int h = (int)t.sat[si].cn0 * maxH / 50;   // 50 dB-Hz 视作满格
        if (h > maxH) h = maxH;
        if (h < 1)    h = 1;
        spr->fillRect(x, baseY - h, bw, h, col);
        x += pitch;
      }
    }
  }

  spr->drawFastHLine(CARD_X + pad, baseY + 1, CARD_W - 2 * pad, CLR_INK);
  spr->setTextColor(CLR_INK2, C_BLOCK);
  spr->drawString("C/N0 per sat", CARD_X + pad, baseY + 3);
  // 图例:绿 GPS / 青蓝 BDS(与趋势图 ② 同一套,落点共用 LEG_RX/LEG_DX)
  spr->setTextDatum(TR_DATUM);
  spr->setTextColor(C_BDS, C_BLOCK);
  spr->drawString("BDS", LEG_RX, baseY + 3);
  spr->setTextColor(C_GPS, C_BLOCK);
  spr->drawString("GPS", LEG_RX - LEG_DX, baseY + 3);
  spr->setTextDatum(TL_DATUM);
}

// ---- ② 双星座 C/N0 趋势 ----
inline int trendRow(int v) {                 // 纵轴 15..50 dB-Hz
  const int y0 = TRD_Y + 6, y1 = TRD_Y + TRD_H - 14;
  if (v <= 15) return y1;
  if (v >= 50) return y0;
  return y1 - (v - 15) * (y1 - y0) / 35;
}

inline void drawTrendOne(const uint8_t* buf, uint16_t col) {
  int n     = trdFull ? TRD_N : trdHead;
  int start = trdFull ? trdHead : 0;
  const int x0 = CARD_X + 4;
  const int w  = CARD_W - 8;
  int px = -1, py = 0;
  for (int k = 0; k < n; ++k) {
    int v = buf[(start + k) % TRD_N];
    if (v == 0) { px = -1; continue; }       // 该拍没数据 -> 断开,不画假连线
    int x = x0 + k * w / TRD_N;
    int y = trendRow(v);
    if (px < 0)      spr->fillRect(x, y, 1, 1, col);
    else if (x > px) spr->drawLine(px, py, x, y, col);
    px = x; py = y;
  }
}

inline void drawTrend() {
  spr->fillRoundRect(CARD_X, TRD_Y, CARD_W, TRD_H, 3, C_BLOCK);
  const int x0 = CARD_X + 4;
  const int w  = CARD_W - 8;

  spr->drawFastHLine(x0, trendRow(40), w, CLR_RULE);   // 40 dB-Hz 参考(淡)
  spr->drawFastHLine(x0, trendRow(25), w, C_WARN);     // 25 dB-Hz 阈值(醒目)

  drawTrendOne(trdGps, C_GPS);
  drawTrendOne(trdBds, C_BDS);

  spr->setTextColor(CLR_INK2, C_BLOCK);
  spr->drawString("C/N0 median", CARD_X + 5, TRD_Y + TRD_H - 11);
  spr->setTextDatum(TR_DATUM);
  spr->setTextColor(C_BDS, C_BLOCK);
  spr->drawString("BDS", LEG_RX, TRD_Y + TRD_H - 11);
  spr->setTextColor(C_GPS, C_BLOCK);
  spr->drawString("GPS", LEG_RX - LEG_DX, TRD_Y + TRD_H - 11);
  spr->setTextDatum(TL_DATUM);
}

// ---- ③ 完好性:保护级横条 + 数值(★ v1.9.0 新增,顶掉原来的漂移散点)----
//  这张卡的读法:横条是 HPL,中间那条竖线是告警限 AL。
//  **条子没越过竖线 = 此刻的结论可信**;越过了 = 连"报警"都不该信。
inline void drawInteg() {
  spr->fillRoundRect(CARD_X, INT_Y, CARD_W, INT_H, 3, C_BLOCK);
  const int pad = 6;
  const int x0  = CARD_X + pad;
  const int w   = CARD_W - 2 * pad;
  const int by  = INT_Y + 17, bh = 9;
  char b[44];

  spr->setTextColor(CLR_INK2, C_BLOCK);
  spr->drawString("INTEGRITY (est)", x0, INT_Y + 4);
  spr->setTextDatum(TR_DATUM);
  spr->setTextColor(C_WARN, C_BLOCK);
  // ★ v1.10.0:AL 不再是写死的 "30m" —— 它现在能在 PARAMS 页改,
  //   屏上这一行必须跟着变,**否则"改完没反应"就是这么来的**。
  snprintf(b, sizeof(b), "AL %.0fm", tune::alertM());
  spr->drawString(b, CARD_X + CARD_W - pad, INT_Y + 4);
  spr->setTextDatum(TL_DATUM);

  // 横条量程 0..2*AL ⇒ AL 正好落在正中,"离警戒线还有多远"一眼可读。
  spr->fillRect(x0, by, w, bh, CLR_RULE);
  if (integ::valid()) {
    int fw = (int)(integ::hpl() / (2.0f * tune::alertM()) * (float)w);
    if (fw > w) fw = w;
    if (fw < 1) fw = 1;
    spr->fillRect(x0, by, fw, bh, integ::hplBadOn() ? C_ST_HPL : C_ST_OK);
  }
  spr->drawFastVLine(x0 + w / 2, by - 2, bh + 4, C_WARN);       // AL 刻线

  if (integ::valid()) snprintf(b, sizeof(b), "HPL %4.1fm  VPL %4.1fm", integ::hpl(), integ::g.vpl);
  else                snprintf(b, sizeof(b), "HPL  ---   VPL  ---");
  spr->setTextColor(integ::hplBadOn() ? C_ST_HPL : CLR_INK, C_BLOCK);
  spr->drawString(b, x0, INT_Y + 28);

  // ★★ v1.10.1 新增:距离 vs 门限。**这一行就是"为什么报 POS-BAD"的答案**
  //   上面 HPL 那行回答"这个解可不可信";这一行回答"到底偏了多少、跟谁比"。
  //   没有它的时候,现场只能看到状态词,看不出是被距离判据还是被慢漂移点着的
  //   (2026-10-17 用户当场就问了这个)。
  if (judge::baseSet) {
    const float lim = tune::posLim();
    const float ofm = judge::res.offsetM;
    snprintf(b, sizeof(b), "OFF %4.1fm  LIM %.0fm", ofm, lim);
    uint16_t oc = CLR_INK;
    if (ofm > lim)            oc = C_ST_POS;      // 越线 = 事实级,红
    else if (integ::slowOn()) oc = C_ST_DRIFT;    // 没越线但在漂 = 趋势级,琥珀
    spr->setTextColor(oc, C_BLOCK);
  } else {
    snprintf(b, sizeof(b), "OFF  ---  (no base)");
    spr->setTextColor(CLR_INK2, C_BLOCK);
  }
  spr->drawString(b, x0, INT_Y + 41);

  // 状态词与主界面**同源**(phrase::text + statusColor565),不另起一套说法
  spr->setTextColor(statusColor565(judge::res.ph, 0), C_BLOCK);
  snprintf(b, sizeof(b), "STATE  %s", phrase::text(judge::res.ph));
  spr->drawString(b, x0, INT_Y + 53);

  spr->setTextColor(CLR_INK2, C_BLOCK);
  // n 是**真正参与 σ 统计的星数**:与 sat 使用数一致 = 走的是 GSA 星表;
  // 比它大 = 退回了"全部可见星"(见 sigmaUere 的两级回退)。串口/现场都能据此判断。
  if (nmea::st.gsaCount)
    snprintf(b, sizeof(b), "PDOP %.1f VDOP %.1f  sig %.1fm n%d",
             nmea::st.pdop, nmea::st.vdop, integ::g.sigma, integ::g.nSig);
  else
    snprintf(b, sizeof(b), "PDOP --- VDOP ---  sig %.1fm n%d",
             integ::g.sigma, integ::g.nSig);
  spr->drawString(b, x0, INT_Y + 65);
}

// ---- ④ 检测器:CUSUM(慢漂移)+ 多径(CN0–仰角模板残差)(★ v1.9.0 新增)----
inline void drawDetect() {
  spr->fillRoundRect(CARD_X, DET_Y, CARD_W, DET_H, 3, C_BLOCK);
  const int pad = 6;
  const int x0  = CARD_X + pad;
  const int w   = CARD_W - 2 * pad;
  char b[44];

  spr->setTextColor(CLR_INK2, C_BLOCK);
  spr->drawString("DETECTORS", x0, DET_Y + 5);
  spr->setTextDatum(TR_DATUM);
  spr->setTextColor(integ::slowOn() ? C_ST_DRIFT : C_ST_OK, C_BLOCK);
  spr->drawString(integ::slowOn() ? "SLOW-DRIFT" : "OK", CARD_X + CARD_W - pad, DET_Y + 5);
  spr->setTextDatum(TL_DATUM);

  // CUSUM 条:0..H 线性;过 H(条子满)就是"开始漂了"
  spr->setTextColor(CLR_INK, C_BLOCK);
  snprintf(b, sizeof(b), "CUSUM %4.1f / %.1f", integ::g.cusum, kCusumH);
  spr->drawString(b, x0, DET_Y + 18);

  const int by = DET_Y + 29, bh = 5;
  spr->fillRect(x0, by, w, bh, CLR_RULE);
  int fw = (int)(integ::g.cusum / kCusumH * (float)w);
  if (fw > w) fw = w;
  if (fw > 0) spr->fillRect(x0, by, fw, bh, integ::slowOn() ? C_ST_POS : C_DATA);

  // 多径:只报"低仰角却比模板明显更强"的那几颗。
  // 没有可疑星时**显式写 thr**,否则屏上只有一句 "MP none",看的人不知道
  // "none" 是"没有多径"还是"这个功能没在跑"。
  if (integ::g.mpN) snprintf(b, sizeof(b), "MP  n%d  max %+.1fdB", integ::g.mpN, integ::g.mpMax);
  else              snprintf(b, sizeof(b), "MP  none  (thr %.0fdB)", tune::mpThrDb());
  spr->setTextColor(integ::g.mpN ? C_ST_SIG : CLR_INK2, C_BLOCK);
  spr->drawString(b, x0, DET_Y + 39);

  if (integ::g.mpN) snprintf(b, sizeof(b), "PRN%d  el %d", integ::g.mpPrn, integ::g.mpEl);
  else              strcpy(b, "no multipath");
  spr->setTextColor(CLR_INK2, C_BLOCK);
  spr->drawString(b, x0, DET_Y + 52);

  // 最后一行:让看的人能自己判断"这些结论是建立在什么几何上"
  snprintf(b, sizeof(b), "fix %s  used %d  gsa %lu",
           nmea::st.gsaFix == 3 ? "3D" : (nmea::st.gsaFix == 2 ? "2D" : "--"),
           nmea::st.satsUsed, (unsigned long)nmea::st.gsaCount);
  spr->drawString(b, x0, DET_Y + 65);
}

// ---- 面板通道(@CHT):把这一拍的数据发给电脑端演示面板 ----
//   @CHT,T(每 talker 一行): 每星 方位角,仰角,C/N0;
//   @CHT,S(汇总一行):       trend 这一拍两个中位 + **完好性一组**(★ v1.9.0 改了后四个字段)
//   面板自己按拍攒历史(TRD_N 同参数),图上效果与屏一致。
//   ★ v1.9.0 字段变更(面板脚本必须同步改,否则后四列会读成东/北偏移):
//        @CHT,S,<gps中位>,<bds中位>,<HPL 0.1m>,<sigma 0.1m>,<CUSUM 0.1>,<mpMax 0.1dB>
//        HPL 为 -1 表示"当前算不出保护级"。
//   塞不下整拍不发(与串口写超时同规矩);不插电脑时零开销。
inline void emitForPanel() {
  if (Serial.availableForWrite() <= 0) return;
  for (int ti = 0; ti < nmea::talkerN; ++ti) {
    const nmea::Talker& t = nmea::talkers[ti];
    if (t.satN <= 0) continue;
    Serial.printf("@CHT,T,%s,%d", t.id, t.satN);
    for (int si = 0; si < t.satN; ++si) {
      const nmea::SatFix& f = t.sat[si];
      Serial.printf(",%d,%d,%d", (int)f.az, (int)f.el, (int)f.cn0);
    }
    Serial.print('\n');
  }
  int tg = (trdHead + TRD_N - 1) % TRD_N;      // sample() 刚写入的那一拍
  Serial.printf("@CHT,S,%d,%d,%d,%d,%d,%d\n",
                (int)trdGps[tg], (int)trdBds[tg],
                (int)(integ::valid() ? integ::hpl() * 10.0f : -1.0f),
                (int)(integ::g.sigma * 10.0f),
                (int)(integ::g.cusum * 10.0f),
                (int)(integ::g.mpMax  * 10.0f));
}

// ---- 整页合成 + 一次推上屏 ----
inline void tick() {
  if (!spr) return;
  sample();

  spr->fillSprite(CLR_PAPER);

  // ---- 顶条:左页名 + 右 BACK 按钮(★ v1.7.0:BACK 替代"点哪都回主界面")----
  spr->fillRect(0, 0, CW, TOP_H, C_BLOCK);
  spr->setTextFont(2);
  spr->setTextSize(1);
  spr->setTextColor(CLR_INK, C_BLOCK);
  spr->setTextDatum(ML_DATUM);
  spr->drawString("CHARTS", 6, TOP_H / 2);

  // ★ v1.7.1:BACK 改"凹槽式" —— 在 C_BLOCK 顶栏上挖一块**页底色**的圆角块。
  //   原来靠"比顶栏亮一档的底色 + 一圈描边"来显形,现在描边整段去掉(照 C3),
  //   底色又与顶栏同色,不挖成页底就跟顶栏糊在一起、按钮直接消失。
  spr->fillRoundRect(BACK_X, BACK_Y, BACK_W, BACK_H, 4, CLR_PAPER);
  spr->setTextColor(C_DATA, CLR_PAPER);
  spr->setTextDatum(MC_DATUM);
  spr->drawString("BACK", BACK_X + BACK_W / 2, BACK_Y + BACK_H / 2);
  spr->setTextDatum(TL_DATUM);
  spr->setTextFont(1);

  drawBars();
  drawTrend();
  drawInteg();
  drawDetect();

  spr->pushSprite(0, 0);       // 到这一行为止,屏幕一个像素都没动过
  emitForPanel();              // 同一拍的数据发给演示面板(大屏投影画同款动图)
}

}  // namespace charts

// ============================================================================
//  第五 / 第六界面:参数页 PARAMS + 单项编辑(★ v1.10.0 新增)
// ----------------------------------------------------------------------------
//  入口(用户 2026-10-17 拍板,三个都做):
//    ① 主界面**长按 CHARTS 键 1.5 s**  —— 零几何改动,复用 CLR-BS 那套进度条
//    ② 第二界面**点 INTEGRITY 卡**      —— 看见 HPL 超限就想调 HPL LIM,动机顺
//    ③ 串口 **`p`**                     —— 在电脑前/屏不方便时不用伸手指
//    为什么不是长按 LOG(那也是候选):LOG 那一格短按要进事件列表,再挂一个长按
//    等于同一格有两种时间语义;CHARTS 是用户自己挑的,照办。
//
//  ★ 交互为什么是**按键**而不是拖动块(算过,不是偏好):
//      MP THR   4..20 共 16 档,220px 刻度条每档 **13.8px** —— 勉强;
//      HDOP MAX 1.0..5.0 共 40 档,每档 **5.5px** —— 手指一抖差 3 档。
//    而且触摸层有 kSwipePx=40 的横向滑动判定,横拖会被当成翻页手势吃掉。
//    ⇒ 四个步进键 `-5 / -1 / +1 / +5`(各自乘以 step,HDOP 上就是 ±0.5 / ±0.1),
//      刻度条**只做指示、不响应拖动**:画量程、画默认值刻线、画当前游标。
//
//  ★ 什么时候写 flash:改值只动 RAM + 标脏,**退出编辑页 / 回主界面时才落盘**。
//    理由:连点十下不该写十次 NVS(寿命 + 每次几十毫秒的卡顿)。
//  ★ 顶点灰线:值 ≠ 出厂默认时,列表里那一行的数字变橙 —— 一眼看出"这台被动过"。
// ============================================================================
namespace tune {

// ---- 布局(240x320)----
//  列表页 y  0..27 顶条 / 28..251 八行(28px) / 258..291 DEFAULTS / 298 底注
//  编辑页 y  0..27 顶条 / 58 大字 / 92..99 刻度条 / 103 轴标 / 117 def·step
//           136..189 四个步进键 / 196..249 DEFAULT·DONE / 262 说明
constexpr int TP_ROWS   = 8;                     // 与 kN 的 static_assert 在参数表那边
constexpr int TP_ROW_H  = 28;
constexpr int TP_ROW_Y0 = 28;
constexpr int TP_BTN_Y  = 258, TP_BTN_H = 34;
constexpr int TP_HINT_Y = 298;

constexpr int TE_BIG_Y   = 58;
constexpr int TE_BAR_Y   = 92, TE_BAR_H = 8;
constexpr int TE_AXIS_Y  = 103;
constexpr int TE_INFO_Y  = 117;
constexpr int TE_K1_Y    = 136, TE_K1_H = 54;
constexpr int TE_K2_Y    = 196, TE_K2_H = 54;
constexpr int TE_NOTE_Y  = 262;
constexpr int TE_STEP_X0 = 4,  TE_STEP_W = 56, TE_STEP_DX = 60;
constexpr int TE_ACT_X0  = 4,  TE_ACT_W  = 114;
constexpr int TE_ACT_X1  = 122;

static_assert(TP_ROW_Y0 + TP_ROWS * TP_ROW_H <= TP_BTN_Y, "参数行压到 DEFAULTS 按钮了");
static_assert(TP_BTN_Y + TP_BTN_H < TP_HINT_Y, "DEFAULTS 按钮压到底注了");
static_assert(TE_K1_Y + TE_K1_H <= TE_K2_Y, "步进键与动作键重叠");
static_assert(TE_K2_Y + TE_K2_H < TE_NOTE_Y, "动作键压到说明行了");
static_assert(TE_NOTE_Y < SCR_H, "编辑页说明行超出屏底");

static TFT_eSprite* spr = nullptr;
static int  editIdx = -1;       // -1 = 列表页;0..kN-1 = 正在编辑某一项
static bool dirty   = false;    // 改了还没落盘

inline void commit() { if (dirty) { save(); dirty = false; } }

inline bool enter() {
  if (spr) return true;
  disp::releaseMain();                       // 与主界面两块 sprite 互斥(154KB 只有一份)
  spr = new TFT_eSprite(&disp::tft);
  if (spr) {
    spr->setColorDepth(16);
    if (!spr->createSprite(SCR_W, SCR_H)) { spr->deleteSprite(); delete spr; spr = nullptr; }
  }
  if (!spr) {                                // 申请不到:把主界面还回去,留在原地
    disp::ensureMain();
    heapDump("tune enter FAIL");
    Serial.println("[bench] tune: full-screen sprite alloc FAILED -> stay put");
    return false;
  }
  editIdx = -1;
  return true;
}

inline void leave() {
  commit();                                  // 走之前把改过的写进 flash
  if (spr) { spr->deleteSprite(); delete spr; spr = nullptr; }
  disp::ensureMain();
  disp::resetPaintCaches();
  editIdx = -1;
}

inline void listMode()      { editIdx = -1; }
inline void editMode(int i) { if (i >= 0 && i < kN) editIdx = i; }
inline int  curEdit()       { return editIdx; }

inline void resetOne(int i) { if (i >= 0 && i < kN) { *items[i].p = items[i].def; dirty = true; } }

// ---- 顶条(两页共用)----
inline void drawTop(const char* title) {
  spr->fillRect(0, 0, SCR_W, charts::TOP_H, C_BLOCK);
  spr->setTextFont(2);
  spr->setTextSize(1);
  spr->setTextColor(CLR_INK, C_BLOCK);
  spr->setTextDatum(ML_DATUM);
  spr->drawString(title, 6, charts::TOP_H / 2);
  // BACK 与 charts / 事件页同一种"凹槽"画法,三页一致
  spr->fillRoundRect(BACK_X, BACK_Y, BACK_W, BACK_H, 4, CLR_PAPER);
  spr->setTextColor(C_DATA, CLR_PAPER);
  spr->setTextDatum(MC_DATUM);
  spr->drawString("BACK", BACK_X + BACK_W / 2, BACK_Y + BACK_H / 2);
  spr->setTextDatum(TL_DATUM);
}

// ---- 列表页 ----
inline void drawList() {
  spr->fillSprite(CLR_PAPER);
  drawTop("PARAMS");

  char b[20];
  for (int i = 0; i < kN; ++i) {
    const int y = TP_ROW_Y0 + i * TP_ROW_H;
    spr->fillRoundRect(3, y, SCR_W - 6, TP_ROW_H - 1, 3, C_BLOCK);

    spr->setTextFont(2);
    spr->setTextSize(1);
    spr->setTextColor(CLR_INK, C_BLOCK);
    spr->setTextDatum(ML_DATUM);
    spr->drawString(items[i].name, 8, y + TP_ROW_H / 2);

    const bool isChg = fabsf(*items[i].p - items[i].def) > 1e-3f;
    fmtVal(b, sizeof(b), i);
    spr->setTextColor(isChg ? C_WARN : C_DATA, C_BLOCK);
    spr->setTextDatum(MR_DATUM);
    spr->drawString(b, SCR_W - 8, y + TP_ROW_H / 2);
    spr->setTextDatum(TL_DATUM);
  }

  spr->fillRoundRect(3, TP_BTN_Y, SCR_W - 6, TP_BTN_H, 4, C_BLOCK);
  spr->setTextFont(2);
  spr->setTextSize(1);
  spr->setTextColor(C_WARN, C_BLOCK);
  spr->setTextDatum(MC_DATUM);
  spr->drawString("RESET TO DEFAULTS", SCR_W / 2, TP_BTN_Y + TP_BTN_H / 2);

  spr->setTextFont(1);
  spr->setTextSize(1);
  spr->setTextColor(CLR_INK2, CLR_PAPER);
  spr->drawString("tap a row to edit - live", 8, TP_HINT_Y);
  spr->setTextDatum(TL_DATUM);
}

// ---- 编辑页 ----
inline void drawEdit() {
  const int i = editIdx;
  if (i < 0 || i >= kN) return;
  const Item& it = items[i];
  char b[32];

  spr->fillSprite(CLR_PAPER);
  drawTop(it.name);

  // 大字当前值 —— 非默认时橙色(与列表里那一格同一条规则)
  const bool isChg = fabsf(*it.p - it.def) > 1e-3f;
  fmtVal(b, sizeof(b), i);
  spr->setTextFont(4);
  spr->setTextSize(1);
  spr->setTextColor(isChg ? C_WARN : C_DATA, CLR_PAPER);
  spr->setTextDatum(MC_DATUM);
  spr->drawString(b, SCR_W / 2, TE_BIG_Y);

  // 刻度条:量程 lo..hi,填充到当前值,橙色竖线标出厂默认
  const int x0 = 10, w = SCR_W - 20;
  spr->fillRect(x0, TE_BAR_Y, w, TE_BAR_H, CLR_RULE);
  int fw = (int)((*it.p - it.lo) / (it.hi - it.lo) * (float)w);
  if (fw < 1) fw = 1;
  if (fw > w) fw = w;
  spr->fillRect(x0, TE_BAR_Y, fw, TE_BAR_H, C_ST_OK);
  const int fx = x0 + (int)((it.def - it.lo) / (it.hi - it.lo) * (float)w);
  spr->drawFastVLine(fx, TE_BAR_Y - 4, TE_BAR_H + 8, C_WARN);

  // 轴标 + def/step
  spr->setTextFont(1);
  spr->setTextSize(1);
  spr->setTextColor(CLR_INK2, CLR_PAPER);
  spr->setTextDatum(TL_DATUM);
  snprintf(b, sizeof(b), "%.*f", (int)it.dec, it.lo);
  spr->drawString(b, x0, TE_AXIS_Y);
  snprintf(b, sizeof(b), "%.*f", (int)it.dec, it.hi);
  spr->setTextDatum(TR_DATUM);
  spr->drawString(b, x0 + w, TE_AXIS_Y);
  spr->setTextDatum(TL_DATUM);
  snprintf(b, sizeof(b), "def %.*f   step %.*f",
           (int)it.dec, it.def, (int)it.dec, it.step);
  spr->drawString(b, x0, TE_INFO_Y);

  // 四个步进键(标签就是"点一下变多少",不写倍数 —— 写倍数在 HDOP 上会骗人)
  static const int kMult[4] = { -5, -1, +1, +5 };
  spr->setTextFont(2);
  spr->setTextSize(1);
  for (int k = 0; k < 4; ++k) {
    const int x  = TE_STEP_X0 + k * TE_STEP_DX;
    const float dv = (float)kMult[k] * it.step;
    spr->fillRoundRect(x, TE_K1_Y, TE_STEP_W, TE_K1_H, 4, C_BLOCK);
    snprintf(b, sizeof(b), "%+.*f", (int)it.dec, dv);
    spr->setTextColor(kMult[k] > 0 ? C_ST_OK : CLR_INK, C_BLOCK);
    spr->setTextDatum(MC_DATUM);
    spr->drawString(b, x + TE_STEP_W / 2, TE_K1_Y + TE_K1_H / 2);
  }

  // DEFAULT / DONE
  spr->fillRoundRect(TE_ACT_X0, TE_K2_Y, TE_ACT_W, TE_K2_H, 4, C_BLOCK);
  spr->setTextColor(C_WARN, C_BLOCK);
  spr->drawString("DEFAULT", TE_ACT_X0 + TE_ACT_W / 2, TE_K2_Y + TE_K2_H / 2);

  spr->fillRoundRect(TE_ACT_X1, TE_K2_Y, TE_ACT_W, TE_K2_H, 4, C_ST_OK);
  spr->setTextColor(CLR_PAPER, C_ST_OK);
  spr->drawString("DONE", TE_ACT_X1 + TE_ACT_W / 2, TE_K2_Y + TE_K2_H / 2);

  // 说明(这一项"改了会影响什么")
  spr->setTextFont(1);
  spr->setTextSize(1);
  spr->setTextColor(CLR_INK2, CLR_PAPER);
  spr->setTextDatum(TL_DATUM);
  spr->drawString(it.note, 8, TE_NOTE_Y);
}

inline void tick() {
  if (!spr) return;
  if (editIdx >= 0) drawEdit(); else drawList();
  spr->pushSprite(0, 0);
}

// ---- 命中(全部用"抬起点",与其它页一致)----
inline bool hitDefaults(int x, int y) {
  (void)x;
  return y >= TP_BTN_Y && y < TP_BTN_Y + TP_BTN_H;
}

inline int hitRow(int x, int y) {
  (void)x;
  if (y < TP_ROW_Y0 || y >= TP_ROW_Y0 + TP_ROWS * TP_ROW_H) return -1;
  const int r = (y - TP_ROW_Y0) / TP_ROW_H;
  return (r >= 0 && r < kN) ? r : -1;
}

// 返回 -5 / -1 / +1 / +5(乘数),0 = 没命中
inline int hitStep(int x, int y) {
  if (y < TE_K1_Y || y >= TE_K1_Y + TE_K1_H) return 0;
  for (int k = 0; k < 4; ++k) {
    const int bx = TE_STEP_X0 + k * TE_STEP_DX;
    if (x >= bx && x < bx + TE_STEP_W)
      return (k == 0) ? -5 : ((k == 1) ? -1 : ((k == 2) ? 1 : 5));
  }
  return 0;
}

// 1 = DEFAULT / 2 = DONE / 0 = 没命中
inline int hitAction(int x, int y) {
  if (y < TE_K2_Y || y >= TE_K2_Y + TE_K2_H) return 0;
  if (x >= TE_ACT_X0 && x < TE_ACT_X0 + TE_ACT_W) return 1;
  if (x >= TE_ACT_X1 && x < TE_ACT_X1 + TE_ACT_W) return 2;
  return 0;
}

}  // namespace tune

// ============================================================================
//  第三 / 第四界面:事件日志(★★ v1.7.0 新增)
// ----------------------------------------------------------------------------
//  掉电不丢的**状态流水** —— "拔电重启日志还在"就是这一段的全部理由。
//  ★ v1.8.4(用户 2026-10-17 中午定):**任何状态词的变化都要记一条**,
//    不再只记 LV_ABNORMAL / LV_LOST。现在 WAIT / SEARCH / NORMAL / DEGRADED /
//    SIG-ABNORMAL / POS-ANOMALY / LOST / NO-DATA 全都会留下记录 ——
//    屏上状态词怎么变,日志里就怎么排。
//  · 每条记录 = "从 t0 起处于该状态,一直到 t1 变成下一个状态为止"(状态区间)
//  · 新状态必须连续保持 kStableMs(=15 s)才落笔 —— 压住抖动,不做过度过滤
//    (★ v1.8.5:原为 2 s。实测 2 s 档约 1 条/分钟,100 条只够 1.7 小时;
//     15 s + 500 条 ⇒ 覆盖 8 小时以上。15 s 内来回切的状态不单独成条。)
//  · 序号 seq 必须**和记录一起落盘**(v1.8.4 修的 bug:以前只读不写,
//    开机载入恒为 n=0 ⇒ 记录写进了 NVS 却永远读不出来)
//  · 事件**开始时先写一条"未闭环"记录**(掉电也有记录),结束时**补写同一条**
//    (填 closed 与 t1)—— 不拆成 begin / end 两条
//  NVS 命名空间 gnsevt:
//    ev00..ev499 500 条事件(每条 sizeof(EventRec) 字节;%02d 至少两位,100 起三位)
//    seq         单调递增序号(决定 FIFO 槽位:写 slot = seq % 500)
//    stat        统计累加器(6 个 uint32)
//  ★ 统计存**分子与分母**,平均值显示时现算 —— 否则掉电后无法继续累加。
//  ★ NVS 写入会阻塞 50~200 ms,所以只落在**主循环收尾**(eventlog::poll 在 loop 末尾)。
//  ★ 滚动用整块 sprite 重绘(不玩硬件 VSCRDEF):绝对不闪;不追求丝滑,够看就行。
// ============================================================================
namespace eventlog {

constexpr int         kMaxEvt   = 500;      // 事件池(FIFO 500 条)
constexpr uint32_t    kStableMs = 15000;    // 新状态连续保持多久才落一条(压抖动)
constexpr const char* kNvsName  = "gnsevt";
constexpr int         kListRows = 10;       // 列表可见行数
constexpr int         kRowH     = 22;       // 行高
constexpr int         kTfaH     = 54;       // 顶固定区高
constexpr int         kBfaH     = 36;       // 底固定区高
constexpr int         kGgaCap   = 82;       // 事件开始那条 GGA 原文(含 '\0')

// ---- 翻页按钮(★ v1.7.1 新增)----
// 由来(用户 2026-10-16 原话):"log 列表那个界面你要做个翻页的按钮出来,
//   不然点到日志就出记录了,没法翻页" —— 此前只能靠**上下滑动**(kSwipePx = 40px 竖直位移),
//   屏上没有任何可见的翻页入口。
// 落位:顶栏**第二行**。顶栏 54px 原来只用掉 y 0..26(标题 FONT2 居中 + 右上 BACK),
//   y 36..53 那 18px 从头到尾是空的 —— 按钮就塞这儿:
//     · 列表 10 行(kListRows)不动  · 底栏 36px(kBfaH)不动  · 统计与页码一行不让
//   不往底栏放是故意的:底栏只有 36px,要塞按钮就得把列表砍到 9 行 —— 没必要动那个。
// 尺寸:两块 115x22,中间 2px 缝,左右各留 4px。
// 字号:**字体1(6x8),与列表行同号** —— 用户明确"每页只显示10条记录,但是字体不要变大"。
constexpr int         kPgY   = 28;                 // 按钮行顶(y)
constexpr int         kPgH   = 22;                 // 按钮高
constexpr int         kPgW   = 115;                // 按钮宽
constexpr int         kPgXL  = 4;                  // 左块(PREV)左上角 x
constexpr int         kPgXR  = kPgXL + kPgW + 2;   // 右块(NEXT)左上角 x = 121 -> 右沿 236
static_assert(kPgY + kPgH <= kTfaH - 2, "翻页按钮行会压进列表区 —— 调小 kPgH 或调大 kTfaH");
static_assert(kPgXR + kPgW <= SCR_W - 4, "翻页按钮行右端出屏 —— 调小 kPgW 或 kPgXL");

struct EventRec {
  uint32_t seq;                    // 序号(单调递增)
  char     date[6];                // "MM-DD"(UTC)
  uint32_t t0;                     // 开始时刻(当天秒,UTC)
  uint32_t t1;                     // 恢复时刻(当天秒,UTC);未恢复 = 0
  uint8_t  type;                   // phrase::PhraseId
  uint8_t  closed;                 // 1 = 已恢复正常
  int16_t  offDm;                  // 事件开始时的偏离(0.1 m)
  char     gga[kGgaCap];           // 事件开始那条 GGA 原文
};

static EventRec    pool[kMaxEvt];
static uint32_t    seqCur = 0;               // 已写入的最大序号
static uint32_t    nHave  = 0;               // 有效条数(<= kMaxEvt)
static bool        ready  = false;
static Preferences prefs;

// ---- 统计累加器(★ 存分子分母,掉电可续)----
static uint32_t stTotSec = 0, stOnlineSec = 0, stOkSec = 0;
static uint32_t stSumSats = 0, stSumCn0x10 = 0, stNSats = 0;

// ---- 运行时状态 ----
static int      curType   = -1;              // 当前"正在持续"的那条记录的类型(-1 = 无)
//   ★ v1.8.4:lastType / lastEvtMs(同类型 10 s 冷却)已删 —— 状态流水要"变了就记",
//     冷却会把真实转变吞掉;防抖改由 poll() 里的 kStableMs(候选稳定 2 s)承担。
static bool     statDirty = false;

// ---- 页面状态 ----
static TFT_eSprite* spr      = nullptr;
static int          scrollTop = 0;           // 列表顶部显示第几条(0 = 最新)
static int          selIdx    = 0;           // 详情页看的是第几条
static bool         needPaint = true;

// ★ 页面切换/选择变化时**必须**调它,否则 tick() 里 `if (!needPaint) return;` 直接把
//   这一拍吞掉 —— 现场表现就是"按下去了、串口日志也打了、屏上却一动不动"。
//   (2026-10-16 自查抓到两处漏标:selectRow 进详情、backFromEvt 回列表。)
inline void markDirty() { needPaint = true; }

// ★ v1.8.2:**行色 = 事件类型色,只看类型**——
//   "是否闭环"不再参与取色(用户 2026-10-17 明确:"没有闭环恢复这种事情,
//   每个事件类型用自己的固定颜色")。恢复本身是一种**事件类型**(PH_RECOVER),
//   由 poll() 在"异常 -> 正常"那一下记一条(v1.8.4 起并入统一的状态流水),它自然是绿色。
//   与状态词/光环同一个 statusColor565() 取色,「S3 上是什么颜色,C3 上就是什么颜色」。
inline uint16_t rowColor(const EventRec& e) {
  return statusColor565((phrase::PhraseId)e.type, 0);
}

// idx = 0 表示"最新那条",1 表示次新…… 调用方保证 idx < nHave
inline const EventRec& at(int idx) {
  return pool[(seqCur - 1u - (uint32_t)idx + (uint32_t)(kMaxEvt * 4)) % (uint32_t)kMaxEvt];
}

inline uint32_t nowSod() {                   // 当天秒(UTC)
  return (uint32_t)nmea::st.uH * 3600u + (uint32_t)nmea::st.uMi * 60u + (uint32_t)nmea::st.uS;
}
inline void nowDate(char* out, size_t n) {
  if (nmea::st.utcValid) snprintf(out, n, "%02d-%02d", nmea::st.uMo, nmea::st.uD);
  else                   snprintf(out, n, "??-??");
}

// 抓"最近一条 GGA 原文"(事件起始证据;池里从最新往回找)
static void latestGga(char* out, size_t n) {
  out[0] = '\0';
  for (int b = 0; b < gnss::stored && b < NMEA_POOL; ++b) {
    const char* l = gnss::recent(b);
    if (!l) break;
    if (strstr(l, "GGA")) { strncpy(out, l, n - 1); out[n - 1] = '\0'; return; }
  }
}

static void keyOf(int i, char* k, size_t n) { snprintf(k, n, "ev%02d", i); }

inline void saveOne(int slot) {
  char k[8];
  keyOf(slot, k, sizeof(k));
  prefs.putBytes(k, &pool[slot], sizeof(EventRec));
}

inline void saveStat() {
  uint32_t st[6] = { stTotSec, stOnlineSec, stOkSec, stSumSats, stSumCn0x10, stNSats };
  prefs.putBytes("stat", st, sizeof(st));
}

// ---- 开机载入(setup 里调)----
// ★ v1.8.4 修的 bug:去重是"序号 seq 从不落盘"。
//   以前这里只有 getUInt("seq", 0),全文件没有任何 putUInt("seq", …) ⇒ seqCur 恒 0
//   ⇒ n = min(seqCur,100) 恒 0 ⇒ 载入循环一次都不进 ⇒ **记录确实写进了 NVS,
//   但永远读不出来**。2026-10-17 实测横幅 `evt: loaded seq=0 n=0`,
//   而把 NVS 分区整片 dump 出来解析,里面躺着 24 条有效的 evNN blob。
//   槽位映射两端本来是一致的:写入 slot = seq(加一前) % kMaxEvt(第一条 seq=1 落 ev00),
//   载入读 ev00..ev(n-1) —— 所以只要 seq 落盘,顺序与 at() 立刻全对上,别处不用动。
inline void begin() {
  prefs.begin(kNvsName, false);
  seqCur = prefs.getUInt("seq", 0);

  uint32_t st[6] = {0, 0, 0, 0, 0, 0};
  if (prefs.getBytesLength("stat") == (int)sizeof(st)) prefs.getBytes("stat", st, sizeof(st));
  stTotSec = st[0]; stOnlineSec = st[1]; stOkSec = st[2];
  stSumSats = st[3]; stSumCn0x10 = st[4]; stNSats = st[5];

  const uint32_t n = (seqCur < (uint32_t)kMaxEvt) ? seqCur : (uint32_t)kMaxEvt;
  nHave = 0;
  for (uint32_t i = 0; i < n; ++i) {
    char k[8];
    keyOf((int)i, k, sizeof(k));
    if (prefs.getBytesLength(k) == (int)sizeof(EventRec)) {
      prefs.getBytes(k, &pool[i], sizeof(EventRec));
      ++nHave;
    }
  }
  // ★ v1.8.4:上次关机时留下的那条"未闭环"记录,重启后不可能还在进行 —— 就地收尾。
  //   不补这一下,屏上会永远挂着一条"进行中",而且每重启一次就多挂一条。
  if (nHave > 0) {
    const int slot = (int)((seqCur - 1u) % (uint32_t)kMaxEvt);
    if (pool[slot].closed == 0) {
      pool[slot].closed = 1;
      pool[slot].t1 = pool[slot].t0;      // 关机那一瞬结束;具体时刻无从得知,时长记 0
      saveOne(slot);
    }
  }
  ready = true;
  Serial.printf("[bench] evt: loaded seq=%lu n=%lu\n",
                (unsigned long)seqCur, (unsigned long)nHave);
}

// ---- 记一条"事件开始"(先写未闭环)----
inline void startEvent(int type) {
  const uint32_t slot = seqCur % (uint32_t)kMaxEvt;

  EventRec e;
  memset(&e, 0, sizeof(e));
  e.seq    = ++seqCur;
  nowDate(e.date, sizeof(e.date));
  e.t0     = nowSod();
  e.t1     = 0;
  e.type   = (uint8_t)type;
  e.closed = 0;
  int od = (int)(judge::res.offsetM * 10.0f + 0.5f);
  if (od > 3000) od = 3000;
  if (od < 0)    od = 0;
  e.offDm  = (int16_t)od;
  latestGga(e.gga, sizeof(e.gga));
  pool[slot] = e;

  saveOne((int)slot);            // ★ 先落"未闭环"一条 —— 掉电也有记录
  prefs.putUInt("seq", seqCur);  // ★ v1.8.4:序号必须**和记录一起**落盘 ——
                                 //   少了这一句,开机载入恒为 n=0,记录写进去也读不出来
  saveStat();                    // 搭车写统计(主力路径)
  if (nHave < (uint32_t)kMaxEvt) ++nHave;

  curType   = type;
  needPaint = true;
  Serial.printf("[evt] BEGIN #%lu %s off=%.0fm\n",
                (unsigned long)e.seq, phrase::text((phrase::PhraseId)type), judge::res.offsetM);
}

// ---- 事件恢复:补写同一条 ----
inline void closeEvent() {
  if (curType < 0) return;
  const int slot = (int)((seqCur - 1u) % (uint32_t)kMaxEvt);
  EventRec& e = pool[slot];
  e.closed = 1;
  e.t1     = nowSod();
  saveOne(slot);
  saveStat();
  needPaint = true;
  const uint32_t dur = (e.t1 >= e.t0) ? (e.t1 - e.t0) : 0;
  Serial.printf("[evt] CLOSE #%lu %s dur=%lus\n",
                (unsigned long)e.seq, phrase::text((phrase::PhraseId)e.type), (unsigned long)dur);
  curType = -1;
}

// ---- 主循环每圈调:判状态变化 + 记事件 + 攒统计 ----
inline void flushStat();                  // 定义在下面,这里先声明(poll 里要 60s 兜一次)

// ★ v1.8.4(用户 2026-10-17 中午定):**任何状态词的变化都要记一条**。
//   上一版(v1.8.2/1.8.3)只记 ABNORMAL / LOST,DEGRADED 只进统计 ——
//   于是屏上状态词明明在变,日志里却一条都不多,现场看着像"日志坏了"。
//   现在日志就是**完整的流水**:WAIT / SEARCH / NORMAL / DEGRADED / SIG-ABNORMAL /
//   POS-ANOMALY / LOST / NO-DATA 每次切换都留一条,条目的 t0..t1 就是该状态的区间。
//
//   关于"恢复正常"这条(2026-10-17 上午定的口径,保留):
//     进入 LV_OK 时,若**进过 ABNORMAL/LOST**(latchedBad),类型记 PH_RECOVER;
//     否则记 PH_OK。两者在屏上**同词 NORMAL、同色绿**,只是内部 type 不同。
//     DEGRADED **不**锁存 latchedBad —— 否则 C/N0 在阈值上下抖一下,NORMAL 就会
//     在 PH_OK / PH_RECOVER 之间来回跳,分类反而乱。
inline void poll() {
  if (!ready) return;
  const uint32_t now = millis();

  // ---- ① 本拍该记成哪种事件 ----
  static bool latchedBad = false;
  const bool isBad = (judge::res.level == judge::LV_ABNORMAL || judge::res.level == judge::LV_LOST);
  if (isBad) latchedBad = true;

  int t = (int)judge::res.ph;
  if (judge::res.level == judge::LV_OK) {
    t = latchedBad ? (int)phrase::PH_RECOVER : (int)phrase::PH_OK;
    latchedBad = false;
  }

  // ---- ② 防抖:候选状态连续保持 kStableMs 才落笔 ----
  //   这一段是**必须**的,不是"顺手加的过滤":C/N0 中位数在 25 dB-Hz 上下单帧抖一次,
  //   DEGRADED <-> NORMAL 就会来回切;不设稳定时间,事件池几分钟就被刷满,
  //   真正要看的异常反而被挤掉。
  //   ★ v1.8.5:2 s -> **15 s**。实测 2 s 档下记录速率约 **1 条/分钟**(开关一次机就有一条),
  //     100 条 FIFO 只覆盖约 1.7 小时 —— "想翻昨天怎么回事"直接没数据。
  //     15 s 只认"站稳了的状态",3~10 s 的瞬断不再单独成条(它本来也不是要看的对象);
  //     池子同时扩到 500 ⇒ 按同一速率可覆盖 **8 小时以上**,抖动环境更久。
  static int      candType  = -1;
  static uint32_t candSince = 0;
  if (t == curType) {
    candType = -1;                                   // 回到当前态:候选作废
  } else if (t != candType) {
    candType  = t;                                   // 换了候选:重新计时
    candSince = now;
  }
  const bool took = (candType == t) && ((uint32_t)(now - candSince) >= kStableMs);

  if (curType < 0) {
    if (took) { startEvent(t); candType = -1; }       // 开机第一拍 / 上一条刚收尾:落笔
  } else if (took) {
    closeEvent();                                     // 旧状态收尾(t1 = 现在)
    startEvent(t);                                    // 新状态开一条(t0 = 现在)
    candType = -1;
  }

  // ---- 统计:1Hz 累加;冷启动 90 s 宽限时段剔除(不算进统计)----
  static uint32_t tStat = 0;
  if (now - tStat >= 1000) {
    tStat = now;
    if (now >= TH_COLDSTART_MS) {
      ++stTotSec;
      if (nmea::st.fixValid) ++stOnlineSec;
      if (judge::res.level == judge::LV_OK) {          // 平均星数 / CN0 只统计 LV_OK 时段
        ++stOkSec;
        stSumSats   += (uint32_t)nmea::st.satsUsed;
        stSumCn0x10 += (uint32_t)(nmea::st.cn0Median * 10.0f);
        ++stNSats;
      }
      statDirty = true;
    }
  }

  // ---- 统计落盘:60 s 一次 ----
  //   ★ v1.7.0 自查补:原来 flushStat() 只挂在 leave()(离开日志页)上 ——
  //     意味着"从来没打开过日志页"的设备,统计累加器**永远不进 NVS**,重启即归零。
  //     60 s 一次足够(≈1440 次/天,NVS 擦写寿命 ~10 万次量级,远远够用),
  //     且与 1Hz 的累加节拍同频收敛在循环收尾,不额外引入抖动。
  static uint32_t tFlush = 0;
  if (statDirty && (now - tFlush >= 60000)) { tFlush = now; flushStat(); }
}

inline void flushStat() {
  if (!statDirty) return;
  statDirty = false;
  saveStat();
}

// ---- 页面:sprite 申请 / 释放(与主界面互斥,153,600B 只有一份)----
inline bool enter() {
  if (!spr) {
    disp::releaseMain();
    spr = new TFT_eSprite(&disp::tft);
    if (spr) {
      spr->setColorDepth(16);
      if (!spr->createSprite(SCR_W, SCR_H)) { spr->deleteSprite(); delete spr; spr = nullptr; }
    }
    if (!spr) {
      disp::ensureMain();
      heapDump("eventlog enter FAIL");
      Serial.println("[bench] evt: full-screen sprite alloc FAILED -> stay on main page");
      return false;
    }
  }
  needPaint = true;
  return true;
}

inline void leave() {
  if (spr) { spr->deleteSprite(); delete spr; spr = nullptr; }
  flushStat();                     // ★ 出页兜底:统计落盘一次
  disp::ensureMain();
  disp::resetPaintCaches();
}

// ---- 绘制:BACK 按钮(三/四界面共用)----
inline void paintBack() {
  // ★ v1.7.1:BACK 改"凹槽式" —— 在 C_BLOCK 顶栏上挖一块**页底色**的圆角块。
  //   原来靠"比顶栏亮一档的底色 + 一圈描边"来显形,现在描边整段去掉(照 C3),
  //   底色又与顶栏同色,不挖成页底就跟顶栏糊在一起、按钮直接消失。
  spr->fillRoundRect(BACK_X, BACK_Y, BACK_W, BACK_H, 4, CLR_PAPER);
  spr->setTextFont(2); spr->setTextSize(1);
  spr->setTextColor(C_DATA, CLR_PAPER);
  spr->setTextDatum(MC_DATUM);
  spr->drawString("BACK", BACK_X + BACK_W / 2, BACK_Y + BACK_H / 2);
  spr->setTextDatum(TL_DATUM);
  spr->setTextFont(1);
}

// ---- ★ v1.7.1 新增:翻页按钮(顶栏第二行) ----
// "能不能翻"只在这一处判 —— 按钮画不画禁用态、点了生不生效,共用同一个判据,
// 免得出现"看着能点、点了没反应"这种不一致。
inline bool canPrev() { return nHave > 0 && scrollTop > 0; }
inline bool canNext() { return nHave > 0 && (scrollTop + kListRows < (int)nHave); }

// 画一颗翻页按钮。side:0 = 左(PREV)/ 1 = 右(NEXT)。
// 样式与 BACK 完全同款(顶栏上挖一块页底凹槽 + 青色文字)—— 全屏按钮只有这一种样子。
// ⚠ 字号是**字体1(6x8)**,不是 BACK 用的 FONT2(16px):用户明确"字体不要变大",要与列表行同号。
inline void paintPgBtn(int side, bool enabled) {
  const int x = side ? kPgXR : kPgXL;
  spr->fillRoundRect(x, kPgY, kPgW, kPgH, 4, CLR_PAPER);
  spr->setTextFont(1); spr->setTextSize(1);
  spr->setTextColor(enabled ? C_DATA : C_DIM, CLR_PAPER);
  spr->setTextDatum(MC_DATUM);
  spr->drawString(side ? "NEXT >" : "< PREV", x + kPgW / 2, kPgY + kPgH / 2);
  spr->setTextDatum(TL_DATUM);
}

// 命中翻页按钮 -> -1(上一页)/ +1(下一页)/ 0(没命中,或该方向已经翻不动)。
// 翻不动时返回 0、而不是"给个方向但不动" —— 调用方据此跳过重绘,不会白刷一帧。
inline int hitPgBtn(int x, int y) {
  if (y < kPgY || y >= kPgY + kPgH) return 0;
  if (x >= kPgXL && x < kPgXL + kPgW) return canPrev() ? -1 : 0;
  if (x >= kPgXR && x < kPgXR + kPgW) return canNext() ? +1 : 0;
  return 0;
}

// ★ v1.8.0:详情页版 —— 判据从 scrollTop 换成 selIdx,其余与 hitPgBtn 完全同构。
//   PREV(-1) = 上一条(更新)/ NEXT(+1) = 下一条(更旧),翻不动返回 0。
inline int hitPgBtnD(int x, int y) {
  if (y < kPgY || y >= kPgY + kPgH) return 0;
  if (x >= kPgXL && x < kPgXL + kPgW) return (selIdx > 0) ? -1 : 0;
  if (x >= kPgXR && x < kPgXR + kPgW) return (selIdx < (int)nHave - 1) ? +1 : 0;
  return 0;
}

// 翻一页(dir:+1 = 下一页(更旧)/ -1 = 上一页(更新))。
// ★ 与 scroll() 的分工写清楚:scroll 是**滑动**用的细滚(一次 3 行),本函数是**按钮**用的整页跳。
//   scrollTop 语义没动:0 = 最新那条在列表顶上,数字越大看的越旧。
inline void page(int dir) {
  if (nHave == 0) return;
  int maxTop = (int)nHave - kListRows;
  if (maxTop < 0) maxTop = 0;
  int t = scrollTop + dir * kListRows;
  if (t < 0) t = 0;
  if (t > maxTop) t = maxTop;
  if (t != scrollTop) { scrollTop = t; markDirty(); }
}

// 详情页翻上一条 / 下一条(d:-1 看更新的 / +1 看更旧的),返回是否真的动了。
// 已经在池子两端就返回 false —— 调用方据此跳过重绘。
inline bool stepEvt(int d) {
  if (nHave == 0) return false;
  int t = selIdx + d;
  if (t < 0) t = 0;
  if (t >= (int)nHave) t = (int)nHave - 1;
  if (t == selIdx) return false;
  selIdx = t;
  markDirty();
  return true;
}

// ---- 界面3:事件列表(TFA 54 / VSA 220 / BFA 36)----
inline void paintList() {
  if (!spr) return;
  spr->fillSprite(CLR_PAPER);

  // 顶固定区
  spr->fillRect(0, 0, SCR_W, kTfaH, C_BLOCK);
  spr->setTextFont(2); spr->setTextSize(1);
  spr->setTextColor(CLR_INK, C_BLOCK);
  spr->setTextDatum(ML_DATUM);
  // ★ v1.7.1:标题从"顶栏正中"(y = kTfaH/2 = 27)上移到 y = 14 ——
  //   下半 28px 要腾给翻页按钮行(FONT2 16px 在 y=14 居中 ⇒ 占约 y 6..22,
  //   与右上 BACK 的 y 2..25 大致同高,互不打架)。
  spr->drawString("EVENT LOG", 8, 14);
  spr->setTextDatum(TL_DATUM);
  paintBack();

  // ★ v1.7.1:翻页按钮行(y 28..49):左 PREV / 右 NEXT。
  paintPgBtn(0, canPrev());
  paintPgBtn(1, canNext());

  // 滚动区
  if (nHave == 0) {
    spr->setTextFont(1); spr->setTextSize(1);
    spr->setTextColor(CLR_INK2, CLR_PAPER);
    spr->drawString("no events yet (normal = no entries)", 6, kTfaH + 14);
  } else {
    for (int r = 0; r < kListRows; ++r) {
      const int idx = scrollTop + r;
      if (idx >= (int)nHave) break;
      const EventRec& e = at(idx);
      const int y = kTfaH + r * kRowH;
      const uint16_t bg = (r & 1) ? 0x10A2 : CLR_PAPER;      // 隔行底色(深灰)
      if (r & 1) spr->fillRect(0, y, SCR_W, kRowH, bg);
      char ln[40];
      snprintf(ln, sizeof(ln), "%s %02lu:%02lu  %-9s %s",
               e.date,
               (unsigned long)(e.t0 / 3600u), (unsigned long)((e.t0 / 60u) % 60u),
               phrase::text((phrase::PhraseId)e.type),
               e.closed ? "OK" : "--");
      spr->setTextFont(1); spr->setTextSize(1);
      // ★ v1.8.2:行文字按**事件类型**着色(LOST 红 / NORMAL 绿 / 各类型自己的状态色),
      //   "是否闭环"不参与取色。
      spr->setTextColor(rowColor(e), bg);
      spr->drawString(ln, 6, y + 7);
    }
  }

  // 底固定区:统计摘要 + 页码
  spr->fillRect(0, SCR_H - kBfaH, SCR_W, kBfaH, C_BLOCK);
  spr->setTextFont(1); spr->setTextSize(1);
  // ★ v1.8.1:统计/页码用数据青(原 CLR_INK2 暗灰)—— 用户定,与 C3 搬运页底栏一致
  spr->setTextColor(C_DATA, C_BLOCK);
  const uint32_t okp  = stTotSec ? (stOkSec * 100u / stTotSec) : 0;
  const uint32_t avgS = stNSats   ? (stSumSats / stNSats) : 0;
  const uint32_t avgC = stNSats   ? (stSumCn0x10 / stNSats) : 0;
  char b[48];
  snprintf(b, sizeof(b), "evt %lu/%d  OK %lu%%  sat %lu  cn0 %lu.%lu",
           (unsigned long)nHave, kMaxEvt, (unsigned long)okp, (unsigned long)avgS,
           (unsigned long)(avgC / 10u), (unsigned long)(avgC % 10u));
  spr->drawString(b, 6, SCR_H - kBfaH + 8);
  char pg[28];
  const int lastShown = (scrollTop + kListRows < (int)nHave) ? (scrollTop + kListRows) : (int)nHave;
  if (nHave) snprintf(pg, sizeof(pg), "%d-%d / %d", scrollTop + 1, lastShown, (int)nHave);
  else       snprintf(pg, sizeof(pg), "0 / 0");
  spr->drawString(pg, 6, SCR_H - kBfaH + 22);

  spr->pushSprite(0, 0);
  needPaint = false;
}

// ---- 界面4:事件详情 ----
inline void paintDetail() {
  if (!spr) return;
  spr->fillSprite(CLR_PAPER);
  if (nHave == 0) { paintList(); return; }
  if (selIdx >= (int)nHave) selIdx = 0;
  const EventRec& e = at(selIdx);
  const uint16_t tc = statusColor565((phrase::PhraseId)e.type, 0);

  // 顶栏:事件类型大字(FONT4)+ BACK
  // ★ v1.8.0:标题从 y=kTfaH/2=27 上移到 y=14(与列表页同款)—— 下半 28px 腾给翻页按钮行。
  spr->fillRect(0, 0, SCR_W, kTfaH, C_BLOCK);
  spr->setTextFont(4); spr->setTextSize(1);
  spr->setTextColor(tc, C_BLOCK);
  spr->setTextDatum(ML_DATUM);
  spr->drawString(phrase::text((phrase::PhraseId)e.type), 8, 14);
  spr->setTextDatum(TL_DATUM);
  paintBack();

  // ★ v1.8.0:PREV/NEXT(与列表页同几何 y28..49)—— PREV=上一条(更新)/ NEXT=下一条(更旧)。
  //   左右滑动保留,按钮只是补一个"看得见"的入口(用户 2026-10-17:"就差这两个按键")。
  paintPgBtn(0, selIdx > 0);
  paintPgBtn(1, selIdx < (int)nHave - 1);

  // 中部元数据
  spr->setTextFont(1); spr->setTextSize(1);
  int y = kTfaH + 12;
  const char* lab[5] = { "seq", "start", "end", "dur", "off" };
  for (int i = 0; i < 5; ++i) {
    spr->setTextColor(CLR_INK2, CLR_PAPER);
    spr->drawString(lab[i], 6, y + i * 14);
  }
  char b[48];
  spr->setTextColor(CLR_INK, CLR_PAPER);
  snprintf(b, sizeof(b), "#%lu", (unsigned long)e.seq);
  spr->drawString(b, 46, y);
  snprintf(b, sizeof(b), "%s %02lu:%02lu UTC", e.date,
           (unsigned long)(e.t0 / 3600u), (unsigned long)((e.t0 / 60u) % 60u));
  spr->drawString(b, 46, y + 14);
  if (e.closed) snprintf(b, sizeof(b), "%02lu:%02lu UTC",
                         (unsigned long)(e.t1 / 3600u), (unsigned long)((e.t1 / 60u) % 60u));
  else          strcpy(b, "--  (not recovered)");
  spr->drawString(b, 46, y + 28);
  if (e.closed && e.t1 >= e.t0) snprintf(b, sizeof(b), "%lu s", (unsigned long)(e.t1 - e.t0));
  else                          strcpy(b, "--");
  spr->drawString(b, 46, y + 42);
  snprintf(b, sizeof(b), "%.1f m", e.offDm / 10.0f);
  spr->drawString(b, 46, y + 56);

  // 下部:GGA 原文(事件开始那条)
  int gy = 176;
  spr->setTextColor(CLR_INK2, CLR_PAPER);
  spr->drawString("GGA at event start:", 6, gy - 14);
  spr->setTextColor(CLR_INK, CLR_PAPER);
  if (e.gga[0]) {
    const int len = (int)strlen(e.gga);
    for (int c = 0; c * MSG_CHARS < len && gy < SCR_H - 10; ++c) {
      char ln[MSG_CHARS + 1];
      int n = len - c * MSG_CHARS;
      if (n > MSG_CHARS) n = MSG_CHARS;
      memcpy(ln, e.gga + c * MSG_CHARS, n);
      ln[n] = '\0';
      spr->drawString(ln, 6, gy);
      gy += 10;
    }
  } else {
    spr->drawString("(no GGA captured)", 6, gy);
  }

  spr->pushSprite(0, 0);
  needPaint = false;
}

inline void tick(bool detail) {
  if (!spr || !needPaint) return;
  if (detail) paintDetail();
  else        paintList();
}

// ---- 交互 ----
inline void scroll(int d) {                  // d > 0 = 往更新的方向
  if (nHave == 0) return;
  int maxTop = (int)nHave - kListRows;
  if (maxTop < 0) maxTop = 0;
  int t = scrollTop - d;
  if (t < 0) t = 0;
  if (t > maxTop) t = maxTop;
  if (t != scrollTop) { scrollTop = t; needPaint = true; }
}

inline bool selectRow(int y) {               // 命中列表行 -> 记下要看哪条
  if (nHave == 0) return false;
  if (y < kTfaH || y >= kTfaH + kListRows * kRowH) return false;
  const int idx = scrollTop + (y - kTfaH) / kRowH;
  if (idx >= (int)nHave) return false;
  selIdx = idx;
  markDirty();                     // ★ 不标脏 -> ui::toEvt() 里的 tick(true) 会直接返回
  return true;
}

// ============================================================================
//  ★★ v1.8.0 链路服务:C3 的「S3 LOG」搬运页(2026-10-17,用户定)
// ----------------------------------------------------------------------------
//  C3 点 VIEW/LOG 后发**整行命令**(与 w* 同类,不走单键白名单):
//    evtlist <top>   -> 回 evth(表头) + evts(统计) + 最多 10 行 evtr(行摘要,含行色)
//    evtdet <seq>    -> 回 evtd(单条详情) + evtg xN(GGA 原文按 MSG_CHARS=38 字符截段)
//    evtstat         -> 只回一行 evts
//  帧**不带** "@s ok " 前缀 —— 前缀由 benchlink::poll() 的排干循环统一加
//  (与 wscan 的 wsc 分条回执同一个规矩:每圈最多 2 条,不堵主循环、不顶写缓冲)。
//  设计口径:**颜色在 S3 端定、随帧发**(evtr/evtd 里的 <col> 字段);
//  行摘要只发 seq / 日期 / t0 / closed / 行色 / 类型词 ——
//  C3 端拿这些把列表画成和这边**逐像素同款**(含行色),不自己判色、不另存词表。
//  ★ v1.8.2:<col> 一律 = statusColor565(type)(纯类型色,列表行与详情大字同源),
//   C3 端因此自动跟着改成"按类型着色",不需要改一行 C3 代码。
//  详情用 **seq** 寻址(不用下标):列表看完再点详情之间若进了新事件,
//  下标会整体移位,seq 单调递增不受影响。
// ============================================================================
constexpr int kEvtTxQ   = 16;                 // 排队帧数:一页 = 1+1+10 = 12 帧,留 4 帧余量
constexpr int kEvtTxLen = 112;
static char    txQ[kEvtTxQ][kEvtTxLen];
static uint8_t txHead = 0, txTail = 0;

inline bool txPush(const char* s) {
  if (!s[0]) return false;
  const uint8_t n = (uint8_t)((txTail + 1) % kEvtTxQ);
  if (n == txHead) return false;              // 满则丢新的:链路慢不能拖主循环
  snprintf(txQ[txTail], kEvtTxLen, "%s", s);
  txTail = n;
  return true;
}
inline bool evtNextLine(char* out, size_t cap) {   // benchlink::poll() 每圈排 2 条
  if (txHead == txTail) return false;
  snprintf(out, cap, "%s", txQ[txHead]);
  txHead = (uint8_t)((txHead + 1) % kEvtTxQ);
  return true;
}

inline void fmtStats(char* out, size_t n) {        // 6 个原始分子分母,格式化在 C3 端做
  snprintf(out, n, "evts %lu %lu %lu %lu %lu %lu",
           (unsigned long)stTotSec, (unsigned long)stOnlineSec, (unsigned long)stOkSec,
           (unsigned long)stSumSats, (unsigned long)stSumCn0x10, (unsigned long)stNSats);
}

inline void reqList(int top) {
  int maxTop = (int)nHave - kListRows;
  if (maxTop < 0) maxTop = 0;
  if (top < 0) top = 0;
  if (top > maxTop) top = maxTop;
  char b[kEvtTxLen];
  snprintf(b, sizeof(b), "evth %lu %d", (unsigned long)nHave, top);
  txPush(b);
  fmtStats(b, sizeof(b));
  txPush(b);
  for (int r = 0; r < kListRows; ++r) {
    const int idx = top + r;
    if (idx >= (int)nHave) break;
    const EventRec& e = at(idx);
    snprintf(b, sizeof(b), "evtr %lu %s %lu %d %u %s",
             (unsigned long)e.seq, e.date, (unsigned long)e.t0,
             e.closed ? 1 : 0, (unsigned)rowColor(e),
             phrase::text((phrase::PhraseId)e.type));
    txPush(b);
  }
}

// d = +1/-1:以 seq 所在位置为基准**步进一条**(C3 详情页 PREV/NEXT 用 —— C3 手里没有
// 整个池子,不知道"下一条"的 seq,所以相对步进必须由这边代做)。找不到 seq 或越界
// 就返回**夹紧后**的那条(帧里带 idx/nHave,C3 据此把到头的按钮画成禁用态)。
inline void reqDet(uint32_t seq, int d) {
  for (int i = 0; i < (int)nHave; ++i) {
    if (at(i).seq != seq) continue;
    int idx = i + d;
    if (idx < 0) idx = 0;
    if (idx >= (int)nHave) idx = (int)nHave - 1;
    const EventRec& e = at(idx);
    char b[kEvtTxLen];
    // ★ v1.8.2:<col> = rowColor(e) = statusColor565(type) —— 列表行与详情大字
    //   现在**完全同源**(同一个函数),C3 端两处因此必然一致。
    snprintf(b, sizeof(b), "evtd %lu %s %lu %lu %d %d %u %d %d %s",
             (unsigned long)e.seq, e.date,
             (unsigned long)e.t0, (unsigned long)e.t1,
             e.closed ? 1 : 0, (int)e.offDm,
             (unsigned)rowColor(e),
             idx, (int)nHave, phrase::text((phrase::PhraseId)e.type));
    txPush(b);
    if (e.gga[0]) {                              // GGA 截段与屏上 paintDetail 同宽
      const int len = (int)strlen(e.gga);
      for (int c = 0; c * MSG_CHARS < len; ++c) {
        char seg[MSG_CHARS + 1];
        int nn = len - c * MSG_CHARS;
        if (nn > MSG_CHARS) nn = MSG_CHARS;
        memcpy(seg, e.gga + c * MSG_CHARS, nn);
        seg[nn] = '\0';
        snprintf(b, sizeof(b), "evtg %s", seg);
        txPush(b);
      }
    }
    return;
  }
}

inline void reqStat() {
  char b[80];
  fmtStats(b, sizeof(b));
  txPush(b);
}

}  // namespace eventlog

// ============================================================================
//  页面切换(★ 2026-10-06)
// ----------------------------------------------------------------------------
//  两个入口,同一个动作:
//    · 屏上 —— 主界面点报文区 / 动图页点任意处(在 touch::poll 里调)
//    · 串口 —— 敲 d(和屏上完全等价;演示面板点报文区走的就是这条路)
//  失败处理有讲究:enter() 申请不到全屏 sprite 时**留在主界面**并打印原因 ——
//  宁可不进,也不能把屏刷黑、或者卡在一个画不出东西的页面上出不来。
// ============================================================================
namespace ui {

// ★★ v1.7.0:页面从 2 个扩到 4 个
//   0 主界面(PAGE_MAIN)  星空图 + 按键条 + 数据条
//   1 第二界面(PAGE_CHARTS)  三张图(竖条 / 曲线 / 漂移盘),右上 BACK 返回
//   2 第三界面(PAGE_LOG)     事件列表(10 行 x 22px),右上 BACK 返回主界面
//   3 第四界面(PAGE_EVT)     事件详情,BACK 返回**第三界面**(不是主界面)
enum Page : uint8_t { PAGE_MAIN = 0, PAGE_CHARTS = 1, PAGE_LOG = 2, PAGE_EVT = 3,
                      PAGE_TUNE = 4, PAGE_TPEDIT = 5 };   // ★ v1.10.0:参数列表 / 单项编辑
static Page page = PAGE_MAIN;

inline void toCharts() {
  if (page == PAGE_CHARTS) return;
  if (!charts::enter()) return;              // 失败原因 enter() 里已经打印过
  page = PAGE_CHARTS;
  charts::tick();                            // 立刻出第一帧,不等下一个 1s 到点
  Serial.println("[bench] ui: -> charts page");
}

// ★ v1.10.0:参数页。三个入口(长按 CHARTS / 点 INTEGRITY 卡 / 串口 p)都走这里。
//   ★ 从第二界面点卡进来时**必须先把 charts 的 sprite 放掉** —— 全屏 sprite 只有
//     一份(240x320x2 = 153,600 B),两块同时在场必然分配失败。
//     charts::leave() 内部会 ensureMain(),随后 tune::enter() 又 releaseMain(),
//     一来一回多一次分配释放,但换来"任意页都能进参数页"的统一入口,值这个代价。
inline void toTune() {
  if (page == PAGE_TUNE) return;
  if (page == PAGE_CHARTS)                              charts::leave();
  else if (page == PAGE_LOG || page == PAGE_EVT)        eventlog::leave();
  if (!tune::enter()) return;                // 分配失败:留在原地,主界面 already 还回去了
  page = PAGE_TUNE;
  tune::listMode();
  tune::tick();
  Serial.println("[bench] ui: -> params page");
}

inline void toTpEdit(int i) {
  if (page != PAGE_TUNE) return;
  tune::editMode(i);
  page = PAGE_TPEDIT;
  tune::tick();
  Serial.printf("[bench] ui: -> param edit #%d\n", i);
}

inline void backFromTpEdit() {               // 编辑页 BACK/DONE -> 回列表(**不是**直接回主界面)
  if (page != PAGE_TPEDIT) return;
  tune::commit();                            // 离开这一项再落盘,连点十下只写一次
  tune::listMode();
  page = PAGE_TUNE;
  tune::tick();
  Serial.println("[bench] ui: -> (back) params page");
}

inline void toLog() {
  if (page == PAGE_LOG) return;
  // ★ v1.10.0:同上 —— 从参数页直接跳到日志页时,必须先把 tune 的 sprite 放掉。
  if (page == PAGE_TUNE || page == PAGE_TPEDIT) tune::leave();
  if (!eventlog::enter()) return;
  page = PAGE_LOG;
  eventlog::tick(false);
  Serial.println("[bench] ui: -> event log page");
}

inline void toEvt() {
  if (page != PAGE_LOG) return;
  page = PAGE_EVT;
  eventlog::tick(true);
  Serial.println("[bench] ui: -> event detail page");
}

inline void backFromEvt() {                  // 界面4 的 BACK -> 回界面3(不是直接回主界面)
  if (page != PAGE_EVT) return;
  page = PAGE_LOG;
  eventlog::markDirty();                     // ★ 详情页刚画过,needPaint 已被吃掉 -> 回列表必须重新标脏
  eventlog::tick(false);
  Serial.println("[bench] ui: -> (back) event log page");
}

// ★ v1.7.1:界面4(事件详情)左右滑动 = 上一条 / 下一条,不用退回列表再点。
//   d:-1 = 看更新的(右滑)/ +1 = 看更旧的(左滑)。
//   必须在这里补一次 tick(true):详情页只在 `eventlog::tick()` 里画,而主循环那个
//   节拍是 400ms 一次(见 loop 里的 `now - tEvt > 400`)—— 不补就要等最多 0.4 秒才翻过去,
//   手感像"没反应"。这个坑与 K.2(漏标脏)是同一族:改完必须立刻出一帧。
inline void evtStep(int d) {
  if (page != PAGE_EVT) return;
  if (!eventlog::stepEvt(d)) return;         // 已经在池子两端 -> 什么都不做
  eventlog::tick(true);
  Serial.printf("[bench] ui: event detail -> %s\n", d > 0 ? "older" : "newer");
}

inline void toMain() {
  if (page == PAGE_MAIN) return;
  // ★ v1.10.0:参数页(列表/编辑)也占着那张全屏 sprite,退出时必须一起放掉 ——
  //   漏了它就会"两个全屏 sprite 同时在场",下一次分配直接失败。
  if      (page == PAGE_CHARTS)                       charts::leave();
  else if (page == PAGE_TUNE || page == PAGE_TPEDIT)  tune::leave();
  else                                                eventlog::leave();
  page = PAGE_MAIN;
  disp::tft.fillScreen(CLR_PAPER);           // 先铺回页底色:否则各元素之间的缝里会留着上一页的底
  disp::resetPaintCaches();
  disp::tick();                              // 立刻出一帧,不等下一个节拍
  Serial.println("[bench] ui: -> main page");
}

// ★ v1.10.0:`d` 键在参数页里必须是"回主界面",不能直接 toCharts() ——
//   否则 charts::enter() 会在 tune 的 sprite 还在时再要一块 153KB。
inline void toggle() {
  if (page == PAGE_CHARTS) toMain();
  else if (page == PAGE_TUNE || page == PAGE_TPEDIT) toMain();
  else toCharts();
}

// ★ v1.5.20:告诉 C3 现在停在哪一页('d' 键的回显)
inline const char* pageName() {
  switch (page) {
    case PAGE_CHARTS: return "charts";
    case PAGE_LOG:    return "log";
    case PAGE_EVT:    return "event";
    case PAGE_TUNE:   return "params";
    case PAGE_TPEDIT: return "param";
    default:          return "board";
  }
}

}  // namespace ui

// ============================================================================
//  触摸控制(XPT2046,TFT_eSPI 内建驱动) —— 可选
// ----------------------------------------------------------------------------
//  机制照搬已交付项目 RF_Source_Controller(同一块屏,已跑通验证):
//    · 校准数据存 NVS,用 "cal_flag" 记是否校准过 —— 全生命周期只校一次
//    · 要重校准:串口发 r,重启后在屏上点 4 个十字
//    · 读点用压力阈值 600,判松手用 100 —— 阈值给小了会一直误触(老项目踩过)
//    · 校准值与屏幕方向绑定。本固件与老项目都是 rotation 0(竖屏),
//      所以老项目那组校准值能当初值用;换了方向或换了屏就必须重校。
//
//  主界面可点区域(★ v1.7.0 全面改写 —— 旧版"顶部存基准 / 中部进动图 / 底部切注入"
//  三段分区**全部作废**;分区定义见 touch 命名空间顶部那段,这里只留结论):
//    星空区 (y 0..239)   -> 无动作(误触安全)
//    按键条 (y 240..279) -> x 四等分:BASE / CLR-BS / CHARTS / LOG
//                            ★ v1.8.3:第 2 键 = CLR-BS,**按住 5 s** 才清基准
//    数据条 (y 280..319) -> 长按 >=800ms 切"报文窗",再长按切回
//  第二/三/四界面 -> 只认右上 BACK,第三界面另认上下滑动与列表行点击。
//  另:**动作一律在"抬起"时执行** —— 只有抬起才能分辨点击与滑动。
//  串口命令照旧可用,两条路并存,互不影响(动图页对应命令 d,日志页对应 v)。
// ============================================================================
// 串口日志开关:'v' 命令切换。默认安静(设备正常跑着时输出越少越好),
// 排查问题时敲 v 打开,再看 5 秒状态行/触摸坐标这些滚动日志。
// ★ v1.7.0:命令集已收敛为 r b c d v s g t(+ w* 一族;★ v1.8.3 起 `i` 注入与 `m` mirror 都已删)。
//   `p`(坐标链路打印)与
//   `l`(led probe)两条探针命令**已删** —— 本行原来还写着它们,别再照着找。
// 敲过的命令自己的回显不受日志开关管,永远打印。
static bool gLogOn = false;   // ★ v1.8.0:v 键已改义为日志页开关,本开关暂无按键(默认关,要用改这里重编)

namespace touch {

// 放在 #if 之外:v1.5.6 开机自检要用它,触摸被编译裁掉时也能取值(false)。
static bool calibrated = false;    // NVS 里有校准值(或本次刚校完)= true

#if ENABLE_TOUCH && defined(TOUCH_CS)

static uint16_t    calData[5] = {192, 3708, 281, 3512, 5};  // 未校准前的初值(老项目实测值)
// ★ 历史坑(2026-09-28):换新屏后必须重新校准触摸('r' 命令+重启)。
//   旧屏的 NVS 校准值配新屏,上半截碰巧能用、下半截 raw 超量程被 getTouch 判无效,
//   表现就是"顶部能点、底部没反应"——用 touchraw 打原始值对比校准量程才定位到。
static uint32_t    lastMs     = 0;
// ★ 2026-10-06:初值改成 **true** —— 意思是"要求先看到一次抬手"才武装。
//   踩坑记录:带第二界面的固件第一次烧完上电,板子**自己**停在了动图页。
//   根因就在这里:初值原来是 false,而触摸屏刚上电那几百毫秒读数不稳,
//   getTouch 偶尔回一个"按下 + 随机坐标",down && !wasDown 恰好成立 ->
//   被当成一次真点击,坐标又恰巧落在报文区,于是自动进了动图页。
//   初值给 true 之后,必须先真的抬手一次,才会认第一个点击。
static bool        wasDown    = true;
static Preferences prefs;

// 触摸校准单独用一个 NVS 命名空间。
// 注意不要和判据段共用 —— judge::clearBase() 里是 prefs.clear(),会把同一
// 命名空间下的所有键一起抹掉,共用的话清一次基准就把校准值也清了。
constexpr const char* kNvsName = "gnstouch";

// ★★ v1.7.0 触摸分区(旧版"顶部存基准 / 中部进动图 / 底部切注入"三段**全部作废**):
//   主界面 y 0..239 星空区    -> **无动作**(误触安全 —— 比旧版"点顶部就改基准"安全得多)
//   主界面 y 240..279 按键条  -> x 四等分:BASE / CLR-BS / CHARTS / LOG
//                                ★ v1.8.3:CLR-BS **按住 5 s** 才清基准(短按无动作)
//   主界面 y 280..319 数据条  -> **长按 ≥800ms** 切报文窗 / 切回;短按无动作
//   第二界面                   -> 只认右上 BACK
//   第三界面(事件列表)        -> **PREV / NEXT 按钮整页翻**(★ v1.7.1 新增,顶栏第二行)
//                                / 上下滑动细滚 3 行 / 点 BACK 回主界面 / 点列表行进详情
//   第四界面(事件详情)        -> **左右滑动看上/下一条**(★ v1.7.1 新增) / 点 BACK 回第三界面
constexpr int kPressure    = 600;       // 按下判定压力阈值
constexpr int kLongPressMs = 800;       // 数据条长按阈值(切报文窗)
constexpr uint32_t kClearHoldMs = 5000; // ★ v1.8.3:CLR-BS 长按阈值(清基准,满 5 s 当场生效)
constexpr uint32_t kTuneHoldMs  = 1500; // ★ v1.10.0:CHARTS 长按阈值(进参数页,1.5 s)
constexpr int kSwipePx     = 40;        // 日志页滑动判定:竖直位移达到它才算翻页
constexpr int kDebounce = 350;          // 两次有效点击最小间隔 ms
constexpr int kTouchWarm = 2000;        // 上电头 2 秒不认触摸(见 poll 里的说明)

static void loadCal() {
  for (int i = 0; i < 5; ++i) {
    char k[6] = "cal0";
    k[3] = (char)('0' + i);
    calData[i] = (uint16_t)prefs.getUInt(k, calData[i]);
  }
}

static void saveCal() {
  for (int i = 0; i < 5; ++i) {
    char k[6] = "cal0";
    k[3] = (char)('0' + i);
    prefs.putUInt(k, calData[i]);
  }
  prefs.putUInt("cal_flag", 1);
}

inline void begin() {
  prefs.begin(kNvsName, false);

  if (prefs.getUInt("cal_flag", 0) == 1) {
    loadCal();
    calibrated = true;
    disp::tft.setTouch(calData);
    Serial.printf("[bench] touch: 校准值已载入 %d,%d,%d,%d,%d\n",
                  calData[0], calData[1], calData[2], calData[3], calData[4]);
  } else {
    Serial.println("[bench] touch: 首次开机 —— 请在屏上依次点 4 个十字完成校准");
    disp::tft.fillScreen(TFT_BLACK);
    disp::tft.setTextSize(2);
    disp::tft.setTextColor(TFT_WHITE, TFT_BLACK);
    disp::tft.setTextDatum(TC_DATUM);
    disp::tft.drawString("Touch the cross", SCR_W / 2, 140);
    disp::tft.setTextDatum(TL_DATUM);
    disp::tft.calibrateTouch(calData, TFT_WHITE, TFT_BLACK, 3);
    saveCal();
    calibrated = true;
    disp::tft.setTouch(calData);
    Serial.printf("[bench] touch: 校准完成并已写入 NVS %d,%d,%d,%d,%d\n",
                  calData[0], calData[1], calData[2], calData[3], calData[4]);
    disp::tft.fillScreen(CLR_PAPER);   // 校准完铺回页底色(黑底只在校准那几步用)
  }
}

inline void resetCal() {
  prefs.putUInt("cal_flag", 0);
  Serial.println("[bench] touch: 校准值已清除 —— 重启后重新校准");
}

// ★ 2026-10-15:`[[maybe_unused]]` —— 本函数**当前没有任何命令入口**(电脑串口的
//   命令表里没有它;C3 命令板的 `c = print cal` 指的是 **C3 自己那份**,不是这里)。
//   保留不删的理由:"校准值打不出来"是排障硬需求 —— 2026-10-15 触摸失灵那天,
//   手里就没有任何手段把 calData 读出来看。要启用:在 consoleKey() 加一个 case 调它
//   (注意小写 `c` 已被 clear base 占用,得换个大写或并入 `s`)。
[[maybe_unused]] inline void printCal() {
  Serial.printf("[bench] touch cal: %d,%d,%d,%d,%d\n",
                calData[0], calData[1], calData[2], calData[3], calData[4]);
}

// 按下点 / 当前点。★ 松开那一刻 getTouch 返回 false、坐标无效,所以"终点坐标"
// 必须靠按住期间一直存着的 moveX/moveY —— 否则滑动方向判不出来。
static uint16_t downX = 0, downY = 0, moveX = 0, moveY = 0;
static bool     downSeen = false;
static uint32_t pressMs  = 0;
static bool     clrFired = false;   // ★ v1.8.3:本次按压是否已经执行过"长按 5 s 清基准"
static bool     tuneFired = false;  // ★ v1.10.0:本次按压是否已经执行过"长按 1.5 s 进参数页"

// ---------------------------------------------------------------------------
//  ★★ v1.10.2:演示注入 —— 让**电脑**替手指按屏。
//  用户 2026-10-17 定:"我去碰的话,手在屏幕上就看着不好看,挡住一部分内容"。
//  做法:**只替换 poll() 读触摸那一行的返回值**,下面几百行(去抖 / 长按 5 s 进度 /
//  长按 1.5 s 进参数页 / 滑动翻页 / handleTap 全部分支)**一行都不改**。
//  为什么不做成"直接调 ui::toTune() / eventlog::selectRow()"这类语义命令:
//    ① 那样等于给同一件事开第二条实现路径 —— 迟早分叉(本项目已有先例:
//       'i' 注入与 VFO 偏移两套逻辑并存,最后两边一起删);
//    ② 演示时**按键高亮、长按走条、翻页手势都是真的**,观众看到的是设备在工作,
//       不是界面被硬切;语义命令做不到这一点。
//  ★ 安全边界:`k` **不进 linkKeyAllowed() 白名单**(仍 8 个) ⇒ C3 发不进来,
//    只有电脑 USB 串口能敲。上电 2 s 的 kTouchWarm 闸**照旧生效**(注入走同一条下游)。
namespace inj {
  // 一次动作 = 从 (x0,y0) 按下、持续 durMs、期间坐标线性走到 (x1,y1)。
  //   点   : 两点相同,durMs = 120
  //   长按 : 两点相同,durMs = 用户给的值(kh 150 257 1800 ⇒ 进参数页)
  //   划动 : 两点不同,durMs 默认 400
  struct Act { uint16_t x0, y0, x1, y1, durMs; };

  [[maybe_unused]] static Act      act       = {};
  [[maybe_unused]] static bool     queued    = false;   // 命令已到、还没被 poll 取走
  [[maybe_unused]] static bool     running   = false;   // 正在"按着"
  [[maybe_unused]] static uint32_t tStart    = 0;
  [[maybe_unused]] static uint8_t  downTicks = 0;       // 本段已吐的 down=true 圈数(封顶在 kMinDownTicks)
  [[maybe_unused]] static uint8_t  liftLeft  = 0;       // 还需吐几圈 down=false

  // ★★ 为什么要用**圈数**兜底,而不能只看时长:poll 一圈的间隔由主循环决定
  //   (GNSS 解析、1 Hz 绘制、WiFi 轮询都在同一个 loop 里),不同时刻差得多。
  //   若某圈特别慢,durMs 可能在**同一圈内**被跨过 ⇒ down 一次都没为 true ⇒
  //   handleTap 根本不会执行 —— 现象是"敲了命令,屏幕没反应",而且**不报错**。
  //   所以:每段动作至少吐 2 圈 true、结束后至少吐 2 圈 false。
  constexpr uint8_t kMinDownTicks = 2;
  constexpr uint8_t kLiftTicks    = 2;

  inline void queue(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint16_t ms) {
    act.x0 = x0; act.y0 = y0; act.x1 = x1; act.y1 = y1;
    act.durMs = (ms < 30) ? 30 : ms;   // 太短连一圈都撑不住,给个下限
    queued    = true;
  }
  inline void tap(uint16_t x, uint16_t y, uint16_t ms) { queue(x, y, x, y, ms); }

  inline bool busy() { return queued || running || liftLeft; }

  // 返回这一圈"屏上是否按着";x / y 同时给出当前坐标(划动时逐圈插值)。
  inline bool sample(uint32_t now, uint16_t& x, uint16_t& y) {
    if (queued) { queued = false; running = true; tStart = now; downTicks = 0; }
    if (running) {
      const uint32_t el = now - tStart;
      if (el < act.durMs || downTicks < kMinDownTicks) {
        uint32_t k = (act.durMs == 0) ? 1000u : (el * 1000u / act.durMs);
        if (k > 1000u) k = 1000u;
        const int32_t kk = (int32_t)k;
        x = (uint16_t)((int32_t)act.x0 + ((int32_t)act.x1 - (int32_t)act.x0) * kk / 1000);
        y = (uint16_t)((int32_t)act.y0 + ((int32_t)act.y1 - (int32_t)act.y0) * kk / 1000);
        if (downTicks < kMinDownTicks) ++downTicks;      // 封顶,不会溢出
        return true;
      }
      running  = false;
      liftLeft = kLiftTicks;                             // 这一圈起算"已松开"
      return false;
    }
    if (liftLeft) { --liftLeft; }
    return false;
  }
}  // namespace inj

inline bool hitBack(uint16_t x, uint16_t y) {
  return (x >= BACK_X && x < BACK_X + BACK_W && y >= BACK_Y && y < BACK_Y + BACK_H);
}

// ★★ v1.7.0:所有动作统一在"抬起"时执行 —— 只有这样才能分辨"点击"与"滑动"
//   (旧版在"刚按下"时动作,分不出滑动;而且按住期间不做任何事)。
// ★ v1.7.1:入口参数从"竖直滑动结论 swipe + dy"改成**原始位移 dx / dy**,
//   竖直与水平两个结论都挪到函数里算 —— 事件详情页要用**水平**滑动翻上/下一条,
//   原来那套只判竖直的判据根本表达不出来(而且从签名上就能看出少了哪个方向)。
inline void handleTap(uint16_t px, uint16_t py, int dx, int dy, uint32_t held) {
  const bool swipe  = (abs(dy) >= kSwipePx) && (abs(dy) > abs(dx));   // 竖直滑动
  const bool hswipe = (abs(dx) >= kSwipePx) && (abs(dx) > abs(dy));   // 水平滑动
  if (gLogOn)
    Serial.printf("[bench] touch: x=%u y=%u dx=%d dy=%d swipe=%d hswipe=%d held=%lums\n",
                  px, py, dx, dy, (int)swipe, (int)hswipe, (unsigned long)held);

  switch (ui::page) {
    // ---------- 第二界面(CHARTS):BACK + INTEGRITY 卡(→ 参数页) ----------
    case ui::PAGE_CHARTS:
      if (swipe) break;
      if (hitBack(px, py)) { ui::toMain(); break; }
      // ★ v1.10.0:点「INTEGRITY」这张卡直接进参数页。
      //   动机:看见 HPL 顶着 AL 就知道"该把 HPL LIM 放宽点",这时候人**已经盯着那张卡**了。
      //   命中区就是卡片本身(234x61),不是某个小按钮 —— 卡片在 y 192..252,离 BACK 很远。
      if (py >= charts::INT_Y && py < charts::INT_Y + charts::INT_H &&
          px >= charts::CARD_X && px < charts::CARD_X + charts::CARD_W) { ui::toTune(); }
      break;

    // ---------- 第五界面(参数列表):BACK / DEFAULTS / 点行进编辑 ----------
    case ui::PAGE_TUNE: {
      if (swipe) break;
      if (hitBack(px, py)) { ui::toMain(); break; }
      if (tune::hitDefaults((int)px, (int)py)) {
        tune::resetAll();
        tune::tick();
        Serial.println("[bench] tune: all params back to defaults");
        break;
      }
      const int r = tune::hitRow((int)px, (int)py);
      if (r >= 0) ui::toTpEdit(r);
      break;
    }

    // ---------- 第六界面(单项编辑):BACK=回列表 / 四步进键 / DEFAULT / DONE ----------
    case ui::PAGE_TPEDIT: {
      if (swipe) break;
      if (hitBack(px, py)) { ui::backFromTpEdit(); break; }
      const int s = tune::hitStep((int)px, (int)py);
      if (s) {
        tune::bump(tune::curEdit(), (s > 0) ? +1 : -1, (s > 0) ? s : -s);
        tune::tick();
        break;
      }
      const int a = tune::hitAction((int)px, (int)py);
      if (a == 1) { tune::resetOne(tune::curEdit()); tune::tick(); }
      else if (a == 2) { ui::backFromTpEdit(); }        // DONE 与 BACK 等价(都会 commit)
      break;
    }

    // ---------- 第三界面(事件列表) ----------
    case ui::PAGE_LOG:
      if (swipe) { eventlog::scroll(dy < 0 ? +3 : -3); break; }   // 上滑 = 看更新的(细滚 3 行)
      if (hitBack(px, py)) { ui::toMain(); break; }               // 右上 BACK 回主界面
      // ★ v1.7.1:翻页按钮。**排在 selectRow 之前** —— 按钮行 y 28..49 本就在 kTfaH=54 之上、
      //   命不中列表行(selectRow 首句就把 y < kTfaH 挡了),但顺序写明白:
      //   以后若把按钮挪进列表区,这里不至于"选了行、又翻页"双重触发。
      {
        const int d = eventlog::hitPgBtn((int)px, (int)py);
        if (d) { eventlog::page(d); eventlog::tick(false); break; }
      }
      if (eventlog::selectRow((int)py)) ui::toEvt();
      break;

    // ---------- 第四界面(事件详情):BACK + 左右滑动切换上/下一条 ----------
    case ui::PAGE_EVT:
      // ★ v1.7.1:左滑 = 下一条(更旧,与列表"往下 = 更旧"一致)/ 右滑 = 上一条(更新)。
      //   水平滑动优先判 —— 它和点 BACK 不可能同时成立(滑够 40px 早离开 BACK 那块 64x24 了)。
      if (hswipe) { ui::evtStep(dx < 0 ? +1 : -1); break; }
      if (!swipe) {
        // ★ v1.8.0:PREV/NEXT 按钮(与滑动并存 —— 按钮是"看得见"的入口,滑动是"顺手"的入口)
        const int d = eventlog::hitPgBtnD((int)px, (int)py);
        if (d) { ui::evtStep(d); break; }
        if (hitBack(px, py)) ui::backFromEvt();
      }
      break;

    // ---------- 主界面 ----------
    default: {
      if (swipe) break;
      // ★ v1.7.2:WiFi 信息卡在屏上时,**任意一次点击只负责收卡** —— 不再顺手触发别的动作,
      //   免得"想关卡片,结果把基准给清了"。放在最前面,四条分支一个都别想抢先。
      if (disp::cardUp()) { disp::hideWifiCard(); break; }
      if (py >= BTN_Y && py < BTN_Y + BTN_H) {          // 按键条:x 四等分
        switch (px / BTN_W) {
          case 0:                                        // BASE —— 存基准(★ v1.8.6:P1 门限 + P2 采样)
            // 三个入口共用 judge::requestBase():门限不过就拒绝,过了也只是**开始**
            // 10 s 采样,真正的落盘在 judge::sampleStep() 里(那时才写 NVS)。
            {
              char m[40] = "";
              judge::requestBase(m, sizeof(m));
              wifi10::rptText(m);
            }
            disp::resetPaintCaches();                    // 按键底色(BASE 三态/采样进度)+ 星图都要重画
            disp::tick();
            break;
          case 1:                                        // CLR-BS —— 清基准(★ 必须长按 5 s)
            // ★ v1.8.3:INJECT 已删,这一格改为清基准。短按在这里**什么都不做**,
            //   只打一行日志 —— 清基准是破坏性动作,误触一下基准就没了。
            //   真正的执行在 poll() 的"按住"分支里(满 5 s 当场生效,不等松手);
            //   这里只兜住"短按后松手"这一种结束方式。
            if (!judge::baseSet) Serial.println("[bench] touch: CLR-BS - no base to clear");
            else Serial.println("[bench] touch: CLR-BS short tap ignored (hold 5s to clear)");
            break;
          case 2: ui::toCharts(); break;                 // CHARTS
          default: ui::toLog();   break;                 // LOG
        }
      } else if (py >= DATA_Y) {                         // 数据条:长按切报文窗
        if (held >= kLongPressMs) disp::toggleMsg();
      } else if (py < HUD_YT + HUD_H && px >= HUD_XR) {  // ★ v1.7.2 右上角 WiFi 图标 -> 弹信息卡
        //   热点 = **整个右上四角块**(92x24),不是那 22px 的图标本身 —— 小目标按不准,
        //   而这块地方本来就没有别的动作(旧版"星空区一律无动作")。判据与四角块几何同源。
        disp::toggleWifiCard();
        disp::tick();                                    // 立刻画出来(不等下一拍)
      }
      // 星空区**其余部分**(y 0..239)仍是无动作 —— 误触安全
      break;
    }
  }
}

inline void poll() {
  uint16_t x = 0, y = 0;
  uint32_t now = millis();
  // ★★ v1.10.2:**唯一**的注入点 —— 有注入动作时,合成坐标**替代** getTouch 的返回值。
  //   往下所有判据(去抖、长按 5 s、长按 1.5 s、滑动、handleTap)完全不知情,
  //   所以"电脑替手按"与"手指按"走的是同一段代码。
  bool down = inj::busy() ? inj::sample(now, x, y)
                          : disp::tft.getTouch(&x, &y, kPressure);

  // ★ 上电头 2 秒不认任何触摸 —— 触摸屏和它的 ADC 刚上电时读数不稳,
  //   实测会冒出一个"幽灵点击"。wasDown 初值 true 是第一道闸,这里是第二道,
  //   两条一起兜(缺一条都可能漏进去)。
  if (now < kTouchWarm) { wasDown = down; return; }

  if (down) {
    // ★★ v1.10.2 修:起点判据从 `!wasDown` 改成 `!downSeen`。
    //   原写法依赖"上一圈是抬起的"。但**开机 splash 是阻塞循环**(约 7 s:
    //   展开 kExpandMs=5000 + 定格 kHoldMs=2000),主循环第一次进 poll() 时
    //   `wasDown` 还停在**初值 true**(第一道闸)—— 于是这一次真实按下被当成
    //   "本来就一直按着",起点不记、downSeen 不置位 ⇒ **整次点击静默丢失**。
    //   2026-10-17 实测就是被这一条吃掉了演示脚本的第一击(`kt 150 257` 按理要
    //   进图表页,屏上没反应、串口只有 "inj: tap" 一行,没有任何报错)。
    //   换成 `!downSeen`(false = 这次按压还没记过起点)更直接,且与 wasDown 解耦。
    if (!downSeen) { downX = x; downY = y; pressMs = now; downSeen = true;
                     clrFired = false; tuneFired = false; }    // 刚按下:记起点
    moveX = x; moveY = y;                                                    // 按住期间持续更新终点

    // ★★ v1.8.3:主界面第 2 键(CLR-BS)**按住 5 s 当场清基准**,不等松手。
    //   为什么不放在 handleTap 里:那里只有"抬起"这一刻 —— 按 5 秒期间屏上毫无变化,
    //   用户会怀疑"到底按到了没有"。这里每圈算一次档位(1..7),按键底部进度条逐格
    //   长满,满 5 s 立刻执行;手指滑出该键(超过 kSwipePx)视为放弃,进度条缩回去。
    //   短按不触发(见 handleTap 的 case 1)—— 清基准是破坏性动作,必须防误触。
    if (downSeen && ui::page == ui::PAGE_MAIN && judge::baseSet && !clrFired) {
      const bool inKey = (downY >= BTN_Y && downY < BTN_Y + BTN_H) && ((downX / BTN_W) == 1);
      const bool still = (abs((int)x - (int)downX) < kSwipePx) && (abs((int)y - (int)downY) < kSwipePx);
      if (inKey && still) {
        const uint32_t h  = now - pressMs;
        const uint8_t  st = (h >= kClearHoldMs) ? 7 : (uint8_t)(1 + (h * 6u) / kClearHoldMs);
        if (st != disp::gClrHoldStep) { disp::gClrHoldStep = st; disp::tick(); }
        if (h >= kClearHoldMs) {
          clrFired = true;
          judge::clearBase();
          disp::gClrHoldStep = 0;
          Serial.println("[bench] touch: base cleared (held 5s)");
          disp::resetPaintCaches();        // 键色(绿/灰对调)+ HUD 的 DRIFT 一起重画
          disp::tick();
        }
      } else if (disp::gClrHoldStep) {     // 滑出/滑开:进度作废
        disp::gClrHoldStep = 0;
        disp::tick();
      }
    }

    // ★★ v1.10.0:主界面第 3 键(CHARTS)**按住 1.5 s → 参数页**,不等松手。
    //   为什么不是长按 LOG(那本是第一候选):LOG 短按要进事件列表,再挂一个长按
    //   等于同一格背两种时间语义;CHARTS 这一格是用户 2026-10-17 自己挑的,照办。
    //   与 CLR-BS 共用同一套"逐档长满"的观感(进度条由 btnState(2) 提供),
    //   但阈值只有 1.5 s —— 它**不具破坏性**,不需要按那么久,也不该那么久。
    if (downSeen && ui::page == ui::PAGE_MAIN && !tuneFired) {
      const bool inKey = (downY >= BTN_Y && downY < BTN_Y + BTN_H) && ((downX / BTN_W) == 2);
      const bool still = (abs((int)x - (int)downX) < kSwipePx) && (abs((int)y - (int)downY) < kSwipePx);
      if (inKey && still) {
        const uint32_t h  = now - pressMs;
        const uint8_t  st = (h >= kTuneHoldMs) ? 7 : (uint8_t)(1 + (h * 6u) / kTuneHoldMs);
        if (st != disp::gChartsHoldStep) { disp::gChartsHoldStep = st; disp::tick(); }
        if (h >= kTuneHoldMs) {
          tuneFired = true;
          disp::gChartsHoldStep = 0;
          const ui::Page before = ui::page;
          ui::toTune();                       // 内部会 releaseMain + 画参数页
          // 没进去(全屏 sprite 分配失败):主界面还在,把残留的进度条擦掉
          if (ui::page == before) { disp::resetPaintCaches(); disp::tick(); }
        }
      } else if (disp::gChartsHoldStep) {     // 滑出/滑开:进度作废
        disp::gChartsHoldStep = 0;
        disp::tick();
      }
    }

    wasDown = true;
    return;
  }

  // ---- 已抬起 ----
  if (wasDown && downSeen) {
    downSeen = false;
    if (disp::gClrHoldStep)    { disp::gClrHoldStep = 0;    disp::tick(); }  // 进度条擦掉
    if (disp::gChartsHoldStep) { disp::gChartsHoldStep = 0; }                 // ★ v1.10.0:已进参数页,无需重画
    // 长按已经生效:这次抬手不能再当成一次"点击"(否则 handleTap 会再打一行
    // "short tap ignored",现场看着自相矛盾;CHARTS 那格更糟 —— 会把参数页顶掉)。
    if (clrFired || tuneFired) { clrFired = false; tuneFired = false; wasDown = down; return; }
    if (now - lastMs > kDebounce) {                    // 两次有效动作最小间隔
      lastMs = now;
      const int dy = (int)moveY - (int)downY;
      const int dx = (int)moveX - (int)downX;
      handleTap(downX, downY, dx, dy, now - pressMs);   // swipe/hswipe 的判定都在 handleTap 里
    }
  }
  wasDown = down;
}

#else    // 未启用触摸 —— 给同名的空实现,这样主循环不用写条件编译

inline void begin()    {}
inline void poll()     {}
inline void resetCal() { Serial.println("[bench] touch: 本固件未启用触摸(ENABLE_TOUCH=0)"); }
[[maybe_unused]] inline void printCal() {}   // 见上面那条:目前无命令入口,标上备用

#endif

}  // namespace touch

// ============================================================================
//  第 5 段:串口控制台 + 主循环
//  两条控制路径并存:串口敲单字符,或者直接点屏。
// ============================================================================
// ★ v1.7.0:主界面旧的 tMet / tStatus / tClk / tWifi 四个节拍**全部删除** ——
//   新版主界面只有一个刷新入口 disp::tick()(内部按块比对,内容没变就一个像素不动)。
static uint32_t tMsg = 0, tLed = 0, tMain = 0, tCharts = 0, tEvt = 0;

// ============================================================================
//  复位原因 / 重启计数 / 主循环卡顿(2026-10-01 晚加)
// ----------------------------------------------------------------------------
//  为什么要有这一段 —— "屏不动了,像死机" 其实是三种完全不同的病,现场分不出来:
//
//   A) 芯片被**外部复位**了。ILI9341 自带显存、不需要主机不停刷新,所以芯片一停,
//      屏上就永远定格在最后一帧 —— 肉眼看和死机一模一样,连"没数据"的星号都不会变。
//      最常见的凶手就是电脑端串口工具:ESP32-S3 用 USB-Serial-JTAG 当串口时,
//      硬件把 RTS 当 EN(复位)、DTR 当 GPIO0(下载模式)。串口工具一开端口
//      默认就会去拉这两条线 —— 芯片当场被摁住,屏定格,而串口那边还连得上、
//      看着一切正常。这也是本项目一个老坑:工具一开端口,板上基准就没了(base ---)。
//   B) 某个阻塞调用把主循环卡住几秒。旧版最典型的是串口发 'l' 试灯 ——
//      它会连续 delay(1000) 挨个点 8 个候选脚,约 8 秒内主循环完全不动
//      (v1.5.10 已删除该命令)。现在若还见到 >1500ms,重点查 NVS 落盘、
//      首次 sprite 分配、WiFi 重连这三处。
//   C) 固件真的挂了。这种复位原因会写成 PANIC / TASK_WDT / INT_WDT。
//
//  esp_reset_reason() 是 ROM 记在硬件里的**事实**,不是推测 —— 开机横幅直接告诉
//  你这次属于 A / B / C 的哪一种;gLoopMaxGap 再把 B 量化成毫秒数。
//  "开机次数 / 上次复位原因码"放在 RTC 内存里,**软复位不丢**(拔电才清),
//  所以"开机第 N 次"不拔电就一直涨 —— 一眼看出中间偷偷复位过几回。
// ============================================================================
// ★ 这里刻意用 RTC_NOINIT_ATTR 而不是 RTC_DATA_ATTR:
//   RTC_DATA_ATTR 允许带初值,而"带初值"在某些启动流程下会被重新抄一遍 ——
//   那样计数永远停在 1,等于白记。NOINIT 明确"开机绝不动这块内存",
//   掉电才清、软复位/看门狗复位/USB 复位都保留,正是这里要的语义。
//   [0] 哨兵(必须等于 kRtcMagic,否则说明这块内存是崭新的)
//   [1] 开机次数   [2] 上一次的复位原因码
constexpr int kRtcMagicSlot = 0, kRtcBootSlot = 1, kRtcRstSlot = 2;
constexpr uint32_t kRtcMagic = 0x474E5353u;       // "GNSS"
RTC_NOINIT_ATTR static uint32_t gRtc[3];

// 主循环卡顿统计:loop() 每圈记一次,两次之间的间隔就是"这一圈花了多久"。
static uint32_t gLoopLastMs = 0;      // 上一圈的时刻
static uint32_t gLoopMaxGap = 0;      // 本次开机见过的最长一圈(ms)
static uint32_t gLoopStalls = 0;      // 超过 1500ms 的圈数(点一次 'l' 计一次)
static const char* gRstText = "?";    // 本次复位原因(人话),给 's' 状态行用

static const char* resetReasonText(esp_reset_reason_t r) {
  switch (r) {
    case ESP_RST_POWERON:    return "POWERON(正常上电)";
    case ESP_RST_EXT:        return "EXT(复位脚)";
    case ESP_RST_SW:         return "SW(软件复位)";
    case ESP_RST_PANIC:      return "PANIC(程序异常)";
    case ESP_RST_INT_WDT:    return "INT_WDT(中断看门狗)";
    case ESP_RST_TASK_WDT:   return "TASK_WDT(任务看门狗)";
    case ESP_RST_WDT:        return "WDT(看门狗)";
    case ESP_RST_DEEPSLEEP:  return "DEEPSLEEP(深睡唤醒)";
    case ESP_RST_BROWNOUT:   return "BROWNOUT(电压跌落→查供电)";
    case ESP_RST_SDIO:       return "SDIO";
    case ESP_RST_USB:        return "USB(被USB主机复位 ← 串口工具拉DTR/RTS)";
    case ESP_RST_JTAG:       return "JTAG(被调试器复位)";
    case ESP_RST_EFUSE:      return "EFUSE";
    case ESP_RST_PWR_GLITCH: return "PWR_GLITCH(电源毛刺→查供电)";
    case ESP_RST_CPU_LOCKUP: return "CPU_LOCKUP(CPU锁死)";
    default:                 return "UNKNOWN";
  }
}

// 开机时调一次:算出本次复位原因,并把"上次是第几次开机 / 上次被谁复位"留在 RTC 里。
// 返回本次复位原因码,方便调用处再打一行。
static esp_reset_reason_t bootDiag() {
  if (gRtc[kRtcMagicSlot] != kRtcMagic) {   // 这块内存是崭新的 —— 这次是真的拔过电
    gRtc[kRtcMagicSlot] = kRtcMagic;
    gRtc[kRtcBootSlot]  = 0;
    gRtc[kRtcRstSlot]   = 0;
  }
  gRtc[kRtcBootSlot]++;
  esp_reset_reason_t r = esp_reset_reason();
  gRstText = resetReasonText(r);
  return r;
}

static void serialReport(bool force);   // 前置声明,串口命令 s 会用到

// ============================================================================
//  WiFi 存档(v1.5.6 起;★ v1.5.17 按用户 2026-10-11 定的机制重写)
// ----------------------------------------------------------------------------
//  ★ 机制(用户 2026-10-11 明确,参照另一个工程 RF_Source_Controller 的
//    wifiSaveNetwork() / wifiOpen() —— 那边跑通过的做法):
//    ① 扫 -> 出列表 -> 点一条 SSID -> 弹键盘敲密码 -> **立刻连**;
//    ② **拿到正确 IP 才算成功**,成功之后才把 SSID+密码**一起**写进 NVS 台账;
//    ③ 连不上 ⇒ **一个字都不存**。用户原话:"不是随便找到一个、连都没连上
//       我就把它存起来,那个是没用的" —— 明确否决"扫到就存"。
//    ④ 台账 **10 组**:新条目插**最前**;同 SSID 先删旧再插最前;满 10 丢**末尾(最旧)**。
//    ⑤ 开机不再无脑连"存档第 1 条" —— 先扫一遍,只在
//       "**已存档 ∧ 在范围内**"里挑**信号最强**的那条去连;一条都不在范围就不连。
//  存储:Preferences + 独立命名空间 "wifi10"(与触摸校准互不干扰,掉电不擦)。
//        ★ 为什么不用 PSRAM 放台账:PSRAM 是**易失内存**,一断电/一复位全丢,
//          做不到"永久保存"。PSRAM 只适合放"当次扫描列表"这种临时数据。
//  ★ 全程**非阻塞**:参考实现里那个"等 10 秒"是阻塞 delay 写的,它的主循环是
//    专用信号源控制器,堵 10 s 无所谓;本机不行 —— 镜像每秒十几行、触摸、PPS
//    积分、色带判据全在主循环里。所以这里做成**状态机**:`poll()` 每圈问一句
//    "IP 到了吗 / 超时了吗",10 s 超时即判失败、不存。行为一模一样,主循环不堵。
//  命令(先敲 w,再跟子命令):
//    wadd  <ssid> <pass>   试着连上,**连上才存**(不是存了再连)
//    wseed <ssid> <pass>   只写台账、不连(C3 的 PUSH 整表抄过来用这条)
//    wtry  [n]              ★ v1.5.19:从 #1 起**挨个试连**到 #n(缺省 = 全部);连上即停,
//                          失败自动跳下一条,全败回 `wifi fail all`。C3 的 PUSH 收尾发它。
//                          快失败:reason=201(不在范围)/202(密码错)不等满 10 s,立刻跳。
//    wlist / wdel <n> / wconn [n] / wscan
//  组号 n 从 1 数(1 = 最新放进去的那条);密码可以含空格(a2 取行内剩余全部),
//  SSID 不能含空格。
//
//  ★★ 为什么扫描前必须先 disconnect()(scanStart 里那一行):
//     ESP-IDF 的 esp_wifi.h 明写一条限制 ——
//       ESP_ERR_WIFI_STATE: "WiFi still connecting when invoke esp_wifi_scan_start"
//     即:**连接尝试还在跑的时候,驱动不允许启动扫描**。Arduino 层把它转成
//     WiFiScanClass 的 -2(WIFI_SCAN_FAILED),而屏上文案却是 "no AP found" ——
//     2026-10-11 实测:C3 扫不到热点(=/c3] scan: C3 radio got -2 AP)的真根因
//     就是它开机就在连一条连不上的存档网,连接尝试一直挂着。
//     S3 当时扫得动只是因为它的存档是**空的**;一旦存进任何一条,它也会得同样的病。
// ============================================================================
namespace wifi10 {

constexpr int         kMax     = 10;
constexpr const char* kNvsName = "wifi10";

struct Entry { char ssid[33]; char pass[65]; };   // SSID ≤32+1;WPA2 密码 ≤64+1
static Entry       list[kMax];
static int         cnt = 0;        // 已存组数 0..10
static int         cur = 0;        // 正在连的组号(1 起算),0=没在连
static Preferences prefs;
static bool        wifiUp = false; // 射频这次开机起来过没有(决定 radioOff 要不要 deinit)

// ★ v1.5.19:巡台账("挨个试")状态 —— C3 的 PUSH 收尾会发 `wtry <n>` 进来
//   用户 2026-10-12 定:"PUSH 完就该去连,第一个不行就连第二个"。
static bool     trOn   = false;    // true = 正在从 #1 往 #trN 挨个试连
static int      trN    = 0;        // 本次要试到第几条(1 起算)
static int      trIdx  = 0;        // 当前正试第几条
static volatile uint8_t evFast = 0; // 事件回调置的"快失败"reason(201/202),poll() 里消费

static void load() {
  prefs.begin(kNvsName, false);
  cnt = (int)prefs.getUInt("cnt", 0);
  if (cnt > kMax) cnt = kMax;
  char k[16];   // ★ 2026-10-15:8 -> 16(与 C3 侧**同步改**,见 tools/check_wifi10_sync.py)。
                //   原来刚好放下 "ssid10"(7 字节),全靠 kMax≤10 兜着 —— 一旦 kMax 改成 100
                //   就会撞名(键退化成 "ssid1")。加宽后编译器也不再报 -Wformat-truncation。
  for (int i = 0; i < cnt; ++i) {
    snprintf(k, sizeof(k), "ssid%d", i + 1);
    prefs.getString(k, list[i].ssid, sizeof(list[i].ssid));
    snprintf(k, sizeof(k), "pass%d", i + 1);
    prefs.getString(k, list[i].pass, sizeof(list[i].pass));
  }
}

static void saveAll() {
  prefs.putUInt("cnt", (uint32_t)cnt);
  char k[16];   // ★ 2026-10-15:8 -> 16(与 C3 侧**同步改**,见 tools/check_wifi10_sync.py)。
                //   原来刚好放下 "ssid10"(7 字节),全靠 kMax≤10 兜着 —— 一旦 kMax 改成 100
                //   就会撞名(键退化成 "ssid1")。加宽后编译器也不再报 -Wformat-truncation。
  for (int i = 0; i < cnt; ++i) {
    snprintf(k, sizeof(k), "ssid%d", i + 1); prefs.putString(k, list[i].ssid);
    snprintf(k, sizeof(k), "pass%d", i + 1); prefs.putString(k, list[i].pass);
  }
}

// ============================================================================
//  台账操作:插**最前** + 同 SSID 去重 + 满 10 丢**最旧**(用户 2026-10-11 定)
//  (旧版是"追加到末尾、满了整体前移" —— 那样同一个 SSID 能占满 10 格,已改)
// ============================================================================
static int findSsid(const char* s) {
  for (int i = 0; i < cnt; ++i) if (!strcmp(list[i].ssid, s)) return i;
  return -1;
}

static void insertFront(const char* ssid, const char* pass) {
  const int ex = findSsid(ssid);
  if (ex >= 0) {                                   // 已有同 SSID:更新密码 + 提到最前
    snprintf(list[ex].pass, sizeof(list[ex].pass), "%s", pass);
    const Entry e = list[ex];
    for (int i = ex; i > 0; --i) list[i] = list[i - 1];
    list[0] = e;
    return;
  }
  if (cnt == kMax) cnt = kMax - 1;                 // 满:丢掉末尾那条(最旧)
  for (int i = cnt; i > 0; --i) list[i] = list[i - 1];
  snprintf(list[0].ssid, sizeof(list[0].ssid), "%s", ssid);
  snprintf(list[0].pass, sizeof(list[0].pass), "%s", pass);
  ++cnt;
}

// ============================================================================
//  结果回执队列:连接结果要**告诉 C3**(C3 屏上要显示 "S3 到底连上没有")
//  ★ 为什么必须新加这条:benchlink 对任何命令都**无条件回显** "@s ok wadd ..." ——
//    回显 ≠ 连上。不给结果回执的话,C3 完全不知道 S3 那边成没成。
// ============================================================================
constexpr int kRptMax = 14;                  // ★ v1.5.20:6 -> 14
// ★ 为什么要 14:队列是"FIFO,满了丢最旧",而 benchlink 每圈只发 2 条 ⇒
//   `wlist` 一次要回最多 10 行,旧容量 6 会把前 4 行顶掉(C3 上看到的台账缺头)。

static char rptQ[kRptMax][56];
static int  rptH = 0, rptN = 0;
static void queueRpt(const char* s);        // 定义在下面(先声明,好让 rptText 用)
static void rptText(const char* s);         // ★ v1.5.20:定义在 rptNext 之后

static void queueRpt(const char* s) {
  if (rptN == kRptMax) { rptH = (rptH + 1) % kRptMax; --rptN; }   // 满了丢最旧
  snprintf(rptQ[(rptH + rptN) % kRptMax], sizeof(rptQ[0]), "%s", s);
  ++rptN;
}

// 给 benchlink 取一行待发(返回 false = 没有)
static bool rptNext(char* out, size_t cap) {
  if (!rptN) return false;
  snprintf(out, cap, "%s", rptQ[rptH]);
  rptH = (rptH + 1) % kRptMax;
  --rptN;
  return true;
}

// ★ v1.5.20:把**命令的结果**(不是"收到了")回给 C3 命令板。
// ----------------------------------------------------------------------------
//  背景(用户 2026-10-12 23:5x):"很多 C3 的控制命令,比如 status 发给 S3 没反馈。
//  你要分清楚哪些命令给 S3 是 S3 在自身操作的;哪些命令是要求 S3 返回给 C3 显示"。
//  实话是:以前**只有** wscan / wadd / wconn / wtry 会回内容,其余命令 S3 一律只回
//  一行 `@s ok <原命令>` 的空回显,而命令真正的输出全打在 S3 自己的串口上 ——
//  所以 C3 屏幕上什么都看不到。
//
//  这一条就是补上那些"要回 C3 显示"的命令的通道:
//    · 复用现成的 rptQ -> benchlink 的 `@s ok <行>` 发送通道 ⇒ **协议零改动**
//    · 加 "txt " 前缀,C3 侧 drainLink() 认它,显示成 "* <文本>"
//    · ⚠ 内容必须**纯 ASCII 且 ≤32 字符**:C3 输出卡一行 34 列(kOutCols),
//      还要让出 "* " 前缀;中文在那边是 GLCD 字体,一个汉字会画成 3 个乱符号。
//    · 谁该调它 = "命令的结果对用户有意义"的那些(status/温湿度/PPS/台账/开关状态);
//      纯动作类(mirror 开关、切页)也回一行状态,让用户有个确认。
static void rptText(const char* s) {
  char b[56];
  snprintf(b, sizeof(b), "txt %s", s);
  queueRpt(b);
}

// ============================================================================
//  "连上才存"状态机(非阻塞)
// ============================================================================
constexpr uint32_t kConnMs = 10000;        // 等 IP 的上限 —— 与参考实现的 10 s 一致

static bool     csBusy = false;
static uint32_t csT0   = 0;
static char     csSsid[33] = "";
static char     csPass[65] = "";

static void scanStart(bool forBoot);       // 定义在本段之后(见下 scanStart 段头)
static void tryAll(int n);                 // ★ v1.5.20:定义在 connFailed 之后(begin 要用)

// 没连上就把射频驱动那块内存**真的**还给堆(见 v1.5.16 说明:光 mode(WIFI_OFF)
// 只调 esp_wifi_stop(),esp_wifi_init() 分配的那块不释放,必须补 deinit)。
// 已连接 / 正在连 -> 不动(连接要维持)。
static void radioOff() {
  if (csBusy) return;
  if (WiFi.status() == WL_CONNECTED) return;
  if (!wifiUp) return;
  WiFi.mode(WIFI_OFF);
  esp_wifi_deinit();
  wifiUp = false;
}

// ★★ 扫描前把射频"归零"(2026-10-11 22:1x **实测**出来的第二层根因)
// ----------------------------------------------------------------------------
//  第一层(上一版已修):ESP-IDF 不允许"连接尝试在跑的时候启动扫描"
//    -> esp_wifi_scan_start() 返回 ESP_ERR_WIFI_STATE -> Arduino 层 -2。
//    当时的修法是先 `WiFi.disconnect()` 把连接断掉。
//  ★ 第二层(实测**不够**的原因,读完 esp32 核心源码才找着):
//    `libraries/WiFi/src/STA.cpp` 第 138-172 行的 STA 事件回调里写着 ——
//       else if (first_connect) { first_connect = false; DoReconnect = true; }  // 开机第一次断线:无条件重连一次
//       else if (getAutoReconnect() && _is_staReconnectableReason(reason)) DoReconnect = true;
//       ...
//       if (DoReconnect) { disconnect(); connect(); }        // ← 立刻又 esp_wifi_connect()
//    而 `_autoReconnect` 的**默认值是 true**(STA.cpp L231)。
//    ⇒ 我们这边一喊 `WiFi.disconnect()`,核心在事件回调里**马上又 connect 一次**,
//      射频一直处于 connecting,`esp_wifi_scan_start()` 就一直是 -2。
//      这不是"没断干净",是"断了马上又被接上"。
//  所以两手都要:
//    ① `WiFi.setAutoReconnect(false)` —— 关掉核心的自动重连(重试与否由我们的
//       状态机决定,不能让核心背着我们偷偷重连);
//    ② `WiFi.mode(WIFI_OFF)` -> `WIFI_STA` —— 把 STA 状态机整个停一轮再起来,
//       靠 esp_wifi_stop()/start() 强制回到 IDLE(connecting 标志随之清掉)。
//       比 disconnect() 硬,多花约 120ms,换的是"一定能扫"。
//  已连上(真拿到 IP 的)绝不动 —— 扫描不该掐断一条好连接。
// ----------------------------------------------------------------------------
static void radioIdleForScan() {
  if (WiFi.status() == WL_CONNECTED) { Serial.println("[wifi] scan: keep the live connection"); return; }
  WiFi.setAutoReconnect(false);
  if (!wifiUp) return;
  WiFi.mode(WIFI_OFF);
  delay(60);
  WiFi.mode(WIFI_STA);
  delay(60);
}

// 非阻塞发起"连上才存"。成功/失败都在 poll() 里收口。
// ★ v1.5.18:断连 reason 日志(移植自 C3 v0.8.2,那边已实测有效)。
//   这是判别"密码错 / 射频坏 / 距离远 / AP 拒绝"的**唯一硬证据**。
//   ★ 社区判例:reason=15 + 信号强 + 密码对 ≈ "AP 拒绝与你完成密钥交换"
//     (MAC 被黑名单 / mesh 节点故障),不是凭据问题。
static const char* wifiReasonName(uint8_t r) {
  switch (r) {
    case 1:   return "UNSPECIFIED";
    case 2:   return "AUTH_EXPIRE";
    case 3:   return "AUTH_LEAVE";
    case 4:   return "ASSOC_EXPIRE";
    case 5:   return "ASSOC_TOOMANY";
    case 6:   return "NOT_AUTHED";
    case 7:   return "NOT_ASSOCED";
    case 8:   return "ASSOC_LEAVE";
    case 9:   return "ASSOC_NOT_AUTHED";
    case 15:  return "4WAY_HANDSHAKE_TIMEOUT";
    case 16:  return "GROUP_KEY_UPDATE_TIMEOUT";
    case 200: return "BEACON_TIMEOUT";
    case 201: return "NO_AP_FOUND";
    case 202: return "AUTH_FAIL";
    case 203: return "ASSOC_FAIL";
    case 204: return "HANDSHAKE_TIMEOUT";
    case 205: return "CONNECTION_FAIL";
    default:  return "OTHER";
  }
}

// 事件回调 —— 只打日志,不改行为(自动重连已由 setAutoReconnect(false) 关掉)
static void onWifiEvt(arduino_event_id_t id, arduino_event_info_t info) {
  if (id == ARDUINO_EVENT_WIFI_STA_CONNECTED) {
    Serial.printf("[wifi] EVENT connected ssid=\"%.*s\" ch=%u auth=%d\n",
                  (int)info.wifi_sta_connected.ssid_len,
                  (const char*)info.wifi_sta_connected.ssid,
                  (unsigned)info.wifi_sta_connected.channel,
                  (int)info.wifi_sta_connected.authmode);
  } else if (id == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
    Serial.printf("[wifi] EVENT DISCONNECTED ssid=\"%.*s\" reason=%u (%s) rssi=%d\n",
                  (int)info.wifi_sta_disconnected.ssid_len,
                  (const char*)info.wifi_sta_disconnected.ssid,
                  (unsigned)info.wifi_sta_disconnected.reason,
                  wifiReasonName(info.wifi_sta_disconnected.reason),
                  (int)info.wifi_sta_disconnected.rssi);
    // ★ v1.5.19:快失败信号 —— 只在"正在连"时收,避免巡航/空闲期的杂散事件误触发。
    //   201 NO_AP_FOUND = 根本扫不到这个 AP(不在范围);202 AUTH_FAIL = 密码错。
    //   这两种再等满 10 s 纯属浪费,直接交给 poll() 跳下一条。
    const uint8_t fr = (uint8_t)info.wifi_sta_disconnected.reason;
    if (csBusy && (fr == 201 || fr == 202)) evFast = fr;
  }
}

static void startConnect(const char* ssid, const char* pass) {
  if (!ssid || !ssid[0]) { Serial.println("[wifi] usage: wadd <ssid> <pass>"); return; }
  evFast = 0;                                     // ★ v1.5.19:新一次尝试,清掉上一条的快失败信号

  if (WiFi.status() == WL_CONNECTED && WiFi.SSID() == ssid) {   // 已经连着它 -> 直接算成功
    trOn = false;                                 // ★ v1.5.19:巡台账到此结束(它已经连着)
    insertFront(ssid, pass);
    saveAll();
    // ★ v1.5.21(用户 2026-10-14 定):回执**必须带 SSID**。
    //   用户原话:"不能只说 wifi 然后给我一个 IP 就完了,你得告诉我你连在哪个热点上"。
    //   分两行:1) `wifi ok <ssid>` 2) `ip <ip>`(走 rptText -> C3 显示 `[S3] ip …`)。
    char b[56];
    snprintf(b, sizeof(b), "wifi ok %.18s", ssid);
    queueRpt(b);
    snprintf(b, sizeof(b), "ip %s", WiFi.localIP().toString().c_str());
    rptText(b);
    Serial.printf("[wifi] already on \"%s\" -> saved to front (%d/%d)\n", ssid, cnt, kMax);
    return;
  }

  WiFi.setAutoReconnect(false);   // ★ 别让核心在 disconnect 事件里偷偷再 connect(见 radioIdleForScan)
  if (wifiUp) { WiFi.mode(WIFI_OFF); delay(60); }   // 把状态机停一轮,回到干净 IDLE
  WiFi.mode(WIFI_STA);
  // ★ v1.5.18:关 modem sleep —— 乐鑫官方 ESP-FAQ「连不上路由器」排查清单里明列的一条:
  //   "在连接 WiFi 前额外调用 esp_wifi_set_ps(WIFI_PS_NONE) 后是否能正常连上"。
  //   C3 v0.8.2 已做,这边补上。注:mode(WIFI_OFF) 会把该设置清掉,所以每次连接都要重设。
  esp_wifi_set_ps(WIFI_PS_NONE);
  WiFi.begin(ssid, pass);
  wifiUp = true;
  csBusy = true;
  csT0   = millis();
  snprintf(csSsid, sizeof(csSsid), "%s", ssid);
  snprintf(csPass, sizeof(csPass), "%s", pass);
  const int k = findSsid(ssid);
  cur = (k >= 0) ? k + 1 : 0;
  Serial.printf("[wifi] connecting \"%s\" ... (saved only if it gets an IP)\n", ssid);
}

// ---------------- 只读出口(★ v1.7.2,给屏上的「WiFi 信息卡」用) ----------------
// 为什么做成函数而不是让信息卡直接读 `wifi10::cnt` / `wifi10::kMax`:
//   信息卡在 disp 段(文件靠前)、本命名空间在文件靠后,而单文件 .ino 的自动原型
//   **只覆盖函数、不覆盖 namespace 成员** ⇒ 靠前那处根本看不见这两个名字。
//   若在前置声明里把 kMax 重写一遍,就成了"同一个常量手抄两处" —— 本项目栽过好几次的那个坑
//   (链路白名单、@MIR 字段数都是这么错的)。所以宁可多两行函数。
inline int savedCount() { return cnt; }
inline int capacity()   { return kMax; }

inline void begin() {              // 开机:载入台账 -> **按台账顺序挨个试**(不扫描)
  WiFi.onEvent(onWifiEvt, ARDUINO_EVENT_WIFI_STA_CONNECTED);      // ★ v1.5.18:reason 日志
  WiFi.onEvent(onWifiEvt, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);   // ★ v1.5.18:reason 日志
  WiFi.setAutoReconnect(false);   // ★ 关掉核心自动重连(默认 true,见 radioIdleForScan 那段)
  load();
  // ★★ v1.5.20(用户 2026-10-12 23:5x 定):开机**不自己扫描**。
  //   旧版是 scanStart(true) -> poll() 里 pickBestStored():扫一遍、只连"范围内 ∧ 已存档"
  //   里最强的那一条 —— 那套的适用场景是"首次配网、手上还没有台账"。
  //   现在只要台账非空,就直接 tryAll(cnt) 按 #1..#n 顺序挨个试(失败自动跳下一条)。
  //   scanStart/scanCollect/pickBestStored **保留不删**(仍被人工 'wscan' 用)。
  if (cnt > 0) {
    Serial.printf("[wifi] %d saved; trying them in order (no scan)\n", cnt);
    tryAll(cnt);
  } else {
    Serial.println("[wifi] none saved (wadd <ssid> <pass> to add)");
  }
}

// 供 C3 本机菜单 / 串口用:按组号连(1 起算)
static void connIdx(int n) {
  if (n < 1 || n > cnt) { Serial.println("[wifi] no such entry"); return; }
  startConnect(list[n - 1].ssid, list[n - 1].pass);
}

// ============================================================================
//  ★ v1.5.19:巡台账("挨个试连") —— C3 的 PUSH 收尾发 `wtry <n>` 触发
//  ---------------------------------------------------------------------------
//  用户 2026-10-12 23:1x 定:"PUSH 完先去连第一个,第一个不行就连第二个"。
//  语义:从台账 #1 起按顺序试到 #n(缺省 = 全部);
//        · 连上 -> 立刻停,回 `wifi ok <ip>`(由 poll() 的成功分支发)
//        · 失败 -> connFailed() 里自动接上下一条,回 `wifi try k/n <ssid>` 当进度
//        · 全试完都没成 -> 回 `wifi fail all`,停在原地(不退回扫码选网)
//  ⚠ 试连期间**不能给它发 wscan** —— ESP-IDF 明写"连接中拒绝扫描",会拿到 -2。
// ============================================================================
// ★ v1.5.21(用户 2026-10-14 定):把 ESP-IDF 的 reason 译成**人话短标签**回给 C3。
// ----------------------------------------------------------------------------
//  起因:用户"密码不对的时候你有没有一个告警提示?" —— 旧版只回 `wifi fail <ssid>`,
//  "连不上"和"密码错"在 C3 屏上长得一模一样,用户没法判断该不该重输密码。
//  ⚠ 纯 ASCII:C3 输出卡一行 34 列,中文在那边是 GLCD 字体会画成乱符号(见 rptText 段头)。
//  reason 取值见 esp_wifi_types.h(wifi_err_reason_t)。
static const char* failTag(const char* why) {
  const char* p = strstr(why, "reason=");
  if (p) {
    switch (atoi(p + 7)) {
      case 201:                    return "AP NOT FOUND";   // 扫不到这个 AP(不在范围)
      case 202: case 15: case 204: return "BAD PASSWORD";   // 认证/四次握手失败 = 密码大概率错
      case 203:                    return "AP REFUSED";     // 关联被 AP 拒绝
      case 205:                    return "CONNECT FAILED";
      case 200:                    return "NO BEACON";
      default:                     break;
    }
  }
  if (strstr(why, "TIMEOUT")) return "NO RESPONSE";       // 等满 10s 没有响应
  return "CONNECT FAILED";
}

// 一次连接尝试失败的统一收口(超时 / 快失败 都走这里)
static void connFailed(const char* why) {
  csBusy = false; cur = 0;
  Serial.printf("[wifi] connect \"%s\" FAILED (%s)%s\n", csSsid, why,
                trOn ? " -> try next" : " -> NOT saved");
  if (trOn && trIdx < trN) {                       // 还有下一条 -> 立刻接上
    ++trIdx;
    char b[56];
    snprintf(b, sizeof(b), "wifi try %d/%d %.16s", trIdx, trN, list[trIdx - 1].ssid);
    queueRpt(b);
    connIdx(trIdx);
    return;
  }
  if (trOn) {                                      // 台账全试完,一个都没成
    trOn = false;
    queueRpt("wifi fail all");
    Serial.println("[wifi] try: ALL FAILED -> stay offline");
  } else {
    // ★ v1.5.21:分两行 —— 第一行"谁失败了",第二行"为什么"。
    //   挤一行放不下(SSID 长了会把原因顶掉),而"为什么"正是用户最要看的那条。
    char b[56];
    snprintf(b, sizeof(b), "wifi fail %.18s", csSsid);
    queueRpt(b);
    snprintf(b, sizeof(b), "fail %s", failTag(why));
    rptText(b);
  }
  WiFi.disconnect();
  radioOff();
}

// 巡台账入口:n = 试到第几条(1 起算);n <= 0 或超界 = 试全部
static void tryAll(int n) {
  if (!cnt) {
    queueRpt("wifi fail all");
    Serial.println("[wifi] try: none saved");
    return;
  }
  if (n < 1 || n > cnt) n = cnt;
  trOn = true; trN = n; trIdx = 1;
  char b[56];
  snprintf(b, sizeof(b), "wifi try %d/%d %s", trIdx, trN, list[0].ssid);
  queueRpt(b);
  Serial.printf("[wifi] try: %d entr%s, start with #1 \"%s\"\n",
                trN, trN == 1 ? "y" : "ies", list[0].ssid);
  connIdx(1);
}

static void del(int n) {
  if (n < 1 || n > cnt) { Serial.println("[wifi] no such entry"); rptText("no such entry"); return; }
  for (int i = n - 1; i < cnt - 1; ++i) list[i] = list[i + 1];
  cnt--;
  saveAll();
  Serial.printf("[wifi] deleted #%d, %d left\n", n, cnt);
  char b[32];                                   // ★ v1.5.20:回 C3
  snprintf(b, sizeof(b), "deleted #%d, %d left", n, cnt);
  rptText(b);
}

static void listCmd() {            // #1 = 最新放进来的那条
  if (!cnt) { Serial.println("[wifi] none saved"); rptText("none saved"); return; }
  char b[34];                                   // ★ v1.5.20:每条也回 C3(C3 只能显示 32 字符)
  for (int i = 0; i < cnt; ++i) {
    Serial.printf("[wifi] #%d %-32s %s%s\n", i + 1, list[i].ssid,
                  list[i].pass[0] ? "(key)" : "(open)",
                  (i + 1 == cur) ? "  <- connecting" : "");
    snprintf(b, sizeof(b), "#%d %.20s %s", i + 1, list[i].ssid,
             list[i].pass[0] ? "(key)" : "(open)");
    rptText(b);
  }
}

// ============================================================================
//  ★ v1.5.15 新增:wscan —— 给 C3 命令板的 "WIFI / S3 / SCAN" 当**扫描服务端**
// ----------------------------------------------------------------------------
//  为什么在 S3 上扫,而不是让 C3 扫完把结果拿来用:
//    C3 的射频只在**C3 所在的位置**听到热点。将来 S3 装到杆子上、人在杆下时,
//    两者听到的根本不是同一批 AP —— 要配置 S3 连哪个网,必须**由 S3 自己听**。
//    (用户 2026-10-11 定,参照另一个工程 RF_Source_Controller 里跑通过的方案。)
//
//  ★ 与那份参考实现的关键差别:那边用的是**同步** WiFi.scanNetworks() ——
//    它会阻塞 2~5 秒。本机主循环同时还要喂 GNSS / 刷屏 / 收 C3 的命令,
//    绝不能被卡住哪怕一秒,所以这里一律走**异步**:
//        WiFi.scanNetworks(true, false)  立刻返回
//        WiFi.scanComplete()             -1=还在扫 / -2=失败 / >=0=条数
//    结果由 poll() 在**主循环**里分拍收割,一条也不阻塞。
//
//  回执格式(交给 benchlink 发,前缀 "@s ok " 由它加):
//        wsc  <序> <总条数> <rssi> <加密> <SSID>
//        wsce <总条数>                 正常扫完
//        wscf <-2|-3>                  扫失败(-2 驱动拒绝 / -3 超时)
//    ★ 为什么用 wsc/wsce/wscf 而不是 wscan:benchlink 收到任何命令都会**无条件回显**
//      一行 "@s ok <原命令>"(即 "@s ok wscan")。若结果行也叫 wscan,C3 就没法把
//      "回显"和"数据"分开。换名从协议上根治,不用在 C3 里玩特判。
//    ★ wscf 是 v1.5.17 新增:以前扫失败/扫到 0 个,**屏上都是同一句 "no AP found"**,
//      把"驱动拒绝扫描""链路不通""真没热点"三种死因糊成一样(上次排查就被带偏过)。
//  加密字段用短词(OPEN/WEP/WPA/WPA2/WPA3/...),SSID 放在**行尾**——
//  SSID 里可以有空格,放最后一列就不用怕它把前面的字段切乱。
// ============================================================================
constexpr int      kScanMax       = 24;      // 一次最多报几条(按信号强度降序截断)
constexpr uint32_t kScanTimeoutMs = 10000;   // 超时兜底:扫了 10s 还没结果就当失败报上去
constexpr int      kScanRunning   = -1;      // WiFi.scanComplete() 的三种返回值
constexpr int      kScanFailed    = -2;      //  (刻意写字面值,不用那几个宏 —— 不同核心
                                             //   版本里它们有时是 #define 有时是 enum)
constexpr int      kScanTimeout   = -3;      // 超时码(驱动没有这个概念,我们自己加的)

static bool     scWaiting = false;           // 已发起异步扫描,正等 scanComplete()
static bool     scReady   = false;           // 结果已备好,按 scIdx 逐条往外吐
static bool     scEnded   = false;           // 结束标记已经吐过
static bool     scReport  = false;           // true = 本轮结果要发回 C3
static bool     scBoot    = false;           // true = 本轮是"开机选网",不外发
static int      scFail    = 0;               // 0=正常;否则 kScanFailed / kScanTimeout
static uint32_t scT0      = 0;
static int      scN       = 0;               // 本次要报的条数
static int      scIdx     = 0;               // 下一条的下标
static char     scSsid[kScanMax][33];
static int8_t   scRssi[kScanMax];
static uint8_t  scAuth[kScanMax];

static const char* scanAuthName(uint8_t a) {
  switch (a) {
    case WIFI_AUTH_OPEN:            return "OPEN";
    case WIFI_AUTH_WEP:             return "WEP";
    case WIFI_AUTH_WPA_PSK:         return "WPA";
    case WIFI_AUTH_WPA2_PSK:        return "WPA2";
    case WIFI_AUTH_WPA_WPA2_PSK:    return "WPA/WPA2";
    case WIFI_AUTH_WPA2_ENTERPRISE: return "WPA2-ENT";
    case WIFI_AUTH_WPA3_PSK:        return "WPA3";
    case WIFI_AUTH_WPA2_WPA3_PSK:   return "WPA2/3";
    default:                        return "?";
  }
}

// 起一次异步扫描。forBoot=true = 开机选网(扫完自己挑一条连,不回 C3);
//              forBoot=false = C3 的 "WIFI / S3 / SCAN" 请求(结果发回 C3)。
static void scanStart(bool forBoot) {
  if (scWaiting) {
    // 开机那轮还没扫完时,操作员又点了 C3 的 "WIFI / S3 / SCAN" ——
    // 用户是**明确要这轮结果**,那就把这轮改成"对外报",开机自动连让位(下次开机再说)。
    if (scBoot && !forBoot) {
      scBoot = false; scReport = true;
      Serial.println("[wifi] scan: boot scan hijacked by explicit C3 request -> report to C3");
      return;
    }
    Serial.println("[wifi] scan: already running");
    return;
  }
  if (scReady) { Serial.println("[wifi] scan: previous results not drained yet"); return; }

  // ★★ 关键一行(见 radioIdleForScan 的段头):把射频停一轮再起,强制回 IDLE。
  //    只调 WiFi.disconnect() **实测不够** —— 核心的自动重连会立刻把它接回来。
  radioIdleForScan();

  WiFi.mode(WIFI_STA);                        // 从未连过网时也要有射频可听
  wifiUp = true;
  WiFi.scanDelete();                          // 清掉上一轮,免得结果数组越积越多
  WiFi.scanNetworks(true, false);             // ★ async=true, show_hidden=false
  scWaiting = true; scT0 = millis();
  scBoot    = forBoot;
  scReport  = !forBoot;
  scFail    = 0;
  Serial.printf("[wifi] scan: started (%s)\n",
                forBoot ? "boot: pick a saved net in range" : "on request, will report to C3");
}

// 把 scanComplete() 拿到的结果抄进本地数组(必须先抄再 scanDelete —— 删了就没了)
static void scanCollect(int n) {
  scN = 0;
  if (n <= 0) return;
  for (int i = 0; i < n; ++i) {
    const String s = WiFi.SSID(i);
    if (s.length() == 0) continue;                  // 隐藏 SSID:不列
    const int8_t  rssi = (int8_t)WiFi.RSSI(i);
    const uint8_t auth = (uint8_t)WiFi.encryptionType(i);
    // 插入排序:按 RSSI 降序,只留前 kScanMax 条(比榜尾还弱就直接丢)
    int pos = scN;
    if (pos >= kScanMax) {
      if (rssi <= scRssi[kScanMax - 1]) continue;
      pos = kScanMax - 1;
    } else {
      ++scN;
    }
    while (pos > 0 && scRssi[pos - 1] < rssi) {
      scRssi[pos] = scRssi[pos - 1];
      scAuth[pos] = scAuth[pos - 1];
      memcpy(scSsid[pos], scSsid[pos - 1], sizeof(scSsid[0]));
      --pos;
    }
    scRssi[pos] = rssi;
    scAuth[pos] = auth;
    s.toCharArray(scSsid[pos], sizeof(scSsid[0]));
  }
  Serial.printf("[wifi] scan: %d AP in range, reporting %d\n", n, scN);
}

// 开机选网:在"本轮扫到的 AP"里挑一条 **已存档 ∧ 信号最强** 的,返回台账下标;没有 -1。
static int pickBestStored() {
  int best = -1, bestR = -1000;
  for (int i = 0; i < scN; ++i)
    for (int j = 0; j < cnt; ++j)
      if (!strcmp(scSsid[i], list[j].ssid) && scRssi[i] > bestR) {
        bestR = scRssi[i];
        best  = j;
      }
  return best;
}

// 主循环每圈调一次:① "连上才存"状态机 ② 收割异步扫描结果
static void poll() {
  // ---- ① 连接状态机:"拿到 IP 才算成功,才存" ----
  if (csBusy) {
    if (evFast) {                                  // ★ v1.5.19:事件回调报的 201/202,别干等 10s
      const uint8_t r = evFast; evFast = 0;
      char w[40];
      snprintf(w, sizeof(w), "reason=%u %s", (unsigned)r, wifiReasonName(r));
      connFailed(w);
    } else if (WiFi.status() == WL_CONNECTED) {
      csBusy = false; cur = 0; trOn = false;       // ★ v1.5.19:巡台账到此结束
      insertFront(csSsid, csPass);
      saveAll();
      // ★ v1.5.21:同 startConnect —— 回执带 SSID,分两行(见那一处的长注)
      char b[56];
      snprintf(b, sizeof(b), "wifi ok %.18s", csSsid);
      queueRpt(b);
      snprintf(b, sizeof(b), "ip %s", WiFi.localIP().toString().c_str());
      rptText(b);
      Serial.printf("[wifi] CONNECTED \"%s\" ip=%s -> saved to NVS front (%d/%d)\n",
                    csSsid, WiFi.localIP().toString().c_str(), cnt, kMax);
    } else if (millis() - csT0 > kConnMs) {
      connFailed("TIMEOUT 10s");                   // ★ v1.5.19:统一收口,自动跳下一条
    }
  }

  // ---- ② 扫描收割 ----
  if (scWaiting) {
    const int r = WiFi.scanComplete();
    if (r == kScanRunning) {
      if (millis() - scT0 <= kScanTimeoutMs) return;      // 还在扫,接着等
      Serial.println("[wifi] scan: timeout -> report failed(-3)");
      WiFi.scanDelete();
      scN = 0; scFail = kScanTimeout;
    } else if (r == kScanFailed) {
      // 走到这儿基本都是"射频还在连、驱动拒绝扫描" —— 见段头那条 ESP-IDF 限制
      Serial.println("[wifi] scan: FAILED(-2 driver refused: radio busy connecting?)");
      scN = 0; scFail = kScanFailed;
    } else {
      scanCollect(r);
      WiFi.scanDelete();                                  // 已抄完,可以释放
      scFail = 0;
    }
    scWaiting = false;

    if (scBoot) {                       // ---- 开机选网:扫完自己挑一条,不回 C3 ----
      scBoot = false;
      const int j = pickBestStored();
      if (j >= 0) {
        Serial.printf("[wifi] boot: in range \"%s\" (%d dBm) -> try\n", list[j].ssid, (int)scRssi[0]);
        startConnect(list[j].ssid, list[j].pass);
      } else {
        Serial.println("[wifi] boot: none of the saved nets in range -> stay offline");
      }
      radioOff();                       // 没连上就关射频(csBusy 时内部自己跳过)
      return;
    }

    scReady = true; scIdx = 0; scEnded = false;
    return;
  }

  // ---- ③ 结果已全部吐完 -> 收摊,允许下一次扫描 ----
  if (scReady && scEnded && scIdx >= scN) {
    scReady = false; scEnded = false; scN = 0; scIdx = 0; scFail = 0;
    // ★ v1.5.16:扫完把射频关掉,把那几十 KB **连续**内存还回去。
    //   起因(2026-10-11 20:47 实机 A/B 测出):WiFi.mode(WIFI_STA) 一调用就常驻,
    //   而 charts 页的全屏 sprite 要 153,600 B 连续内部 SRAM —— 只要扫过一次,
    //   createSprite 必然失败、动图页再也进不去(实测:WiFi 前 "-> charts page" /
    //   WiFi 后 "full-screen sprite alloc FAILED",同一次上电内可复现)。
    //   ⚠ 关键:Arduino 的 WiFi.mode(WIFI_OFF) **只调 esp_wifi_stop()**,
    //     wifi 驱动在 esp_wifi_init() 时分配的那块内存**不释放** —— 实测没效果。
    //     所以必须再补一发 esp_wifi_deinit() 才能真正还给堆。
    //     Arduino 侧状态安全:mode(WIFI_OFF) 已把 _esp_wifi_started 置 false,
    //     下次 scanStart() 里 WiFi.mode(WIFI_STA) 会走完整 init 路径重新初始化。
    //   只在**没连上网**时关:已连接要维持,不能断。
    radioOff();
    Serial.println("[wifi] scan: done");
  }
}

// 取一行待发内容(不含 "@s ok " 前缀)。返回 false = 这一轮没东西可发。
// 全部吐完后会自动再吐一行 "wsce <n>"(正常)/"wscf <码>"(失败),C3 收到就知道齐了。
static bool scanNextLine(char* out, size_t cap) {
  if (!scReady || !scReport) return false;
  if (scIdx < scN) {
    const int i = scIdx++;
    snprintf(out, cap, "wsc %d %d %d %s %s",
             i + 1, scN, (int)scRssi[i], scanAuthName(scAuth[i]), scSsid[i]);
    return true;
  }
  if (!scEnded) {
    scEnded = true;
    if (scFail) snprintf(out, cap, "wscf %d", scFail);
    else        snprintf(out, cap, "wsce %d", scN);
    return true;
  }
  return false;
}

// 解析 'w' 之后的子命令串(args 已不含 'w' 本身,如 "add myssid pass123")。
static void cmd(const char* args) {
  while (*args == ' ') args++;
  char op[8] = "", a1[40] = "", a2[72] = "";
  int  nscan = sscanf(args, "%7s %39s %71[^\n]", op, a1, a2);
  const char* p2 = (nscan >= 3) ? a2 : "";
  if      (!strcmp(op, "list"))                 listCmd();
  else if (!strcmp(op, "add") && nscan >= 2)    startConnect(a1, p2);   // ★ 连上才存
  else if (!strcmp(op, "seed") && nscan >= 2) { // 只写台账不连(C3 的 PUSH 用)
    insertFront(a1, p2); saveAll();
    Serial.printf("[wifi] seeded \"%s\" (%d/%d) -- no connect\n", a1, cnt, kMax);
  }
  else if (!strcmp(op, "del"))                  del(atoi(a1));
  else if (!strcmp(op, "conn"))                 connIdx(a1[0] ? atoi(a1) : 1);
  else if (!strcmp(op, "try"))                  tryAll(a1[0] ? atoi(a1) : cnt);   // ★ v1.5.19
  else if (!strcmp(op, "scan"))                 scanStart(false);      // ★ v1.5.15
  else Serial.println("[wifi] usage: wadd <ssid> <pass> | wseed <ssid> <pass> | wtry [n] | wlist | wdel <n> | wconn [n] | wscan");
}

// 给开机动画/状态行用:当前 WiFi 状态文案(纯 ASCII,行内不含换行)。buf ≥ 44 字节。
static void statusText(char* buf, size_t n) {
  if (WiFi.status() == WL_CONNECTED) { snprintf(buf, n, "OK %s", WiFi.SSID().c_str()); return; }
  if (csBusy)                        { snprintf(buf, n, "CONNECTING...");                return; }
  if (!cnt)                          { snprintf(buf, n, "NONE SAVED");                  return; }
  snprintf(buf, n, "IDLE (%d saved)", cnt);   // 不再自称 "retrying":新版不会反复重连
}

} // namespace wifi10

// ============================================================================
//  开机画面 + 自检(v1.5.8,2026-10-11 按用户实测反馈改版)
// ----------------------------------------------------------------------------
//  · 黑底;两行文字全部**第一帧整行显示,无从左到右动画**(v1.5.8 用户定):
//      主标题   "GNSS SIGNAL MONITOR"  2 号字,绿色(与自检 OK 同款);
//      工作室行 "MANFACE STUDIO"       3 号字一行,MANFACE 7 字母彩虹
//      (一字母一色)、STUDIO 6 字母绿,同行同字号;右下角版本号。
//      3 号字一格 18px、14 字符共 252px 超屏宽 → 逐字母手排,步距 16px
//      (字母实宽 15+1 间隙),共 224px 刚好放下。
//  · 自检 4 行按节奏打出(v1.5.7 节奏保留):
//      SHT30 0.5s → 触摸 1.5s → GNSS(实测完成) → WiFi(5s 处)。
//  · GNSS 判据 = 报文池收到首条 NMEA(gnss::stored > 0),最多等 3s,
//    超时标 NO DATA 照常开机(室内无星正合演示场景,不拦)。
//  · WiFi 只显示状态,连不上不拦开机(连接在后台自己走)。
//  · 总时长 5s 展开 + 2s 定格(v1.5.7 用户定,保留)进主界面;期间串口任意
//    输入 / 触摸按下可跳过(跳过用 tft.getTouch 直读,不走 touch::poll,
//    免得误触页面切换)。
//  · ★ 退出时补铺一次白底:主界面大量区域靠"内容变化才重画",开机白底被
//    动画黑底盖掉会导致状态区残留黑底(v1.5.6 用户实测报的 bug)。
// ============================================================================
// ============================================================================
//  ★ v1.6.0(2026-10-16):链路**协议版本号** —— 与 C3 的同名常量**必须一致**。
// ----------------------------------------------------------------------------
//  · 它和固件版本号(kVer,两板各自两轨、每版都变)**是两件事**:协议号只在
//    **报文格式/语义**变了才 +1(即 SemVer 里 MAJOR 那一档),所以"这两块板能不能
//    对话"只能由它回答,kVer 答不了。本轮之前的无版本号时代记为 **P0**。
//  · 流程:C3 的心跳带号(`@k ping P1`)-> 本侧**仅在收到带号的 ping 时**回一条
//    自己的(`@s ok proto P1`)-> 两边不一致就串口/屏上点名。
//    ⚠ "仅在带号时回"是**新 S3 + 老 C3 的安全阀**:老 C3 发的是不带号的 `ping`,
//      此时保持沉默 —— 否则老 C3 每 2 秒收到一条它不认识的 `proto` 回执,会当普通
//      回执上屏,把 14 行的输出卡刷爆。
//  · ⚠ **为什么定义在这里、而不是 splash 里 kVer 旁边**:splash 与 benchlink 都要
//    用它(splash 不在内部用它,但 benchlink::poll() 必须),放在 splash **之前**
//    两处都能直接引用;放 splash 里面就得写 `splash::kProto`。
//  · ⚠ 改这个数 = 两板必须同批烧(见 docs/配套表-双板烧录.md)。
//    校验器:`python tools/check_proto_sync.py`(比对两板 kProto 的值)。
constexpr uint8_t kProto = 2;       // P2(2026-10-17:删 m/MIRROR,新增 evtlist/evtdet/evtstat 三个链路命令)

namespace splash {

constexpr const char* kTitle = "GNSS SIGNAL MONITOR"; // 主标题:第一帧整行显示
constexpr const char* kMain  = "MANFACE";             // 工作室名:7 字母彩虹
constexpr const char* kSub2  = "STUDIO";              // 与 MANFACE 同行同字号,绿色
// ★ v1.6.0(2026-10-16):MINOR 5 -> 6、PATCH 归零 —— **向下兼容地新增**"协议号握手"
//   (ping 改前缀匹配 + 带号时回 `@s ok proto P1` + 不匹配告警;老 C3 发不带号的 ping
//   时本侧行为与 v1.5.25 **完全一致** ⇒ 不是 MAJOR)。⇒ 与 C3 **v1.1.0 同批烧**。
//   注:`1.5` 这一轨是从 PC 演示面板继承来的历史包袱(MINOR 语义早被污染),本轮
//   **续用不重置** —— v1.6.0 是这一段第一次按新语义使用,别借机跳 v2.0.0。
// ★ v1.7.0(2026-10-16):MINOR 6 -> 7、PATCH 归零 —— 全部改动**向下兼容**:
//   ① 主界面三段式重排(星空图 R66->80 + 外圈光环 + 按键条 + 数据条)
//   ② 深色主题(配色一处定义,全局引用)
//   ③ 全英文界面(删 cnfont 整表,改 phrase 词表 + FONT4)
//   ④ 判据基准落盘(ns `gnsjudge`,三条防护)+ 事件日志(ns `gnsevt`,500 条 FIFO)
//   ⑤ 页面扩到 4(MAIN/CHARTS/LOG/EVT)+ 触摸改抬起判定(点击/滑动/长按)
//   ⇒ 协议仍 P1、命令集全保留 ⇒ 不是 MAJOR。**与 C3 v1.1.0 同批烧**。
// ★ v1.7.1(2026-10-16 深夜,PATCH 0 -> 1):**纯外观 + 触摸交互**,一行判据/协议逻辑都没动。
//   七条改动(全部来自用户当晚逐条拍板):
//   ① 四角 HUD:字模 FONT2(16px) -> 字体1(6x8)、块高 32 -> 24、去描边、圆角 4 —— "字体小一号";
//   ② 星空图放大 7px:圆盘 80->87、光环 83/84 -> 90/91(块高缩出余量后重算,上限是 +13);
//   ③ 块底三色合一:C_CARD/C_PANEL/C_BTN -> C_BLOCK(0x18E4,照 C3 的 CLR_BAR),
//      **块一律不再描边**(照 C3 的规律:边框只留给危险态),BACK 与翻页键改"页底凹槽"式;
//   ④ 数据文字统一青色 C_DATA(0x07FF,照 C3 屏上 [S3] 消息行):四角八行 + 数据条 CN0/SAT;
//   ⑤ 经纬度 ⇄ 温湿度 两块的**位置互换**,WiFi 图标从"星空中顶"搬进**右上块内右端**(原大小);
//   ⑥ 事件列表页新增 **PREV / NEXT 整页翻**按钮(塞进顶栏原有空白,列表仍 10 行、底栏不动);
//      事件详情页新增**左右滑动**看上/下一条(不用退回列表再点);
//   ⑦ 按键条改"圆角 4 + 四周 2px 页底缝 + 无描边";BASE 两态从**描边色**改由**底色**承载。
//   ⇒ 协议仍 P1、命令集、NVS 布局全不变 ⇒ 仍是 PATCH;**只烧 app(0x10000)即可,触摸校准不丢**。
// ★ v1.7.2(2026-10-16 深夜,PATCH 1 -> 2):**一处修 bug + 一处补显示**,判据/协议仍一个字没动。
//   起因(用户原话):"S3 IP地址显示在哪里?特别注意一下状态大英文显示区域,
//   我发现有不干净的显示,是不是那个区域显示了别的东西?"
//   ① **修:数据条残影**(真根因,不是猜的)—— `disp::paintBot()` 原来只在报文窗分支里
//      `fillSprite`,按键条/数据条分支**一个底都不铺**;而报文窗与"按键条+数据条"
//      **共用同一块 botSpr**。所以:长按切出报文窗、再长按切回来,8 行 NMEA 原文
//      (x 1..228 / 行高 10 / 铺满 y 0..79)**原样留着**,正好横贯状态大英文的上下;
//      另外四键块之间那 2px 页底缝、数据条"只画字不铺底",会把**上一次更长的状态词**
//      (DEGRADED -> NORMAL)的字尾留在两侧。修法 = 入口统一 `fillSprite(CLR_PAPER)`,
//      与 `paintSky()` 对齐。代价是内存里一次 ~0.2ms 的填充,**不增加任何 SPI 流量**
//      (pushSprite 本来就是整块 240x80)⇒ 不会变慢、不会更闪。纪律写进文件头 ⑧。
//   ② **补:S3 自己的 IP 终于屏上有地方看了** —— 原来它只作为回执发给 C3
//      (`@s ok ip x.x.x.x`),屏上一个字都不显示。主界面已排满(四角块 92 宽、
//      数据条右侧只剩 56px),11 字符的 IP 塞不进任何固定位置 ⇒ 做**按需信息卡**:
//      **点右上角那颗 WiFi 图标**(整个 92x24 的四角块都是热点)-> 弹 4 秒卡,
//      显示 `SSID / IP / SAVED n/10  RSSI`;再点任意处或到点自动收。
//      零侵入:不点它,屏上一个像素都不变。卡片在屏上时,任意点击**只负责收卡**,
//      不叠加别的动作(免得"想关卡片、结果把基准清了")。
//   ⇒ 协议仍 P1、命令集/NVS 布局/四个哨兵的对象全不变 ⇒ **PATCH,只烧 app(0x10000),校准不丢**。
//
// ★ v1.8.0(2026-10-17 上午,MINOR —— 加了链路命令 + 删了命令,协议号升 P2):
//   ① **mirror 全套删除(两板一起,用户定)**:namespace mir(@MIR 1Hz + @MSG 转发)、
//      串口/链路 'm' 命令、白名单去 'm'(10 -> 9)、pps::integral 只读出口(唯一消费者是 @MIR)。
//      理由:C3 只认 "@s ok " 前缀,@MIR/@MSG 从来就被 C3 丢掉 —— 纯死代码;PC 演示面板已放弃。
//   ② **事件日志搬运服务(给 C3 的「S3 LOG」页)**:链路整行命令 evtlist <top> / evtdet <seq> /
//      evtstat(与 w* 同类,不走单键白名单);回帧 evth/evts/evtr/evtd/evtg 经排干循环
//      每圈 2 条(与 wsc 同规矩),前缀 "@s ok " 由 benchlink 统一加。
//   ③ **事件列表行按类型着色**(用户 2026-10-17 口述):已恢复(closed)= 恢复绿(C_ST_OK),
//      未闭环 = 该事件类型的状态色(statusColor565,与状态词/光环同源)。
//   ④ **第四界面(详情页)加 PREV/NEXT 按钮**(与列表页同几何 y28..49),左右滑动保留。
//   ⑤ **`v` 键改义**(实机验证当场抓到):C3 的 LOG 发 'v',S3 原来执行的是
//      **串口日志开关**("log ON",屏上没反应)—— 与"C3 LOG = S3 屏上日志页"的语义差了十万八千里。
//      现改为**日志页开关**(与 'd' 对称:不在日志页 -> 进,在 -> 回主界面)。
//      gLogOn(5 秒状态行)暂无按键、默认关,变量保留(要用改源码重编)。
// ★ v1.8.1(2026-10-17 上午,PATCH —— 两处一行的颜色/取色修正,协议/判据零改动):
//   ① 列表底栏统计/页码 CLR_INK2(暗灰)-> C_DATA(数据青)—— 用户定,
//     "C3 搬运页底栏青色非常好看醒目,S3 也应该一样"。
//   ② evtd 帧的 <col> 从行色 rowColor(闭环=绿)改为**详情页大字色**
//     statusColor565(type) —— 与本屏 paintDetail 同源。v1.8.0 里同一条已闭环的
//     LOST:S3 大字红、C3 大字绿,用户看着"跟 S3 不一致"(evtr 行色不变)。
// ★ v1.8.2(2026-10-17 上午,PATCH —— 事件取色口径统一,协议/帧格式/判据零改动):
//   起因(用户原话):"没有什么是闭环恢复正常这种情况……每个事件类型要用自己固定的
//   颜色,包括正常的状态。正常状态是绿色。从异常状态转变到正常状态,这个正常状态的
//   一开始有一个记录。"
//   ① **"闭环"彻底退出取色**:rowColor() = statusColor565(type),只看类型。
//      v1.8.0/1.8.1 的"closed ? 绿 : 类型色"作废 —— 那会让**所有已结束的异常事件
//      一律变绿**,用户看到的是"整页全绿"。
//   ② **新增事件类型 PH_RECOVER(屏上词 NORMAL,绿色)**:异常 -> 正常的转变
//      单独记一条事件(startRecover,t0 = t1、closed = 1,时长 0 s),它自己进池、
//      自己一条记录 —— 不是挂在某条异常事件上的"恢复标记"。
//      触发条件:进过 ABNORMAL/LOST(latchedBad 锁存)且当前 level 回到 LV_OK;
//      DEGRADED 不算 bad ⇒ DEGRADED<->OK 抖动不会反复刷记录;正常期间零记录。
//   ③ evtr / evtd 的 <col> 统一发 rowColor(e) ⇒ C3 两处显示与 S3 逐像素同色,
//      C3 侧**无需改代码**(它一直只用帧里的颜色)。
// ★ v1.8.3(2026-10-17,PATCH —— **注入功能整套删除**;判据阈值/事件类型/协议帧格式零改动):
//   起因(用户原话):"我们现在不需要演示了,所以我想 C3 S3 两边都删除 inject 功能。
//   S3 主界面的 inject 改为 clear base 的功能……而且这个功能要按着 5 秒才有效。"
//   ① 删除面(S3):`judge::inject` / `injLat` / `injLon` / `setInject()` /
//      INJECT_OFFSET_M / 串口 'i' / 第 2 键的 INJECT 态 / 白名单 'i'(9 -> 8)。
//      判据里"注入开启就把比对坐标换成偏移点"那段一并去掉 —— **只比对真实坐标**。
//      ⚠ **POS-ANOMALY 判据与事件类型保留**:真实坐标偏移仍要报,那不是演示用的。
//   ② 第 2 键改 **CLR-BS(清基准)**:键宽只有 60px、FONT2 等宽 8px ⇒ 上限 6 字符,
//      "CLEAR BASE"(10 字符)放不下,故简写(与原来 INJECT 同宽 48px)。
//   ③ **按住 5 s 当场生效**(不等松手;短按只打一行日志,不做任何动作)。
//      按住期间键底保持灰、底部一条绿进度条按 7 档长满 ⇒ 有"还差多少"的反馈,
//      不会像"按了没反应"。手指滑出该键即作废。
//   ④ 配色语义(用户定):**同一时刻只有一个绿键** ——
//      base 已设 -> BASE 绿/青蓝(SET/SAV),CLR-BS 灰;
//      base 已空 -> BASE 灰,CLR-BS **亮绿**(表示"现在没有基准")。
//   ⑤ C3 侧:JUDGE 菜单删 INJECT 项(3 -> 2),速查注释同步。
// ★ v1.8.4(2026-10-17 中午,PATCH —— 事件日志两处修正,判据阈值/协议帧格式零改动):
//   ① **修的 bug:序号 seq 从不落盘**。begin() 里只 getUInt("seq",0),全文件没有一处
//      putUInt("seq",…) ⇒ 每次开机 seqCur=0 ⇒ 载入循环 n=0 ⇒ 记录写进了 NVS 却永远
//      读不出来(横幅恒为 `evt: loaded seq=0 n=0`;2026-10-17 把 NVS 整片 dump 解析,
//      里面明明躺着 24 条有效 evNN)。现改为 startEvent() 里与记录一起写回。
//      ⇒ 从此以后**掉电/重启记录不变**,直到 FIFO 把它挤掉(当时 100 条)。
//   ② **任何状态词变化都记一条**(用户原话:"degree / normal / searching / position
//      这些所有的状态,只要发生变化都要记一条")。上一版只记 ABNORMAL/LOST,
//      DEGRADED 只进统计 —— 屏上状态在变、日志不动,现场看着像日志坏了。
//      现为完整状态流水:WAIT / SEARCH / NORMAL / DEGRADED / SIG-ABNORMAL /
//      POS-ANOMALY / LOST / NO-DATA 每次切换一条,t0..t1 就是该状态区间。
//      防抖:新状态连续保持 kStableMs(2 s)才落笔(只压单帧毛刺,不做过度过滤)。
//   ③ 开机载入时把上次留下的"未闭环"那条就地收尾(closed=1、t1=t0),
//      否则屏上会永远挂着"进行中",且每重启一次多挂一条。
//   ④ C3 **不需要动**:evtr 的 <col>、evtd 的 <col>/<word> 都是 S3 发的。
// ★ v1.8.5(2026-10-17 下午,PATCH —— **两个常量**,判据阈值/协议帧/帧字段宽度零改动):
//   ① **事件池 100 -> 500 条**。由来:v1.8.4 改成全状态流水后实测记录速率约
//      **1 条/分钟**(开机 40 分钟已到 seq #40),100 条 FIFO 只覆盖 **≈1.7 小时**
//      —— 用户要的是"翻得到历史",1.7 小时等于没有。
//      NVS 键名 `ev%02d`:0..99 仍是 ev00..ev99,**100 起变三位** ev100..ev499,
//      与旧键不冲突 ⇒ 现有 seq<100 的历史记录**自动延续**,不丢不错位。
//      代价:静态 RAM +41.6 KB(pool 从 10.4 KB 变 52 KB),S3 内部 SRAM 余量充足;
//            满载 500 条时开机载入循环读 500 次 NVS(setup 内,约 0.5~1.5 s 量级)。
//   ② **防抖 2 s -> 15 s**(kStableMs)。只认"站稳了的状态";3~10 s 瞬断不再成条。
//      与 ① 叠加后覆盖时长从 1.7 小时拉到 **8 小时以上**(按同一速率)。
//      如实说明:15 s 内来回切的状态**一条都不记** —— 这是"压抖动"的必然代价,
//      不是 bug;要看短时抖动请回放串口状态行。
//   ③ 协议 **P2 不变**:evth 的 <nHave> 发的是 `%lu`、C3 用 `int` 收,
//      evtr 的 <seq> 也是 `%lu`;页码/滚动全是 int ⇒ **C3 一行不用改、不用重烧**。
constexpr const char* kVer   = "v1.10.2";

constexpr uint32_t kExpandMs = 5000;   // 展开时间下限(双进度取慢的"慢"边;2.5s→5s 用户定)
constexpr uint32_t kHoldMs   = 2000;   // 展开满后的定格时长(0.8s→2s 用户定)
constexpr uint32_t kGnssToMs = 3000;   // GNSS 首条报文等待上限(并行藏在展开时间里)

// 彩虹 7 色(黑底,一个字母一色)
constexpr uint16_t kRainbow[7] = {
  TFT_RED, TFT_ORANGE, TFT_YELLOW, TFT_GREEN, TFT_CYAN, TFT_BLUE, TFT_MAGENTA
};
// 深灰(TFT_eSPI 的 TFT_GREY 在本配置未定义,按库里的同值 0x8410 本地给)
constexpr uint16_t kGrey = 0x8410;

// 主标题 2 号字(12x16),19 字符共 228px,x=(240-228)/2=6
// v1.5.9:下移到 y=90(工作室行原位置);顶部 y=40 起空一条黑边(用户拍板"按字面")
constexpr int kTitleX = 6,   kTitleY = 90;
// 工作室行 1 号字(5x8,字格 6px):14 字符共 84px,x=(240-84)/2=78
// v1.5.9:比主标题小一号(1 号字),显示在主标题下面(y=132,自检行上方)
constexpr int kMainX  = 78,  kMainY  = 132, kStep = 6;
// 自检 4 行:1 号字,行距 20
constexpr int kChkX   = 30,  kChkY   = 176, kChkLH = 20, kChkRight = 54;
// 版本号右下角:★ v1.5.22 修 —— **按实际字宽右对齐**。
//   原来是 `kVerX = 195`,注释写着"1 号字 6px/字符,v1.5.10 起是 7 字符共 42px,x=237-42=195"
//   —— x 是**按"版本号正好 7 个字符"算死的**。`v1.5.100` 这种 8 字符就会 195+48 = 243 > 240
//   ⇒ 右边被裁(C3 那边已经因为同样的写法踩过一次:`v0.9.1` 6 字符就溢出)。
//   现改为每帧现算:x = kVerRight - strlen(kVer)*6,右边界恒 237。
constexpr int kVerRight = 237, kVerY = 306;
inline int verX() { return kVerRight - (int)strlen(kVer) * 6; }

// 工作室行:MANFACE 7 字母彩虹 + 空格 + STUDIO 6 字母绿,同一字号一行,
// 第一帧整行显示(v1.5.8 用户定:不再从左到右展开;v1.5.9 降 1 号字、移到主标题下)
static void drawStudio() {
  char s[2] = { 0, 0 };
  disp::tft.setTextSize(1);
  for (int i = 0; i < 7; ++i) {                 // MANFACE:彩虹
    s[0] = kMain[i];
    disp::tft.setTextColor(kRainbow[i], TFT_BLACK);
    disp::tft.drawString(s, kMainX + i * kStep, kMainY);
  }
  for (int i = 0; i < 6; ++i) {                 // STUDIO:绿色,空格占 1 步
    s[0] = kSub2[i];
    disp::tft.setTextColor(TFT_GREEN, TFT_BLACK);
    disp::tft.drawString(s, kMainX + (8 + i) * kStep, kMainY);
  }
}

static void line(int row, const char* name, const char* val, uint16_t col) {
  disp::tft.setTextSize(1);
  disp::tft.setTextColor(TFT_SILVER, TFT_BLACK);
  disp::tft.drawString(name, kChkX, kChkY + row * kChkLH);
  disp::tft.setTextColor(col, TFT_BLACK);
  disp::tft.drawString(val, kChkX + kChkRight, kChkY + row * kChkLH);
}

inline void run() {
  disp::tft.fillScreen(TFT_BLACK);

  // 主标题:第一帧整行显示,绿色(与自检 OK 同款,v1.5.8 用户定)
  disp::tft.setTextSize(2);
  disp::tft.setTextColor(TFT_GREEN, TFT_BLACK);
  disp::tft.drawString(kTitle, kTitleX, kTitleY);

  // 工作室行:MANFACE STUDIO 一行同字号,第一帧整行显示(无展开动画,v1.5.8 用户定)
  drawStudio();
  // 版本号
  disp::tft.setTextSize(1);
  disp::tft.setTextColor(kGrey, TFT_BLACK);
  disp::tft.drawString(kVer, verX(), kVerY);   // ★ v1.5.22:右对齐,见 verX() 说明

  const uint32_t t0 = millis();
  int  drawn   = 0;       // 已打出的自检行数 0..4
  bool gDone = false, gOk = false;
  char buf[48];

  while (true) {
    const uint32_t el = millis() - t0;

    // ---- GNSS 自检:等首条报文,最多 3s ----
    if (!gDone) {
      gnss::poll();                                  // 收 UART -> 池
      if (gnss::stored > 0)            { gDone = true; gOk = true; }
      else if (el >= kGnssToMs)        { gDone = true; }
    }

    // ---- 自检行按节奏打出(前两项 setup 里已测完,这里只排播出时刻) ----
    if (drawn == 0 && el >= 500) {                   // ① SHT30
      if (sht::ok) {
        snprintf(buf, sizeof(buf), "OK %.1fC %.0f%%", sht::tempC, sht::rhPct);
        line(0, "SHT30", buf, TFT_GREEN);
      } else  line(0, "SHT30", "NOT FOUND", TFT_RED);
      drawn = 1;
    }
    if (drawn == 1 && el >= 1500) {                  // ② 触摸
      if (touch::calibrated) line(1, "TOUCH", "OK (cal)", TFT_GREEN);
      else                   line(1, "TOUCH", "NO CAL",    TFT_ORANGE);
      drawn = 2;
    }
    if (drawn == 2 && gDone) {                       // ③ GNSS(实测完成才算)
      if (gOk) line(2, "GNSS", "OK first NMEA", TFT_GREEN);
      else     line(2, "GNSS", "NO DATA (3s)",  TFT_RED);
      drawn = 3;
    }

    // ---- ④ WiFi 行:展开满后打出,并每 500ms 刷新状态(连接在后台走) ----
    if (drawn == 3 && el >= kExpandMs) {
      wifi10::statusText(buf, sizeof(buf));
      uint16_t col = kGrey;
      if (!strncmp(buf, "OK ", 3))       col = TFT_GREEN;
      else if (!strncmp(buf, "FAIL", 4)) col = TFT_RED;
      line(3, "WIFI", buf, col);
      drawn = 4;
    }

    // ---- 退出:全部画完 + 定格时满;串口输入/触摸可提前跳过 ----
    if (drawn == 4 && el >= kExpandMs + kHoldMs) break;
    if (Serial.available()) break;
    uint16_t tx, ty;
    if (disp::tft.getTouch(&tx, &ty, 600)) break;

    delay(20);           // 帧节奏 ~50fps,顺带喂看门狗
  }

  // ★ 修复(2026-10-11 用户报):退出必须补铺一次白底 —— 主界面大量区域靠
  //   "内容变化才重画",开机时 disp::begin 铺的白底被动画黑底盖掉,
  //   状态区就会残留黑底。补这一下等效于"动画没发生过",
  //   主循环照常按首帧逻辑把该画的画全。正常结束/中途跳过都走这里。
  disp::tft.fillScreen(CLR_PAPER);
}

} // namespace splash

// ============================================================================
//  台架链路 benchlink:C3 命令板 ←→ S3 台架 的串口通道
//  (v1.5.10 新增 / v1.5.11 改 UART2 / v1.5.12 换脚到 38/39 / v1.5.23 换回 44/43)
// ----------------------------------------------------------------------------
//  物理:v1.5.23 起接 **GPIO44(RX) / GPIO43(TX)** —— 即 S3 芯片**官方 UART0 那对**
//        (U0RXD=44 / U0TXD=43),与 C3 侧的原生 **GPIO20(RX)/GPIO21(TX)** 正好配对。
//        接线:`C3 GPIO20 ← S3 GPIO43`、`C3 GPIO21 → S3 GPIO44`、GND 共地。
//        ★ ★ 为什么绕了一圈才用上(这段沿革别删,免得日后再判错):
//          v1.5.12 曾把这对脚判为"做不了":本板是 **ESP32-S3-DevKitC-1 v1.1**,
//          "UART 口"的桥片 **CP2102N 挂在 GPIO43/44** 上;当时(2026-10-11 17:2x)
//          实测 44 上 **0 条 "[bench] link cmd"**,于是归因为"CP2102N 的 TXD
//          一直驱动 GPIO44、跟 C3 的 TX 顶牛"。
//          ★★ **2026-10-15 20:5x 复盘推翻**:那次不通的真因是**面包板孔松**
//          (同一轮里 C3 那侧的 21 脚孔也是松的,用户已改成焊线)。
//          ⇒ 官方 43/44 恢复使用,与 C3 的 20/21 对齐。
//          ⚠ 保留的物理前提:C3↔S3 这三根必须**焊死(或可靠插接)**;日后若再
//          "时好时坏",先拿万用表量两端通断,再怀疑芯片/固件(见 docs/接线表 §六)。
//        (v1.5.12~v1.5.22 用的是 38/39:36/37 带八线 PSRAM 的模块上会与 PSRAM 复用
//         故避开,39~42 是 JTAG 备用脚。现已不用。)
//        ★ ★ 另一个修(v1.5.11):原来写的是 `Serial1.begin(...)` —— **`Serial1` 就是
//          UART1,而 GNSS 模块用的 `gnss::port(1)` 也是 UART1**。setup 里本段
//          晚于 gnss::begin() 执行,于是这一句把 UART1 的脚从 16/17 整体搬走:
//          ① GNSS 再也收不到数据;② C3 发来的字节虽然进了 UART1 的 FIFO,
//          却被每圈先跑的 gnss::poll() 抢走当 NMEA 垃圾丢掉。
//          S3 有 3 路 UART(SOC_UART_NUM=3),故本段改用 **UART2(Serial2)**:
//          GNSS 稳占 UART1、ROM 日志稳占 UART0,三方互不打扰。
//        ★ S3 自己的电脑串口(烧录 / 抓日志 / 面板)走的是**芯片原生 USB**
//          (GPIO19/20),与本段这俩脚完全无关,因此两者可以同时用。
//  协议(两个方向都带前缀 —— 这是防"悬空 RX 噪声误触发命令"的关键):
//        C3 → S3 :  "@k <payload>\n"
//                   payload = 单键(如 "s" / "1")或整行 wifi 子命令(如 "wadd myssid pw")
//        S3 → C3 :  "@s ok <payload>\r\n"
//        ★ 没有 "@k " 前缀的行一律**忽略**。悬空 RX 上拾到的随机字节凑不出这个
//          前缀,所以绝不会出现"噪声里蹦出个 'c' 把基准清了"这种事故。
//  执行:收到合法 payload 后转发给**同一份** consoleKey() / wifi10::cmd() ——
//        与电脑串口敲键行为完全等价,不存在两套逻辑。
//  状态:无对外状态量(v1.5.14 起把 ok()/rxBytes() 等调试访问器摘掉了;
//        链路是否通,看 C3 屏上有无 `[s3] …` 行即可)。
//  ★ v1.5.18 加(2026-10-12 21:4x,用户报"通信经常不通"):
//    软件环形缓冲 256 B -> 2048 B, TX 缓冲 0 -> 1024 B(见 begin())。
//    依据(社区已定案,不再自己造实验):UART RX 中断优先级=1,
//    而 WiFi/BT 的 ISR 优先级=3 ⇒ WiFi 密集收发时抢占 UART ISR,
//    硬件 FIFO(128 B)无人及时抽 ⇒ 溢出丢字节;量产实测丢包**集中在 beacon 广播时刻**,
//    WiFi 连接态下吞吐量传感器每秒触发 120 次。
//    侧面证据:C3 那边同样的环形缓冲在 v0.8 就因"抽得比来得慢"而加到 1 KB。
// ============================================================================
static void consoleKey(char c);            // 定义在本段之后,先声明

namespace benchlink {

constexpr int      PIN_RX   = 44;          // ★ v1.5.23 换回官方 U0RXD:S3 收 ← C3 GPIO21(TX)
constexpr int      PIN_TX   = 43;          //   官方 U0TXD:S3 发 → C3 GPIO20(RX)。沿革见段头。
constexpr uint32_t kBaud    = 115200;
constexpr int      kLineMax = 176;         // 最长上行:"@k wadd " + 32(ssid) + 1 + 64(pass)

// 编译期守住 ①:链路脚不能撞台架任何一条已用线(否则现场会变成"时好时坏"的玄学故障)
static_assert(PIN_RX != 4 && PIN_RX != 6 && PIN_RX != 7 && PIN_RX != 8 &&
              PIN_RX != 9 && PIN_RX != 10 && PIN_RX != 11 && PIN_RX != 12 &&
              PIN_RX != 13 && PIN_RX != 16 && PIN_RX != 17 && PIN_RX != 18 &&
              PIN_TX != 4 && PIN_TX != 6 && PIN_TX != 7 && PIN_TX != 8 &&
              PIN_TX != 9 && PIN_TX != 10 && PIN_TX != 11 && PIN_TX != 12 &&
              PIN_TX != 13 && PIN_TX != 16 && PIN_TX != 17 && PIN_TX != 18 &&
              PIN_RX != PIN_TX,
              "链路脚撞了台架已用线(屏 4/6/10/11/12/13、触摸 9、I2C 7/8、GNSS 16/17/18)—— 换一对脚。");

// 编译期守住 ②:链路脚不能踩板子/内核的保留脚 —— 这些脚接上去必然"时好时坏":
//   19/20 = 芯片原生 USB(本工程的电脑串口);26..32 = SPI flash;33..37 = 八线 PSRAM(若模块带);
//   48 = 板载 WS2812。
//   ★ v1.5.23(2026-10-15 20:5x):**43/44 已从本表移出** —— 它是官方 U0TXD/U0RXD,
//     当年(2026-10-11)被判"CP2102N 顶牛、做不了",2026-10-15 复盘推翻该结论:
//     那次不通的真因是**面包板孔松**(用户已改焊线)。现恢复使用,见段头说明。
constexpr bool linkPinReserved(int p) {
  return p == 19 || p == 20 || p == 48 || (p >= 26 && p <= 37);
}
static_assert(!linkPinReserved(PIN_RX) && !linkPinReserved(PIN_TX),
              "链路脚踩到保留脚了(USB 19/20、flash 26-32、PSRAM 33-37、灯 48)—— 换一对脚。");

static char     line[kLineMax];
static int      n        = 0;
static uint32_t nCmd     = 0;              // 累计收到几条合法命令(只用于日志里的序号)

// ★ v1.6.0:对端(C3)协议号 —— 上一次看到的那个值,-1 = 还没见过。
//   只为**去重**用:心跳每 2 秒一条,协议号却不怎么变 —— 不去重的话
//   "协议不匹配"那行日志/那条屏上提示会跟着心跳一起每 2 秒刷一遍。
static int      peerProtoSeen = -1;

// ★★ 2026-10-15 新增:链路直通键**白名单**。
//   为什么要:C3 发来的整行命令**只取首字符**丢给 consoleKey() —— 与"在 S3 上敲一下键"
//   完全等价。这层安全性以前**完全依赖巧合**:靠"C3 恰好没发过别的字母开头的词"。
//   现场演示时只要给 C3 新增一条 `sendCmd("status")`,S3 就会去执行 `s`(打印状态),
//   C3 屏上突然多出一堆无关回执 —— 而你不会立刻想到是这个原因。
//   现在显式登记:**未登记的只留一行日志、不执行**(回执照旧发,所以心跳不受影响)。
//   ⚠ 必须与 **C3 菜单表**(C3_CmdPad.ino 里 kind=IK_CMD 的那些项)一一对应:
//      当前 C3 会发的单键 = `r b c d v s g t`(8 个,全在下面这个串里;
//      ★ v1.8.3 删 `i` 后 9 -> 8,2026-10-17 两板一起删);
//      整行 `w*`(wscan/wadd/wseed/wdel/wtry)走上面的 wifi 分支,不经过这里;
//      心跳 `ping` **不在**白名单 —— 它在 poll() 里有**单独一支**提前放行
//      (不执行、也不报"未登记";每 2 秒一条,不能让它刷日志)。
//      **C3 那边加一个菜单项,这里就要同步加一个字。**
//   ★★ 2026-10-15 修 + 加哨兵 ★★
//      这个串**第一次就抄错了**:写成 `"0123bcgimrstv"`(13 个)—— 把 VIEW/CHARTS 的
//      **`d` 漏掉**、又塞了个没用的位置。后果不是崩溃而是**静默失灵**:按 C3 的
//      VIEW / CHARTS,串口上 C3 照样打印 `-> link 'd'`,S3 却一个反应都没有。
//      13 和 14 只差一个字符,字符串本身完全"长得像对的",人眼审不出来。
//      ⇒ 别再手抄。改完跑一次 **`python tools/check_link_whitelist.py`**
//        (它从 C3 菜单表程序化提取 IK_CMD 的 arg 首字符,与本串比对,不一致就 exit 1)。
//      下面按 C3 菜单**同序分组**写,并加了 `c != '\0'` ——
//      `strchr` 找 `'\0'` 会命中**串尾**并返回非空指针,空 payload 会被误放行;
//      虽然 consoleKey('\0') 只是走到 default 什么也不做,但"白名单放行空字符"本身就不对。
static inline bool linkKeyAllowed(char c) {
  //        JUDGE(2)  VIEW(2)  SYSTEM(3)  TOUCH(2)   (SIM 已删 -> 不再有 '0'..'3';
  //        MIRROR 已删 -> 不再有 'm',2026-10-17 两板一起删;
  //        INJECT 已删 -> 不再有 'i',2026-10-17 v1.8.3 两板一起删)
  return c != '\0' && strchr("bc" "dv" "sgt" "r", c) != nullptr;
}

inline void begin() {
  // ★ v1.5.18:加大链路缓冲 —— 社区已定案的丢字节根因:UART RX 中断(优先级1)被
  //   WiFi/BT 的 ISR(优先级3)抢占 ⇒ 硬件 FIFO(128 B)无人及时抽走 ⇒ 溢出丢字节;
  //   量产实测丢包**集中在 WiFi beacon 广播时刻**。两个 set*BufferSize 都必须在 begin() 之前调。
  Serial2.setRxBufferSize(2048);    // 默认仅 256 B
  Serial2.setTxBufferSize(1024);    // 默认无软缓冲 -> write() 直接撞硬件 FIFO,满了就阻塞主循环
  Serial2.begin(kBaud, SERIAL_8N1, PIN_RX, PIN_TX);   // ★ UART2(不是 UART1,见段头)
  Serial.printf("[bench] link: UART2 RX=GPIO%d TX=GPIO%d @%lu (C3 command pad)\n",
                PIN_RX, PIN_TX, (unsigned long)kBaud);
}

inline void poll() {
  // 与 readConsole 同样的 64 字节/轮上限:宁可下一圈接着读,也不让主循环被锁住
  // ★ v1.5.17:单轮上限 64 -> 256。C3 最长的上行是
  //   "@k wadd <32 字节 ssid> <64 字节密码>" = 176 B,64 B/圈要抽 3 圈;
  //   遇到"刚复位、ROM 日志还没排空"的时刻容易和下一圈抢 -> 干脆一次多抽点。
  for (int guard = 0; guard < 256 && Serial2.available(); ++guard) {
    const char c = (char)Serial2.read();
    if (c == '\n' || c == '\r') {
      if (n == 0) continue;
      line[n] = 0;
      n = 0;
      char* p = line;
      if (strncmp(p, "@k ", 3) != 0) continue;         // 无前缀 -> 丢弃(抗噪声)
      p += 3;
      while (*p == ' ') ++p;
      if (!*p) continue;
      ++nCmd;
      if (*p == 'w' && p[1]) {                          // 整行:wifi 子命令
        wifi10::cmd(p + 1);                             // "wadd x y" -> cmd("add x y")
      } else if (!strncmp(p, "ping", 4) && (p[4] == 0 || p[4] == ' ')) {
        // ★★ 2026-10-15:心跳**单独放行** —— 不执行,也不当成"未登记"报警。
        //   心跳每 **2 秒**一条(C3 靠它撑住 s3link::ok() 的 5 秒窗口)。要是让它掉进
        //   下面的 else,S3 串口会被 "unregistered key 'p'" **每 2 秒刷一行** ——
        //   一个只为降噪加的白名单,反倒成了新的噪声源。
        //   ⚠ 这一支必须放在 linkKeyAllowed() **之前**:`p` 不是"命令",本来就不该进
        //     那张表(进去的话 tools/check_link_whitelist.py 会报"多了一个没人在发的字",
        //     而那才是对的 —— 表应与 C3 菜单一一对应)。
        //   为什么"不执行"是安全的:`case 'p'` 在 2026-10-11 就删了,真去执行也只是
        //   落到 consoleKey() 的 `default: break`,零副作用 —— 所以两者等价。
        //
        // ★ v1.6.0(2026-10-16):两处改动,起因都是"心跳现在带协议号了"
        //   (`@k ping P1`,不再是 `@k ping`):
        //   ① 判据由 `strcmp`(完全相等)改成**前缀匹配**,且后随字符必须是空格或结尾 ——
        //      判等会落空 ⇒ 掉进下面的 else ⇒ `linkKeyAllowed('p')` 里没有 'p' ⇒
        //      每 2 秒一行 "unregistered key 'p'",把 S3 串口刷成流水。
        //      ⚠ 用 `p[4]==0 || p[4]==' '` 而不是裸 `strncmp`,是为了**不误吃**
        //        `pingpong` 这类"以 ping 开头但不是心跳"的词。
        //   ② **只在收到带号的 ping 时才回协议号** —— 这是"新 S3 + 老 C3"的安全阀:
        //      老 C3(v1.0.3)发的是不带号的 `ping`,此时本侧保持沉默,行为与 v1.5.25
        //      **完全一致**;否则老 C3 每 2 秒收到一条它不认识的 `proto` 回执,
        //      当普通回执上屏,把 14 行输出卡刷爆。
        //   ⚠ 回执前缀**必须仍是 `@s ok `**(所以写成 `@s ok proto P1`):C3 的 poll()
        //     只认这个前缀,发成别的格式会被它**静默丢弃**(见 C3_CmdPad.ino 里那段硬化说明)。
        if (p[4] == ' ') {                             // 带协议号 -> 回握手 + 比对
          const int their = atoi(p + 6);               // "ping P1" -> 1(跳过 "ping P")
          Serial2.printf("@s ok proto P%d\r\n", kProto);
          if (their != peerProtoSeen) {                // 只在**对端协议号变化**时动作
            peerProtoSeen = their;
            if (their != kProto) {
              // 只告警、**不拒绝执行** —— 版本不齐时"命令还能用"比"直接罢工"有用得多
              // (现场可以直接先干活,回头再补烧那一块)。
              Serial.printf("[bench] LINK PROTO MISMATCH: S3=P%d, C3=P%d (firmware pairing?)\n",
                            kProto, their);
              // 顺带让 C3 也能看到(走既有的 txt 回执通道 -> C3 输出卡上一行
              // "[s3] txt PROTO MISMATCH C3 fw")。
              // ⚠ 必须写限定名 `wifi10::` —— rptText 定义在 wifi10 命名空间里
              //   (3364 行),不加限定会 "was not declared in this scope" 编译失败。
              // ⚠ 注意:`rptText` **不会**上 S3 自己的屏 —— 那份 rpt 队列只被
              //   benchlink::poll() 排干、发给 C3(见文件里 rptNext 的两处调用)。
              //   所以 S3 侧的现场可见性靠上面那行串口日志 + C3 屏。
              wifi10::rptText("PROTO MISMATCH C3 fw");
            } else {
              Serial.printf("[bench] link proto ok (both P%d)\n", kProto);
            }
          }
        }
      } else if (!strncmp(p, "evtlist", 7) && (p[7] == 0 || p[7] == ' ')) {
        eventlog::reqList(p[7] ? atoi(p + 8) : 0);      // "evtlist 20" / "evtlist"(top 缺省 0)
      } else if (!strncmp(p, "evtdet", 6) && (p[6] == 0 || p[6] == ' ')) {
        uint32_t sq = 0; int st = 0;                    // "evtdet <seq> [+1|-1]"
        if (p[6]) { char* e = nullptr; sq = (uint32_t)strtoul(p + 7, &e, 10);
                    if (e && *e) st = atoi(e); }
        eventlog::reqDet(sq, st);
      } else if (!strcmp(p, "evtstat")) {
        eventlog::reqStat();
      } else if (linkKeyAllowed(*p)) {                  // 单键:与电脑串口同一份分派
        consoleKey(*p);
      } else {                                          // ★ 2026-10-15:未登记 -> 不执行
        Serial.printf("[bench] link: unregistered key '%c' ignored (add it to linkKeyAllowed())\n", *p);
      }
      Serial.printf("[bench] link cmd #%lu: \"%s\"\n", (unsigned long)nCmd, p);
      Serial2.printf("@s ok %s\r\n", p);                // 回执:C3 屏上当 [s3] 行显示
    } else if (n < kLineMax - 1) {
      line[n++] = c;
      // ★ v1.5.17 修(2026-10-11 22:2x 实机揪出):**前缀对齐 / 重新同步**。
      //   现象:C3 的 `WIFI / S3 / SCAN` 发来的 `@k wscan` 到不了 S3(屏上 12s 超时),
      //         而紧接着的 `@k wadd ...` 却收到了 —— "复位后第一条命令必丢"。
      //   根因:C3 每次复位,ROM bootloader 都会在 **GPIO21** 上吐一段启动日志,
      //         而 GPIO21 正是 C3→S3 的那根 TX(它默认是 U0TXD)。
      //         那段日志直接灌进 S3 的 UART2 RX。旧的攒行逻辑**不会重新同步**:
      //         噪声先攒进 line[],真正的 `@k wscan\n` 被**粘在噪声后面**,
      //         前缀判据对不上 -> 整条命令被静默丢掉(还顺带把计数也归零)。
      //   修法:每收一个字节就查一次前缀;发现攒的不是 "@k " 就立刻滑到最后 2 字节
      //         重新对齐(留 2 字节是为了 '@' 'k' 分两次到达时也能拼回前缀)。
      if (n >= 3 && strncmp(line, "@k ", 3) != 0) {
        line[0] = line[n - 2];
        line[1] = line[n - 1];
        n = 2;
      }
    } else {
      n = 0;                                  // 超长且迟迟不见换行 -> 清空重新同步
    }
  }

  // ★ v1.5.15:wscan 的结果**分条**回执(见 wifi10 里那段说明)。
  //   每圈最多 2 条 —— UART2 的写缓冲只有 256 B,一口气灌满会在这里阻塞
  //   (115200 下 256 B 要 22 ms),分拍发就完全不会顶到主循环。
  //   顺序上必然晚于上面那行 "@s ok wscan" 的无条件回显,所以 C3 永远先看到回显、
  //   再看到 wsc 数据 —— 两段天然可分辨。
  char sc[96];
  for (int k = 0; k < 2 && wifi10::scanNextLine(sc, sizeof(sc)); ++k)
    Serial2.printf("@s ok %s\r\n", sc);

  // ★ v1.5.17:wifi **连接结果**回执("wifi ok <ip>" / "wifi fail <ssid>")。
  //   为什么必须有:上面那行 `@s ok <原命令>` 对任何命令都无条件回显,
  //   它只证明"S3 收到了这条命令",**不证明连上了** —— C3 屏上看到的
  //   "[s3] ok wadd xxx" 是个假信号(2026-10-11 用户就是被它骗了)。
  //   这两行才是真结果:拿到 IP 才发 ok,10 s 超时发 fail。
  char rp[64];
  for (int k = 0; k < 2 && wifi10::rptNext(rp, sizeof(rp)); ++k)
    Serial2.printf("@s ok %s\r\n", rp);

  // ★ v1.8.0:事件日志搬运帧(evth/evts/evtr/evtd/evtg)分条回执 —— 与 wscan 同规矩:
  //   每圈最多 2 条,一页 12 帧六圈发完(主循环一圈毫秒级,C3 感知不到等待)。
  //   前缀 "@s ok " 在这里统一加 —— C3 的 poll() 只认这个前缀,发成别的格式会被静默丢弃。
  char el[128];
  for (int k = 0; k < 2 && eventlog::evtNextLine(el, sizeof(el)); ++k)
    Serial2.printf("@s ok %s\r\n", el);
}

// 注:v1.5.13 这里曾有一个链路自检 selftest()(串口命令 'k')—— 靠它自环/逐根验线,
//     当晚把链路查通了。v1.5.14 出正式版时**已摘除**(调试口不该留在正式固件里)。
//     需要时把命令 'k' 与那几个计数加回来即可,逻辑就是"读 Serial2.available() 计数 +
//     真发几条 '@k ping' 让对端/自环回执"。

}  // namespace benchlink

static void printHelp() {
  Serial.println("[bench] commands: b=set base  c=clear base  s=status  p=params  t=temp/RH  g=pps  d=charts page  k=inject tap/hold/swipe  w=wifi add/seed/try/list/del/conn/scan  r=reset touch cal  v=event log page  ?=help");
  // v1.5.17:`wadd` 语义变了 —— 现在是"**连上才存**"(不是存了再连);
  //         另加 `wseed <ssid> <pass>` = 只写台账不连(C3 的 PUSH 走这条)。
  // 2026-10-02:删掉 o=panel RGB/BGR 与 n=invert 两条(命令一并删除,屏已调好)。
  // 2026-10-06:新增 d —— 第二界面"动图页"的开关,和"点报文区"完全等价。
  // 2026-10-11:新增 w —— WiFi 存档(v1.5.6),子命令 wadd/wlist/wdel/wconn,满 10 组 FIFO 覆盖最旧。
  // 2026-10-11(v1.5.10):**删除 l=led probe 与 p=坐标链路打印** —— 用户定。
  //            l 的选脚任务早已完成(灯脚 = 48),且它阻塞画面约 8 秒,演示现场易被
  //            误判成死机;p 是隐藏调试命令(本就不进帮助),要看链路敲 s 即可。
  //  2026-10-11(v1.5.14):删掉排查链路时临时加的 `k`=链路自检 → 又回到 **16 条**。
  //  2026-10-17(v1.8.3):删 `i`(注入偏移)→ **15 条**。
  //  ★ 2026-10-17 傍晚(v1.10.0):**`p` 回来了,但换了内容** —— 现在是「参数表」
  //    (打印 PARAMS 页那八个数 + 各自 def/范围/步长 + 改过哪几项)。
  //    与 v1.8.3 删掉的那个"坐标链路打印"毫无关系,只是复用了字母。
  //  ★ 2026-10-17 深夜(v1.10.2):新增 `k` = **演示注入**(kt / kh / ks)。
  //    它不是"遥控命令",是"让电脑替手指按屏":合成坐标在 poll() 里**替代**
  //    getTouch 的返回值,下游全是原路径 ⇒ 按键高亮、长按走条、翻页手势都是真的。
  //    **不进 linkKeyAllowed()** —— C3 发不进来,只有电脑 USB 串口能敲。
  //    至此 **17 条**;本行与 README 命令表是同一份清单(电脑端面板已删除,不用再同步)。
  // 's' 那行末尾新增 rst / boot / loopMax —— 现场判断"屏不动了"是哪一类问题,见 serialReport 顶部注释。
}

// ★ v1.5.25:TOUCH('r') 清完校准后的**自动重启**标志(case 'r' 挂、loop() 消费)。
//   为什么不直接在 case 里 ESP.restart():无条件回显 "@s ok r" 要等 consoleKey
//   **返回之后**才发(benchlink::poll),rptQ 也是 poll 每圈只排 2 条 ——
//   在 case 里直接重启,回显和回执会**一起被重启吞掉**。
static bool gTouchRebootPend = false;

// 按键分派 —— ★ 2026-10-11 从 readConsole() 里原样抽出,电脑串口与 C3 链路
// (benchlink)共用这一份。这样"屏上点一下"和"电脑敲一下"行为完全等价,
// 不可能出现两套逻辑走偏;将来加命令也只需改这一处。
static void consoleKey(char c) {
  switch (c) {
      case 'b': {
        // ★ v1.8.6:与屏上第 1 键**完全等价**(同一入口 requestBase),不再直接落盘。
        char m[40] = "";
        judge::requestBase(m, sizeof(m));
        wifi10::rptText(m);                            // ★ v1.5.20:回 C3
        break;
      }
      case 'c':
        judge::clearBase();
        Serial.println("[bench] base cleared");
        wifi10::rptText("base cleared");                 // ★ v1.5.20:回 C3
        break;
      // ★ v1.8.3:case 'i'(切换注入)已删 —— 注入功能整体移除(用户 2026-10-17 定)。
      //   串口 'i' 现在落到 default:什么也不做,连回显都没有(与删掉的 l 同待遇;
      //   'p' 已在 v1.10.0 改作"参数表",见下面那条 case)。
      //   白名单也已去掉 'i',C3 更发不进来了。
      case 's': {
        serialReport(true);              // 手动敲的必须立刻出,不受自动节流限制
        // ★ v1.5.20:两行摘要回 C3(屏上一行 34 列,只能这么短)。原来这条命令
        //   的信息全在 S3 的串口长句里,C3 那边只有一行 "@s ok s" 的空回显。
        char t1[34], t2[34];
        snprintf(t1, sizeof(t1), "ST sat%d/%d hdop%.1f cn0 %.0f",
                 nmea::st.satsUsed, nmea::st.satsView, nmea::st.hdop, nmea::st.cn0Median);
        snprintf(t2, sizeof(t2), "ST off%.0fm base %s",
                 judge::res.offsetM, judge::baseSet ? "set" : "---");
        wifi10::rptText(t1);
        wifi10::rptText(t2);
        break;
      }
      // ★ 2026-10-11 按用户要求**删除两条命令**:
      //   'l' = led probe —— 选脚任务 2026-09-28 已完成(脚就是 48),留着只会在
      //         演示现场被人误敲:它内部是阻塞 delay,画面一停 8 秒像死机。
      //   'p' = 坐标链路逐环节打印 —— 隐藏调试命令(连帮助都不进),现场用不到;
      //         要看链路健康度敲 's' 即可(有 rst/boot/loopMax/cksumErr)。
      //   ★ v1.10.0:`p` **重新启用,但语义完全换掉** —— 现在是「参数表」,
      //     打印 PARAMS 页那八个数。原"坐标链路打印"没有回来,也永远不回来了
      //     (它的信息 s 里全有)。看下面 case 'p'。
      //   连带删除:led::probe() 函数 + 候选脚数组 + 帮助里的那行说明。
      case 't': {
        sht::pollNow();
        Serial.printf("[bench] sht30 @0x%02X  %.1fC  %.0f%%RH  (%s)\n",
                      sht::addr, sht::tempC, sht::rhPct, sht::ok ? "ok" : "READ FAILED");
        char b[32];                      // ★ v1.5.20:回 C3
        snprintf(b, sizeof(b), "SHT %.1fC %.0f%%RH %s",
                 sht::tempC, sht::rhPct, sht::ok ? "ok" : "FAIL");
        wifi10::rptText(b);
        break;
      }
      case 'g': {                        // PPS 秒脉冲观测:脉冲数 / 间隔 / 本地时钟 ppm
        pps::report();
        // ★ 2026-10-15:34 -> 44。pps::brief 的 `%+.1f` 最坏要写 ~28 字节,34 只剩 6 字节
        //   余量,编译器一直报 -Wformat-truncation。加大缓冲**不改输出**(输出长度由内容定)。
        char b[44];                      // ★ v1.5.20:回 C3(pps 那两行中文长句 C3 放不下)
        pps::brief(b, sizeof(b));
        wifi10::rptText(b);
        break;
      }
      case 'r':
        touch::resetCal();
        // ★ 2026-10-15 改文案:`resetCal()` 只清 NVS 里的 cal_flag —— 原文案说
        //   "-> reboot" 是在骗人(2026-10-15 用户按了 C3 的 S3 TOUCH 后屏上毫无
        //   变化,正是被这句话带偏了方向)。
        // ★ v1.5.25(2026-10-16 用户拍板):现在说到做到 —— 挂 gTouchRebootPend,
        //   由 loop() 在 benchlink::poll() 之后真正重启(直接在这里 restart 会把
        //   回显 "@s ok r" 和 rptQ 里的回执一起吞掉,见 gTouchRebootPend 处说明)。
        //   重启后开机自检看到 cal_flag=0,直接进 4 点校准,免手动断电。
        wifi10::rptText("touch cal cleared - rebooting");   // ★ v1.5.20:回 C3
        gTouchRebootPend = true;
        break;
      // 注:v1.5.13 这里曾有 case 'k':链路自检 —— v1.5.14 出正式版时摘除(见 printHelp 注释)。
      case 'v':                          // ★ v1.8.0 改义:S3 日志页开关(原来是串口日志开关)
        //   用户 2026-10-17 定:C3 的 VIEW/LOG 要的是**屏上的日志页**(第三界面),
        //   不是串口流水 —— 实测烧机后 3 分钟就抓到:'v' 打出 "log ON",S3 屏纹丝不动。
        //   现在与 'd'(动图页开关)完全对称:不在日志页 -> 进;在 -> 回主界面。
        if (ui::page == ui::PAGE_LOG || ui::page == ui::PAGE_EVT) {
          ui::toMain();
          wifi10::rptText("page -> main");                // ★ v1.5.20:回 C3
        } else {
          ui::toLog();
          { char b[24];
            snprintf(b, sizeof(b), "page -> %s", ui::pageName());
            wifi10::rptText(b); }
        }
        break;
      case 'd':                          // ★ 2026-10-06:切第二界面"动图页"
        ui::toggle();                    // 和"点报文区"完全等价(面板镜像走的就是这条)
        { char b[24];                    // ★ v1.5.20:回 C3
          snprintf(b, sizeof(b), "page -> %s", ui::pageName());
          wifi10::rptText(b); }
        break;
      case 'p': {                        // ★ v1.10.0:**参数表**(只打印,不修改)
        // 为什么加它:屏上能改之后,排障第一句话一定是"现在这几个数到底是多少?"
        // 屏可能不在手边、也可能正停在别的页 —— 串口一条命令必须能自证。
        // 顺带兼容老习惯:v1.8.3 删掉的 'p' 是"坐标链路打印",那条已经没用了,
        // 这里改用同一个字母做**参数表**,不会有人混淆(措辞完全换掉)。
        Serial.printf("[bench] params: %d/%d non-default (NVS gnstune)\n",
                      tune::changed(), tune::kN);
        for (int i = 0; i < tune::kN; ++i) {
          const tune::Item& it = tune::items[i];
          char v[16];
          tune::fmtVal(v, sizeof(v), i);
          const char* chg = (fabsf(*it.p - it.def) < 1e-3f) ? "" : "  <- changed";
          Serial.printf("  %-9s = %-9s  def %g  range %g..%g  step %g%s\n",
                        it.name, v, it.def, it.lo, it.hi, it.step, chg);
        }
        // 两个入口都写出来:现场三个人,三种记忆。
        Serial.println("[bench] params: touch = hold CHARTS 1.5s (PARAMS) / tap INTEGRITY card on charts page");
        { char b[34];
          snprintf(b, sizeof(b), "PARAM %d chg", tune::changed());
          wifi10::rptText(b); }
        break;
      }
      case 'k': {                        // ★ v1.10.2:**演示注入**(电脑替手按屏)
        // 三种形式(整行;凑行方式与 'w' 逐字相同 —— 手敲终端一次回车整行到):
        //   kt <x> <y>                  点一下(120 ms)
        //   kh <x> <y> <ms>             长按 ms 毫秒(kh 150 257 1800 = 进参数页)
        //   ks <x1> <y1> <x2> <y2> [ms] 划动(默认 400 ms;翻页手势)
        // 坐标就是**屏绘图坐标**(触摸已校准,两者同一套);范围 0..239 / 0..319。
        // 为什么做成"通用坐标"而不是 k-base / k-charts 这种语义命令:见 inj 命名空间注释。
        char ln[64]; int n = 0;
        uint32_t t0 = millis();
        while (millis() - t0 < 100) {
          if (Serial.available()) {
            char ch = (char)Serial.read();
            if (ch == '\n') break;
            if (ch != '\r' && n < (int)sizeof(ln) - 1) ln[n++] = ch;
          }
        }
        ln[n] = 0;
        int X1 = -1, Y1 = -1, X2 = -1, Y2 = -1, MS = 120;
        const char sub = ln[0];
        int got = 0;
        if      (sub == 't') { got = sscanf(ln + 1, "%d %d", &X1, &Y1); }
        else if (sub == 'h') { got = sscanf(ln + 1, "%d %d %d", &X1, &Y1, &MS); }
        else if (sub == 's') { got = sscanf(ln + 1, "%d %d %d %d %d", &X1, &Y1, &X2, &Y2, &MS);
                               if (got == 4) MS = 400; }
        const bool ok = (sub == 't') ? (got == 2)
                      : (sub == 'h') ? (got == 3)
                      : (sub == 's') ? (got == 4 || got == 5) : false;
        if (!ok || X1 < 0 || Y1 < 0 || X1 >= SCR_W || Y1 >= SCR_H ||
            (sub == 's' && (X2 < 0 || Y2 < 0 || X2 >= SCR_W || Y2 >= SCR_H)) ||
            MS < 20 || MS > 10000) {
          Serial.println("[bench] inj usage: kt x y | kh x y ms | ks x1 y1 x2 y2 [ms]");
          break;
        }
        // 注意限定名:`inj` 在 `namespace touch` 里(它替换的正是 touch 的输入)。
        if (sub == 's') touch::inj::queue((uint16_t)X1, (uint16_t)Y1, (uint16_t)X2, (uint16_t)Y2, (uint16_t)MS);
        else            touch::inj::tap((uint16_t)X1, (uint16_t)Y1, (uint16_t)MS);
        // 回显要如实说这是哪种动作 —— 写成一律 "tap" 会让人以为 `kh` 没生效
        // (v1.10.2 实测时就这么误读过一次:长按进了参数页,回显还写着 tap)。
        const char* kn = (sub == 's') ? "swipe" : (sub == 'h') ? "hold" : "tap";
        Serial.printf("[bench] inj: %s (%d,%d)", kn, X1, Y1);
        if (sub == 's') Serial.printf(" -> (%d,%d)", X2, Y2);
        Serial.printf(" %dms\n", MS);
        break;
      }
      case 'w': {                        // WiFi 子命令:wadd/wlist/wdel/wconn
        // 敲的是整行(如 "wadd myssid pass123"),'w' 已读走,把行尾凑齐再交给 wifi10。
        // 最多等 100ms 凑行 —— 手敲终端回车一次整行就到,凑不满就按已收到的算。
        char ln[140]; int n = 0;
        uint32_t t0 = millis();
        while (millis() - t0 < 100) {
          if (Serial.available()) {
            char ch = (char)Serial.read();
            if (ch == '\n') break;
            if (ch != '\r' && n < (int)sizeof(ln) - 1) ln[n++] = ch;
          }
        }
        ln[n] = 0;
        wifi10::cmd(ln);
        break;
      }
      case '?':
        printHelp();
        break;
      default:
        break;                           // 忽略回车换行等
  }
}

// 电脑串口读键:每轮最多 64 字节一轮。串口是 USB-CDC,不是真串口;
//   USB 主机拔插/工具重连的瞬间,available() 有极小概率报出一个假的
//   "还有数据",而 read() 却读不出东西 —— 裸 while 就会卡死在这。
//   64 个字节对手敲命令绰绰有余(一轮处理不完下一圈接着来),
//   但主循环绝不会被它锁住。(2026-10-01 晚加的这道保险,抽函数时保留)
static void readConsole() {
  for (int guard = 0; guard < 64 && Serial.available(); ++guard) {
    consoleKey((char)Serial.read());
  }
}

// 串口每 5 秒打一次状态,方便不开屏也能看数 —— 受 gLogOn 开关管(默认关)。
// force=true 时绕过一切节流和开关(串口命令 s 用):敲了 s 必须立刻出,绝不吞命令。
static void serialReport(bool force) {
  static uint32_t t = 0;
  uint32_t now = millis();
  if (!force) {
    if (!gLogOn) return;               // 日志安静模式:自动状态行不打印
    if (now - t < 5000) return;
  }
  t = now;

  // 末尾 cksumErr = 开机以来"NMEA 校验和失败"的行数,正常恒为 0(2026-10-01 加)。
  // 屏上 par% 掉下来时靠它分死因:
  //   par<100 且 cksumErr 不涨 -> 被"长度/句名"筛掉的,线路本身没问题
  //   par<100 且 cksumErr 在涨 -> 真·线上翻位(接线松/干扰/波特率漂)
  // 想看增量:隔一会儿再敲一次 s,比较两次的数值。
  //
  // 末尾 zda / utc / bj(2026-10-01 加) = 屏顶时钟条那一行的数据源,不开屏也能核对:
  //   zda 0        -> 模块根本没吐 ZDA 句(句名或输出配置问题)
  //   utc 显示 *   -> 句到了但字段不合法,或超过 3 秒没更新(失锁)
  //   utc          -> **原始 UTC**,就是模块报文里那一串(校验换算是否正确看它)
  //   bj           -> **屏上正在显示的那一串**(= utc + CLK_TZ_HOURS 小时)
  //   两者之差应恒等于 8 小时,跨日的那几分钟也一样要对得上。
  //
  // 末尾 rst / boot / loopMax(2026-10-01 晚加)= 现场"屏不动了"的定性工具:
  //   rst USB     -> 不是固件挂,是电脑端串口工具开端口把芯片摁去复位了
  //                  (ILI9341 自带显存,芯片一停屏就定格,所以看着像死机)
  //   rst BROWNOUT/PWR_GLITCH -> 供电问题
  //   rst PANIC/TASK_WDT/INT_WDT -> 固件的问题,这才需要改代码
  //   boot N      -> 自上次拔电以来第 N 次开机;不拔电一直涨 = 中间偷偷复位过
  //   loopMax     -> 本次开机见过的最长一圈主循环;>1500ms 只可能是阻塞调用,
  //                  正常运行在几十毫秒以内
  //                  (旧版 'l' 试灯每敲一次必产生一个约 8 秒的尖峰,v1.5.10 已删该命令;
  //                   现在若还见到 >1500ms,重点查 NVS 写入、首次 sprite 分配、WiFi 重连)
  char utc[24];
  if (!nmea::st.utcValid || (now - nmea::st.utcMs) > 3000) {
    strcpy(utc, "****-**-** **:**:**");
  } else {
    snprintf(utc, sizeof(utc), "%04d-%02d-%02d %02d:%02d:%02d",
             nmea::st.uY, nmea::st.uMo, nmea::st.uD,
             nmea::st.uH, nmea::st.uMi, nmea::st.uS);
  }
  char bj[24];
  disp::clockText(bj, sizeof(bj));       // 屏上那串,原样取出(不重算)

  // ★★ 2026-10-16 修(实机抓到的**崩溃**,不是显示问题):
  //   删 SIM 段时把 `sim::activeNow() ? " [SIM]" : ""` 这个**实参**删了,
  //   格式串里对应的那个 `%s` 忘了删 —— 变成 18 个转换符对 17 个实参。
  //   后果不是"多打一段乱码",而是 `%s` 把 `nmea::st.satsUsed`(小整数)当成
  //   **指针**解引用 -> LoadProhibited panic -> 板子重启。串口敲 `s` 必崩。
  //   原来正确写法是 `[%lus]%s %s | ...`,现在 SIM 没了,两个 %s 并成一个:
  // ★ 2026-10-17(v1.8.3):删掉 `| inj %s` 与它那个实参 —— 注入已删。
  //   ⚠ 这一带的格式串与实参**必须成对改**,改完跑 `python tools/check_printf_args.py`:
  //   少一个实参就是上面那种 panic,不会给编译错误。
  // ★ v1.9.0:追加三列(hpl / pdop / gsa)。`gsa 0` 是最关键的一条诊断 ——
  //   为 0 说明模块**根本不吐 GSA**,PDOP/VDOP 与"参与星表"就是空的,
  //   σ 会退化成"所有看得见的星"的均值(HPL 偏保守)。这一点必须能从串口自证。
  char hb[12];
  if (integ::valid()) snprintf(hb, sizeof(hb), "%.0fm", integ::hpl());
  else                strcpy(hb, "---");
  Serial.printf("[%lus] %s | sat %d/%d | hdop %.1f | cn0 %.0f | off %.0fm | hpl %s | pdop %.1f | gsa %lu | cus %.1f | mp %.1f | base %s | tune %d | cksumErr %lu | zda %lu | utc %s | bj %s | rst %s | boot %lu | loopMax %lums x%lu\n",
                (unsigned long)(now / 1000),
                judge::res.tag,
                nmea::st.satsUsed, nmea::st.satsView, nmea::st.hdop,
                nmea::st.cn0Median, judge::res.offsetM, hb,
                nmea::st.pdop, (unsigned long)nmea::st.gsaCount,
                integ::g.cusum, integ::g.mpMax,          // ★ v1.9.0:两个检测器
                judge::baseSet ? "set" : "---",
                tune::changed(),                         // ★ v1.10.0:几个参数不是出厂值
                (unsigned long)nmea::st.badCksum,
                (unsigned long)nmea::st.zdaCount,
                utc, bj, gRstText,
                (unsigned long)gRtc[kRtcBootSlot],
                (unsigned long)gLoopMaxGap, (unsigned long)gLoopStalls);
  // 注:v1.5.13 这里曾在行尾追加 5 个链路字段(rx=/cmds=/tx=/ok=/脚位)——
  //     v1.5.14 出正式版时摘除(调试口不留在正式固件里)。链路是否通,看 C3 屏上的 [s3] 行。

  pps::reportIntegral();       // PPS 观测:5 秒日志里只带**积分行**(瞬时行太噪,留给 'g')
}

void setup() {
  Serial.begin(115200);

  // ==========================================================================
  //  ★★ 串口写超时 —— 现场"跑着跑着报文不滚、时钟也不走了,像死机"的**真凶** ★★
  // --------------------------------------------------------------------------
  //  这一行原来写的是 Serial.setTxTimeoutMs(0),注释是"没人听就直接丢弃,
  //  绝不阻塞主循环"。**意图没错,但 0 在 HWCDC 里的含义恰好相反 ——
  //  它不是"不等待",而是"永远等下去"。**
  //
  //  看本机实测的核心源码 cores/esp32/HWCDC.cpp 的 HWCDC::write()(节选):
  //
  //    size_t space = xRingbufferGetCurFreeSize(tx_ring_buf);  // 发送环,固定 256B
  //    ...
  //    uint32_t tries = tx_timeout_ms;                  // 超时=0 ⇒ tries = 0
  //    while (connected && to_send) {
  //      space = xRingbufferGetCurFreeSize(tx_ring_buf);
  //      if (xRingbufferSend(..., tx_timeout_ms / portTICK_PERIOD_MS) != pdTRUE) break;
  //      if (last_toSend == to_send) {                  // 这一圈一个字节都没发出去
  //        tries--;  delay(1);                          // 0 减 1 → 0xFFFFFFFF
  //      }
  //      if (tries == 0) { size = so_far; connected = false; }  // ← 永远撞不到 0
  //    }
  //
  //  所以只要"USB 插着(connected 为真),但电脑那头一时不取数据",
  //  256 字节的发送环一满,write() 就**卡在这个 while 里、每圈 delay(1)、
  //  几乎永远退不出来**。主循环被拖死 ⇒ 报文停、时钟停、判据也停 ⇒ 看着像死机。
  //  电脑一恢复读取,硬件把环腾空,它立刻又活过来 —— 这就是"有时候"会停。
  //
  //  ⚠ 最容易命中的场景:HWCDC 的 connected **只表示"USB 插着"**,
  //    不表示"有人在读"。所以**把演示面板/串口监视器关掉、而线还插着**,
  //    或者换插到只供电的口、电脑休眠一下 —— 都会命中。
  //
  //  修法两层(缺一不可):
  //    ① 这里给一个**小正数** 20ms:tries 非 0,卡满 20ms 就自己 connected=false
  //       退出,之后走"直接丢弃"的快速通道,彻底自愈。
  //       (别用默认的 100ms —— 每行日志卡 100ms 照样拖慢屏;
  //        更别用 0 —— 0 才是那个死循环。)
  //    ② 逐条转发的地方(@CHT)每次写之前先问 availableForWrite(),环快满就整条丢。
  //  原则一句话:**日志可以丢,主循环一秒都不能堵**。
  // ==========================================================================
#if ARDUINO_USB_CDC_ON_BOOT
  constexpr uint32_t kSerialTxTimeoutMs = 20;   // 想调就改这一个数(单位 ms)
  Serial.setTxTimeoutMs(kSerialTxTimeoutMs);
#endif

  // ---- 复位原因体检(2026-10-01 晚加)----
  // 排在所有初始化**之前**打印:万一后面哪一步崩了,这行也已经发出去了。
  // 现场"屏不动了"到底是外部复位、阻塞卡顿、还是固件挂 —— 看这一行。(详见本段开头)
  {
    uint32_t prev = gRtc[kRtcRstSlot];            // 先取上一次的,别被下面覆盖
    esp_reset_reason_t r = bootDiag();
    Serial.printf("[bench] boot #%lu | 本次复位: %s | 上次复位: %s\n",
                  (unsigned long)gRtc[kRtcBootSlot], gRstText,
                  prev ? resetReasonText((esp_reset_reason_t)prev) : "(首次/刚上电)");
    if (r == ESP_RST_USB || r == ESP_RST_JTAG) {
      Serial.println("[bench] ↑↑ 是**电脑端串口工具**复位了板子,不是固件的问题。"
                     "被复位那一瞬屏就定格在最后一帧(ILI9341 自带显存、不用主机刷新),"
                     "看着和死机一模一样。在工具里先把 DTR/RTS 关掉再开端口。");
    } else if (r == ESP_RST_BROWNOUT || r == ESP_RST_PWR_GLITCH) {
      Serial.println("[bench] ↑↑ 供电问题(电压跌落/毛刺):换一根粗一点的 USB 线、"
                     "换一个能出 1A 的口,别走不带供电的 HUB。");
    } else if (r == ESP_RST_PANIC || r == ESP_RST_TASK_WDT ||
               r == ESP_RST_INT_WDT || r == ESP_RST_CPU_LOCKUP) {
      Serial.println("[bench] ↑↑ 这次是**固件**的问题(异常或看门狗)—— 把这一段和"
                     "复位前的最后几行日志留下来,是需要改代码的那种。");
    }
    gRtc[kRtcRstSlot] = (uint32_t)r;              // 留给下一次开机能查
  }

  judge::begin();           // 记录开机时刻,用于冷启动宽限(内部 begin 了 gnsjudge 的 NVS)
  judge::loadBase();        // ★ v1.7.0:从 NVS 恢复基准(带 magic/范围校验);无则 base ---
  tune::load();             // ★ v1.10.0:恢复 PARAMS 页那八个数(NVS gnstune,独立的命名空间)
                            //   ★ 必须在 judge::loadBase() 之后、主循环之前 —— 判据第一拍就要用
                            //     这些值,晚一拍就会出现"用出厂 30 m 判了一次"的假报警。
  eventlog::begin();        // ★ v1.7.0:载入事件池 + 统计累加器(gnsevt)

  gnss::begin();
  pps::begin();             // PPS 秒脉冲中断(GPIO18)。只挂中断,不占主循环时间

  // ---- 时基体检:本机 RTC 慢时钟到底用的哪个源 ----
  // 内部 RC(~136kHz) 还是 外挂 32.768k 晶振,相差 4 倍,一读就分清。
  // 这一步同时回答了"板子上那个 40MHz 晶振在哪" —— 它在模组屏蔽罩里,外面看不见;
  // 而 32.768k 晶振**绝大多数开发板根本没焊**,所以 RTC 只能走内部 RC。
#if HAVE_CLK_TREE
  {
    uint32_t slowHz = 0;
    if (esp_clk_tree_src_get_freq_hz(SOC_MOD_CLK_RTC_SLOW,
                                     ESP_CLK_TREE_SRC_FREQ_PRECISION_APPROX,
                                     &slowHz) == ESP_OK) {
      const char* who = (slowHz >= 30000 && slowHz <= 40000)
                          ? "外挂 32.768k 晶振(准,±20ppm)"
                          : (slowHz >= 100000)
                              ? "片上内部 RC —— 本板没焊 32.768k 晶振(默认,±5%)"
                              : "未知源";
      Serial.printf("[bench] RTC 慢时钟: %lu Hz —— %s\n",
                    (unsigned long)slowHz, who);
    } else {
      Serial.println("[bench] RTC 慢时钟: 查询失败(不影响其它功能)");
    }
  }
#endif

  nmea::reset();
  disp::begin();
  led::begin();
  sht::begin();             // I2C 温湿度。必须排在 disp::begin() 之后 ——
                            // 屏初始化时可能去动 GPIO7,让 I2C 在其后拿回这条线的控制权
  touch::begin();           // 必须在 disp::begin() 之后 —— 它要用 tft

  wifi10::begin();          // ★ v1.5.17:读 NVS 台账 -> 先异步扫 -> 只连"范围内 ∧ 已存档"里最强的
  benchlink::begin();       // v1.5.23:与 C3 命令板的串口链路(GPIO44=RX / GPIO43=TX,走 UART2)
  splash::run();            // v1.5.6:开机动画 + 自检(内部自己等 GNSS 首报文,最多 3s)

  // ★ v1.6.0:开局自报协议号 —— 与 C3 的 `[c3] proto P1` 对称,排障时(尤其
  //   "是不是两块板烧错版本了")一眼就能比对,不用去翻源码或比那两个不可比的固件号。
  Serial.printf("[bench] proto P%d\n", kProto);
  // ★ v1.6.0:横幅里的版本号**不写死** —— 用 `splash::kVer` 拼(C3 侧 v1.1.0 刚在
  //   实机上抓到同一处漂移:板上跑 v1.1.0、横幅还报 v1.0.3)。写死必漂,一律占位符。
  Serial.printf("[bench] boot ok (%s: 链路协议号握手 —— 心跳 `@k ping P%d` 带号,C3 收到带号的 ping 时"
                "本侧回 `@s ok proto P%d`;两边不一致只**告警不拒绝执行**(串口一行 + 给 C3 一条 txt 回执);"
                "老 C3(不带号的 ping)时本侧保持沉默,行为与 v1.5.25 完全一致;协议号改动**两板必须同批烧**,"
                "校验跑 tools/check_proto_sync.py;上一版 v1.5.25 的 TOUCH 自动重启见 docs/变更记录-S3.md)\n",
                splash::kVer, (int)kProto, (int)kProto);
  printHelp();
}

void loop() {
  gnss::poll();             // 1 收(真实 UART)
  nmea::drain();            // 2 解
  judge::update();          // 3 判
  sht::poll();              // 4 环境温湿度(内部自己按 2 秒节流)
  readConsole();            // 5 串口命令(电脑 USB)
  wifi10::poll();           // ★ v1.5.17:① "连上才存"状态机 ② 收割异步扫描结果
                            //   (排在 benchlink::poll 之前 —— 连上/失败的回执同一圈就发出去)
  benchlink::poll();        // 5.5 C3 命令板的串口上行(与 5 共用同一份 consoleKey 分派)

  // ★ v1.5.25:TOUCH('r') 清完校准的自动重启(用户 2026-10-16 拍板)。
  //   走到这里时,本圈 benchlink::poll 已经把 "@s ok r" 回显发出去了;
  //   再把 rptQ 排干(rptNext 一条条取,不限每圈 2 条 —— 重启前最后一次机会),
  //   flush 等 UART 真发完,留 1.5 s 给 C3 收行,然后重启。
  //   重启瞬间链路会断 ~2 s:C3 心跳下一拍自动恢复,屏上 LINK 短暂 "--" 属预期。
  if (gTouchRebootPend) {
    gTouchRebootPend = false;
    char rp[64];
    while (wifi10::rptNext(rp, sizeof(rp)))
      Serial2.printf("@s ok %s\r\n", rp);   // 格式与 benchlink::poll 逐字一致
    Serial2.flush();                        // 等 UART2 真发完再动
    Serial.println("[bench] touch: 校准已清,1.5s 后重启进 4 点校准 ...");
    Serial.flush();
    delay(1500);
    ESP.restart();                          // 不返回;重启后 cal_flag=0 -> 直进 4 点校准
  }
  touch::poll();            // 触摸点击

  uint32_t now = millis();

  // ---- 主循环卡顿记账(2026-10-01 晚加)----
  // 两次 loop 之间的间隔 = 上一圈总共花了多久。正常运行在几十毫秒以内
  // (最重的是报文窗整块重画,几十毫秒量级)。出现 >1500ms 只有一种解释:
  // 中间有处阻塞调用 —— 典型是 NVS 落盘或首次 sprite 分配(旧版还有串口 'l'
  // 试灯,每敲一次约 8 秒)。v1.5.10 已删 'l'。
  // 间隔只记账、不干预:知道了它,才能把"屏不动了"分成外部复位 / 阻塞 / 真挂。
  if (gLoopLastMs) {
    uint32_t gap = now - gLoopLastMs;
    if (gap > gLoopMaxGap) gLoopMaxGap = gap;
    if (gap > 1500) gLoopStalls++;
  }
  gLoopLastMs = now;

  if (now - tLed    > 100) { tLed    = now; led::apply(); }

  // ★ 2026-10-06:按当前页面分流。
  //   动图页独占全屏,主界面那几路刷新**全部停掉** —— 报文窗 sprite 在进页时
  //   已经归还(根本没法画),其余画上去也会立刻被动图页的整帧盖掉,纯属白费 CPU。
  //   分流的另一层意义:动图页每秒一次全屏 push(约 30ms),和报文窗的重画
  //   不会挤在同一拍,时序上更好预期。
  // ★★ v1.7.0:按页面分流(四页)。
  //   主界面   —— 报文窗签名检查 + 主界面 tick(200ms;内部按块比对,没变不推屏)。
  //   第二界面 —— 1s 整屏 tick。
  //   第三/四  —— 400ms 检查(只有 needPaint 才真的重绘)。
  //   各页面的 sprite 互斥申请,峰值内存始终只有一份 153,600B。
  if (ui::page == ui::PAGE_MAIN) {
    // 报文窗节拍:MSG_REFRESH_MS(400ms = 2.5Hz)。只有**签名变了**(有新报文)才真的重画;
    // 而且只有报文窗开着时才推屏 —— 关着时这个函数几乎不花时间。
    if (now - tMsg > MSG_REFRESH_MS) { tMsg = now; disp::drawMessages(); }
    if (now - tMain > 200)           { tMain = now; disp::tick(); }
  } else if (ui::page == ui::PAGE_CHARTS) {
    if (now - tCharts > charts::PERIOD_MS) { tCharts = now; charts::tick(); }
  } else {
    if (now - tEvt > 400) { tEvt = now; eventlog::tick(ui::page == ui::PAGE_EVT); }
  }

  eventlog::poll();         // ★ v1.7.0:事件记录 + 统计。NVS 写全落在这里(主循环收尾)
  serialReport(false);      // 主循环里的自动打印,保持 5 秒一次
}

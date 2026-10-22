// ============================================================================
//  C3_CmdPad —— ESP32-C3 SuperMini 命令输入板(现场配网工装 / 演示命令板)
//  作用:本机键盘 + 菜单 -> 发命令给 S3 台架;自己也能扫描/存 WiFi 台账
// ----------------------------------------------------------------------------
//  ★ 版本沿革(v0.7 → v1.0.1 的**完整**改动记录,含每次为什么改)见
//    docs/变更记录-C3.md —— 文件头只留**现在有效**的口径,历史过程不再堆这里。
// ----------------------------------------------------------------------------
//  ★★ 现行口径(接线 / 禁令 / 编译)—— 改代码前先读这一段 ★★
//   ① **链路脚**:C3 `GPIO20(RX) / GPIO21(TX)` ↔ S3 `GPIO44(RX) / GPIO43(TX)`,
//      即两侧各自的**原生 UART0**。接线颜色:
//        `C3-20 (白) <- S3-43(TX)` ｜ `C3-21 (棕) -> S3-44(RX)` ｜ `GND (黑) 共地`
//      黑线夹在两根信号线**中间**(抑制串扰)。权威接线表见
//        docs/接线表-双板与彩色杜邦线配色.md。
//      ⚠ **改了引脚定义就必须改线**(v0.8.4 踩过:"定义改了、杜邦线还插在旧孔上")。
//      ⚠ 21 是 `U0TXD`:C3 复位瞬间 ROM 日志会灌给 S3 —— 靠 **S3 的前缀重同步**吃掉噪声。
//   ② **GPIO8 / GPIO9 / GPIO5 三只脚不再作链路使用**,别再按旧注释接线:
//      `GPIO8` = 板载 LED(**2 s 翻转一次**的心跳灯,即亮 2 s / 灭 2 s、周期 4 s);
//      `GPIO9` = BOOT 键(按下即接地,运行时别按);`GPIO2` = strapping(**保持空**);
//      `GPIO5` = 空。**三者都不接任何线**(LED 不占排针)。
//      ⚠ GPIO8 是 strapping(上电须为高)⇒ **别在这只脚上外挂对地电阻/按钮**。
//   ③ **屏上所有文案纯 ASCII** —— TFT_eSPI 的 GLCD 字体画不出汉字,汉字会变乱符号。
//   ④ **双轨版本号**:本文件 `v1.x` = 正式版/可交付,`0.x` = 开发中。
//      升版依据唯一 = "完成一轮用户确认的改动 → 号 +1";MAJOR 只在换硬件/改协议时动。
//      规则见 AGENTS.md「版本号规则」。**号段与 S3 互不干涉**,`v0.x` 不代表落后。
//   ⑤ **必须与 S3 同批烧录**(链路脚成对改) —— 两板不同批 = C3 发 20/21、S3 收 38/39,
//      链路直接断。当前配对:**C3 v1.0.1 ↔ S3 v1.5.24**。
// ----------------------------------------------------------------------------

//  硬件:ESP32-C3FH4 SuperMini + ILI9341 2.4" 触摸屏(与 S3 台架同款)
//  屏引脚:CLK=6  MOSI=7  MISO=4  CS=10  DC=3  RST=1  T_CS=0  BLK->3V3  PEN不接
//          (接线表与本文件末尾的 printHelp 是同一份)
//
//  功能地图(当前实现):
//    1. 命令板主界面(A 方案骨架):
//         顶栏  COMMAND PAD + 右上角 WiFi 符号
//         输出卡 OUTPUT :命令回显,14 行环形缓存(满了整体前移,最旧顶掉)
//         状态行        :LINK ok/-- | 115200 | <本机IP>(t=cal 已删,原位改显 IP)
//         底部三格键    :MENU(绿) / RESEND / CLR,各 72x64
//         RESEND 键 = 重发最近一条命令(现场救急用;v1.2.1 起标签 RESEND)
//    2. 菜单弹出(点 MENU)-> 全宽卡片从屏底**滑上来**,底边贴屏底:
//         ★ **三级**结构:一级 5 组 -> 二级项 -> 三级(SYSTEM/TOUCH、WIFI/S3、WIFI/C3)。
//         ★ 卡片高度**按项数自适应**:h = 26(头) + 2 + 28*n + 4;6 项=200(最高),2 项=88。
//           (当前:行高 28、内置 Font2 比例字体)
//           (当前:每层上限 6 项 —— WIFI/S3 那一层要装得下 SCAN+PUSH)
//         ★ 头部是面包屑(`< SYSTEM / TOUCH`);**点头部 = 返回上一级**;点菜单外 = 全收起。
//         ★ 行尾 `>` = 还有下一级;橙字 `keys` = 该项要键盘(点它直接开键盘)。
//         ★ 渲染进 sprite(240x200 = 96,000 B),用 6 参 pushSprite 只推卡片那块做滑入。
//           (关菜单即释放;分配失败降级为直接画屏、无动画)
//         ★ 关闭 / 换层时**不读屏**(readRect 实测不可用)-> 按内部状态重绘。
//    3. 虚拟键盘 MODE_KB:
//         240x320 全屏,5 行键位(单位宽 24px),两行字段(SSID / PASS,密码遮 *),
//         点字段行切格,SHIFT 切大小写,SAVE/DEL 提交,点顶栏取消。
//         WIFI 四支都通了:C3 本机 ADD/DEL 直接写 NVS;S3 那边发 "@k wadd ..." 过去。
//    4. 与 S3 的链路真收发:
//         上行 "@k <payload>"(单键或整行 wifi 子命令);下行 **"@s ok <payload>"**,
//         收到后剥掉 "@s ok " 再显示成 [s3] <payload> 行。
//         带前缀是为了防"悬空 RX 噪声误触发命令"。
//         (判据必须按 **"@s ok " 全前缀**剥 —— 早期只剥 "@s " 三个字符,
//          结果 pl 恒带 "ok ",把 scLineSink() 的三处判据全废掉 —— 详见 poll() 里那段。)
//         S3 侧对应实现在同项目 GNSS_Bench 的 benchlink 段。
//    5. 校准屏 CAL 卡有个 **BOARD** 按钮:从菜单跳过来,点它回命令板
//         (以前只能回电脑敲 't' 才出得去)。
//    6. 基础件:开机画面 + 自检 4 行 + 触摸 4 点校准(NVS gnsc3touch)
//       + WiFi 10 组 FIFO 存档(NVS wifi10)+ 深色"校准界面"(用户定稿保留)。
//       串口 't' 在 命令板 <-> 校准屏 之间切换。
//    7. 调试钩子(串口,不用手指就能验):k add c3 / k del c3 / k add s3 / k del s3 开键盘;
//       y <text> 模拟敲键('|' = 切字段,末尾自动 OK)。
//
//  ★ 编译:必须带 -DC3_CMDPAD,TFT_eSPI 才走 Setup_C3_CmdPad.h
//    (User_Setup_Select.h 已做好宏切换;不带宏=默认 S3 配置,引脚全错)
//  板管理:Arduino-ESP32 3.3.8;FQBN esp32:esp32:esp32c3:CDCOnBoot=cdc
//    (SuperMini 无 USB 转接芯片,靠 GPIO18/19 原生 USB,CDC 打开才有串口)
//
//  实测底数(2026-10-11 C3_MemProbe 实测,后续上机复测):
//    · 无 PSRAM;WiFi 开启后 largest 连续块 ≈ **114,676 B**
//    · ⇒ 全屏 16bpp sprite(153,600 B)**永远分配失败** —— 不能用它做整屏动画
//    · 菜单卡 sprite 现为 **240x200 = 96,000 B**(行高 28 × 6 项),留 18.7 KB 余量;
//      分配失败会自动降级为**直接画屏(无滑入动画,功能不减)**,串口打印一行;
//      关菜单即 deleteSprite,把这 96 KB 还给 WiFi 栈。
//      开机 / 开菜单时串口会打印实际分配结果
//    · readRect 读屏不可用(读回来全是错值)⇒ 收/换菜单只能"按内部状态重绘" 
// ============================================================================

#include <TFT_eSPI.h>
#include <WiFi.h>
#include <Preferences.h>
#include <esp_wifi.h>        // ★ v0.8:esp_wifi_deinit() —— 收摊时把射频驱动内存真的还回堆

// ★★ v1.2.3(2026-10-17,用户报「C3 进 log data 左上角少了事件类型」)——
//   **编译期哨兵:本板必须能画 4 号字**。
//   起因:C3 编译带 -DC3_CMDPAD,TFT_eSPI 会走 `User_Setups/Setup_C3_CmdPad.h`
//   (不是 S3 用的根目录 `User_Setup.h`)。那个文件原先只开了 LOAD_GLCD/LOAD_FONT2,
//   **没有 LOAD_FONT4** ⇒ `setTextFont(4)` 之后 `drawString()` **一个像素都不画**:
//   编译零告警、运行不崩溃,只是那行字凭空消失 —— 搬运页详情页的"事件类型大字"
//   (eFont(4),与 S3 第 4 界面同款)就是这么整段丢掉两天的。
//   ⇒ 这条不是"记得去改库"的口头约定,而是**编译不过**的硬约束:
//   换机器/重装库忘了加 LOAD_FONT4 时,这里会直接报错点名。
#ifndef LOAD_FONT4
#error "TFT_eSPI: LOAD_FONT4 未开启 —— C3 走的是 User_Setups/Setup_C3_CmdPad.h, 请在其中加 #define LOAD_FONT4 (搬运页事件类型大字要用 4 号字)"
#endif

TFT_eSPI tft;

// ------------------------------------------------------------------ 屏幕常量
constexpr int SCR_W = 240;
constexpr int SCR_H = 320;

// 深色配色(RGB565):主界面走"深底色"(用户 2026-10-11 拍板)
constexpr uint16_t CLR_BG   = 0x0882;   // 底     #0E1116
constexpr uint16_t CLR_BAR  = 0x18E4;   // 卡片   #1A1F27
constexpr uint16_t CLR_SEL  = 0x2166;   // 选中行底(比卡片亮一档)#252C36
constexpr uint16_t CLR_FG   = 0xE75E;   // 主文字 #E6EAF0
constexpr uint16_t CLR_DIM  = 0x8C94;   // 次文字 #8A93A0
constexpr uint16_t CLR_LINE = 0x3A09;   // 分隔线 #3A4048
constexpr uint16_t CLR_ACC  = TFT_GREEN;
constexpr uint16_t CLR_WARN = TFT_ORANGE;
constexpr uint16_t CLR_ERR  = TFT_RED;
constexpr uint16_t CLR_SELB = TFT_CYAN; // 选中态左侧指示条
constexpr uint16_t kGrey    = 0x8410;   // TFT_eSPI 本配置没定义 TFT_GREY,本地给

// 顶栏 + WiFi 图标(RF_Source_Controller 那套:4 段弧,半径 1/5/9/13,朝上 135~225°)
constexpr int BAR_H  = 24;
constexpr int ICON_X = 210;   // 图标左上角 x(宽 26,贴右留 4)
constexpr int ICON_Y = 4;
constexpr int ICON_H = 16;

// ============================================================================
//  触摸:4 点校准 + 存 NVS
// ----------------------------------------------------------------------------
//  ★ 校准值不能照抄 S3 —— 同款屏但走线/摆位不同,C3 必须自己校一遍。
//  命名空间故意与 S3 的 gnstouch 区分开(两台板子可能插在同一台电脑上,别串了)。
// ============================================================================
namespace touch {

static bool        calibrated = false;
// 未校准前的初值(量纲同 S3,仅作占位;真正生效要过 calibrateTouch)
static uint16_t    calData[5] = {192, 3708, 281, 3512, 5};
static Preferences prefs;

constexpr const char* kNvsName = "gnsc3touch";
constexpr int      kPressure = 600;      // 按下判定压力阈值
constexpr int      kDebounce = 350;      // 两次有效点击最小间隔 ms
constexpr uint32_t kTouchWarm = 2000;    // 上电头 2 秒不认触摸(防幽灵点击)

static uint32_t lastMs  = 0;
static bool     wasDown = true;          // 初值 true:必须先真抬手一次才认第一个点击

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
    tft.setTouch(calData);
    Serial.printf("[c3] touch: cal loaded %d,%d,%d,%d,%d\n",
                  calData[0], calData[1], calData[2], calData[3], calData[4]);
  } else {
    Serial.println("[c3] touch: 首次开机 -> 请在屏上依次点 4 个十字完成校准");
    tft.fillScreen(TFT_BLACK);
    tft.setTextSize(2);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextDatum(TC_DATUM);
    tft.drawString("Touch the cross", SCR_W / 2, 140);
    tft.setTextDatum(TL_DATUM);
    tft.calibrateTouch(calData, TFT_WHITE, TFT_BLACK, 3);
    saveCal();
    calibrated = true;
    tft.setTouch(calData);
    Serial.printf("[c3] touch: cal saved %d,%d,%d,%d,%d\n",
                  calData[0], calData[1], calData[2], calData[3], calData[4]);
    tft.fillScreen(TFT_BLACK);
  }
}

inline void resetCal() {
  prefs.putUInt("cal_flag", 0);
  Serial.println("[c3] touch: 校准值已清除 -- 重启后重新校准");
}

inline void printCal() {
  Serial.printf("[c3] touch cal: %d,%d,%d,%d,%d\n",
                calData[0], calData[1], calData[2], calData[3], calData[4]);
}

// 给校准界面(CAL 卡)读当前 5 个校准值用
inline const uint16_t* cal() { return calData; }

// 屏上 RECAL 按钮:清校准 + 重启 -> 直接回到 4 点校准(等于 'r' + 复位,不用回电脑)
inline void forceRecal() {
  resetCal();
  Serial.println("[c3] touch: rebooting for 4-point calibration ...");
  delay(150);                 // 等 NVS 落盘 + 串口把这行发完
  ESP.restart();
}

// 返回 true = 发生一次"新按下",屏幕坐标写回 px/py
inline bool poll(int* px, int* py) {
  uint16_t x = 0, y = 0;
  uint32_t now = millis();
  bool down = tft.getTouch(&x, &y, kPressure);

  if (now < kTouchWarm) { wasDown = down; return false; }   // 上电头 2s 不认

  bool fired = false;
  if (down && !wasDown && (now - lastMs > kDebounce)) {
    lastMs = now;
    *px = x; *py = y;
    fired = true;
  }
  wasDown = down;
  return fired;
}

} // namespace touch

// ============================================================================
//  WiFi 10 组存档(命名空间与 S3 同名、机制与 S3 完全一致)
// ----------------------------------------------------------------------------
//  ★ v0.8 按用户 2026-10-11 定的机制重写(与 S3 v1.5.17 同一套,参照
//    RF_Source_Controller 跑通过的做法):
//    ① 扫 -> 出列表 -> 点一条 SSID -> 弹键盘敲密码 -> **立刻连**;
//    ② **拿到正确 IP 才算成功**,成功之后才把 SSID+密码**一起**写进 NVS 台账;
//    ③ 连不上 ⇒ **一个字都不存**("连都没连上就存,那条是废的");
//    ④ 台账 **10 组**:新条目插**最前**;同 SSID 先删旧再插最前;满 10 丢**最旧**;
//    ⑤ 开机不再无脑连"存档第 1 条" —— 先扫,只在"已存档 ∧ 在范围内"里挑
//       **信号最强**的那条连;一条都不在范围就不连(旧行为正是 C3 屏上一直
//       "CONNECTING..."/"FAIL" 的原因)。
//  ★ 全程**非阻塞**:参考实现那个"等 10 秒"是阻塞 delay 写的,本机主循环
//    (触摸/扫描/链路)一秒都不能堵,所以做成状态机:poll() 每圈问一句。
//  ★ 为什么"扫描前必须 disconnect()":ESP-IDF 明写
//      ESP_ERR_WIFI_STATE: WiFi still connecting when invoke esp_wifi_scan_start
//    —— **连接尝试还在跑的时候驱动拒绝扫描**。C3 旧版开机就在连一条连不上的
//    存档网(连接尝试永远挂着),于是每次扫描都被拒(实测 `got -2 AP`),
//    屏上却显示成 "no AP found" —— 2026-10-11 排查被这行文案带偏过一次。
// ============================================================================
namespace wifi10 {

constexpr int         kMax     = 10;
constexpr const char* kNvsName = "wifi10";

struct Entry { char ssid[33]; char pass[65]; };   // SSID <=32+1;WPA2 密码 <=64+1
static Entry       list[kMax];
static int         cnt = 0;      // 已存组数 0..10(#1 = 最新那条)
static int         cur = 0;      // 正在连的组号(1 起算),0=没在连
static Preferences prefs;
static bool        wifiUp = false;

static void load() {
  prefs.begin(kNvsName, false);
  cnt = (int)prefs.getUInt("cnt", 0);
  if (cnt > kMax) cnt = kMax;
  char k[16];   // ★ 2026-10-15:8 -> 16。原来刚好放下 "ssid10"(7 字节),靠 kMax≤10 兜着;
                //   一旦 kMax 改成 100 就会撞名(键变 "ssid1")。加宽后编译器也不再报截断警告。
  for (int i = 0; i < cnt; ++i) {
    snprintf(k, sizeof(k), "ssid%d", i + 1);
    prefs.getString(k, list[i].ssid, sizeof(list[i].ssid));
    snprintf(k, sizeof(k), "pass%d", i + 1);
    prefs.getString(k, list[i].pass, sizeof(list[i].pass));
  }
}

static void saveAll() {
  prefs.putUInt("cnt", (uint32_t)cnt);
  char k[16];   // ★ 2026-10-15:8 -> 16。原来刚好放下 "ssid10"(7 字节),靠 kMax≤10 兜着;
                //   一旦 kMax 改成 100 就会撞名(键变 "ssid1")。加宽后编译器也不再报截断警告。
  for (int i = 0; i < cnt; ++i) {
    snprintf(k, sizeof(k), "ssid%d", i + 1); prefs.putString(k, list[i].ssid);
    snprintf(k, sizeof(k), "pass%d", i + 1); prefs.putString(k, list[i].pass);
  }
}

// ---- 台账:插最前 + 去重 + 满 10 丢最旧 ----
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

// ---- 事件队列:给 UI 用(outPush 定义在后面,不能直接调) ----
constexpr int kEvtMax = 4;
static char evtQ[kEvtMax][48];
static int  evtH = 0, evtN = 0;
static void queueEvt(const char* s) {
  if (evtN == kEvtMax) { evtH = (evtH + 1) % kEvtMax; --evtN; }
  snprintf(evtQ[(evtH + evtN) % kEvtMax], sizeof(evtQ[0]), "%s", s);
  ++evtN;
}
static bool evtNext(char* out, size_t n) {
  if (!evtN) return false;
  snprintf(out, n, "%s", evtQ[evtH]);
  evtH = (evtH + 1) % kEvtMax;
  --evtN;
  return true;
}

// ---- "连上才存"状态机 ----
constexpr uint32_t kConnMs = 10000;
static bool     csBusy = false;
static uint32_t csT0   = 0;
static uint32_t csBeat = 0;          // ★ v0.8.2:连接期 2s 一次的状态心跳
static char     csSsid[33] = "";
static char     csPass[65] = "";

// ★ v0.8.7:巡台账("按台账顺序挨个试连")状态 —— 用户 2026-10-12 定
//   "有台账就按列表按序试,别人工不自己扫描"。与 S3 v1.5.19/v1.5.20 同一套语义。
static bool     trOn  = false;       // true = 正在从 #1 往 #trN 挨个试
static int      trN   = 0;           // 本次要试到第几条(1 起算)
static int      trIdx = 0;           // 当前正试第几条
static volatile uint8_t evFast = 0;  // 事件回调置的"快失败"reason(201/202),poll() 里消费

// ★★ v0.8.2 射频档位(详见文件头 v0.8.2 段)
// ----------------------------------------------------------------------------
//  34 × 0.25dBm = 8.5dBm。单位/映射表见 esp_wifi.h L1120 的注释:
//    "Param power unit is 0.25dBm, range is [8, 84]",
//    "Mapping Table {Power, max_tx_power} = {{8,2},{20,5},{28,7},{34,8},{44,11},...}"
//  ★ 头文件里**没有** WIFI_POWER_* 枚举(实测 grep 不到),所以直接调官方 API,
//    不用 Arduino 的 WiFi.setTxPower()(它内部也就是调这一个函数)。
static int8_t kTxDefault = 0;        // 原始默认功率(值域 8..84,不会是 0)
static bool   kTxSaved   = false;    // 是否已经把原始值存下来了

static void txPowerForConnect() {
  if (!kTxSaved) {                   // ★ 必须在第一次降功率**之前**读,否则会把 34 当默认
    int8_t d = 0;
    if (esp_wifi_get_max_tx_power(&d) == ESP_OK) { kTxDefault = d; kTxSaved = true; }
  }
  const esp_err_t e = esp_wifi_set_max_tx_power(34);     // 20.5dBm -> 8.5dBm
  const esp_err_t p = esp_wifi_set_ps(WIFI_PS_NONE);     // 关 modem sleep
  Serial.printf("[c3] wifi: radio -> tx 8.5dBm (%s), ps NONE (%s)\n",
                esp_err_to_name(e), esp_err_to_name(p));
}

static void txPowerForScan() {       // 扫描要的是发射距离,把功率还回去
  if (!kTxSaved) return;             // 从没降过 -> 本来就在默认档,别多事
  const esp_err_t e = esp_wifi_set_max_tx_power(kTxDefault);
  Serial.printf("[c3] wifi: radio -> tx back to %d (default) (%s)\n",
                (int)kTxDefault, esp_err_to_name(e));
}

// 断连原因码 -> 人话(esp_wifi_types_generic.h 的 wifi_err_reason_t)
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

// 事件回调 —— ★ 只打日志,不改任何行为(自动重连已由 setAutoReconnect(false) 关掉)
static void onWifiEvt(arduino_event_id_t id, arduino_event_info_t info) {
  if (id == ARDUINO_EVENT_WIFI_STA_CONNECTED) {
    Serial.printf("[c3] wifi: EVENT connected ssid=\"%.*s\" ch=%u auth=%d\n",
                  (int)info.wifi_sta_connected.ssid_len,
                  (const char*)info.wifi_sta_connected.ssid,
                  (unsigned)info.wifi_sta_connected.channel,
                  (int)info.wifi_sta_connected.authmode);
  } else if (id == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
    Serial.printf("[c3] wifi: EVENT DISCONNECTED ssid=\"%.*s\" reason=%u (%s) rssi=%d\n",
                  (int)info.wifi_sta_disconnected.ssid_len,
                  (const char*)info.wifi_sta_disconnected.ssid,
                  (unsigned)info.wifi_sta_disconnected.reason,
                  wifiReasonName(info.wifi_sta_disconnected.reason),
                  (int)info.wifi_sta_disconnected.rssi);
    // ★ v0.8.7:快失败信号 —— 只在"正在连"时收,避免巡航/空闲期的杂散事件误触发。
    //   201 NO_AP_FOUND = 根本扫不到这个 AP(不在范围);202 AUTH_FAIL = 密码错。
    //   这两种再等满 10 s 纯属浪费 —— 巡视台账时直接跳下一条(移植自 S3 v1.5.19)。
    const uint8_t fr = (uint8_t)info.wifi_sta_disconnected.reason;
    if (csBusy && (fr == 201 || fr == 202)) evFast = fr;
  }
}

static void radioOff() {
  if (csBusy) return;
  if (WiFi.status() == WL_CONNECTED) return;
  if (!wifiUp) return;
  WiFi.mode(WIFI_OFF);
  esp_wifi_deinit();        // 光 mode(WIFI_OFF) 不还内存,必须补这一发(见 S3 v1.5.16)
  wifiUp = false;
}

// ★★ 扫描前把射频"归零"(2026-10-11 22:1x **实测**出来的第二层根因,与 S3 同一份)
// ----------------------------------------------------------------------------
//  第一层:ESP-IDF 不允许"连接尝试在跑的时候启动扫描"
//    -> esp_wifi_scan_start() 返回 ESP_ERR_WIFI_STATE -> Arduino 层 -2。
//  ★ 第二层(光 `WiFi.disconnect()` **实测不够**,读完 esp32 核心源码才找着):
//    `libraries/WiFi/src/STA.cpp` L138-172 的 STA 事件回调里写着 ——
//       else if (first_connect) { first_connect = false; DoReconnect = true; }  // 开机第一次断线:无条件重连一次
//       else if (getAutoReconnect() && _is_staReconnectableReason(reason)) DoReconnect = true;
//       ...
//       if (DoReconnect) { disconnect(); connect(); }        // ← 立刻又 esp_wifi_connect()
//    而 `_autoReconnect` 的**默认值是 true**(STA.cpp L231)。
//    ⇒ 我们喊完 `WiFi.disconnect()`,核心马上又 connect 一次 —— 射频永远 connecting,
//      扫描永远是 -2。这不是"没断干净",是"断了马上又被接上"。
//  两手都要:① 关掉核心自动重连;② stop -> start 一轮,强制回 IDLE。
//  实测(C3 v0.8 首版):只做 ① 里的 `disconnect()` 时,扫描屏仍然 `got -2 AP`。
static void radioIdleForScan() {
  if (WiFi.status() == WL_CONNECTED) { Serial.println("[c3] wifi: scan keeps live connection"); return; }
  WiFi.setAutoReconnect(false);
  if (!wifiUp) return;
  WiFi.mode(WIFI_OFF);
  delay(60);
  WiFi.mode(WIFI_STA);
  delay(60);
  txPowerForScan();      // ★ v0.8.2:扫描要的是发射距离,把功率还回默认档
}

// ★ v0.9(用户 2026-10-14 定):把 ESP-IDF 的 reason 译成**人话短标签**。
// ----------------------------------------------------------------------------
//  起因:用户"密码不对的时候,你有没有一个告警提示?" —— 旧版只回 `WIFI FAIL <ssid>`,
//  "连不上"和"密码错"在屏上长得一模一样,用户没法判断该不该重输密码。
//  ⚠ 纯 ASCII:C3 输出卡一行 34 列,中文是 GLCD 字体会画成乱符号(见 rptText 段头那条)。
//  reason 取值见 esp_wifi_types.h(wifi_err_reason_t);与 S3 侧 failTag() 同一张表。
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

// ★ v0.9(O1):连不上的 SSID 记成"密码可疑" —— 下次在扫描页点它,**直接弹键盘**让重输,
//   不再拿名单里那份多半已作废的密码去撞。只存 RAM(最多 4 条),掉电即忘。
static char staleSs[4][24];
static int  staleN = 0;
static void markStale(const char* s) {
  if (!s || !s[0]) return;
  for (int i = 0; i < staleN; ++i) if (!strcmp(staleSs[i], s)) return;   // 已在册
  if (staleN < 4) { snprintf(staleSs[staleN], sizeof(staleSs[0]), "%.18s", s); ++staleN; }
  else {                                                                 // 满了:丢最旧
    for (int i = 0; i < 3; ++i) memcpy(staleSs[i], staleSs[i + 1], sizeof(staleSs[0]));
    snprintf(staleSs[3], sizeof(staleSs[0]), "%.18s", s);
  }
}
static void clearStale(const char* s) {
  for (int i = 0; i < staleN; ++i)
    if (!strcmp(staleSs[i], s)) {
      for (int j = i; j < staleN - 1; ++j) memcpy(staleSs[j], staleSs[j + 1], sizeof(staleSs[0]));
      --staleN;
      return;
    }
}

// 非阻塞发起"连上才存"
static void startConnect(const char* ssid, const char* pass) {
  if (!ssid || !ssid[0]) { Serial.println("[c3] wifi: usage wadd <ssid> <pass>"); return; }
  evFast = 0;                     // ★ v0.8.7:新一次尝试,清掉上一条的快失败信号

  if (WiFi.status() == WL_CONNECTED && WiFi.SSID() == ssid) {   // 已连着它 -> 直接算成功
    trOn = false;                 // ★ v0.8.7:巡台账到此结束(它已经连着)
    insertFront(ssid, pass); saveAll();
    clearStale(ssid);             // ★ v0.9:它已经连着 -> 撤掉"密码可疑"标记
    // ★ v0.9(用户 2026-10-14 定):本机回执也**必须带 SSID** —— 与 S3 侧同规矩,见 GNSS_Bench 的 v1.5.21 注释。
    //   分两行:`WIFI OK <ssid>` + `IP <ip>`(显示成 `[C3] WIFI OK …` / `[C3] IP …`)。
    char b[48];
    snprintf(b, sizeof(b), "WIFI OK %.18s", ssid);
    queueEvt(b);
    snprintf(b, sizeof(b), "IP %s", WiFi.localIP().toString().c_str());
    queueEvt(b);
    Serial.printf("[c3] wifi: already on \"%s\" -> saved front (%d/%d)\n", ssid, cnt, kMax);
    return;
  }

  WiFi.setAutoReconnect(false);   // ★ 别让核心在 disconnect 事件里偷偷再 connect(见 radioIdleForScan)
  if (wifiUp) { WiFi.mode(WIFI_OFF); delay(60); }   // 把状态机停一轮,回到干净 IDLE
  WiFi.mode(WIFI_STA);
  txPowerForConnect();            // ★ v0.8.2:降功率到 8.5dBm + 关省电(见文件头长注)
  WiFi.begin(ssid, pass);
  wifiUp = true;
  csBusy = true;
  csT0   = millis();
  csBeat = millis();              // ★ v0.8.2:心跳起点
  snprintf(csSsid, sizeof(csSsid), "%s", ssid);
  snprintf(csPass, sizeof(csPass), "%s", pass);
  const int k = findSsid(ssid);
  cur = (k >= 0) ? k + 1 : 0;
  Serial.printf("[c3] wifi: connecting \"%s\" ... (saved only if it gets an IP)\n", ssid);
}

static void scanStart(bool forBoot);     // 定义在本段之后(★ v0.8.7 起开机不再走它)
static void tryAll(int n);               // 定义在 connIdx 之后(begin() 要用它)

inline void begin() {                    // 开机:载入台账 -> **按台账顺序挨个试**(不扫描)
  WiFi.setAutoReconnect(false);          // ★ 关掉核心自动重连(默认 true,见 radioIdleForScan)
  // ★ v0.8.2:把断连原因打出来 —— 判别"密码错 / 射频坏 / 距离远"的唯一硬证据
  WiFi.onEvent(onWifiEvt, ARDUINO_EVENT_WIFI_STA_CONNECTED);
  WiFi.onEvent(onWifiEvt, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
  load();
  // ★★ v0.8.7(用户 2026-10-12 23:5x 定):开机**不自己扫描**。
  //   旧版是"扫一遍 -> 只连'范围内 ∧ 已存档'里最强的那一条"(scanStart(true)) ——
  //   那套的适用场景是"完全没连过、手上还没有台账";既然有台账,就按序试。
  //   要不要扫,只在菜单 WIFI/C3/SCAN、WIFI/S3/SCAN 里由人工点(见 scStart)。
  if (cnt > 0) {
    Serial.printf("[c3] wifi: %d saved; trying them in order (no scan)\n", cnt);
    tryAll(cnt);
  } else {
    Serial.println("[c3] wifi: none saved (menu WIFI/C3/ADD, or serial 'w add <ssid> <pass>')");
  }
}

static void connIdx(int n) {
  if (n < 1 || n > cnt) { Serial.println("[c3] wifi: no such entry"); return; }
  startConnect(list[n - 1].ssid, list[n - 1].pass);
}

// ★ v0.8.7:seed —— 只写台账不连(与 S3 的 wseed 对齐,两边 API 一致才不容易记错)
inline void seedNet(const char* ssid, const char* pass) {
  if (!ssid || !ssid[0]) { Serial.println("[c3] wifi: usage w seed <ssid> <pass>"); return; }
  insertFront(ssid, pass); saveAll();
  Serial.printf("[c3] wifi: seeded \"%s\" (%d/%d) -- no connect\n", ssid, cnt, kMax);
}

// ============================================================================
//  ★ v0.8.7:巡台账("按台账顺序挨个试连") —— 与 S3 v1.5.19/1.5.20 同一套语义
// ----------------------------------------------------------------------------
//  用户 2026-10-12 23:5x 定:"有台账就按列表按序试,别人工不自己扫描"。
//    · 开机 begin() 直接进 tryAll(cnt);要扫只在菜单 WIFI/*/SCAN 里点
//    · 连上 -> 立刻停,报 "WIFI OK <ip>"          (poll() 的成功分支发)
//    · 失败 -> 回 "WIFI TRY k/n <ssid>" 当进度,自动接上下一条(connFailed 里接)
//    · 全试完都没成 -> "WIFI FAIL ALL",停在原地(不自动退回扫描)
//  ⚠ 试连期间**不能给它起扫描** —— ESP-IDF 明写"连接中拒绝扫描",会拿到 -2。
//    所以巡台账与 SCAN 菜单项天然互斥,由人工决定先做哪个。
// ============================================================================
// ★ v0.9(O3):"本次连接失败"的整屏告警**请求** —— 由 UI 侧 takeAlert() 取走并弹窗。
//   只在**用户主动点的单条连接**失败时置位(下方 connFailed 的 else 分支里 trOn 恒为 false);
//   开机自动巡台账(trOn=true)失败**不弹** —— 那是后台行为,不该在半路打断用户。
static char alertSs[24]  = "";
static char alertTag[20] = "";
static bool alertReq    = false;

static void connFailed(const char* why) {
  csBusy = false; cur = 0;
  Serial.printf("[c3] wifi: connect \"%s\" FAILED (%s)%s\n", csSsid, why,
                trOn ? " -> try next" : " -> NOT saved");
  if (trOn && trIdx < trN) {                       // 还有下一条 -> 立刻接上
    ++trIdx;
    char b[48];
    snprintf(b, sizeof(b), "WIFI TRY %d/%d %.16s", trIdx, trN, list[trIdx - 1].ssid);
    queueEvt(b);
    connIdx(trIdx);
    return;
  }
  if (trOn) {                                      // 台账全试完,一个都没成
    trOn = false;
    queueEvt("WIFI FAIL ALL");
    Serial.println("[c3] wifi: try: ALL FAILED -> stay offline");
  } else {
    // ★ v0.9:分两行 —— 第一行"谁失败了",第二行"为什么"(原因才是用户要看的那条)
    const char* tag = failTag(why);
    char b[48];
    snprintf(b, sizeof(b), "WIFI FAIL %.18s", csSsid);
    queueEvt(b);
    snprintf(b, sizeof(b), "FAIL %s", tag);
    queueEvt(b);
    markStale(csSsid);                                          // ★ O1:下次点它直接弹键盘重输
    snprintf(alertSs,  sizeof(alertSs),  "%.18s", csSsid);      // ★ O3:请求整屏告警
    snprintf(alertTag, sizeof(alertTag), "%s", tag);
    alertReq = true;
  }
  WiFi.disconnect();
  radioOff();
}

// 巡台账入口:n = 试到第几条(1 起算);n <= 0 或超界 = 试全部
static void tryAll(int n) {
  if (!cnt) { Serial.println("[c3] wifi: try: none saved"); return; }
  if (n < 1 || n > cnt) n = cnt;
  trOn = true; trN = n; trIdx = 1;
  char b[48];
  snprintf(b, sizeof(b), "WIFI TRY %d/%d %s", trIdx, trN, list[0].ssid);
  queueEvt(b);
  Serial.printf("[c3] wifi: try: %d entr%s, start with #1 \"%s\"\n",
                trN, trN == 1 ? "y" : "ies", list[0].ssid);
  connIdx(1);
}

static void del(int n) {
  if (n < 1 || n > cnt) { Serial.println("[c3] wifi: no such entry"); return; }
  for (int i = n - 1; i < cnt - 1; ++i) list[i] = list[i + 1];
  cnt--;
  saveAll();
  Serial.printf("[c3] wifi: deleted #%d, %d left\n", n, cnt);
}

static void listCmd() {
  if (!cnt) { Serial.println("[c3] wifi: none saved"); return; }
  for (int i = 0; i < cnt; ++i)
    Serial.printf("[c3] wifi: #%d %-32s %s%s\n", i + 1, list[i].ssid,
                  list[i].pass[0] ? "(key)" : "(open)",
                  (i + 1 == cur) ? "  <- connecting" : "");
}

// ---- 开机选网用的异步扫描(结果不外发,只给 pickBestStored 挑一条) ----
// ★ v0.8.7 起 **begin() 不再调 scanStart()** —— 开机改成"按台账顺序挨个试"(tryAll)。
//   这套"扫一遍再挑最强的存档网"保留不删:它的适用场景是"首次配网/还没有台账",
//   真要用时把 begin() 里那行换成 scanStart(true) 即可(整条链路都是好的)。
//   ⚠ 人工扫描走的是 scStart(false)/scRestart(),那是另一套(结果直接上扫描屏)。
constexpr uint32_t kScanTimeoutMs = 10000;
constexpr int      kScanMax       = 24;
// ★ 2026-10-15 改名:三个状态码统一成 kScanCode*(原来那个 `-3` 叫 `kScanTimeout`,
//   与上面的毫秒时长 `kScanTimeoutMs` 只差两个字符 —— 谁写一句
//   `if (millis()-scT0 > kScanTimeout)` 都会恒真(`> -3`),超时判断静默失效。
constexpr int      kScanCodeRunning = -1;
constexpr int      kScanCodeFailed  = -2;
constexpr int      kScanCodeTimeout = -3;

static bool     scWaiting = false;
static bool     scBoot    = false;
static uint32_t scT0      = 0;
static int      scN       = 0;
static char     scSsid[kScanMax][33];
static int      scRssi[kScanMax];

// ★ 2026-10-15:`[[maybe_unused]]` —— 这是**预留路径**而不是漏删的死代码(见上面段头:
//   留给"首次配网向导")。标上是为两件事:① 消掉编译器的 `-Wunused-function`;
//   ② 明确告诉读代码的人"它没被落下,是刻意留的后备"。
[[maybe_unused]] static void scanStart(bool forBoot) {
  if (scWaiting) return;
  // ★★ 关键一行:把射频停一轮再起,强制回 IDLE(见 radioIdleForScan 的段头)。
  radioIdleForScan();
  WiFi.mode(WIFI_STA);
  txPowerForScan();      // ★ v0.8.2:radioIdleForScan 在 !wifiUp 时会早退,这里兜底
  wifiUp = true;
  WiFi.scanDelete();
  WiFi.scanNetworks(true, false);
  scWaiting = true; scT0 = millis(); scN = 0; scBoot = forBoot;
  Serial.printf("[c3] wifi: scan started (%s)\n",
                forBoot ? "boot: pick a saved net in range" : "on request");
}

static void scanCollect(int n) {
  scN = 0;
  if (n <= 0) return;
  for (int i = 0; i < n; ++i) {
    const String s = WiFi.SSID(i);
    if (!s.length()) continue;
    const int rssi = (int)WiFi.RSSI(i);
    if (scN >= kScanMax) break;
    snprintf(scSsid[scN], sizeof(scSsid[0]), "%s", s.c_str());
    scRssi[scN] = rssi;
    ++scN;
  }
  Serial.printf("[c3] wifi: scan got %d AP\n", scN);
}

static int pickBestStored() {
  int best = -1, bestR = -1000;
  for (int i = 0; i < scN; ++i)
    for (int j = 0; j < cnt; ++j)
      if (!strcmp(scSsid[i], list[j].ssid) && scRssi[i] > bestR) {
        bestR = scRssi[i]; best = j;
      }
  return best;
}

// 主循环每圈调一次(挂在 ui::tick 里)
static void poll() {
  // ---- ① "连上才存" ----
  if (csBusy) {
    if (evFast) {
      // ★ v0.8.7:事件回调报的 201/202 快失败 —— 别干等满 10s(巡视台账时这是关键)
      const uint8_t r = evFast; evFast = 0;
      char w[40];
      snprintf(w, sizeof(w), "reason=%u %s", (unsigned)r, wifiReasonName(r));
      connFailed(w);
    } else if (WiFi.status() == WL_CONNECTED) {
      csBusy = false; cur = 0;
      trOn   = false;                            // ★ v0.8.7:巡台账到此结束
      insertFront(csSsid, csPass); saveAll();
      clearStale(csSsid);                        // ★ v0.9:这次真连上了 -> 撤掉"密码可疑"标记
      // ★ v0.9:回执带 SSID,分两行(见 startConnect 同处注释)
      char b[48];
      snprintf(b, sizeof(b), "WIFI OK %.18s", csSsid);
      queueEvt(b);
      snprintf(b, sizeof(b), "IP %s", WiFi.localIP().toString().c_str());
      queueEvt(b);
      Serial.printf("[c3] wifi: CONNECTED \"%s\" ip=%s -> saved front (%d/%d)\n",
                    csSsid, WiFi.localIP().toString().c_str(), cnt, kMax);
    } else if (millis() - csT0 > kConnMs) {
      connFailed("TIMEOUT 10s");                 // ★ v0.8.7:统一收口,自动跳下一条
    } else if (millis() - csBeat >= 2000) {
      // ★ v0.8.2:每 2s 打一次心跳,看它卡在哪一步(status / rssi)
      csBeat = millis();
      Serial.printf("[c3] wifi: ... %lus status=%d rssi=%d\n",
                    (unsigned long)((millis() - csT0) / 1000),
                    (int)WiFi.status(), (int)WiFi.RSSI());
    }
  }

  // ---- ② 开机选网扫描收割 ----
  if (scWaiting) {
    const int r = WiFi.scanComplete();
    if (r == kScanCodeRunning) {
      if (millis() - scT0 <= kScanTimeoutMs) return;
      Serial.println("[c3] wifi: boot scan timeout");
      WiFi.scanDelete(); scN = 0;
    } else if (r == kScanCodeFailed) {
      Serial.println("[c3] wifi: boot scan FAILED(-2 driver refused: radio busy?)");
      scN = 0;
    } else {
      scanCollect(r);
      WiFi.scanDelete();
    }
    scWaiting = false;
    const int j = scBoot ? pickBestStored() : -1;
    scBoot = false;
    if (j >= 0) {
      Serial.printf("[c3] wifi: boot: in range \"%s\" -> try\n", list[j].ssid);
      startConnect(list[j].ssid, list[j].pass);
    } else {
      Serial.println("[c3] wifi: boot: no saved net in range -> stay offline");
    }
    radioOff();                       // 没连上就关射频(csBusy 时内部跳过)
  }
}

static void cmd(const char* args) {           // 'w' 之后的子命令串
  while (*args == ' ') args++;
  char op[8] = "", a1[40] = "", a2[72] = "";
  int  nscan = sscanf(args, "%7s %39s %71[^\n]", op, a1, a2);
  const char* p2 = (nscan >= 3) ? a2 : "";
  if      (!strcmp(op, "list"))                 listCmd();
  else if (!strcmp(op, "add") && nscan >= 2)    startConnect(a1, p2);   // ★ 连上才存
  else if (!strcmp(op, "seed") && nscan >= 2)   seedNet(a1, p2);        // ★ v0.8.7:只写不连
  else if (!strcmp(op, "del"))                  del(atoi(a1));
  else if (!strcmp(op, "conn"))                 connIdx(a1[0] ? atoi(a1) : 1);
  else if (!strcmp(op, "try"))                  tryAll(a1[0] ? atoi(a1) : cnt);   // ★ v0.8.7
  else Serial.println("[c3] wifi: usage wadd <ssid> <pass> | w seed <ssid> <pass> | w try [n] | wlist | wdel <n> | wconn [n]");
}

// 给开机自检/主界面用:当前 WiFi 状态文案(buf >= 44 字节)
static void statusText(char* buf, size_t n) {
  if (WiFi.status() == WL_CONNECTED) { snprintf(buf, n, "OK %s", WiFi.SSID().c_str()); return; }
  if (csBusy)                        { snprintf(buf, n, "CONNECTING...");                return; }
  if (!cnt)                          { snprintf(buf, n, "NONE SAVED");                  return; }
  snprintf(buf, n, "IDLE (%d saved)", cnt);
}

inline int  count() { return cnt; }
// 给 C3 本机菜单(WIFI / C3 / LIST)读列表用 —— 只读,不改行为
inline const char* ssidAt(int i)  { return (i >= 0 && i < cnt) ? list[i].ssid : ""; }
inline bool        hasKeyAt(int i){ return (i >= 0 && i < cnt) && list[i].pass[0] != '\0'; }
// ★ v0.7:给 PUSH 用 —— 推给 S3 要的是**密码原文**,不是 "(key)" 这种提示串
inline const char* passAt(int i)  { return (i >= 0 && i < cnt) ? list[i].pass : ""; }
// UI 侧调用的入口(键盘 SAVE / 菜单 CONNECT)
inline void addNet(const char* ssid, const char* pass) { startConnect(ssid, pass); }
inline void delNet(int n) { del(n); }
inline bool takeEvent(char* out, size_t n) { return evtNext(out, n); }

// ★ v0.9(用户 2026-10-14 定):给**扫描页**翻名单用 —— "这条我手里有没有它的密码"。
//   命中返回下标(≥0),没命中 -1。用户要的就是"扫一下哪些 SSID 是我知道密码能进去的"。
inline int  indexOf(const char* s) { return findSsid(s); }
// ★ v0.9(O1):这条是不是"上次连失败过、密码可能已作废"的。
inline bool isStale(const char* s) {
  for (int i = 0; i < staleN; ++i) if (!strcmp(staleSs[i], s)) return true;
  return false;
}
// ★ v0.9(O3):UI 取"请弹连接失败告警"(取走即清)。返回 false = 没有待弹的。
inline bool takeAlert(char* ssid, size_t ns, char* tag, size_t nt) {
  if (!alertReq) return false;
  alertReq = false;
  snprintf(ssid, ns, "%s", alertSs);
  snprintf(tag,  nt, "%s", alertTag);
  return true;
}
// ★ v0.8:扫描屏要自己用射频时,先把"开机选网"那轮挂起的扫描作废 ——
//   两边都在驱动同一份扫描结果数组,不作废会互相抢(一个 scanDelete 把另一个的
//   结果清掉,scanComplete() 拿到的是对方的数)。
inline void abortScan() { scWaiting = false; scBoot = false; scN = 0; }
inline void idleRadioForScan() { radioIdleForScan(); }

} // namespace wifi10

// ============================================================================
//  s3link:与 S3 台架的串口链路
//  (★ v0.10 C3 侧定型 20/21;★ v1.0.0 起 **S3 侧也回到官方 U0 那对 44/43**)
// ----------------------------------------------------------------------------
//  v0.5 / P3:从"只判联通"升级为**真收发**。
//  ★ C3 侧必须重映射到 20/21:UART1 默认脚是 GPIO6/7,那两根在 C3 是屏的 SCLK/MOSI。
//  ★ S3 侧 = 排针上那对 RX/TX(GPIO44=U0RXD / GPIO43=U0TXD)走 **UART2** 重映射
//    (S3 的 UART1 归 GNSS 模块、UART0 归 ROM 日志,见 GNSS_Bench.ino 的 benchlink 段头),
//    与它自己的 USB CDC(GPIO19/20)不冲突 —— 两端可以同时插在电脑上。
//    ★ v1.0.0 起 S3 侧回到这对**官方脚**(旧判"CP2102N 顶着 GPIO44、做不了"已推翻,
//      真因是面包板孔松,用户已改焊线)。
//  协议(**两个方向都带前缀**,这是防"悬空 RX 噪声误触发命令"的关键):
//      C3 -> S3 :  "@k <payload>\n"    payload = 单键("s"/"1")或整行("wadd ssid pw")
//      S3 -> C3 :  "@s ok <payload>\r\n"
//      ★ S3 只认带 "@k " 前缀的行,所以线上噪声凑不出合法命令,
//        不会出现"噪声里蹦出个 'c' 把台架基准清了"这种事故。
//  ★ 名字不能叫 link —— POSIX unistd.h 里有 int link(const char*, const char*),
//    同名会撞成 "'link' is not a class, namespace, or enumeration"。
// ============================================================================
// ★ 链路原始字节取证开关:临时置 1,把收到的**前 256 个原始字节**以十六进制打到串口,
//   用来判断"S3 那边推过来的是不是人话"(2026-10-11 22:3x 排查 S3->C3 方向用)。
//   正式固件保持 0。取证完把这里改回 0 再编一次即可。
//   ★ v0.9.1(2026-10-14 22:3x):**临时置 1**,配合"回 20/21"做自环判据 ——
//     把 TX 线的 S3 端拔下来插回 C3 自己的 RX 孔,发一条命令看能不能读回自己的字节:
//     能读到 ⇒ 线通 + C3 的 TX 吐字节正常(矛头转向 S3 侧);读不到 ⇒ 换线再测。
//   ★★ v0.10(2026-10-15,任务单 §B5):**已改回 0**。两条理由:
//     ① 正式固件不该把原始 HEX 往 USB 串口刷;
//     ② 与下面的**心跳**(每 2 秒一条回执)**直接冲突** —— 开着它串口日志会被 HEX 刷爆。
#define LINK_RAW_DUMP 0

// ============================================================================
//  ★ v1.1.0(2026-10-16):链路**协议版本号** —— 与 S3 的同名常量**必须一致**。
// ----------------------------------------------------------------------------
//  · 它和固件版本号(kVer,两板各自两轨、每版都变)**是两件事**:协议号只在
//    **报文格式/语义**变了才 +1(即 SemVer 里 MAJOR 那一档),所以"这两块板能不能
//    对话"只能由它回答,kVer 答不了。本轮之前的无版本号时代记为 **P0**。
//  · 用法:C3 的心跳带上它(`@k ping P1`),S3 收到带号的 ping 后**回一条自己的**
//    (`@s ok proto P1`);两边不一致就屏上点名 —— 免得又落回"版本不匹配"与
//    "线接错了"长得一模一样那个坑(现场最费时间的一类故障)。
//  · ⚠ **为什么定义在这里、而不是 splash 里 kVer 旁边**:`s3link::poll()` 从 844 行
//    就开始了,比 splash(1044)早 —— 放在后面这里根本看不见它。S3 侧同理。
//  · ⚠ 改这个数 = **两板必须同批烧**(见 docs/配套表-双板烧录.md)。
//    校验器:`python tools/check_proto_sync.py`(比对两板 kProto 的值)。
constexpr uint8_t kProto = 2;       // P2(2026-10-17:两板删 MIRROR、新增 evtlist/evtdet/evtstat)

namespace s3link {

// 链路引脚(★ v0.10 C3 侧定型 20/21;★ v1.0.0 起 **S3 侧也回到官方 U0 那对**)
//   —— 这里是**唯一一处** C3 侧引脚定义,不再有 A/B 双方案宏(旧宏 LINK_PINS_89 已删)。
//      两端现在都是各自芯片的**官方 UART0 脚**,配对:
//        C3 GPIO20(RX)  ←  S3 GPIO43(U0TXD,TX)      (白线)
//        C3 GPIO21(TX)  →  S3 GPIO44(U0RXD,RX)      (棕线)
//        GND            ↔  GND                       (黑线,共地;夹在上面两根中间)
//      ★ 线的颜色按 docs/接线表-双板与彩色杜邦线配色.md §四(排线色序 v2):
//        白/黑/棕 必须从排线上**当一束**取(黑的两邻居就是白和棕),把黑夹中间屏蔽串扰。
//   ★ 为什么是 20/21:C3 原生 UART0 的 `U0RXD`/`U0TXD`,引脚图上标的就是 RX/TX。
//   ★ S3 侧 43/44:v1.5.23(2026-10-15 20:5x)换回 —— 当年"CP2102N 顶着 GPIO44、
//     做不了"的结论已推翻,真因是**面包板孔松**(用户已改焊线)。
//   ⚠ 接线必须跟着改(本项目踩过:"定义改了、杜邦线还插在旧孔上")。
//   ⚠ **改了引脚定义就要同步**改 docs/接线表-双板与彩色杜邦线配色.md。
constexpr int      PIN_RX   = 20;        // ← 接 S3 的 TX(GPIO43)
constexpr int      PIN_TX   = 21;        // → 接 S3 的 RX(GPIO44)
constexpr uint32_t kBaud    = 115200;    // ★ v0.7:状态行直接引用它,不再手写字面量
constexpr uint32_t kOkWinMs = 5000;      // ★ v0.7:多久没收到合法回执就算"链路断了"
// ★ v0.11(2026-10-15)删:这里原来还有一个 `kPollMs = 3000`("自检里等待首次链路的窗口")。
//   它把**开机自检**变成了"3 秒定生死":窗口一到就写死 `NO LINK (3s)` —— 即使 3 秒后
//   链路马上就通了,那行**也不会自己变**。C3 先上电、S3 后上电时必现 ⇒ 开机画面上
//   留一个**假**的 NO LINK,现场看着心慌。
//   现在自检改成"**一直重试 + 画出来的行会自我修正**"(用户 2026-10-15 拍板),
//   "窗口"这个概念本身已作废 ⇒ 常量删掉。自检用的两个节拍改放 splash 里
//   (见 `splash::kPingMs` / `splash::kUartMs`)。
constexpr int      kLineMax = 176;       // 最长上行:"wadd " + 32(ssid) + 1 + 64(pass)
// ★ v0.8.7:6 -> 14。S3 的 `wlist` 一次最多回 10 行(v1.5.20 的 rptText),
//   而 S3 自己的 loop 比 C3 快得多 —— 它会在几十毫秒里把 10 行全推过来,
//   C3 的 s3link::poll() 一轮就能抽到好几行,全部 pushLine 进来。
//   旧容量 6 会把表头那几行顶掉(屏上看到的台账缺前几条)。14 = 10 行台账 + 余量。
//   每行 kLineMax(176) B ⇒ 14×176 = 2.5 KB,C3 的堆(约 97 KB 空闲)完全放得下。
constexpr int      kQMax    = 14;        // 收到的行:小队列,给 UI 慢慢取

// ★★ v0.8.1 修(2026-10-11 22:4x,**抓原始字节抓出来的**):
//   `strlen("@s ok ")` = **6**,不是 7!
//   上一版(v0.7.1)`strncmp(acc, "@s ok ", 7)` 写成了 7 —— `strncmp` 比第 7 个字符时
//   拿的是字面量的结尾 '\0' 去和正文的第一个字符比,两者必然不等,**于是除了
//   "正文为空"以外,所有回执一律判不等 -> 被 continue 丢掉**。
//   现象就是:S3 明明一条条推了 `@s ok wsc ...`,C3 这边一条都进不来,
//   屏上只有 12 s 超时的 "no AP found";连最普通的 `@s ok s` 都不显示成 [s3] 行。
//   (用 LINK_RAW_DUMP 打出来是干净的 `40 73 20 6F 6B 20 73 0D 0A` = "@s ok s" ——
//    字节没问题,是判据写错了。)
constexpr int      kRxPfxLen = 6;        // = strlen("@s ok ")

// ★ 2026-10-15:`seen` 与 `everOk()` 一并删除 —— 它们是"曾经通过就永远 ok"那套锁存的
//   残留。v0.7 改成真·实时后,`lastOkMs != 0` 已经等价于"开机以来收到过合法回执"
//   (它只被赋值、从不归零),再留一个 seen 就是第二份账,哪天对不上反而误导。
static uint32_t lastOkMs = 0;            // 最后一条合法回执的时刻(millis),ok() 用它
static uint32_t nBytes = 0;
static uint32_t nSent  = 0;
static uint32_t nRxLines = 0;            // ★ v0.8:收到几条**合法回执**(口径与 nBytes 不同)

// ---- ★ v0.7:回执"分流钩子" ----
// 扫描屏要把 S3 回的 wsc/wsce 行**截下来填列表**,而不是丢进输出卡滚屏。
// 用函数指针做最轻的依赖倒置:钩子返回 true = 这行已被吃掉,不要再进输出卡。
// 扫描屏不在时钩子为 nullptr,一行代码的额外开销。
typedef bool (*LineSink)(const char* payload);
static LineSink sink = nullptr;
inline void setSink(LineSink f) { sink = f; }

// ---- 接收侧:行累积 + 小队列(S3 的回执不丢,但也不让它堆起来) ----
static char rxq[kQMax][kLineMax];
static int  qHead = 0, qN = 0;
static char acc[kLineMax];
static int  accN = 0;

static void pushLine(const char* s) {
  if (qN == kQMax) return;                       // 队列满就丢:屏上也来不及显示
  // ★★ v0.10 修(2026-10-15,任务单 A【P0】):这一行的 `snprintf` **原先是写在上一行的
  //   行尾注释里的** —— 被 `//` 一直吞到行尾 ⇒ 本函数只做了 `++qN`,**从来没往 rxq 里写过东西**。
  //   后果链:popLine() 返回 static 数组的初值(全 0 = 空串) ⇒ drainLink() 里
  //   `strncmp(l,"wifi ok ",8)` 等判据**全部不匹配** ⇒ 落到 else 分支 ⇒ 屏上只剩空的
  //   `[S3] OK `;`txt fail <原因>` 同样变空 ⇒ 整屏 CONNECT FAILED 告警**从来不弹**。
  //   (wscan 的 wsc/wsce/wscf 走 sink 钩子、不经过 rxq ⇒ "扫描能填列表、普通回执看不到"
  //    这个组合正好指向这里。)—— 千万别再把这个语句挪回注释行尾。
  snprintf(rxq[(qHead + qN) % kQMax], kLineMax, "%s", s);
  ++qN;
}

// 取一行(UI 每圈调一次)。返回 false = 队列空。
inline bool popLine(char* out, size_t n) {
  if (qN == 0) return false;
  snprintf(out, n, "%s", rxq[qHead]);
  qHead = (qHead + 1) % kQMax;
  --qN;
  return true;
}

inline void begin() {
  // ★ v0.8:S3 的扫描结果是一口气推过来的(12 条 wsc ≈ 500 B,115200 下 43 ms 发完),
  //   而本机 loop 一圈只抽 64 B —— 抽得比来得慢,RX 环形缓冲(默认 256 B)会溢出,
  //   行被截断 -> sscanf 解不出 -> 屏上 12 s 超时、一个 AP 都填不进去。
  //   (2026-10-11 22:2x 实测:S3 侧日志明明 "reporting 12 / scan: done",C3 侧一条没到。)
  //   两手一起上:缓冲调到 1 KB + 每圈最多抽 512 B。setRxBufferSize 必须在 begin 之前。
  // ★ v0.8.5(2026-10-12):1 KB -> 2 KB,并补 TX 缓冲。社区已定案的丢字节根因是
  //   "UART RX 中断(优先级1)被 WiFi/BT 的 ISR(优先级3)抢占 ⇒ 硬件 FIFO(128 B)溢出",
  //   丢包集中在 WiFi beacon 时刻。S3 侧(v1.5.18)同步做同一处改动。
  Serial1.setRxBufferSize(2048);
  Serial1.setTxBufferSize(1024);
  Serial1.begin(kBaud, SERIAL_8N1, PIN_RX, PIN_TX);
  Serial.printf("[c3] link: UART1 RX=%d TX=%d @%lu (to S3 bench)\n",
                PIN_RX, PIN_TX, (unsigned long)kBaud);
  // ★ v1.1.0:开机自报协议号 —— 排障时(尤其"是不是烧错版本了")一眼就能看到,
  //   不用去翻源码或比对固件号(固件号两轨不可比,协议号才回答"能不能对话")。
  Serial.printf("[c3] proto P%d\n", kProto);
}

// 上行:统一加 "@k " 前缀(S3 只认带前缀的行 -> 悬空脚噪声不会误触发命令)
inline void sendCmd(const char* payload) {
  Serial1.print("@k ");
  Serial1.print(payload);
  Serial1.print('\n');
  ++nSent;
  Serial.printf("[c3] link tx #%lu: %s\n", (unsigned long)nSent, payload);
}

// ★ v1.1.0:心跳统一走这里 —— **只此一处**拼协议号(拼成 "ping P1")。
//   为什么收敛成一个函数:这份 payload 有 **3 个**发送点(开机自检 2 处 + 主循环 1 处),
//   手抄三遍就是"必须与别处一致"的典型 —— 漏一处 = 那一处发的还是老格式,而现象只是
//   "某段时间里握手不生效",极难定位(本项目已为同类错误付过两次学费:白名单漏 'd'、
//   心跳掉进 else 刷屏)。⇒ 一处拼、三处调。
inline void sendPing() {
  char b[16];
  snprintf(b, sizeof b, "ping P%d", kProto);
  sendCmd(b);
}

// ★ v1.1.0:协议号握手的状态(解析在 poll() 里,告警由 UI 层取走)
//   · `peerProto`    = 对端(S3)最近报的协议号,0 = 还没收到过
//   · `peerProtoBad` = **当前**是否处于"不一致"状态(专门用来做边沿判定)
//   · `protoWarnQ`   = 一次性告警:被 takeProtoWarn() 取走即清零 ⇒ 只在"由一致变
//                      不一致"那一拍往输出卡推一次,不每 2 秒刷屏(照抄断链告警的做法)。
//   ⚠ 定义必须放在 `poll()` **之前**(第一版就写错在这:c 语言是按位置找名字的,
//     放到 poll 后面直接 "'peerProto' was not declared in this scope" 编译失败)。
//   ⚠ 为什么"状态"放 s3link、"上屏"留给 UI 层:`ui::outPush()` 定义在 1449 行,
//     比 s3link 晚 —— 命名空间里根本调不到(也不给它加前置声明:与 wifi10 的
//     queueEvt/evtNext 是同一个理由,那份注释就在 298 行)。
static int  peerProto    = 0;
static bool peerProtoBad = false;
static bool protoWarnQ   = false;

inline int  peerProtoNo()   { return peerProto; }
inline bool takeProtoWarn() { const bool t = protoWarnQ; protoWarnQ = false; return t; }

inline void poll() {
  // ★ v0.8:单轮上限 64 -> 512。64 是"抽得比 S3 发得慢"(见 begin 里的说明),
  //   512 仍是个有界的数 —— 宁可下一圈接着读,也不让主循环被锁住。
  for (int guard = 0; guard < 512 && Serial1.available(); ++guard) {
    const char c = (char)Serial1.read();
    ++nBytes;                                  // 原始计数:收到多少算多少(含噪声)
#if LINK_RAW_DUMP
    if (nBytes <= 256) {
      Serial.printf("%02X ", (uint8_t)c);
      if ((nBytes % 32) == 0) Serial.println();
    }
#endif
    if (c == '\n' || c == '\r') {
      if (accN == 0) continue;
      acc[accN] = 0;
      accN = 0;
      // ★ v0.6 硬化:只认带 "@s ok " 前缀的行,其余一律丢弃 —— 正好对称于
      //   S3 侧"只认 @k "。
      //   起因:S3 每次复位,ROM bootloader 会在 GPIO43 上吐一段启动日志,
      //   那串乱码原本会以 "[s3] 乱码" 的形式糊在输出卡上,还让 ok() 误判成"链路通"。
      //   现在只有**合法回执**才会走下面那两行 —— 开机自检那行才可信。
      //
      // ★★ v0.7.1 修(2026-10-11 上机抓出来的):原来只剥 "@s " **三个**字符,
      //   于是 pl 恒带着 "ok "(pl = "ok wsc 1 10 -45 WPA2 xxx"),而 scLineSink()
      //   比的是 "wscan" / "wsce" / "wsc " ⇒ **三处判据全部落空** ⇒
      //   远端扫描结果一条也填不进列表 → scBusy 永真 → 12s 超时 → 屏上 "no AP found"。
      //   (同一个原因也让所有回执带着 "ok " 上屏,就是现场看到的 "[s3] ok wconn"。)
  //   S3 下行**恒为** "@s ok <payload>"(benchlink 两处 printf 都是这个格式,
  //   且 C3 文件头记的协议也是它),所以这里剥掉这 6 个字符 —— **是 6 不是 7**,
  //   见上面 kRxPfxLen 那段(v0.7.1 写成 7,把所有回执都干掉了)。
      if (strncmp(acc, "@s ok ", kRxPfxLen) != 0) continue;
      lastOkMs = millis();                     // ★ v0.7:真·实时的 ok() 靠这一行
      const char* pl = acc + kRxPfxLen;
      ++nRxLines;                              // 收到第几条合法回执(日志用,便于数链路)
      if (sink && sink(pl)) continue;          // ★ v0.7.1:现在真的能截到 wsc/wsce 了
      // ★ v1.1.0:协议号握手回执(`@s ok proto P1`)—— 不上屏、不进输出卡。
      //   它和心跳一样是**每 2 秒一条**的周期性报文,上屏必刷屏(与下面那一支同理)。
      //   位置:`lastOkMs = millis()` 已在上面执行过 ⇒ 这里的 continue 不影响 ok()
      //   —— 它同时也是一次心跳,顺带刷新链路存活。
      //   边沿判定放在这里(而不是 UI 层):只有这一层能看到"每一条"回执;
      //   UI 层拿到的应当是"状态",不该由它去数边沿。
      if (!strncmp(pl, "proto P", 7)) {                // ⚠ 7 = strlen("proto P")
        const int their = atoi(pl + 7);                // "P1" -> 1
        const bool bad  = (their != kProto);
        if (bad && !peerProtoBad) protoWarnQ = true;   // 只在"由一致变不一致"那一拍排队
        peerProtoBad = bad;                            // 恢复一致时自动重新武装
        peerProto    = their;
        continue;
      }
      // ★ v0.10(任务单 §C3):**心跳回执(`@s ok ping`)不上屏、也不打日志** ——
      //   它每 2 秒来一次,上屏会把输出卡(14 行)整片占满,真回执反而看不见;
      //   打日志会把串口刷成 ping 流水。
      //   ⚠ 必须放在 `lastOkMs = millis()` **之后**:那一行已经执行过,所以这里的
      //     `continue` 不影响 `ok()` —— 心跳的全部价值(刷新链路存活)**已经拿到了**。
      //   ★ v1.1.0:判据由 `strcmp`(完全相等)改**前缀匹配** —— 心跳现在带协议号,
      //     正文是 `ping P1`,判等会落空 ⇒ 这一行失效 ⇒ 每 2 秒往输出卡推一行
      //     `[s3] ping P1`,把 14 行的卡片刷爆、真回执全被挤掉。
      if (!strncmp(pl, "ping", 4)) continue;           // "ping" 与 "ping P1" 都不上屏
      // ★ v1.2.0(2026-10-17):事件日志搬运帧(evth/evts/evtr/evtd/evtg)与命令回显
      //   (evtlist/evtdet/evtstat)**都不上输出卡** —— 它们的唯一消费者是「S3 LOG」页
      //   (开着时走上面的 sink 截走);页没开时到达的(极少:退出瞬间还有几帧在路上)
      //   一律丢,防 14 行输出卡被刷爆。与 ping 过滤同款理由。
      Serial.printf("[c3] link rx #%lu: %s\n", (unsigned long)nRxLines, pl);
      pushLine(pl);                            // 去掉前缀再上屏
    } else if (accN < kLineMax - 1) {
      acc[accN++] = c;
      // ★ v0.8:同上,**前缀对齐**(同一个坑的另一个方向 —— S3 复位时它自己的
      //   U0TXD 也会吐 ROM 日志,S3 那边已经先加了同样的重同步)。
      if (accN >= kRxPfxLen && strncmp(acc, "@s ok ", kRxPfxLen) != 0) {
        memmove(acc, acc + accN - (kRxPfxLen - 1), kRxPfxLen - 1);
        accN = kRxPfxLen - 1;
      }
    } else {
      accN = 0;                                // 超长且迟迟不见换行 -> 清空重新同步
    }
  }
}

// ★ v0.7:ok() 由"曾经通过就永远 ok"的锁存改成**真·实时** ——
//   5 秒内收到过合法回执才算通。旧行为会骗人:S3 拔线、断电、跑飞,
//   屏上照样显示 ok,而操作员正拿着它当"链路正常"的依据。
inline bool ok()            { return lastOkMs != 0 && (millis() - lastOkMs) < kOkWinMs; }
inline uint32_t baud()      { return kBaud; }
inline uint32_t rxBytes()   { return nBytes; }
inline uint32_t txCount()   { return nSent; }

} // namespace s3link

// ============================================================================
//  开机画面 + 自检(v0.2 起,规格逐条对齐 S3 splash)
// ----------------------------------------------------------------------------
//  · 黑底;两行文字第一帧整行显示(无从左到右动画):
//      主标题 "Command Pad"   2 号字,绿色,居中 x=54 y=90(与 S3 主标题同规格);
//      副标题 "MANFACE STUDIO" 1 号字,MANFACE 7 字母彩虹、STUDIO 绿,同行同字号,
//                             x=78 y=132(与 S3 副标题坐标/字号完全一致);
//      右下角版本号,灰。
//  · 自检 4 行(项名银色 x=30,结果彩色 x=84,行距 20,起 y=176 —— 逐项照抄 S3):
//      ① TFT    0.5s  -> "OK 240x320"
//      ② TOUCH  1.5s  -> "OK (cal)" / "NO CAL"
//      ③ UART   ★ v0.11:"一直重试 + 会自我修正" —— 2 s 时落笔,通了画 `S3 ok (n ms)`,
//                没通画 `NO LINK (retrying)` 并**继续 ping**;之后任何一帧通了就当场把这行
//                改写成 `S3 ok (n ms)`。(v0.2~v0.10 是"最多等 3 s,到点写死 NO LINK")
//      ④ WIFI   5s 处 -> wifi10::statusText
//  · 总长 5s + 定格 2s;期间串口任意输入 / 触摸按下可跳过
//    (跳过用 tft.getTouch 直读,不走 touch::poll,免得误触页面切换)。
//  · 退出铺黑底(等价于"动画没发生过"),主界面随后整屏重画。
// ============================================================================
namespace splash {

constexpr const char* kTitle = "Command Pad";
constexpr const char* kMain  = "MANFACE";
constexpr const char* kSub2  = "STUDIO";
// ★ v1.0.0(2026-10-15 20:5x):**两段 -> 三段**,与 S3 的 `vX.Y.Z` 格式统一;
//   1.x = 语义上的**正式版**(0.x 是开发中)。节点是用户定的"C3 可以发布了"。
// ★ v1.0.2(2026-10-15 22:5x,用户:"其实我要的是都不闪"):修 menuDraw 的重绘判据 ——
//   纯**绘制层**改动(打开菜单不再整块重绘、层间变矮只补露出来的那条带),
//   链路协议与屏上版式**一个像素没动**,所以**不要求与 S3 同批烧**。
// ★ v1.1.0(2026-10-16):MINOR +1 —— **向下兼容地新增**"协议号握手"
//   (心跳带号 + proto 回执 + 不匹配时屏上点名;老 S3 收到带号 ping 只是判据落空,
//   链路功能不受影响 ⇒ 不是 MAJOR)。同批还改了 **一级菜单重排**(SIM 从一级移走)
//   ⇒ 与 S3 **v1.6.0 必须同批烧**(配套表 ID `2026-10-16+`)。
// ★ v1.1.1(2026-10-16 20:47 用户拍板):**删掉 SIM 模拟信源菜单**(+ S3 侧同名代码)。
//   同时修两处本轮新引入的毛病:① boot 横幅里的版本号写死(改引用 splash::kVer);
//   ② dbgMenu() 入参不夹紧(方案 A 把一级减到 4 组后,`z 0 4 0 0` 会越界读数组外内存,
//   实测冒出 `S3 / JUDGE` 这个不存在的菜单项)。⇒ 与 S3 **v1.7.0 同批烧**。
// ★ v1.2.0(2026-10-17 上午,MINOR —— 加了整页功能,协议号升 P2):
//   ① 菜单:VIEW/MIRROR 删除;SYSTEM 幻影行修复(nkids 5->4,用户看到的"SYSTEM 里
//      的第二个 MIRROR"就是它 —— v1.1.1 删 SIM 忘改 nkids,菜单多读一行相邻内存)。
//   ② 新增**「S3 LOG」搬运页**:VIEW/LOG 一键双动作(发 'v' 让 S3 切日志页 +
//      本机整屏镜像 S3 的第三/四界面 —— 10 行列表(行色随类型,同 S3)+ PREV/NEXT
//      整页翻 + 底栏统计 + 点行进详情(BACK 回列表,详情也有 PREV/NEXT)。
//      数据走 S3 新增的 evtlist/evtdet/evtstat 链路命令,帧 evth/evts/evtr/evtd/evtg。
// ★ v1.2.1(2026-10-17 上午,PATCH —— 纯显示层一行修,协议/链路/版式零改动):
//   用户报:进「S3 LOG」搬运页后,C3 自己的 WiFi 图标还在原地每秒补画、压在搬运页
//   顶栏右上角。根因 = ui tick 的"自己有顶栏的页面不刷图标"名单(v0.9 立的纪律)
//   漏了新加的 MODE_EVT —— 与当年扫描页漏网是同一类错。修法:名单补 MODE_EVT。
//   (进入搬运页时顶栏整宽铺底已把图标擦掉;退出走 enterBoard()->drawTopBar() 自动恢复,
//   故只需挡住 tick 这一个画家。S3 不受影响,不需同批烧。)
//   ② 主界面底部三格键 **SEND -> RESEND**(标签字面与行为对齐 —— 它本来就重发上一条,
//   逻辑零改动,只改 kKeyLabel 与相关提示行/注释的字面;Font2 现算居中,6 字符不溢出 72px 键宽)。
// ★ v1.2.2(2026-10-17 上午,PATCH —— 搬运页显示工艺,协议/链路零改动):
//   用户报:搬运页列表/详情"所有显示重画的过程都发生闪动(包括显出 log list 界面),
//   看起来非常糟糕;没有用 sprite 吗?"。根因两条:① v1.2.0 直画 tft 无原子性
//   (fillScreen -> 逐件画,中间态全可见);② evtlist 一批 12 帧(evth+evts+10xevtr),
//   每收 1 帧 evtDirty -> 整页重画一次 = 开一页连闪 12 次。
//   修法:① 重画走 kEvBnd 五个**内容对齐分带 sprite**(带边界全落在文字行之间,
//   单带 <=43.2KB;全屏 sprite 153.6KB 在无 PSRAM+WiFi 开着时放不下 —— 这正是
//   v1.2.0 不用 sprite 的原因),每带整带一次 push 再释放,分配失败降级直画;
//   ② **批次收敛**(最后一帧 60ms 后)才重画一次(evtPoll + evtLastRx)。
//   另:evtd 解析加串口打印(核对详情大字;S3 同批把 evtd 的 <col> 改成详情页
//   同款取色 —— v1.2.0 发的是行色(闭环=绿),S3 详情页大字却是状态色 ⇒ 色差)。
// ★ v1.2.3(2026-10-17 上午,PATCH —— 只跟**编译配置**有关,源码里一行绘制逻辑没动):
//   用户第二次报同一件事:「C3 进 log data 界面后,左上角少了事件类型显示」。
//   真因**不在源码**:C3 编译带 -DC3_CMDPAD ⇒ TFT_eSPI 走 `User_Setups/Setup_C3_CmdPad.h`,
//   而那个文件**只开了 LOAD_GLCD / LOAD_FONT2,没有 LOAD_FONT4**。详情页那行大字是
//   `eFont(4)`(与 S3 第 4 界面同款 26px)→ 库里 `case 4:` 没编进去 ⇒ drawString
//   不报错、不崩溃、**一个像素都不画** ⇒ 那块区域全空。S3 走根目录 User_Setup.h
//   (有 LOAD_FONT4)所以只有 C3 缺。上一版我按"颜色发错了"去调取色,方向错了 ——
//   字根本没落地,谈什么颜色。
//   修法:① 库配置补 `#define LOAD_FONT4`(两板字体表对齐,屏上才能"是什么就是什么");
//   ② 源码顶部加 `#ifndef LOAD_FONT4 #error` **编译期哨兵** —— 换机器/重装库漏改时
//   当场编译不过,而不是又一次静默丢字。
// ★ v1.2.4(2026-10-17,PATCH —— 菜单删一项;绘制逻辑一行没动):
//   用户定:"我们现在不需要演示了,所以我想 C3 S3 两边都删除 inject 功能"。
//   ① JUDGE 子菜单 INJECT 整项删除(3 -> 2);`kGroups` 里的 nkids 同步 3 -> 2 ——
//      ⚠ 那个 nkids 是**手写常量**,忘改会读到数组外内存(v1.2.0 的"幻影行"就是这么来的)。
//   ② S3 侧白名单同步删 'i' ⇒ 可发单键 9 -> 8(`r b c d v s g t`)。
//   ③ C3 本身没有注入实现(它只是发命令的那一端),其余源码不用动。
// ★ v1.2.5(2026-10-17 中午,PATCH —— 只改**一级菜单第一组的显示名**,其余零改动):
//   用户:"C3 菜单第一条还是原来那个样子,是 JUDGE……我们不做演示了,我觉得 JUDGE
//   应该写成 BASE"。⇒ `kGroups[0].label` 由 "JUDGE" 改为 **"BASE"**。
//   为什么合理:这一组下面就是 SET BASE / CLEAR BASE,组名叫 BASE 才对得上;而
//   "JUDGE(判据)"是评测语境的说法,自用阶段没有再出现的必要。
//   ⚠ 内部数组名 `kLv2_JUDGE` **保留不改**(纯符号,不上屏,改名只增 diff 不长功能)。
// ★ v1.2.6(2026-10-22 晚,板载 RGB 呼吸灯 —— C3 换 DevKit 后 GPIO8 从"电平 LED"变"WS2812"):
//   用户:"把它做成呼吸灯效果 青蓝色,用它来表示 UART 通信的节拍";断链行为拍板"变红"。
//   ① 驱动改 `neopixelWrite()`(esp32 核心 3.3.8 内置,RMT 外设)—— **零新库依赖**,
//     不走 Adafruit_NeoPixel;② 链路活 = 青蓝 RGB(0,160,255) 正弦呼吸,周期 2 s
//     (与 `@k ping` 心跳同拍,接替当年 SuperMini 2s 明灭的角色);链路断 = 红常亮;
//   ③ 旧「2 s 电平翻转」**删除**:WS2812 对普通高低电平不响应(这就是换板后灯从不亮的
//     原因),且 digitalWrite 与 RMT 输出在同一个脚上打架;setup() 里 pinMode/digitalWrite(8)
//     一并撤;④ 引脚 = GPIO8 是官方 DevKitM-1 口径(接线图 J3-6「IO8 板载 RGB」佐证)。
constexpr const char* kVer   = "v1.2.6";

constexpr uint32_t kExpandMs = 5000;    // 展开时间下限(与 S3 同)
constexpr uint32_t kHoldMs   = 2000;    // 定格(与 S3 同)

// ★ v0.11(2026-10-15,用户拍板):自检期间的链路节拍。
//   `kPingMs` —— 主动 ping 的节奏。进自检**立刻**发第一条,之后每 500 ms 补一条,
//     **一直补到自检画面结束为止**(不再"3 秒收手")。为什么要一直补:C3 先上电、
//     S3 后上电是很常见的次序,单发必失败,只有持续重试才能在画面还在的时候握上手。
//   `kUartMs` —— "③ UART"这一行**落笔**的时刻。给链路 2 秒探测时间,这样正常链路上
//     这行一出来就是绿的 `S3 ok`,不会先闪一下橙色的 retrying。
constexpr uint32_t kPingMs = 500;
constexpr uint32_t kUartMs = 2000;

constexpr uint16_t kRainbow[7] = {
  TFT_RED, TFT_ORANGE, TFT_YELLOW, TFT_GREEN, TFT_CYAN, TFT_BLUE, TFT_MAGENTA
};

// 主标题 2 号字(12x16):"Command Pad" 11 字符共 132px,x=(240-132)/2=54
constexpr int kTitleX = 54, kTitleY = 90;
// 副标题 1 号字(6x8):14 字符共 84px,x=78,y=132(与 S3 一致)
constexpr int kMainX  = 78, kMainY  = 132, kStep = 6;
// 自检 4 行
constexpr int kChkX = 30, kChkY = 176, kChkLH = 20, kChkRight = 54;
// 版本号右下角:★ v0.10 修 —— **按实际字宽右对齐**。
//   原来是 `kVerX = 213`,注释写着"4 字符 24px,x=237-24=213" —— 也就是说 x 是**按
//   版本号只有 4 个字符算死的**。版本号一长(`v0.9.1` = 6 字符 = 36px)就变成
//   213+36 = 249 > SCR_W(240) ⇒ **右边被裁掉几个字符**(用户 2026-10-15 报的缺陷)。
//   现改为每帧现算:1 号字 6px/字符 ⇒ x = kVerRight - strlen(kVer)*6,右边界恒 237。
//   ⚠ 以后版本号再长也不会溢出(除非超过 39 个字符)。
constexpr int kVerRight = 237, kVerY = 306;
inline int verX() { return kVerRight - (int)strlen(kVer) * 6; }

static void drawStudio() {                    // MANFACE 彩虹 + STUDIO 绿,同一行
  char s[2] = { 0, 0 };
  tft.setTextSize(1);
  for (int i = 0; i < 7; ++i) {
    s[0] = kMain[i];
    tft.setTextColor(kRainbow[i], TFT_BLACK);
    tft.drawString(s, kMainX + i * kStep, kMainY);
  }
  for (int i = 0; i < 6; ++i) {
    s[0] = kSub2[i];
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.drawString(s, kMainX + (8 + i) * kStep, kMainY);
  }
}

static void line(int row, const char* name, const char* val, uint16_t col) {
  tft.setTextSize(1);
  tft.setTextColor(TFT_SILVER, TFT_BLACK);
  tft.drawString(name, kChkX, kChkY + row * kChkLH);
  tft.setTextColor(col, TFT_BLACK);
  tft.drawString(val, kChkX + kChkRight, kChkY + row * kChkLH);
}

// ③ UART 行的文案(★ v0.11)。两种形态:
//   通了  -> `S3 ok (n ms)` —— 括号里是**从自检开始到拿到回执**的耗时。为什么要报这个数:
//            它把两种"通"分开了 ——
//              · 十几毫秒 ⇒ S3 本来就在跑,链路一直是好的;
//              · 好几秒   ⇒ S3 是**刚上电**的(C3 先上电的次序),这行是从 retrying 翻过来的。
//   没通  -> `NO LINK (retrying)` —— ★ 不再写 `(3s)`:现在**没有**"3 秒窗口"这回事了,
//            自检期间会一直重试,"还在试"才是实话。
//   长度核对:最长 `S3 ok (7000 ms)` = 16 字符 = 96px,从 x=84 画到 180 < 236,不溢出。
static void uartLine(bool ok, uint32_t ms) {
  char b[24];
  if (ok) snprintf(b, sizeof(b), "S3 ok (%lu ms)", (unsigned long)ms);
  else    snprintf(b, sizeof(b), "NO LINK (retrying)");
  line(2, "UART", b, ok ? TFT_GREEN : TFT_ORANGE);
}

inline void run() {
  tft.fillScreen(TFT_BLACK);

  tft.setTextSize(2);
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.drawString(kTitle, kTitleX, kTitleY);

  drawStudio();

  tft.setTextSize(1);
  tft.setTextColor(kGrey, TFT_BLACK);
  tft.drawString(kVer, verX(), kVerY);            // ★ v0.10:右对齐,见 verX() 说明

  const uint32_t t0 = millis();
  int  drawn = 0;       // 已打出的自检行数 0..4
  bool uOk = false;     // 拿到过 S3 的合法回执(拿到即**永久为真**,不会再变回假)
  int  uShown = 0;      // ③ 行现在屏上的样子:0=还没画 1=NO LINK(retrying) 2=S3 ok
  uint32_t uOkMs = 0;   // 拿到回执时的自检耗时(给 ③ 行显示)
  uint32_t tPing = millis();
  char buf[48];

  // ★ v0.11:进自检**立刻**打第一条 ping(不等 kPingMs)——
  //   这样 kUartMs(2 s)时"③ UART"落笔,正常链路必定已握手完成,不会先闪一下 NO LINK。
  // ★ v1.1.0:改成 sendPing() —— 自检期的心跳也**必须带协议号**,否则开机那几秒
  //   发的还是老格式(S3 侧仍能识别,但协议号握手要等主循环才开始)。
  s3link::sendPing();

  while (true) {
    const uint32_t el = millis() - t0;

    // ---- UART 自检(★ v0.11:"一直重试 + 自我修正") ----
    //   历史沿革:
    //     v0.2~v0.9 —— "被动等首个字节,最多 3 s"。可开机时**没有任何一方会先开口**
    //       (C3 不主动发、S3 也不主动发)⇒ 必然显示 `NO LINK` —— 纯假阴性(任务单 §C1 ②)。
    //     v0.10    —— "每 500 ms 主动 ping,3 s 到点就收手"。假阴性修掉了,但**留下了
    //       "3 秒定生死"**:窗口一到就写死 `NO LINK (3s)`,之后哪怕链路马上通了,
    //       那一行**也不会自己变**。C3 先上电、S3 后上电时必现 ⇒ 开机画面上一个假红灯。
    //     v0.11    —— 把最后这点也去掉(用户 2026-10-15 拍板):
    //       ① **一直 ping 到自检画面结束**,不再 3 s 收手;
    //       ② ③ 行**画出来之后还会自我修正** —— 落笔时没通写 `NO LINK (retrying)`,
    //          之后任何一帧拿到回执就**当场改写**成 `S3 ok (n ms)`。
    //   ⚠ `poll()` 必须**每帧无条件**跑(它同时负责把回执从 UART 抽出来),不能只在
    //     "还没通"时跑 —— 否则会漏字节。
    //   ★ 心跳回执不上屏(见 poll() 里的 `if (!strncmp(pl,"ping",4)) continue`;
    //     v1.1.0 起判据是前缀匹配 —— 心跳正文带协议号,是 "ping P1"),
    //     所以屏上始终是干净的 4 行自检。
    s3link::poll();
    if (!uOk && s3link::ok()) {
      uOk   = true;
      uOkMs = millis() - t0;              // 握手耗时(从自检开始那一刻算)
    }
    if (!uOk && millis() - tPing >= kPingMs) {
      tPing = millis();
      s3link::sendPing();                          // ★ v1.1.0:带协议号(见 sendPing)
    }

    if (drawn == 0 && el >= 500) {                  // ① TFT
      line(0, "TFT", "OK 240x320", TFT_GREEN);
      drawn = 1;
    }
    if (drawn == 1 && el >= 1500) {                 // ② TOUCH
      if (touch::calibrated) line(1, "TOUCH", "OK (cal)", TFT_GREEN);
      else                   line(1, "TOUCH", "NO CAL",   TFT_ORANGE);
      drawn = 2;
    }
    if (drawn == 2 && el >= kUartMs) {              // ③ UART
      uartLine(uOk, uOkMs);
      uShown = uOk ? 2 : 1;
      drawn = 3;
    }
    // ★ v0.11 自我修正:③ 行画的时候还没通,之后通了就**当场改写这一行**
    //   (其余三行自检一律不动)。用户 2026-10-15 要的正是"别在开机画面上留一个假 NO LINK"。
    //   只在 `uShown == 1`(屏上还是 retrying)时才重画,已绿的不会反复刷。
    if (uShown == 1 && uOk) {
      uartLine(true, uOkMs);
      uShown = 2;
    }
    if (drawn == 3 && el >= kExpandMs) {            // ④ WIFI
      wifi10::statusText(buf, sizeof(buf));
      uint16_t col = kGrey;
      if (!strncmp(buf, "OK ", 3))       col = TFT_GREEN;
      else if (!strncmp(buf, "FAIL", 4)) col = TFT_RED;
      // ★ v0.10:自检结果从 x=84(30+54)起画,右边界到 ~236 ⇒ **25 字符封顶**。
      //   statusText 会给 `OK <ssid>`,SSID 长时会溢出屏幕右侧(与输出卡截断同一类问题),
      //   这里按宽度截一刀。仅影响开机这 5 秒的自检行,不影响菜单/回执。
      char w[26];
      snprintf(w, sizeof(w), "%.25s", buf);
      line(3, "WIFI", w, col);
      drawn = 4;
    }

    if (drawn == 4 && el >= kExpandMs + kHoldMs) break;
    if (Serial.available()) break;
    uint16_t tx, ty;
    if (tft.getTouch(&tx, &ty, 600)) break;

    delay(20);
  }

  tft.fillScreen(TFT_BLACK);   // 退出补底:主界面随后整屏重画,不留残影
}

} // namespace splash

// ============================================================================
//  ui:深色界面(命令板主屏 + 校准屏 + 菜单弹出)
// ----------------------------------------------------------------------------
//  · 命令板主屏 MODE_BOARD(A 方案,用户审定):
//      顶栏  COMMAND PAD + 右上角 WiFi 符号
//      输出卡 OUTPUT :命令回显 14 行,环形缓存(满了整体前移,最旧顶掉)
//      状态行        :LINK ok/-- · 115200 · t=cal
//      底部三格键    :MENU(绿) / RESEND / CLR,各 72x64(y=252..316)
//  · 菜单:点 MENU -> 全宽卡从屏底滑上来,底边贴屏底;**三级**结构。
//      一级 4 组(BASE/VIEW/SYSTEM/WIFI;v1.2.5 前第一组叫 JUDGE)
//      二级 SYSTEM 的 TOUCH  -> 三级 S3 TOUCH / C3 TOUCH
//      二级 WIFI 的 S3 / C3   -> 三级 LIST / ADD / DEL / CONNECT
//      卡片高度按项数自适应(5 项 132px,2 项 72px);头部面包屑,点头部 = 回上一级。
//      选中态 = 左 3px 青条 + 稍亮行底;点菜单外空白 = 全收起。
//      ★ 渲染进 240x132 sprite -> 6 参 pushSprite 只推卡片那块逐帧上移(8 帧)。
//      ★ sprite 分配失败时自动降级:直接画屏上,无动画(功能不减)。
//  · 关闭菜单 / 换层不读屏(readRect 实测不可用)-> repaintUnderMenu() 按内部状态重绘
//    (含一次整带铺底,顺手消掉卡片色的残留竖条)。
//  · 虚拟键盘 MODE_KB(v0.5):WIFI 的 ADD / DEL 用,全屏接管,详见下面那一节。
//  · 校准屏 MODE_CAL —— 用户定稿保留,布局/字号/配色一个像素不动
//    (v0.5 只在 CAL 卡里多了个 BOARD 返回按钮;这是用户点名要的)。
//  · 串口 't' 在 命令板 <-> 校准屏 之间切换(键盘开着时先收键盘)。
// ============================================================================
namespace ui {

// ★ v0.9:新增 MODE_LIST(名单页,给 DEL 选条目用)与 MODE_ALERT(整屏弹窗)。
enum Mode : uint8_t { MODE_BOARD = 0, MODE_CAL = 1, MODE_KB = 2, MODE_SCAN = 3,
                      MODE_LIST = 4, MODE_ALERT = 5, MODE_EVT = 6 };

static Mode     mode = MODE_BOARD;
static int      lastX = -1, lastY = -1;
static uint32_t lastTick = 0;
static uint16_t rawX = 0, rawY = 0, rawZ = 0;   // 未校准原始值

// ---------------------------------------------------------------- 命令板几何
constexpr int kCardX = 8,  kCardY = 32, kCardW = 224, kCardH = 196;  // 输出卡 32..227
constexpr int kOutY  = 52, kOutLH = 12;                             // 行起点/行高
constexpr int kOutRows = 14, kOutCols = 34;                         // 14 行 x 34 字符

constexpr int kStatY = 234;                                         // 状态行
constexpr int kKeyY  = 252, kKeyH = 64, kKeyW = 72, kKeyGap = 4;    // 三格键 252..316
constexpr int kKeyX[3] = { 8, 84, 160 };

// ------------------------------------------------------------------ 菜单几何
constexpr int kMenuHeadH = 26;                  // 头部条(标题/面包屑)
constexpr int kMenuRowH  = 28;                  // ★ v0.8.4(A 方案):40 -> 28;实际填充 26,留 2px 间隙
constexpr int kMenuPad   = 2;                   // 头部与首行之间
constexpr int kMenuTail  = 4;                   // 末行下方留白
constexpr int kMenuMaxN  = 6;                   // 菜单每层上限(kMenuHMax 按它算 sprite 高度)
// 卡片高 = 头 + 间隙 + 行*n + 留白 -> 6 项 272(最高),2 项 112(最矮);底边恒贴屏底。
// ★ v0.8.4(A 方案):行高 40 -> 28 -> 卡高 6 项 200 / 2 项 88(v0.8.3 是 272/112)。
constexpr int kMenuHMax  = kMenuHeadH + kMenuPad + kMenuMaxN * kMenuRowH + kMenuTail;
static inline int menuCardH(int n) { return kMenuHeadH + kMenuPad + n * kMenuRowH + kMenuTail; }
static inline int menuTopY(int n)  { return SCR_H - menuCardH(n); }

// ------------------------------------------------------------------ 菜单树(三级)
//  kind: IK_CMD  = 发给 S3 的键/整行(arg = 发过去的内容;v0.5 起**真发**,见 s3link)
//        IK_LOCAL= C3 本机动作(arg = 动作名)
//        IK_SUB  = 还有下一级(kids / nkids)
//        IK_KEYS = 要键盘输入(arg = "add" / "del";v0.5 起点它直接开键盘)
enum ItemKind : uint8_t { IK_CMD = 0, IK_LOCAL = 1, IK_SUB = 2, IK_KEYS = 3 };

struct Node {
  const char*  label;
  const char*  arg;
  ItemKind     kind;
  const Node*  kids;
  uint8_t      nkids;
};

// ---- 三级节点 ----
static const Node kTouchKids[2] = {                     // SYSTEM / TOUCH
  { "S3 TOUCH", "r",   IK_CMD,   nullptr, 0 },          // 发 'r' 重置 **S3** 的触摸校准
  { "C3 TOUCH", "cal", IK_LOCAL, nullptr, 0 },          // 跳本机校准屏
};
// ★★ v0.9(用户 2026-10-14 定):WIFI 这两个子菜单**砍掉 LIST 和 CONNECT**。
//   · CONNECT 删 —— 它只会闷头去连名单里的 #1(connIdx(1)),用户"这个没用,删掉"。
//     串口上的 `wconn [n]` 保留(调试用,零成本),只是菜单里不再出现。
//   · LIST 删 —— 用户原话:"专门开个页面把存过的热点列出来看**没有意义**,
//     因为既然存过,那肯定是连上去过,而且知道密码的";"我肯定是用 scan 去扫一下
//     有哪个 SSID 并且是我知道密码能进去的"。
//     ⇒ 名单不再单独"看";要看"我有没有密码"直接体现在**扫描列表的标记**上(见 scDraw)。
//   · DEL 保留但改成 IK_LOCAL —— 打开的名单页**自带选择界面**,点一条删一条
//     (LIST 没了以后,"删第几条"的数字没处可看,所以 DEL 自己带列表)。
static const Node kWifiS3Kids[4] = {                    // WIFI / S3(真发给 S3)
  { "SCAN", "s3scan", IK_LOCAL, nullptr, 0 },           // 让 S3 自己扫,结果回 C3 显示
  { "ADD",  "add",    IK_KEYS,  nullptr, 0 },           // 手敲 SSID+密码(隐藏热点兜底)
  { "DEL",  "del",    IK_LOCAL, nullptr, 0 },           // 打开名单页,点一条删一条
  { "PUSH", "push",   IK_LOCAL, nullptr, 0 },           // 把 C3 整份名单抄给 S3
};
static const Node kWifiC3Kids[3] = {                    // WIFI / C3(**真执行**,本机)
  { "SCAN", "c3scan", IK_LOCAL, nullptr, 0 },           // 用 C3 自己的射频扫
  { "ADD",  "add",    IK_KEYS,  nullptr, 0 },
  { "DEL",  "del",    IK_LOCAL, nullptr, 0 },
};

// ---- 二级节点 ----
// ★ v1.2.4(2026-10-17,用户定):**INJECT 项删除**(3 -> 2)—— 不再做演示,
//   注入功能两板一起删干净(S3 侧同步删了 `i` 命令、判据里的偏移坐标、第 2 键的
//   INJECT 态;白名单 9 -> 8)。保留 SET BASE / CLEAR BASE 两项:
//   S3 屏上第 2 键现在也是"长按 5 s 清基准",这条菜单与屏上按键是**同一件事的两个入口**。
// ★ v1.2.5(2026-10-17,用户定):**组名 JUDGE -> BASE** —— 不做演示了,"判据"这个
//   评测语境的说法退役;这一组底下就是 SET BASE / CLEAR BASE,叫 BASE 才对得上。
//   数组符号名 `kLv2_JUDGE` 保留(不上屏,改名收益为零)。
static const Node kLv2_JUDGE[2] = {
  { "SET BASE",   "b", IK_CMD, nullptr, 0 },
  { "CLEAR BASE", "c", IK_CMD, nullptr, 0 },
};
// ★ v1.2.0(2026-10-17,用户定):**MIRROR 整项删除** —— S3 的 @MIR/@MSG 从来就被
//   C3 丢掉(s3link 只认 "@s ok " 前缀),PC 演示面板也已放弃 ⇒ 两板一起删干净
//   (S3 白名单同步去 'm',10 -> 9;跑 python tools/check_link_whitelist.py)。
//   LOG 保留并**升级**:发 'v' 让 S3 切日志页 + 同时打开本机「S3 LOG」搬运页(见 evtOpen)。
static const Node kLv2_VIEW[2] = {
  { "CHARTS", "d", IK_CMD, nullptr, 0 },
  { "LOG",    "v", IK_CMD, nullptr, 0 },
};
// ★ v1.1.0(方案 A):SIM 从一级降级到这一级。
// ★★ v1.1.1(用户 2026-10-16 20:47 拍板):**SIM 整项删除,不再降级**。
//   理由:项目未进复赛、转自用,"造异常演示"的需求消失 ⇒ 模拟信源在**两边一起清掉**
//   (C3 这条菜单 + S3 的 `namespace sim` 与 0/1/2/3 四个键)。详见 S3 侧同名注释。
//   ⚠ 连带:`kLv2_SIM` 数组已删;S3 白名单里的 `"0123"` 也删 ⇒ **两板白名单要同步**,
//     改完必须跑 `python tools/check_link_whitelist.py`(这类"必须与别处一致"的表
//     一律脚本化校验,禁止手抄 —— 白名单漏字符是静默失灵,踩过两次)。
static const Node kLv2_SYSTEM[4] = {
  { "STATUS",  "s",     IK_CMD, nullptr,     0 },
  { "PPS",     "g",     IK_CMD, nullptr,     0 },
  { "TEMP/RH", "t",     IK_CMD, nullptr,     0 },
  { "TOUCH",   nullptr, IK_SUB, kTouchKids,  2 },
};
static const Node kLv2_WIFI[2] = {
  { "S3", nullptr, IK_SUB, kWifiS3Kids, 4 },    // ★ v0.9:6 -> 4(砍掉 LIST / CONNECT)
  { "C3", nullptr, IK_SUB, kWifiC3Kids, 3 },    // ★ v0.9:5 -> 3(砍掉 LIST / CONNECT)
};

// ---- 一级(组) ----
// ★ v1.1.0(方案 A):**5 组 -> 4 组**,SIM 从一级移走。v1.1.1 起 SIM 已整项删除。
//   排序原则 = "日常最常按的靠前":基准(BASE)是主用,VIEW 次之,SYSTEM/WIFI 是设置类。
//   ★ v1.2.5:第一组显示名 JUDGE -> **BASE**(内部数组仍叫 kLv2_JUDGE)。
//   ⚠⚠ `kGroupN` 是**手写常量**(不是 sizeof 推的)—— 增删组**必须同步改它**,
//      否则 levelList(0) 会按旧值多读一项,读到数组外的内存(菜单显示乱码 / 崩)。
//      ⇒ 这就是"必须与别处一致、又靠手抄"的典型,改完请跑
//        `python tools/check_link_whitelist.py`(它同时会把菜单表解析一遍,表被改坏会报)。
static const Node kGroups[4] = {
  { "BASE",   nullptr, IK_SUB, kLv2_JUDGE,  2 },   // ★ v1.2.5:组名 JUDGE -> BASE(用户定)
                                                    //   v1.2.4:kids 3 -> 2(INJECT 删除)
  { "VIEW",   nullptr, IK_SUB, kLv2_VIEW,   2 },   // ★ v1.2.0:3 -> 2(MIRROR 删除)
  { "SYSTEM", nullptr, IK_SUB, kLv2_SYSTEM, 4 },   // ★★ v1.2.0 修:5 -> 4(幻影行)
                                                   //   v1.1.1 删 SIM 时数组减到 4 项、这里忘了改,
                                                   //   菜单多读一行相邻内存 —— 用户看到
                                                   //   "SYSTEM 里多出一个 MIRROR" 就是它(点到真执行)。
  { "WIFI",   nullptr, IK_SUB, kLv2_WIFI,   2 },
};
static constexpr int kGroupN = 4;

// ------------------------------------------------------------------ 运行状态
static bool menuOpen = false;
static int  menuLevel = 0;                       // 0=一级 1=二级 2=三级
static int  menuSel   = 0;                       // 当前层选中行
static int  menuPath[2] = { 0, 0 };              // 下钻时记录的上级选中项
static int  menuCoveredH = 0;                    // 屏上已被菜单卡盖住的高度(0=没开)
static TFT_eSprite* menuSpr = nullptr;
static uint32_t menuBusyUntil = 0;               // 动画期间忽略点击

// ★ v0.8.3(B 方案):把菜单 sprite 真还回堆(★ v0.8.4:240x200 = 96,000 B)。
//   三个"菜单没了"的入口都走它:closeMenu() / drawBoardScreen() / drawCalScreen()。
//   ⚠ 只在菜单**真的关掉**时调;换层(下钻/返回)时 sprite 必须留着。
static void menuFreeSprite(const char* why) {
  if (!menuSpr) return;
  menuSpr->deleteSprite();
  delete menuSpr;
  menuSpr = nullptr;
  Serial.printf("[c3] ui: menu sprite freed (%s, free heap %u)\n",
                why, (unsigned)ESP.getFreeHeap());
}

// 取第 level 层的列表(0=一级)。返回 nullptr 表示路径越界。
static const Node* levelList(int level, int* n) {
  const Node* l = kGroups;
  int c = kGroupN;
  for (int i = 0; i < level; ++i) {
    if (!l || menuPath[i] < 0 || menuPath[i] >= c) { *n = 0; return nullptr; }
    const Node& nd = l[menuPath[i]];
    l = nd.kids;
    c = nd.nkids;
  }
  *n = c;
  return l;
}

// 面包屑:一级 "MENU";二级 "< SYSTEM";三级 "< SYSTEM / TOUCH"
static void menuCrumb(char* b, size_t n) {
  if (menuLevel <= 0)       { snprintf(b, n, "MENU"); return; }
  if (menuLevel == 1)       { snprintf(b, n, "< %s", kGroups[menuPath[0]].label); return; }
  snprintf(b, n, "< %s / %s", kGroups[menuPath[0]].label,
           kGroups[menuPath[0]].kids[menuPath[1]].label);
}

// 串口/回显用的完整路径:二级 "BASE / SET BASE";三级 "SYSTEM / TOUCH / C3 TOUCH"
// (v1.1.0 前 SIM 是一级、示例写的是 "SIM / NORMAL";v1.1.1 起 SIM 已整项删除,
//  故三级示例改用 TOUCH —— 它是现在唯一还带第三层的项)
static void menuTrail(char* b, size_t n) {
  int nn = 0;
  const Node* l = levelList(menuLevel, &nn);
  const char* leaf = (l && menuSel >= 0 && menuSel < nn) ? l[menuSel].label : "?";
  if (menuLevel <= 1) { snprintf(b, n, "%s / %s", kGroups[menuPath[0]].label, leaf); return; }
  snprintf(b, n, "%s / %s / %s", kGroups[menuPath[0]].label,
           kGroups[menuPath[0]].kids[menuPath[1]].label, leaf);
}

// 输出卡行缓存(环形:满了整体前移 -> 最旧顶掉,与 wifi10 同一约定)
static char     outBuf[kOutRows][kOutCols + 1];
static uint16_t outCol[kOutRows];
static int      outN = 0;
// ★★ v1.0.3:输出卡的"脏行游标" —— 值 = 第一行需要重绘的行号,`kOutRows` = 没有脏行。
//   outPush() 只改这里,真正落笔交给 drawOutDirty()。见它的说明。
static int      outDirtyFrom = kOutRows;
static char     lastPayload[96] = "";     // RESEND 键:记住最近一条发往 S3 的命令,按一下重发

// ============================================================================
//  基础绘制件
// ============================================================================
static void drawWifiIcon(uint16_t bg) {
  uint16_t color = (WiFi.status() == WL_CONNECTED) ? TFT_CYAN : TFT_DARKGREY;
  const int16_t cx = ICON_X + 8;
  const int16_t cy = ICON_Y + ICON_H - 1;
  const int16_t radii[4] = { 1, 5, 9, 13 };
  for (int i = 0; i < 4; ++i) {
    const int16_t r = radii[i], ir = r - 1;             // 厚 1px
    tft.drawArc(cx, cy, r, ir, 135, 225, color, bg);
  }
}

static void drawTopBar() {
  tft.fillRect(0, 0, SCR_W, BAR_H, CLR_BAR);
  tft.setTextSize(1);
  tft.setTextColor(CLR_FG, CLR_BAR);
  tft.drawString("COMMAND PAD", 6, 8);
  drawWifiIcon(CLR_BAR);
}

static void card(int x, int y, int w, int h) {
  tft.fillRoundRect(x, y, w, h, 6, CLR_BAR);
}

// ============================================================================
//  命令板主屏
// ============================================================================
static void outPush(const char* s, uint16_t col) {
  if (outN == kOutRows) {                            // FIFO 前移,最旧顶掉
    for (int i = 0; i < kOutRows - 1; ++i) {
      memcpy(outBuf[i], outBuf[i + 1], kOutCols + 1);
      outCol[i] = outCol[i + 1];
    }
    outN = kOutRows - 1;
    outDirtyFrom = 0;                                // ★ v1.0.3:整体上移 ⇒ 14 行全变了
  } else if (outN < outDirtyFrom) {
    outDirtyFrom = outN;                             // ★ v1.0.3:还没满 ⇒ 只有新添这一行变
  }
  snprintf(outBuf[outN], kOutCols + 1, "%s", s);
  outCol[outN] = col;
  ++outN;
}

// ★★ v0.10(用户 2026-10-15 报的缺陷):输出卡只有 **34 列 × 14 行**,而带 SSID 的
//   **操作回显**(`> WIFI / S3 / ADD <ssid>`)会超宽被截断 —— SSID 最长 32 字符,
//   加上前缀"WIFI / S3 / ADD "必然撞线。
//   ⇒ 统一规矩:**凡是"操作 + SSID"的回显,一律拆成两行**
//        第一行 = 操作(面包屑等)     第二行 = SSID **原文独占一行**
//     这样 32 字符的 SSID 也能完整显示,而且输入完能一眼核对自己敲的 SSID 对不对。
//   ★ 只管**操作回显**;S3/C3 回来的**回执**不用管 —— 那两侧的 SSID 出厂就截到 18 字符。
static void outPush2(const char* head, const char* tail) {
  outPush(head, CLR_ACC);      // 第一行:操作(青色)
  outPush(tail, CLR_FG);       // 第二行:SSID 原文(正常前景色)
}

static void drawOutputCard() {
  card(kCardX, kCardY, kCardW, kCardH);
  tft.setTextSize(1);
  tft.setTextColor(CLR_ACC, CLR_BAR);
  tft.drawString("OUTPUT", kCardX + 10, kCardY + 6);
  for (int i = 0; i < kOutRows; ++i) {
    const int ly = kOutY + i * kOutLH;
    tft.fillRect(kCardX + 10, ly, kCardW - 20, 8, CLR_BAR);
    if (i < outN) {
      tft.setTextColor(outCol[i], CLR_BAR);
      tft.drawString(outBuf[i], kCardX + 10, ly);
    }
  }
  // ⚠ 这里**故意不**清 outDirtyFrom —— 本函数也会被 repaintUnderMenuBand() 和
  //   menuSlideOut() 的逐帧 **viewport 裁剪**调用,那时只有落在带内的那几行真被写到屏上;
  //   顺手把脏标记清掉,带外的脏行就**永远补不上了**(菜单开着时进来的回执/告警会丢)。
  //   脏标记一律由 drawOutDirty() 自己在末尾清,另外 closeMenu() 收尾会补画一次。
}

// ★★ v1.0.3(用户 2026-10-15 报的缺陷:"点了菜单中的命令,命令行和 ACK 行那部分会闪"):
//   输出卡以前是**整卡重绘** —— card() 那一个 224x196 的 fillRoundRect 就是
//   43,904 px;再加 14 行 x (204x8) 的 fillRect = 22,848 px,合计 66,752 px
//   ≈ 133 KB ≈ **53 ms @20MHz**。而**点一下菜单命令**会连着触发两三次
//   (命令回显 / S3 的 ACK 行 / WiFi 事件各一次)⇒ 每点一下,输出卡整块黑一下再
//   画回来,这就是那个闪。
//   ⇒ 改成**增量**:outPush() 只标"从第几行开始脏",这里只重绘那几行。
//      · 常态(未满 14 行)= 只画 1 行 = 204x8 = 1,632 px ≈ **0.7 ms**(省掉 98.7% 的像素)
//      · 满了 14 行后整体上移 = 14 行 ≈ 9 ms(仍比原来省掉 card() 那 35 ms 的大头)
//   ⚠ 只画**行**,不碰卡片底色和 "OUTPUT" 标题 —— 它们没变,没必要重画。
//     所以"整屏重绘"和"补带"那几条路径仍然调全量的 drawOutputCard()。
//   ⚠ 它**不清** outDirtyFrom 之外的任何状态;重复调用是幂等的(第二次直接 return)。
static void drawOutDirty() {
  if (outDirtyFrom >= kOutRows) return;              // 没有脏行,一个字节都不写
  tft.setTextSize(1);
  for (int i = outDirtyFrom; i < kOutRows; ++i) {
    const int ly = kOutY + i * kOutLH;
    tft.fillRect(kCardX + 10, ly, kCardW - 20, 8, CLR_BAR);
    if (i < outN) {
      tft.setTextColor(outCol[i], CLR_BAR);
      tft.drawString(outBuf[i], kCardX + 10, ly);
    }
  }
  outDirtyFrom = kOutRows;
}

// ★ v0.7 改版(用户 2026-10-11 定):
//   ① 删掉 `t=cal` —— 那是格式串里的**纯静态文字**,代码里没有任何东西在算它,
//      挂在状态行上纯属占位,没有任何信息量。
//   ② `115200` 不再写死字面量,改成引用 s3link::baud()(改波特率不会再对不上)。
//   ③ `LINK ok` 由"曾经通过就永远 ok"的锁存改成**真·实时**(5 秒窗口,见 s3link::ok)。
//   ④ 腾出来的地方显示**本机 IP** —— 现场要 telnet/抓包时,没有 IP 就只能干瞪眼。
//      屏幕宽 240:最长的行 "LINK ok | 115200 | 255.255.255.255" 也才 34 字符 = 204px。
static void drawStatusLine() {
  tft.fillRect(0, kStatY, SCR_W, 14, CLR_BG);
  char b[48], ip[20];
  if (WiFi.status() == WL_CONNECTED) {
    const IPAddress a = WiFi.localIP();
    snprintf(ip, sizeof(ip), "%u.%u.%u.%u", a[0], a[1], a[2], a[3]);
  } else {
    snprintf(ip, sizeof(ip), "--");
  }
  snprintf(b, sizeof(b), "LINK %s | %lu | %s",
           s3link::ok() ? "ok" : "--", (unsigned long)s3link::baud(), ip);
  tft.setTextSize(1);
  tft.setTextColor(CLR_DIM, CLR_BG);
  tft.drawString(b, 8, kStatY + 3);
}

static const char* const kKeyLabel[3] = { "MENU", "RESEND", "CLR" };   // ★ v1.2.1:SEND -> RESEND(字面与行为对齐:它本来就重发上一条)

static void drawKeys() {
  for (int i = 0; i < 3; ++i) {
    const uint16_t bg = (i == 0) ? CLR_ACC : CLR_BAR;    // MENU 用绿色突出
    const uint16_t fg = (i == 0) ? CLR_BG  : CLR_FG;
    tft.fillRoundRect(kKeyX[i], kKeyY, kKeyW, kKeyH, 6, bg);
    tft.setTextFont(2);                                  // ★ v0.8.4:内置 Font2 比例字
    tft.setTextColor(fg, bg);
    const int lw = tft.textWidth(kKeyLabel[i]);          // ★ v0.8.4:比例字体现算宽度
    tft.drawString(kKeyLabel[i], kKeyX[i] + (kKeyW - lw) / 2, kKeyY + 24);
  }
  // ★ v0.8.4:Font2 会"粘"在 tft 对象上 —— 后面用字号 1 的地方(状态行/输出卡)
  //   若不归位就会拿 Font2 去画,所以这里显式还回 GLCD 字号 1。
  tft.setTextFont(1);
  tft.setTextSize(1);
}

// 菜单可能盖住的整块 -> 按内部状态重绘(含整带铺底,消掉卡片色的残留竖条)
static void repaintUnderMenu() {
  tft.fillRect(0, BAR_H, SCR_W, SCR_H - BAR_H, CLR_BG);
  drawOutputCard();
  drawStatusLine();
  drawKeys();
}

// ★ v1.0.2:只重绘 [y0, y1) 这**一条带**里的命令板内容 —— 给"菜单层间切换"补
//   露出来的那一条用(见 menuDraw 的说明)。
//   为什么不直接 `setViewport + repaintUnderMenu()`:
//     repaintUnderMenu() 末尾的 drawKeys() 用的是 fillRoundRect,而它在 TFT_eSPI 里
//     走 fillCircleHelper(TFT_eSPI.cpp:2641)—— **不吃 viewport 裁剪**,
//     会无条件把三个圆角键的圆弧写出去。补带时屏上还压着上一张菜单卡,
//     那几道弧就会**透到卡片上**。所以这里按 y 区间**条件调用**各部件:
//     带没盖到谁,就一个字节都不碰谁。
//   ⚠ 三个条件里的区间必须跟着 kCardY/kCardH、kStatY、kKeyY/kKeyH 一起改。
//   ⚠ **调用方必须让 y1 ≥ kCardY+kCardH**:drawOutputCard() 里的 card() 也是
//      fillRoundRect,它画的**下圆角**在 [kCardY+kCardH-6, kCardY+kCardH) ——
//      只有 y1 盖过 kCardY+kCardH,那两段弧才会整个落在带内,
//      不会越界到带外的旧菜单卡上。(上圆角在 y≈32..38,永远在带外,
//      但那里本来就被旧卡让开了、显示的就是命令板,重画一遍同色无影响。)
static void repaintUnderMenuBand(int y0, int y1) {
  tft.setViewport(0, y0, SCR_W, y1 - y0, false);        // false = 保持绝对坐标,只裁不平移
  tft.fillRect(0, y0, SCR_W, y1 - y0, CLR_BG);          // 铺底(fillRect 吃 viewport)
  if (y1 > kCardY && y0 < kCardY + kCardH) drawOutputCard();
  if (y1 > kStatY && y0 < kStatY + 14)     drawStatusLine();   // 状态行 14 高
  if (y1 > kKeyY  && y0 < kKeyY  + kKeyH)  drawKeys();
  tft.resetViewport();
}

static void drawBoardScreen() {
  mode      = MODE_BOARD;
  menuOpen  = false;
  menuLevel = 0;
  menuSel   = 0;
  menuCoveredH = 0;
  menuFreeSprite("board");                       // ★ v0.8.3:整屏重绘,菜单那块没用了
  lastX = lastY = -1;
  tft.fillScreen(CLR_BG);
  drawTopBar();
  drawOutputCard();
  outDirtyFrom = kOutRows;    // ★ v1.0.3:这里是真·全量(fillScreen 重来),脏标记作废
  drawStatusLine();
  drawKeys();
}

// ============================================================================
//  虚拟键盘 MODE_KB(P2):给 WIFI 的 ADD / DEL 输入文字
// ----------------------------------------------------------------------------
//  240x320 全屏接管,底边贴屏底:
//    顶栏    INPUT + 右侧面包屑(如 "WIFI / C3 / ADD");★ **点头栏 = 取消**
//    字段行  第 1 行 SSID(ADD)或组号(DEL)、第 2 行 PASS(密码用 * 遮);
//            ★ **点字段行 = 切换正在输入哪一格**;当前格左侧 3px 青条 + 亮底
//    键位    5 行,单位宽 24px(10 单位 = 240):
//              R1  1 2 3 4 5 6 7 8 9 0
//              R2  q w e r t y u i o p
//              R3  a s d f g h j k l .
//              R4  SHIFT  z x c v b n m  _
//              R5  BACK   SPACE(4)   SAVE/DEL(3)
//            ★ v0.7:SHIFT 现在出符号 —— R1 `1234567890` -> `!@#$%^&*()`,
//              且 `_` 键 -> `-`。映射表就是 kKbCharsShift,画与敲共用 kbCharAt()。
//  提交: ADD -> C3 本机 wifi10::addNet() / 发给 S3 "@k wadd <ssid> <pass>"
//        DEL -> C3 本机 wifi10::delNet(n) / 发给 S3 "@k wdel <n>"
//  ★ v0.8:ADD 两边都是**"连上才存"**,提示语已改成 "connecting... (saved only if IP ok)"。
//    发给 S3 的 `wadd` 只是"试着连",S3 连上了才存并回 `wifi ok <ip>`(见 drainLink)。
//  ★ SSID/密码里**不能有空格**(S3 用空格分词解析),提交时拦下并提示重填。
//  ★ 调试钩子(串口,不用手指也能把整条逻辑跑通):
//      k add c3 | k del c3 | k add s3 | k del s3   -> 直接打开键盘
//      y <text>                                    -> 模拟敲键,'|' = 切字段,末尾自动按 OK
// ============================================================================
constexpr int kKbHdrH   = 24;                    // 顶栏高(点它 = 取消)
constexpr int kKbTop    = 68;                    // 键区顶
constexpr int kKbKeyH   = 42;                    // 键高
constexpr int kKbKeyGap = 2;                     // 键间隙
constexpr int kKbUnit   = 24;                    // 单位宽
constexpr int kKbRows   = 5;
constexpr int kKbFieldA = 26, kKbFieldB = 40;    // 两行字段文字 y
constexpr int kKbHintY  = 54;                    // 字段下方小字提示 y

enum KbWhat : uint8_t { KB_ADD = 0, KB_DEL = 1 };

static bool   kbOpen  = false;
static bool   kbC3    = true;                    // true = 存 C3 本机;false = 发给 S3
static KbWhat kbWhat  = KB_ADD;
static int    kbField = 0;                       // 0 = SSID/组号;1 = 密码
static bool   kbShift = false;
static char   kbA[33] = "";                      // SSID 或组号
static char   kbB[65] = "";                      // 密码
static char   kbTrail[28] = "";                  // 面包屑

// 4 行字符键。R4 前面要给 SHIFT 留 2 个单位,所以第 4 行的字符从 ux=2 起。
static const char* const kKbChars[4]   = { "1234567890", "qwertyuiop", "asdfghjkl.", "zxcvbnm_" };
static const int         kKbCharsU0[4] = { 0, 0, 0, 2 };

// ★ v0.7:SHIFT 映射表(用户定)。
//   以前 SHIFT 只把 a-z 转大写,数字键按了毫无变化 —— 而 PC/手机键盘的惯例是
//   数字行出符号。WiFi 密码里 `- _ .` 和 `!@#$` 都是高频字符,值得补上。
//   ① 数字行:`1..0` -> `! @ # $ % ^ & * ( )`
//   ② `_` 键 -> `-`(原键盘**根本敲不出 `-`**,这一个键同时解决 `_` 和 `-`)
//   刻意不加独立的 SYM 键:用户 2026-10-11 定"暂时够了,以后有需求再说"。
static const char* const kKbCharsShift[4] = { "!@#$%^&*()", "QWERTYUIOP", "ASDFGHJKL.", "ZXCVBNM-" };

// 取第 row 行第 ux 单位上的字符(shift = 是否处于 SHIFT 态)。
// 键盘**画**和**敲**都必须走这一个函数 —— 两处各写一份,迟早对不上。
static char kbCharAt(int row, int ux) {
  if (row < 0 || row >= 4) return 0;
  const int i = ux - kKbCharsU0[row];
  const char* src = kbShift ? kKbCharsShift[row] : kKbChars[row];
  if (i < 0 || !src[i]) return 0;
  return src[i];
}

static char*  kbBuf()    { return (kbWhat == KB_ADD && kbField == 1) ? kbB : kbA; }
static size_t kbBufMax() { return (kbWhat == KB_ADD && kbField == 1) ? sizeof(kbB) : sizeof(kbA); }

// 值太长时显示**尾部**(光标在末尾,得看得见正在敲什么);超宽加一个前导 '<'
static void kbTail(char* out, size_t n, const char* s, size_t maxch) {
  const size_t L = strlen(s);
  if (L <= maxch) { snprintf(out, n, "%s", s); return; }
  const size_t k = maxch - 1;
  out[0] = '<';
  memcpy(out + 1, s + (L - k), k);
  out[1 + k] = 0;
}

static void kbDrawFields() {
  tft.fillRect(0, kKbFieldA - 2, SCR_W, kKbHintY + 14 - (kKbFieldA - 2), CLR_BG);
  tft.setTextSize(1);

  for (int f = 0; f < 2; ++f) {
    const bool     act = (kbField == f);
    const int      y   = f ? kKbFieldB : kKbFieldA;
    const uint16_t bg  = act ? CLR_SEL : CLR_BG;
    tft.fillRect(0, y - 2, SCR_W, 14, bg);
    if (act) tft.fillRect(0, y - 2, 3, 14, CLR_SELB);
    tft.setTextColor(CLR_DIM, bg);
    tft.drawString(f ? "PASS" : (kbWhat == KB_ADD ? "SSID" : "DEL #"), 8, y);
    tft.setTextColor(CLR_FG, bg);

    char raw[80], out[36];
    if (f == 1) {
      const size_t L = strlen(kbB);
      const size_t k = (L < sizeof(raw) - 1) ? L : sizeof(raw) - 1;
      for (size_t i = 0; i < k; ++i) raw[i] = '*';
      raw[k] = 0;
    } else {
      snprintf(raw, sizeof(raw), "%s", kbA);
    }
    kbTail(out, sizeof(out), raw, 30);        // x=46 起,180px / 6px = 30 字符
    tft.drawString(out, 46, y);
  }

  tft.setTextColor(CLR_DIM, CLR_BG);
  tft.drawString(kbWhat == KB_ADD ? "tap a line to switch field"
                                  : "which entry to delete (1..10)", 8, kKbHintY);
}

static void kbCell(int row, int ux, int uw, const char* label, bool hi) {
  const int x = ux * kKbUnit;
  const int y = kKbTop + row * (kKbKeyH + kKbKeyGap);
  const int w = uw * kKbUnit - kKbKeyGap;
  const uint16_t bg = hi ? CLR_ACC : CLR_BAR;
  tft.fillRoundRect(x, y, w, kKbKeyH, 4, bg);
  tft.setTextSize(1);
  tft.setTextColor(hi ? CLR_BG : CLR_FG, bg);
  const int lw = (int)strlen(label) * 6;
  tft.drawString(label, x + (w - lw) / 2, y + (kKbKeyH - 8) / 2);
}

static void kbDrawKeys() {
  tft.fillRect(0, kKbTop, SCR_W, SCR_H - kKbTop, CLR_BG);
  for (int r = 0; r < 4; ++r) {
    for (int i = 0; kKbChars[r][i]; ++i) {
      char s[2] = { kbCharAt(r, kKbCharsU0[r] + i), 0 };   // ★ v0.7:走同一张 SHIFT 映射表
      kbCell(r, kKbCharsU0[r] + i, 1, s, false);
    }
  }
  kbCell(3, 0, 2, "SHIFT", kbShift);
  kbCell(4, 0, 3, "BACK",  false);
  kbCell(4, 3, 4, "SPACE", false);
  kbCell(4, 7, 3, (kbWhat == KB_ADD) ? "SAVE" : "DEL", true);
}

static void kbDrawHdr() {
  tft.fillRect(0, 0, SCR_W, kKbHdrH, CLR_BAR);
  tft.setTextSize(1);
  tft.setTextColor(CLR_ACC, CLR_BAR);
  tft.drawString("INPUT", 6, 8);
  tft.setTextColor(CLR_DIM, CLR_BAR);
  tft.drawString(kbTrail, SCR_W - 6 - (int)strlen(kbTrail) * 6, 8);
}

// ---- 键动作 ----
static void kbType(char ch) {
  if (kbWhat == KB_DEL) kbField = 0;               // DEL 只有一个输入格
  char* const  b = kbBuf();
  const size_t m = kbBufMax();
  const size_t L = strlen(b);
  if (L + 1 >= m) return;
  b[L] = ch;
  b[L + 1] = 0;
  kbDrawFields();
}

static void kbBack() {
  char* const  b = kbBuf();
  const size_t L = strlen(b);
  if (!L) return;
  b[L - 1] = 0;
  kbDrawFields();
}

inline void enterBoard();                          // 定义在后面,先声明

static void kbCancel() {
  kbOpen = false;
  Serial.println("[c3] ui: keyboard cancelled");
  enterBoard();
}

static void kbSubmit() {
  const bool add = (kbWhat == KB_ADD);
  char b[kOutCols + 1];

  if (!kbA[0]) { outPush("  empty - cancelled", CLR_WARN); kbCancel(); return; }
  if (strchr(kbA, ' ') || (add && strchr(kbB, ' '))) {     // S3 用空格分词,不能带空格
    outPush("  no space allowed - retry", CLR_ERR);
    kbDrawFields();
    return;
  }

  kbOpen = false;
  if (!add) {                                              // ---------- DEL ----------
    const int n = atoi(kbA);
    snprintf(b, sizeof(b), "> %s #%d", kbTrail, n);
    outPush(b, CLR_ACC);
    if (kbC3) { wifi10::delNet(n); outPush("  done (C3)", CLR_DIM); }
    else      {
      snprintf(b, sizeof(b), "wdel %d", n);
      snprintf(lastPayload, sizeof(lastPayload), "%s", b);
      s3link::sendCmd(b);
    }
  } else {                                                 // ---------- ADD ----------
    // ★ v0.10:拆两行 —— `> WIFI / S3 / ADD` + 单独一行 SSID 原文(见 outPush2 说明)
    char h[kOutCols + 1], t[kOutCols + 1];
    snprintf(h, sizeof(h), "> %s", kbTrail);
    snprintf(t, sizeof(t), "  %s", kbA);
    outPush2(h, t);
    if (kbC3) {
      wifi10::addNet(kbA, kbB);
      outPush("  connecting... (saved only if IP ok)", CLR_DIM);
    } else {
      char p[160];
      snprintf(p, sizeof(p), "wadd %s %s", kbA, kbB);
      snprintf(lastPayload, sizeof(lastPayload), "%s", p);
      s3link::sendCmd(p);
    }
  }
  enterBoard();                                            // 重绘会带上刚写入的输出行
}

static void kbDrawScreen() {
  mode = MODE_KB;
  tft.fillScreen(CLR_BG);
  kbDrawHdr();
  kbDrawFields();
  kbDrawKeys();
}

// 打开键盘(c3 = 本机 / 发给 S3;add = ADD / DEL;trail = 面包屑)
inline void enterKb(bool c3, bool add, const char* trail) {
  kbOpen = true; kbC3 = c3; kbWhat = add ? KB_ADD : KB_DEL;
  kbField = 0; kbShift = false;
  kbA[0] = kbB[0] = 0;
  snprintf(kbTrail, sizeof(kbTrail), "%s", trail);
  Serial.printf("[c3] ui: keyboard open -- %s -> %s\n", trail, c3 ? "C3" : "S3");
  kbDrawScreen();
}

// ★ v0.7:带预填的键盘(扫描屏点一行走这条)—— 先正常开键盘,再把 SSID 填进第 1 格,
//   然后把输入焦点**直接放到 PASS 格**。用户要的体验就是"选中热点 -> 只敲密码"。
inline void enterKbPrefill(bool c3, const char* trail, const char* ssid) {
  enterKb(c3, true, trail);                        // ADD 模式;会清空两格并停在 SSID
  snprintf(kbA, sizeof(kbA), "%.32s", ssid);       // SSID ≤32 字符,正好塞满 kbA(33)
  kbField = 1;                                     // 光标跳到 PASS
  kbDrawFields();
  Serial.printf("[c3] ui: keyboard prefill -- \"%s\" (cursor on PASS)\n", kbA);
}

static void kbTap(int x, int y) {
  if (y < kKbHdrH) { kbCancel(); return; }                 // 点头栏 = 取消
  if (y < kKbTop) {                                        // 点字段行 = 切输入格
    kbField = (kbWhat == KB_ADD && y >= kKbFieldB - 2) ? 1 : 0;
    kbDrawFields();
    return;
  }
  const int row = (y - kKbTop) / (kKbKeyH + kKbKeyGap);
  if (row < 0 || row >= kKbRows) return;
  const int u = x / kKbUnit;

  if (row == 3) {                                          // SHIFT + 8 个字母
    if (u < 2) { kbShift = !kbShift; kbDrawKeys(); return; }
    const char c = kbCharAt(3, u);
    if (c) kbType(c);
    return;
  }
  if (row == 4) {                                          // BACK / SPACE / SAVE
    if (u < 3) { kbBack();       return; }
    if (u < 7) { kbType(' ');    return; }
    kbSubmit();
    return;
  }
  const char ch = kbCharAt(row, u);                        // ★ v0.7:与键面同一张表
  if (ch) kbType(ch);
}

// 调试钩子:把一串文字"敲"进键盘('|' = 切字段),末尾自动按 OK
inline void kbFeed(const char* text) {
  if (!kbOpen) { Serial.println("[c3] kb: not open"); return; }
  for (const char* p = text; *p; ++p) {
    if (*p == '|') { kbField = 1; kbDrawFields(); continue; }
    kbType(*p);
  }
  kbSubmit();
}

// ============================================================================
//  扫描屏 MODE_SCAN(★ v0.7 新增)
// ----------------------------------------------------------------------------
//  两个入口,同一块屏、同一套交互:
//    WIFI / C3 / SCAN  -> 用 **C3 自己的射频**扫(异步 WiFi.scanNetworks(true))
//    WIFI / S3 / SCAN  -> 发 "@k wscan" 让 **S3 自己扫**,回执 wsc/wsce 由 sink 截走
//
//  ★ 为什么"两个都要":C3 听到的热点 = **C3 所在位置**能用的热点,S3 听到的 =
//    **S3 所在位置**能用的。两块板现在就挨在一起,结果看起来一样;但真到了
//    "C3 在杆下、S3 在杆上"的场面,两者就是两回事 —— 给 S3 配网必须问 S3 自己。
//
//  版式(240x320 竖屏):
//     顶栏  24px  "SCAN" + 面包屑;★ 点顶栏 = 退出回命令板
//     列表  行高 26,一页 10 行(按信号强度**从强到弱**):
//              [色条] SSID(最多 18 字符)   加密    -42
//     底栏  34px  `<<` / 状态+RESTART / `>>`
//  交互:点某一行 -> 弹键盘,**SSID 预填 + 光标直接落 PASS 格**(用户要的体验)。
//  ★ 一律**异步**:同步 scanNetworks() 会卡 2~5 秒,这个项目最忌讳主循环停住。
// ============================================================================
constexpr int kScHdrH  = 24;
constexpr int kScRowH  = 26;
constexpr int kScRows  = 10;
constexpr int kScTop   = kScHdrH + 2;                    // 26
constexpr int kScBot   = kScTop + kScRows * kScRowH;     // 286
constexpr int kApMax   = 24;                             // 与服务端 kScanMax 一致

// ★ v0.9:链路回执的**统一入口**(定义在名单页那一段之后)。
//   s3link 只认一个 sink,而 v0.9 多了"名单页"也要收 S3 的 wlist 回执,所以中间加一层分发。
static bool uiSink(const char* pl);

static bool     scOpen  = false;     // 是否停在扫描屏
static bool     scRemote= false;     // true = 数据来自 S3
static bool     scBusy  = false;     // 正在扫(本地)或等回执(远端)
static bool     scDone  = false;
static bool     scDirty = false;
static int      scFail  = 0;         // ★ v0.8:0=正常;-2=驱动拒绝(射频忙);-3=超时
static uint32_t scT0    = 0;
static uint32_t scDrawMs= 0;
static int      scN     = 0;
static int      scPage  = 0;
static char     scSs[kApMax][33];
static int      scRssi[kApMax];
static char     scAuth[kApMax][10];
static char     scTrail[28] = "";

static const char* scAuthName(uint8_t a) {
  switch (a) {
    case WIFI_AUTH_OPEN:            return "OPEN";
    case WIFI_AUTH_WEP:             return "WEP";
    case WIFI_AUTH_WPA_PSK:         return "WPA";
    case WIFI_AUTH_WPA2_PSK:        return "WPA2";
    case WIFI_AUTH_WPA_WPA2_PSK:    return "WPA/WPA2";
    case WIFI_AUTH_WPA2_ENTERPRISE: return "ENT";
    case WIFI_AUTH_WPA3_PSK:        return "WPA3";
    case WIFI_AUTH_WPA2_WPA3_PSK:   return "WPA2/3";
    default:                        return "?";
  }
}

static uint16_t scRssiColor(int r) {
  if (r >= -55) return CLR_ACC;
  if (r >= -70) return TFT_YELLOW;
  return CLR_WARN;
}

static int scPages() { return (scN + kScRows - 1) / kScRows; }

// 收一条 AP:按 RSSI 降序插入排序,只留前 kApMax 条。
// 远端(S3)其实已经排好序了,本地扫描是乱序的 —— 统一在这里排,两边行为一致。
static void scAddAp(const char* ssid, int rssi, const char* auth) {
  if (!ssid || !ssid[0]) return;
  int pos = scN;
  if (pos >= kApMax) {
    if (rssi <= scRssi[kApMax - 1]) return;              // 比榜尾还弱:直接丢
    pos = kApMax - 1;
  } else {
    ++scN;
  }
  while (pos > 0 && scRssi[pos - 1] < rssi) {
    scRssi[pos] = scRssi[pos - 1];
    memcpy(scSs[pos],   scSs[pos - 1],   sizeof(scSs[0]));
    memcpy(scAuth[pos], scAuth[pos - 1], sizeof(scAuth[0]));
    --pos;
  }
  scRssi[pos] = rssi;
  snprintf(scSs[pos],   sizeof(scSs[0]),   "%s", ssid);
  snprintf(scAuth[pos], sizeof(scAuth[0]), "%s", auth ? auth : "?");
  scDirty = true;
}

static void scDraw() {
  mode     = MODE_SCAN;
  lastX = lastY = -1;
  tft.fillScreen(CLR_BG);

  // ---- 顶栏(点它 = 退出) ----
  // ★ v0.9(用户 2026-10-14 报):这里**不能再画 WiFi 图标**。
  //   图标占 x≈205~231,而面包屑 "WIFI / S3 / SCAN"(16 字符)右对齐到 x=234
  //   ⇒ 两者重叠约 26px,把 "SCAN" 几个字压花。用户原话:"它跟要显示的那个菜单状态
  //   重叠了"。键盘页(MODE_KB)本来就没有这颗图标,所以那边不重叠 —— 现在扫描页也去掉,
  //   两类"有自己的顶栏"的页面就一致了。tick() 里也要把 MODE_SCAN 一起挡掉,否则每秒
  //   又会把它补画回来(见 tick() 那一行)。
  tft.fillRect(0, 0, SCR_W, kScHdrH, CLR_BAR);
  tft.setTextSize(1);
  tft.setTextColor(CLR_ACC, CLR_BAR);
  tft.drawString("SCAN", 6, 8);
  tft.setTextColor(CLR_DIM, CLR_BAR);
  tft.drawString(scTrail, SCR_W - 6 - (int)strlen(scTrail) * 6, 8);

  // ---- 列表 ----
  const int pages = scPages();
  if (pages && scPage >= pages) scPage = pages - 1;
  if (scPage < 0) scPage = 0;
  char b[48];
  for (int r = 0; r < kScRows; ++r) {
    const int y = kScTop + r * kScRowH;
    tft.fillRect(0, y, SCR_W, kScRowH - 2, CLR_BG);
    const int i = scPage * kScRows + r;
    if (i >= scN) continue;
    tft.fillRoundRect(4, y, SCR_W - 8, kScRowH - 2, 4, CLR_BAR);
    tft.fillRect(4, y, 3, kScRowH - 2, scRssiColor(scRssi[i]));   // 左 3px 信号色条
    // ★★ v0.9(用户 2026-10-14 定,C 方案):**名单里存过密码**的那些行,一眼看得出来。
    //   用户原话:"我肯定是用 scan 去扫一下有哪个 SSID **并且是我知道密码能进去的**"。
    //   做法:SSID 用青色字 + 行尾一个 `*`(名单里没有的 = 白字、无记号)。
    //   ⚠ 记号只能用 ASCII —— 屏上走 TFT_eSPI 的 GLCD 字体,`✓` 那种字符画不出来。
    const bool known = (wifi10::indexOf(scSs[i]) >= 0);
    snprintf(b, sizeof(b), "%.17s", scSs[i]);
    tft.setTextColor(known ? TFT_CYAN : CLR_FG, CLR_BAR);
    tft.drawString(b, 12, y + 9);
    tft.setTextColor(CLR_DIM, CLR_BAR);
    tft.drawString(scAuth[i], 126, y + 9);
    if (known) {                                                 // 行尾记号(在 RSSI 左边)
      tft.setTextColor(TFT_CYAN, CLR_BAR);
      tft.drawString("*", 188, y + 9);
    }
    snprintf(b, sizeof(b), "%d", scRssi[i]);
    tft.setTextColor(scRssiColor(scRssi[i]), CLR_BAR);
    tft.drawString(b, SCR_W - 12 - (int)strlen(b) * 6, y + 9);
  }

  // ---- 底栏 ----
  tft.fillRect(0, kScBot, SCR_W, SCR_H - kScBot, CLR_BAR);
  tft.fillRect(0, kScBot, SCR_W, 1, CLR_LINE);
  tft.setTextSize(1);
  if (pages > 1) {
    tft.setTextColor(scPage > 0 ? CLR_FG : CLR_DIM, CLR_BAR);
    tft.drawString("<<", 10, kScBot + 5);
    tft.setTextColor(scPage + 1 < pages ? CLR_FG : CLR_DIM, CLR_BAR);
    tft.drawString(">>", SCR_W - 22, kScBot + 5);
  }
  if (scBusy)              snprintf(b, sizeof(b), "scanning...");
  else if (scFail == -2)   snprintf(b, sizeof(b), "scan REFUSED (radio busy)");
  else if (scFail == -3)   snprintf(b, sizeof(b), "scan TIMEOUT (%s)", scRemote ? "link?" : "12s");
  else if (!scN)           snprintf(b, sizeof(b), "no AP found");
  else                     snprintf(b, sizeof(b), "%d AP  (%d/%d)", scN, scPage + 1, pages ? pages : 1);
  tft.setTextColor(CLR_ACC, CLR_BAR);
  tft.drawString(b, 10, kScBot + 5);
  tft.setTextColor(CLR_DIM, CLR_BAR);
  tft.drawString("tap a line to pick  |  tap here = rescan", 10, kScBot + 19);
  Serial.printf("[c3] scan: draw %d AP, page %d/%d, %s\n",
                scN, scPage + 1, pages ? pages : 1, scBusy ? "busy" : "idle");
}

// 远端回执的"分流钩子":把 S3 的 wsc/wsce 截下来填列表,不让它滚进输出卡。
static bool scLineSink(const char* pl) {
  if (!scOpen) return false;
  if (!strcmp(pl, "wscan")) return true;                 // 命令回显:吃掉,不显示
  if (!strncmp(pl, "wscf", 4)) {                         // ★ v0.8:S3 报"扫失败"(-2 拒绝/-3 超时)
    scFail  = atoi(pl + 4);
    if (scFail >= 0) scFail = -2;
    scBusy  = false; scDone = true; scDirty = true;
    Serial.printf("[c3] scan: S3 reported FAIL %d\n", scFail);
    return true;
  }
  if (!strncmp(pl, "wsce", 4)) {                         // 结束标记
    scBusy = false; scDone = true; scDirty = true;
    Serial.println("[c3] scan: S3 reported END");
    return true;
  }
  if (strncmp(pl, "wsc ", 4) != 0) return false;         // 不是扫描数据 -> 照常上输出卡
  // "wsc <序> <总数> <rssi> <auth> <SSID...>"  —— SSID 在行尾,可以含空格
  int idx = 0, tot = 0, rssi = 0;
  char auth[12] = "";
  if (sscanf(pl + 4, "%d %d %d %11s", &idx, &tot, &rssi, auth) == 4) {
    const char* s = pl + 4;
    for (int k = 0; k < 4 && s; ++k) s = strchr(s + 1, ' ');   // 跳过 序/总数/rssi/auth
    if (s) scAddAp(s + 1, rssi, auth);
  }
  return true;
}

// 起一次扫描。remote=true 走 S3,false 走 C3 自己的射频。
static void scStart(bool remote) {
  scOpen   = true;
  scRemote = remote;
  scN      = 0;
  scPage   = 0;
  scBusy   = true;
  scDone   = false;
  scDirty  = false;
  scFail   = 0;                                          // ★ v0.8
  scT0     = millis();
  snprintf(scTrail, sizeof(scTrail), "WIFI / %s / SCAN", remote ? "S3" : "C3");
  s3link::setSink(remote ? &uiSink : nullptr);          // ★ v0.9:走统一入口(扫描页/名单页共用)
  scDraw();
  if (remote) {
    outPush("> WIFI / S3 / SCAN  (S3 radio)", CLR_ACC);
    s3link::sendCmd("wscan");
    Serial.println("[c3] scan: asked S3 (@k wscan)");
  } else {
    outPush("> WIFI / C3 / SCAN  (C3 radio)", CLR_ACC);
    // ★★ v0.8 关键一行:连接尝试挂着的时候**驱动会拒绝扫描**(ESP-IDF 明写的
    //    "WiFi still connecting when invoke esp_wifi_scan_start" -> Arduino 层 -2)。
    //    旧版 C3 开机就在连一条连不上的存档网,于是每次扫描都 `got -2 AP`,
    //    屏上却显示成 "no AP found" —— 上一轮排查就被这行文案带偏过。
    wifi10::abortScan();                                 // ★ v0.8:别跟"开机选网"那轮抢结果数组
    wifi10::idleRadioForScan();                          // ★ v0.8:射频归零 —— 否则驱动会拒扫(-2)
    WiFi.scanDelete();
    WiFi.scanNetworks(true, false);                      // ★ 异步,立刻返回
    Serial.println("[c3] scan: started on C3 radio (async)");
  }
}

static void scExit() {
  s3link::setSink(nullptr);
  scOpen = false; scBusy = false;
  if (!scRemote) WiFi.scanDelete();                      // 本机扫描结果自己释放
  Serial.println("[c3] scan: exit");
  enterBoard();
}

// ★★ v0.9(用户 2026-10-14 定,这是本轮的核心改动):点中一行 —— **先翻名单**。
//   用户原话:"点了其中一个热点之后,它要首先去看在列表里面是不是已经存在了,如果在
//   列表里面存在了,那么它直接就可以连接,因为它这个时候已经有它的密码了;除非是它在
//   列表里面没保存过(也没有密码),它才跳出那个输密码的界面。"
//   三类落法:
//     ① 名单里有 + 存过密码 + 不是"上次连失败过" -> **直接拿密码连,不弹键盘**
//     ② 名单里没有 / 没存密码      -> 弹键盘(SSID 预填、光标落 PASS) ← 旧行为
//     ③ 名单里有但上次连失败过(isStale,v0.9 的 O1)-> 也弹键盘,让你重输
//   为什么"翻名单"只能翻 C3 这份:S3 的 `wlist` 只回 "(key)" 提示、不回密码原文。
static void scPick(int i) {
  const bool  c3     = !scRemote;
  const char* ssid   = scSs[i];
  char trail[28];
  snprintf(trail, sizeof(trail), "WIFI / %s / ADD", c3 ? "C3" : "S3");

  const int  hit    = wifi10::indexOf(ssid);                              // 名单里有这条吗
  const bool havePw = (hit >= 0) && wifi10::hasKeyAt(hit) && !wifi10::isStale(ssid);

  // ---- 不管走哪条路,先离开扫描屏 ----
  s3link::setSink(nullptr);
  scOpen = false; scBusy = false;
  if (!scRemote) WiFi.scanDelete();

  if (havePw) {
    // ---- ① 有密码:一步直连 ----
    // ★ v0.10:拆两行(见 outPush2)—— 原来 `> S3  <ssid>` 挤一行,长 SSID 会被 34 列裁掉
    char h[kOutCols + 1], t[kOutCols + 1];
    snprintf(h, sizeof(h), "> %s  (direct)", c3 ? "C3" : "S3");
    snprintf(t, sizeof(t), "  %s", ssid);
    outPush2(h, t);
    outPush("  pwd from list, connecting...", CLR_DIM);
    if (c3) {
      wifi10::addNet(ssid, wifi10::passAt(hit));
    } else {
      char p[160];
      snprintf(p, sizeof(p), "wadd %s %s", ssid, wifi10::passAt(hit));
      snprintf(lastPayload, sizeof(lastPayload), "%s", p);
      s3link::sendCmd(p);
    }
    Serial.printf("[c3] scan: picked \"%s\" -> direct connect (pwd from list #%d)\n",
                  ssid, hit + 1);
    enterBoard();
    return;
  }

  // ---- ② / ③ 没有可用密码:弹键盘 ----
  Serial.printf("[c3] scan: picked \"%s\" (%d dBm) -> keyboard (no usable pwd%s)\n",
                ssid, scRssi[i], (hit >= 0) ? ", saved one is stale" : "");
  enterKbPrefill(c3, trail, ssid);
}

static void scRestart() {
  if (scBusy) return;
  scN = 0; scPage = 0; scBusy = true; scDone = false; scDirty = false;
  scFail = 0;                                            // ★ v0.8
  scT0 = millis();
  scDraw();
  if (scRemote) { s3link::setSink(&uiSink); s3link::sendCmd("wscan"); }
  else {
    wifi10::abortScan();                                                   // ★ v0.8:见 scStart
    wifi10::idleRadioForScan();                                            // ★ v0.8:见 scStart
    WiFi.scanDelete();
    WiFi.scanNetworks(true, false);
  }
  Serial.printf("[c3] scan: rescan (%s)\n", scRemote ? "S3" : "C3");
}

// 主循环每圈调:收割本机异步结果 + 超时兜底 + 需要时重画
static void scPoll() {
  if (!scOpen) return;
  if (scBusy) {
    if (millis() - scT0 > 12000) {                       // 超时兜底:12s 还没齐就收摊
      Serial.println("[c3] scan: timeout");
      scFail = -3;                                       // ★ v0.8:与"真没热点"分开
      scBusy = false; scDone = true; scDirty = true;
    } else if (!scRemote) {
      const int r = WiFi.scanComplete();
      if (r != -1) {                                     // -1 = 还在扫;其余都收摊
        if (r == -2) scFail = -2;                        // ★ v0.8:驱动拒绝(射频还在连)
        for (int i = 0; i < r; ++i) {                    // r<0 时循环不执行 = 0 条
          const String s = WiFi.SSID(i);
          if (!s.length()) continue;                     // 隐藏 SSID 不列
          scAddAp(s.c_str(), (int)WiFi.RSSI(i),
                  scAuthName((uint8_t)WiFi.encryptionType(i)));
        }
        WiFi.scanDelete();
        scBusy = false; scDone = true; scDirty = true;
        if (r < 0) Serial.println("[c3] scan: C3 radio got -2 (driver refused: radio busy connecting)");
        else       Serial.printf("[c3] scan: C3 radio got %d AP\n", r);
      }
    }
  }
  if (scDirty && millis() - scDrawMs > 200) {            // 节流重画:列表"活"起来但不刷爆
    scDirty = false; scDrawMs = millis();
    scDraw();
  }
}

static void scTap(int x, int y) {
  if (y < kScHdrH) { scExit(); return; }                 // 点顶栏 = 退出
  if (y < kScBot) {                                      // 点列表
    const int r = (y - kScTop) / kScRowH;
    const int i = scPage * kScRows + r;
    if (r >= 0 && r < kScRows && i >= 0 && i < scN) scPick(i);
    return;
  }
  if (x < 48)  { if (scPage > 0) { --scPage; scDraw(); } return; }
  if (x >= 192){ const int p = scPages(); if (scPage + 1 < p) { ++scPage; scDraw(); } return; }
  scRestart();                                           // 底栏中间 = 重扫
}

// ============================================================================
//  「S3 LOG」搬运页(★ v1.2.0 新增,2026-10-17 用户定)
// ----------------------------------------------------------------------------
//  需求原话:VIEW/LOG 一键做两件事 —— ①让 S3 显示日志页(发 'v'),②把 S3 的
//  第三/四界面**全部搬到 C3**:10 行事件列表 + PREV/NEXT 翻页 + 统计,每条点进去
//  看详情(含 GGA 原文)。数据走 S3 v1.8.0 新增的 evtlist/evtdet/evtstat 命令。
//  设计口径:**「S3 上是什么,C3 上就是什么」** ——
//    · 行色由 S3 定、随帧发(<col> 字段),C3 不判色、不存词表;
//    · 版式/几何照抄 S3(顶栏 54 / 列表 10 行 x 22 / 底栏 36 / 翻页按钮 y28..49);
//    · 统计的 6 个分子分母原样搬运,百分比/均值在本地按同公式现算。
//  实现:★ v1.2.2 改为**内容对齐分带 sprite 原子重绘**(v1.2.0"直接画 tft 不会闪"的
//    假设错了 —— 真正的杀手是 evtlist 一批 12 帧、每帧整页重画一次)。两层修:
//    ① 收帧只标脏,**批次收敛(最后一帧后 60ms)才重画一次**(evtPoll);
//    ② 重画走 kEvBnd 五个带(边界全落在文字行之间),每带建 16bit sprite 整带一次
//       push 再释放 —— 单带最大 240x90x2=43.2KB,连 WiFi 开着也放得下
//       (全屏 sprite 153.6KB 在无 PSRAM+WiFi 下放不下,这正是 v1.2.0 不用 sprite 的原因);
//       分配失败自动降级直画(闪,但不白屏)。
// ============================================================================
constexpr int kEvTfaH = 54;                    // 顶固定区(与 S3 同)
constexpr int kEvRowH = 22, kEvRows = 10;      // 列表 10 行 x 22
constexpr int kEvBfaH = 36;                    // 底固定区(统计 + 页码)
constexpr int kEvPgY = 28, kEvPgH = 22, kEvPgW = 115;
constexpr int kEvPgXL = 4,  kEvPgXR = 121;     // 右沿 236(与 S3 同几何)
constexpr int kEvBackX = 170, kEvBackY = 2, kEvBackW = 64, kEvBackH = 24;
constexpr uint16_t kEvAltBg = 0x10A2;          // 隔行底色(与 S3 列表同值)
constexpr uint32_t kEvWaitMs = 4000;           // S3 回帧超时

struct EvtRow {
  uint32_t seq;                                // 详情请求按 seq 寻址(防移位)
  char     date[6];                            // "MM-DD"
  uint32_t t0;                                 // 开始时刻(当天秒,UTC)
  bool     closed;
  uint16_t col;                                // 行色(S3 定,565)
  char     word[10];                           // 类型词(<=9 字符,S3 phrase::text)
};

static bool     evtOpenFlag = false;           // 页开着(列表或详情)
static bool     evtDetail   = false;           // true = 详情子页
static bool     evtDirty    = false;           // 需要重画
static int      evtTop = 0, evtTotal = 0, evtN = 0;
static int      evtReqTop = -1;                // 等待中的 evtlist 请求(-1 = 无)
static uint32_t evtReqMs = 0;
static bool     evtTimeout = false;            // 超时兜底(画"no reply")
static uint32_t evtLastRx = 0;                 // ★ v1.2.2:最近一帧 evt* 到达时刻(批内持续刷新)
static EvtRow   evtRows[kEvRows];

// 统计(6 个原始分子分母,与 S3 底栏同源;公式在本地现算)
static uint32_t evTot = 0, evOnl = 0, evOk = 0, evSumS = 0, evSumC = 0, evNS = 0;

// 详情子页
static bool     detWait = false;               // 等待 evtd/evtg
static uint32_t detReqMs = 0;
static bool     detHave = false;
static uint32_t detSeq = 0, detT0 = 0, detT1 = 0;
static char     detDate[6] = "";
static int      detClosed = 0, detOffDm = 0, detIdx = 0, detTotal = 0;
static uint16_t detCol = 0;
static char     detWord[10] = "";
static char     detGga[3][40];                 // GGA 82 字符 -> 最多 3 段(38 字符/段)
static int      detGgaN = 0;

// ---- 分带原子重绘的画布层(★ v1.2.2) ----------------------------------------
//  画布指针 eS 非空 = 画进当前带的 sprite(y 坐标自动平移 + 带边裁剪);
//  eS 为空 = 直画 tft(createSprite 失败的降级路径)。绘制函数两种模式共用。
static TFT_eSprite* eS  = nullptr;
static int          eY0 = 0, eH = 0;

static void eFill(int x, int y, int w, int h, uint16_t c) {
  if (!eS) { tft.fillRect(x, y, w, h, c); return; }
  const int a = (y > eY0) ? y : eY0;
  const int b = (y + h < eY0 + eH) ? (y + h) : (eY0 + eH);
  if (b <= a) return;                          // 与当前带不相交
  eS->fillRect(x, a - eY0, w, b - a, c);
}
static void eFillAll(uint16_t c) { eFill(0, 0, SCR_W, SCR_H, c); }
static void eFont(uint8_t f)   { if (eS) eS->setTextFont(f);   else tft.setTextFont(f); }
static void eSize(uint8_t s)   { if (eS) eS->setTextSize(s);   else tft.setTextSize(s); }
static void eColor(uint16_t fg, uint16_t bg) { if (eS) eS->setTextColor(fg, bg); else tft.setTextColor(fg, bg); }
static void eDatum(uint8_t d)  { if (eS) eS->setTextDatum(d);  else tft.setTextDatum(d); }
static void eStr(const char* s, int x, int y) {
  if (eS) eS->drawString(s, x, y - eY0); else tft.drawString(s, x, y);
}

// ---- 帧解析(sink;只吃 evth/evts/evtr/evtd/evtg,命令回显一律吞掉) ----
static bool evtLineSink(const char* pl) {
  if (!strncmp(pl, "evt", 3)) evtLastRx = millis();   // ★ v1.2.2:任一 evt* 帧到 = 批次在进
  if (!strncmp(pl, "evth ", 5)) {              // "evth <nHave> <top>"
    sscanf(pl + 5, "%d %d", &evtTotal, &evtTop);
    evtN = 0; evtReqTop = -1; evtTimeout = false;
    evtDetail = false; evtDirty = true;
    return true;
  }
  if (!strncmp(pl, "evts ", 5)) {              // "evts <tot> <onl> <ok> <sumS> <sumC> <nS>"
    sscanf(pl + 5, "%lu %lu %lu %lu %lu %lu",
           (unsigned long*)&evTot, (unsigned long*)&evOnl, (unsigned long*)&evOk,
           (unsigned long*)&evSumS, (unsigned long*)&evSumC, (unsigned long*)&evNS);
    return true;
  }
  if (!strncmp(pl, "evtr ", 5)) {              // "evtr <seq> <MM-DD> <t0> <closed> <col> <WORD>"
    if (evtN < kEvRows) {
      EvtRow& r = evtRows[evtN];
      char date[6] = "";
      unsigned long seq = 0, t0 = 0, col = 0;
      int closed = 0;
      if (sscanf(pl + 5, "%lu %5s %lu %d %lu", &seq, date, &t0, &closed, &col) == 5) {
        r.seq = seq; r.t0 = t0; r.closed = (closed != 0); r.col = (uint16_t)col;
        snprintf(r.date, sizeof(r.date), "%s", date);
        const char* w = pl + 5;                // 第 6 个字段(类型词)在行尾,可含字母数字-
        for (int k = 0; k < 5 && w; ++k) w = strchr(w + 1, ' ');
        snprintf(r.word, sizeof(r.word), "%s", w ? w + 1 : "?");
        ++evtN;
      }
    }
    evtDirty = true;
    return true;
  }
  if (!strncmp(pl, "evtd ", 5)) {              // "evtd <seq> <MM-DD> <t0> <t1> <closed> <offDm> <col> <idx> <nHave> <WORD>"
    unsigned long seq = 0, t0 = 0, t1 = 0, col = 0;
    char date[6] = ""; int closed = 0, off = 0, idx = 0, tot = 0;
    if (sscanf(pl + 5, "%lu %5s %lu %lu %d %d %lu %d %d",
               &seq, date, &t0, &t1, &closed, &off, &col, &idx, &tot) == 9) {
      detSeq = seq; detT0 = t0; detT1 = t1;
      snprintf(detDate, sizeof(detDate), "%s", date);
      detClosed = closed; detOffDm = off; detCol = (uint16_t)col;
      detIdx = idx; detTotal = tot;
      const char* w = pl + 5;                  // 第 10 个字段 = 类型词
      for (int k = 0; k < 9 && w; ++k) w = strchr(w + 1, ' ');
      snprintf(detWord, sizeof(detWord), "%s", w ? w + 1 : "?");
      detGgaN = 0; detHave = true; detWait = false;
      evtDetail = true; evtDirty = true;
      Serial.printf("[c3] evt: evtd seq=%lu col=%u word='%s' idx=%d/%d\n",
                    (unsigned long)detSeq, detCol, detWord, detIdx, detTotal);
    } else {
      Serial.printf("[c3] evt: evtd PARSE FAIL: '%s'\n", pl);
    }
    return true;
  }
  if (!strncmp(pl, "evtg ", 5)) {              // "evtg <38 字符段>"
    if (detGgaN < 3) { snprintf(detGga[detGgaN], sizeof(detGga[0]), "%s", pl + 5); ++detGgaN; }
    evtDirty = true;
    return true;
  }
  if (!strncmp(pl, "evt", 3)) return true;     // 命令回显(evtlist/evtdet/evtstat):吞掉
  return false;                                // 其余照常(理论上到不了这)
}

// ---- 绘制小件:凹槽按钮(与 S3 的 BACK/翻页键同款:块底上挖页底色 + 青字) ----
//  ★ v1.2.2:走 e* 画布层(按钮全在顶栏带内,不需要额外裁剪)。
static void evtBtn(int x, int y, int w, int h, const char* txt, bool enabled) {
  if (eS) eS->fillRoundRect(x, y - eY0, w, h, 4, CLR_BG);
  else    tft.fillRoundRect(x, y, w, h, 4, CLR_BG);
  eFont(1); eSize(1);
  eColor(enabled ? TFT_CYAN : CLR_DIM, CLR_BG);
  eDatum(MC_DATUM);
  eStr(txt, x + w / 2, y + h / 2);
  eDatum(TL_DATUM);
}

// ---- 列表页(镜像 S3 第三界面) ----
static void evtPaintList() {
  eFillAll(CLR_BG);
  // 顶栏
  eFill(0, 0, SCR_W, kEvTfaH, CLR_BAR);
  eFont(2); eSize(1);
  eColor(CLR_FG, CLR_BAR);
  eDatum(ML_DATUM);
  eStr("S3 EVENT LOG", 8, 14);
  eDatum(TL_DATUM);
  evtBtn(kEvBackX, kEvBackY, kEvBackW, kEvBackH, "BACK", true);
  // 翻页按钮行(y28..49,与 S3 同几何)
  evtBtn(kEvPgXL, kEvPgY, kEvPgW, kEvPgH, "< PREV", evtTop > 0);
  evtBtn(kEvPgXR, kEvPgY, kEvPgW, kEvPgH, "NEXT >", evtTop + kEvRows < evtTotal);
  // 列表区
  if (evtReqTop >= 0) {
    eFont(1); eColor(CLR_DIM, CLR_BG);
    eStr("loading... (S3)", 6, kEvTfaH + 14);
  } else if (evtTimeout) {
    eFont(1); eColor(CLR_WARN, CLR_BG);
    eStr("no reply from S3 (link?)", 6, kEvTfaH + 14);
  } else if (evtN == 0) {
    eFont(1); eColor(CLR_DIM, CLR_BG);
    eStr("no events yet (normal = no entries)", 6, kEvTfaH + 14);
  } else {
    char ln[48];
    for (int r = 0; r < kEvRows; ++r) {
      if (r >= evtN) break;
      const EvtRow& e = evtRows[r];
      const int y = kEvTfaH + r * kEvRowH;
      const uint16_t bg = (r & 1) ? kEvAltBg : CLR_BG;
      if (r & 1) eFill(0, y, SCR_W, kEvRowH, bg);
      snprintf(ln, sizeof(ln), "%s %02lu:%02lu  %-9s %s",
               e.date, (unsigned long)(e.t0 / 3600u), (unsigned long)((e.t0 / 60u) % 60u),
               e.word, e.closed ? "OK" : "--");
      eFont(1); eSize(1);
      eColor(e.col, bg);             // ★ 行色 = S3 发来的(已按类型/闭环定好)
      eStr(ln, 6, y + 7);
    }
  }
  // 底栏:统计 + 页码(公式与 S3 底栏逐字同源)
  eFill(0, SCR_H - kEvBfaH, SCR_W, kEvBfaH, CLR_BAR);
  eFont(1); eSize(1);
  eColor(TFT_CYAN, CLR_BAR);
  const uint32_t okp  = evTot ? (evOk * 100u / evTot) : 0;
  const uint32_t avgS = evNS  ? (evSumS / evNS) : 0;
  const uint32_t avgC = evNS  ? (evSumC / evNS) : 0;
  char b[52];
  snprintf(b, sizeof(b), "evt %d/100  OK %lu%%  sat %lu  cn0 %lu.%lu",
           evtTotal, (unsigned long)okp, (unsigned long)avgS,
           (unsigned long)(avgC / 10u), (unsigned long)(avgC % 10u));
  eStr(b, 6, SCR_H - kEvBfaH + 8);
  const int lastShown = (evtTop + kEvRows < evtTotal) ? (evtTop + kEvRows) : evtTotal;
  if (evtTotal) snprintf(b, sizeof(b), "%d-%d / %d", evtTop + 1, lastShown, evtTotal);
  else          snprintf(b, sizeof(b), "0 / 0");
  eStr(b, 6, SCR_H - kEvBfaH + 22);
}

// ---- 详情页(镜像 S3 第四界面) ----
static void evtPaintDetail() {
  eFillAll(CLR_BG);
  // 顶栏:类型大字(用 S3 发来的行色)+ BACK + PREV/NEXT
  eFill(0, 0, SCR_W, kEvTfaH, CLR_BAR);
  eFont(4); eSize(1);
  eColor(detCol, CLR_BAR);
  eDatum(ML_DATUM);
  eStr(detHave ? detWord : "...", 8, 14);
  eDatum(TL_DATUM);
  evtBtn(kEvBackX, kEvBackY, kEvBackW, kEvBackH, "BACK", true);
  evtBtn(kEvPgXL, kEvPgY, kEvPgW, kEvPgH, "< PREV", detHave && detIdx > 0);
  evtBtn(kEvPgXR, kEvPgY, kEvPgW, kEvPgH, "NEXT >", detHave && detIdx < detTotal - 1);
  if (detWait && !detHave) {                   // 首次进详情还没等到帧
    eFont(1); eColor(CLR_DIM, CLR_BG);
    eStr("loading... (S3)", 6, kEvTfaH + 14);
    return;
  }
  // 中部元数据(标签灰 / 值白,与 S3 同版式)
  eFont(1); eSize(1);
  int y = kEvTfaH + 12;
  const char* lab[5] = { "seq", "start", "end", "dur", "off" };
  for (int i = 0; i < 5; ++i) {
    eColor(CLR_DIM, CLR_BG);
    eStr(lab[i], 6, y + i * 14);
  }
  char b[48];
  eColor(CLR_FG, CLR_BG);
  snprintf(b, sizeof(b), "#%lu", (unsigned long)detSeq);
  eStr(b, 46, y);
  snprintf(b, sizeof(b), "%s %02lu:%02lu UTC", detDate,
           (unsigned long)(detT0 / 3600u), (unsigned long)((detT0 / 60u) % 60u));
  eStr(b, 46, y + 14);
  if (detClosed) snprintf(b, sizeof(b), "%02lu:%02lu UTC",
                          (unsigned long)(detT1 / 3600u), (unsigned long)((detT1 / 60u) % 60u));
  else           strcpy(b, "--  (not recovered)");
  eStr(b, 46, y + 28);
  if (detClosed && detT1 >= detT0) snprintf(b, sizeof(b), "%lu s", (unsigned long)(detT1 - detT0));
  else                             strcpy(b, "--");
  eStr(b, 46, y + 42);
  snprintf(b, sizeof(b), "%.1f m", detOffDm / 10.0f);
  eStr(b, 46, y + 56);
  // 下部:GGA 原文(S3 发来的截段,最多 3 段 x 38 字符)
  int gy = 176;
  eColor(CLR_DIM, CLR_BG);
  eStr("GGA at event start:", 6, gy - 14);
  eColor(CLR_FG, CLR_BG);
  if (detGgaN == 0 && detWait) { eStr("loading...", 6, gy); return; }
  if (detGgaN == 0)            { eStr("(no GGA captured)", 6, gy); return; }
  for (int i = 0; i < detGgaN; ++i) eStr(detGga[i], 6, gy + i * 10);
}

// ---- 带边界:全部落在"文字行与文字行之间" ----
//  (列表行文字 y+7..y+15;详情标签 66..134、GGA 162..204;顶栏/按钮/底栏各自成带)
//  ⇒ 任何文字都不会被带边界切成两半。单带最大 240x90x2 = 43.2KB。
static constexpr int16_t kEvBnd[6] = { 0, 54, 140, 230, 274, 320 };

static void evtPaintInto() {
  if (evtDetail) evtPaintDetail(); else evtPaintList();
}

static void evtDraw() {
  mode = MODE_EVT;
  lastX = lastY = -1;
  // ★ v1.2.2:逐带建 16bit sprite -> 整带一次 push -> 释放(原子重绘,治闪动)。
  //   分配失败(极端碎片)降级为整页直画:闪,但不白屏。
  for (int i = 0; i < 5; ++i) {
    const int y0 = kEvBnd[i], h = kEvBnd[i + 1] - y0;
    TFT_eSprite s(&tft);
    s.setColorDepth(16);
    if (!s.createSprite(SCR_W, h)) { eS = nullptr; evtPaintInto(); evtDirty = false; return; }
    eS = &s; eY0 = y0; eH = h;
    evtPaintInto();
    s.pushSprite(0, y0);
    eS = nullptr;
    s.deleteSprite();
  }
  evtDirty = false;
}

// ---- 开页 / 关页 ----
static void evtOpen() {
  evtOpenFlag = true; evtDetail = false; evtDirty = false;
  evtTop = 0; evtTotal = 0; evtN = 0; evtTimeout = false;
  detHave = false; detWait = false; detGgaN = 0;
  evTot = evOnl = evOk = evSumS = evSumC = evNS = 0;
  evtReqTop = 0; evtReqMs = millis();
  mode = MODE_EVT; lastX = lastY = -1;
  s3link::setSink(&uiSink);
  evtDraw();                                   // 先画骨架(列表区显示 loading...)
  s3link::sendCmd("evtlist 0");
  Serial.println("[c3] evt: page open, asked evtlist 0");
}

static void evtExit() {
  s3link::setSink(nullptr);
  evtOpenFlag = false;
  Serial.println("[c3] evt: exit");
  enterBoard();
}

// ---- 请求(翻页 / 详情) ----
static void evtReqList(int top) {
  if (top < 0) top = 0;
  evtTop = top; evtN = 0; evtTimeout = false;
  evtReqTop = top; evtReqMs = millis();
  evtDetail = false; detWait = false;
  char c[24];
  snprintf(c, sizeof(c), "evtlist %d", top);
  s3link::sendCmd(c);
  evtDraw();
}

static void evtReqDet(uint32_t seq, int d) {
  detWait = true; detReqMs = millis();
  detHave = false; detGgaN = 0;
  evtDetail = true;
  char c[28];
  if (d) snprintf(c, sizeof(c), "evtdet %lu %+d", (unsigned long)seq, d);
  else   snprintf(c, sizeof(c), "evtdet %lu", (unsigned long)seq);
  s3link::sendCmd(c);
  evtDraw();
}

// ---- 触摸 ----
static void evtTap(int x, int y) {
  const bool inPgY = (y >= kEvPgY && y < kEvPgY + kEvPgH);
  const bool inBack = (x >= kEvBackX && x < kEvBackX + kEvBackW &&
                       y >= kEvBackY && y < kEvBackY + kEvBackH);
  if (evtDetail) {                             // ---- 详情子页 ----
    if (inBack) { evtDetail = false; detWait = false; evtDraw(); return; }  // BACK 回列表
    if (inPgY && detHave) {
      if (x >= kEvPgXL && x < kEvPgXL + kEvPgW && detIdx > 0)
        evtReqDet(detSeq, -1);                 // PREV = 上一条(更新)
      else if (x >= kEvPgXR && x < kEvPgXR + kEvPgW && detIdx < detTotal - 1)
        evtReqDet(detSeq, +1);                 // NEXT = 下一条(更旧)
    }
    return;
  }
  // ---- 列表页 ----
  if (inBack) { evtExit(); return; }
  if (inPgY) {
    if (x >= kEvPgXL && x < kEvPgXL + kEvPgW && evtTop > 0)
      evtReqList(evtTop - kEvRows);
    else if (x >= kEvPgXR && x < kEvPgXR + kEvPgW && evtTop + kEvRows < evtTotal)
      evtReqList(evtTop + kEvRows);
    return;
  }
  if (y >= kEvTfaH && y < kEvTfaH + kEvRows * kEvRowH) {   // 点行 -> 详情
    const int r = (y - kEvTfaH) / kEvRowH;
    if (r >= 0 && r < evtN) evtReqDet(evtRows[r].seq, 0);
  }
}

// ---- 主循环:超时兜底 + 按需重绘 ----
static void evtPoll() {
  if (!evtOpenFlag) return;
  const uint32_t now = millis();
  bool timeout = false;
  if (evtReqTop >= 0 && now - evtReqMs > kEvWaitMs) { evtReqTop = -1; timeout = true; }
  if (detWait && now - detReqMs > kEvWaitMs) { detWait = false; timeout = true; }
  if (timeout) { evtTimeout = true; evtDirty = true; }
  // ★ v1.2.2:批次收敛后再画 —— evtlist 一批 12 帧(evth+evts+10xevtr),v1.2.0 每帧
  //   整页重画一次 = 开一页连闪 12 次;现在帧到只刷新 evtLastRx,最后一帧 60ms 后才重画。
  if (evtDirty && now - evtLastRx >= 60) evtDraw();
}

// ============================================================================
//  整屏弹窗 MODE_ALERT(★ v0.9 新增,用户 2026-10-14 定"整屏弹一个提示框是挺好的")

// ----------------------------------------------------------------------------
//  一套件两种用法:
//    · alertInfo()    —— 1 个按钮(连接失败告警:标题 + 大字原因 + 小字 SSID)
//    · alertConfirm() —— 2 个按钮(删除确认)
//  模态:弹窗期间 onTap 只投给它;关掉后回到弹它之前那一屏整块重绘,再跑回调。
//  ⚠ 文案必须**纯 ASCII** —— 屏上走 TFT_eSPI 的 GLCD 字体,中文会画成乱符号。
//  ⚠ 只对"用户主动点的连接"失败弹:C3 侧在 wifi10::connFailed 里判过 trOn,
//    开机自动巡台账失败不弹(那是后台行为,不该半路打断用户)。
// ============================================================================
static void closeMenu();                               // 前向:弹窗前先把菜单收掉
static void lsDraw();                                  // 前向:弹窗关掉后要重绘名单页

constexpr int kAlW    = SCR_W - 32;
constexpr int kAlH    = 152;
constexpr int kAlBtnH = 30;

static bool   alertOpen   = false;
static Mode   alertBack   = MODE_BOARD;
static char   alTitle[20] = "";
static char   alBig[24]   = "";
static char   alSub[30]   = "";
static char   alB1[12]    = "";
static char   alB2[12]    = "";
static void (*alCb)(bool) = nullptr;
static int    alBtnY      = 0;

static void alertDraw() {
  mode = MODE_ALERT;
  lastX = lastY = -1;
  tft.fillScreen(CLR_BG);
  const int x = 16, y = (SCR_H - kAlH) / 2;
  tft.fillRoundRect(x, y, kAlW, kAlH, 10, CLR_BAR);
  tft.drawRoundRect(x, y, kAlW, kAlH, 10, CLR_LINE);
  tft.setTextSize(1);

  tft.setTextColor(CLR_DIM, CLR_BAR);
  tft.drawString(alTitle, x + (kAlW - (int)strlen(alTitle) * 6) / 2, y + 16);

  // 大字:短就用 size 2(12px/字),太长降回 size 1(6px/字)以免出框
  const bool big2 = ((int)strlen(alBig) * 12 <= kAlW - 16);
  tft.setTextSize(big2 ? 2 : 1);
  const int bw = big2 ? 12 : 6;
  tft.setTextColor(CLR_ERR, CLR_BAR);
  tft.drawString(alBig, x + (kAlW - (int)strlen(alBig) * bw) / 2, y + 44);
  tft.setTextSize(1);

  tft.setTextColor(CLR_FG, CLR_BAR);
  tft.drawString(alSub, x + (kAlW - (int)strlen(alSub) * 6) / 2, y + 80);

  alBtnY = y + kAlH - kAlBtnH - 16;
  if (alB2[0]) {                                       // ---- 两个按钮(左=主操作,右=取消) ----
    const int w = 84;
    tft.fillRoundRect(x + 16, alBtnY, w, kAlBtnH, 6, CLR_BAR);
    tft.drawRoundRect(x + 16, alBtnY, w, kAlBtnH, 6, CLR_ERR);
    tft.setTextColor(CLR_ERR, CLR_BAR);
    tft.drawString(alB1, x + 16 + (w - (int)strlen(alB1) * 6) / 2, alBtnY + 11);
    const int x2 = x + kAlW - 16 - w;
    tft.fillRoundRect(x2, alBtnY, w, kAlBtnH, 6, CLR_BAR);
    tft.drawRoundRect(x2, alBtnY, w, kAlBtnH, 6, CLR_LINE);
    tft.setTextColor(CLR_DIM, CLR_BAR);
    tft.drawString(alB2, x2 + (w - (int)strlen(alB2) * 6) / 2, alBtnY + 11);
  } else {                                             // ---- 一个按钮,居中 ----
    const int w  = 96;
    const int x1 = x + (kAlW - w) / 2;
    tft.fillRoundRect(x1, alBtnY, w, kAlBtnH, 6, CLR_ACC);
    tft.setTextColor(CLR_BG, CLR_ACC);
    tft.drawString(alB1, x1 + (w - (int)strlen(alB1) * 6) / 2, alBtnY + 11);
  }
  Serial.printf("[c3] alert: '%s' / '%s' / '%s' two=%d\n",
                alTitle, alBig, alSub, alB2[0] ? 1 : 0);
}

// 关窗 -> 回到弹它之前那一屏(整块重绘)-> 再跑回调(回调里可能又要弹下一个)
static void alertClose(bool ok) {
  void (*cb)(bool) = alCb;
  alCb = nullptr;
  alertOpen = false;
  const Mode back = alertBack;
  if      (back == MODE_LIST) lsDraw();
  else if (back == MODE_SCAN) scDraw();
  else                        enterBoard();
  if (cb) cb(ok);
}

static void alertTap(int x, int y) {
  const bool two = (alB2[0] != 0);
  if (y >= alBtnY && y < alBtnY + kAlBtnH) {
    if (!two) { alertClose(true); return; }
    if (x < SCR_W / 2) alertClose(true);               // 左 = 确定
    else               alertClose(false);              // 右 = 取消
    return;
  }
  alertClose(!two);        // 单按钮:点任意处 = 确定;双按钮:点窗外 = 取消(安全)
}

static void alertInfo(const char* title, const char* big, const char* sub) {
  snprintf(alTitle, sizeof(alTitle), "%.19s", title);
  snprintf(alBig,   sizeof(alBig),   "%.19s", big);
  snprintf(alSub,   sizeof(alSub),   "%.24s", sub);
  snprintf(alB1,    sizeof(alB1),    "OK");
  alB2[0] = 0;
  alCb = nullptr;
  if (mode != MODE_ALERT) alertBack = mode;
  if (menuOpen) closeMenu();
  alertOpen = true;
  alertDraw();
}

static void alertConfirm(const char* title, const char* big, const char* sub,
                         const char* b1, const char* b2, void (*cb)(bool)) {
  snprintf(alTitle, sizeof(alTitle), "%.19s", title);
  snprintf(alBig,   sizeof(alBig),   "%.19s", big);
  snprintf(alSub,   sizeof(alSub),   "%.24s", sub);
  snprintf(alB1,    sizeof(alB1),    "%.11s", b1);
  snprintf(alB2,    sizeof(alB2),    "%.11s", b2);
  alCb = cb;
  if (mode != MODE_ALERT) alertBack = mode;
  if (menuOpen) closeMenu();
  alertOpen = true;
  alertDraw();
}

// ============================================================================
//  名单页 MODE_LIST(★ v0.9 新增) —— 菜单 WIFI / C3|S3 / DEL 进这里
// ----------------------------------------------------------------------------
//  为什么要有它(用户 2026-10-14 定):
//    · LIST 菜单项撤掉 —— "专门把存过的热点列出来看没有意义"。
//    · 但 DEL 得选"删第几条",LIST 没了编号就无处可看 ⇒ 把"看"和"删"合成一片:
//      点进来看见名单,点某一条 -> 弹一句确认 -> 删掉。
//  两个数据源:
//    · WIFI / C3 / DEL -> 直接读本机 wifi10 那份(零通信)
//    · WIFI / S3 / DEL -> 先发 `wlist`,等 S3 把名单回上来(回执由 lsLineSink 截走)
//  版式与扫描页同族(顶栏 24 / 行高 26),右侧显示 `(key)`/`(open)` 而非信号强度。
// ============================================================================
constexpr int kLsMax = 10;                             // 与 wifi10 的 kMax 一致
constexpr int kLsTop = kScHdrH + 2;
constexpr int kLsBot = kLsTop + kScRows * kScRowH;

static bool     lsOpen   = false;
static bool     lsRemote = false;                      // true = 名单来自 S3
static bool     lsWait   = false;                      // 还在等 S3 的 wlist
static bool     lsDirty  = false;
static int      lsN      = 0;
static int      lsPage   = 0;
static int      lsSel    = -1;
static uint32_t lsT0     = 0;
static char     lsSs[kLsMax][33];
static bool     lsKey[kLsMax];
static char     lsTrail[28] = "";

static int lsPages() { return (lsN + kScRows - 1) / kScRows; }

static void lsDraw() {
  mode = MODE_LIST;
  lastX = lastY = -1;
  tft.fillScreen(CLR_BG);

  // ---- 顶栏(点它 = 退出) ----  ★ v0.9:同样**不画 WiFi 图标**(会压到面包屑)
  tft.fillRect(0, 0, SCR_W, kScHdrH, CLR_BAR);
  tft.setTextSize(1);
  tft.setTextColor(CLR_ACC, CLR_BAR);
  tft.drawString("LIST", 6, 8);
  tft.setTextColor(CLR_DIM, CLR_BAR);
  tft.drawString(lsTrail, SCR_W - 6 - (int)strlen(lsTrail) * 6, 8);

  // ---- 列表 ----
  const int pages = lsPages();
  if (pages && lsPage >= pages) lsPage = pages - 1;
  if (lsPage < 0) lsPage = 0;
  char b[48];
  for (int r = 0; r < kScRows; ++r) {
    const int y = kLsTop + r * kScRowH;
    tft.fillRect(0, y, SCR_W, kScRowH - 2, CLR_BG);
    const int i = lsPage * kScRows + r;
    if (i >= lsN) continue;
    tft.fillRoundRect(4, y, SCR_W - 8, kScRowH - 2, 4, CLR_BAR);
    snprintf(b, sizeof(b), "#%d", i + 1);
    tft.setTextColor(CLR_ACC, CLR_BAR);
    tft.drawString(b, 10, y + 9);
    snprintf(b, sizeof(b), "%.19s", lsSs[i]);
    tft.setTextColor(CLR_FG, CLR_BAR);
    tft.drawString(b, 34, y + 9);
    tft.setTextColor(CLR_DIM, CLR_BAR);
    tft.drawString(lsKey[i] ? "(key)" : "(open)", SCR_W - 12 - 6 * 6, y + 9);
  }

  // ---- 底栏 ----
  tft.fillRect(0, kLsBot, SCR_W, SCR_H - kLsBot, CLR_BAR);
  tft.fillRect(0, kLsBot, SCR_W, 1, CLR_LINE);
  tft.setTextSize(1);
  if (pages > 1) {
    tft.setTextColor(lsPage > 0 ? CLR_FG : CLR_DIM, CLR_BAR);
    tft.drawString("<<", 10, kLsBot + 5);
    tft.setTextColor(lsPage + 1 < pages ? CLR_FG : CLR_DIM, CLR_BAR);
    tft.drawString(">>", SCR_W - 22, kLsBot + 5);
  }
  if (lsWait)    snprintf(b, sizeof(b), "reading S3 list...");
  else if (!lsN) snprintf(b, sizeof(b), "none saved");
  else           snprintf(b, sizeof(b), "%d saved  (%d/%d)", lsN, lsPage + 1, pages ? pages : 1);
  tft.setTextColor(CLR_ACC, CLR_BAR);
  tft.drawString(b, 10, kLsBot + 5);
  tft.setTextColor(CLR_DIM, CLR_BAR);
  tft.drawString("tap a line to DELETE  |  here = back", 10, kLsBot + 19);
  Serial.printf("[c3] list: draw %d entr%s, page %d/%d\n",
                lsN, lsN == 1 ? "y" : "ies", lsPage + 1, pages ? pages : 1);
}

// S3 的 wlist 回执钩子:"txt #1 <ssid> (key)" / "txt none saved"
static bool lsLineSink(const char* pl) {
  if (!lsOpen) return false;
  if (!strcmp(pl, "wlist")) return true;                       // 命令回显:吃掉
  if (!strncmp(pl, "txt none saved", 14)) {
    lsN = 0; lsWait = false; lsDirty = true;
    return true;
  }
  if (strncmp(pl, "txt #", 5) != 0) return false;              // 不是名单行 -> 照常上输出卡
  int n = 0;
  char ssid[33] = "", kind[12] = "";
  if (sscanf(pl + 5, "%d %32s %11s", &n, ssid, kind) >= 2 && n >= 1 && n <= kLsMax) {
    snprintf(lsSs[n - 1], sizeof(lsSs[0]), "%s", ssid);
    lsKey[n - 1] = (strstr(kind, "key") != nullptr);
    if (n > lsN) lsN = n;
  }
  lsWait = false; lsDirty = true;
  return true;
}

// 打开名单页。remote=true 走 S3(先发 wlist 把名单拉回来)
static void listStart(bool remote) {
  lsOpen   = true;
  lsRemote = remote;
  lsN      = 0;
  lsPage   = 0;
  lsWait   = false;
  lsDirty  = false;
  lsSel    = -1;
  lsT0     = millis();
  snprintf(lsTrail, sizeof(lsTrail), "WIFI / %s / DEL", remote ? "S3" : "C3");

  outPush(remote ? "> WIFI / S3 / DEL" : "> WIFI / C3 / DEL", CLR_ACC);

  if (remote) {
    lsWait = true;
    s3link::setSink(&uiSink);
    lsDraw();
    s3link::sendCmd("wlist");
    Serial.println("[c3] list: asked S3 (@k wlist)");
    return;
  }
  // 本机:直接从 wifi10 抄一份,零通信、零等待
  const int n = wifi10::count();
  for (int i = 0; i < n && i < kLsMax; ++i) {
    // ★ 2026-10-15:`%.32s` 显式限宽 —— SSID 规范上限就是 32 字节,所以输出与 `%s`
    //   完全一致;加上是为了让编译器能自己算出上界、不再报 -Wformat-truncation。
    snprintf(lsSs[i], sizeof(lsSs[0]), "%.32s", wifi10::ssidAt(i));
    lsKey[i] = wifi10::hasKeyAt(i);
  }
  lsN = (n > kLsMax) ? kLsMax : n;
  s3link::setSink(nullptr);
  lsDraw();
  Serial.printf("[c3] list: local (%d entries)\n", lsN);
}

static void lsExit() {
  s3link::setSink(nullptr);
  lsOpen = false; lsWait = false;
  Serial.println("[c3] list: exit");
  enterBoard();
}

static void lsDoDel() {
  const int i = lsSel;
  if (i < 0 || i >= lsN) return;
  if (lsRemote) {
    char p[24];
    snprintf(p, sizeof(p), "wdel %d", i + 1);
    snprintf(lastPayload, sizeof(lastPayload), "%s", p);
    s3link::sendCmd(p);
    Serial.printf("[c3] list: sent wdel %d\n", i + 1);
  } else {
    wifi10::delNet(i + 1);
    Serial.printf("[c3] list: deleted #%d locally\n", i + 1);
  }
  // 本地立刻把这一行抹掉(马上有反馈);S3 那一侧的最终状态由它自己的回执确认
  for (int j = i; j < lsN - 1; ++j) {
    memcpy(lsSs[j], lsSs[j + 1], sizeof(lsSs[0]));
    lsKey[j] = lsKey[j + 1];
  }
  if (lsN) --lsN;
  if (lsPage && lsPage >= lsPages()) --lsPage;
  lsDraw();
}

static void lsDelCb(bool ok) { if (ok) lsDoDel(); }

static void lsTap(int x, int y) {
  if (y < kScHdrH) { lsExit(); return; }                        // 点顶栏 = 退出
  if (y < kLsBot) {                                             // 点列表 -> 先问一句
    const int r = (y - kLsTop) / kScRowH;
    const int i = lsPage * kScRows + r;
    if (r >= 0 && r < kScRows && i >= 0 && i < lsN) {
      lsSel = i;
      char big[24], sub[28];
      snprintf(big, sizeof(big), "%.18s", lsSs[i]);
      snprintf(sub, sizeof(sub), "delete entry #%d ?", i + 1);
      alertConfirm("DELETE", big, sub, "DELETE", "CANCEL", lsDelCb);
    }
    return;
  }
  if (x < 48)  { if (lsPage > 0) { --lsPage; lsDraw(); } return; }
  if (x >= 192){ const int p = lsPages(); if (lsPage + 1 < p) { ++lsPage; lsDraw(); } return; }
  lsExit();                                                     // 底栏中间 = 返回
}

// 主循环每圈调:等 S3 名单的超时兜底 + 按需重绘
static void lsPoll() {
  if (!lsOpen) return;
  if (lsWait && millis() - lsT0 > 4000) {                       // 4s 还没回来就收摊
    lsWait = false; lsDirty = true;
    Serial.println("[c3] list: timeout waiting S3 wlist");
  }
  if (lsDirty) { lsDirty = false; lsDraw(); }
}

// ★ v0.9:链路回执统一入口 —— 扫描页与名单页各认领自己关心的行,都不认的才放行去输出卡。
//   为什么需要它:s3link 只认一个 sink,而 v0.9 起有两个页面都要收 S3 的回执。
static bool uiSink(const char* pl) {
  if (scOpen && scLineSink(pl)) return true;
  if (lsOpen && lsLineSink(pl)) return true;
  if (evtOpenFlag && evtLineSink(pl)) return true;    // ★ v1.2.0:「S3 LOG」搬运页
  return false;
}

// ============================================================================
//  PUSH(★ v0.7):把 C3 存的整份 WiFi 列表逐条推给 S3
// ----------------------------------------------------------------------------
//  ★ 为什么做成"每秒一条"的状态机:一条命令在 UART 上是几十字节,S3 收到还要
//    写 NVS。一口气全推会在 UI 线程里卡住好几秒 —— 这是本项目最忌讳的事。
//  ★ 行为说明(用户 2026-10-11 明确):复用 S3 现有的 `wadd`,而 `wadd` 是
//    **存 + 立刻连**。所以推完,S3 会顺手连上**最后推过去的那一条** ——
//    这正是用户要的("存了 SSID 和密码就该去连"),不是副作用。
//  ★ 语义:这是"把工装上的配置同步到产品上",产线上一次动作省去在 S3 上重敲。
// ============================================================================
static bool pushActive = false;
static int  pushIdx    = 0;
static int  pushTot    = 0;

static void pushStart() {
  pushTot = wifi10::count();
  pushIdx = 0;
  if (!pushTot) {
    outPush("  push: C3 has no saved WiFi", CLR_WARN);
    if (mode == MODE_BOARD && !menuOpen) drawOutDirty();   // ★ v1.0.3:只重绘变了的那行
    Serial.println("[c3] push: nothing to push");
    return;
  }
  pushActive = true;
  char b[kOutCols + 1];
  snprintf(b, sizeof(b), "> PUSH C3 -> S3 : %d entr%s", pushTot, pushTot == 1 ? "y" : "ies");
  outPush(b, CLR_ACC);
  if (mode == MODE_BOARD && !menuOpen) drawOutDirty();   // ★ v1.0.3:只重绘变了的那行
  Serial.printf("[c3] push: start, %d entries\n", pushTot);
}

// 挂在每秒的 tick 上:一拍发一条,绝不阻塞
static void pushTick() {
  if (!pushActive) return;
  char b[kOutCols + 1];
  if (pushIdx >= pushTot) {
    pushActive = false;
    // ★ v0.8.6:推完不是就完了 —— 补发 `wtry <N>`,让 S3 从 #1 起挨个试连。
    char t[24];
    snprintf(t, sizeof(t), "wtry %d", pushTot);
    s3link::sendCmd(t);
    snprintf(b, sizeof(b), "  pushed %d/%d -> S3 trying #1", pushTot, pushTot);
    outPush(b, CLR_ACC);
    if (mode == MODE_BOARD && !menuOpen) drawOutDirty();   // ★ v1.0.3:只重绘变了的那行
    Serial.printf("[c3] push: done %d -> sent wtry %d\n", pushTot, pushTot);
    return;
  }
  const int idx = pushIdx++;               // 第 idx 条(0 起算,已推几条)
  // ★ v0.8.6:倒序推。S3 的 insertFront 是"插最前",正着推过去 S3 台账顺序会反,
  //   它以为的 #1 会变成 C3 列表的最后一条。从 C3 的最后一条往回推,顺序才对得上。
  const int i   = pushTot - 1 - idx;
  char p[160];
  // ★ v0.8:改用 `wseed`(只写台账**不连**)。旧版用 `wadd`,而 `wadd` 现在是
  //   "连上才存" —— 10 条一口气推过去会互相打断连接(10 s 超时),最后一条都存不进去。
  //   PUSH 的语义本来就是"把工装上已摸好的台账整表抄到产品上",不是"再连一遍"。
  snprintf(p, sizeof(p), "wseed %s %s", wifi10::ssidAt(i), wifi10::passAt(i));
  snprintf(lastPayload, sizeof(lastPayload), "%s", p);
  s3link::sendCmd(p);
  // ★ v0.10:拆两行(见 outPush2)—— 原来 `  push i/n <ssid>` 挤一行,长 SSID 被裁
  char h[kOutCols + 1], t[kOutCols + 1];
  snprintf(h, sizeof(h), "  push %d/%d", pushIdx, pushTot);
  snprintf(t, sizeof(t), "    %s", wifi10::ssidAt(i));
  outPush2(h, t);
  if (mode == MODE_BOARD && !menuOpen) drawOutDirty();   // ★ v1.0.3:只重绘变了的那两行
}

// ============================================================================
//  菜单:渲染 / 滑入 / 交互
// ============================================================================
static bool menuEnsureSprite() {
  if (menuSpr) return true;
  menuSpr = new TFT_eSprite(&tft);
  if (!menuSpr) return false;
  menuSpr->setColorDepth(16);
  if (!menuSpr->createSprite(SCR_W, kMenuHMax)) {       // ★ v0.8.4:240*200*2 = 96,000 B
    Serial.printf("[c3] ui: menu sprite alloc FAILED -> draw directly (no anim). need %u, free %u, largest %u\n",
                  (unsigned)(SCR_W * kMenuHMax * 2),
                  (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap());
    menuSpr->deleteSprite();
    delete menuSpr;
    menuSpr = nullptr;
    return false;
  }
  menuSpr->setTextDatum(TL_DATUM);
  Serial.printf("[c3] ui: menu sprite 240x%d ok (need %u B, free %u, largest %u)\n",
                kMenuHMax, (unsigned)(SCR_W * kMenuHMax * 2),
                (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap());
  return true;
}

// 把当前层菜单画进 sprite(无 sprite 时直接画屏上,坐标加 oy 偏移)
static void menuRender() {
  int n = 0;
  const Node* list = levelList(menuLevel, &n);
  if (!list || n <= 0) return;

  const bool sp = (menuSpr != nullptr);
  TFT_eSPI&  g  = sp ? (TFT_eSPI&)(*menuSpr) : tft;
  const int  oy = sp ? 0 : menuTopY(n);

  g.fillRect(0, oy, SCR_W, menuCardH(n), CLR_BAR);      // 卡片底(高度随项数)

  // ---- 头部:面包屑(一级显示 "MENU") ----
  g.fillRect(0, oy, SCR_W, kMenuHeadH, CLR_BG);
  g.setTextColor(CLR_ACC, CLR_BG);
  char b[32];
  menuCrumb(b, sizeof(b));
  // ★ v0.8.4(A 方案):字号从"GLCD 放大 2 倍"换成内置 Font2(同为 16px 高,比例字体)。
  //   右侧提示 "select a group" 仍走字号 1(GLCD 8px,14 字符 × 6px = 84px)。
  g.setTextFont(2);
  g.drawString(b, 12, oy + 5);                 // 26 高的头里居中:(26-16)/2 = 5
  if (menuLevel == 0) {
    g.setTextFont(1);
    g.setTextSize(1);
    g.setTextColor(CLR_DIM, CLR_BG);
    g.drawString("select a group", 150, oy + 9);   // 中线与字号 2 对齐(5+8 = 9+4)
  }
  g.drawFastHLine(0, oy + kMenuHeadH - 1, SCR_W, CLR_LINE);

  // ---- 列表 ----
  g.setTextFont(2);                                  // ★ v0.8.4:内置 Font2 比例字(16px 高)
  for (int i = 0; i < n; ++i) {
    const int      ry  = oy + kMenuHeadH + kMenuPad + i * kMenuRowH;
    const Node&    nd  = list[i];
    const bool     sel = (i == menuSel);
    const uint16_t bg  = sel ? CLR_SEL : CLR_BAR;

    g.fillRect(0, ry, SCR_W, kMenuRowH - 2, bg);
    if (sel) g.fillRect(0, ry, 3, kMenuRowH - 2, CLR_SELB);   // 左侧 3px 青条
    g.setTextColor(sel ? CLR_FG : CLR_DIM, bg);
    g.drawString(nd.label, 14, ry + 5);                    // ★ v0.8.4:26 高条里居中 (26-16)/2

    if (nd.kind == IK_SUB) {                                  // 行尾 > = 还有下一级
      g.setTextColor(CLR_DIM, bg);
      g.drawString(">", SCR_W - 12 - g.textWidth(">"), ry + 5);        // ★ v0.8.4:右对齐现算
    } else if (nd.kind == IK_KEYS) {                          // 橙字 keys = 要键盘(P2)
      g.setTextColor(CLR_WARN, bg);
      g.drawString("keys", SCR_W - 12 - g.textWidth("keys"), ry + 5);  // ★ v0.8.4:同上
    }
  }
  // ★ v0.8.4:降级路径(g==tft)下 Font2 会留在 tft 上,归位,免得状态行串味。
  if (!sp) { g.setTextFont(1); g.setTextSize(1); }
}

// 渲染 + 上屏:animate=true 从屏底滑入(8 帧)。
// underClean = "菜单之外那部分屏现在就是命令板的最新内容,不需要铺底"。
//   只有从命令板点 MENU 进来才是 true;层间下钻/退回也传 true(它们各自只补需要的带);
//   从扫描页/键盘页经串口 `z` 打开则是 false。
static void menuDraw(bool animate, bool underClean = true) {
  int n = 0;
  if (!levelList(menuLevel, &n) || n <= 0) return;

  const int h      = menuCardH(n);
  const int newTop = menuTopY(n);
  const int oldTop = SCR_H - menuCoveredH;      // menuCoveredH==0 -> 320,即"屏上没有旧卡"

  // ★★ v1.0.2(2026-10-15,用户报"点 MENU 弹出时整屏闪一下"):原来这一行是
  //      if (animate || h < menuCoveredH) repaintUnderMenu();
  //   而 `animate` 在**每次打开菜单**时都为真 ⇒ 每次都先整屏抹成背景色
  //   (240x296x2 = 142,080 B;C3 屏 SPI = 20 MHz ⇒ **约 57 ms 屏上什么都没有**),
  //   再逐元素把命令板画回来(约 43 ms)。合计约 100 ms 的中间态 = 肉眼看到的"闪一下"。
  //   (退出菜单之所以好看,是因为 menuSlideOut() 把**同一个** repaintUnderMenu()
  //    放在 setViewport 裁剪下**逐带**调用 —— 每带只有 ~4 ms,看不见。)
  //
  //   现在按"**到底哪里需要补**"分三种情形:
  //     ① 底下不是命令板        -> 整块铺一次(降级路径,与改动前一致)
  //     ② 新卡比旧卡**矮**      -> 只补 [oldTop, newTop) 那一条带
  //        (那段旧卡盖过、新卡盖不到,不补就会留上一张卡的残影)
  //     ③ 打开菜单 / 新卡变高或等高 -> **一个字节都不写**
  //        · 打开:底下本来就是命令板,而 loop() 一直在增量刷它,这样重绘是纯重复劳动;
  //        · 变高/等高:新卡完全盖得住旧卡(两者底边都贴屏底),旧像素露不出来。
  if (!underClean) {
    repaintUnderMenu();
  } else if (newTop > oldTop) {
    // ★ 下界要**盖过输出卡底边**(kCardY+kCardH = 228),不能就用 newTop:
    //   多补出来的 [newTop, 228) 随后会被新卡盖住(新卡占 [newTop, 320)),
    //   动画期间露出来的也是正确的命令板内容,没有任何代价;
    //   而好处是能让 card() 的下圆角(222..228)**整个落在带内** ——
    //   否则 fillRoundRect 的 fillCircleHelper 不吃 viewport,会把它**越界**画到
    //   屏上此刻正压着的旧菜单卡上(选中行底色 CLR_SEL 比卡片底色 CLR_BAR 亮,
    //   四个小弧是看得见的)。
    const int bandBot = (newTop > kCardY + kCardH) ? newTop : (kCardY + kCardH);
    repaintUnderMenuBand(oldTop, bandBot);
  }
  menuCoveredH = h;

  if (!menuSpr) {                       // 降级:直接画屏,无动画(功能不减)
    menuRender();
    menuBusyUntil = millis() + 120;
    return;
  }

  menuRender();
  const int y1 = menuTopY(n);
  if (!animate) {
    menuSpr->pushSprite(0, y1, 0, 0, SCR_W, h);
    menuBusyUntil = millis() + 120;
    return;
  }
  // 滑入:y 从屏底 320 逐帧升到 y1,只推卡片那块(h 行)。
  // 卡片之外的区域上面已经**按需要**补好了(打开菜单时压根不用补 —— 底下本来就是
  // 命令板),一路露出来就是对的。
  const int y0 = SCR_H, N = 8;
  for (int k = 1; k <= N; ++k) {
    const int y = y0 + (y1 - y0) * k / N;
    menuSpr->pushSprite(0, y, 0, 0, SCR_W, h);
    yield();                             // 喂看门狗 + 让串口有机会收发
  }
  menuBusyUntil = millis() + 120;
}

// ★ v0.8.7:菜单**下滑出屏**(与滑入对称的 8 帧)。只给 closeMenu() 用。
//   ⚠ 调它时 menuOpen 必须还是 true(要按当前层算卡片高);滑完由 closeMenu 收尾。
//   ★ v0.8.8:改「逐帧补带」。v0.8.7 是滑出前把底下整块铺一次 —— 但滑出的压帧区域
//     **一帧比一帧小**,每帧腾出来的上面那条带没有任何帧去补,留下的是上一帧卡片的
//     头部 26 行(带面包屑文字),整块被抹成横条残影;最后一帧 y=SCR_H 完全在屏外,
//     push 被裁剪什么都没画 ⇒ 残影就停在那。现改为:每帧先把「卡片刚腾出来的那条带」
//     按命令板重绘(tft.setViewport 裁剪,只写带内),再把卡片压到新位置 —— 滑完全程,
//     命令板正好被逐带补完,零残影。
//     (TFT_eSPI 2.5.43 的 fillRect / drawChar / pushImage 均吃 viewport 裁剪,已核对
//      库源码;setViewport 第 5 参 false = 保持绝对坐标,只裁剪不平移。)
//   返回值:true = 真做了滑出(且底下已逐带重绘好);false = 走不了动画(降级),调用方自己补重绘。
static bool menuSlideOut() {
  if (!menuSpr || !menuOpen) return false;
  int n = 0;
  if (!levelList(menuLevel, &n) || n <= 0) return false;

  const int h  = menuCardH(n);
  const int y1 = menuTopY(n);
  const int N  = 8;
  int yPrev = y1;
  for (int k = 1; k <= N; ++k) {
    const int y = y1 + (SCR_H - y1) * k / N;
    // ① 卡片刚腾出来的那条带 [yPrev, y) -> 按命令板重绘(裁剪在带内,带外一字节不写)
    if (y > yPrev) {
      tft.setViewport(0, yPrev, SCR_W, y - yPrev, false);
      repaintUnderMenu();
      tft.resetViewport();
    }
    // ② 卡片压到新位置(最后一帧 y=SCR_H 在屏外,库会整块裁掉,直接跳过)
    if (y < SCR_H) menuSpr->pushSprite(0, y, 0, 0, SCR_W, h);
    yPrev = y;
    yield();
  }
  menuBusyUntil = millis() + 120;
  return true;
}

static void openMenu() {
  menuOpen    = true;
  menuLevel   = 0;
  menuSel     = 0;
  menuPath[0] = menuPath[1] = 0;
  drawWifiIcon(CLR_BAR);                 // 顶栏图标确保还在

  menuEnsureSprite();
  // ★ v1.0.2:显式告诉 menuDraw "屏上菜单之外现在是不是命令板"。
  //   从命令板点 MENU 进来时(mode == MODE_BOARD)屏上已是最新命令板 ——
  //   再铺一次纯属重复劳动,也正是"整屏闪一下"的来源 ⇒ 跳过。
  //   而 `dbgMenu()` 是串口调试钩子,可以在**任何页面上**调 openMenu();
  //   那种情况底下不是命令板,必须整块铺回去。
  //   ⚠ 用**显式判据**,不拿 `menuCoveredH == 0` 去推 —— 那正是 v0.8.7 注释里
  //     说的"侥幸成立"。
  const bool underClean = (mode == MODE_BOARD);
  menuDraw(true, underClean);
  Serial.printf("[c3] ui: menu opened (level 1, %d groups, under=%s)\n",
                kGroupN, underClean ? "board" : "other");
}

static void closeMenu() {
  if (!menuOpen) return;
  // ★ v0.8.7:先做下滑出屏动画(与打开时的滑入对称)。
  //   menuSlideOut() 内部已经把底下的命令板整块重绘好了 ⇒ 做了动画就不用再补一次;
  //   只有走不了动画(没有 sprite / 列表为空)时才退回"直接重绘"的老路。
  const bool slid = menuSlideOut();
  menuOpen  = false;
  menuLevel = 0;
  menuSel   = 0;
  if (menuCoveredH && !slid) {           // 降级路径:不读屏 -> 按内部状态把整块重绘
    repaintUnderMenu();
  }
  menuCoveredH = 0;
  // ★ v1.0.3:菜单**开着**的时候进来的回执/告警,只是 push 进了行缓存(outPush),
  //   没有上屏 —— 那几条路径全都带 `!menuOpen` 判断。而滑出动画里的 drawOutputCard()
  //   是**裁剪**着画的,只补了"卡片刚腾出来的那条带";输出卡上半截
  //   ([kCardY, 一级菜单顶边 120)) 根本不在带内,那几行的污点补不掉。
  //   ⇒ 在这里统一收口:菜单一关,把还欠着的行一次补齐。
  //   (没有脏行时 drawOutDirty() 直接 return,一个字节都不写 ⇒ 不给"关闭菜单"添闪。)
  if (mode == MODE_BOARD) drawOutDirty();
  // ★ v0.8.3(B 方案):重绘完就把那块 sprite(★ v0.8.4:96,000 B)还回堆。
  //   安全性:菜单与键盘互斥(关菜单之后才开键盘),而整块重绘已经把 sprite 的像素
  //   "落到屏上"了 ⇒ sprite 此刻已无用。连 WiFi 正是从菜单里触发的,
  //   这一下等于把 ~96 KB 让给 WiFi 栈。
  menuFreeSprite("closed");
  menuBusyUntil = millis() + 120;
  Serial.println("[c3] ui: menu closed");
}

// 菜单项执行(先声明,下面函数互调)
static void menuExec();
// enterCal 定义在本命名空间后部,这里先声明(C3 TOUCH 项要用)
inline void enterCal();

static void menuTap(int x, int y) {
  (void)x;
  int n = 0;
  if (!levelList(menuLevel, &n) || n <= 0) { closeMenu(); return; }
  const int top = menuTopY(n);                       // 顶边随项数变,底边恒贴屏底

  if (y < top) { closeMenu(); return; }              // 点菜单外 -> 全收起

  const int ry = y - top;
  if (ry < kMenuHeadH) {                             // 点头部 -> 返回上一级
    if (menuLevel > 0) {
      menuLevel--;
      menuSel = menuPath[menuLevel];
      menuDraw(true);                                // ★ v0.8.7:退回上级也走滑入(以前是瞬切)
      Serial.printf("[c3] ui: menu back (level %d)\n", menuLevel + 1);
    }
    return;
  }

  const int idx = (ry - (kMenuHeadH + kMenuPad)) / kMenuRowH;
  if (idx < 0 || idx >= n) return;
  menuSel = idx;
  menuExec();
}

// ★★ v0.9(2026-10-14):`c3WifiList()` / `c3WifiConn()` 已删。
//   · LIST 菜单项撤掉 —— 用户:"专门把存过的热点列出来看没有意义,既然存过那肯定
//     是连上去过、而且知道密码。"
//   · CONNECT 菜单项撤掉 —— 它只会闷头连名单里的 #1,用户"这个可以删掉我们不用"。
//   现在:名单只在 **DEL 打开的名单页**里看得见(listDraw,而且点一条就删);
//         "连某一条"只在 **扫描页点选** 里发生(scPick),而且带密码直连。

// 菜单项执行:
//   IK_SUB   -> 下钻一层(卡片自适应变矮,先补齐露出来的带)
//   IK_KEYS  -> 回显"要键盘"(P2 才做)
//   IK_LOCAL -> C3 本机真执行
//   IK_CMD   -> 发给 S3 的键(本版只回显;P3 链路通了才真发)
static void menuExec() {
  int n = 0;
  const Node* list = levelList(menuLevel, &n);
  if (!list || menuSel < 0 || menuSel >= n) { closeMenu(); return; }

  // ★★ 先把条目与路径存成局部量,再动菜单状态! ★★
  //    closeMenu() 会把 menuLevel 复位;若之后还按下标取表就是越界读
  //    (2026-10-11 实测:曾因此在 snprintf 里踩 NULL -> Load access fault 重启)。
  const Node nd = list[menuSel];
  char       trail[48];
  menuTrail(trail, sizeof(trail));

  if (nd.kind == IK_SUB) {                           // ---- 还有下一级 ----
    menuPath[menuLevel] = menuSel;
    menuLevel++;
    menuSel = 0;
    Serial.printf("[c3] ui: menu -> %s (level %d)\n", trail, menuLevel + 1);
    menuDraw(true);                                  // ★ v0.8.7:下钻也走滑入(以前是瞬切)
    return;
  }

  char b[kOutCols + 1];
  snprintf(b, sizeof(b), "> %s", trail);
  outPush(b, CLR_ACC);

  if (nd.kind == IK_KEYS) {                          // ---- 要键盘输入 -> 打开虚拟键盘(P2) ----
    const char* a   = (nd.arg && nd.arg[0]) ? nd.arg : "add";
    const bool  add = (a[0] == 'a');
    const bool  c3  = (strstr(trail, "C3") != nullptr);   // WIFI 才分 S3 / C3
    closeMenu();                                     // 重绘会带上刚写入的那行 banner
    Serial.printf("[c3] menu: %s -> keyboard (%s)\n", trail, add ? "ADD" : "DEL");
    enterKb(c3, add, trail);
    return;
  }

  if (nd.kind == IK_LOCAL) {                         // ---- C3 本机真执行 ----
    const char* a = nd.arg ? nd.arg : "";
    if (!strcmp(a, "del")) {                         // ★ v0.9:打开名单页(里面点一条删一条)
      const bool c3 = (strstr(trail, "C3") != nullptr);
      closeMenu();
      Serial.printf("[c3] menu: %s -> del page (%s)\n", trail, c3 ? "C3" : "S3");
      listStart(!c3);                                // remote = !c3
      return;
    } else if (!strcmp(a, "c3scan")) {               // ★ v0.7:用 C3 自己的射频扫
      closeMenu();
      Serial.printf("[c3] menu: %s -> scan (C3 radio)\n", trail);
      scStart(false);
      return;
    } else if (!strcmp(a, "s3scan")) {               // ★ v0.7:让 S3 自己扫
      closeMenu();
      Serial.printf("[c3] menu: %s -> scan (S3 radio)\n", trail);
      scStart(true);
      return;
    } else if (!strcmp(a, "push")) {                 // ★ v0.7:C3 的列表推给 S3
      closeMenu();
      Serial.printf("[c3] menu: %s -> push\n", trail);
      pushStart();
      return;
    } else if (!strcmp(a, "cal")) {                  // 跳本机校准屏
      Serial.printf("[c3] menu: %s -> calibration screen\n", trail);
      closeMenu();
      enterCal();                                    // 不返回
      return;
    } else {
      outPush("  unknown local action", CLR_ERR);
    }
    closeMenu();
    Serial.printf("[c3] menu: %s -> local '%s'\n", trail, a);
    return;
  }

  // ---- 发给 S3(P3:真发;回执由 s3link 收上来,显示成 [s3] 行) ----
  const char* payload = (nd.arg && nd.arg[0]) ? nd.arg : "";
  closeMenu();                                       // 重绘带上 banner
  if (payload[0]) {
    snprintf(lastPayload, sizeof(lastPayload), "%s", payload);   // 给 RESEND 键留一份
    s3link::sendCmd(payload);
    // ★ v1.2.0:LOG('v')**双动作** —— ①让 S3 切日志页(上面这条 'v');
    //   ②同时把 S3 的第三/四界面**搬回本机**(列表 + 翻页 + 统计 + 点行看详情)。
    //   S3 侧配了 evtlist/evtdet/evtstat 三个链路命令(协议 P2)。
    if (!strcmp(payload, "v")) { evtOpen(); return; }
  }
  // ★ v0.10 两处收口(任务单 §D1/§D2):
  //   ① **空 payload 不再报链路问题** —— 命令根本没发出去,却跟着报 "S3 silent"
  //      属于"什么都没发就说错了"。挪进 `payload[0]` 判据内。
  //   ② 文案:原句 `  (S3 silent so far - check wiring)` 前面带两个空格缩进(35 字符),
  //      超过输出卡 34 列,尾部会被截。★ v1.0.3 用户定:**去掉缩进用回原句** ——
  //      `(S3 silent so far - check wiring)` = 33 字符,**正好放得下**。
  //      ★ 保留 `check wiring` 字样 —— 心跳上线后这句只在"链路真死"时才触发,
  //        此时指向接线是合理的归因。
  //   ⚠ v1.0.3 起**链路一断、tick() 那边就已经自动推过一条同样的告警**(见 tick);
  //     这里这条的语义不同 —— 它是"你刚发的这条命令没送到"的即时确认,跟着你这次
  //      操作走。两条都留:一条报链路状态,一条报这次发送的结果。
  if (payload[0] && !s3link::ok()) outPush("(S3 silent so far - check wiring)", CLR_WARN);
  drawOutDirty();
  Serial.printf("[c3] menu: %s -> link '%s'\n", trail, payload);
}

// 调试钩子:直接按索引跑一个菜单项(串口 `z <lvl> <p0> <p1> <sel>`)。
// 用途:手上没空点屏(或远程)时验证菜单链路 —— 它走的和手指点菜单是**同一个
// menuExec()**,所以这里通了等价于"点下去不会崩"。
//
// ★★ `z <lvl> <p0> <p1> <sel>` 的准确语义(2026-10-16 定稿 —— 这行注释以前是 v0.7 的
//    过时下标,我连着两轮巡场都照它写、连着两轮写错层,把"没执行"误读成"功能不工作"):
//      `lvl` = **menuLevel**,即"要执行的是第几层的项"(0-based);
//      `p0`  = 一级下标(只在 lvl>=1 时有意义);  `p1` = 二级下标(只在 lvl>=2 时有意义);
//      `sel` = **要点的那个项在它自己那一层的下标** ← 真正的"选谁"是它,不是 p1。
//    ⇒ 一级项都是 IK_SUB(点了只下钻),所以:
//       `z 0 <哪一组> 0 0`  = 展开某一组(进二级),不执行任何命令;
//       `z 1 <p0> 0 <sel>`  = 执行 `kGroups[p0]` 下第 sel 项(叶子在这一层执行);
//       `z 2 <p0> <p1> <sel>` = 执行 `kGroups[p0].kids[p1]` 下第 sel 项(只用于 SUB 项,
//                               如 SYSTEM/TOUCH、WIFI/S3 —— 叶子没有第三层)。
//    ★ 当前菜单(v1.1.1,一级 4 组)常用速查:
//       BASE   = 0: SET BASE `z 1 0 0 0` / CLEAR BASE `z 1 0 0 1`   ← v1.2.5 前叫 JUDGE
//                   ★ v1.2.4:INJECT(`z 1 0 0 2`)已删,别再照旧脚本发这一条
//       VIEW   = 1: MIRROR   `z 1 1 0 0` / CHARTS     `z 1 1 0 1` / LOG    `z 1 1 0 2`
//       SYSTEM = 2: STATUS   `z 1 2 0 0` / PPS        `z 1 2 0 1` / TEMP-RH `z 1 2 0 2`
//                   TOUCH(p1=3): S3 TOUCH `z 2 2 3 0` · C3 TOUCH `z 2 2 3 1`
//       WIFI   = 3: S3(p1=0: SCAN/ADD/DEL/PUSH) / C3(p1=1: SCAN/ADD/DEL)
//    ⚠ SIM 那一支(原 `z 2 2 4 x`)在 v1.1.1 已随模拟信源一起删除,别再照旧脚本发。
//   ⚠ 叶子给 lvl=2 不是"执行失败",而是 `levelList(2)` 那里根本没有下一层 ⇒
//     照 menuExec 的兜底静默 closeMenu(实测 `z 2 0 0 0` 就这样,别再当成 bug 查)。
inline void dbgMenu(int lvl, int p0, int p1, int sel) {
  if (!menuOpen) openMenu();
  menuLevel   = (lvl < 0) ? 0 : (lvl > 2 ? 2 : lvl);
  // ★ v1.1.0:入参**必须夹紧** —— 方案 A 把一级从 5 组减到 4 组之后,照旧习惯发的
  //   `z 0 4 0 0` 就成了越界索引。实测(2026-10-16 实机)menuTrail() 会去读
  //   `kGroups[4].label` —— 那是**数组外的相邻静态数据**,于是日志里冒出一个
  //   根本不存在的菜单项 `'S3 / JUDGE'`,看着像"菜单表被改坏了"。
  //   `menuExec()` 只夹 menuSel(越界就 closeMenu),**不夹 menuPath** ⇒ 这里不管就没人管。
  //   正常按键路径由 levelCount 夹紧、不受影响 —— 这一步纯粹给"手发的调试参数"兜底。
  menuPath[0] = (p0 < 0) ? 0 : (p0 >= kGroupN ? kGroupN - 1 : p0);
  if (menuLevel >= 2) {
    const int n1 = kGroups[menuPath[0]].nkids;
    menuPath[1] = (p1 < 0) ? 0 : (p1 >= n1 ? n1 - 1 : p1);
  }
  menuSel     = sel;
  char trail[48];
  menuTrail(trail, sizeof(trail));
  Serial.printf("[c3] dbg: menu exec '%s' (level %d)\n", trail, menuLevel + 1);
  menuExec();
}

// ============================================================================
//  校准屏 MODE_CAL(用户 2026-10-11 定稿:布局/字号/配色不动)
// ============================================================================
constexpr int RECAL_X = 148, RECAL_Y = 254, RECAL_W = 76, RECAL_H = 20;
// ★ v0.5 新增:BOARD = 返回命令板(以前从菜单的 C3 TOUCH 跳过来,只能回电脑敲 t 才出得去)
constexpr int BOARD_X = 66,  BOARD_Y = 254, BOARD_W = 76, BOARD_H = 20;

static void drawWifiCard() {
  char b[48];
  wifi10::statusText(b, sizeof(b));
  uint16_t col = CLR_FG;
  if (!strncmp(b, "OK ", 3))       col = CLR_ACC;
  else if (!strncmp(b, "FAIL", 4)) col = CLR_ERR;
  tft.fillRect(18, 144, 204, 12, CLR_BAR);
  tft.setTextSize(1);
  tft.setTextColor(col, CLR_BAR);
  tft.drawString(b, 18, 144);

  snprintf(b, sizeof(b), "saved %d/10", wifi10::count());
  tft.fillRect(18, 164, 204, 12, CLR_BAR);
  tft.setTextColor(CLR_DIM, CLR_BAR);
  tft.drawString(b, 18, 164);
}

static void drawRawLine() {
  char b[40];
  snprintf(b, sizeof(b), "raw %u,%u z=%u",
           (unsigned)rawX, (unsigned)rawY, (unsigned)rawZ);
  tft.fillRect(18, 242, 126, 12, CLR_BAR);
  tft.setTextSize(1);
  tft.setTextColor(CLR_DIM, CLR_BAR);
  tft.drawString(b, 18, 242);
}

static void drawCalCard() {
  card(8, 208, 224, 76);
  tft.setTextSize(1);
  tft.setTextColor(CLR_ACC, CLR_BAR);
  tft.drawString("CAL", 18, 216);

  const uint16_t* c = touch::cal();
  char b[40];
  snprintf(b, sizeof(b), "%u,%u,%u,%u,%u",
           (unsigned)c[0], (unsigned)c[1], (unsigned)c[2],
           (unsigned)c[3], (unsigned)c[4]);
  tft.fillRect(18, 230, 204, 12, CLR_BAR);
  tft.setTextColor(CLR_DIM, CLR_BAR);
  tft.drawString(b, 18, 230);

  drawRawLine();

  // RECAL 按钮:点它 == 串口 'r' + 复位
  tft.fillRoundRect(RECAL_X, RECAL_Y, RECAL_W, RECAL_H, 4, CLR_ERR);
  tft.setTextColor(TFT_WHITE, CLR_ERR);
  tft.drawString("RECAL", RECAL_X + 23, RECAL_Y + 6);

  // BOARD 按钮(v0.5):回命令板
  tft.fillRoundRect(BOARD_X, BOARD_Y, BOARD_W, BOARD_H, 4, CLR_ACC);
  tft.setTextColor(CLR_BG, CLR_ACC);
  tft.drawString("BOARD", BOARD_X + 23, BOARD_Y + 6);
}

static void drawCalScreen() {
  mode      = MODE_CAL;
  menuOpen  = false;
  menuLevel = 0;
  menuSel   = 0;
  menuCoveredH = 0;
  menuFreeSprite("cal");                         // ★ v0.8.3:同上
  lastX = lastY = -1;
  tft.fillScreen(CLR_BG);
  drawTopBar();

  // 卡 1:触摸坐标(用户认可的原样)
  card(8, 32, 224, 78);
  tft.setTextSize(1);
  tft.setTextColor(CLR_ACC, CLR_BAR);
  tft.drawString("TOUCH TEST", 18, 42);
  tft.setTextColor(CLR_DIM, CLR_BAR);
  tft.drawString("tap anywhere on screen", 18, 88);

  // 卡 2:WiFi 标题(内容由 drawWifiCard 填)
  card(8, 120, 224, 78);
  tft.setTextColor(CLR_ACC, CLR_BAR);
  tft.drawString("WIFI", 18, 130);

  drawCalCard();

  // 底条(两行)
  tft.fillRect(0, 288, SCR_W, SCR_H - 288, CLR_BAR);
  tft.fillRect(0, 288, SCR_W, 1, CLR_LINE);
  tft.setTextColor(CLR_DIM, CLR_BAR);
  tft.drawString("BOARD = back   RECAL = redo cal", 6, 294);
  tft.drawString("t = command board <-> this screen", 6, 306);

  drawWifiCard();
}

// ============================================================================
//  触摸路由
// ============================================================================
static void drawCross(int x, int y, uint16_t c) {
  tft.drawFastHLine(x - 5, y, 11, c);
  tft.drawFastVLine(x, y - 5, 11, c);
}

static void calTap(int x, int y) {
  // 两个按钮优先判定(用已校准坐标)
  if (x >= BOARD_X && x < BOARD_X + BOARD_W &&
      y >= BOARD_Y && y < BOARD_Y + BOARD_H) {
    Serial.printf("[c3] touch: BOARD pressed (x=%d y=%d)\n", x, y);
    enterBoard();
    return;
  }
  if (x >= RECAL_X && x < RECAL_X + RECAL_W &&
      y >= RECAL_Y && y < RECAL_Y + RECAL_H) {
    Serial.printf("[c3] touch: RECAL pressed (x=%d y=%d)\n", x, y);
    touch::forceRecal();                         // 不返回:内部 ESP.restart()
    return;
  }

  // 顺带读一次未校准原始值,填 CAL 卡
  rawX = rawY = rawZ = 0;
  tft.getTouchRaw(&rawX, &rawY);
  rawZ = tft.getTouchRawZ();

  if (lastX >= 0) drawCross(lastX, lastY, CLR_BG);   // 擦掉上一个光标
  drawCross(x, y, TFT_CYAN);
  lastX = x; lastY = y;

  char b[24];
  snprintf(b, sizeof(b), "x=%3d  y=%3d", x, y);
  tft.fillRect(18, 54, 200, 24, CLR_BAR);
  tft.setTextSize(2);
  tft.setTextColor(CLR_FG, CLR_BAR);
  tft.drawString(b, 18, 56);

  drawRawLine();
  Serial.printf("[c3] touch: x=%d y=%d | raw %u,%u z=%u\n",
                x, y, (unsigned)rawX, (unsigned)rawY, (unsigned)rawZ);
}

// RESEND 键(v0.5 起真有用了;★ v1.2.1 标签 SEND -> RESEND,字面与行为对齐):
// 把**最近一条**发往 S3 的命令重发一遍。
// 现场意义:台架那边被误关成 mirror off、或者想再打一次 status,不用重新翻菜单。
static void doSend() {
  if (!lastPayload[0]) {
    outPush("> RESEND: nothing sent yet", CLR_WARN);
  } else {
    char b[kOutCols + 1];
    snprintf(b, sizeof(b), "> resend %s", lastPayload);
    outPush(b, CLR_DIM);
    s3link::sendCmd(lastPayload);
  }
  drawOutDirty();                                   // ★ v1.0.3:增量重绘(见 drawOutDirty)
  Serial.printf("[c3] ui: RESEND pressed (resend '%s')\n", lastPayload);
}
static void doClear() {
  outN = 0;
  outDirtyFrom = 0;                                 // ★ v1.0.3:全清 ⇒ 14 行都得擦掉
  drawOutDirty();
  Serial.println("[c3] ui: output cleared");
}

static void boardTap(int x, int y) {
  if (y < kKeyY || y >= kKeyY + kKeyH) return;
  for (int i = 0; i < 3; ++i) {
    if (x >= kKeyX[i] && x < kKeyX[i] + kKeyW) {
      Serial.printf("[c3] ui: key %s tapped (x=%d y=%d)\n", kKeyLabel[i], x, y);
      if      (i == 0) openMenu();
      else if (i == 1) doSend();
      else             doClear();
      return;
    }
  }
}

inline void onTap(int x, int y) {
  if (alertOpen)               { alertTap(x, y); return; }  // ★ v0.9:弹窗最优先(模态)
  if (millis() < menuBusyUntil) return;         // 菜单动画期间忽略
  if (kbOpen)                  { kbTap(x, y);   return; }   // 键盘页优先(全屏接管)
  if (mode == MODE_SCAN)       { scTap(x, y);   return; }   // ★ v0.7:扫描屏(全屏接管)
  if (mode == MODE_LIST)       { lsTap(x, y);   return; }   // ★ v0.9:名单页(全屏接管)
  if (mode == MODE_EVT)        { evtTap(x, y);  return; }   // ★ v1.2.0:S3 LOG 搬运页(全屏接管)
  if (menuOpen)                { menuTap(x, y); return; }
  if (mode == MODE_CAL) calTap(x, y);
  else                  boardTap(x, y);
}

// ============================================================================
//  屏间切换 + 周期刷新
// ============================================================================
inline void enterBoard() {
  drawBoardScreen();
  Serial.println("[c3] ui: -> command board");
}
inline void enterCal() {
  drawCalScreen();
  Serial.println("[c3] ui: -> calibration screen");
}
// 串口 't':命令板 <-> 校准屏。若正开着键盘/扫描屏/名单页/弹窗,先把它们收掉(都是临时模态)。
inline void toggle() {
  if (alertOpen)         { alertClose(false); return; }   // ★ v0.9:弹窗先关掉
  if (kbOpen)            { kbCancel();        return; }
  if (mode == MODE_SCAN) { scExit();          return; }   // ★ v0.7
  if (mode == MODE_LIST) { lsExit();          return; }   // ★ v0.9
  if (mode == MODE_CAL) enterBoard();
  else                  enterCal();
}

inline void begin() {
  // ★ 2026-10-15:原来这里**硬编码** "> Command Pad v1.0.0 (release)" —— 版本升到
  //   v1.0.1 后它就漂了(这是开机输出卡的**第一行**,用户一眼能看到)。改成引用 kVer,
  //   以后升版只改 kVer 一处,不会再有第二份账。
  //   注:`kVer` 定义在 `namespace splash` 里(C3 全片只有这一处版本常量),
  //   所以这里要写 `splash::kVer` —— 不限定会编不过。
  char h[40];
  snprintf(h, sizeof(h), "> Command Pad %s (release)", splash::kVer);
  outPush(h, CLR_DIM);
  outPush("> tap MENU / RESEND / CLR", CLR_DIM);
  drawBoardScreen();                             // 开机默认进命令板
}

// ★ v0.9:整屏告警需要两样东西 —— "哪条失败了" + "为什么"。
//   S3 是**分两行**回的(`wifi fail <ssid>` 之后跟一行 `txt fail <原因>`),
//   所以收到第一行先把 SSID 记下,收到第二行才弹窗。
static char failSs[24] = "";
static bool failPend   = false;

// 链路回执行:不管当前在哪一屏都要收(收进输出卡缓存,回到命令板才看得见)
static void drainLink() {
  char l[64];
  for (int n = 0; n < 4 && s3link::popLine(l, sizeof(l)); ++n) {
    char b[kOutCols + 1];
    // ★★ v0.8.9:来源徽标化 —— 来自 S3 的行一律 `[S3] …`,每条占一行(用户 2026-10-13 20:5x 定)。
    // ------------------------------------------------------------------
    //   背景:v0.8.7 把 S3 的**空回显**(`@s ok <原命令>`)降噪丢掉了,用户随即指出
    //   "我发命令过去、它收没收到我不知道" —— **"收到了"和"结果是啥"是两件事**,
    //   不能只留后者。现改为三类分流,全部用 `[S3]` 徽标开头(与 C3 本机行的 `[C3]` 对称):
    //     · `wifi ok/fail/try …` = S3 的连接结果 -> `[S3] wifi ok 192.168.4.40`
    //     · `txt <文本>`         = S3 的**命令结果**(status/温湿度/PPS/台账…),
    //                              由 S3 侧 rptText() 发 -> `[S3] ST sat0/12 hdop7.8 cn0 25`
    //     · 其余(`s` / `t` / `wadd …`) = **收到回执** -> `[S3] OK s`(OK 大写)
    //   ⚠ wscan 的 wsc/wscf/wsce/wscan 仍归 scLineSink(),**不能**上输出卡 ——
    //     否则扫描时那几十行 AP 会把卡刷爆,这四类必须在"回执"分支**之前**挡掉。
    //   注:"@s ok " 前缀与 lastOkMs 在 s3link::poll() 里已经处理过,
    //       所以这里怎么显示都不影响底部那行 LINK ok/-- 的实时性。
    if (!strncmp(l, "wifi ok ", 8) || !strncmp(l, "wifi fail ", 10) ||
        !strncmp(l, "wifi try ", 9)) {
      snprintf(b, sizeof(b), "[S3] %s", l);           // 例: [S3] wifi ok WiFi-A
      // ★ v0.9:记下"哪条失败了" —— 紧随其后的 `txt fail <原因>` 行要靠它凑成整屏告警。
      //   `wifi fail all`(巡台账全挂)不算单条失败,不置位。
      if (!strncmp(l, "wifi fail ", 10)) {
        if (strncmp(l, "wifi fail all", 13) != 0) {
          snprintf(failSs, sizeof(failSs), "%.20s", l + 10);
          failPend = true;
        }
      } else {
        failPend = false;                             // wifi ok / wifi try:把悬着的清掉
      }
    } else if (!strncmp(l, "txt ", 4)) {
      const char* t = l + 4;
      snprintf(b, sizeof(b), "[S3] %s", t);           // S3 的命令结果(短行)
      // ★ v0.9(O3):这一行如果是失败原因(纯 ASCII 短标签)-> 弹整屏告警
      if (!strncmp(t, "fail ", 5) && failPend) {
        failPend = false;
        alertInfo("CONNECT FAILED", t + 5, failSs);
      }
    } else if (!strncmp(l, "wsc ", 4) || !strncmp(l, "wscf", 4) ||
               !strncmp(l, "wsce", 4) || !strncmp(l, "wscan", 5)) {
      continue;                                       // 扫描数据:归扫描页,不上卡
    } else {
      // ★ v0.9:回执**脱敏** —— `wadd`/`wseed` 的回显不再带参数。
      //   起因:scPick 的"带密码直连"会让**每次点选**都发一条 `wadd <ssid> <pass>`,
      //   不脱敏的话每次点一下密码就原样打在屏上。
      const char* show = l;
      if      (!strncmp(l, "wadd ",  5)) show = "wadd";
      else if (!strncmp(l, "wseed ", 6)) show = "wseed";
      snprintf(b, sizeof(b), "[S3] OK %s", show);     // ★ v0.8.9:收到回执(每条一行)
    }
    outPush(b, TFT_CYAN);
    if (mode == MODE_BOARD && !menuOpen) drawOutDirty();   // ★ v1.0.3:只重绘变了的那行
  }
}

// C3 本机 WiFi 状态机事件("WIFI OK <ip>" / "WIFI FAIL <ssid>")
//   ★ v0.8.9:`** ` 改成 `[C3] ` —— 与 S3 的 `[S3] ` 对称,一眼看出这行是谁产生的。
//     起因:原来 `** ` 前缀被**两边共用**(S3 的 WiFi 结果也是 `** S3 …`),分不清来源。
static void drainWifiEvt() {
  char e[48];
  for (int n = 0; n < 4 && wifi10::takeEvent(e, sizeof(e)); ++n) {
    char b[kOutCols + 1];
    snprintf(b, sizeof(b), "[C3] %s", e);
    outPush(b, CLR_ACC);
    if (mode == MODE_BOARD && !menuOpen) drawOutDirty();   // ★ v1.0.3:只重绘变了的那行
  }
}

// 每圈收链路 + 每秒刷一次 WiFi 图标(状态行也顺带刷,链路状态会变)
static void tick() {
  drainLink();                                  // 链路回执每圈都收,别漏
  {
    // ★★ v1.0.3(用户 2026-10-15 定):链路**由通变断**的那一刻,自动往输出卡推一条橙色告警。
    //   在此之前这句只在"点菜单命令、且链路不通"时才出现 —— 链路断了但你不动菜单,
    //   屏上一点提示都没有(只有状态行那行 LINK 由 ok 变成 --),现场不知道该去查线。
    //   用户原话:「C3和S3之间的通信断开了,也就是 link 不是 OK 的时候,显示一条
    //   `(S3 silent so far - check wiring)` …… 仅仅是这个时候出现这条提示」。
    //   · **只在边沿推一次**:断着期间不重复刷屏;链路恢复后再断,会再出现一次。
    //   · `wasOk` 初值 **false** ⇒ 开机时链路本来就还没建立(从没通过),不会误报首条。
    //     "开机就没连上"那件事由开机自检的三行负责(NO LINK (retrying)),不归这里管。
    //   · `s3link::ok()` 是 **5 秒窗口** ⇒ 心跳停约 5 秒才判 silent,与状态行同一判据,不会打架。
    //   · 屏上不在命令板时(扫描页/键盘页/校准屏)只入队不重绘 —— 等回到命令板,
    //     drawBoardScreen() 整屏重绘时那行自然就在了。
    static bool wasOk = false;
    const bool nowOk = s3link::ok();
    if (wasOk && !nowOk) {
      outPush("(S3 silent so far - check wiring)", CLR_WARN);
      if (mode == MODE_BOARD && !menuOpen) drawOutDirty();
      Serial.println("[c3] link: went silent (ok -> --) -> output card warned");
    }
    wasOk = nowOk;
  }
  {
    // ★ v1.1.0:协议号不匹配 -> 输出卡告警**一次**(照抄上面断链告警的边沿做法)。
    //   · 只告警、**不拒绝执行** —— 版本不齐时"命令还能用"比"直接罢工"有用得多
    //     (现场可以先干活,回头再补烧那一块)。
    //   · 文案 25 字符,输出卡 34 列放得下,不用折行。
    //   · 为什么不放在 s3link::poll() 里直接推:`ui::outPush()` 定义在后面(1449 行),
    //     命名空间里调不到 —— 见那边 peerProto 那段的注释。
    if (s3link::takeProtoWarn()) {
      char pb[40];
      snprintf(pb, sizeof pb, "PROTO MISMATCH C3=P%d S3=P%d",
               kProto, s3link::peerProtoNo());
      outPush(pb, CLR_WARN);
      if (mode == MODE_BOARD && !menuOpen) drawOutDirty();
      Serial.printf("[c3] link: proto mismatch (C3=P%d S3=P%d) -> output card warned\n",
                    kProto, s3link::peerProtoNo());
    }
  }
  drainWifiEvt();                               // ★ v0.8:本机"连上才存"的结果
  wifi10::poll();                               // ★ v0.8:开机选网 + 连接超时收口
  scPoll();                                     // ★ v0.7:扫描结果收割(每圈,不阻塞)
  lsPoll();                                     // ★ v0.9:名单页超时兜底 + 按需重绘
  evtPoll();                                    // ★ v1.2.0:S3 LOG 页超时兜底 + 按需重绘
  {
    // ★ v0.9(O3):C3 本机的连接失败 -> 弹整屏告警。
    //   wifi10 那边已经判过"只有用户主动点的那条"才置位(开机巡台账不弹)。
    char ss[24], tg[20];
    if (wifi10::takeAlert(ss, sizeof(ss), tg, sizeof(tg))) {
      alertInfo("CONNECT FAILED", tg, ss);
    }
  }
  if (millis() - lastTick < 1000) return;
  lastTick = millis();
  pushTick();                                   // ★ v0.7:一秒推一条给 S3(状态机)
  // ★★ v0.9(用户 2026-10-14 报的缺陷):凡是"自己有顶栏"的页面都**不刷 WiFi 图标**。
  //   这颗图标占 x≈205~231,而扫描页/名单页的面包屑右对齐到 x=234 ⇒ 两者重叠约 26px,
  //   把面包屑尾巴压花。键盘页(MODE_KB)老早就挡了,扫描页是**漏网的那个** ——
  //   所以以前在扫描页上它会每秒被补画一次,盖都盖不住。
  //   ★ v1.2.1:补 MODE_EVT —— 搬运页顶栏右上角是 NEXT 按钮,图标每秒补画会压花它
  //   (与当年扫描页漏网同一类错:加了新页面,忘了进这支名单)。
  if (mode == MODE_KB || mode == MODE_SCAN ||
      mode == MODE_LIST || mode == MODE_ALERT ||
      mode == MODE_EVT) return;
  drawWifiIcon(CLR_BAR);
  if (mode == MODE_CAL) drawWifiCard();
  else if (mode == MODE_BOARD && !menuOpen) drawStatusLine();
}

} // namespace ui

// ============================================================================
//  串口命令台
// ============================================================================
static void printHelp() {
  Serial.println("[c3] commands: ? = help  |  w add <ssid> <pass> / w seed <ssid> <pass> / w try [n] / w list / w del <n> / w conn [n]");
  Serial.println("[c3]            t = command board <-> calibration screen  |  r = reset touch cal  |  c = print cal  |  s = status");
  Serial.println("[c3] test hooks: k add c3|k del c3|k add s3|k del s3 = open keyboard  |  y <text> = type it ('|' = next field, auto-OK)  |  z <lvl> <p0> <p1> <sel> = run a menu item");
  // ★ 2026-10-15:原来这行把同一对脚号**打印了两遍**(出来是 "RX20 TX20 -> S3 (GPIO20/20)"),
  //   而且标着 UART1 —— 实际是 `Serial1` 显式钉在 C3 的 20/21 上。现在把**对端脚号**也写出来,
  //   现场对线不用再翻文档。
  Serial.printf("[c3] pins: CLK6 MOSI7 MISO4 CS10 DC3 RST1 T_CS0 BLK3V3  |  link: RX%d TX%d (C3 GPIO) <-> S3 GPIO43(TX) / GPIO44(RX)\n",
                s3link::PIN_RX, s3link::PIN_TX);
  // 菜单层次记在这里,免得每次都要点屏翻:
  Serial.println("[c3] menu: WIFI/C3 -> SCAN ADD DEL   |   WIFI/S3 -> SCAN ADD DEL PUSH");
  Serial.println("[c3]            SCAN(C3) 用本机射频扫;SCAN(S3) 发 @k wscan 让 S3 自己扫,结果回本屏");
  Serial.println("[c3]            DEL = 打开名单页,点一条删一条(LIST / CONNECT 菜单项已撤)。");
  Serial.println("[c3]            SCAN 页点一行:名单里有密码 -> 直连;没有(或上次连失败过)-> 弹键盘。");
  Serial.println("[c3] wifi: wadd = 连上(拿到 IP)才存;连不上一个字不存。台账 10 组,新的插最前,满 10 丢最旧。");
  Serial.println("[c3]       PUSH = 逐条 wseed 抄给 S3(只写不连),收尾发 wtry N 让 S3 从 #1 挨个试连,失败自动跳下一条。");
  Serial.println("[c3]       开机不自己扫描 —— 台账非空就按 #1..#n 顺序挨个试(201/202 快失败立即跳下一条);");
  Serial.println("[c3]       w seed <ssid> <pass> = 只写台账不连(与 S3 的 wseed 对齐) | w try [n] = 立刻巡一次台账。");
  Serial.println("[c3]       S3 回传的命令结果以 \"* ...\" 打在输出卡上;\"@s ok xxx\" 那种空回显已降噪不上屏。");
}

static void statusPrint() {
  char b[48], ip[20];
  wifi10::statusText(b, sizeof(b));
  if (WiFi.status() == WL_CONNECTED) {
    const IPAddress a = WiFi.localIP();
    snprintf(ip, sizeof(ip), "%u.%u.%u.%u", a[0], a[1], a[2], a[3]);
  } else {
    snprintf(ip, sizeof(ip), "--");
  }
  Serial.printf("[c3] status: heap=%u  wifi=\"%s\" (saved %d/10)  ip=%s  link ok=%d rx=%u tx=%lu  touch_cal=%d\n",
                (unsigned)ESP.getFreeHeap(), b, wifi10::count(), ip,
                (int)s3link::ok(), (unsigned)s3link::rxBytes(),
                (unsigned long)s3link::txCount(), (int)touch::calibrated);
}

static void handleLine(char* s) {
  while (*s == ' ') s++;
  if (!*s) return;
  switch (s[0]) {
    case '?': printHelp();          break;
    case 'w': wifi10::cmd(s + 1);   break;
    case 'r': touch::resetCal();    break;
    case 'c': touch::printCal();    break;
    case 's': statusPrint();        break;
    case 't': ui::toggle();         break;
    // ---- 调试钩子(不用手指也能把键盘/链路整条跑通) ----
    case 'k': {                     // k add c3 / k del c3 / k add s3 / k del s3
      const char* a = s + 1;
      while (*a == ' ') ++a;
      const bool add = (strncmp(a, "add", 3) == 0);
      const bool c3  = (strstr(a, "c3") != nullptr);
      char trail[24];
      snprintf(trail, sizeof(trail), "WIFI / %s / %s", c3 ? "C3" : "S3", add ? "ADD" : "DEL");
      ui::enterKb(c3, add, trail);
      break;
    }
    case 'y': {                     // y <text>  把 text 敲进键盘并提交
      const char* a = s + 1;
      while (*a == ' ') ++a;
      ui::kbFeed(a);
      break;
    }
    case 'z': {                     // z <lvl> <p0> <p1> <sel>  直接跑一个菜单项
      int lvl = 0, p0 = 0, p1 = 0, sel = 0;
      sscanf(s + 1, "%d %d %d %d", &lvl, &p0, &p1, &sel);
      ui::dbgMenu(lvl, p0, p1, sel);
      break;
    }
    default:  Serial.println("[c3] unknown -- press ? for help"); break;
  }
}

static void readConsole() {
  static char    line[96];
  static uint8_t n = 0;
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\r') continue;
    if (c == '\n') {
      line[n] = 0;
      if (n) handleLine(line);
      n = 0;
    } else if (n < sizeof(line) - 1) {
      line[n++] = c;
    }
  }
}

// ============================================================================
//  setup / loop
// ============================================================================
void setup() {
  Serial.begin(115200);
  delay(400);                       // 原生 USB CDC 枚举需要一点时间

  Serial.println("\n[c3] boot");
  // ★ 2026-10-15:两个实参都显式转 (unsigned) —— 以前只转了 psram 那个,
  //   flash 那个是 `uint32_t`(= `unsigned long`)配 `%u`,编译器报类型不匹配。
  //   ESP32 上两者同宽所以打印一直是对的,但这是一行里"转一个漏一个"的疏忽。
  Serial.printf("[c3] chip=%s rev=%d cores=%d flash=%uKB psram=%u\n",
                ESP.getChipModel(), ESP.getChipRevision(), ESP.getChipCores(),
                (unsigned)(ESP.getFlashChipSize() / 1024), (unsigned)ESP.getPsramSize());
  Serial.printf("[c3] free heap @boot = %u\n", (unsigned)ESP.getFreeHeap());

  // 板载 RGB(WS2812,GPIO8):★ v1.2.6 起由 loop() 里的 neopixelWrite() 协议驱动。
  //   旧的 pinMode/digitalWrite(8) 在这里初始化电平 —— 对 WS2812 无效,且与 RMT 输出
  //   在同一个脚上打架,已删。neopixelWrite() 自管引脚,无需 pinMode。

  tft.init();
  tft.setRotation(0);               // 竖屏,与 S3 主界面同向
  tft.fillScreen(TFT_BLACK);

  touch::begin();                   // 必须在 tft.init() 之后
  wifi10::begin();                  // ★ v0.8:读 NVS 台账 -> 先扫 -> 只连"范围内 ∧ 已存档"里最强的
  s3link::begin();                  // 与 S3 的串口链路(★ v0.10 定型:C3 GPIO20(RX) / GPIO21(TX))

  splash::run();                    // 开机画面 + 自检(v0.11:链路项一直重试到画面结束、且会自我修正)

  ui::begin();                      // 深色命令板主屏(串口 't' 切校准屏)

  // ★ v1.1.0:横幅里的版本号**不再写死** —— 用 `splash::kVer` 拼。
  //   同一个坑踩过两次:v1.0.1 时是输出卡首行硬编码 `v1.0.0`(漂到 v1.0.1 才发现),
  //   这次改 kVer 又把本行漏了(板上实跑 v1.1.0,横幅还报 v1.0.3 —— 2026-10-16 实机抓到)。
  //   写死必漂 ⇒ 一律占位符。改完务必扫:
  //     grep -nE '"[^"]*v[01]\.[0-9]' firmware/*/*.ino
  //   除 kVer 与本行(已是 %s)外,应当只剩注释里的历史版本号。
  Serial.printf("[c3] boot ok (%s: 板载 RGB(GPIO8)呼吸灯 —— 链路活=青蓝呼吸 2s/断=红常亮,"
                "neopixelWrite 内置 RMT 零新依赖);协议 **P2**(与 S3 同代)",
                splash::kVer);
  Serial.println();
  printHelp();
}

void loop() {
  int x = 0, y = 0;
  if (touch::poll(&x, &y)) {
    ui::onTap(x, y);
  }

  s3link::poll();
  readConsole();
  ui::tick();

  // ---- 链路心跳(★ v0.10,任务单 §C2):每 2 s 发一条 `@k ping` ----
  //   为什么必须发:链路平时**零流量** ⇒ `s3link::ok()`(5 秒窗口)在"链路完全正常、
  //     只是闲了 5 秒"时**必然为假** ⇒ 屏上 `check wiring` 必然误报、开机自检必然 NO LINK。
  //   心跳一上,ok() 变成真正可信的链路存活指示;用户要的"发送前先看通信"也随之成立
  //     —— 它只是读一个**已经在维护**的状态,零额外延迟、零探测开销。
  //   ★ S3 侧**零改动**:`@k ping` → consoleKey('p') → `case 'p'` 已于 2026-10-11 删除
  //     → `default: break`(零副作用)→ 回 `@s ok ping`。
  //   ⚠ 不能用 w 开头的词(会进 wifi10::cmd());也不能发空 payload(S3 直接丢、不回执)。
  //   ⚠ PUSH 传输中不插心跳,免得与"多行台账"在 S3 侧交错。
  static uint32_t tPing = 0;
  if (millis() - tPing >= 2000) {
    tPing = millis();
    if (!ui::pushActive) s3link::sendPing();        // ★ v1.1.0:心跳带协议号(见 sendPing)
                                                    // ⚠ 必须带 ui:: / s3link:: 限定 ——
                                                    //   这里是全局 loop(),不带会编译不过
  }

  // 板载 RGB 呼吸灯(★ v1.2.6):链路活 = 青蓝呼吸(周期 2 s,与上方 @k ping 同拍);
  //   链路断 = 红常亮。20 ms 步进(50 fps 足够顺滑,RMT 负载可忽略)。
  //   ★ lastOk 初值 true ⇒ 开机若链路未通,先亮一次红(与屏上 `--` 同刻出现),
  //     首条合法回执把它切进青蓝呼吸。
  static uint32_t tLed  = 0;
  static bool     lastOk = true;
  if (millis() - tLed >= 20) {
    tLed  = millis();
    bool ok = s3link::ok();
    if (ok) {
      // 正弦呼吸:0.05..1.0,cos 起止都在谷值,亮暗过渡自然无跳变
      float ph = (float)(millis() % 2000) * 0.00031415927f;   // 2π / 2000ms
      float s  = 0.05f + 0.95f * (0.5f - 0.5f * cosf(ph));
      neopixelWrite(8, 0, (uint8_t)(160.0f * s), (uint8_t)(255.0f * s));
    } else if (lastOk) {
      neopixelWrite(8, 255, 0, 0);    // 刚断:只写一次红,之后保持
    }
    lastOk = ok;
  }

  delay(5);
}

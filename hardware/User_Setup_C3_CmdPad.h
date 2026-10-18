// ============================================================================
//  ★★★ 这是 `Arduino/libraries/TFT_eSPI/User_Setups/Setup_C3_CmdPad.h` 的项目内权威副本。
//      库里那份**不在项目目录**,换电脑 / 重装 TFT_eSPI 就丢 —— 丢了以后 C3 详情页的
//      事件类型大字(4 号字)**编译照过、屏上一个像素都不画**,而且没有任何报错。
//
//      换机器后第一件事:
//          python tools/check_tft_fonts.py --restore     # 把本文件覆盖回库目录
//          python tools/check_tft_fonts.py               # 复验,应全 [OK]
//
//      ⚠ 改本文件要同步改库里那份(两边必须一致);`--restore` 的方向是"本文件 -> 库"。
//      ⚠ 2026-10-17 之前本文件缺 `#define LOAD_FONT4` —— 就是那天"少字"的真因。
// ============================================================================
// ============================================================================
//  TFT_eSPI 配置 —— C3_CmdPad (ESP32-C3 SuperMini + ILI9341 2.4" 触摸屏)
// ----------------------------------------------------------------------------
//  ★ 生效方式:编译时加宏 -DC3_CMDPAD,User_Setup_Select.h 会选本文件;
//    不加宏(含 Arduino IDE 直编 S3 工程)走 User_Setup.h,S3 台架完全不受影响。
//
//  引脚依据(2026-10-11 定,依据 ESP32-C3 数据手册 v2.4 引脚表):
//    · C3 QFN32 可用 GPIO = 0~10 + 18/19(USB 占用) + 20/21(UART0),GPIO11-17
//      被内置 flash 占用。SuperMini 上 GPIO8=板载 LED、GPIO9=BOOT 键、GPIO2=strapping。
//    · SPI 走硬件 FSPI:SCLK=GPIO6(FSPICLK)、MOSI=GPIO7(FSPID)、CS=GPIO10(FSPICS0);
//      MISO=GPIO4(GPIO2 是 strapping 脚,虽 MISO 平时高阻,开机瞬间悬空有风险,挪开最稳)。
//    · 背光 BLK 接 3.3V 常亮(与 S3 同款屏模块),不占 IO。
//
//  接线表(屏脚 -> C3):
//    CLK -> GPIO6   MOSI -> GPIO7   MISO -> GPIO4   CS  -> GPIO10
//    DC  -> GPIO3   RES -> GPIO1    T_CS-> GPIO0    BLK -> 3.3V
//    T_IRQ 不接(轮询方式)
//
//  ⚠ 触摸/色序等其余参数与 S3 台架 User_Setup.h 保持一致(同一块屏实测值)。
// ============================================================================

#define USER_SETUP_INFO "C3 CmdPad ILI9341 240x320 + XPT2046 touch"

#define ILI9341_DRIVER

#define TFT_WIDTH  240
#define TFT_HEIGHT 320

// ---------------- 色序:同一块屏,S3 实测 BGR,原样照抄 ----------------
#define TFT_RGB_ORDER TFT_BGR
// #define TFT_INVERSION_ON
// #define TFT_INVERSION_OFF

// ---------------- 屏幕接线 ----------------
#define TFT_MISO  4
#define TFT_MOSI  7
#define TFT_SCLK  6
#define TFT_CS    10
#define TFT_DC    3
#define TFT_RST   1

// C3 只有一个用户 SPI 口(SPI2/FSPI),库的 C3 头文件自动选定,无需 USE_FSPI_PORT

// ★ 背光:BLK 接 3.3V 常亮,不定义 TFT_BL(与 S3 同理)

// ---------------- 触摸(XPT2046) ----------------
#define XPT2046_Touchscreen
#define TOUCH_CS   0              // GPIO0 - 触摸片选 T_CS(避开 GPIO9=BOOT、GPIO8=LED)
#define SPI_TOUCH_FREQUENCY 2500000

// ---------------- 字体(与 S3 台架一致) ----------------
#define LOAD_GLCD
#define LOAD_FONT2
// ★★ 2026-10-17 补:**必须开 FONT4**,否则"事件类型大字"静默消失。
//   现场症状(用户两次报同一件事):「C3 进 log data,左上角少了事件类型显示」——
//   S3 第 4 界面那里是 26px 彩色大字,C3 这边同一块区域**全空**。
//   根因:搬运页详情页用 `setTextFont(4)` 画事件类型(S3 同款),而本文件原先只开了
//   GLCD/FONT2 ⇒ TFT_eSPI 里 `case 4:` 整段没编进去 ⇒ `drawString()` 不报错、不崩溃,
//   **一个像素都不画**。S3 走根目录 User_Setup.h(那里有 LOAD_FONT4),所以只有 C3 缺字。
//   ⇒ 两板"屏上必须一样"的东西,字体表也得一样。删这行 = 那行大字再次凭空消失;
//   `C3_CmdPad.ino` 顶部有 `#ifndef LOAD_FONT4 #error` 兜底,删了会直接编译不过。
#define LOAD_FONT4

// ---------------- SPI 速度 ----------------
// 接线方式与 S3 台架相同(杜邦线),先按实测过的 20MHz 起步
#define SPI_FREQUENCY       20000000
#define SPI_READ_FREQUENCY  10000000

#define SUPPORT_TRANSACTIONS

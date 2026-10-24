// ============================================================================
//  TFT_eSPI 配置 —— GNSS 台架 (ESP32-S3 + ILI9341 2.4" 触摸屏)
// ----------------------------------------------------------------------------
//  ★★★ 本文件已与「2026-09-30 实测编译生效」的那份配置逐行核对齐 ★★★
//      之前这份副本漏了三个关键宏 —— TFT_RGB_ORDER TFT_BGR、USE_FSPI_PORT、
//      XPT2046_Touchscreen。照旧版挂到新电脑上会「蓝红反色」甚至屏不亮,现已补齐。
//      以后改动本文件,记得同步改本机库里的那份,两边必须一致。
//
//  引脚不是猜的:与已交付项目 RF_Source_Controller(40GHz 射频信号源控制器,
//  同一块 ESP32-S3 + 同一块 2.4" ILI9341 触摸屏)的实测接线完全一致。
//
//  ---------------------------------------------------------------------------
//  用法(二选一,推荐第一条):
//
//   【推荐·最省事】直接用它覆盖 TFT_eSPI 根目录下的 User_Setup.h
//      把本文件复制到  <库目录>/TFT_eSPI/User_Setup.h   (覆盖同名文件)
//      库目录一般在:   我的文档/Arduino/libraries/TFT_eSPI/
//      —— 因为 TFT_eSPI/User_Setup_Select.h 开头默认就是 #include <User_Setup.h>,
//         覆盖它即可生效,不必再去动 User_Setup_Select.h。
//
//   【想保留原文件】放进 User_Setups/ 并在选择表里指向它
//      1) 本文件复制到  <库目录>/TFT_eSPI/User_Setups/
//      2) 打开          <库目录>/TFT_eSPI/User_Setup_Select.h
//      3) 注释掉开头的  #include <User_Setup.h>
//         改成          #include <User_Setups/User_Setup_GNSS_Bench.h>
//
//   改完重新编译。若报 "当前生效的 User_Setup 引脚不是台架接线(应为 CLK12 ...)" ——
//   就是这一步没生效(改错了文件,或改完没重新编译)。固件里有一条编译期自检,
//   直接比对这里定义的引脚和台架接线,不用烧进去试。
//
//   另外:固件不再自己抄一份引脚表,而是直接引用下面这些宏(见 .ino 配置区)。
//   所以要改接线,只改本文件,固件那边自动跟着走。
// ============================================================================

#define USER_SETUP_INFO "GNSS Bench ILI9341 240x320 + XPT2046 touch"

#define ILI9341_DRIVER            // 若屏是 ST7789,换成 ST7789_DRIVER

#define TFT_WIDTH  240
#define TFT_HEIGHT 320

// ---------------- 色序(★ 换屏后颜色不对时的第一嫌疑) ----------------
// 本项目这块 2.4" ILI9341 实测需要 BGR;不定义的话蓝色会显成红色、红色显成蓝色。
// 白底黑字看不出来,只有出现红/蓝时才暴露 —— 所以这一行务必保留。
// 现场想当场翻转验证:串口发 `o`(固件里已实现,只翻色序位、不动方向位)。
// 若整体明暗反了(白底变黑底),那不是色序问题而是反显:串口发 `n` 试,
// 命中后把下面的 TFT_INVERSION_ON 打开。
#define TFT_RGB_ORDER TFT_BGR     // 实测值。若显得红蓝互换,改成 TFT_RGB
// #define TFT_INVERSION_ON
// #define TFT_INVERSION_OFF

// ---------------- 屏幕接线(必须与固件编译期自检一致) ----------------
// 实测接法(来自 RF_Source_Controller 交付版):
//   屏脚    ->  ESP32-S3
//   CLK     ->  GPIO12
//   MOSI    ->  GPIO11
//   MISO    ->  GPIO13   ← 触摸要用这条回线,必须接(之前漏了)
//   CS      ->  GPIO10
//   DC      ->  GPIO 6
//   RES     ->  GPIO 4
//   BLK     ->  3.3V      ← 常亮(本项目实测,不占 IO);正因为不接 GPIO,GPIO7 才空出来给了 SHT30 的 SCL
//   T_CS    ->  GPIO 9    ← 触摸片选
//   T_IRQ   ->  GPIO 5    ← 触摸中断(轮询方式可不接)
#define TFT_MISO  13
#define TFT_MOSI  11
#define TFT_SCLK  12
#define TFT_CS    10
#define TFT_DC     6
#define TFT_RST    4

// ★ ESP32-S3 + Arduino-ESP32 3.x 必须显式指定用 FSPI(SPI2)口 —— 少了这行屏不亮。
//   (S3 与 ESP32 的默认 SPI 端口选择规则不同,库的自动选择在 3.x 核心上会落到错的口)
#define USE_FSPI_PORT

// ★ 背光(★ 2026-10-24 起由 GPIO7 驱动 —— P9 实测后用户拍板"试一试"):
//   BLK 经杜邦线接 GPIO7,固件 disp::begin() 里 ledcAttach 接管调光(5kHz/8bit)。
//   历史(2026-10-22 重排时):GPIO7 曾给 SHT30 当 SCL,现已挪去 GPIO9,本脚空闲。
//   ⚠ 别把 TFT_BL 改成 8/9 —— 8=GNSS TXD,9=SHT SCL,static_assert 会拦但别去撞。
#define TFT_BL     7
#define TFT_BACKLIGHT_ON HIGH     // 上电默认亮;调光由固件 LEDC 接管,此值只在库 init 时用一次

// ---------------- 触摸(XPT2046) ----------------
// 想用触屏就保留下面这两行;不想用就注释掉,同时把固件里 ENABLE_TOUCH 改 0。
// 两个开关必须一致,不一致会在编译时报 #error 提醒。
#define XPT2046_Touchscreen       // 打开触摸控制器支持(TFT_eSPI 里就叫这个名字)
#define TOUCH_CS   14             // GPIO14 - 触摸片选 T_CS(2026-10-22 官方重排:FSPIWP 焊盘;沿革 9)
#define TOUCH_PEN  5              // GPIO5 - 触摸中断 T_IRQ(轮询方式不接也行)
#define SPI_TOUCH_FREQUENCY 2500000   // XPT2046 上限约 2.5MHz,别往上调

// ---------------- 字体 ----------------
#define LOAD_GLCD                 // 字体 1(6x8),报文窗用的就是这个
#define LOAD_FONT2                // 字体 2(16px),标题/标签用
// ★ v1.7.0 新增:FONT4(Font32rle)—— 屏上状态大字改英文后用它。
//   为什么必须是它:FONT6/FONT8(Font64rle/Font72rle)只含 `[space] 0-9 : - . a p m`,
//   打 "NORMAL" 会输出一片空白。**只有 FONT4 含完整 96 个 ASCII**。
//   大写字母实测宽 16~19px、字形高约 26px;它由 #define LOAD_FONT4 提供,
//   用 tft.setFreeFont(FONT4) 挂上。中文字模(cnfont)v1.7.0 已删,不再需要自绘点阵。
#define LOAD_FONT4
// #define SMOOTH_FONT            // ★ 本项目不需要(上屏文字全是 ASCII 内置字体);
//                                //   两边都不开,保持一致也省 flash

// ---------------- SPI 速度 ----------------
// ★ 实测 20MHz:飞线(Dupont)连接、线又长,再往上容易花屏。
//   屏若是焊在 PCB 上的短接线,才可以试着提到 27~40MHz。
#define SPI_FREQUENCY       20000000
#define SPI_READ_FREQUENCY  10000000

// ESP32 上事务支持由库按平台自动开启,这里显式写上,与实测配置保持一致
#define SUPPORT_TRANSACTIONS

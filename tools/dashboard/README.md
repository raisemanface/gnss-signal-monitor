# PC 端实时看板

台架上的 ESP32-S3 把 NMEA 解析结果打到局域网，这个目录里的服务把它收下来，
在浏览器里画成八块面板。**零第三方依赖**（只用 Python 标准库），断外网也能跑。

```
S3 ──UDP 9911──▶ dashboard.py ──HTTP 8787──▶ 浏览器
                   └─ 逐拍落 CSV（与板上 `o dump` 同构）
```

---

## 一、怎么启动

**Windows：双击 `StartDashboard.bat`。** 它会先探 8787 端口——已经在跑就直接开页面，
没跑就先起服务（窗口别关，关窗即停），4 秒后自动打开 <http://127.0.0.1:8787/>。

**任何平台：**

```bash
python dashboard.py                     # 默认 UDP 9911 / HTTP 8787
python dashboard.py --udp 9911 --http 8787 --out ./out
python dashboard.py --bind 0.0.0.0      # 对局域网开放（见第四节）
```

需要 Python 3.9+。启动脚本按这个顺序找解释器：环境变量 `GNSS_PY` → `C:\Python311\python.exe` → `PATH` 里的 `python`。

**首次运行必须在防火墙放行入站**，否则症状是「板子在发、页面永远没数据」：

```bat
netsh advfirewall firewall add rule name="GNSS-Bench-UDP" dir=in action=allow protocol=UDP localport=9911
```

---

## 二、八块面板

| 编号 | 面板 | 看什么 |
|---|---|---|
| 01 | 实时指标 | 卫星数 / HDOP / 定位解状态 / HPL / C/N0 |
| 02 | 天球星图 | 方位-仰角投影，星的位置与 C/N0 一起变 |
| 03 | 告警与统计 | 事件计数、跳号、链路健康 |
| 04 | SQW−PPS 脉冲对比 | 秒脉冲对齐情况 |
| 05 | 位置偏移 | 相对基准的漂移轨迹 |
| 06 | C/N0 中位趋势 | 全星座信号中位数的走势 |
| 07 | 完好性 · 检测器 | CUSUM 统计量与绝对闸 |
| 08 | C/N0 声化 | 把信号变成能听的声音（下节） |

面板可以拖标题栏移动、拖右下角缩放，落点自动对齐网格。布局存浏览器 `localStorage`，
另有「原版 / 天球满高 / 天球全高」三个预设一键切换。删掉服务端 `layout.json` 可恢复默认。

---

## 三、声化怎么玩（08 面板）

1. 在 **02 天球星图**上直接点卫星——点哪颗哪颗接管，再点取消，最多 8 颗；
2. 点 **08 面板的 ♪** 开声；
3. 每颗星的音高跟着它自己的 C/N0 走，**音变了才敲一下**（不是长音），三种音色：钟 / 玻璃 / 木；
4. ⚙ 浮层只有两个参数：**音阶**（五声 / 大调七声 / 全音）和**音量**；
5. 点选的星与全部声化设置都会存档，刷新页面原样恢复。

刷新时如果之前点的星已经落下地平线，读数会写成「在唱/点选」（例如 `♪ 2/8`），
一分钟后这些没有数据的旧点选会被自动摘掉——不会留下"天球只有 2 颗、面板却说 8 个在唱"的幽灵声部。

强星 C/N0 稳，十秒敲一两下是正常的；想听密集敲击就点低仰角的弱星。
敲击有全局限速（每颗星 450 ms、全局 160 ms），信号抖动不会把它变成噪音。

合成走 Web Audio，全部在页面内完成，不需要任何音频文件或服务端参与。

---

## 四、换一台电脑

**情况 A：别的电脑只想看页面（服务还在原机器跑）**

1. 原机器改用 `StartDashboard-LAN.bat` 启动（等价于 `--bind 0.0.0.0`）；
2. 首次管理员运行一次，放行 TCP 8787：
   ```bat
   netsh advfirewall firewall add rule name="GNSS-Bench-WEB" dir=in action=allow protocol=TCP localport=8787
   ```
3. `ipconfig | findstr IPv4` 查本机 IP（例如 192.168.1.20）；
4. 别的电脑打开 `http://192.168.1.20:8787`。

**情况 B：本机 IP 老是变**

板子往哪推数据是**写死在板子里**的（串口 `w push <IP>` 保存）。IP 一变，
数据就推到空处，页面「永远没数据」。根治办法是在路由器上给本机 MAC 做
**DHCP 静态绑定**（有的路由器叫「地址保留」）。

**情况 C：把服务搬到另一台电脑**

整个目录拷过去，双击 `StartDashboard.bat` 即可——页面部分任何电脑都一样。
但板子不认识新电脑，要接串口重新指路：

```
w push <新电脑IP>                  # UDP
w push <新电脑IP> 9911 tcp save     # 或 TCP
```

> 一句话：**看板搬到哪台电脑都能跑；只有「板子往哪推」需要重新指一次路。**

---

## 五、目录

| 文件 | 用途 |
|---|---|
| `dashboard.py` | 服务端：UDP 收包、落 CSV、HTTP 页面与 `/api/live` |
| `static/index.html` | 看板主页面（单文件，含全部 JS/CSS） |
| `static/chart.umd.min.js` | Chart.js 4（MIT），本地打包，不依赖 CDN |
| `static/sky-chorus.html` | 全天空声化调参台（实验页，独立打开） |
| `static/cn0-sonify.html` | 单声部声化试验台（实验页） |
| `static/_trail-demo.html` | 轨迹绘制的独立小样 |
| `verify/` | 浏览器自动验证与录制脚本（playwright-core + 本机 Edge） |
| `layout.json` | 面板布局存档，运行时生成 |

服务只做三件事：收包落盘、查序号连续性、提供页面。**不向 S3 发任何控制命令**，
遥控一律走串口或 C3 手柄。

---

## 六、verify 脚本（可选）

改版面或录屏时才用得上。需要 Node 18+。

```bash
cd verify
npm i

node layout.js      # 版面检查：八个面板是否溢出、是否重叠
node strike.js      # 点选声化：点星 → 声部建立 → 敲击是否发生
node rec.js 16 ./out 60   # 录屏（时长秒 / 输出目录 / 预热秒），带页面内录音
```

`_env.js` 统一了浏览器路径与产物目录，都可用环境变量覆盖：

```bash
EDGE_PATH="D:/Edge/msedge.exe" OUT_DIR="./shots" node verify.js
```

产物落在 `verify/out/`（已忽略）。`rec.js` 输出 JPEG 帧序列 + `audio.webm`，
用 ffmpeg 合成即可。

---

## 许可

与仓库其余部分一致，见根目录 `LICENSE`。Chart.js 为 MIT，版权归其作者。

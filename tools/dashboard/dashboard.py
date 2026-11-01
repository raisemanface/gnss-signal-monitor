#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
GNSS 台架实时看板 —— PC 侧（任务单-2026-10-30-4）

职责（只做三件事，别加第四件）：
  1. UDP 9911 收 S3 推来的 W1 包，OBS 逐行落 CSV（与板上 `o dump` 同构）；
  2. 每类包各自查序号连续性，跳号记 gaps.log；
  3. 在 http://127.0.0.1:8787 提供页面 + /api/live（页面 2 s 轮询一次）。

三条纪律：
  · 标准库，零第三方依赖（http.server 自带，够用；阳台 PC 可能断外网）；
  · **只读展示 + 落盘，不向 S3 发任何控制命令**（遥控走串口/C3）；
  · 不改 tools/ 既有脚本，不混入既有实验数据目录（本脚本只写 dashboard/ 子目录）。

用法：
    python dashboard.py                 # 默认 9911 / 8787
    python dashboard.py --udp 9911 --http 8787
    python dashboard.py --out "./out"    # 落盘目录默认 ./out，可指向任意路径

首次运行必须在 Windows 防火墙放行 UDP 入站，否则症状是"S3 在发、页面永远没数据"：
    netsh advfirewall firewall add rule name="GNSS-Bench-UDP" dir=in action=allow protocol=UDP localport=9911
"""

from __future__ import annotations

import argparse
import csv
import json
import os
import socket
import sys
import threading
import time
from collections import deque
from datetime import datetime, timezone
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

# ----------------------------------------------------------------------------
# 常量
# ----------------------------------------------------------------------------
MAGIC = "W1"
UDP_PORT = 9911
TCP_PORT = 9911          # 同号不同协议；板子用 `w push <ip> [port] tcp` 切过去
HTTP_PORT = 8787
MAX_LINE = 4096          # TCP 单行上限（SKY 包最大 ~960 B，留足余量）
OBS_KEEP = 300          # 页面只用最近 260 点（HIST），多留一点余量；原来 720 让每 2 s 的
                        # /api/live 多背 40 KB 无用观测，解析完立刻被裁掉，纯属给 GC 添活
EVT_KEEP = 200
LINK_WARN_S = 15.0      # 距最后一包 >15 s 黄
LINK_DOWN_S = 60.0      # >60 s 红

HERE = os.path.dirname(os.path.abspath(__file__))
STATIC = os.path.join(HERE, "static")
# 页面布局存档（v8 起看板可拖拽）：POST /api/layout 落盘，GET 取回。
# 放这里而不是只存 localStorage —— 换浏览器/清缓存后布局还在，也方便离线核对数值。
LAYOUT_FILE = os.path.join(HERE, "layout.json")
_WS_OUT = os.path.normpath(os.path.join(HERE, "..", "..", "docs", "实验数据", "dashboard"))
# 工作区里落到 docs/实验数据/dashboard；单独把本目录拷出去用时落到 ./out，不往上报目录写脏数据
DEFAULT_OUT = _WS_OUT if os.path.isdir(os.path.dirname(_WS_OUT)) else os.path.join(HERE, "out")

# 列名与 tools/观测落盘-导出.py 的 COLS_LINE **逐字一致** —— 回放脚本靠它认列。
COLS_LINE = (
    "#COLS,OBS=行标记,millis=板内毫秒(重启归零),sats_used=参与解算星数(-1=无fix),"
    "hdop,cn0_med=中位载噪比,cn0_last=末星载噪比,mpN=多径星数,mpPk=多径峰值dB,"
    "hpl_m=水平保护级,off_m=相对基准偏移,sog_mps=多普勒地速,cause=归因,tag=状态词,"
    "temp_c=SHT30温度,rh_pct=SHT30湿度,pdo_ms=SQW-PPS相位差(负值/正=SQW落后;-1=无脉冲),"
    "tsrc=时间源G/R/-,utc=Unix秒(0=时间不可信)"
)

TYPES = ("OBS", "EVT", "HB", "SKY", "STA")


def _f(x: str, default=-1.0) -> float:
    try:
        return float(x)
    except (TypeError, ValueError):
        return default


def _i(x: str, default=-1) -> int:
    try:
        return int(float(x))
    except (TypeError, ValueError):
        return default


def _utc_hms(date_md: str, sod: str) -> str:
    """事件的 date('MM-DD') + t0('HH:MM:SS') 直接就是 UTC 墙上时间。"""
    return sod if sod else "--:--:--"


# ----------------------------------------------------------------------------
# 数据仓
# ----------------------------------------------------------------------------
class Store:
    def __init__(self, out_root: str):
        self.out_root = out_root
        self.lock = threading.Lock()

        self.obs = deque(maxlen=OBS_KEEP)
        self.events = deque(maxlen=EVT_KEEP)
        self.sky = []
        self.sta = {}
        self.dev = {}

        self.last_seen = 0.0          # monotonic
        self.last_wall = 0.0          # 最后一包的 UTC epoch
        self.seq = {t: None for t in TYPES}
        self.gaps = {t: 0 for t in TYPES}
        self.restarts = {t: 0 for t in TYPES}   # ★ 序号回退 = 板子重启,不算丢包
        self.pkt = {t: 0 for t in TYPES}
        self.transport = "-"                    # ★ 最近一包走的传输:udp / tcp
        self.bad = 0
        self.started = time.time()

        self._day = None
        self._fh = None
        self._path = None
        self._last_flush = 0.0

    # ---- CSV ----
    def _open_day(self):
        day = datetime.now().strftime("%Y-%m-%d")
        if self._fh and self._day == day:
            return
        if self._fh:
            try:
                self._fh.close()
            except OSError:
                pass
            self._fh = None
        d = os.path.join(self.out_root, day)
        os.makedirs(d, exist_ok=True)
        name = "obs-%s.csv" % datetime.now().strftime("%H%M%S")
        path = os.path.join(d, name)
        fh = open(path, "w", encoding="utf-8", newline="\n")
        fh.write(COLS_LINE + "\n")
        fh.flush()
        self._fh, self._day, self._path = fh, day, path
        self._gap_file(d)
        print("[data] %s" % path)

    def _gap_file(self, day_dir: str):
        """每个数据目录里放一份 README.txt，写明口径（任务单 §2.1-5）。"""
        p = os.path.join(day_dir, "README.txt")
        if os.path.exists(p):
            return
        with open(p, "w", encoding="utf-8") as f:
            f.write(
                "本目录由 tools/dashboard/dashboard.py 自动生成。\n\n"
                "obs-<HHMMSS>.csv 是实时看板落下的观测流，**与板上 /obs.csv 同构**：\n"
                "  · 列定义见文件首行 #COLS（与 tools/观测落盘-导出.py 的 COLS_LINE 逐字一致）；\n"
                "  · 每行以 OBS, 开头、18 列，可直接喂 tools/pdo判据-离线标定.py / cusum_replay.py；\n"
                "  · 时间轴用第 18 列 utc（GNSS 时间），不要用 PC 本地时间。\n\n"
                "gaps.log 记录 UDP 丢包（S3 序号的洞）—— 只是链路证据，不代表板上数据缺失：\n"
                "  板上 /obs.csv 是正本，LittleFS 一直在写；本 CSV 是实时窗口。\n"
            )

    def _append_obs(self, payload: str):
        self._open_day()
        if not self._fh:
            return
        self._fh.write("OBS," + payload + "\n")
        self.maybe_flush()

    def maybe_flush(self):
        """★ 不能只在"来了新行"时才 flush —— 数据流一停，缓冲区里的行会一直悬着。
        udp_loop 每 0.5 s 超时一次，顺便调这里（参照板上 rec 60 s 一刷的同一条纪律）。"""
        now = time.monotonic()
        if self._fh and now - self._last_flush >= 5.0:
            self._fh.flush()
            self._last_flush = now

    def _log_event(self, typ: str, expected: int, got: int, kind: str = "gap"):
        """写 gaps.log。kind=restart 的是板子重启（序号回退），不与真丢包混为一谈。"""
        self._open_day()
        if not self._day:
            return
        p = os.path.join(self.out_root, self._day, "gaps.log")
        ts = datetime.now().strftime("%Y-%m-%dT%H:%M:%S")
        tail = "  (restart)" if kind == "restart" else ""
        with open(p, "a", encoding="utf-8") as f:
            f.write("%s %s expected=%d got=%d%s\n" % (ts, typ, expected, got, tail))

    def _gap(self, typ: str, expected: int, got: int):
        self.gaps[typ] += 1
        self._log_event(typ, expected, got)

    # ---- 包 ----
    def feed(self, data: bytes, addr, transport: str = "udp"):
        try:
            text = data.decode("ascii", "replace").strip("\r\n\x00 ")
        except Exception:
            self.bad += 1
            return
        parts = text.split(",", 3)
        if len(parts) < 3 or parts[0] != MAGIC:
            self.bad += 1
            return
        typ = parts[1]
        if typ not in TYPES:
            self.bad += 1
            return
        seq = _i(parts[2], -1)
        payload = parts[3] if len(parts) > 3 else ""

        with self.lock:
            self.pkt[typ] += 1
            self.transport = transport
            self.last_seen = time.monotonic()
            self.last_wall = time.time()
            last = self.seq[typ]
            if seq >= 0:
                if last is not None:
                    want = (last + 1) & 0xFFFFFFFF
                    if seq != want:
                        # ★ 2026-10-30 晚修:原来的"不等就是洞"把板子重启也记成丢包 ——
                        #   烧录三次就在 gaps.log 里留下 14 条 "expected=187 got=1",
                        #   看起来像"UDP 掉包严重",实际是假警。
                        #   分三种,按序号只能增大的物理事实判:
                        #     seq < last  -> 对端重启(序号从头数)
                        #     seq > want  -> 真丢包(中间少了几包)
                        #     其余        -> 重复包(UDP 可能重复,不计数)
                        if seq < last:
                            self.restarts[typ] += 1
                            self._log_event(typ, want, seq, kind="restart")
                        elif seq > want:
                            self._gap(typ, want, seq)
                self.seq[typ] = seq

            if typ == "OBS":
                self._on_obs(payload)
            elif typ == "EVT":
                self._on_evt(payload)
            elif typ == "SKY":
                self._on_sky(payload)
            elif typ == "STA":
                self._on_sta(payload)
            else:
                self._on_hb(payload)

    def _on_obs(self, p: str):
        c = p.split(",")
        if len(c) != 17:          # 17 列 + 行标记 = 18 列
            self.bad += 1
            return
        row = {
            "ms": _i(c[0]), "sats": _i(c[1]), "hdop": _f(c[2]),
            "cn0med": _f(c[3]), "cn0max": _f(c[4]), "mpn": _i(c[5]), "mppk": _f(c[6]),
            "hpl": _f(c[7]), "off": _f(c[8]), "sog": _f(c[9]),
            "cause": c[10], "tag": c[11], "temp": _f(c[12]), "rh": _f(c[13]),
            "pdo": _f(c[14]), "tsrc": c[15], "utc": _i(c[16]),
        }
        self.obs.append(row)
        self._append_obs(p)

    def _on_evt(self, p: str):
        try:
            c = next(csv.reader([p]))
        except Exception:
            self.bad += 1
            return
        if len(c) < 13:
            self.bad += 1
            return
        ev = {
            "seq": _i(c[0]), "date": c[1], "t0": c[2], "t1": c[3],
            "cross": _i(c[4], 0), "dur": _i(c[5], 0), "type": c[6],
            "closed": _i(c[7], 0), "off": _f(c[8]), "temp": _f(c[9]), "rh": c[10],
            "gga": c[11], "cause": c[12],
        }
        self.events.appendleft(ev)

    def _on_sky(self, p: str):
        out = []
        for it in p.split(";"):
            it = it.strip()
            if not it:
                continue
            f = it.split(",")
            if len(f) != 4 or not f[0]:
                continue
            out.append({
                "sys": f[0][0], "prn": f[0],
                "az": _i(f[1]), "el": _i(f[2]), "cn0": _i(f[3], 0),
            })
        self.sky = out

    def _on_sta(self, p: str):
        c = p.split(",")
        if len(c) < 5:
            return
        self.sta = {
            "qual": _i(c[0], 0), "view": _i(c[1], 0), "used": _i(c[2], -1),
            "hdop": _f(c[3]), "tsrc": c[4],
            # ★ v1.14.9 固件尾插：cusum(m·s) / slow(0|1)。旧固件没有这两列，
            #   缺省 -1 / 0，页面按"---"显示 —— 前后向都兼容。
            "cusum": _f(c[5], -1.0) if len(c) > 5 else -1.0,
            "slow": _i(c[6], 0) if len(c) > 6 else 0,
            # ★ v1.14.12 固件再尾插：gated(0|1) = 统计量正被绝对闸拦着（off ≤ 15 m）。
            #   旧固件没有这列 → 缺省 0，页面行为不变。
            "gated": _i(c[7], 0) if len(c) > 7 else 0,
        }

    def _on_hb(self, p: str):
        c = p.split(",")
        if len(c) < 4:
            return
        self.dev = {
            "uptime": _i(c[0]), "heap": _i(c[1]),
            "obs_tx": _i(c[2]), "evt_tx": _i(c[3]),
        }

    # ---- 对外 JSON ----
    def snapshot(self):
        with self.lock:
            age = (time.monotonic() - self.last_seen) if self.last_seen else None
            link = "down" if (age is None or age > LINK_DOWN_S) else ("warn" if age > LINK_WARN_S else "ok")
            obs = list(self.obs)
            evs = list(self.events)[:60]
            sky = list(self.sky)
            sta = dict(self.sta)
            dev = dict(self.dev)
            gaps = dict(self.gaps)
            pkt = dict(self.pkt)
            transport = self.transport
            restarts = dict(self.restarts)

        last = obs[-1] if obs else {}
        # 12 小时告警频次：按事件 t0 的小时归档（UTC，与页面时间轴同源）
        buckets = [0] * 12
        now_h = datetime.now(timezone.utc).hour
        for e in evs:
            try:
                h = int(e["t0"][:2])
            except (ValueError, KeyError):
                continue
            k = (now_h - h) % 12
            buckets[11 - k] += 1
        # SNR 分布：SKY 里每颗星一票，6 桶（15~20 ... 40+），与视觉稿一致
        bins = [0] * 6
        for s in sky:
            v = s["cn0"]
            if v <= 0:
                continue
            bins[min(5, max(0, (v - 15) // 5))] += 1

        return {
            "link": {"state": link, "age": round(age, 1) if age is not None else None,
                     "bad": self.bad, "pkt": pkt, "gaps": gaps,
                     "transport": transport,                  # ★ udp / tcp
                     "restarts": restarts},                   # ★ 序号回退次数（不是丢包）
            "kpi": {
                "pps": last.get("pdo", -1.0), "sats_used": last.get("sats", -1),
                "sats_view": sta.get("view", -1), "hdop": last.get("hdop", -1.0),
                "off": last.get("off", -1.0), "sog": last.get("sog", -1.0),
                "qual": sta.get("qual", -1), "tsrc": sta.get("tsrc", "-"),
                "cause": last.get("cause", "---"), "tag": last.get("tag", "NO-DATA"),
                "crt": last.get("utc", 0),
                # ★ 2026-10-30 晚:算法图那四张卡要用的量,原来只留在 CSV 里没上看板。
                #   hpl 是完好性的主指标,mpn/mppk 是 DETECTORS 卡的内容,
                #   temp/rh 是环境旁证。缺字段一律给 -1,页面按 "---" 显示。
                "hpl": last.get("hpl", -1.0),
                "cn0med": last.get("cn0med", -1.0),
                "cn0max": last.get("cn0max", -1.0),
                "mpn": last.get("mpn", -1),
                "mppk": last.get("mppk", -1.0),
                "temp": last.get("temp", -1.0),
                "rh": last.get("rh", -1.0),
                # ★ v1.14.9:CUSUM 统计量(位置域慢漂移,固件 STA 包尾插列)。
                "cusum": sta.get("cusum", -1.0),
                "slow": sta.get("slow", 0),
                "gated": sta.get("gated", 0),
            },
            "obs": [{"t": r["utc"], "pps": r["pdo"], "off": r["off"],
                     "hdop": r["hdop"], "sats": r["sats"],
                     "hpl": r["hpl"], "cn0": r["cn0med"]} for r in obs],
            "sky": sky,
            "events": evs,
            "alarm_bins": buckets,
            "snr_bins": bins,
            "dev": dev,
            "started": self.started,
        }


# ----------------------------------------------------------------------------
# UDP 监听
# ----------------------------------------------------------------------------
def udp_loop(store: Store, port: int, stop: threading.Event):
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    try:
        s.bind(("0.0.0.0", port))
    except OSError as e:
        print("[udp] bind 0.0.0.0:%d FAILED: %s" % (port, e))
        stop.set()
        return
    s.settimeout(0.5)
    print("[udp] listening 0.0.0.0:%d" % port)
    while not stop.is_set():
        try:
            data, addr = s.recvfrom(2048)
        except socket.timeout:
            store.maybe_flush()          # ★ 无包也要按钟落盘（防停流后数据悬在缓冲区）
            continue
        except OSError:
            break
        store.feed(data, addr)
    s.close()


# ----------------------------------------------------------------------------
# TCP 监听（★ 2026-10-30 晚新增）
# ----------------------------------------------------------------------------
# 和 UDP 用同一个端口号，但协议不同，两个 socket 互不冲突。板子是**客户端**，
# 主动连过来 —— 这样板子换 IP、路由器重新分配地址都不影响看板，重连由板子那头做。
# 分帧靠行尾 '\n'（固件 TCP 模式自动补）；UDP 那边是一包一行，不补也能用。
TCP_IDLE_TIMEOUT = 120   # s —— 单连接空闲多久判死。板子整机断电不会发 FIN/RST，
                         # 旧连接会一直挂在 ESTABLISHED 上；不主动清就会越积越多，
                         # 而单线程串行服务旧连接时，新连接永远排不进 accept。
def _tcp_client(store: Store, conn: socket.socket, addr, stop: threading.Event):
    conn.settimeout(0.5)
    buf = b""
    last_rx = time.monotonic()
    try:
        while not stop.is_set():
            try:
                chunk = conn.recv(4096)
            except socket.timeout:
                store.maybe_flush()          # 停流时也要按钟落盘
                if time.monotonic() - last_rx > TCP_IDLE_TIMEOUT:
                    print("[tcp] client %s:%d idle %ds, dropping"
                          % (addr[0], addr[1], TCP_IDLE_TIMEOUT))
                    break
                continue
            if not chunk:
                break
            last_rx = time.monotonic()
            buf += chunk
            while b"\n" in buf:
                line, buf = buf.split(b"\n", 1)
                if line.strip():
                    store.feed(line, addr, transport="tcp")
            if len(buf) > MAX_LINE:          # 对端不发换行时别把内存吃光
                store.bad += 1
                buf = b""
    except OSError:
        pass
    finally:
        try:
            conn.close()
        except OSError:
            pass
        print("[tcp] client %s:%d disconnected" % addr)


def tcp_loop(store: Store, port: int, stop: threading.Event):
    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    try:
        srv.bind(("0.0.0.0", port))
        srv.listen(8)
    except OSError as e:
        print("[tcp] bind 0.0.0.0:%d FAILED: %s" % (port, e))
        return
    srv.settimeout(0.5)
    print("[tcp] listening 0.0.0.0:%d" % port)
    clients = []
    while not stop.is_set():
        try:
            conn, addr = srv.accept()
        except socket.timeout:
            store.maybe_flush()
            continue
        except OSError:
            break
        print("[tcp] client %s:%d connected" % (addr[0], addr[1]))
        # ★ 每连接一线程：板子每次复位都会新建一条，旧的多半是半开死连接。
        #   串行 accept 会被死连接堵住，收不到新连接上的数据（2026-10-31 实测踩到）。
        t = threading.Thread(target=_tcp_client, args=(store, conn, addr, stop), daemon=True)
        t.start()
        clients = [c for c in clients if c.is_alive()] + [t]
    srv.close()


# ----------------------------------------------------------------------------
# HTTP
# ----------------------------------------------------------------------------
class Handler(BaseHTTPRequestHandler):
    store: Store = None          # type: ignore
    static_dir: str = STATIC

    def log_message(self, fmt, *args):     # 静音：页面 2 s 一轮，刷屏没意义
        pass

    def _send(self, code, body: bytes, ctype: str, cache: str = "no-store"):
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", cache)
        self.end_headers()
        try:
            self.wfile.write(body)
        except (BrokenPipeError, ConnectionResetError):
            pass

    def do_GET(self):
        path = self.path.split("?", 1)[0]
        if path == "/api/live":
            body = json.dumps(self.store.snapshot(), ensure_ascii=False).encode("utf-8")
            self._send(200, body, "application/json; charset=utf-8")
            return
        if path == "/api/layout":
            if os.path.isfile(LAYOUT_FILE):
                with open(LAYOUT_FILE, "rb") as f:
                    return self._send(200, f.read(), "application/json; charset=utf-8")
            return self._send(200, b"{}", "application/json; charset=utf-8")
        if path in ("/", "/index.html"):
            return self._file("index.html", "text/html; charset=utf-8")
        if path == "/chart.umd.min.js":
            return self._file("chart.umd.min.js", "text/javascript; charset=utf-8", "max-age=86400")
        # 附加静态页（白名单，防路径穿越）
        _CT = {".html": "text/html; charset=utf-8", ".js": "text/javascript; charset=utf-8",
               ".css": "text/css; charset=utf-8", ".png": "image/png", ".svg": "image/svg+xml"}
        ext = os.path.splitext(path)[1]
        if ext in _CT:
            return self._file(os.path.basename(path), _CT[ext])
        self._send(404, b"not found", "text/plain; charset=utf-8")

    def do_POST(self):
        path = self.path.split("?", 1)[0]
        if path != "/api/layout":
            return self._send(404, b"not found", "text/plain; charset=utf-8")
        try:
            n = int(self.headers.get("Content-Length") or 0)
            raw = self.rfile.read(n) if n > 0 else b"{}"
            obj = json.loads(raw.decode("utf-8"))
        except Exception as e:                                  # noqa: BLE001
            return self._send(400, ("bad json: %s" % e).encode("utf-8"), "text/plain; charset=utf-8")
        try:
            # 注意：本目录在百度同步盘下，os.replace() 覆盖已存在文件会 WinError 5，
            # 这里按现场约定直接整体覆盖写（数据量 < 2 KB，不存在截断风险窗口）。
            with open(LAYOUT_FILE, "wb") as f:
                f.write(json.dumps(obj, ensure_ascii=False, indent=1).encode("utf-8"))
        except Exception as e:                                  # noqa: BLE001
            return self._send(500, ("write fail: %s" % e).encode("utf-8"), "text/plain; charset=utf-8")
        self._send(200, b'{"ok":true}', "application/json; charset=utf-8")

    def _file(self, name, ctype, cache="no-store"):
        p = os.path.join(self.static_dir, name)
        if not os.path.isfile(p):
            return self._send(404, ("missing %s" % name).encode("utf-8"), "text/plain; charset=utf-8")
        with open(p, "rb") as f:
            self._send(200, f.read(), ctype, cache)


# ----------------------------------------------------------------------------
# main
# ----------------------------------------------------------------------------
def main() -> int:
    ap = argparse.ArgumentParser(description="GNSS 台架实时看板（PC 侧）")
    ap.add_argument("--udp", type=int, default=UDP_PORT)
    ap.add_argument("--tcp", type=int, default=TCP_PORT)
    ap.add_argument("--http", type=int, default=HTTP_PORT)
    ap.add_argument("--out", default=DEFAULT_OUT, help="CSV 输出根目录（默认 docs/实验数据/dashboard）")
    ap.add_argument("--bind", default="127.0.0.1", help="Web 绑定地址（默认只本机可见）")
    a = ap.parse_args()

    if not os.path.isdir(STATIC):
        print("[web] 缺少 static/ 目录：%s" % STATIC)
        return 2
    if not os.path.isfile(os.path.join(STATIC, "chart.umd.min.js")):
        print("[web] 警告：static/chart.umd.min.js 缺失，趋势图不可用（页面其余部分仍工作）")

    store = Store(a.out)
    os.makedirs(a.out, exist_ok=True)

    print("=" * 74)
    print(" GNSS 台架实时看板")
    print("   UDP   0.0.0.0:%d  ← S3 推流（串口敲 w push <本机IP>）" % a.udp)
    print("   TCP   0.0.0.0:%d  ← S3 推流（串口敲 w push <本机IP> 9911 tcp save）" % a.tcp)
    print("   Web   http://%s:%d" % (a.bind, a.http))
    print("   数据  %s" % os.path.abspath(a.out))
    print("-" * 74)
    print(" 首次运行必须放行入站，否则 S3 在发、这里永远是空的：")
    print('   netsh advfirewall firewall add rule name="GNSS-Bench-UDP" '
          'dir=in action=allow protocol=UDP localport=%d' % a.udp)
    print('   netsh advfirewall firewall add rule name="GNSS-Bench-TCP" '
          'dir=in action=allow protocol=TCP localport=%d' % a.tcp)
    print(" 排障第一步：串口敲 `w push status`，看发送计数在不在涨。")
    print("=" * 74)

    stop = threading.Event()
    th = threading.Thread(target=udp_loop, args=(store, a.udp, stop), daemon=True)
    th.start()
    th2 = threading.Thread(target=tcp_loop, args=(store, a.tcp, stop), daemon=True)
    th2.start()

    Handler.store = store
    Handler.static_dir = STATIC
    srv = ThreadingHTTPServer((a.bind, a.http), Handler)
    srv.daemon_threads = True
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        print("\n[web] bye")
    finally:
        stop.set()
        srv.shutdown()
        if store._fh:
            store._fh.flush()
            store._fh.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())

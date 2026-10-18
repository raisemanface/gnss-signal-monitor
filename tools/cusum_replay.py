#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""CUSUM 慢漂移检测器 —— 离线回放验证（不是模拟固件，是**同一份算法**的回放）

为什么要这个脚本
----------------
固件里的 CUSUM 抓的是"一分钟拉偏几米"这类**缓慢漂移**。这种东西在工位上
造不出来（不做注入之后，没有任何手段让真机慢慢漂），所以：
  · 真机只能验证**负方向**：正常静置 N 分钟，统计量必须一直贴着 0；
  · **正方向**（真的漂了能不能抓到、多久抓到）只能靠离线回放：
    拿真机采到的 offset 序列 + 人为叠加一个斜坡，用**与固件逐参数相同的**
    CUSUM 重算一遍。
这份脚本就是那条正方向证据。答辩时被问"你这个算法管用吗"，
答案不是一个形容词，是这张表。

固件里的对应实现
----------------
  firmware/GNSS_Bench/GNSS_Bench.ino  namespace integ::tick()  "③ CUSUM"
  常量：kCusumAlpha / kCusumMuA / kCusumSlack / kCusumH / kCusumMinM
  ★ 改固件常量必须同步改这里，否则这份验证立刻失效（会验证一个不存在的算法）。

用法
----
  python tools/cusum_replay.py                       # 用内置合成噪声自检
  python tools/cusum_replay.py --file offs.txt       # 用真机采到的 off 序列（每行一个，单位 m，1 Hz）
  python tools/cusum_replay.py --file offs.txt --ramp 0.1      # 叠加 0.1 m/s 的斜坡
  python tools/cusum_replay.py --sweep               # 扫一遍 0.02~0.4 m/s 的检出时间表
"""

import argparse
import math
import random
import sys
from pathlib import Path

# ---- 与固件逐值相同的参数（改一处必须改两处）----
ALPHA = 0.05      # kCusumAlpha 一阶低通
MU_A  = 0.003     # kCusumMuA   慢基线
SLACK = 1.0       # kCusumSlack 允许的起伏(m)
H     = 20.0      # kCusumH     判决门限(m·s)
GATE  = 8.0       # kCusumMinM  绝对闸(m)
WARM_S = 90       # kCusumWarmMs 预热秒数（只建 μ，不判决）
WARM_A = 0.15     # kCusumWarmA  预热期 μ 的 α


def run(series):
    """回放一条 1 Hz 的 offset 序列。

    返回 (统计量曲线, CUSUM 触发下标或 None, 绝对闸首次越过的下标或 None)。

    与 integ::tick() 的 ③ 段逐行对应：
        offFilt += ALPHA * (off - offFilt)
        dev      = offFilt - mu
        [i < WARM_S]  mu += WARM_A * dev;  cusum = 0        # 预热期只建基线
        [否则]        mu += MU_A  * dev;  cusum = max(0, cusum + dev - SLACK)
                      slow  = cusum > H and offFilt > GATE
    ★ 预热期（WARM_S）是实机上抓出来的补丁，见固件 kCusumWarmMs 的注释：
      开机头几分钟 μ 从不可靠的初值往真实工作点爬，这段爬升会被当成"漂"。
    ★ 固件里 `slow` 是这两条的**与**，所以下面两个下标哪个更晚，哪个才是真正的瓶颈。
      分开报出来，是为了回答一个很实际的问题：「这条判据到底在做什么？」
        · 闸先到 → 报的是"偏移够大了"；CUSUM 还没攒够，等于**在被闸放行之前拦一道**
        · CUSUM 先到 → 说明它比闸更早看到趋势，是真的"提前"
    """
    if not series:
        return [], None, None
    off_filt = series[0]
    mu = series[0]
    cusum = 0.0
    curve = []
    trip_at = None
    gate_at = None
    for i, off in enumerate(series):
        off_filt += ALPHA * (off - off_filt)
        dev = off_filt - mu
        if i < WARM_S:
            mu += WARM_A * dev
            cusum = 0.0
            curve.append(0.0)
            continue
        mu += MU_A * dev
        cusum = max(0.0, cusum + dev - SLACK)
        curve.append(cusum)
        if gate_at is None and off_filt > GATE:
            gate_at = i
        if trip_at is None and cusum > H and off_filt > GATE:
            trip_at = i
    return curve, trip_at, gate_at


def synthesize(n=600, base=1.0, sigma=1.0, seed=20261017):
    """静置时的 offset：围绕 base 的抖动（对数正态更贴近实际，恒为正）。"""
    rnd = random.Random(seed)
    out = []
    for _ in range(n):
        out.append(abs(base + rnd.gauss(0, sigma)))
    return out


def load(path):
    vals = []
    for ln in Path(path).read_text(encoding="utf-8", errors="replace").splitlines():
        ln = ln.strip()
        if not ln:
            continue
        try:
            vals.append(float(ln))
        except ValueError:
            continue
    return vals


def add_ramp(series, rate, start_frac=0.25):
    """从序列 1/4 处开始叠加一个线性斜坡（单位 m/s，采样 1 Hz）。"""
    k0 = int(len(series) * start_frac)
    out = []
    for i, v in enumerate(series):
        out.append(v + (rate * (i - k0) if i >= k0 else 0.0))
    return out, k0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--file", help="真机采到的 off 序列（每行一个，m，1 Hz）")
    ap.add_argument("--ramp", type=float, help="叠加的斜坡速度 m/s")
    ap.add_argument("--sweep", action="store_true", help="扫描多个斜坡速度，输出检出时间表")
    ap.add_argument("--n", type=int, default=600, help="无 --file 时合成多少个样本")
    args = ap.parse_args()

    base = load(args.file) if args.file else synthesize(args.n)
    if len(base) < 60:
        sys.exit("[X] 样本太少（%d 个，至少 60）" % len(base))

    src = args.file if args.file else "内置合成噪声"
    print("=" * 74)
    print(" CUSUM 离线回放 —— 参数与固件逐值相同")
    print("=" * 74)
    print(" 数据源 : %s  (%d 个样本 ≈ %d 秒)" % (src, len(base), len(base)))
    print(" 参数   : alpha=%.3f  muA=%.4f  slack=%.1f  H=%.1f  gate=%.1f  预热=%ds(α=%.2f)"
          % (ALPHA, MU_A, SLACK, H, GATE, WARM_S, WARM_A))

    # ---- 负方向：原样回放，统计量不该起来 ----
    curve, trip, _ = run(base)
    print("-" * 74)
    print(" 静置回放：统计量最大 %.2f / 门限 %.1f  -> %s"
          % (max(curve), H, "误报！" if trip is not None else "无误报"))
    if trip is not None:
        print("           ↑ 第 %d 秒触发，说明参数在当前噪声水平下会误报" % trip)

    # ---- 冷启动：开机那一下 off 从 0 跳到工作点 ----
    #  这正是 2026-10-17 实机上抓到的形态：设备一动不动，统计量却逐拍 +1.25。
    #  把 WARM_S 设成 0 跑同一条数据就能看到它涨到 20 —— 修复前后的对照实验。
    cold = [0.0] * 5 + [float(3.0)] * 300
    cc, ct, _ = run(cold)
    print(" 冷启动  : off 从 0 阶跃到 3 m（300 s）统计量最大 %.2f  -> %s"
          % (max(cc), "误报！" if ct is not None else "无误报（预热期按设计锁 0）"))

    # ---- 正方向：叠加斜坡 ----
    if args.ramp is not None:
        r = args.ramp
        s2, k0 = add_ramp(base, r)
        c2, t2, g2 = run(s2)
        print("-" * 74)
        if t2 is None:
            print(" 斜坡 %.2f m/s：未检出（%d 秒内）" % (r, len(s2) - k0))
        else:
            print(" 斜坡 %.2f m/s：第 %d 秒检出（起点后 %d 秒），此时偏移 ≈ %.1f m"
                  % (r, t2, t2 - k0, s2[t2]))
            if g2 is not None:
                print("          绝对闸在起点后 %d 秒越过（偏移 %.1f m）；"
                      "%s" % (g2 - k0, s2[g2],
                              "CUSUM 比闸更早" if t2 < g2 else "CUSUM 在闸之后又确认了一次"))

    if args.sweep:
        print("-" * 74)
        print(" %-10s %-12s %-14s %s" % ("斜坡 m/s", "检出(s)", "检出时偏移(m)", "说明"))
        for r in (0.02, 0.05, 0.10, 0.15, 0.20, 0.40):
            s2, k0 = add_ramp(base, r)
            c2, t2, g2 = run(s2)
            if t2 is None:
                print(" %-10.2f %-12s %-14s %s" % (r, "—", "—", "慢于 slack，不累积"))
            else:
                if g2 is None:
                    note = "CUSUM 先到（闸还没越过）"
                elif t2 < g2:
                    note = "CUSUM 先到（比闸早 %d s）" % (g2 - t2)
                else:
                    note = "闸先到，CUSUM 再确认 %d s" % (t2 - g2)
                print(" %-10.2f %-12d %-14.1f %s" % (r, t2 - k0, s2[t2], note))
    print("=" * 74)


if __name__ == "__main__":
    main()

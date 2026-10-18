# -*- coding: utf-8 -*-
"""协议号一致性哨兵 —— C3 与 S3 的 `kProto` 必须**完全相等**。

背景(2026-10-16,协议号 P1 落地当天):
    `kProto` 是链路**协议版本号**(只在报文格式/语义变化时才 +1),两板靠它在
    开机握手里互报,不一致就屏上/串口点名。它和固件版本号是两件事:
    固件号两轨独立、每版都变,**不可比**;协议号是"能不能对话"的唯一答案。

    正因为它**必须两边一样**,它天然属于"必须与别处一致、又靠手抄"那一类 ——
    本项目在同一个坑里付过两次学费:
      · 2026-10-15 白名单手抄漏了 'd'  -> 一条菜单项**静默失灵**;
      · 2026-10-16 心跳掉进 else 分支  -> 串口每 2 秒刷一行,把真回执淹掉。
    两个的形态完全一样:**不崩、不报错、串口照常打印,只是行为少了**。
    ⇒ 规矩:发现耦合的**第一动作是写校验脚本**,不靠人眼比对。

    ⚠ 不一致的后果比上面两个更隐蔽:两板各自照常工作、命令照常执行,
      只是屏上多一行 `PROTO MISMATCH`。要是没人看屏,这个错能一直躺着。

用法:
    python tools/check_proto_sync.py            # 跑一次,不一致则 exit 1
    python tools/check_proto_sync.py --show     # 附带打印两边出处(文件:行号)
    python tools/check_proto_sync.py --c3 <f> --s3 <f>     # 换文件(自检用)

退出码: 0 = 一致; 1 = 不一致 / 有一边没找到(找不到就得报,别当"通过")。
"""

import argparse
import re
import sys
from pathlib import Path

# tools/check_proto_sync.py -> 项目根 = 上一级
ROOT = Path(__file__).resolve().parent.parent
C3_INO = ROOT / "firmware" / "C3_CmdPad" / "C3_CmdPad.ino"
S3_INO = ROOT / "firmware" / "GNSS_Bench" / "GNSS_Bench.ino"

# 形态:  constexpr uint8_t kProto = 1;            // P1
#   ★ 允许 `constexpr` / `const` 与任意空白;数字后允许行尾注释。
#   ★ 故意**收紧**成"必须有 constexpr|const" —— 免得把注释里举例的 `kProto = 2`
#     也匹配上(排障文档里就会写这种例子),那样校验器反而会报假警。
PROTO_RE = re.compile(
    r'^[ \t]*(?:constexpr|const)\s+(?:uint8_t|unsigned\s+char|int)\s+'
    r'kProto\s*=\s*(\d+)\s*;',
    re.MULTILINE,
)


def read(p: Path) -> str:
    if not p.exists():
        sys.exit("[X] 找不到文件: %s" % p)
    return p.read_text(encoding="utf-8", errors="replace")


def find_proto(src: str, path: Path):
    """-> (值:int, 行号:int) 或 (None, None)"""
    m = PROTO_RE.search(src)
    if not m:
        return None, None
    line = src[: m.start()].count("\n") + 1
    return int(m.group(1)), line


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--show", action="store_true", help="打印两边的定义出处")
    ap.add_argument("--c3", default=str(C3_INO))
    ap.add_argument("--s3", default=str(S3_INO))
    args = ap.parse_args()

    c3p, s3p = Path(args.c3), Path(args.s3)
    c3v, c3n = find_proto(read(c3p), c3p)
    s3v, s3n = find_proto(read(s3p), s3p)

    print("=" * 70)
    print(" 协议号一致性哨兵  (C3 kProto  vs  S3 kProto)")
    print("=" * 70)
    print("  C3 : %s" % c3p)
    print("  S3 : %s" % s3p)
    print("-" * 70)

    bad = False
    if c3v is None:
        print("  [X] C3 里没找到 `constexpr ... kProto = <数字>;` —— 解析规则失效了,别信这个结果。")
        bad = True
    if s3v is None:
        print("  [X] S3 里没找到 `constexpr ... kProto = <数字>;` —— 解析规则失效了,别信这个结果。")
        bad = True
    if bad:
        return 1

    print("  C3 kProto = P%d" % c3v)
    print("  S3 kProto = P%d" % s3v)
    if args.show:
        print("-" * 70)
        print("    C3 出处: %s:%d" % (c3p, c3n))
        print("    S3 出处: %s:%d" % (s3p, s3n))
    print("-" * 70)

    if c3v != s3v:
        print("  [BAD] 两板协议号**不一致**:C3=P%d,S3=P%d。" % (c3v, s3v))
        print("        症状是『版本不匹配』与『线接错了』长得一模一样,而两板各自照常工作 ——")
        print("        改哪一边都行,但**改完必须与对端同批烧**(见 docs/配套表-双板烧录.md)。")
        return 1

    print("  [OK] 一致 —— 两板都是 P%d。" % c3v)
    print("       (提醒:协议号只在**报文格式/语义**变化时才 +1;加了新命令但格式没变,")
    print("        不动它 —— 那属于固件号 MINOR 的范畴。)")
    return 0


if __name__ == "__main__":
    sys.exit(main())

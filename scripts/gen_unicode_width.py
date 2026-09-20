#!/usr/bin/env python3
"""从 unicode.org 拉取钉版 EastAsianWidth.txt，生成紧凑区间表 .inc（开发期工具，不进构建链）。

用法：python3 scripts/gen_unicode_width.py（仓库根目录下运行；仅标准库）。
"""
import datetime
import os
import sys
import tempfile
import urllib.request

UNICODE_VERSION = "16.0.0"
URL = f"https://www.unicode.org/Public/{UNICODE_VERSION}/ucd/EastAsianWidth.txt"
OUT = "include/ZzTerm/detail/UnicodeWidthData.inc"
KEEP = {"W": "Wide", "F": "Fullwidth", "A": "Ambiguous"}


def main() -> int:
    try:
        with urllib.request.urlopen(URL, timeout=30) as resp:
            if resp.status != 200:
                print(f"下载失败：HTTP {resp.status}", file=sys.stderr)
                return 1
            text = resp.read().decode("utf-8")
    except Exception as exc:  # 网络不可达等
        print(f"下载失败：{exc}", file=sys.stderr)
        return 1

    intervals = []  # [(lo, hi, cls)]
    for lineno, raw in enumerate(text.splitlines(), 1):
        line = raw.split("#", 1)[0].strip()
        if not line:
            continue
        fields = [f.strip() for f in line.split(";")]
        if len(fields) != 2:
            print(f"第 {lineno} 行格式不符：{raw!r}", file=sys.stderr)
            return 1
        rng, cls = fields
        if cls not in KEEP:
            continue
        lo_s, _, hi_s = rng.partition("..")
        lo, hi = int(lo_s, 16), int(hi_s or lo_s, 16)
        if lo > hi:
            print(f"第 {lineno} 行区间方向异常：{rng!r}", file=sys.stderr)
            return 1
        intervals.append((lo, hi, cls))

    intervals.sort()
    merged = []
    for lo, hi, cls in intervals:
        if merged and merged[-1][2] == cls and lo <= merged[-1][1] + 1:
            merged[-1] = (merged[-1][0], max(merged[-1][1], hi), cls)
        else:
            merged.append((lo, hi, cls))
    for prev, cur in zip(merged, merged[1:]):
        if cur[0] <= prev[1]:
            print(f"合并后区间重叠：{prev} vs {cur}", file=sys.stderr)
            return 1

    date = datetime.date.today().isoformat()
    out = [
        "// 本文件由 scripts/gen_unicode_width.py 生成，请勿手改。",
        f"// 数据源：{URL}",
        f"// Unicode 版本：{UNICODE_VERSION}  生成日期：{date}",
        "// 仅收录 W/F/A 三类；N/Na/H 与未列出码位默认窄（返回 1），不入表。",
        "// 区间按 lo 升序、互不重叠，供 zzCellWidthOf 二分查找。",
        f"inline constexpr std::array<ZzEawInterval, {len(merged)}> kZzEawIntervals {{{{",
    ]
    out += [
        f"    ZzEawInterval{{0x{lo:04X}u, 0x{hi:04X}u, ZzEawClass::{KEEP[cls]}}},"
        for lo, hi, cls in merged
    ]
    out.append("}};")
    content = "\n".join(out) + "\n"

    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    fd, tmp = tempfile.mkstemp(dir=os.path.dirname(OUT), suffix=".tmp")
    with os.fdopen(fd, "w") as fh:
        fh.write(content)
    os.replace(tmp, OUT)  # 原子替换，不产生半截生成物
    print(f"生成 {OUT}：{len(merged)} 个区间，Unicode {UNICODE_VERSION}")
    return 0


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
"""从 unicode.org 拉取钉版 UCD 四份文件，生成 UAX #29 聚簇断行区间表 .inc
与 golden 测试数据（开发期工具，不进构建链）。

用法：python3 scripts/gen_grapheme_break.py（仓库根目录下运行；仅标准库）。

数据源（Unicode 16.0.0）：
- GraphemeBreakProperty.txt：全部 GCB 类（未列出码位默认 Other 不入表）
- emoji-data.txt：Extended_Pictographic 与 Emoji 区间（Emoji 属性为
  VS16 变宽资格标志——M7c T3 实测：数字/#/*/©/‼ + VS16 均判宽而 a+VS16
  保窄，判别属性是 Emoji 而非 Emoji_Presentation——数字系 EP=No）
- DerivedCoreProperties.txt：InCB; Linker、InCB; Consonant 与 InCB; Extend
  区间全收（InCB=Extend ⊊ GCB=Extend 真子集——GCB=Extend 而 InCB=None
  的码位 16.0.0 实测仅 U+200C，不能由 GCB 表达，故显式入表）
- GraphemeBreakTest.txt：官方 golden，原样落盘 tests/data/

三份属性区间取边界并集做统一区间打包：每条区间携带
gcb（4 位枚举值，对应 ZzGcb）+ flags（bit0=ExtPic，bit1-2=InCB
0=None/1=Linker/2=Consonant/3=Extend，bit3=Emoji），与
src/unicode/GraphemeBreak.h 的 ZzGcbInterval 布局一致。
"""
import datetime
import os
import sys
import tempfile
import urllib.request

UNICODE_VERSION = "16.0.0"
BASE = f"https://www.unicode.org/Public/{UNICODE_VERSION}/ucd"
URL_GCB = f"{BASE}/auxiliary/GraphemeBreakProperty.txt"
URL_EMOJI = f"{BASE}/emoji/emoji-data.txt"
URL_DCP = f"{BASE}/DerivedCoreProperties.txt"
URL_TEST = f"{BASE}/auxiliary/GraphemeBreakTest.txt"

OUT_INC = "include/ZzTerm/detail/GraphemeBreakData.inc"
OUT_TEST = "tests/data/GraphemeBreakTest.txt"

# 与 src/unicode/GraphemeBreak.h 的 enum class ZzGcb 顺序一致（Other=0 不入表）。
GCB_ENUM = [
    "Other", "CR", "LF", "Control", "Extend", "Prepend", "SpacingMark",
    "L", "V", "T", "LV", "LVT", "Regional_Indicator", "ZWJ",
]
GCB_INDEX = {name: i for i, name in enumerate(GCB_ENUM)}

# InCB：0=None 1=Linker 2=Consonant 3=Extend（与 enum class ZzIncb 一致）。
INCB_INDEX = {"Linker": 1, "Consonant": 2, "Extend": 3}


def fail(msg: str) -> int:
    print(msg, file=sys.stderr)
    return 1


def download(url: str) -> str:
    with urllib.request.urlopen(url, timeout=60) as resp:
        if resp.status != 200:
            raise RuntimeError(f"HTTP {resp.status}")
        return resp.read().decode("utf-8")


def parse_range(rng: str):
    lo_s, _, hi_s = rng.partition("..")
    lo, hi = int(lo_s, 16), int(hi_s or lo_s, 16)
    if lo > hi:
        raise ValueError(f"区间方向异常：{rng!r}")
    return lo, hi


def parse_gcb(text: str):
    """GraphemeBreakProperty.txt → [(lo, hi, gcb_index)]，收录全部 GCB 类。"""
    out = []
    for lineno, raw in enumerate(text.splitlines(), 1):
        line = raw.split("#", 1)[0].strip()
        if not line:
            continue
        fields = [f.strip() for f in line.split(";")]
        if len(fields) != 2 or fields[1] not in GCB_INDEX:
            raise ValueError(f"GraphemeBreakProperty 第 {lineno} 行格式不符：{raw!r}")
        lo, hi = parse_range(fields[0])
        out.append((lo, hi, GCB_INDEX[fields[1]]))
    return out


def parse_emoji(text: str, prop: str):
    """emoji-data.txt → [(lo, hi)]，仅收 prop 属性（Extended_Pictographic / Emoji）。"""
    out = []
    for lineno, raw in enumerate(text.splitlines(), 1):
        line = raw.split("#", 1)[0].strip()
        if not line:
            continue
        fields = [f.strip() for f in line.split(";")]
        if len(fields) != 2:
            raise ValueError(f"emoji-data 第 {lineno} 行格式不符：{raw!r}")
        if fields[1] != prop:
            continue
        out.append(parse_range(fields[0]))
    return out


def parse_incb(text: str):
    """DerivedCoreProperties.txt → [(lo, hi, incb_index)]，Linker/Consonant/Extend 全收。"""
    out = []
    for lineno, raw in enumerate(text.splitlines(), 1):
        line = raw.split("#", 1)[0].strip()
        if not line:
            continue
        fields = [f.strip() for f in line.split(";")]
        if len(fields) < 2 or fields[1] != "InCB":
            continue
        if len(fields) != 3:
            raise ValueError(f"DerivedCoreProperties 第 {lineno} 行格式不符：{raw!r}")
        kind = fields[2]
        if kind not in INCB_INDEX:
            raise ValueError(f"DerivedCoreProperties 第 {lineno} 行未知 InCB 类：{raw!r}")
        lo, hi = parse_range(fields[0])
        out.append((lo, hi, INCB_INDEX[kind]))
    return out


def lookup(intervals, cp, default):
    """在按 lo 升序、互不重叠的 [(lo, hi, val...)] 中二分查 cp 的属性值。"""
    lo_i, hi_i = 0, len(intervals)
    while lo_i < hi_i:
        mid = (lo_i + hi_i) // 2
        e = intervals[mid]
        if cp < e[0]:
            hi_i = mid
        elif cp > e[1]:
            lo_i = mid + 1
        else:
            return e[2] if len(e) > 2 else True
    return default


def atomic_write(path: str, content: str) -> None:
    os.makedirs(os.path.dirname(path), exist_ok=True)
    fd, tmp = tempfile.mkstemp(dir=os.path.dirname(path), suffix=".tmp")
    with os.fdopen(fd, "w") as fh:
        fh.write(content)
    os.replace(tmp, path)  # 原子替换，不产生半截生成物


def main() -> int:
    try:
        texts = {url: download(url) for url in (URL_GCB, URL_EMOJI, URL_DCP, URL_TEST)}
    except Exception as exc:  # 网络不可达等
        return fail(f"下载失败：{exc}")

    try:
        gcb = sorted(parse_gcb(texts[URL_GCB]))
        extpic = sorted(parse_emoji(texts[URL_EMOJI], "Extended_Pictographic"))
        emoji = sorted(parse_emoji(texts[URL_EMOJI], "Emoji"))
        incb = sorted(parse_incb(texts[URL_DCP]))
    except ValueError as exc:
        return fail(str(exc))

    # 源内区间互不重叠校验（同 gen_unicode_width.py:55-58 惯例）。
    for name, ivs in (("GCB", gcb), ("ExtPic", extpic), ("Emoji", emoji), ("InCB", incb)):
        for prev, cur in zip(ivs, ivs[1:]):
            if cur[0] <= prev[1]:
                return fail(f"{name} 源区间重叠：{prev} vs {cur}")

    # 统一边界合并：四份区间取边界并集，逐统一区间采样打包属性。
    bounds = set()
    for ivs in (gcb, extpic, emoji, incb):
        for lo, hi, *_ in ivs:
            bounds.add(lo)
            bounds.add(hi + 1)
    bounds = sorted(bounds)

    unified = []  # (lo, hi, gcb_index, flags)
    for lo, hi_excl in zip(bounds, bounds[1:]):
        hi = hi_excl - 1
        g = lookup(gcb, lo, 0)          # 未列出 → Other(0)
        f = 0
        if lookup(extpic, lo, False):
            f |= 0x1
        f |= lookup(incb, lo, 0) << 1
        if lookup(emoji, lo, False):
            f |= 0x8
        if g == 0 and f == 0:           # Other 且无附加属性：不入表
            continue
        if unified and unified[-1][2] == g and unified[-1][3] == f \
                and lo == unified[-1][1] + 1:
            unified[-1] = (unified[-1][0], hi, g, f)
        else:
            unified.append((lo, hi, g, f))
    for prev, cur in zip(unified, unified[1:]):
        if cur[0] <= prev[1]:
            return fail(f"统一区间重叠：{prev} vs {cur}")

    date = datetime.date.today().isoformat()
    out = [
        "// 本文件由 scripts/gen_grapheme_break.py 生成，请勿手改。",
        f"// 数据源：{URL_GCB}",
        f"//         {URL_EMOJI}",
        f"//         {URL_DCP}",
        f"//         {URL_TEST}",
        f"// Unicode 版本：{UNICODE_VERSION}  生成日期：{date}",
        "// gcb 列为 ZzGcb 枚举值（Other=0 且无附加属性的码位不入表）；",
        "// flags 位布局：bit0=Extended_Pictographic，bit1-2=InCB（0=None/1=Linker/2=Consonant/3=Extend），bit3=Emoji。",
        "// 区间按 lo 升序、互不重叠，供 zzGraphemePropsOf 二分查找。",
        f"inline constexpr std::array<ZzGcbInterval, {len(unified)}> kZzGcbIntervals {{{{",
    ]
    for lo, hi, g, f in unified:
        flags_desc = []
        if f & 0x1:
            flags_desc.append("ExtPic")
        incb_v = (f >> 1) & 0x3
        if incb_v:
            flags_desc.append({1: "Linker", 2: "Consonant", 3: "InCB_Extend"}[incb_v])
        if f & 0x8:
            flags_desc.append("Emoji")
        desc = " ".join(flags_desc) if flags_desc else "-"
        out.append(
            f"    ZzGcbInterval{{0x{lo:04X}u, 0x{hi:04X}u, {g}u, {f}u}},"
            f" // {GCB_ENUM[g]} {desc}")
    out.append("}};")
    atomic_write(OUT_INC, "\n".join(out) + "\n")

    # golden 数据源原样落盘（下载失败已在上方报错退出，此处必为完整文件）。
    atomic_write(OUT_TEST, texts[URL_TEST])

    print(f"生成 {OUT_INC}：{len(unified)} 个统一区间，Unicode {UNICODE_VERSION}")
    print(f"生成 {OUT_TEST}：{len(texts[URL_TEST])} 字节")
    return 0


if __name__ == "__main__":
    sys.exit(main())

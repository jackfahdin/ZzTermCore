#!/usr/bin/env python3
"""ZzTermSmoke 自动化交互验证：pexpect 驱动真实 PTY，pyte 作为独立终端模拟器
解释 demo 的渲染输出，逐步断言屏幕内容。全程保存原始字节日志与屏幕快照。

用法：verify_smoke.py <ZzTermSmoke 路径> [工作目录（默认 /tmp/zz-smoke-verify）] [backend（默认 contour）]
依赖：python3 + pexpect + pyte（pip install pexpect pyte）。
退出码：0 全部通过；1 有失败断言；2 脚本自身异常。

M2 追加：CJK 混排格位、1049 备用屏裸序列进出、DECTCEM ?25 端到端断言。
M3a 追加：输入链路端到端——sendText 回显、sendKey 方向键（DECCKM 下 SS3）
召回 bash 历史；vim 步骤同时断言退出后主屏恢复。stdin 不再透传 PTY，
而是经 demo InputTranslator -> Core sendText/sendKey -> output 通道。
Core 层同语义另有双后端逐格强对照（tests/unit/test_backend_compat.cpp 的
testCjkWide / testAltScreen / testCursorVisibility），此处走真实 PTY + bash，
两层互补不重复。
M3b 追加：vim 鼠标点击定位——合成 SGR 1006 点击经 demo InputTranslator ->
sendMouse -> PTY -> vim，以 vim ruler 状态行文本断言（Contour 备用屏
不上报光标，见步骤 14 注释）。
M4 追加：resize reflow 步骤 5b/5c/5d——200 字符长行在 100 列占 2 行，
窄化 50 列重排为 4 行、回宽 100 列合并回 2 行，拼接整屏断言标记串
在三种列宽下均完整可见（双后端）。"""
import os
import re
import sys
import time
import traceback

import pexpect
import pyte

COLS, ROWS = 80, 24
DEMO = os.path.abspath(sys.argv[1]) if len(sys.argv) > 1 else "ZzTermSmoke"
OUT = sys.argv[2] if len(sys.argv) > 2 else "/tmp/zz-smoke-verify"
BACKEND = sys.argv[3] if len(sys.argv) > 3 else "contour"
PLAY = os.path.join(OUT, "playground")

failures = []
step_no = 0


def screen_text(screen):
    return "\n".join(screen.display)


def settle(child, stream, quiet=0.6, overall=10.0):
    """读取直到输出平静 quiet 秒（或总超时 overall）。"""
    deadline = time.time() + overall
    while time.time() < deadline:
        try:
            data = child.read_nonblocking(size=65536, timeout=quiet)
            stream.feed(data)
        except pexpect.TIMEOUT:
            return True
        except pexpect.EOF:
            return False
    return False


def snapshot(screen, label):
    global step_no
    step_no += 1
    path = os.path.join(OUT, f"screen-{step_no:02d}-{label}.txt")
    with open(path, "w", encoding="utf-8") as f:
        f.write(f"=== {label} ===\n")
        f.write(screen_text(screen))
        f.write("\n=== end ===\n")
    return path


def check(ok, name, detail=""):
    tag = "PASS" if ok else "FAIL"
    print(f"[{tag}] [{BACKEND}] {name} {detail}", flush=True)
    if not ok:
        failures.append(name)


def color_cells(screen):
    """返回屏幕上前景色非 default 的 (row, col, fg) 采样。"""
    hits = []
    for row, line in screen.buffer.items():
        for col, ch in line.items():
            if ch.fg != "default" and ch.data.strip():
                hits.append((row, col, ch.fg, ch.data))
    return hits


def main():
    os.makedirs(PLAY, exist_ok=True)
    os.makedirs(os.path.join(PLAY, "subdir"), exist_ok=True)
    with open(os.path.join(PLAY, "hello.txt"), "w") as f:
        f.write("hello\n")
    with open(os.path.join(PLAY, "lines.txt"), "w") as f:
        for i in range(1, 61):
            f.write(f"line-{i:02d}\n")

    screen = pyte.Screen(COLS, ROWS)
    stream = pyte.ByteStream(screen)

    env = dict(os.environ, TERM="xterm-256color", LANG="C.UTF-8")
    child = pexpect.spawn(DEMO, [f"--backend={BACKEND}"], cwd=PLAY, dimensions=(ROWS, COLS),
                          encoding=None, timeout=10, env=env)
    rawlog = open(os.path.join(OUT, "raw.log"), "wb")
    child.logfile_read = rawlog

    # 1. 启动：bash 提示符应出现
    settle(child, stream)
    snap = snapshot(screen, "startup-prompt")
    txt = screen_text(screen)
    check("bash" in txt or "$" in txt or "#" in txt, "1.启动-bash提示符", f"(快照 {snap})")

    # 2. echo 标记
    child.sendline("echo zz-auto-echo-ok")
    settle(child, stream)
    snap = snapshot(screen, "echo-marker")
    check("zz-auto-echo-ok" in screen_text(screen), "2.echo标记", f"(快照 {snap})")

    # 3. ls --color：文件名可见 + 至少一个非默认前景色（目录蓝）
    child.sendline("ls --color")
    settle(child, stream)
    snap = snapshot(screen, "ls-color")
    txt = screen_text(screen)
    check("hello.txt" in txt and "subdir" in txt, "3.ls内容可见", f"(快照 {snap})")
    hits = color_cells(screen)
    check(len(hits) > 0, "3.ls着色生效", f"非默认色单元格 {len(hits)} 个，采样 {hits[:3]}")

    # 4. stty size：验证 PTY 尺寸透传
    child.sendline("stty size")
    settle(child, stream)
    snapshot(screen, "stty-size")
    check(f"{ROWS} {COLS}" in screen_text(screen), "4.stty尺寸透传",
          f"期望 '{ROWS} {COLS}'")

    # 5. SIGWINCH：改外层尺寸 -> demo 应同步内层 PTY 与 Core
    child.setwinsize(20, 100)
    time.sleep(0.3)
    settle(child, stream)
    # demo resize 后 Core 屏幕变为 100x20；换 pyte 屏幕尺寸继续解释
    screen.resize(20, 100)
    child.sendline("stty size")
    settle(child, stream)
    snap = snapshot(screen, "after-resize")
    check("20 100" in screen_text(screen), "5.SIGWINCH同步", f"(快照 {snap})")

    # 5b. resize reflow（M4）：200 字符长行在 100 列占 2 行，50 列应重排为 4 行
    marker = "zzreflow" + "x" * 192
    child.sendline(f"printf '%s\\n' '{marker}'")
    settle(child, stream)
    snap = snapshot(screen, "reflow-before")
    check(marker in "".join(line.rstrip() for line in screen.display),
          "5b.reflow基线(100列)", f"(快照 {snap})")
    # 每次 setwinsize 后 pyte 屏幕须与 Core 同尺寸再触发重绘：先 settle 排干
    # bash 的 SIGWINCH 提示符重绘（pyte 仍是旧几何，内容随即被覆盖），
    # screen.resize 对齐后由 stty 触发一次几何一致的全屏重绘（同步骤 5 的
    # 既定模式）。否则变宽时 100 列重绘落进 50 列 pyte 会折行滚屏，把
    # marker 顶出 pyte 可视区造成假性失败（native 后端实测钉住）。
    child.setwinsize(20, 50)
    time.sleep(0.3)
    settle(child, stream)
    screen.resize(20, 50)
    child.sendline("stty size")  # 触发重绘确认尺寸同步
    settle(child, stream)
    snap = snapshot(screen, "reflow-narrow-50")
    check(marker in "".join(line.rstrip() for line in screen.display),
          "5c.reflow窄化重排(50列)", f"(快照 {snap})")
    child.setwinsize(20, 100)
    time.sleep(0.3)
    settle(child, stream)
    screen.resize(20, 100)
    child.sendline("stty size")
    settle(child, stream)
    snap = snapshot(screen, "reflow-back-100")
    check(marker in "".join(line.rstrip() for line in screen.display),
          "5d.reflow变宽合并(回100列)", f"(快照 {snap})")

    # 6. CJK 混排：宽字符格位断言（pyte 与 Core 同按 UAX #11 解释宽度）
    child.sendline("printf 'AB中文C-zz\\n'")
    settle(child, stream)
    snap = snapshot(screen, "cjk-mixed")
    check("AB中文C-zz" in screen_text(screen), "6.CJK混排可见", f"(快照 {snap})")
    # 格位：中/文 各占 2 列，命中行内应为 A B 中 _ 文 _ C - z z
    row_cols = None
    for r, line in screen.buffer.items():
        cols = {c: ch.data for c, ch in line.items()}
        for c, d in cols.items():
            if d == "中" and cols.get(c - 2) == "A" and cols.get(c - 1) == "B":
                row_cols = cols
                break
        if row_cols is not None:
            break
    ok_cells = False
    if row_cols is not None:
        c = next(cc for cc, d in row_cols.items() if d == "中")
        ok_cells = (row_cols.get(c + 2) == "文" and row_cols.get(c + 4) == "C"
                    and row_cols.get(c + 5) == "-")
    check(ok_cells, "6.CJK格位(宽2列)", f"命中列 {c if row_cols else None}")

    # 7. DECTCEM：Core 上报真实 cursor().visible，demo 重绘据此决定是否发 ?25h，
    # pyte 光标 hidden 状态即端到端证据。
    # 注意须排在任何 1049 之前：Contour 自 1049h 起 RenderBuffer 不再上报光标
    #（visible 恒 false，?25h 亦不复现，compat testAltScreen 钉住的上游分歧），
    # 1049 之后再断言 ?25h 恢复会对 Contour 误报失败。
    child.sendline("printf '\\e[?25l'")
    settle(child, stream)
    check(screen.cursor.hidden, "7.光标隐藏(?25l)")
    child.sendline("printf '\\e[?25h'")
    settle(child, stream)
    check(not screen.cursor.hidden, "7.光标恢复(?25h)")

    # 8. 1049 备用屏裸序列进出：主屏标记 -> 1049h -> alt 写标记 -> 1049l -> 主屏恢复
    child.sendline("printf 'zzMAIN-\\e[?1049h'")
    settle(child, stream)
    child.sendline("printf 'zzALT-IN'")  # 落在备用屏
    settle(child, stream)
    snap = snapshot(screen, "alt-1049")
    check("zzALT-IN" in screen_text(screen), "8.备用屏写入(1049h)", f"(快照 {snap})")
    child.sendline("printf '\\e[?1049l\\n'")
    settle(child, stream)
    snap = snapshot(screen, "alt-1049-exit")
    txt = screen_text(screen)
    check("zzMAIN-" in txt and "zzALT-IN" not in txt, "8.主屏恢复(1049l)", f"(快照 {snap})")

    # 9. less：打开长文件、翻页、退出；less 的 smcup/rmcup 即 1049h/1049l，
    # 退出后主屏标记应随 1049l 恢复
    child.sendline("echo zz-main-mark-1049")
    settle(child, stream)
    child.sendline("less lines.txt")
    settle(child, stream)
    snap = snapshot(screen, "less-open")
    check("line-01" in screen_text(screen), "9.less打开", f"(快照 {snap})")
    child.send(" ")  # 翻页
    settle(child, stream)
    snap = snapshot(screen, "less-page2")
    check("line-20" in screen_text(screen) or "line-19" in screen_text(screen),
          "9.less翻页", f"(快照 {snap})")
    child.send("q")
    settle(child, stream)
    snap = snapshot(screen, "less-quit")
    check("lines.txt" not in screen_text(screen) or "$" in screen_text(screen),
          "9.less退出回提示符", f"(快照 {snap})")
    check("zz-main-mark-1049" in screen_text(screen), "9.less退出主屏恢复(1049)",
          f"(快照 {snap})")

    # 10. vim：打开、插入、保存退出
    vim_path = os.path.join(PLAY, "vim-test.txt")
    if os.path.exists(vim_path):
        os.remove(vim_path)  # 可重复运行：避免上次内容干扰
    child.sendline("vim -u NONE vim-test.txt")
    settle(child, stream, quiet=1.0)
    snap = snapshot(screen, "vim-open")
    txt = screen_text(screen)
    check("~" in txt or "vim-test.txt" in txt, "10.vim打开", f"(快照 {snap})")
    child.send("i")
    child.send("zz-vim-content")
    time.sleep(0.3)
    child.send("\x1b")  # Esc
    time.sleep(0.3)
    child.send(":wq\r")
    settle(child, stream)
    snap = snapshot(screen, "vim-quit")
    ok_file = os.path.exists(vim_path)
    content = open(vim_path).read() if ok_file else ""
    check(ok_file and "zz-vim-content" in content, "10.vim写入并退出",
          f"文件内容 {content!r} (快照 {snap})")
    # M3a：vim 经 1049 备用屏进出，退出后主屏标记应恢复可见（原屏恢复证据）。
    check("zz-main-mark-1049" in screen_text(screen), "10.vim退出主屏恢复(1049)",
          f"(快照 {snap})")

    # 11. Ctrl+C：中断 sleep，提示符恢复
    child.sendline("sleep 100")
    time.sleep(0.5)
    settle(child, stream)
    child.sendcontrol("c")
    settle(child, stream)
    child.sendline("echo zz-after-ctrlc")
    settle(child, stream)
    snap = snapshot(screen, "ctrl-c")
    check("zz-after-ctrlc" in screen_text(screen), "11.Ctrl+C中断恢复", f"(快照 {snap})")

    # 12. 输入链路（M3a）：按键经 Core sendText -> PTY -> bash 回显。
    # stdin 字节不再透传，而是经 demo InputTranslator -> sendText/sendKey -> output 通道。
    child.send("echo M3A_INPUT_OK\r")
    settle(child, stream)
    snap = snapshot(screen, "input-sendtext-echo")
    check("M3A_INPUT_OK" in screen_text(screen), "12.输入链路-sendText回显", f"(快照 {snap})")

    # 13. 方向键经 sendKey + DECCKM——bash readline 启用 application cursor（?1h），
    # Up 编码为 SS3 OA 才能召回历史。断言召回命令出现在当前提示符行（屏幕
    # 最后一个非空行），而非整屏——否则即使 Up 未生效也会命中步骤 12 的
    # 回显残留行造成假性通过（任务审查钉住）。注意不能用 clear 清屏：
    # clear 自身进入历史成为最新条目，Up 会召回 clear 而非目标命令。
    child.send("\x1b[A")  # Up：经 InputTranslator -> sendKey(Up)
    settle(child, stream)
    snap = snapshot(screen, "input-up-recall")
    nonempty = [ln for ln in screen_text(screen).splitlines() if ln.strip()]
    prompt_line = nonempty[-1] if nonempty else ""
    check("echo M3A_INPUT_OK" in prompt_line, "13.输入链路-Up召回历史",
          f"提示符行 {prompt_line!r} (快照 {snap})")
    child.send(chr(3))  # Ctrl+C 放弃该行（0x03 经 sendText 透传控制字节）
    settle(child, stream)
    # Ctrl+C 使 $? = 130，裸 exit 会以 130 退出；跑一条成功命令清零，保持
    # 末步"exit 干净退出（exitstatus == 0）"语义与既有一致。
    child.sendline("true")
    settle(child, stream)

    # 14. 鼠标（M3b）——vim mouse=a，合成 SGR 点击经 InputTranslator ->
    # sendMouse -> PTY -> vim；点击定位结果经 vim ruler（状态行右下
    # "行,列" 文本）断言。两点实测钉住（2026-09-20，双后端）：
    # - 空缓冲区点击会被 vim 钳到最近文本位（仅 1 行时落 (1,1)），故
    #   预置 12 行内容使点击行/列精确可达；
    # - Contour 进 1049 备用屏后 RenderBuffer 不再上报光标（步骤 7 注释
    #   钉住的上游分歧），pyte screen.cursor 对 Contour 是主屏残留值，
    #   不能用光标位置断言——ruler 是屏幕文本，两后端均可读。
    mouse_path = os.path.join(PLAY, "mouse-m3b.txt")
    with open(mouse_path, "w") as f:  # 每次重写：可重复运行
        for i in range(1, 13):
            f.write(f"m3b-line-{i:02d} aaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\n")
    child.sendline("vim -u NONE -c 'set mouse=a ruler' mouse-m3b.txt")
    settle(child, stream, quiet=1.0)
    child.send("\x1b[<0;10;5M")  # 左键点击第 5 行第 10 列（SGR 1006，1 起始）
    settle(child, stream)
    snap = snapshot(screen, "vim-mouse-click")
    status_line = screen_text(screen).splitlines()[-1]
    m = re.search(r"(\d+),(\d+)", status_line)
    ok_click = m is not None and abs(int(m.group(1)) - 5) <= 1 \
        and abs(int(m.group(2)) - 10) <= 1
    check(ok_click, "14.vim鼠标点击定位",
          f"状态行 {status_line.strip()!r} (快照 {snap})")
    child.send(":q!\r")
    settle(child, stream)

    # 15. exit：demo 应以子进程退出码 0 退出
    child.sendline("exit")
    try:
        child.expect(pexpect.EOF, timeout=10)
    except pexpect.TIMEOUT:
        pass
    child.close()
    check(child.exitstatus == 0, "15.exit干净退出", f"exitstatus={child.exitstatus}")

    rawlog.close()
    print(f"\n原始字节日志: {OUT}/raw.log")
    print(f"屏幕快照: {OUT}/screen-*.txt")
    if failures:
        print(f"\n{len(failures)} 项失败: {failures}")
        return 1
    print("\n全部通过")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception:
        traceback.print_exc()
        sys.exit(2)

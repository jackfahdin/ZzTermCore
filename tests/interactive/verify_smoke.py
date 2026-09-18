#!/usr/bin/env python3
"""ZzTermSmoke 自动化交互验证：pexpect 驱动真实 PTY，pyte 作为独立终端模拟器
解释 demo 的渲染输出，逐步断言屏幕内容。全程保存原始字节日志与屏幕快照。

用法：verify_smoke.py <ZzTermSmoke 路径> [工作目录（默认 /tmp/zz-smoke-verify）]
依赖：python3 + pexpect + pyte（pip install pexpect pyte）。
退出码：0 全部通过；1 有失败断言；2 脚本自身异常。"""
import os
import sys
import time
import traceback

import pexpect
import pyte

COLS, ROWS = 80, 24
DEMO = os.path.abspath(sys.argv[1]) if len(sys.argv) > 1 else "ZzTermSmoke"
OUT = sys.argv[2] if len(sys.argv) > 2 else "/tmp/zz-smoke-verify"
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
    print(f"[{tag}] {name} {detail}", flush=True)
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
    child = pexpect.spawn(DEMO, [], cwd=PLAY, dimensions=(ROWS, COLS),
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

    # 6. less：打开长文件、翻页、退出
    child.sendline("less lines.txt")
    settle(child, stream)
    snap = snapshot(screen, "less-open")
    check("line-01" in screen_text(screen), "6.less打开", f"(快照 {snap})")
    child.send(" ")  # 翻页
    settle(child, stream)
    snap = snapshot(screen, "less-page2")
    check("line-20" in screen_text(screen) or "line-19" in screen_text(screen),
          "6.less翻页", f"(快照 {snap})")
    child.send("q")
    settle(child, stream)
    snap = snapshot(screen, "less-quit")
    check("lines.txt" not in screen_text(screen) or "$" in screen_text(screen),
          "6.less退出回提示符", f"(快照 {snap})")

    # 7. vim：打开、插入、保存退出
    vim_path = os.path.join(PLAY, "vim-test.txt")
    if os.path.exists(vim_path):
        os.remove(vim_path)  # 可重复运行：避免上次内容干扰
    child.sendline("vim -u NONE vim-test.txt")
    settle(child, stream, quiet=1.0)
    snap = snapshot(screen, "vim-open")
    txt = screen_text(screen)
    check("~" in txt or "vim-test.txt" in txt, "7.vim打开", f"(快照 {snap})")
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
    check(ok_file and "zz-vim-content" in content, "7.vim写入并退出",
          f"文件内容 {content!r} (快照 {snap})")

    # 8. Ctrl+C：中断 sleep，提示符恢复
    child.sendline("sleep 100")
    time.sleep(0.5)
    settle(child, stream)
    child.sendcontrol("c")
    settle(child, stream)
    child.sendline("echo zz-after-ctrlc")
    settle(child, stream)
    snap = snapshot(screen, "ctrl-c")
    check("zz-after-ctrlc" in screen_text(screen), "8.Ctrl+C中断恢复", f"(快照 {snap})")

    # 9. exit：demo 应以子进程退出码 0 退出
    child.sendline("exit")
    try:
        child.expect(pexpect.EOF, timeout=10)
    except pexpect.TIMEOUT:
        pass
    child.close()
    check(child.exitstatus == 0, "9.exit干净退出", f"exitstatus={child.exitstatus}")

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

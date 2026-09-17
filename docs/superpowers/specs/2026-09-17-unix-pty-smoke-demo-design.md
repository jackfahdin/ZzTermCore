# Unix PTY + 控制台冒烟 Demo 设计规格

> 2026-09-17 · 状态：已批准 · 对应里程碑：M1（PTY/Demo 部分）

## 1. 目标与范围

让 Core 第一次驱动真实 shell：提供 Unix PTY 模块（`ZzTermPty`）与
控制台冒烟 Demo（`ZzTermSmoke`），能真实运行 bash/vim/less 并以
脚本化方式冒烟验证。

**纳入：**

- `ZzPty`：forkpty/openpty 封装（Linux），含窗口尺寸同步
  （TIOCSWINSZ）、子进程退出状态收集（SIGCHLD/waitpid）
- `ZzTermSmoke` 控制台 demo：PTY 输出喂 `ZzTerminal`，RenderView 渲染回
  真实终端（stdout），stdin 字节透传
- 子进程退出时 demo 以其退出码干净退出（即 `--command` 语义：
  `zzterm-smoke -- bash -c 'echo hello'` 执行后自动退出，无需单独 flag）
- PTY 单元测试 + 脚本化冒烟测试（CTest）
- SIGWINCH 处理（demo 运行期间终端尺寸变化时同步 PTY 与 Terminal）

**不纳入：**

- Qt 6 ZzTermWidget / 正式 ZzTermDemo（下一个迭代，独立设计）
- macOS PTY（M5，与 CI 一起；仅头文件差异 `util.h` vs `pty.h`）
- Windows ConPTY（里程碑靠后）
- InputEncoder 接入（控制台 demo 的 stdin 已是编码后的字节流，
  InputEncoder 面向 GUI 前端）
- demo 渲染性能优化（全屏重绘即可，dirty 优化留给 Qt Widget）
- demo 不渲染 scrollback（冒烟范围只验证当前屏幕）

## 2. 架构

### 模块与文件

- `pty/unix/ZzPty.h` / `pty/unix/ZzPty.cpp`：PTY 封装，独立 CMake target
  `ZzTermPty`。**不依赖 ZzTermCore**——它是纯 OS 封装（Architecture.md
  第 3 节依赖方向：外层模块依赖 Core，PTY 连这个都不需要）。
- `examples/ZzTermSmoke/main.cpp`：demo，target `ZzTermSmoke`，链接
  `ZzTermCore` + `ZzTermPty`。命名避开正式的 Qt 版 ZzTermDemo，
  明确表示这是调试工具。
- `tests/unit/test_pty.cpp`：PTY 模块测试（CTest 按现有约定自动收编）。

### ZzPty API（薄 RAII 封装，无线程无回调）

```cpp
struct ZzPtyConfig {
    std::vector<std::string> argv;  // 程序与参数
    int cols = 80, rows = 24;       // 初始窗口尺寸（TIOCSWINSZ）
    bool rawMode = true;            // slave 设 raw（cfmakeraw），避免 PTY
                                    // echo/规范模式干扰 Core 验证
};

class ZzPty {
public:
    /// 失败返回 nullptr，errno 保留给调用方诊断。
    static std::unique_ptr<ZzPty> spawn(const ZzPtyConfig& cfg);
    int masterFd() const;                        // 供 poll/select 使用
    std::ptrdiff_t read(std::span<std::byte> buf); // 0 = EOF（子进程退出后
                                                   // Linux 返回 EIO，归为 EOF）
    bool writeAll(std::span<const std::byte> data);  // 循环写直到写完或出错
    bool resize(int cols, int rows);             // TIOCSWINSZ（触发 SIGWINCH）
    std::optional<int> tryWait();                // 非阻塞收集退出码；
                                                 // nullopt = 仍在运行
    ~ZzPty();                                    // SIGHUP 子进程、关 fd、
                                                 // 回收僵尸
};
```

事件驱动由调用方决定（demo 用 poll 循环）。未来 Qt Widget 集成时把
masterFd 接到 QSocketNotifier，PTY API 不变。

## 3. Demo 结构（ZzTermSmoke）

- **参数**：`zzterm-smoke [-- 命令...]`，默认 `bash`。
- **启动**：ioctl(TIOCGWINSZ) 取当前终端尺寸（取不到用 80x24）→ RAII
  TermiosGuard 进 raw mode → spawn PTY（同步尺寸）→ 构造
  `ZzTerminal(cols, rows, scrollback)`。
- **主循环**（poll(masterFd, STDIN, SIGWINCH 自管道)）：
  - masterFd 可读 → `pty.read` → `term.feed` → 渲染当前屏幕；
  - stdin 可读 → 原样 `pty.writeAll`；
  - SIGWINCH → 重读 winsize → `pty.resize` + `term.resize`。
- **渲染**：逐行读 ZzRenderView，把 ZzColor（Indexed/RGB/Default）与
  ZzCellAttributes 映射回 ANSI SGR 序列写到 stdout，全屏重绘 + CUP
  定位光标。只渲染当前屏幕，不渲染 scrollback。
- **退出**：子进程退出（tryWait 命中或 read EOF）→ guard 析构恢复
  termios → 以子进程退出码退出。

## 4. 错误处理

- spawn 失败：stderr 报告 errno 描述，退出码 1；
- PTY read 的 EIO 归为 EOF（Linux 惯例）；
- writeAll 处理部分写入；EINTR 重试；
- 所有退出路径经 RAII guard 恢复原始终端状态。

## 5. 测试与验收

- `tests/unit/test_pty.cpp`：
  - spawn `/bin/cat` round-trip（raw 模式写入读回一致）；
  - resize 后 TIOCGWINSZ 生效（master 侧 ioctl 验证）；
  - spawn `/bin/sh -c 'exit 42'` 退出码收集为 42；
  - spawn 不存在的程序：返回 nullptr、不崩溃、errno 有效。
- 脚本化冒烟（CTest）：运行 `ZzTermSmoke -- bash -c 'echo zz-smoke-ok'`，
  断言输出含标记串且退出码 0。
- 人工验证：demo 中运行 bash（ls --color / vim 打开编辑退出 / less 翻页 /
  Ctrl+C），肉眼确认渲染正确。
- `docs/VT-Xterm-Checklist.md` 勾选：PTY / Demo 的 Linux PTY、bash/zsh。
- `docs/API.md` 补 PTY 章节；新增公开 API 全中文 Doxygen。
- 静态/动态构建、ctest、doxygen 全绿。

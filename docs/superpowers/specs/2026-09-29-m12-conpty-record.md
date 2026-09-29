# M12 Windows ConPTY 移植记录

- 日期：2026-09-29
- 分支：contour
- 交付 commit：T1 `ff71ceb`（目录重构与单头平台分支）、T2 `c522afc` + `e0a37e9`（Windows ConPTY 实现与 test_pty 全平台用例）、T3 `d31bc6b`（ZzTermSmoke Windows IO 循环 + examples 全平台收编）+ CI 迭代 `b140995`、`69f1359`、`d2277c2`、`c7a9258`、`a63c56e`、`05a3584`

## 1. mingw 预检发现清单

- T2：`pty/windows/ZzPty.cpp` 与 `pty/ZzPty.h` 交叉编译零错误（仅有的 dllimport attribute 警告为独立编译单元未定义导出宏所致，属预检既有形态，基线判据为零错误）。
- T3 首轮：`examples/ZzTermSmoke/main.cpp` 报错 renderScreen 未声明——Windows 版 run 落在共享渲染函数定义之前。修复为两段平台分支（尺寸查询/stdout 写出助手一段、run 主循环一段），共享渲染段居中，POSIX 段逐字保留；复编译零错误。
- CI 迭代各轮（R1/R2/R3/R5/R6 探针与 R6 根因修复）的 `tests/unit/test_pty.cpp` 与 `pty/windows/ZzPty.cpp` 改动，每轮推送前均过 mingw 交叉编译零错误 + 本机基线（linux-gcc-debug 50/50、doxygen 零警告）。

## 2. CI 迭代轮次与 run id

windows-msvc job 首次编译 pty/examples 并运行 test_pty Windows 用例，共七轮（每轮修复均先过本机基线再复推；ConPTY 运行时行为修复属 CI-only 验证，commit message 已逐轮标注）：

| 轮次 | run id | 结果 | 发现与修复 |
| --- | --- | --- | --- |
| R1 `d31bc6b` | 36535477949 | 34/36 | test_pty 报 FINDSTR: Bad command line（裸 caret 被 cmd 元字符解析吞掉，T2 审查首嫌疑点实证）；ZzTermSmokeEcho_native 失败（注册命令硬编码 bash，Windows 无此语义）。修复 `b140995`：pattern 强制引用 + smoke 测试改平台分支命令（Unix bash / Windows cmd，tests/CMakeLists.txt 为简报文件面外的最小 CI 修复） |
| R2 `b140995` | 36536262644 | 35/36 | test_pty 仍速败（无 FINDSTR 错误输出）——强制引用经 cmd 引号保留规则与 caret 转义交互后失真。修复 `69f1359`：直跑 findstr.exe 绕开 cmd 解析层 |
| R3 `69f1359` | 36536720356 | 35/36 | test_pty 仍速败（0.15s，无任何错误输出）。修复 `d2277c2`：往返改 cmd 交互会话 + echo 命令，读到 marker 退出 |
| R4 `d2277c2` | 36537246618 | 35/36 | test_pty 仍败（0.10s）——cmd 横幅出现在 ctest 捕获输出（未走 pty 管道），pty 侧读到 EOF。修复 `c7a9258`：诊断探针（EOF 时打印退出码与已读内容） |
| R5 `c7a9258` | 36538063085 | 35/36 | 探针实证：exit=0，已读仅 85 字节 ConPTY 初始化序列（含标题设置，证明伪控制台附着成功）——cmd 附着后启动即退。修复 `a63c56e`：2s 启动延迟 + echo 与 exit 43 复合命令探针，区分时机竞争与输入路径故障 |
| R6 `a63c56e` | 36538699030 | 35/36 | exit=0 依旧，排除启动竞争；横幅仍直通父进程输出。根因锁定：父进程标准句柄被重定向时（ctest 管道），内核把父 std 句柄复制给子进程使其绕过伪控制台（microsoft/terminal 讨论 15814 官方确认的既有行为，Vim PR 19589 同款遭遇）。修复 `05a3584`：spawn 置 STARTF_USESTDHANDLES 空 std 句柄（库级修复）+ 往返用例定稿 |
| R7 `05a3584` | 36539701096 | 36/36 | 全绿。test_pty 0.31s 通过，六 workflow 全部 success（docs 36539701059 / ubuntu-gcc 36539701064 / ubuntu-clang 36539701074 / macos-clang 36539701093 / macos-contour 36539701095 / windows-msvc 36539701096） |

根因补注：R2-R6 期间 ZzTermSmokeEcho_native 虽绿，但属句柄泄漏下的空转通过（cmd echo 输出直通父进程管道命中正则）；R7 根因修复后才是真实 ConPTY 全链路。

## 3. Windows 测试计数前后对照与往返实证

- 前（基线 run 36528178890，sha `203e9a3`）：35 个测试，test_pty 为平凡 main 空跑（输出 skipped，0.01s）。
- 后（R7 run 36539701096）：36 个测试——新增 ZzTermSmokeEcho_native（cmd /c echo 驱动 demo 的 Windows IO 循环：ConPTY 输出经 ZzTerminal 渲染回 stdout 命中 marker）；test_pty 转为四个真实 ConPTY 用例（往返 / resize / 退出码 / spawn 失败）。其余平台计数不变（Linux 50、含 interactive 冒烟的两套 m2 台账 40/50 本机复核不变）。

findstr 往返路线三轮实证弃用：R1 裸 caret 被 cmd 吞（FINDSTR: Bad command line）；R2 强制引用经 cmd 引号规则失真；R3 直跑 findstr.exe 在句柄泄漏 bug 下读到父进程管道 stdin 的 EOF 立即退出。最终往返实证形态：cmd 交互会话 + `echo zz-pty-roundtrip & exit 43` 复合命令——marker 回读证明 写入→执行→输出→读取 全链路，退出码 43 证明输入确实送达 cmd（R7 实测 0.31s 通过）。写入前先读到会话就绪标记（ConPTY 初始化序列中的标题设置）规避启动竞争。

## 4. API 对齐语义表

五方法 + EOF/EAGAIN 归一 + resize 无回读的处理：

| 方法 | Unix openpty | Windows ConPTY |
| --- | --- | --- |
| `spawn` | openpty + fork + execvp；失败 nullptr + errno | 双 CreatePipe + CreatePseudoConsole + 属性列表 + CreateProcessW；STARTF_USESTDHANDLES 空句柄防父进程重定向 std 泄漏（R6 根因修复）；失败 nullptr + GetLastError |
| `read` | 非阻塞 read；子进程退出后 EIO 归一 EOF（0）；EAGAIN 返回 -1 | PeekNamedPipe + ReadFile；ERROR_BROKEN_PIPE 与 子进程已退出且排空 归一 EOF（0）；暂无可读返回 -1 并 SetLastError(ERROR_NO_DATA)（等价 EAGAIN） |
| `writeAll` | 循环 write 处理部分写入与 EINTR | 循环 WriteFile 处理部分写入 |
| `resize` | TIOCSWINSZ，触发子进程 SIGWINCH | ResizePseudoConsole；ConPTY 无尺寸回读 API，测试仅断言返回 true |
| `tryWait` | waitpid WNOHANG；信号杀死为 128 + 信号号 | GetExitCodeProcess；STILL_ACTIVE 归一 nullopt；退出码直返（无信号语义） |
| 析构 | SIGHUP → 宽限 → SIGKILL 兜底；关闭 master fd、回收僵尸 | ClosePseudoConsole → 200ms 宽限 → TerminateProcess 兜底；收尾退出码并关闭全部句柄 |
| 事件 accessor | `masterFd()`（int，供 poll/select） | `readHandle()` 与 `writeHandle()`（void 指针即 HANDLE，供 PeekNamedPipe/overlapped） |

EOF/EAGAIN 归一总结：两平台 read 均返回 >0 字节数、0 EOF、-1 暂不可读或错误；Unix 侧诊断走 errno，Windows 侧走 GetLastError。调用方（demo 与 test）无需平台分支即可驱动同一读取循环。

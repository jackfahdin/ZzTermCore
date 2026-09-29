# M12：Windows ConPTY 移植

- 日期：2026-09-29
- 分支：contour（仓库默认分支已切换为 contour，master 推送决策作废——contour 即主分支）
- 前置：M11 已合并 master——M7a 台账清零，CI 六 workflow 绿；windows-msvc job 长期绿但 PTY 层从未在 Windows 构建（根 CMake 的 if(UNIX) 排除）
- 依据：docs/Architecture.md 路线图（Windows ConPTY 里程碑）；pty/unix/ZzPty.h 分层注释（纯 OS 封装、事件驱动由调用方决定）
- 用户批准决策：contour 为主分支（不再推 master；远程默认分支已切 contour）；1A 单头双实现（方法签名全平台统一，平台 accessor 各取所需，Unix 侧零改动）；2A 本机 mingw-w64 交叉预检（纯编译不运行）+ CI 迭代；3A 全量范围（PTY 库本体 + test_pty Windows 用例 + examples 编译通过，CI 不跑 interactive）；4A 运行时版本契约声明 Windows 10 1809+ 为最低要求，旧系统 spawn 返回 nullptr 留诊断，不做版本探测

## 1. 背景与目标

PTY 层（pty/）目前 Unix-only（Linux + macOS 经 M9a 打通），Windows 上根 CMake 以 if(UNIX) 整体排除 pty 与 examples——Windows 平台矩阵缺最后一块。Windows 的等价物是 ConPTY（Pseudo Console API：CreatePseudoConsole / ResizePseudoConsole / ClosePseudoConsole，Windows 10 1809+），模型与 Unix PTY 不同：HPCON + 两根匿名管道 HANDLE，无 fd、无 poll、无 SIGWINCH、无 waitpid。本里程碑以"单头双实现"补齐 Windows 实现，五个方法（spawn/read/writeAll/resize/tryWait）签名与语义全平台对齐，Windows CI 首次编译并运行 PTY 测试。

## 2. 改动点

1. 目录重构：pty/unix/ZzPty.h 与 ZzPtyExport.h 上移至 pty/ 根；实现按平台分 pty/unix/ZzPty.cpp（逐字不动）与 pty/windows/ZzPty.cpp（新建）；pty/CMakeLists.txt 的 include 根从 unix/ 改 pty/，按 WIN32 选择实现源文件；根 CMake 的 pty/examples 收编从 if(UNIX) 放开为全平台。
2. 头的平台分支：五方法签名统一；Unix 保留 masterFd()，Windows 提供 readHandle()/writeHandle()（返回 void* 即 HANDLE，供调用方接 PeekNamedPipe 或 overlapped IO）；sys/types.h 包含与成员区加平台守卫；ZzPtyConfig 的 rawMode 注释注明 Windows 忽略（ConPTY 无终端行 discipline 概念）。
3. pty/windows/ZzPty.cpp 实现路径：CreatePipe 双管 -> CreatePseudoConsole（COORD 尺寸）-> InitializeProcThreadAttributeList + UpdateProcThreadAttribute（PseudoConsole 属性）-> CreateProcessW（EXTENDED_STARTUPINFO_PRESENT）-> 关闭多余句柄端；resize 走 ResizePseudoConsole；tryWait 走 GetExitCodeProcess（STILL_ACTIVE 归一为 nullopt；退出码语义对齐 Unix 约定——Windows 无信号概念，退化为纯进程退出码）；析构 ClosePseudoConsole + 子进程终止兜底 + 句柄全收。read 语义对齐：子进程退出后 EOF 归一、"暂无可读数据"以 Unix EAGAIN 等价形式表达（具体形态实施期定，以 test_pty 双平台断言一致为准）。
4. tests/unit/test_pty.cpp：守卫从 Unix 限定放开为全平台，Windows 段实现等价用例（spawn cmd.exe 回显往返、resize 成功、退出码收集、重复 tryWait 同值、析构清理）；每文件独立可执行自带 main 的约定不变。
5. examples/ZzTermSmoke/main.cpp：IO 循环加 Windows 分支（PeekNamedPipe + Sleep 轮询替代 poll）；只要求编译通过（interactive 测试 pexpect 无 Windows 版，CI 不跑）。

## 3. 验证策略

- mingw 交叉预检：独立 build 目录（build/ 下 gitignore 覆盖），x86_64-w64-mingw32-g++ 编译 Windows 实现与 test_pty Windows 段——纯编译不运行（产出为 Windows 二进制）。预检只抓公共层语法/API 误用；mingw gcc 与 MSVC 的头文件与警告差异以 MSVC 为准，属 CI-only。
- 本机基线五项零回归：linux-gcc-debug 50/50、m2-off-check 40/40、m2-shared-check 50/50、linux-clang-fuzz 3/3、doxygen exit 0 零警告。Linux/macOS 侧行为零变化（Unix 实现逐字不动；头文件重构经全量测试背书）。
- CI 六 workflow 绿：windows-msvc job 首次编译 pty 并运行 test_pty Windows 用例（runner 为 Windows Server 2022+，满足 1809+）；Windows 测试计数增长落账，其余平台计数不变。

## 4. 错误处理与风险

- ConPTY API 细节（attribute list 尺寸计算、句柄关闭顺序、子进程退出后管道 EOF 形态、PeekNamedPipe 在写入端关闭后的返回值）以 CI 实证为准，本机不可运行验证的标注 CI-only。
- 红线：third_party/contour 永不改；若撞出必须改 third_party 的情况立即停下回报。
- 运行时版本契约（4A）：Windows 10 1809+ 为 ZzPty Windows 版最低要求；旧系统 CreatePseudoConsole 不存在导致 spawn 失败返回 nullptr，GetLastError 留诊断，不做版本探测。
- 已知风险排序：ConPTY spawn 序列细节（最高，CI 迭代消化）> 管道非阻塞读语义对齐 > mingw/MSVC 头文件差异 > 头文件上移对下游 include 的影响（仓库内引用全部经 CMake include 根，无硬编码相对路径——实施期确认）。

## 5. 排除项（本里程碑不做）

- Windows interactive 测试（pexpect 无 Windows 版；wexpect 等替代方案不引入）；
- examples 的 Windows 运行验证（只编译）；
- ConPTY 的 overlapped IO / 事件驱动封装（accessor 暴露 HANDLE，事件模型由调用方决定——与 Unix 版"事件驱动由调用方决定"同约定）；
- 版本运行时探测与多 Windows SDK 兼容层（4A）；
- master 推送（contour 已为主分支，该决策线关闭）。

## 6. 记录与收尾

- 记录文档 docs/superpowers/specs/2026-09-29-m12-conpty-record.md：mingw 预检发现、CI 迭代轮次、Windows 测试计数前后对照、API 对齐语义表。
- 收尾惯例：finishing 合并 master（--no-ff，master 转为本地归档线；合并后本机全基线回归 + doxygen），contour 保留。

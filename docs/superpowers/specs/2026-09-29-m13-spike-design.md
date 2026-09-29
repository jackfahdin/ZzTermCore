# M13：ZzClawTerm 集成 spike（RenderView 契约的 Qt 前端可行性验证）

- 日期：2026-09-29
- 分支：contour（规格与过程档案入 ZzTermCore 仓）；spike 代码落 ZzClawTerm 仓 spike/ 目录（独立小工程，不进 ZzClawTerm 主构建）
- 前置：M12 已收尾（Windows ConPTY，平台矩阵全）；下游打包验证批已落（4831f08——ZzTerm::Pty 进导出集、静态消费 contour OFF 约束实证、ci-downstream 常驻）
- 依据：外部评审建议（spike 先行，三阶段路线：特性对齐 -> 历史调和/ptyqt 退役 -> 删 ZzTermWidget）经主代理修正后的形态；docs/Architecture-v2.md 的 ZzRenderView 前后端分离契约
- 用户批准决策：1A 独立 spike 窗口程序（ZzClawTerm/spike/，弃子宿主 + 可复用 widget 类）；2A SSH 阶段直驱 ZzSshCore channel API（绕开 ZzSshTransport 少一层变量）；3A spike 代码保留演进（widget 按特性对齐阶段种子标准写）；4A QTest + offscreen 自动断言（人工验证仍做）；既定约束：Qt 代码不入 Core 库、第一版只做等宽字体基础渲染（无选择/鼠标上报/IME/配色）、add_subdirectory 消费 Core 源码、三清单交付（打包问题/API 缺口/行为观察）

## 1. 目标与判定标准

验证"在 ZzRenderView 稳定契约上新建 Qt QWidget 前端"的可行性，一周量级出结论。通过标准（全部满足为"行"）：本地 PTY 链（ZzTermPty spawn bash -> ZzTerminal feed -> widget 渲染 + 键盘输入回写）人工与自动双验证通过；SSH 链（ZzSshCore channel -> 同 widget）人工验证通过；三清单（打包问题/API 缺口/行为观察）产出。任一链路卡住且根因在 RenderView 契约或 Core API 本身，结论为"不行"——如实记录损失范围（仅 spike 工程），ZzClawTerm 现状零影响。

## 2. 改动点（全部在 ZzClawTerm 仓 spike/）

1. `spike/ZzCoreViewWidget.h/.cpp`（产物，按可复用标准写）：QWidget 持有 ZzTerminal（native 后端）；paintEvent 按 renderView() 逐行绘制文本（等宽字体，QFontMetrics 定网格，光标与脏区全量重绘——spike 不做增量）；keyPressEvent 经 include/ZzTerm/Input.h 编码为字节流，经 std::function 写出回调交给宿主；resizeEvent 触发 ZzTerminal::resize。
2. `spike/main.cpp`（弃子宿主）：QApplication + 主窗口装 widget；`--local`（默认）走 ZzTermPty spawn bash，QSocketNotifier 接 masterFd 读、写回经 writeAll（Unix fd 与 Qt 事件循环天然接合，零线程）；`--ssh <host> [user]` 走 ZzSshCore channel。
3. SSH 接入：直驱 ZzSshCore 的 channel API（密码或 key 认证取当前用户默认 agent/key；读回调 feed、写回调经 Input 编码），不经过 ZzSshTransport。
4. `spike/tests/`：QTest（offscreen）——PTY 起 bash 写 echo 标记，断言 widget 内 ZzTerminal 的 renderView 文本含标记（QSignalSpy/轮询等待，不依赖时序 sleep）；进 ZzClawTerm 的 ctest 体系（offscreen platform 插件）。
5. `spike/CMakeLists.txt`：独立工程，add_subdirectory 指向 ZzTermCore 源码路径（缓存变量 ZZTERMCORE_SOURCE_DIR，默认 ../ZzTermCore 相对布局，本机两仓同级的既有布局）；Qt6 Widgets + Test（Qt 基线 6.8+，本机 6.11.1 前缀 /home/zz/Qt/6.11.1/gcc_64）；ZZTERM_WITH_CONTOUR 默认沿用 Core 默认（OFF on APPLE/ON Linux——spike 只用 native 后端，链接面不受影响；静态链接时注意 M13 前置批实证的 contour OFF 约束，spike 构建显式 -DZZTERM_WITH_CONTOUR=OFF 规避）。

## 3. 验证策略

- 自动：spike ctest（offscreen）绿——PTY echo 标记断言 + widget 文本对照。
- 人工（用户执行，主代理给清单）：echo/ls 基本输出、CJK 与 emoji 显示、vim 进出（alt screen）、窗口拖resize 文本重排、键盘输入含回车/方向键/Ctrl 组合、滚动查看历史。
- SSH 目标：默认 `ssh localhost`（需本机 sshd 与当前用户 key/agent；不通则由用户提供测试主机凭证）。
- 记录：三清单写入 docs/superpowers/specs/2026-09-29-m13-spike-record.md（ZzTermCore 仓）。

## 4. 错误处理

- RenderView 契约缺口的处置：spike 中确需而 RenderView 没有的读取件，先以 widget 侧绕行实现并在 API 缺口清单标注，**不改 ZzTermCore**——Core 的任何改动回主仓走独立流程，spike 仓只消费。
- Qt 事件循环与 Core 非线程安全约束：全部 Core 调用（feed/resize/renderView）限定 GUI 线程，QSocketNotifier 回调天然满足。
- SSH 认证失败/网络不可达：如实记录形态（不判 spike 失败，除非根因在 Core）。

## 5. 排除项（本 spike 不做）

- 文本选择、鼠标上报映射、IME、配色应用、比例字体/软折叠渲染、增量脏区绘制、滚动条精细交互；
- ZzSshTransport 适配、嵌入 ZzClawTerm 标签体系（特性对齐阶段）；
- ZzScrollbackBridge/mmap 冷层历史调和（独立设计里程碑，三阶段之二）；
- ptyqt 实际退役（三阶段之二顺带）；
- ZzTermCore 本仓代码改动（发现缺口只记录）。

## 6. 记录与收尾

- 记录文档：docs/superpowers/specs/2026-09-29-m13-spike-record.md——三清单 + 结论判定（行/不行）+ 三阶段路线图的输入建议。
- 收尾：ZzClawTerm 仓 spike commit 独立落地（遵其仓库约定）；ZzTermCore 仓仅规格/计划/记录文档 commit，contour 打 tag m13 收尾（新惯例：不再 merge master）。

# M1a：ZzContourBackend 核心封装 — 设计规格

> 2026-09-19 · 分支 `contour` · 对应 `docs/Architecture-v2.md`（v2.1）整合版里程碑 M1 上半部分
> 本规格经头脑风暴确认：M1 拆分（Q1=A：M1a 核心封装 + headless 测试 / M1b 零拷贝视图与接入）、
> API 形态（Q2=A：贴合 Contour 语义的独立 API，不实现 M0 的 `ZzTerminalBackend` 接口）、
> 快照方案（方案一：拷贝式快照 + 轻量语义查询）。

## 1. 背景与目标

M0 已完成 Contour 最小构建集成、`ZzTerminalBackend` 内部接口与 `ZzTerminal` PImpl 骨架。
M1 的目标是让 Contour `vtbackend::Terminal` 真正跑起来并纳入 ZzTermCore 封装。经头脑风暴，
M1 拆为两步：

-   **M1a（本规格）**：`ZzContourBackend` 核心封装——headless 驱动 Contour Terminal
    （构造、feed、resize、事件回写、拷贝式快照），配套 headless 冒烟/回归测试。
-   **M1b（后续规格）**：`ZzRenderView`/`ZzCellView` 零拷贝映射、`ZzExternalTransportAdapter`
    公开形态、`ZzTerminal` 双后端接入、demo 层兼容性测试。

M1a 的 API 是**贴合 Contour 语义的独立 API**，不实现 M0 的 `ZzTerminalBackend` 接口。
原因：该接口的 `renderView()` 绑定 native `ZzRenderView` 类型，Contour 侧无法零拷贝满足；
M0 规格已声明接口可在 M1 修订，收口工作留 M1b。

## 2. 已确认的决策

| 决策点 | 结论 |
|---|---|
| M1 拆分 | M1a = 核心封装 + headless 测试；M1b = 零拷贝视图 + 接入 + 兼容性测试 |
| API 形态 | 独立 API（贴合 Contour 语义），不实现 `ZzTerminalBackend`，M1b 再收口 |
| 快照方案 | 方案一：拷贝式快照 `ZzContourSnapshot` + 轻量语义查询接口 |
| 驱动方式 | `Terminal::writeToScreen()` 同步直驱，绝不调 `Terminal::start()` |
| 事件回调 | 锁内回调只置 dirty 标志 defer，绝不回读 Terminal（防死锁） |

## 3. 关键事实（Contour `6777ff0` 实测调查结论）

-   `Terminal` 构造签名（`src/vtbackend/screen/Terminal.hpp:465`）：
    `Terminal(Events&, crispy::Environment const&, std::unique_ptr vtpty::Pty, Settings, std::chrono::steady_clock::time_point)`
    （实际签名为 unique_ptr 包装 vtpty::Pty，此处略去尖括号以免文档解析问题）。
    MockTerm 蓝本（`src/vtbackend/test/MockTerm.hpp:285-300`）：自继承 Events（借用引用，
    寿命包住 Terminal），`crispy::defaultEnvironment()`，unique_ptr 移交 Pty，Settings，
    steady_clock 起点。
-   feed：`Terminal::writeToScreen(std::string_view)`（:977）——自加锁、锁外调
    `screenUpdated()`，可外部线程直调。
-   resize：`Terminal::resizeScreen(PageSize, std::optional vtbackend ImageSize)`（:909）。
-   查询：`currentScreen()` / `isAlternateScreen()`；Grid scrollback 用**负 LineOffset**
    表示（主屏第 0 行 = LineOffset(0)）。
-   Cell 读取：Grid/CellProxy（`src/vtbackend/grid/CellProxy.hpp`）读接口
    codepoint / width / flags / foregroundColor / backgroundColor / hyperlink；
    CellProxy 禁跨 grid 变更保存（故 M1a 用拷贝式快照）。
-   Color：packed uint32，ColorType 枚举含 Undefined / Default / Bright / Indexed / RGB；
    Bright 映射为 Indexed(8+n)；Indexed 需经 `colorPalette()` 才能解析为 RGB——
    快照**保留颜色身份不预转 RGB**。
-   光标：Cursor struct 无 visible/shape 字段（在渲染层）。光标可见性经 RenderBuffer
    路径：`ensureFreshRenderBuffer()` + `renderBuffer()`（返回 RAII 读锁句柄），
    RenderCursor 为 nullopt 即光标不可见。精确 API 在计划阶段钉死。
-   Events 接口：全部有默认空实现（唯一纯虚 `openDocument`），可继承
    `Terminal::NullEvents` 再覆盖所需方法。`setWindowTitle` / `bell` / `screenUpdated` /
    `bufferChanged(ScreenType)` 为同步回调。
-   **锁内回调**：`cursorPositionChanged` / `progressChanged` / `contextChanged` 三个回调
    在 `_stateMutex` 持有时触发——回调内不得回读 Terminal，只置标志 defer。
-   回写单点：`flushInput()` → `Pty::write()`。输入编码 `sendKeyEvent` / `sendCharEvent` /
    `sendPaste` 属 M3 范围。
-   `tick()` 推进内部时钟；纯文本 dump 场景可不 tick。
-   `vtpty::Pty` 纯虚全集（`src/vtpty/Pty.hpp:90-152`）：start / slave / close /
    waitForClosed / isClosed / wakeupReader / read / write / pageSize / resizeScreen；
    `ReadResult` 在 :93；`PtySlaveDummy` 在 :47-57。
-   **已知坑**：vtparser bulk `print(string_view, cellCount)` 返回值被忽略
    （`src/vtparser/Parser-impl.hpp:660`）。backend 内部不经 parser 事件拿文本，对 M1a
    无影响；M1b/M3 adapter 设计必读。
-   `Terminal::start()` 在头文件有声明但全树无实现——**绝不调用**。

## 4. 设计

### 4.1 仓库变更

```
src/backend/contour/
├── CMakeLists.txt            # 新增：ZzTermContourBackend target 定义
├── ZzContourBackend.h        # 新增：对外头（本 target 的公开接口）
├── ZzContourBackend.cpp      # 新增
├── ZzContourEvents.h         # 新增：事件抽象接口
├── ZzContourEvents.cpp       # 新增（如需）
├── ZzContourPtyBridge.h      # 新增：vtpty::Pty 回写桥
└── ZzContourPtyBridge.cpp    # 新增
tests/unit/test_contour_backend.cpp  # 新增：headless 回归测试（条件构建）
```

-   新 target `ZzTermContourBackend`（STATIC），仅 `ZZTERM_WITH_CONTOUR=ON` 时构建。
-   `target_compile_features(... PRIVATE cxx_std_23)`——消费 `vtpty/Pty.hpp` 需要
    `std::expected`；PRIVATE 链接 `vtbackend`。
-   **`ZzTermCore` 库不链接 `ZzTermContourBackend`**（收口留 M1b）。因此动态库构建
    （`BUILD_SHARED_LIBS=ON`）天然不回归，PIC 问题不存在。
-   测试注册沿用 `test_contour_smoke` 的模式：`tests/CMakeLists.txt` 中 GLOB 后
    `REMOVE_ITEM` + `if(TARGET ZzTermContourBackend)` 条件注册。

### 4.2 ZzContourBackend 核心类

构造（MockTerm 五件套）：

1.  自身内部 Events 实现对象以借用引用传给 Terminal（寿命包住 Terminal 整个生命周期）；
2.  `crispy::defaultEnvironment()`；
3.  `std::make_unique vtpty::Pty 实现 ZzContourPtyBridge(...)` 移交；
4.  Settings：仅设四项——pageSize（行列）、historyLimits（scrollback 上限）、
    ptyReadBufferSize、goodImageProtocol（关图像协议）；
5.  `std::chrono::steady_clock::now()` 起点。

公开操作：

-   `feed(std::string_view)`：直调 `Terminal::writeToScreen()`（同步、自加锁）；
-   `resize(cols, rows)`：直调 `Terminal::resizeScreen()`，cell 像素固定 8x17 传入；
-   轻量语义查询：`size()` / `isAlternateScreen()` / `title()` / `historyLineCount()` /
    `lineWrapped(row)` / 光标可见性与位置（经 RenderBuffer 路径）；
-   `snapshot()`：见 4.4。

**不 tick**（纯文本场景）；**不调 start()**。

### 4.3 ZzContourPtyBridge（vtpty::Pty 回写桥）

继承 `vtpty::Pty`，职责是把终端的回传字节（DA 响应、光标位置上报等）引出库外：

-   `write(std::string_view data, bool flush)`：转发到
    `std::function void(std::string_view)` 回调（构造时注入），返回写入长度；
-   `read()`：恒返回无数据（headless 无真实 PTY 输入）；
-   `slave()`：返回内部持有的 `vtpty::PtySlaveDummy`；
-   `pageSize()` / `resizeScreen(PageSize)`：内部记录，resize 时同步返回值；
-   `start()` / `close()` / `waitForClosed()` / `isClosed()` / `wakeupReader()`：
    按 headless 语义给空实现或状态记录。

`flushInput()` 由 Terminal 内部触发，最终落到本桥的 `write()`——回写路径单点可控。

### 4.4 ZzContourEvents（事件抽象接口）

库使用方（测试、M1b 的 adapter）实现该接口接收终端事件：

-   `onTitleChanged(std::string)`——对应 Events::setWindowTitle；
-   `onBell()`——对应 Events::bell；
-   `onScreenDirty()`——对应 screenUpdated / bufferChanged（合并为"需要重绘"信号）；
-   `onActiveBufferChanged(bool alternate)`——对应 bufferChanged(ScreenType)；
-   `onWriteToTransport(std::string)`——由 PtyBridge 的 write 转发而来。

**线程与锁规则**：所有回调在 Terminal 内部线程上下文同步触发；其中
cursorPositionChanged / progressChanged / contextChanged 在 `_stateMutex` 持有时触发，
实现侧**只置 dirty 标志 defer**，回调体内绝不回读 Terminal 对象。

### 4.5 拷贝式快照 ZzContourSnapshot

`snapshot()` 返回值类型，持有数据所有权（不引用 Terminal 内部）：

-   `cols` / `rows`；
-   `alternateScreen`（bool）；
-   按行展开的 Cell 数组。每 Cell：
    -   `std::u32string codepoints`（簇内全部 codepoint）；
    -   `ZzColor fg` / `ZzColor bg`——保留颜色身份（Default / Bright→Indexed(8+n) /
        Indexed(n) / RGB），**不预转 RGB**；`ZzColor` 为本头内定义的轻量结构
        （tag + uint32 值），与 native `ZzCell` 的颜色表示在 M1b 收口时统一；
    -   `flags`（粗体/斜体/下划线等待定位掩码，按计划阶段核定的位集）；
    -   `width`（1 或 2；CJK wide-cell 的续格按 Contour 惯例呈现）；
-   `std::optional` 包装的光标信息（列、行、可见性；不可见时无值）。

数据源：`currentScreen()` → Grid/CellProxy 逐行读取；scrollback 经负 LineOffset 访问
（`historyLineCount()` 与 `snapshot()` 是否含 scrollback 行由参数控制，默认仅主屏）。

### 4.6 测试（tests/unit/test_contour_backend.cpp）

沿用现有单测约定：自带 main()、文件名即 CTest 名、`REMOVE_ITEM` + `if(TARGET)` 条件注册
（`ZZTERM_WITH_CONTOUR=OFF` 时不构建不注册）。覆盖 PoC 九项中可 headless 验证的内容：

1.  ASCII 文本写入与快照读回一致；
2.  UTF-8 / CJK 文本快照正确；
3.  SGR 颜色（如红色前景）在快照中保留颜色身份；
4.  RGB TrueColor 写入与读回；
5.  CJK wide-cell：width=2 与续格呈现；
6.  OSC title 设置触发 `onTitleChanged`；
7.  alt screen 进入/退出/恢复主屏内容（`onActiveBufferChanged` + 快照断言）；
8.  resize 80x24 → 100x30 后尺寸与内容正确；
9.  scrollback 增长（`historyLineCount()` 递增）；
10. 回写：feed `\x1b[c`（DA 请求）后断言桥收到 DA 响应字节；
11. bell 字符触发 `onBell`；
12. 多 chunk 边界（跨 chunk 拆分 UTF-8 序列 / CSI 序列后快照一致）。

**回归基线**：现有 14/14 测试（ON）与 13/13（OFF）不得退化。

## 5. 明确不做（M1a 范围外）

-   不实现/不修订 `ZzTerminalBackend` 接口，不接 `ZzTerminal`（M1b）；
-   不做 `ZzRenderView` / `ZzCellView` 零拷贝视图（M1b）；
-   不做 `ZzExternalTransportAdapter` 公开形态，仅留 `onWriteToTransport` 回调缝（M1b）；
-   不做输入编码 sendKeyEvent / sendCharEvent / sendPaste（M3）；
-   不动 `ZzTermCore` 库目标，不处理 PIC / shared 链接（M1b）；
-   不做颜色调色板解析（Indexed → RGB 的 `colorPalette()` 解析留给消费方/M1b）；
-   不 tick、不启动 Terminal 内部线程。

## 6. 验收标准（DoD）

-   `cmake --preset linux-gcc-debug && cmake --build --preset linux-gcc-debug` 通过；
-   `ctest --preset linux-gcc-debug` 全绿：14 项基线 + 新增 test_contour_backend；
-   OFF 路径验证：`cmake -S . -B build/m1a-off-check -G Ninja -DZZTERM_WITH_CONTOUR=OFF`
    构建 + ctest 13/13 不回归（新 target 与新测试均不出现）；
-   `doxygen Doxyfile` exit 0 零 warning（新增头文件注释遵守 docs/ 内 markdown 约束：
    行内 code span 不含尖括号、紧邻 code span 后不紧跟顿号）；
-   代码符合项目现有命名与结构约定；子代理审查（SDD 流程）通过。

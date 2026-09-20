# M1b：Contour 后端收口（视图统一 + ZzTerminal 双后端接入）— 设计规格

> 2026-09-19 · 分支 `contour` · 对应 `docs/Architecture-v2.md`（v2.1）整合版里程碑 M1 下半部分
> 本规格经头脑风暴确认：M1b 一次做完四块（Q1=A）、运行期工厂切换后端（Q2=A）、
> dirty 契约形式统一 + Contour 恒报全行脏（Q3=C）、兼容性测试仅现有脚本双后端化（Q4=A）、
> 渲染视图采用方案一（公开视图抽象化，零拷贝两端适配）。

## 1. 背景与目标

M1a 已交付 `ZzContourBackend` 核心封装（headless 驱动 Contour vtbackend，独立 API +
拷贝式快照 + 事件接口），但它与 M0 的 `ZzTerminalBackend` 接口无继承关系，`ZzTerminal`
也尚未接线任何后端。M1b 完成收口，四块工作一次做完：

1.  渲染视图抽象：公开 `ZzRenderView`/`ZzLineView`/`ZzCellView` 后端无关契约（零拷贝两端适配）；
2.  `ZzTerminal` 双后端接入：运行期工厂 + 事件桥接（Contour 推模型 → ZzTermChanges 拉模型）；
3.  ExternalTransport 公开形态：`ZzTerminal` 统一 output 通道（回传字节到传输层）；
4.  demo 层兼容性测试：ZzTermSmoke 双后端 + pyte 交叉验证。

目标架构（v2.1 §1）：Contour 为默认后端方向，native 保留为一等后端与兼容性对照基准。

## 2. 已确认的决策

| 决策点 | 结论 |
|---|---|
| M1b 范围 | 一次做完视图抽象 + 双后端接入 + output 通道 + demo 兼容性测试，不再拆 |
| 后端切换 | 运行期：`ZzTerminal` 构造参数 `ZzBackendKind`（无默认值，显式选择），PImpl 持有接口指针多态分发 |
| dirty 契约 | 接口保留行级形式（dirtyGeneration/rowDirty/dirtyRange），Contour 实现恒报全行脏，语义诚实、后续可无缝细化 |
| 兼容性测试 | 现有 verify_smoke.py 双后端化（native 对照基准 + Contour），不扩 v2 §10 完整矩阵 |
| 视图方案 | 方案一：公开契约抽象化，native/Contour 各自零拷贝适配；`ZzCellView` 为值语义单格视图（单格拷贝不等于全量复制，不违反 v2 禁令） |
| ExternalTransportAdapter | 不单独建类：M1a 的 onWriteToTransport 缝 + ZzTerminal 统一 output 通道即为其公开形态；demo 把 output handler 接 ZzPty 即落地 |

## 3. 关键事实（现状调查结果）

- `src/backend/ZzTerminalBackend.h`（M0，32 行，8 纯虚）全仓库无消费点；native 引擎状态
  直接内嵌在 `src/terminal/TerminalImpl.h` 的 `ZzTerminal::Impl` 中（screen/scrollback/
  renderView/parser/sink/utf8/penAttrs 等），无 ZzNativeBackend 类。接口头注释已声明
  「M1 接入首个实现时可按需修订」。
- 公开 `ZzRenderView`（`include/ZzTerm/RenderView.h:40-130`）是具体类，持有
  `const ZzScreen*`/`const ZzScrollback*` 借用指针，`lineAt(row)`/`cellAt(col)` 二级访问；
  无独立 ZzCellView 类型。`ZzCell` 为 16B 公开 struct（`include/ZzTerm/Cell.h:323-437`）；
  `ZzColor` 为 4B tagged union（同文件 :38-139）。
- `ZzTerminal` 公开 API（`include/ZzTerm/Terminal.h:67-164`）：feed 返回 `ZzTermChanges`
  （screenDirty/scrollbackChanged/activeBufferChanged/titleChanged/bell/scrolledOutLines），
  无回调机制；dirty 经 RenderView 查询，clearDirty 复位。
- demo（`examples/ZzTermSmoke/main.cpp`）：PTY → feed → renderScreen 全屏重绘
  （lineAt/cellAt，跳过 WideContinuation，cluster 经 line.clusterText）→ stdout ANSI；
  `tests/interactive/verify_smoke.py` 用 pexpect + pyte 交叉验证（bash/ls 着色/SIGWINCH/
  less/vim/Ctrl+C/退出码）。
- M1a Contour 侧：`ZzContourBackend`（构造/feed/resize/snapshot/语义查询/flushReplies）+
  `ZzContourEvents`（title/bell 锁内、dirty/writeToTransport/altBuffer——其中 altBuffer
  锁内、dirty 与 writeToTransport 锁外）+ 私有 `struct ZzColor`（与公开 ZzColor 同名不同类型，
  M1b 废除）。锁契约：锁内回调禁止调用 backend 任何方法（含 snapshot）。
- Contour 无行级 dirty 信号（screenUpdated 仅"有脏"）；无 scrollback 变更专用事件
  （可经 historyLineCount 前后比对得出）；DA 等回传字节经 flushReplies → onWriteToTransport。
- M0 契约测试 `tests/unit/test_backend_interface.cpp` 用 FakeBackend 断言接口可实例化，
  接口修订时需同步。
- 测试约定：tests/unit/*.cpp 各自带 main()、GLOB 收编、文件名即 CTest 名；contour 相关
  测试 REMOVE_ITEM + if(TARGET) 条件注册。

## 4. 设计

### 4.1 仓库变更

```
include/ZzTerm/RenderView.h            # 重写：ZzRenderView/ZzLineView/ZzCellView 契约
include/ZzTerm/Terminal.h              # +ZzBackendKind、构造签名变更、+setOutputHandler
src/backend/ZzTerminalBackend.h        # 接口修订（+setOutputHandler、cursor 语义统一）
src/backend/native/
├── CMakeLists.txt                     # 新增（如需独立 target；可并入主库）
├── ZzNativeBackend.h                  # 新增：现有 Terminal::Impl 引擎迁入
└── ZzNativeBackend.cpp                # 新增
src/backend/contour/
├── ZzContourBackendAdapter.{h,cpp}    # 新增：ZzTerminalBackend 接口实现 + 事件聚合
└── ZzContourRenderView.{h,cpp}        # 新增：零拷贝视图（包 currentScreen/CellProxy）
src/terminal/Terminal.cpp              # 瘦身：facade 委托
tests/unit/test_backend_compat.cpp     # 新增：双后端对照测试（条件注册，需 Contour ON）
tests/unit/test_backend_interface.cpp  # 修订：跟随接口变化
tests/interactive/verify_smoke.py      # 修订：backend 参数化
examples/ZzTermSmoke/main.cpp          # +--backend 参数、视图契约适配、output 接 PTY
```

### 4.2 视图层契约（include/ZzTerm/RenderView.h 重写）

-   `ZzCellView`：值语义单格视图——`std::u32string codepoints`、`ZzColor fg`/`bg`、
    `std::uint32_t flags`、`int width`。flags 位集沿用 M1a ZzCellFlag（Bold/Faint/Italic/
    Underline/Blinking/Inverse/Hidden/CrossedOut/WideCharContinuation），上移为公开定义
    （放 Cell.h 或 RenderView.h，计划阶段定）。
-   `ZzLineView`：只读行访问接口——`cellAt(col)` 返回 `ZzCellView`、`cellCount()`。
    实现方为轻量句柄（持有后端行指针/引用），可拷贝、借用语义。
-   `ZzRenderView`：抽象接口——`size()`、`lineAt(row)`、`dirtyGeneration()`、
    `rowDirty(row)`、`dirtyRange(row)`。dirty 行级形式保留；Contour 实现有脏时
    rowDirty 恒 true、dirtyRange 恒全行。
-   **颜色统一**：M1a contour 后端的私有 `struct ZzColor` 废除，统一使用公开
    `ZzColor`（include/ZzTerm/Cell.h 的 4B tagged union）。zzColor 转换逻辑改产出
    公开 ZzColor；Bright→Indexed(8+n) 规则不变。计划阶段先核实公开 ZzColor 能否
    表达 Undefined/Default/Indexed/RGB 全部身份，不能则最小扩展公开 ZzColor。
-   生命周期契约：视图为借用式，须与 feed 同线程使用；feed/resize 后既有视图句柄
    失效（重新 lineAt 获取）。
-   native 适配：`ZzNativeRenderView` 包现有 `ZzScreen`（零拷贝；`ZzCell`/16B 布局
    降为 native 内部类型，公开契约只看 ZzCellView）；cluster 文本经行级 interning
    读出填 codepoints。
-   Contour 适配：`ZzContourRenderView` 包 `currentScreen()`/CellProxy（零拷贝）；
    复用 M1a 的 isBlank 守卫与列钳制（Grid::resize 未物化行）、fillAttrs 背景还原、
    WideCharContinuation 语义。

### 4.3 事件桥接与 output 通道

`ZzContourBackendAdapter`（内部，`src/backend/contour/`，实现 `ZzTerminalBackend`）：

-   持有 M1a `ZzContourBackend` 与内部 EventsImpl（实现 `ZzContourEvents`）；
    锁内回调（onTitleChanged/onBell/onActiveBufferChanged）只置标志与缓存值
    （title 字符串拷贝是允许的——不触碰 backend），遵守锁契约。
-   `feed()`：清标志 → contourBackend.feed() → flushReplies()（回传字节经
    onWriteToTransport 进 output handler，锁外安全）→ 锁外聚合标志为
    `ZzTermChanges` 返回；scrollbackChanged/scrolledOutLines 由 feed 前后
    historyLineCount() 差值得出。
-   `ZzTerminalBackend` 接口新增 `setOutputHandler`（参数为 std::function 包装的
    string_view 回调）；
    `ZzTerminal` 公开同名委托方法。native 实现为空操作（M3 前无回传）。
-   `cursor()` 统一为 `ZzCursorState{line, column, visible}`：Contour 侧经
    refreshRenderBuffer + renderBuffer 路径（接口 const 约束用内部 mutable 处理）；
    native 侧沿用现有光标语义。
-   `ZzContourBackendAdapter` 不进 `ZzTermCore` 公开头；ZzTerminal 构造时按 kind
    工厂创建（Contour kind 仅在 ZZTERM_WITH_CONTOUR=ON 可用，OFF 时构造报错）。

### 4.4 ZzTerminal 双后端接入

-   公开 `enum class ZzBackendKind { Native, Contour };`（Terminal.h）。
-   构造函数：`ZzTerminal(int cols, int rows, ZzBackendKind backend,
    std::size_t scrollbackMaxLines = 10000)`——**backend 无默认值**，所有构造点
    显式选择、可审计。
-   `ZzTerminal::Impl` 瘦身为持有 unique_ptr 包装的 `ZzTerminalBackend` + 委托；
    facade 公开方法（feed/resize/renderView/size/cursor/isAlternateScreen/title/
    clearDirty/setOutputHandler）逐一委托。
-   **native 引擎迁移**：现有 `ZzTerminal::Impl` 的引擎逻辑（screen/scrollback/
    parser/sink/CsiDispatch/Sgr/utf8/pen）整体迁入
    `src/backend/native/ZzNativeBackend.{h,cpp}` 并实现 `ZzTerminalBackend`——
    机械移动、不改引擎行为；feed 的 ZzTermChanges 聚合沿用现有 activeChanges_
    机制。现有公开方法 `screen()`/`scrollback()`（标注 Core 内部）在双后端后
    不再能返回 native 具体类型——评估消费点（ZzTermPty？测试？），计划阶段定
    去留（倾向：删除或降级为 Native 专属内部口）。
-   **PIC/shared**：`ZzTermCore` PRIVATE 链接 `ZzTermContourBackend`；
    cmake/ContourBackend.cmake 聚合的 Contour 各 target 统一
    `POSITION_INDEPENDENT_CODE ON`；`BUILD_SHARED_LIBS=ON` 纳入验收。

### 4.5 测试

-   现有 12 个 native 单测：构造处显式传 `ZzBackendKind::Native`，断言不动、全绿。
-   新增 `tests/unit/test_backend_compat.cpp`（REMOVE_ITEM + if(TARGET) 条件注册）：
    双后端对照——同一 VT 序列分别喂 Native/Contour 两个 ZzTerminal，经统一
    ZzRenderView 逐格比对 codepoints/fg/bg/flags/width。场景：ASCII、SGR 索引色、
    RGB 真彩色、CJK 宽字符（含续格）、粗/斜/下划线、alt screen 进出与主屏恢复、
    resize 后尺寸与内容、scrollback 增长（historyLineCount 或 scrolledOutLines）、
    光标位置与可见性、title 事件、ZzTermChanges 标志一致性（screenDirty/bell 等）。
    native 为对照基准（v2 §10 精神）。
-   M1a `test_contour_backend` 16 用例：ZzColor 统一后适配，不断言退化。
-   `test_backend_interface`：跟随接口修订（+setOutputHandler 覆盖）。
-   demo：`examples/ZzTermSmoke/main.cpp` 加 `--backend=native|contour`（默认 contour，
    体现 v2 默认后端方向）；output handler 接 ZzPty 写回；renderScreen 适配新视图契约。
    `verify_smoke.py` 加 backend 参数；注册两个 ctest（Native/Contour 各一），
    pyte 断言两边一致。

### 4.6 验收标准（DoD）

-   `cmake --preset linux-gcc-debug && cmake --build --preset linux-gcc-debug &&
    ctest --preset linux-gcc-debug` 全绿（现有 15 + 适配后 native 12 + 新增 compat +
    interactive ×2，按实际计数）。
-   OFF 构建：`cmake -S . -B build/m1b-off-check -G Ninja -DZZTERM_WITH_CONTOUR=OFF`
    构建 + ctest 全绿；`ZzBackendKind::Contour` 构造报错路径有测试或断言覆盖；
    Contour 相关 target/测试不出现。
-   shared 构建：`cmake -S . -B build/m1b-shared-check -G Ninja -DBUILD_SHARED_LIBS=ON`
    构建 + ctest 全绿（PIC 处理生效）。
-   `doxygen Doxyfile` exit 0 零 warning（docs/ 内 markdown 约束：行内 code span
    不含尖括号、内容不以点开头、后不紧跟顿号）。
-   SDD 流程：逐任务审查 + 最终整分支审查通过。

## 5. 明确不做（M1b 范围外）

-   输入编码（sendKeyEvent/sendCharEvent/sendPaste）、鼠标、bracketed paste（M3）；
-   真实行级 dirty（Contour 恒全脏，契约已按此定）；
-   scrollback 统一视图（保留 native 专有访问，不进统一契约）；
-   IME、OSC 8、tmux/top 等 v2 §10 完整兼容矩阵（后续里程碑）；
-   `ZzExternalTransportAdapter` 独立类（以 output 通道落地，见 4.3）；
-   上游 parseBulkText 双写 bug 的 issue 上报（建议项，不阻塞）；
-   pendingUtf8 流尾兜底、Doxyfile INPUT 纳入 src/（M1a 遗留，记入 M2 候选）。

## 6. 风险与备选

-   **公开 ZzColor 表达力不足**：若 4B tagged union 无法表达 Undefined 或 Indexed
    全范围，最小扩展公开 ZzColor（计划阶段核实，扩展属公开 API 演化，已被 v2 允许）。
-   **native 引擎迁移回归**：纯机械移动，以 12 个 native 单测断言不动为回归网；
    任何需要改断言才能通过的情况都视为迁移缺陷，回溯处理。
-   **视图接口性能**：ZzCellView 值返回（含 u32string 小拷贝）对 demo 全屏重绘
    （约 2k 格/帧）可接受；若 profiling 显示热点，后续版本引入 string_view span
    优化，契约预留空间。
-   **Contour 光标 const 约束**：refreshRenderBuffer 非 const——adapter 内部以
    mutable 成员或 const_cast 收口，接口语义保持 const（不暴露实现细节）。

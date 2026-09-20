# M2 设计：EAW 真实宽度表 + DEC 私有模式（备用屏幕 / DECAWM / DECTCEM）

- 日期：2026-09-20
- 分支：contour
- 前置：M1b 已合并 master（双后端 facade + 渲染视图统一）
- 架构依据：Architecture.md §19（M2 里程碑）、§6（禁止 1 code point == 1 cell 假设）、§8（EAW / grapheme 要求）、§2（平台无关，禁 wcwidth）

## 1. 背景与目标

native 自研引擎的两块已知兼容缺口在 M1a/M1b 的 compat 测试中按 b 类分歧钉住（各自断言、留"恢复逐格对照"提示）：

1. `zzCellWidthOf` 为 M1 占位实现，恒返回窄（`include/ZzTerm/UnicodeWidth.h:34`），CJK 宽字符全部错位落格；
2. `NativeCsiDispatch.cpp` 整体忽略 DEC 私有 CSI（文件头注释指明 1049/1047/1048/?7/?25 属 M2），导致备用屏幕（vim/htop/less 必需）、自动换行开关、光标可见性全部无响应。

M2 目标：补齐这两块，使 compat 测试 3 处 b 类分歧恢复逐格强对照，demo 能正确运行 1049 进出与 CJK 文本。

## 2. 范围

### 2.1 包含

- EAW 真实宽度表（UAX #11 per-codepoint）：生成脚本 + 紧凑区间表 + 二分查找 + Ambiguous 配置口 + 表完整性测试；
- native `putChar` 宽字符流：WideLead/WideContinuation 双格写入、光标前进 2 列、右边界放不下时按 xterm 语义换行、覆写宽字符任一半时另一半清为空格；
- DEC 私有模式接线（native CSI 层）：
  - `?1049h/l`：备用屏幕进出（含光标保存/恢复、进 alt 清屏）；
  - `?1047h/l`：buffer 切换（进入时清屏）；`?1048h/l`：仅保存/恢复光标；
  - `?7h/l`：DECAWM 自动换行（默认开）；
  - `?25h/l`：DECTCEM 光标可见性；
- `ZzScreen` 新增光标可见性存储位，`cursor()` 上报真实值；
- compat 测试 3 处 b 类恢复逐格强对照 + 新增 DECAWM 强对照样例；
- demo 实测 1049 进出（less/vim 类序列）与 CJK 对齐。

### 2.2 明确排除（用户已确认）

- grapheme 聚簇（UAX #29：combining mark、variation selector、emoji/ZWJ、regional indicator）→ M3 与输入编码一起做。M2 对 combining/控制区间码位不做特殊处理，查表按窄格独立落格（见 4.1），代码注释钉住；
- 鼠标、bracketed paste、其余 DEC 私有模式 → M3，继续安全忽略；
- Resize Reflow → M4（Architecture.md §19）；
- Contour 后端本体：不动上游代码，分歧只通过适配层或注释处理。

## 3. 现状盘点（已核实的落点）

- `ZzScreen` 已实现 Primary/Alternate 双缓冲：`setActiveBuffer()`（Screen.h:96）、`saveCursor()/restoreCursor()`（Screen.h:149/152，已含 wrap-pending 保存）、`setAutoWrapMode()`（Screen.h:233）；Alternate 滚动永不进历史（Screen.h:54-56）；
- `putChar` 的 pending-wrap 与 DECAWM 分支已写好（ZzNativeBackend.cpp:109-139），`?7` 接线后即生效；
- `putChar` 已按 `zzCellWidthOf` 置 WideLead，续格补写留注释指明 M2 任务（ZzNativeBackend.cpp:123-124）；
- `ZzScreen::putCell` 注释明确不做 wide/continuation 一致性修复（Screen.h:112 附近），该修复责任在 putChar 层；
- `ZzCursorState.visible` 字段已存在（Types.h:80），Contour 侧已上报真实值，native 侧 `ZzScreen` 尚无存储位；
- facade 已有 `isAlternateScreen()`（ZzNativeBackend.cpp:77-80），无需新增；
- Contour 侧宽度来自 libunicode（UAX #11 全套），native 不可复用（OFF 构建必须无 Contour 依赖）。

## 4. 设计

### 4.1 EAW 宽度表

- 生成脚本 `scripts/gen_unicode_width.py`：
  - 从 unicode.org 下载钉版 Unicode 16.0.0 的 `EastAsianWidth.txt`（构建期无网络依赖，生成物入库）；
  - 解析码位区间，合并相邻同属性区间，输出升序、不重叠的紧凑区间表到 `include/ZzTerm/detail/UnicodeWidthData.inc`（随公开头安装）；
  - 生成文件头部记录 Unicode 版本、数据来源 URL、生成日期（§8 可追踪性）；
  - 网络失败、数据格式漂移（字段数/区间方向异常）显式报错退出非零；
- `zzCellWidthOf(char32_t)` 改为对区间表二分查找：`F`/`W` 返回 2；`A`（Ambiguous）由配置决定（默认 1，xterm 兼容）；其余（`N`/`Na`/`H` 及未列出码位）返回 1；C0/C1 控制区间与 combining 区间（`Mn`/`Me` 不在 EAW 数据内，M2 不查 General_Category）按 1 处理，注释钉住 M3 聚簇；
- Ambiguous 配置口：facade 新增 C++ 成员方法 `ZzTerminal::setAmbiguousWidthMode(bool wide)`（facade 为纯 C++ 类，无 C 接口层），经 ZzTerminalBackend 新纯虚下发；native 存储标志并传入查表；Contour 无对应配置项，适配层空操作并在注释钉住为已知分歧，compat 不含 Ambiguous 维度对照；
- 表完整性单测：区间升序不重叠、抽查已知码位（U+4E2D 中→W、U+FF21 Ａ→F、U+00B7 ·→A、U+0041 A→Na）、版本字符串非空。

### 4.2 putChar 宽字符流（native）

- 宽度 2：写 WideLead 于当前格、WideContinuation 于下一格，光标前进 2 列；
- 右边界放不下（`col == cols-1`）：xterm 语义——当前格改写为空格（携带画笔背景），立即执行 wrap 流程（滚动区底则 scrollUp，否则下一行行首），宽字符落新行行首；不置 wrap-pending 给宽字符自身（宽字符占满行尾两格时置 wrap-pending，下一个字符换行）；
- 覆写一致性：落格位置若覆盖既有宽字符的 WideLead 或 WideContinuation 任一半，另一半清为空格（携带被清格的背景）后再写入；
- 换行触发的滚动仅在 Primary + 全屏滚动区时进历史（沿用 ZzScreen 现有回调语义）。

### 4.3 DEC 私有模式接线（NativeCsiDispatch.cpp）

仅处理下列模式，其余 DEC 私有与 intermediate 序列继续安全忽略：

- `?1049h`：`saveCursor()` + `setActiveBuffer(Alternate)` + 清 alt 全屏（eraseFill 填充）+ 光标回 alt 的 (0,0)；`?1049l`：`setActiveBuffer(Primary)` + `restoreCursor()`；
- `?1047h`：`setActiveBuffer(Alternate)` + 清 alt 全屏；`?1047l`：`setActiveBuffer(Primary)`；
- `?1048h/l`：`saveCursor()/restoreCursor()`；
- `?7h/l`：`setAutoWrapMode(true/false)`（默认 true，构造期初值已是开）；
- `?25h/l`：`ZzScreen` 新增 `setCursorVisible(bool)` 存储位，`cursor()` 读该位填 `ZzCursorState.visible`；
- 进入 Alternate（1049h/1047h）时按 xterm 语义重置滚动区为全屏高度；回到 Primary 时滚动区恢复为切换前状态（切换时保存/恢复，与光标保存独立）；
- 每次 buffer 切换置 screenDirty（noteScreenDirty）。

### 4.4 测试

- 新增单测：
  - 宽度表完整性（见 4.1）；
  - putChar 宽字符：双格写入、光标 +2、右边界换行、覆写半格清理、宽字符位于行尾时 wrap-pending；
  - DEC 模式：1049 进出（alt 清屏、主屏恢复、光标恢复）、1047/1048 各自语义、?7l 右边界覆写不换行、?25l/h 可见性翻转、buffer 切换 dirty 置位；
- compat 测试（test_backend_compat.cpp）：
  - 用例 5（CJK 宽字符）、6（备用屏幕）、10（DECTCEM）删除 b 类分支，恢复逐格强对照；
  - 新增 DECAWM 强对照样例（`?7l` 下两后端同行为）；
  - Ambiguous 配置若 Contour 不可映射，该维度不进逐格对照，注释钉住；
- demo 实测：pyte 冒烟双后端沿用；人工/脚本验证 1049 进出（模拟 less：写主屏、1049h、写 alt、1049l、主屏恢复）与 CJK 行（中文与 ASCII 混排对齐）。

### 4.5 错误处理

- 生成脚本：网络失败 / HTTP 非 200 / 行格式不符 → 显式报错非零退出，不产生半截生成物（写临时文件成功后原子替换）；
- 运行时：无新异常路径；DEC 序列参数异常沿用现有"省略或 <= 0 回退默认值、钳到网格范围"约定；
- 宽度表查找：码位越界（大于 0x10FFFF）返回 1（防御，调用方保证解码合法）。

### 4.6 验收（沿用 M1b DoD）

- `ctest --preset linux-gcc-debug` ON 全绿（19 + 新增）；
- OFF（`-DZZTERM_WITH_CONTOUR=OFF`）构建与测试全绿（新增用例中 Contour 相关部分按现有 OFF 条件编译惯例处理）；
- shared 构建全绿；
- `doxygen Doxyfile` 零警告（注意 docs markdown 陷阱：行内 code 禁尖括号、禁以点开头、后禁紧跟顿号、禁 `#` 预处理词）。

## 5. 风险与对策

- **Contour 与 native 宽度表版本差异**：Contour/libunicode 的 Unicode 版本可能不是 16.0.0，个别码位宽度判定不同会导致逐格对照偶发失败 → 抽查样例选用两版数据中稳定为宽的常用 CJK 区间；若撞见漂移码位，换用例并注释钉住；
- **xterm 1049 细节分歧**（光标恢复时机、alt 清屏时机）：以 xterm 实际行为为准，compat 逐格对照同时约束 Contour 侧；若 Contour 语义不同（上游实现差异），退回报 b 类并注释钉住，不强行对齐；
- **覆写一致性修复波及滚动/擦除路径**：修复只放 putChar 落格处，不动 `ZzScreen::putCell` 本体，控制波及面；
- **生成脚本跨平台**：仅 Python 3 标准库，开发期工具，不进构建链。

## 6. 里程碑外后续（记录不实施）

- grapheme 聚簇（UAX #29）、输入编码、鼠标/粘贴 → M3；
- Contour cursor shape/blinking 真实值、scrollback 单调计数 → 见 M1b 收尾遗留清单；
- Unicode 版本升级机制（脚本参数化版本号）随 M5 Unicode edge cases 再评估。

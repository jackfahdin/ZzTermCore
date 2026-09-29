# M11：M7a 台账清零（UTF-8 decoder 独立 harness + 深度 fuzz 机制）

- 日期：2026-09-29
- 分支：contour
- 前置：M10 已合并 master——search 双画像进 5s 门，M8 五项门控全达标收官；CI 六 workflow 绿
- 依据：docs/superpowers/specs/2026-09-21-m7a-fuzz-infrastructure-design.md（台账：深度 fuzz、UTF-8 decoder 独立 harness）；M8/M9 系列规格排除项均将本两项移交本里程碑
- 用户批准决策：1A 台账两项全做；2A 深度 fuzz 走独立 workflow（workflow_dispatch 手动 + schedule 每周定时长跑，push CI 的 30s smoke 不动）；3A 只加深现有双 harness，不扩 harness 面（grapheme/scrollback/search 不立）；4B decoder harness 带不变量断言（decoder 为安全敏感面，不只验不崩）

## 1. 背景与目标

M7a 建成 fuzz 基建（fuzz_parser / fuzz_feed 双 harness、ASan、CI 30s smoke、种子语料、崩溃预案）时留两项台账：UTF-8 decoder（src/unicode/Utf8.cpp）仅经 parser 间接 fuzz，overlong/代理区/截断续接等边界可能被状态机分流而覆盖不足；30s smoke 只是冒烟，无长时段机制。本里程碑清零台账：decoder 独立 harness（带不变量断言）+ 深度 fuzz 机制（独立 workflow 长跑 + 种子扩充）。

## 2. 改动点

1. 新建 `tests/fuzz/fuzz_utf8.cpp`：直接驱动 ZzUtf8Decoder——整喂 decodeAll；任意切点分段 feed（切点由输入字节导出）后 finish；逐字节 feed 后 finish。三路输出码点流两两断言一致（续接不变量，decoder 文档承诺）。不变量断言：输出码点数不超过输入字节数加一（finish 收尾至多一个 U+FFFD）；输出码点不落在代理区（U+D800-U+DFFF）且不超过 U+10FFFF。断言失败即 abort（libFuzzer 捕获为 crash，artifact 落盘）。
2. `tests/fuzz/CMakeLists.txt`：收编 fuzz_utf8 第三 target（既有 foreach 列表加名）；corpus-work 复制与 smoke 注册同款追加（30s，TIMEOUT 60，ASAN_OPTIONS 同款）。本机基线 fuzz 计数 2/2 自然增长为 3/3。
3. 新建 `tests/fuzz/corpus/utf8/` 种子目录：overlong 编码、代理区编码、超 U+10FFFF、5/6 字节序列、截断的多字节序列、CJK/组合符合法样本、与 ASCII 混合流等边界种子（约 6-10 条，每条附一行注释说明覆盖意图）。
4. 新建 `.github/workflows/ci-fuzz-deep.yml`：触发为 workflow_dispatch + schedule（每周日 07:13 UTC，避整点）；三 harness 各 -max_total_time=1800（30 分钟），linux-clang-fuzz preset 构建（configure 显式 `-D CMAKE_CXX_COMPILER=clang++`，runner 的 clang 元包）；crash 即红，artifact（崩溃单元 + 最终语料）经 actions/upload-artifact 上传（版本取最新稳定 Major，执行期查 releases 页核）。push 不触发。
5. feed/parser 种子顺手扩充数条（深化不是扩面）：parser 补 DEC 私有模式组合、OSC 超长串、CSI 参数溢出形态；feed 补 resize 高频抖动 + 大 historyCap 混合流。

## 3. 验证策略

- 本机基线五项：linux-gcc-debug 50/50、m2-off-check 40/40、m2-shared-check 50/50、linux-clang-fuzz 3/3（新增 smoke）、doxygen exit 0 零警告。
- push CI 六 workflow 绿（smoke 30s 不变）。
- 深度 workflow 实证：手动触发一次 ci-fuzz-deep，确认 30 分钟档跑通、artifact 机制工作（撞 crash 按 §4 处置）。
- decoder 语义以 Utf8.h 文档为唯一准绳：断言与文档承诺不一致时以代码既有语义为准修断言并留痕，本里程碑不修改 decoder 行为。

## 4. 错误处理

- deep 长跑撞出 crash：不阻塞里程碑收尾——crash 单元与复现输入入库留痕（tests/fuzz/corpus/ 回归种子 + 记录文档），修复排期由用户裁定（M7a 规格 4.5 预案同款）。
- smoke 阶段 fuzz_utf8 即撞不变量失败：属本里程碑必须修复项——先判断言错还是 decoder 错，decoder 错则修复并补回归用例（decoder 是公开 API 内部件，修复走正常审查流）。
- schedule 仅未来生效，本里程碑验证以 dispatch 手动触发为准。

## 5. 排除项（本里程碑不做）

- 新 harness 面（grapheme break / scrollback / search，3A）；
- push CI smoke 时长与形态变更（30s 不动）；
- decoder 行为/语义修改（只 fuzz 现状）；
- macOS fuzz（M9a 3B 既定 Linux 专属）；
- corpus 的 Git LFS 或外部语料库引入（种子维持小体量源码内）。

## 6. 记录与收尾

- 记录文档 docs/superpowers/specs/2026-09-29-m11-fuzz-deepening-record.md：harness 断言设计、种子清单与覆盖意图、deep 首次运行结果（三 harness 的执行轮次/覆盖计数/是否 crash）、台账清零声明。
- 收尾惯例：finishing 合并 master（--no-ff），合并后本机全基线回归 + doxygen，contour 保留。

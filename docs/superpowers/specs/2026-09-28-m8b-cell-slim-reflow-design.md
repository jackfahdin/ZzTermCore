# M8b 设计：优化波（Cell 12B bit-pack + ChunkedScrollback 裁剪越界修复 + reflow 峰值治理）

- 日期：2026-09-28
- 分支：contour
- 前置：M8 测量波已合并 master（c534a70）——双轨 benchmark 矩阵 + 全矩阵基线（docs/superpowers/specs/2026-09-22-m8-million-line-probe.md），50/50 全绿
- 依据：M8 门控判定（1M 档 RSS 峰值 6712MB 超 512MB 阈值 13 倍、全量 search 9880ms 超 5s 阈值 2 倍，其余达标）；T3 审查确认的 ChunkedScrollback trimToCapacity/lineAt 越界缺陷（部分擦除破坏定长寻址不变量，SIGABRT 可复现）
- 用户批准决策（M8 决策点）：1A Cell 瘦身方向、2A 缺陷随本里程碑修复、3A 测量波先合并；（M8b brainstorming）：1A 12B bit-pack（值语义保持）、2A 顺带 reflow 峰值治理、3A 优化复测后按门控修正规则调阈值（留痕报确认）、4A headOffset_ 偏移寻址、5A search 不专项优化只复测记录
- 瘦身深度量化结论（brainstorming 摆证后用户知情确认）：1M 行 80 列下，16B 现状 1294MB、12B bit-pack 约 990MB、8B 样式表约 660MB；512MB 门控需不超过 6.4B/cell，只有压缩或行级稀疏编码可达——本里程碑不追硬达标，目标为值语义保持下的最大削减 + 峰值治理 + 门控诚实修正

## 1. 背景与目标

M8 测量波给出数据驱动的优化靶点：RSS 主矛盾是 Cell 明文驻留（16.7B/cell），峰值主放大源是 reflow 双套并存；另有一个已确认的正确性缺陷（裁剪越界）随本里程碑清偿。本里程碑三件事：缺陷修复（正确性）、Cell 12B bit-pack（-25% 常驻内存）、streaming reflow（消除 2 倍峰值），最后全矩阵复测与门控修正留痕。

## 2. T1：ChunkedScrollback 裁剪越界修复（正确性，最高优先）

- 根因：trimToCapacity() 对头部块部分擦除后，块 0 行数小于 256，破坏 lineAt() "index / 256 定长寻址"的隐含不变量（src/history/ChunkedScrollback.cpp:51-57 vs :121-135）；
- 修法：ZzChunkedScrollback 增 headOffset_ 成员（头部块已擦除行数），lineAt 物理索引 = headOffset_ + index 后再定长寻址；整块弹出（pop_front）时 headOffset_ 归零；reflow 重建块链后归零；append/setCapacity/clear 路径语义不变；
- 强制流程（兼容 bug 铁律）：先在 tests/unit/test_scrollback.cpp 加最小复现——容量非 256 整数倍（如 300），append 超出触发部分裁剪，逐行校验 lineAt 内容序列与 wrapped 标记——确认 FAIL（现状越界），再修复确认 PASS；
- 复现用例修后保留为常态化回归。

## 3. T2：ZzCell 12B bit-pack（公开 API 语义零变化）

- text_（4B）：width 2 位移入 bits[25:24]（codepoint 模式 bits[20:0] 与 cluster 模式 bits[23:0] 均不占用，两模式兼容）；bit26 保留为未来的稀疏扩展位；reserved_ 字段删除——hyperlink id 等预留扩展点改由 bit26 或行级侧表承担（本规格留痕该取舍）；
- attrs（12 位已用）拆分嵌入：低 6 位嵌 fg_ 空闲高位 bits[29:24]，高 6 位嵌 bg_ 同位（ZzColor value_ 的 kind 占 bits[31:30]、数据最多 24 位，bits[29:24] 全模式空闲）；ZzColor 公开类本身不变（4 字节 static_assert 保持）；ZzCell 内 fgWord_/bgWord_ 裸存 32 位，foreground()/background() 掩码还原，setForeground()/setBackground() 读改写保住嵌入属性位；
- operator== 逐字比较语义不变（裸字比较等价于字段逐项比较）；ZzCellAttributes 公开类型不变（仍为 2 字节 API 类型）；
- static_assert 改钉 sizeof(ZzCell)==12、alignof==4；include/ZzTerm/Cell.h 文件头与类注释的布局记录同步更新（含 text_ 位分配新表与 attrs 嵌入约定）；
- tests/unit/test_cell.cpp 的 sizeof 断言同步改为 12；属性/颜色/宽度/cluster 全组合行为用例既有断言零改动通过即值语义保持的证据；
- 影响面自证：全部调用方经公开 API（双后端、compat、reflow、selection、search、bench）零改动，50/50 原样绿为门。

## 4. T3：streaming reflow（峰值治理，内部实现零 API 影响）

- 现状：ChunkedScrollback::reflow 把全部历史搬进单个 vector 调 zzReflowLines，输入输出并存造成约 2 倍瞬时峰值（facade 1m mixed 峰值 6712MB 的主放大源）；
- 改造：src/screen/Reflow.h（内部头，非公开）的 zzReflowLines 增流式形态——逐逻辑行链消费、逐物理行产出，跨边界只携带未完成链；ChunkedScrollback::reflow 逐 chunk 喂入、产出直接写回新块链，额外内存 O(链长)；
- 语义一致性门禁：zzReflowLines 重构前后的输出对同一输入必须逐字节一致——tests/unit/test_reflow.cpp、test_screen_reflow.cpp、test_native_reflow.cpp 既有断言零改动通过；
- native 后端 resize 路径（ZzNativeBackend）若同样经全量 vector 调 reflow，顺带对齐流式口（同文件族改动；screen 区行数 = rows，量级可忽略，以 scrollback 为主战场）。

## 5. T4：全矩阵复测 + 门控修正 + probe 续表

- 重跑 bench-long 六命令（同机同 preset），与 M8 基线（tests/perf/records/2026-09-22-m8-*.json）前后对照，新 JSON 以 2026-09-28-m8b-*.json 入库；
- probe 续表：docs/superpowers/specs/2026-09-22-m8-million-line-probe.md 追加 M8b 节（前后对照表 + 改善幅度）；
- 门控修正（规格 m8-million-line-design.md §4 修正规则通道）：预设候选——单元轨 1M RSS 口径新阈值 1100MB（12B 预估约 990MB + 10% 余量）、facade peak 口径新阈值按复测实测定（消除峰值翻倍后预估 ascii 约 1.5GB / mixed 约 3GB）；原值/新值/理由留痕，T4 结果连同修正值报用户确认后落定；
- search 复测记录（不专项优化；12B 降内存流量可能顺带提速，如实记录）。

## 6. 明确排除

- warm LZ4 / cold mmap 分层（架构演进路线后续里程碑）；
- search 路径专项优化（增量索引等）；
- ZzColor / ZzLine / ZzScrollback 公开签名任何变化；ZzCell 公开 API 语义变化（内部位布局变化除外）；
- 样式表 interning（8B 路线，破坏值语义，已否决）；
- contour 侧与 third_party 任何改动；
- macOS（M9）。

## 7. 验收（DoD)

- T1：裁剪越界最小复现用例入常态化回归（FAIL→PASS 流程留痕于 commit 序），三配置全绿；
- T2：sizeof(ZzCell)==12 static_assert 落位，50/50 + OFF 40/40 + shared 50/50 原样绿（含全部 compat/golden），fuzz 2/2，doxygen 零警告；
- T3：reflow 语义一致性三测试原样绿；facade 1m 峰值较 M8 基线显著下降（数据说话）；
- T4：probe 续表前后对照入库；门控修正留痕并经用户确认；
- 规格/计划入库。

## 8. 任务划分

- T1：ChunkedScrollback 裁剪越界修复（复现 FAIL → 修复 → PASS）；
- T2：ZzCell 12B bit-pack（值语义保持，static_assert + 全回归自证）；
- T3：streaming reflow（Reflow.h 流式口 + ChunkedScrollback 接入 + 语义一致性门禁）；
- T4：全矩阵复测 + probe 续表 + 门控修正（报用户确认）。

顺序执行：T1 独立最小；T2 改公开头内部位布局（全量回归重）；T3 与 T2 同碰 ChunkedScrollback 但不同函数域，后置避免交织；T4 收尾决策点。任何任务发现行为偏差即停下报 BLOCKED。

# M13 集成 spike 记录

- 日期：2026-09-29
- 代码：ZzClawTerm 仓 spike/（commit `19a24f6` spike 主体 + `bbe0443` 链接名统一 + `84cb83c` SSH 链与 probe）
- 关联 commit：ZzTermCore `655cd5b`（build-tree 导出名统一，T1 审查升格修复）
- 规格：2026-09-29-m13-spike-design.md

## 1. 结论判定

对照规格 §1 判定标准逐项：

| 判定项 | 自动验证 | 人工验证 |
| --- | --- | --- |
| 本地 PTY 链（spawn bash -> feed -> 渲染 + 键盘回写） | 通过（QTest 双用例绿；probe offscreen 标记回读 PROBE-OK） | 通过（ls/回车/vim 进出正常，方向键可用） |
| SSH 链（ZzSshCore channel -> 同 widget） | 通过（localhost 公钥认证、shell channel、标记回读全链绿） | 通过（ls/echo/vim 正常；resize 后 tput cols 随窗口尺寸变化） |
| 三清单产出 | 已产出（本文 §2/§3/§4） | — |

判定：**行**。用户人工实测两链路通——本地 PTY 链 ls/回车/vim 进出/方向键均正常，SSH 链 ls/echo/vim 正常且 resize 后远端 tput cols 随窗口尺寸变化（reflow + SIGWINCH 与 window-change 两条 resize 通路均实证）。人工实测暴露的问题（IME 不可输入中文、回显错位、resize 内容丢失、Ctrl+C 无响应）全部落在 spike 种子层 widget 与排除项上，见 §4 定性，**无一根因在 RenderView 契约或 Core API**——Core 侧「行」的判定成立，三阶段路线可按 §5 建议推进。

## 2. 打包问题清单

- add_subdirectory 形态：顺。spike 独立工程以 add_subdirectory 消费 Core 源码，ZZTERM_WITH_CONTOUR OFF + ZZTERM_BUILD_TESTS OFF 两个缓存变量即静态编进（libZzTermCore.a / libZzTermPty.a），配置 0.3s 级、零告警。ZzSshCore 同款收编：vendored OpenSSL bundle 命中后固化 OPENSSL_ROOT_DIR，嵌套 libssh2 子模块自动静态编译，ZZSSHCORE_BUILD_TESTS OFF。
- 静态 contour OFF 约束的实际影响：零。spike 只用 Native 后端，显式 OFF 后链接面无 contour 符号，前置批实证的约束在本场景无额外代价。
- ZzTerm::Pty 导出可用性实证：add_subdirectory 下直接可链接、全链可用（前置批 install-tree 导出集复核亦绿——ds-consumer-static 重装后运行输出 downstream check OK）。
- 前置批已知项复核一（导出名不一致，已修复）：install-tree 导出 ZzTerm::ZzTermCore 而 build-tree 仅有别名 ZzTerm::Core，同一 Core 两种消费路径两个名字。ZzTermCore `655cd5b` 在根 CMakeLists 补 build-tree 别名后统一，spike 与下游共用文档名链接；基线 50/50 + 下游 + doxygen 三重回归绿。
- 前置批已知项复核二（ZzPty 阻塞 fd）：见 §3 缺口 2，已转 API 缺口跟踪。
- 新观察（ZzSshCore 收编两行成本，非 Core 问题）：其一，BUILD_SHARED_LIBS OFF 须在其 add_subdirectory 之前强制（libssh2 默认 ON）；其二，Qt 公共依赖扫描要求顶层 find_package 声明 Network，否则 finalize 告警「Network target ... not declared」——spike 已补，正式集成照此一行即可。

## 3. API 缺口清单

| # | 想要的接口 | spike 期绕行形态 | 建议归宿 |
| --- | --- | --- | --- |
| 1 | RenderView 历史行只读访问（滚动查看历史） | 不实现滚动——ZzRenderView 仅暴露屏幕区 lineAt；ZzTerminal 的 scrollback 存在但标注 Core 内部使用、仅 Native、不属 Renderer API | M13 特性对齐阶段由 Core 提供后端无关的历史行只读视图，或明确放行 scrollback 给前端——**M14 已闭环**（ZzHistoryView，见 2026-09-30-m14-history-view-design.md） |
| 2 | ZzPty master fd 非阻塞配置 | 调用方自置 fcntl O_NONBLOCK（宿主与 QTest 各一处，已注释）；阻塞 fd 上「read 循环到 EAGAIN」会吊死事件循环（T1 实证挂起 301s 被 watchdog SIGABRT，栈钉在 ZzPty read） | 评估 ZzPtyConfig 加 nonBlocking 项或 spawn 默认置非阻塞；文档补写「需调用方自置 O_NONBLOCK」 |
| 3 | ZzSshCore 默认私钥探测 | 宿主侧自探测 id_ed25519 / id_ecdsa / id_rsa 首个存在者（另留 --key 显式指定）——ZzSshAuthConfig 的 privateKeyPath 为空即整段跳过公钥认证直落密码，与 OpenSSH 默认 key 扫描习惯不同 | 属 ZzSshCore（ZzClawTerm 仓）而非 Core：正式集成时在应用装配层或 ZzSshCore 内补默认路径扫描 |

无其他阻塞性缺口：feed / renderView / resize / sendText / sendKey / setOutputHandler / ZzPty 的 spawn、read、writeAll、resize、masterFd 与 ZzSshCore 的 connectToHost、createShellChannel、openShell、write、resize 均与设计事实一致，两链未踩新缺口。

## 4. 行为观察清单

人工验证（**用户已执行闭环**，两链实测现象与定性如下）：

```bash
cd /home/zz/Jackfahdin/github/ZzClawTerm
./build/spike-debug/zzcore_spike --local          # 本地 PTY 链
./build/spike-debug/zzcore_spike --ssh localhost  # SSH 链（首次弹密码框为兜底，localhost 走公钥）
```

- [x] echo/ls 输出正常上屏（两链）
- [ ] CJK 与 emoji 渲染（宽度对齐）——中文不可输入（IME 排除项，见下现象 1），渲染宽度未单独验
- [x] vim 进出（Alternate Screen、方向键 application 模式；两链正常）
- [x] 拖 resize 重排（SSH 链实证：resize 后 tput cols 随窗口尺寸变化）
- [ ] 键盘输入：回车/方向键正常；Ctrl+C 在 cat 阻塞场景无响应（见下现象 4）
- [ ] 滚动查看历史（本 spike 未实现——缺口 1，预期跳过，已转 M14 议题）

人工实测暴露的八现象与定性（全部落在 spike 种子层，无一指向 Core 契约层缺陷）：

1. **无法输入中文（两链）**：IME 是 spike 规格明示的排除项，未接 QInputMethodEvent——非缺陷，是已知的未实现面。特性对齐阶段优先级由 P2 升至 **P1**（中文输入是目标用户群的刚需交互）。
2. **打字不可见、回车后提示符所在行错位（--local）**：~~疑 spike widget 的种子层渲染/光标定位问题~~ **已根因并修复**（ZzClawTerm `ec9acdf`）：runLocal 用 rawMode=true 调 ZzPty::spawn，slave 被 cfmakeraw（-echo -opost），打字零回显、换行裸 LF 列保持（提示符缩到行尾、ls 多栏塌单行）。修复=rawMode=false 与 ZzTermSmoke 对齐，TDD 红绿形态正确（回显链用例 bashEchoChainWorks 钉住）。
3. **方向键翻看历史命令看不到之前输入（--local）**：与现象 2 同根（-echo/-opost 的 readline 重绘缺失），随 `ec9acdf` 一并修复。
4. **cat 阻塞后 Ctrl+C 无响应、界面似卡死（--local）**：~~疑 sendKey 编码路径或 ISIG 投递缺失~~ **同根修复**（`ec9acdf`）：cfmakeraw 的 -isig 使 Ctrl+C 不产生 SIGINT；rawMode=false 后恢复。
4a. **补充根因证据**（调查反事实闭环）：raw spawn 的 bash 里 `stty sane` 后回显、CRLF 换行、SIGINT 立即全恢复，唯一变量为 slave termios；两模式均无 job control 告警，ZzPty spawn 的 setsid/TIOCSCTTY 完好；ZzTermSmoke 正常仅因显式 rawMode=false。
5. **resize 后内容丢失、拉大不恢复（两链）**：疑 spike widget 的 resizeEvent 早退或格宽换算错误尺寸下发；SSH 链 tput cols 随尺寸变化实证 Core resize 通路正常，故定种子层。历史重排语义与 M14 历史访问契约相关，顺带在 M14 一并定性。**M15/M16 已根治**：行向截断不压历史由 M15 对齐 contour 根治（缩行压历史/扩行回抽）；列向硬行截断由 M16 对齐 contour/xterm 根治（缩列多行化/拉大接回，spec 2026-09-30-m16-hardline-reflow-design.md）
6. --ssh 其余项（启动、ls、echo、vim、方向键）正常，无新增现象。

以上 backlog 条目属 ZzClawTerm 仓 spike widget 演进面，不阻塞 Core 侧里程碑推进。

自动实测行为（本批 offscreen 实证）：

- probe 自检（--probe 配合 offscreen，两链通用）：链路就绪后写 echo 标记，100ms 轮询 renderView 文本命中即 PROBE-OK 退出 0，15s 超时退出 3。实测 --local 与 --ssh localhost 均秒级 PROBE-OK；带用户与端口的完整目标形式解析正常；缺目标参数退出码 2。
- SSH 建连计时实测：DNS 0-1ms、libssh2 握手 9-12ms、公钥认证 14ms、shell channel 打开 61ms，总计 24-27ms（localhost）。
- 认证顺序实证：agent 失败 -> 公钥成功（ZzSshAuthConfig 固定顺序如实工作）；probe 模式密码请求直接取消，无人值守可跑。
- TOFU 实证：首轮自动信任、打印指纹并落临时目录 known_hosts，次轮静默校验通过（不再打印信任行）——ZzSshHostKeyStore 存取行为正常。**注意：自动信任是 spike 弃子简化（无人值守自检所需），不可沿用——正式集成必须回到 ZzSshTransport 形态：hostKeyUnknown/hostKeyChanged 确认弹窗 + AppConfigLocation 持久化 known_hosts。**
- 渲染契约层（T1 观察沿用并复验）：光标反色矩形、全量重绘无增量、resize 按格宽换算调终端 resize 并发 gridResized——同一信号 PTY 侧转 SIGWINCH、SSH 侧转 window-change（库侧尾随去抖 150ms），两传输链接法完全同形。
- 键盘编码经终端 output 通道（sendText/sendKey），application cursor 等模式位与 feed 联动同步（T1 钉住路径，SSH 链同 widget 直接继承）。

## 5. 三阶段路线输入建议

- 特性对齐阶段：P0 = 历史行访问缺口（滚动是终端基础交互，ZzClawTerm 现有 ZzTermWidget 有滚动，缺它无法对齐）；P1 = ZzPty 非阻塞配置（小改消绕行）+ IME 中文输入（人工实测两链均不可输入中文，由排除项升为刚需）。ZzCoreViewWidget 种子已验证渲染/键盘/resize/两传输链，可按对齐标准直接演进；选择/鼠标/配色与其余 IME 面为 spike 排除项，是该阶段工作量主体与主要不确定面。
- 历史调和与 ptyqt 退役：ZzPty 经两链实证可替 ptyqt 驱动本地会话（补上非阻塞配置后无绕行）；SSH 侧 ZzSshCore 直驱形态与 ZzSshTransport 适配层接口（write/resize/dataReceived/closed）一一对应，适配层改写成本低；ZzSshCore 默认私钥探测（缺口 3）在会话装配退役 ptyqt 时一并处理；spike 的 TOFU 自动信任同为退役面——正式会话装配必须回到确认弹窗 + AppConfigLocation 持久化 known_hosts，不得沿用弃子简化。
- 删 ZzTermWidget：风险排序——历史/滚动（依赖 Core 新 API，最高）> 选择/鼠标/IME/配色（spike 未验证，中）> 渲染契约本身（两链实证，低）。打包面无阻塞：add_subdirectory 与 install-tree 两种消费形态均实证可用。

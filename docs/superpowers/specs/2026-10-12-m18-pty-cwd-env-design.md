# M18 ZzPtyConfig cwd/env 扩展设计

- 日期：2026-10-12
- 前置：M17b 拼接行视图（tag m17b，f1c54fc）；ZzClawTerm Z1 收官（fabc421）
- 分支：contour

## 1. 目的与范围

ZzClawTerm Z1 换血后两个行为回退的根治（ptyqt 时代有、ZzPty 没有）：

1. 本地会话起始目录：ptyqt 强制 home，ZzPty 继承父进程 cwd（GUI
   启动器场景可能是 /）；
2. 环境变量注入：ptyqt 在缺失时补 TERM/COLORTERM（Z1 以进程级
   qputenv 过渡顶上，属权宜）。

范围内：ZzPtyConfig 增 cwd/env 两可选字段 + Unix/Windows 两侧实现
+ Core 单测 + ZzClawTerm 侧接线切换（删 qputenv 过渡）。

范围外：ZzPty 其他能力（信号转发、线程模型等）；ZzClawTerm 其他
Z2 条目。

## 2. 决策记录（brainstorming 定案）

| # | 议题 | 定案 |
| --- | --- | --- |
| 1 | API 形态 | `std::string cwd`（空=继承父进程）+ `std::vector<std::string> env`（"KEY=VALUE" 形，同名覆盖父环境）；纯新增字段，默认值 = 旧行为 |
| 2 | chdir 失败语义 | 按 spawn 失败处理（子进程写 errPipe/errno 保留），与 exec 失败同通道 |
| 3 | 测试 | Core 单测风格（无 Qt）：spawn `pwd` 验证 cwd、spawn `printenv` 验证注入与覆盖；Windows 经 CI |

## 3. 接口（pty/ZzPty.h，纯新增）

```cpp
struct ZzPtyConfig {
    std::vector<std::string> argv; // ……既有字段不动……
    int  cols    = 80;
    int  rows    = 24;
    bool rawMode = true;
    /// 子进程工作目录（空 = 继承父进程 cwd）。
    std::string cwd;
    /// 额外环境变量（"KEY=VALUE" 形），同名覆盖父进程环境。
    std::vector<std::string> env;
};
```

ABI：纯新增字段（聚合体尾部），按 M14 先例登记 ABI 新增，
次版本口径沿用。

## 4. 实现语义

### 4.1 Unix（pty/unix/ZzPty.cpp）

- fork 前：构建合并环境向量——复制 `environ` 全部条目，再按
  `env` 覆盖同名键（先删同名旧条目再压入新条目），转 `char*` 数组；
- 子进程（保持纯 async-signal-safe）：cwd 非空先 `::chdir(cwd)`，
  失败写 errPipe 的 errno 后 `_exit(127)`（与 exec 失败同通道，
  父进程现有失败处理零改动）；exec 换带环境的形式（见勘误 E-1）；
- 父进程逻辑不变。

勘误 E-1（2026-10-12 实现期发现）：初稿写「execvp 换 execve」，
实测 execve 不搜 PATH（argv[0] 为程序名时 ENOENT）。落地口径：
子进程内 `environ = envp.data();`（指针赋值，async-signal-safe）
后仍走 `::execvp`——合并环境经 environ 全局指针生效，PATH 搜索
保留，macOS 无 execvpe 的可移植性坑也一并绕开。

### 4.2 Windows（pty/windows/ZzPty.cpp）

- cwd 非空：`CreateProcessW` 的 `lpCurrentDirectory` 传宽字符 cwd
  （既存 toWide 辅助复用）；为空保持 nullptr（继承）；
- env 非空：构建双 NUL 结尾宽字符环境块——`GetEnvironmentStringsW`
  取父环境，按键序合并（env 覆盖同名；Windows 环境块按字母序的
  约定遵守），传 `lpEnvironment`；为空保持 nullptr（继承）；
- STARTF_USESTDHANDLES 等既有守则不动（M12 R1-R6 实证成果）。

## 5. 测试

新增 `tests/unit/test_pty_cwd_env.cpp`（GLOB 收编）：

1. cwd 注入：临时目录为 cwd spawn `pwd`，读回输出等于该目录
   （realpath 对账）；
2. cwd 缺省：spawn `pwd` 输出 == 父进程 getcwd；
3. cwd 非法路径：spawn 失败返回 nullptr（errno 通道）；
4. env 注入：spawn `printenv ZZ_M18_PROBE` 读回标记值；
5. env 覆盖：先注 `ZZ_M18_PROBE=a` 再同键 `=b`，读回 b（覆盖语义）。

Windows 对应路径经 CI（windows-msvc）验证；本机 Linux 只保 Unix
分支编译与测试绿。

回归：五项基线（linux-gcc-debug 62、m2-off-check 51、
m2-shared-check 62、fuzz 3、doxygen 零警告）+1 新测试
（63/52/63）。

## 6. ZzClawTerm 侧接线（同里程碑，ZzClawTerm 仓库一笔）

`ZzLocalPtyTransport::open`：
- `config.cwd = QDir::homePath().toStdString()`（恢复 ptyqt 旧行为）；
- env 注入改走 `config.env`：TERM（取 endpoint.terminalType）与
  COLORTERM=truecolor，仅父环境缺失时注入（保持「不覆盖用户环境」
  口径）——删 Z1 的 qputenv 过渡两行；
- 子模块 pin 前移到 M18 落地 commit。

## 7. 文档同步

- pty/ZzPty.h 字段 doxygen 注释；
- docs/API.md 版本节 ABI 新增登记；
- Architecture-v2.md PTY 小节一句。

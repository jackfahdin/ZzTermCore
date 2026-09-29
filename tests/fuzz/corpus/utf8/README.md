# corpus/utf8 种子清单（每行：文件名 + 覆盖意图）

- `ascii_basic` — 纯 ASCII 基线（单字节直通路径）
- `cjk_2x` — 两个合法三字节 CJK 序列（U+4E2D U+6587）
- `overlong_nul` — overlong 两字节编码 NUL（C0 80，Table 3-7 非法）
- `overlong_3byte` — overlong 三字节形态（E0 80 80，越下界非法）
- `surrogate_d800` — 代理区编码（ED A0 80，Table 3-7 非法）
- `beyond_u10ffff` — 超 U+10FFFF 编码（F4 90 80 80，越上界非法）
- `five_byte_seq` — 5 字节序列（F8 …，RFC 3629 已废除形态）
- `truncated_tail` — 三字节序列截断（E4 B8，驱动 finish 收尾补 U+FFFD）
- `mixed_cjk_combining` — ASCII + CJK + 组合符（U+0301）混合流
- `stray_continuations` — 裸续接字节流（80 80 BF，非法起点重解释路径）

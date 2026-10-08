# ZzTermCore 文档导航

## 现行文档

-   [Architecture.md](Architecture.md)：技术设计与开发规范（开发基线）。
-   [VT-Xterm-Checklist.md](VT-Xterm-Checklist.md)：VT/xterm 功能、开发进度与测试追踪。
-   [API.md](API.md)：高层 API 说明。必须随公开接口实时更新，属于代码评审的
    Definition of Done（见 Architecture.md 第 17 节）。
-   [Scrollback-and-Reflow.md](Scrollback-and-Reflow.md)：resize 内容保全的
    实现说明（物理行/逻辑行模型、列变 reflow 协调、行变条件语义）。

## 生成文档

-   `api-html/`：由 Doxygen 从 `include/ZzTerm/` 的中文注释自动生成
    （`doxygen Doxyfile`），详细接口以生成结果为准。该目录已被
    `.gitignore` 忽略，不入库。

## 计划中的文档

随实现逐步增加：`Unicode-and-Cell-Model.md`、`Rendering.md`、
`Highlighting.md`、`Testing.md`、`Coding-Standard.md`。

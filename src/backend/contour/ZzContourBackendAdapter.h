#pragma once

// Contour 后端适配器工厂（M1b）。头文件 C++20 干净、无 Contour include，
// 供 ZzTermCore 主库（Terminal.cpp 工厂分支）在 ZZTERM_WITH_CONTOUR 下引用。

#include "../ZzTerminalBackend.h"

#include <cstddef>
#include <memory>

/// \brief 工厂：创建 Contour 后端的 ZzTerminalBackend 实现（M1b）。
/// scrollbackLines 透传 ZzContourBackend。
std::unique_ptr<ZzTerminalBackend> zzCreateContourBackendAdapter(int cols, int rows,
                                                                 std::size_t scrollbackLines);

// 逻辑行拼接与选区文本提取（M5a）。纯函数，双后端共用。
#pragma once

#include <ZzTerm/Types.h>

#include <cstdint>
#include <string>

class ZzIPhysicalLineSource;

// 统一空间逻辑行总数（按 wrapped 链分组）。
[[nodiscard]] std::int64_t zzLogicalLineCount(const ZzIPhysicalLineSource& src);

// 提取半开区间 [start, end) 的纯文本。规则（规格 5.4）：
//   1. 宽字符按格步进，续格跳过；边界落在半字上时归一（start 退到 lead、
//      end 进到续格之后），不拆半字；
//   2. cluster 格取整串；
//   3. 软换行不插换行；跨逻辑行插单个 '\n'，末尾无换行；
//   4. 每条逻辑行尾部的空单元格与空格修剪；行内空单元格输出为一个空格。
// 坐标越界 clamp；start == end 返回空串。
[[nodiscard]] std::string zzExtractSelectionText(const ZzIPhysicalLineSource& src,
                                                 ZzLogicalPos start,
                                                 ZzLogicalPos end);

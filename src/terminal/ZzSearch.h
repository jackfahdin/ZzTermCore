// zzSearchLines：子串搜索引擎（M5b）。纯函数，双后端共用。
// 单次物理行扫描，按 wrapped 链逐条组建逻辑行文本，命中格偏移惰性遍历
// 换算（不建逐字节回映表），每时刻只持有一条逻辑行的数据——不拼全量大串
//（Architecture §13）。
#pragma once

#include <ZzTerm/Types.h>

#include <string_view>
#include <vector>

class ZzIPhysicalLineSource;

// 在统一空间内搜索子串，返回全部命中（坐标升序，半开区间）。
// 规则（规格 5.2）：不跨逻辑行匹配（pattern 含换行符永不命中）；命中不重叠
// （命中后从 match 末尾继续）；空 pattern 返回空列表；caseSensitive=false 时
// 按 ASCII 大小写折叠（Unicode 不折叠，v1 钉死）。命中坐标由字节位置
// 惰性遍历换算为格偏移，宽字符完整覆盖不拆半字。
[[nodiscard]] std::vector<ZzLogicalRange> zzSearchLines(const ZzIPhysicalLineSource& src,
                                                        std::string_view pattern,
                                                        ZzSearchOptions options);

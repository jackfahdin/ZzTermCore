// ZzContourLineSource：contour 后端的统一物理行数据源（M5a）实现。
//
// 步骤 0 调研结论（Contour reflow × stable id，据 third_party/contour 本地 fork
// Grid.cpp/Grid.hpp，行号以本提交为准）：
//
// ①「列变化 resize 后 stableRangeFloor 是否只随容量裁剪前移（reflow 本身不制造
//    虚假前移）」——不成立：reflow 会扰动 floor。
//    列变化时 growColumns（Grid.cpp:973）与 shrinkColumns（Grid.cpp:1123）的
//    reflow 路径重建整条环形缓冲后，以 rotateBuffersLeft(newHistoryLineCount)
//    收账：_stableBase += newHistoryLineCount，随后 syncStableFloor() 的
//    max(_stableFloor, _stableBase - historyLineCount()) 把 floor 顶到至少旧 base
//    （= 新 base - 新历史深度）。reflow 前历史未满容量（floor < base）时，floor
//    会跳过整个旧历史深度——这不是容量裁剪，而是行身份重建的副产：旧历史行的
//    stable id 全部低于新 floor 被作废，重建后的历史以 [旧base, 旧base+新历史)
//    的全新 id 重新编号。Grid.cpp:1169-1174 在 columnsChanged 时
//    bumpGeneration(RowIdentity::Destroyed)，与同文件 :1116-1122 的注释（直转
//    shrunkLines 会让新历史全部沉到 floor 之下）互证：列变化即 RowIdentity
//    ::Destroyed，stable id 不跨 reflow 保留。纯行数变化（growLines/shrinkLines）
//    走 rotateBuffersLeft/Right 正常收账，RowIdentity::Preserved，floor 仅在真实
//    裁剪时前移；唯一降低 floor 的路径是 rotateBuffersRight 的 zero-history 分支
//    （Grid.hpp:1062-1072，alternate 屏反转滚）。
//    对丢弃计数的调整：列变化 resize 后 floor 的前移不对应任何真实丢弃（内容
//    仍可读），若按差值累计会把整个旧历史深度误记为 dropped。因此适配层约定：
//    feed 后与纯行数 resize 后调 noteFloor() 累计真实裁剪；列变化 resize 后调
//    reanchorFloor() 直接对齐不累计（此时选区锚点因 RowIdentity::Destroyed 本就
//    失效，由后续任务重建）。
//
// ②「历史行内容在 reflow 后仍可按负偏移读取」——成立。
//    growColumns/shrinkColumns 把全部历史+页面内容重绕进新环形缓冲（内容保留、
//    物理行按新宽度重新切分），_linesUsed 与 historyLineCount() 相应更新，
//    lineAt(LineOffset 负偏移) 照常工作；nothingToReflowOrCut 快路径仅原地
//    resize 各行，内容不动。
//
// ③ noteFloor 在 Alternate 期间读的是 alt Grid 的 floor（主屏/备屏各自独立
//    Grid，stableFloor 跟随当前激活 Grid）。进 Alternate 时 alt floor 低于
//    lastFloor_ 则按 noteFloor 的回退分支静默对齐（不计负丢弃）；Alternate
//    期间 alt Grid 的真实裁剪会被计入 droppedAccum_。今天该行为不可观察：
//    Alternate 切换即清选区（Terminal 侧 activeBufferChanged 分支），且
//    lastFloor_ 每轮自校正、回主屏后不回补，主屏裁剪不漏记。钉住该语义；
//    未来若开放 Alternate 下选区，需重审此计数路径。
//
// ④ reanchorFloor 会一并吞掉列变 reflow 中的真实容量裁剪：历史已满时缩列，
//    reflow 重建伴随的真实丢弃（超出容量的行被裁）与行身份重建副产混在一起，
//    被整体对齐而不累计，选区锚点因此漏平移。属边角案例（列变且历史恰好满
//    容量），终审裁定 v1 接受，钉住备查。
#include "ZzContourLineSource.h"

#include "ZzContourBackend.h"

ZzContourLineSource::ZzContourLineSource(const ZzContourBackend& backend) noexcept
    : backend_(backend)
{
    lastFloor_ = backend_.stableFloor();
}

std::size_t ZzContourLineSource::historyLineCount() const
{
    if (backend_.isAlternateScreen())
        return 0; // Alternate 无历史（规格 5.1）
    return static_cast<std::size_t>(backend_.historyLineCount());
}

int ZzContourLineSource::screenRowCount() const
{
    return backend_.size().second;
}

int ZzContourLineSource::cols() const
{
    return backend_.size().first;
}

ZzLine ZzContourLineSource::lineAt(std::size_t unifiedRow) const
{
    const auto history = historyLineCount();
    if (unifiedRow < history)
        return backend_.historyLineSnapshot(static_cast<int>(unifiedRow));
    return backend_.screenLineSnapshot(static_cast<int>(unifiedRow - history));
}

bool ZzContourLineSource::lineWrapped(std::size_t unifiedRow) const
{
    const auto history = historyLineCount();
    if (unifiedRow < history)
        return backend_.historyLineWrapped(static_cast<int>(unifiedRow));
    return backend_.lineWrapped(static_cast<int>(unifiedRow - history));
}

std::uint64_t ZzContourLineSource::droppedLineCount() const
{
    return droppedAccum_;
}

void ZzContourLineSource::noteFloor() noexcept
{
    const std::int64_t floor = backend_.stableFloor();
    if (floor > lastFloor_)
        droppedAccum_ += static_cast<std::uint64_t>(floor - lastFloor_);
    lastFloor_ = floor; // 回退（zero-history 分支）直接对齐，不计负丢弃
}

void ZzContourLineSource::reanchorFloor() noexcept
{
    // 列变化 resize（reflow）后调用：floor 前移是行身份重建的副产而非真实
    // 丢弃（文件头结论①），直接对齐，不累计。
    lastFloor_ = backend_.stableFloor();
}

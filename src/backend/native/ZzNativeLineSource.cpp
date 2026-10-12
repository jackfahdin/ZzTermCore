#include "ZzNativeLineSource.h"

#include <ZzTerm/Screen.h>
#include <ZzTerm/Scrollback.h>

ZzNativeLineSource::ZzNativeLineSource(const ZzScreen& screen, const ZzScrollback& scrollback) noexcept
    : screen_(screen), scrollback_(scrollback)
{
}

std::size_t ZzNativeLineSource::historyLineCount() const
{
    if (screen_.activeBuffer() == ZzScreenBuffer::Alternate)
        return 0; // Alternate 无历史（规格 5.1）
    return scrollback_.lineCount();
}

int ZzNativeLineSource::screenRowCount() const
{
    return screen_.size().rows;
}

int ZzNativeLineSource::cols() const
{
    return screen_.size().cols;
}

void ZzNativeLineSource::lineAt(std::size_t unifiedRow, ZzLine& out) const
{
    const std::size_t history = historyLineCount();
    if (unifiedRow < history)
        out = scrollback_.lineAt(unifiedRow); // copy-assign：复用 out 容量
    else
        out = screen_.lineAt(static_cast<int>(unifiedRow - history));
}

bool ZzNativeLineSource::lineWrapped(std::size_t unifiedRow) const
{
    const std::size_t history = historyLineCount();
    if (unifiedRow < history)
        return scrollback_.lineAt(unifiedRow).wrapped();
    return screen_.lineAt(static_cast<int>(unifiedRow - history)).wrapped();
}

std::uint64_t ZzNativeLineSource::droppedLineCount() const
{
    // 绝对行号基线（含 M19 注入计数）：基线平移使注入前后既有行号稳定。
    return scrollback_.stats().totalDropped + scrollback_.stats().totalPrepended;
}

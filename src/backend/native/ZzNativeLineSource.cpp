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

ZzLine ZzNativeLineSource::lineAt(std::size_t unifiedRow) const
{
    const std::size_t history = historyLineCount();
    if (unifiedRow < history)
        return scrollback_.lineAt(unifiedRow);
    return screen_.lineAt(static_cast<int>(unifiedRow - history));
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
    return scrollback_.stats().totalDropped;
}

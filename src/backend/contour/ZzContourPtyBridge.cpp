#include "ZzContourPtyBridge.h"

ZzContourPtyBridge::ZzContourPtyBridge(vtpty::PageSize initialPageSize, WriteCallback onWrite)
    : pageSize_(initialPageSize)
    , onWrite_(std::move(onWrite))
{
}

vtpty::StartResult ZzContourPtyBridge::start()
{
    return vtpty::StartOutcome {};
}

vtpty::PtySlave& ZzContourPtyBridge::slave() noexcept
{
    return slave_;
}

void ZzContourPtyBridge::close()
{
    closed_ = true;
}

void ZzContourPtyBridge::waitForClosed()
{
}

bool ZzContourPtyBridge::isClosed() const noexcept
{
    return closed_;
}

std::optional<vtpty::Pty::ReadResult> ZzContourPtyBridge::read(
    crispy::BufferObject<char>& /*storage*/,
    std::optional<std::chrono::milliseconds> /*timeout*/,
    size_t /*size*/)
{
    return std::nullopt;
}

void ZzContourPtyBridge::wakeupReader()
{
}

int ZzContourPtyBridge::write(std::string_view buf)
{
    if (onWrite_)
        onWrite_(buf);
    return static_cast<int>(buf.size());
}

vtpty::PageSize ZzContourPtyBridge::pageSize() const noexcept
{
    return pageSize_;
}

void ZzContourPtyBridge::resizeScreen(vtpty::PageSize cells,
                                      std::optional<vtpty::ImageSize> /*pixels*/)
{
    pageSize_ = cells;
}

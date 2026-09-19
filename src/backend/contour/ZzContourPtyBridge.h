#pragma once

#include <vtpty/Pty.hpp>

#include <functional>
#include <optional>
#include <string_view>

/// \brief headless 场景下的 vtpty::Pty 实现：把终端回传字节（DA 响应等）转发到注入回调。
/// read 恒无数据；slave 使用 PtySlaveDummy；pageSize 内部记录。
class ZzContourPtyBridge : public vtpty::Pty
{
public:
    using WriteCallback = std::function<void(std::string_view)>;

    ZzContourPtyBridge(vtpty::PageSize initialPageSize, WriteCallback onWrite);

    vtpty::StartResult start() override;
    vtpty::PtySlave& slave() noexcept override;
    void close() override;
    void waitForClosed() override;
    [[nodiscard]] bool isClosed() const noexcept override;
    std::optional<ReadResult> read(crispy::BufferObject<char>& storage,
                                   std::optional<std::chrono::milliseconds> timeout,
                                   size_t size) override;
    void wakeupReader() override;
    int write(std::string_view buf) override;
    [[nodiscard]] vtpty::PageSize pageSize() const noexcept override;
    void resizeScreen(vtpty::PageSize cells, std::optional<vtpty::ImageSize> pixels) override;

private:
    vtpty::PtySlaveDummy slave_;
    vtpty::PageSize pageSize_;
    WriteCallback onWrite_;
    bool closed_ = false;
};

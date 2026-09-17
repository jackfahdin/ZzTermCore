#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "ZzPtyExport.h"

#include <sys/types.h> // pid_t

/**
 * @file ZzPty.h
 * @brief ZzPty：Unix PTY 薄 RAII 封装（openpty/fork/exec/winsize/waitpid）。
 *
 * 职责（Architecture.md 第 2/3 节）：
 * - 纯 OS 封装，不依赖 ZzTermCore；事件驱动由调用方决定（demo 用 poll，
 *   未来 Qt Widget 把 masterFd() 接 QSocketNotifier，本 API 不变）；
 * - 无线程、无回调；所有方法必须在同一线程调用（非线程安全）。
 *
 * ownership：ZzPty 独占拥有 master fd 与子进程 pid；析构时 SIGHUP 子进程、
 * 关闭 fd 并回收僵尸。
 */

/**
 * @brief PTY 启动配置。
 */
struct ZzPtyConfig {
    std::vector<std::string> argv; ///< 程序与参数，argv[0] 为程序路径或名称（execvp 搜索 PATH）。
    int  cols    = 80;             ///< 初始列数（TIOCSWINSZ）。
    int  rows    = 24;             ///< 初始行数（TIOCSWINSZ）。
    bool rawMode = true;           ///< slave 置 raw（cfmakeraw），避免 PTY echo/规范模式干扰 Core 验证。
};

/**
 * @brief Unix PTY 会话（master 侧）。
 */
class ZZTERM_PTY_API ZzPty {
public:
    /**
     * @brief 启动子进程并挂到新的 PTY。
     * @param cfg 启动配置（argv 非空、cols/rows > 0，否则失败 errno = EINVAL）。
     * @return 成功返回 PTY 会话；失败返回 nullptr（含子进程 exec 失败），errno 保留供诊断。
     */
    static std::unique_ptr<ZzPty> spawn(const ZzPtyConfig& cfg);

    /// @brief 析构：SIGHUP 子进程（不退出则短暂等待后 SIGKILL）、关闭 master fd、回收僵尸。
    ~ZzPty();

    ZzPty(const ZzPty&)            = delete;
    ZzPty& operator=(const ZzPty&) = delete;

    /**
     * @brief master 侧文件描述符（供 poll/select/QSocketNotifier 使用）。
     * @return master fd（本对象存续期间有效）。
     */
    [[nodiscard]] int masterFd() const noexcept;

    /**
     * @brief 从 master 读取子进程输出。
     * @param buf 读取缓冲。
     * @return > 0 读取字节数；0 = EOF（子进程退出后 Linux 返回 EIO，归一为 EOF）；
     *         -1 = 错误，errno 保留（fd 非阻塞时可能为 EAGAIN，表示暂不可读）。
     */
    std::ptrdiff_t read(std::span<std::byte> buf) noexcept;

    /**
     * @brief 循环写入直到全部写完或出错（处理部分写入与 EINTR）。
     * @param data 待写入数据。
     * @return true 全部写完；false 出错（errno 保留）。
     */
    bool writeAll(std::span<const std::byte> data) noexcept;

    /**
     * @brief 调整 PTY 窗口尺寸（TIOCSWINSZ，触发子进程 SIGWINCH）。
     * @param cols 新列数（> 0）。
     * @param rows 新行数（> 0）。
     * @return true 成功。
     */
    bool resize(int cols, int rows) noexcept;

    /**
     * @brief 非阻塞收集子进程退出码（WNOHANG waitpid）。
     * @return 子进程仍在运行返回 std::nullopt；已退出返回退出码
     *         （被信号杀死为 128 + 信号号）。已回收后重复调用返回同一退出码。
     */
    std::optional<int> tryWait() noexcept;

private:
    ZzPty(int masterFd, pid_t childPid) noexcept;

    int   masterFd_ = -1;   ///< master fd。
    pid_t childPid_ = -1;   ///< 子进程 pid。
    bool  reaped_   = false; ///< 是否已 waitpid 回收。
    int   exitCode_ = 0;    ///< 已回收时的退出码（供重复 tryWait 返回）。
};

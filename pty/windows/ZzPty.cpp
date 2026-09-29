// ZzPty Windows 实现：ConPTY（CreatePseudoConsole + 匿名管道 + CreateProcessW）。
// 最低系统要求 Windows 10 1809（规格 4A：旧系统 spawn 返回 nullptr，GetLastError 留诊断）。
#include "ZzPty.h"

#if defined(_WIN32)

#include <string>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <consoleapi.h> // CreatePseudoConsole / ResizePseudoConsole / ClosePseudoConsole

namespace {

// UTF-8 -> UTF-16（MultiByteToWideChar 两段式：先查长度再转换）。
std::wstring toWide(const std::string& s)
{
    if (s.empty()) return {};
    const int n = ::MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()),
                                        nullptr, 0);
    if (n <= 0) return {};
    std::wstring w(static_cast<std::size_t>(n), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

// 单参数按 CommandLineToArgvW 规则引用：含空白/引号或为空时加引号，
// 引号前反斜杠翻倍加一、收尾引号前反斜杠翻倍，其余原样。
std::wstring quoteArg(const std::wstring& arg)
{
    if (!arg.empty() && arg.find_first_of(L" \t\"") == std::wstring::npos) {
        return arg;
    }
    std::wstring out = L"\"";
    std::size_t backslashes = 0;
    for (const wchar_t c : arg) {
        if (c == L'\\') {
            ++backslashes;
            continue;
        }
        if (c == L'"') {
            out.append(backslashes * 2 + 1, L'\\');
        } else {
            out.append(backslashes, L'\\');
        }
        backslashes = 0;
        out += c;
    }
    out.append(backslashes * 2, L'\\');
    out += L'"';
    return out;
}

// 保存/恢复 GetLastError，保证清理路径不覆盖 spawn 失败的诊断码。
class LastErrorGuard {
public:
    explicit LastErrorGuard(DWORD code) : code_(code) {}
    ~LastErrorGuard() { ::SetLastError(code_); }
    LastErrorGuard(const LastErrorGuard&) = delete;
    LastErrorGuard& operator=(const LastErrorGuard&) = delete;

private:
    DWORD code_;
};

void closeHandleNoNull(HANDLE h) noexcept
{
    if (h != nullptr && h != INVALID_HANDLE_VALUE) {
        ::CloseHandle(h);
    }
}

} // namespace

ZzPty::ZzPty(void* readPipe, void* writePipe, void* hpcon, void* processHandle,
             void* threadHandle) noexcept
    : readPipe_(readPipe), writePipe_(writePipe), hpcon_(hpcon),
      processHandle_(processHandle), threadHandle_(threadHandle)
{
}

std::unique_ptr<ZzPty> ZzPty::spawn(const ZzPtyConfig& cfg)
{
    if (cfg.argv.empty() || cfg.argv.front().empty() || cfg.cols <= 0 || cfg.rows <= 0) {
        ::SetLastError(ERROR_INVALID_PARAMETER);
        return nullptr;
    }

    // 管道安全属性可继承；CreateProcessW 以 bInheritHandles = FALSE 调用
    // （ConPTY 自行 dup 管道端，子进程不经继承获得句柄）。
    SECURITY_ATTRIBUTES sa {};
    sa.nLength        = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE inRead   = nullptr; // 子进程 stdin 管道读取端（交给 ConPTY）
    HANDLE inWrite  = nullptr; // 子进程 stdin 管道写入端（父进程保留）
    HANDLE outRead  = nullptr; // 子进程 stdout 管道读取端（父进程保留）
    HANDLE outWrite = nullptr; // 子进程 stdout 管道写入端（交给 ConPTY）
    if (!::CreatePipe(&inRead, &inWrite, &sa, 0)) {
        return nullptr; // GetLastError 由 CreatePipe 保留
    }
    if (!::CreatePipe(&outRead, &outWrite, &sa, 0)) {
        const LastErrorGuard guard(::GetLastError());
        closeHandleNoNull(inRead);
        closeHandleNoNull(inWrite);
        return nullptr;
    }

    const COORD size {static_cast<SHORT>(cfg.cols), static_cast<SHORT>(cfg.rows)};
    HPCON hpcon = nullptr;
    const HRESULT hr = ::CreatePseudoConsole(size, inRead, outWrite, 0, &hpcon);
    // ConPTY 成功时已 dup 管道两端，父侧副本无论成败均可立即关闭。
    closeHandleNoNull(inRead);
    closeHandleNoNull(outWrite);
    if (FAILED(hr)) {
        const LastErrorGuard guard(HRESULT_CODE(hr) != 0 ? HRESULT_CODE(hr)
                                                         : ERROR_INVALID_FUNCTION);
        closeHandleNoNull(inWrite);
        closeHandleNoNull(outRead);
        return nullptr;
    }

    // 属性列表两段式：先查尺寸再分配。
    SIZE_T attrSize = 0;
    ::InitializeProcThreadAttributeList(nullptr, 1, 0, &attrSize); // 预期失败，仅取尺寸
    std::vector<BYTE> attrBuf(attrSize);
    auto* attrList = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attrBuf.data());
    if (!::InitializeProcThreadAttributeList(attrList, 1, 0, &attrSize)) {
        const LastErrorGuard guard(::GetLastError());
        ::ClosePseudoConsole(hpcon);
        closeHandleNoNull(inWrite);
        closeHandleNoNull(outRead);
        return nullptr;
    }
    if (!::UpdateProcThreadAttribute(attrList, 0, PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE,
                                     hpcon, sizeof(hpcon), nullptr, nullptr)) {
        const LastErrorGuard guard(::GetLastError());
        ::DeleteProcThreadAttributeList(attrList);
        ::ClosePseudoConsole(hpcon);
        closeHandleNoNull(inWrite);
        closeHandleNoNull(outRead);
        return nullptr;
    }

    // argv 拼接为命令行（引号转义规则同 CommandLineToArgvW），UTF-8 -> UTF-16。
    std::wstring cmdline;
    for (std::size_t i = 0; i < cfg.argv.size(); ++i) {
        if (i > 0) cmdline += L' ';
        cmdline += quoteArg(toWide(cfg.argv[i]));
    }

    STARTUPINFOEXW si {};
    si.StartupInfo.cb  = sizeof(si);
    // STARTF_USESTDHANDLES + 空 std 句柄：阻止内核把父进程的标准句柄复制给
    // 子进程。父进程 std 被重定向时（如 ctest 管道），缺省行为会让子进程绕过
    // 伪控制台直接读写父进程管道——表现为子进程横幅泄漏到父进程 stdout、
    // 读控制台输入的子进程启动即得 EOF 退出（microsoft/terminal 讨论 15814，
    // Vim PR 19589 同款修复；M12 CI 迭代 R1-R6 六轮实证定位）。
    si.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    si.lpAttributeList = attrList;
    PROCESS_INFORMATION pi {};
    const BOOL created = ::CreateProcessW(nullptr, cmdline.data(), nullptr, nullptr, FALSE,
                                          EXTENDED_STARTUPINFO_PRESENT, nullptr, nullptr,
                                          &si.StartupInfo, &pi);
    const DWORD createErr = ::GetLastError();
    ::DeleteProcThreadAttributeList(attrList);
    if (!created) {
        const LastErrorGuard guard(createErr);
        ::ClosePseudoConsole(hpcon);
        closeHandleNoNull(inWrite);
        closeHandleNoNull(outRead);
        return nullptr;
    }

    return std::unique_ptr<ZzPty>(new ZzPty(outRead, inWrite, hpcon, pi.hProcess, pi.hThread));
}

ZzPty::~ZzPty()
{
    // ClosePseudoConsole 通知伪控制台会话结束（语义对齐 Unix SIGHUP），并释放
    // ConPTY 持有的管道端；随后短暂宽限，子进程仍存活则 TerminateProcess 兜底。
    if (hpcon_ != nullptr) {
        ::ClosePseudoConsole(static_cast<HPCON>(hpcon_));
        hpcon_ = nullptr;
    }
    if (processHandle_ != nullptr) {
        HANDLE hProcess = static_cast<HANDLE>(processHandle_);
        if (::WaitForSingleObject(hProcess, 200) == WAIT_TIMEOUT) {
            ::TerminateProcess(hProcess, 1);
            ::WaitForSingleObject(hProcess, 1000);
        }
        // 收尾退出码：不遗留僵尸等价物，重复 tryWait 语义与 Unix 版一致。
        DWORD code = 0;
        if (::GetExitCodeProcess(hProcess, &code) && code != STILL_ACTIVE) {
            exitCode_ = static_cast<int>(code);
            reaped_   = true;
        }
        closeHandleNoNull(hProcess);
        processHandle_ = nullptr;
    }
    closeHandleNoNull(static_cast<HANDLE>(threadHandle_));
    closeHandleNoNull(static_cast<HANDLE>(readPipe_));
    closeHandleNoNull(static_cast<HANDLE>(writePipe_));
    threadHandle_ = nullptr;
    readPipe_     = nullptr;
    writePipe_    = nullptr;
}

void* ZzPty::readHandle() const noexcept
{
    return readPipe_;
}

void* ZzPty::writeHandle() const noexcept
{
    return writePipe_;
}

std::ptrdiff_t ZzPty::read(std::span<std::byte> buf) noexcept
{
    HANDLE hRead = static_cast<HANDLE>(readPipe_);
    DWORD avail  = 0;
    if (!::PeekNamedPipe(hRead, nullptr, 0, nullptr, &avail, nullptr)) {
        // 管道断裂（子进程退出、写端全关）归一为 EOF；其余为错误，GetLastError 保留。
        return ::GetLastError() == ERROR_BROKEN_PIPE ? 0 : -1;
    }
    if (avail == 0) {
        // 暂无可读：子进程已退出且排空归一 EOF，否则等价 Unix EAGAIN 返回 -1。
        HANDLE hProcess = static_cast<HANDLE>(processHandle_);
        if (hProcess != nullptr && ::WaitForSingleObject(hProcess, 0) == WAIT_OBJECT_0) {
            return 0;
        }
        ::SetLastError(ERROR_NO_DATA);
        return -1;
    }
    DWORD n = 0;
    if (!::ReadFile(hRead, buf.data(), static_cast<DWORD>(buf.size()), &n, nullptr)) {
        return ::GetLastError() == ERROR_BROKEN_PIPE ? 0 : -1;
    }
    return static_cast<std::ptrdiff_t>(n);
}

bool ZzPty::writeAll(std::span<const std::byte> data) noexcept
{
    HANDLE hWrite  = static_cast<HANDLE>(writePipe_);
    std::size_t off = 0;
    while (off < data.size()) {
        DWORD n = 0;
        if (!::WriteFile(hWrite, data.data() + off,
                         static_cast<DWORD>(data.size() - off), &n, nullptr)) {
            return false; // GetLastError 保留（ERROR_BROKEN_PIPE 等）
        }
        off += static_cast<std::size_t>(n);
    }
    return true;
}

bool ZzPty::resize(int cols, int rows) noexcept
{
    if (cols <= 0 || rows <= 0) return false;
    const COORD size {static_cast<SHORT>(cols), static_cast<SHORT>(rows)};
    return SUCCEEDED(::ResizePseudoConsole(static_cast<HPCON>(hpcon_), size));
}

std::optional<int> ZzPty::tryWait() noexcept
{
    if (reaped_) return exitCode_;
    DWORD code = 0;
    if (!::GetExitCodeProcess(static_cast<HANDLE>(processHandle_), &code)) {
        return std::nullopt;
    }
    if (code == STILL_ACTIVE) return std::nullopt; // 仍在运行
    reaped_   = true;
    exitCode_ = static_cast<int>(code);
    return exitCode_;
}

#endif // defined(_WIN32)

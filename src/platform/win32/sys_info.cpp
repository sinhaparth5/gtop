// SystemInfo — Win32 (GetComputerNameExW / RtlGetVersion).

#include "platform/sys_info.hpp"

#include <windows.h>

#include <array>
#include <string>
#include <string_view>

#include "platform/win32/wide_string.hpp"

namespace gtop::platform {

namespace {

// GetVersionEx lies: since Windows 8.1 it reports 6.2 unless the binary
// carries a compatibility manifest naming every later release. RtlGetVersion
// is the kernel's own answer and is not manifest-gated, so it is what actually
// reports Windows 11. It lives in ntdll and is not in any import library,
// which is fine — this project resolves things at runtime anyway.
using RtlGetVersionFn = LONG(WINAPI*)(PRTL_OSVERSIONINFOW);

std::string query_os_version() {
    HMODULE ntdll = ::GetModuleHandleW(L"ntdll.dll");
    if (ntdll == nullptr) {
        return {};
    }

    auto rtl_get_version =
        reinterpret_cast<RtlGetVersionFn>(
            reinterpret_cast<void (*)()>(::GetProcAddress(ntdll, "RtlGetVersion")));
    if (rtl_get_version == nullptr) {
        return {};
    }

    RTL_OSVERSIONINFOW version{};
    version.dwOSVersionInfoSize = sizeof(version);
    if (rtl_get_version(&version) != 0) {
        return {};
    }

    return std::to_string(version.dwMajorVersion) + '.' +
           std::to_string(version.dwMinorVersion) + '.' +
           std::to_string(version.dwBuildNumber);
}

}  // namespace

SystemInfo query_system_info() {
    SystemInfo info;

    std::array<wchar_t, MAX_COMPUTERNAME_LENGTH + 1> host{};
    DWORD length = static_cast<DWORD>(host.size());
    if (::GetComputerNameExW(ComputerNameNetBIOS, host.data(), &length) != 0) {
        info.hostname = win32::to_utf8(std::wstring_view(host.data(), length));
    }

    info.os_name = "Windows";
    info.os_version = query_os_version();
    return info;
}

}  // namespace gtop::platform

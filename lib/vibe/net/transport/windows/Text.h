#pragma once

#include <windows.h>

#include <string>
#include <string_view>

namespace vibe::net::windows_text {

inline std::wstring wide(std::string_view text) {
    if (text.empty()) {
        return {};
    }
    const int length = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (length <= 0) {
        return {};
    }
    std::wstring out(static_cast<std::size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), length);
    return out;
}

inline std::string utf8(std::wstring_view text) {
    if (text.empty()) {
        return {};
    }
    const int length =
        WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (length <= 0) {
        return {};
    }
    std::string out(static_cast<std::size_t>(length), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), length, nullptr, nullptr);
    return out;
}

inline std::string errorText(DWORD error) {
    wchar_t* buffer = nullptr;
    HMODULE winhttp = GetModuleHandleW(L"winhttp.dll");
    const DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS |
                        (winhttp ? FORMAT_MESSAGE_FROM_HMODULE : 0);
    const DWORD length = FormatMessageW(flags, winhttp, error, 0, reinterpret_cast<LPWSTR>(&buffer), 0, nullptr);
    std::string text = length > 0 && buffer ? utf8(std::wstring_view(buffer, length)) : std::string();
    if (buffer) {
        LocalFree(buffer);
    }
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == ' ' || text.back() == '.')) {
        text.pop_back();
    }
    return "error " + std::to_string(error) + (text.empty() ? std::string() : ": " + text);
}

} // namespace vibe::net::windows_text

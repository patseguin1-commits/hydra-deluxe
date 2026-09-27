#define _CRT_SECURE_NO_WARNINGS  // _wfopen

#include "core/winstr.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>  // CommandLineToArgvW

#include <stdexcept>

namespace hydra {

std::wstring utf8_to_wide(const std::string& s) {
    if (s.empty()) return L"";
    int wlen = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()),
                                   nullptr, 0);
    std::wstring w(static_cast<size_t>(wlen), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), &w[0], wlen);
    return w;
}

std::string wide_to_utf8(const std::wstring& w) {
    if (w.empty()) return "";
    int len = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()),
                                  nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<size_t>(len), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), &s[0], len,
                        nullptr, nullptr);
    return s;
}

std::FILE* fopen_utf8(const std::string& utf8_path, const wchar_t* mode) {
    return _wfopen(utf8_to_wide(utf8_path).c_str(), mode);
}

bool file_exists_utf8(const std::string& utf8_path) {
    return GetFileAttributesW(utf8_to_wide(utf8_path).c_str()) !=
           INVALID_FILE_ATTRIBUTES;
}

std::vector<uint8_t> read_file_bytes(const std::string& utf8_path) {
    std::FILE* f = fopen_utf8(utf8_path, L"rb");
    if (f == nullptr) throw std::runtime_error("cannot open file: " + utf8_path);
    std::fseek(f, 0, SEEK_END);
    long size = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    std::vector<uint8_t> buf(size > 0 ? static_cast<size_t>(size) : 0);
    if (size > 0) buf.resize(std::fread(buf.data(), 1, buf.size(), f));
    std::fclose(f);
    return buf;
}

std::string read_file_text(const std::string& utf8_path) {
    std::vector<uint8_t> bytes = read_file_bytes(utf8_path);
    return std::string(bytes.begin(), bytes.end());
}

std::vector<std::string> split_command_line_utf8(const std::wstring& command_line) {
    // An empty string makes CommandLineToArgvW return the exe path instead.
    if (command_line.empty()) return {};
    int argc = 0;
    LPWSTR* wargv = CommandLineToArgvW(command_line.c_str(), &argc);
    if (wargv == nullptr) throw std::runtime_error("cannot split the command line");
    std::vector<std::string> args;
    args.reserve(static_cast<size_t>(argc));
    for (int i = 0; i < argc; ++i) args.push_back(wide_to_utf8(wargv[i]));
    LocalFree(wargv);
    return args;
}

std::vector<std::string> utf8_argv() {
    return split_command_line_utf8(GetCommandLineW());
}

}  // namespace hydra

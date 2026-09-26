// UTF-8 <-> UTF-16 conversion and UTF-8-path file helpers for Win32.
//
// Chart libraries contain non-ASCII paths (e.g. a fullwidth slash). The
// narrow CRT / std::ifstream path APIs go through the ANSI codepage on
// Windows and mangle them, so every file open routes through the wide API
// via these helpers.

#ifndef HYDRA_CORE_WINSTR_H
#define HYDRA_CORE_WINSTR_H

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace hydra {

std::wstring utf8_to_wide(const std::string& s);
std::string wide_to_utf8(const std::wstring& w);

// _wfopen with a UTF-8 path; nullptr on failure, like fopen.
std::FILE* fopen_utf8(const std::string& utf8_path, const wchar_t* mode);

bool file_exists_utf8(const std::string& utf8_path);

// The whole file's bytes; throws std::runtime_error when the open fails.
std::vector<uint8_t> read_file_bytes(const std::string& utf8_path);

// The whole file as text, bytes as they are (no newline translation); throws
// std::runtime_error when the open fails.
std::string read_file_text(const std::string& utf8_path);

}  // namespace hydra

#endif  // HYDRA_CORE_WINSTR_H

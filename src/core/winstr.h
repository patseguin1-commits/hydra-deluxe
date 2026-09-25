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

// A command line split into arguments (CommandLineToArgvW's rules), each one
// UTF-8. Separate from utf8_argv() so tests can feed it any command line.
std::vector<std::string> split_command_line_utf8(const std::wstring& command_line);

// This process's arguments as UTF-8, argv[0] included. main()'s char** argv
// is the ANSI copy of the command line: Windows swaps each character the code
// page lacks for a look-alike (a fullwidth slash becomes '/'), so a path to a
// chart folder with such a name points somewhere else. Every entry point
// reads its arguments from here instead.
std::vector<std::string> utf8_argv();

}  // namespace hydra

#endif  // HYDRA_CORE_WINSTR_H

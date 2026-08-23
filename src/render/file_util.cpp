#include "render/file_util.h"

#include <fstream>

namespace hydra::render {

std::vector<uint8_t> read_file_bytes(const std::string& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) return {};
    std::streamoff size = f.tellg();
    if (size <= 0) return {};
    std::vector<uint8_t> bytes(static_cast<size_t>(size));
    f.seekg(0);
    f.read(reinterpret_cast<char*>(bytes.data()), size);
    if (!f) return {};
    return bytes;
}

std::string read_file_text(const std::string& path) {
    std::vector<uint8_t> bytes = read_file_bytes(path);
    return std::string(bytes.begin(), bytes.end());
}

}  // namespace hydra::render

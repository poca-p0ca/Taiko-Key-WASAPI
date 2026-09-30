#pragma once
#include "core/Common.h"
#include <map>
#include <vector>
namespace taiko {
struct SoundSpec {
    uint32_t id;
    std::filesystem::path path;
    size_t line;
};
struct KeyConfig {
    std::filesystem::path file;
    std::array<uint32_t, 256> keys{};
    std::vector<SoundSpec> sounds;
};
const std::map<std::string, uint16_t>& keyNames();
KeyConfig parseConfig(const std::string& text, const std::filesystem::path& file);
KeyConfig readConfig(const std::filesystem::path& file);
std::string bindingsText(const KeyConfig& config);
} // namespace taiko

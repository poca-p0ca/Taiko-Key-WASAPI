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
// Canonical INI name for a virtual-key code (first alias in name order), or empty if unsupported.
std::string keyName(uint16_t vk);
KeyConfig parseConfig(const std::string& text, const std::filesystem::path& file);
KeyConfig readConfig(const std::filesystem::path& file);
// Serializes a config as KeyBind.ini text; sound paths are written relative to config.file when possible.
std::string configText(const KeyConfig& config);
std::string bindingsText(const KeyConfig& config);
} // namespace taiko

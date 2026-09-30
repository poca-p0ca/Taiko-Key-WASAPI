#include "config/KeyConfig.h"
#include <charconv>
#include <fstream>
#include <set>
#include <sstream>

namespace taiko {
const std::map<std::string, uint16_t>& keyNames() {
    static const auto names = [] {
        std::map<std::string, uint16_t> n;
        for (int k = 'A'; k <= 'Z'; ++k)
            n[std::string(1, static_cast<char>(k))] = static_cast<uint16_t>(k);
        for (int k = '0'; k <= '9'; ++k)
            n[std::string(1, static_cast<char>(k))] = static_cast<uint16_t>(k);
        for (int k = 0; k < 10; ++k)
            n["VK_NUMPAD" + std::to_string(k)] = static_cast<uint16_t>(VK_NUMPAD0 + k);
        for (int k = 1; k <= 24; ++k)
            n["VK_F" + std::to_string(k)] = static_cast<uint16_t>(VK_F1 + k - 1);
#define KEY(name) n[#name] = name
        KEY(VK_LBUTTON);
        KEY(VK_RBUTTON);
        KEY(VK_CANCEL);
        KEY(VK_MBUTTON);
        KEY(VK_XBUTTON1);
        KEY(VK_XBUTTON2);
        KEY(VK_HANGEUL);
        KEY(VK_BACK);
        KEY(VK_TAB);
        KEY(VK_CLEAR);
        KEY(VK_RETURN);
        KEY(VK_SHIFT);
        KEY(VK_CONTROL);
        KEY(VK_MENU);
        KEY(VK_PAUSE);
        KEY(VK_CAPITAL);
        KEY(VK_KANA);
        KEY(VK_HANGUL);
        KEY(VK_IME_ON);
        KEY(VK_JUNJA);
        KEY(VK_FINAL);
        KEY(VK_HANJA);
        KEY(VK_KANJI);
        KEY(VK_IME_OFF);
        KEY(VK_ESCAPE);
        KEY(VK_CONVERT);
        KEY(VK_NONCONVERT);
        KEY(VK_ACCEPT);
        KEY(VK_MODECHANGE);
        KEY(VK_SPACE);
        KEY(VK_PRIOR);
        KEY(VK_NEXT);
        KEY(VK_END);
        KEY(VK_HOME);
        KEY(VK_LEFT);
        KEY(VK_UP);
        KEY(VK_RIGHT);
        KEY(VK_DOWN);
        KEY(VK_SELECT);
        KEY(VK_PRINT);
        KEY(VK_EXECUTE);
        KEY(VK_SNAPSHOT);
        KEY(VK_INSERT);
        KEY(VK_DELETE);
        KEY(VK_HELP);
        KEY(VK_LWIN);
        KEY(VK_RWIN);
        KEY(VK_APPS);
        KEY(VK_SLEEP);
        KEY(VK_MULTIPLY);
        KEY(VK_ADD);
        KEY(VK_SEPARATOR);
        KEY(VK_SUBTRACT);
        KEY(VK_DECIMAL);
        KEY(VK_DIVIDE);
        KEY(VK_NUMLOCK);
        KEY(VK_SCROLL);
        KEY(VK_LSHIFT);
        KEY(VK_RSHIFT);
        KEY(VK_LCONTROL);
        KEY(VK_RCONTROL);
        KEY(VK_LMENU);
        KEY(VK_RMENU);
        KEY(VK_BROWSER_BACK);
        KEY(VK_BROWSER_FORWARD);
        KEY(VK_BROWSER_REFRESH);
        KEY(VK_BROWSER_STOP);
        KEY(VK_BROWSER_SEARCH);
        KEY(VK_BROWSER_FAVORITES);
        KEY(VK_BROWSER_HOME);
        KEY(VK_VOLUME_MUTE);
        KEY(VK_VOLUME_DOWN);
        KEY(VK_VOLUME_UP);
        KEY(VK_MEDIA_NEXT_TRACK);
        KEY(VK_MEDIA_PREV_TRACK);
        KEY(VK_MEDIA_STOP);
        KEY(VK_MEDIA_PLAY_PAUSE);
        KEY(VK_LAUNCH_MAIL);
        KEY(VK_LAUNCH_MEDIA_SELECT);
        KEY(VK_LAUNCH_APP1);
        KEY(VK_LAUNCH_APP2);
        KEY(VK_OEM_1);
        KEY(VK_OEM_PLUS);
        KEY(VK_OEM_COMMA);
        KEY(VK_OEM_MINUS);
        KEY(VK_OEM_PERIOD);
        KEY(VK_OEM_2);
        KEY(VK_OEM_3);
        KEY(VK_OEM_4);
        KEY(VK_OEM_5);
        KEY(VK_OEM_6);
        KEY(VK_OEM_7);
        KEY(VK_OEM_8);
        KEY(VK_OEM_102);
        KEY(VK_PROCESSKEY);
        KEY(VK_PACKET);
        KEY(VK_ATTN);
        KEY(VK_CRSEL);
        KEY(VK_EXSEL);
        KEY(VK_EREOF);
        KEY(VK_PLAY);
        KEY(VK_ZOOM);
        KEY(VK_NONAME);
        KEY(VK_PA1);
        KEY(VK_OEM_CLEAR);
        KEY(VK_NAVIGATION_VIEW);
        KEY(VK_NAVIGATION_MENU);
        KEY(VK_NAVIGATION_UP);
        KEY(VK_NAVIGATION_DOWN);
        KEY(VK_NAVIGATION_LEFT);
        KEY(VK_NAVIGATION_RIGHT);
        KEY(VK_NAVIGATION_ACCEPT);
        KEY(VK_NAVIGATION_CANCEL);
        KEY(VK_OEM_NEC_EQUAL);
        KEY(VK_OEM_FJ_JISHO);
        KEY(VK_OEM_FJ_MASSHOU);
        KEY(VK_OEM_FJ_TOUROKU);
        KEY(VK_OEM_FJ_LOYA);
        KEY(VK_OEM_FJ_ROYA);
        KEY(VK_GAMEPAD_A);
        KEY(VK_GAMEPAD_B);
        KEY(VK_GAMEPAD_X);
        KEY(VK_GAMEPAD_Y);
        KEY(VK_GAMEPAD_RIGHT_SHOULDER);
        KEY(VK_GAMEPAD_LEFT_SHOULDER);
        KEY(VK_GAMEPAD_LEFT_TRIGGER);
        KEY(VK_GAMEPAD_RIGHT_TRIGGER);
        KEY(VK_GAMEPAD_DPAD_UP);
        KEY(VK_GAMEPAD_DPAD_DOWN);
        KEY(VK_GAMEPAD_DPAD_LEFT);
        KEY(VK_GAMEPAD_DPAD_RIGHT);
        KEY(VK_GAMEPAD_MENU);
        KEY(VK_GAMEPAD_VIEW);
        KEY(VK_GAMEPAD_LEFT_THUMBSTICK_BUTTON);
        KEY(VK_GAMEPAD_RIGHT_THUMBSTICK_BUTTON);
        KEY(VK_GAMEPAD_LEFT_THUMBSTICK_UP);
        KEY(VK_GAMEPAD_LEFT_THUMBSTICK_DOWN);
        KEY(VK_GAMEPAD_LEFT_THUMBSTICK_RIGHT);
        KEY(VK_GAMEPAD_LEFT_THUMBSTICK_LEFT);
        KEY(VK_GAMEPAD_RIGHT_THUMBSTICK_UP);
        KEY(VK_GAMEPAD_RIGHT_THUMBSTICK_DOWN);
        KEY(VK_GAMEPAD_RIGHT_THUMBSTICK_RIGHT);
        KEY(VK_GAMEPAD_RIGHT_THUMBSTICK_LEFT);
        KEY(VK_OEM_AX);
        KEY(VK_ICO_HELP);
        KEY(VK_ICO_00);
        KEY(VK_ICO_CLEAR);
        KEY(VK_OEM_RESET);
        KEY(VK_OEM_JUMP);
        KEY(VK_OEM_PA1);
        KEY(VK_OEM_PA2);
        KEY(VK_OEM_PA3);
        KEY(VK_OEM_WSCTRL);
        KEY(VK_OEM_CUSEL);
        KEY(VK_OEM_ATTN);
        KEY(VK_OEM_FINISH);
        KEY(VK_OEM_COPY);
        KEY(VK_OEM_AUTO);
        KEY(VK_OEM_ENLW);
        KEY(VK_OEM_BACKTAB);
#undef KEY
        return n;
    }();
    return names;
}
static std::string trim(std::string s) {
    auto a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos)
        return {};
    return s.substr(a, s.find_last_not_of(" \t\r\n") - a + 1);
}
KeyConfig parseConfig(const std::string& text, const std::filesystem::path& file) {
    if (text.size() > 1024 * 1024)
        throw std::runtime_error("INI exceeds 1 MiB");
    wide(text); // validate the entire UTF-8 document, including comments
    KeyConfig c;
    c.file = std::filesystem::absolute(file);
    std::istringstream input(text.substr(text.starts_with("\xEF\xBB\xBF") ? 3 : 0));
    std::string line, section;
    size_t number = 0;
    std::set<uint32_t> ids;
    std::set<std::string> sections;
    std::array<size_t, 256> keyLines{};
    auto fail = [&](const std::string& reason) {
        throw std::runtime_error(utf8(c.file.wstring()) + ":" + std::to_string(number) + ": " + reason);
    };
    auto integer = [&](const std::string& s) {
        uint32_t value{};
        auto r = std::from_chars(s.data(), s.data() + s.size(), value);
        if (r.ec != std::errc{} || r.ptr != s.data() + s.size() || value == 0 || value > 65535)
            fail("Sound ID must be 1..65535");
        return value;
    };
    while (std::getline(input, line)) {
        ++number;
        auto comment = line.find("//");
        if (comment != std::string::npos)
            line.resize(comment);
        line = trim(line);
        if (line.empty())
            continue;
        if (line.front() == '[') {
            if (line != "[Sound Set]" && line != "[KeyBind]")
                fail("Unknown section: " + line);
            if (!sections.insert(line).second)
                fail("Duplicate section");
            section = line;
            continue;
        }
        auto colon = line.find(':');
        if (colon == std::string::npos)
            fail("Expected name: value");
        auto left = trim(line.substr(0, colon)), right = trim(line.substr(colon + 1));
        if (left.empty() || right.empty())
            fail("Empty name or value");
        if (section == "[Sound Set]") {
            auto id = integer(left);
            if (!ids.insert(id).second)
                fail("Duplicate sound ID");
            auto path = std::filesystem::path(wide(right));
            if (path.is_relative())
                path = c.file.parent_path() / path;
            c.sounds.push_back({id, path.lexically_normal(), number});
            if (c.sounds.size() > 256)
                fail("At most 256 sounds are supported");
        } else if (section == "[KeyBind]") {
            auto it = keyNames().find(left);
            if (it == keyNames().end())
                fail("Unknown key: " + left + " (see docs/KEY_NAMES.md)");
            if (c.keys[it->second])
                fail("Duplicate key mapping: " + left);
            c.keys[it->second] = integer(right);
            keyLines[it->second] = number;
        } else
            fail("Entry before section");
    }
    if (c.sounds.empty() || sections.size() != 2)
        fail("Both Sound Set and KeyBind are required");
    for (size_t k = 0; k < c.keys.size(); ++k)
        if (c.keys[k] && !ids.contains(c.keys[k])) {
            number = keyLines[k];
            fail("Mapping references a missing sound ID");
        }
    return c;
}
KeyConfig readConfig(const std::filesystem::path& file) {
    if (std::filesystem::file_size(file) > 1024 * 1024)
        throw std::runtime_error("INI exceeds 1 MiB");
    std::ifstream in(file, std::ios::binary);
    if (!in)
        throw std::runtime_error("Cannot open " + utf8(file.wstring()));
    return parseConfig(std::string(std::istreambuf_iterator<char>(in), {}), file);
}
std::string bindingsText(const KeyConfig& config) {
    std::string result;
    for (size_t k = 0; k < 256; ++k)
        if (config.keys[k]) {
            for (const auto& [name, code] : keyNames())
                if (code == k) {
                    result += name + " → " + std::to_string(config.keys[k]) + "   ";
                    break;
                }
        }
    return result;
}
} // namespace taiko

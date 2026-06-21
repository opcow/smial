#include "keycodes.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <stdexcept>
#include <unordered_map>

static std::unordered_map<std::string, uint16_t> KC;
static std::vector<std::pair<std::string, uint16_t>> ENTRIES;

static void buildTables() {
    if (!KC.empty()) return;

    KC["NO"] = 0x00; KC["TRNS"] = 0x01;

    const char* alpha = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    for (int i = 0; alpha[i]; i++) KC[std::string(1, alpha[i])] = (uint16_t)(0x04 + i);

    const char* digits = "123456789";
    for (int i = 0; digits[i]; i++) KC[std::string(1, digits[i])] = (uint16_t)(0x1E + i);

    KC["0"]=0x27; KC["ENT"]=0x28; KC["ESC"]=0x29; KC["BSPC"]=0x2A; KC["TAB"]=0x2B;
    KC["SPC"]=0x2C; KC["MINS"]=0x2D; KC["EQL"]=0x2E; KC["LBRC"]=0x2F; KC["RBRC"]=0x30;
    KC["BSLS"]=0x31; KC["SCLN"]=0x33; KC["QUOT"]=0x34; KC["GRV"]=0x35; KC["COMM"]=0x36;
    KC["DOT"]=0x37; KC["SLSH"]=0x38; KC["CAPS"]=0x39; KC["PSCR"]=0x46; KC["SCRL"]=0x47;
    KC["PAUS"]=0x48; KC["INS"]=0x49; KC["HOME"]=0x4A; KC["PGUP"]=0x4B; KC["DEL"]=0x4C;
    KC["END"]=0x4D; KC["PGDN"]=0x4E; KC["RGHT"]=0x4F; KC["LEFT"]=0x50; KC["DOWN"]=0x51;
    KC["UP"]=0x52; KC["NUM"]=0x53;

    for (int i = 1; i <= 12; i++)  KC["F" + std::to_string(i)] = (uint16_t)(0x3A + (i - 1));
    for (int i = 13; i <= 24; i++) KC["F" + std::to_string(i)] = (uint16_t)(0x68 + (i - 13));

    KC["COLN"]=0x0233; KC["EXLM"]=0x021E; KC["AT"]=0x021F;   KC["HASH"]=0x0220;
    KC["DLR"]=0x0221;  KC["PERC"]=0x0222; KC["CIRC"]=0x0223; KC["AMPR"]=0x0224;
    KC["ASTR"]=0x0225; KC["LPRN"]=0x0226; KC["RPRN"]=0x0227; KC["UNDS"]=0x022D;
    KC["PLUS"]=0x022E; KC["LCBR"]=0x022F; KC["RCBR"]=0x0230; KC["PIPE"]=0x0231;
    KC["DQUO"]=0x0234; KC["TILD"]=0x0235; KC["LT"]=0x0236;   KC["GT"]=0x0237;
    KC["QUES"]=0x0238;

    KC["CW_TOGG"]=0x7C73; KC["QK_LOCK"]=0x7C59;
    KC["AS_DOWN"]=0x7C10; KC["AS_UP"]=0x7C11; KC["AS_RPT"]=0x7C12;
    KC["AS_ON"]=0x7C13;   KC["AS_OFF"]=0x7C14; KC["AS_TOGG"]=0x7C15;

    for (int i = 0; i < 32; i++) KC["TD" + std::to_string(i)] = (uint16_t)(0x5700 | i);

    std::unordered_map<uint16_t, bool> seen;
    for (auto& [n, c] : KC) {
        if (seen[c]) continue;
        seen[c] = true;
        ENTRIES.push_back({n, c});
    }
    std::sort(ENTRIES.begin(), ENTRIES.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });
}

std::string nameOf(uint16_t code) {
    buildTables();
    if (code >= 0x5700 && code <= 0x57FF)
        return "TD" + std::to_string(code & 0xFF);
    for (auto& [n, c] : ENTRIES)
        if (c == code) return n;
    char buf[12];
    snprintf(buf, sizeof(buf), "0x%04X", code);
    return buf;
}

uint16_t kcParse(const std::string& raw) {
    buildTables();
    std::string s = raw;
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c){ return (char)toupper(c); });
    if (s.rfind("KC_", 0) == 0) s = s.substr(3);
    if (s.rfind("TD", 0) == 0 && s.size() > 2 && std::isdigit((unsigned char)s[2]))
        return (uint16_t)(0x5700 | std::stoi(s.substr(2)));
    if (s.rfind("0X", 0) == 0)
        return (uint16_t)std::stoul(s, nullptr, 16);
    auto it = KC.find(s);
    if (it != KC.end()) return it->second;
    throw std::runtime_error("Unknown keycode: " + raw);
}

const std::vector<std::pair<std::string, uint16_t>>& allKeycodes() {
    buildTables();
    return ENTRIES;
}

#include "protocol.h"
#include <algorithm>
#include <cstring>
#include <string>

static const char* const DB_METHODS[] = {"none", "sym_defer_g", "sym_eager_pk", "asym_eager_defer_pk"};
static constexpr int DB_METHOD_COUNT = 4;

const char* dbMethodName(uint8_t idx) {
    return (idx < DB_METHOD_COUNT) ? DB_METHODS[idx] : "?";
}
int dbMethodIndex(const char* name) {
    for (int i = 0; i < DB_METHOD_COUNT; i++)
        if (!strcmp(name, DB_METHODS[i])) return i;
    return -1;
}

static uint16_t u16(const std::array<uint8_t,32>& d, int o) {
    return (uint16_t)(d[o] | (d[o+1] << 8));
}

GlobalState getGlobal(HidDevice& d) {
    auto r = d.xfer({CMD, 0x01});
    return {u16(r,2), r[4], r[21], r[22]};
}
void setTT(HidDevice& d, uint16_t ms) {
    d.xfer({CMD, 0x02, (uint8_t)(ms & 0xFF), (uint8_t)(ms >> 8)});
}
void setTdEn(HidDevice& d, int i, bool on) {
    d.xfer({CMD, 0x03, (uint8_t)i, (uint8_t)(on ? 1 : 0)});
}
void setTdMode(HidDevice& d, int i, uint8_t m) {
    d.xfer({CMD, 0x04, (uint8_t)i, m});
}
void resetCfg(HidDevice& d) { d.xfer({CMD, 0x05}); }

TdSlot getTd(HidDevice& d, int i) {
    auto r = d.xfer({CMD, 0x06, (uint8_t)i});
    return {u16(r,3), u16(r,5), (bool)r[7], r[8], u16(r,9), r[11]};
}
void setTdKc(HidDevice& d, int i, uint16_t tap, uint16_t sec) {
    d.xfer({CMD, 0x07, (uint8_t)i,
            (uint8_t)(tap & 0xFF), (uint8_t)(tap >> 8),
            (uint8_t)(sec & 0xFF), (uint8_t)(sec >> 8)});
}
void setTdTiming(HidDevice& d, int i, uint16_t tt, uint8_t f) {
    d.xfer({CMD, 0x14, (uint8_t)i,
            (uint8_t)(tt & 0xFF), (uint8_t)(tt >> 8), f});
}
void startIdentify(HidDevice& d) { d.xfer({CMD, 0x08}); }

FeatState getFeat(HidDevice& d) {
    auto r = d.xfer({CMD, 0x09});
    return {u16(r,2), u16(r,4), u16(r,6), u16(r,8), r[10], r[11], u16(r,12)};
}
void setFlag(HidDevice& d, int bit, bool on) {
    d.xfer({CMD, 0x0A, (uint8_t)bit, (uint8_t)(on ? 1 : 0)});
}
void setParam(HidDevice& d, uint8_t pid, uint16_t v) {
    d.xfer({CMD, 0x0B, pid, (uint8_t)(v & 0xFF), (uint8_t)(v >> 8)});
}
IndState getInd(HidDevice& d, int i) {
    auto r = d.xfer({CMD, 0x0C, (uint8_t)i});
    return {(bool)r[3], r[4], r[5], r[6], r[7], r[8], {r[9], r[10], r[11], r[12]}};
}
void setInd(HidDevice& d, int i, bool en, uint8_t r, uint8_t g, uint8_t b,
            uint8_t scope, uint8_t count, const uint8_t* items) {
    uint8_t i0 = items ? items[0] : 0, i1 = items ? items[1] : 0;
    uint8_t i2 = items ? items[2] : 0, i3 = items ? items[3] : 0;
    d.xfer({CMD, 0x0D, (uint8_t)i, (uint8_t)(en ? 1 : 0), r, g, b,
            scope, count, i0, i1, i2, i3});
}

Combo getCombo(HidDevice& d, int i) {
    auto r = d.xfer({CMD, 0x0E, (uint8_t)i});
    Combo c{};
    for (int j = 0; j < COMBO_MAX_KEYS; j++) c.keys[j] = u16(r, 3 + j * 2);
    c.output  = u16(r, 11);
    c.enabled = (bool)r[13];
    return c;
}
void setCombo(HidDevice& d, int i, const Combo& c) {
    d.xfer({CMD, 0x0F, (uint8_t)i,
            (uint8_t)(c.keys[0] & 0xFF), (uint8_t)(c.keys[0] >> 8),
            (uint8_t)(c.keys[1] & 0xFF), (uint8_t)(c.keys[1] >> 8),
            (uint8_t)(c.keys[2] & 0xFF), (uint8_t)(c.keys[2] >> 8),
            (uint8_t)(c.keys[3] & 0xFF), (uint8_t)(c.keys[3] >> 8),
            (uint8_t)(c.output & 0xFF), (uint8_t)(c.output >> 8),
            (uint8_t)(c.enabled ? 1 : 0)});
}

KeyOverride getKo(HidDevice& d, int i) {
    auto r = d.xfer({CMD, 0x10, (uint8_t)i});
    return {u16(r,3), u16(r,5), r[7], r[8], r[9], r[10], r[11], (bool)r[12]};
}
void setKo(HidDevice& d, int i, const KeyOverride& k) {
    d.xfer({CMD, 0x11, (uint8_t)i,
            (uint8_t)(k.trigger & 0xFF), (uint8_t)(k.trigger >> 8),
            (uint8_t)(k.replacement & 0xFF), (uint8_t)(k.replacement >> 8),
            k.triggerMods, k.suppressedMods, k.negativeMods, k.layers, k.options,
            (uint8_t)(k.enabled ? 1 : 0)});
}

const char* const* modMaskNames() {
    static const char* const N[8] = {"LCtl","LSft","LAlt","LGui","RCtl","RSft","RAlt","RGui"};
    return N;
}

// ── VIA lighting ──────────────────────────────────────────────────────────────
// Channel-based protocol: [cmd, 0x03, value_id, data...]  reply[3] = first data byte.
static uint8_t rgbGet1(HidDevice& d, uint8_t vid) {
    return d.xfer({0x08, VIA_RGB_CHAN, vid})[3];
}
ViaLightState getLighting(HidDevice& d) {
    auto r = d.xfer({0x08, VIA_RGB_CHAN, VIA_RGB_COL_ID});  // hue+sat together
    return { rgbGet1(d, VIA_RGB_MODE_ID), r[3], r[4],
             rgbGet1(d, VIA_RGB_VAL_ID),  rgbGet1(d, VIA_RGB_SPD_ID) };
}
void setLightMode (HidDevice& d, uint8_t m)            { d.xfer({0x07, VIA_RGB_CHAN, VIA_RGB_MODE_ID, m}); }
void setLightVal  (HidDevice& d, uint8_t v)            { d.xfer({0x07, VIA_RGB_CHAN, VIA_RGB_VAL_ID,  v}); }
void setLightSpeed(HidDevice& d, uint8_t s)            { d.xfer({0x07, VIA_RGB_CHAN, VIA_RGB_SPD_ID,  s}); }
void setLightColor(HidDevice& d, uint8_t h, uint8_t s) { d.xfer({0x07, VIA_RGB_CHAN, VIA_RGB_COL_ID, h, s}); }
void saveLighting (HidDevice& d)                       { d.xfer({0x09, VIA_RGB_CHAN}); }

// ── VIA macros ────────────────────────────────────────────────────────────────
int viaMacroGetCount(HidDevice& d)   { return d.xfer({0x0C})[1]; }
int viaMacroGetBufSize(HidDevice& d) { auto r = d.xfer({0x0D}); return (r[1] << 8) | r[2]; }

std::vector<uint8_t> viaMacroGetBuf(HidDevice& d, int bufSize) {
    std::vector<uint8_t> buf(bufSize, 0);
    for (int off = 0; off < bufSize; ) {
        int sz = std::min(28, bufSize - off);
        auto r = d.xfer({0x0E, (uint8_t)(off >> 8), (uint8_t)(off & 0xFF), (uint8_t)sz});
        for (int i = 0; i < sz; i++) buf[off + i] = r[4 + i];
        off += sz;
    }
    return buf;
}

void viaMacroSetBuf(HidDevice& d, const std::vector<uint8_t>& data) {
    int total = (int)data.size();
    for (int off = 0; off < total; ) {
        int sz = std::min(28, total - off);
        uint8_t pkt[32] = {0x0F, (uint8_t)(off >> 8), (uint8_t)(off & 0xFF), (uint8_t)sz};
        for (int i = 0; i < sz; i++) pkt[4 + i] = data[off + i];
        d.xfer(pkt, 32);
        off += sz;
    }
}

void viaMacroReset(HidDevice& d) { d.xfer({0x10}); }

// Parse the flat macro buffer into per-macro action lists.
// Format: raw ASCII → Text; 0x01 prefix → action (Tap/Down/Up/Delay); 0x00 → end of macro.
std::vector<Macro> parseMacros(const std::vector<uint8_t>& buf, int count) {
    std::vector<Macro> macros(count);
    int pos = 0, n = (int)buf.size();
    for (int m = 0; m < count && pos < n; m++) {
        Macro& mac = macros[m];
        std::string textAcc;  // accumulate consecutive ASCII chars into one Text step
        while (pos < n) {
            uint8_t b = buf[pos++];
            if (b == 0x00) {  // end of this macro
                if (!textAcc.empty()) { mac.push_back({MacroOp::Text,0,0,textAcc}); textAcc.clear(); }
                break;
            }
            if (b == 0x01) {  // SS_QMK_PREFIX
                if (!textAcc.empty()) { mac.push_back({MacroOp::Text,0,0,textAcc}); textAcc.clear(); }
                if (pos >= n) break;
                uint8_t code = buf[pos++];
                if (code == 0x01 || code == 0x02 || code == 0x03) {  // Tap/Down/Up
                    if (pos >= n) break;
                    uint8_t kc = buf[pos++];
                    mac.push_back({(MacroOp)code, kc, 0, {}});
                } else if (code == 0x04) {  // Delay — decimal digits then '|'
                    std::string digits;
                    while (pos < n && buf[pos] != '|') digits += (char)buf[pos++];
                    if (pos < n) pos++;  // skip '|'
                    uint16_t ms = digits.empty() ? 0 : (uint16_t)std::stoi(digits);
                    mac.push_back({MacroOp::Delay, 0, ms, {}});
                }
            } else {
                textAcc += (char)b;  // plain ASCII — accumulate
            }
        }
        if (!textAcc.empty()) { mac.push_back({MacroOp::Text,0,0,textAcc}); textAcc.clear(); }
    }
    return macros;
}

// Serialize macro action lists back into the flat buffer.
std::vector<uint8_t> serializeMacros(const std::vector<Macro>& macros, int count, int bufSize) {
    std::vector<uint8_t> buf(bufSize, 0);
    int pos = 0;
    for (int m = 0; m < count; m++) {
        if (m < (int)macros.size()) {
            for (const auto& s : macros[m]) {
                if (s.op == MacroOp::Text) {
                    for (char c : s.text) {
                        if (pos >= bufSize - 1) goto done;
                        buf[pos++] = (uint8_t)c;
                    }
                } else if (s.op == MacroOp::Delay) {
                    std::string ds = std::to_string(s.delay);
                    if (pos + 2 + (int)ds.size() >= bufSize) goto done;
                    buf[pos++] = 0x01; buf[pos++] = 0x04;
                    for (char c : ds) buf[pos++] = (uint8_t)c;
                    buf[pos++] = '|';
                } else {
                    if (pos + 2 >= bufSize) goto done;
                    buf[pos++] = 0x01; buf[pos++] = (uint8_t)s.op; buf[pos++] = s.kc;
                }
            }
        }
        if (pos >= bufSize) goto done;
        buf[pos++] = 0x00;  // macro terminator
    }
done:
    return buf;
}

uint16_t viaGet(HidDevice& d, int l, int row, int col) {
    auto r = d.xfer({0x04, (uint8_t)l, (uint8_t)row, (uint8_t)col});
    return (uint16_t)((r[4] << 8) | r[5]);
}
void viaSet(HidDevice& d, int l, int row, int col, uint16_t kc) {
    d.xfer({0x05, (uint8_t)l, (uint8_t)row, (uint8_t)col,
            (uint8_t)(kc >> 8), (uint8_t)(kc & 0xFF)});
}
void viaKmReset(HidDevice& d) { d.xfer({0x06}); }

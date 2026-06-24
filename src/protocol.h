#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "hid.h"

static constexpr uint8_t CMD = 0xAC;

// Total tap-dance slots (must match TD_SLOT_COUNT in the firmware keymap).
static constexpr int TD_SLOT_COUNT = 32;
// RGB state indicators (must match INDICATOR_COUNT in the firmware).
static constexpr int INDICATOR_COUNT = 9;
// Dynamic-keymap dimensions (must match the firmware: 4 layers, 6x16 matrix).
static constexpr int KM_LAYERS = 4;
static constexpr int KM_ROWS   = 6;
static constexpr int KM_COLS   = 16;
// Runtime combos / key overrides (must match firmware).
static constexpr int COMBO_SLOT_COUNT = 16;
static constexpr int COMBO_MAX_KEYS   = 4;
static constexpr int KO_SLOT_COUNT    = 16;
// Per-slot PH flags (must match bit encoding in keymap.c td_ph_has/td_ph_value)
static constexpr uint8_t TD_PH_VALUE = 0x01;  // permissive-hold on/off value
static constexpr uint8_t TD_HAS_PH   = 0x02;  // whether PH is overridden for this slot

// td_enabled/td_mode are also in GET_GLOBAL but unused here (read per-slot via
// getTd); they're 64-bit on the wire now, so we don't decode them.
struct GlobalState { uint16_t tt; uint8_t slots; uint8_t comboSlots; uint8_t koSlots; };
struct FeatState   { uint16_t flags; uint16_t quicktap; uint16_t astimeout; uint16_t cwtimeout; uint16_t debounce; uint8_t debounceMethod; uint16_t oneshotTimeout; };
struct TdSlot      { uint16_t tap; uint16_t sec; bool enabled; uint8_t mode;
                     uint16_t tappingTerm; uint8_t phFlags; }; // mode: 0=double 1=hold
// scope: 0 board, 1 keys, 2 rows, 3 cols. count = items[] in use (0..4). items[]:
// KEYS = packed (row<<4)|col; ROWS/COLS = row/col index.
enum { IND_SCOPE_BOARD = 0, IND_SCOPE_KEYS = 1, IND_SCOPE_ROWS = 2, IND_SCOPE_COLS = 3 };
struct IndState    { bool enabled; uint8_t r, g, b; uint8_t scope; uint8_t count; uint8_t items[4]; };
struct Combo       { uint16_t keys[COMBO_MAX_KEYS]; uint16_t output; bool enabled; };
struct KeyOverride { uint16_t trigger; uint16_t replacement; uint8_t triggerMods;
                     uint8_t suppressedMods; uint8_t negativeMods; uint8_t layers;
                     uint8_t options; bool enabled; };

// Debounce method index <-> canonical name (must match keymap.c dispatcher order:
// 0 none, 1 sym_defer_g, 2 sym_eager_pk, 3 asym_eager_defer_pk).
const char* dbMethodName(uint8_t idx);  // "?" if out of range
int         dbMethodIndex(const char* name);  // -1 if unknown

// 0xAC commands
GlobalState getGlobal(HidDevice& d);
void        setTT(HidDevice& d, uint16_t ms);
void        setTdEn(HidDevice& d, int idx, bool on);
void        setTdMode(HidDevice& d, int idx, uint8_t mode);
void        resetCfg(HidDevice& d);
TdSlot      getTd(HidDevice& d, int idx);
void        setTdKc(HidDevice& d, int idx, uint16_t tap, uint16_t sec);
void        setTdTiming(HidDevice& d, int idx, uint16_t tt, uint8_t phFlags);
void        startIdentify(HidDevice& d);
FeatState   getFeat(HidDevice& d);
void        setFlag(HidDevice& d, int bit, bool on);
void        setParam(HidDevice& d, uint8_t pid, uint16_t val);
IndState    getInd(HidDevice& d, int idx);
void        setInd(HidDevice& d, int idx, bool en, uint8_t r, uint8_t g, uint8_t b,
                   uint8_t scope = 0, uint8_t count = 0, const uint8_t* items = nullptr);
Combo       getCombo(HidDevice& d, int idx);
void        setCombo(HidDevice& d, int idx, const Combo& c);
KeyOverride getKo(HidDevice& d, int idx);
void        setKo(HidDevice& d, int idx, const KeyOverride& k);

// Modifier-mask bit names for trigger_mods/suppressed_mods/negative_mods (QMK
// MOD_* order: bit0 LCtl, 1 LSft, 2 LAlt, 3 LGui, 4 RCtl, 5 RSft, 6 RAlt, 7 RGui).
const char* const* modMaskNames();  // 8 entries

// VIA dynamic keymap (big-endian keycodes, layer 0–3, 6 rows × 16 cols)
uint16_t viaGet(HidDevice& d, int layer, int row, int col);
void     viaSet(HidDevice& d, int layer, int row, int col, uint16_t kc);
void     viaKmReset(HidDevice& d);

// VIA RGB Matrix — channel-based protocol (via.c id_custom_get/set_value 0x07/0x08/0x09)
// Packet format: [cmd, channel=0x03, value_id, data...]
// Response:      [cmd, channel, value_id, data[0], data[1], ...]
static constexpr uint8_t VIA_RGB_CHAN     = 0x03;
static constexpr uint8_t VIA_RGB_VAL_ID  = 0x01;  // brightness (val)
static constexpr uint8_t VIA_RGB_MODE_ID = 0x02;  // effect/mode
static constexpr uint8_t VIA_RGB_SPD_ID  = 0x03;  // speed
static constexpr uint8_t VIA_RGB_COL_ID  = 0x04;  // hue+sat packed (2 bytes)
struct ViaLightState { uint8_t mode, hue, sat, val, speed; };
ViaLightState getLighting(HidDevice& d);
void          setLightMode (HidDevice& d, uint8_t mode);
void          setLightVal  (HidDevice& d, uint8_t val);
void          setLightSpeed(HidDevice& d, uint8_t speed);
void          setLightColor(HidDevice& d, uint8_t hue, uint8_t sat);
void          saveLighting (HidDevice& d);

// VIA macros (via.c 0x0C–0x10)
// Buffer format: QMK SS strings — raw ASCII for text; 0x01 prefix for actions:
//   Tap:   0x01 0x01 <hid_usage>          (3 bytes)
//   Down:  0x01 0x02 <hid_usage>          (3 bytes)
//   Up:    0x01 0x03 <hid_usage>          (3 bytes)
//   Delay: 0x01 0x04 <decimal_digits> '|' (variable)
// Each macro ends with 0x00. Text uses 1-byte HID usage for keycodes.
enum class MacroOp : uint8_t { Text=0, Tap=1, Down=2, Up=3, Delay=4 };
struct MacroStep {
    MacroOp  op    = MacroOp::Text;
    uint8_t  kc    = 0;    // HID usage (1-byte) for Tap/Down/Up
    uint16_t delay = 0;    // ms for Delay
    std::string text;      // for Text
};
using Macro = std::vector<MacroStep>;

int  viaMacroGetCount(HidDevice& d);
int  viaMacroGetBufSize(HidDevice& d);
std::vector<uint8_t> viaMacroGetBuf(HidDevice& d, int bufSize);
void viaMacroSetBuf(HidDevice& d, const std::vector<uint8_t>& data);
void viaMacroReset(HidDevice& d);

std::vector<Macro>    parseMacros(const std::vector<uint8_t>& buf, int count);
std::vector<uint8_t>  serializeMacros(const std::vector<Macro>& macros, int count, int bufSize);

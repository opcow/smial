# Porting real-time config to another QMK keyboard

This guide explains the firmware side of q1config and how to add a compatible
real-time-configuration interface to **another QMK keyboard that has no Vial port**. Once a
board speaks this protocol, the same `q1config.py` CLI and `q1config.html` WebHID GUI work
against it (after a VID/PID + layout tweak).

The original reference is the Keychron Q1 Pro `rtcfg` keymap. The same firmware has since been
factored into a shared core and ported across the whole Keychron **`_max`** family (Q1/Q2/Q3/
Q5/Q6/Q8/Q60/Q65 Max, K1/K3/K5/K7 Max, V1/V2/V5 Max — ANSI/ISO/JIS, RGB and white), so a
second board is now a thin layout-only keymap. See **§7** for that shared-core pattern. Exact
byte layouts live in [PROTOCOL.md](PROTOCOL.md); this document focuses on *how it's wired* and
*what to copy*.

---

## 1. What this gives you / when to use it

A host app can read and write keyboard settings **at runtime, over USB**, with no recompile
or reflash: tapping term, per-tap-dance-slot tap-hold options, tap-dance behavior, combos,
key overrides, one-shot timeout, debounce method/time, Caps Word, Auto Shift, RGB state
indicators, and even key remapping. Settings persist in EEPROM. It's a lightweight, custom
subset of what Vial provides — useful when your board isn't supported by the vial-qmk fork.

Requirements on the target board:

- QMK with **`VIA_ENABLE = yes`** (recommended — it pulls in `RAW_ENABLE` and the dynamic
  keymap used for key remapping). Plain **`RAW_ENABLE = yes`** also works if you don't want
  VIA; you then lose the built-in key-remap path.
- Host side: Python 3 + `hidapi` for the CLI; a Chromium browser (Chrome/Edge) for the GUI.

---

## 2. How it works (the firmware changes)

**Transport.** Standard QMK raw HID: USB usage page `0xFF60`, usage `0x61`, fixed **32-byte**
reports, report ID 0. Every request gets one 32-byte reply.

**Custom command byte.** All custom packets start with a byte that doesn't collide with
anything else on the wire — the reference uses **`0xAC`**. `data[1]` is a subcommand; for our
replies `data[1]` is a status (`0x00` OK / `0xFF` error). See [PROTOCOL.md](PROTOCOL.md).

**Routing — the important bit.** When VIA is enabled, `quantum/via.c`'s `raw_hid_receive()`
calls **`via_command_kb(data, length)` first**, before VIA's own command switch. If it
returns `true`, VIA treats the command as fully handled (you must call `raw_hid_send`
yourself). `via_command_kb` is declared **weak** in via.c, so:

- On a **plain VIA board** (no keyboard-level `via_command_kb`), just define
  `via_command_kb()` in your keymap.
- On a board whose core **already defines** `via_command_kb` (e.g. Keychron handles `0xAA`
  BT-DFU / `0xAB` factory test), you can't add a second definition — instead add a weak
  `via_command_user()` to the keyboard `.c` and call it from the existing `default:` branch,
  then override `via_command_user()` in your keymap.
- With **no VIA**, define `raw_hid_receive()` directly.

**Storage.** Config lives in QMK's per-user EEPROM "data block" (`EECONFIG_USER_DATA_SIZE`).
A version dword (`EECONFIG_USER_DATA_VERSION`) guards the layout: bump it whenever the struct
changes and stale data is discarded so defaults reapply on the next boot.

**Responses & IDENTIFY.** Handlers mutate the 32-byte buffer and call `raw_hid_send`. One
command (IDENTIFY) acks immediately, then sends an *unsolicited* report with the next
keypress's row/col — handy for "click a key in the GUI."

**Key remapping for free.** Assigning a physical key to a tap-dance slot (or any keycode)
uses QMK's **standard VIA dynamic-keymap** commands (`0x05` set / `0x04` get), no custom
firmware needed — a tap-dance keycode is just `TD(n) = 0x5700 | n`.

**Rebuilt live tables.** Several QMK features read a compile-time table (combos, key
overrides) or fix one algorithm at compile time (debounce). To make them runtime-configurable
the keymap keeps the *definitions* in EEPROM and rebuilds the live RAM table whenever config
changes (`rebuild_combos()` / `rebuild_kos()`, or a custom-debounce dispatcher) — see §4.

**Neutral defaults.** The reference ships *stock* compile-time defaults (everything off) so
the firmware is shareable; personal setup is applied at runtime and stored only in the
owner's EEPROM. Recommended but optional.

---

## 3. Porting steps (minimal core)

### Step 0 — build flags

In your keymap's `rules.mk`:

```make
VIA_ENABLE = yes        # also enables RAW_ENABLE + dynamic keymap
# keep LTO_ENABLE off — it can break weak-symbol overrides across compile units
```

Pick a command byte that won't collide: avoid VIA's command IDs (`0x00`–`0x0D`) and any your
board already uses (Keychron uses `0xAA`/`0xAB`). The reference uses `0xAC`.

### Step 1 — route the command

**Plain VIA board** — in `keymap.c`:

```c
#include "raw_hid.h"
#define HID_CMD 0xAC

bool via_command_kb(uint8_t *data, uint8_t length) {
    if (data[0] != HID_CMD) return false;   // let VIA handle everything else
    handle_cmd(data, length);               // your dispatcher (Step 3)
    raw_hid_send(data, length);
    return true;                            // fully handled
}
```

**Board that already defines `via_command_kb`** (Keychron pattern) — in the keyboard `.c`,
add a weak hook and call it from the existing `default:`:

```c
__attribute__((weak)) bool via_command_user(uint8_t *data, uint8_t length) { return false; }

bool via_command_kb(uint8_t *data, uint8_t length) {
    switch (data[0]) {
        /* ...existing 0xAA / 0xAB cases... */
        default: return via_command_user(data, length);
    }
    return true;
}
```

…then implement `via_command_user()` (same body as the `via_command_kb` above) in `keymap.c`.
If a whole board family shares one common `.c` (Keychron's `common/keychron_common.c` backs
all `_max` boards), add the weak hook + `default:` call **there once** and every board in the
family is routed — the keymap that links a strong `via_command_user` (the rtcfg core) claims
the byte; all other keymaps are unaffected.

**No VIA** — define `raw_hid_receive()` directly and dispatch on `data[0]`.

### Step 2 — EEPROM config block

In `config.h`:

```c
#define EECONFIG_USER_DATA_SIZE    32      // sizeof(user_config_t), keep in sync
#define EECONFIG_USER_DATA_VERSION 0x0001  // bump on any struct-layout change
```

In `keymap.c`:

```c
typedef struct {
    uint16_t tapping_term;   // 0 = "uninitialized" sentinel
    /* ...add your fields; keep 32-bit fields first to avoid padding... */
    uint8_t  reserved[ /* pad to EECONFIG_USER_DATA_SIZE */ ];
} user_config_t;
_Static_assert(sizeof(user_config_t) == 32, "size must match EECONFIG_USER_DATA_SIZE");

static user_config_t user_config;
static const user_config_t default_config = { .tapping_term = 200, /* neutral defaults */ };

void eeconfig_init_user_datablock(void) {        // called on version mismatch / fresh EEPROM
    user_config = default_config;
    eeconfig_update_user_datablock(&user_config);
}

void keyboard_post_init_user(void) {
    eeconfig_read_user_datablock(&user_config);   // returns zeros if block is invalid
    if (user_config.tapping_term == 0) {          // sentinel ⇒ first run ⇒ defaults
        user_config = default_config;
        eeconfig_update_user_datablock(&user_config);
    }
}
```

### Step 3 — a minimal GET/SET handler

```c
enum { SUB_GET = 0x01, SUB_SET_TT = 0x02 };

static void handle_cmd(uint8_t *data, uint8_t length) {
    switch (data[1]) {
        case SUB_GET:
            data[1] = 0x00;                                   // OK
            data[2] = user_config.tapping_term & 0xFF;
            data[3] = user_config.tapping_term >> 8;
            break;
        case SUB_SET_TT:
            user_config.tapping_term = data[2] | (data[3] << 8);
            eeconfig_update_user_datablock(&user_config);
            data[1] = 0x00;
            break;
        default:
            data[1] = 0xFF;                                   // error
    }
}
```

Make the runtime value take effect — e.g. add the tapping-term callback (needs
`TAPPING_TERM_PER_KEY = yes`):

```c
uint16_t get_tapping_term(uint16_t kc, keyrecord_t *r) { return user_config.tapping_term; }
```

That's a complete, working real-time setting. Everything else is more of the same pattern.

---

## 4. Optional feature recipes

Each maps a `user_config` field (or bit) to a QMK hook; see the `rtcfg` keymap for full code.

- **Global + per-slot tap-hold** — `#define PERMISSIVE_HOLD_PER_KEY` (and
  `HOLD_ON_OTHER_KEY_PRESS_PER_KEY` / `RETRO_TAPPING_PER_KEY` / `QUICK_TAP_TERM_PER_KEY`) in
  `config.h`, then return a runtime flag from `get_permissive_hold` / `get_hold_on_other_key_press`
  / `get_retro_tapping` / `get_quick_tap_term`. For *per-tap-dance-slot* overrides, have those
  callbacks (plus `get_tapping_term`) check whether the keycode is a tap-dance trigger
  (`QK_TAP_DANCE <= kc < QK_TAP_DANCE + slots`) and return that slot's stored override if set,
  else fall through to the global value — per-slot timing without per-physical-key storage.
- **Runtime tap-dance slots** — give every slot the same `ACTION_TAP_DANCE_FN_ADVANCED`
  callbacks; read keycodes/mode/enable from `user_config` (`user_data` = slot index). Place
  `TD(n)` in the layout for keys you want configurable. The reference exposes 32 slots
  (`TD0`–`TD31`); `tap_dance_actions[]` must have an entry for every slot so any can be assigned.
- **Runtime combos** (`COMBO_ENABLE = yes`) — store combo defs (input keycodes + output) in
  `user_config` and rebuild QMK's table from them. Because `keymap_introspection.c` `#include`s
  the keymap (same translation unit), you can't override the weak `combo_count()`/`combo_get()`
  there; instead define a full-size `combo_t key_combos[]` (which stock `combo_count_raw()`/
  `combo_get_raw()` serve) and a `rebuild_combos()` that fills each slot's `COMBO_END`-terminated
  key list — disabled/invalid slots get an empty list so they never fire. Key-list pointers may
  live in RAM on ARM (`pgm_read_word` is a plain load); AVR would need PROGMEM.
- **Runtime key overrides** (`KEY_OVERRIDE_ENABLE = yes`) — QMK reads the weak `key_overrides`
  (a NULL-terminated array of `const key_override_t *`). Point it at a RAM table rebuilt from
  `user_config` (`rebuild_kos()`); include only enabled slots and keep the array NULL-terminated.
- **Runtime one-shot timeout** — `#define ONESHOT_TIMEOUT 0` to disable QMK's built-in expiry,
  then expire a pending one-shot mod/layer yourself in `housekeeping_task_user()`: read
  `get_oneshot_mods()` / `get_oneshot_layer_state()`, start a timer when one arms, and
  `clear_oneshot_mods()` / `clear_oneshot_layer_state(...)` on timeout (skip a tap-toggled, i.e.
  held, layer). `ONESHOT_TAP_TOGGLE` stays compile-time — QMK has no runtime hook for it.
- **Runtime debounce method + time** — `DEBOUNCE_TYPE = custom` (+ `SRC += debounce_rt.c`).
  Compile several stock algorithms into one file, each with uniquely-prefixed symbols and the
  compile-time `DEBOUNCE` constant replaced by a `rtcfg_debounce_time()` accessor, behind a
  dispatcher exposed as the single `debounce()/debounce_init()/debounce_free()`. The dispatcher
  picks the algorithm from `rtcfg_debounce_method()` each scan, calling the old method's `free`
  + new method's `init` on a switch; a time of 0 means "no debounce" regardless of method. Defer
  real per-method init to the first `debounce()` call — `debounce_init()` runs before
  `keyboard_post_init_user()` loads `user_config`. Read settings through tiny accessors
  (`rtcfg.h`) so the dispatcher needn't know the `user_config_t` layout.
- **Caps Word** — `#define CAPS_WORD_IDLE_TIMEOUT 0` to disable the built-in timer; gate
  activation in `caps_word_set_user(true)` (call `caps_word_off()` if disabled) and roll a
  runtime idle timeout in `housekeeping_task_user()`.
- **Auto Shift** — `AUTO_SHIFT_ENABLE = yes` + `#define AUTO_SHIFT_DISABLED_AT_STARTUP`;
  apply stored enable/timeout at boot with `autoshift_enable/disable` + `set_autoshift_timeout`
  (Auto Shift state isn't EEPROM-backed, so you own persistence).
- **Key Lock** — `KEY_LOCK_ENABLE = yes`; assign the `QK_LOCK` keycode to a key via the
  dynamic-keymap path. No stored config.
- **RGB state indicators** — store `{enabled, r, g, b}` per state and paint in
  `rgb_matrix_indicators_advanced_user()`.

---

## 5. Adapting the host app

The protocol is board-independent, so the CLI and GUI port with two changes:

1. **VID/PID** — set `VID`/`PID` (`q1config.py`) and the WebHID filter (`q1config.html`) to
   your board's USB IDs (from its `info.json` / `config.h`).
2. **Layout** (GUI only) — replace the `LAYOUT` array in `q1config.html` with your board's
   key coordinates from its `info.json` `layouts[...]layout` (each `{matrix:[r,c], x, y, w}`).

Drop any subcommands/UI for features you didn't implement. The preset JSON schema is
unchanged. Every board has its own VID/PID, so multi-board support is just a table of
`{VID, PID, layout}` entries selected after the device is opened.

---

## 6. Gotchas

- **EEPROM reset:** changing `EECONFIG_USER_DATA_SIZE`/`_VERSION` (or first install) discards
  stored settings on the next flash — expected; reapply via a preset.
- **Command-byte collisions:** don't reuse VIA IDs `0x00`–`0x0D` or board-specific bytes.
- **Framing:** always 32-byte reports, report ID 0. The host pads to 32 and (hidapi) prepends
  a `0x00` report-ID byte.
- **One request at a time:** serialize host requests; each command expects exactly one reply.
- **VIA reply convention differs:** for VIA dynamic-keymap commands the reply's `data[1]` is
  data (the layer), *not* a status — don't status-check those.
- **Weak symbols + LTO:** keep `LTO_ENABLE` off; LTO can interfere with overriding weak
  functions across compilation units.
- **Combos in the introspection TU:** `keymap_introspection.c` `#include`s the keymap and takes
  `sizeof(key_combos)`, so the `combo_t key_combos[]` table must be **defined in the keymap TU**
  (the core `extern`s and fills it) — and you override that array, not the weak
  `combo_count()`/`combo_get()`, which live in the same TU and can't be redefined.
- **Custom debounce init timing:** `debounce_init()` fires before EEPROM config is loaded; defer
  per-method allocation/selection to the first `debounce()` call so it sees the loaded settings.
- **RAM vs PROGMEM tables:** rebuilding combo/keymap tables in RAM works on ARM, where `pgm_read_*`
  is a plain memory read; on AVR the data must be in PROGMEM.
- **WebHID:** the GUI must be served from `http://localhost` (or https); `file://` is blocked.

---

## 7. Porting to a whole board family (the shared-core pattern)

Once more than one board needs this, don't copy `keymap.c` around — split it into a **shared
core** plus **thin per-board keymaps**. This is how the Keychron `_max` fleet is built
(`keyboards/keychron/rtcfg_common/` in the qmk tree):

- **Shared core** (`rtcfg_common.c` + `.h` + `.mk`, pulled in with one `include` from each
  keymap's `rules.mk`) owns everything board-independent: the `user_config_t` + EEPROM I/O,
  the `0xAC` handler (`via_command_user`), tap-dance slots, combos, key overrides, one-shot,
  Caps Word, Auto Shift, the debounce dispatcher, and the state indicators. Move `debounce_rt.c`
  here too.
- **Each board keymap** keeps only what QMK's introspection must see in the keymap translation
  unit — `keymaps[]`, `encoder_map[]` (knob boards), and the `combo_t key_combos[N]` table
  (the core `extern`s and fills it) — plus a 4-line `process_record_user` that calls the
  board's existing special-key handler then `rtcfg_process_record()`. Copy the layout straight
  from the board's stock `via` keymap; this is the only per-board work.

Three things make the core board-agnostic:

- **Don't bake layer names into the core.** Boards differ (`MAC_BASE/MAC_FN/WIN_BASE/WIN_FN`,
  but also `…/WIN_FN1/FN2`, or a 60% `…/FN/L3`). Keep each board's own layer enum in its
  keymap and have the core reference the FN-indicator layer by a numeric macro
  (`RTCFG_FN_INDICATOR_LAYER`, default `3`), overridable in a board's `config.h`.
- **Support both backlight types.** Guard the indicator on `RGB_MATRIX_ENABLE`
  (`rgb_matrix_set_color`, full color) vs `LED_MATRIX_ENABLE` (`led_matrix_set_value`, single
  color — derive brightness from the stored color's strongest channel) so white variants build.
- **Compile is the verification.** With no hardware, a port that compiles + links and speaks
  the protocol is most of the way there; build every target (`qmk compile -kb … -km rtcfg`) in
  CI. A thin generator that rewrites each board's `via` keymap into its `rtcfg` keymap keeps the
  layouts faithful and the boilerplate identical across the fleet.

---

See [PROTOCOL.md](PROTOCOL.md) for the full command table and preset schema, and
[README.md](README.md) for using the tools.

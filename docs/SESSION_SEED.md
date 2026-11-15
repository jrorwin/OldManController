# Seed: OldManController local Claude Code session (Windows)

You're picking up an existing project on the owner's Windows PC. Read this whole file, then
`README.md`, then the files listed under "Code map" before changing anything. The owner is new to
git: handle git for them and explain in plain words what you did.

## 1. First-time setup (do this first, and tell the owner what you're doing)

1. **Repo.** It should be at `D:\Github\OldManController`. If it isn't there yet, clone it:
   `git clone https://github.com/jrorwin/OldManController D:\Github\OldManController`.
   If it's already cloned, `git pull` on `main`.
2. **Remotes.** `origin` = `https://github.com/jrorwin/OldManController` (the owner's repo, push
   here). Add upstream HayBox as a read-only remote so it can never be pushed to by mistake:
   `git remote add upstream https://github.com/JonnyHaystack/HayBox` then
   `git remote set-url --push upstream no-push-to-upstream`.
3. **Build tools.** Check for `pio --version`. If missing, install PlatformIO Core
   (`pip install platformio`, which needs Python 3) or ask the owner whether they'd rather use the
   VS Code PlatformIO extension. Ask before installing anything.
4. **First build.** From the repo root run `pio run -e pico -e pico_encoder_debug`. The first run
   downloads the RP2040 toolchain and libraries and can take a long time.
   - Known quirk: if the first build fails with `config.pb.h: No such file or directory`, delete
     `.pio\build\pico\nanopb` (and `.pio\build\pico_encoder_debug\nanopb`) and build again. The
     nanopb generator runs before the proto library has finished downloading on a fresh checkout.
   - The only expected warnings come from framework/library code (LittleFS, ArduinoCore `Common.h`,
     upstream `HAL/pico/src/display/ConfigMenu.cpp`). Any warning in our files is a regression.
5. **Check the build matches the repo.** Copy `.pio\build\pico\firmware.uf2` over
   `firmware\haybox-pico-encoder.uf2` (and the debug one) and run `git status`. Small binary
   differences between machines are fine; just commit them with the next change.

## 2. Workflow rules (every change)

The owner wants it to "just work": they pull with GitHub Desktop and flash the `.uf2`.

1. Make the change. Keep the repo's style (`.clang-format`, naming, comments).
2. Build **both** envs: `pio run -e pico -e pico_encoder_debug`. Fix any new warnings.
3. Copy the outputs into `firmware\`:
   - `.pio\build\pico\firmware.uf2` → `firmware\haybox-pico-encoder.uf2`
   - `.pio\build\pico_encoder_debug\firmware.uf2` → `firmware\haybox-pico-encoder-DEBUG.uf2`
4. If the change affects behaviour, pins or settings, update the OldManController section at the
   top of `README.md`.
5. Commit source and firmware together, with a clear message, on `main`, and `git push origin main`.
6. Tell the owner in plain words what changed, that it's pushed, and what to test.

Never push to upstream HayBox. Never rewrite history that's already on GitHub (no force pushes).

**Flashing from the PC (optional, ask first):** with the Pico held in BOOTSEL and plugged in,
`pio run -e pico -t upload` flashes it. Otherwise the owner drags the `.uf2` onto the `RPI-RP2`
drive. To reach the bootloader without BOOTSEL, hold the GP15 button (C-Down) while plugging in.

## 3. What this project is

A HayBox (https://github.com/JonnyHaystack/HayBox, based on commit `2cffcd2`, v3.1.0) build for a
Raspberry Pi Pico box controller, plus a KY-040 rotary encoder. Targets: GameCube/Wii through a
custom GCC breakout and cable (joybus, data on GP28), **and** USB (XInput/DInput/Switch). All
stock modes and USB features must keep working.

### Pinout (`config/pico/config.cpp`)

All stock HayBox Pico mappings are unchanged. Main ones:

| Function | InputState field | GPIO |
|---|---|---|
| A / B / X / Y | `rt1` / `rf1` / `rf2` / `rf6` | 14 / 26 / 21 / 22 |
| Z / L / R | `rf3` / `lf4` / `rf5` | 19 / 5 / 27 |
| Stick L / R / Down / Up | `lf3` / `lf1` / `lf2` / `rf4` | 4 / 2 / 3 / 17 |
| Mod X / Mod Y | `lt1` / `lt2` | 6 / 7 |
| C-stick L / R / Down / Up | `rt3` / `rt5` / `rt2` / `rt4` | 13 / 16 / 15 / 12 |
| Start | `mb1` | 0 |
| Light shield / Mid shield | `rf7` / `rf8` | 20 / 18 |
| D-pad left / right (Melee) | `mb3` / `mb2` | 11 / 10 |
| **Knob press = D-pad mod** | `lf5` | 1 (physical pin 2) |
| **Encoder CLK / DT** | (PIO) | 8 / 9 |
| Joybus data | | 28 |

No free GPIOs remain on the Pico header. Encoder `+` goes to 3V3 (never VBUS), GND to GND.
GP15 held at boot = reboot to bootloader (keep it mapped).

### Features we added

1. **Rotary encoder → stick pulses.** Each clockwise detent pulses left stick **up**, each
   counter-clockwise detent pulses left stick **down**. Implemented in Melee mode
   (`Melee20Button`) only. A pulse overrides a held opposite direction; Mod X/Y still apply.
2. **D-pad mod.** While the knob press (GP1, `BTN_LF5`) is held, the four left-stick buttons output
   D-pad directions in **every** controller mode and the stick stays centred. Opposites cancel.

### Decisions already made (don't revisit without asking)

- Keep all stock pin mappings, all modes, USB, Mod X, Mod Y, light/mid shield.
- Knob press is the D-pad mod (it was otherwise unused).
- The owner tried and rejected: Mod X/Y as D-pad up/down; LS/MS as D-pad up/down.
- Encoder pulses only in Melee mode for now.

## 4. Code map and design

| File | What |
|---|---|
| `HAL/pico/include/input/RotaryEncoderInput.hpp`, `HAL/pico/src/input/RotaryEncoderInput.cpp` | Encoder input source: PIO decoder, detent accumulation, pulse state machine |
| `config/pico/config.cpp` | Pins, all encoder and D-pad mod settings, wiring into `setup`/`loop`/`loop1` |
| `config/pico/env.ini` | `pico` env and `pico_encoder_debug` env (`-D ENCODER_DEBUG`) |
| `src/modes/Melee20Button.cpp` | Encoder CW/CCW (`lt7`/`lt8`) → stick up/down |
| `include/core/ControllerMode.hpp`, `src/core/ControllerMode.cpp` | D-pad mod (`ControllerMode::SetDpadMod`), shared by all controller modes; disabled unless a config sets it, so other controllers built from this repo are unaffected |

Key design points (all deliberate):

- **Decoder:** the pico-examples `quadrature_encoder` PIO program, loaded at offset 0 on **pio1**,
  state machine claimed with `pio_claim_unused_sm`. Joybus and other console backends use
  **pio0**. No CPU interrupts. Its instructions are embedded in `RotaryEncoderInput.cpp` and were
  verified against `pioasm`. CLK/DT must be consecutive GPIOs (DT = CLK + 1).
- **Cores:** core0 runs the backend, mode logic and joybus replies. Core1 (`loop1`) scans GPIO
  buttons and writes them into the shared `InputState`. `set_button()` is a non-atomic 64-bit
  read-modify-write, so **all `InputState` writes must stay on core1**. The encoder's
  `UpdateInputs()` (a FAST source, called by the backend on core0 once per poll) only increments a
  volatile poll counter. `Service()`, called from `loop1()` after the GPIO scan, reads the PIO
  count, accumulates detents and drives the virtual buttons.
- **Virtual buttons:** `BTN_LT7` (CW) and `BTN_LT8` (CCW). No config in the repo maps these to
  physical buttons (`lt3`/`lt4` are physical on Smashbox/Schism, so don't use those).
- **Pulse timing:** on GameCube/N64, counted in console polls (`encoder_pulse_polls` /
  `encoder_gap_polls`, default 2/2), because FAST sources are scanned exactly once per poll,
  40 us into it (`HAL/pico/src/comms/GamecubeBackend.cpp`). Core1 applies each step 150 us after
  the poll is counted so it lands after core0 has read `InputState` and well before the next poll.
  On USB backends (about 1 kHz reports) it uses milliseconds (`encoder_pulse_ms` /
  `encoder_gap_ms`, default 20/20). `setup()` picks the mode from `backends[0]->BackendId()`.
- **Detents:** `encoder_transitions_per_detent = 2` for the owner's KY-040 (4 made it register only
  every other detent). `encoder_max_pending = 4` caps queued detents. `encoder_reverse` swaps
  direction. No time-based debounce on A/B: the state table is bounce-tolerant.
- **Knob switch:** a normal GPIO button through `DebouncedGpioButtonInput` (eager 5 ms lockout).
- **Debug build:** `pico_encoder_debug` prints `count`, `detents`, `acc` and `pending` over USB
  serial at 115200, from `loop()` on core0, only when the backend is not GameCube/N64. Never use it
  on a console.

### Hard constraints

- Don't touch joybus timing or backend comms code (`HAL/pico/src/comms/*`, joybus-pio).
- Nothing added to core0's poll path beyond a few microseconds. No ISRs on core0.
- No dynamic allocation in hot paths. No time-based debounce on the encoder.
- No regressions for existing buttons, modes, USB, or other controller configs in the repo.

## 5. Status and next steps

Tested by the owner:
- USB (Xbox/XInput mode) works. Before the fix, the encoder registered only every other detent.

Not yet confirmed on hardware:
- The `transitions_per_detent = 2` fix (should be exactly one pulse per click).
- D-pad mod.
- Wii/GameCube over the GCC cable (encoder pulses, D-pad mod, joybus stability while spinning).

Likely next tasks:
1. Help the owner test each item above and adjust settings from what they report.
2. Tune pulse timing in Melee (the owner wants to tune it; try `encoder_pulse_polls` /
   `encoder_gap_polls` = 1/1 as well as 2/2).
3. Optional, only if asked: encoder pulses in other modes, or exposing the encoder's signed count
   to modes so a detent adjusts a value (propose a design first).

Acceptance checks to keep in mind: one detent = exactly one press at slow/normal/fast speeds; at
most `encoder_max_pending` trailing presses after a fast spin; no controller dropouts while
spinning for a minute with other inputs held; all other buttons behave like stock HayBox.

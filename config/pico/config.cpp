#include "comms/backend_init.hpp"
#include "config_defaults.hpp"
#include "core/CommunicationBackend.hpp"
#include "core/ControllerMode.hpp"
#include "core/KeyboardMode.hpp"
#include "core/Persistence.hpp"
#include "core/mode_selection.hpp"
#include "core/pinout.hpp"
#include "core/state.hpp"
#include "input/DebouncedGpioButtonInput.hpp"
#include "input/NunchukInput.hpp"
#include "input/RotaryEncoderInput.hpp"
#include "reboot.hpp"
#include "stdlib.hpp"

#include <config.pb.h>

Config config = default_config;

GpioButtonMapping button_mappings[] = {
    { BTN_LF1, 2  },
    { BTN_LF2, 3  },
    { BTN_LF3, 4  },
    { BTN_LF4, 5  },
    { BTN_LF5, 1  },

    { BTN_LT1, 6  },
    { BTN_LT2, 7  },

    { BTN_MB1, 0  },
    { BTN_MB2, 10 },
    { BTN_MB3, 11 },

    { BTN_RT1, 14 },
    { BTN_RT2, 15 },
    { BTN_RT3, 13 },
    { BTN_RT4, 12 },
    { BTN_RT5, 16 },

    { BTN_RF1, 26 },
    { BTN_RF2, 21 },
    { BTN_RF3, 19 },
    { BTN_RF4, 17 },

    { BTN_RF5, 27 },
    { BTN_RF6, 22 },
    { BTN_RF7, 20 },
    { BTN_RF8, 18 },
};
const size_t button_count = sizeof(button_mappings) / sizeof(GpioButtonMapping);

DebouncedGpioButtonInput<button_count> gpio_input(button_mappings);

/* D-pad mod: hold the knob press (GP1, BTN_LF5) to turn the left stick buttons into a D-pad, in
 * every controller mode. */
const DpadModConfig dpad_mod = {
    .modifier = BTN_LF5,
    .left = BTN_LF3,
    .right = BTN_LF1,
    .down = BTN_LF2,
    .up = BTN_RF4,
};

/* Rotary encoder (KY-040). CLK/DT must be consecutive GPIOs (PIO requirement). The knob's push
 * switch is the BTN_LF5 mapping on GP1 above (the D-pad mod) and is debounced like every other
 * button. */
const uint encoder_pin_a = 8; // CLK
const uint encoder_pin_b = 9; // DT
// Virtual buttons asserted for one clockwise/counter-clockwise detent. No config maps LT7/LT8 to a
// physical button. ControllerMode turns them into left stick up/down pulses in every mode (see
// encoder_stick below).
const Button encoder_cw_button = BTN_LT7;
const Button encoder_ccw_button = BTN_LT8;
// Pulse timing on joybus (GameCube/N64), counted in console polls: hold the press for
// encoder_pulse_polls polls, then keep it released for at least encoder_gap_polls polls.
const uint32_t encoder_pulse_polls = 2;
const uint32_t encoder_gap_polls = 2;
// Pulse timing on USB backends (they report at ~1kHz, so polls can't be used), in milliseconds.
const uint32_t encoder_pulse_ms = 20;
const uint32_t encoder_gap_ms = 20;
// Most detents that can be queued, so a fast spin doesn't leave a long tail of presses.
const int32_t encoder_max_pending = 4;
// Quadrature transitions per detent. This KY-040 does half a quadrature cycle per detent (2);
// use 4 for encoders that do a full cycle per detent.
const int32_t encoder_transitions_per_detent = 2;
// Swap this if clockwise comes out as counter-clockwise.
const bool encoder_reverse = false;

// Clockwise = left stick up, counter-clockwise = left stick down (D-pad Up/Down while the D-pad mod
// is held), in every controller mode.
const EncoderStickConfig encoder_stick = {
    .up = encoder_cw_button,
    .down = encoder_ccw_button,
};

RotaryEncoderInput encoder_input(
    encoder_pin_a,
    encoder_pin_b,
    encoder_cw_button,
    encoder_ccw_button,
    encoder_pulse_polls,
    encoder_gap_polls,
    encoder_pulse_ms,
    encoder_gap_ms,
    encoder_max_pending,
    encoder_transitions_per_detent,
    encoder_reverse
);

const Pinout pinout = {
    .joybus_data = 28,
    .nes_data = -1,
    .nes_clock = -1,
    .nes_latch = -1,
    .mux = -1,
    .nunchuk_detect = -1,
    .nunchuk_sda = -1,
    .nunchuk_scl = -1,
};

CommunicationBackend **backends = nullptr;
size_t backend_count;
KeyboardMode *current_kb_mode = nullptr;

void setup() {
    static InputState inputs;

    // Create GPIO input source and use it to read button states for checking button holds.
    gpio_input.UpdateInputs(inputs);

    // Check bootsel button hold as early as possible for safety.
    if (inputs.rt2) {
        reboot_bootloader();
    }

    // Turn on LED to indicate firmware booted.
    gpio_init(PICO_DEFAULT_LED_PIN);
    gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT);
    gpio_put(PICO_DEFAULT_LED_PIN, 1);

    // Attempt to load config, or write default config to flash if failed to load config.
    if (!persistence.LoadConfig(config)) {
        persistence.SaveConfig(config);
    }

    ControllerMode::SetDpadMod(dpad_mod);
    ControllerMode::SetEncoderStick(encoder_stick);

    // Create array of input sources to be used.
    // The encoder is FAST so the backend calls it once per poll, but it only counts polls there.
    // Its decoding and virtual button output happen on core1 (see loop1).
    static InputSource *input_sources[] = { &encoder_input };
    size_t input_source_count = sizeof(input_sources) / sizeof(InputSource *);

    backend_count =
        initialize_backends(backends, inputs, input_sources, input_source_count, config, pinout);

    setup_mode_activation_bindings(config.game_mode_configs, config.game_mode_configs_count);

    // Joybus backends scan FAST sources exactly once per console poll, so encoder pulses can be
    // counted in polls. Everything else (USB etc.) falls back to milliseconds.
    CommunicationBackendId backend_id = backends[0]->BackendId();
    encoder_input.SetTimingMode(
        backend_id == COMMS_BACKEND_GAMECUBE || backend_id == COMMS_BACKEND_N64
            ? EncoderTimingMode::POLLS
            : EncoderTimingMode::MILLISECONDS
    );
}

void loop() {
    select_mode(backends, backend_count, config);

    for (size_t i = 0; i < backend_count; i++) {
        backends[i]->SendReport();
    }

    if (current_kb_mode != nullptr) {
        current_kb_mode->SendReport(backends[0]->GetInputs());
    }

#ifdef ENCODER_DEBUG
    // Debug builds only: print encoder state over USB serial. Never on joybus, where this loop
    // must stay free to answer polls.
    CommunicationBackendId backend_id = backends[0]->BackendId();
    if (backend_id != COMMS_BACKEND_GAMECUBE && backend_id != COMMS_BACKEND_N64 && Serial) {
        static int32_t last_count = INT32_MIN;
        static int32_t last_pending = INT32_MIN;
        static uint32_t last_print_ms = 0;
        int32_t count = encoder_input.RawCount();
        int32_t pending = encoder_input.Pending();
        uint32_t now_ms = millis();
        if ((count != last_count || pending != last_pending) && now_ms - last_print_ms >= 5) {
            last_count = count;
            last_pending = pending;
            last_print_ms = now_ms;
            Serial.printf(
                "encoder ok=%d count=%ld detents=%ld acc=%ld pending=%ld\n",
                encoder_input.Initialized(),
                (long)count,
                (long)(count / encoder_transitions_per_detent),
                (long)encoder_input.Accumulator(),
                (long)pending
            );
        }
    }
#endif
}

/* Button inputs are read from the second core */

void setup1() {
    while (backends == nullptr) {
        tight_loop_contents();
    }
}

void loop1() {
    if (backends != nullptr) {
        gpio_input.UpdateInputs(backends[0]->GetInputs());
        // Same core as the GPIO buttons, so InputState writes never race between cores.
        encoder_input.Service(backends[0]->GetInputs());
    }
}

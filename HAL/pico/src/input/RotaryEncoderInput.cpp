#include "input/RotaryEncoderInput.hpp"

#include "util/state_util.hpp"

#include <hardware/gpio.h>
#include <hardware/pio.h>
#include <hardware/timer.h>

/*
 * quadrature_encoder PIO program from pico-examples (pio/quadrature_encoder/quadrature_encoder.pio,
 * Copyright (c) 2023 Raspberry Pi (Trading) Ltd., SPDX-License-Identifier: BSD-3-Clause),
 * assembled with pioasm. It must be loaded at offset 0 because it uses computed jumps. It keeps a
 * signed count of quadrature transitions in Y and continuously pushes it to the RX FIFO without
 * blocking. No CPU interrupts are involved.
 */
static const uint16_t quadrature_encoder_program_instructions[] = {
    0x000f, //  0: jmp    15        ; 00 -> 00
    0x000e, //  1: jmp    14        ; 00 -> 01 (decrement)
    0x0015, //  2: jmp    21        ; 00 -> 10 (increment)
    0x000f, //  3: jmp    15        ; 00 -> 11 (invalid)
    0x0015, //  4: jmp    21        ; 01 -> 00
    0x000f, //  5: jmp    15        ; 01 -> 01
    0x000f, //  6: jmp    15        ; 01 -> 10 (invalid)
    0x000e, //  7: jmp    14        ; 01 -> 11
    0x000e, //  8: jmp    14        ; 10 -> 00
    0x000f, //  9: jmp    15        ; 10 -> 01 (invalid)
    0x000f, // 10: jmp    15        ; 10 -> 10
    0x0015, // 11: jmp    21        ; 10 -> 11
    0x000f, // 12: jmp    15        ; 11 -> 00 (invalid)
    0x0015, // 13: jmp    21        ; 11 -> 01
    0x008f, // 14: jmp    y--, 15   ; 11 -> 10 / decrement
    0xa0c2, // 15: mov    isr, y    ; 11 -> 11 / .wrap_target
    0x8000, // 16: push   noblock
    0x60c2, // 17: out    isr, 2
    0x4002, // 18: in     pins, 2
    0xa0e6, // 19: mov    osr, isr
    0xa0a6, // 20: mov    pc, isr
    0xa04a, // 21: mov    y, !y     ; increment
    0x0097, // 22: jmp    y--, 23
    0xa04a, // 23: mov    y, !y     ; .wrap
};

static const struct pio_program quadrature_encoder_program = {
    .instructions = quadrature_encoder_program_instructions,
    .length = sizeof(quadrature_encoder_program_instructions) / sizeof(uint16_t),
    .origin = 0,
};

static constexpr uint quadrature_encoder_wrap_target = 15;
static constexpr uint quadrature_encoder_wrap = 23;

RotaryEncoderInput::RotaryEncoderInput(
    uint pin_a,
    uint pin_b,
    Button cw_button,
    Button ccw_button,
    uint32_t pulse_polls,
    uint32_t gap_polls,
    uint32_t pulse_ms,
    uint32_t gap_ms,
    int32_t max_pending,
    int32_t transitions_per_detent,
    bool reverse,
    PIO pio
) {
    _pio = pio;
    _cw_button = cw_button;
    _ccw_button = ccw_button;
    _pulse_polls = pulse_polls > 0 ? pulse_polls : 1;
    _gap_polls = gap_polls > 0 ? gap_polls : 1;
    _pulse_ms = pulse_ms > 0 ? pulse_ms : 1;
    _gap_ms = gap_ms > 0 ? gap_ms : 1;
    _max_pending = max_pending > 0 ? max_pending : 1;
    _transitions_per_detent = transitions_per_detent > 0 ? transitions_per_detent : 1;
    _reverse = reverse;

    // The PIO program samples both pins with a single IN, so they must be consecutive. If they are
    // not, or if the PIO block has no room, leave the encoder disabled rather than fight joybus for
    // resources.
    if (pin_b != pin_a + 1) {
        return;
    }
    if (!pio_can_add_program_at_offset(_pio, &quadrature_encoder_program, 0)) {
        return;
    }
    _sm = pio_claim_unused_sm(_pio, false);
    if (_sm < 0) {
        return;
    }
    pio_add_program_at_offset(_pio, &quadrature_encoder_program, 0);

    pio_sm_set_consecutive_pindirs(_pio, _sm, pin_a, 2, false);
    pio_gpio_init(_pio, pin_a);
    pio_gpio_init(_pio, pin_b);
    // The KY-040 has 10k pull-ups to its + pin already. The internal ones are harmless extra.
    gpio_pull_up(pin_a);
    gpio_pull_up(pin_b);

    pio_sm_config c = pio_get_default_sm_config();
    sm_config_set_wrap(&c, quadrature_encoder_wrap_target, quadrature_encoder_wrap);
    sm_config_set_in_pins(&c, pin_a);
    sm_config_set_jmp_pin(&c, pin_a);
    // Shift left, no autopush/autopull.
    sm_config_set_in_shift(&c, false, false, 32);
    sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_NONE);
    // Run at full speed. The worst-case loop is 10 cycles, far faster than any hand-turned knob.
    sm_config_set_clkdiv(&c, 1.0f);

    pio_sm_init(_pio, _sm, 0, &c);
    pio_sm_set_enabled(_pio, _sm, true);

    _last_count = ReadCount();
    _raw_count = _last_count;
    _initialized = true;
}

InputScanSpeed RotaryEncoderInput::ScanSpeed() {
    return InputScanSpeed::FAST;
}

bool RotaryEncoderInput::Initialized() {
    return _initialized;
}

void RotaryEncoderInput::SetTimingMode(EncoderTimingMode mode) {
    _timing_mode = mode;
}

void RotaryEncoderInput::UpdateInputs(InputState &inputs) {
    // Runs on core0 inside the joybus reply window: keep it to a single increment. InputState is
    // deliberately not touched here (see header).
    _poll_count = _poll_count + 1;
}

int32_t RotaryEncoderInput::ReadCount() {
    // Drain stale samples, then take one fresh sample. The SM pushes a new value at least every
    // 10 PIO cycles, so the blocking read waits at most ~50ns.
    uint level = pio_sm_get_rx_fifo_level(_pio, _sm);
    while (level-- > 0) {
        pio_sm_get(_pio, _sm);
    }
    return (int32_t)pio_sm_get_blocking(_pio, _sm);
}

void RotaryEncoderInput::Decode() {
    int32_t count = ReadCount();
    // Unsigned subtraction handles counter wraparound.
    int32_t delta = (int32_t)((uint32_t)count - (uint32_t)_last_count);
    _last_count = count;
    _raw_count = count;
    if (delta == 0) {
        return;
    }
    if (_reverse) {
        delta = -delta;
    }

    // The PIO state table already discards invalid transitions and contact bounce nets to zero, so
    // no time-based filtering is needed here: one detent = transitions_per_detent counts.
    int32_t accumulator = _accumulator + delta;
    int32_t pending = _pending;
    while (accumulator >= _transitions_per_detent) {
        accumulator -= _transitions_per_detent;
        if (pending < _max_pending) {
            pending++;
        }
    }
    while (accumulator <= -_transitions_per_detent) {
        accumulator += _transitions_per_detent;
        if (pending > -_max_pending) {
            pending--;
        }
    }
    _accumulator = accumulator;
    _pending = pending;
}

void RotaryEncoderInput::StepPulse(
    InputState &inputs,
    uint32_t now,
    uint32_t pulse_len,
    uint32_t gap_len
) {
    if (_active) {
        // Hold for pulse_len, then release. Never reverse or swap direction mid-pulse.
        if (now - _mark >= pulse_len) {
            set_button(inputs.buttons, _direction > 0 ? _cw_button : _ccw_button, false);
            _active = false;
            _released_once = true;
            _mark = now;
        }
        return;
    }

    int32_t pending = _pending;
    if (pending == 0) {
        return;
    }
    if (_released_once && now - _mark < gap_len) {
        return;
    }

    _direction = pending > 0 ? 1 : -1;
    _pending = pending - _direction;
    // Only one of CW/CCW is ever asserted, because a new pulse only starts after the previous one
    // has been released.
    set_button(inputs.buttons, _direction > 0 ? _cw_button : _ccw_button, true);
    _active = true;
    _mark = now;
}

void RotaryEncoderInput::Service(InputState &inputs) {
    if (!_initialized) {
        return;
    }

    Decode();

    if (_timing_mode == EncoderTimingMode::MILLISECONDS) {
        StepPulse(inputs, to_ms_since_boot(get_absolute_time()), _pulse_ms, _gap_ms);
        return;
    }

    // Poll mode: advance the pulse state machine exactly once per counted poll, a fixed delay
    // after the poll so the change never races core0's read of InputState for that poll.
    uint32_t poll_count = _poll_count;
    uint32_t now_us = time_us_32();
    if (poll_count != _seen_poll_count) {
        _seen_poll_count = poll_count;
        _seen_poll_time_us = now_us;
        _poll_step_due = true;
    }
    if (_poll_step_due && now_us - _seen_poll_time_us >= poll_apply_delay_us) {
        _poll_step_due = false;
        StepPulse(inputs, poll_count, _pulse_polls, _gap_polls);
    }
}

int32_t RotaryEncoderInput::RawCount() {
    return _raw_count;
}

int32_t RotaryEncoderInput::Accumulator() {
    return _accumulator;
}

int32_t RotaryEncoderInput::Pending() {
    return _pending;
}

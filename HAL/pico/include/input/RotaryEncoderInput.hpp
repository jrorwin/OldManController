#ifndef _INPUT_ROTARYENCODERINPUT_HPP
#define _INPUT_ROTARYENCODERINPUT_HPP

#include "core/InputSource.hpp"
#include "core/state.hpp"
#include "stdlib.hpp"

#include <config.pb.h>
#include <hardware/pio.h>

/*
 * Quadrature rotary encoder (e.g. KY-040) that turns each detent into one virtual button press.
 *
 * Decoding is done by the pico-examples quadrature_encoder PIO program, so there are no CPU
 * interrupts at all. The work is split across the two cores:
 *
 * - UpdateInputs() is a FAST input source, called by the backend on core0. In poll timing mode it
 *   only counts console polls and never touches InputState, because the GPIO buttons are written
 *   to the same InputState from core1 and InputState writes are non-atomic read-modify-writes.
 * - Service() must be called from core1 (loop1), after the GPIO button scan. It reads the PIO
 *   count, converts it to detents and runs the pulse state machine that sets/clears the CW and CCW
 *   virtual buttons. Since all InputState writes happen on core1, there is no cross-core race.
 *
 * Pulse timing can be counted in console polls (exact, for joybus backends where FAST sources are
 * scanned exactly once per poll) or in milliseconds (for USB backends, which report at ~1kHz).
 */

enum class EncoderTimingMode {
    POLLS,
    MILLISECONDS,
};

class RotaryEncoderInput : public InputSource {
  public:
    // pin_b must be pin_a + 1, because the PIO program reads both pins with a single IN.
    RotaryEncoderInput(
        uint pin_a,
        uint pin_b,
        Button cw_button,
        Button ccw_button,
        uint32_t pulse_polls,
        uint32_t gap_polls,
        uint32_t pulse_ms,
        uint32_t gap_ms,
        int32_t max_pending = 4,
        int32_t transitions_per_detent = 4,
        bool reverse = false,
        PIO pio = pio1
    );
    InputScanSpeed ScanSpeed();

    // Core0 (backend). Counts polls only.
    void UpdateInputs(InputState &inputs);

    // Core1 (loop1). Decodes and drives the virtual buttons.
    void Service(InputState &inputs);

    // Call once after the backend is known, before core1 starts calling Service().
    void SetTimingMode(EncoderTimingMode mode);

    bool Initialized();

    // Debug accessors (safe to read from either core; values may be one update stale).
    int32_t RawCount();
    int32_t Accumulator();
    int32_t Pending();

  private:
    // In poll mode, wait this long after a poll is counted before changing the virtual buttons,
    // so the change always lands after core0 has read InputState for that poll and well before
    // the next poll (a joybus poll + response takes >300us).
    static constexpr uint32_t poll_apply_delay_us = 150;

    PIO _pio;
    int _sm = -1;
    bool _initialized = false;

    Button _cw_button;
    Button _ccw_button;
    uint32_t _pulse_polls;
    uint32_t _gap_polls;
    uint32_t _pulse_ms;
    uint32_t _gap_ms;
    int32_t _max_pending;
    int32_t _transitions_per_detent;
    bool _reverse;

    EncoderTimingMode _timing_mode = EncoderTimingMode::MILLISECONDS;

    // Written by core0, read by core1.
    volatile uint32_t _poll_count = 0;

    // Core1 state. Marked volatile only so the debug accessors can read them from core0.
    volatile int32_t _raw_count = 0;
    volatile int32_t _accumulator = 0;
    volatile int32_t _pending = 0;

    int32_t _last_count = 0;
    uint32_t _seen_poll_count = 0;
    uint32_t _seen_poll_time_us = 0;
    bool _poll_step_due = false;

    bool _active = false;
    int8_t _direction = 0;
    uint32_t _mark = 0;
    bool _released_once = false;

    int32_t ReadCount();
    void Decode();
    void StepPulse(InputState &inputs, uint32_t now, uint32_t pulse_len, uint32_t gap_len);
};

#endif

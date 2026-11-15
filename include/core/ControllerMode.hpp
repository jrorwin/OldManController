#ifndef _CORE_CONTROLLERMODE_HPP
#define _CORE_CONTROLLERMODE_HPP

#include "core/InputMode.hpp"
#include "core/socd.hpp"
#include "core/state.hpp"

#include <config.pb.h>

// D-pad mod: while the modifier button is held, the four left stick direction buttons output D-pad
// directions instead (in every controller mode) and the stick stays centred. Disabled unless a
// config calls ControllerMode::SetDpadMod().
typedef struct {
    Button modifier;
    Button left;
    Button right;
    Button down;
    Button up;
} DpadModConfig;

// Encoder stick pulses: while the CW/CCW virtual button is pressed, the left stick is pushed fully
// up/down (at the current mode's own full deflection), in every controller mode. While the D-pad
// mod is held they press D-pad Up/Down instead. Disabled unless a config calls
// ControllerMode::SetEncoderStick().
typedef struct {
    Button up;
    Button down;
} EncoderStickConfig;

class ControllerMode : public InputMode {
  public:
    ControllerMode();
    static void SetDpadMod(const DpadModConfig &dpad_mod);
    static void SetEncoderStick(const EncoderStickConfig &encoder_stick);
    void UpdateOutputs(const InputState &inputs, OutputState &outputs);
    void ResetDirections();
    virtual void UpdateDirections(
        bool lsLeft,
        bool lsRight,
        bool lsDown,
        bool lsUp,
        bool rsLeft,
        bool rsRight,
        bool rsDown,
        bool rsUp,
        uint8_t analogStickMin,
        uint8_t analogStickNeutral,
        uint8_t analogStickMax,
        OutputState &outputs
    );

  protected:
    StickDirections directions;

  private:
    static DpadModConfig _dpad_mod;
    static EncoderStickConfig _encoder_stick;

    // Left stick range last passed to UpdateDirections() by the mode, used for encoder pulses.
    uint8_t _stick_min = 48;
    uint8_t _stick_max = 208;

    virtual void UpdateDigitalOutputs(const InputState &inputs, OutputState &outputs) = 0;
    virtual void UpdateAnalogOutputs(const InputState &inputs, OutputState &outputs) = 0;
};

#endif

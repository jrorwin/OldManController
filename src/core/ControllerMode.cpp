#include "core/ControllerMode.hpp"

#include "util/state_util.hpp"

DpadModConfig ControllerMode::_dpad_mod = {
    .modifier = BTN_UNSPECIFIED,
    .left = BTN_UNSPECIFIED,
    .right = BTN_UNSPECIFIED,
    .down = BTN_UNSPECIFIED,
    .up = BTN_UNSPECIFIED,
};

ControllerMode::ControllerMode() : InputMode() {
    // Set up initial state.
    ResetDirections();
}

void ControllerMode::SetDpadMod(const DpadModConfig &dpad_mod) {
    _dpad_mod = dpad_mod;
}

void ControllerMode::UpdateOutputs(const InputState &inputs, OutputState &outputs) {
    // D-pad mod works on the physical buttons, before remapping, SOCD and mode logic, so it behaves
    // the same in every mode. The modifier itself is never passed on to the mode.
    InputState physical_inputs = inputs;
    bool dpad_left = false;
    bool dpad_right = false;
    bool dpad_down = false;
    bool dpad_up = false;
    if (_dpad_mod.modifier != BTN_UNSPECIFIED) {
        if (get_button(physical_inputs.buttons, _dpad_mod.modifier)) {
            dpad_left = get_button(physical_inputs.buttons, _dpad_mod.left);
            dpad_right = get_button(physical_inputs.buttons, _dpad_mod.right);
            dpad_down = get_button(physical_inputs.buttons, _dpad_mod.down);
            dpad_up = get_button(physical_inputs.buttons, _dpad_mod.up);
            set_button(physical_inputs.buttons, _dpad_mod.left, false);
            set_button(physical_inputs.buttons, _dpad_mod.right, false);
            set_button(physical_inputs.buttons, _dpad_mod.down, false);
            set_button(physical_inputs.buttons, _dpad_mod.up, false);
        }
        set_button(physical_inputs.buttons, _dpad_mod.modifier, false);
    }

    // Create a copy of the input state here so remapping can be many-to-one (many physical buttons
    // to one activated button).
    InputState remapped_inputs = physical_inputs;
    HandleRemap(physical_inputs, remapped_inputs);
    HandleSocd(remapped_inputs);
    UpdateDigitalOutputs(remapped_inputs, outputs);
    UpdateAnalogOutputs(remapped_inputs, outputs);

    // Opposite D-pad directions cancel out.
    if (dpad_left != dpad_right) {
        outputs.dpadLeft = outputs.dpadLeft || dpad_left;
        outputs.dpadRight = outputs.dpadRight || dpad_right;
    }
    if (dpad_down != dpad_up) {
        outputs.dpadDown = outputs.dpadDown || dpad_down;
        outputs.dpadUp = outputs.dpadUp || dpad_up;
    }
}

void ControllerMode::ResetDirections() {
    directions = {
        .horizontal = false,
        .vertical = false,
        .diagonal = false,
        .x = 0,
        .y = 0,
        .cx = 0,
        .cy = 0,
    };
}

void ControllerMode::UpdateDirections(
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
) {
    ResetDirections();

    outputs.leftStickX = analogStickNeutral;
    outputs.leftStickY = analogStickNeutral;
    outputs.rightStickX = analogStickNeutral;
    outputs.rightStickY = analogStickNeutral;

    // Coordinate calculations to make modifier handling simpler.
    if (lsLeft || lsRight) {
        directions.horizontal = true;
        if (lsLeft) {
            directions.x = -1;
            outputs.leftStickX = analogStickMin;
        } else {
            directions.x = 1;
            outputs.leftStickX = analogStickMax;
        }
    }
    if (lsDown || lsUp) {
        directions.vertical = true;
        if (lsDown) {
            directions.y = -1;
            outputs.leftStickY = analogStickMin;
        } else {
            directions.y = 1;
            outputs.leftStickY = analogStickMax;
        }
    }
    if (directions.horizontal && directions.vertical)
        directions.diagonal = true;

    if (rsLeft || rsRight) {
        if (rsLeft) {
            directions.cx = -1;
            outputs.rightStickX = analogStickMin;
        } else {
            directions.cx = 1;
            outputs.rightStickX = analogStickMax;
        }
    }
    if (rsDown || rsUp) {
        if (rsDown) {
            directions.cy = -1;
            outputs.rightStickY = analogStickMin;
        } else {
            directions.cy = 1;
            outputs.rightStickY = analogStickMax;
        }
    }
}

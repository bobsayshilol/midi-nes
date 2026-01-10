#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

//
// This library intends to reproduce the MIDI output of the basicNES 2000 emulator.
// The code itself is based on the code in olafnes 1.1c, which is a continuation of basicNES 2000.
//

struct MIDINES_State;

// Open a MIDI device ready to be used as a NES output.
MIDINES_State* midines_open();

// Close the MIDI device.
void midines_close(MIDINES_State* state);

// Perform a write to an APU address.
// Any addresses outside of the APU address range (0x4000:0x4017) will be ignored.
bool midines_write(MIDINES_State* state, uint16_t addr, uint8_t value);

// Update the current playback.
// Should be called at the end of each frame.
void midines_update(MIDINES_State* state);

#ifdef __cplusplus
}
#endif

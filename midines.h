#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

//
// This library intends to reproduce the MIDI output of the basicNES 2000 emulator.
// The code itself is based on the code in olafnes 1.1c, which is a continuation of basicNES 2000.
//

typedef struct MIDINES_State MIDINES_State;

// Open a MIDI device ready to be used as a NES output.
MIDINES_State* midines_open();

// Close the MIDI device.
void midines_close(MIDINES_State* state);

// Set the MIDI instruments for each channel (0-based).
// See https://en.wikipedia.org/wiki/General_MIDI#Program_change_events.
bool midines_set_instruments(MIDINES_State* state, uint8_t pulse1, uint8_t pulse2, uint8_t triangle, uint8_t noise);

// Adjust the fixed volumes of the triangle and noise channels.
// Volumes should be <=127.
void midines_set_volumes(MIDINES_State* state, uint8_t triangle, uint8_t noise);

// Perform a write to an APU address.
// Any addresses outside of the APU address range (0x4000:0x4017) will be ignored.
bool midines_write(MIDINES_State* state, uint16_t addr, uint8_t value);

// Update the current playback.
// Should be called at the end of each frame.
void midines_update(MIDINES_State* state);

#ifdef __cplusplus
}
#endif

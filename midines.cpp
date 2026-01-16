#include "midines.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#ifdef __linux__
#include <alsa/asoundlib.h>
struct Handle {
    snd_seq_t* seq;
    int port;
    snd_midi_event_t* coder;
};
#else
#include <windows.h>
using Handle = HMIDIOUT;
#endif

#define NUM_CHANNELS 4

struct MIDINES_State {
    uint8_t registers[NUM_CHANNELS * 4];
    uint8_t channels_enabled;

    uint8_t pending_write;
    uint8_t channel_timer[NUM_CHANNELS];
    uint8_t tones[NUM_CHANNELS];
    uint8_t volumes[NUM_CHANNELS];
    uint8_t stop_tones[NUM_CHANNELS];

    uint8_t triangle_volume;
    uint8_t noise_volume;

    Handle handle;

    bool silent;
};

namespace {

const uint8_t CH_Pulse1 = 0;
const uint8_t CH_Pulse2 = 1;
const uint8_t CH_Triangle = 2;
const uint8_t CH_Noise = 3;

//
// MIDI
//

bool midi_open(Handle& handle) {
#ifdef __linux__
    if (snd_seq_open(&handle.seq, "default", SND_SEQ_OPEN_OUTPUT, 0) < 0) {
        return false;
    }
    snd_seq_set_client_name(handle.seq, "MIDINES");
    handle.port = snd_seq_create_simple_port(handle.seq, "midines port", SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_WRITE, SND_SEQ_PORT_TYPE_APPLICATION);
    if (handle.port < 0) {
        return false;
    }
    if (snd_midi_event_new(3 /* max message size */, &handle.coder) < 0) {
        return false;
    }
    snd_midi_event_init(handle.coder);
    snd_midi_event_no_status(handle.coder, 1);
    return true;
#else
    return midiOutOpen(&handle, 0, 0, 0, 0) == MMSYSERR_NOERROR;
#endif
}

void midi_close(Handle& handle) {
#ifdef __linux__
    snd_midi_event_free(handle.coder);
    snd_seq_close(handle.seq);
#else
    midiOutClose(handle);
#endif
}

void midi_send(Handle& handle, uint32_t msg) {
#ifdef __linux__
    // Copied from the RtMidi project.
    snd_seq_event_t evt;
    snd_seq_ev_clear(&evt);
    snd_seq_ev_set_direct(&evt);
    snd_seq_ev_set_dest(&evt, 128, 0); // TODO: server port should be configurable
    snd_midi_event_encode(handle.coder, (const uint8_t*)&msg, sizeof(msg), &evt);
    snd_seq_event_output(handle.seq, &evt);
    snd_seq_drain_output(handle.seq);
#else
    midiOutShortMsg(handle, msg);
#endif
}



//
// Utils
//

void select_instrument(Handle& handle, uint8_t channel, uint8_t patch) {
    midi_send(handle, 0xC0 | channel | (static_cast<uint32_t>(patch) << 8));
}

void tone_off(Handle& handle, uint8_t channel, uint8_t tone) {
    tone = std::clamp<uint8_t>(tone, 0U, 127U);
    midi_send(handle, 0x80 | channel | (static_cast<uint32_t>(tone) << 8));
}

void tone_on(Handle& handle, uint8_t channel, uint8_t tone, uint8_t volume) {
    tone = std::clamp<uint8_t>(tone, 0U, 127U);
    volume = std::clamp<uint8_t>(volume, 0U, 127U);
    midi_send(handle, 0x90 | channel | (static_cast<uint32_t>(tone) << 8) | (static_cast<uint32_t>(volume) << 16));
}

uint8_t get_tone(uint32_t freq) {
    freq = 111861 / (freq + 1);
    return std::log(freq / 8.176) * 17.31234;
}



//
// Playback
//

const uint8_t vlengths[32] = {
      5, 127,  10,   1,  19,   2,  40,   3,
     80,   4,  30,   5,   7,   6,  13,   7,
      6,   8,  12,   9,  24,  10,  48,  11,
     96,  12,  36,  13,   8,  14,  16,  15,
};

void playback_start_tone(MIDINES_State* state, uint8_t channel, uint8_t tone, uint8_t volume) {
    uint8_t& current_tone = state->tones[channel];
    uint8_t& current_volume = state->volumes[channel];

    // Stop current playback if there's a change.
    // TODO: magic number here
    if (tone != current_tone || volume + 3 * 8 < current_volume || volume > current_volume || volume == 0) {
        tone_off(state->handle, channel, current_tone);
        current_tone = 0;
        current_volume = 0;
    }

    if (tone > 0 && tone <= 127 && volume > 0) {
        current_volume = volume;
        current_tone = tone;
        tone_on(state->handle, channel, tone, volume);
    }
}

void playback_stop_tone(MIDINES_State* state, uint8_t channel) {
    // Note: the tone won't be stopped until playback_apply_stops() is called (end of frame).
    uint8_t& tone = state->tones[channel];
    if (tone != 0) {
        state->stop_tones[channel] = tone;
        tone = 0;
        state->volumes[channel] = 0;
    }
}

void playback_apply_stops(MIDINES_State* state) {
    for (int channel = 0; channel < NUM_CHANNELS; channel++) {
        uint8_t& stop_tone = state->stop_tones[channel];
        if (stop_tone != 0 && stop_tone != state->tones[channel]) {
            tone_off(state->handle, channel, stop_tone);
            stop_tone = 0;
        }
    }
}

void playback_kill_all(MIDINES_State* state) {
    for (int channel = 0; channel < NUM_CHANNELS; channel++) {
        playback_stop_tone(state, channel);
    }
    playback_apply_stops(state);
}

void playback_channel_common(MIDINES_State* state, uint8_t channel, uint8_t volume, uint32_t freq, uint8_t len) {
    if (state->silent) volume = 0;

    const uint8_t channel_bit = 1 << channel;
    if (state->channels_enabled & channel_bit) {
        if (volume > 0 && freq > 1) {
            if (state->pending_write & channel_bit) {
                state->pending_write &= ~channel_bit;
                state->channel_timer[channel] = len + 1;
                playback_start_tone(state, channel, get_tone(freq), volume);
            }
        } else {
            playback_stop_tone(state, channel);
        }
    } else {
        state->pending_write |= channel_bit;
        playback_stop_tone(state, channel);
    }

    // Note: this assumes that we get called once a frame.
    uint8_t& timer = state->channel_timer[channel];
    if (timer > 0 && --timer == 0) {
        playback_stop_tone(state, channel);
    }
}

void playback_noise(MIDINES_State* state, uint8_t channel) {
    const uint8_t volume = state->noise_volume;

    const uint8_t b3 = state->registers[channel * 4 + 3];
    const uint8_t len = vlengths[b3 >> 3];

    const uint8_t b2 = state->registers[channel * 4 + 2];
    const uint32_t freq = (b2 & 0x0f) * 128;

    playback_channel_common(state, channel, volume, freq, len);
}

void playback_pulse(MIDINES_State* state, uint8_t channel) {
    const uint8_t b0 = state->registers[channel * 4 + 0];
    const uint8_t volume = (b0 & 0x0f) << 3; // 127 is max, 0xf<<3 is 120

    const uint8_t b3 = state->registers[channel * 4 + 3];
    const uint8_t len = vlengths[b3 >> 3];

    const uint8_t b2 = state->registers[channel * 4 + 2];
    const uint32_t freq = b2 + (b3 & 0x07) * 256;

    playback_channel_common(state, channel, volume, freq, len);
}

void playback_triangle(MIDINES_State* state, uint8_t channel) {
    const uint8_t volume = state->triangle_volume;

    const uint8_t b3 = state->registers[channel * 4 + 3];
    const uint8_t len = vlengths[b3 >> 3];

    const uint8_t b2 = state->registers[channel * 4 + 2];
    const uint32_t freq = b2 + (b3 & 0x07) * 256;

    playback_channel_common(state, channel, volume, freq, len);
}

} // namespace

MIDINES_State* midines_open() {
    MIDINES_State* state = (MIDINES_State*)calloc(1, sizeof(MIDINES_State));
    if (!state || !midi_open(state->handle)) {
        free(state);
        return nullptr;
    }

    // Set default MIDI Program Numbers.
    const uint8_t PN_Pulse = 80;    // Synth Lead 1
    const uint8_t PN_Triangle = 74; // Recorder
    const uint8_t PN_Noise = 127;   // Gunshot
    midines_set_instruments(state, PN_Pulse, PN_Pulse, PN_Triangle, PN_Noise);

    // Set default volumes.
    midines_set_volumes(state, 100, 64);
    return state;
}

void midines_close(MIDINES_State* state) {
    if (!state) return;

    playback_kill_all(state);
    midi_close(state->handle);
    free(state);
}

bool midines_set_instruments(MIDINES_State* state, uint8_t pulse1, uint8_t pulse2, uint8_t triangle, uint8_t noise) {
    if (!state) return false;
    if ((pulse1 | pulse2 | triangle | noise) > 127) return false;

    select_instrument(state->handle, CH_Pulse1, pulse1);
    select_instrument(state->handle, CH_Pulse2, pulse2);
    select_instrument(state->handle, CH_Triangle, triangle);
    select_instrument(state->handle, CH_Noise, noise);
    return true;
}

void midines_set_volumes(MIDINES_State* state, uint8_t triangle, uint8_t noise) {
    if (!state) return;
    if ((triangle | noise) > 127) return;

    state->triangle_volume = triangle;
    state->noise_volume = noise;
}

bool midines_write(MIDINES_State* state, uint16_t addr, uint8_t value) {
    if (!state) return false;

    const uint16_t offset = addr - 0x4000;
    if (offset < NUM_CHANNELS * 4) {
        // 0x4000:0x4013 - channel write
        state->registers[offset] = value;
        const uint8_t channel = offset / 4;
        state->pending_write |= 1 << channel;
    } else if (offset == 0x15) {
        // 0x4015 - enable/disable channels
        state->channels_enabled = value;
    }

    return true;
}

void midines_update(MIDINES_State* state) {
    if (!state) return;

    playback_apply_stops(state);
    playback_pulse(state, CH_Pulse1);
    playback_pulse(state, CH_Pulse2);
    playback_triangle(state, CH_Triangle);
    playback_noise(state, CH_Noise);
}

void midines_set_silent(MIDINES_State* state, bool silent) {
    if (!state) return;

    state->silent = silent;
}

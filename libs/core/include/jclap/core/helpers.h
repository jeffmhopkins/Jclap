#pragma once

// Small helpers most plugins need: building param infos, writing events, audio pass-through and
// reading/writing state streams.

#include <clap/clap.h>

#include <cstdint>

namespace jclap::core {

// ---- params ----

struct ParamDef {
   clap_id id;
   const char *name;
   const char *module; // host-side grouping, e.g. "Filter"; may be ""
   double min, max, defaultValue;
   clap_param_info_flags flags = CLAP_PARAM_IS_AUTOMATABLE;
};

void fillParamInfo(const ParamDef &def, clap_param_info_t *info);

// Reads a number from the start of `text` ("0.5", " -12 dB") the same way in every locale, so a
// '.' is always the decimal point. False for no number or for NaN and infinity. `end`, if given,
// is set to the first character after the number, for reading a unit.
bool parseNumber(const char *text, double &value, const char **end = nullptr);

// ---- events ----

// True for note and MIDI events, which a note effect usually passes straight through.
bool isNoteEvent(const clap_event_header_t *header);

bool pushParamValue(const clap_output_events_t *out, clap_id id, double value, uint32_t time = 0);
bool pushParamGesture(const clap_output_events_t *out, clap_id id, bool begin, uint32_t time = 0);
// A 3-byte MIDI message on note port 0.
bool pushMidi(const clap_output_events_t *out, uint32_t time, uint8_t status, uint8_t data1, uint8_t data2);

// ---- audio ----

// Copies the first input port to the first output port (32 or 64 bit, converting if they differ),
// silencing extra output channels, or every output channel with no input. Passes the input's
// constant_mask on. For utilities that only have audio ports because hosts expect them.
void passAudioThrough(const clap_process_t *process);

// ---- state ----

// Write or read exactly `size` bytes, looping over short reads/writes.
bool writeAll(const clap_ostream_t *stream, const void *data, int64_t size);
bool readAll(const clap_istream_t *stream, void *data, int64_t size);

} // namespace jclap::core

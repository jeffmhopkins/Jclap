#include "jclap/core/helpers.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace jclap::core {

void fillParamInfo(const ParamDef &def, clap_param_info_t *info) {
   std::memset(info, 0, sizeof(*info));
   info->id = def.id;
   info->flags = def.flags;
   std::snprintf(info->name, sizeof(info->name), "%s", def.name);
   std::snprintf(info->module, sizeof(info->module), "%s", def.module ? def.module : "");
   info->min_value = def.min;
   info->max_value = def.max;
   info->default_value = def.defaultValue;
}

bool isNoteEvent(const clap_event_header_t *h) {
   if (h->space_id != CLAP_CORE_EVENT_SPACE_ID)
      return false;
   switch (h->type) {
   case CLAP_EVENT_NOTE_ON:
   case CLAP_EVENT_NOTE_OFF:
   case CLAP_EVENT_NOTE_CHOKE:
   case CLAP_EVENT_NOTE_END:
   case CLAP_EVENT_NOTE_EXPRESSION:
   case CLAP_EVENT_MIDI:
   case CLAP_EVENT_MIDI_SYSEX:
   case CLAP_EVENT_MIDI2:
      return true;
   default:
      return false;
   }
}

bool pushParamValue(const clap_output_events_t *out, clap_id id, double value, uint32_t time) {
   clap_event_param_value_t ev{};
   ev.header = {sizeof(ev), time, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_PARAM_VALUE, 0};
   ev.param_id = id;
   ev.note_id = -1;
   ev.port_index = -1;
   ev.channel = -1;
   ev.key = -1;
   ev.value = value;
   return out->try_push(out, &ev.header);
}

bool pushParamGesture(const clap_output_events_t *out, clap_id id, bool begin, uint32_t time) {
   clap_event_param_gesture_t ev{};
   ev.header = {sizeof(ev), time, CLAP_CORE_EVENT_SPACE_ID,
                static_cast<uint16_t>(begin ? CLAP_EVENT_PARAM_GESTURE_BEGIN : CLAP_EVENT_PARAM_GESTURE_END), 0};
   ev.param_id = id;
   return out->try_push(out, &ev.header);
}

bool pushMidi(const clap_output_events_t *out, uint32_t time, uint8_t status, uint8_t data1, uint8_t data2) {
   clap_event_midi_t ev{};
   ev.header = {sizeof(ev), time, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_MIDI, 0};
   ev.port_index = 0;
   ev.data[0] = status;
   ev.data[1] = data1;
   ev.data[2] = data2;
   return out->try_push(out, &ev.header);
}

void passAudioThrough(const clap_process_t *proc) {
   if (proc->audio_inputs_count < 1 || proc->audio_outputs_count < 1)
      return;
   const clap_audio_buffer_t &in = proc->audio_inputs[0];
   const clap_audio_buffer_t &out = proc->audio_outputs[0];
   const uint32_t channels = std::min(in.channel_count, out.channel_count);
   for (uint32_t c = 0; c < channels; ++c) {
      if (in.data32 && out.data32 && in.data32[c] != out.data32[c])
         std::memcpy(out.data32[c], in.data32[c], proc->frames_count * sizeof(float));
      if (in.data64 && out.data64 && in.data64[c] != out.data64[c])
         std::memcpy(out.data64[c], in.data64[c], proc->frames_count * sizeof(double));
   }
   for (uint32_t c = channels; c < out.channel_count; ++c) {
      if (out.data32)
         std::memset(out.data32[c], 0, proc->frames_count * sizeof(float));
      if (out.data64)
         std::memset(out.data64[c], 0, proc->frames_count * sizeof(double));
   }
}

bool writeAll(const clap_ostream_t *stream, const void *data, int64_t size) {
   const char *buf = static_cast<const char *>(data);
   while (size > 0) {
      const int64_t written = stream->write(stream, buf, static_cast<uint64_t>(size));
      if (written <= 0)
         return false;
      buf += written;
      size -= written;
   }
   return true;
}

bool readAll(const clap_istream_t *stream, void *data, int64_t size) {
   char *buf = static_cast<char *>(data);
   while (size > 0) {
      const int64_t got = stream->read(stream, buf, static_cast<uint64_t>(size));
      if (got <= 0)
         return false;
      buf += got;
      size -= got;
   }
   return true;
}

} // namespace jclap::core

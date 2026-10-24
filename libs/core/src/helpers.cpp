#include "jclap/core/helpers.h"

#include <algorithm>
#include <cstdio>
#include <cmath>
#include <cstring>
#include <locale>
#include <sstream>
#include <string>

namespace jclap::core {

void fillParamInfo(const ParamDef &def, clap_param_info_t *info) {
   std::memset(info, 0, sizeof(*info));
   info->id = def.id;
   info->flags = def.flags;
   std::snprintf(info->name, sizeof(info->name), "%s", def.name ? def.name : "");
   std::snprintf(info->module, sizeof(info->module), "%s", def.module ? def.module : "");
   info->min_value = def.min;
   info->max_value = def.max;
   info->default_value = def.defaultValue;
}

bool parseNumber(const char *text, double &value, const char **end) {
   if (!text)
      return false;
   // Find the number's extent first, [+-]digits[.digits][e[+-]digits], so a unit after it ("3dB")
   // is never taken for part of it, then read just that in the C locale.
   const char *p = text;
   while (*p == ' ' || *p == '\t')
      ++p;
   const char *start = p;
   if (*p == '+' || *p == '-')
      ++p;
   int digits = 0;
   for (; *p >= '0' && *p <= '9'; ++p)
      ++digits;
   if (*p == '.')
      for (++p; *p >= '0' && *p <= '9'; ++p)
         ++digits;
   if (!digits)
      return false;
   if (*p == 'e' || *p == 'E') {
      const char *q = p + 1;
      if (*q == '+' || *q == '-')
         ++q;
      if (*q >= '0' && *q <= '9') {
         while (*q >= '0' && *q <= '9')
            ++q;
         p = q;
      }
   }
   std::istringstream in(std::string(start, p));
   in.imbue(std::locale::classic());
   double v = 0.0;
   if (!(in >> v) || !std::isfinite(v))
      return false;
   value = v;
   if (end)
      *end = p;
   return true;
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
   if (proc->audio_outputs_count < 1)
      return;
   const uint32_t frames = proc->frames_count;
   const clap_audio_buffer_t *in = proc->audio_inputs_count > 0 ? &proc->audio_inputs[0] : nullptr;
   clap_audio_buffer_t &out = proc->audio_outputs[0];
   const uint32_t channels = in ? std::min(in->channel_count, out.channel_count) : 0;
   uint64_t constant = 0;
   for (uint32_t c = 0; c < out.channel_count; ++c) {
      float *o32 = out.data32 ? out.data32[c] : nullptr;
      double *o64 = out.data64 ? out.data64[c] : nullptr;
      const float *i32 = c < channels && in->data32 ? in->data32[c] : nullptr;
      const double *i64 = c < channels && in->data64 ? in->data64[c] : nullptr;
      if (!i32 && !i64) {
         // Nothing to copy: silence.
         if (o32)
            std::memset(o32, 0, frames * sizeof(float));
         if (o64)
            std::memset(o64, 0, frames * sizeof(double));
         constant |= c < 64 ? uint64_t(1) << c : 0;
         continue;
      }
      if (o32 && o32 != i32) {
         if (i32)
            std::memcpy(o32, i32, frames * sizeof(float));
         else
            for (uint32_t i = 0; i < frames; ++i)
               o32[i] = static_cast<float>(i64[i]);
      }
      if (o64 && o64 != i64) {
         if (i64)
            std::memcpy(o64, i64, frames * sizeof(double));
         else
            for (uint32_t i = 0; i < frames; ++i)
               o64[i] = i32[i];
      }
      if (c < 64 && (in->constant_mask >> c) & 1)
         constant |= uint64_t(1) << c;
   }
   out.constant_mask = constant;
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

// JTunnel's params: ids, ranges and display text, shared by the plugin and the panel.
#pragma once

#include <clap/clap.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace jtunnel {

enum ParamId : clap_id {
   kParamMode = 0,
   kParamCodec,
   kParamBitrate,
   kParamRedundancy,
   kParamBuffer,
   kParamAutoBuffer,
   kParamThru,
   kParamLevel,
   kParamCarry,
   kParamCount
};

enum Mode { kModeSend = 0, kModeReceive = 1 };
enum CodecChoice { kCodecOpus = 0, kCodecPcm = 1 };
// What a tunnel carries. A sender sends only this; a receiver plays only this of what arrives.
enum Carry { kCarryBoth = 0, kCarryAudio = 1, kCarryMidi = 2 };
inline bool carriesAudio(int carry) { return carry != kCarryMidi; }
inline bool carriesMidi(int carry) { return carry != kCarryAudio; }

struct ParamInfo {
   const char *name;
   double min, max, def;
   bool stepped;
};

inline constexpr ParamInfo kParams[kParamCount] = {
   {"Mode", 0, 1, kModeSend, true},
   {"Format", 0, 1, kCodecOpus, true},
   {"Bitrate", 64, 512, 192, true},
   {"Redundancy", 0, 1, 1, true},
   {"Buffer", 1, 250, 20, false},
   {"Auto Buffer", 0, 1, 1, true},
   {"Thru", 0, 1, 1, true},
   {"Level", -60, 12, 0, false},
   {"Carry", 0, 2, kCarryBoth, true},
};

inline const char *const kModeLabels[] = {"Send", "Receive"};
inline const char *const kCodecLabels[] = {"Opus", "PCM 24-bit"};
inline const char *const kOnOffLabels[] = {"Off", "On"};
inline const char *const kCarryLabels[] = {"Audio + MIDI", "Audio", "MIDI"};

inline const char *const *choiceLabels(clap_id id) {
   switch (id) {
   case kParamMode:
      return kModeLabels;
   case kParamCodec:
      return kCodecLabels;
   case kParamCarry:
      return kCarryLabels;
   case kParamRedundancy:
   case kParamAutoBuffer:
   case kParamThru:
      return kOnOffLabels;
   default:
      return nullptr;
   }
}

inline void formatParam(clap_id id, double value, char *out, uint32_t size) {
   if (const char *const *labels = choiceLabels(id))
      std::snprintf(out, size, "%s",
                    labels[std::clamp(static_cast<int>(std::lround(value - kParams[id].min)), 0,
                                      static_cast<int>(kParams[id].max - kParams[id].min))]);
   else if (id == kParamBitrate)
      std::snprintf(out, size, "%d kbps", static_cast<int>(value + 0.5));
   else if (id == kParamBuffer)
      std::snprintf(out, size, value < 10.0 ? "%.1f ms" : "%.0f ms", value);
   else if (id == kParamLevel) {
      if (value <= kParams[kParamLevel].min)
         std::snprintf(out, size, "-inf dB");
      else
         std::snprintf(out, size, "%+.1f dB", value);
   }
   else
      std::snprintf(out, size, "%g", value);
}

inline bool sameText(const char *a, const char *b) {
   for (; *a && *b; ++a, ++b)
      if (std::tolower(static_cast<unsigned char>(*a)) != std::tolower(static_cast<unsigned char>(*b)))
         return false;
   return *a == *b;
}

// Labels in any case ("pcm" too, for the format), or a number with or without its unit, kept to
// the param's range.
inline bool parseParam(clap_id id, const char *text, double &value) {
   while (std::isspace(static_cast<unsigned char>(*text)))
      ++text;
   if (const char *const *labels = choiceLabels(id)) {
      for (int i = 0; i <= static_cast<int>(kParams[id].max - kParams[id].min); ++i)
         if (sameText(text, labels[i]) || (id == kParamCodec && i == 1 && sameText(text, "pcm")) ||
             (id == kParamCarry && i == 0 && (sameText(text, "both") || sameText(text, "audio+midi")))) {
            value = i;
            return true;
         }
   }
   if (id == kParamLevel && (sameText(text, "-inf") || sameText(text, "-inf dB"))) {
      value = kParams[id].min;
      return true;
   }
   double v;
   if (std::sscanf(text, "%lf", &v) != 1 || !std::isfinite(v))
      return false;
   if (kParams[id].stepped)
      v = std::round(v);
   value = std::clamp(v, kParams[id].min, kParams[id].max);
   return true;
}

// Tunnel names travel in mDNS names and packets, so they are kept short and to characters that
// survive both: letters, digits, spaces, - and _. Anything else is dropped.
constexpr size_t kMaxNameLength = 31;

inline void sanitizeName(const char *in, char *out, size_t size) {
   size_t n = 0;
   for (; *in && n + 1 < size && n < kMaxNameLength; ++in) {
      const unsigned char c = static_cast<unsigned char>(*in);
      if (std::isalnum(c) || c == ' ' || c == '-' || c == '_')
         out[n++] = static_cast<char>(c);
   }
   // No leading or trailing spaces: they'd make two names that look the same.
   while (n > 0 && out[n - 1] == ' ')
      --n;
   out[n] = 0;
   size_t start = 0;
   while (out[start] == ' ')
      ++start;
   if (start)
      std::memmove(out, out + start, n - start + 1);
}

} // namespace jtunnel

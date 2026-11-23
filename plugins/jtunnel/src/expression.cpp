#include "expression.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "protocol.h"

namespace jtunnel {
namespace {

constexpr int kPressureExpression = 6; // CLAP_NOTE_EXPRESSION_PRESSURE

// Down goes to -1 (raw 0) and up to +1 (raw 16383), so the two sides scale a little differently.
float bendValue(int raw) {
   const int offset = raw - 8192;
   return offset < 0 ? offset / 8192.0f : offset / 8191.0f;
}

void putNumber(std::string &out, float v) {
   char text[16];
   std::snprintf(text, sizeof(text), "%.3f", v);
   // "-0.000" is 0.
   out += std::strcmp(text, "-0.000") ? text : "0.000";
}

} // namespace

std::string expressionJson(const ExpressionSnapshot &s) {
   int furthest = 8192;
   float hardest = 0;
   std::string channels;
   for (int ch = 0; ch < 16; ++ch) {
      const float pressure = std::max(s.channelPressure[ch] / 127.0f, s.keyPressure[ch]);
      if (std::abs(s.bend[ch] - 8192) > std::abs(furthest - 8192))
         furthest = s.bend[ch];
      hardest = std::max(hardest, pressure);
      if (!s.hasNotes[ch] && s.bend[ch] == 8192 && pressure <= 0)
         continue;
      if (!channels.empty())
         channels += ',';
      channels += "{\"channel\":" + std::to_string(ch + 1) + ",\"bend\":";
      putNumber(channels, bendValue(s.bend[ch]));
      channels += ",\"pressure\":";
      putNumber(channels, pressure);
      channels += '}';
   }
   std::string out = "{\"bend\":";
   putNumber(out, bendValue(furthest));
   out += ",\"pressure\":";
   putNumber(out, hardest);
   out += ",\"channels\":[" + channels + "],\"notes\":[";
   for (int i = 0; i < s.noteCount; ++i) {
      const ExpressionSnapshot::Note &n = s.notes[i];
      if (i)
         out += ',';
      out += "{\"channel\":" + std::to_string(n.channel + 1) + ",\"pitch\":" + std::to_string(n.key) +
             ",\"velocity\":" + std::to_string(n.velocity) + ",\"bend\":";
      putNumber(out, bendValue(s.bend[n.channel]));
      out += ",\"pressure\":";
      putNumber(out, std::max(n.pressure, s.channelPressure[n.channel] / 127.0f));
      out += '}';
   }
   out += "]}";
   return out;
}

void ExpressionTracker::reset() {
   std::fill(std::begin(bend_), std::end(bend_), static_cast<int16_t>(8192));
   std::fill(std::begin(channelPressure_), std::end(channelPressure_), static_cast<uint8_t>(0));
   for (auto &channel : velocity_)
      std::fill(std::begin(channel), std::end(channel), static_cast<uint8_t>(0));
   for (auto &channel : keyPressure_)
      std::fill(std::begin(channel), std::end(channel), 0.0f);
   changed_ = false;
   rebuild();
}

bool ExpressionTracker::add(const TimedMidi *events, int count) {
   for (int i = 0; i < count; ++i)
      add(events[i].data, events[i].size);
   if (!changed_)
      return false;
   changed_ = false;
   rebuild();
   return true;
}

void ExpressionTracker::add(const uint8_t *d, int size) {
   const auto set = [this](auto &field, auto value) {
      if (field != value) {
         field = value;
         changed_ = true;
      }
   };
   if (size >= kExpressionBytes && d[0] == kTagExpression) {
      if (d[1] != kPressureExpression)
         return;
      uint64_t bits = 0;
      for (int i = 0; i < 8; ++i)
         bits |= static_cast<uint64_t>(d[4 + i]) << (8 * i);
      double value;
      std::memcpy(&value, &bits, 8);
      if (!std::isfinite(value))
         return;
      const float v = static_cast<float>(std::clamp(value, 0.0, 1.0));
      // A wildcard channel or key means every held note it covers.
      for (int ch = 0; ch < 16; ++ch)
         if (d[2] == 255 || d[2] == ch)
            for (int key = 0; key < 128; ++key)
               if (velocity_[ch][key] && (d[3] == 255 || d[3] == key))
                  set(keyPressure_[ch][key], v);
      return;
   }
   if (size < 2 || d[0] < 0x80 || d[0] >= 0xF0)
      return;
   const int ch = d[0] & 0x0F;
   const int key = d[1] & 0x7F;
   switch (d[0] & 0xF0) {
   case 0x90:
      if (size >= 3 && d[2] > 0) {
         set(velocity_[ch][key], static_cast<uint8_t>(d[2] & 0x7F));
         set(keyPressure_[ch][key], 0.0f);
         break;
      }
      [[fallthrough]]; // a note on at velocity 0 is a note off
   case 0x80:
      set(velocity_[ch][key], static_cast<uint8_t>(0));
      set(keyPressure_[ch][key], 0.0f);
      break;
   case 0xA0:
      if (size >= 3 && velocity_[ch][key])
         set(keyPressure_[ch][key], (d[2] & 0x7F) / 127.0f);
      break;
   case 0xD0:
      set(channelPressure_[ch], static_cast<uint8_t>(d[1] & 0x7F));
      break;
   case 0xE0:
      if (size >= 3)
         set(bend_[ch], static_cast<int16_t>((d[2] & 0x7F) << 7 | (d[1] & 0x7F)));
      break;
   default:
      break;
   }
}

void ExpressionTracker::rebuild() {
   ExpressionSnapshot &s = snapshot_;
   s.noteCount = 0;
   for (int ch = 0; ch < 16; ++ch) {
      s.bend[ch] = bend_[ch];
      s.channelPressure[ch] = channelPressure_[ch];
      s.hasNotes[ch] = false;
      s.keyPressure[ch] = 0;
      for (int key = 0; key < 128; ++key) {
         if (!velocity_[ch][key])
            continue;
         s.hasNotes[ch] = true;
         s.keyPressure[ch] = std::max(s.keyPressure[ch], keyPressure_[ch][key]);
         if (s.noteCount < ExpressionSnapshot::kMaxNotes)
            s.notes[s.noteCount++] = {static_cast<uint8_t>(ch), static_cast<uint8_t>(key), velocity_[ch][key],
                                      keyPressure_[ch][key]};
      }
   }
}

bool SharedExpression::publish(const ExpressionSnapshot &snapshot) {
   std::unique_lock<std::mutex> lock(mutex_, std::try_to_lock);
   if (!lock.owns_lock())
      return false;
   snapshot_ = snapshot;
   ++version_;
   return true;
}

uint32_t SharedExpression::read(ExpressionSnapshot &out) const {
   std::lock_guard<std::mutex> lock(mutex_);
   out = snapshot_;
   return version_;
}

void SharedExpression::reset() {
   ExpressionTracker empty;
   std::lock_guard<std::mutex> lock(mutex_);
   snapshot_ = empty.snapshot();
   ++version_;
}

} // namespace jtunnel

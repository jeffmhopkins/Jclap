#pragma once

// JArp's engine: the fingers held, the grid's rows mapped onto them, and the pattern played through
// them in time with the host. No allocation, no locks: it belongs to the audio thread while the
// plugin is active.
//
// Time runs in steps (one step is the Rate). The clock is the host's song position while it plays,
// and the engine's own while it is stopped, so the arp plays either way. Swing moves the second step
// of each pair later: a note's place on the grid is its musical time m, and warp(m) is when it
// sounds. Each step boundary the playhead crosses starts the notes on that step; each note's hits
// (its ratchet) are queued at their musical times and sent when the clock reaches them.
//
// Every arp note goes out on the held finger's own channel and key (plus the step's octave), so an
// MPE synth hears that finger's pressure, slide and bend on it, which pass straight through.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

#include "params.h"
#include "shared.h"

namespace jarp {

struct Settings {
   int rate = 2;
   double swing = 0.5; // 0.5 straight .. 0.75
   int slot = 0;
   Advance advance = Advance::Off;
   bool latch = false;
   Restart restart = Restart::Note;
   bool degree = false;
   // Spread: the pattern's rows spread over the notes held, bottom row to the lowest, top row to the
   // highest, the rows between to the nearest by position.
   bool spread = false;
   // In Degree mode, a degree that isn't held stays silent, rather than playing the nearest note.
   bool strict = false;
   bool wrap = false;
   // Octaves above the lowest note held an octave shift (OCT rows, the Octave lane) may reach; past
   // it a shifted note drops back down by octaves (and below as far, it comes back up).
   int range = 2;
   // With wrap, each time round the held notes goes an octave higher.
   bool octaves = false;
   int key = 0;
   int scale = 0; // 0 major, 1 minor
   double pressVelocity = 0.5, curve = 0.0, pressRatchet = 0.6;
   int maxHits = 4;
};

struct Block {
   uint32_t frames = 0;
   double sampleRate = 48000.0;
   bool hasTransport = false, playing = false, hasBar = false;
   double tempo = 120.0, beats = 0.0, barStart = 0.0;
};

struct MidiOut {
   uint32_t time;
   uint8_t status, data1, data2;
   // For an arp note-on, the finger it comes from (see Engine::carried); -1 otherwise.
   int8_t finger = -1;
};

// Where a finger's pressure, slide or bend came from, so it can be carried onto each new note.
enum class Source : uint8_t {
   None,
   Channel,    // MIDI channel pressure, CC74 or pitch bend: sent again on the channel before each note
   Poly,       // MIDI poly aftertouch: sent again for each note's key
   Expression, // a CLAP note expression: sent again to each note after it starts
};

// One finger's expression, for the notes it plays.
struct Carried {
   double value[3] = {0.0, 0.0, 0.0}; // pressure, slide (0..1) and bend (-1..1 of 48 semitones)
   Source source[3] = {Source::None, Source::None, Source::None};
};

// What the engine sends in one block, without allocating on the audio thread.
struct OutBuffer {
   static constexpr size_t kCapacity = 2048;
   MidiOut items[kCapacity];
   size_t count = 0;
   void push(const MidiOut &m) {
      if (count < kCapacity)
         items[count++] = m;
   }
   void clear() { count = 0; }
};

inline const int kScaleSteps[2][7] = {{0, 2, 4, 5, 7, 9, 11}, {0, 2, 3, 5, 7, 8, 10}};

inline bool inScale(int pitch, int key, int scale) {
   const int rel = ((pitch - key) % 12 + 12) % 12;
   for (int s : kScaleSteps[scale & 1])
      if (s == rel)
         return true;
   return false;
}

// The pitch `degrees` scale steps above the scale note at or below `from`.
inline int degreeAbove(int from, int degrees, int key, int scale) {
   const int *steps = kScaleSteps[scale & 1];
   const int rel = ((from - key) % 12 + 12) % 12;
   const int base = from - rel;
   int i = 0;
   for (int k = 0; k < 7; ++k)
      if (steps[k] <= rel)
         i = k;
   const int d = i + degrees;
   return base + 12 * (d / 7) + steps[d % 7];
}

// `key` shifted by `octaves`, folded back by octaves into `range` octaves of `lowest` (the lowest
// note held). Only the shift folds, so a held note itself always plays as held.
inline int foldOctaves(int key, int octaves, int lowest, int range) {
   const int top = lowest + 12 * range, bottom = lowest - 12 * range;
   while (octaves > 0 && key + 12 * octaves > top)
      --octaves;
   while (octaves < 0 && key + 12 * octaves < bottom)
      ++octaves;
   return key + 12 * octaves;
}

// The highest row with a note in `p`, or 0 when it has none: the top row Spread spreads to.
inline int topRow(const Pattern &p) {
   int top = 0;
   for (int i = 0; i < p.count; ++i)
      top = std::max<int>(top, p.notes[i].row);
   return top;
}

// Maps the grid's rows onto the held keys (lowest first, `count` of them): `key[r]` is the index of
// the held key row r plays, or -1, and `target[r]` the pitch Degree mode wanted for it. `top` is
// the pattern's top row, for Spread.
inline void mapRows(const int *held, int count, const Settings &s, int *key, int *target, int *octave = nullptr,
                    int top = kRows - 1) {
   for (int r = 0; r < kRows; ++r) {
      key[r] = -1;
      target[r] = -1;
      if (octave)
         octave[r] = 0;
   }
   if (count <= 0)
      return;
   if (s.spread) {
      top = std::clamp(top, 0, kRows - 1);
      for (int r = 0; r <= top; ++r)
         key[r] = top ? static_cast<int>(std::lround(r * (count - 1) / static_cast<double>(top))) : 0;
      return;
   }
   if (!s.degree) {
      for (int r = 0; r < kRows; ++r) {
         key[r] = r < count ? r : s.wrap ? r % count : -1;
         if (octave && s.wrap && s.octaves)
            octave[r] = r / count;
      }
      return;
   }
   const int scale = std::clamp(s.scale, 0, 1), root = ((s.key % 12) + 12) % 12;
   for (int r = 0; r < kRows; ++r) {
      const int want = r == 0 ? held[0] : degreeAbove(held[0], r, root, scale);
      target[r] = want;
      if (s.strict) {
         // Only the degree itself: the nearest held note that is that degree, in any octave.
         int best = -1;
         for (int i = 0; i < count; ++i)
            if (((held[i] - want) % 12 + 12) % 12 == 0 && (best < 0 || std::abs(held[i] - want) < std::abs(held[best] - want)))
               best = i;
         key[r] = best;
         continue;
      }
      int best = 0;
      for (int i = 1; i < count; ++i) {
         const int d = std::abs(held[i] - want), b = std::abs(held[best] - want);
         // Nearest wins; on a tie, the one in the scale, and then the lower (already best).
         if (d < b || (d == b && inScale(held[i], root, scale) && !inScale(held[best], root, scale)))
            best = i;
      }
      key[r] = best;
   }
}

// Swing as a warp of each pair of steps: the second starts at 2 * swing instead of 1.
inline double warp(double m, double swing) {
   const double b = 2.0 * std::clamp(swing, 0.5, 0.75);
   const double pair = std::floor(m / 2.0), t = m - 2.0 * pair;
   return 2.0 * pair + (t < 1.0 ? t * b : b + (t - 1.0) * (2.0 - b));
}
inline double unwarp(double p, double swing) {
   const double b = 2.0 * std::clamp(swing, 0.5, 0.75);
   const double pair = std::floor(p / 2.0), r = p - 2.0 * pair;
   return 2.0 * pair + (r < b ? r / b : 1.0 + (r - b) / (2.0 - b));
}

class Engine {
public:
   void setSettings(const Settings &s) {
      const bool dropLatched = settings_.latch && !s.latch;
      settings_ = s;
      settings_.slot = std::clamp(s.slot, 0, kSlots - 1);
      // Picking a slot plays it from the next step on.
      if (settings_.slot != selected_) {
         selected_ = settings_.slot;
         playing_ = selected_;
         // In CHORD, the next chord plays the slot just picked, not the one after it.
         played_ = false;
      }
      settings_.rate = std::clamp(s.rate, 0, 5);
      settings_.maxHits = std::clamp(s.maxHits, 1, kMaxRatchet);
      settings_.range = std::clamp(s.range, 1, 4);
      dropLatched_ = dropLatched_ || dropLatched;
   }
   const Settings &settings() const { return settings_; }

   void setPattern(int slot, const Pattern &p) {
      if (slot >= 0 && slot < kSlots)
         bank_.slots[slot] = p;
   }
   void setBank(const Bank &b) {
      bank_ = b;
      played_ = false;
   }
   const Bank &bank() const { return bank_; }
   int playing() const { return playing_; }

   // Starts a block: works out where the clock is, and if it jumped (the host looped or located, or
   // the rate changed) ends the notes sounding and starts the pattern over from there.
   void beginBlock(const Block &b, OutBuffer &out) {
      const double stepBeats = kRateBeats[settings_.rate];
      const double tempo = b.tempo > 0.0 ? b.tempo : 120.0;
      perSample_ = tempo / 60.0 / std::max(1.0, b.sampleRate) / stepBeats;
      const bool host = b.hasTransport && b.playing;
      start_ = host ? b.beats / stepBeats : free_;
      cur_ = 0;
      // A hundredth of a step off is a jump; less is the host's tempo moving within the block (a ramp
      // reaches the next block a little early or late), which the clock just follows.
      const bool jumped = started_ && (host != wasHost_ || stepBeats != stepBeats_ ||
                                       std::abs(start_ - expected_) > kJumpSteps);
      // Set before originAt, which counts the bar in steps of the new rate.
      stepBeats_ = stepBeats;
      if (jumped && running()) {
         allOff(0, out);
         origin_ = originAt(start_, b);
         next_ = kUnset;
         // The step count starts over, so a Loop chain does too (from the slot selected), unless
         // the song says where it is.
         slotStart_ = 0;
         hits_.fill(0);
         if (settings_.advance == Advance::Loop && settings_.restart != Restart::Song)
            playing_ = firstWithNotes(selected_);
      }
      started_ = true;
      wasHost_ = host;
      block_ = b;
      expected_ = start_ + b.frames * perSample_;
      free_ = expected_;
      if (dropLatched_) {
         dropLatched_ = false;
         for (int f = 0; f < kMaxFingers; ++f)
            if (fingers_[f].used && !fingers_[f].held && !sustain_)
               release(f, 0, out);
      }
   }

   // Runs the clock on to `sample` within the block, sending what falls before it.
   void advance(uint32_t sample, OutBuffer &out) {
      if (sample <= cur_)
         return;
      if (!running()) {
         cur_ = sample;
         return;
      }
      const double p0 = clock(cur_) - origin_, p1 = clock(sample) - origin_;
      const double m0 = unwarp(p0, settings_.swing), m1 = unwarp(p1, settings_.swing);
      int64_t j = static_cast<int64_t>(std::ceil(m0 - 1e-7));
      if (next_ != kUnset)
         j = std::max(j, next_);
      for (; static_cast<double>(j) < m1; ++j) {
         trigger(j);
         next_ = j + 1;
      }
      // Send what is due, in time order (note-offs first at the same time).
      int due[kMaxPending];
      int n = 0;
      for (int i = 0; i < pendingCount_; ++i)
         if (pending_[i].m < m1)
            due[n++] = i;
      std::sort(due, due + n, [this](int a, int b) {
         const Pending &x = pending_[a], &y = pending_[b];
         return x.m != y.m ? x.m < y.m : x.on < y.on;
      });
      for (int k = 0; k < n; ++k) {
         const Pending &e = pending_[due[k]];
         const double at = cur_ + (warp(e.m, settings_.swing) - p0) / perSample_;
         const uint32_t t = static_cast<uint32_t>(std::clamp(std::floor(at + 0.5), static_cast<double>(cur_), static_cast<double>(sample - 1)));
         if (e.on)
            sendOn(t, e, out);
         else
            sendOff(t, e, out);
      }
      if (n) {
         int w = 0;
         for (int i = 0; i < pendingCount_; ++i)
            if (!(pending_[i].m < m1))
               pending_[w++] = pending_[i];
         pendingCount_ = w;
      }
      position_ = m1;
      cur_ = sample;
   }

   void noteOn(uint32_t t, int channel, int key, int velocity, OutBuffer &out) {
      advance(t, out);
      channel &= 15;
      key &= 127;
      bool anyHeld = false;
      for (const Finger &f : fingers_)
         anyHeld = anyHeld || (f.used && f.held);
      // With Latch on, a new chord after letting go of the last one replaces it.
      if (settings_.latch && !anyHeld)
         for (int f = 0; f < kMaxFingers; ++f)
            if (fingers_[f].used)
               release(f, t, out);
      const bool wasRunning = running();
      int slot = find(channel, key);
      if (slot < 0)
         for (int f = 0; f < kMaxFingers && slot < 0; ++f)
            if (!fingers_[f].used)
               slot = f;
      if (slot < 0)
         return;
      Finger &f = fingers_[slot];
      if (!f.used) {
         f = Finger{};
         f.used = true;
         f.channel = static_cast<uint8_t>(channel);
         f.key = static_cast<uint8_t>(key);
         f.pressure = channels_[channel].pressure;
         f.slide = channels_[channel].slide;
         f.bend = channels_[channel].bend;
         for (int i = 0; i < 3; ++i)
            f.source[i] = channels_[channel].source[i];
      }
      f.held = true;
      f.velocity = static_cast<uint8_t>(std::clamp(velocity, 1, 127));
      if (!wasRunning) {
         origin_ = originAt(clock(t), block_);
         next_ = kUnset;
         slotStart_ = 0;
         hits_.fill(0);
         // A new chord moves on to the next pattern with notes in it (Advance Chord); the first
         // chord plays the one selected.
         // A Loop chain plays from the one selected each time, as one phrase.
         if (settings_.advance == Advance::Off)
            playing_ = selected_;
         else if (settings_.advance == Advance::Chord && played_)
            playing_ = bank_.next(playing_);
         else if (settings_.advance == Advance::Loop)
            playing_ = firstWithNotes(selected_);
         else if (bank_.slots[playing_].empty())
            playing_ = bank_.next(playing_);
         played_ = true;
      }
   }

   void noteOff(uint32_t t, int channel, int key, OutBuffer &out) {
      advance(t, out);
      const int slot = find(channel & 15, key & 127);
      if (slot < 0)
         return;
      // Latched, or with the sustain pedal down, the finger stays in the chord after it lets go.
      if (settings_.latch || sustain_)
         fingers_[slot].held = false;
      else
         release(slot, t, out);
   }

   // The sustain pedal (CC64): while it's down, keys let go stay in the chord and the arp keeps
   // playing them; lifting it lets go of them (unless Latch keeps them).
   void setSustain(uint32_t t, bool down, OutBuffer &out) {
      advance(t, out);
      const bool lifted = sustain_ && !down;
      sustain_ = down;
      if (lifted && !settings_.latch)
         for (int f = 0; f < kMaxFingers; ++f)
            if (fingers_[f].used && !fingers_[f].held)
               release(f, t, out);
   }
   bool sustain() const { return sustain_; }

   // Lets go of the fingers on (channel, key) whatever Latch says, -1 for any: a host's note
   // choke, or All Notes Off / All Sound Off.
   void letGo(uint32_t t, int channel, int key, OutBuffer &out) {
      advance(t, out);
      for (int f = 0; f < kMaxFingers; ++f) {
         const Finger &g = fingers_[f];
         if (g.used && (channel < 0 || g.channel == (channel & 15)) && (key < 0 || g.key == key))
            release(f, t, out);
      }
   }

   // Expression from the fingers. A key of -1 means every finger on the channel (MIDI channel
   // pressure, CC74 and pitch bend), and is remembered for the next note on that channel too.
   // `source` says how it came, so each new arp note can be given it again (a synth starts a new
   // note's own expression from nothing).
   void setPressure(int channel, int key, double v, Source source = Source::Channel) { expression(channel, key, 0, v, source); }
   void setSlide(int channel, int key, double v, Source source = Source::Channel) { expression(channel, key, 1, v, source); }
   void setBend(int channel, int key, double v, Source source = Source::Channel) { expression(channel, key, 2, v, source); }

   // The expression of finger `finger` (MidiOut::finger), to carry onto a note it starts.
   Carried carried(int finger) const {
      Carried c;
      if (finger < 0 || finger >= kMaxFingers)
         return c;
      const Finger &f = fingers_[finger];
      c.value[0] = f.pressure;
      c.value[1] = f.slide;
      c.value[2] = f.bend;
      for (int i = 0; i < 3; ++i)
         c.source[i] = f.source[i];
      return c;
   }

   // Calls f(outKey) for each note sounding from the finger on (channel, key), or from every finger
   // on the channel for key -1: where that finger's expression goes.
   template <typename F>
   void forEachSounding(int channel, int key, F &&f) const {
      channel &= 15;
      for (int k = 0; k < 128; ++k) {
         const Sound &s = sounding_[channel][k];
         if (!s.serial)
            continue;
         const Finger &g = fingers_[s.finger];
         if (key < 0 || (g.used && g.key == key && g.channel == channel))
            f(k);
      }
   }

   // Ends every note sounding and forgets the queue (the plugin stops or resets).
   void allOff(uint32_t t, OutBuffer &out) {
      for (int c = 0; c < 16; ++c)
         for (int k = 0; k < 128; ++k)
            if (sounding_[c][k].serial) {
               out.push({t, static_cast<uint8_t>(0x80 | c), static_cast<uint8_t>(k), 0});
               sounding_[c][k] = {};
            }
      pendingCount_ = 0;
   }

   // Forgets every finger and note without sending anything (deactivate).
   void reset() {
      for (Finger &f : fingers_)
         f = {};
      for (auto &c : sounding_)
         for (Sound &s : c)
            s = {};
      pendingCount_ = 0;
      started_ = false;
      next_ = kUnset;
      hits_.fill(0);
      played_ = false;
   }

   bool running() const {
      for (const Finger &f : fingers_)
         if (f.used)
            return true;
      return false;
   }

   void fillFrame(Frame &frame) const {
      frame.running = running();
      const double len = bank_.slots[playing_].length;
      frame.position = frame.running ? std::fmod(std::max(0.0, position_ - slotStart_), len) : 0.0;
      frame.slot = static_cast<int8_t>(playing_);
      int held[kMaxFingers], slots[kMaxFingers];
      const int n = sortedFingers(held, slots);
      int key[kRows], target[kRows], octave[kRows];
      mapRows(held, n, settings_, key, target, octave, topRow(bank_.slots[playing_]));
      for (int r = 0; r < kRows; ++r) {
         RowView &v = frame.rows[r];
         v = RowView{};
         if (key[r] < 0)
            continue;
         const Finger &f = fingers_[slots[key[r]]];
         v.key = static_cast<int8_t>(std::clamp(foldOctaves(f.key, octave[r], held[0], settings_.range), 0, 127));
         v.target = target[r] >= 0 && target[r] != f.key ? static_cast<int8_t>(std::clamp(target[r], 0, 127)) : -1;
         v.channel = f.channel;
         v.pressure = static_cast<float>(f.pressure);
         v.slide = static_cast<float>(f.slide);
         v.bend = static_cast<float>(f.bend);
      }
      frame.hits = hits_;
   }

   // The hits a note on a step plays for a finger pressing `pressure` (0..1, before the curve).
   int hitsFor(const Lane &lane, double pressure) const {
      int n = std::clamp<int>(lane.ratchet, 1, kMaxRatchet);
      if (lane.pressure && settings_.maxHits > n) {
         const double p = curvePressure(pressure, settings_.curve) * settings_.pressRatchet;
         n += static_cast<int>(std::lround(p * (settings_.maxHits - n)));
      }
      return n;
   }

   // A note's velocity: the finger's, leaning toward its pressure by Pressure to Velocity, plus the
   // step's offset.
   int velocityFor(const Lane &lane, int fingerVelocity, double pressure) const {
      const double v = fingerVelocity / 127.0;
      const double p = curvePressure(pressure, settings_.curve);
      const double mixed = v + (p - v) * settings_.pressVelocity;
      return std::clamp(static_cast<int>(std::lround(mixed * 127.0)) + lane.velocity, 1, 127);
   }

private:
   static constexpr int64_t kUnset = INT64_MIN;
   static constexpr double kJumpSteps = 0.01;
   static constexpr int kMaxPending = 1024;

   struct Finger {
      bool used = false, held = false;
      uint8_t channel = 0, key = 0, velocity = 100;
      double pressure = 0.0, slide = 0.0, bend = 0.0;
      Source source[3] = {Source::None, Source::None, Source::None};
   };
   struct ChannelState {
      double pressure = 0.0, slide = 0.0, bend = 0.0;
      Source source[3] = {Source::None, Source::None, Source::None};
   };
   struct Pending {
      double m;
      uint32_t serial;
      uint8_t channel, key, velocity;
      int8_t finger;
      bool on;
   };
   struct Sound {
      uint32_t serial = 0;
      int8_t finger = 0;
   };

   static uint8_t toByte(double v) { return static_cast<uint8_t>(std::clamp(static_cast<int>(std::lround(v * 127.0)), 0, 127)); }

   double clock(uint32_t sample) const { return start_ + sample * perSample_; }

   // The pattern and its start for musical step j of a Loop chain locked to the song.
   void songChain(int64_t j) {
      int chain[kSlots], n = 0, cycle = 0;
      int s = bank_.slots[selected_].empty() ? bank_.next(selected_) : selected_;
      for (int k = 0; k < kSlots && !bank_.slots[s].empty(); ++k) {
         chain[n++] = s;
         cycle += std::clamp<int>(bank_.slots[s].length, 1, kSteps);
         s = bank_.next(s);
         if (s == chain[0])
            break;
      }
      if (!n) {
         slotStart_ = 0;
         return;
      }
      int64_t at = ((j % cycle) + cycle) % cycle;
      for (int i = 0; i < n; ++i) {
         const int len = std::clamp<int>(bank_.slots[chain[i]].length, 1, kSteps);
         if (at < len || i + 1 == n) {
            playing_ = chain[i];
            slotStart_ = j - at;
            return;
         }
         at -= len;
      }
   }

   double originAt(double now, const Block &b) const {
      switch (settings_.restart) {
      case Restart::Song: return 0.0;
      case Restart::Bar:
         if (b.hasTransport && b.playing && b.hasBar)
            return b.barStart / stepBeats_;
         return now;
      case Restart::Note:
      default: return now;
      }
   }

   // `from` if it has notes, otherwise the next slot that does.
   int firstWithNotes(int from) const { return bank_.slots[from].empty() ? bank_.next(from) : from; }

   int find(int channel, int key) const {
      for (int f = 0; f < kMaxFingers; ++f)
         if (fingers_[f].used && fingers_[f].channel == channel && fingers_[f].key == key)
            return f;
      return -1;
   }

   // The fingers by pitch, lowest first: their keys and their slots.
   int sortedFingers(int *keys, int *slots) const {
      int n = 0;
      for (int f = 0; f < kMaxFingers; ++f)
         if (fingers_[f].used)
            slots[n++] = f;
      std::sort(slots, slots + n, [this](int a, int b) {
         const Finger &x = fingers_[a], &y = fingers_[b];
         return x.key != y.key ? x.key < y.key : x.channel < y.channel;
      });
      for (int i = 0; i < n; ++i)
         keys[i] = fingers_[slots[i]].key;
      return n;
   }

   void expression(int channel, int key, int which, double v, Source source) {
      channel &= 15;
      if (key < 0) {
         double *c[] = {&channels_[channel].pressure, &channels_[channel].slide, &channels_[channel].bend};
         *c[which] = v;
         channels_[channel].source[which] = source;
      }
      for (Finger &f : fingers_)
         if (f.used && f.channel == channel && (key < 0 || f.key == key)) {
            double *d[] = {&f.pressure, &f.slide, &f.bend};
            *d[which] = v;
            f.source[which] = source;
         }
   }

   // Starts the notes on musical step j (counted from the origin).
   void trigger(int64_t j) {
      int len = std::clamp<int>(bank_.slots[playing_].length, 1, kSteps);
      // Locked to the song, a Loop chain is a sequence along it: the patterns with notes in them,
      // from the one selected, one after another from the song's start, so where the song is says
      // which pattern plays and which step.
      if (settings_.advance == Advance::Loop && settings_.restart == Restart::Song) {
         const int was = playing_;
         songChain(j);
         if (playing_ != was)
            hits_.fill(0);
         len = std::clamp<int>(bank_.slots[playing_].length, 1, kSteps);
      }
      // Otherwise, each time round, Advance Loop moves on to the next pattern with notes in it.
      else if (settings_.advance == Advance::Loop && j > slotStart_ && (j - slotStart_) % len == 0) {
         const int next = bank_.next(playing_);
         if (next != playing_) {
            playing_ = next;
            hits_.fill(0);
         }
         slotStart_ = j;
         len = std::clamp<int>(bank_.slots[playing_].length, 1, kSteps);
      }
      const int64_t rel = j - slotStart_;
      const int step = static_cast<int>(((rel % len) + len) % len);
      const Pattern &pattern_ = bank_.slots[playing_];
      int held[kMaxFingers], slots[kMaxFingers];
      const int n = sortedFingers(held, slots);
      if (!n)
         return;
      int key[kRows], target[kRows], octave[kRows];
      mapRows(held, n, settings_, key, target, octave, topRow(pattern_));
      // In Spread, rows that land on the same held note play it once.
      bool spreadPlayed[kMaxFingers] = {};
      for (int i = 0; i < pattern_.count; ++i) {
         const Note &note = pattern_.notes[i];
         if (note.step != step || note.row >= kRows || key[note.row] < 0)
            continue;
         if (settings_.spread) {
            if (spreadPlayed[key[note.row]])
               continue;
            spreadPlayed[key[note.row]] = true;
         }
         // The step's lane, with any values the note has of its own.
         const Lane lane = pattern_.laneOf(i);
         // Chance: the note plays this time round only if the roll comes in under it.
         if (lane.chance < 100 && roll() % 100 >= lane.chance)
            continue;
         const int slot = slots[key[note.row]];
         const Finger &f = fingers_[slot];
         const int pitch = foldOctaves(f.key, lane.octave + octave[note.row], held[0], settings_.range);
         if (pitch < 0 || pitch > 127)
            continue;
         const double length = std::min(note.length / static_cast<double>(kQuarters), static_cast<double>(len - step));
         const int hits = hitsFor(lane, f.pressure);
         if (lane.pressure)
            hits_[step] = static_cast<uint8_t>(hits);
         const int velocity = velocityFor(lane, f.velocity, f.pressure);
         const double each = length / hits, gate = hits > 1 ? 0.6 : 1.0;
         for (int h = 0; h < hits && pendingCount_ + 2 <= kMaxPending; ++h) {
            const uint32_t serial = ++serial_ ? serial_ : ++serial_;
            const double on = static_cast<double>(j) + h * each;
            Pending e{on, serial, f.channel, static_cast<uint8_t>(pitch), static_cast<uint8_t>(velocity),
                      static_cast<int8_t>(slot), true};
            pending_[pendingCount_++] = e;
            e.m = on + each * gate;
            e.on = false;
            pending_[pendingCount_++] = e;
         }
      }
   }

   void sendOn(uint32_t t, const Pending &e, OutBuffer &out) {
      Sound &s = sounding_[e.channel][e.key];
      if (s.serial)
         out.push({t, static_cast<uint8_t>(0x80 | e.channel), e.key, 0});
      // The finger's MIDI expression first, as MPE asks, so the note starts bent, pressed and slid
      // where the finger is now; a host that turns MIDI into notes starts a new note from it.
      const Finger &f = fingers_[e.finger];
      const uint8_t ch = e.channel;
      if (f.source[2] == Source::Channel) {
         const int b = std::clamp(static_cast<int>(std::lround(f.bend * 8192.0)) + 8192, 0, 16383);
         out.push({t, static_cast<uint8_t>(0xE0 | ch), static_cast<uint8_t>(b & 127), static_cast<uint8_t>(b >> 7)});
      }
      if (f.source[0] == Source::Channel)
         out.push({t, static_cast<uint8_t>(0xD0 | ch), toByte(f.pressure), 0});
      if (f.source[1] == Source::Channel)
         out.push({t, static_cast<uint8_t>(0xB0 | ch), 74, toByte(f.slide)});
      out.push({t, static_cast<uint8_t>(0x90 | e.channel), e.key, e.velocity, e.finger});
      if (f.source[0] == Source::Poly)
         out.push({t, static_cast<uint8_t>(0xA0 | ch), e.key, toByte(f.pressure)});
      s.serial = e.serial;
      s.finger = e.finger;
   }

   void sendOff(uint32_t t, const Pending &e, OutBuffer &out) {
      Sound &s = sounding_[e.channel][e.key];
      if (s.serial != e.serial)
         return;
      out.push({t, static_cast<uint8_t>(0x80 | e.channel), e.key, 0});
      s = {};
   }

   // Lets go of a finger: its queued notes are dropped and its sounding notes end at `t`.
   void release(int slot, uint32_t t, OutBuffer &out) {
      int w = 0;
      for (int i = 0; i < pendingCount_; ++i)
         if (pending_[i].finger != slot)
            pending_[w++] = pending_[i];
      pendingCount_ = w;
      for (int ch = 0; ch < 16; ++ch)
         for (int k = 0; k < 128; ++k)
            if (sounding_[ch][k].serial && sounding_[ch][k].finger == slot) {
               out.push({t, static_cast<uint8_t>(0x80 | ch), static_cast<uint8_t>(k), 0});
               sounding_[ch][k] = {};
            }
      fingers_[slot] = {};
   }

   Settings settings_;
   Bank bank_;
   int selected_ = 0, playing_ = 0;
   bool played_ = false;
   int64_t slotStart_ = 0;
   std::array<Finger, kMaxFingers> fingers_{};
   std::array<ChannelState, 16> channels_{};
   bool sustain_ = false;
   Sound sounding_[16][128]{};
   Pending pending_[kMaxPending];
   int pendingCount_ = 0;
   uint32_t serial_ = 0;
   uint32_t dice_ = 0x9e3779b9u; // xorshift state for Chance

   uint32_t roll() {
      dice_ ^= dice_ << 13;
      dice_ ^= dice_ >> 17;
      dice_ ^= dice_ << 5;
      return dice_;
   }
   std::array<uint8_t, kSteps> hits_{};

   // The clock: steps at the block's start and per sample, where the next block should start, and
   // the free-running clock used while the host is stopped.
   Block block_;
   double start_ = 0.0, perSample_ = 0.0, expected_ = 0.0, free_ = 0.0, stepBeats_ = 0.25;
   double origin_ = 0.0, position_ = 0.0;
   uint32_t cur_ = 0;
   int64_t next_ = kUnset;
   bool started_ = false, wasHost_ = false, dropLatched_ = false;
};

} // namespace jarp

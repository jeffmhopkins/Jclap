#pragma once

// JDrummer's engine: plays the song on a clock of 48 ticks a beat, from the host's transport or its
// own. Every tick it asks which section, variation or fill is playing, and when a tick lands on a
// pattern's step it plays that step's hits: each through its chance, then VARY and GHOSTS, rolled
// into its ratchet, swung and nudged. Events are scheduled a little ahead (so a hit nudged early can
// still be early) and sent at their sample in the block.
//
// Section changes (from the Section param, the panel or a section note) wait for the boundary SWITCH
// AT names; a fill can lead into them. Header only, so the tests drive it without a host.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

#include "generator.h"
#include "kit.h"
#include "params.h"
#include "shared.h"

namespace jdrummer {

struct Settings {
   int section = 0, variation = 0;
   int switchAt = 1, switchBy = 0;
   bool song = false;
   double vary = 0.2, ghosts = 0.0;
   int sameDiceBars = 4, autoFillBars = 8;
   double swing = 0.5;
   int swingFeel = 0; // 0 swings the sixteenths, 1 the eighths
   int sync = 0;
   bool play = false;
   double freeTempo = 120.0;
   int kit = kDrumKit;
   int percKit = kPercussionKit; // the kit the percussionist plays
   int channel = 9, percChannel = 10; // 0-based
   int noteMap = 0, noteOctave = 0, noteChannel = 0;
   bool fillInto = true, velEnergy = false, heldOnly = false, passNotes = true;
   std::array<int, kActionCount> learn{};
   uint16_t muted = 0, soloed = 0; // EDIT's M and S, by pad
   // The note each action answers to, by the map in use.
   int noteFor(int action) const {
      return noteMap == 1 ? learn[static_cast<size_t>(action)] : 12 * (noteOctave + 1) + kActionKey[action];
   }
};

struct Block {
   uint32_t frames = 0;
   double sampleRate = 48000.0;
   double tempo = 120.0;
   bool hasTransport = false;
   bool playing = false;
   double beats = 0.0;
};

struct MidiOut {
   uint32_t time;
   uint8_t status, data1, data2;
};

struct OutBuffer {
   static constexpr size_t kCapacity = 2048;
   std::array<MidiOut, kCapacity> items{};
   size_t count = 0;
   void clear() { count = 0; }
   void push(uint32_t time, uint8_t status, uint8_t d1, uint8_t d2) {
      if (count < kCapacity)
         items[count++] = {time, status, d1, d2};
   }
};

class Engine {
public:
   void setSong(const Song &s) { song_ = s; }
   const Song &song() const { return song_; }

   void setSettings(const Settings &s) {
      // The Section and Variation params act when they change, so a section picked by note isn't
      // pulled back by a param that still holds the old one.
      if (s.section != settings_.section && s.switchBy != 1)
         queueSection(std::clamp(s.section, 0, kSections - 1), false);
      if (s.variation != settings_.variation && s.switchBy != 1)
         pendingVar_ = std::clamp(s.variation, 0, kVariations - 1);
      if (s.play && !settings_.play)
         stopped_ = false;
      settings_ = s;
   }
   const Settings &settings() const { return settings_; }

   // Starts at the Section param's section, as when the plugin is first made or loaded.
   void reset() {
      queueLen_ = 0;
      running_ = false;
      section_ = std::clamp(settings_.section, 0, kSections - 1);
      variation_ = std::clamp(settings_.variation, 0, kVariations - 1);
      pending_ = -1;
      pendingVar_ = -1;
      fill_ = fillPending_ = false;
      stopped_ = false;
      for (auto &k : held_)
         k = 0;
      heldSections_ = 0;
      muteHats_ = muteKick_ = hold_ = false;
   }

   // A note from the host: true if it was a section note (and is used up), false to pass it on.
   bool noteOn(int channel, int key, int velocity) {
      if (learning_ >= 0) {
         learned_ = key;
         learning_ = -1;
         return true;
      }
      const int action = actionFor(channel, key);
      if (action < 0)
         return false;
      held_[static_cast<size_t>(key & 127)] = static_cast<uint8_t>(action + 1);
      act(action, velocity, true);
      return true;
   }
   bool noteOff(int channel, int key) {
      const uint8_t a = held_[static_cast<size_t>(key & 127)];
      if (!a) {
         return actionFor(channel, key) >= 0;
      }
      held_[static_cast<size_t>(key & 127)] = 0;
      act(a - 1, 0, false);
      return true;
   }

   // From the panel: as a section note played at a medium velocity, pressed and let go.
   void request(int action) {
      act(action, 100, true);
      if (action == kActMuteHats || action == kActMuteKick || action == kActHold)
         return; // held from the panel: toggled
      if (action <= kActOutro && heldSections_ > 0)
         --heldSections_;
   }
   void toggleHeld(int action) {
      if (action == kActMuteHats)
         muteHats_ = !muteHats_;
      else if (action == kActMuteKick)
         muteKick_ = !muteKick_;
      else if (action == kActHold)
         hold_ = !hold_;
   }

   // Learn: the next note in becomes the answer, read back with learned().
   void learn(bool on) {
      learning_ = on ? 1 : -1;
      if (on)
         learned_ = -1;
   }
   int learned() {
      const int n = learned_;
      learned_ = -1;
      return n;
   }

   // Plays a block: all of it, after any notes in it have been taken.
   void process(const Block &b, OutBuffer &out) {
      const bool hostRunning = b.hasTransport && b.playing;
      bool following = false, run = false;
      double tempo = b.tempo;
      switch (settings_.sync) {
      case 1: following = true; run = hostRunning; break;
      case 2: run = settings_.play; tempo = settings_.freeTempo; break;
      default:
         if (hostRunning) {
            following = true;
            run = true;
         } else {
            run = settings_.play;
            tempo = settings_.freeTempo;
         }
      }
      if (hostRunning && !hostWasRunning_)
         stopped_ = false;
      hostWasRunning_ = hostRunning;
      if (stopped_)
         run = false;
      tempo_ = std::clamp(tempo, 1.0, 999.0);
      following_ = following;
      hostPlaying_ = hostRunning;
      const double perFrame = tempo_ / 60.0 / std::max(1.0, b.sampleRate);

      if (!run) {
         if (running_)
            stopAll(out);
         running_ = false;
         return;
      }
      double b0;
      if (following)
         b0 = b.beats;
      else {
         if (!running_ || followingBefore_)
            freeBeat_ = 0.0;
         b0 = freeBeat_;
      }
      // A start, or a jump in the host's position: pick up from here, at the bar.
      if (!running_ || following != followingBefore_ || std::abs(b0 - expected_) > 0.02) {
         flushOffs(out);
         queueLen_ = 0;
         const int64_t t = static_cast<int64_t>(std::ceil(b0 * kTicksPerBeat - 1e-6));
         nextTick_ = t;
         if (!running_ || following != followingBefore_) {
            // From the top of the section, at the bar the playhead is in.
            const int64_t bar = static_cast<int64_t>(std::floor(b0 / kBeatsPerBar));
            anchor_ = bar * kTicksPerBar;
            barStarted_ = false;
            sectionStarted_ = true;
            absoluteBar_ = 0;
            salt_ = mix(salt_, 0x5eed);
         } else {
            const int64_t bar = static_cast<int64_t>(std::floor(b0 / kBeatsPerBar));
            anchor_ = bar * kTicksPerBar;
         }
      }
      running_ = true;
      followingBefore_ = following;
      const double b1 = b0 + perFrame * b.frames;
      const double early = 0.045 * tempo_ / 60.0; // the most a hit can be pulled ahead, in beats
      while (static_cast<double>(nextTick_) / kTicksPerBeat < b1 + early) {
         tick(nextTick_);
         ++nextTick_;
      }
      emit(b0, perFrame, b.frames, out);
      expected_ = b1;
      if (!following)
         freeBeat_ = b1;
   }

   // Every note sounding stops now.
   void stopAll(OutBuffer &out) {
      flushOffs(out);
      queueLen_ = 0;
   }

   void fillFrame(Frame &f) const {
      f.running = running_;
      f.following = following_;
      f.hostPlaying = hostPlaying_;
      f.tempo = tempo_;
      const int64_t since = std::max<int64_t>(0, lastTick_ - anchor_);
      f.bar = static_cast<int>(since / kTicksPerBar);
      f.beat = static_cast<int>((since % kTicksPerBar) / kTicksPerBeat);
      f.section = section_;
      f.variation = barVar_;
      f.fill = fill_;
      f.queued = pending_;
      f.queuedVar = pendingVar_;
      f.step = running_ ? step_ : -1;
      f.steps = steps_;
      f.hits = hits_;
      f.percHits = percHits_;
      f.muteHats = muteHats_;
      f.muteKick = muteKick_;
      f.hold = hold_;
      f.stopped = stopped_;
   }

   int section() const { return section_; }
   int variation() const { return variation_; }
   bool running() const { return running_; }

private:
   int actionFor(int channel, int key) const {
      if (settings_.switchBy == 2)
         return -1;
      if (settings_.noteChannel > 0 && channel != settings_.noteChannel - 1)
         return -1;
      for (int a = 0; a < kActionCount; ++a)
         if (settings_.noteFor(a) == key)
            return a;
      return -1;
   }

   void act(int action, int velocity, bool down) {
      if (action == kActMuteHats) {
         muteHats_ = down;
         return;
      }
      if (action == kActMuteKick) {
         muteKick_ = down;
         return;
      }
      if (action == kActHold) {
         hold_ = down;
         return;
      }
      if (action <= kActOutro) {
         if (!down) {
            heldSections_ = std::max(0, heldSections_ - 1);
            return;
         }
         ++heldSections_;
      }
      if (!down)
         return;
      if (settings_.velEnergy && (action <= kActOutro || action == kActNext || action == kActPrev))
         energy_ = 0.55 + 0.45 * std::clamp(velocity, 1, 127) / 127.0;
      switch (action) {
      case kActFill: fillPending_ = true; break;
      case kActVar1: case kActVar2: case kActVar3: case kActVar4: pendingVar_ = action - kActVar1; break;
      case kActNext: stopped_ = false; queueSection((target() + 1) % kSections, false); break;
      case kActPrev: stopped_ = false; queueSection((target() + kSections - 1) % kSections, false); break;
      case kActRestart: stopped_ = false; queueSection(section_, true); break;
      case kActStop: stopped_ = true; break;
      default:
         stopped_ = false;
         queueSection(action, false);
         break;
      }
   }

   int target() const { return pending_ >= 0 ? pending_ : section_; }

   void queueSection(int s, bool restart) {
      if (s == section_ && !restart && running_) {
         pending_ = -1;
         return;
      }
      pending_ = s;
      restart_ = restart;
      if (!running_) {
         // Not playing: it is simply the section to start on.
         section_ = s;
         pending_ = -1;
      }
   }

   // Whether a switch queued now takes effect at tick t (on a beat).
   bool switchesAt(int64_t t) const {
      const int64_t rel = t - anchor_;
      if (rel % kTicksPerBeat)
         return false;
      const int64_t bar = rel / kTicksPerBar;
      const bool onBar = rel % kTicksPerBar == 0;
      switch (settings_.switchAt) {
      case 0: return true;
      case 1: return onBar;
      case 2: return onBar && bar % 2 == 0;
      case 3: return onBar && bar % 4 == 0;
      default: {
         const int bars = std::max<int>(1, song_.sections[static_cast<size_t>(section_)].bars);
         return onBar && bar % bars == 0;
      }
      }
   }

   double dice(int bar, int step, int pad, int what) const {
      const uint32_t h = mix(mix(salt_, static_cast<uint32_t>(bar)),
                             static_cast<uint32_t>(step * 64 + pad + (what << 12)));
      return (h >> 8) * (1.0 / 16777216.0);
   }

   void tick(int64_t t) {
      lastTick_ = t;
      const int64_t rel0 = t - anchor_;
      // A beat: a queued switch, a fill starting.
      if (rel0 % kTicksPerBeat == 0 && rel0 > 0) {
         if (pending_ >= 0 && switchesAt(t)) {
            section_ = pending_;
            pending_ = -1;
            anchor_ = t;
            sectionStarted_ = true;
            fill_ = false;
            if (pendingVar_ >= 0) {
               variation_ = pendingVar_;
               pendingVar_ = -1;
            }
         } else if (fillPending_) {
            fill_ = true;
            fillPending_ = false;
         } else if (settings_.fillInto && pending_ >= 0 && settings_.switchAt != 0 && rel0 % kTicksPerBar != 0) {
            // The fill leads into a switch that lands at the end of this bar.
            if (switchesAt(t - rel0 % kTicksPerBar + kTicksPerBar))
               fill_ = true;
         }
      }
      const int64_t rel = t - anchor_;
      const Section &sec = song_.sections[static_cast<size_t>(section_)];
      // A bar: the song moves on, the fill ends, this bar's variation and dice.
      if (rel % kTicksPerBar == 0) {
         // A fill just ended: the next bar comes in on a crash.
         const bool afterFill = fillPlayed_;
         fillPlayed_ = false;
         int bar = static_cast<int>(rel / kTicksPerBar);
         if (bar > 0 && fillPending_) {
            fill_ = true;
            fillPending_ = false;
         } else if (bar > 0)
            fill_ = false;
         if (settings_.song && !hold_ && pending_ < 0 && bar >= sec.bars) {
            section_ = (section_ + 1) % kSections;
            anchor_ = t;
            sectionStarted_ = true;
            bar = 0;
         }
         if (pendingVar_ >= 0) {
            variation_ = pendingVar_;
            pendingVar_ = -1;
         }
         const Section &now = song_.sections[static_cast<size_t>(section_)];
         ++absoluteBar_;
         const int same = settings_.sameDiceBars;
         diceBar_ = hold_ ? diceBar_ : same > 0 ? bar % same : static_cast<int>(absoluteBar_);
         barVar_ = variation_;
         if (!hold_ && settings_.vary > 0.0 && dice(diceBar_, 0, 0, 7) < settings_.vary * 0.35) {
            const int other = (variation_ + 1 + static_cast<int>(dice(diceBar_, 0, 0, 8) * 3)) % kVariations;
            if (!now.vars[static_cast<size_t>(other)].empty())
               barVar_ = other;
         }
         // Fills: auto every N bars, and the last bar before the song moves on.
         const int every = settings_.autoFillBars;
         if (every > 0 && (bar + 1) % every == 0)
            fill_ = true;
         if (settings_.song && settings_.fillInto && pending_ < 0 && bar + 1 == now.bars)
            fill_ = true;
         if (settings_.fillInto && pending_ >= 0 && switchesAt(t + kTicksPerBar) && settings_.switchAt != 0)
            fill_ = true;
         if (now.fill.empty())
            fill_ = false;
         crashNow_ = afterFill && !fill_ && now.energy >= 35;
      }
      const Section &s = song_.sections[static_cast<size_t>(section_)];
      const int64_t r = t - anchor_;
      // A crash on the one: when a section that has one starts, and after a fill.
      if (sectionStarted_ || crashNow_) {
         if ((s.crash || crashNow_) && settings_.kit == kDrumKit && !muteHats_ &&
             !(settings_.muted >> CR & 1) && !(settings_.soloed && !(settings_.soloed >> CR & 1)))
            schedule(t, 0.0, settings_.channel, CR, 110, 0.25, false);
         sectionStarted_ = false;
         crashNow_ = false;
      }
      const int bv = std::clamp(barVar_, 0, kVariations - 1);
      const Pattern &main = s.vars[static_cast<size_t>(bv)].empty() ? s.vars[0] : s.vars[static_cast<size_t>(bv)];
      const Pattern &p = fill_ ? s.fill : main;
      if (fill_)
         fillPlayed_ = true;
      playLayer(p, r, t, settings_.channel, false);
      if (!s.perc.empty()) {
         // The percussionist plays its variation with the drummer's, and its fill with the drummer's.
         const Pattern *pp = &s.perc;
         if (fill_ && !s.percFill.empty())
            pp = &s.percFill;
         else if (!fill_ && bv > 0 && !s.percVars[static_cast<size_t>(bv - 1)].empty())
            pp = &s.percVars[static_cast<size_t>(bv - 1)];
         playLayer(*pp, r, t, settings_.percChannel, true);
      }
   }

   // How late a step is swung, in ticks. Sixteenths: every second one late. Eighths: the off-beat
   // eighth late, the sixteenths either side of it moved with it, so a sixteenth grid swings like a
   // shuffle (at 67% the eighths are triplets).
   double swingTicks(const Pattern &p, int step, int stepTicks) const {
      const double sw = settings_.swing;
      if (p.rate == kRate8)
         return step % 2 == 1 ? (sw - 0.5) * 2.0 * stepTicks : 0.0;
      if (p.rate != kRate16)
         return 0.0;
      if (settings_.swingFeel == 0)
         return step % 2 == 1 ? (sw - 0.5) * 2.0 * stepTicks : 0.0;
      switch (step % 4) {
      case 1: return (sw / 2.0 - 0.25) * kTicksPerBeat;
      case 2: return (sw - 0.5) * kTicksPerBeat;
      case 3: return ((1.0 + sw) / 2.0 - 0.75) * kTicksPerBeat;
      default: return 0.0;
      }
   }

   void playLayer(const Pattern &p, int64_t rel, int64_t t, int channel, bool perc) {
      const int stepTicks = kRateTicks[std::min<int>(p.rate, kRate32)];
      if (rel % stepTicks)
         return;
      const int steps = std::max<int>(1, p.steps);
      const int step = static_cast<int>((rel / stepTicks) % steps);
      if (!perc) {
         step_ = step;
         steps_ = steps;
      }
      const double swing = swingTicks(p, step, stepTicks);
      const int kit = perc ? settings_.percKit : settings_.kit;
      const double stepBeats = static_cast<double>(stepTicks) / kTicksPerBeat;
      const double vary = hold_ ? 0.0 : settings_.vary;
      const int ghostPad = p.uses(SN) ? SN : p.uses(CL) ? CL : -1;
      const int bar = diceBar_ * 4 + (perc ? 2 : 0);
      for (int pad = 0; pad < kPads; ++pad) {
         if (!perc && ((muteHats_ && isHatPad(kit, pad)) || (muteKick_ && isKickPad(kit, pad))))
            continue;
         if (!perc && ((settings_.muted >> pad & 1) || (settings_.soloed && !(settings_.soloed >> pad & 1))))
            continue;
         const Hit &h = p.at(pad, step);
         if (!h.on()) {
            // GHOSTS: soft snare hits around the beat.
            if (!perc && pad == ghostPad && settings_.ghosts > 0.0 && step % 2 == 1 && settings_.kit != kFxKit &&
                !p.at(pad, (step + 1) % steps).on() && !p.at(pad, (step + steps - 1) % steps).on() &&
                dice(bar, step, pad, 1) < settings_.ghosts * 0.45)
               schedule(t, swing, channel, pad, 22 + static_cast<int>(dice(bar, step, pad, 2) * 20), stepBeats, perc);
            // VARY: a hat now and then where there was none.
            else if (vary > 0.0 && isHatPad(kit, pad) && p.uses(pad) && step % 2 == 1 &&
                     dice(bar, step, pad, 3) < vary * 0.1)
               schedule(t, swing, channel, pad, 45, stepBeats, perc);
            continue;
         }
         if (h.chance < 100 && dice(bar, step, pad, 4) * 100.0 >= h.chance)
            continue;
         const bool anchorHit = step % 4 == 0 && (isKickPad(kit, pad) || padRole(kit, pad) == kSnare);
         if (vary > 0.0 && !anchorHit && h.vel < kAccent && dice(bar, step, pad, 5) < vary * 0.22)
            continue;
         double vel = h.vel * energy_;
         if (vary > 0.0)
            vel += (dice(bar, step, pad, 6) * 2.0 - 1.0) * vary * 14.0;
         const double nudge = h.nudge * tempo_ / 60000.0; // ms to beats
         const int r = std::clamp<int>(h.ratchet, 1, kMaxRatchet);
         for (int k = 0; k < r; ++k) {
            const double at = swing / kTicksPerBeat + nudge + stepBeats * k / r;
            schedule(t, at * kTicksPerBeat, channel, pad, static_cast<int>(vel * (k == 0 ? 1.0 : 0.82)), stepBeats / r,
                     perc);
         }
      }
   }

   // Queues a hit at tick t plus `offsetTicks`, and its note-off half its length later.
   void schedule(int64_t t, double offsetTicks, int channel, int pad, int velocity, double lengthBeats, bool perc) {
      if (settings_.heldOnly && heldSections_ == 0)
         return;
      const double at = (static_cast<double>(t) + offsetTicks) / kTicksPerBeat;
      const uint8_t key = static_cast<uint8_t>(kFirstNote + pad);
      const uint8_t ch = static_cast<uint8_t>(channel & 15);
      push({at, static_cast<uint8_t>(0x90 | ch), key, static_cast<uint8_t>(std::clamp(velocity, 1, 127)), perc});
      push({at + std::max(0.02, lengthBeats * 0.5), static_cast<uint8_t>(0x80 | ch), key, 0, perc});
   }

   struct Event {
      double beat;
      uint8_t status, d1, d2;
      bool perc;
   };

   void push(const Event &e) {
      if (queueLen_ < kQueue)
         queue_[queueLen_++] = e;
   }

   // Sends the queued events that fall in this block, in time order, offs before ons at a time.
   void emit(double b0, double perFrame, uint32_t frames, OutBuffer &out) {
      const double b1 = b0 + perFrame * frames;
      std::sort(queue_.begin(), queue_.begin() + static_cast<long>(queueLen_), [](const Event &a, const Event &b) {
         if (a.beat != b.beat)
            return a.beat < b.beat;
         return (a.status & 0xF0) < (b.status & 0xF0);
      });
      size_t keep = 0;
      for (size_t i = 0; i < queueLen_; ++i) {
         const Event &e = queue_[i];
         if (e.beat < b1 - 1e-7) {
            const double f = (e.beat - b0) / perFrame;
            const uint32_t time = static_cast<uint32_t>(std::clamp<double>(std::round(f), 0.0, frames ? frames - 1 : 0));
            out.push(time, e.status, e.d1, e.d2);
            if ((e.status & 0xF0) == 0x90) {
               auto &counts = e.perc ? percHits_ : hits_;
               ++counts[static_cast<size_t>((e.d1 - kFirstNote) & 15)];
            }
         } else
            queue_[keep++] = e;
      }
      queueLen_ = keep;
   }

   // Sends every queued note-off now (and drops the note-ons not yet sent).
   void flushOffs(OutBuffer &out) {
      for (size_t i = 0; i < queueLen_; ++i)
         if ((queue_[i].status & 0xF0) == 0x80) {
            // Only for notes that started: a pending note-on just behind it is dropped with it.
            bool started = true;
            for (size_t j = 0; j < queueLen_; ++j)
               if ((queue_[j].status & 0xF0) == 0x90 && queue_[j].d1 == queue_[i].d1 &&
                   (queue_[j].status & 15) == (queue_[i].status & 15) && queue_[j].beat <= queue_[i].beat)
                  started = false;
            if (started)
               out.push(0, queue_[i].status, queue_[i].d1, 0);
         }
      queueLen_ = 0;
   }

   static constexpr size_t kQueue = 1024;
   Song song_;
   Settings settings_;
   std::array<Event, kQueue> queue_{};
   size_t queueLen_ = 0;

   bool running_ = false, following_ = false, followingBefore_ = false, hostPlaying_ = false;
   bool hostWasRunning_ = false;
   double tempo_ = 120.0, freeBeat_ = 0.0, expected_ = -1.0;
   int64_t nextTick_ = 0, anchor_ = 0, lastTick_ = 0, absoluteBar_ = 0;
   uint32_t salt_ = 0x1234;
   int section_ = 0, variation_ = 0, barVar_ = 0, pending_ = -1, pendingVar_ = -1;
   bool restart_ = false, fill_ = false, fillPending_ = false, sectionStarted_ = false, barStarted_ = false;
   bool fillPlayed_ = false, crashNow_ = false;
   int diceBar_ = 0;
   int step_ = -1, steps_ = 16;
   double energy_ = 1.0;
   bool stopped_ = false, muteHats_ = false, muteKick_ = false, hold_ = false;
   std::array<uint8_t, 128> held_{};
   int heldSections_ = 0;
   int learning_ = -1, learned_ = -1;
   std::array<uint32_t, kPads> hits_{}, percHits_{};
};

} // namespace jdrummer

#pragma once

// JComp's engine: plays the song on a clock of 48 ticks a beat, from the host's transport or its
// own. On each step of the take playing it asks which chord is up (the section's progression, the
// chord held on the chord keys, or both), voices it led from the chord before (harmony.h), and
// plays the hit: through its chance and VARY, swung, nudged and laid back by TIMING, its notes
// started together, rolled or strummed, its length by its articulation. A few steps ahead it looks
// for a hit with a run into a new chord, and plays the run into that chord's top note.
//
// With RHYTHM FROM THE BASS, the notes coming in on the bass channel play the chords instead (all of
// them, the strong ones, or one a bar), at the sample they arrive.
//
// Section changes (from the Section param, the panel or a section note) wait for the boundary SWITCH
// AT names; a fill can lead into them. Header only, so the tests drive it without a host.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

#include "generator.h"
#include "harmony.h"
#include "params.h"
#include "shared.h"

namespace jcomp {

struct Settings {
   int section = 0, take = 0;
   int switchAt = 1, switchBy = 0;
   bool song = false;
   int sameDiceBars = 4, autoFillBars = 8;
   int sync = 0;
   bool play = false;
   double freeTempo = 100.0;
   double swing = 0.5;
   int swingFeel = 0;
   // Chords.
   int key = 4, scale = kDorian;
   int chordsFrom = 2; // progression, held notes, both
   int changeAt = 2;   // at once, next 16th, next beat, next bar
   bool latch = true, slashBass = true;
   int voiceHeld = 0;  // its own way, as held, my top note
   int chordSplit = 48;
   VoiceSettings voicing;
   // Feel.
   double timing = 8.0;     // ms behind (+) or ahead (-) of the grid
   double velocityVar = 8.0;
   double topVoice = 6.0;
   double lengths = 0.25;
   int onsets = 0;
   double vary = 0.2;
   double loose = 0.3;      // the player's: how far each chord wanders off the grid
   // Runs.
   double runs = 0.25;
   int runEveryBars = 2;
   bool runLandTop = true, runHalfStep = true, runShorten = true;
   // Output.
   int channel = 0; // 0-based
   int rhythmFrom = 0, bassChannel = 1, follow = 1;
   // Section notes.
   int noteMap = 0, noteOctave = 0, noteChannel = 0;
   bool fillInto = true, velEnergy = false, heldOnly = false, passNotes = false;
   std::array<int, kActionCount> learn{};
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
      if (s.section != settings_.section && s.switchBy != 1)
         queueSection(std::clamp(s.section, 0, kSections - 1), false);
      if (s.take != settings_.take && s.switchBy != 1)
         pendingTake_ = std::clamp(s.take, 0, kTakes - 1);
      if (s.play && !settings_.play)
         stopped_ = false;
      if (!s.velEnergy)
         energy_ = 1.0;
      settings_ = s;
   }
   const Settings &settings() const { return settings_; }

   void reset() {
      queueLen_ = 0;
      running_ = false;
      section_ = std::clamp(settings_.section, 0, kSections - 1);
      take_ = std::clamp(settings_.take, 0, kTakes - 1);
      pending_ = pendingTake_ = -1;
      fill_ = fillPending_ = false;
      energy_ = 1.0;
      stopped_ = false;
      for (auto &k : held_)
         k = 0;
      heldSections_ = 0;
      runsOff_ = holdChord_ = false;
      keys_.fill(0);
      heldChord_ = latched_ = pendingHeld_ = Chord{};
      heldNow_ = false;
      heldPending_ = false;
      last_ = Voicing{};
      lastChord_ = Chord{};
      bassQueue_ = 0;
      runTick_ = -1;
   }

   // ---- notes in ----

   // A note from the host: true if JComp used it (a section note, a chord key, the bass), false to
   // pass it on. `frame` is where in the block it came.
   bool noteOn(int channel, int key, int velocity, uint32_t frame = 0) {
      if (learning_ >= 0) {
         learned_ = key;
         learning_ = -1;
         return true;
      }
      if (settings_.rhythmFrom > 0 && channel == settings_.bassChannel) {
         if (bassQueue_ < kBassQueue)
            bassIn_[bassQueue_++] = {frame, key, velocity, true};
         return true;
      }
      const int action = actionFor(channel, key);
      if (action >= 0) {
         held_[static_cast<size_t>(key & 127)] = static_cast<uint8_t>(action + 1);
         act(action, velocity, true);
         return true;
      }
      if (settings_.chordsFrom > 0 && key >= settings_.chordSplit) {
         keys_[static_cast<size_t>(key & 127)] = 1;
         heldChanged();
         return true;
      }
      return false;
   }
   bool noteOff(int channel, int key, uint32_t frame = 0) {
      if (settings_.rhythmFrom > 0 && channel == settings_.bassChannel) {
         if (bassQueue_ < kBassQueue)
            bassIn_[bassQueue_++] = {frame, key, 0, false};
         return true;
      }
      const uint8_t a = held_[static_cast<size_t>(key & 127)];
      if (a) {
         held_[static_cast<size_t>(key & 127)] = 0;
         act(a - 1, 0, false);
         return true;
      }
      if (keys_[static_cast<size_t>(key & 127)]) {
         keys_[static_cast<size_t>(key & 127)] = 0;
         heldChanged();
         return true;
      }
      return actionFor(channel, key) >= 0;
   }

   // From the panel: as a section note played at a medium velocity, pressed and let go.
   void request(int action) {
      act(action, 100, true);
      if (action == kActRunsOff || action == kActHoldChord)
         return;
      if (action <= kActOutro && heldSections_ > 0)
         --heldSections_;
   }
   void toggleHeld(int action) {
      if (action == kActRunsOff)
         runsOff_ = !runsOff_;
      else if (action == kActHoldChord)
         holdChord_ = !holdChord_;
   }

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

   // ---- playing ----

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
         bassQueue_ = 0;
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
      if (!running_ || following != followingBefore_ || std::abs(b0 - expected_) > 0.02) {
         flushOffs(out);
         queueLen_ = 0;
         const int64_t t = static_cast<int64_t>(std::ceil(b0 * kTicksPerBeat - 1e-6));
         nextTick_ = t;
         if (!running_ || following != followingBefore_) {
            const int64_t bar = static_cast<int64_t>(std::floor(b0 / kBeatsPerBar));
            anchor_ = bar * kTicksPerBar;
            fill_ = fillPending_ = false;
            absoluteBar_ = 0;
            salt_ = mix(salt_, 0x5eed);
            last_ = Voicing{};
            lastChord_ = Chord{};
            runTick_ = -1;
            lastRunBar_ = -100;
         } else {
            const int64_t len = kTicksPerBar;
            anchor_ = t - (((t - anchor_) % len) + len) % len;
         }
      }
      running_ = true;
      followingBefore_ = following;
      const double b1 = b0 + perFrame * b.frames;
      // The bass coming in plays chords where it lands.
      for (size_t i = 0; i < bassQueue_; ++i)
         bass(bassIn_[i], b0 + bassIn_[i].frame * perFrame);
      bassQueue_ = 0;
      const double early = 0.045 * tempo_ / 60.0;
      while (static_cast<double>(nextTick_) / kTicksPerBeat < b1 + early) {
         tick(nextTick_);
         ++nextTick_;
      }
      emit(b0, perFrame, b.frames, out);
      expected_ = b1;
      if (!following)
         freeBeat_ = b1;
   }

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
      f.take = barTake_;
      f.fill = fill_;
      f.queued = pending_;
      f.queuedTake = pendingTake_;
      f.step = running_ ? step_ : -1;
      f.steps = steps_;
      f.chord = lastChord_;
      f.next = nextChord_;
      f.voices = last_.count;
      for (int i = 0; i < last_.count; ++i)
         f.voicing[static_cast<size_t>(i)] = last_.notes[static_cast<size_t>(i)];
      f.hits = hits_;
      f.held = heldNow_ ? heldChord_ : latched_;
      f.heldCount = 0;
      for (int k = 0; k < 128 && f.heldCount < 8; ++k)
         if (keys_[static_cast<size_t>(k)])
            f.heldNotes[static_cast<size_t>(f.heldCount++)] = static_cast<uint8_t>(k);
      f.bassHits = bassHits_;
      f.lastBass = lastBassKey_;
      f.stopped = stopped_;
      f.hold = holdChord_;
      f.runsOff = runsOff_;
   }

   int section() const { return section_; }
   bool running() const { return running_; }
   const Voicing &lastVoicing() const { return last_; }
   Chord heldChord() const { return heldNow_ ? heldChord_ : latched_; }

private:
   // ---- section notes ----

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
      if (action == kActRunsOff) {
         runsOff_ = down;
         return;
      }
      if (action == kActHoldChord) {
         holdChord_ = down;
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
      case kActTake1: case kActTake2: case kActTake3: case kActTake4: pendingTake_ = action - kActTake1; break;
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
      if (!running_) {
         section_ = s;
         pending_ = -1;
      }
   }

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

   // ---- held chords ----

   void heldChanged() {
      std::vector<int> notes;
      for (int k = 0; k < 128; ++k)
         if (keys_[static_cast<size_t>(k)])
            notes.push_back(k);
      if (notes.empty()) {
         heldNow_ = false;
         heldPending_ = false;
         if (!settings_.latch)
            latched_ = Chord{};
         return;
      }
      pendingHeld_ = nameChord(notes, settings_.slashBass);
      heldNotes_ = notes;
      if (settings_.changeAt == 0 || !running_)
         takeHeld();
      else
         heldPending_ = true;
   }
   void takeHeld() {
      heldChord_ = pendingHeld_;
      latched_ = heldChord_;
      heldNow_ = true;
      heldPending_ = false;
   }
   bool heldActive() const { return heldNow_ || (settings_.latch && !latched_.none()); }

   // The chord at a tick (relative to the section's start).
   Chord progressionChord(int64_t rel) const {
      const Section &sec = song_.sections[static_cast<size_t>(section_)];
      const int bar = static_cast<int>(rel / kTicksPerBar);
      const bool half = (rel % kTicksPerBar) >= kTicksPerBar / 2;
      Chord c = sec.chordAt(bar, half);
      if (!c.none()) {
         c.root = static_cast<int8_t>((c.root + transpose()) % 12);
         if (c.bass >= 0)
            c.bass = static_cast<int8_t>((c.bass + transpose()) % 12);
      }
      return c;
   }
   // The progression is written in the song's key; KEY moves it.
   int transpose() const { return ((settings_.key - song_.key) % 12 + 12) % 12; }

   Chord chordAt(int64_t rel) const {
      if (holdChord_ && !lastChord_.none())
         return lastChord_;
      if (settings_.chordsFrom == 1)
         return heldActive() ? (heldNow_ ? heldChord_ : latched_) : Chord{};
      if (settings_.chordsFrom == 2 && heldNow_)
         return heldChord_;
      return progressionChord(rel);
   }

   // ---- the clock ----

   double dice(int bar, int step, int what) const {
      const uint32_t h = mix(mix(salt_, static_cast<uint32_t>(bar)), static_cast<uint32_t>(step * 64 + (what << 12)));
      return (h >> 8) * (1.0 / 16777216.0);
   }

   void tick(int64_t t) {
      lastTick_ = t;
      // A held chord waiting for its moment.
      if (heldPending_) {
         const int64_t grid = settings_.changeAt == 1 ? 12 : settings_.changeAt == 2 ? kTicksPerBeat : kTicksPerBar;
         if ((t - anchor_) % grid == 0)
            takeHeld();
      }
      const int64_t rel0 = t - anchor_;
      if (rel0 % kTicksPerBeat == 0 && rel0 > 0) {
         if (pending_ >= 0 && switchesAt(t)) {
            section_ = pending_;
            pending_ = -1;
            anchor_ = t;
            fill_ = false;
            if (pendingTake_ >= 0) {
               take_ = pendingTake_;
               pendingTake_ = -1;
            }
         } else if (fillPending_) {
            fill_ = true;
            fillPending_ = false;
         } else if (settings_.fillInto && pending_ >= 0 && settings_.switchAt != 0 && rel0 % kTicksPerBar != 0) {
            if (switchesAt(t - rel0 % kTicksPerBar + kTicksPerBar))
               fill_ = true;
         }
      }
      const int64_t rel = t - anchor_;
      const Section &sec = song_.sections[static_cast<size_t>(section_)];
      if (rel % kTicksPerBar == 0) {
         int bar = static_cast<int>(rel / kTicksPerBar);
         if (bar > 0 && fillPending_) {
            fill_ = true;
            fillPending_ = false;
         } else if (bar > 0)
            fill_ = false;
         if (settings_.song && !holdChord_ && pending_ < 0 && bar >= sec.bars) {
            section_ = (section_ + 1) % kSections;
            anchor_ = t;
            bar = 0;
         }
         if (pendingTake_ >= 0) {
            take_ = pendingTake_;
            pendingTake_ = -1;
         }
         const Section &now = song_.sections[static_cast<size_t>(section_)];
         ++absoluteBar_;
         const int same = settings_.sameDiceBars;
         diceBar_ = same > 0 ? bar % same : static_cast<int>(absoluteBar_);
         barTake_ = take_;
         if (settings_.vary > 0.0 && dice(diceBar_, 0, 7) < settings_.vary * 0.3) {
            const int other = (take_ + 1 + static_cast<int>(dice(diceBar_, 0, 8) * 3)) % kTakes;
            if (!now.takes[static_cast<size_t>(other)].empty())
               barTake_ = other;
         }
         const int every = settings_.autoFillBars;
         if (every > 0 && (bar + 1) % every == 0)
            fill_ = true;
         if (settings_.song && settings_.fillInto && pending_ < 0 && bar + 1 == now.bars)
            fill_ = true;
         if (settings_.fillInto && pending_ >= 0 && switchesAt(t + kTicksPerBar) && settings_.switchAt != 0)
            fill_ = true;
         if (now.fill.empty())
            fill_ = false;
      }
      const Section &s = song_.sections[static_cast<size_t>(section_)];
      const int64_t r = t - anchor_;
      const int bt = std::clamp(barTake_, 0, kTakes - 1);
      const Pattern &main = s.takes[static_cast<size_t>(bt)].empty() ? s.takes[0] : s.takes[static_cast<size_t>(bt)];
      const Pattern &p = fill_ ? s.fill : main;
      const auto fillAt = [&](const Pattern &f) {
         const int64_t len = std::max(1, patternTicks(f));
         return (len - kTicksPerBar % len) % len + r % kTicksPerBar;
      };
      nextChord_ = progressionChord(r - r % (kTicksPerBar / 2) + kTicksPerBar / 2);
      for (int k = 0; k < 4 && nextChord_ == progressionChord(r); ++k)
         nextChord_ = progressionChord(r - r % (kTicksPerBar / 2) + (k + 2) * (kTicksPerBar / 2));
      if (settings_.rhythmFrom == 1)
         return;
      playStep(p, fill_ ? fillAt(p) : r, t, r);
   }

   double swingTicks(const Pattern &p, int step, int stepTicks) const {
      const double sw = settings_.swing;
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

   // One step of a pattern: a run starting, or a hit.
   void playStep(const Pattern &p, int64_t prel, int64_t t, int64_t secRel) {
      const int stepTicks = kRateTicks[std::min<int>(p.rate, kRate8T)];
      if (prel % stepTicks)
         return;
      const int steps = std::max<int>(1, p.steps);
      const int step = static_cast<int>((prel / stepTicks) % steps);
      step_ = step;
      steps_ = steps;
      const int bar = static_cast<int>(secRel / kTicksPerBar);
      if (settings_.heldOnly && !heldNow_)
         return;
      // A run coming: look a few steps ahead for a hit with a run into a new chord.
      if (runTick_ < 0 && settings_.runs > 0.0 && !runsOff_)
         lookForRun(p, step, steps, stepTicks, t, secRel, bar);
      if (runTick_ >= 0 && t < runTick_)
         return; // the run has these steps
      const Hit &h = p.at(step);
      const bool target = runTick_ == t;
      if (!h.on()) {
         if (target)
            runTick_ = -1;
         return;
      }
      if (!target) {
         if (h.chance < 100 && dice(diceBar_, step, 4) * 100.0 >= h.chance)
            return;
         // VARY drops a soft hit now and then (never one on a beat).
         const bool onBeat = step % (p.rate == kRate8T ? 3 : 4) == 0;
         if (settings_.vary > 0.0 && !onBeat && h.vel < kNormal && dice(diceBar_, step, 5) < settings_.vary * 0.25)
            return;
      }
      const double stepBeats = static_cast<double>(stepTicks) / kTicksPerBeat;
      // How long, in steps, until the next hit (for RING and the run's room).
      int gap = 1;
      while (gap < steps && !p.at((step + gap) % steps).on())
         ++gap;
      const Chord c = chordAt(secRel + (h.push ? kTicksPerBeat / 2 : 0));
      Voicing v;
      if (target) {
         v = runVoicing_;
         runTick_ = -1;
      } else
         v = voicingFor(c, h.voicing);
      const double swing = swingTicks(p, step, stepTicks) / kTicksPerBeat;
      double vel = h.vel * energy_;
      if (settings_.vary > 0.0)
         vel += (dice(diceBar_, step, 6) * 2.0 - 1.0) * settings_.vary * 10.0;
      playChord(t, swing + h.nudge * tempo_ / 60000.0, c, v, h, vel, h.len * stepBeats, gap * stepBeats, step);
   }

   Voicing voicingFor(const Chord &c, int pick) {
      if (c.none())
         return Voicing{};
      const bool held = heldNow_ && (settings_.chordsFrom == 1 || settings_.chordsFrom == 2) && c == heldChord_;
      if (held && settings_.voiceHeld == 1) {
         Voicing v;
         for (int n : heldNotes_)
            if (v.count < kMaxVoices)
               v.notes[static_cast<size_t>(v.count++)] = static_cast<uint8_t>(n);
         return v;
      }
      if (held && settings_.voiceHeld == 2 && !heldNotes_.empty())
         return voiceUnder(c, heldNotes_.back(), last_.empty() ? nullptr : &last_, settings_.voicing);
      return voice(c, last_.empty() ? nullptr : &last_, settings_.voicing, pick, lastChord_.none() ? nullptr : &lastChord_);
   }

   void lookForRun(const Pattern &p, int step, int steps, int stepTicks, int64_t t, int64_t secRel, int bar) {
      if (bar - lastRunBar_ < settings_.runEveryBars && lastRunBar_ > -100)
         return;
      for (int len : {3, 2, 4}) {
         const Hit &h = p.at((step + len) % steps);
         if (!h.on() || !h.run)
            continue;
         const int64_t at = secRel + static_cast<int64_t>(len) * stepTicks;
         const Chord c = chordAt(at + (h.push ? kTicksPerBeat / 2 : 0));
         if (c.none() || c == chordAt(secRel) || last_.empty())
            return;
         // RUNS sets how many of the chances are taken.
         if (dice(diceBar_ + bar * 7, step, 9) >= std::min(1.0, settings_.runs * 1.6))
            return;
         runVoicing_ = voicingFor(c, h.voicing);
         if (runVoicing_.empty())
            return;
         int land = runVoicing_.top();
         if (!settings_.runLandTop) {
            // ANY NOTE: the chord note nearest the line's last note.
            int best = 99;
            for (int i = 0; i < runVoicing_.count; ++i) {
               const int n = runVoicing_.notes[static_cast<size_t>(i)];
               if (std::abs(n - last_.top()) < best) {
                  best = std::abs(n - last_.top());
                  land = n;
               }
            }
         }
         const std::vector<int> run = passingRun(last_.top(), land, len, (settings_.key + 0) % 12, settings_.scale,
                                                 settings_.runHalfStep);
         const double stepBeats = static_cast<double>(stepTicks) / kTicksPerBeat;
         const double lay = settings_.timing * tempo_ / 60000.0;
         const double start = static_cast<double>(t) / kTicksPerBeat + lay;
         if (settings_.runShorten)
            cutFrom(start - 0.004);
         for (size_t k = 0; k < run.size(); ++k) {
            const double at2 = start + k * stepBeats + swingTicks(p, (step + static_cast<int>(k)) % steps, stepTicks) / kTicksPerBeat;
            const int vel = static_cast<int>((62 + 10 * k) * energy_);
            note(at2, run[k], vel, stepBeats * 0.85);
         }
         runTick_ = t + static_cast<int64_t>(len) * stepTicks;
         lastRunBar_ = bar;
         return;
      }
   }

   // Plays a chord: its notes started together, rolled or strummed; its length by articulation.
   void playChord(int64_t t, double offsetBeats, const Chord &c, Voicing v, const Hit &h, double vel, double lenBeats,
                  double gapBeats, int step) {
      if (c.none() || v.empty())
         return;
      // The part of the chord the hit plays.
      if (h.part == kPartTop) {
         v.notes[0] = static_cast<uint8_t>(v.top());
         v.count = 1;
      } else if (h.part == kPartUpper && v.count > 2) {
         v.notes[0] = v.notes[static_cast<size_t>(v.count - 2)];
         v.notes[1] = v.notes[static_cast<size_t>(v.count - 1)];
         v.count = 2;
      } else if (h.part == kPartBass) {
         const int pc = c.bass >= 0 ? c.bass : c.root;
         int n = settings_.voicing.floor;
         while (n % 12 != pc)
            ++n;
         v.notes[0] = static_cast<uint8_t>(n);
         v.count = 1;
      }
      if (h.part == kPartChord) {
         last_ = v;
         lastChord_ = c;
      }
      ++hits_;
      const double ms = tempo_ / 60000.0; // a millisecond, in beats
      const double wander = (dice(diceBar_, step, 11) * 2.0 - 1.0) * settings_.loose * 6.0 * ms;
      const double at = static_cast<double>(t) / kTicksPerBeat + offsetBeats + settings_.timing * ms + wander;
      // How long: the take's length, or the articulation's, then LENGTHS varies it.
      double len = lenBeats * 0.92;
      switch (h.art) {
      case kArtChop: len = std::min(lenBeats, 0.25) * 0.45; break;
      case kArtHalf: len = lenBeats * 0.5; break;
      case kArtRing:
      case kArtTopRings: len = gapBeats * 0.97; break;
      default: break;
      }
      len *= 1.0 + (dice(diceBar_, step, 12) * 2.0 - 1.0) * settings_.lengths;
      len = std::max(len, 0.05 * tempo_ / 60.0);
      cutFrom(at - 0.003);
      const int n = v.count;
      const bool up = settings_.onsets == 3 && (step % 4) >= 2; // strum both: the off-beats strum up
      for (int i = 0; i < n; ++i) {
         const int idx = up ? n - 1 - i : i;
         const int key = v.notes[static_cast<size_t>(idx)];
         double on = at;
         switch (settings_.onsets) {
         case 0: on += (dice(diceBar_, step * 8 + i, 13) - 0.5) * 2.0 * ms; break;
         case 1: on += i * 14.0 * ms; break;
         default: on += i * 9.0 * ms; break;
         }
         double vv = vel + (dice(diceBar_, step * 8 + i, 14) * 2.0 - 1.0) * settings_.velocityVar * 0.5 +
                     (dice(diceBar_, step, 15) * 2.0 - 1.0) * settings_.velocityVar * 0.5;
         if (idx == n - 1 && n > 1)
            vv += settings_.topVoice;
         double l = len;
         if (h.art == kArtTopRings && idx != n - 1)
            l = std::min(len, lenBeats * 0.5);
         note(on, key, static_cast<int>(std::lround(vv)), l);
      }
   }

   // The bass coming in (RHYTHM FROM THE BASS): a chord where it lands, if FOLLOW takes it.
   struct BassEvent {
      uint32_t frame;
      int key, vel;
      bool on;
   };
   void bass(const BassEvent &e, double beat) {
      if (!e.on) {
         if (settings_.follow == 0 && e.key == bassKey_)
            cutFrom(beat);
         return;
      }
      ++bassHits_;
      const double inBar = std::fmod(beat - static_cast<double>(anchor_) / kTicksPerBeat, kBeatsPerBar);
      const double fromBeat = std::abs(inBar - std::round(inBar));
      const int barNo = static_cast<int>(std::floor((beat - static_cast<double>(anchor_) / kTicksPerBeat) / kBeatsPerBar));
      bool take = true;
      if (settings_.follow == 1) {
         const bool strong = e.vel >= 96 || fromBeat < 0.06 || (lastBassKey_ >= 0 && e.key >= lastBassKey_ + 10);
         take = strong && (beat - lastBassBeat_ > 0.45 || fromBeat < 0.06);
      } else if (settings_.follow == 2)
         take = barNo != lastBassBar_;
      lastBassKey_ = e.key;
      if (!take)
         return;
      // Played by the take too (BOTH): one chord is enough.
      if (settings_.rhythmFrom == 2 && std::abs(beat - lastOwnBeat_) < 0.2)
         return;
      lastBassBeat_ = beat;
      lastBassBar_ = barNo;
      bassKey_ = e.key;
      const int64_t rel = static_cast<int64_t>(std::llround(beat * kTicksPerBeat)) - anchor_;
      const Chord c = chordAt(std::max<int64_t>(0, rel));
      Hit h;
      h.vel = static_cast<uint8_t>(std::clamp(e.vel, 40, 120));
      h.art = settings_.follow == 0 ? kArtAsWritten : kArtRing;
      const Voicing v = voicingFor(c, -1);
      const double lenBeats = settings_.follow == 0 ? 4.0 : 2.0;
      const int64_t t = static_cast<int64_t>(std::floor(beat * kTicksPerBeat));
      playChord(t, beat - static_cast<double>(t) / kTicksPerBeat - settings_.timing * tempo_ / 60000.0, c, v, h,
                h.vel * energy_, lenBeats, lenBeats, static_cast<int>(rel / 12));
   }

   // ---- out ----

   struct Event {
      double beat;
      uint8_t status, d1, d2;
   };

   void note(double at, int key, int vel, double lenBeats) {
      key = std::clamp(key, 0, 127);
      const uint8_t ch = static_cast<uint8_t>(settings_.channel & 15);
      push({at, static_cast<uint8_t>(0x90 | ch), static_cast<uint8_t>(key), static_cast<uint8_t>(std::clamp(vel, 1, 127))});
      push({at + std::max(0.02, lenBeats), static_cast<uint8_t>(0x80 | ch), static_cast<uint8_t>(key), 0});
      if (settings_.rhythmFrom != 1)
         lastOwnBeat_ = at;
   }

   // Lets go of every note still sounding at `beat` (a comp lifts its hands for the next chord).
   void cutFrom(double beat) {
      for (size_t i = 0; i < queueLen_; ++i) {
         Event &off = queue_[i];
         if ((off.status & 0xF0) != 0x80 || off.beat <= beat)
            continue;
         // Only a note already started by then.
         bool startedBefore = false;
         for (size_t j = 0; j < queueLen_; ++j)
            if ((queue_[j].status & 0xF0) == 0x90 && queue_[j].d1 == off.d1 && queue_[j].beat < beat &&
                queue_[j].beat < off.beat)
               startedBefore = true;
         if (startedBefore || sounding_[off.d1])
            off.beat = std::max(beat, lastEmitted_);
      }
   }

   void push(const Event &e) {
      if (queueLen_ < kQueue)
         queue_[queueLen_++] = e;
   }

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
            const bool on = (e.status & 0xF0) == 0x90;
            // A key struck again while it still sounds: let it go first.
            if (on && sounding_[e.d1]) {
               out.push(time, static_cast<uint8_t>(0x80 | (e.status & 15)), e.d1, 0);
               --sounding_[e.d1];
            }
            if (!on && !sounding_[e.d1])
               continue; // already let go
            out.push(time, e.status, e.d1, e.d2);
            if (on)
               ++sounding_[e.d1];
            else
               --sounding_[e.d1];
         } else
            queue_[keep++] = e;
      }
      queueLen_ = keep;
      lastEmitted_ = b1;
   }

   void flushOffs(OutBuffer &out) {
      for (int k = 0; k < 128; ++k)
         while (sounding_[static_cast<size_t>(k)] > 0) {
            out.push(0, static_cast<uint8_t>(0x80 | (settings_.channel & 15)), static_cast<uint8_t>(k), 0);
            --sounding_[static_cast<size_t>(k)];
         }
      queueLen_ = 0;
   }

   static constexpr size_t kQueue = 1024;
   static constexpr size_t kBassQueue = 64;
   Song song_;
   Settings settings_;
   std::array<Event, kQueue> queue_{};
   size_t queueLen_ = 0;
   std::array<uint8_t, 128> sounding_{};
   double lastEmitted_ = 0.0;

   bool running_ = false, following_ = false, followingBefore_ = false, hostPlaying_ = false;
   bool hostWasRunning_ = false;
   double tempo_ = 120.0, freeBeat_ = 0.0, expected_ = -1.0;
   int64_t nextTick_ = 0, anchor_ = 0, lastTick_ = 0, absoluteBar_ = 0;
   uint32_t salt_ = 0x1234;
   int section_ = 0, take_ = 0, barTake_ = 0, pending_ = -1, pendingTake_ = -1;
   bool fill_ = false, fillPending_ = false;
   int diceBar_ = 0;
   int step_ = -1, steps_ = 16;
   double energy_ = 1.0;
   bool stopped_ = false, runsOff_ = false, holdChord_ = false;
   std::array<uint8_t, 128> held_{};
   int heldSections_ = 0;
   int learning_ = -1, learned_ = -1;
   uint32_t hits_ = 0;

   // Chords and voicing.
   Voicing last_;
   Chord lastChord_, nextChord_;
   std::array<uint8_t, 128> keys_{};
   std::vector<int> heldNotes_;
   Chord heldChord_, latched_, pendingHeld_;
   bool heldNow_ = false, heldPending_ = false;
   // A run under way: the tick its chord lands on, and that chord's voicing.
   int64_t runTick_ = -1;
   Voicing runVoicing_;
   int lastRunBar_ = -100;

   // The bass.
   std::array<BassEvent, kBassQueue> bassIn_{};
   size_t bassQueue_ = 0;
   uint32_t bassHits_ = 0;
   int lastBassKey_ = -1, lastBassBar_ = -1, bassKey_ = -1;
   double lastBassBeat_ = -100.0, lastOwnBeat_ = -100.0;
};

} // namespace jcomp

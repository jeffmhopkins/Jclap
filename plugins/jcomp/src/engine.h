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
   bool songEnds = true;   // SONG: after the outro's ending it stops (else round to the start again)
   bool introSetup = true; // the intro's last bar a held chord setting up the verse, not a fill
   int sameDiceBars = 4, autoFillBars = 8;
   int sync = 0;
   bool play = false;
   double freeTempo = 100.0;
   double swing = 0.5;
   int swingFeel = 0;
   bool swingEase = true; // the swing relaxes toward straight at fast tempos
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
   double length = 0.5;  // LENGTH: down to chops, as written at the middle, up to legato
   double density = 0.5; // DENSITY: thinned below the middle, filled in above
   int onsets = 0;
   double vary = 0.2;
   double loose = 0.3;      // the player's: how far each chord wanders off the grid
   // Runs.
   double runs = 0.25;
   int runEveryBars = 2;
   bool runLandTop = true, runHalfStep = true, runShorten = true;
   // Output.
   int channel = 0; // 0-based
   bool pad = false;   // a held chord on its own channel under the comp
   int padChannel = 2; // 0-based
   int rhythmFrom = 0, bassChannel = 1, follow = 1;
   // Section notes.
   int noteMap = 0, noteOctave = 0, noteChannel = 0;
   bool fillInto = true, velEnergy = false, heldOnly = false, passNotes = false;
   std::array<int, kActionCount> learn{};
   // From the style and the player.
   bool fixed = false;       // a figure played as written: DENSITY adds nothing to it
   bool slip = false;        // Cramer's slip note: a grace note a step under an inner chord tone
   bool shorts = false;      // a style chopped short (a skank, the organ bubble): its left hand too
   bool sustainBass = false; // an organ or a pad: the low note stays in every chord
   int noteFor(int action) const {
      return noteMap == 1 ? learn[static_cast<size_t>(action)] : 12 * (noteOctave + 1) + kActionKey[action];
   }
};

// The engine's dice: the same for a bar, a step and what they decide, from a salt that a start
// resets (a render comes out the same every time). The panel's rolls throw the same ones.
constexpr uint32_t kDiceSalt = 0x1234;
inline double diceFor(uint32_t salt, int bar, int step, int what) {
   const uint32_t h = mix(mix(salt, static_cast<uint32_t>(bar)), static_cast<uint32_t>(step * 64 + (what << 12)));
   return (h >> 8) * (1.0 / 16777216.0);
}
// The bar the dice are thrown for: SAME DICE's bars round, or (off) every bar its own, counted from 1.
inline int diceBarOf(int bar, int same) { return same > 0 ? bar % same : bar + 1; }

// What the recipe's style and player tell the engine.
inline void applyRecipe(Settings &s, const Recipe &r) {
   const unsigned mods = styleOf(r).mods;
   s.fixed = (mods & kFixed) != 0;
   s.slip = (mods & kSlip) != 0;
   s.shorts = (mods & kShorts) != 0;
   const int instrument = playerOf(r).instrument;
   s.sustainBass = instrument == kOrgan || instrument == kPad;
}

struct Block {
   uint32_t frames = 0;
   double sampleRate = 48000.0;
   double tempo = 120.0;
   bool hasTransport = false;
   bool playing = false;
   double beats = 0.0;
   // The host's loop, when it has one: the clock doesn't run past its end (the host jumps back).
   bool loopActive = false;
   double loopStart = 0.0, loopEnd = 0.0;
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

   // Back to the start. The notes still sounding are let go at the next block, played or not.
   void reset() {
      queueLen_ = 0;
      flushPending_ = true;
      running_ = false;
      section_ = std::clamp(settings_.section, 0, kSections - 1);
      take_ = std::clamp(settings_.take, 0, kTakes - 1);
      pending_ = pendingTake_ = -1;
      fill_ = fillPending_ = fillDown_ = ending_ = ended_ = false;
      padChord_ = Chord{};
      pad_ = Voicing{};
      energy_ = 1.0;
      stopped_ = false;
      for (auto &k : held_)
         k = 0;
      runsOff_ = holdChord_ = false;
      keys_.fill(0);
      heldChord_ = latched_ = pendingHeld_ = Chord{};
      heldNow_ = false;
      heldPending_ = heldFresh_ = false;
      heldNotes_.clear();
      last_ = Voicing{};
      lastChord_ = bassChord_ = Chord{};
      lastBass_ = -1;
      bassQueue_ = 0;
      runTick_ = -1;
      heldStrike_ = latchUntil_ = -1;
      strikes_ = 0;
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
      // The chord keys: for the chords they name, or (PLAY ONLY WHILE HELD) to say when to play.
      if ((settings_.chordsFrom > 0 || settings_.heldOnly) && key >= settings_.chordSplit) {
         keys_[static_cast<size_t>(key & 127)] = 1;
         keyAt_[static_cast<size_t>(key & 127)] = clock_ + frame;
         heldChanged(true, frame);
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
         heldChanged(false, frame);
         return true;
      }
      return actionFor(channel, key) >= 0;
   }
   // Every key let go (a host's note-off for all keys, a choke, All Notes Off): the chord keys and the
   // section notes held.
   void releaseAll() {
      for (size_t k = 0; k < held_.size(); ++k)
         if (held_[k]) {
            const int a = held_[k] - 1;
            held_[k] = 0;
            act(a, 0, false);
         }
      bool any = false;
      for (auto &k : keys_) {
         any = any || k;
         k = 0;
      }
      if (any)
         heldChanged(false, 0);
   }

   // From the panel: as a section note played at a medium velocity, pressed and let go.
   // A click on the panel: at full velocity (VELOCITY SETS ENERGY gives a click the full energy).
   void request(int action) { act(action, 127, true); }
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
      if (flushPending_) {
         flushOffs(out);
         flushPending_ = false;
      }
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
      sampleRate_ = std::max(1.0, b.sampleRate);
      clock_ += b.frames; // the key clock: this block's events are stamped from the block before's end

      if (!run) {
         if (running_)
            stopAll(out);
         running_ = false;
         bassQueue_ = 0;
         if (heldFresh_) {
            heldFresh_ = false;
            if (heldPending_)
               takeHeld();
         }
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
         const bool start = !running_ || following != followingBefore_;
         flushOffs(out);
         queueLen_ = 0;
         strikes_ = 0;
         missed_.clear();
         shown_ = {};
         const int64_t t = static_cast<int64_t>(std::floor(b0 * kTicksPerBeat + 1e-6));
         // From the step at or just before the block: a host that wraps its loop a little past the
         // loop's start (by up to a block), or a start a few ms after the bar line, still gets the
         // downbeat there (a little late, not dropped). A step is a sixteenth or a triplet eighth.
         const double graceTicks = kGrace * std::max(1.0, b.tempo) / 60.0 * kTicksPerBeat;
         nextTick_ = t;
         for (int grid : {12, 16}) {
            const int64_t back = t - (((t % grid) + grid) % grid);
            if (static_cast<double>(b0 * kTicksPerBeat - static_cast<double>(back)) <= graceTicks)
               nextTick_ = std::min(nextTick_, back);
         }
         // Every pass of a loop plays as the first did: the player starts afresh (voicing, runs, the
         // left hand, the bass coming in), and a cut is placed from here, not from where it was.
         last_ = Voicing{};
         lastChord_ = bassChord_ = Chord{};
         lastBass_ = -1;
         octaveStep_ = 0;
         runTick_ = -1;
         lastRunBar_ = -100;
         lastBassKey_ = lastBassBar_ = bassKey_ = -1;
         lastBassBeat_ = lastOwnBeat_ = -100.0;
         lastEmitted_ = b0;
         freshClock_ = true;
         padChord_ = Chord{};
         pad_ = Voicing{};
         if (start) {
            const int64_t bar = static_cast<int64_t>(std::floor(b0 / beats()));
            anchor_ = bar * barTicks();
            fill_ = fillPending_ = fillDown_ = ending_ = ended_ = false;
            absoluteBar_ = 0;
            salt_ = kDiceSalt; // the same dice every time it plays: a render comes out the same
         } else {
            // A jump: the section keeps its bars on the bar lines.
            const int64_t len = barTicks();
            anchor_ = t - (((t - anchor_) % len) + len) % len;
         }
         // Following the host, a looped section lies on the host's timeline (its bar 1 on a multiple
         // of its length from the host's bar 1), so a host bar plays the same chord every pass of a
         // loop, after a jump, and wherever playback started.
         if (following && !settings_.song) {
            const int64_t secLen =
                static_cast<int64_t>(std::max<int>(1, song_.sections[static_cast<size_t>(section_)].bars)) * barTicks();
            const int64_t bar = static_cast<int64_t>(std::floor(static_cast<double>(t) / barTicks())) * barTicks();
            anchor_ = bar - (((bar % secLen) + secLen) % secLen);
         }
         // SONG mode on the host's timeline: where the song is at this bar, counted from its start.
         if (settings_.song && following)
            placeInSong(t);
      }
      running_ = true;
      followingBefore_ = following;
      const double b1 = b0 + perFrame * b.frames;
      if (releasePending_) {
         releasePending_ = false;
         if (!heldNow_)
            silenceFrom(b0 + releaseFrame_ * perFrame);
      }
      // A chord played on the keys around a change it was meant for.
      if (heldFresh_) {
         heldFresh_ = false;
         takeHeldAt(b0 + heldFrame_ * perFrame);
      }
      // The bass coming in plays chords where it lands. With RHYTHM FROM = BOTH, on the first block
      // after a start or a jump, the take's own hits go first, so a chord they both play is struck
      // once (later blocks have the take's hits already placed, a lookahead ahead).
      const bool takeFirst = settings_.rhythmFrom == 2 && freshClock_;
      freshClock_ = false;
      const auto bassIn = [&] {
         for (size_t i = 0; i < bassQueue_; ++i)
            bass(bassIn_[i], b0 + bassIn_[i].frame * perFrame);
         bassQueue_ = 0;
      };
      if (!takeFirst)
         bassIn();
      // Far enough ahead for the earliest a note can be placed (TIMING ahead, a nudge, the wander).
      double limit = b1 + kLookahead * tempo_ / 60.0;
      if (following && b.loopActive && b.loopEnd > b.loopStart && b0 < b.loopEnd - 1e-9)
         limit = std::min(limit, b.loopEnd);
      while (static_cast<double>(nextTick_) / kTicksPerBeat < limit - 1e-9) {
         tick(nextTick_);
         ++nextTick_;
      }
      if (takeFirst)
         bassIn();
      emit(b0, perFrame, b.frames, out);
      expected_ = b1;
      nowTick_ = static_cast<int64_t>(std::floor(b0 * kTicksPerBeat + 1e-6));
      if (!following)
         freeBeat_ = b1;
   }

   void stopAll(OutBuffer &out) {
      flushOffs(out);
      queueLen_ = 0;
      strikes_ = 0;
   }

   void fillFrame(Frame &f) const {
      f.running = running_;
      f.following = following_;
      f.hostPlaying = hostPlaying_;
      f.tempo = tempo_;
      // Where the music is now (the clock runs a lookahead ahead of what's heard): the latest step
      // at or before the host's beat.
      const Shown *now = nullptr;
      for (const Shown &x : shown_)
         if (x.steps > 0 && x.t <= nowTick_ && (!now || x.t > now->t))
            now = &x;
      const int64_t anchor = now ? now->anchor : anchor_;
      const int64_t since = std::max<int64_t>(0, (running_ ? std::min(nowTick_, lastTick_) : lastTick_) - anchor);
      f.bar = static_cast<int>(since / barTicks());
      f.beat = static_cast<int>((since % barTicks()) / kTicksPerBeat);
      f.section = section_;
      f.take = barTake_;
      f.fill = fill_ || ending_;
      f.ending = ending_;
      f.fillDown = fill_ && fillDown_;
      f.ended = ended_;
      f.beats = beats();
      f.queued = pending_;
      f.queuedTake = pendingTake_;
      f.step = running_ && now ? now->step : -1;
      f.steps = now ? now->steps : 16;
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
   bool ended() const { return ended_; }
   // A bar: four beats, or three in a waltz.
   int beats() const { return song_.beats == 3 ? 3 : 4; }
   int64_t barTicks() const { return static_cast<int64_t>(kTicksPerBeat) * beats(); }
   // Where a bar's second chord comes in: half way, but on beat 3 in a waltz (6/8's half bar is
   // its second beat).
   int64_t halfTicks() const {
      const bool sixEight = song_.sections[static_cast<size_t>(std::clamp(section_, 0, kSections - 1))].takes[0].rate == kRate68;
      return beats() == 3 && !sixEight ? 2 * kTicksPerBeat : barTicks() / 2;
   }
   // The start of the next chord slot (a bar's first or second half) after `rel`.
   int64_t nextSlot(int64_t rel) const {
      const int64_t bar = rel - ((rel % barTicks()) + barTicks()) % barTicks();
      return rel - bar < halfTicks() ? bar + halfTicks() : bar + barTicks();
   }
   const Voicing &lastVoicing() const { return last_; }
   Chord heldChord() const { return heldNow_ ? heldChord_ : latched_; }

   // How far ahead of the host the clock runs, in seconds.
   static constexpr double kLookahead = 0.08;

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
      const int64_t bar = rel / barTicks();
      const bool onBar = rel % barTicks() == 0;
      // A two-bar clave figure (a montuno, a guajeo) changes section only where its clave starts
      // again, so the new progression starts on the figure's first bar, not its other side.
      if (settings_.switchAt == 1 && settings_.fixed) {
         const Section &sec = song_.sections[static_cast<size_t>(section_)];
         if (sec.takes[0].steps > sec.takes[0].stepsPerBar()) {
            const int64_t hostBar = static_cast<int64_t>(std::floor(static_cast<double>(t) / barTicks()));
            return onBar && ((hostBar % 2) + 2) % 2 == 0;
         }
      }
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

   // SONG mode: the section playing at the host's tick `t`, the song starting at bar 1 with the
   // Section param's section and going on through the sections in turn.
   void placeInSong(int64_t t) {
      const int64_t bar = static_cast<int64_t>(std::floor(static_cast<double>(t) / barTicks()));
      const int first = std::clamp(settings_.section, 0, kSections - 1);
      int64_t total = 0;
      // A song that ends runs from its first section to the outro; one on repeat goes round them all.
      for (int k = settings_.songEnds ? first : 0; k < kSections; ++k)
         total += std::max<int>(1, song_.sections[static_cast<size_t>(k)].bars);
      ended_ = ending_ = false;
      if (settings_.songEnds && bar >= total) {
         // Past the end: the song is over until the host comes back into it.
         ended_ = true;
         section_ = kOutro;
         pending_ = -1;
         anchor_ = bar * barTicks();
         return;
      }
      int64_t into = ((bar % total) + total) % total;
      int s = first;
      for (int k = 0; k < kSections; ++k) {
         const int bars = std::max<int>(1, song_.sections[static_cast<size_t>(s)].bars);
         if (into < bars)
            break;
         into -= bars;
         s = (s + 1) % kSections;
      }
      section_ = s;
      pending_ = -1;
      anchor_ = (bar - into) * barTicks();
   }

   // ---- held chords ----

   // The chord keys changed. A chord is named when a key goes down; letting keys go keeps it (with
   // LATCH, until a new one), so a chord let go a key at a time doesn't turn into its last key.
   void heldChanged(bool down, uint32_t frame) {
      FixedVec<int, 32> notes;
      for (int k = 0; k < 128; ++k)
         if (keys_[static_cast<size_t>(k)])
            notes.push_back(k);
      if (notes.empty()) {
         heldNow_ = false;
         // PLAY ONLY WHILE HELD stops when the chord is let go: what sounds and what's queued.
         if (settings_.heldOnly && running_) {
            releasePending_ = true;
            releaseFrame_ = frame;
         }
         if (!settings_.latch) {
            latched_ = Chord{};
            heldPending_ = false;
         } else if (!heldPending_) {
            latchBoth();
         }
         return; // with LATCH a chord still waiting for its moment is taken then
      }
      // Letting keys go doesn't rename the chord (a finger lifted from Cmaj7 doesn't make it Em).
      if (!down) {
         heldNow_ = heldNow_ || !heldPending_;
         return;
      }
      // A legato change: the new chord's keys go down while the old one's are still held. The chord
      // is the keys pressed together (within 60 ms of the newest), when two or more were.
      {
         uint64_t newest = 0;
         for (int k : notes)
            newest = std::max(newest, keyAt_[static_cast<size_t>(k)]);
         const uint64_t window = static_cast<uint64_t>(0.06 * sampleRate_);
         FixedVec<int, 32> group;
         for (int k : notes)
            if (keyAt_[static_cast<size_t>(k)] + window >= newest)
               group.push_back(k);
         if (group.size() >= 2 && group.size() < notes.size())
            notes = group;
      }
      pendingHeld_ = nameChord(notes, settings_.slashBass);
      heldNotes_ = notes;
      if (!running_) {
         takeHeld();
         return;
      }
      heldPending_ = true;
      heldFresh_ = true;
      heldFrame_ = frame;
   }
   // A NEW CHORD STARTS: at once (any tick), the next sixteenth, beat or bar.
   int64_t changeGrid() const {
      return settings_.changeAt == 0 ? 1 : settings_.changeAt == 1 ? 12 : settings_.changeAt == 2 ? kTicksPerBeat : barTicks();
   }
   void takeHeld() {
      heldChord_ = pendingHeld_;
      latched_ = heldChord_;
      bool any = false;
      for (uint8_t k : keys_)
         any = any || k;
      heldNow_ = any;
      heldPending_ = false;
      if (!any && settings_.latch)
         latchBoth(); // a quick tap, let go before its moment
   }
   // LATCH in BOTH: the chord let go keeps playing until the progression's next change.
   void latchBoth() {
      latchUntil_ = -1;
      if (settings_.chordsFrom != 2 || latched_.none())
         return;
      const int64_t cur = std::max<int64_t>(0, std::max(lastTick_, nextTick_) - anchor_);
      const int64_t next = nextChange(cur);
      latchUntil_ = anchor_ + (next < 0 ? cur + 4 * barTicks() : next);
   }
   bool latchedBoth(int64_t tick) const {
      return settings_.chordsFrom == 2 && settings_.latch && !heldNow_ && !latched_.none() && tick < latchUntil_;
   }
   bool heldActive() const { return heldNow_ || (settings_.latch && !latched_.none()); }

   // A chord played on the keys at `beat`: CHANGE AT's next point takes it, or the one just gone
   // when it came within a moment after (a chord played on the beat, a little late). The clock runs
   // ahead, so a point already played is played again with the new chord.
   void takeHeldAt(double beat) {
      const int64_t grid = changeGrid();
      const double grace = kGrace * tempo_ / 60.0 * kTicksPerBeat;
      const double from = beat * kTicksPerBeat - grace - static_cast<double>(anchor_);
      const int64_t g = anchor_ + static_cast<int64_t>(std::ceil(from / static_cast<double>(grid) - 1e-9)) * grid;
      if (g >= nextTick_)
         return; // its tick is still to come
      takeHeld();
      heldStrike_ = g;
      replayFrom(g, beat);
      // Hits the clock passed with no chord to play (the first chord on the keys, or PLAY ONLY WHILE
      // HELD before it was held): played now, with it.
      FixedVec<Missed, kMissed> again;
      for (const Missed &m : missed_)
         if (m.t >= g)
            again.push_back(m);
      missed_.clear();
      for (const Missed &m : again)
         if (m.p == patternAt(m.t))
            playStep(*m.p, m.prel, m.t, m.secRel);
   }

   // The next point (relative to the anchor) after `cur` where a queued section takes over.
   int64_t nextSwitch(int64_t cur) const {
      const int64_t step = settings_.switchAt == 0 ? kTicksPerBeat : barTicks();
      const int64_t from = (cur / step + 1) * step;
      for (int k = 0; k < 256; ++k)
         if (switchesAt(anchor_ + from + k * step))
            return from + k * step;
      return from;
   }

   // The chord at a tick (relative to the section's start). Past the point where this section gives
   // way (SONG mode's next section, or a queued one at its switch point) it's the coming section's
   // chord, so a push or a run into the new section plays the chord it's going to.
   Chord progressionChord(int64_t rel) const {
      int si = section_;
      if (!holdChord_ && pending_ >= 0) {
         const int64_t cur = std::max<int64_t>(0, lastTick_ - anchor_);
         const int64_t sw = nextSwitch(cur);
         if (rel >= sw) {
            si = pending_;
            const int64_t at = anchor_ + sw;
            rel = rel - sw + (((at % barTicks()) + barTicks()) % barTicks());
         }
      } else if (!holdChord_ && settings_.song) {
         for (int k = 0; k < kSections; ++k) {
            const int64_t end = static_cast<int64_t>(song_.sections[static_cast<size_t>(si)].bars) * barTicks();
            if (rel < end || end <= 0)
               break;
            rel -= end;
            si = (si + 1) % kSections;
         }
      }
      const Section &sec = song_.sections[static_cast<size_t>(si)];
      const int bar = static_cast<int>(rel / barTicks());
      const bool half = (rel % barTicks()) >= halfTicks();
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

   bool fromProgression() const {
      return !holdChord_ && settings_.chordsFrom != 1 && !(settings_.chordsFrom == 2 && heldNow_) &&
             !latchedBoth(lastTick_);
   }

   Chord chordAt(int64_t rel) const {
      if (holdChord_ && !lastChord_.none())
         return lastChord_;
      if (settings_.chordsFrom == 1)
         return heldActive() ? (heldNow_ ? heldChord_ : latched_) : Chord{};
      if (settings_.chordsFrom == 2 && heldNow_)
         return heldChord_;
      if (latchedBoth(anchor_ + rel))
         return latched_;
      return progressionChord(rel);
   }

   // The next tick (relative to the section) after `rel` where the progression's chord changes, or
   // the section gives way to the next; -1 if none within four bars or the chords aren't the
   // progression's.
   int64_t nextChange(int64_t rel) const {
      if (!fromProgression())
         return -1;
      const Chord now = progressionChord(rel);
      const int64_t end = static_cast<int64_t>(song_.sections[static_cast<size_t>(section_)].bars) * barTicks();
      for (int64_t at = nextSlot(rel), k = 0; k < 8; ++k, at = nextSlot(at)) {
         if (at >= end && ((settings_.song && pending_ < 0) || pending_ >= 0))
            return at;
         if (pending_ >= 0 && switchesAt(anchor_ + at))
            return at;
         if (!sameHarmony(progressionChord(at), now))
            return at;
      }
      return -1;
   }
   // Whether two chords are the same harmony (root, bass and third): a 9th turning into a 7th is a
   // change of colour the chord sounding can carry on through, not a change to play.
   static bool sameHarmony(const Chord &a, const Chord &b) {
      if (a.none() || b.none())
         return a.none() == b.none();
      if (a.root != b.root || a.bass != b.bass)
         return false;
      const auto &x = qualityOf(a).tones, &y = qualityOf(b).tones;
      for (int i : {3, 4, 5, 6, 8})
         if ((std::find(x.begin(), x.end(), i) != x.end()) != (std::find(y.begin(), y.end(), i) != y.end()))
            return false;
      return true;
   }
   // Whether the progression changes chord at `rel`.
   bool changesAt(int64_t rel) const {
      if (!fromProgression() || (rel % barTicks() != 0 && rel % barTicks() != halfTicks()))
         return false;
      if (rel == 0)
         return !sameHarmony(progressionChord(0), lastChord_);
      return !sameHarmony(progressionChord(rel), progressionChord(rel - 1));
   }

   // ---- the clock ----

   double dice(int bar, int step, int what) const { return diceFor(salt_, bar, step, what); }

   // Whether the fill going into section `to` is the fill down: into a quieter section, or (INTRO
   // SETS UP) the intro's held chord into the verse.
   bool downInto(int to) const {
      if (to < 0 || to == section_)
         return false;
      const Section &now = song_.sections[static_cast<size_t>(section_)];
      if (now.fillDown.empty())
         return false;
      if (section_ == kIntro && to == kVerse && settings_.introSetup)
         return true;
      return song_.sections[static_cast<size_t>(to)].energy + 5 < now.energy;
   }
   // The section SONG mode goes to after this one (-1 at the song's end).
   int songNext() const {
      if (settings_.songEnds && section_ == kOutro)
         return -1;
      return (section_ + 1) % kSections;
   }
   // The pattern playing now: the ending, a fill (up or down), or the bar's take.
   const Pattern &current() const {
      const Section &s = song_.sections[static_cast<size_t>(section_)];
      if (ending_ && !s.ending.empty())
         return s.ending;
      if (fill_)
         return fillDown_ && !s.fillDown.empty() ? s.fillDown : s.fill;
      const int bt = std::clamp(barTake_, 0, kTakes - 1);
      return s.takes[static_cast<size_t>(bt)].empty() ? s.takes[0] : s.takes[static_cast<size_t>(bt)];
   }

   void tick(int64_t t) {
      lastTick_ = t;
      if (ended_ && pending_ < 0)
         return; // the song is over: nothing until a section is asked for, or a start
      // A run whose target never played (the chord let go, a fill on another grid): it's over.
      if (runTick_ >= 0 && t > runTick_)
         runTick_ = -1;
      // A held chord waiting for its moment.
      if (heldPending_ && !heldFresh_) {
         const int64_t grid = changeGrid();
         if ((t - anchor_) % grid == 0) {
            takeHeld();
            heldStrike_ = t;
         }
      }
      const int64_t rel0 = t - anchor_;
      if (rel0 % kTicksPerBeat == 0 && rel0 > 0) {
         if (pending_ >= 0 && switchesAt(t)) {
            section_ = pending_;
            pending_ = -1;
            // The new section starts here, its bars on the host's bar lines (NEXT BEAT included). A
            // two-bar clave figure's progression starts on the figure's first side (an even host
            // bar), so a switch in its second bar comes in mid-figure rather than turning it round.
            {
               const Pattern &tk = song_.sections[static_cast<size_t>(section_)].takes[0];
               const int64_t unit = settings_.fixed && tk.steps > tk.stepsPerBar() ? 2 * barTicks() : barTicks();
               anchor_ = t - (((t % unit) + unit) % unit);
            }
            fill_ = fillDown_ = ending_ = ended_ = false;
            if (pendingTake_ >= 0) {
               take_ = pendingTake_;
               pendingTake_ = -1;
            }
         } else if (ended_) {
            return; // over, until the section asked for comes in
         } else if (fillPending_) {
            fill_ = true;
            fillDown_ = false;
            fillPending_ = false;
         } else if (settings_.fillInto && pending_ >= 0 && settings_.switchAt != 0 && rel0 % barTicks() != 0) {
            if (switchesAt(t - rel0 % barTicks() + barTicks())) {
               fill_ = true;
               fillDown_ = downInto(pending_);
            }
         }
      }
      if (ended_)
         return;
      const int64_t rel = t - anchor_;
      const Section &sec = song_.sections[static_cast<size_t>(section_)];
      if (rel % barTicks() == 0) {
         int bar = static_cast<int>(rel / barTicks());
         if (bar > 0 && fillPending_) {
            fill_ = true;
            fillDown_ = false;
            fillPending_ = false;
         } else if (bar > 0)
            fill_ = false;
         if (settings_.song && !holdChord_ && pending_ < 0 && bar >= sec.bars) {
            if (songNext() < 0) {
               // The end of the song: the ending has rung out.
               ended_ = true;
               ending_ = fill_ = false;
               padAt(t, true);
               return;
            }
            section_ = songNext();
            anchor_ = t;
            bar = 0;
            ending_ = false;
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
         if (every > 0 && (bar + 1) % every == 0) {
            fill_ = true;
            fillDown_ = false;
         }
         if (settings_.song && settings_.fillInto && pending_ < 0 && bar + 1 == now.bars) {
            fill_ = true;
            fillDown_ = downInto(songNext());
         }
         if (settings_.fillInto && pending_ >= 0 && switchesAt(t + barTicks()) && settings_.switchAt != 0) {
            fill_ = true;
            fillDown_ = downInto(pending_);
         }
         if (fillDown_ && now.fillDown.empty())
            fillDown_ = false;
         if (!fillDown_ && now.fill.empty())
            fill_ = false;
         // The song's last bars: the outro's ending, in place of its fill.
         if (settings_.song && settings_.songEnds && !holdChord_ && pending_ < 0 && songNext() < 0 && !now.ending.empty()) {
            const int endBars = std::min<int>(now.ending.bars(), now.bars);
            if (bar >= now.bars - endBars) {
               if (!ending_)
                  endFrom_ = static_cast<int64_t>(now.bars - endBars) * barTicks();
               ending_ = true;
               fill_ = false;
            }
         } else
            ending_ = false;
      }
      const Section &s = song_.sections[static_cast<size_t>(section_)];
      const int64_t r = t - anchor_;
      if (fill_ && (fillDown_ ? s.fillDown : s.fill).empty())
         fill_ = false; // a fill cleared in EDIT: the take plays on
      const Pattern &p = current();
      int64_t slot = nextSlot(r);
      nextChord_ = progressionChord(slot);
      for (int k = 0; k < 4 && nextChord_ == progressionChord(r); ++k)
         nextChord_ = progressionChord(slot = nextSlot(slot));
      if (settings_.pad)
         padAt(t, false);
      if (settings_.rhythmFrom == 1)
         return;
      playStep(p, patternPos(p, t, r), t, r);
   }

   // The pad layer: the chord up held on its own channel, a new one at each change (and nothing
   // while the comp is silent: before a chord is held, or at the song's end).
   void padAt(int64_t t, bool off) {
      if (t % 12 != 0 && !off)
         return;
      const double at = static_cast<double>(t) / kTicksPerBeat + settings_.timing * tempo_ / 60000.0;
      Chord c;
      if (!off && settings_.pad && !(settings_.heldOnly && !heldNow_))
         c = chordAt(t - anchor_);
      if (c == padChord_ && c.none() == pad_.empty())
         return;
      if (!pad_.empty() && sameHarmony(c, padChord_) && !c.none()) {
         padChord_ = c; // a change of colour only: the pad holds on
         return;
      }
      cutFrom(at - 0.003, 3);
      padChord_ = c;
      pad_ = Voicing{};
      if (c.none())
         return;
      VoiceSettings vs = settings_.voicing;
      vs.shape = kClose;
      vs.voices = 4;
      vs.lo = 48;
      vs.hi = 76;
      vs.floor = 43;
      vs.center = 60;
      pad_ = voice(c, padLast_.empty() ? nullptr : &padLast_, vs, -1, nullptr);
      padLast_ = pad_;
      const double e = song_.sections[static_cast<size_t>(section_)].energy / 100.0;
      const int vel = static_cast<int>(std::lround((52.0 + 26.0 * e) * energy_));
      for (int i = 0; i < pad_.count; ++i)
         note(at, pad_.notes[static_cast<size_t>(i)], vel, 64.0, 3, settings_.padChannel);
   }

   // Where in a pattern the tick `t` falls. A take of two bars (a clave, a two-bar riff) keeps its
   // phase to the host's bars, so a section starting on an odd bar, a jump or a loop doesn't turn
   // the clave round. A fill plays its last bar into the next, unless it's the take's own figure
   // (a fixed figure's fill), which keeps the clave's side too.
   int64_t patternPos(const Pattern &p, int64_t t, int64_t r) const {
      const int64_t len = std::max(1, patternTicks(p));
      if (ending_)
         return std::clamp<int64_t>(r - endFrom_, 0, len - 1);
      if (!fill_ || (settings_.fixed && !fillDown_))
         return ((t % len) + len) % len;
      return (len - barTicks() % len) % len + r % barTicks();
   }

   // The swing: as set, or (SWING EASES WHEN FAST) relaxing above 160 BPM toward a quarter of it at
   // 280, as players' long-short ratio falls with the tempo.
   double swingNow() const {
      const double sw = settings_.swing;
      if (!settings_.swingEase)
         return sw;
      return 0.5 + (sw - 0.5) * std::clamp((320.0 - tempo_) / 160.0, 0.25, 1.0);
   }
   double swingTicks(const Pattern &p, int step, int stepTicks) const {
      const double sw = swingNow();
      // A triplet grid swings already; 6/8's eighths are its own lilt, not swung (its second beat,
      // eighth 4, stays on time).
      if (p.rate == kRate8T || p.rate == kRate68)
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

   static int handOf(uint8_t part) { return leftHand(part) ? 1 : part == kPartLine ? 2 : 0; }
   // Whether a hit sounding the chord (the chords or the line, not the bass) comes in the steps
   // from..to (counted on from `step`).
   bool chordHitIn(const Pattern &p, int step, int from, int to) const {
      for (int k = from; k <= to; ++k) {
         const int n = (step + k) % std::max<int>(1, p.steps);
         const Hit &h = p.at(n);
         if (h.on() && !leftHand(h.part) && !thinned(p, n))
            return true;
      }
      return false;
   }

   // One step of a pattern: a run starting, or a hit.
   // VARY drops a soft hit now and then (never one on a beat, nor the bar's first chord): soft as
   // written, before the section's energy scaled it.
   bool varied(const Pattern &p, const Hit &h, int step) const {
      if (settings_.vary <= 0.0 || step % perBeat(p) == 0 || firstChord(p, step))
         return false;
      const double e = song_.sections[static_cast<size_t>(section_)].energy / 100.0;
      const double soft = kSoft * (e < 0.15 ? 0.5 + 2.2 * e : 0.82 + 0.3 * e) + 1.0;
      return h.vel <= soft && dice(diceBar_, step, 5) < settings_.vary * 0.25;
   }

   void playStep(const Pattern &p, int64_t prel, int64_t t, int64_t secRel) {
      const int stepTicks = p.stepTicks();
      if (prel % stepTicks)
         return;
      const int steps = std::max<int>(1, p.steps);
      const int step = static_cast<int>((prel / stepTicks) % steps);
      shown_[shownAt_++ % shown_.size()] = {t, step, steps, anchor_};
      const int bar = static_cast<int>(secRel / barTicks());
      barRel_ = secRel - secRel % barTicks();
      if (settings_.heldOnly && !heldNow_) {
         miss(p, prel, t, secRel);
         return;
      }
      // A band hit written on the chord: struck where the chord starts; a hold or a shot rests the take
      // until the next chord.
      if (!ending_) {
         int64_t from = 0;
         const int mark = markAt(secRel, from);
         if (mark != kMarkNone) {
            if (secRel == from) {
               strikeMark(mark, p, step, steps, t, secRel);
               return;
            }
            if (mark != kMarkAccent)
               return;
         }
      }
      // A run coming: look a few steps ahead for a hit with a run into a new chord.
      if (runTick_ < 0 && settings_.runs > 0.0 && !runsOff_)
         lookForRun(p, step, steps, stepTicks, t, secRel, bar);
      // The run has these steps (the right hand's: the left hand keeps its notes under it).
      if (runTick_ >= 0 && t >= runStart_ && t < runTick_ && !leftHand(p.at(step).part))
         return;
      const Hit &written = p.at(step);
      const bool target = runTick_ == t;
      const int perBeat = p.perBeat();
      const bool onBeat = step % perBeat == 0;
      const bool onEighth = p.rate == kRate8T ? step % 3 != 1 : step % 2 == 0;
      const bool fine = fineGrid(p);
      const bool offbeat = offbeatTake(p);
      const double stepBeats = static_cast<double>(stepTicks) / kTicksPerBeat;
      // A change of chord the take has no hit for (two chords in a bar, the second between hits):
      // the chord is struck where it comes, as the take's first hit of the bar is played.
      const int64_t change = nextChange(secRel);
      // A chord taken from the keys is struck the same way, at its moment (A NEW CHORD STARTS), unless
      // the take strikes within the beat: the old chord doesn't ring on to the take's next hit.
      const bool heldChange = heldStrike_ >= 0 && t >= heldStrike_;
      if (heldChange)
         heldStrike_ = -1;
      if (!target && (changesAt(secRel) || heldChange) && !(written.on() && !leftHand(written.part)) &&
          !lastChord_.none()) {
         // Unless the take strikes within an eighth (a hit that soon carries it), the change sounds
         // where it comes: a turnaround's passing chord on beat 3 isn't left to the take's beat 4.
         const int toNext = change < 0 ? steps : static_cast<int>((change - secRel) / stepTicks) - 1;
         // (Not in an offbeat take, which leaves its beats empty: its next chop brings the change.)
         const int toChange = offbeat ? toNext : std::min(toNext, std::max(1, perBeat / 2));
         if (!chordHitIn(p, step, 1, std::max(0, toChange)) && !sameHarmony(chordAt(secRel), lastChord_))
            strikeChange(p, step, steps, t, secRel, stepBeats, perBeat, fine);
      }
      Hit h = written;
      if (!h.on()) {
         if (target) {
            runTick_ = -1;
            return;
         }
         // DENSITY above the middle fills the gaps: eighths first, then sixteenths (only in a rhythm
         // that already plays sixteenths), as softer repeats of the chord sounding. A step a written
         // chord still holds isn't a gap; a figure played as written gets nothing, and an offbeat
         // take nothing on the beats.
         const double more = (settings_.density - 0.5) * 2.0;
         if (more <= 0.0 || settings_.fixed || hasLine(p) || lastChord_.none() || last_.empty() || heldOver(p, step))
            return;
         if (!onEighth && !fine)
            return;
         if (offbeat && onBeat)
            return;
         const double chance = onEighth ? more * 0.85 : std::max(0.0, more - 0.35) * 1.2;
         if (dice(diceBar_, step, 21) >= chance)
            return;
         h.vel = static_cast<uint8_t>(onBeat ? kSoft : kGhost + (onEighth ? 10 : 0));
         h.len = 1;
         h.art = more > 0.85 ? kArtAsWritten : kArtChop;
      } else if (!target && thinned(p, step)) {
         return;
      }
      if (!target) {
         if (!plays(h, step))
            return;
         if (varied(p, h, step))
            return;
      }
      // How long, in steps, until the next hit of the same hand (for RING and the run's room): the
      // left hand's notes and the chords above them sound on their own. A hit its chance leaves out
      // this time round isn't one; and with a two-handed voicing the left hand gives way to a chord
      // on 1 or 3, which brings its own bass.
      const int hand = handOf(h.part);
      int gap = 1;
      while (gap < steps) {
         const int n = (step + gap) % steps;
         const Hit &o = p.at(n);
         if (o.on() && !thinned(p, n)) {
            const bool certain = (o.chance >= 100 || (step + gap < steps && plays(o, n))) &&
                                 !(step + gap < steps && varied(p, o, n));
            if (certain && handOf(o.part) == hand)
               break;
            if (certain && hand == 1 && o.part == kPartChord && n % strongSteps(p) == 0 &&
                settings_.voicing.shape == kSpread)
               break;
         }
         ++gap;
      }
      // A left-hand note as written is held to the next one, two beats at most (not in a style
      // chopped short: the organ bubble's left hand is a detached pulse).
      if (hand == 1 && h.art == kArtAsWritten && h.len <= 1 && !settings_.shorts) {
         h.art = kArtRing;
         gap = std::min(gap, 2 * perBeat);
      }
      // A chord an eighth before a change, with nothing after it until the change, anticipates it
      // (the and of 2 into a ii-V's V, a dotted-quarter pop rhythm into the next bar): it plays the
      // new chord. Not in an offbeat take, where an upbeat chop is the old chord's.
      int64_t chordRel = secRel + (h.push ? kTicksPerBeat / 2 : 0);
      if (!target && !h.push && !onBeat && !offbeat && chordPart(h.part) && change > secRel &&
          change - secRel <= kTicksPerBeat / 2 && !chordHitIn(p, step, 1, static_cast<int>((change - secRel) / stepTicks)))
         chordRel = change;
      const Chord c = chordAt(chordRel);
      if (c.none() && !target) {
         miss(p, prel, t, secRel);
         return;
      }
      Voicing v;
      if (target && c == runChord_) {
         v = runVoicing_;
         runTick_ = -1;
      } else if (target) {
         runTick_ = -1; // the chord isn't the one the run was going to (a chord played on the keys)
         v = voicingFor(c, h.voicing, h.part == kPartChord);
      } else
         v = voicingFor(c, h.voicing, h.part == kPartChord);
      double vel = h.vel * energy_;
      if (settings_.vary > 0.0)
         vel += (dice(diceBar_, step, 6) * 2.0 - 1.0) * settings_.vary * 10.0;
      // A two-handed voicing on keys: the left hand's low note sounds with the chords on beats 1 and 3
      // and on a change of chord; the chops and backbeats between are the right hand alone. An organ
      // or a pad keeps it.
      const bool strong = step % strongSteps(p) == 0;
      Strike k;
      k.t = t;
      k.offset = swingTicks(p, step, stepTicks) / kTicksPerBeat + h.nudge * tempo_ / 60000.0;
      k.c = c;
      k.v = v;
      k.h = h;
      k.vel = vel;
      k.lenBeats = h.len * stepBeats;
      k.gapBeats = gap * stepBeats;
      k.step = step;
      k.triplet = p.rate == kRate8T;
      k.fine = fine;
      // A two-handed voicing's low note is struck on 1 and 3 and held (an organ's too: it holds it
      // rather than striking it again with every chord between).
      const bool leftHandTake = hasLeftHand(p);
      // A guitar's grip in a take with a bass of its own (boom-chick, the palm-wine thumb) strums the
      // chord without its bass string: the thumb has it.
      k.dropBass = !target && h.part == kPartChord &&
                   ((!strong && settings_.voicing.shape == kSpread && settings_.onsets < 2 &&
                     (c == lastChord_ || c == bassChord_)) ||
                    (settings_.voicing.shape == kGrip && leftHandTake));
      const int toStrong = strongSteps(p) - step % strongSteps(p);
      k.holdBeats = toStrong * stepBeats;
      const int64_t after = nextChange(chordRel);
      k.capBeat = after < 0 ? -1.0 : static_cast<double>(anchor_ + after) / kTicksPerBeat;
      playChord(k, !target);
   }

   // A hit the clock passed with nothing to play, kept a moment for a chord on the keys to take.
   struct Missed {
      const Pattern *p;
      int64_t prel, t, secRel;
   };
   static constexpr size_t kMissed = 8;
   void miss(const Pattern &p, int64_t prel, int64_t t, int64_t secRel) {
      const int64_t keep = static_cast<int64_t>(kGrace * 2.0 * tempo_ / 60.0 * kTicksPerBeat) + 1;
      FixedVec<Missed, kMissed> kept;
      for (const Missed &m : missed_)
         if (m.t >= t - keep && m.t != t && kept.size() + 1 < kMissed)
            kept.push_back(m);
      kept.push_back({&p, prel, t, secRel});
      missed_ = kept;
   }
   // The pattern playing at tick `t` now (to check a missed hit is still the one there).
   const Pattern *patternAt(int64_t) const {
      return &current();
   }

   // Whether a hit's chance lets it play this time round.
   bool plays(const Hit &h, int step) const {
      return h.chance >= 100 || dice(diceBar_, step, 4) * 100.0 < h.chance;
   }

   // The chord a change brings, struck where the take has no hit for it: as the bar's first chord.
   void strikeChange(const Pattern &p, int step, int steps, int64_t t, int64_t secRel, double stepBeats, int perBeat,
                     bool fine) {
      Hit h;
      h.vel = kNormal;
      h.len = static_cast<uint8_t>(perBeat / 2 + 1);
      const int from = step - step % p.stepsPerBar();
      for (int k = 0; k < steps; ++k) {
         const Hit &o = p.at((from + k) % steps);
         if (o.on() && o.part == kPartChord) {
            h.vel = o.vel;
            h.len = o.len;
            h.art = o.art;
            break;
         }
      }
      const Chord c = chordAt(secRel);
      Strike k;
      k.t = t;
      k.offset = 0.0;
      k.c = c;
      k.v = voicingFor(c, -1, true);
      k.h = h;
      k.vel = h.vel * energy_;
      k.lenBeats = h.len * stepBeats;
      int gap = 1;
      while (gap < steps && !p.at((step + gap) % steps).on())
         ++gap;
      k.gapBeats = gap * stepBeats;
      k.step = step;
      k.triplet = p.rate == kRate8T;
      k.fine = fine;
      k.holdBeats = strongSteps(p) * stepBeats;
      const int64_t after = nextChange(secRel);
      k.capBeat = after < 0 ? -1.0 : static_cast<double>(anchor_ + after) / kTicksPerBeat;
      playChord(k, true);
   }

   // The band hit (Mark) on the chord sounding at `rel`, and where that chord starts (`from`). Only
   // over the section's own progression.
   int markAt(int64_t rel, int64_t &from) const {
      if (!fromProgression() || pending_ >= 0)
         return kMarkNone;
      const Section &sec = song_.sections[static_cast<size_t>(section_)];
      const int64_t bar = rel / barTicks(), half = halfTicks();
      const int pb = std::max<int>(1, sec.progBars);
      const int b = static_cast<int>(bar % pb);
      const bool second = rel % barTicks() >= half && (sec.split >> b & 1);
      from = bar * barTicks() + (second ? half : 0);
      return sec.marks[static_cast<size_t>(b * 2 + (second ? 1 : 0))];
   }
   // A band hit struck: accented, held to the next chord, or a short shot.
   void strikeMark(int mark, const Pattern &p, int step, int steps, int64_t t, int64_t secRel) {
      const int stepTicks = p.stepTicks();
      const double stepBeats = static_cast<double>(stepTicks) / kTicksPerBeat;
      const Chord c = chordAt(secRel);
      if (c.none())
         return;
      int64_t end = nextChange(secRel);
      if (end < 0)
         end = secRel + barTicks();
      Hit h;
      h.vel = kAccent;
      h.art = mark == kMarkHold ? kArtRing : mark == kMarkShot ? kArtChop : kArtAsWritten;
      h.len = static_cast<uint8_t>(std::max(1, p.perBeat() / 2 + 1));
      Strike k;
      k.t = t;
      k.offset = 0.0;
      k.c = c;
      k.v = voicingFor(c, -1, true);
      k.h = h;
      k.vel = std::min(127.0, (kAccent + 8) * energy_);
      k.lenBeats = h.len * stepBeats;
      k.gapBeats = std::max<double>(1.0, static_cast<double>(end - secRel) / stepTicks) * stepBeats;
      k.step = step;
      k.triplet = p.rate == kRate8T;
      k.fine = fineGrid(p);
      k.holdBeats = k.gapBeats;
      k.capBeat = static_cast<double>(anchor_ + end) / kTicksPerBeat;
      (void)steps;
      playChord(k, true);
   }

   // Whether DENSITY below the middle drops this hit (thinnedBy, with this bar's dice and whether
   // the progression changes chord half way through it).
   bool thinned(const Pattern &p, int step) const {
      if (settings_.density >= 0.5 || !p.at(step).on())
         return false;
      return thinnedBy(p, step, settings_.density, dice(diceBar_, step, 20), changesAt(barRel_ + halfTicks()),
                       settings_.fixed);
   }

   // The voicing a hit plays. `chord` for a hit playing the chord itself: with the OCTAVES shape
   // the octave line moves through the chord (5, 3, 1, 3), as a montuno's does, not one block
   // repeated.
   Voicing voicingFor(const Chord &c, int pick, bool chord = false) {
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
      const Voicing *prev = last_.empty() ? nullptr : &last_;
      if (chord && pick < 0 && settings_.voicing.shape == kOctaves) {
         if (c != lastChord_)
            octaveStep_ = 0;
         const Voicing v = octaveLine(c, prev, settings_.voicing, octaveStep_++);
         if (!v.empty())
            return v;
      }
      return voice(c, prev, settings_.voicing, pick, lastChord_.none() ? nullptr : &lastChord_);
   }

   void lookForRun(const Pattern &p, int step, int steps, int stepTicks, int64_t t, int64_t secRel, int bar) {
      (void)bar;
      // AT MOST EVERY counts the host's bars, so a new section doesn't wait on the last one's run.
      const int hostBar = static_cast<int>(t / barTicks());
      if (hostBar - lastRunBar_ < settings_.runEveryBars && lastRunBar_ > -100)
         return;
      // Never in a figure played as written (Freddie Green's four, the batida, a montuno).
      if (settings_.fixed)
         return;
      // On the take's grid: sixteenths where it plays them, else eighths.
      const int stride = fineGrid(p) || p.rate == kRate8T ? 1 : 2;
      if (stride == 2 && step % 2)
         return;
      // Not from a change of chord: the run would take its place.
      if (changesAt(secRel))
         return;
      for (int len : {3, 2, 4}) {
         const int span = len * stride;
         const Hit &h = p.at((step + span) % steps);
         if (!h.on() || !h.run || h.part != kPartChord)
            continue;
         const int64_t at = secRel + static_cast<int64_t>(span) * stepTicks;
         const Chord c = chordAt(at + (h.push ? kTicksPerBeat / 2 : 0));
         const Chord from = chordAt(secRel);
         if (c.none() || c == from || last_.empty())
            return;
         // The chord it leaves has to have been played: a run doesn't take the place of a chord.
         if (!sameHarmony(from, lastChord_))
            return;
         // Nor over another: every step the run takes is still the chord it leaves.
         for (int k = 1; k < span; ++k)
            if (chordAt(secRel + static_cast<int64_t>(k) * stepTicks) != from)
               return;
         // RUNS sets how many of the chances are taken.
         if (dice(diceBar_ + bar * 7, step, 9) >= std::min(1.0, settings_.runs * 1.6))
            return;
         runVoicing_ = voicingFor(c, h.voicing);
         runChord_ = c;
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
         // The run walks from the note sounding to the landing note, starting a step away from it. With
         // no room between them it starts further off (a leap of at most a sixth, then steps into it):
         // a run is at least two notes, never the note already sounding.
         const int top = last_.top();
         Ints run = passingRun(top, land, len, settings_.key % 12, settings_.scale, settings_.runHalfStep, c, from);
         if (run.size() < 2) {
            // Too close to walk: start further off, below or above, whichever begins nearer the note
            // sounding.
            Ints best;
            for (int other : {land - 12, land + 12}) {
               const Ints r = passingRun(other, land, len, settings_.key % 12, settings_.scale, settings_.runHalfStep, c, from);
               if (r.size() >= 2 && r.front() != top &&
                   (best.empty() || std::abs(r.front() - top) < std::abs(best.front() - top)))
                  best = r;
            }
            if (best.empty() || std::abs(best.front() - top) > 9)
               continue;
            run = best;
         }
         const int late = len - static_cast<int>(run.size()); // a shorter run starts later, still into the hit
         // It doesn't take the place of a written chord: only rests and soft hits under it.
         bool clear = true;
         for (int k = late * stride; k < span; ++k) {
            const Hit &o = p.at((step + k) % steps);
            if (o.on() && !leftHand(o.part) && o.vel >= kNormal)
               clear = false;
         }
         if (!clear)
            continue;
         const double stepBeats = static_cast<double>(stepTicks) / kTicksPerBeat;
         const double lay = settings_.timing * tempo_ / 60000.0;
         const double start = static_cast<double>(t) / kTicksPerBeat + lay;
         if (settings_.runShorten)
            cutFrom(start - 0.004, 0);
         for (size_t k = 0; k < run.size(); ++k) {
            const int s = (static_cast<int>(k) + late) * stride;
            const double at2 = start + s * stepBeats + swingTicks(p, (step + s) % steps, stepTicks) / kTicksPerBeat;
            const int vel = static_cast<int>((62 + 10 * k) * energy_);
            note(at2, run[k], vel, stride * stepBeats * 0.85);
         }
         runTick_ = t + static_cast<int64_t>(span) * stepTicks;
         runStart_ = t + static_cast<int64_t>(late * stride) * stepTicks;
         lastRunBar_ = hostBar;
         return;
      }
   }

   // A chord to play: where, what, and how long it has.
   struct Strike {
      int64_t t = 0;
      double offset = 0.0; // beats off the tick (swing, a nudge)
      Chord c;
      Voicing v;
      Hit h;
      double vel = 0.0, lenBeats = 0.0, gapBeats = 0.0;
      double holdBeats = 0.0; // to the next 1 or 3, for a two-handed voicing's bass
      double capBeat = -1.0;  // the next change of chord: nothing rings over it
      int step = 0;
      bool triplet = false, fine = true, dropBass = false;
      // Kept to play again (a held chord taken late): the voicing state before, and its notes.
      Voicing before;
      Chord beforeChord;
      uint32_t firstId = 0, lastId = 0;
   };

   // Plays a chord: its notes started together, rolled or strummed; its length by articulation.
   // `keep`: remembered, to be played again with a held chord that came a moment late.
   void playChord(Strike k, bool keep = false) {
      const Chord &c = k.c;
      Voicing v = k.v;
      const Hit &h = k.h;
      if (c.none() || v.empty())
         return;
      k.before = last_;
      k.beforeChord = lastChord_;
      const int hand = handOf(h.part);
      // The part of the chord the hit plays.
      if (h.part == kPartTop) {
         v.notes[0] = static_cast<uint8_t>(v.top());
         v.count = 1;
      } else if (h.part == kPartUpper && v.count > 2) {
         v.notes[0] = v.notes[static_cast<size_t>(v.count - 2)];
         v.notes[1] = v.notes[static_cast<size_t>(v.count - 1)];
         v.count = 2;
      } else if (h.part == kPartLine) {
         // A line note: its degree of the chord, from the root in the octave round the window's middle.
         const int low = settings_.voicing.center - 6;
         const int root = low + ((c.root - low) % 12 + 12) % 12;
         v.notes[0] = static_cast<uint8_t>(std::clamp(root + lineInterval(c, h.degree, settings_.key, settings_.scale), 0, 127));
         v.count = 1;
      } else if (leftHand(h.part)) {
         // The alternating bass's fifth only once the chord's root has sounded: a chord arriving on
         // the fifth's step (a change on beat 3) gets its root first.
         const bool fifth = h.part == kPartFifth && sameHarmony(bassChord_, c);
         v.notes[0] = static_cast<uint8_t>(bassNote(c, settings_.voicing.floor, fifth, settings_.voicing.shape == kGrip));
         v.count = 1;
         lastBass_ = v.notes[0];
      }
      if (hand == 1)
         bassChord_ = c; // the left hand has sounded this chord's bass
      else if (h.part == kPartChord) {
         // A chord whose lowest note is its bass (a spread voicing's left hand) has sounded the root:
         // the alternating bass's fifth comes next, not the root again.
         const int bassPc = ((c.bass >= 0 ? c.bass : c.root) % 12 + 12) % 12;
         if (v.count > 0 && v.notes[0] % 12 == bassPc)
            bassChord_ = c;
         else if (c != bassChord_)
            bassChord_ = Chord{};
      }
      // A two-handed voicing's low note, struck with the chord on 1 and 3: held to the next of them
      // (or the change), by the left hand, so the right hand's chops don't lift it.
      bool holdLow = false;
      const Voicing before = last_;
      const bool newChord = !(lastChord_ == c);
      if (h.part == kPartChord) {
         last_ = v;
         lastChord_ = c;
         if (k.dropBass && v.count > 2) {
            // The bass string goes; a guitar's strum also leaves out the strings the thumb has just
            // played (the chick after the alternate bass's fifth).
            int drop = 1;
            if (settings_.voicing.shape == kGrip) {
               // Both of the thumb's strings: the root's and the alternate's.
               const int floor = settings_.voicing.floor;
               const int thumb = std::max({lastBass_, bassNote(c, floor), bassNote(c, floor, true)});
               while (drop < v.count - 2 && v.notes[static_cast<size_t>(drop)] <= thumb)
                  ++drop;
            }
            for (int i = drop; i < v.count; ++i)
               v.notes[static_cast<size_t>(i - drop)] = v.notes[static_cast<size_t>(i)];
            v.count -= drop;
         } else if (settings_.voicing.shape == kSpread && v.count > 2 && hand == 0)
            holdLow = true;
      }
      ++hits_;
      const double ms = tempo_ / 60000.0; // a millisecond, in beats
      const double wander = (dice(diceBar_, k.step, 11) * 2.0 - 1.0) * settings_.loose * 6.0 * ms;
      const double at = static_cast<double>(k.t) / kTicksPerBeat + k.offset + settings_.timing * ms + wander;
      const double lenBeats = k.lenBeats, gapBeats = k.gapBeats;
      // How long: the take's length, or the articulation's, then LENGTHS varies it.
      double len = lenBeats * 0.92;
      switch (h.art) {
      case kArtChop: len = std::min(lenBeats, 0.25) * 0.45; break;
      case kArtHalf: len = std::max(lenBeats, gapBeats) * 0.5; break;
      case kArtRing:
      case kArtTopRings: len = gapBeats * 0.97; break;
      default: break;
      }
      // LENGTH: shorter toward a chop, longer toward the next chord.
      if (settings_.length < 0.5)
         len *= 0.15 + 0.85 * settings_.length / 0.5;
      else if (settings_.length > 0.5 && gapBeats * 0.97 > len)
         len += (gapBeats * 0.97 - len) * (settings_.length - 0.5) / 0.5;
      // LENGTHS varies it: a chord meant to ring only longer (the next chord cuts it), never leaving a
      // hole in a held part.
      const double vary = (dice(diceBar_, k.step, 12) * 2.0 - 1.0) * settings_.lengths;
      len *= 1.0 + (h.art == kArtRing || h.art == kArtTopRings ? std::abs(vary) : vary);
      const double shortest = 0.05 * tempo_ / 60.0;
      len = std::max(len, shortest);
      // Nothing rings over the next change of chord: the change is played on time, by itself.
      const double cap = k.capBeat < 0.0 ? 1e9 : k.capBeat + settings_.timing * ms - 0.01;
      cutFrom(at - 0.003, hand);
      if (holdLow)
         cutFrom(at - 0.003, 1);
      const int n = v.count;
      // Strum both ways: down on the beat and its eighths, up on what falls between (the e and a of
      // a sixteenth rhythm, the and of an eighth one, a triplet's later two).
      const bool up = settings_.onsets == 3 && (k.triplet ? k.step % 3 != 0 : k.fine ? k.step % 2 == 1 : k.step % 4 == 2);
      k.firstId = nextId_ + 1;
      // Cramer's slip: a grace note a whole step under an inner note, slid into it just before. An
      // ornament, not a habit: now and then, on a new chord, into a note not already sounding.
      if (settings_.slip && h.part == kPartChord && n >= 3 && h.vel > kSoft && at - 35.0 * ms > lastEmitted_ &&
          newChord && dice(diceBar_, k.step, 16) < 0.35) {
         const int inner = v.notes[static_cast<size_t>(n - 2)];
         bool clear = true;
         for (int i = 0; i < n; ++i)
            clear = clear && v.notes[static_cast<size_t>(i)] != inner - 2;
         for (int i = 0; i < before.count; ++i)
            clear = clear && before.notes[static_cast<size_t>(i)] != inner;
         if (clear)
            note(at - 35.0 * ms, inner - 2, static_cast<int>(k.vel * 0.8), 32.0 * ms, 0);
      }
      for (int i = 0; i < n; ++i) {
         const int idx = up ? n - 1 - i : i;
         const int key = v.notes[static_cast<size_t>(idx)];
         double on = at;
         switch (settings_.onsets) {
         case 0: on += (dice(diceBar_, k.step * 8 + i, 13) - 0.5) * 2.0 * ms; break;
         case 1: on += i * 14.0 * ms; break;
         default: on += i * 9.0 * ms; break;
         }
         double vv = k.vel + (dice(diceBar_, k.step * 8 + i, 14) * 2.0 - 1.0) * settings_.velocityVar * 0.5 +
                     (dice(diceBar_, k.step, 15) * 2.0 - 1.0) * settings_.velocityVar * 0.5;
         if (idx == n - 1 && n > 1)
            vv += settings_.topVoice;
         double l = len;
         int noteHand = hand;
         if (h.art == kArtTopRings && idx != n - 1)
            l = std::min(len, std::max(lenBeats, gapBeats * 0.5));
         if (holdLow && idx == 0) {
            l = std::max(l, k.holdBeats - 0.02);
            noteHand = 1;
         }
         l = std::max(shortest, std::min(l, cap - on));
         note(on, key, static_cast<int>(std::lround(vv)), l, noteHand);
      }
      k.lastId = nextId_;
      if (keep && k.lastId >= k.firstId) {
         if (strikes_ == kStrikes) {
            for (size_t i = 1; i < kStrikes; ++i)
               strike_[i - 1] = strike_[i];
            --strikes_;
         }
         strike_[strikes_++] = k;
      }
   }

   // The chords struck from the tick `g` on, played again with the chord now up (a held chord that
   // came a moment late): their notes not yet out are taken back and played as the new chord; a
   // chord already sounding is let go, and the new one played at `beat`, where it came.
   void replayFrom(int64_t g, double beat) {
      size_t first = strikes_;
      for (size_t i = 0; i < strikes_; ++i)
         if (strike_[i].t >= g) {
            first = i;
            break;
         }
      if (first == strikes_)
         return;
      FixedVec<Strike, kStrikes> again;
      for (size_t i = first; i < strikes_; ++i)
         again.push_back(strike_[i]);
      strikes_ = first;
      last_ = again[0].before;
      lastChord_ = again[0].beforeChord;
      for (Strike k : again) {
         size_t waiting = 0;
         for (size_t i = 0; i < queueLen_; ++i)
            waiting += queue_[i].id >= k.firstId && queue_[i].id <= k.lastId;
         if (waiting == 0)
            continue; // over already
         if (waiting == 2 * static_cast<size_t>(k.lastId - k.firstId + 1)) {
            size_t keep = 0;
            for (size_t i = 0; i < queueLen_; ++i)
               if (queue_[i].id < k.firstId || queue_[i].id > k.lastId)
                  queue_[keep++] = queue_[i];
            queueLen_ = keep;
         } else
            k.offset = beat - static_cast<double>(k.t) / kTicksPerBeat - settings_.timing * tempo_ / 60000.0;
         k.c = chordAt(k.t - anchor_);
         k.v = voicingFor(k.c, k.h.voicing, k.h.part == kPartChord);
         playChord(k, true);
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
      const double inBar = std::fmod(beat - static_cast<double>(anchor_) / kTicksPerBeat, beats());
      const double fromBeat = std::abs(inBar - std::round(inBar));
      const int barNo = static_cast<int>(std::floor((beat - static_cast<double>(anchor_) / kTicksPerBeat) / beats()));
      bool take = true;
      if (settings_.follow == 1) {
         const bool strong = e.vel >= 96 || fromBeat < 0.06 || (lastBassKey_ >= 0 && e.key >= lastBassKey_ + 10);
         take = strong && (beat - lastBassBeat_ > 0.45 || fromBeat < 0.06);
      } else if (settings_.follow == 2)
         take = barNo != lastBassBar_;
      lastBassKey_ = e.key;
      if (!take)
         return;
      // PLAY ONLY WHILE HELD: the bass brings chords in only while one is held, as the take does.
      if (settings_.heldOnly && !heldNow_)
         return;
      // Played by the take too (BOTH): one chord is enough.
      if (settings_.rhythmFrom == 2 && std::abs(beat - lastOwnBeat_) < 0.2)
         return;
      lastBassBeat_ = beat;
      lastBassBar_ = barNo;
      bassKey_ = e.key;
      const int64_t rel = static_cast<int64_t>(std::llround(beat * kTicksPerBeat)) - anchor_;
      const Chord c = chordAt(std::max<int64_t>(0, rel));
      Strike k;
      k.h.vel = static_cast<uint8_t>(std::clamp(e.vel, 40, 120));
      k.h.art = settings_.follow == 0 ? kArtAsWritten : kArtRing;
      k.c = c;
      k.v = voicingFor(c, -1);
      const double lenBeats = settings_.follow == 0 ? 4.0 : 2.0;
      k.t = static_cast<int64_t>(std::floor(beat * kTicksPerBeat));
      k.offset = beat - static_cast<double>(k.t) / kTicksPerBeat - settings_.timing * tempo_ / 60000.0;
      k.vel = k.h.vel * energy_;
      k.lenBeats = k.gapBeats = lenBeats;
      k.step = static_cast<int>(rel / 12);
      playChord(k);
   }

   // ---- out ----

   // A note's on and its off share an id: an off lets go only the note it belongs to, so a key struck
   // again (by the other hand, or the next chord) isn't cut short by the earlier note's end.
   struct Event {
      double beat;
      uint8_t status, d1, d2;
      uint8_t hand = 0; // 0 the chords (right hand), 1 the left hand's bass notes, 2 a line, 3 the pad
      uint32_t id = 0;
   };

   void note(double at, int key, int vel, double lenBeats, int hand = 0, int channel = -1) {
      if (queueLen_ + 2 > kQueue)
         return; // both or neither: an on without its off would hang
      key = std::clamp(key, 0, 127);
      const uint8_t ch = static_cast<uint8_t>((channel >= 0 ? channel : settings_.channel) & 15);
      const uint8_t hd = static_cast<uint8_t>(hand);
      const uint32_t id = ++nextId_;
      push({at, static_cast<uint8_t>(0x90 | ch), static_cast<uint8_t>(key), static_cast<uint8_t>(std::clamp(vel, 1, 127)), hd, id});
      push({at + std::max(0.02, lenBeats), static_cast<uint8_t>(0x80 | ch), static_cast<uint8_t>(key), 0, hd, id});
      if (settings_.rhythmFrom != 1 && hand != 3)
         lastOwnBeat_ = at;
   }

   // Lets go of every note still sounding at `beat` (a comp lifts its hands for the next chord): with
   // `hand`, only that hand's (a bass note doesn't lift the chord above it, nor a chord the bass); -1
   // every hand's but the pad's, -2 everything.
   // A note that starts after `beat` keeps its length: its off never comes before its on.
   void cutFrom(double beat, int hand = -1) {
      for (size_t i = 0; i < queueLen_; ++i) {
         Event &off = queue_[i];
         if ((off.status & 0xF0) != 0x80 || off.beat <= beat || (hand >= 0 && off.hand != hand) ||
             (hand == -1 && off.hand == 3))
            continue;
         bool later = false;
         for (size_t j = 0; j < queueLen_; ++j)
            if (queue_[j].id == off.id && (queue_[j].status & 0xF0) == 0x90) {
               later = queue_[j].beat >= beat;
               break;
            }
         if (!later)
            off.beat = std::max(beat, lastEmitted_);
      }
   }

   // Nothing sounds from `beat` on: notes queued to start after it are dropped (with their offs), and
   // the ones sounding end there.
   void silenceFrom(double beat) {
      size_t keep = 0;
      for (size_t i = 0; i < queueLen_; ++i) {
         const Event &e = queue_[i];
         bool late = false;
         for (size_t j = 0; j < queueLen_; ++j)
            if (queue_[j].id == e.id && (queue_[j].status & 0xF0) == 0x90) {
               late = queue_[j].beat > beat;
               break;
            }
         if (!late)
            queue_[keep++] = e;
      }
      queueLen_ = keep;
      cutFrom(beat, -2);
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
         if ((a.status & 0xF0) != (b.status & 0xF0))
            return (a.status & 0xF0) < (b.status & 0xF0);
         return a.id < b.id;
      });
      size_t keep = 0;
      for (size_t i = 0; i < queueLen_; ++i) {
         const Event &e = queue_[i];
         if (e.beat < b1 - 1e-7) {
            const double f = (e.beat - b0) / perFrame;
            const uint32_t time = static_cast<uint32_t>(std::clamp<double>(std::round(f), 0.0, frames ? frames - 1 : 0));
            const bool on = (e.status & 0xF0) == 0x90;
            uint32_t &sounding = sounding_[e.status & 15][e.d1];
            double &onBeat = onBeat_[e.status & 15][e.d1];
            if (on) {
               if (sounding && e.beat - onBeat < 0.02) {
                  // Struck twice at once (the thumb and the chord on the same string): one note, held
                  // to the later of the two ends.
                  double ends[2] = {-1.0, -1.0};
                  for (size_t j = i + 1; j < queueLen_; ++j)
                     if ((queue_[j].status & 0xF0) == 0x80)
                        for (int w = 0; w < 2; ++w)
                           if (queue_[j].id == (w ? e.id : sounding))
                              ends[w] = queue_[j].beat;
                  if (ends[1] > ends[0])
                     sounding = e.id;
                  continue;
               }
               // A key struck again while it still sounds: let it go first.
               if (sounding)
                  out.push(time, static_cast<uint8_t>(0x80 | (e.status & 15)), e.d1, 0);
               sounding = e.id;
               onBeat = e.beat;
            } else {
               if (sounding != e.id)
                  continue; // struck again since: that note's own off lets it go
               sounding = 0;
            }
            out.push(time, e.status, e.d1, e.d2);
         } else
            queue_[keep++] = e;
      }
      queueLen_ = keep;
      lastEmitted_ = b1;
   }

   // Lets go of everything sounding, on the channel it was played on.
   void flushOffs(OutBuffer &out) {
      for (size_t ch = 0; ch < 16; ++ch)
         for (size_t k = 0; k < 128; ++k)
            if (sounding_[ch][k]) {
               out.push(0, static_cast<uint8_t>(0x80 | ch), static_cast<uint8_t>(k), 0);
               sounding_[ch][k] = 0;
            }
      queueLen_ = 0;
   }

   static constexpr size_t kQueue = 1024;
   static constexpr size_t kBassQueue = 64;
   static constexpr size_t kStrikes = 4;
   static constexpr double kGrace = 0.08; // seconds: a held chord this late still takes the change
   Song song_;
   Settings settings_;
   std::array<Event, kQueue> queue_{};
   size_t queueLen_ = 0;
   std::array<std::array<uint32_t, 128>, 16> sounding_{}; // the id of the note sounding, by channel and key
   std::array<std::array<double, 128>, 16> onBeat_{};     // and when it was struck
   uint32_t nextId_ = 0;
   double lastEmitted_ = 0.0;
   bool flushPending_ = false;

   bool running_ = false, following_ = false, followingBefore_ = false, hostPlaying_ = false;
   bool hostWasRunning_ = false;
   double tempo_ = 120.0, freeBeat_ = 0.0, expected_ = -1.0;
   int64_t nextTick_ = 0, anchor_ = 0, lastTick_ = 0, absoluteBar_ = 0;
   uint32_t salt_ = kDiceSalt;
   int section_ = 0, take_ = 0, barTake_ = 0, pending_ = -1, pendingTake_ = -1;
   bool fill_ = false, fillPending_ = false;
   bool fillDown_ = false; // the fill playing is the fill down (or the intro's setup)
   bool ending_ = false, ended_ = false; // the outro's ending playing; the song over
   int64_t endFrom_ = 0;                 // where the ending starts, from the section's start
   int diceBar_ = 0;
   // The steps played lately, for the panel to show the one being heard.
   struct Shown {
      int64_t t = 0;
      int step = -1, steps = 0;
      int64_t anchor = 0;
   };
   std::array<Shown, 8> shown_{};
   size_t shownAt_ = 0;
   int64_t nowTick_ = 0;
   double energy_ = 1.0;
   bool stopped_ = false, runsOff_ = false, holdChord_ = false;
   std::array<uint8_t, 128> held_{};
   int learning_ = -1, learned_ = -1;
   uint32_t hits_ = 0;

   // Chords and voicing.
   Voicing last_;
   Chord lastChord_, nextChord_, bassChord_;
   int lastBass_ = -1; // the left hand's last note
   Chord padChord_;    // the pad layer's chord, and its notes
   Voicing pad_, padLast_;
   int octaveStep_ = 0;
   std::array<uint8_t, 128> keys_{};
   std::array<uint64_t, 128> keyAt_{}; // when each chord key went down, in samples
   uint64_t clock_ = 0;
   double sampleRate_ = 48000.0;
   FixedVec<int, 32> heldNotes_;
   Chord heldChord_, latched_, pendingHeld_;
   bool heldNow_ = false, heldPending_ = false, heldFresh_ = false;
   uint32_t heldFrame_ = 0, releaseFrame_ = 0;
   bool releasePending_ = false;
   // The last few chords struck, to play again with a held chord that came a moment late.
   std::array<Strike, kStrikes> strike_{};
   size_t strikes_ = 0;
   // A run under way: the tick its chord lands on, and that chord's voicing.
   int64_t runTick_ = -1, runStart_ = -1;
   int64_t heldStrike_ = -1; // where a chord taken from the keys is to be struck
   int64_t latchUntil_ = -1; // LATCH in BOTH: the latched chord plays up to here
   Voicing runVoicing_;
   Chord runChord_;
   FixedVec<Missed, kMissed> missed_;
   int64_t barRel_ = 0; // the bar playing, relative to the section (for DENSITY's thinning)
   int lastRunBar_ = -100;

   // The bass.
   std::array<BassEvent, kBassQueue> bassIn_{};
   size_t bassQueue_ = 0;
   uint32_t bassHits_ = 0;
   int lastBassKey_ = -1, lastBassBar_ = -1, bassKey_ = -1;
   double lastBassBeat_ = -100.0, lastOwnBeat_ = -100.0;
   bool freshClock_ = false; // the first block after a start or a jump
};

} // namespace jcomp

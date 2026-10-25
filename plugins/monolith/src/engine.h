#pragma once

// Monolith's engine: the scribe that writes glyphs, the slab that holds them and the voice that reads
// them out as notes in time with the host. Plain C++ with no plugin or GUI code: the plugin feeds it
// one block at a time and sends the notes it returns, and the tests drive it directly.
//
//    Scribe  builds a lexicon of glyphs from the seed and writes the slab from it, word by word,
//            following a grammar: each word has two favourite successors, and GRAMMAR is how often
//            it picks another word instead (and, half as often, changes a stroke of it).
//    Slab    55 cells, read in order over the first LENGTH of them. When the voice leaves a cell, the
//            scribe may rewrite it (DRIFT) or it may lose a stroke (ERODE); LOCK stops both.
//    Voice   steps through the slab at RATE in time with the host transport, one glyph per 16 steps,
//            and plays each glyph's notes in the Dialect. With no transport it runs at 120 BPM.
//    Listen  in Listen mode the voice plays nothing; notes played in are transcribed into a glyph per
//            window and carved into the cell the voice is on.

#include <algorithm>
#include <array>
#include <climits>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

#include "glyph.h"
#include "params.h"
#include "shared.h"

namespace monolith {

// Plain values the engine reads each block, taken from the params.
struct EngineParams {
   Orthography orthography = Orthography::Monolith;
   int seed = 1, lexicon = 16, density = 3;
   double grammar = 0.3, drift = 0.25, erode = 0.0;
   bool lock = false;
   int length = 8;
   int rate = 2;
   Direction direction = Direction::Forward;
   Mode mode = Mode::Speak;
   double swing = 0.0;
   int root = 5, scale = 3, rows = 0, octave = 0;
   double velocity = 0.8, gate = 0.9, humanize = 0.0;
   bool follow = false;
};

// The params that decide what the scribe writes. When any of them changes, the whole slab is
// written again from the seed, so the same settings always give the same script.
struct ScriptKey {
   int seed = 1, orthography = 0, density = 3, lexicon = 16;
   // GRAMMAR in twentieths, so a knob drag rewrites the slab at 21 points rather than continuously.
   int grammar = 6;

   bool operator==(const ScriptKey &o) const {
      return seed == o.seed && orthography == o.orthography && density == o.density && lexicon == o.lexicon &&
             grammar == o.grammar;
   }
   bool operator!=(const ScriptKey &o) const { return !(*this == o); }

   static ScriptKey of(const EngineParams &p) {
      return {p.seed, static_cast<int>(p.orthography), p.density, p.lexicon,
              static_cast<int>(std::lround(std::clamp(p.grammar, 0.0, 1.0) * 20.0))};
   }
   uint64_t pack() const {
      return static_cast<uint64_t>(seed & 0xffff) | static_cast<uint64_t>(orthography & 0xff) << 16 |
             static_cast<uint64_t>(density & 0xff) << 24 | static_cast<uint64_t>(lexicon & 0xff) << 32 |
             static_cast<uint64_t>(grammar & 0xff) << 40;
   }
   static ScriptKey unpack(uint64_t v) {
      return {static_cast<int>(v & 0xffff), static_cast<int>(v >> 16 & 0xff), static_cast<int>(v >> 24 & 0xff),
              static_cast<int>(v >> 32 & 0xff), static_cast<int>(v >> 40 & 0xff)};
   }
};

// ---- the scribe ----

class Scribe {
public:
   static constexpr int kMaxLexicon = 64;

   // A new lexicon for the script `key`.
   void build(const ScriptKey &key) {
      orthography_ = static_cast<Orthography>(std::clamp(key.orthography, 0, kOrthographyCount - 1));
      density_ = std::clamp(key.density, 1, kMaxStrokes);
      size_ = std::clamp(key.lexicon, 1, kMaxLexicon);
      Rng rng(hashMix(static_cast<uint64_t>(key.seed), static_cast<uint64_t>(key.orthography * 131 + key.density * 7 + key.lexicon)));
      for (int i = 0; i < size_; ++i) {
         words_[i] = make(rng);
         successors_[i] = {static_cast<uint8_t>(rng.below(size_)), static_cast<uint8_t>(rng.below(size_))};
      }
   }

   int size() const { return size_; }
   const Glyph &word(int i) const { return words_[std::clamp(i, 0, size_ - 1)]; }
   Orthography orthography() const { return orthography_; }

   // The word that follows `previous` (-1 for none): one of its two favourites, or with probability
   // `grammar` any word, changed by a stroke half that often.
   Cell next(int previous, double grammar, Rng &rng) const {
      int w;
      if (previous < 0 || previous >= size_ || rng.uniform() < grammar)
         w = rng.below(size_);
      else
         w = successors_[previous][rng.below(2)];
      Cell c;
      c.glyph = words_[w];
      c.word = static_cast<int16_t>(w);
      if (rng.uniform() < grammar * 0.5)
         mutate(c.glyph, rng);
      return c;
   }

   // A new glyph of about `density` strokes. Strokes mostly carry on from where the last one ended,
   // sometimes start again from the last one's start (a chord), sometimes anywhere.
   Glyph make(Rng &rng) const {
      Glyph g;
      const double r = rng.uniform();
      const int count = std::clamp(density_ + (r < 0.2 ? -1 : r > 0.8 ? 1 : 0), 1, kMaxStrokes);
      int from = rng.below(kNodes), previousFrom = -1;
      for (int i = 0; i < count; ++i) {
         if (i > 0) {
            const double pick = rng.uniform();
            from = pick < 0.65 ? g.strokes[i - 1].to : pick < 0.8 ? previousFrom : rng.below(kNodes);
         }
         const int to = randomTarget(from, rng);
         if (to < 0)
            break;
         g.strokes[g.count++] = {static_cast<uint8_t>(from), static_cast<uint8_t>(to)};
         previousFrom = from;
      }
      if (rng.uniform() < 0.3)
         g.dot = static_cast<int8_t>(rng.below(kNodes));
      g.bar = rng.uniform() < 0.15;
      return g;
   }

   // Adds a stroke, takes the last one away, or moves one stroke's end.
   void mutate(Glyph &g, Rng &rng) const {
      const int op = rng.below(3);
      if (op == 0 && g.count < kMaxStrokes && g.count > 0) {
         const int from = g.strokes[g.count - 1].to, to = randomTarget(from, rng);
         if (to >= 0)
            g.strokes[g.count++] = {static_cast<uint8_t>(from), static_cast<uint8_t>(to)};
      }
      else if (op == 1 && g.count > 1)
         --g.count;
      else if (g.count > 0) {
         Stroke &s = g.strokes[rng.below(g.count)];
         const int to = randomTarget(s.from, rng);
         if (to >= 0)
            s.to = static_cast<uint8_t>(to);
      }
   }

private:
   int randomTarget(int from, Rng &rng) const {
      int options[kNodes], n = 0;
      for (int to = 0; to < kNodes; ++to)
         if (strokeAllowed(orthography_, from, to))
            options[n++] = to;
      return n ? options[rng.below(n)] : -1;
   }

   Orthography orthography_ = Orthography::Monolith;
   int density_ = 3, size_ = 16;
   std::array<Glyph, kMaxLexicon> words_{};
   std::array<std::array<uint8_t, 2>, kMaxLexicon> successors_{};
};

// ---- the engine ----

struct NoteIn {
   uint32_t time;
   uint8_t pitch, velocity;
   bool on;
};

struct NoteOut {
   uint32_t time;
   uint8_t pitch, velocity;
   bool on;
};

struct Block {
   uint32_t frames = 256;
   double sampleRate = 48000.0;
   // With no transport the engine keeps its own clock at 120 BPM, always playing.
   bool hasTransport = false;
   bool playing = false;
   double tempo = 120.0;
   // Song position at the block's first sample.
   double beats = 0.0;
};

class Engine {
public:
   static constexpr int kMaxActive = 32;
   static constexpr int kMaxListen = 32;
   static constexpr double kFreeTempo = 120.0;

   Engine() { regenerate(ScriptKey::of(params_)); }

   void setParams(const EngineParams &p) {
      params_ = p;
      params_.length = std::clamp(p.length, 1, kCells);
      params_.rate = std::clamp(p.rate, 0, 5);
      if (ScriptKey::of(params_) != slabKey_)
         regenerate(ScriptKey::of(params_));
   }
   const EngineParams &params() const { return params_; }

   // Writes the whole slab again for `key`, from its seed.
   void regenerate(const ScriptKey &key) {
      scribe_.build(key);
      Rng rng(hashMix(static_cast<uint64_t>(key.seed), 0x5c41b));
      const double grammar = key.grammar / 20.0;
      int previous = -1;
      for (Cell &c : cells_) {
         c = scribe_.next(previous, grammar, rng);
         previous = c.word;
      }
      drift_ = Rng(hashMix(static_cast<uint64_t>(key.seed), 0xd71f7));
      slabKey_ = key;
      ++generation_;
   }

   const std::array<Cell, kCells> &cells() const { return cells_; }
   const Scribe &scribe() const { return scribe_; }
   const ScriptKey &slabKey() const { return slabKey_; }

   void apply(const Command &c) {
      const int cell = std::clamp(static_cast<int>(c.cell), 0, kCells - 1);
      switch (c.kind) {
      case Command::Kind::Jump: pendingJump_ = cell; break;
      case Command::Kind::Reroll: cells_[cell] = scribe_.next(wordBefore(cell), params_.grammar, drift_); break;
      case Command::Kind::Set: cells_[cell] = {c.glyph, 0, -1}; break;
      case Command::Kind::Restore:
         for (Cell &x : cells_)
            x.eroded = 0;
         break;
      case Command::Kind::Load: cells_[cell] = {c.glyph, c.eroded, c.word}; break;
      case Command::Kind::LoadKey:
         // The slab loaded with a state stands for its script: rebuild that script's lexicon so drift
         // and re-rolls carry on in it, without writing over what was loaded.
         slabKey_ = ScriptKey::unpack(c.key);
         scribe_.build(slabKey_);
         ++generation_;
         break;
      }
   }

   // Silences everything that's sounding, at `time`.
   template <class Out>
   void allNotesOff(Out &out, uint32_t time = 0) {
      for (int i = 0; i < activeCount_; ++i)
         out.push_back({time, active_[i].pitch, 0, false});
      activeCount_ = 0;
   }

   // One block: input notes in, notes to send out (sorted by time, note-offs first at equal times).
   template <class Out>
   void process(const Block &b, const NoteIn *in, int inCount, Out &out) {
      const size_t first = out.size();
      const bool playing = b.hasTransport ? b.playing : true;
      const double tempo = b.hasTransport && b.tempo > 0.0 ? b.tempo : kFreeTempo;
      const double bps = tempo / 60.0 / b.sampleRate;
      const double b0 = b.hasTransport ? b.beats : freeBeats_;
      const double b1 = b0 + b.frames * bps;
      if (!b.hasTransport)
         freeBeats_ = b1;

      handleFollow(in, inCount);

      if (!playing) {
         if (running_)
            stop(out);
         publishPitches();
         return;
      }
      // A jump in the song position (a loop, a seek) silences what's sounding and starts reading
      // afresh from there.
      if (!running_ || std::fabs(b0 - expected_) > 0.01) {
         allNotesOff(out, 0);
         if (!running_)
            offset_ = 0;
         glyph_ = INT_MIN;
         listenGlyph_ = INT_MIN;
         listenCount_ = 0;
      }
      running_ = true;
      expected_ = b1;

      const double stepBeats = kRateBeats[params_.rate];
      const double swingDelay = std::clamp(params_.swing, 0.0, 0.75) * stepBeats * (2.0 / 3.0);
      const bool speaking = params_.mode == Mode::Speak;

      if (!speaking)
         listen(in, inCount, b0, bps, stepBeats);
      else
         listenCount_ = 0;

      const long long sFirst = std::max(0LL, static_cast<long long>(std::floor(b0 / stepBeats)) - 1);
      const long long sLast = static_cast<long long>(std::floor(b1 / stepBeats));
      for (long long s = sFirst; s <= sLast; ++s) {
         const long long g = s / kWindowSteps;
         const int w = static_cast<int>(s % kWindowSteps);
         const GlyphNotes *notes = nullptr;
         if (g > glyph_)
            enter(g, speaking);
         if (g == glyph_)
            notes = &notes_;
         else if (g == glyph_ - 1)
            notes = &previousNotes_;
         if (!notes || !speaking)
            continue;
         for (int i = 0; i < notes->count; ++i) {
            const GlyphNote &n = notes->notes[i];
            if (n.start != w)
               continue;
            const double jitter = humanizeNoise(s, i);
            double beat = s * stepBeats + ((s & 1) ? swingDelay : 0.0);
            beat += std::max(0.0, jitter) * params_.humanize * 0.25 * stepBeats;
            if (beat < b0 || beat >= b1)
               continue;
            const uint32_t time = std::min(b.frames - 1, static_cast<uint32_t>((beat - b0) / bps));
            int velocity = static_cast<int>(std::lround(n.velocity * (0.4 + 0.75 * params_.velocity) +
                                                        jitter * params_.humanize * 24.0));
            velocity = std::clamp(velocity, 1, 127);
            const double length = std::max(n.length * stepBeats * params_.gate, 16.0 * bps);
            start(out, time, static_cast<uint8_t>(pitches_[n.row]), static_cast<uint8_t>(velocity), beat + length);
         }
      }

      // Note-offs due before the block ends.
      for (int i = 0; i < activeCount_;) {
         if (active_[i].off < b1) {
            const double t = std::clamp((active_[i].off - b0) / bps, 0.0, static_cast<double>(b.frames - 1));
            out.push_back({static_cast<uint32_t>(t), active_[i].pitch, 0, false});
            active_[i] = active_[--activeCount_];
         }
         else
            ++i;
      }
      std::stable_sort(out.begin() + first, out.end(), [](const NoteOut &a, const NoteOut &c) {
         return a.time != c.time ? a.time < c.time : (!a.on && c.on);
      });

      step_ = static_cast<float>(b1 / stepBeats - static_cast<double>(std::max<long long>(glyph_, 0)) * kWindowSteps);
      step_ = std::clamp(step_, 0.0f, static_cast<float>(kWindowSteps));
      publishPitches();
   }

   // The cell the voice reads at glyph position `g` (the count of glyph windows since the song's start).
   int cellAt(long long g) const {
      const long long p = g + offset_;
      const int length = params_.length;
      const auto mod = [](long long a, long long m) { return static_cast<int>(((a % m) + m) % m); };
      switch (params_.direction) {
      case Direction::Forward: return mod(p, length);
      case Direction::Reverse: return length - 1 - mod(p, length);
      case Direction::PingPong: {
         if (length < 2)
            return 0;
         const int q = mod(p, 2LL * length - 2);
         return q < length ? q : 2 * length - 2 - q;
      }
      case Direction::Random:
         return static_cast<int>(hashMix(static_cast<uint64_t>(params_.seed) * 977 + 13, static_cast<uint64_t>(p)) %
                                 static_cast<uint64_t>(length));
      }
      return 0;
   }

   // Forgets what was sounding and where the voice was, without sending anything: for a fresh
   // activation, when the host has already silenced everything.
   void resetVoice() {
      activeCount_ = 0;
      running_ = false;
      offset_ = 0;
      glyph_ = INT_MIN;
      listenGlyph_ = INT_MIN;
      listenCount_ = 0;
      pendingJump_ = -1;
      held_ = 0;
      followRoot_ = -1;
      step_ = 0.0f;
      current_ = cellAt(0);
      history_.fill(-1);
   }

   void fillFrame(SlabFrame &f) const {
      f.cells = cells_;
      f.key = slabKey_.pack();
      f.generation = generation_;
      f.length = params_.length;
      f.current = current_;
      f.step = running_ ? step_ : 0.0f;
      f.playing = running_;
      f.listening = params_.mode == Mode::Listen;
      f.heard = listenCount_;
      f.history = history_;
      f.rowPitch = pitches_;
      f.orthography = params_.orthography;
   }

   // ---- state ----

   static constexpr size_t kCellBytes = 18;

   // A slab and its script (ScriptKey::pack), as bytes for the plugin's state.
   static std::vector<uint8_t> save(const std::array<Cell, kCells> &cells, uint64_t key) {
      std::vector<uint8_t> out;
      for (int i = 0; i < 8; ++i)
         out.push_back(static_cast<uint8_t>(key >> (8 * i)));
      for (const Cell &c : cells) {
         uint8_t bytes[kCellBytes];
         writeCell(c, bytes);
         out.insert(out.end(), bytes, bytes + kCellBytes);
      }
      return out;
   }

   // The commands that put a saved slab back; false if `data` isn't one.
   template <class Push>
   static bool load(const uint8_t *data, size_t size, Push &&push) {
      if (size != 8 + kCells * kCellBytes)
         return false;
      uint64_t key = 0;
      for (int i = 0; i < 8; ++i)
         key |= static_cast<uint64_t>(data[i]) << (8 * i);
      for (int i = 0; i < kCells; ++i) {
         Cell c;
         if (!readCell(data + 8 + i * kCellBytes, c))
            return false;
         Command cmd;
         cmd.kind = Command::Kind::Load;
         cmd.cell = static_cast<int8_t>(i);
         cmd.glyph = c.glyph;
         cmd.eroded = c.eroded;
         cmd.word = c.word;
         push(cmd);
      }
      Command k;
      k.kind = Command::Kind::LoadKey;
      k.key = key;
      push(k);
      return true;
   }

   static void writeCell(const Cell &c, uint8_t *b) {
      std::memset(b, 0, kCellBytes);
      b[0] = c.glyph.count;
      for (int i = 0; i < kMaxStrokes; ++i) {
         b[1 + 2 * i] = c.glyph.strokes[i].from;
         b[2 + 2 * i] = c.glyph.strokes[i].to;
      }
      b[13] = static_cast<uint8_t>(c.glyph.dot);
      b[14] = c.glyph.bar ? 1 : 0;
      b[15] = c.eroded;
      b[16] = static_cast<uint8_t>(c.word & 0xff);
      b[17] = static_cast<uint8_t>((c.word >> 8) & 0xff);
   }

   static bool readCell(const uint8_t *b, Cell &c) {
      c = Cell{};
      if (b[0] > kMaxStrokes)
         return false;
      c.glyph.count = b[0];
      for (int i = 0; i < kMaxStrokes; ++i) {
         c.glyph.strokes[i] = {b[1 + 2 * i], b[2 + 2 * i]};
         if (i < c.glyph.count && (c.glyph.strokes[i].from >= kNodes || c.glyph.strokes[i].to >= kNodes))
            return false;
      }
      const int8_t dot = static_cast<int8_t>(b[13]);
      c.glyph.dot = dot >= 0 && dot < kNodes ? dot : -1;
      c.glyph.bar = b[14] != 0;
      c.eroded = std::min<uint8_t>(b[15], kMaxStrokes);
      c.word = static_cast<int16_t>(b[16] | (b[17] << 8));
      return true;
   }

private:
   struct Active {
      uint8_t pitch;
      double off;
   };

   template <class Out>
   void start(Out &out, uint32_t time, uint8_t pitch, uint8_t velocity, double off) {
      for (int i = 0; i < activeCount_; ++i)
         if (active_[i].pitch == pitch) {
            out.push_back({time, pitch, 0, false});
            active_[i] = active_[--activeCount_];
            break;
         }
      if (activeCount_ >= kMaxActive)
         return;
      out.push_back({time, pitch, velocity, true});
      active_[activeCount_++] = {pitch, off};
   }

   template <class Out>
   void stop(Out &out) {
      allNotesOff(out, 0);
      running_ = false;
      offset_ = 0;
      glyph_ = INT_MIN;
      listenCount_ = 0;
      listenGlyph_ = INT_MIN;
      current_ = cellAt(0);
      step_ = 0.0f;
   }

   // The voice moves on to glyph window `g`: the cell it leaves may drift or erode (not in Listen,
   // not when locked, not on the first glyph after a start or jump), and the new cell's notes are
   // read now, so an edit while it plays is heard the next time round.
   void enter(long long g, bool speaking) {
      if (glyph_ != INT_MIN && speaking)
         leave(current_);
      if (pendingJump_ >= 0) {
         for (long long k = 0; k < 4LL * kCells; ++k)
            if (cellAt(g + k) == pendingJump_) {
               offset_ += k;
               break;
            }
         pendingJump_ = -1;
      }
      previousNotes_ = glyph_ == g - 1 ? notes_ : GlyphNotes{};
      glyph_ = g;
      current_ = cellAt(g);
      notes_ = decode(cells_[current_].glyph, cells_[current_].visible());
      std::rotate(history_.begin(), history_.begin() + 1, history_.end());
      history_.back() = current_;
   }

   void leave(int cell) {
      if (params_.lock)
         return;
      if (drift_.uniform() < params_.drift) {
         cells_[cell] = scribe_.next(wordBefore(cell), params_.grammar, drift_);
         return;
      }
      if (drift_.uniform() < params_.erode && cells_[cell].visible() > 1)
         ++cells_[cell].eroded;
   }

   // The word in the cell read before `cell`, for the grammar.
   int wordBefore(int cell) const {
      const int length = params_.length;
      return cells_[(cell + length - 1) % length].word;
   }

   // A repeatable -1..1 per step and note, so humanizing never depends on block sizes.
   double humanizeNoise(long long s, int i) const {
      const uint64_t h = hashMix(static_cast<uint64_t>(s) * 31 + static_cast<uint64_t>(i), 0x7f4a);
      return static_cast<double>(h >> 11) * (2.0 / 9007199254740992.0) - 1.0;
   }

   // Key follow: in Speak mode, the newest held note sets the root while any are held.
   void handleFollow(const NoteIn *in, int count) {
      for (int i = 0; i < count; ++i) {
         if (in[i].on) {
            followRoot_ = in[i].pitch % 12;
            ++held_;
         }
         else if (held_ > 0 && --held_ == 0)
            followRoot_ = -1;
      }
   }

   void publishPitches() {
      const int root = params_.follow && params_.mode == Mode::Speak && followRoot_ >= 0 ? followRoot_ : params_.root;
      for (int r = 0; r < 4; ++r)
         pitches_[r] = rowPitch(r, root, params_.scale, params_.rows, params_.octave);
   }

   // ---- listen ----

   struct Heard {
      double start;
      double end; // < 0 while held
      uint8_t pitch, velocity;
   };

   void listen(const NoteIn *in, int count, double b0, double bps, double stepBeats) {
      for (int i = 0; i < count; ++i) {
         const double steps = (b0 + in[i].time * bps) / stepBeats;
         const long long g = static_cast<long long>(std::floor(steps / kWindowSteps));
         if (g != listenGlyph_) {
            finishListening();
            listenGlyph_ = g;
         }
         const double local = steps - static_cast<double>(g) * kWindowSteps;
         if (in[i].on) {
            if (listenCount_ < kMaxListen)
               heard_[listenCount_++] = {local, -1.0, in[i].pitch, in[i].velocity};
         }
         else
            for (int k = 0; k < listenCount_; ++k)
               if (heard_[k].pitch == in[i].pitch && heard_[k].end < 0.0) {
                  heard_[k].end = local;
                  break;
               }
      }
      // A window the song has left is transcribed even if nothing more is played.
      const long long now = static_cast<long long>(std::floor((b0 / stepBeats) / kWindowSteps));
      if (listenGlyph_ != INT_MIN && now > listenGlyph_) {
         finishListening();
         listenGlyph_ = now;
      }
   }

   // The notes heard in the window `listenGlyph_` become a glyph in the cell that window reads.
   void finishListening() {
      if (listenGlyph_ == INT_MIN || listenCount_ == 0) {
         listenCount_ = 0;
         return;
      }
      PlayedNote played[kMaxListen];
      for (int i = 0; i < listenCount_; ++i) {
         const Heard &h = heard_[i];
         const int start = std::clamp(static_cast<int>(std::lround(h.start)), 0, kWindowSteps - 1);
         const double end = h.end < 0.0 ? kWindowSteps : h.end;
         played[i] = {start, std::max(1, static_cast<int>(std::lround(end - h.start))), nearestRow(h.pitch), h.velocity};
      }
      std::stable_sort(played, played + listenCount_, [](const PlayedNote &a, const PlayedNote &c) { return a.start < c.start; });
      const Glyph g = transcribe(played, listenCount_, params_.orthography);
      if (g.count > 0)
         cells_[cellAt(listenGlyph_)] = {g, 0, -1};
      listenCount_ = 0;
   }

   // The row whose pitch is nearest, octaves aside; on a tie, the nearest in absolute terms.
   int nearestRow(int pitch) const {
      int best = 0, bestFolded = 99, bestAbs = 999;
      for (int r = 0; r < 4; ++r) {
         int d = ((pitch - pitches_[r]) % 12 + 12) % 12;
         d = std::min(d, 12 - d);
         const int a = std::abs(pitch - pitches_[r]);
         if (d < bestFolded || (d == bestFolded && a < bestAbs)) {
            best = r;
            bestFolded = d;
            bestAbs = a;
         }
      }
      return best;
   }

   EngineParams params_;
   Scribe scribe_;
   std::array<Cell, kCells> cells_{};
   ScriptKey slabKey_;
   uint32_t generation_ = 0;
   Rng drift_;

   // The voice.
   bool running_ = false;
   double expected_ = 0.0, freeBeats_ = 0.0;
   long long glyph_ = INT_MIN, offset_ = 0;
   int current_ = 0, pendingJump_ = -1;
   float step_ = 0.0f;
   GlyphNotes notes_, previousNotes_;
   std::array<Active, kMaxActive> active_{};
   int activeCount_ = 0;
   std::array<int, kHistory> history_{-1, -1, -1, -1, -1, -1};
   std::array<int, 4> pitches_{72, 68, 65, 61};
   int followRoot_ = -1, held_ = 0;

   // Listen.
   std::array<Heard, kMaxListen> heard_{};
   int listenCount_ = 0;
   long long listenGlyph_ = INT_MIN;
};

} // namespace monolith

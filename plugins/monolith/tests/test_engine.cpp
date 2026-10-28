// Monolith's engine on its own: decoding, the scribe and its scripts, the voice in time, drift,
// erosion, listening and the saved slab.

#include <jclap/core/test_host.h>

#include <set>
#include <vector>

#include "engine.h"

using namespace monolith;

namespace {

Glyph glyphOf(std::initializer_list<std::pair<int, int>> strokes, int dot = -1, bool bar = false) {
   Glyph g;
   for (auto [a, b] : strokes)
      g.strokes[g.count++] = {static_cast<uint8_t>(a), static_cast<uint8_t>(b)};
   g.dot = static_cast<int8_t>(dot);
   g.bar = bar;
   return g;
}

// Runs the engine from beat `from` for `beats` beats of a playing transport at 120 BPM, 48 kHz, in
// blocks of 256, and returns every note it sent with times counted from `from`.
struct Timed {
   double beat;
   NoteOut note;
};

std::vector<Timed> run(Engine &e, double from, double beats, const std::vector<NoteIn> &in = {}, bool playing = true) {
   std::vector<Timed> all;
   const double perFrame = 2.0 / 48000.0;
   const int blocks = static_cast<int>(beats / (256 * perFrame) + 0.5);
   for (int k = 0; k < blocks; ++k) {
      Block b;
      b.hasTransport = true;
      b.playing = playing;
      b.frames = 256;
      b.beats = from + k * 256 * perFrame;
      std::vector<NoteIn> block;
      for (const NoteIn &n : in)
         if (n.time >= static_cast<uint32_t>(k * 256) && n.time < static_cast<uint32_t>((k + 1) * 256))
            block.push_back({n.time - k * 256u, n.pitch, n.velocity, n.on});
      std::vector<NoteOut> out;
      e.process(b, block.data(), static_cast<int>(block.size()), out);
      for (const NoteOut &n : out)
         all.push_back({k * 256 * perFrame + n.time * perFrame, n});
   }
   return all;
}

int ons(const std::vector<Timed> &notes) {
   int n = 0;
   for (const Timed &t : notes)
      n += t.note.on;
   return n;
}

EngineParams still() {
   EngineParams p;
   p.lock = true;
   p.drift = 0.0;
   p.length = 1;
   return p;
}

} // namespace

int main() {
   // ---- decoding ----
   {
      // Across row 2 (2 grid units: 3 steps), down five rows from the same start as a chord (2.1
      // units: 3 steps), then a short diagonal up two rows; a dot on the bottom row.
      const Glyph g = glyphOf({{6, 8}, {6, 21}, {21, 16}}, 22);
      const GlyphNotes n = decode(g);
      JCLAP_CHECK_EQ(n.count, 4);
      JCLAP_CHECK_EQ(n.notes[0].start, 0);
      JCLAP_CHECK_EQ(n.notes[0].length, 3);
      JCLAP_CHECK_EQ(n.notes[0].row, 2);
      JCLAP_CHECK_EQ(n.notes[0].velocity, 80);
      JCLAP_CHECK_EQ(n.notes[1].start, 0); // a chord with the first
      JCLAP_CHECK_EQ(n.notes[1].length, 3);
      JCLAP_CHECK_EQ(n.notes[2].start, 3);
      JCLAP_CHECK_EQ(n.notes[2].row, 7);
      JCLAP_CHECK_EQ(n.notes[2].length, 2); // 1.32 units * 1.5 rounds to 2
      JCLAP_CHECK(n.notes[3].ghost);
      JCLAP_CHECK_EQ(n.notes[3].start, 5);
      JCLAP_CHECK_EQ(n.notes[3].velocity, 40);
      JCLAP_CHECK_EQ(n.end, 5);

      // Eroded down to one stroke: the rest are gone, the dot still follows.
      const GlyphNotes worn = decode(g, 1);
      JCLAP_CHECK_EQ(worn.count, 2);
      JCLAP_CHECK_EQ(worn.notes[1].start, 3);

      // An overbar holds the last note to the end of the window.
      const GlyphNotes held = decode(glyphOf({{0, 1}, {1, 2}}, -1, true));
      JCLAP_CHECK_EQ(held.notes[1].start, 2);
      JCLAP_CHECK_EQ(held.notes[1].length, 14);

      // Long glyphs are cut at the window.
      const GlyphNotes cut = decode(glyphOf({{0, 23}, {23, 0}, {0, 23}, {23, 0}}));
      JCLAP_CHECK_EQ(cut.count, 4); // 5 + 5 + 5 steps, then the fourth starts at 15 and is cut to one
      JCLAP_CHECK(cut.notes[cut.count - 1].start + cut.notes[cut.count - 1].length <= kWindowSteps);
   }

   // ---- rows ----
   {
      // F Phrygian in thirds: Bb3 on the bottom row, the root F4 on the third row up, then Ab4, C5
      // and on up to Bb5 on the top row, two octaves.
      JCLAP_CHECK_EQ(rowPitch(7, 5, 3, 1, 0), 58);
      JCLAP_CHECK_EQ(rowPitch(5, 5, 3, 1, 0), 65);
      JCLAP_CHECK_EQ(rowPitch(4, 5, 3, 1, 0), 68);
      JCLAP_CHECK_EQ(rowPitch(3, 5, 3, 1, 0), 72);
      JCLAP_CHECK_EQ(rowPitch(0, 5, 3, 1, 0), 82);
      // In steps, Db4 to Db5; in fifths, Eb3 to Eb6.
      JCLAP_CHECK_EQ(rowPitch(7, 5, 3, 0, 0), 61);
      JCLAP_CHECK_EQ(rowPitch(0, 5, 3, 0, 0), 73);
      JCLAP_CHECK_EQ(rowPitch(7, 5, 3, 3, 0), 51);
      JCLAP_CHECK_EQ(rowPitch(0, 5, 3, 3, 0), 99);
      // Every spread climbs from the bottom row to the top.
      for (int spread = 0; spread < 4; ++spread)
         for (int r = 1; r < kGridRows; ++r)
            JCLAP_CHECK(rowPitch(r - 1, 5, 3, spread, 0) > rowPitch(r, 5, 3, spread, 0));
   }

   // ---- the scribe ----
   {
      Engine a, b;
      EngineParams p;
      a.setParams(p);
      b.setParams(p);
      JCLAP_CHECK(a.cells() == b.cells());
      p.seed = 2;
      b.setParams(p);
      JCLAP_CHECK(!(a.cells() == b.cells()));

      // Every script only writes the strokes it allows, at about the density asked for.
      for (int o = 0; o < kOrthographyCount; ++o)
         for (int density = 1; density <= kMaxStrokes; ++density) {
            EngineParams q;
            q.orthography = static_cast<Orthography>(o);
            q.density = density;
            q.grammar = 0.0;
            Engine e;
            e.setParams(q);
            int strokes = 0;
            for (const Cell &c : e.cells()) {
               JCLAP_CHECK(c.glyph.count >= 1 && c.glyph.count <= kMaxStrokes);
               strokes += c.glyph.count;
               for (int i = 0; i < c.glyph.count; ++i)
                  JCLAP_CHECK(strokeAllowed(q.orthography, c.glyph.strokes[i].from, c.glyph.strokes[i].to));
            }
            const double mean = strokes / static_cast<double>(kCells);
            JCLAP_CHECK(std::fabs(mean - density) < 1.0);
         }

      // A small lexicon repeats itself; a large one hardly does.
      EngineParams few;
      few.lexicon = 4;
      few.grammar = 0.0;
      Engine e;
      e.setParams(few);
      std::set<int> words;
      for (const Cell &c : e.cells())
         words.insert(c.word);
      JCLAP_CHECK(words.size() <= 4u);

      // Changing a param the script doesn't depend on leaves the slab alone.
      EngineParams q;
      Engine f;
      f.setParams(q);
      const auto before = f.cells();
      q.rate = 4;
      q.root = 2;
      q.length = 30;
      f.setParams(q);
      JCLAP_CHECK(f.cells() == before);
   }

   // ---- transcribing ----
   {
      // A glyph of back-to-back strokes, played and transcribed, decodes to the same notes.
      const Glyph g = glyphOf({{4, 7}, {7, 6}, {6, 0}});
      const GlyphNotes n = decode(g);
      PlayedNote played[kMaxGlyphNotes];
      for (int i = 0; i < n.count; ++i)
         played[i] = {n.notes[i].start, n.notes[i].length, n.notes[i].row, n.notes[i].velocity};
      const GlyphNotes back = decode(transcribe(played, n.count, Orthography::Monolith));
      JCLAP_CHECK_EQ(back.count, n.count);
      for (int i = 0; i < n.count && i < back.count; ++i) {
         JCLAP_CHECK_EQ(back.notes[i].start, n.notes[i].start);
         JCLAP_CHECK_EQ(back.notes[i].row, n.notes[i].row);
         JCLAP_CHECK_EQ(back.notes[i].velocity, n.notes[i].velocity);
         JCLAP_CHECK_EQ(back.notes[i].length, n.notes[i].length);
      }
      // In a script that doesn't allow a stroke, the nearest it does allow stands in.
      const Glyph circuit = transcribe(played, n.count, Orthography::Circuit);
      for (int i = 0; i < circuit.count; ++i)
         JCLAP_CHECK(strokeAllowed(Orthography::Circuit, circuit.strokes[i].from, circuit.strokes[i].to));
   }

   // ---- reading order ----
   {
      Engine e;
      EngineParams p;
      p.length = 4;
      p.direction = Direction::PingPong;
      e.setParams(p);
      const int expected[] = {0, 1, 2, 3, 2, 1, 0, 1};
      for (int g = 0; g < 8; ++g)
         JCLAP_CHECK_EQ(e.cellAt(g), expected[g]);
      p.direction = Direction::Reverse;
      e.setParams(p);
      JCLAP_CHECK_EQ(e.cellAt(0), 3);
      JCLAP_CHECK_EQ(e.cellAt(5), 2);
      p.direction = Direction::Random;
      e.setParams(p);
      for (int g = 0; g < 50; ++g)
         JCLAP_CHECK(e.cellAt(g) >= 0 && e.cellAt(g) < 4);
   }

   // ---- the start ----
   {
      // START moves the cells read round the slab, wrapping at its end, in every direction.
      Engine e;
      EngineParams p;
      p.length = 4;
      p.start = 53;
      e.setParams(p);
      const int forward[] = {53, 54, 0, 1, 53};
      for (int g = 0; g < 5; ++g)
         JCLAP_CHECK_EQ(e.cellAt(g), forward[g]);
      p.direction = Direction::Reverse;
      e.setParams(p);
      JCLAP_CHECK_EQ(e.cellAt(0), 1);
      p.direction = Direction::Random;
      e.setParams(p);
      for (int g = 0; g < 50; ++g)
         JCLAP_CHECK(inWindow(e.cellAt(g), 53, 4));
      JCLAP_CHECK(inWindow(0, 53, 4) && !inWindow(2, 53, 4) && !inWindow(52, 53, 4));
      JCLAP_CHECK_EQ(windowStep(1, 1, 53, 4), 53);
      JCLAP_CHECK_EQ(windowStep(53, -1, 53, 4), 1);

      // Playing, the voice reads from the start: a jump to a cell outside the cells read is ignored.
      p = still();
      p.lock = true;
      p.length = 2;
      p.start = 10;
      e.setParams(p);
      e.resetVoice();
      run(e, 0.0, 0.5);
      SlabFrame f;
      e.fillFrame(f);
      JCLAP_CHECK_EQ(f.current, 10);
      JCLAP_CHECK_EQ(f.start, 10);
      Command jump;
      jump.kind = Command::Kind::Jump;
      jump.cell = 11;
      e.apply(jump);
      run(e, 0.5, 4.0);
      e.fillFrame(f);
      JCLAP_CHECK_EQ(f.current, 11);
   }

   // ---- changing rate ----
   {
      // The voice carries on from the glyph it is reading when the rate changes, faster or slower,
      // rather than falling silent until the song reaches where the old rate had got to.
      for (int rate : {0, 1, 3, 4, 5}) {
         Engine e;
         EngineParams p;
         p.lock = true;
         p.drift = 0.0;
         p.length = 8;
         p.density = 4;
         e.setParams(p);
         run(e, 0.0, 9.0); // into the third glyph at 1/16
         SlabFrame f;
         e.fillFrame(f);
         const int reading = f.current;
         p.rate = rate;
         e.setParams(p);
         const std::vector<Timed> after = run(e, 9.0, 4.0 * kRateBeats[rate] * kWindowSteps);
         e.fillFrame(f);
         JCLAP_CHECK(ons(after) > 0);
         // Four glyphs later at the new rate, the voice is four cells on.
         JCLAP_CHECK_EQ(f.current, (reading + 4) % 8);
      }
   }

   // ---- the voice ----
   {
      // One locked glyph at 1/16: one bar plays its notes once, at their steps.
      Engine e;
      e.setParams(still());
      const GlyphNotes expected = decode(e.cells()[0].glyph, e.cells()[0].visible());
      const std::vector<Timed> bar = run(e, 0.0, 4.0);
      JCLAP_CHECK_EQ(ons(bar), expected.count);
      int on = 0;
      for (const Timed &t : bar)
         if (t.note.on) {
            // Notes go out within a sample of their step (a quarter beat each).
            JCLAP_CHECK(std::fabs(t.beat - expected.notes[on].start * 0.25) < 0.0001);
            ++on;
         }
      // Every note that started has ended or is still sounding into the next bar; stopping silences it.
      std::vector<Timed> stop = run(e, 4.0, 0.1, {}, false);
      int offs = 0;
      for (const Timed &t : bar)
         offs += !t.note.on;
      for (const Timed &t : stop)
         offs += !t.note.on;
      JCLAP_CHECK_EQ(offs, ons(bar));

      // Two bars repeat exactly while locked.
      Engine loop;
      loop.setParams(still());
      const std::vector<Timed> two = run(loop, 0.0, 8.0);
      JCLAP_CHECK_EQ(ons(two), 2 * expected.count);

      // Block size doesn't change what plays: one long block per beat gives the same note-ons.
      Engine big;
      big.setParams(still());
      int bigOns = 0;
      for (int k = 0; k < 4; ++k) {
         Block b;
         b.hasTransport = true;
         b.playing = true;
         b.frames = 24000;
         b.beats = k;
         std::vector<NoteOut> out;
         big.process(b, nullptr, 0, out);
         for (const NoteOut &n : out)
            bigOns += n.on;
      }
      JCLAP_CHECK_EQ(bigOns, expected.count);
   }
   {
      // Swing pushes odd steps later; velocity follows the column and the VELOCITY param.
      EngineParams p = still();
      p.swing = 0.5;
      Engine e;
      e.setParams(p);
      const GlyphNotes expected = decode(e.cells()[0].glyph, e.cells()[0].visible());
      const std::vector<Timed> bar = run(e, 0.0, 4.0);
      int on = 0;
      for (const Timed &t : bar)
         if (t.note.on && on < expected.count) {
            const double grid = expected.notes[on].start * 0.25;
            const double swing = (expected.notes[on].start & 1) ? 0.5 * 0.25 * (2.0 / 3.0) : 0.0;
            JCLAP_CHECK(std::fabs(t.beat - grid - swing) < 0.0001);
            ++on;
         }
   }
   {
      // No transport: the engine keeps its own time at 120 BPM.
      Engine e;
      e.setParams(still());
      int count = 0;
      for (int k = 0; k < 370; ++k) { // just short of 4 beats: 370 blocks of 256 at 48 kHz
         Block b;
         b.frames = 256;
         std::vector<NoteOut> out;
         e.process(b, nullptr, 0, out);
         for (const NoteOut &n : out)
            count += n.on;
      }
      JCLAP_CHECK_EQ(count, decode(e.cells()[0].glyph, e.cells()[0].visible()).count);
   }

   // ---- drift and erosion ----
   {
      EngineParams p;
      p.length = 1;
      p.drift = 1.0;
      Engine e;
      e.setParams(p);
      const Cell first = e.cells()[0];
      // Rewritten every bar (sometimes with the word it had, since words have favourite successors).
      std::set<int> seen = {first.word};
      for (int bar = 0; bar < 8; ++bar) {
         run(e, 4.0 * bar, 4.0);
         seen.insert(e.cells()[0].word);
      }
      JCLAP_CHECK(seen.size() >= 3u);

      p.lock = true;
      Engine locked;
      locked.setParams(p);
      const auto before = locked.cells();
      run(locked, 0.0, 4.0 * 6);
      JCLAP_CHECK(locked.cells() == before);

      EngineParams q;
      q.length = 1;
      q.drift = 0.0;
      q.erode = 1.0;
      q.density = 6;
      Engine worn;
      worn.setParams(q);
      const int strokes = worn.cells()[0].visible();
      run(worn, 0.0, 4.0 * 3);
      JCLAP_CHECK(worn.cells()[0].visible() < strokes || strokes == 1);
      JCLAP_CHECK(worn.cells()[0].visible() >= 1);
      Command restore;
      restore.kind = Command::Kind::Restore;
      worn.apply(restore);
      JCLAP_CHECK_EQ(worn.cells()[0].visible(), strokes);
   }

   // ---- commands ----
   {
      Engine e;
      EngineParams p;
      p.length = 8;
      p.drift = 0.0;
      e.setParams(p);
      Command jump;
      jump.kind = Command::Kind::Jump;
      jump.cell = 5;
      e.apply(jump);
      run(e, 0.0, 0.5);
      SlabFrame f;
      e.fillFrame(f);
      JCLAP_CHECK_EQ(f.current, 5);
      run(e, 0.5, 4.0);
      e.fillFrame(f);
      JCLAP_CHECK_EQ(f.current, 6);

      Command carve;
      carve.kind = Command::Kind::Set;
      carve.cell = 2;
      carve.glyph = glyphOf({{0, 2}});
      e.apply(carve);
      JCLAP_CHECK(e.cells()[2].glyph == carve.glyph);
      JCLAP_CHECK_EQ(e.cells()[2].word, -1);
   }

   // ---- the saved slab ----
   {
      Engine e;
      EngineParams p;
      p.seed = 99;
      p.orthography = Orthography::Cuneiform;
      e.setParams(p);
      Command carve;
      carve.kind = Command::Kind::Set;
      carve.cell = 7;
      carve.glyph = glyphOf({{1, 4}, {4, 8}}, 3, true);
      e.apply(carve);
      const std::vector<uint8_t> bytes = Engine::save(e.cells(), e.slabKey().pack());

      Engine other;
      bool ok = Engine::load(bytes.data(), bytes.size(), [&](const Command &c) { other.apply(c); });
      JCLAP_CHECK(ok);
      JCLAP_CHECK(other.cells() == e.cells());
      JCLAP_CHECK(other.slabKey() == e.slabKey());
      // Its params then match the loaded script, so they don't write over it.
      other.setParams(p);
      JCLAP_CHECK(other.cells() == e.cells());

      JCLAP_CHECK(!Engine::load(bytes.data(), bytes.size() - 1, [](const Command &) {}));
      std::vector<uint8_t> bad = bytes;
      bad[8] = 9; // seven strokes is more than a glyph can have
      JCLAP_CHECK(!Engine::load(bad.data(), bad.size(), [](const Command &) {}));

      // A slab saved on the old 3 x 4 grid keeps its shapes on the 3 x 8 one: rows 0, 1, 2, 3 become
      // 0, 2, 5, 7.
      Cell old;
      old.glyph = glyphOf({{3, 11}, {0, 2}}, 7);
      std::array<Cell, kCells> oldCells{};
      oldCells[4] = old;
      std::vector<uint8_t> oldBytes = Engine::save(oldCells, 0);
      JCLAP_CHECK(Engine::upgradeFromFourRows(oldBytes));
      std::vector<Command> upgraded;
      JCLAP_CHECK(Engine::load(oldBytes.data(), oldBytes.size(), [&](const Command &c) { upgraded.push_back(c); }));
      JCLAP_CHECK(upgraded[4].glyph == glyphOf({{nodeAt(0, 2), nodeAt(2, 7)}, {0, 2}}, nodeAt(1, 5)));
      // Nodes past the old grid can't be in an old slab.
      oldCells[4].glyph = glyphOf({{3, 20}});
      oldBytes = Engine::save(oldCells, 0);
      JCLAP_CHECK(!Engine::upgradeFromFourRows(oldBytes));
   }

   // ---- listening ----
   {
      EngineParams p;
      p.mode = Mode::Listen;
      p.length = 2;
      p.lock = true;
      p.root = 5; // F Phrygian
      p.scale = 3;
      Engine e;
      e.setParams(p);
      const auto before = e.cells();
      // Within the first bar: F4 (row 5) soft on step 1 for 2 steps, Ab4 (row 4) hard on step 3.
      // 1 step = 0.25 beat = 6000 frames.
      const std::vector<NoteIn> in = {
         {0, 65, 70, true}, {12000, 65, 0, false}, {12000, 68, 120, true}, {30000, 68, 0, false}};
      const std::vector<Timed> heard = run(e, 0.0, 5.0, in);
      JCLAP_CHECK_EQ(ons(heard), 0); // nothing is read out while listening
      const Glyph g = e.cells()[0].glyph;
      JCLAP_CHECK_EQ(g.count, 2);
      JCLAP_CHECK_EQ(g.strokes[0].from, nodeAt(0, 5));
      JCLAP_CHECK_EQ(g.strokes[1].from, nodeAt(2, 4));
      const GlyphNotes n = decode(g);
      JCLAP_CHECK_EQ(n.notes[0].length, 2);
      JCLAP_CHECK_EQ(n.notes[1].start, 2);
      // The second cell heard nothing and keeps its glyph.
      JCLAP_CHECK(e.cells()[1] == before[1]);
   }
   {
      // Partway through a window, the frame shows the notes heard so far and the glyph they carve,
      // before the cell itself changes.
      EngineParams p;
      p.mode = Mode::Listen;
      p.lock = true;
      p.root = 5;
      p.scale = 3;
      Engine e;
      e.setParams(p);
      const auto before = e.cells();
      run(e, 0.0, 1.0, {{0, 65, 90, true}, {6000, 65, 0, false}, {12000, 68, 110, true}});
      SlabFrame f;
      e.fillFrame(f);
      JCLAP_CHECK(f.listening);
      JCLAP_CHECK_EQ(f.heard, 2);
      JCLAP_CHECK_EQ(f.heardNotes[0].pitch, 65);
      JCLAP_CHECK(f.heardNotes[0].end > 0.0f);
      JCLAP_CHECK_EQ(f.heardNotes[1].velocity, 110);
      JCLAP_CHECK(f.heardNotes[1].end < 0.0f); // still held
      JCLAP_CHECK_EQ(f.carving.count, 2);
      JCLAP_CHECK_EQ(f.carving.strokes[0].from, nodeAt(1, 5));
      JCLAP_CHECK_EQ(f.carving.strokes[1].from, nodeAt(2, 4));
      JCLAP_CHECK(e.cells()[0] == before[0]);
   }

   // ---- key follow ----
   {
      EngineParams p = still();
      p.follow = true;
      Engine e;
      e.setParams(p);
      const std::vector<NoteIn> in = {{0, 62, 100, true}}; // hold a D
      run(e, 0.0, 0.1, in);
      SlabFrame f;
      e.fillFrame(f);
      JCLAP_CHECK_EQ(f.rowPitch[2], rowPitch(2, 2, p.scale, p.spread, p.octave));
      run(e, 0.1, 0.1, {{0, 62, 0, false}});
      e.fillFrame(f);
      JCLAP_CHECK_EQ(f.rowPitch[2], rowPitch(2, p.root, p.scale, p.spread, p.octave));
   }

   // The very first notes, read in the block that starts the song, play the rows' pitches: never
   // MIDI note 0 from rows not worked out yet.
   {
      EngineParams p;
      p.root = 2;
      Engine e;
      e.setParams(p);
      const auto notes = run(e, 0.0, 4.0);
      JCLAP_CHECK(ons(notes) > 0);
      for (const Timed &t : notes)
         if (t.note.on) {
            bool onARow = false;
            for (int r = 0; r < kGridRows; ++r)
               onARow |= t.note.pitch == rowPitch(r, p.root, p.scale, p.spread, p.octave);
            JCLAP_CHECK(onARow);
         }
   }

   // ---- range ----
   {
      // A range of one octave plays just as before, whatever the path.
      EngineParams p;
      p.length = 4;
      Engine plain, loop, climb;
      plain.setParams(p);
      p.path = Path::Loop;
      loop.setParams(p);
      p.path = Path::Climb;
      climb.setParams(p);
      const std::vector<Timed> a = run(plain, 0.0, 32.0), b = run(loop, 0.0, 32.0), c = run(climb, 0.0, 32.0);
      JCLAP_CHECK_EQ(a.size(), b.size());
      JCLAP_CHECK_EQ(a.size(), c.size());
      for (size_t i = 0; i < a.size() && i < b.size() && i < c.size(); ++i) {
         JCLAP_CHECK_EQ(a[i].note.pitch, b[i].note.pitch);
         JCLAP_CHECK_EQ(a[i].note.pitch, c[i].note.pitch);
      }
   }
   {
      // Loop over two octaves: a one-note glyph read on its own goes up an octave each time round
      // and back down.
      EngineParams p = still();
      p.range = 2;
      Engine e;
      e.setParams(p);
      Command carve;
      carve.kind = Command::Kind::Set;
      carve.cell = 0;
      carve.glyph = glyphOf({{nodeAt(0, 4), nodeAt(1, 4)}});
      e.apply(carve);
      const int base = rowPitch(4, p.root, p.scale, p.spread, p.octave);
      const std::vector<Timed> bars = run(e, 0.0, 16.0);
      std::vector<int> pitches;
      for (const Timed &t : bars)
         if (t.note.on)
            pitches.push_back(t.note.pitch);
      JCLAP_CHECK_EQ(pitches.size(), 4u);
      if (pitches.size() == 4) {
         JCLAP_CHECK_EQ(pitches[0], base);
         JCLAP_CHECK_EQ(pitches[1], base + 12);
         JCLAP_CHECK_EQ(pitches[2], base);
         JCLAP_CHECK_EQ(pitches[3], base + 12);
      }
   }
   {
      // Strokes from one node (a chord) change octave together, and every note stays in range.
      EngineParams p = still();
      p.range = 3;
      Engine e;
      e.setParams(p);
      Command carve;
      carve.kind = Command::Kind::Set;
      carve.cell = 0;
      carve.glyph = glyphOf({{6, 8}, {6, 21}});
      e.apply(carve);
      const int row = rowPitch(2, p.root, p.scale, p.spread, p.octave);
      const std::vector<Timed> bars = run(e, 0.0, 4.0 * 8);
      std::vector<int> on;
      for (const Timed &t : bars)
         if (t.note.on)
            on.push_back(t.note.pitch);
      JCLAP_CHECK_EQ(on.size(), 16u);
      std::set<int> lifts;
      for (size_t i = 0; i + 1 < on.size(); i += 2) {
         const int a = on[i] - row;
         JCLAP_CHECK_EQ(on[i + 1], on[i]);
         JCLAP_CHECK(a == 0 || a == 12 || a == 24);
         lifts.insert(a);
      }
      JCLAP_CHECK_EQ(lifts.size(), 3u);
   }
   {
      // Climb and Contour stay inside the range and use more than one octave of it; Contour over
      // eight glyphs and three octaves arches 0, 1, 1, 2, 2, 1, 1, 0.
      for (Path path : {Path::Climb, Path::Contour}) {
         EngineParams p = still();
         p.length = 8;
         p.range = 3;
         p.path = path;
         Engine e;
         e.setParams(p);
         for (int c = 0; c < 8; ++c) {
            Command carve;
            carve.kind = Command::Kind::Set;
            carve.cell = static_cast<int8_t>(c);
            carve.glyph = glyphOf({{nodeAt(0, 4), nodeAt(1, 4)}});
            e.apply(carve);
         }
         const int base = rowPitch(4, p.root, p.scale, p.spread, p.octave);
         const std::vector<Timed> bars = run(e, 0.0, 4.0 * 16);
         std::vector<int> lifts;
         for (const Timed &t : bars)
            if (t.note.on)
               lifts.push_back((t.note.pitch - base) / 12);
         JCLAP_CHECK_EQ(lifts.size(), 16u);
         for (int l : lifts)
            JCLAP_CHECK(l >= 0 && l <= 2);
         JCLAP_CHECK(std::set<int>(lifts.begin(), lifts.end()).size() > 1);
         if (path == Path::Contour && lifts.size() == 16) {
            const int arch[8] = {0, 1, 1, 2, 2, 1, 1, 0};
            for (int i = 0; i < 16; ++i)
               JCLAP_CHECK_EQ(lifts[i], arch[i % 8]);
         }
      }
   }

   return jclap::core::testResult();
}

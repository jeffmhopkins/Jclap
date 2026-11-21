// Prints what JComp plays, as text, for review: every style (or one), its sections' progressions,
// takes and fills as step grids, and the MIDI the engine sends for them (note names, velocities,
// lengths), bar by bar. Not part of the plugin.
//
//   jcomp-dump [genre|-1] [style|-1] [seed] [weird] [energy] [density] [length] [sections] [takes] [human]
//
// sections: a string of section digits (0 intro .. 6 outro), default "13" (verse, chorus).
// takes: how many takes of each section to print as grids (MIDI is printed for take 0 and the fill).
// human: 1 keeps the player's timing, velocity variation and looseness; 0 (default) plays dead on.

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "engine.h"
#include "recipe.h"

using namespace jcomp;

namespace {

const char *kNames[] = {"C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B"};
std::string noteName(int n) { return std::string(kNames[n % 12]) + std::to_string(n / 12 - 1); }

char hitChar(const Hit &h) {
   if (!h.on())
      return '.';
   switch (h.part) {
   case kPartTop: return 't';
   case kPartUpper: return 'u';
   case kPartBass: return 'b';
   case kPartFifth: return 'f';
   default: break;
   }
   if (h.art == kArtChop || h.vel <= kGhost)
      return 'g';
   return h.vel >= kAccent ? 'X' : (h.vel <= kSoft ? 'o' : 'x');
}

std::string grid(const Pattern &p) {
   std::string s;
   for (int i = 0; i < p.steps; ++i) {
      if (i && i % p.stepsPerBar() == 0)
         s += '|';
      const Hit &h = p.at(i);
      char c = hitChar(h);
      // A held step: '-' after a hit while its length lasts.
      if (!h.on()) {
         for (int k = 1; k <= i && k <= 32; ++k) {
            const Hit &b = p.at(i - k);
            if (b.on()) {
               if (b.len > k)
                  c = '-';
               break;
            }
         }
      }
      s += c;
   }
   std::string extra;
   for (int i = 0; i < p.steps; ++i) {
      const Hit &h = p.at(i);
      if (h.on() && (h.push || h.run || h.chance < 100 || h.nudge || h.art != kArtAsWritten || h.voicing >= 0)) {
         char buf[96];
         std::snprintf(buf, sizeof buf, " [%d:%s%s%s%s%s]", i, h.push ? "push " : "", h.run ? "run " : "",
                       h.chance < 100 ? ("p" + std::to_string(h.chance) + " ").c_str() : "",
                       h.art != kArtAsWritten ? kArtNames[h.art] : "", h.nudge ? (" n" + std::to_string(h.nudge)).c_str() : "");
         extra += buf;
      }
   }
   return s + (p.rate == kRate8T ? "  (8T)" : p.rate == kRate34 ? "  (3/4)" : "") + extra;
}

} // namespace

int main(int argc, char **argv) {
   const int onlyGenre = argc > 1 ? std::atoi(argv[1]) : -1;
   const int onlyStyle = argc > 2 ? std::atoi(argv[2]) : -1;
   const uint32_t seed = argc > 3 ? static_cast<uint32_t>(std::atoi(argv[3])) : 1;
   const double weird = argc > 4 ? std::atof(argv[4]) : -1;
   const double energy = argc > 5 ? std::atof(argv[5]) : -1;
   const double density = argc > 6 ? std::atof(argv[6]) : -1;
   const double length = argc > 7 ? std::atof(argv[7]) : -1;
   const std::string sections = argc > 8 ? argv[8] : "13";
   const int takes = argc > 9 ? std::atoi(argv[9]) : 1;
   const bool human = argc > 10 && std::atoi(argv[10]) != 0;

   for (int gi = 0; gi < genreCount(); ++gi) {
      if (onlyGenre >= 0 && gi != onlyGenre)
         continue;
      const Genre &g = genreAt(gi);
      for (int si = 0; si < static_cast<int>(g.styles.size()); ++si) {
         if (onlyStyle >= 0 && si != onlyStyle)
            continue;
         std::vector<double> v(kParamCount);
         for (clap_id i = 0; i < kParamCount; ++i)
            v[i] = kParams[i].defaultValue;
         for (const auto &[id, x] : styleValues(gi, si))
            v[id] = x;
         v[kSeed] = seed;
         if (weird >= 0)
            v[kWeird] = weird;
         if (energy >= 0)
            v[kEnergy] = energy;
         if (density >= 0)
            v[kDensity] = density;
         if (length >= 0)
            v[kLength] = length;
         const Recipe r = recipeFrom([&](clap_id id) { return v[id]; });
         Song song;
         generateSong(r, song);
         const Style &st = styleAt(g, si);
         const Player &pl = kPlayerList[std::clamp(static_cast<int>(v[kPlayer]), 0, kPlayers - 1)];
         std::printf("==== [%d.%d] %s / %s  bpm %d  player %s  swing %.2f feel %d  key %s  shape %d voices %d "
                     "window %d..%d center %d  runs %.2f  mods 0x%x  weird %.2f energy %.2f density %.2f length %.2f\n",
                     gi, si, g.name, st.name, st.bpm, pl.name, v[kSwing], static_cast<int>(v[kSwingFeel]),
                     kNames[song.key], static_cast<int>(v[kShape]), static_cast<int>(v[kVoices]),
                     static_cast<int>(v[kRangeLow]), static_cast<int>(v[kRangeHigh]), static_cast<int>(v[kCenter]),
                     v[kRuns], st.mods, v[kWeird], v[kEnergy], v[kDensity], v[kLength]);
         for (char sc : sections) {
            const int sec = sc - '0';
            if (sec < 0 || sec >= kSections)
               continue;
            const Section &S = song.sections[static_cast<size_t>(sec)];
            std::printf("-- %s (energy %d, %d bars, prog %d bars):", kSectionNames[sec], S.energy, S.bars, S.progBars);
            for (int b = 0; b < S.progBars; ++b) {
               const Chord a = S.chordAt(b, false), z = S.chordAt(b, true), nx = S.chordAt(b + 1, false);
               std::printf(" |%s", chordName(a, song.key, kMajor, z != a ? &z : &nx).c_str());
               if (S.split >> b & 1)
                  std::printf(" %s", chordName(z, song.key, kMajor, &nx).c_str());
            }
            std::printf(" |\n");
            for (int t = 0; t < std::clamp(takes, 1, kTakes); ++t)
               std::printf("   take %d: %s\n", t, grid(S.takes[static_cast<size_t>(t)]).c_str());
            std::printf("   fill  : %s\n", grid(S.fill).c_str());
            std::printf("   down  : %s\n", grid(S.fillDown).c_str());
            if (!S.ending.empty())
               std::printf("   ending: %s\n", grid(S.ending).c_str());

            // The engine, playing this section (take 0) with the host for its bars at the style's tempo.
            Engine e;
            e.setSong(song);
            Settings s;
            const auto whole = [&](clap_id id) { return static_cast<int>(std::lround(v[id])); };
            s.section = sec;
            s.take = 0;
            s.swing = v[kSwing];
            s.swingFeel = whole(kSwingFeel);
            s.key = song.key;
            s.scale = whole(kScale);
            s.chordsFrom = 0;
            s.voicing = voiceSettings([&](clap_id id) { return v[id]; });
            s.timing = human ? v[kTiming] : 0;
            s.velocityVar = human ? v[kVelocityVar] : 0;
            s.topVoice = v[kTopVoice];
            s.lengths = v[kLengths];
            s.length = v[kLength];
            s.density = v[kDensity];
            s.onsets = human ? whole(kOnsets) : 0;
            s.vary = human ? v[kVary] : 0;
            s.loose = human ? pl.loose : 0;
            s.runs = v[kRuns];
            s.runEveryBars = kRunEveryBars[choiceIndex(v[kRunEvery], 3)];
            s.runLandTop = whole(kRunLand) == 0;
            s.runHalfStep = whole(kRunHalfStep) != 0;
            s.runShorten = whole(kRunShorten) != 0;
            s.autoFillBars = kAutoFillBars[choiceIndex(v[kAutoFill], 4)];
            s.sameDiceBars = kSameDiceBars[choiceIndex(v[kSameDice], 5)];
            applyRecipe(s, r);
            e.setSettings(s);
            e.reset();
            const double tempo = st.bpm;
            const double sr = 48000.0;
            const double spb = sr * 60.0 / tempo;
            const int bars = S.bars;
            const int beats = song.beats;
            const int64_t total = static_cast<int64_t>(spb * beats * bars) + 2;
            auto out = std::make_unique<OutBuffer>();
            struct On {
               double beat;
               int key, vel;
            };
            std::map<int, On> started;
            std::vector<std::pair<On, double>> notes; // note, length in beats
            for (int64_t at = 0; at < total + static_cast<int64_t>(spb * 8); at += 256) {
               Block b;
               b.frames = 256;
               b.sampleRate = sr;
               b.tempo = tempo;
               b.hasTransport = true;
               b.playing = at < total;
               b.beats = at / spb;
               out->clear();
               if (b.playing)
                  e.process(b, *out);
               else {
                  e.stopAll(*out);
               }
               for (size_t i = 0; i < out->count; ++i) {
                  const MidiOut &m = out->items[i];
                  const double beat = (at + m.time) / spb;
                  const bool on = (m.status & 0xF0) == 0x90 && m.data2 > 0;
                  const bool off = (m.status & 0xF0) == 0x80 || ((m.status & 0xF0) == 0x90 && m.data2 == 0);
                  if (on) {
                     if (started.count(m.data1))
                        notes.push_back({started[m.data1], beat - started[m.data1].beat});
                     started[m.data1] = {beat, m.data1, m.data2};
                  } else if (off && started.count(m.data1)) {
                     notes.push_back({started[m.data1], beat - started[m.data1].beat});
                     started.erase(m.data1);
                  }
               }
               if (!b.playing)
                  break;
            }
            for (auto &[k, o] : started)
               notes.push_back({o, -1});
            std::sort(notes.begin(), notes.end(), [](auto &a, auto &b) {
               return a.first.beat != b.first.beat ? a.first.beat < b.first.beat : a.first.key < b.first.key;
            });
            // Group notes starting within 1/64 beat.
            int lastBar = -1;
            for (size_t i = 0; i < notes.size();) {
               size_t j = i;
               std::string line;
               while (j < notes.size() && notes[j].first.beat - notes[i].first.beat < 0.02)
                  ++j;
               std::sort(notes.begin() + static_cast<long>(i), notes.begin() + static_cast<long>(j),
                         [](auto &a, auto &b) { return a.first.key < b.first.key; });
               for (size_t k = i; k < j; ++k) {
                  char buf[48];
                  std::snprintf(buf, sizeof buf, " %s:%d/%.2f", noteName(notes[k].first.key).c_str(), notes[k].first.vel,
                                notes[k].second);
                  line += buf;
               }
               const double beat = notes[i].first.beat + 0.02;
               const int bar = static_cast<int>(beat / beats);
               if (bar != lastBar) {
                  const Chord &c = S.chordAt(bar, false);
                  std::printf("   bar %d (%s%s%s)\n", bar + 1, chordName(c, song.key).c_str(),
                              (S.split >> (bar % std::max<int>(1, S.progBars)) & 1) ? " / " : "",
                              (S.split >> (bar % std::max<int>(1, S.progBars)) & 1) ? chordName(S.chordAt(bar, true), song.key).c_str() : "");
                  lastBar = bar;
               }
               std::printf("     %6.3f%s\n", std::fmod(beat, static_cast<double>(beats)) + 0.98, line.c_str());
               i = j;
            }
         }
      }
   }
   return 0;
}

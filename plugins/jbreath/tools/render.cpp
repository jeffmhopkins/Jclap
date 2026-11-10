// Plays a score through the whole plugin (CLAP notes, pressure, the body, room and limiter) and
// writes it as a 32-bit float stereo WAV at 48 kHz: for listening to the instruments, and for
// comparing them with recordings.
//
//    jbreath-render SCORE OUT.wav
//
// The score is text, one command per line ('#' starts a comment):
//    preset NAME            load a factory preset (every other param at its default)
//    param NAME VALUE       set a param by its name, as the panel shows it ("Breath", "Bite")
//    note T KEY VEL PRESS DUR [BEND]
//                           at T seconds, a note on KEY (MIDI) at velocity VEL (0..1), its pressure
//                           PRESS (0..1, -1 for none), held DUR seconds
//    press T KEY PRESS      the pressure of the note on KEY from T seconds
//    swell T KEY P0 P1 SEC  the pressure of the note on KEY ramped from P0 to P1 over SEC seconds
//    end T                  stop at T seconds (default: two seconds after the last note ends)
//    words TEXT             the Alien's typed words (and switches it to TYPED)
//    say T KEY SEC TEXT     the Alien says TEXT from T seconds: TEXT becomes its typed words and
//                           each syllable a note on KEY, as long as speech would make it (SEC
//                           scales the pace: 0.22 is natural; stressed syllables longer and
//                           higher, little words short, the last falling, a pause at each comma
//                           or full stop; in Legato the syllables run together)

#include <jclap/core/test_host.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "editor.h"
#include "lyrics.h"
#include "params.h"
#include "presets.h"

extern "C" const clap_plugin_entry_t clap_entry;

namespace {

using namespace jbreath;

constexpr double kRate = 48000.0;
constexpr uint32_t kBlock = 64;

struct Ev {
   double t;
   int kind; // 0 on, 1 off, 2 pressure
   int key;
   double a, b;
   int id;
};

bool findParam(const std::string &name, clap_id &id) {
   for (clap_id i = 0; i < kParamCount; ++i)
      if (kParams[i].name && strcasecmp(kParams[i].name, name.c_str()) == 0) {
         id = i;
         return true;
      }
   return false;
}

void writeWav(const char *path, const std::vector<float> &l, const std::vector<float> &r) {
   FILE *f = std::fopen(path, "wb");
   if (!f)
      return;
   const uint32_t n = static_cast<uint32_t>(l.size()), bytes = n * 8;
   const auto u32 = [f](uint32_t v) { std::fwrite(&v, 4, 1, f); };
   const auto u16 = [f](uint16_t v) { std::fwrite(&v, 2, 1, f); };
   std::fwrite("RIFF", 1, 4, f);
   u32(36 + bytes);
   std::fwrite("WAVEfmt ", 1, 8, f);
   u32(16);
   u16(3);
   u16(2);
   u32(static_cast<uint32_t>(kRate));
   u32(static_cast<uint32_t>(kRate) * 8);
   u16(8);
   u16(32);
   std::fwrite("data", 1, 4, f);
   u32(bytes);
   for (uint32_t i = 0; i < n; ++i) {
      std::fwrite(&l[i], 4, 1, f);
      std::fwrite(&r[i], 4, 1, f);
   }
   std::fclose(f);
}

} // namespace

int main(int argc, char **argv) {
   if (argc < 3) {
      std::fprintf(stderr, "usage: jbreath-render SCORE OUT.wav\n");
      return 2;
   }
   std::ifstream in(argv[1]);
   if (!in) {
      std::fprintf(stderr, "can't read %s\n", argv[1]);
      return 1;
   }
   jclap::core::TestHost h(clap_entry, "com.jeffmhopkins.jclap.jbreath", kRate);
   std::vector<Ev> evs;
   double end = -1.0, last = 0.0;
   int nextId = 1;
   std::string line;
   while (std::getline(in, line)) {
      if (const size_t hash = line.find('#'); hash != std::string::npos)
         line.resize(hash);
      std::istringstream s(line);
      std::string cmd;
      if (!(s >> cmd))
         continue;
      if (cmd == "preset") {
         std::string name;
         std::getline(s >> std::ws, name);
         std::vector<Preset> bank = factoryPresets();
         jclap::ui::completePresets(bank, defaultValues());
         bool found = false;
         for (const Preset &p : bank)
            if (p.name == name) {
               for (const auto &v : p.values)
                  h.setParam(v.first, v.second);
               found = true;
            }
         if (!found)
            std::fprintf(stderr, "no preset %s\n", name.c_str());
      } else if (cmd == "param") {
         std::string rest;
         std::getline(s >> std::ws, rest);
         const size_t sp = rest.find_last_of(' ');
         clap_id id;
         if (sp == std::string::npos || !findParam(rest.substr(0, sp), id)) {
            std::fprintf(stderr, "no param in: %s\n", rest.c_str());
            continue;
         }
         h.setParam(id, std::atof(rest.c_str() + sp + 1));
      } else if (cmd == "note") {
         double t, vel, press, dur;
         int key;
         s >> t >> key >> vel >> press >> dur;
         const int id = nextId++;
         evs.push_back({t, 0, key, vel, press, id});
         evs.push_back({t + dur, 1, key, 0.5, 0, id});
         last = std::max(last, t + dur);
      } else if (cmd == "press") {
         double t, p;
         int key;
         s >> t >> key >> p;
         evs.push_back({t, 2, key, p, 0, -1});
      } else if (cmd == "swell") {
         double t, p0, p1, sec;
         int key;
         s >> t >> key >> p0 >> p1 >> sec;
         for (double u = 0; u <= sec + 1e-9; u += 0.005)
            evs.push_back({t + u, 2, key, p0 + (p1 - p0) * std::min(1.0, u / std::max(sec, 1e-6)), 0, -1});
      } else if (cmd == "words") {
         std::string text;
         std::getline(s >> std::ws, text);
         h.setParam(kAlienSource, 1);
         h.object<EditorHost>()->setLyrics(text);
      } else if (cmd == "say") {
         double t, sec;
         int key;
         std::string text;
         s >> t >> key >> sec;
         std::getline(s >> std::ws, text);
         h.setParam(kAlienSource, 1);
         h.object<EditorHost>()->setLyrics(text);
         speech::Book book;
         speech::readLyrics(text, book);
         for (int i = 0; i < book.count; ++i) {
            const speech::Syllable &y = book.syl[i];
            const bool stressed = y.stress == 1, last = i + 1 == book.count;
            const bool phraseEnd = last || text.find_first_of(",.;?!", book.end[i]) == static_cast<size_t>(book.end[i]);
            // (Its length as speech has it: its consonants' and its vowel's own lengths, the vowel
            // longer stressed and shorter in a little word, all longer at the end of a phrase; SEC
            // sets the pace, 0.22 natural.)
            std::string word = text.substr(book.begin[i], book.end[i] - book.begin[i]);
            for (char &c : word)
               c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            static const char *kLittle[] = {"the", "a", "an", "of", "to", "and", "in", "is", "it", "for", "on", "at",
                                            "as", "by", "his", "her", "was", "or", "but", "from", "with"};
            bool little = false;
            for (const char *w : kLittle)
               little = little || word == w;
            const double vowelMs = speech::kPhones[y.vowel].ms * (little ? 0.6 : stressed ? 1.0 : y.stress == 2 ? 0.8 : 0.6);
            double consMs = 0.0;
            for (int c = 0; c < y.onsetCount(); ++c)
               consMs += speech::kPhones[y.onset[c]].ms * (speech::kPhones[y.onset[c]].kind == speech::kStop ? 1.6 : 1.0);
            // (The last syllable's closing consonants are said as this note starts, so they're in
            // this one's length.)
            if (i > 0)
               for (int c = 0; c < book.syl[i - 1].codaCount(); ++c)
                  consMs += 0.8 * speech::kPhones[book.syl[i - 1].coda[c]].ms;
            double dur = sec / 0.22 * (vowelMs + consMs) * 0.001 * (phraseEnd ? 1.4 : 1.0);
            // (Speech's tune: falling slowly through each phrase, up on its stressed syllables,
            // down at its end.)
            const double through = book.count > 1 ? static_cast<double>(i) / (book.count - 1) : 0.0;
            const int k = key + static_cast<int>(std::lround(2.0 - 4.0 * through)) + (stressed && !little ? 3 : 0) - (phraseEnd ? 3 : 0);
            const int id = nextId++;
            const double gap = phraseEnd ? 0.2 : 0.0;
            // (Within a phrase the notes overlap a little, so in Legato each syllable runs on into
            // the next, as speech does.)
            evs.push_back({t, 0, k, 0.75, stressed ? 0.75 : 0.6, id});
            evs.push_back({t + dur + (phraseEnd ? 0.0 : 0.02), 1, k, 0.5, 0, id});
            t += dur + gap;
         }
         last = std::max(last, t);
      } else if (cmd == "end") {
         s >> end;
      }
   }
   if (end < 0)
      end = last + 2.0;
   std::stable_sort(evs.begin(), evs.end(), [](const Ev &a, const Ev &b) { return a.t < b.t; });
   // The note id sounding on each key, for its pressure.
   int keyId[128] = {};
   const uint32_t total = static_cast<uint32_t>(end * kRate);
   std::vector<float> L, R;
   L.reserve(total);
   R.reserve(total);
   float bl[kBlock], br[kBlock];
   float *bufs[2] = {bl, br};
   size_t next = 0;
   for (uint32_t start = 0; start < total; start += kBlock) {
      const uint32_t n = std::min(kBlock, total - start);
      while (next < evs.size() && evs[next].t * kRate < start + n) {
         const Ev &e = evs[next++];
         const uint32_t frame = static_cast<uint32_t>(e.t * kRate);
         const uint32_t t = frame - std::min(frame, start);
         const int16_t key = static_cast<int16_t>(std::clamp(e.key, 0, 127));
         if (e.kind == 0) {
            keyId[key] = e.id;
            h.note(CLAP_EVENT_NOTE_ON, key, e.a, e.id, 0, t);
            if (e.b >= 0)
               h.noteExpression(CLAP_NOTE_EXPRESSION_PRESSURE, key, e.b, e.id, 0, t);
         } else if (e.kind == 1) {
            h.note(CLAP_EVENT_NOTE_OFF, key, e.a, e.id, 0, t);
         } else {
            h.noteExpression(CLAP_NOTE_EXPRESSION_PRESSURE, key, e.a, keyId[key], 0, t);
         }
      }
      h.process(n, nullptr, bufs);
      L.insert(L.end(), bl, bl + n);
      R.insert(R.end(), br, br + n);
   }
   writeWav(argv[2], L, R);
   float peak = 0;
   for (size_t i = 0; i < L.size(); ++i)
      peak = std::max({peak, std::fabs(L[i]), std::fabs(R[i])});
   std::printf("%s: %.2f s, peak %.3f\n", argv[2], L.size() / kRate, peak);
   return 0;
}

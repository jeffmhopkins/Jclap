// Local tool: plays chords through a JArp factory preset and prints the arp's notes as a log
// (seconds status data1 data2).   bass-notes "Preset" bpm offsetSeconds bar:key,key[,key] ... endBar
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <sstream>
#include <string>
#include <vector>
#include "engine.h"
#include "params.h"
#include "presets.h"
using namespace jarp;
int main(int argc, char **argv) {
   if (argc < 6) return 2;
   const Preset *preset = nullptr;
   for (const Preset &p : factoryPresets()) if (p.name == argv[1]) preset = &p;
   if (!preset) { std::fprintf(stderr, "no preset %s\n", argv[1]); return 1; }
   const double bpm = std::atof(argv[2]), offset = std::atof(argv[3]);
   std::vector<double> v(kParamCount);
   for (auto &[id, x] : preset->values) v[id] = x;
   auto whole = [&](clap_id id) { return (int)std::lround(v[id]); };
   Settings s;
   s.rate = whole(kRate); s.swing = v[kSwing]; s.slot = slotIndex(v[kSlot]); s.advance = (Advance)whole(kAdvance);
   s.latch = false; s.restart = (Restart)whole(kRestart); s.degree = whole(kRowsBy) == 1 || whole(kRowsBy) == 2;
   s.strict = whole(kRowsBy) == 2; s.spread = whole(kRowsBy) == 3; s.wrap = whole(kEmptyRows) != 0;
   s.octaves = whole(kEmptyRows) == 2; s.key = whole(kKey); s.range = whole(kRange) + 1; s.keepRows = whole(kKeepRows) != 0;
   s.scale = whole(kScale); s.pressVelocity = v[kPressVelocity]; s.curve = v[kCurve]; s.pressRatchet = v[kPressRatchet]; s.maxHits = whole(kMaxHits);
   auto e = std::make_unique<Engine>();
   e->setSettings(s);
   e->setBank(*factoryBank(preset->name));
   struct Chord { int bar; std::vector<int> keys; };
   std::vector<Chord> chords;
   for (int i = 4; i < argc - 1; ++i) {
      std::string a = argv[i]; Chord c; c.bar = std::atoi(a.c_str());
      std::stringstream ks(a.substr(a.find(':') + 1)); std::string k;
      while (std::getline(ks, k, ',')) c.keys.push_back(std::atoi(k.c_str()));
      chords.push_back(c);
   }
   const int endBar = std::atoi(argv[argc - 1]);
   const double sr = 48000.0, beatSamples = sr * 60.0 / bpm;
   const int64_t total = (int64_t)std::llround(endBar * 4 * beatSamples);
   auto out = std::make_unique<OutBuffer>();
   const uint32_t kBlock = 64;
   size_t ci = 0; const std::vector<int> *held = nullptr;
   for (int64_t at = 0; at < total; at += kBlock) {
      Block b; b.frames = kBlock; b.sampleRate = sr; b.hasTransport = true; b.playing = true; b.tempo = bpm;
      b.beats = at / beatSamples;
      out->clear();
      e->beginBlock(b, *out);
      while (ci < chords.size()) {
         const int64_t t = (int64_t)std::llround(chords[ci].bar * 4 * beatSamples);
         if (t >= at + kBlock) break;
         const uint32_t off = (uint32_t)std::max<int64_t>(0, t - at);
         e->advance(off, *out);
         if (held) for (int k : *held) e->noteOff(off, 0, k, *out);
         held = &chords[ci].keys;
         for (int k : *held) e->noteOn(off, 0, k, 100, *out);
         ++ci;
      }
      e->advance(kBlock, *out);
      for (size_t k = 0; k < out->count; ++k) {
         const MidiOut &m = out->items[k];
         if ((m.status & 0xE0) != 0x80) continue;
         std::printf("%.6f %d %d %d\n", offset + (at + m.time) / sr, m.status & 0xF0, m.data1, m.data2);
      }
   }
   if (held) std::printf("%.6f %d %d %d\n", offset + total / sr, 0x80, 0, 0); // marker
   return 0;
}

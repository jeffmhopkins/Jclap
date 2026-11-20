// JComp's panel, on a 1000 x 640 design grid scaled to the window, in JDrummer's style: a title bar
// with the four tabs, a small transport and the preset browser, and cards under it.
//
//    GENERATE   GENRE (twelve tiles, the style, the browser of every genre), HOW IT PLAYS (the player,
//               ENERGY, WEIRD, ROLL SONG), THE SONG (seven sections with their chords, each its own
//               dice, lock and energy), MUTATE (the section's take now and five children)
//    CHORDS     KEY and scale, where the chords come from (the progression, held notes or both), each
//               section's progression bar by bar, and the chord picked: its root, quality, bass and
//               length, how JComp voices it, and what could stand in for it
//    EDIT       one section's take or fill on a piano roll, voiced as it will play, with each hit's
//               velocity, length, chance and nudge; how chords are voiced; the hit picked
//    PLAY       SECTIONS, NOW PLAYING, FEEL, TRANSPORT, PASSING RUNS, OUTPUT
//
// Colour carries meaning: the top voice is orange, the inner voices blue, the bottom purple, a
// passing run yellow; orange is also what is picked and playing.

#include "editor.h"
#include "engine.h"

#include <jclap/ui/button.h>
#include <jclap/ui/design_frame.h>
#include <jclap/ui/dropdown.h>
#include <jclap/ui/knob.h>
#include <jclap/ui/number_box.h>
#include <jclap/ui/paint.h>
#include <jclap/ui/param_bindings.h>
#include <jclap/ui/preset_bar.h>
#include <jclap/ui/presets.h>
#include <jclap/ui/segmented.h>
#include <jclap/ui/tabs.h>
#include <jclap/ui/theme.h>
#include <jclap/ui/tooltip.h>
#include <jclap/ui/update_notice.h>

#include <visage/widgets.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <map>
#include <random>
#include <string>
#include <vector>

#include "harmony.h"
#include "params.h"
#include "players.h"
#include "presets.h"
#include "recipe.h"
#include "styles.h"

namespace jcomp {
namespace {

namespace ui = jclap::ui;
using PlayFrame = jcomp::Frame;

constexpr float kW = kEditorWidth, kH = kEditorHeight;
constexpr float kHeader = 64.0f;

constexpr unsigned kOrange = 0xfff28c28;
constexpr unsigned kBlue = 0xff9fc3ee;
constexpr unsigned kPurple = 0xffbb8fec;
constexpr unsigned kGreen = 0xff8fd1a8;
constexpr unsigned kYellow = 0xffe8c66a;
constexpr unsigned kWhite = 0xffe6e8eb;
constexpr unsigned kSlate = 0xff454c58;
constexpr unsigned kRule = 0xff2c323b;
constexpr unsigned kCell = 0xff272c34;
constexpr unsigned kWell = 0xff181b20;
constexpr unsigned kTitle = 0xffc3c7cd;
constexpr unsigned kFaint = 0xff5d636c;
constexpr unsigned kWarm = 0xff2c2620; // a selected card's fill
constexpr unsigned kRed = 0xffc0503c;
constexpr unsigned kInk = 0xff1b1408;

enum Page { kGeneratePage, kChordsPage, kEditPage, kPlayPage };

unsigned alpha(unsigned c, float a) {
   return (static_cast<unsigned>(std::clamp(a, 0.0f, 1.0f) * ((c >> 24) & 0xff)) << 24) | (c & 0xffffff);
}

std::string upper(std::string s) {
   for (char &c : s)
      c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
   return s;
}

std::string pct(double v) { return std::to_string(static_cast<int>(std::lround(v * 100.0))); }

void drawCard(visage::Canvas &c, float s, float x, float y, float w, float h, const std::string &title,
              const std::string &note = {}, unsigned titleColour = kOrange) {
   const ui::Theme &t = ui::defaultTheme();
   c.setColor(t.panel);
   c.roundedRectangle(x * s, y * s, w * s, h * s, 6 * s);
   c.setColor(t.panelBorder);
   c.roundedRectangleBorder(x * s, y * s, w * s, h * s, 6 * s, 1.0f);
   if (!title.empty()) {
      c.setColor(titleColour);
      c.text(title, ui::font(11 * s), visage::Font::kLeft, (x + 13) * s, (y + 9) * s, (w - 26) * s, 16 * s);
   }
   if (!note.empty()) {
      c.setColor(alpha(t.dimText, 0.85f));
      c.text(note, ui::font(9 * s), visage::Font::kRight, (x + 13) * s, (y + 9) * s, (w - 26) * s, 16 * s);
   }
}

void label(visage::Canvas &c, float s, const std::string &l, float x, float y, float w = 160, unsigned colour = 0,
           visage::Font::Justification j = visage::Font::kLeft, float size = 9.5f) {
   c.setColor(colour ? colour : ui::defaultTheme().dimText);
   c.text(l, ui::font(size * s), j, x * s, y * s, w * s, (size + 5) * s);
}

// A dashed outline (square corners), in the current colour.
void dashed(visage::Canvas &c, float x, float y, float w, float h, float dash, float t) {
   for (float a = 0; a < w; a += dash * 2) {
      const float l = std::min(dash, w - a);
      c.fill(x + a, y, l, t);
      c.fill(x + a, y + h - t, l, t);
   }
   for (float a = 0; a < h; a += dash * 2) {
      const float l = std::min(dash, h - a);
      c.fill(x, y + a, t, l);
      c.fill(x + w - t, y + a, t, l);
   }
}

// A chord moved by `t` semitones (the KEY param against the key the song was written in).
Chord moved(Chord c, int t) {
   if (c.none() || t == 0)
      return c;
   c.root = static_cast<int8_t>((c.root + t + 120) % 12);
   if (c.bass >= 0)
      c.bass = static_cast<int8_t>((c.bass + t + 120) % 12);
   return c;
}

std::string notesText(const Voicing &v) {
   std::string out;
   for (int i = 0; i < v.count; ++i) {
      if (i)
         out += " ";
      out += noteText(v.notes[static_cast<size_t>(i)]);
   }
   return out;
}

// What a quality is, in a few words, for the chord card.
const char *qualityWords(int q) {
   switch (q) {
   case kMaj: return "a major triad";
   case kMin: return "a minor triad";
   case kDom7: return "the dominant seventh";
   case kMaj7: return "major seventh";
   case kMin7: return "minor seventh";
   case kDom9: return "the dominant with a ninth";
   case kMin9: return "minor with a ninth";
   case kMaj9: return "major seventh with a ninth";
   case kDom13: return "the dominant with a thirteenth";
   case kSharp9: return "the dominant, sharp 9 on top";
   case kFlat9: return "the dominant, flat 9";
   case kSix: return "major with a sixth";
   case kMin6: return "minor with a sixth";
   case kSixNine: return "major, sixth and ninth";
   case kSus: return "suspended fourth";
   case kSus7: return "the dominant with a suspended fourth";
   case kDim: return "a diminished triad";
   case kDim7: return "diminished seventh, a passing chord";
   case kHalfDim: return "half-diminished, the minor ii";
   case kAug: return "augmented";
   case kAdd9: return "major with an added ninth";
   case kMin11: return "minor with an eleventh";
   case kEleven: return "the dominant with an eleventh";
   case kMinMaj7: return "minor, major seventh";
   case kSharp11: return "the dominant, sharp 11";
   case kMaj7Sharp11: return "lydian major seventh";
   case kMinAdd9: return "minor with an added ninth";
   case kSus13: return "the dominant, suspended, thirteenth";
   default: return "";
   }
}

// ---- a take, voiced as it will play ----

enum NoteKind : uint8_t { kTopNote, kInnerNote, kBottomNote, kRunNote };
inline unsigned kindColour(int k) {
   switch (k) {
   case kTopNote: return kOrange;
   case kInnerNote: return kBlue;
   case kBottomNote: return kPurple;
   default: return kYellow;
   }
}

struct RollNote {
   float start, len; // in steps of the take's grid, from the view's start
   int key;
   uint8_t kind;
   float vel;
};
struct PlacedHit {
   int view = 0; // the step in the view
   Chord chord;
   Voicing voicing;
   Chord before; // the chord voiced before it
   int fromTop = -1;
};
struct Preview {
   std::vector<RollNote> notes;
   std::vector<PlacedHit> hits;
   int steps = 16, perBar = 16, bars = 1, perBeat = 4;
};

// Plays a take through over a section's progression for `bars` bars, as the engine would (without
// the dice): each hit's chord, its voicing led from the one before, its part and length, and a run
// into a hit that asks for one on a change of chord.
// DENSITY and LENGTH as the panel last read them, so every roll draws the part as it will sound.
struct LiveFeel {
   double density = 0.5, length = 0.5, runs = 0.25;
   int same = 4;       // SAME DICE's bars
   int every = 1;      // RUNS AT MOST every so many bars
   bool fixed = false; // the style's figure is played as written
};
LiveFeel gFeel;

void readFeel(const EditorHost &host) {
   gFeel.density = host.paramValue(kDensity);
   gFeel.length = host.paramValue(kLength);
   gFeel.runs = host.paramValue(kRuns);
   gFeel.same = kSameDiceBars[choiceIndex(host.paramValue(kSameDice), 5)];
   gFeel.every = kRunEveryBars[choiceIndex(host.paramValue(kRunEvery), 3)];
   gFeel.fixed = (styleOf(recipeFrom([&](clap_id id) { return host.paramValue(id); })).mods & kFixed) != 0;
}

// The engine's own dice, for the bar of the view a step is in: the roll draws the hits the first
// pass plays.
double rollDice(const Pattern &p, int view, int what) {
   const int bar = view / std::max(1, p.stepsPerBar());
   return diceFor(kDiceSalt, diceBarOf(bar, gFeel.same), view % std::max<int>(1, p.steps), what);
}

// The roll's DENSITY thinning, by the engine's weights.
bool thinnedInRoll(const Section &sec, const Pattern &p, int step, int view) {
   const int bar = view / std::max(1, p.stepsPerBar());
   return thinnedBy(p, step, gFeel.density, rollDice(p, view, 20), sec.chordAt(bar, true) != sec.chordAt(bar, false),
                    gFeel.fixed);
}

Preview preview(const Section &sec, const Pattern &p, int bars, const VoiceSettings &vs, int transpose, int key,
                int scale, bool runs = true) {
   Preview out;
   out.perBar = p.stepsPerBar();
   out.perBeat = p.perBeat();
   out.bars = std::max(1, bars);
   out.steps = out.perBar * out.bars;
   const int stepTicks = p.stepTicks();
   const int64_t barTicks = static_cast<int64_t>(p.stepsPerBar()) * stepTicks;
   Voicing prev;
   Chord prevChord;
   const int n = std::max<int>(1, p.steps);
   const int perBeat = p.perBeat();
   const bool fine = fineGrid(p);
   const bool offbeat = offbeatTake(p);
   int octaveStep = 0;
   // Runs on the take's grid, as the engine plays them: sixteenths where it plays them, else eighths.
   const int stride = fine || p.rate == kRate8T ? 1 : 2;
   int lastRunBar = -100;
   const auto chordAtView = [&](int view) {
      const int64_t tick = static_cast<int64_t>(view) * stepTicks;
      return moved(sec.chordAt(static_cast<int>(tick / barTicks), tick % barTicks >= barTicks / 2), transpose);
   };
   for (int v = 0; v < out.steps; ++v) {
      Hit h = p.at(v % n);
      const bool onBeat = v % perBeat == 0;
      const bool onEighth = p.rate == kRate8T ? v % 3 != 1 : v % 2 == 0;
      if (!h.on()) {
         // DENSITY above the middle fills gaps with softer repeats of the chord sounding.
         const double more = (gFeel.density - 0.5) * 2.0;
         const double chance = onEighth ? more * 0.85 : std::max(0.0, more - 0.35) * 1.2;
         if (more <= 0.0 || gFeel.fixed || hasLine(p) || prevChord.none() || heldOver(p, v % n) || (!onEighth && !fine) ||
             (offbeat && onBeat) ||
             rollDice(p, v, 21) >= chance)
            continue;
         h.vel = static_cast<uint8_t>(onBeat ? kSoft : kGhost + (onEighth ? 10 : 0));
         h.len = 1;
         h.art = more > 0.85 ? kArtAsWritten : kArtChop;
      } else if (thinnedInRoll(sec, p, v % n, v)) {
         continue; // DENSITY below the middle drops the weakest hits; each bar's first stays
      }
      const int64_t tick = static_cast<int64_t>(v) * stepTicks + (h.push ? kTicksPerBeat / 2 : 0);
      const int bar = static_cast<int>(tick / barTicks);
      const bool half = tick % barTicks >= barTicks / 2;
      const Chord c = moved(sec.chordAt(bar, half), transpose);
      if (c.none())
         continue;
      if (c != prevChord)
         octaveStep = 0;
      Voicing vc = vs.shape == kOctaves && h.part == kPartChord && h.voicing < 0
                       ? octaveLine(c, prev.empty() ? nullptr : &prev, vs, octaveStep++)
                       : voice(c, prev.empty() ? nullptr : &prev, vs, h.voicing, prevChord.none() ? nullptr : &prevChord);
      if (vc.empty())
         continue;
      PlacedHit pl;
      pl.view = v;
      pl.chord = c;
      pl.before = prevChord;
      pl.fromTop = prev.empty() ? -1 : prev.top();
      // A run into a change, as the engine finds it: from the step it would start on (four, three
      // or two notes ahead), over the chord before held to the change, where its dice and AT MOST
      // EVERY allow, never over a written chord; the soft hits under it give way.
      if (runs && gFeel.runs > 0.0 && !gFeel.fixed && h.run && h.part == kPartChord && !prev.empty() && c != prevChord)
         for (int len : {4, 3, 2}) {
            const int span = len * stride, start = v - span;
            if (start < 1 || (stride == 2 && start % 2))
               continue;
            const int sbar = start / out.perBar;
            if (lastRunBar > -100 && sbar - lastRunBar < gFeel.every)
               break;
            bool held = chordAtView(start - 1) == prevChord;
            for (int k = 0; k < span && held; ++k)
               held = chordAtView(start + k) == prevChord;
            if (!held)
               continue;
            if (diceFor(kDiceSalt, diceBarOf(sbar, gFeel.same) + sbar * 7, start % n, 9) >= std::min(1.0, gFeel.runs * 1.6))
               continue;
            const int top = prev.top(), land = vc.top();
            Ints r = passingRun(top, land, len, key, scale, true, c, prevChord);
            if (r.size() < 2) {
               Ints best;
               for (int other : {land - 12, land + 12}) {
                  const Ints o = passingRun(other, land, len, key, scale, true, c, prevChord);
                  if (o.size() >= 2 && o.front() != top && (best.empty() || std::abs(o.front() - top) < std::abs(best.front() - top)))
                     best = o;
               }
               if (best.empty() || std::abs(best.front() - top) > 9)
                  continue;
               r = best;
            }
            const int late = len - static_cast<int>(r.size());
            bool clear = true;
            for (int k = late * stride; k < span; ++k) {
               const Hit &o = p.at((start + k) % n);
               clear = clear && !(o.on() && !leftHand(o.part) && o.vel >= kNormal);
            }
            if (!clear)
               continue;
            const int from = start + late * stride;
            const auto under = [&](float at) {
               const int view = static_cast<int>(at);
               return view >= from && view < v && !leftHand(p.at(view % n).part);
            };
            out.notes.erase(std::remove_if(out.notes.begin(), out.notes.end(), [&](const RollNote &x) { return under(x.start); }),
                            out.notes.end());
            out.hits.erase(std::remove_if(out.hits.begin(), out.hits.end(),
                                          [&](const PlacedHit &x) { return under(static_cast<float>(x.view)); }),
                           out.hits.end());
            for (size_t k = 0; k < r.size(); ++k)
               out.notes.push_back({static_cast<float>(from + static_cast<int>(k) * stride), 0.85f * stride, r[k], kRunNote, 0.6f});
            lastRunBar = sbar;
            break;
         }
      // The gap to the next hit of the same hand, for RING; a left-hand note as written holds to the
      // next, two beats at most.
      const bool left = leftHand(h.part);
      int gap = 1;
      while (gap < n && (!p.at((v + gap) % n).on() || thinnedInRoll(sec, p, (v + gap) % n, v + gap) ||
                         leftHand(p.at((v + gap) % n).part) != left))
         ++gap;
      if (left && h.art == kArtAsWritten && h.len <= 1) {
         h.art = kArtRing;
         gap = std::min(gap, 2 * perBeat);
      }
      float len = static_cast<float>(h.len) * 0.92f;
      switch (h.art) {
      case kArtChop: len = 0.45f; break;
      case kArtHalf: len = std::max<float>(h.len, static_cast<float>(gap)) * 0.5f; break;
      case kArtRing: case kArtTopRings: len = gap * 0.97f; break;
      default: break;
      }
      if (gFeel.length < 0.5)
         len *= static_cast<float>(0.15 + 0.85 * gFeel.length / 0.5);
      else if (gFeel.length > 0.5 && gap * 0.97f > len)
         len += (gap * 0.97f - len) * static_cast<float>((gFeel.length - 0.5) / 0.5);
      Voicing played = vc;
      if (h.part == kPartTop) {
         played.notes[0] = static_cast<uint8_t>(vc.top());
         played.count = 1;
      } else if (h.part == kPartUpper && vc.count > 2) {
         played.notes[0] = vc.notes[static_cast<size_t>(vc.count - 2)];
         played.notes[1] = vc.notes[static_cast<size_t>(vc.count - 1)];
         played.count = 2;
      } else if (h.part == kPartLine) {
         const int low = vs.center - 6;
         const int root = low + ((c.root - low) % 12 + 12) % 12;
         played.notes[0] = static_cast<uint8_t>(std::clamp(root + lineInterval(c, h.degree, key, scale), 0, 127));
         played.count = 1;
      } else if (leftHand(h.part)) {
         played.notes[0] = static_cast<uint8_t>(bassNote(c, vs.floor, h.part == kPartFifth, vs.shape == kGrip));
         played.count = 1;
      }
      for (int i = 0; i < played.count; ++i) {
         const bool top = i == played.count - 1 && !leftHand(h.part);
         const uint8_t kind = leftHand(h.part) ? kBottomNote : top ? kTopNote : i == 0 ? kBottomNote : kInnerNote;
         float l = len;
         if (h.art == kArtTopRings && !top)
            l = std::min(len, std::max<float>(h.len, gap * 0.5f));
         out.notes.push_back({static_cast<float>(v), std::max(0.35f, l), played.notes[static_cast<size_t>(i)], kind,
                              h.vel / 127.0f});
      }
      if (h.part == kPartChord) {
         pl.voicing = vc;
         prev = vc;
         prevChord = c;
      } else
         pl.voicing = played;
      out.hits.push_back(pl);
   }
   return out;
}

// How many bars a section's view shows: its progression, at least its take, at most four.
int viewBars(const Section &sec, const Pattern &p) {
   return std::clamp<int>(std::max<int>(sec.progBars, p.bars()), 1, 4);
}

// Draws a preview's notes in a box: top voice orange with the line it sings dotted, inner blue,
// bottom purple, runs yellow. `playing` lights a step.
void drawRoll(visage::Canvas &c, float s, const Preview &pv, float x, float y, float w, float h, int playing = -1,
              bool beats = true, int selected = -1, int lo = -1, int hi = -1, float minRow = 1.5f) {
   c.setColor(kWell);
   c.roundedRectangle(x * s, y * s, w * s, h * s, 3 * s);
   if (pv.notes.empty()) {
      c.setColor(kFaint);
      c.text("silent", ui::font(8.5f * s), visage::Font::kCenter, x * s, y * s, w * s, h * s);
      return;
   }
   if (lo < 0) {
      lo = 127;
      hi = 0;
      for (const RollNote &n : pv.notes) {
         lo = std::min(lo, n.key);
         hi = std::max(hi, n.key);
      }
      lo -= 1;
      hi += 1;
   }
   const float pad = 3;
   const float cw = (w - 2 * pad) / std::max(1, pv.steps);
   const float rh = std::max(minRow, (h - 2 * pad) / std::max(1, hi - lo + 1));
   auto rowY = [&](int key) { return y + pad + (hi - key) * (h - 2 * pad - rh) / std::max(1, hi - lo); };
   if (beats) {
      const int perBeat = pv.perBeat;
      for (int st = perBeat; st < pv.steps; st += perBeat) {
         c.setColor(st % pv.perBar == 0 ? 0xff2a3039 : 0xff20242b);
         c.fill((x + pad + st * cw) * s, (y + 2) * s, (st % pv.perBar == 0 ? 1.0f : 0.6f) * s, (h - 4) * s);
      }
   }
   if (playing >= 0 && playing < pv.steps) {
      c.setColor(0x22ffffff);
      c.fill((x + pad + playing * cw) * s, (y + 1) * s, cw * s, (h - 2) * s);
   }
   // The top line, dotted from one top note to the next.
   const RollNote *last = nullptr;
   for (const RollNote &n : pv.notes) {
      if (n.kind != kTopNote && n.kind != kRunNote)
         continue;
      if (last) {
         const float x0 = x + pad + (last->start + std::min(last->len, n.start - last->start)) * cw;
         const float x1 = x + pad + n.start * cw;
         const float y0 = rowY(last->key) + rh / 2, y1 = rowY(n.key) + rh / 2;
         c.setColor(alpha(kOrange, 0.45f));
         const int dots = std::max(1, static_cast<int>((x1 - x0) / 3.5f));
         for (int d = 0; d < dots && x1 > x0 + 1; ++d) {
            const float f = (d + 0.5f) / dots;
            c.fill((x0 + (x1 - x0) * f) * s, (y0 + (y1 - y0) * f - 0.4f) * s, 1.2f * s, 0.9f * s);
         }
      }
      last = &n;
   }
   for (const RollNote &n : pv.notes) {
      const float nx = x + pad + n.start * cw, ny = rowY(n.key);
      const float nw = std::max(1.0f, std::min(n.len, static_cast<float>(pv.steps) - n.start) * cw - 0.8f);
      const bool sel = selected >= 0 && static_cast<int>(n.start) == selected && n.kind != kRunNote;
      c.setColor(alpha(kindColour(n.kind), 0.45f + 0.55f * std::min(1.0f, n.vel + 0.15f)));
      c.roundedRectangle(nx * s, ny * s, nw * s, std::max(1.2f, rh - 0.6f) * s, std::min(2.0f, rh / 2) * s);
      if (sel) {
         c.setColor(kWhite);
         c.roundedRectangleBorder((nx - 1) * s, (ny - 1) * s, (nw + 2) * s, (rh + 1.4f) * s, 2.5f * s, 1.2f * s);
      }
   }
}

// A keyboard from `lo` to `hi` (whole octaves look best), with keys lit by colour.
void drawKeys(visage::Canvas &c, float s, float x, float y, float w, float h, int lo, int hi,
              const std::map<int, unsigned> &lit, bool names = true, int rangeLo = -1, int rangeHi = -1) {
   auto black = [](int k) {
      const int pc = k % 12;
      return pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
   };
   int whites = 0;
   for (int k = lo; k <= hi; ++k)
      whites += !black(k);
   const float kw = w / std::max(1, whites);
   int wi = 0;
   std::map<int, float> at;
   for (int k = lo; k <= hi; ++k) {
      if (black(k))
         continue;
      const float kx = x + wi * kw;
      at[k] = kx;
      const auto it = lit.find(k);
      const bool out = rangeLo >= 0 && (k < rangeLo || k > rangeHi);
      c.setColor(it != lit.end() ? it->second : out ? 0xff8a9099u : 0xffdfe2e6u);
      c.roundedRectangle((kx + 0.5f) * s, y * s, (kw - 1) * s, h * s, 1.5f * s);
      if (names && k % 12 == 0)
         label(c, s, "C" + std::to_string(k / 12 - 1), kx + 1, y + h - 11, kw + 6, 0xff5d636cu, visage::Font::kLeft, 6.5f);
      ++wi;
   }
   wi = 0;
   for (int k = lo; k <= hi; ++k) {
      if (!black(k)) {
         ++wi;
         continue;
      }
      const float kx = x + wi * kw - kw * 0.3f;
      const auto it = lit.find(k);
      const bool out = rangeLo >= 0 && (k < rangeLo || k > rangeHi);
      c.setColor(it != lit.end() ? it->second : out ? 0xff3a3f47u : 0xff1c1f24u);
      c.roundedRectangle(kx * s, y * s, kw * 0.6f * s, h * 0.6f * s, 1.2f * s);
   }
   if (rangeLo >= 0) {
      const float x0 = at.count(rangeLo) ? at[rangeLo] : x, x1 = at.count(rangeHi) ? at[rangeHi] + kw : x + w;
      c.setColor(kOrange);
      c.roundedRectangleBorder((x0 - 1) * s, (y - 1.5f) * s, (x1 - x0 + 2) * s, (h + 3) * s, 2 * s, 1.4f * s);
   }
}

// The colours a voicing lights: top orange, bottom purple, the rest blue.
std::map<int, unsigned> voicingKeys(const Voicing &v) {
   std::map<int, unsigned> m;
   for (int i = 0; i < v.count; ++i)
      m[v.notes[static_cast<size_t>(i)]] = i == v.count - 1 ? kOrange : i == 0 ? kPurple : kBlue;
   return m;
}

// ---- small widgets ----

class CheckBox : public visage::Frame {
public:
   explicit CheckBox(std::string label) : label_(std::move(label)) {}
   void setText(const std::string &t) {
      if (t != label_) {
         label_ = t;
         redraw();
      }
   }
   void draw(visage::Canvas &c) override {
      const float s = ui::designScale(*this);
      const ui::Theme &t = ui::defaultTheme();
      const float box = 11, y = (height() / s - box) / 2;
      if (on) {
         c.setColor(kSlate);
         c.roundedRectangle(0, y * s, box * s, box * s, 2.5f * s);
         c.setColor(kOrange);
         c.roundedRectangle(3 * s, (y + 3) * s, (box - 6) * s, (box - 6) * s, 1.5f * s);
      }
      c.setColor(t.dimText);
      c.roundedRectangleBorder(0, y * s, box * s, box * s, 2.5f * s, 1.0f * s);
      c.setColor(on ? t.text : t.dimText);
      c.text(label_, ui::font(9.5f * s), visage::Font::kLeft, (box + 6) * s, 0, width() - (box + 6) * s, height());
   }
   void mouseDown(const visage::MouseEvent &) override {
      on = !on;
      redraw();
      if (onToggle)
         onToggle(on);
   }
   bool on = false;
   std::function<void(bool)> onToggle;

private:
   std::string label_;
};

// A button drawn by the panel's rules: plain, lit (slate), or the accent.
class FlatButton : public visage::Frame {
public:
   explicit FlatButton(std::string text, float size = 9.5f) : text_(std::move(text)), size_(size) {}
   void setText(const std::string &t) {
      if (t != text_) {
         text_ = t;
         redraw();
      }
   }
   void draw(visage::Canvas &c) override {
      const float s = ui::designScale(*this);
      const ui::Theme &t = ui::defaultTheme();
      const unsigned fill = accent ? (hover_ ? 0xfff59a3fu : kOrange) : lit ? kSlate : hover_ ? t.buttonHover : t.button;
      c.setColor(fill);
      c.roundedRectangle(0, 0, width(), height(), 5 * s);
      if (!accent) {
         c.setColor(outline ? outline : t.panelBorder);
         c.roundedRectangleBorder(0, 0, width(), height(), 5 * s, 1.0f);
      }
      float tx = 0;
      if (dice) {
         c.setColor(accent ? kInk : kOrange);
         const float d = std::min(height() * 0.5f, 14 * s);
         ui::drawDie(c, width() / 2 - textWidth() * s / 2 - d * 0.4f, height() / 2, d);
         tx = d * 0.55f / s;
      }
      c.setColor(accent ? kInk : enabled ? (textColour ? textColour : t.text) : kFaint);
      c.text(text_, ui::font(size_ * s), visage::Font::kCenter, tx * s, 0, width(), height());
   }
   void mouseEnter(const visage::MouseEvent &) override {
      hover_ = true;
      redraw();
   }
   void mouseExit(const visage::MouseEvent &) override {
      hover_ = false;
      redraw();
   }
   void mouseDown(const visage::MouseEvent &e) override {
      if (enabled && onClick)
         onClick(e);
   }
   bool accent = false, lit = false, dice = false, enabled = true;
   unsigned outline = 0, textColour = 0;
   std::function<void(const visage::MouseEvent &)> onClick;

private:
   float textWidth() const { return text_.size() * size_ * 0.62f; }
   std::string text_;
   float size_;
   bool hover_ = false;
};

// A flat slider for a param, with words at each end: ENERGY, WEIRD.
class ParamSlider : public visage::Frame {
public:
   ParamSlider(EditorHost &host, clap_id id, std::string name, std::string left, std::string right, unsigned colour)
      : host_(host), id_(id), name_(std::move(name)), left_(std::move(left)), right_(std::move(right)),
        colour_(colour) {}
   void draw(visage::Canvas &c) override {
      const float s = ui::designScale(*this);
      const float w = width() / s;
      const double v = host_.paramValue(id_);
      const float f = static_cast<float>((v - kParams[id_].min) / (kParams[id_].max - kParams[id_].min));
      if (!name_.empty()) {
         label(c, s, name_, 0, 0, 120, kWhite, visage::Font::kLeft, 10.5f);
         label(c, s, pct(f), w - 60, 0, 60, colour_, visage::Font::kRight, 10.5f);
      }
      const float ty = name_.empty() ? height() / s / 2 - 2 : 21;
      c.setColor(ui::defaultTheme().track);
      c.roundedRectangle(0, ty * s, w * s, 4 * s, 2 * s);
      c.setColor(colour_);
      c.roundedRectangle(0, ty * s, std::max(4.0f, f * w) * s, 4 * s, 2 * s);
      c.setColor(kWhite);
      c.circle((f * (w - 14)) * s, (ty - 5) * s, 14 * s);
      if (!left_.empty()) {
         label(c, s, left_, 0, ty + 10, 150, 0, visage::Font::kLeft, 8.5f);
         label(c, s, right_, w - 150, ty + 10, 150, 0, visage::Font::kRight, 8.5f);
      }
   }
   void mouseDown(const visage::MouseEvent &e) override {
      if (e.repeatClickCount() >= 2) {
         host_.beginEdit(id_);
         host_.setParam(id_, kParams[id_].defaultValue);
         host_.endEdit(id_);
         redraw();
         if (onDone)
            onDone();
         return;
      }
      host_.beginEdit(id_);
      dragging_ = true;
      set(e);
   }
   void mouseDrag(const visage::MouseEvent &e) override {
      if (dragging_)
         set(e);
   }
   void mouseUp(const visage::MouseEvent &) override {
      if (!dragging_)
         return;
      dragging_ = false;
      host_.endEdit(id_);
      if (onDone)
         onDone();
   }
   std::function<void()> onDone;

private:
   void set(const visage::MouseEvent &e) {
      const float s = ui::designScale(*this);
      const float f = std::clamp((e.position.x / s - 7) / (width() / s - 14), 0.0f, 1.0f);
      host_.setParam(id_, kParams[id_].min + f * (kParams[id_].max - kParams[id_].min));
      redraw();
   }
   EditorHost &host_;
   clap_id id_;
   std::string name_, left_, right_;
   unsigned colour_;
   bool dragging_ = false;
};

// Chips in a row (or rows): a choice of words, one lit. Used for CHORDS' roots and qualities and
// TRY INSTEAD.
class Chips : public visage::Frame {
public:
   void draw(visage::Canvas &c) override {
      const float s = ui::designScale(*this);
      const ui::Theme &t = ui::defaultTheme();
      const int n = static_cast<int>(items.size());
      const int cols = perRow > 0 ? perRow : std::max(1, n);
      const int rows = (n + cols - 1) / std::max(1, cols);
      const float cw = width() / s / cols, ch = height() / s / std::max(1, rows);
      for (int i = 0; i < n; ++i) {
         const float x = (i % cols) * cw, y = (i / cols) * ch;
         const bool on = i == selected;
         c.setColor(on ? kWarm : t.button);
         c.roundedRectangle((x + 2) * s, (y + 2) * s, (cw - 4) * s, (ch - 4) * s, 4 * s);
         c.setColor(on ? kOrange : colour ? alpha(colour, 0.6f) : t.panelBorder);
         c.roundedRectangleBorder((x + 2) * s, (y + 2) * s, (cw - 4) * s, (ch - 4) * s, 4 * s, on ? 1.4f * s : 1.0f);
         label(c, s, items[static_cast<size_t>(i)], x + 2, y + (ch - 14) / 2, cw - 4, on ? kOrange : colour ? colour : kTitle,
               visage::Font::kCenter, size);
      }
   }
   void mouseDown(const visage::MouseEvent &e) override {
      const float s = ui::designScale(*this);
      const int n = static_cast<int>(items.size());
      const int cols = perRow > 0 ? perRow : std::max(1, n);
      const int rows = (n + cols - 1) / std::max(1, cols);
      const int col = static_cast<int>(e.position.x / s / (width() / s / cols));
      const int row = static_cast<int>(e.position.y / s / (height() / s / std::max(1, rows)));
      const int i = row * cols + col;
      if (i >= 0 && i < n && onPick)
         onPick(i);
   }
   std::vector<std::string> items;
   int selected = -1, perRow = 0;
   unsigned colour = 0;
   float size = 9.5f;
   std::function<void(int)> onPick;
};

// ---- GENERATE ----

// The genres on the twelve tiles. The genre picked last stands in for the twelfth when it isn't
// one of them, so the tile lit is always the genre playing.
const char *const kTileGenres[12] = {"Funk", "Neo-Soul", "Gospel", "Swing Jazz", "Pop", "90s R&B",
                                     "House", "Disco", "Reggae", "Salsa", "Rock", "Piano Ballad"};
int genreIndex(const char *name) {
   for (int i = 0; i < genreCount(); ++i)
      if (std::strcmp(genreAt(i).name, name) == 0)
         return i;
   return 0;
}

// A genre's first style, voiced over its verse, for its tile.
const Preview &genrePreview(int g) {
   static std::map<int, Preview> cache;
   auto it = cache.find(g);
   if (it != cache.end())
      return it->second;
   Recipe r;
   r.genre = g;
   r.style = 0;
   r.player = stylePlayer(genreAt(g), styleAt(genreAt(g), 0));
   r.weird = 0.1;
   r.seed = 11;
   const Section sec = makeSection(r, kChorus, 11);
   VoiceSettings vs;
   const Player &p = kPlayerList[r.player];
   vs.shape = p.shape;
   vs.voices = p.voices;
   vs.lo = p.lo;
   vs.hi = p.hi;
   vs.center = p.center;
   Pattern pat = sec.takes[0];
   const LiveFeel feel = gFeel;
   gFeel = LiveFeel{}; // the tiles show each style as written
   cache[g] = preview(sec, pat, 2, vs, 0, r.key, kMajor, false);
   gFeel = feel;
   return cache[g];
}

class GenreTiles : public visage::Frame {
public:
   explicit GenreTiles(EditorHost &host) : host_(host) {}
   std::vector<int> genres() const {
      std::vector<int> g;
      for (const char *n : kTileGenres)
         g.push_back(genreIndex(n));
      const int now = genre();
      if (std::find(g.begin(), g.end(), now) == g.end())
         g.back() = now;
      return g;
   }
   void draw(visage::Canvas &c) override {
      const float s = ui::designScale(*this);
      const std::vector<int> g = genres();
      const int blend = static_cast<int>(std::lround(host_.paramValue(kBlendGenre))) - 1;
      for (int i = 0; i < 12; ++i) {
         const float x = (i % 6) * kTileW, y = (i / 6) * kTileH;
         const bool on = g[static_cast<size_t>(i)] == genre(), mixed = g[static_cast<size_t>(i)] == blend;
         c.setColor(on ? kWarm : kCell);
         c.roundedRectangle(x * s, y * s, (kTileW - 5) * s, (kTileH - 5) * s, 5 * s);
         c.setColor(on ? kOrange : mixed ? kPurple : ui::defaultTheme().panelBorder);
         c.roundedRectangleBorder(x * s, y * s, (kTileW - 5) * s, (kTileH - 5) * s, 5 * s, on || mixed ? 1.5f * s : 1.0f);
         const Genre &gen = genreAt(g[static_cast<size_t>(i)]);
         label(c, s, gen.name, x + 6, y + 4, kTileW - 12, on ? kOrange : kWhite, visage::Font::kLeft, 9.5f);
         if (mixed)
            label(c, s, pct(host_.paramValue(kBlend)) + "%", x + 6, y + 4, kTileW - 17, kPurple, visage::Font::kRight, 8);
         drawRoll(c, s, genrePreview(g[static_cast<size_t>(i)]), x + 5, y + 21, kTileW - 15, kTileH - 31, -1, false);
      }
   }
   void mouseDown(const visage::MouseEvent &e) override {
      const float s = ui::designScale(*this);
      const int col = static_cast<int>(e.position.x / s / kTileW), row = static_cast<int>(e.position.y / s / kTileH);
      if (col < 0 || col > 5 || row < 0 || row > 1)
         return;
      const int g = genres()[static_cast<size_t>(row * 6 + col)];
      if (e.isShiftDown()) {
         if (onBlend)
            onBlend(g);
      } else if (onPick)
         onPick(g);
   }
   std::function<void(int)> onPick, onBlend;
   static constexpr float kTileW = 84.5f, kTileH = 66.0f;

private:
   int genre() const { return static_cast<int>(std::lround(host_.paramValue(kGenre))); }
   EditorHost &host_;
};

// The player's icon: keys, an organ's drawbars, or a guitar's neck.
void drawPlayerIcon(visage::Canvas &c, float s, float cx, float cy, int kind, unsigned col) {
   c.setColor(kWarm);
   c.circle((cx - 10) * s, (cy - 10) * s, 20 * s);
   c.setColor(col);
   c.ring((cx - 10) * s, (cy - 10) * s, 20 * s, 1.2f * s);
   if (kind == kGuitarKind) {
      c.segment((cx - 5) * s, (cy + 5) * s, (cx + 5) * s, (cy - 5) * s, 1.6f * s, true);
      c.circle((cx - 7) * s, (cy + 1) * s, 6 * s);
   } else if (kind == kOrganKind) {
      for (int i = 0; i < 4; ++i)
         c.roundedRectangle((cx - 5 + i * 2.8f) * s, (cy - 5 + (i % 2) * 3) * s, 1.8f * s, (8 - (i % 2) * 3) * s, 0.8f * s);
   } else {
      c.roundedRectangleBorder((cx - 6) * s, (cy - 4.5f) * s, 12 * s, 9 * s, 1.5f * s, 1.2f * s);
      for (int i = 0; i < 3; ++i)
         c.fill((cx - 3.5f + i * 3.5f) * s, (cy - 4.5f) * s, 1.4f * s, 5 * s);
   }
}

// The player button.
class PlayerButton : public visage::Frame {
public:
   explicit PlayerButton(EditorHost &host) : host_(host) {}
   void draw(visage::Canvas &c) override {
      const float s = ui::designScale(*this);
      const ui::Theme &t = ui::defaultTheme();
      const Player &p = kPlayerList[std::clamp(static_cast<int>(std::lround(host_.paramValue(kPlayer))), 0, kPlayers - 1)];
      const float w = width() / s, h = height() / s;
      c.setColor(host_.playersOpen ? kSlate : t.button);
      c.roundedRectangle(0, 0, width(), height(), 5 * s);
      c.setColor(host_.playersOpen ? kOrange : t.panelBorder);
      c.roundedRectangleBorder(0, 0, width(), height(), 5 * s, 1.0f);
      drawPlayerIcon(c, s, 16, h / 2, p.kind, kOrange);
      label(c, s, p.name, 32, h / 2 - 8, w - 60, kWhite, visage::Font::kLeft, 11);
      label(c, s, "plays", 32, h / 2 - 7, w - 52, 0, visage::Font::kRight, 8.5f);
      c.setColor(t.dimText);
      c.triangle((w - 16) * s, (h / 2 - 2) * s, (w - 8) * s, (h / 2 - 2) * s, (w - 12) * s, (h / 2 + 3) * s);
   }
   void mouseDown(const visage::MouseEvent &) override {
      if (onOpen)
         onOpen();
   }
   std::function<void()> onOpen;

private:
   EditorHost &host_;
};

// The bars of a progression as chips: a bar's chord, or its two (as "G9 · A9").
std::vector<std::string> barNames(const Section &sec, int key, int transpose, int bars, int scale = kMajor) {
   std::vector<std::string> out;
   for (int b = 0; b < bars; ++b) {
      const Chord a = moved(sec.chordAt(b, false), transpose), z = moved(sec.chordAt(b, true), transpose);
      const Chord next = moved(sec.chordAt(b + 1, false), transpose);
      std::string n = chordName(a, key, scale, z != a ? &z : &next);
      if (z != a)
         n += " \xC2\xB7 " + chordName(z, key, scale, &next);
      out.push_back(n);
   }
   return out;
}

void drawBarChips(visage::Canvas &c, float s, const std::vector<std::string> &names, float x, float y, float w, float h,
                  unsigned colour = kTitle, int lit = -1) {
   const int n = std::max<int>(1, static_cast<int>(names.size()));
   const float cw = w / n;
   for (int i = 0; i < static_cast<int>(names.size()); ++i) {
      const bool on = i == lit;
      c.setColor(on ? kWarm : kWell);
      c.roundedRectangle((x + i * cw + 1) * s, y * s, (cw - 2) * s, h * s, 3 * s);
      c.setColor(on ? kOrange : ui::defaultTheme().panelBorder);
      c.roundedRectangleBorder((x + i * cw + 1) * s, y * s, (cw - 2) * s, h * s, 3 * s, 1.0f);
      label(c, s, names[static_cast<size_t>(i)], x + i * cw, y + (h - 13) / 2, cw, on ? kOrange : colour,
            visage::Font::kCenter, cw < 34 ? 7.0f : 8.0f);
   }
}

// THE SONG: the seven sections side by side, each as wide as its bars, with its chords, its take
// voiced, its energy (drag it), its dice, lock and MUTATE.
class SongStrip : public visage::Frame {
public:
   explicit SongStrip(EditorHost &host) : host_(host) {}

   struct Box {
      float x, w;
   };
   std::vector<Box> boxes() const {
      const float s = ui::designScale(*this);
      const float total = width() / s, gap = 6;
      int bars = 0;
      for (const Section &sec : host_.song().sections)
         bars += std::max<int>(1, sec.bars);
      const float minW = 104;
      const float spare = total - gap * (kSections - 1) - minW * kSections;
      std::vector<Box> b;
      float x = 0;
      for (const Section &sec : host_.song().sections) {
         const float w = minW + spare * std::max<int>(1, sec.bars) / static_cast<float>(bars);
         b.push_back({x, w});
         x += w + gap;
      }
      return b;
   }

   void draw(visage::Canvas &c) override {
      readFeel(host_);
      const float s = ui::designScale(*this);
      const float h = height() / s;
      const PlayFrame &f = frame ? *frame : PlayFrame{};
      const std::vector<Box> b = boxes();
      const int key = static_cast<int>(std::lround(host_.paramValue(kKey)));
      for (int i = 0; i < kSections; ++i) {
         const Section &sec = host_.song().sections[static_cast<size_t>(i)];
         const float x = b[static_cast<size_t>(i)].x, w = b[static_cast<size_t>(i)].w;
         const bool sel = host_.mutateSection == i;
         c.setColor(sel ? kWarm : kCell);
         c.roundedRectangle(x * s, 0, w * s, h * s, 5 * s);
         c.setColor(sel ? kOrange : ui::defaultTheme().panelBorder);
         c.roundedRectangleBorder(x * s, 0, w * s, h * s, 5 * s, sel ? 1.5f * s : 1.0f);
         label(c, s, kSectionCaps[i], x + 8, 5, w - 16, sel ? kOrange : kWhite, visage::Font::kLeft, 10.5f);
         label(c, s, std::to_string(sec.bars) + " bars", x + 8, 6, w - 16, 0, visage::Font::kRight, 8.5f);
         drawBarChips(c, s, barNames(sec, key, transpose(), std::min<int>(4, sec.progBars), scale()), x + 6, 23, w - 12, 14);
         const bool playing = f.running && f.section == i;
         const Pattern &p = sec.takes[0];
         const int bars = viewBars(sec, p);
         const Preview pv = preview(sec, p, bars, vs(), transpose(), key, scale(), host_.paramValue(kRuns) > 0.0);
         drawRoll(c, s, pv, x + 6, 42, w - 12, 60, playing ? f.step : -1);
         const double e = host_.paramValue(static_cast<clap_id>(kSectionEnergy + i));
         c.setColor(playing ? kGreen : kOrange);
         c.circle((x + 8) * s, 109 * s, 7 * s);
         label(c, s, "energy " + pct(e), x + 19, 106.5f, 80, dragging_ == i ? kWhite : 0, visage::Font::kLeft, 9);
         if (sec.generation > 0)
            label(c, s, "gen " + std::to_string(sec.generation + 1), x + 8, 107, w - 16, kPurple, visage::Font::kRight, 8);
         icon(c, s, x + 6, h - 22, 0, false);
         icon(c, s, x + 34, h - 22, 1, sec.locked != 0);
         if (w > 130) {
            c.setColor(sel ? kSlate : ui::defaultTheme().button);
            c.roundedRectangle((x + 62) * s, (h - 22) * s, 62 * s, 17 * s, 4 * s);
            label(c, s, "+ MUTATE", x + 62, h - 21, 62, sel ? kWhite : 0, visage::Font::kCenter, 8);
         }
      }
   }

   void mouseDown(const visage::MouseEvent &e) override {
      const float s = ui::designScale(*this);
      const float x = e.position.x / s, y = e.position.y / s, h = height() / s;
      const std::vector<Box> b = boxes();
      for (int i = 0; i < kSections; ++i) {
         const float bx = b[static_cast<size_t>(i)].x, w = b[static_cast<size_t>(i)].w;
         if (x < bx || x >= bx + w)
            continue;
         const float lx = x - bx;
         if (y >= h - 24) {
            if (lx < 30) {
               if (onDice)
                  onDice(i);
               return;
            }
            if (lx < 58) {
               if (onLock)
                  onLock(i);
               return;
            }
         }
         if (y >= 102 && y < 118 && lx < 80) {
            dragging_ = i;
            dragY_ = y;
            dragFrom_ = host_.paramValue(static_cast<clap_id>(kSectionEnergy + i));
            host_.beginEdit(static_cast<clap_id>(kSectionEnergy + i));
            return;
         }
         if (onSelect)
            onSelect(i);
         return;
      }
   }
   void mouseDrag(const visage::MouseEvent &e) override {
      if (dragging_ < 0)
         return;
      const float s = ui::designScale(*this);
      const double v = std::clamp(dragFrom_ + (dragY_ - e.position.y / s) / 100.0, 0.0, 1.0);
      host_.setParam(static_cast<clap_id>(kSectionEnergy + dragging_), v);
      redraw();
   }
   void mouseUp(const visage::MouseEvent &) override {
      if (dragging_ < 0)
         return;
      const int i = dragging_;
      dragging_ = -1;
      host_.endEdit(static_cast<clap_id>(kSectionEnergy + i));
      if (onEnergy)
         onEnergy(i);
   }

   const PlayFrame *frame = nullptr;
   std::function<void(int)> onSelect, onDice, onLock, onEnergy;
   std::function<VoiceSettings()> vs;
   std::function<int()> transpose, scale;

private:
   static void icon(visage::Canvas &c, float s, float x, float y, int which, bool lit) {
      const ui::Theme &t = ui::defaultTheme();
      c.setColor(lit ? kWarm : t.button);
      c.roundedRectangle(x * s, y * s, 24 * s, 17 * s, 4 * s);
      c.setColor(lit ? kOrange : t.panelBorder);
      c.roundedRectangleBorder(x * s, y * s, 24 * s, 17 * s, 4 * s, 1.0f);
      const float cx = (x + 12) * s, cy = (y + 8.5f) * s;
      if (which == 0) {
         c.setColor(kWhite);
         ui::drawDie(c, cx, cy, 10 * s);
      } else {
         c.setColor(lit ? kOrange : t.dimText);
         c.roundedRectangle(cx - 3.5f * s, cy - 0.5f * s, 7 * s, 5.5f * s, 1.2f * s);
         c.ring(cx - 2.6f * s, cy - 5.2f * s, 5.2f * s, 1.1f * s);
      }
   }
   EditorHost &host_;
   int dragging_ = -1;
   float dragY_ = 0;
   double dragFrom_ = 0;
};

// MUTATE: the section's take now, and five children, each tagged with how it differs. Click a child
// to put it in the song; "back" takes the one before.
class MutateRow : public visage::Frame {
public:
   explicit MutateRow(EditorHost &host) : host_(host) {}
   void draw(visage::Canvas &c) override {
      readFeel(host_);
      const float s = ui::designScale(*this);
      const float h = height() / s;
      const Section &sec = host_.song().sections[static_cast<size_t>(host_.mutateSection)];
      const int key = static_cast<int>(std::lround(host_.paramValue(kKey)));
      auto pv = [&](const Pattern &p) { return preview(sec, p, viewBars(sec, p), vs(), transpose(), key, scale(), host_.paramValue(kRuns) > 0.0); };
      c.setColor(kWarm);
      c.roundedRectangle(0, 0, kNowW * s, h * s, 5 * s);
      c.setColor(kOrange);
      c.roundedRectangleBorder(0, 0, kNowW * s, h * s, 5 * s, 1.5f * s);
      label(c, s, "NOW \xC2\xB7 in the song", 9, 6, 200, kOrange, visage::Font::kLeft, 9.5f);
      drawRoll(c, s, pv(sec.takes[0]), 9, 23, kNowW - 18, h - 46);
      if (!host_.parents.empty())
         label(c, s, "< back to gen " + std::to_string(sec.generation), 9, h - 18, 110, 0, visage::Font::kLeft, 9);
      label(c, s, "click a child to put it in the song", 9, h - 18, kNowW - 18, kFaint, visage::Font::kRight, 8);
      c.setColor(kFaint);
      c.triangle((kNowW + 7) * s, (h / 2 - 6) * s, (kNowW + 7) * s, (h / 2 + 6) * s, (kNowW + 15) * s, (h / 2) * s);
      for (int i = 0; i < 5 && i < static_cast<int>(host_.litter.size()); ++i) {
         const float x = kNowW + 22 + i * kChildW;
         const bool hover = hover_ == i;
         c.setColor(kCell);
         c.roundedRectangle(x * s, 0, (kChildW - 6) * s, h * s, 5 * s);
         c.setColor(hover ? kWhite : ui::defaultTheme().panelBorder);
         c.roundedRectangleBorder(x * s, 0, (kChildW - 6) * s, h * s, 5 * s, hover ? 1.5f * s : 1.0f);
         std::string tag = host_.tags[static_cast<size_t>(i)];
         if (tag.size() > 26)
            tag = tag.substr(0, 25) + "\xE2\x80\xA6";
         label(c, s, tag, x + 8, 6, kChildW - 20, kWhite, visage::Font::kLeft, 9.5f);
         drawRoll(c, s, pv(host_.litter[static_cast<size_t>(i)]), x + 8, 23, kChildW - 22, h - 46);
         label(c, s, hover ? "click to use" : "", x + 8, h - 18, kChildW - 22, kOrange, visage::Font::kLeft, 8.5f);
      }
   }
   void mouseMove(const visage::MouseEvent &e) override {
      const int i = childAt(e);
      if (i != hover_) {
         hover_ = i;
         redraw();
      }
   }
   void mouseExit(const visage::MouseEvent &) override {
      hover_ = -1;
      redraw();
   }
   void mouseDown(const visage::MouseEvent &e) override {
      const float s = ui::designScale(*this);
      const int i = childAt(e);
      if (i >= 0) {
         if (onPick)
            onPick(i);
      } else if (e.position.x / s < 120 && e.position.y / s > height() / s - 22 && onBack)
         onBack();
   }
   std::function<void(int)> onPick;
   std::function<void()> onBack;
   std::function<VoiceSettings()> vs;
   std::function<int()> transpose, scale;
   static constexpr float kNowW = 228, kChildW = 140;

private:
   int childAt(const visage::MouseEvent &e) const {
      const float s = ui::designScale(*this);
      const float x = e.position.x / s - kNowW - 22;
      if (x < 0)
         return -1;
      const int i = static_cast<int>(x / kChildW);
      return i < static_cast<int>(host_.litter.size()) && i < 5 && std::fmod(x, kChildW) < kChildW - 6 ? i : -1;
   }
   EditorHost &host_;
   int hover_ = -1;
};

// ---- CHORDS ----

// PROGRESSION: a row per section, a box per bar (two chords in a split bar), its Roman numerals.
// Click a chord to edit it on the right; "..." for the row's length and copying.
class ProgressionRows : public visage::Frame {
public:
   explicit ProgressionRows(EditorHost &host) : host_(host) {}
   void draw(visage::Canvas &c) override {
      const float s = ui::designScale(*this);
      const ui::Theme &t = ui::defaultTheme();
      const int key = static_cast<int>(std::lround(host_.paramValue(kKey)));
      const int tr = transpose();
      const PlayFrame &f = frame ? *frame : PlayFrame{};
      for (int i = 0; i < kSections; ++i) {
         const Section &sec = host_.song().sections[static_cast<size_t>(i)];
         const float y = i * kRowH, h = kRowH - 6, w = width() / s;
         const bool sel = host_.chordSection == i;
         c.setColor(sel ? kWarm : kCell);
         c.roundedRectangle(0, y * s, w * s, h * s, 5 * s);
         c.setColor(sel ? kOrange : t.panelBorder);
         c.roundedRectangleBorder(0, y * s, w * s, h * s, 5 * s, sel ? 1.4f * s : 1.0f);
         label(c, s, kSectionCaps[i], 10, y + 7, 90, sel ? kOrange : kWhite, visage::Font::kLeft, 10.5f);
         std::string bars = std::to_string(sec.bars) + " bars";
         if (sec.bars > sec.progBars)
            bars += " \xC2\xB7 loop \xC3\x97" + std::to_string(sec.bars / std::max<int>(1, sec.progBars));
         label(c, s, bars, 10, y + 24, 90, 0, visage::Font::kLeft, 8.5f);
         const int n = sec.progBars;
         const float bx = kNameW, bw = (w - kNameW - kMoreW - 6) / n;
         const bool playing = f.running && f.section == i;
         const int playBar = playing ? f.bar % std::max<int>(1, n) : -1;
         for (int b = 0; b < n; ++b) {
            const float x = bx + b * bw;
            const bool split = sec.split >> b & 1;
            for (int half = 0; half < (split ? 2 : 1); ++half) {
               const int slot = b * 2 + half;
               const float hx = x + (split ? half * bw / 2 : 0), hw = split ? bw / 2 : bw;
               const bool on = sel && (host_.chordSlot == slot || (!split && host_.chordSlot == b * 2 + 1));
               c.setColor(on ? kSlate : playBar == b ? 0xff2b3038u : kWell);
               c.roundedRectangle((hx + 2) * s, (y + 4) * s, (hw - 4) * s, (h - 8) * s, 3 * s);
               c.setColor(on ? kOrange : t.panelBorder);
               c.roundedRectangleBorder((hx + 2) * s, (y + 4) * s, (hw - 4) * s, (h - 8) * s, 3 * s, on ? 1.4f * s : 1.0f);
               const Chord ch = moved(sec.chords[static_cast<size_t>(slot)], tr);
               const Chord after = moved(sec.chordAt(half == 0 && split ? b : b + 1, half == 0 && split), tr);
               const float size = hw < 44 ? 8.5f : hw < 70 ? 10.0f : 12.0f;
               label(c, s, chordName(ch, key, scale(), &after), hx, y + 8, hw, kWhite, visage::Font::kCenter, size);
               label(c, s, roman(ch, key), hx, y + h - 19, hw, 0, visage::Font::kCenter, 7.5f);
            }
         }
         // "..."
         c.setColor(t.button);
         c.roundedRectangle((w - kMoreW) * s, (y + 4) * s, (kMoreW - 4) * s, (h - 8) * s, 3 * s);
         label(c, s, "\xE2\x80\xA6", w - kMoreW, y + h / 2 - 9, kMoreW - 4, kTitle, visage::Font::kCenter, 12);
      }
   }
   void mouseDown(const visage::MouseEvent &e) override {
      const float s = ui::designScale(*this);
      const float x = e.position.x / s, y = e.position.y / s, w = width() / s;
      const int i = static_cast<int>(y / kRowH);
      if (i < 0 || i >= kSections)
         return;
      const Section &sec = host_.song().sections[static_cast<size_t>(i)];
      if (x >= w - kMoreW) {
         if (onMore)
            onMore(i, e.position);
         return;
      }
      int slot = 0;
      if (x >= kNameW) {
         const int n = sec.progBars;
         const float bw = (w - kNameW - kMoreW - 6) / n;
         const int b = std::clamp(static_cast<int>((x - kNameW) / bw), 0, n - 1);
         const bool split = sec.split >> b & 1;
         slot = b * 2 + (split && std::fmod(x - kNameW, bw) >= bw / 2 ? 1 : 0);
      }
      if (onPick)
         onPick(i, slot);
   }
   const PlayFrame *frame = nullptr;
   std::function<void(int section, int slot)> onPick;
   std::function<void(int section, visage::Point at)> onMore;
   std::function<int()> transpose, scale;
   static constexpr float kRowH = 52, kNameW = 104, kMoreW = 28;

private:
   EditorHost &host_;
};

// The chord picked, voiced as JComp will voice it (led from the chord before it).
class ChordVoicing : public visage::Frame {
public:
   void draw(visage::Canvas &c) override {
      const float s = ui::designScale(*this);
      std::map<int, unsigned> lit = voicingKeys(v);
      drawKeys(c, s, 0, 0, width() / s, height() / s - 18, 48, 84, lit);
      std::string line = notesText(v);
      if (!line.empty())
         line += "  \xC2\xB7  " + note;
      label(c, s, line, 0, height() / s - 14, width() / s, 0, visage::Font::kLeft, 8.5f);
   }
   Voicing v;
   std::string note;
};

// ---- EDIT ----

// The take on a piano roll, voiced as it will play over its section's chords. A chord lane on top,
// a keyboard down the side. Click an empty step to add a hit, click a hit to pick it, drag it
// sideways to move it, double-click to take it away.
class TakeRoll : public visage::Frame {
public:
   explicit TakeRoll(EditorHost &host) : host_(host) {}
   void draw(visage::Canvas &c) override {
      gFeel = LiveFeel{}; // EDIT shows the take as written
      const float s = ui::designScale(*this);
      const float w = width() / s, h = height() / s;
      const Song &song = host_.song();
      const Section &sec = song.sections[static_cast<size_t>(host_.editSection)];
      const Pattern &p = pattern();
      const int key = static_cast<int>(std::lround(host_.paramValue(kKey)));
      const int bars = viewBars(sec, p);
      const Preview pv = preview(sec, p, bars, vs(), transpose(), key, scale(), host_.paramValue(kRuns) > 0.0);
      // Chords on top.
      drawBarChips(c, s, barNames(sec, key, transpose(), bars, scale()), kKeysW, 0, w - kKeysW, 16, kWhite,
                   host_.editStep >= 0 ? host_.editStep / p.stepsPerBar() % bars : -1);
      // The range shown: the take's notes, or the voicing window.
      int lo = 127, hi = 0;
      for (const RollNote &n : pv.notes) {
         lo = std::min(lo, n.key);
         hi = std::max(hi, n.key);
      }
      if (pv.notes.empty()) {
         lo = static_cast<int>(std::lround(host_.paramValue(kRangeLow)));
         hi = static_cast<int>(std::lround(host_.paramValue(kRangeHigh)));
      }
      lo = std::min(lo - 2, hi - 22);
      hi = std::max(hi + 2, lo + 22);
      lo_ = lo;
      hi_ = hi;
      const float gy = kTop, gh = h - kTop - 18;
      const float rh = gh / (hi - lo + 1);
      // The keys down the side.
      for (int k = lo; k <= hi; ++k) {
         const float y = gy + (hi - k) * rh;
         const int pc = k % 12;
         const bool black = pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
         c.setColor(black ? 0xff1c1f24u : 0xffdfe2e6u);
         c.fill(0, (y + 0.3f) * s, (black ? kKeysW * 0.6f : kKeysW - 4) * s, (rh - 0.6f) * s);
         c.setColor(black ? 0xff1d2127u : 0xff22262du);
         c.fill(kKeysW * s, y * s, (w - kKeysW) * s, rh * s);
         if (pc == 0)
            label(c, s, "C" + std::to_string(k / 12 - 1), 2, y + rh / 2 - 6, 30, 0xff5d636cu, visage::Font::kLeft, 7);
      }
      Preview shown = pv;
      const int shownTake = frame ? (frame->ending ? 6 : frame->fillDown ? 5 : frame->fill ? 4 : frame->take) : -1;
      const int playing =
          frame && frame->running && frame->section == host_.editSection && shownTake == host_.editTake ? frame->step : -1;
      drawRoll(c, s, shown, kKeysW, gy, w - kKeysW, gh, playing, true, host_.editStep, lo, hi, rh);
      // Legend.
      const char *names[4] = {"top voice", "inner voices", "bottom", "passing run"};
      for (int k = 0; k < 4; ++k) {
         c.setColor(kindColour(k));
         c.fill((kKeysW + k * 110) * s, (h - 10) * s, 10 * s, 4 * s);
         label(c, s, names[k], kKeysW + 14 + k * 110, h - 15, 100, 0, visage::Font::kLeft, 8.5f);
      }
      label(c, s, "- - top-note line: the most it moves is " + std::to_string(static_cast<int>(std::lround(host_.paramValue(kTopMove)))),
            kKeysW, h - 15, w - kKeysW, 0, visage::Font::kRight, 8.5f);
   }
   void mouseDown(const visage::MouseEvent &e) override {
      const int st = stepAt(e);
      if (st < 0)
         return;
      Song song = host_.song();
      Pattern &p = pick(song);
      const int idx = st % std::max<int>(1, p.steps);
      if (e.repeatClickCount() >= 2 && p.at(idx).on()) {
         p.at(idx) = Hit{};
         host_.editStep = -1;
         commit(song);
         return;
      }
      if (p.at(idx).on()) {
         host_.editStep = idx;
         drag_ = idx;
         redraw();
         if (onPick)
            onPick();
         return;
      }
      Hit h;
      h.vel = kNormal;
      p.at(idx) = h;
      host_.editStep = idx;
      drag_ = idx;
      commit(song);
   }
   void mouseDrag(const visage::MouseEvent &e) override {
      if (drag_ < 0)
         return;
      const int st = stepAt(e);
      Song song = host_.song();
      Pattern &p = pick(song);
      if (st < 0 || st % p.steps == drag_ || p.at(st % p.steps).on())
         return;
      const int to = st % p.steps;
      p.at(to) = p.at(drag_);
      p.at(drag_) = Hit{};
      drag_ = to;
      host_.editStep = to;
      commit(song);
   }
   void mouseUp(const visage::MouseEvent &) override { drag_ = -1; }

   Pattern &pick(Song &song) const { return pick(song, host_.editSection, host_.editTake); }
   static Pattern &pick(Song &song, int section, int take) {
      Section &sec = song.sections[static_cast<size_t>(section)];
      Pattern &p = patternOf(sec, take);
      // An empty fill down or ending written on the song's grid: a bar (two for an ending).
      if (p.empty() && p.rate != sec.takes[0].rate) {
         p.rate = sec.takes[0].rate;
         p.steps = static_cast<uint8_t>(p.stepsPerBar() * (take == 6 ? 2 : 1));
      }
      return p;
   }
   const Pattern &pattern() const { return pick(const_cast<Song &>(host_.song())); }

   const PlayFrame *frame = nullptr;
   std::function<void()> onChange, onPick;
   std::function<VoiceSettings()> vs;
   std::function<int()> transpose, scale;
   static constexpr float kKeysW = 40, kTop = 22;

private:
   int stepAt(const visage::MouseEvent &e) const {
      const float s = ui::designScale(*this);
      const float x = e.position.x / s - kKeysW - 3, y = e.position.y / s;
      if (x < 0 || y < kTop)
         return -1;
      const Section &sec = host_.song().sections[static_cast<size_t>(host_.editSection)];
      const Pattern &p = pattern();
      const int steps = viewBars(sec, p) * p.stepsPerBar();
      const float cw = (width() / s - kKeysW - 6) / steps;
      const int st = static_cast<int>(x / cw);
      return st >= 0 && st < steps ? st : -1;
   }
   void commit(Song &song) {
      host_.setSong(song);
      redraw();
      if (onChange)
         onChange();
   }
   EditorHost &host_;
   int drag_ = -1;
   int lo_ = 48, hi_ = 76;
};

// VELOCITY, LENGTH, CHANCE and NUDGE for each hit of the take: drag a bar up or down, double-click
// for its default.
class HitLanes : public visage::Frame {
public:
   explicit HitLanes(EditorHost &host) : host_(host) {}
   void draw(visage::Canvas &c) override {
      const float s = ui::designScale(*this);
      const float w = width() / s;
      const Song &song = host_.song();
      const Section &sec = song.sections[static_cast<size_t>(host_.editSection)];
      const Pattern &p = TakeRoll::pick(const_cast<Song &>(song), host_.editSection, host_.editTake);
      const int steps = viewBars(sec, p) * p.stepsPerBar();
      const float gx = kLabelW, cw = (w - gx - 6) / steps;
      const char *names[4] = {"VELOCITY", "LENGTH", "CHANCE", "NUDGE"};
      const unsigned cols[4] = {kOrange, kBlue, kPurple, kGreen};
      for (int r = 0; r < 4; ++r) {
         const float y = r * kRowH, h = kRowH - 6;
         label(c, s, names[r], 0, y + h / 2 - 7, gx, 0, visage::Font::kLeft, 8.5f);
         c.setColor(kWell);
         c.roundedRectangle(gx * s, y * s, (w - gx) * s, h * s, 3 * s);
         for (int v = 0; v < steps; ++v) {
            const Hit &hit = p.at(v % p.steps);
            if (!hit.on())
               continue;
            float f = 0;
            switch (r) {
            case 0: f = hit.vel / 127.0f; break;
            case 1: f = hit.art == kArtChop ? 0.15f : hit.art == kArtRing || hit.art == kArtTopRings ? 1.0f
                                                                                                   : std::min(1.0f, hit.len / 8.0f + 0.12f);
               break;
            case 2: f = hit.chance / 100.0f; break;
            default: f = 0.5f + hit.nudge / (2.0f * kMaxNudge); break;
            }
            const bool sel = v % p.steps == host_.editStep;
            const float x = gx + 3 + v * cw;
            c.setColor(sel ? kWhite : alpha(cols[r], v >= p.steps ? 0.55f : 1.0f));
            if (r == 3) {
               const float mid = y + h / 2;
               const float top = std::min(mid, y + 3 + (1 - f) * (h - 6)), bot = std::max(mid, y + 3 + (1 - f) * (h - 6));
               c.fill((x + 0.5f) * s, top * s, std::max(1.5f, cw - 1.5f) * s, std::max(1.5f, bot - top) * s);
            } else
               c.fill((x + 0.5f) * s, (y + 3 + (1 - f) * (h - 6)) * s, std::max(1.5f, cw - 1.5f) * s,
                      std::max(1.5f, f * (h - 6)) * s);
         }
      }
   }
   void mouseDown(const visage::MouseEvent &e) override {
      const float s = ui::designScale(*this);
      const int r = static_cast<int>(e.position.y / s / kRowH);
      const int st = stepAt(e);
      if (r < 0 || r > 3 || st < 0)
         return;
      Song song = host_.song();
      Pattern &p = TakeRoll::pick(song, host_.editSection, host_.editTake);
      Hit &h = p.at(st);
      if (!h.on())
         return;
      host_.editStep = st;
      if (e.repeatClickCount() >= 2) {
         if (r == 0)
            h.vel = kNormal;
         else if (r == 1) {
            h.len = 1;
            h.art = kArtAsWritten;
         } else if (r == 2)
            h.chance = 100;
         else
            h.nudge = 0;
         host_.setSong(song);
         changed();
         return;
      }
      drag_ = {true, r, st};
      set(e);
   }
   void mouseDrag(const visage::MouseEvent &e) override {
      if (drag_.on)
         set(e);
   }
   void mouseUp(const visage::MouseEvent &) override { drag_ = {}; }
   std::function<void()> onChange;
   static constexpr float kLabelW = 66, kRowH = 34;

private:
   int stepAt(const visage::MouseEvent &e) const {
      const float s = ui::designScale(*this);
      const Song &song = host_.song();
      const Section &sec = song.sections[static_cast<size_t>(host_.editSection)];
      const Pattern &p = TakeRoll::pick(const_cast<Song &>(song), host_.editSection, host_.editTake);
      const int steps = viewBars(sec, p) * p.stepsPerBar();
      const float cw = (width() / s - kLabelW - 6) / steps;
      const int v = static_cast<int>((e.position.x / s - kLabelW - 3) / cw);
      return v >= 0 && v < steps ? v % p.steps : -1;
   }
   void set(const visage::MouseEvent &e) {
      const float s = ui::designScale(*this);
      const float y = e.position.y / s - drag_.row * kRowH, h = kRowH - 6;
      const float f = std::clamp(1.0f - (y - 3) / (h - 6), 0.0f, 1.0f);
      Song song = host_.song();
      Hit &hit = TakeRoll::pick(song, host_.editSection, host_.editTake).at(drag_.step);
      if (!hit.on())
         return;
      switch (drag_.row) {
      case 0: hit.vel = static_cast<uint8_t>(std::clamp(static_cast<int>(f * 127), 1, 127)); break;
      case 1:
         if (f < 0.12f)
            hit.art = kArtChop;
         else if (f > 0.97f)
            hit.art = kArtRing;
         else {
            hit.art = kArtAsWritten;
            hit.len = static_cast<uint8_t>(std::clamp(static_cast<int>(std::lround((f - 0.12f) * 8)), 1, 16));
         }
         break;
      case 2: hit.chance = static_cast<uint8_t>(std::lround(f * 100)); break;
      default: hit.nudge = static_cast<int8_t>(std::lround((f - 0.5f) * 2 * kMaxNudge)); break;
      }
      host_.setSong(song);
      changed();
   }
   void changed() {
      redraw();
      if (onChange)
         onChange();
   }
   struct Drag {
      bool on = false;
      int row = 0, step = 0;
   };
   EditorHost &host_;
   Drag drag_;
};

// The voicing window on a keyboard: click below the middle of it for the low end, above for the
// high end.
class RangeKeys : public visage::Frame {
public:
   explicit RangeKeys(EditorHost &host) : host_(host) {}
   void draw(visage::Canvas &c) override {
      const float s = ui::designScale(*this);
      const int off = static_cast<int>(std::lround(host_.paramValue(kOffset)));
      const int lo = static_cast<int>(std::lround(host_.paramValue(kRangeLow))) + off;
      const int hi = static_cast<int>(std::lround(host_.paramValue(kRangeHigh))) + off;
      const int floor = std::min(lo, static_cast<int>(std::lround(host_.paramValue(kFloor))));
      std::map<int, unsigned> lit;
      lit[floor] = kGreen;
      drawKeys(c, s, 0, 0, width() / s, height() / s, kLo, kHi, lit, false, lo, hi);
   }
   void mouseDown(const visage::MouseEvent &e) override {
      const float s = ui::designScale(*this);
      const float f = e.position.x / s / (width() / s);
      // The window as it sounds is LOWEST and HIGHEST moved by OFFSET; a click sets them before it.
      const int off = static_cast<int>(std::lround(host_.paramValue(kOffset)));
      const int key = kLo + static_cast<int>(f * (kHi - kLo + 1)) - off;
      const int lo = static_cast<int>(std::lround(host_.paramValue(kRangeLow)));
      const int hi = static_cast<int>(std::lround(host_.paramValue(kRangeHigh)));
      const clap_id id = std::abs(key - lo) < std::abs(key - hi) ? kRangeLow : kRangeHigh;
      if (onSet)
         onSet(id, std::clamp<double>(key, kParams[id].min, kParams[id].max));
   }
   std::function<void(clap_id, double)> onSet;
   static constexpr int kLo = 36, kHi = 96;

private:
   EditorHost &host_;
};

// ---- PLAY ----

// SECTIONS: a tile each, with its note, chords, take, energy, and its takes and fill. Click a tile to
// switch to it (at SWITCH AT), a number for that take, FILL for a fill.
class SectionTiles : public visage::Frame {
public:
   explicit SectionTiles(EditorHost &host) : host_(host) {}
   void draw(visage::Canvas &c) override {
      readFeel(host_);
      const float s = ui::designScale(*this);
      const float h = height() / s, w = tileW();
      const PlayFrame &f = frame ? *frame : PlayFrame{};
      const int key = static_cast<int>(std::lround(host_.paramValue(kKey)));
      Settings st;
      st.noteMap = static_cast<int>(std::lround(host_.paramValue(kNoteMap)));
      st.noteOctave = static_cast<int>(std::lround(host_.paramValue(kNoteOctave)));
      for (int a = 0; a < kActionCount; ++a)
         st.learn[static_cast<size_t>(a)] = static_cast<int>(std::lround(host_.paramValue(static_cast<clap_id>(kLearnNote + a))));
      for (int i = 0; i < kSections; ++i) {
         const float x = i * (w + 6);
         const Section &sec = host_.song().sections[static_cast<size_t>(i)];
         const bool playing = f.section == i, queued = f.queued == i;
         c.setColor(playing ? kWarm : kCell);
         c.roundedRectangle(x * s, 6 * s, w * s, (h - 6) * s, 6 * s);
         if (queued) {
            c.setColor(kOrange);
            dashed(c, x * s, 6 * s, w * s, (h - 6) * s, 3 * s, 1.0f * s);
            c.roundedRectangle((x + w / 2 - 15) * s, 0, 30 * s, 12 * s, 2 * s);
            label(c, s, "NEXT", x + w / 2 - 15, 0.5f, 30, kInk, visage::Font::kCenter, 7);
         } else {
            c.setColor(playing ? kOrange : ui::defaultTheme().panelBorder);
            c.roundedRectangleBorder(x * s, 6 * s, w * s, (h - 6) * s, 6 * s, playing ? 1.5f * s : 1.0f);
         }
         label(c, s, kSectionCaps[i], x + 10, 13, w - 20, playing ? kOrange : kWhite, visage::Font::kLeft, 11);
         label(c, s, noteText(st.noteFor(i)), x + 10, 14.5f, w - 20, 0, visage::Font::kRight, 8);
         drawBarChips(c, s, barNames(sec, key, transpose(), std::min<int>(4, sec.progBars), scale()), x + 7, 32, w - 14, 13, kTitle);
         const int show = playing ? (f.ending ? 6 : f.fillDown ? 5 : f.fill ? 4 : f.take) : 0;
         const Pattern &p = patternOf(sec, show);
         const Preview pv = preview(sec, p, viewBars(sec, p), vs(), transpose(), key, scale(), host_.paramValue(kRuns) > 0.0);
         drawRoll(c, s, pv, x + 7, 50, w - 14, 52, playing && f.running ? f.step : -1);
         c.setColor(ui::defaultTheme().track);
         c.roundedRectangle((x + 9) * s, 108 * s, (w - 18) * s, 3 * s, 1.5f * s);
         c.setColor(kOrange);
         c.roundedRectangle((x + 9) * s, 108 * s, std::max(3.0f, (w - 18) * sec.energy / 100.0f) * s, 3 * s, 1.5f * s);
         const float bw = (w - 18 - 4 * 3) / 5.4f;
         for (int k = 0; k < 5; ++k) {
            const float bx = x + 9 + k * (bw + 3), by = h - 24, ww = k == 4 ? bw * 1.4f : bw;
            const bool lit = playing && (k == 4 ? f.fill : !f.fill && f.take == k);
            const bool qd = (playing || queued) && k < 4 && f.queuedTake == k;
            c.setColor(lit ? kSlate : ui::defaultTheme().button);
            c.roundedRectangle(bx * s, by * s, ww * s, 17 * s, 3 * s);
            c.setColor(qd ? kOrange : ui::defaultTheme().panelBorder);
            c.roundedRectangleBorder(bx * s, by * s, ww * s, 17 * s, 3 * s, 1.0f);
            label(c, s, k == 4 ? "FILL" : std::to_string(k + 1), bx, by + 2, ww, lit ? kWhite : 0, visage::Font::kCenter, 8.5f);
         }
      }
   }
   void mouseDown(const visage::MouseEvent &e) override {
      const float s = ui::designScale(*this);
      const float w = tileW(), x = e.position.x / s, y = e.position.y / s, h = height() / s;
      const int i = static_cast<int>(x / (w + 6));
      if (i < 0 || i >= kSections || std::fmod(x, w + 6) > w || !onAction)
         return;
      if (y >= h - 26) {
         const float bw = (w - 18 - 4 * 3) / 5.4f;
         const int k = std::clamp(static_cast<int>((std::fmod(x, w + 6) - 9) / (bw + 3)), 0, 4);
         if (frame && frame->section != i)
            onAction(i);
         onAction(k == 4 ? kActFill : kActTake1 + k);
         return;
      }
      onAction(i);
   }
   const PlayFrame *frame = nullptr;
   std::function<void(int)> onAction;
   std::function<VoiceSettings()> vs;
   std::function<int()> transpose, scale;

private:
   float tileW() const { return (width() / ui::designScale(*this) - 6 * (kSections - 1)) / kSections; }
   EditorHost &host_;
};

// NOW PLAYING: the chord sounding, big, what comes next, its notes; and the take on a roll with the
// playhead.
class NowPlaying : public visage::Frame {
public:
   explicit NowPlaying(EditorHost &host) : host_(host) {}
   void draw(visage::Canvas &c) override {
      readFeel(host_);
      const float s = ui::designScale(*this);
      const PlayFrame &f = frame ? *frame : PlayFrame{};
      const int key = static_cast<int>(std::lround(host_.paramValue(kKey)));
      const Section &sec = host_.song().sections[static_cast<size_t>(std::clamp(f.section, 0, kSections - 1))];
      const Pattern &p = patternOf(sec, f.ending ? 6 : f.fillDown ? 5 : f.fill ? 4 : std::clamp(f.take, 0, kTakes - 1));
      Chord now = f.chord;
      Voicing v;
      for (int i = 0; i < f.voices && i < kMaxVoices; ++i)
         v.notes[static_cast<size_t>(v.count++)] = f.voicing[static_cast<size_t>(i)];
      const Preview pv = preview(sec, p, viewBars(sec, p), vs(), transpose(), key, scale(), host_.paramValue(kRuns) > 0.0);
      if (now.none() && !pv.hits.empty()) {
         now = pv.hits.front().chord;
         v = pv.hits.front().voicing;
      }
      label(c, s, now.none() ? "N.C." : chordName(now, key, scale()), 0, 4, 140, kWhite, visage::Font::kLeft, 30);
      if (!f.next.none() && f.next != now)
         label(c, s, "then " + chordName(f.next, key, scale()), 0, 52, 140, kTitle, visage::Font::kLeft, 9.5f);
      if (!f.held.none() && std::lround(host_.paramValue(kChordsFrom)) > 0)
         label(c, s, "held: " + chordName(f.held, key, scale()), 0, 68, 140, kGreen, visage::Font::kLeft, 9.5f);
      label(c, s, notesText(v), 0, 96, 140, kOrange, visage::Font::kLeft, 10);
      label(c, s, std::string(kShapeNames[std::clamp(static_cast<int>(std::lround(host_.paramValue(kShape))), 0, kShapeCount - 1)]) +
                     (v.count ? " \xC2\xB7 top " + noteText(v.top()) : ""),
            0, 113, 140, 0, visage::Font::kLeft, 8.5f);
      drawRoll(c, s, pv, 140, 0, width() / s - 140, height() / s, f.running ? f.step : -1);
      if (f.running && f.step >= 0 && f.step < pv.steps) {
         const float cw = (width() / s - 140 - 6) / pv.steps;
         c.setColor(kWhite);
         c.fill((140 + 3 + f.step * cw + cw / 2) * s, 0, 1.4f * s, height());
      }
   }
   const PlayFrame *frame = nullptr;
   std::function<VoiceSettings()> vs;
   std::function<int()> transpose, scale;

private:
   EditorHost &host_;
};

// TRANSPORT's big button and readout.
class TransportView : public visage::Frame {
public:
   void draw(visage::Canvas &c) override {
      const float s = ui::designScale(*this);
      const PlayFrame &f = frame ? *frame : PlayFrame{};
      const ui::Theme &t = ui::defaultTheme();
      const bool playing = f.running;
      c.setColor(playing ? kWarm : t.button);
      c.roundedRectangle(0, 0, 60 * s, 52 * s, 7 * s);
      c.setColor(playing ? kOrange : t.panelBorder);
      c.roundedRectangleBorder(0, 0, 60 * s, 52 * s, 7 * s, 1.0f);
      c.setColor(playing ? kOrange : t.dimText);
      if (playing && !f.following)
         c.roundedRectangle(22 * s, 18 * s, 16 * s, 16 * s, 2 * s);
      else
         c.triangle(23 * s, 16 * s, 23 * s, 36 * s, 39 * s, 26 * s);
      const unsigned dot = f.following ? kGreen : playing ? kOrange : kFaint;
      c.setColor(dot);
      c.circle(72 * s, 3 * s, 7 * s);
      label(c, s, f.following ? "FOLLOWING HOST" : playing ? "RUNNING FREE" : "STOPPED", 83, 0, 160, dot,
            visage::Font::kLeft, 9.5f);
      char bpm[32];
      std::snprintf(bpm, sizeof(bpm), "%.1f", f.tempo);
      label(c, s, bpm, 70, 14, 90, kWhite, visage::Font::kLeft, 19);
      label(c, s, "BPM", 136, 23, 40, 0, visage::Font::kLeft, 9.5f);
      label(c, s, "bar " + std::to_string(f.bar + 1) + " \xC2\xB7 beat " + std::to_string(f.beat + 1), 70, 40, 160, 0,
            visage::Font::kLeft, 9);
   }
   void mouseDown(const visage::MouseEvent &e) override {
      const float s = ui::designScale(*this);
      if (e.position.x / s < 60 && e.position.y / s < 52 && onPlay)
         onPlay();
   }
   const PlayFrame *frame = nullptr;
   std::function<void()> onPlay;
};

// OUTPUT's keyboard: the notes going out (top orange, bottom purple, inner blue) and what is held
// (green).
class OutKeys : public visage::Frame {
public:
   void draw(visage::Canvas &c) override {
      const float s = ui::designScale(*this);
      std::map<int, unsigned> lit;
      if (frame) {
         for (int i = 0; i < frame->heldCount; ++i)
            lit[frame->heldNotes[static_cast<size_t>(i)]] = kGreen;
         Voicing v;
         for (int i = 0; i < frame->voices && i < kMaxVoices; ++i)
            v.notes[static_cast<size_t>(v.count++)] = frame->voicing[static_cast<size_t>(i)];
         for (const auto &[k, col] : voicingKeys(v))
            lit[k] = col;
      }
      drawKeys(c, s, 0, 0, width() / s, height() / s, 48, 84, lit);
   }
   const PlayFrame *frame = nullptr;
};

// The header's small transport: play, the tempo, and whether it follows the host.
class HeaderTransport : public visage::Frame {
public:
   void draw(visage::Canvas &c) override {
      const float s = ui::designScale(*this);
      const ui::Theme &t = ui::defaultTheme();
      const bool playing = frame && frame->running;
      c.setColor(playing ? kWarm : t.button);
      c.roundedRectangle(0, 0, 28 * s, 28 * s, 5 * s);
      c.setColor(playing ? kOrange : t.panelBorder);
      c.roundedRectangleBorder(0, 0, 28 * s, 28 * s, 5 * s, 1.0f);
      c.setColor(playing ? kOrange : t.dimText);
      if (playing && frame && !frame->following)
         c.fill(9 * s, 9 * s, 10 * s, 10 * s);
      else
         c.triangle(10 * s, 8 * s, 10 * s, 20 * s, 20 * s, 14 * s);
      char bpm[32];
      std::snprintf(bpm, sizeof(bpm), "%.0f BPM", frame ? frame->tempo : 120.0);
      label(c, s, bpm, 34, 0, 70, kWhite, visage::Font::kLeft, 10.5f);
      const bool host = frame && frame->following;
      c.setColor(host ? kGreen : playing ? kOrange : kFaint);
      c.circle(35 * s, 18 * s, 5 * s);
      label(c, s, host ? "HOST" : "FREE", 43, 14.5f, 50, host ? kGreen : playing ? kOrange : 0, visage::Font::kLeft, 8.5f);
   }
   void mouseDown(const visage::MouseEvent &e) override {
      if (e.position.x < 28 * ui::designScale(*this) && onPlay)
         onPlay();
   }
   const PlayFrame *frame = nullptr;
   std::function<void()> onPlay;
};

// ---- overlays ----

// PLAYERS: every player, with what each plays like.
class PlayersOverlay : public visage::Frame {
public:
   explicit PlayersOverlay(EditorHost &host) : host_(host) {}
   void draw(visage::Canvas &c) override {
      const float s = ui::designScale(*this);
      c.setColor(0x88000000);
      c.fill(0, 0, width(), height());
      const float x0 = kX, y0 = kY;
      c.setColor(ui::defaultTheme().panel);
      c.roundedRectangle(x0 * s, y0 * s, kWBox * s, kHBox * s, 8 * s);
      c.setColor(ui::defaultTheme().panelBorder);
      c.roundedRectangleBorder(x0 * s, y0 * s, kWBox * s, kHBox * s, 8 * s, 1.0f);
      label(c, s, "PLAYERS", x0 + 16, y0 + 12, 100, kOrange, visage::Font::kLeft, 11.5f);
      label(c, s, "A player is how the chords are played: the instrument it voices for, how busy, how rich, how long the "
                  "chords ring, how often a run leads in, how far behind the beat. It never changes the chords.",
            x0 + 110, y0 + 14, kWBox - 150, 0, visage::Font::kLeft, 8.5f);
      label(c, s, "\xC3\x97", x0 + kWBox - 26, y0 + 10, 16, 0, visage::Font::kLeft, 13);
      const int who = static_cast<int>(std::lround(host_.paramValue(kPlayer)));
      for (int i = 0; i < kPlayers; ++i) {
         const Player &p = kPlayerList[i];
         const float x = x0 + 14 + (i % 3) * kCardW, y = y0 + 40 + (i / 3) * kCardH;
         const bool on = who == i;
         c.setColor(on ? kWarm : kCell);
         c.roundedRectangle(x * s, y * s, (kCardW - 8) * s, (kCardH - 8) * s, 6 * s);
         c.setColor(on ? kOrange : ui::defaultTheme().panelBorder);
         c.roundedRectangleBorder(x * s, y * s, (kCardW - 8) * s, (kCardH - 8) * s, 6 * s, on ? 1.5f * s : 1.0f);
         drawPlayerIcon(c, s, x + 22, y + 22, p.kind, kOrange);
         label(c, s, p.name, x + 42, y + 8, 160, on ? kOrange : kWhite, visage::Font::kLeft, 11.5f);
         label(c, s, upper(kInstrumentNames[p.instrument]), x + 42, y + 9, kCardW - 62, kBlue, visage::Font::kRight, 7.5f);
         label(c, s, p.blurb, x + 42, y + 24, kCardW - 60, kTitle, visage::Font::kLeft, 9);
         label(c, s, p.genres, x + 42, y + 37, kCardW - 60, 0, visage::Font::kLeft, 8.5f);
         const char *traits[4] = {"BUSY", "RICH", "RUNS", "RING"};
         const double v[4] = {p.busy, p.rich, p.runs, p.ring};
         const float tw = (kCardW - 32) / 4;
         for (int k = 0; k < 4; ++k) {
            const float tx = x + 10 + k * tw;
            label(c, s, traits[k], tx, y + 51, tw, 0, visage::Font::kLeft, 7.5f);
            c.setColor(ui::defaultTheme().track);
            c.roundedRectangle(tx * s, (y + 66) * s, (tw - 10) * s, 2.5f * s, 1 * s);
            c.setColor(kOrange);
            c.roundedRectangle(tx * s, (y + 66) * s, std::max(2.0f, static_cast<float>((tw - 10) * v[k])) * s, 2.5f * s, 1 * s);
         }
      }
   }
   void mouseDown(const visage::MouseEvent &e) override {
      const float s = ui::designScale(*this);
      const float x = e.position.x / s - kX - 14, y = e.position.y / s - kY - 40;
      if (x < -14 || y < -40 || x > kWBox - 14 || y > kHBox - 40 || (y < 0 && x > kWBox - 50)) {
         if (onClose)
            onClose();
         return;
      }
      if (x < 0 || y < 0)
         return;
      const int col = static_cast<int>(x / kCardW), row = static_cast<int>(y / kCardH);
      if (col > 2 || row * 3 + col >= kPlayers)
         return;
      if (onPick)
         onPick(row * 3 + col);
   }
   std::function<void(int)> onPick;
   std::function<void()> onClose;
   static constexpr float kX = 20, kY = 76, kWBox = 960, kHBox = 500, kCardW = 310, kCardH = 90;

private:
   EditorHost &host_;
};

// The style browser: every genre by family, and the styles of the genre picked, with where the
// genre's content comes from.
class BrowserOverlay : public visage::Frame {
public:
   explicit BrowserOverlay(EditorHost &host) : host_(host) {}
   void open() {
      shown_ = static_cast<int>(std::lround(host_.paramValue(kGenre)));
      setVisible(true);
      redraw();
   }
   void draw(visage::Canvas &c) override {
      const float s = ui::designScale(*this);
      c.setColor(0x88000000);
      c.fill(0, 0, width(), height());
      c.setColor(ui::defaultTheme().panel);
      c.roundedRectangle(kX * s, kY * s, kWBox * s, kHBox * s, 8 * s);
      c.setColor(ui::defaultTheme().panelBorder);
      c.roundedRectangleBorder(kX * s, kY * s, kWBox * s, kHBox * s, 8 * s, 1.0f);
      label(c, s, "ALL GENRES", kX + 16, kY + 12, 200, kOrange, visage::Font::kLeft, 11.5f);
      int styles = 0;
      for (int g = 0; g < genreCount(); ++g)
         styles += static_cast<int>(genreAt(g).styles.size());
      label(c, s, std::to_string(genreCount()) + " genres \xC2\xB7 " + std::to_string(styles) +
                     " styles \xC2\xB7 every style is also a preset",
            kX + 120, kY + 14, 400, 0, visage::Font::kLeft, 9);
      label(c, s, "\xC3\x97", kX + kWBox - 26, kY + 10, 16, 0, visage::Font::kLeft, 13);
      const int current = static_cast<int>(std::lround(host_.paramValue(kGenre)));
      for (const Item &it : items()) {
         if (it.genre < 0) {
            label(c, s, upper(kFamilies[it.family]), it.x, it.y, 150, kFaint, visage::Font::kLeft, 8);
            continue;
         }
         const bool on = it.genre == shown_;
         if (on) {
            c.setColor(kSlate);
            c.roundedRectangle((it.x - 5) * s, (it.y - 2) * s, 138 * s, 17 * s, 3 * s);
         }
         label(c, s, genreAt(it.genre).name, it.x, it.y, 110, it.genre == current ? kOrange : on ? kWhite : kTitle,
               visage::Font::kLeft, 10);
         label(c, s, std::to_string(genreAt(it.genre).styles.size()), it.x, it.y + 1, 128, 0, visage::Font::kRight, 8.5f);
      }
      const float lx = kX + kListX;
      c.setColor(ui::defaultTheme().panelBorder);
      c.fill((lx - 14) * s, (kY + 40) * s, 1.0f * s, (kHBox - 52) * s);
      const Genre &g = genreAt(shown_);
      label(c, s, upper(g.name), lx, kY + 42, 200, kOrange, visage::Font::kLeft, 10.5f);
      label(c, s, std::to_string(g.styles.size()) + " styles \xC2\xB7 " + kPlayerList[g.player].name, lx + 140, kY + 43, 200, 0,
            visage::Font::kLeft, 9);
      const int style = static_cast<int>(std::lround(host_.paramValue(kStyle)));
      for (size_t i = 0; i < g.styles.size(); ++i) {
         const float y = kY + 64 + i * kRowH;
         const bool on = shown_ == current && static_cast<int>(i) == style;
         if (on) {
            c.setColor(kSlate);
            c.roundedRectangle((lx - 6) * s, (y - 2) * s, (kWBox - kListX - 12) * s, (kRowH - 3) * s, 4 * s);
         }
         label(c, s, g.styles[i].name, lx, y, 170, on ? kWhite : kTitle, visage::Font::kLeft, 10.5f);
         label(c, s, std::string(g.styles[i].tag) + " \xC2\xB7 " + std::to_string(g.styles[i].bpm) + " BPM", lx, y + 14, 170, 0,
               visage::Font::kLeft, 8.5f);
         const ProgSet &ps = g.progs[static_cast<size_t>(std::clamp(g.styles[i].progSet, 0, static_cast<int>(g.progs.size()) - 1))];
         label(c, s, std::string(kScaleNames[ps.scale]) + ": " + ps.roles[kRoleVerse], lx + 176, y + 6, kWBox - kListX - 196,
               0, visage::Font::kLeft, 8.5f);
      }
      label(c, s, std::string("Sources (see the README): ") + g.source, lx, kY + kHBox - 26, kWBox - kListX - 20, kFaint,
            visage::Font::kLeft, 8.5f);
   }
   void mouseDown(const visage::MouseEvent &e) override {
      const float s = ui::designScale(*this);
      const float x = e.position.x / s, y = e.position.y / s;
      if (x < kX || x > kX + kWBox || y < kY || y > kY + kHBox || (y < kY + 32 && x > kX + kWBox - 40)) {
         if (onClose)
            onClose();
         return;
      }
      for (const Item &it : items())
         if (it.genre >= 0 && x >= it.x - 5 && x < it.x + 133 && y >= it.y - 2 && y < it.y + 15) {
            shown_ = it.genre;
            redraw();
            return;
         }
      const float lx = kX + kListX;
      if (x >= lx - 6) {
         const int i = static_cast<int>((y - kY - 62) / kRowH);
         if (i >= 0 && i < static_cast<int>(genreAt(shown_).styles.size()) && onPick)
            onPick(shown_, i);
      }
   }
   std::function<void(int genre, int style)> onPick;
   std::function<void()> onClose;
   static constexpr float kX = 20, kY = 84, kWBox = 960, kHBox = 528, kListX = 470, kRowH = 36;

private:
   struct Item {
      float x, y;
      int family, genre; // genre -1: the family's heading
   };
   std::vector<Item> items() const {
      std::vector<Item> out;
      for (int col = 0; col < 3; ++col) {
         float y = kY + 44;
         for (int fam = col * 2; fam < (col == 2 ? kFamilyCount : col * 2 + 2); ++fam) {
            const float x = kX + 18 + col * 146;
            out.push_back({x, y, fam, -1});
            y += 15;
            for (int g = 0; g < genreCount(); ++g)
               if (genreAt(g).family == kFamilies[fam]) {
                  out.push_back({x, y, fam, g});
                  y += 18;
               }
            y += 10;
         }
      }
      return out;
   }
   EditorHost &host_;
   int shown_ = 0;
};

// SECTION NOTES: the keyboard map, or each action's own learned note.
class NotesOverlay : public visage::Frame {
public:
   explicit NotesOverlay(EditorHost &host) : host_(host) {}
   void draw(visage::Canvas &c) override {
      const float s = ui::designScale(*this);
      const ui::Theme &t = ui::defaultTheme();
      c.setColor(t.panel);
      c.roundedRectangle(0, 0, width(), height(), 8 * s);
      c.setColor(t.panelBorder);
      c.roundedRectangleBorder(0, 0, width(), height(), 8 * s, 1.0f);
      label(c, s, "SECTION NOTES", 16, 12, 200, kOrange, visage::Font::kLeft, 11.5f);
      const bool custom = std::lround(host_.paramValue(kNoteMap)) == 1;
      if (!custom) {
         label(c, s, "One note per section, on any channel or one you pick. Chords are played from " +
                        noteText(static_cast<int>(std::lround(host_.paramValue(kChordSplit)))) + " up.",
               16, 32, 560, 0, visage::Font::kLeft, 9);
         drawMap(c, s);
      } else {
         label(c, s, "Press LEARN, then play any note to give it that job. Orange: learned; the rest are as the keyboard map.",
               16, 32, 560, 0, visage::Font::kLeft, 9);
         drawCustom(c, s);
      }
      label(c, s, "MORE: HOW THE SONG ENDS AND STARTS, THE SWING, A PAD", 16, 264, 600, kFaint, visage::Font::kLeft, 8);
   }
   void mouseDown(const visage::MouseEvent &e) override {
      const float s = ui::designScale(*this);
      const float x = e.position.x / s, y = e.position.y / s;
      if (std::lround(host_.paramValue(kNoteMap)) != 1)
         return;
      for (int a = 0; a < kActionCount; ++a) {
         const float rx = rowX(a), ry = rowY(a);
         if (y >= ry && y < ry + kRowH - 3 && x >= rx + 138 && x < rx + 194) {
            if (onLearn)
               onLearn(host_.learnAction == a ? -1 : a);
            return;
         }
      }
   }
   std::function<void(int)> onLearn;
   static constexpr float kKeysY = 52, kRowH = 21;

private:
   float rowX(int a) const { return 16 + (a % 3) * 204.0f; }
   float rowY(int a) const { return kKeysY + 4 + (a / 3) * kRowH; }

   void drawCustom(visage::Canvas &c, float s) {
      const ui::Theme &t = ui::defaultTheme();
      for (int a = 0; a < kActionCount; ++a) {
         const float x = rowX(a), y = rowY(a);
         const bool learning = host_.learnAction == a;
         c.setColor(learning ? kWarm : kCell);
         c.roundedRectangle(x * s, y * s, 198 * s, (kRowH - 3) * s, 4 * s);
         c.setColor(learning ? kOrange : t.panelBorder);
         c.roundedRectangleBorder(x * s, y * s, 198 * s, (kRowH - 3) * s, 4 * s, learning ? 1.4f * s : 1.0f);
         label(c, s, upper(kActionNames[a]), x + 8, y + 2.5f, 70, learning ? kOrange : kWhite, visage::Font::kLeft, 9);
         const int note = static_cast<int>(std::lround(host_.paramValue(static_cast<clap_id>(kLearnNote + a))));
         const bool learned = note != 12 + kActionKey[a];
         c.setColor(learned ? kWarm : kWell);
         c.roundedRectangle((x + 76) * s, (y + 2) * s, 58 * s, (kRowH - 7) * s, 3 * s);
         if (learned) {
            c.setColor(kOrange);
            c.roundedRectangleBorder((x + 76) * s, (y + 2) * s, 58 * s, (kRowH - 7) * s, 3 * s, 1.0f);
         }
         label(c, s, learning ? "play\xE2\x80\xA6" : noteText(note), x + 76, y + 3, 58, learning ? kOrange : kWhite,
               visage::Font::kCenter, 9);
         c.setColor(learning ? kOrange : t.button);
         c.roundedRectangle((x + 138) * s, (y + 2) * s, 54 * s, (kRowH - 7) * s, 3 * s);
         label(c, s, learning ? "CANCEL" : "LEARN", x + 138, y + 3, 54, learning ? kInk : kOrange, visage::Font::kCenter, 8.5f);
      }
   }

   void drawMap(visage::Canvas &c, float s) {
      const int base = 12 * (static_cast<int>(std::lround(host_.paramValue(kNoteOctave))) + 1);
      const int whites[11] = {0, 2, 4, 5, 7, 9, 11, 12, 14, 16, 17};
      const float kw = (width() / s - 32) / 12, kh = 150, x0 = 16, y0 = kKeysY;
      const PlayFrame *f = frame;
      auto actionAt = [](int semis) {
         for (int a = 0; a < kActionCount; ++a)
            if (kActionKey[a] == semis)
               return a;
         return -1;
      };
      for (int i = 0; i < 12; ++i) {
         const int semis = i < 11 ? whites[i] : 19;
         const int a = actionAt(semis);
         const bool playing = f && a >= 0 && a <= kActOutro && f->section == a;
         const bool queued = f && a >= 0 && f->queued == a;
         c.setColor(playing ? 0xfff9e3cdu : a >= 0 ? 0xffdfe2e6u : 0xff8a9099u);
         c.roundedRectangle((x0 + i * kw) * s, y0 * s, (kw - 2) * s, kh * s, 3 * s);
         if (queued) {
            c.setColor(kOrange);
            dashed(c, (x0 + i * kw) * s, y0 * s, (kw - 2) * s, kh * s, 3 * s, 1.5f * s);
         }
         if (a >= 0) {
            label(c, s, upper(kActionNames[a]), x0 + i * kw, y0 + kh - 36, kw - 2, a <= kActOutro ? 0xff1b1d22u : 0xff5d636cu,
                  visage::Font::kCenter, 8.5f);
            label(c, s, noteText(base + semis), x0 + i * kw, y0 + kh - 20, kw - 2, 0xff5d636cu, visage::Font::kCenter, 8);
         }
      }
      for (int i = 0; i < 11; ++i) {
         const int semis = whites[i] + 1;
         if (i == 2 || i == 6 || i == 9 || semis > 18)
            continue;
         const int a = actionAt(semis);
         const float x = x0 + (i + 1) * kw - kw * 0.3f;
         const bool lit = f && ((a >= kActTake1 && a <= kActTake4 && f->take == a - kActTake1 && !f->fill) ||
                                (a == kActFill && f->fill) || (a == kActRunsOff && f->runsOff) ||
                                (a == kActHoldChord && f->hold));
         c.setColor(lit ? 0xff5a3a1au : 0xff1c1f24u);
         c.roundedRectangle(x * s, y0 * s, kw * 0.6f * s, kh * 0.6f * s, 2.5f * s);
         if (a >= 0) {
            const std::string name = upper(kActionNames[a]);
            const size_t space = name.find(' ');
            const bool two = space != std::string::npos && name.size() > 6;
            const unsigned col = lit ? kOrange : kTitle;
            if (two) {
               label(c, s, name.substr(0, space), x - 4, y0 + kh * 0.6f - 31, kw * 0.6f + 8, col, visage::Font::kCenter, 7);
               label(c, s, name.substr(space + 1), x - 4, y0 + kh * 0.6f - 21, kw * 0.6f + 8, col, visage::Font::kCenter, 7);
            } else
               label(c, s, name, x - 4, y0 + kh * 0.6f - 21, kw * 0.6f + 8, col, visage::Font::kCenter, 7);
         }
      }
   }

public:
   const PlayFrame *frame = nullptr;

private:
   EditorHost &host_;
};

const std::vector<double> &defaults() {
   static const std::vector<double> d = defaultValues();
   return d;
}

// The qualities the chord card offers, in its order.
const int kQualityChips[] = {kMaj, kMin, kDom7, kDom9, kDom13, kSharp9, kFlat9, kMin7, kMin9, kMaj7, kMaj9, kSixNine,
                             kSus7, kDim7};

// ---- the panel ----

class Panel : public ui::DesignFrame, public visage::EventTimer {
public:
   explicit Panel(EditorHost &host)
      : DesignFrame(kW, kH), host_(host), bindings_(host),
        rng_(static_cast<uint32_t>(std::chrono::steady_clock::now().time_since_epoch().count())) {
      tips_.maxWidth = 260.0f;
      tips_.add(add<ui::HoverArea>({14, 12, 150, 40}), "JComp version " JCLAP_PLUGIN_VERSION);
      tabs_ = &add<ui::TabBar>({176, 18, 368, 30}, std::vector<std::string>{"GENERATE", "CHORDS", "EDIT", "PLAY"});
      tabs_->setAccent(kOrange);
      tabs_->onChange = [this](int p) {
         if (p == host_.page)
            return;
         host_.page = p;
         closeOverlays();
         redraw();
      };
      header_ = &add<HeaderTransport>({564, 18, 92, 28});
      header_->onPlay = [this] { togglePlay(); };
      tips_.add(*header_, "PLAY runs JComp on its own while the host is stopped.");
      add<ui::UpdateNotice>({176, 50, 140, 12});
      buildPresets();
      buildGenerate();
      buildChords();
      buildEdit();
      buildPlay();
      buildOverlays();
      tabs_->select(host_.page);
      showOverlays();
      if (host_.litter.empty())
         breed();
      startTimer(33);
   }
   ~Panel() override {
      if (host_.learnAction >= 0)
         host_.armLearn(false);
   }

   void showPage(int page) {
      host_.page = page;
      tabs_->select(page);
      showOverlays();
   }
   void showOverlays() {
      players_->setVisible(host_.playersOpen && host_.page == kGeneratePage);
      if (host_.browserOpen && host_.page == kGeneratePage)
         browser_->open();
      else
         browser_->setVisible(false);
      notes_->setVisible(host_.notesOpen && host_.page == kPlayPage);
      notesControls(host_.notesOpen && host_.page == kPlayPage);
   }

   void timerCallback() override {
      const PlayFrame &f = host_.frame();
      frame_ = f;
      header_->frame = &frame_;
      header_->redraw();
      if (host_.page == kGeneratePage) {
         song_->frame = &frame_;
         song_->redraw();
      } else if (host_.page == kChordsPage) {
         rows_->frame = &frame_;
         rows_->redraw();
      } else if (host_.page == kEditPage) {
         roll_->frame = &frame_;
         roll_->redraw();
      } else {
         tiles_->frame = &frame_;
         tiles_->redraw();
         now_->frame = &frame_;
         now_->redraw();
         transport_->frame = &frame_;
         transport_->redraw();
         outKeys_->frame = &frame_;
         outKeys_->redraw();
         notes_->frame = &frame_;
         if (notes_->isVisible())
            notes_->redraw();
      }
      if (host_.learnAction >= 0) {
         const int note = host_.takeLearned();
         if (note >= 0) {
            setParam(static_cast<clap_id>(kLearnNote + host_.learnAction), note);
            host_.learnAction = -1;
            notes_->redraw();
            redraw();
         }
      }
   }

   void draw(visage::Canvas &c) override {
      if (host_.page == kEditPage)
         gFeel = LiveFeel{}; // EDIT shows the take as written
      else
         readFeel(host_);
      const ui::Theme &t = ui::defaultTheme();
      const float s = scale();
      c.setColor(t.background);
      c.fill(0, 0, width(), height());
      c.setColor(t.headerBar);
      c.fill(0, 0, width(), kHeader * s);
      // The logo: three keys.
      c.setColor(kOrange);
      c.roundedRectangleBorder(16 * s, 17 * s, 34 * s, 28 * s, 4 * s, 2.2f * s);
      for (int i = 0; i < 3; ++i)
         c.roundedRectangle((22 + i * 8.5f) * s, 17 * s, 4.5f * s, 15 * s, 1 * s);
      c.text("J", ui::font(28 * s), visage::Font::kLeft, 59 * s, 12 * s, 30 * s, 40 * s);
      c.setColor(t.text);
      c.text("Comp", ui::font(28 * s), visage::Font::kLeft, 72 * s, 12 * s, 140 * s, 40 * s);
      switch (host_.page) {
      case kGeneratePage: drawGenerate(c, s); break;
      case kChordsPage: drawChords(c, s); break;
      case kEditPage: drawEdit(c, s); break;
      default: drawPlay(c, s); break;
      }
   }

private:
   template <typename T, typename... Args>
   T &onPage(Page page, ui::Rect bounds, Args &&...args) {
      return tabs_->addToPage(page, add<T>(bounds, std::forward<Args>(args)...));
   }

   void setParam(clap_id id, double v) {
      host_.beginEdit(id);
      host_.setParam(id, v);
      host_.endEdit(id);
   }
   double param(clap_id id) const { return host_.paramValue(id); }
   int whole(clap_id id) const { return static_cast<int>(std::lround(host_.paramValue(id))); }
   Recipe recipe() const { return recipeFrom([this](clap_id id) { return host_.paramValue(id); }); }
   VoiceSettings vs() const { return voiceSettings([this](clap_id id) { return host_.paramValue(id); }); }
   int transpose() const { return ((whole(kKey) - host_.song().key) % 12 + 12) % 12; }
   int key() const { return whole(kKey); }
   int scaleNow() const { return whole(kScale); }

   // The panel's views share these.
   template <typename T>
   void wire(T &v) {
      v.vs = [this] { return vs(); };
      v.transpose = [this] { return transpose(); };
      v.scale = [this] { return scaleNow(); };
   }

   void togglePlay() { setParam(kPlay, whole(kPlay) ? 0.0 : 1.0); }

   // ---- making songs ----

   void remember() {
      host_.previous.push_back(host_.song());
      if (host_.previous.size() > 16)
         host_.previous.erase(host_.previous.begin());
   }
   // `keepChords`: ENERGY and BLEND remake the rhythms only, the chords (and their edits) kept.
   void regenerate(bool keepChords = false) {
      Song s = host_.song();
      generateSong(recipe(), s, keepChords);
      host_.setSong(s);
      host_.parents.clear();
      breed();
      redraw();
   }
   void rollSong() {
      remember();
      setParam(kSeed, static_cast<double>(1 + rng_() % 99999));
      regenerate();
   }
   void previousRoll() {
      if (host_.previous.empty())
         return;
      host_.setSong(host_.previous.back());
      host_.previous.pop_back();
      host_.parents.clear();
      breed();
      redraw();
   }
   void pickStyle(int genre, int style) {
      for (const auto &[id, v] : styleValues(genre, style))
         setParam(id, v);
      if (whole(kBlendGenre) - 1 == genre)
         setParam(kBlendGenre, 0);
      remember();
      regenerate();
   }
   // Five children of the section's take. The same section, generation and litter number give the
   // same five; NEW LITTER asks for the next.
   void breed(bool fresh = false) {
      if (fresh)
         ++litterRolls_;
      const Section &sec = host_.song().sections[static_cast<size_t>(host_.mutateSection)];
      Rng rng(mix(mix(sec.seed, sec.generation), litterRolls_ * 31u + static_cast<uint32_t>(host_.mutateSection)));
      host_.litter.clear();
      host_.tags.clear();
      for (int i = 0; i < 5; ++i) {
         std::string tag;
         host_.litter.push_back(mutate(sec.takes[0], param(kMutation), rng, tag, styleOf(recipe()).mods));
         host_.tags.push_back(tag);
      }
      if (mutate_)
         mutate_->redraw();
   }
   void adopt(int child) {
      Song s = host_.song();
      Section &sec = s.sections[static_cast<size_t>(host_.mutateSection)];
      host_.parents.push_back(sec.takes[0]);
      adoptChild(recipe(), sec, host_.litter[static_cast<size_t>(child)], rng_(), host_.mutateSection);
      host_.setSong(s);
      breed();
      redraw();
   }
   void back() {
      if (host_.parents.empty())
         return;
      Song s = host_.song();
      Section &sec = s.sections[static_cast<size_t>(host_.mutateSection)];
      adoptChild(recipe(), sec, host_.parents.back(), rng_(), host_.mutateSection);
      sec.generation = static_cast<uint16_t>(std::max(0, sec.generation - 2));
      host_.parents.pop_back();
      host_.setSong(s);
      breed();
      redraw();
   }

   // ---- header ----

   void buildPresets() {
      presets_ = &add<ui::PresetBar>({664, 18, 322, 28}, factoryPresets(), kPresetFormat, host_);
      presets_->setAccent(kOrange);
      presets_->defaults = defaults();
      presets_->currentName = [this] { return host_.presetName(); };
      presets_->setCurrentName = [this](const std::string &n) { host_.setPresetName(n); };
      presets_->onLoad = [this](const ui::Preset &p) {
         // A preset is the sound and the song: the routing, the transport and the notes set up for
         // this project stay as they are.
         ui::Preset sound = p;
         // A preset from before 0.6.0 (no SWING EASES WHEN FAST): its Song Mode 1 went round and
         // round, and its swing didn't ease.
         if (std::none_of(sound.values.begin(), sound.values.end(), [](const auto &v) { return v.first == kSwingEase; })) {
            for (auto &v : sound.values)
               if (v.first == kSongMode && std::lround(v.second) == 1)
                  v.second = 2;
            sound.values.push_back({kSwingEase, 0.0});
         }
         std::vector<double> d = defaults();
         sound.values.erase(std::remove_if(sound.values.begin(), sound.values.end(),
                                           [](const auto &v) { return isSetupParam(v.first); }),
                            sound.values.end());
         for (clap_id id = 0; id < d.size(); ++id)
            if (isSetupParam(id))
               d[id] = host_.paramValue(id);
         ui::applyPreset(sound, host_, d);
         Song song;
         const std::filesystem::path file = ui::userPresetFolder(kPresetFormat) / (p.name + kPresetFormat.extension);
         if (readSong(songFile(file), song, recipe()))
            host_.setSong(song);
         else {
            Song s;
            generateSong(recipe(), s);
            host_.setSong(s);
         }
         host_.parents.clear();
         breed();
         redraw();
      };
      presets_->onSaved = [this](const std::filesystem::path &file) { writeSong(songFile(file), host_.song()); };
      presets_->onDeleted = [](const std::filesystem::path &file) {
         std::error_code ec;
         std::filesystem::remove(songFile(file), ec);
      };
      presets_->onDice = [this] { rollSong(); };
      tips_.add(*presets_, "Every style is a preset, filed by family and genre. The die rolls the song again.");
   }

   void closeOverlays() {
      host_.playersOpen = host_.browserOpen = host_.notesOpen = false;
      if (host_.learnAction >= 0) {
         host_.learnAction = -1;
         host_.armLearn(false);
      }
      showOverlays();
   }

   // ---- GENERATE ----

   void buildGenerate() {
      tiles12_ = &onPage<GenreTiles>(kGeneratePage, {26, 106, 507, 132}, host_);
      tiles12_->onPick = [this](int g) { pickStyle(g, g == whole(kGenre) ? whole(kStyle) : 0); };
      tiles12_->onBlend = [this](int g) {
         setParam(kBlendGenre, whole(kBlendGenre) - 1 == g || g == whole(kGenre) ? 0 : g + 1);
         remember();
         regenerate(true);
      };
      tips_.add(*tiles12_, "Click a genre to hear it. Shift-click another to blend its rhythms in (BLEND sets how much).");
      style_ = &onPage<ui::Dropdown>(kGeneratePage, {90, 83, 150, 19});
      style_->setAccent(kOrange);
      style_->onChange = [this](int i) { pickStyle(whole(kGenre), i); };
      tips_.add(*style_, "The genre's styles. Each is a rhythm, a progression set, a tempo and its twists.");
      auto &all = onPage<FlatButton>(kGeneratePage, {410, 83, 76, 19}, "ALL " + std::to_string(genreCount()) + " \xE2\x96\xB8", 8.5f);
      all.textColour = kOrange;
      all.onClick = [this](const visage::MouseEvent &) {
         host_.browserOpen = true;
         showOverlays();
      };
      blend_ = &onPage<ParamSlider>(kGeneratePage, {490, 89, 40, 8}, host_, kBlend, "", "", "", kPurple);
      blend_->onDone = [this] { regenerate(true); };
      tips_.add(*blend_, "How much of the blended genre's rhythm comes in.");

      player_ = &onPage<PlayerButton>(kGeneratePage, {564, 104, 230, 28}, host_);
      player_->onOpen = [this] {
         host_.playersOpen = !host_.playersOpen;
         showOverlays();
      };
      tips_.add(*player_, "Who plays: the instrument it voices for and how it touches the chords. Picking one sets the "
                          "voicing and feel to suit.");
      energy_ = &onPage<ParamSlider>(kGeneratePage, {564, 142, 250, 44}, host_, kEnergy, "ENERGY", "laid back", "driving", kOrange);
      energy_->onDone = [this] { regenerate(true); };
      tips_.add(*energy_, "How hard the whole song goes. Each section's own energy (in THE SONG) rides on it.");
      weird_ = &onPage<ParamSlider>(kGeneratePage, {564, 194, 250, 44}, host_, kWeird, "WEIRD", "triads, as written",
                                    "extended, reharmonised", kPurple);
      weird_->onDone = [this] { regenerate(); };
      tips_.add(*weird_, "Richer chords and substitutions (tritone subs, approach chords), hits left to chance and off the "
                         "grid.");
      auto &roll = onPage<FlatButton>(kGeneratePage, {830, 148, 144, 46}, "ROLL SONG", 12.5f);
      roll.accent = roll.dice = true;
      roll.onClick = [this](const visage::MouseEvent &) { rollSong(); };
      tips_.add(roll, "A new seed for every section that isn't locked: new rhythms and new colour, the same progressions.");
      auto &prev = onPage<FlatButton>(kGeneratePage, {830, 202, 144, 26}, "\xE2\x86\x90 PREVIOUS ROLL", 9.5f);
      prev.onClick = [this](const visage::MouseEvent &) { previousRoll(); };

      song_ = &onPage<SongStrip>(kGeneratePage, {26, 278, 948, 158}, host_);
      wire(*song_);
      song_->onSelect = [this](int i) {
         host_.mutateSection = i;
         host_.parents.clear();
         breed();
         redraw();
      };
      song_->onDice = [this](int i) {
         remember();
         Song s = host_.song();
         rollSection(recipe(), s, i, rng_());
         host_.setSong(s);
         if (i == host_.mutateSection)
            breed();
         redraw();
      };
      song_->onLock = [this](int i) {
         Song s = host_.song();
         s.sections[static_cast<size_t>(i)].locked ^= 1;
         host_.setSong(s);
         redraw();
      };
      song_->onEnergy = [this](int i) {
         Song s = host_.song();
         rollSection(recipe(), s, i, s.sections[static_cast<size_t>(i)].seed, true);
         host_.setSong(s);
         if (i == host_.mutateSection)
            breed();
         redraw();
      };
      tips_.add(*song_, "Click a section to mutate it below. Drag its energy up or down (to 0 for silence); the dice "
                        "rolls just that section, the lock keeps it from ROLL SONG.");

      mutation_ = &onPage<ParamSlider>(kGeneratePage, {650, 466, 120, 8}, host_, kMutation, "", "", "", kPurple);
      mutation_->onDone = [this] { breed(); };
      auto &litter = onPage<FlatButton>(kGeneratePage, {836, 459, 138, 22}, "NEW LITTER", 9.5f);
      litter.dice = true;
      litter.onClick = [this](const visage::MouseEvent &) { breed(true); };
      mutate_ = &onPage<MutateRow>(kGeneratePage, {26, 488, 948, 112}, host_);
      wire(*mutate_);
      mutate_->onPick = [this](int i) { adopt(i); };
      mutate_->onBack = [this] { back(); };
   }

   void drawGenerate(visage::Canvas &c, float s) {
      drawCard(c, s, 12, 76, 530, 168, "GENRE");
      const int blend = whole(kBlendGenre) - 1;
      label(c, s, blend >= 0 ? "+ " + std::string(genreAt(blend).name) : "shift-click: blend", 246, 86, 160,
            blend >= 0 ? kPurple : kFaint, visage::Font::kLeft, 8.5f);
      drawCard(c, s, 550, 76, 438, 168, "HOW IT PLAYS");
      const Player &p = kPlayerList[std::clamp(whole(kPlayer), 0, kPlayers - 1)];
      label(c, s, "ON", 804, 112, 24, 0, visage::Font::kLeft, 9.5f);
      label(c, s, upper(kInstrumentNames[p.instrument]), 826, 110, 150, kWhite, visage::Font::kLeft, 11);
      char note[200];
      std::snprintf(note, sizeof(note), "in %s %s \xC2\xB7 %d bars \xC2\xB7 the dice re-roll how it plays, never your chords \xC2\xB7 "
                                        "CHORDS to change them",
                    kKeyNames[key()], kScaleNames[std::clamp(scaleNow(), 0, kScaleCount - 1)], host_.song().totalBars());
      drawCard(c, s, 12, 252, 976, 192, "THE SONG", note);
      const Section &sec = host_.song().sections[static_cast<size_t>(host_.mutateSection)];
      drawCard(c, s, 12, 452, 976, 156, std::string("MUTATE ") + kSectionCaps[host_.mutateSection]);
      label(c, s, "gen " + std::to_string(sec.generation + 1), 150, 462, 60, kPurple, visage::Font::kLeft, 9.5f);
      label(c, s, "MUTATION", 560, 459, 84, 0, visage::Font::kRight, 9.5f);
      label(c, s, "wild", 776, 459, 40, kFaint, visage::Font::kLeft, 8.5f);
      label(c, s, "Pick a genre and a player, roll the song, then mutate any section until it sits right. CHORDS sets what "
                  "it plays, EDIT the last details.",
            12, 618, 976, 0, visage::Font::kRight, 8.5f);
      syncGenerate();
   }

   void syncGenerate() {
      const Genre &g = genreAt(whole(kGenre));
      std::vector<std::string> names;
      for (const Style &st : g.styles)
         names.push_back(st.name);
      if (names != style_->options())
         style_->setOptions(names);
      style_->setSelected(std::clamp(whole(kStyle), 0, static_cast<int>(names.size()) - 1));
      blend_->setVisible(host_.page == kGeneratePage && whole(kBlendGenre) > 0);
   }

   // ---- CHORDS ----

   void buildChords() {
      auto &keyBox = onPage<ui::Dropdown>(kChordsPage, {26, 118, 70, 22}, namesOf(kKey));
      keyBox.setAccent(kOrange);
      keyBox.onChange = [this](int k) { setKey(k); };
      keyBox_ = &keyBox;
      tips_.add(keyBox, "The key: the progressions move with it.");
      auto &sc = onPage<ui::SegmentedSwitch>(kChordsPage, {108, 118, 196, 22},
                                             std::vector<std::string>{"MAJ", "MIN", "DOR", "MIXO", "BLUES"});
      sc.setAccent(kOrange);
      bindings_.bind(sc, kScale);
      tips_.add(sc, "The scale runs walk, and that names the Roman numerals' home.");

      auto &from = onPage<ui::SegmentedSwitch>(kChordsPage, {334, 104, 316, 22}, namesOf(kChordsFrom));
      from.setAccent(kOrange);
      bindings_.bind(from, kChordsFrom);
      from.onChange = [this, old = from.onChange](int i) {
         if (old)
            old(i);
         redraw();
      };
      tips_.add(from, "Where the chords come from: each section's progression, the chords you hold, or both (holding a "
                      "chord takes over; let go and the progression picks up).");
      auto &held = onPage<ui::SegmentedSwitch>(kChordsPage, {334, 170, 316, 20}, namesOf(kVoiceHeld));
      held.setAccent(kOrange);
      bindings_.bind(held, kVoiceHeld);
      tips_.add(held, "A held chord: voiced JComp's way, played as you hold it, or voiced under your top note.");
      auto &at = onPage<ui::SegmentedSwitch>(kChordsPage, {670, 116, 304, 22}, namesOf(kChangeAt));
      at.setAccent(kOrange);
      bindings_.bind(at, kChangeAt);
      tips_.add(at, "When a newly held chord takes over: at once, or on the next sixteenth, beat or bar.");
      latch_ = &onPage<CheckBox>(kChordsPage, {670, 146, 200, 18}, "Latch: keep the last chord");
      latch_->onToggle = [this](bool on) { setParam(kLatch, on ? 1.0 : 0.0); };
      slash_ = &onPage<CheckBox>(kChordsPage, {670, 166, 230, 18}, "Lowest held note is the bass (slash chords)");
      slash_->onToggle = [this](bool on) { setParam(kSlashBass, on ? 1.0 : 0.0); };
      auto &split = onPage<ui::NumberBox>(kChordsPage, {898, 146, 76, 18}, rangeOf(kChordSplit, 1.0));
      split.setStepButtons(true);
      split.valueText = [](double v) { return noteText(static_cast<int>(std::lround(v))); };
      bindings_.bind(split, kChordSplit);
      tips_.add(split, "Chords are read from this note up; below it are the section notes.");

      rows_ = &onPage<ProgressionRows>(kChordsPage, {26, 236, 592, ProgressionRows::kRowH * kSections}, host_);
      rows_->transpose = [this] { return transpose(); };
      rows_->scale = [this] { return scaleNow(); };
      rows_->onPick = [this](int section, int slot) {
         host_.chordSection = section;
         host_.chordSlot = slot;
         redraw();
      };
      rows_->onMore = [this](int section, visage::Point at) { rowMenu(section, at); };
      tips_.add(*rows_, "Click a chord to change it on the right. \xE2\x80\xA6 sets the row's length, splits a bar, or copies "
                        "another section's chords.");

      roots_ = &onPage<Chips>(kChordsPage, {652, 316, 324, 50});
      roots_->perRow = 6;
      for (int i = 0; i < 12; ++i)
         roots_->items.push_back(kSharpNames[i]);
      roots_->onPick = [this](int r) {
         editChord([&](Chord &ch) {
            const int was = ch.root;
            ch.root = static_cast<int8_t>(r);
            if (ch.bass >= 0 && was >= 0)
               ch.bass = static_cast<int8_t>((ch.bass + r - was + 12) % 12);
         });
      };
      qualities_ = &onPage<Chips>(kChordsPage, {652, 386, 324, 50});
      qualities_->perRow = 7;
      for (int q : kQualityChips)
         qualities_->items.push_back(q == kMaj ? "maj" : qualities()[static_cast<size_t>(q)].suffix);
      qualities_->onPick = [this](int i) {
         editChord([&](Chord &ch) {
            if (ch.none())
               ch.root = static_cast<int8_t>(key());
            ch.quality = static_cast<uint8_t>(kQualityChips[i]);
         });
      };
      bass_ = &onPage<ui::Dropdown>(kChordsPage, {652, 454, 110, 20});
      bass_->setAccent(kOrange);
      bass_->onChange = [this](int i) {
         editChord([&](Chord &ch) { ch.bass = static_cast<int8_t>(i == 0 ? -1 : i - 1); });
      };
      lasts_ = &onPage<ui::SegmentedSwitch>(kChordsPage, {776, 454, 200, 20}, std::vector<std::string>{"THE BAR", "HALF"});
      lasts_->setAccent(kOrange);
      lasts_->onChange = [this](int i) { setSplit(host_.chordSection, host_.chordSlot / 2, i == 1); };
      tips_.add(*lasts_, "The bar's chord lasts the whole bar, or the bar splits in two halves with a chord each.");
      mark_ = &onPage<ui::SegmentedSwitch>(kChordsPage, {824, 248, 152, 18},
                                           std::vector<std::string>{"COMP", "ACC", "HOLD", "SHOT"});
      mark_->setAccent(kOrange);
      mark_->onChange = [this](int i) { setMark(i); };
      tips_.add(*mark_, "A band hit on this chord: COMP as the take plays it; ACCENT its first stroke; HOLD struck once and "
                        "held to the next chord; SHOT struck once, short. The take rests under a hold or a shot.");
      voicing_ = &onPage<ChordVoicing>(kChordsPage, {652, 498, 324, 46});
      instead_ = &onPage<Chips>(kChordsPage, {652, 566, 324, 22});
      instead_->colour = kPurple;
      instead_->size = 8.5f;
      instead_->onPick = [this](int i) {
         if (i < static_cast<int>(insteadChords_.size())) {
            const Chord pickC = moved(insteadChords_[static_cast<size_t>(i)], -transpose());
            editChord([&](Chord &ch) { ch = pickC; });
         }
      };
      tips_.add(*instead_, "Chords that could stand in here: click one to use it. WEIRD reaches for these on a roll.");
   }

   void setKey(int k) {
      // The song's chords move with the key.
      Song s = host_.song();
      const int t = ((k - s.key) % 12 + 12) % 12;
      for (Section &sec : s.sections)
         for (Chord &ch : sec.chords)
            ch = moved(ch, t);
      s.key = static_cast<uint8_t>(k);
      setParam(kKey, k);
      host_.setSong(s);
      redraw();
   }

   void editChord(const std::function<void(Chord &)> &f) {
      Song s = host_.song();
      Section &sec = s.sections[static_cast<size_t>(host_.chordSection)];
      const int slot = std::clamp(host_.chordSlot, 0, kSlots - 1);
      const int bar = slot / 2;
      Chord ch = moved(sec.chords[static_cast<size_t>(slot)], transpose());
      f(ch);
      ch = moved(ch, -transpose());
      sec.chords[static_cast<size_t>(slot)] = ch;
      if (!(sec.split >> bar & 1))
         sec.chords[static_cast<size_t>(bar * 2 + 1)] = ch;
      host_.setSong(s);
      redraw();
   }

   void setMark(int mark) {
      Song s = host_.song();
      Section &sec = s.sections[static_cast<size_t>(host_.chordSection)];
      const int slot = std::clamp(host_.chordSlot, 0, kSlots - 1);
      const int bar = slot / 2;
      const bool split = sec.split >> bar & 1;
      sec.marks[static_cast<size_t>(split ? slot : bar * 2)] = static_cast<uint8_t>(std::clamp(mark, 0, 3));
      if (!split)
         sec.marks[static_cast<size_t>(bar * 2 + 1)] = kMarkNone;
      host_.setSong(s);
      redraw();
   }

   void setSplit(int section, int bar, bool on) {
      Song s = host_.song();
      Section &sec = s.sections[static_cast<size_t>(section)];
      if (on)
         sec.split = static_cast<uint16_t>(sec.split | (1u << bar));
      else {
         sec.split = static_cast<uint16_t>(sec.split & ~(1u << bar));
         sec.chords[static_cast<size_t>(bar * 2 + 1)] = sec.chords[static_cast<size_t>(bar * 2)];
         host_.chordSlot = bar * 2;
      }
      host_.setSong(s);
      redraw();
   }

   void rowMenu(int section, visage::Point at) {
      visage::PopupMenu m;
      for (int b : {1, 2, 4, 8, 12})
         m.addOption(b, std::to_string(b) + (b == 1 ? " bar" : " bars"));
      m.addBreak();
      for (int i = 0; i < kSections; ++i)
         if (i != section)
            m.addOption(100 + i, std::string("Copy ") + kSectionNames[i] + "'s chords");
      m.addBreak();
      m.addOption(200, "Back to the style's progression");
      m.onSelection() += [this, section](int id) {
         Song s = host_.song();
         Section &sec = s.sections[static_cast<size_t>(section)];
         if (id >= 1 && id <= kMaxProgBars) {
            const int was = sec.progBars;
            for (int b = was; b < id; ++b) {
               sec.chords[static_cast<size_t>(b * 2)] = sec.chords[static_cast<size_t>((b % was) * 2)];
               sec.chords[static_cast<size_t>(b * 2 + 1)] = sec.chords[static_cast<size_t>((b % was) * 2 + 1)];
               if (sec.split >> (b % was) & 1)
                  sec.split = static_cast<uint16_t>(sec.split | (1u << b));
            }
            sec.progBars = static_cast<uint8_t>(id);
            if (sec.bars % id)
               sec.bars = static_cast<uint8_t>(std::min(kMaxBars, (sec.bars / id + 1) * id));
         } else if (id >= 100 && id < 100 + kSections) {
            const Section &from = s.sections[static_cast<size_t>(id - 100)];
            sec.chords = from.chords;
            sec.split = from.split;
            sec.progBars = from.progBars;
         } else if (id == 200) {
            Rng rng(sec.seed);
            Recipe r = recipe();
            r.key = s.key;
            makeChords(r, section, sec, rng);
         }
         host_.chordSlot = std::min(host_.chordSlot, sec.progBars * 2 - 1);
         host_.setSong(s);
         redraw();
      };
      m.show(rows_, at);
   }

   void drawChords(visage::Canvas &c, float s) {
      drawCard(c, s, 12, 76, 300, 124, "KEY");
      label(c, s, "KEY", 26, 100, 60, 0, visage::Font::kLeft, 9.5f);
      label(c, s, "SCALE", 108, 100, 60, 0, visage::Font::kLeft, 9.5f);
      const std::string sc = std::string(kKeyNames[key()]) + " " + upper(kScaleNames[std::clamp(scaleNow(), 0, kScaleCount - 1)]);
      label(c, s, "Runs and suggestions walk " + sc + ".", 26, 150, 280, 0, visage::Font::kLeft, 9);
      label(c, s, "A chord you type can leave the key; JComp follows it.", 26, 166, 280, 0, visage::Font::kLeft, 9);
      char note[160];
      std::snprintf(note, sizeof(note), "channel any \xC2\xB7 notes %s and up (below are the section notes)",
                    noteText(whole(kChordSplit)).c_str());
      drawCard(c, s, 320, 76, 668, 124, "CHORDS FROM", note);
      static const char *const words[3] = {"Each section plays its own progression.",
                                           "Hold a chord: JComp comps it in the section's rhythm.",
                                           "The progression plays; a held chord takes over until you let go."};
      label(c, s, words[std::clamp(whole(kChordsFrom), 0, 2)], 334, 132, 320, 0, visage::Font::kLeft, 8.5f);
      label(c, s, "VOICE WHAT I HOLD", 334, 156, 200, 0, visage::Font::kLeft, 9);
      label(c, s, "A NEW CHORD STARTS", 670, 100, 200, 0, visage::Font::kLeft, 9.5f);
      latch_->on = whole(kLatch) != 0;
      slash_->on = whole(kSlashBass) != 0;
      keyBox_->setSelected(key());

      drawCard(c, s, 12, 208, 620, 400, "PROGRESSION",
               "a loop per section \xC2\xB7 click a chord to change it \xC2\xB7 \xE2\x80\xA6 for length and copies");
      // The chord picked.
      const Section &sec = host_.song().sections[static_cast<size_t>(host_.chordSection)];
      host_.chordSlot = std::clamp(host_.chordSlot, 0, sec.progBars * 2 - 1);
      const int bar = host_.chordSlot / 2;
      const bool split = sec.split >> bar & 1;
      const int slot = split ? host_.chordSlot : bar * 2;
      const Chord ch = moved(sec.chords[static_cast<size_t>(slot)], transpose());
      std::string title = std::string(kSectionCaps[host_.chordSection]) + " \xC2\xB7 BAR " + std::to_string(bar + 1);
      drawCard(c, s, 640, 208, 348, 400, title, split ? (slot % 2 ? "the second half" : "the first half") : "the whole bar");
      label(c, s, chordName(ch, key(), scaleNow()), 652, 230, 170, kWhite, visage::Font::kLeft, 34);
      label(c, s, "BAND HIT", 824, 232, 152, 0, visage::Font::kLeft, 9);
      mark_->setSelected(sec.marks[static_cast<size_t>(slot)]);
      label(c, s, roman(ch, key()) + " \xC2\xB7 " + qualityWords(ch.quality), 652, 276, 320, 0, visage::Font::kLeft, 9.5f);
      label(c, s, "ROOT", 652, 300, 60, 0, visage::Font::kLeft, 9.5f);
      label(c, s, "CHORD", 652, 370, 60, 0, visage::Font::kLeft, 9.5f);
      label(c, s, "BASS", 652, 438, 60, 0, visage::Font::kLeft, 9.5f);
      label(c, s, "LASTS", 776, 438, 60, 0, visage::Font::kLeft, 9.5f);
      label(c, s, "AS JCOMP VOICES IT", 652, 482, 200, 0, visage::Font::kLeft, 9.5f);
      label(c, s, "TRY INSTEAD", 652, 550, 200, 0, visage::Font::kLeft, 9.5f);
      roots_->selected = ch.none() ? -1 : ch.root;
      qualities_->selected = -1;
      for (int i = 0; i < static_cast<int>(std::size(kQualityChips)); ++i)
         if (kQualityChips[i] == ch.quality)
            qualities_->selected = i;
      // Bass: the root, or any note.
      std::vector<std::string> basses{std::string(ch.none() ? "-" : pitchName(ch.root, key(), scaleNow())) + " (root)"};
      for (int i = 0; i < 12; ++i)
         basses.push_back(pitchName(i, key(), scaleNow()));
      if (basses != bass_->options())
         bass_->setOptions(basses);
      bass_->setSelected(ch.bass < 0 ? 0 : ch.bass + 1);
      lasts_->setSelected(split ? 1 : 0);
      // Voiced from the chord before it in the progression.
      const VoiceSettings v = vs();
      Voicing prev;
      Chord before;
      const int start = (slot - 4 + kSlots * 4) % 2;
      (void)start;
      for (int k = std::max(0, slot - 4); k < slot; ++k) {
         const Chord b = moved(sec.chords[static_cast<size_t>(k)], transpose());
         if (b.none() || (k % 2 == 1 && !(sec.split >> (k / 2) & 1)))
            continue;
         prev = voice(b, prev.empty() ? nullptr : &prev, v, -1, before.none() ? nullptr : &before);
         before = b;
      }
      voicing_->v = ch.none() ? Voicing{} : voice(ch, prev.empty() ? nullptr : &prev, v, -1, before.none() ? nullptr : &before);
      std::string how = upper(kShapeNames[v.shape]);
      if (!prev.empty() && !voicing_->v.empty()) {
         const int d = voicing_->v.top() - prev.top();
         how += ", top " + std::string(d >= 0 ? "+" : "") + std::to_string(d) + " from " + noteText(prev.top());
      }
      voicing_->note = how;
      // TRY INSTEAD.
      const int nextSlot = split && slot % 2 == 0 ? slot + 1 : ((bar + 1) % sec.progBars) * 2;
      const Chord next = moved(sec.chords[static_cast<size_t>(nextSlot)], transpose());
      insteadChords_.clear();
      instead_->items.clear();
      for (const auto &[alt, why] : tryInstead(ch, next)) {
         if (insteadChords_.size() >= 4)
            break;
         insteadChords_.push_back(alt);
         instead_->items.push_back(chordName(alt, key(), scaleNow()));
      }
      (void)before;
      label(c, s, "Hold a chord to hear it; chords you hold show in PLAY.", 12, 618, 976, 0, visage::Font::kRight, 8.5f);
   }

   // ---- EDIT ----

   void buildEdit() {
      std::vector<std::string> sections;
      for (const char *n : kSectionCaps)
         sections.push_back(n);
      editSection_ = &onPage<ui::SegmentedSwitch>(kEditPage, {74, 80, 404, 24}, sections);
      editSection_->setAccent(kOrange);
      editSection_->onChange = [this](int i) {
         host_.editSection = i;
         host_.editStep = -1;
         redraw();
      };
      editTake_ = &onPage<ui::SegmentedSwitch>(kEditPage, {522, 80, 256, 24},
                                               std::vector<std::string>{"1", "2", "3", "4", "FILL", "DOWN", "END"});
      editTake_->setAccent(kOrange);
      editTake_->onChange = [this](int i) {
         host_.editTake = i;
         host_.editStep = -1;
         redraw();
      };
      tips_.add(*editTake_, "The section's four takes, its fill, its fill DOWN into a quieter section (the intro's: its "
                            "held chord setting up the verse) and the song's ENDING (the outro's).");
      auto &dice = onPage<FlatButton>(kEditPage, {786, 80, 202, 24}, "MUTATE THIS TAKE", 9.5f);
      dice.dice = true;
      dice.onClick = [this](const visage::MouseEvent &) {
         Song s = host_.song();
         Pattern &p = TakeRoll::pick(s, host_.editSection, host_.editTake);
         Rng rng(rng_());
         std::string tag;
         p = mutate(p, param(kMutation), rng, tag, styleOf(recipe()).mods);
         host_.setSong(s);
         diceTag_ = tag;
         redraw();
      };
      tips_.add(dice, "Mutates this take once, by MUTATION. Again for another.");
      roll_ = &onPage<TakeRoll>(kEditPage, {26, 140, 672, 296}, host_);
      wire(*roll_);
      roll_->onChange = [this] {
         diceTag_.clear();
         redraw();
      };
      roll_->onPick = [this] { redraw(); };
      tips_.add(*roll_, "Click an empty step to add a hit, click a hit to pick it, drag it sideways to move it, "
                        "double-click to take it away. The notes are voiced as they will play.");
      lanes_ = &onPage<HitLanes>(kEditPage, {26, 466, 672, HitLanes::kRowH * 4}, host_);
      lanes_->onChange = [this] { redraw(); };
      tips_.add(*lanes_, "Drag a bar for each hit's velocity, length (down to a chop, up to ring), chance and nudge; "
                         "double-click for its default.");

      const float vx = 734;
      shape1_ = &onPage<ui::SegmentedSwitch>(kEditPage, {vx, 140, 240, 22},
                                             std::vector<std::string>{"CLS", "DR2", "2+4", "SHL", "4TH", "SPR", "RTL", "8VE", "GRIP"});
      shape1_->setAccent(kOrange);
      shape1_->onChange = [this](int i) {
         setParam(kShape, i);
         redraw();
      };
      tips_.add(*shape1_, "The voicing's shape: close, drop 2 (the second voice from the top down an octave), drop 2+4, "
                          "shells (3rd and 7th), fourths, spread over two hands, rootless (Levine's A and B forms), "
                          "octaves (a close chord with its bottom note doubled on top), or a guitar's grip (the "
                          "bass, its fifth and octave, the chord above; the power chord).");
      auto knob = [this](Page page, ui::Rect r, clap_id id, const char *name, unsigned accent) -> ui::Knob & {
         auto &k = onPage<ui::Knob>(page, r, rangeOf(id));
         k.setLabel(name);
         k.setAccent(accent);
         k.valueText = [id](double v) {
            char b[32];
            formatParam(id, v, b, sizeof(b));
            return std::string(b);
         };
         bindings_.bind(k, id);
         k.onChange = [this, id, old = k.onChange](double v) {
            if (old)
               old(v);
            if (id == kCenter || id == kLead || id == kTopMove || id == kMaxSpan || id == kRangeLow || id == kRangeHigh ||
                id == kOffset || id == kFloor)
               redraw();
         };
         return k;
      };
      tips_.add(knob(kEditPage, {vx - 2, 164, 58, 70}, kCenter, "CENTER", kOrange), "Where the voicings sit: their middle is "
                                                                                    "pulled to this note (OFFSET moves it too).");
      tips_.add(knob(kEditPage, {vx + 60, 164, 58, 70}, kLead, "LEAD", kOrange),
                "How much each chord is led from the one before (the least movement) rather than placed in the middle.");
      tips_.add(knob(kEditPage, {vx + 122, 164, 58, 70}, kTopMove, "TOP MOVE", kOrange),
                "The most the top voice should move from chord to chord, in semitones.");
      tips_.add(knob(kEditPage, {vx + 184, 164, 58, 70}, kMaxSpan, "SPREAD", kOrange), "The widest a voicing may be, bottom to "
                                                                                       "top.");
      tips_.add(knob(kEditPage, {vx - 2, 236, 58, 70}, kRangeLow, "LOWEST", kBlue),
                "The lowest note a voicing may use. With HIGHEST it sets the window chords are led inside.");
      tips_.add(knob(kEditPage, {vx + 60, 236, 58, 70}, kRangeHigh, "HIGHEST", kBlue),
                "The highest note a voicing may use (the window is at least an octave).");
      tips_.add(knob(kEditPage, {vx + 122, 236, 58, 70}, kOffset, "OFFSET", kBlue),
                "Slides the whole window (and CENTER) up or down, up to an octave. Automate it and the voicings follow, "
                "still led from chord to chord.");
      tips_.add(knob(kEditPage, {vx + 184, 236, 58, 70}, kFloor, "FLOOR", kBlue), "Nothing goes under this note, even the "
                                                                                    "bass part.");
      range_ = &onPage<RangeKeys>(kEditPage, {vx, 324, 240, 18}, host_);
      range_->onSet = [this](clap_id id, double v) {
         setParam(id, v);
         redraw();
      };
      tips_.add(*range_, "The window voicings stay in: click near either end to move it. Green: nothing under this note.");
      const clap_id boxes[3] = {kDoubleMiddle, kNoRubs, kSameShape};
      const char *names[3] = {"Double a note in the middle, not on top", "No half-step rubs below C4",
                              "Same shape for repeated chords"};
      for (int i = 0; i < 3; ++i) {
         auto &b = onPage<CheckBox>(kEditPage, {vx, 348 + i * 16.0f, 240, 15}, names[i]);
         const clap_id id = boxes[i];
         b.onToggle = [this, id](bool on) {
            setParam(id, on ? 1.0 : 0.0);
            redraw();
         };
         voiceChecks_[i] = &b;
      }

      hitKeys_ = &onPage<ChordVoicing>(kEditPage, {vx, 428, 240, 44});
      art_ = &onPage<ui::SegmentedSwitch>(kEditPage, {vx, 494, 240, 20},
                                          std::vector<std::string>{"AS IS", "CHOP", "HALF", "RING", "TOP"});
      art_->setAccent(kOrange);
      art_->onChange = [this](int i) { editHit([&](Hit &h) { h.art = static_cast<uint8_t>(i); }); };
      tips_.add(*art_, "How long the hit's notes sound: as written, chopped short, half, ringing to the next chord, or only "
                       "the top note ringing.");
      runIn_ = &onPage<CheckBox>(kEditPage, {vx, 520, 240, 16}, "Run into it");
      runIn_->onToggle = [this](bool on) { editHit([&](Hit &h) { h.run = on; }); };
      push_ = &onPage<CheckBox>(kEditPage, {vx, 538, 240, 16}, "Anticipate: plays the next chord early");
      push_->onToggle = [this](bool on) { editHit([&](Hit &h) { h.push = on; }); };
      part_ = &onPage<ui::SegmentedSwitch>(kEditPage, {vx, 558, 240, 18}, std::vector<std::string>{"CHORD", "TOP", "TOP 2", "BASS", "5TH", "LINE"});
      part_->setAccent(kOrange);
      part_->onChange = [this](int i) { editHit([&](Hit &h) { h.part = static_cast<uint8_t>(i); }); };
      tips_.add(*part_, "What of the chord the hit plays: all of it, the top note, the top two, the bass note, the fifth "
                       "the bass alternates with, or one note of a line (the degree of the chord the style wrote, the "
                       "root if none).");
      auto &revoice = onPage<FlatButton>(kEditPage, {vx, 582, 117, 20}, "RE-VOICE \xE2\x96\xB8", 9);
      revoice.onClick = [this](const visage::MouseEvent &) {
         editHit([&](Hit &h) { h.voicing = static_cast<int8_t>(h.voicing >= 4 ? -1 : h.voicing + 1); });
      };
      tips_.add(revoice, "Another voicing for this hit: the next best of JComp's choices, round to its own again.");
      auto &mutateHit = onPage<FlatButton>(kEditPage, {vx + 123, 582, 117, 20}, "+ MUTATE HIT", 9);
      mutateHit.onClick = [this](const visage::MouseEvent &) {
         std::mt19937 &r = rng_;
         editHit([&](Hit &h) {
            switch (r() % 4) {
            case 0: h.art = static_cast<uint8_t>(r() % 5); break;
            case 1: h.vel = static_cast<uint8_t>(std::clamp<int>(h.vel + static_cast<int>(r() % 41) - 20, 30, 127)); break;
            case 2: h.push = !h.push; break;
            default: h.part = static_cast<uint8_t>(r() % 3); break;
            }
         });
      };
   }

   void editHit(const std::function<void(Hit &)> &f) {
      if (host_.editStep < 0)
         return;
      Song s = host_.song();
      Pattern &p = TakeRoll::pick(s, host_.editSection, host_.editTake);
      if (host_.editStep >= p.steps || !p.at(host_.editStep).on())
         return;
      f(p.at(host_.editStep));
      host_.setSong(s);
      redraw();
   }

   void drawEdit(visage::Canvas &c, float s) {
      label(c, s, "SECTION", 14, 84, 60, 0, visage::Font::kLeft, 10);
      label(c, s, "TAKE", 486, 84, 36, 0, visage::Font::kLeft, 10);
      editSection_->setSelected(host_.editSection);
      editTake_->setSelected(host_.editTake);
      const Section &sec = host_.song().sections[static_cast<size_t>(host_.editSection)];
      const Pattern &p = TakeRoll::pick(const_cast<Song &>(host_.song()), host_.editSection, host_.editTake);
      const int bars = viewBars(sec, p);
      static const char *const extra[3] = {"FILL", "FILL DOWN", "ENDING"};
      std::string title = std::string(kSectionCaps[host_.editSection]) + " \xC2\xB7 " +
                          (host_.editTake >= kTakes ? std::string(extra[std::min(2, host_.editTake - kTakes)])
                                                    : "TAKE " + std::to_string(host_.editTake + 1));
      char note[160];
      if (host_.editTake == 6 && host_.editSection != kOutro)
         std::snprintf(note, sizeof(note), "only the outro's ending plays: the song's last bars, in SONG");
      else
         std::snprintf(note, sizeof(note), "%d bar%s%s \xC2\xB7 drag a hit to move it, its chord follows the bar",
                       p.bars(), p.bars() == 1 ? "" : "s", bars > p.bars() ? ", played round" : "");
      drawCard(c, s, 12, 112, 700, 334, title, diceTag_.empty() ? note : "mutated: " + diceTag_);
      drawCard(c, s, 12, 454, 700, 154, "");
      drawCard(c, s, 720, 112, 268, 288, "VOICING", "all sections");
      const int shape = std::clamp(whole(kShape), 0, kShapeCount - 1);
      shape1_->setSelected(shape);
      {
         const VoiceSettings v = vs();
         label(c, s, "WINDOW", 734, 309, 60, 0, visage::Font::kLeft, 9.5f);
         label(c, s, noteText(v.lo) + " \xE2\x80\x93 " + noteText(v.hi) + " \xC2\xB7 nothing under " + noteText(v.floor),
               734, 309, 240, 0, visage::Font::kRight, 8.5f);
      }
      const clap_id boxes[3] = {kDoubleMiddle, kNoRubs, kSameShape};
      for (int i = 0; i < 3; ++i)
         voiceChecks_[i]->on = whole(boxes[i]) != 0;
      // The hit picked.
      const bool has = host_.editStep >= 0 && host_.editStep < p.steps && p.at(host_.editStep).on();
      const int perBar = p.stepsPerBar(), perBeat = p.perBeat();
      std::string hitTitle = "THE HIT";
      Chord hitChord;
      Voicing hitVoicing;
      std::string led;
      if (has) {
         hitTitle = "BAR " + std::to_string(host_.editStep / perBar + 1) + " \xC2\xB7 BEAT " +
                    std::to_string(host_.editStep % perBar / perBeat + 1);
         if (host_.editStep % perBeat)
            hitTitle += perBeat == 4 ? (host_.editStep % perBeat == 2 ? " &" : host_.editStep % perBeat == 1 ? " e" : " a")
                                     : (host_.editStep % perBeat == 1 ? " trip" : " let");
         const Preview pv = preview(sec, p, bars, vs(), transpose(), key(), scaleNow(), host_.paramValue(kRuns) > 0.0);
         for (const PlacedHit &pl : pv.hits)
            if (pl.view == host_.editStep) {
               hitChord = pl.chord;
               hitVoicing = pl.voicing;
               if (!pl.before.none())
                  led = "led from " + chordName(pl.before, key(), scaleNow());
               if (pl.fromTop >= 0 && !pl.voicing.empty()) {
                  const int d = pl.voicing.top() - pl.fromTop;
                  led += ", top " + std::string(d >= 0 ? "+" : "") + std::to_string(d);
               }
            }
      }
      drawCard(c, s, 720, 408, 268, 200, hitTitle, has ? chordName(hitChord, key(), scaleNow()) : "click a hit");
      hitKeys_->v = hitVoicing;
      hitKeys_->note = led;
      const Hit h = has ? p.at(host_.editStep) : Hit{};
      art_->setSelected(has ? h.art : -1);
      part_->setSelected(has ? h.part : -1);
      runIn_->on = has && h.run;
      push_->on = has && h.push;
      runIn_->setText(std::string("Run into it: ") + (has ? std::to_string(3) + " steps of " + kKeyNames[key()] + " " +
                                                                  kScaleNames[std::clamp(scaleNow(), 0, kScaleCount - 1)]
                                                           : std::string("on a change")));
      label(c, s, "LENGTH", 734, 478, 60, 0, visage::Font::kLeft, 9);
   }

   // ---- PLAY ----

   void buildPlay() {
      tiles_ = &onPage<SectionTiles>(kPlayPage, {26, 108, 948, 150}, host_);
      wire(*tiles_);
      tiles_->onAction = [this](int a) {
         // In SONG the Section param is where the song starts: a click goes to the section (as a
         // section note) without moving the song's start, unless the notes are switched off.
         if (a <= kActOutro && whole(kSwitchBy) != 1 && (whole(kSongMode) == 0 || whole(kSwitchBy) == 2))
            setParam(kSection, a);
         else if (a >= kActTake1 && a <= kActTake4 && whole(kSwitchBy) != 1)
            setParam(kTake, a - kActTake1);
         // Played as a section note too: a click on the section the param already holds (the song
         // having moved on from it) still goes there.
         host_.request(a);
      };
      tips_.add(*tiles_, "Click a section to switch to it (at SWITCH AT), a number for that take, FILL for a fill.");
      auto &songMode = onPage<ui::SegmentedSwitch>(kPlayPage, {300, 84, 168, 20},
                                                   std::vector<std::string>{"LOOP", "SONG", "REPEAT"});
      songMode.setAccent(kOrange);
      bindings_.bind(songMode, kSongMode);
      tips_.add(songMode, "LOOP stays in a section; SONG plays each for its bars, then the next, and ends with the "
                          "outro's ending; REPEAT plays the song round and round.");
      auto &switchAt = onPage<ui::Dropdown>(kPlayPage, {554, 84, 112, 20}, namesOf(kSwitchAt));
      bindings_.bind(switchAt, kSwitchAt);
      auto &by = onPage<ui::Dropdown>(kPlayPage, {700, 84, 120, 20}, namesOf(kSwitchBy));
      bindings_.bind(by, kSwitchBy);
      tips_.add(by, "What switches sections: a note, the Section parameter (and a click here), or both.");
      notesButton_ = &onPage<FlatButton>(kPlayPage, {870, 82, 104, 24}, "NOTES & MORE", 9);
      notesButton_->onClick = [this](const visage::MouseEvent &) {
         host_.notesOpen = !host_.notesOpen;
         showOverlays();
         redraw();
      };

      now_ = &onPage<NowPlaying>(kPlayPage, {26, 298, 456, 162}, host_);
      wire(*now_);

      auto knob = [this](ui::Rect r, clap_id id, const char *name, unsigned accent) -> ui::Knob & {
         auto &k = onPage<ui::Knob>(kPlayPage, r, rangeOf(id));
         k.setLabel(name);
         k.setAccent(accent);
         k.valueText = [id](double v) {
            char b[32];
            formatParam(id, v, b, sizeof(b));
            return std::string(b);
         };
         bindings_.bind(k, id);
         return k;
      };
      tips_.add(knob({492, 300, 64, 80}, kDensity, "DENSITY", kOrange),
                "Fewer or more chords: below the middle the weakest hits drop out (each bar's first hit stays), above it "
                "softer repeats of the chord fill the gaps, eighths first. The middle plays the take as written.");
      tips_.add(knob({558, 300, 64, 80}, kLength, "LENGTH", kOrange),
                "Every chord shorter (down to a chop) or longer (up to legato, held to the next). The middle is as "
                "written.");
      tips_.add(knob({650, 292, 64, 80}, kTiming, "TIMING", kPurple), "How far behind (or ahead of) the beat the chords sit.");
      tips_.add(knob({716, 292, 64, 80}, kVelocityVar, "VELOCITY", kPurple), "How much each chord's loudness varies.");
      tips_.add(knob({782, 292, 64, 80}, kTopVoice, "TOP VOICE", kPurple), "The top note this much louder, so the line "
                                                                            "sings.");
      tips_.add(knob({848, 292, 64, 80}, kLengths, "LENGTHS", kPurple), "How much each chord's length varies.");
      tips_.add(knob({914, 292, 64, 80}, kSwing, "SWING", kPurple), "Every second sixteenth (or eighth) late.");
      auto &onsets = onPage<ui::SegmentedSwitch>(kPlayPage, {654, 392, 320, 20}, namesOf(kOnsets));
      onsets.setAccent(kOrange);
      bindings_.bind(onsets, kOnsets);
      onsets.onChange = [this, old = onsets.onChange](int i) {
         if (old)
            old(i);
         redraw();
      };
      auto &dice = onPage<ui::SegmentedSwitch>(kPlayPage, {760, 432, 214, 16},
                                               std::vector<std::string>{"NEVER", "1", "2", "4 BARS", "8"});
      dice.setAccent(kOrange);
      bindings_.bind(dice, kSameDice);
      tips_.add(dice, "The dice repeat every so many bars, so the chance hits make a pattern of their own.");
      auto &fills = onPage<ui::SegmentedSwitch>(kPlayPage, {760, 451, 214, 16},
                                                std::vector<std::string>{"OFF", "EVERY 4", "EVERY 8", "16"});
      fills.setAccent(kOrange);
      bindings_.bind(fills, kAutoFill);

      transport_ = &onPage<TransportView>(kPlayPage, {26, 506, 220, 60});
      transport_->onPlay = [this] { togglePlay(); };
      auto &sync = onPage<ui::SegmentedSwitch>(kPlayPage, {106, 578, 140, 20}, std::vector<std::string>{"AUTO", "HOST", "FREE"});
      sync.setAccent(kOrange);
      bindings_.bind(sync, kSync);
      tips_.add(sync, "AUTO follows the host while it plays, and runs free on PLAY while it's stopped.");
      auto &tempo = onPage<ui::NumberBox>(kPlayPage, {26, 578, 72, 20}, rangeOf(kFreeTempo, 1.0));
      tempo.setStepButtons(true);
      tempo.valueText = [](double v) { return std::to_string(static_cast<int>(std::lround(v))); };
      bindings_.bind(tempo, kFreeTempo);
      tips_.add(tempo, "The tempo when running free.");

      tips_.add(knob({272, 504, 64, 84}, kRuns, "AMOUNT", kYellow), "How many of the chances for a run into a change are "
                                                                     "taken.");
      auto &every = onPage<ui::SegmentedSwitch>(kPlayPage, {346, 522, 276, 20}, namesOf(kRunEvery));
      every.setAccent(kOrange);
      bindings_.bind(every, kRunEvery);
      auto &land = onPage<ui::SegmentedSwitch>(kPlayPage, {346, 566, 130, 20}, std::vector<std::string>{"TOP NOTE", "ANY NOTE"});
      land.setAccent(kOrange);
      bindings_.bind(land, kRunLand);
      tips_.add(land, "A run lands on the new chord's top note, or the nearest note of it.");
      half_ = &onPage<CheckBox>(kPlayPage, {486, 558, 140, 16}, "Half step from below");
      half_->onToggle = [this](bool on) { setParam(kRunHalfStep, on ? 1.0 : 0.0); };
      shorten_ = &onPage<CheckBox>(kPlayPage, {486, 578, 140, 16}, "Shorten the chord before");
      shorten_->onToggle = [this](bool on) { setParam(kRunShorten, on ? 1.0 : 0.0); };

      auto &channel = onPage<ui::NumberBox>(kPlayPage, {654, 514, 70, 20}, rangeOf(kChannel, 1.0));
      channel.setStepButtons(true);
      channel.valueText = [](double v) { return std::to_string(static_cast<int>(std::lround(v))); };
      bindings_.bind(channel, kChannel);
      auto &from = onPage<ui::SegmentedSwitch>(kPlayPage, {740, 514, 234, 20}, std::vector<std::string>{"OWN", "THE BASS", "BOTH"});
      from.setAccent(kOrange);
      bindings_.bind(from, kRhythmFrom);
      from.onChange = [this, old = from.onChange](int i) {
         if (old)
            old(i);
         redraw();
      };
      tips_.add(from, "The rhythm: the take's own, the bass line coming in on BASS CH (a chord where it plays), or both.");
      bassCh_ = &onPage<ui::NumberBox>(kPlayPage, {654, 544, 70, 18}, rangeOf(kBassChannel, 1.0));
      bassCh_->setStepButtons(true);
      bassCh_->valueText = [](double v) { return "BASS " + std::to_string(static_cast<int>(std::lround(v))); };
      bindings_.bind(*bassCh_, kBassChannel);
      tips_.add(*bassCh_, "The channel the bass comes in on, for RHYTHM FROM THE BASS.");
      follow_ = &onPage<ui::SegmentedSwitch>(kPlayPage, {740, 544, 234, 18}, std::vector<std::string>{"ALL", "STRONG", "ONE A BAR"});
      follow_->setAccent(kOrange);
      bindings_.bind(*follow_, kFollow);
      tips_.add(*follow_, "Which bass notes get a chord: all of them, the strong ones (accented, on the beat, leaps), or "
                          "one a bar.");
      outKeys_ = &onPage<OutKeys>(kPlayPage, {654, 570, 320, 32});
   }

   static std::vector<std::string> namesOf(clap_id id) {
      int count = 0;
      const char *const *names = choiceNames(id, count);
      std::vector<std::string> out;
      for (int i = 0; i < count; ++i)
         out.push_back(upper(names[i]));
      return out;
   }
   static ui::ValueRange rangeOf(clap_id id, double step = 0.0) {
      return {kParams[id].min, kParams[id].max, kParams[id].defaultValue, step, false,
              (kParams[id].flags & CLAP_PARAM_IS_STEPPED) != 0};
   }

   void drawPlay(visage::Canvas &c, float s) {
      drawCard(c, s, 12, 76, 976, 188, "SECTIONS");
      label(c, s, "PLAY", 260, 87, 36, 0, visage::Font::kRight, 9.5f);
      label(c, s, "SWITCH AT", 474, 87, 76, 0, visage::Font::kRight, 9.5f);
      label(c, s, "BY", 670, 87, 26, 0, visage::Font::kRight, 9.5f);
      char note[128];
      const int sec = std::clamp(frame_.section, 0, kSections - 1);
      const Section &sx = host_.song().sections[static_cast<size_t>(sec)];
      if (frame_.ended)
         std::snprintf(note, sizeof(note), "the song has ended \xC2\xB7 pick a section to play on");
      else
         std::snprintf(note, sizeof(note), "%s, %s \xC2\xB7 bar %d of %d", kSectionNames[sec],
                       frame_.ending ? "ending" : frame_.fillDown ? "fill down" : frame_.fill ? "fill" : ("take " + std::to_string(frame_.take + 1)).c_str(),
                       frame_.bar % std::max<int>(1, sx.bars) + 1, static_cast<int>(sx.bars));
      drawCard(c, s, 12, 272, 620, 198, "NOW PLAYING", note);
      drawCard(c, s, 640, 272, 348, 198, "FEEL", "how a player would touch it");
      {
         auto words = [](double v, const char *low, const char *high) {
            return std::abs(v - 0.5) < 0.03 ? std::string("as written") : std::string(v < 0.5 ? low : high);
         };
         label(c, s, words(param(kDensity), "thinner", "busier"), 492, 384, 64, 0, visage::Font::kCenter, 8.5f);
         label(c, s, words(param(kLength), "shorter", "longer"), 558, 384, 64, 0, visage::Font::kCenter, 8.5f);
      }
      label(c, s, "CHORD NOTES START", 654, 376, 200, 0, visage::Font::kLeft, 9);
      static const char *const onsetWords[4] = {"TOGETHER: every note within 2 ms; the chord as a whole sits off the grid.",
                                                "ROLLED: bottom to top, about 14 ms a note.",
                                                "STRUM DOWN: low string to high, 9 ms apart.",
                                                "STRUM BOTH: down on the beat, up on the offbeats."};
      label(c, s, onsetWords[std::clamp(whole(kOnsets), 0, 3)], 654, 414, 330, 0, visage::Font::kLeft, 8);
      label(c, s, "SAME DICE EVERY", 654, 432, 100, 0, visage::Font::kLeft, 8.5f);
      label(c, s, "AUTO FILLS", 654, 451, 100, 0, visage::Font::kLeft, 8.5f);
      drawCard(c, s, 12, 478, 240, 130, "TRANSPORT", frame_.hostPlaying ? "host playing" : "host stopped");
      drawCard(c, s, 260, 478, 372, 130, "PASSING RUNS", "single notes into a chord change");
      label(c, s, "AT MOST", 346, 506, 100, 0, visage::Font::kLeft, 9);
      label(c, s, "LANDS ON", 346, 550, 100, 0, visage::Font::kLeft, 9);
      label(c, s, std::string("2 to 4 steps in ") + kKeyNames[key()] + " " + kScaleNames[std::clamp(scaleNow(), 0, kScaleCount - 1)],
            346, 590, 140, kFaint, visage::Font::kLeft, 8);
      const Player &p = kPlayerList[std::clamp(whole(kPlayer), 0, kPlayers - 1)];
      drawCard(c, s, 640, 478, 348, 130, "OUTPUT", std::string("MIDI out \xC2\xB7 ") + kInstrumentNames[p.instrument]);
      label(c, s, "CHANNEL", 654, 500, 70, 0, visage::Font::kLeft, 9);
      label(c, s, "RHYTHM FROM", 740, 500, 120, 0, visage::Font::kLeft, 9);
      const bool bass = whole(kRhythmFrom) > 0;
      bassCh_->setVisible(host_.page == kPlayPage && bass);
      follow_->setVisible(host_.page == kPlayPage && bass);
      if (!bass)
         label(c, s, "Plays its own takes. THE BASS: a chord wherever the bass plays.", 654, 546, 320, kFaint,
               visage::Font::kLeft, 8.5f);
      half_->on = whole(kRunHalfStep) != 0;
      shorten_->on = whole(kRunShorten) != 0;
      notesButton_->lit = host_.notesOpen;
      notesButton_->outline = host_.notesOpen ? kOrange : 0;
      const char *hint = whole(kSwitchBy) == 1 ? "Play a section's note to switch to it."
                                               : "Click a section, play its note, or automate the Section parameter.";
      label(c, s, std::string(hint) + " It changes at the " + upper(namesOf(kSwitchAt)[static_cast<size_t>(whole(kSwitchAt))]) + ".",
            12, 618, 976, 0, visage::Font::kRight, 8.5f);
   }

   // ---- overlays ----

   void buildOverlays() {
      players_ = &add<PlayersOverlay>({0, 0, kW, kH}, host_);
      players_->onPick = [this](int i) {
         for (const auto &[id, v] : playerValues(i))
            setParam(id, v);
         regenerate();
      };
      players_->onClose = [this] { closeOverlays(); };
      players_->setOnTop(true);
      players_->setVisible(false);
      browser_ = &add<BrowserOverlay>({0, 0, kW, kH}, host_);
      browser_->onPick = [this](int g, int st) {
         pickStyle(g, st);
         closeOverlays();
      };
      browser_->onClose = [this] { closeOverlays(); };
      browser_->setOnTop(true);
      browser_->setVisible(false);

      const ui::Rect nr{348, 110, 640, 330};
      notes_ = &add<NotesOverlay>(nr, host_);
      notes_->setOnTop(true);
      notes_->onLearn = [this](int a) {
         host_.learnAction = a;
         host_.armLearn(a >= 0);
         notes_->redraw();
      };
      map_ = &add<ui::SegmentedSwitch>({nr.x + 380, nr.y + 10, 244, 20}, std::vector<std::string>{"KEYBOARD MAP", "CUSTOM"});
      map_->setAccent(kOrange);
      bindings_.bind(*map_, kNoteMap);
      map_->onChange = [this, old = map_->onChange](int i) {
         if (old)
            old(i);
         notes_->redraw();
      };
      octave_ = &add<ui::NumberBox>({nr.x + 16, nr.y + 228, 90, 20}, rangeOf(kNoteOctave, 1.0));
      octave_->setStepButtons(true);
      octave_->valueText = [](double v) { return "OCT " + std::to_string(static_cast<int>(std::lround(v))); };
      bindings_.bind(*octave_, kNoteOctave);
      noteChannel_ = &add<ui::NumberBox>({nr.x + 116, nr.y + 228, 74, 20}, rangeOf(kNoteChannel, 1.0));
      noteChannel_->setStepButtons(true);
      noteChannel_->valueText = [](double v) {
         return v < 0.5 ? std::string("ANY") : "CH " + std::to_string(static_cast<int>(std::lround(v)));
      };
      bindings_.bind(*noteChannel_, kNoteChannel);
      reset_ = &add<FlatButton>({nr.x + 200, nr.y + 228, 80, 20}, "RESET ALL", 8.5f);
      reset_->onClick = [this](const visage::MouseEvent &) {
         for (int a = 0; a < kActionCount; ++a)
            setParam(static_cast<clap_id>(kLearnNote + a), 12 + kActionKey[a]);
         notes_->redraw();
      };
      const char *names[kChecks] = {"Fill into the new section", "Velocity sets energy", "Play only while a chord is held",
                                    "Pass other notes through", "Intro sets up the verse", "Swing eases when fast",
                                    "Pad layer, on channel"};
      for (int i = 0; i < kChecks; ++i) {
         const ui::Rect r = i < 4   ? ui::Rect{nr.x + 300 + (i % 2) * 168, nr.y + 228 + (i / 2) * 22, 166, 18}
                            : i < 6 ? ui::Rect{nr.x + 300 + (i % 2) * 168, nr.y + 284, 166, 18}
                                    : ui::Rect{nr.x + 16, nr.y + 284, 150, 18};
         auto &b = add<CheckBox>(r, names[i]);
         const clap_id id = kCheckIds[i];
         b.onToggle = [this, id](bool on) { setParam(id, on ? 1.0 : 0.0); };
         checks_[i] = &b;
      }
      padChannel_ = &add<ui::NumberBox>({nr.x + 170, nr.y + 283, 74, 20}, rangeOf(kPadChannel, 1.0));
      padChannel_->setStepButtons(true);
      padChannel_->valueText = [](double v) { return "CH " + std::to_string(static_cast<int>(std::lround(v))); };
      bindings_.bind(*padChannel_, kPadChannel);
      notesControls(false);
   }

   void notesControls(bool show) {
      for (visage::Frame *f : {static_cast<visage::Frame *>(map_), static_cast<visage::Frame *>(octave_),
                               static_cast<visage::Frame *>(noteChannel_), static_cast<visage::Frame *>(reset_),
                               static_cast<visage::Frame *>(padChannel_)}) {
         f->setOnTop(true);
         f->setVisible(show);
      }
      for (int i = 0; i < kChecks; ++i) {
         checks_[i]->on = whole(kCheckIds[i]) != 0;
         checks_[i]->setOnTop(true);
         checks_[i]->setVisible(show);
      }
   }

   EditorHost &host_;
   ui::ParamBindings bindings_;
   ui::Tooltips tips_;
   std::mt19937 rng_;
   uint32_t litterRolls_ = 0;
   PlayFrame frame_;
   ui::TabBar *tabs_ = nullptr;
   ui::PresetBar *presets_ = nullptr;
   HeaderTransport *header_ = nullptr;
   // GENERATE
   GenreTiles *tiles12_ = nullptr;
   ui::Dropdown *style_ = nullptr;
   ParamSlider *blend_ = nullptr, *energy_ = nullptr, *weird_ = nullptr, *mutation_ = nullptr;
   PlayerButton *player_ = nullptr;
   SongStrip *song_ = nullptr;
   MutateRow *mutate_ = nullptr;
   // CHORDS
   ui::Dropdown *keyBox_ = nullptr, *bass_ = nullptr;
   CheckBox *latch_ = nullptr, *slash_ = nullptr;
   ProgressionRows *rows_ = nullptr;
   Chips *roots_ = nullptr, *qualities_ = nullptr, *instead_ = nullptr;
   ui::SegmentedSwitch *lasts_ = nullptr;
   ChordVoicing *voicing_ = nullptr;
   std::vector<Chord> insteadChords_;
   // EDIT
   ui::SegmentedSwitch *editSection_ = nullptr, *editTake_ = nullptr, *shape1_ = nullptr;
   ui::SegmentedSwitch *art_ = nullptr, *part_ = nullptr;
   TakeRoll *roll_ = nullptr;
   HitLanes *lanes_ = nullptr;
   RangeKeys *range_ = nullptr;
   CheckBox *voiceChecks_[3] = {};
   ChordVoicing *hitKeys_ = nullptr;
   CheckBox *runIn_ = nullptr, *push_ = nullptr;
   std::string diceTag_;
   // PLAY
   SectionTiles *tiles_ = nullptr;
   NowPlaying *now_ = nullptr;
   TransportView *transport_ = nullptr;
   OutKeys *outKeys_ = nullptr;
   CheckBox *half_ = nullptr, *shorten_ = nullptr;
   ui::NumberBox *bassCh_ = nullptr;
   ui::SegmentedSwitch *follow_ = nullptr;
   FlatButton *notesButton_ = nullptr;
   // Overlays
   PlayersOverlay *players_ = nullptr;
   BrowserOverlay *browser_ = nullptr;
   NotesOverlay *notes_ = nullptr;
   ui::SegmentedSwitch *map_ = nullptr;
   ui::NumberBox *octave_ = nullptr, *noteChannel_ = nullptr;
   FlatButton *reset_ = nullptr;
   ui::NumberBox *padChannel_ = nullptr;
   static constexpr int kChecks = 7;
   static constexpr clap_id kCheckIds[kChecks] = {kFillInto, kVelEnergy, kHeldOnly, kPassNotes, kIntroSetup, kSwingEase, kPadLayer};
   CheckBox *checks_[kChecks] = {};
   // CHORDS: the band hit on the chord picked.
   ui::SegmentedSwitch *mark_ = nullptr;
};

} // namespace

std::unique_ptr<ui::Window> createEditor(EditorHost &host) {
   return std::make_unique<ui::Window>(std::make_unique<Panel>(host), ui::WindowOptions{kEditorWidth, kEditorHeight});
}

bool renderScreenshot(EditorHost &host, int page, int width, int height, const char *pngPath) {
   Panel panel(host);
   panel.showPage(page);
   panel.timerCallback();
   panel.timerCallback();
   return ui::renderToPng(panel, width, height, pngPath);
}

} // namespace jcomp

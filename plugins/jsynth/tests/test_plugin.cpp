// JSynth is only a panel mock so far, so these check what exists: the param list, its text, state,
// silence, and that the panel sees the notes it should.

#include <jclap/core/test_host.h>

#include <clap/ext/state.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <set>
#include <string>
#include <vector>

#include "editor.h"
#include "params.h"
#include "presets.h"

extern "C" const clap_plugin_entry_t clap_entry;

using namespace jsynth;

namespace {

// A byte buffer the plugin's state extension writes to and reads from.
struct Buffer {
   std::vector<uint8_t> bytes;
   size_t pos = 0;
   clap_ostream_t out{this, [](const clap_ostream_t *s, const void *data, uint64_t size) -> int64_t {
                         auto *b = static_cast<Buffer *>(s->ctx);
                         b->bytes.insert(b->bytes.end(), static_cast<const uint8_t *>(data),
                                         static_cast<const uint8_t *>(data) + size);
                         return static_cast<int64_t>(size);
                      }};
   clap_istream_t in{this, [](const clap_istream_t *s, void *data, uint64_t size) -> int64_t {
                        auto *b = static_cast<Buffer *>(s->ctx);
                        const size_t n = std::min<size_t>(size, b->bytes.size() - b->pos);
                        std::memcpy(data, b->bytes.data() + b->pos, n);
                        b->pos += n;
                        return static_cast<int64_t>(n);
                     }};
   template <typename T> void put(const T &v) {
      const auto *p = reinterpret_cast<const uint8_t *>(&v);
      bytes.insert(bytes.end(), p, p + sizeof(T));
   }
};

} // namespace

int main() {
   jclap::core::TestHost h(clap_entry, "com.jeffmhopkins.jclap.jsynth");
   const auto *state = static_cast<const clap_plugin_state_t *>(h.plugin()->get_extension(h.plugin(), CLAP_EXT_STATE));
   JCLAP_CHECK(state != nullptr);

   // The param list: ids rising and unique, names unique, both parts the same shape, and every
   // module's params inside its block.
   {
      std::set<std::string> names;
      clap_id last = 0;
      size_t partA = 0, partB = 0;
      for (size_t i = 0; i < paramCount(); ++i) {
         const Param &p = kParams()[i];
         JCLAP_CHECK(i == 0 || p.def.id > last);
         last = p.def.id;
         JCLAP_CHECK(names.insert(p.def.name).second);
         JCLAP_CHECK(p.def.min <= p.def.defaultValue && p.def.defaultValue <= p.def.max);
         JCLAP_CHECK_EQ(paramIndex(p.def.id), static_cast<int>(i));
         const int part = partOf(p.def.id);
         if (part >= 0) {
            JCLAP_CHECK(offsetOf(p.def.id) < kPartEnd);
            JCLAP_CHECK(findParam(partId(1 - part, offsetOf(p.def.id))) != nullptr);
            (part == 0 ? partA : partB) += 1;
         } else {
            JCLAP_CHECK(p.def.id < kGlobalEnd);
         }
         if (p.unit == Unit::Choice)
            JCLAP_CHECK_EQ(p.def.max, static_cast<double>(p.choices->size() - 1));
      }
      JCLAP_CHECK_EQ(partA, partB);
      JCLAP_CHECK(paramCount() > 400);
      JCLAP_CHECK(findParam(kPartBase - 1) == nullptr);
      JCLAP_CHECK_EQ(h.plugin()->get_extension(h.plugin(), CLAP_EXT_PARAMS) != nullptr, true);
   }

   // Every destination is a param, and the menu names them.
   JCLAP_CHECK_EQ(destinationNames().size(), destinations().size());
   for (clap_id offset : destinations())
      JCLAP_CHECK(findParam(partId(0, offset)) != nullptr);
   JCLAP_CHECK(h.paramText(partId(0, routeParam(0, kRouteDestination)), h.paramValue(partId(0, routeParam(0, kRouteDestination)))) ==
               "Filter 1 Cutoff");
   // Nothing from the flock reaches the sound by default: the starting routes stop at three.
   JCLAP_CHECK(h.paramText(partId(0, routeParam(3, kRouteSource)), h.paramValue(partId(0, routeParam(3, kRouteSource)))) == "Off");
   JCLAP_CHECK(h.paramValue(partId(0, flockParam(kNeighbourFm))) == 0.0);
   JCLAP_CHECK(h.paramValue(partId(1, flockParam(kBite))) == 0.0);
   JCLAP_CHECK(h.paramText(partId(1, flockParam(kBehaviour)), h.paramValue(partId(1, flockParam(kBehaviour)))) == "Hunt");
   JCLAP_CHECK(destinationIndex(kPartTranspose) == -1);

   // Text: a few by hand, then every param round-trips at its ends, middle and default.
   JCLAP_CHECK(h.paramText(partId(0, oscParam(1, kOscWave)), 5.0) == "Half Sine");
   JCLAP_CHECK(h.paramText(partId(1, kKeyLow), 60.0) == "C4");
   JCLAP_CHECK(h.paramText(partId(0, envParam(0, kEnvAttack)), timeValue(5.0)) == "5.0 ms");
   JCLAP_CHECK(h.paramText(partId(0, filterParam(0, kFilterCutoff)), cutoffValue(2000.0)) == "2.00 kHz");
   JCLAP_CHECK(h.paramText(partId(0, oscParam(1, kOscRatio)), 1.5) == "x1.5");
   {
      double v = 0.0;
      JCLAP_CHECK(h.textToValue(partId(0, kKeyHigh), "F#2", v) && v == 42.0);
      JCLAP_CHECK(h.textToValue(partId(0, envParam(1, kEnvRelease)), "2 s", v) && std::fabs(timeMs(v) - 2000.0) < 1e-6);
      JCLAP_CHECK(h.textToValue(kPlayMode, "MPE Zones", v) && v == 3.0);
   }
   for (const Param &p : kParams()) {
      const clap_id id = p.def.id;
      for (double v : {p.def.min, p.def.max, 0.5 * (p.def.min + p.def.max), p.def.defaultValue}) {
         if (p.def.flags & CLAP_PARAM_IS_STEPPED)
            v = std::round(v);
         double back = 0.0;
         const std::string shown = h.paramText(id, v);
         JCLAP_CHECK(h.textToValue(id, shown.c_str(), back));
         if (h.paramText(id, back) != shown)
            std::printf("round trip: %s %s -> %s\n", p.def.name, shown.c_str(), h.paramText(id, back).c_str());
         JCLAP_CHECK(h.paramText(id, back) == shown);
      }
   }

   // Params set by the host stick, are clamped, and a per-voice value doesn't move the knob.
   {
      const clap_id cutoff = partId(1, filterParam(1, kFilterCutoff));
      h.setParam(cutoff, 0.25);
      h.process();
      JCLAP_CHECK_EQ(h.paramValue(cutoff), 0.25);
      h.setParam(cutoff, 7.0);
      h.process();
      JCLAP_CHECK_EQ(h.paramValue(cutoff), 1.0);
      h.modulate(cutoff, -0.5);
      h.process();
      JCLAP_CHECK(std::fabs(h.object<EditorHost>()->paramModulated(cutoff) - 0.5) < 1e-9);
   }

   // A note sounds, finite and under full scale.
   {
      h.note(CLAP_EVENT_NOTE_ON, 60, 0.8);
      float outL[256], outR[256];
      float *out[2] = {outL, outR};
      std::fill(outL, outL + 256, 1.0f);
      h.process(256, nullptr, out);
      JCLAP_CHECK(std::any_of(outL, outL + 256, [](float s) { return s != 0.0f; }));
      JCLAP_CHECK(std::all_of(outL, outL + 256, [](float s) { return std::isfinite(s) && std::fabs(s) <= 1.0f; }));
      std::vector<EditorHost::Note> notes;
      h.object<EditorHost>()->notes(notes);
      JCLAP_CHECK(notes.size() == 1 && notes[0].note == 60 && std::fabs(notes[0].strike - 0.8f) < 1e-6f);
      h.noteExpression(CLAP_NOTE_EXPRESSION_PRESSURE, 60, 0.4);
      h.process();
      h.object<EditorHost>()->notes(notes);
      JCLAP_CHECK(notes.size() == 1 && std::fabs(notes[0].pressure - 0.4f) < 1e-6f);
      h.note(CLAP_EVENT_NOTE_OFF, 60, 0.0);
      // The panel's own notes show too.
      h.object<EditorHost>()->noteOn(64, 0.5f);
      h.process();
      h.object<EditorHost>()->notes(notes);
      JCLAP_CHECK(notes.size() == 1 && notes[0].note == 64);
      h.object<EditorHost>()->noteOff(64);
      h.process();
      h.object<EditorHost>()->notes(notes);
      JCLAP_CHECK(notes.empty());
   }

   // A panel edit goes to the host as a gesture.
   {
      EditorHost &panel = *h.object<EditorHost>();
      const clap_id fm = partId(0, fmCell(1, 0));
      panel.beginEdit(fm);
      panel.setParam(fm, 0.6);
      panel.endEdit(fm);
      const auto edits = h.process();
      JCLAP_CHECK_EQ(edits.gestures.size(), 2u);
      JCLAP_CHECK_EQ(h.paramValue(fm), 0.6);
   }

   // State: a save loads back; unknown ids are skipped and missing ones keep their defaults; a
   // truncated state is refused and changes nothing.
   {
      const clap_id level = partId(1, kPartLevel), wave = partId(0, oscParam(2, kOscWave));
      h.setParam(level, -12.0);
      h.setParam(wave, 4.0);
      h.process();
      Buffer saved;
      JCLAP_CHECK(state->save(h.plugin(), &saved.out));
      h.setParam(level, 0.0);
      h.process();
      JCLAP_CHECK(state->load(h.plugin(), &saved.in));
      JCLAP_CHECK_EQ(h.paramValue(level), -12.0);
      JCLAP_CHECK_EQ(h.paramValue(wave), 4.0);

      Buffer partial;
      partial.bytes.insert(partial.bytes.end(), {'J', 'J', 'S', 'Y'});
      partial.put(uint32_t{1});
      partial.put(uint32_t{2});
      partial.put(uint32_t{level});
      partial.put(-3.0);
      partial.put(uint32_t{kPartBase - 1}); // no such param
      partial.put(1.0);
      JCLAP_CHECK(state->load(h.plugin(), &partial.in));
      JCLAP_CHECK_EQ(h.paramValue(level), -3.0);
      JCLAP_CHECK_EQ(h.paramValue(wave), findParam(wave)->def.defaultValue);

      Buffer cut;
      cut.bytes.assign(saved.bytes.begin(), saved.bytes.begin() + saved.bytes.size() / 2);
      JCLAP_CHECK(!state->load(h.plugin(), &cut.in));
      JCLAP_CHECK_EQ(h.paramValue(level), -3.0);
   }

   // Presets: every factory value is a param in range, names are unique, and a user preset saved to
   // a file reads back with every param.
   {
      std::set<std::string> names;
      for (const Preset &p : factoryPresets()) {
         JCLAP_CHECK(names.insert(p.name).second);
         for (const auto &[id, v] : p.values) {
            const Param *param = findParam(id);
            JCLAP_CHECK(param != nullptr);
            if (param)
               JCLAP_CHECK(v >= param->def.min && v <= param->def.max);
         }
         JCLAP_CHECK_EQ(presetValues(p).size(), paramCount());
      }
      struct Values : jclap::ui::ParamAccess {
         std::vector<double> v = presetValues(factoryPresets()[1]);
         double paramValue(clap_id id) const override { return v[paramIndex(id)]; }
         double paramModulated(clap_id id) const override { return paramValue(id); }
         void beginEdit(clap_id) override {}
         void setParam(clap_id, double) override {}
         void endEdit(clap_id) override {}
      } access;
      const auto folder = std::filesystem::temp_directory_path() / "jsynth-preset-test";
      std::filesystem::remove_all(folder);
      JCLAP_CHECK(writePreset(folder / (std::string("Mine") + kPresetExtension), access));
      const std::vector<Preset> user = userPresets(folder);
      JCLAP_CHECK_EQ(user.size(), 1u);
      if (!user.empty()) {
         JCLAP_CHECK(user[0].name == "Mine");
         JCLAP_CHECK(presetValues(user[0]) == access.v);
      }
      std::filesystem::remove_all(folder);
      JCLAP_CHECK(presetFileName("a/b: c ") == "ab c");
   }

   return jclap::core::testResult();
}

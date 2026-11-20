// JComp: a CLAP note effect that comps chords for a polyphonic instrument (an e-piano, keys, organ,
// guitar or synth), the way JDrummer drums: a song made by its generator from 24 genres, played in
// sections, in time with the host or on its own. The chords come from each section's progression, or
// from chords held on the keyboard; each is voiced and led from the one before. The engine
// (engine.h) plays the song; the generator (generator.h) makes it; this file wires them to the
// host: params, state, the transport, notes in (section notes, chord keys, a bass to follow) and
// MIDI out, and the panel (editor.cpp).

#include <jclap/core/edit_queue.h>
#include <jclap/core/entry.h>
#include <jclap/core/helpers.h>
#include <jclap/core/triple_buffer.h>
#include <jclap/ui/clap_gui.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "editor.h"
#include "engine.h"
#include "params.h"
#include "presets.h"

namespace {

using namespace jcomp;

const char *const kFeatures[] = {CLAP_PLUGIN_FEATURE_NOTE_EFFECT, CLAP_PLUGIN_FEATURE_UTILITY, nullptr};

// State layout: 4-byte magic, uint32 version, uint32 param count, one double per param in kParams
// order, uint32 song size and the song (Song::save), then uint32 name length and the preset name.
constexpr char kStateMagic[4] = {'J', 'C', 'M', 'P'};
constexpr uint32_t kStateVersion = 1;

class JComp final : public jclap::core::Plugin, public EditorHost {
public:
   static const clap_plugin_descriptor_t descriptor;

   explicit JComp(const clap_host_t *host) : Plugin(&descriptor, host) {
      for (size_t i = 0; i < kParamCount; ++i) {
         values_[i].store(kParams[i].defaultValue);
         mods_[i].store(0.0);
      }
      song_ = std::make_unique<Song>();
      generateSong(recipeFrom([this](clap_id id) { return values_[id].load(); }), *song_);
      engine_ = std::make_unique<Engine>();
      mail_ = std::make_unique<jclap::core::TripleBuffer<Song>>();
      engine_->setSettings(settings());
      engine_->setSong(*song_);
      engine_->reset();
      publish();
   }

   bool init() override {
      edits_.init(host());
      hostMenu_.init(host());
      gui_.init(*this);
      gui_.createWindow = [this] { return createEditor(*this); };
      return true;
   }

   bool activate(double sampleRate, uint32_t, uint32_t) override {
      sampleRate_ = sampleRate;
      mail_->update();
      engine_->setSong(*song_);
      engine_->setSettings(settings());
      engine_->reset();
      active_.store(true);
      return true;
   }

   void deactivate() override {
      active_.store(false);
      publish();
   }

   void reset() override { engine_->reset(); }

   // A recipe param the host changed: the song it names.
   void onMainThread() override {
      const bool all = remake_.exchange(false);
      const uint32_t sections = remakeSections_.exchange(0);
      if (!all && !sections)
         return;
      Song s = *song_;
      const Recipe r = recipeFrom([this](clap_id id) { return values_[id].load(); });
      if (all)
         generateSong(r, s);
      else
         // A section's energy: that section's rhythm made again, its chords (and their edits) kept.
         for (int i = 0; i < kSections; ++i)
            if (sections >> i & 1)
               rollSection(r, s, i, s.sections[static_cast<size_t>(i)].seed, true);
      setSong(s);
   }

   const void *extension(const char *id) override {
      if (!std::strcmp(id, CLAP_EXT_AUDIO_PORTS))
         return &audioPortsExtension;
      if (!std::strcmp(id, CLAP_EXT_NOTE_PORTS))
         return &notePortsExtension;
      if (!std::strcmp(id, CLAP_EXT_PARAMS))
         return &paramsExtension;
      if (!std::strcmp(id, CLAP_EXT_STATE))
         return &stateExtension;
      return jclap::ui::ClapGui::extensionFor<JComp, &JComp::gui_>(id);
   }

   bool notePortsInfo(uint32_t index, bool isInput, clap_note_port_info_t *info) const override {
      if (!Plugin::notePortsInfo(index, isInput, info))
         return false;
      std::snprintf(info->name, sizeof(info->name), "%s", isInput ? "Chords, Sections, Bass" : "Chords Out");
      return true;
   }

   // ---- EditorHost (main thread) ----

   double paramValue(clap_id id) const override { return id < kParamCount ? values_[id].load() : 0.0; }
   double paramModulated(clap_id id) const override { return id < kParamCount ? value(id) : 0.0; }
   void beginEdit(clap_id id) override { edits_.begin(id); }
   void setParam(clap_id id, double v) override {
      if (id >= kParamCount || !std::isfinite(v))
         return;
      v = std::clamp(v, kParams[id].min, kParams[id].max);
      values_[id].store(v);
      edits_.value(id, v);
   }
   void endEdit(clap_id id) override { edits_.end(id); }
   const jclap::ui::HostMenu *hostMenu() const override { return &hostMenu_; }

   const Frame &frame() override {
      if (!active_.load()) {
         engine_->setSettings(settings());
         publish();
      }
      frames_.update();
      return frames_.front();
   }

   const Song &song() const override { return *song_; }
   void setSong(const Song &s) override {
      if (s == *song_)
         return;
      *song_ = s;
      send();
      if (auto *state = hostExtension<clap_host_state_t>(CLAP_EXT_STATE))
         state->mark_dirty(host());
   }

   void request(int action) override {
      if (action < 0 || action >= kActionCount)
         return;
      if (!active_.load()) {
         if (action == kActRunsOff || action == kActHoldChord)
            engine_->toggleHeld(action);
         else
            engine_->request(action);
         return;
      }
      // One at a time is plenty for clicks: the newest waits for the audio thread.
      requests_[requestWrite_.load() % kRequests].store(action);
      requestWrite_.fetch_add(1);
   }

   void armLearn(bool on) override {
      learned_.store(-1);
      learnArmed_.store(on);
   }
   int takeLearned() override { return learned_.exchange(-1); }

   std::string presetName() const override { return presetName_; }
   void setPresetName(const std::string &name) override { presetName_ = name; }

   // ---- params ----

   uint32_t paramsCount() const override { return kParamCount; }

   bool paramsInfo(uint32_t index, clap_param_info_t *info) const override {
      if (index >= kParamCount)
         return false;
      jclap::core::fillParamInfo(kParams[index], info);
      return true;
   }

   bool paramsValue(clap_id id, double *v) const override {
      if (id >= kParamCount)
         return false;
      *v = values_[id].load();
      return true;
   }

   bool paramsValueToText(clap_id id, double v, char *out, uint32_t size) const override {
      if (id >= kParamCount)
         return false;
      formatParam(id, v, out, size);
      return true;
   }

   bool paramsTextToValue(clap_id id, const char *text, double *v) const override {
      if (id >= kParamCount || !parseParam(id, text, *v))
         return false;
      *v = std::clamp(*v, kParams[id].min, kParams[id].max);
      return true;
   }

   void paramsFlush(const clap_input_events_t *in, const clap_output_events_t *out) override {
      const uint32_t n = in->size(in);
      for (uint32_t i = 0; i < n; ++i)
         handleParam(in->get(in, i));
      edits_.drain(out);
   }

   // ---- state ----

   bool stateSave(const clap_ostream_t *stream) override {
      const uint32_t header[2] = {kStateVersion, kParamCount};
      if (!jclap::core::writeAll(stream, kStateMagic, 4) || !jclap::core::writeAll(stream, header, sizeof(header)))
         return false;
      for (size_t i = 0; i < kParamCount; ++i) {
         const double v = values_[i].load();
         if (!jclap::core::writeAll(stream, &v, sizeof(v)))
            return false;
      }
      const std::vector<uint8_t> saved = song_->save();
      const uint32_t size = static_cast<uint32_t>(saved.size());
      const uint32_t nameLength = static_cast<uint32_t>(std::min<size_t>(presetName_.size(), 255));
      return jclap::core::writeAll(stream, &size, sizeof(size)) && jclap::core::writeAll(stream, saved.data(), size) &&
             jclap::core::writeAll(stream, &nameLength, sizeof(nameLength)) &&
             jclap::core::writeAll(stream, presetName_.data(), nameLength);
   }

   bool stateLoad(const clap_istream_t *stream) override {
      char magic[4];
      uint32_t header[2] = {};
      if (!jclap::core::readAll(stream, magic, 4) || std::memcmp(magic, kStateMagic, 4) ||
          !jclap::core::readAll(stream, header, sizeof(header)) || header[0] < 1 || header[0] > kStateVersion ||
          header[1] > 4096)
         return false;
      std::vector<double> v(kParamCount);
      for (size_t i = 0; i < kParamCount; ++i)
         v[i] = kParams[i].defaultValue;
      for (uint32_t i = 0; i < header[1]; ++i) {
         double x;
         if (!jclap::core::readAll(stream, &x, sizeof(x)))
            return false;
         if (i < kParamCount && std::isfinite(x))
            v[i] = x;
      }
      uint32_t size = 0;
      if (!jclap::core::readAll(stream, &size, sizeof(size)) || size > (1u << 20))
         return false;
      std::vector<uint8_t> saved(size);
      if (!jclap::core::readAll(stream, saved.data(), size))
         return false;
      auto loaded = std::make_unique<Song>();
      if (!loaded->load(saved.data(), saved.size()))
         return false;
      uint32_t nameLength = 0;
      char name[256];
      if (jclap::core::readAll(stream, &nameLength, sizeof(nameLength)) && nameLength < sizeof(name) &&
          jclap::core::readAll(stream, name, nameLength))
         presetName_.assign(name, nameLength);
      else
         presetName_.clear();
      for (size_t i = 0; i < kParamCount; ++i)
         values_[i].store(std::clamp(v[i], kParams[i].min, kParams[i].max));
      // The loaded song is the one: a host's recipe change from before the load doesn't remake it.
      remake_.store(false);
      remakeSections_.store(0);
      *song_ = *loaded;
      send();
      if (!active_.load()) {
         engine_->setSettings(settings());
         engine_->reset();
      }
      if (auto *hostParams = hostExtension<clap_host_params_t>(CLAP_EXT_PARAMS))
         hostParams->rescan(host(), CLAP_PARAM_RESCAN_VALUES);
      return true;
   }

   // ---- audio thread ----

   clap_process_status process(const clap_process_t *proc) override {
      const clap_input_events_t *in = proc->in_events;
      const clap_output_events_t *out = proc->out_events;
      edits_.drain(out);

      const uint32_t n = in->size(in);
      for (uint32_t i = 0; i < n; ++i)
         handleParam(in->get(in, i));
      if (mail_->update())
         engine_->setSong(mail_->front());
      engine_->setSettings(settings());
      // The panel's clicks.
      const uint32_t w = requestWrite_.load();
      while (requestRead_ != w) {
         const int a = requests_[requestRead_ % kRequests].load();
         if (a == kActRunsOff || a == kActHoldChord)
            engine_->toggleHeld(a);
         else
            engine_->request(a);
         ++requestRead_;
      }
      if (learnArmed_.load() && !learning_) {
         engine_->learn(true);
         learning_ = true;
      } else if (!learnArmed_.load() && learning_) {
         engine_->learn(false);
         learning_ = false;
      }

      // Section notes are taken first (at the block's start); the rest pass on in time.
      out_.clear();
      for (uint32_t i = 0; i < n; ++i) {
         const clap_event_header_t *h = in->get(in, i);
         if (h->space_id == CLAP_CORE_EVENT_SPACE_ID)
            handleEvent(h, out);
      }
      if (learning_) {
         const int note = engine_->learned();
         if (note >= 0) {
            learned_.store(note);
            learnArmed_.store(false);
            learning_ = false;
         }
      }

      Block b;
      b.frames = proc->frames_count;
      b.sampleRate = sampleRate_;
      if (const clap_event_transport_t *t = proc->transport) {
         if (t->flags & CLAP_TRANSPORT_HAS_TEMPO && std::isfinite(t->tempo))
            b.tempo = std::clamp(t->tempo, 1.0, 999.0);
         if (t->flags & CLAP_TRANSPORT_HAS_BEATS_TIMELINE) {
            b.hasTransport = true;
            // The count-in isn't the song: JComp starts at the bar the host starts on.
            b.playing = (t->flags & CLAP_TRANSPORT_IS_PLAYING) && !(t->flags & CLAP_TRANSPORT_IS_WITHIN_PRE_ROLL);
            b.beats = static_cast<double>(t->song_pos_beats) / CLAP_BEATTIME_FACTOR;
            if (t->flags & CLAP_TRANSPORT_IS_LOOP_ACTIVE) {
               b.loopActive = true;
               b.loopStart = static_cast<double>(t->loop_start_beats) / CLAP_BEATTIME_FACTOR;
               b.loopEnd = static_cast<double>(t->loop_end_beats) / CLAP_BEATTIME_FACTOR;
            }
         }
      }
      engine_->process(b, out_);
      // Everything out in time order, keeping the order of events at the same time (an off before the
      // on that strikes the key again). An insertion sort: stable, and it allocates nothing.
      for (size_t i = 1; i < out_.count; ++i) {
         const MidiOut m = out_.items[i];
         size_t j = i;
         for (; j > 0 && out_.items[j - 1].time > m.time; --j)
            out_.items[j] = out_.items[j - 1];
         out_.items[j] = m;
      }
      for (size_t i = 0; i < out_.count; ++i)
         jclap::core::pushMidi(out, std::min(out_.items[i].time, proc->frames_count ? proc->frames_count - 1 : 0),
                               out_.items[i].status, out_.items[i].data1, out_.items[i].data2);
      out_.clear();

      publish();
      jclap::core::passAudioThrough(proc);
      return CLAP_PROCESS_CONTINUE;
   }

private:
   void send() {
      mail_->back() = *song_;
      mail_->publish();
      if (!active_.load())
         engine_->setSong(*song_);
   }

   void handleEvent(const clap_event_header_t *h, const clap_output_events_t *) {
      const bool pass = std::lround(values_[kPassNotes].load()) != 0;
      switch (h->type) {
      case CLAP_EVENT_NOTE_CHOKE:
      case CLAP_EVENT_NOTE_ON:
      case CLAP_EVENT_NOTE_OFF: {
         auto *ev = reinterpret_cast<const clap_event_note_t *>(h);
         // A note-off or choke for every key: the chord keys and section notes held are let go.
         if (h->type != CLAP_EVENT_NOTE_ON && ev->key < 0) {
            engine_->releaseAll();
            return;
         }
         if (ev->key < 0 || ev->key > 127)
            return;
         const int channel = ev->channel < 0 ? 0 : ev->channel & 15;
         const bool on = h->type == CLAP_EVENT_NOTE_ON && ev->velocity > 0.0;
         const int vel = std::clamp(static_cast<int>(std::lround(ev->velocity * 127.0)), 1, 127);
         const uint32_t at = h->time;
         const bool used = on ? engine_->noteOn(channel, ev->key, vel, at) : engine_->noteOff(channel, ev->key, at);
         if (!used && pass)
            out_.push(h->time, static_cast<uint8_t>((on ? 0x90 : 0x80) | channel), static_cast<uint8_t>(ev->key),
                      static_cast<uint8_t>(on ? vel : 0));
         return;
      }
      case CLAP_EVENT_MIDI: {
         auto *ev = reinterpret_cast<const clap_event_midi_t *>(h);
         const int status = ev->data[0] & 0xF0, channel = ev->data[0] & 0x0F;
         const int d1 = ev->data[1] & 0x7F, d2 = ev->data[2] & 0x7F;
         if (status == 0x90 || status == 0x80) {
            const bool on = status == 0x90 && d2 > 0;
            const bool used = on ? engine_->noteOn(channel, d1, d2, h->time) : engine_->noteOff(channel, d1, h->time);
            if (!used && pass)
               out_.push(h->time, ev->data[0], ev->data[1], ev->data[2]);
            return;
         }
         // All Sound Off and All Notes Off let go of what's held too.
         if (status == 0xB0 && (d1 == 120 || d1 == 123))
            engine_->releaseAll();
         if (pass)
            out_.push(h->time, ev->data[0], ev->data[1], ev->data[2]);
         return;
      }
      default:
         return;
      }
   }

   bool handleParam(const clap_event_header_t *h) {
      if (h->space_id != CLAP_CORE_EVENT_SPACE_ID)
         return false;
      if (h->type == CLAP_EVENT_PARAM_VALUE) {
         auto *ev = reinterpret_cast<const clap_event_param_value_t *>(h);
         if (ev->param_id < kParamCount && std::isfinite(ev->value)) {
            const double v = std::clamp(ev->value, kParams[ev->param_id].min, kParams[ev->param_id].max);
            // A recipe param set by the host (not the panel, which remakes the song itself): the song
            // is remade from it on the main thread.
            if (isRecipeParam(ev->param_id) && v != values_[ev->param_id].load()) {
               if (ev->param_id >= kSectionEnergy && ev->param_id < kSectionEnergy + kSections)
                  remakeSections_.fetch_or(1u << (ev->param_id - kSectionEnergy));
               else
                  remake_.store(true);
               host()->request_callback(host());
            }
            values_[ev->param_id].store(v);
         }
         return true;
      }
      if (h->type == CLAP_EVENT_PARAM_MOD) {
         auto *ev = reinterpret_cast<const clap_event_param_mod_t *>(h);
         if (ev->param_id < kParamCount && std::isfinite(ev->amount))
            mods_[ev->param_id].store(ev->amount);
         return true;
      }
      return false;
   }

   void publish() {
      engine_->fillFrame(frames_.back());
      frames_.publish();
   }

   Settings settings() const {
      const auto whole = [this](clap_id id) { return static_cast<int>(std::lround(value(id))); };
      Settings s;
      s.section = whole(kSection);
      s.take = whole(kTake);
      s.switchAt = whole(kSwitchAt);
      s.switchBy = whole(kSwitchBy);
      s.song = whole(kSongMode) == 1;
      s.sameDiceBars = kSameDiceBars[choiceIndex(value(kSameDice), 5)];
      s.autoFillBars = kAutoFillBars[choiceIndex(value(kAutoFill), 4)];
      s.sync = whole(kSync);
      s.play = whole(kPlay) != 0;
      s.freeTempo = value(kFreeTempo);
      s.swing = value(kSwing);
      s.swingFeel = whole(kSwingFeel);
      s.key = whole(kKey);
      s.scale = whole(kScale);
      s.chordsFrom = whole(kChordsFrom);
      s.changeAt = whole(kChangeAt);
      s.latch = whole(kLatch) != 0;
      s.slashBass = whole(kSlashBass) != 0;
      s.voiceHeld = whole(kVoiceHeld);
      s.chordSplit = whole(kChordSplit);
      s.voicing = voiceSettings([this](clap_id id) { return value(id); });
      s.timing = value(kTiming);
      s.velocityVar = value(kVelocityVar);
      s.topVoice = value(kTopVoice);
      s.lengths = value(kLengths);
      s.length = value(kLength);
      s.density = value(kDensity);
      s.onsets = whole(kOnsets);
      s.vary = value(kVary);
      s.loose = kPlayerList[std::clamp(whole(kPlayer), 0, kPlayers - 1)].loose;
      s.runs = value(kRuns);
      s.runEveryBars = kRunEveryBars[choiceIndex(value(kRunEvery), 3)];
      s.runLandTop = whole(kRunLand) == 0;
      s.runHalfStep = whole(kRunHalfStep) != 0;
      s.runShorten = whole(kRunShorten) != 0;
      s.channel = whole(kChannel) - 1;
      s.rhythmFrom = whole(kRhythmFrom);
      s.bassChannel = whole(kBassChannel) - 1;
      s.follow = whole(kFollow);
      s.noteMap = whole(kNoteMap);
      s.noteOctave = whole(kNoteOctave);
      s.noteChannel = whole(kNoteChannel);
      s.fillInto = whole(kFillInto) != 0;
      s.velEnergy = whole(kVelEnergy) != 0;
      s.heldOnly = whole(kHeldOnly) != 0;
      s.passNotes = whole(kPassNotes) != 0;
      for (int a = 0; a < kActionCount; ++a)
         s.learn[static_cast<size_t>(a)] = whole(static_cast<clap_id>(kLearnNote + a));
      applyRecipe(s, recipeFrom([this](clap_id id) { return value(id); }));
      return s;
   }

   double value(clap_id id) const {
      return std::clamp(values_[id].load() + mods_[id].load(), kParams[id].min, kParams[id].max);
   }

   std::atomic<double> values_[kParamCount];
   std::atomic<double> mods_[kParamCount];
   jclap::core::EditQueue edits_;
   jclap::ui::HostMenu hostMenu_;

   // The engine belongs to the audio thread while active, and to the main thread otherwise.
   std::unique_ptr<Engine> engine_;
   double sampleRate_ = 48000.0;
   std::atomic<bool> active_{false};
   OutBuffer out_;
   std::unique_ptr<jclap::core::TripleBuffer<Song>> mail_;
   jclap::core::TripleBuffer<Frame> frames_;
   static constexpr uint32_t kRequests = 64;
   std::atomic<int> requests_[kRequests] = {};
   std::atomic<uint32_t> requestWrite_{0};
   uint32_t requestRead_ = 0;
   std::atomic<bool> learnArmed_{false};
   std::atomic<bool> remake_{false};
   std::atomic<uint32_t> remakeSections_{0};
   std::atomic<int> learned_{-1};
   bool learning_ = false;

   // Main thread.
   std::unique_ptr<Song> song_;
   std::string presetName_ = "Pocket Stabs";

   jclap::ui::ClapGui gui_;
};

const clap_plugin_descriptor_t JComp::descriptor = {
   CLAP_VERSION_INIT,
   "com.jeffmhopkins.jclap.jcomp",
   "JComp",
   "jeffmhopkins",
   "https://github.com/jeffmhopkins/Jclap",
   "",
   "",
   JCLAP_PLUGIN_VERSION,
   "A comping generator for keys, organ and guitar: 24 genres, voice leading, passing runs, sections and mutations",
   kFeatures,
};

} // namespace

JCLAP_ENTRY(JComp)

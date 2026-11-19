// JKit: a drum kit whose sounds an AI model makes inside the plugin. Sixteen pads on MIDI notes 36
// to 51, each with pitch, start and end, an envelope, a three-band EQ with a low cut, and sends to a
// shared reverb and delay; then a master EQ, width, BOOM and volume.
//
// The Generate tab (editor.cpp) turns a prompt into sounds with Stable Audio Open Small, run by
// ONNX Runtime on a worker thread (gen/studio.h); both are downloaded the first time they're
// wanted. Everything here is the kit: playing it, and keeping its sounds with the project.
//
// Threads: the pads' sounds are std::shared_ptr<const Sample>, swapped on the main thread under
// kitMutex_. The audio thread holds the same mutex for each whole block (try_lock: if the panel is
// mid-swap, that one block is silent rather than waiting), so a sound is never freed while it plays.

#include <jclap/core/edit_queue.h>
#include <jclap/core/entry.h>
#include <jclap/core/helpers.h>
#include <jclap/core/spsc_queue.h>
#include <jclap/dsp/reverb.h>
#include <jclap/dsp/stereo_delay.h>
#include <jclap/ui/clap_gui.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <memory>
#include <mutex>
#include <random>

#include "editor.h"
#include "gen/studio.h"
#include "kit.h"
#include "params.h"
#include "prompts.h"

namespace {

using namespace jkit;

const char *const kFeatures[] = {CLAP_PLUGIN_FEATURE_INSTRUMENT, CLAP_PLUGIN_FEATURE_DRUM_MACHINE,
                                 CLAP_PLUGIN_FEATURE_SAMPLER, CLAP_PLUGIN_FEATURE_STEREO, nullptr};
constexpr double kPi = 3.14159265358979323846;

// ---- filters ----

// One RBJ biquad, stereo.
struct Biquad {
   double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
   double z[2][2] = {{0, 0}, {0, 0}};
   bool flat = true;

   void setFlat() {
      b0 = 1;
      b1 = b2 = a1 = a2 = 0;
      flat = true;
   }
   void set(double nb0, double nb1, double nb2, double a0, double na1, double na2) {
      b0 = nb0 / a0;
      b1 = nb1 / a0;
      b2 = nb2 / a0;
      a1 = na1 / a0;
      a2 = na2 / a0;
      flat = false;
   }
   void highPass(double fs, double hz, double q = 0.7071) {
      const double w = 2 * kPi * std::min(hz, 0.45 * fs) / fs, c = std::cos(w), al = std::sin(w) / (2 * q);
      set((1 + c) / 2, -(1 + c), (1 + c) / 2, 1 + al, -2 * c, 1 - al);
   }
   void lowPass(double fs, double hz, double q = 0.7071) {
      const double w = 2 * kPi * std::min(hz, 0.45 * fs) / fs, c = std::cos(w), al = std::sin(w) / (2 * q);
      set((1 - c) / 2, 1 - c, (1 - c) / 2, 1 + al, -2 * c, 1 - al);
   }
   void shelf(double fs, double hz, double db, bool high) {
      if (std::fabs(db) < 0.01)
         return setFlat();
      const double A = std::pow(10.0, db / 40.0), w = 2 * kPi * std::min(hz, 0.45 * fs) / fs, c = std::cos(w);
      const double al = std::sin(w) / 2 * std::sqrt(2.0), sa = 2 * std::sqrt(A) * al;
      if (high)
         set(A * ((A + 1) + (A - 1) * c + sa), -2 * A * ((A - 1) + (A + 1) * c), A * ((A + 1) + (A - 1) * c - sa),
             (A + 1) - (A - 1) * c + sa, 2 * ((A - 1) - (A + 1) * c), (A + 1) - (A - 1) * c - sa);
      else
         set(A * ((A + 1) - (A - 1) * c + sa), 2 * A * ((A - 1) - (A + 1) * c), A * ((A + 1) - (A - 1) * c - sa),
             (A + 1) + (A - 1) * c + sa, -2 * ((A - 1) + (A + 1) * c), (A + 1) + (A - 1) * c - sa);
   }
   void peak(double fs, double hz, double db, double q) {
      if (std::fabs(db) < 0.01)
         return setFlat();
      const double A = std::pow(10.0, db / 40.0), w = 2 * kPi * std::min(hz, 0.45 * fs) / fs, c = std::cos(w);
      const double al = std::sin(w) / (2 * q);
      set(1 + al * A, -2 * c, 1 - al * A, 1 + al / A, -2 * c, 1 - al / A);
   }
   void reset() { z[0][0] = z[0][1] = z[1][0] = z[1][1] = 0; }
   float tick(int ch, float x) {
      if (flat)
         return x;
      // Transposed direct form II.
      const double y = b0 * x + z[ch][0];
      z[ch][0] = b1 * x - a1 * y + z[ch][1];
      z[ch][1] = b2 * x - a2 * y;
      return static_cast<float>(y);
   }
};

// A pad's or the master's EQ: low cut, low shelf, mid bell, high shelf, and (master only) a high cut.
struct Eq {
   Biquad cut, low, mid, high, top;
   double key[5] = {-1, -1, -1, -1, -1};
   // Recomputes only what changed. The shelves' corners and the mid's Q default to the master's.
   void set(double fs, double cutHz, double lowDb, double midDb, double midHz, double highDb, double topHz,
            double lowHz = 120.0, double q = 0.9, double highHz = 7000.0) {
      const double k[5] = {cutHz + fs * 1e-9, lowDb * 1e5 + lowHz, (midDb * 1e4 + midHz) * 100.0 + q,
                           highDb * 1e6 + highHz, topHz};
      if (k[0] != key[0])
         cutHz > 0 ? cut.highPass(fs, cutHz) : cut.setFlat();
      if (k[1] != key[1])
         low.shelf(fs, lowHz, lowDb, false);
      if (k[2] != key[2])
         mid.peak(fs, midHz, midDb, q);
      if (k[3] != key[3])
         high.shelf(fs, highHz, highDb, true);
      if (k[4] != key[4])
         topHz > 0 ? top.lowPass(fs, topHz) : top.setFlat();
      std::copy(k, k + 5, key);
   }
   void reset() {
      for (Biquad *b : {&cut, &low, &mid, &high, &top})
         b->reset();
   }
   void tick(float &l, float &r) {
      for (Biquad *b : {&cut, &low, &mid, &high, &top}) {
         l = b->tick(0, l);
         r = b->tick(1, r);
      }
   }
   void invalidate() { std::fill(key, key + 5, -1.0); }
};

// ---- voices ----

// One playing of a sample. A pad has one; a retrigger or a choke moves it to the pad's `fading`
// slot for a few ms so the cut never clicks.
struct Voice {
   const Sample *sample = nullptr;
   double pos = 0.0;          // in sample frames
   double from = 0.0, to = 0.0; // the region, frames
   bool reverse = false;
   double env = 0.0;          // the attack/decay envelope
   double attackStep = 1.0;   // per output sample
   double decayMul = 1.0;     // per output sample, once the attack is done
   bool attacking = true;
   bool released = false;
   double releaseMul = 1.0;
   double fade = 1.0, fadeStep = 0.0; // the de-click fade, toward 0 when fading
   float gain = 1.0f;         // velocity
   float velocity = 1.0f;
   bool on() const { return sample != nullptr; }
};

struct PanelNote {
   int8_t pad;
   bool on;
   float velocity;
};

constexpr char kStateMagic[4] = {'J', 'K', 'I', 'T'};
constexpr uint32_t kStateVersion = 3; // 2 adds the engine, 3 the fade in; 1 and 2 still load

class JKit final : public jclap::core::Plugin, public EditorHost {
public:
   static const clap_plugin_descriptor_t descriptor;

   explicit JKit(const clap_host_t *host) : Plugin(&descriptor, host) {
      for (clap_id i = 0; i < kParamCount; ++i) {
         values_[i].store(param(i).defaultValue);
         mods_[i].store(0.0);
      }
      std::random_device rd;
      gen_.seed = 1 + rd() % 99999;
      gen_.prompt = randomPrompt(static_cast<Role>(gen_.role), gen_.strange, rd());
   }

   ~JKit() override {
      if (studio_ && job_)
         (*studio_)->drop(job_);
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
      room_.prepare(sampleRate);
      delay_.prepare(sampleRate);
      masterEq_.invalidate();
      masterEq_.reset();
      for (PadTone &t : tone_)
         for (PadTone::Point &pt : t.points) {
            pt.type = -1; // set up again at the new rate
            pt.reset();
         }
      boomLp_.lowPass(sampleRate, 110.0);
      boomLp_.reset();
      silence();
      PanelNote stale;
      while (panelNotes_.pop(stale)) {
      }
      volume_ = dbToGain(value(kVolume));
      width_ = value(kWidth);
      active_.store(true);
      return true;
   }

   void deactivate() override {
      active_.store(false);
      silence();
   }

   void reset() override {
      silence();
      room_.reset();
      delay_.reset();
   }

   uint32_t audioPortsCount(bool isInput) const override { return isInput ? 0 : 1; }
   bool audioPortsInfo(uint32_t index, bool isInput, clap_audio_port_info_t *info) const override {
      if (isInput || index != 0)
         return false;
      info->id = 0;
      std::snprintf(info->name, sizeof(info->name), "Output");
      info->flags = CLAP_AUDIO_PORT_IS_MAIN;
      info->channel_count = 2;
      info->port_type = CLAP_PORT_STEREO;
      info->in_place_pair = CLAP_INVALID_ID;
      return true;
   }

   uint32_t notePortsCount(bool isInput) const override { return isInput ? 1 : 0; }
   bool notePortsInfo(uint32_t index, bool isInput, clap_note_port_info_t *info) const override {
      if (!Plugin::notePortsInfo(index, isInput, info))
         return false;
      info->supported_dialects = CLAP_NOTE_DIALECT_CLAP | CLAP_NOTE_DIALECT_MIDI;
      info->preferred_dialect = CLAP_NOTE_DIALECT_CLAP;
      return true;
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
      return jclap::ui::ClapGui::extensionFor<JKit, &JKit::gui_>(id);
   }

   // ---- params ----

   uint32_t paramsCount() const override { return kParamCount; }
   bool paramsInfo(uint32_t index, clap_param_info_t *info) const override {
      if (index >= kParamCount)
         return false;
      jclap::core::fillParamInfo(param(index), info);
      return true;
   }
   bool paramsValue(clap_id id, double *value) const override {
      if (id >= kParamCount)
         return false;
      *value = values_[id].load();
      return true;
   }
   bool paramsValueToText(clap_id id, double value, char *out, uint32_t size) const override {
      if (id >= kParamCount)
         return false;
      formatParam(id, value, out, size);
      return true;
   }
   bool paramsTextToValue(clap_id id, const char *text, double *value) const override {
      if (id >= kParamCount || !parseParam(id, text, *value))
         return false;
      *value = std::clamp(*value, param(id).min, param(id).max);
      return true;
   }
   void paramsFlush(const clap_input_events_t *in, const clap_output_events_t *out) override {
      handleEvents(in, false);
      edits_.drain(out);
   }

   // ---- EditorHost: params ----

   double paramValue(clap_id id) const override { return id < kParamCount ? values_[id].load() : 0.0; }
   double paramModulated(clap_id id) const override { return id < kParamCount ? value(id) : 0.0; }
   void beginEdit(clap_id id) override { edits_.begin(id); }
   void setParam(clap_id id, double v) override {
      if (id >= kParamCount || !std::isfinite(v))
         return;
      v = std::clamp(v, param(id).min, param(id).max);
      values_[id].store(v);
      edits_.value(id, v);
   }
   void endEdit(clap_id id) override { edits_.end(id); }
   const jclap::ui::HostMenu *hostMenu() const override { return &hostMenu_; }

   // ---- EditorHost: the kit ----

   SamplePtr padSample(int pad) const override {
      std::lock_guard<std::mutex> lock(kitMutex_);
      return pad >= 0 && pad < kPads ? pads_[pad] : nullptr;
   }

   void setPadSample(int pad, SamplePtr sample) override {
      if (pad < 0 || pad >= kPads)
         return;
      SamplePtr old;
      {
         std::lock_guard<std::mutex> lock(kitMutex_);
         old = std::move(pads_[pad]);
         pads_[pad] = std::move(sample);
         voices_[pad] = Voice{};
         fading_[pad] = Voice{};
      }
      positions_[pad].store(-1.0f);
      markDirty();
   }

   void swapPads(int a, int b) override {
      if (a == b || a < 0 || b < 0 || a >= kPads || b >= kPads)
         return;
      {
         std::lock_guard<std::mutex> lock(kitMutex_);
         std::swap(pads_[a], pads_[b]);
         for (int p : {a, b}) {
            voices_[p] = Voice{};
            fading_[p] = Voice{};
         }
      }
      for (int f = 0; f < static_cast<int>(kPadParamCount); ++f) {
         const clap_id ia = padParam(a, static_cast<PadParam>(f)), ib = padParam(b, static_cast<PadParam>(f));
         const double va = values_[ia].load(), vb = values_[ib].load();
         if (va == vb)
            continue;
         for (const auto &[id, v] : {std::pair<clap_id, double>{ia, vb}, {ib, va}}) {
            beginEdit(id);
            setParam(id, v);
            endEdit(id);
         }
      }
      markDirty();
   }

   void playPad(int pad, float velocity) override {
      if (active_.load() && pad >= 0 && pad < kPads)
         panelNotes_.push({static_cast<int8_t>(pad), true, std::clamp(velocity, 0.01f, 1.0f)});
   }
   void stopPad(int pad) override {
      if (active_.load() && pad >= 0 && pad < kPads)
         panelNotes_.push({static_cast<int8_t>(pad), false, 0.0f});
   }
   float padPosition(int pad) const override { return pad >= 0 && pad < kPads ? positions_[pad].load() : -1.0f; }
   float padLevel(int pad) const override { return pad >= 0 && pad < kPads ? levels_[pad].load() : 0.0f; }
   uint32_t padHits() const override { return padHits_.load(); }
   int lastHitPad() const override { return lastHitPad_.load(); }
   void meters(float &kit, float &fx, float &out) override {
      kit = kitPeak_.exchange(0.0f);
      fx = fxPeak_.exchange(0.0f);
      out = outPeak_.exchange(0.0f);
   }

   // ---- EditorHost: generating ----

   GenSettings &gen() override { return gen_; }
   Treatment &treatment() override { return treatment_; }
   std::vector<Variant> &variants() override { return variants_; }
   std::vector<Recent> &recent() override { return recent_; }

   gen::Engine engineChoice() const { return gen::engineAt(gen_.engine); }

   EngineStatus engine() override {
      gen::Studio &s = studio();
      const gen::Engine which = engineChoice();
      const gen::Studio::Status st = s.status(which);
      EngineStatus e;
      e.state = static_cast<EngineStatus::State>(st.state);
      e.received = st.received;
      e.total = st.total;
      e.error = st.error;
      e.downloadBytes = gen::Studio::downloadBytes(which);
      // Walking the folder is cheap but not free: once a second is plenty.
      const auto now = std::chrono::steady_clock::now();
      if (now - diskCheckedAt_ > std::chrono::seconds(1) || st.state != lastState_ || which != lastEngine_) {
         diskBytes_ = gen::Studio::diskBytes(which);
         diskCheckedAt_ = now;
         lastState_ = st.state;
         lastEngine_ = which;
      }
      e.diskBytes = diskBytes_;
      e.folder = gen::Studio::modelFolder(which).u8string();
      return e;
   }
   EngineStatus engineStatus(int engine) override {
      const gen::Engine which = gen::engineAt(engine);
      const gen::Studio::Status st = studio().status(which);
      EngineStatus e;
      e.state = static_cast<EngineStatus::State>(st.state);
      e.received = st.received;
      e.total = st.total;
      e.error = st.error;
      e.downloadBytes = gen::Studio::downloadBytes(which);
      e.diskBytes = gen::Studio::diskBytes(which);
      e.folder = gen::Studio::modelFolder(which).u8string();
      return e;
   }
   void installEngine() override { studio().install(engineChoice()); }
   void removeEngine() override {
      if (job_) {
         studio().drop(job_);
         job_ = 0;
      }
      studio().remove(engineChoice());
      diskCheckedAt_ = {};
   }
   void cancelEngine() override {
      if (job_) {
         studio().drop(job_);
         job_ = 0;
      }
      studio().cancel();
   }

   void generate(int around) override {
      if (job_)
         return;
      gen::Request r;
      r.engine = engineChoice();
      r.prompt = gen_.prompt;
      if (r.prompt.empty()) {
         gen_.prompt = randomPrompt(static_cast<Role>(gen_.role), gen_.strange, std::random_device{}());
         r.prompt = gen_.prompt;
      }
      r.seconds = std::clamp(gen_.seconds, 0.1, gen::kMaxSeconds);
      r.steps = std::clamp(gen_.steps, 1, 100);
      r.guidance = std::clamp(gen_.guidance, 1.0, 12.0);
      r.variants = std::clamp(gen_.variants, 1, 8);
      r.variety = std::clamp(gen_.variety, 0.0, 1.0);
      dropFirst_ = false;
      if (around >= 0 && around < static_cast<int>(variants_.size()) && variants_[around].sample) {
         // VARY: start every new one near this one's noise; the first would be the same sound again.
         const SamplePtr &s = variants_[around].sample;
         r.prompt = s->prompt;
         r.seed = s->seed;
         r.variety = 0.35;
         r.variants += 1;
         r.seconds = std::max(0.1, std::min(gen::kMaxSeconds, gen_.seconds));
         dropFirst_ = true;
         jobRole_ = s->role;
      } else {
         // A new random seed every time (there's no SEED control since 0.4); the variants follow from it.
         gen_.seed = 1 + std::random_device{}() % 99999;
         r.seed = gen_.seed;
         jobRole_ = gen_.role;
         remember(r.prompt, gen_.role, r.seed);
      }
      jobPrompt_ = r.prompt;
      job_ = studio().submit(r);
      jobEngine_ = r.engine;
      jobStarted_ = std::chrono::steady_clock::now();
   }

   bool generating() const override { return job_ != 0; }

   double generateProgress() const override {
      if (!job_ || !studio_)
         return 0.0;
      gen::Studio &s = **studio_;
      const auto state = s.jobState(job_);
      if (state == gen::Studio::JobState::Running && s.status(jobEngine_).state == gen::Studio::State::Ready)
         return s.jobProgress(job_);
      return -1.0;
   }

   bool pollGeneration(std::string &error) override {
      if (!job_)
         return false;
      std::vector<gen::Sound> sounds;
      if (!studio().take(job_, sounds, error))
         return false;
      job_ = 0;
      lastRun_ = std::chrono::duration<double>(std::chrono::steady_clock::now() - jobStarted_).count();
      if (!error.empty() || sounds.empty())
         return false;
      // Starred variants stay; the rest make way for the new ones.
      std::vector<Variant> kept;
      for (Variant &v : variants_)
         if (v.starred)
            kept.push_back(std::move(v));
      const size_t first = kept.size();
      for (size_t i = dropFirst_ ? 1 : 0; i < sounds.size(); ++i) {
         auto s = std::make_shared<Sample>();
         s->role = jobRole_;
         s->name = kRoleNames[std::clamp(jobRole_, 0, kRoleCount - 1)];
         s->prompt = jobPrompt_;
         s->seed = sounds[i].seed;
         s->left = std::move(sounds[i].left);
         s->right = std::move(sounds[i].right);
         kept.push_back({std::move(s), false});
      }
      variants_ = std::move(kept);
      selectedVariant = static_cast<int>(first);
      return true;
   }

   double lastRunSeconds() const override { return lastRun_; }

   void audition(SamplePtr sample) override {
      SamplePtr old;
      {
         std::lock_guard<std::mutex> lock(kitMutex_);
         old = std::move(preview_);
         preview_ = std::move(sample);
         previewVoice_ = Voice{};
         if (preview_ && preview_->frames()) {
            previewVoice_.sample = preview_.get();
            previewVoice_.to = static_cast<double>(preview_->frames());
            previewVoice_.env = 1.0;
            previewVoice_.attacking = false;
         }
      }
      previewPos_.store(preview_ ? 0.0f : -1.0f);
   }
   float auditionPosition() const override { return previewPos_.load(); }

   std::string kitName() const override { return kitName_; }
   void setKitName(const std::string &name) override {
      kitName_ = name;
      markDirty();
   }

   // ---- state ----

   bool stateSave(const clap_ostream_t *stream) override {
      Writer w;
      w.bytes.assign(kStateMagic, kStateMagic + 4);
      w.u32(kStateVersion);
      w.u32(kParamCount);
      for (clap_id i = 0; i < kParamCount; ++i)
         w.f64(values_[i].load());
      w.str(kitName_);
      // The Generate tab.
      w.str(gen_.prompt);
      w.u32(static_cast<uint32_t>(gen_.role));
      w.f64(gen_.seconds);
      w.u32(static_cast<uint32_t>(gen_.steps));
      w.f64(gen_.guidance);
      w.f64(gen_.variety);
      w.f64(gen_.strange);
      w.u32(gen_.seed);
      w.u8(gen_.seedLocked ? 1 : 0);
      w.u32(static_cast<uint32_t>(gen_.variants));
      w.u32(static_cast<uint32_t>(gen_.engine));
      w.f64(treatment_.start);
      w.f64(treatment_.end);
      w.f64(treatment_.fadeMs);
      w.u8(static_cast<uint8_t>((treatment_.normalize ? 1 : 0) | (treatment_.trimSilence ? 2 : 0) |
                                (treatment_.reverse ? 4 : 0)));
      w.f64(treatment_.fadeInMs);
      w.u32(static_cast<uint32_t>(recent_.size()));
      for (const Recent &r : recent_) {
         w.str(r.prompt);
         w.u32(static_cast<uint32_t>(r.role));
         w.u32(r.seed);
         w.f64(static_cast<double>(r.when));
      }
      w.u32(static_cast<uint32_t>(page));
      w.u32(static_cast<uint32_t>(selectedPad));
      w.u32(static_cast<uint32_t>(targetPad));
      // The sounds.
      SamplePtr pads[kPads];
      {
         std::lock_guard<std::mutex> lock(kitMutex_);
         for (int i = 0; i < kPads; ++i)
            pads[i] = pads_[i];
      }
      w.bytes += writeKit(pads);
      return jclap::core::writeAll(stream, w.bytes.data(), w.bytes.size());
   }

   bool stateLoad(const clap_istream_t *stream) override {
      std::string bytes;
      char buffer[65536];
      for (;;) {
         const int64_t n = stream->read(stream, buffer, sizeof(buffer));
         if (n < 0)
            return false;
         if (n == 0)
            break;
         bytes.append(buffer, static_cast<size_t>(n));
      }
      if (bytes.size() < 12 || std::memcmp(bytes.data(), kStateMagic, 4))
         return false;
      Reader r(bytes.data() + 4, bytes.size() - 4);
      const uint32_t version = r.u32();
      if (version < 1 || version > kStateVersion)
         return false;
      const uint32_t count = r.u32();
      std::vector<double> v(kParamCount);
      for (clap_id i = 0; i < kParamCount; ++i)
         v[i] = param(i).defaultValue;
      for (uint32_t i = 0; i < count; ++i) {
         const double x = r.f64();
         if (i < kParamCount && std::isfinite(x))
            v[i] = x;
      }
      if (!r.ok())
         return false;
      std::string name = r.str();
      GenSettings g;
      g.prompt = r.str();
      g.role = std::clamp(static_cast<int>(r.u32()), 0, kRoleCount - 1);
      g.seconds = r.f64();
      g.steps = static_cast<int>(r.u32());
      g.guidance = r.f64();
      g.variety = r.f64();
      g.strange = r.f64();
      g.seed = r.u32();
      g.seedLocked = r.u8() != 0;
      g.variants = static_cast<int>(r.u32());
      g.engine = version >= 2 ? std::clamp(static_cast<int>(r.u32()), 0, gen::kEngineCount - 1) : 0;
      Treatment t;
      t.start = r.f64();
      t.end = r.f64();
      t.fadeMs = r.f64();
      const uint8_t flags = r.u8();
      t.normalize = flags & 1;
      t.trimSilence = flags & 2;
      t.reverse = flags & 4;
      t.fadeInMs = version >= 3 ? r.f64() : 0.0;
      std::vector<Recent> recent;
      const uint32_t recentCount = r.u32();
      for (uint32_t i = 0; i < recentCount && r.ok() && i < 64; ++i) {
         Recent e;
         e.prompt = r.str();
         e.role = std::clamp(static_cast<int>(r.u32()), 0, kRoleCount - 1);
         e.seed = r.u32();
         e.when = static_cast<int64_t>(r.f64());
         recent.push_back(std::move(e));
      }
      const int pageRead = static_cast<int>(r.u32()), selected = static_cast<int>(r.u32()),
                target = static_cast<int>(r.u32());
      if (!r.ok())
         return false;
      const size_t used = bytes.size() - 4 - r.left();
      SamplePtr pads[kPads];
      if (!readKit(bytes.substr(4 + used), pads))
         return false;

      for (clap_id i = 0; i < kParamCount; ++i)
         values_[i].store(std::clamp(v[i], param(i).min, param(i).max));
      // Projects from before 0.4 have their EQ in the old params: move it onto points.
      for (int pad = 0; pad < kPads; ++pad)
         migrateLegacyEq(
            pad, [this](clap_id id) { return values_[id].load(); }, [this](clap_id id, double x) { values_[id].store(x); });
      kitName_ = name;
      auto finite = [](double x, double lo, double hi, double fallback) {
         return std::isfinite(x) ? std::clamp(x, lo, hi) : fallback;
      };
      g.seconds = finite(g.seconds, 0.1, gen::kMaxSeconds, 1.0);
      g.steps = std::clamp(g.steps, 1, 100);
      g.guidance = finite(g.guidance, 1.0, 12.0, 1.0);
      g.variety = finite(g.variety, 0.0, 1.0, 1.0);
      g.strange = finite(g.strange, 0.0, 1.0, 0.3);
      g.variants = std::clamp(g.variants, 1, 8);
      gen_ = g;
      t.start = finite(t.start, 0.0, 1.0, 0.0);
      t.end = finite(t.end, 0.0, 1.0, 1.0);
      t.fadeMs = finite(t.fadeMs, 0.0, 2000.0, 0.0);
      t.fadeInMs = finite(t.fadeInMs, 0.0, 2000.0, 0.0);
      treatment_ = t;
      recent_ = std::move(recent);
      page = std::clamp(pageRead, 0, 1);
      if (version < 3)
         page = 1 - page; // the tabs were Generate, Kit
      selectedPad = std::clamp(selected, 0, kPads - 1);
      targetPad = std::clamp(target, 0, kPads - 1);
      SamplePtr old[kPads];
      {
         std::lock_guard<std::mutex> lock(kitMutex_);
         for (int i = 0; i < kPads; ++i) {
            old[i] = std::move(pads_[i]);
            pads_[i] = std::move(pads[i]);
            voices_[i] = Voice{};
            fading_[i] = Voice{};
         }
      }
      if (auto *hostParams = hostExtension<clap_host_params_t>(CLAP_EXT_PARAMS))
         hostParams->rescan(host(), CLAP_PARAM_RESCAN_VALUES);
      return true;
   }

   // ---- audio ----

   clap_process_status process(const clap_process_t *proc) override {
      edits_.drain(proc->out_events);
      std::unique_lock<std::mutex> lock(kitMutex_, std::try_to_lock);
      handleEvents(proc->in_events, lock.owns_lock());
      if (proc->audio_outputs_count < 1 || !proc->audio_outputs[0].data32 || proc->audio_outputs[0].channel_count < 2)
         return CLAP_PROCESS_CONTINUE;
      float *outL = proc->audio_outputs[0].data32[0], *outR = proc->audio_outputs[0].data32[1];
      const uint32_t n = proc->frames_count;
      std::fill(outL, outL + n, 0.0f);
      std::fill(outR, outR + n, 0.0f);
      if (!lock.owns_lock())
         return CLAP_PROCESS_CONTINUE;
      PanelNote p;
      while (panelNotes_.pop(p))
         p.on ? startPad(p.pad, p.velocity) : stopPadVoice(p.pad);

      if (proc->transport && (proc->transport->flags & CLAP_TRANSPORT_HAS_TEMPO) && proc->transport->tempo > 0)
         tempo_ = proc->transport->tempo;
      updateFx();

      buffers(n);
      float *kl = kitL_.data(), *kr = kitR_.data(), *rl = revL_.data(), *rr = revR_.data(), *dl = dlyL_.data(),
            *dr = dlyR_.data();
      std::fill(kl, kl + n, 0.0f);
      std::fill(kr, kr + n, 0.0f);
      std::fill(rl, rl + n, 0.0f);
      std::fill(rr, rr + n, 0.0f);
      std::fill(dl, dl + n, 0.0f);
      std::fill(dr, dr + n, 0.0f);

      for (int pad = 0; pad < kPads; ++pad)
         renderPad(pad, n, kl, kr, rl, rr, dl, dr);
      renderPreview(n, kl, kr);

      // The returns, then the master: EQ, width, boom, volume.
      const float revGain = dbToGain(value(kReverbReturn)), dlyGain = dbToGain(value(kDelayReturn));
      masterEq_.set(sampleRate_, 30.0, value(kMasterLow), value(kMasterMid), midFreqHz(value(kMasterMidFreq)),
                    value(kMasterHigh), 18000.0);
      const double volTarget = dbToGain(value(kVolume)), widthTarget = value(kWidth), boom = value(kBoom);
      const double volStep = (volTarget - volume_) / n, widthStep = (widthTarget - width_) / n;
      float kitPeak = 0, fxPeak = 0, outPeak = 0;
      for (uint32_t i = 0; i < n; ++i) {
         // Wet only: each effect adds to what it's given, so take the dry back off.
         float wl = rl[i], wr = rr[i];
         room_.tick(wl, wr);
         wl -= rl[i];
         wr -= rr[i];
         float el = dl[i], er = dr[i];
         delay_.tick(el, er);
         el -= dl[i];
         er -= dr[i];
         const float fxl = wl * revGain + el * dlyGain, fxr = wr * revGain + er * dlyGain;
         kitPeak = std::max(kitPeak, std::max(std::fabs(kl[i]), std::fabs(kr[i])));
         fxPeak = std::max(fxPeak, std::max(std::fabs(fxl), std::fabs(fxr)));
         float l = kl[i] + fxl, r = kr[i] + fxr;
         masterEq_.tick(l, r);
         width_ += widthStep;
         const float mid = 0.5f * (l + r), side = 0.5f * (l - r) * static_cast<float>(width_);
         l = mid + side;
         r = mid - side;
         if (boom > 0.0) {
            // BOOM: the lows under 110 Hz, driven into a soft clip and added back. The clip flattens
            // the long tail more than the hit, so it reads as punch, not just more bass.
            const float low = boomLp_.tick(0, mid);
            const float drive = static_cast<float>(1.0 + 5.0 * boom);
            const float punch = static_cast<float>(boom * 1.4) * std::tanh(low * drive) / std::tanh(drive) * 1.0f;
            l += punch;
            r += punch;
         }
         volume_ += volStep;
         l *= static_cast<float>(volume_);
         r *= static_cast<float>(volume_);
         outL[i] = l;
         outR[i] = r;
         outPeak = std::max(outPeak, std::max(std::fabs(l), std::fabs(r)));
      }
      volume_ = volTarget;
      width_ = widthTarget;
      raise(kitPeak_, kitPeak);
      raise(fxPeak_, fxPeak);
      raise(outPeak_, outPeak);
      return CLAP_PROCESS_CONTINUE;
   }

private:
   static float dbToGain(double db) { return db <= -59.95 ? 0.0f : static_cast<float>(std::pow(10.0, db / 20.0)); }
   static void raise(std::atomic<float> &a, float v) {
      float seen = a.load(std::memory_order_relaxed);
      while (v > seen && !a.compare_exchange_weak(seen, v)) {
      }
   }

   double value(clap_id id) const {
      return std::clamp(values_[id].load() + mods_[id].load(), param(id).min, param(id).max);
   }
   double padValue(int pad, PadParam f) const { return value(padParam(pad, f)); }

   gen::Studio &studio() {
      if (!studio_)
         studio_ = std::make_unique<gen::Studio::Handle>();
      return **studio_;
   }

   void markDirty() {
      if (auto *s = hostExtension<clap_host_state_t>(CLAP_EXT_STATE))
         s->mark_dirty(host());
   }

   void remember(const std::string &prompt, int role, uint32_t seed) {
      recent_.erase(std::remove_if(recent_.begin(), recent_.end(), [&](const Recent &r) { return r.prompt == prompt; }),
                    recent_.end());
      recent_.insert(recent_.begin(), {prompt, role, seed, static_cast<int64_t>(std::time(nullptr))});
      if (recent_.size() > 12)
         recent_.resize(12);
   }

   void buffers(uint32_t n) {
      for (std::vector<float> *b : {&kitL_, &kitR_, &revL_, &revR_, &dlyL_, &dlyR_, &padL_, &padR_})
         if (b->size() < n)
            b->resize(n); // only when a host sends a block bigger than any before
   }

   void silence() {
      for (int i = 0; i < kPads; ++i) {
         voices_[i] = Voice{};
         fading_[i] = Voice{};
         positions_[i].store(-1.0f);
         levels_[i].store(0.0f);
      }
      previewVoice_ = Voice{};
      previewPos_.store(-1.0f);
   }

   void updateFx() {
      jclap::dsp::RoomSettings room;
      static const double damp[kReverbTypeCount] = {0.5, 0.4, 0.15, 0.85};
      static const double pre[kReverbTypeCount] = {8.0, 24.0, 2.0, 14.0};
      static const double sizeBoost[kReverbTypeCount] = {0.0, 0.35, 0.1, 0.1};
      const int type = std::clamp(static_cast<int>(value(kReverbType) + 0.5), 0, kReverbTypeCount - 1);
      room.mix = 1.0;
      room.size = std::clamp(value(kReverbSize) * (1.0 - sizeBoost[type]) + sizeBoost[type], 0.0, 1.0);
      room.decay = value(kReverbDecay);
      room.damp = damp[type];
      room.preDelayMs = pre[type];
      room_.set(room);
      jclap::dsp::DelaySettings d;
      d.on = true;
      d.mix = 1.0;
      const int sync = std::clamp(static_cast<int>(value(kDelaySync) + 0.5), 0, kDelaySyncChoices - 1) + 1;
      d.timeMs = std::min(jclap::dsp::StereoDelay::kMaxSeconds * 1000.0 - 10.0,
                          jclap::dsp::delaySyncBeats(sync) * 60000.0 / tempo_);
      d.feedback = value(kDelayFeedback);
      d.tone = value(kDelayTone);
      d.spread = 0.6;
      delay_.set(d);
   }

   // ---- notes ----

   void handleEvents(const clap_input_events_t *in, bool canPlay) {
      const uint32_t count = in->size(in);
      for (uint32_t i = 0; i < count; ++i) {
         const clap_event_header_t *h = in->get(in, i);
         if (h->space_id != CLAP_CORE_EVENT_SPACE_ID)
            continue;
         switch (h->type) {
         case CLAP_EVENT_PARAM_VALUE: {
            auto *ev = reinterpret_cast<const clap_event_param_value_t *>(h);
            if (ev->param_id < kParamCount && std::isfinite(ev->value))
               values_[ev->param_id].store(std::clamp(ev->value, param(ev->param_id).min, param(ev->param_id).max));
            break;
         }
         case CLAP_EVENT_PARAM_MOD: {
            auto *ev = reinterpret_cast<const clap_event_param_mod_t *>(h);
            if (ev->param_id < kParamCount && std::isfinite(ev->amount))
               mods_[ev->param_id].store(ev->amount);
            break;
         }
         case CLAP_EVENT_NOTE_ON:
         case CLAP_EVENT_NOTE_OFF:
         case CLAP_EVENT_NOTE_CHOKE: {
            if (!canPlay)
               break;
            auto *ev = reinterpret_cast<const clap_event_note_t *>(h);
            if (h->type == CLAP_EVENT_NOTE_ON && ev->velocity > 0.0)
               startPad(ev->key - kFirstNote, static_cast<float>(ev->velocity));
            else if (h->type == CLAP_EVENT_NOTE_CHOKE)
               chokePad(ev->key - kFirstNote, ev->key == -1);
            else
               stopPadVoice(ev->key - kFirstNote);
            break;
         }
         case CLAP_EVENT_MIDI: {
            if (!canPlay)
               break;
            auto *ev = reinterpret_cast<const clap_event_midi_t *>(h);
            const int status = ev->data[0] & 0xF0;
            if (status == 0x90 && ev->data[2] > 0)
               startPad(ev->data[1] - kFirstNote, ev->data[2] / 127.0f);
            else if (status == 0x80 || status == 0x90)
               stopPadVoice(ev->data[1] - kFirstNote);
            else if (status == 0xB0 && (ev->data[1] == 120 || ev->data[1] == 123))
               chokePad(-1, true);
            break;
         }
         default:
            break;
         }
      }
   }

   // Moves a pad's voice to its fading slot, to fade out over `ms`.
   void fadeOut(int pad, double ms) {
      Voice &v = voices_[pad];
      if (!v.on())
         return;
      fading_[pad] = v;
      fading_[pad].fadeStep = 1.0 / std::max(1.0, ms * 0.001 * sampleRate_);
      v = Voice{};
   }

   void chokePad(int pad, bool all) {
      for (int p = 0; p < kPads; ++p)
         if (all || p == pad)
            fadeOut(p, 5.0);
   }

   void startPad(int pad, float velocity) {
      if (pad < 0 || pad >= kPads)
         return;
      const Sample *s = pads_[pad].get();
      // The choke group: starting this pad cuts the others in it (an open hat by a closed one).
      const int group = static_cast<int>(padValue(pad, kChoke) + 0.5);
      if (group > 0)
         for (int p = 0; p < kPads; ++p)
            if (p != pad && static_cast<int>(padValue(p, kChoke) + 0.5) == group)
               fadeOut(p, 5.0);
      fadeOut(pad, 4.0);
      lastHitPad_.store(pad);
      padHits_.fetch_add(1);
      if (!s || !s->frames())
         return;
      Voice v;
      v.sample = s;
      const double frames = static_cast<double>(s->frames());
      double a = padValue(pad, kStart) * frames, z = padValue(pad, kEnd) * frames;
      if (z < a + 2)
         z = std::min(frames, a + 2);
      v.from = a;
      v.to = z;
      v.reverse = padValue(pad, kReverse) >= 0.5;
      v.pos = v.reverse ? z - 1 : a;
      const double attack = attackMs(padValue(pad, kAttack)) * 0.001 * sampleRate_;
      v.attacking = attack >= 1.0;
      v.env = v.attacking ? 0.0 : 1.0;
      v.attackStep = v.attacking ? 1.0 / attack : 1.0;
      const double decay = padValue(pad, kDecay);
      v.decayMul = decayFull(decay) ? 1.0 : std::pow(0.001, 1.0 / (decayMs(decay) * 0.001 * sampleRate_));
      const double sens = padValue(pad, kVelocity);
      // Velocity sets the level on a power curve: at 1/3 a half-velocity hit is 6 dB down, at the
      // most 18 dB.
      v.gain = static_cast<float>(std::pow(static_cast<double>(velocity), 3.0 * sens));
      v.velocity = velocity;
      voices_[pad] = v;
      // The filter envelope starts again with every hit, at the hit's velocity.
      PadTone &tone = tone_[pad];
      tone.env = 1.0;
      tone.velocity = velocity;
   }

   void stopPadVoice(int pad) {
      if (pad < 0 || pad >= kPads)
         return;
      // Gate: the note's end lets the pad go over 30 ms; a one-shot plays out.
      if (padValue(pad, kPlayMode) >= 0.5 && voices_[pad].on() && !voices_[pad].released) {
         voices_[pad].released = true;
         voices_[pad].releaseMul = std::pow(0.001, 1.0 / (0.03 * sampleRate_));
      }
   }

   // One output sample of a voice into l and r; false once it has finished.
   bool tickVoice(Voice &v, double step, float &l, float &r) {
      const Sample &s = *v.sample;
      const double edge = v.reverse ? v.pos - v.from : v.to - v.pos;
      if (edge <= 0.0 || v.fade <= 0.0 || v.env < 1e-5 * (v.attacking ? -1 : 1)) {
         v = Voice{};
         return false;
      }
      // Hermite interpolation around pos.
      const long n = static_cast<long>(s.frames());
      const long i = static_cast<long>(std::floor(v.pos));
      const float t = static_cast<float>(v.pos - static_cast<double>(i));
      const auto at = [&](const std::vector<float> &d, long k) { return d[static_cast<size_t>(std::clamp(k, 0L, n - 1))]; };
      const auto hermite = [&](const std::vector<float> &d) {
         const float y0 = at(d, i - 1), y1 = at(d, i), y2 = at(d, i + 1), y3 = at(d, i + 2);
         const float c1 = 0.5f * (y2 - y0), c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3,
                     c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
         return ((c3 * t + c2) * t + c1) * t + y1;
      };
      // Envelope: attack, then the decay, times the release and the de-click fade, and a 1.5 ms
      // fade at the region's far end so a cut End never clicks.
      if (v.attacking) {
         v.env += v.attackStep;
         if (v.env >= 1.0) {
            v.env = 1.0;
            v.attacking = false;
         }
      } else
         v.env *= v.decayMul;
      if (v.released)
         v.env *= v.releaseMul;
      if (v.fadeStep > 0.0)
         v.fade -= v.fadeStep;
      const double endFade = std::min(1.0, edge / (0.0015 * kSampleRate));
      const float g = static_cast<float>(v.env * std::max(0.0, v.fade) * endFade) * v.gain;
      l = hermite(s.left) * g;
      r = hermite(s.right) * g;
      v.pos += v.reverse ? -step : step;
      if (v.env < 1e-5 && !v.attacking) {
         v = Voice{};
         return false;
      }
      return true;
   }

   void renderPad(int pad, uint32_t n, float *kl, float *kr, float *rl, float *rr, float *dl, float *dr) {
      Voice &v = voices_[pad], &f = fading_[pad];
      if (!v.on() && !f.on()) {
         positions_[pad].store(-1.0f);
         levels_[pad].store(0.0f);
         return;
      }
      const double step = kSampleRate / sampleRate_ * std::pow(2.0, padValue(pad, kPitch) / 12.0);
      float *pl = padL_.data(), *pr = padR_.data();
      for (uint32_t i = 0; i < n; ++i) {
         float l = 0, r = 0, a, b;
         if (v.on() && tickVoice(v, step, a, b)) {
            l += a;
            r += b;
         }
         if (f.on() && tickVoice(f, step, a, b)) {
            l += a;
            r += b;
         }
         pl[i] = l;
         pr[i] = r;
      }
      tonePad(pad, n, pl, pr);
      const float level = dbToGain(padValue(pad, kLevel));
      const double angle = (padValue(pad, kPan) + 1.0) * 0.25 * kPi;
      const float gl = level * static_cast<float>(std::cos(angle) * std::sqrt(2.0)),
                  gr = level * static_cast<float>(std::sin(angle) * std::sqrt(2.0));
      const float rs = static_cast<float>(padValue(pad, kReverbSend)), ds = static_cast<float>(padValue(pad, kDelaySend));
      float peak = 0.0f;
      for (uint32_t i = 0; i < n; ++i) {
         float l = pl[i], r = pr[i];
         l *= gl;
         r *= gr;
         kl[i] += l;
         kr[i] += r;
         rl[i] += l * rs;
         rr[i] += r * rs;
         dl[i] += l * ds;
         dr[i] += r * ds;
         peak = std::max(peak, std::max(std::fabs(l), std::fabs(r)));
      }
      const Voice &shown = v.on() ? v : f;
      positions_[pad].store(shown.on() ? static_cast<float>(shown.pos / static_cast<double>(shown.sample->frames()))
                                       : -1.0f);
      levels_[pad].store(std::min(1.0f, peak));
   }

   // The pad's drive, then its tone points (the filters among them moved by the envelope and the
   // hit's velocity), in place.
   void tonePad(int pad, uint32_t n, float *pl, float *pr) {
      PadTone &t = tone_[pad];
      const double drive = padValue(pad, kDrive);
      if (drive > 0.001) {
         // A soft clip, up to 24 dB into it, brought back to about the same loudness.
         const float in = static_cast<float>(std::pow(10.0, drive * 24.0 / 20.0));
         const float out = 1.0f / std::tanh(in) * static_cast<float>(1.0 - 0.3 * drive);
         for (uint32_t i = 0; i < n; ++i) {
            pl[i] = std::tanh(pl[i] * in) * out;
            pr[i] = std::tanh(pr[i] * in) * out;
         }
      }
      const double envOct = padValue(pad, kFilterEnv) * kFilterEnvOctaves;
      // Velocity to Filter: at +100% a hit at velocity 0 is 4 octaves lower than a full one.
      const double velOct = padValue(pad, kVelFilter) * kVelFilterOctaves * (static_cast<double>(t.velocity) - 1.0);
      const double envMul = std::pow(0.001, 1.0 / (filterDecayMs(padValue(pad, kFilterDecay)) * 0.001 * sampleRate_));
      const bool moving = std::fabs(envOct) > 1e-6 && t.env > 1e-4;
      for (int i = 0; i < kTonePoints; ++i) {
         PadTone::Point &pt = t.points[i];
         const int type = static_cast<int>(padValue(pad, pointParam(i, kPointType)) + 0.5);
         if (type != pt.type) {
            pt.type = type;
            pt.reset();
            pt.key[0] = -1.0;
         }
         if (type == kToneOff)
            continue;
         const double hz = toneHz(padValue(pad, pointParam(i, kPointFreq)));
         const double db = padValue(pad, pointParam(i, kPointGain)), q = toneQ(padValue(pad, pointParam(i, kPointQ)));
         const bool mod = toneIsFilter(type);
         const double shift = mod ? velOct : 0.0;
         if (mod && moving) {
            // The envelope moves it: new coefficients every 16 samples.
            double env = t.env;
            for (uint32_t j = 0; j < n; j += 16) {
               pt.n = toneDesign(type, sampleRate_, hz * std::exp2(shift + envOct * env), db, q, pt.k);
               const uint32_t e = std::min(n, j + 16);
               pt.run(pl + j, pr + j, e - j);
               env *= std::pow(envMul, static_cast<double>(e - j));
            }
            pt.key[0] = -1.0;
         } else {
            const double key[4] = {hz * std::exp2(shift), db, q, sampleRate_};
            if (!std::equal(key, key + 4, pt.key)) {
               pt.n = toneDesign(type, sampleRate_, key[0], db, q, pt.k);
               std::copy(key, key + 4, pt.key);
            }
            pt.run(pl, pr, n);
         }
      }
      t.env *= std::pow(envMul, static_cast<double>(n));
   }

   void renderPreview(uint32_t n, float *kl, float *kr) {
      Voice &v = previewVoice_;
      if (!v.on()) {
         previewPos_.store(-1.0f);
         return;
      }
      const double step = kSampleRate / sampleRate_;
      for (uint32_t i = 0; i < n && v.on(); ++i) {
         float l, r;
         if (tickVoice(v, step, l, r)) {
            kl[i] += l;
            kr[i] += r;
         }
      }
      previewPos_.store(v.on() ? static_cast<float>(v.pos / static_cast<double>(v.sample->frames())) : -1.0f);
   }

   std::atomic<double> values_[kParamCount];
   std::atomic<double> mods_[kParamCount];
   jclap::core::EditQueue edits_;
   jclap::ui::HostMenu hostMenu_;
   double sampleRate_ = 48000.0, tempo_ = 120.0;
   std::atomic<bool> active_{false};

   // The kit, under kitMutex_ (the audio thread holds it for a whole block).
   mutable std::mutex kitMutex_;
   SamplePtr pads_[kPads];
   SamplePtr preview_;
   Voice voices_[kPads], fading_[kPads], previewVoice_;
   Eq masterEq_;
   struct PadTone {
      struct Point {
         int type = -1;
         ToneCoef k[2];
         int n = 0;
         double z[2][2][2] = {}; // stage, channel, state
         double key[4] = {-1, -1, -1, -1};
         void reset() { std::fill(&z[0][0][0], &z[0][0][0] + 8, 0.0); }
         void run(float *l, float *r, uint32_t count) {
            for (int s = 0; s < n; ++s) {
               const ToneCoef &c = k[s];
               for (int ch = 0; ch < 2; ++ch) {
                  float *x = ch ? r : l;
                  double z0 = z[s][ch][0], z1 = z[s][ch][1];
                  for (uint32_t i = 0; i < count; ++i) {
                     // Transposed direct form II.
                     const double in = x[i], y = c.b0 * in + z0;
                     z0 = c.b1 * in - c.a1 * y + z1;
                     z1 = c.b2 * in - c.a2 * y;
                     x[i] = static_cast<float>(y);
                  }
                  z[s][ch][0] = std::fabs(z0) < 1e-20 ? 0.0 : z0;
                  z[s][ch][1] = std::fabs(z1) < 1e-20 ? 0.0 : z1;
               }
            }
         }
      };
      Point points[kTonePoints];
      double env = 0.0;  // the filter envelope, 1 at a hit
      float velocity = 1.0f;
   };
   PadTone tone_[kPads];
   Biquad boomLp_;
   jclap::dsp::Room room_;
   jclap::dsp::StereoDelay delay_;
   double volume_ = 1.0, width_ = 1.0;
   std::vector<float> kitL_, kitR_, revL_, revR_, dlyL_, dlyR_, padL_, padR_;

   // For the panel.
   jclap::core::SpscQueue<PanelNote> panelNotes_;
   std::atomic<float> positions_[kPads], levels_[kPads];
   std::atomic<uint32_t> padHits_{0};
   std::atomic<int> lastHitPad_{0};
   std::atomic<float> previewPos_{-1.0f};
   std::atomic<float> kitPeak_{0.0f}, fxPeak_{0.0f}, outPeak_{0.0f};

   // The Generate tab, on the main thread.
   GenSettings gen_;
   Treatment treatment_;
   std::vector<Variant> variants_;
   std::vector<Recent> recent_;
   std::string kitName_;
   std::unique_ptr<gen::Studio::Handle> studio_;
   int job_ = 0, jobRole_ = kPerc;
   std::string jobPrompt_;
   bool dropFirst_ = false;
   std::chrono::steady_clock::time_point jobStarted_;
   double lastRun_ = 0.0;
   uint64_t diskBytes_ = 0;
   std::chrono::steady_clock::time_point diskCheckedAt_{};
   gen::Studio::State lastState_ = gen::Studio::State::Missing;
   gen::Engine lastEngine_ = gen::Engine::SaoSmall, jobEngine_ = gen::Engine::SaoSmall;

   // Last, so the panel closes before the state it reads goes away.
   jclap::ui::ClapGui gui_;
};

const clap_plugin_descriptor_t JKit::descriptor = {
   CLAP_VERSION_INIT,
   "com.jeffmhopkins.jclap.jkit",
   "JKit",
   "jeffmhopkins",
   "https://github.com/jeffmhopkins/Jclap",
   "",
   "",
   JCLAP_PLUGIN_VERSION,
   "A 16-pad drum kit whose sounds an AI model makes from your prompts, inside the plugin",
   kFeatures,
};

} // namespace

JCLAP_ENTRY(JKit)

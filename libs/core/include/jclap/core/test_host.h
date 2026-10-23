#pragma once

// A tiny in-process host for plugin tests: creates a plugin from clap_entry, feeds it param, mod
// note and MIDI events, runs blocks (with optional stereo audio) and collects what it sends back.
//
//    jclap::core::TestHost host(clap_entry, "com.jeffmhopkins.jclap.myplugin");
//    host.setParam(kGain, -6.0);
//    auto out = host.process();
//    JCLAP_CHECK_EQ(out.paramValues.size(), 0u);
//    return jclap::core::testResult();

#include <clap/clap.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "jclap/core/plugin.h"

namespace jclap::core {

inline int &testFailures() {
   static int failures = 0;
   return failures;
}

// Prints a summary and returns main()'s exit code.
inline int testResult() {
   if (testFailures())
      std::printf("%d failure(s)\n", testFailures());
   return testFailures() ? 1 : 0;
}

#define JCLAP_CHECK(cond)                                                \
   do {                                                                  \
      if (!(cond)) {                                                     \
         std::printf("%s:%d: check failed: %s\n", __FILE__, __LINE__, #cond); \
         ++jclap::core::testFailures();                                  \
      }                                                                  \
   } while (0)

#define JCLAP_CHECK_EQ(a, b)                                                                               \
   do {                                                                                                    \
      const auto va = (a);                                                                                 \
      const auto vb = (b);                                                                                 \
      if (!(va == vb)) {                                                                                   \
         std::printf("%s:%d: %s == %g, expected %g\n", __FILE__, __LINE__, #a, (double)va, (double)vb); \
         ++jclap::core::testFailures();                                                                    \
      }                                                                                                    \
   } while (0)

// A list of CLAP events usable as both input and output.
struct TestEventList {
   std::vector<std::vector<char>> events;

   void add(const clap_event_header_t &h) {
      const char *p = reinterpret_cast<const char *>(&h);
      events.emplace_back(p, p + h.size);
   }
   const clap_event_header_t *at(size_t i) const {
      return reinterpret_cast<const clap_event_header_t *>(events[i].data());
   }

   clap_input_events_t input() { return {this, size, get}; }
   clap_output_events_t output() { return {this, push}; }

private:
   static uint32_t size(const clap_input_events_t *l) {
      return static_cast<uint32_t>(static_cast<TestEventList *>(l->ctx)->events.size());
   }
   static const clap_event_header_t *get(const clap_input_events_t *l, uint32_t i) {
      return static_cast<TestEventList *>(l->ctx)->at(i);
   }
   static bool push(const clap_output_events_t *l, const clap_event_header_t *h) {
      static_cast<TestEventList *>(l->ctx)->add(*h);
      return true;
   }
};

struct TestMidi {
   uint8_t status, data1, data2;
   uint32_t time;
};

struct TestOutput {
   std::vector<TestMidi> midi;
   std::vector<clap_event_param_value_t> paramValues;
   std::vector<clap_event_param_gesture_t> gestures; // header.type tells begin from end
   std::vector<clap_event_note_t> noteEnds;
};

class TestHost {
public:
   TestHost(const clap_plugin_entry_t &entry, const char *pluginId, double sampleRate = 48000) {
      entry.init("");
      auto *factory = static_cast<const clap_plugin_factory_t *>(entry.get_factory(CLAP_PLUGIN_FACTORY_ID));
      plugin_ = factory->create_plugin(factory, &host_, pluginId);
      plugin_->init(plugin_);
      plugin_->activate(plugin_, sampleRate, 1, kMaxFrames);
      plugin_->start_processing(plugin_);
      params_ = static_cast<const clap_plugin_params_t *>(plugin_->get_extension(plugin_, CLAP_EXT_PARAMS));
   }

   ~TestHost() {
      plugin_->stop_processing(plugin_);
      plugin_->deactivate(plugin_);
      plugin_->destroy(plugin_);
   }

   const clap_plugin_t *plugin() const { return plugin_; }
   // The plugin object, for a jclap::core::Plugin subclass (or any base of it, such as the
   // interface its GUI reads). Null if it isn't one.
   template <typename T>
   T *object() const {
      return dynamic_cast<T *>(Plugin::from(plugin_));
   }

   void setParam(clap_id id, double value, uint32_t time = 0) {
      clap_event_param_value_t ev{};
      ev.header = {sizeof(ev), time, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_PARAM_VALUE, 0};
      ev.param_id = id;
      ev.note_id = ev.port_index = ev.channel = ev.key = -1;
      ev.value = value;
      in_.add(ev.header);
   }

   // A monophonic modulation, as Bitwig sends for a modulator with no note.
   void modulate(clap_id id, double amount, uint32_t time = 0) {
      clap_event_param_mod_t ev{};
      ev.header = {sizeof(ev), time, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_PARAM_MOD, 0};
      ev.param_id = id;
      ev.note_id = ev.port_index = ev.channel = ev.key = -1;
      ev.amount = amount;
      in_.add(ev.header);
   }

   // A CLAP note event: CLAP_EVENT_NOTE_ON, NOTE_OFF or NOTE_CHOKE.
   void note(uint16_t type, int16_t key, double velocity = 1.0, int32_t noteId = -1, int16_t channel = 0,
             uint32_t time = 0) {
      clap_event_note_t ev{};
      ev.header = {sizeof(ev), time, CLAP_CORE_EVENT_SPACE_ID, type, 0};
      ev.note_id = noteId;
      ev.port_index = 0;
      ev.channel = channel;
      ev.key = key;
      ev.velocity = velocity;
      in_.add(ev.header);
   }

   // A per-note expression (CLAP_NOTE_EXPRESSION_TUNING, PRESSURE, ...), by note id or key.
   void noteExpression(clap_note_expression expression, int16_t key, double value, int32_t noteId = -1,
                       int16_t channel = 0, uint32_t time = 0) {
      clap_event_note_expression_t ev{};
      ev.header = {sizeof(ev), time, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_NOTE_EXPRESSION, 0};
      ev.expression_id = expression;
      ev.note_id = noteId;
      ev.port_index = 0;
      ev.channel = channel;
      ev.key = key;
      ev.value = value;
      in_.add(ev.header);
   }

   // A polyphonic modulation for the voices a note id (or key and channel) names, as Bitwig sends
   // for a per-voice modulator.
   void modulateNote(clap_id id, double amount, int32_t noteId, int16_t key = -1, int16_t channel = -1,
                     uint32_t time = 0) {
      clap_event_param_mod_t ev{};
      ev.header = {sizeof(ev), time, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_PARAM_MOD, 0};
      ev.param_id = id;
      ev.note_id = noteId;
      ev.port_index = -1;
      ev.channel = channel;
      ev.key = key;
      ev.amount = amount;
      in_.add(ev.header);
   }

   void midi(uint8_t status, uint8_t data1, uint8_t data2, uint32_t time = 0) {
      clap_event_midi_t ev{};
      ev.header = {sizeof(ev), time, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_MIDI, 0};
      ev.data[0] = status;
      ev.data[1] = data1;
      ev.data[2] = data2;
      in_.add(ev.header);
   }

   // The value the host would read back.
   double paramValue(clap_id id) const {
      double v = -1;
      params_->get_value(plugin_, id, &v);
      return v;
   }

   std::string paramText(clap_id id, double value) const {
      char text[256] = {};
      params_->value_to_text(plugin_, id, value, text, sizeof(text));
      return text;
   }

   bool textToValue(clap_id id, const char *text, double &value) const {
      return params_->text_to_value(plugin_, id, text, &value);
   }

   // Runs one block. Audio buffers, if given, are stereo and `frames` long; output may alias input.
   TestOutput process(uint32_t frames = 256, float *const *audioIn = nullptr, float **audioOut = nullptr) {
      TestEventList out;
      const clap_input_events_t in = in_.input();
      const clap_output_events_t outEvents = out.output();
      clap_audio_buffer_t inBuf{}, outBuf{};
      clap_process_t proc{};
      proc.frames_count = frames;
      proc.steady_time = -1;
      proc.in_events = &in;
      proc.out_events = &outEvents;
      if (audioIn && audioOut) {
         inBuf.data32 = const_cast<float **>(audioIn);
         inBuf.channel_count = 2;
         outBuf.data32 = audioOut;
         outBuf.channel_count = 2;
         proc.audio_inputs = &inBuf;
         proc.audio_inputs_count = 1;
         proc.audio_outputs = &outBuf;
         proc.audio_outputs_count = 1;
      }
      plugin_->process(plugin_, &proc);
      in_.events.clear();
      return collect(out);
   }

   // Calls params flush (what a host does while not processing) and returns what came out.
   TestOutput flush() {
      TestEventList out;
      const clap_input_events_t in = in_.input();
      const clap_output_events_t outEvents = out.output();
      params_->flush(plugin_, &in, &outEvents);
      in_.events.clear();
      return collect(out);
   }

   static constexpr uint32_t kMaxFrames = 1024;

private:
   static TestOutput collect(const TestEventList &out) {
      TestOutput result;
      for (size_t i = 0; i < out.events.size(); ++i) {
         const clap_event_header_t *h = out.at(i);
         if (h->space_id != CLAP_CORE_EVENT_SPACE_ID)
            continue;
         if (h->type == CLAP_EVENT_MIDI) {
            auto *m = reinterpret_cast<const clap_event_midi_t *>(h);
            result.midi.push_back({m->data[0], m->data[1], m->data[2], h->time});
         }
         else if (h->type == CLAP_EVENT_PARAM_VALUE)
            result.paramValues.push_back(*reinterpret_cast<const clap_event_param_value_t *>(h));
         else if (h->type == CLAP_EVENT_NOTE_END)
            result.noteEnds.push_back(*reinterpret_cast<const clap_event_note_t *>(h));
         else if (h->type == CLAP_EVENT_PARAM_GESTURE_BEGIN || h->type == CLAP_EVENT_PARAM_GESTURE_END)
            result.gestures.push_back(*reinterpret_cast<const clap_event_param_gesture_t *>(h));
      }
      return result;
   }

   static const void *getExtension(const clap_host_t *, const char *) { return nullptr; }
   static void noop(const clap_host_t *) {}

   const clap_host_t host_ = {CLAP_VERSION_INIT, nullptr, "jclap test", "jclap", "", "1", getExtension,
                              noop, noop, noop};
   const clap_plugin_t *plugin_ = nullptr;
   const clap_plugin_params_t *params_ = nullptr;
   TestEventList in_;
};

} // namespace jclap::core

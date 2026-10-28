// Loads the built PolarXY.vst3 the way a VST3 host does and checks that the CCs it converts reach
// the host. The VST3 is the CLAP wrapped by clap-wrapper: input CCs arrive as the host's MIDI-mapped
// parameters, and the angle/radius CCs go out as legacy MIDI CC events. Usage: polar-xy-vst3-tests
// <path to PolarXY.vst3>.

#include "public.sdk/source/vst/hosting/eventlist.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/vst/hosting/module.h"
#include "public.sdk/source/vst/hosting/parameterchanges.h"
#include "public.sdk/source/vst/hosting/processdata.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstmidicontrollers.h"

#include <cstdio>
#include <string>
#include <vector>

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace {

int failures = 0;

#define CHECK(cond)                                                       \
   do {                                                                   \
      if (!(cond)) {                                                      \
         std::fprintf(stderr, "%s:%d: CHECK(%s)\n", __FILE__, __LINE__, #cond); \
         ++failures;                                                      \
      }                                                                   \
   } while (0)

constexpr int32 kBlock = 64;

struct CC {
   int channel, number, value;
};

// One loaded, active instance of the plugin.
struct Instance {
   VST3::Hosting::Module::Ptr module;
   IPtr<IComponent> component;
   FUnknownPtr<IAudioProcessor> processor;
   FUnknownPtr<IMidiMapping> midiMapping;
   HostProcessData data;

   bool load(const std::string &path, HostApplication &host) {
      std::string error;
      module = VST3::Hosting::Module::create(path, error);
      if (!module) {
         std::fprintf(stderr, "can't load %s: %s\n", path.c_str(), error.c_str());
         return false;
      }
      const auto factory = module->getFactory();
      for (const auto &info : factory.classInfos())
         if (info.category() == kVstAudioEffectClass) {
            component = factory.createInstance<IComponent>(info.ID());
            if (component)
               break;
         }
      if (!component || component->initialize(static_cast<IHostApplication *>(&host)) != kResultOk)
         return false;
      // clap-wrapper makes a single component: the edit controller and its MIDI mapping are the
      // component itself.
      processor = FUnknownPtr<IAudioProcessor>(component);
      midiMapping = FUnknownPtr<IMidiMapping>(component);
      if (!processor || !midiMapping)
         return false;

      ProcessSetup setup{kRealtime, kSample32, kBlock, 48000.0};
      if (processor->setupProcessing(setup) != kResultOk)
         return false;
      for (auto type : {kAudio, kEvent})
         for (auto dir : {kInput, kOutput})
            for (int32 i = 0; i < component->getBusCount(type, dir); ++i)
               component->activateBus(type, dir, i, true);
      if (component->setActive(true) != kResultOk || !data.prepare(*component, kBlock, kSample32))
         return false;
      processor->setProcessing(true);
      return true;
   }

   ~Instance() {
      if (processor)
         processor->setProcessing(false);
      if (component) {
         component->setActive(false);
         component->terminate();
      }
      data.unprepare();
   }

   // The parameter a host sends a CC on, or kNoParamId.
   ParamID ccParam(int channel, int cc) {
      ParamID id = kNoParamId;
      if (midiMapping->getMidiControllerAssignment(0, static_cast<int16>(channel), static_cast<CtrlNumber>(cc), id) !=
          kResultTrue)
         return kNoParamId;
      return id;
   }

   // Runs one block with these CCs (as the host's mapped parameters) and notes in, and returns the
   // CCs and note-ons that came out.
   void run(const std::vector<CC> &ccsIn, const std::vector<Event> &notesIn, std::vector<CC> &ccsOut,
            std::vector<Event> &notesOut) {
      ParameterChanges params;
      for (const CC &cc : ccsIn) {
         int32 index = 0;
         IParamValueQueue *queue = params.addParameterData(ccParam(cc.channel, cc.number), index);
         CHECK(queue != nullptr);
         if (queue)
            queue->addPoint(0, cc.value / 127.0, index);
      }
      EventList eventsIn, eventsOut;
      for (Event e : notesIn)
         eventsIn.addEvent(e);
      ParameterChanges paramsOut;
      data.numSamples = kBlock;
      data.inputParameterChanges = &params;
      data.outputParameterChanges = &paramsOut;
      data.inputEvents = &eventsIn;
      data.outputEvents = &eventsOut;
      CHECK(processor->process(data) == kResultOk);
      data.inputParameterChanges = data.outputParameterChanges = nullptr;
      data.inputEvents = data.outputEvents = nullptr;

      ccsOut.clear();
      notesOut.clear();
      for (int32 i = 0; i < eventsOut.getEventCount(); ++i) {
         Event e{};
         eventsOut.getEvent(i, e);
         if (e.type == Event::kLegacyMIDICCOutEvent)
            ccsOut.push_back({e.midiCCOut.channel, e.midiCCOut.controlNumber, e.midiCCOut.value});
         else if (e.type == Event::kNoteOnEvent || e.type == Event::kNoteOffEvent)
            notesOut.push_back(e);
      }
   }
};

bool has(const std::vector<CC> &ccs, int channel, int number, int value) {
   for (const CC &cc : ccs)
      if (cc.channel == channel && cc.number == number && cc.value == value)
         return true;
   return false;
}

} // namespace

int main(int argc, char **argv) {
   if (argc < 2) {
      std::fprintf(stderr, "usage: %s <PolarXY.vst3>\n", argv[0]);
      return 2;
   }
   HostApplication host;
   {
      Instance plugin;
      if (!plugin.load(argv[1], host)) {
         std::fprintf(stderr, "couldn't set up the plugin\n");
         return 1;
      }

      // The default input CCs (16 = X, 17 = Y) are mapped on every channel.
      CHECK(plugin.ccParam(0, 16) != kNoParamId);
      CHECK(plugin.ccParam(0, 17) != kNoParamId);
      CHECK(plugin.ccParam(15, 17) != kNoParamId);

      std::vector<CC> ccs;
      std::vector<Event> notes;

      // X at the right edge, Y centred: angle 0 on CC 18, full radius on CC 19.
      plugin.run({{0, 16, 127}, {0, 17, 64}}, {}, ccs, notes);
      CHECK(has(ccs, 0, 18, 0));
      CHECK(has(ccs, 0, 19, 127));
      // The XY CCs are swallowed.
      CHECK(!has(ccs, 0, 16, 127));

      // Straight up on channel 3: a quarter turn (127 / 4, rounded), still full radius.
      plugin.run({{2, 16, 64}, {2, 17, 127}}, {}, ccs, notes);
      CHECK(has(ccs, 2, 18, 32));
      CHECK(has(ccs, 2, 19, 127));

      // Notes pass through.
      Event on{};
      on.type = Event::kNoteOnEvent;
      on.noteOn.channel = 0;
      on.noteOn.pitch = 60;
      on.noteOn.velocity = 0.5f;
      on.noteOn.noteId = -1;
      plugin.run({}, {on}, ccs, notes);
      CHECK(notes.size() == 1 && notes[0].type == Event::kNoteOnEvent && notes[0].noteOn.pitch == 60);
      CHECK(ccs.empty());
   }

   if (failures)
      std::fprintf(stderr, "%d check(s) failed\n", failures);
   else
      std::printf("ok\n");
   return failures ? 1 : 0;
}

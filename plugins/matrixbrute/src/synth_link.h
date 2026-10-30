#pragma once

// The synth's presets as read off the MatrixBrute itself. In Bitwig a plugin can't send or receive
// SysEx, so the controller script in bitwig/MatrixBrute.control.js does the reading and serves the
// presets by OSC over UDP on localhost; this subscribes to them. Every instance of the plugin shares
// the one subscription.
//
//    SynthLink::Use use;            // connects while any panel holds one
//    SynthLink::get().requestRead();
//    if (SynthLink::get().version() != seen) ... SynthLink::get().presets() ...

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "patches.h"

namespace mb {

class SynthLink {
public:
   static constexpr int kPort = 58017;
   // Clients listen on one of these; the script opens a connection to each as it starts.
   static constexpr int kClientPortFirst = 58018, kClientPortCount = 8;

   static SynthLink &get();

   // Keeps the connection up (and retrying) while at least one exists.
   class Use {
   public:
      Use() { get().acquire(); }
      ~Use() { get().release(); }
      Use(const Use &) = delete;
      Use &operator=(const Use &) = delete;
   };

   struct Status {
      bool connected = false; // to the controller script
      bool reading = false;
      int found = 0;          // named presets so far in this read, or in the last one
      int failed = 0;         // slots that got no answer in the last read
      std::string error;      // why the last read stopped, if it did
   };

   // Asks the script to read every preset off the synth (about a minute). False if it isn't there.
   bool requestRead();
   Status status() const;
   // The presets of the last complete read, by preset number. Empty until one arrives.
   std::vector<HardwarePatch> presets() const;
   // Changes whenever presets() does.
   uint32_t version() const { return version_.load(); }

   // One line from the script (see the protocol in MatrixBrute.control.js). Public for tests.
   void handleLine(const std::string &line);
   // "PRESET\t5\tName\t9:12.5,10:0" into `out`; false if the line isn't a valid preset.
   static bool parsePreset(const std::string &line, HardwarePatch &out);

private:
   SynthLink() = default;
   void acquire();
   void release();
   void run();

   mutable std::mutex mutex_;
   Status status_;
   std::vector<HardwarePatch> presets_, incoming_;
   std::atomic<uint32_t> version_{0};
   std::atomic<bool> readRequested_{false};

   std::mutex useMutex_;
   int users_ = 0;
   std::atomic<bool> running_{false};
   std::thread thread_;
};

} // namespace mb

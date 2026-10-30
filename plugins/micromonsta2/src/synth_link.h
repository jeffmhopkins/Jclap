#pragma once

// Programs read off the MicroMonsta 2 itself. In Bitwig a plugin can't send or receive SysEx, so the
// controller script in bitwig/MicroMonsta2.control.js talks to the synth and passes its program dumps
// on by OSC over UDP on localhost; this subscribes to them. Every instance of the plugin shares the
// one subscription.
//
//    SynthLink::Use use;                          // connects while any panel holds one
//    const int ticket = SynthLink::get().requestSound('A');
//    if (SynthLink::get().sound().ticket == ticket) ... load it ...
//    if (SynthLink::get().bankVersion() != seen) ... SynthLink::get().bank() ...

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "patches.h"

namespace mm2 {

class SynthLink {
public:
   static constexpr int kPort = 58018;

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
      bool connected = false;   // to the controller script
      bool askingSound = false; // a sound request is waiting for its answer
      bool receiving = false;   // programs are coming in that nobody asked for (a U02 dump)
      int received = 0;         // programs so far in this dump, or in the last one
      std::string error;        // why the last request or dump failed, if it did
   };

   // A part's sound, as the synth answered the request numbered `ticket`. Its program is 0: the
   // synth doesn't say which program it came from.
   struct Sound {
      int ticket = 0;
      HardwarePatch patch;
   };

   // Asks the synth for the sound on part 'A' or 'B' (firmware 2.7 or later). Returns a ticket for
   // sound(), or 0 if the script isn't there.
   int requestSound(char part);
   Status status() const;
   Sound sound() const;
   // The programs of the last complete U02 dump (P001 first), as the preset bar lists them. Empty
   // until one arrives.
   std::vector<HardwarePatch> bank() const;
   // Changes whenever bank() does.
   uint32_t bankVersion() const { return bankVersion_.load(); }

   // One line from the script (see the protocol in MicroMonsta2.control.js). Public for tests.
   void handleLine(const std::string &line);
   // Hex digits into bytes; false if they aren't pairs of hex digits.
   static bool parseHex(const std::string &hex, std::vector<uint8_t> &out);

private:
   SynthLink() = default;
   void acquire();
   void release();
   void run();

   mutable std::mutex mutex_;
   Status status_;
   Sound sound_;
   int lastTicket_ = 0;
   int pendingTicket_ = 0; // a sound request for the connection thread to send, and its part
   char pendingPart_ = 'A';
   std::vector<HardwarePatch> bank_;
   std::vector<std::vector<uint8_t>> incoming_;
   std::atomic<uint32_t> bankVersion_{0};

   std::mutex useMutex_;
   int users_ = 0;
   std::atomic<bool> running_{false};
   std::thread thread_;
};

} // namespace mm2

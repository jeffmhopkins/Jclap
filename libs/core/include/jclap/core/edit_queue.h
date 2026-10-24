#pragma once

// Carries GUI edits (main thread) to the audio thread, which turns them into CLAP gesture and value
// events so the host records them:
//
//    main thread:   edits.begin(id); store the value; edits.value(id, v); ...; edits.end(id);
//    process():     edits.drain(process->out_events);
//    params flush:  edits.drain(out);
//
// Every gesture that begins also ends, even when nothing drains for a while: each open gesture
// keeps room for its End and for one last value, and when the queue is full the newest value of an
// open gesture waits there instead of being dropped. A Begin that doesn't fit is dropped along with
// its End. An event the host refuses stays queued for the next drain.

#include <clap/clap.h>

#include <array>
#include <atomic>
#include <cstdint>

namespace jclap::core {

// Single-producer (main thread) single-consumer (audio thread) queue.
class EditQueue {
public:
   // Finds the host's params extension, so each edit can ask for a flush when we aren't processing.
   void init(const clap_host_t *host);

   void begin(clap_id id);
   void value(clap_id id, double value);
   void end(clap_id id);
   // A whole gesture for a single change, e.g. from a button.
   void set(clap_id id, double value);

   // Sends the queued edits as CLAP events.
   void drain(const clap_output_events_t *out);

private:
   enum class Kind : uint8_t { Begin, Value, End };
   struct Edit {
      Kind kind;
      clap_id id;
      double value;
   };

   // A gesture whose Begin is queued: its End is owed. `value` is its newest value while the queue
   // had no room for it.
   struct Gesture {
      clap_id id;
      bool pending;
      double value;
   };

   // The caller has checked there is room.
   void push(const Edit &e);
   uint32_t freeSlots() const;
   // Kept free for the open gestures' Ends and last values.
   uint32_t reserved() const { return 2 * openGestures_; }
   Gesture *findGesture(clap_id id);

   static constexpr uint32_t kSize = 1024;
   static constexpr uint32_t kMaxGestures = 32;
   std::array<Edit, kSize> edits_;
   std::atomic<uint32_t> write_{0}, read_{0};
   // Set when a flush has been asked for and no drain has started since.
   std::atomic<bool> flushRequested_{false};
   // Main thread only.
   std::array<Gesture, kMaxGestures> gestures_;
   uint32_t openGestures_ = 0;
   const clap_host_t *host_ = nullptr;
   const clap_host_params_t *hostParams_ = nullptr;
};

} // namespace jclap::core

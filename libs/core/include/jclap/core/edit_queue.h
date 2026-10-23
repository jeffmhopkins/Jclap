#pragma once

// Carries GUI edits (main thread) to the audio thread, which turns them into CLAP gesture and value
// events so the host records them:
//
//    main thread:   edits.begin(id); store the value; edits.value(id, v); ...; edits.end(id);
//    process():     edits.drain(process->out_events);
//    params flush:  edits.drain(out);

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

   void begin(clap_id id) { push({Kind::Begin, id, 0.0}); }
   void value(clap_id id, double value) { push({Kind::Value, id, value}); }
   void end(clap_id id) { push({Kind::End, id, 0.0}); }
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

   void push(const Edit &e);

   static constexpr uint32_t kSize = 1024;
   std::array<Edit, kSize> edits_;
   std::atomic<uint32_t> write_{0}, read_{0};
   const clap_host_t *host_ = nullptr;
   const clap_host_params_t *hostParams_ = nullptr;
};

} // namespace jclap::core

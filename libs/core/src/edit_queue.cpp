#include "jclap/core/edit_queue.h"

#include "jclap/core/helpers.h"

namespace jclap::core {

void EditQueue::init(const clap_host_t *host) {
   host_ = host;
   hostParams_ = static_cast<const clap_host_params_t *>(host->get_extension(host, CLAP_EXT_PARAMS));
}

void EditQueue::set(clap_id id, double value) {
   begin(id);
   this->value(id, value);
   end(id);
}

void EditQueue::push(const Edit &e) {
   const uint32_t w = write_.load(std::memory_order_relaxed);
   const uint32_t next = (w + 1) % kSize;
   if (next != read_.load(std::memory_order_acquire)) {
      edits_[w] = e;
      write_.store(next, std::memory_order_release);
   }
   if (hostParams_)
      hostParams_->request_flush(host_);
}

void EditQueue::drain(const clap_output_events_t *out) {
   for (;;) {
      const uint32_t r = read_.load(std::memory_order_relaxed);
      if (r == write_.load(std::memory_order_acquire))
         return;
      const Edit e = edits_[r];
      read_.store((r + 1) % kSize, std::memory_order_release);
      if (e.kind == Kind::Value)
         pushParamValue(out, e.id, e.value);
      else
         pushParamGesture(out, e.id, e.kind == Kind::Begin);
   }
}

} // namespace jclap::core

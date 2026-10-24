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

EditQueue::Gesture *EditQueue::findGesture(clap_id id) {
   for (uint32_t i = 0; i < openGestures_; ++i)
      if (gestures_[i].id == id)
         return &gestures_[i];
   return nullptr;
}

uint32_t EditQueue::freeSlots() const {
   const uint32_t w = write_.load(std::memory_order_relaxed), r = read_.load(std::memory_order_acquire);
   return kSize - 1 - (w + kSize - r) % kSize;
}

void EditQueue::begin(clap_id id) {
   // CLAP gestures don't nest, so a second Begin for an open one is ignored. One that doesn't fit,
   // with room for its End and a last value, is dropped, and end() then drops its End too.
   if (findGesture(id) || openGestures_ == kMaxGestures || freeSlots() < reserved() + 3)
      return;
   gestures_[openGestures_++] = {id, false, 0.0};
   push({Kind::Begin, id, 0.0});
}

void EditQueue::value(clap_id id, double value) {
   Gesture *g = findGesture(id);
   if (freeSlots() > reserved()) {
      if (g)
         g->pending = false;
      push({Kind::Value, id, value});
   } else if (g) {
      // Full: keep the newest value, and end() queues it ahead of the End.
      g->pending = true;
      g->value = value;
   }
}

void EditQueue::end(clap_id id) {
   Gesture *g = findGesture(id);
   if (!g)
      return;
   // These use the two slots the gesture kept free.
   if (g->pending)
      push({Kind::Value, id, g->value});
   push({Kind::End, id, 0.0});
   *g = gestures_[--openGestures_];
}

void EditQueue::push(const Edit &e) {
   const uint32_t w = write_.load(std::memory_order_relaxed);
   edits_[w] = e;
   // seq_cst here and in drain(): either drain() sees this edit, or this sees the flag it cleared.
   write_.store((w + 1) % kSize, std::memory_order_seq_cst);
   // One request until the next drain, which sends everything queued by then.
   if (hostParams_ && !flushRequested_.exchange(true, std::memory_order_seq_cst))
      hostParams_->request_flush(host_);
}

void EditQueue::drain(const clap_output_events_t *out) {
   // Cleared first, so an edit queued after this point asks for a flush of its own.
   flushRequested_.store(false, std::memory_order_seq_cst);
   for (;;) {
      const uint32_t r = read_.load(std::memory_order_relaxed);
      if (r == write_.load(std::memory_order_seq_cst))
         return;
      const Edit e = edits_[r];
      const bool sent = e.kind == Kind::Value ? pushParamValue(out, e.id, e.value)
                                              : pushParamGesture(out, e.id, e.kind == Kind::Begin);
      // Refused: leave it, and what follows, for the next drain, so no End is lost.
      if (!sent)
         return;
      read_.store((r + 1) % kSize, std::memory_order_release);
   }
}

} // namespace jclap::core

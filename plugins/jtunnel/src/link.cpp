#include "link.h"

#include <algorithm>
#include <cctype>
#include <deque>

#include "discovery.h"

namespace jtunnel {
namespace {

constexpr auto kHousekeepingEvery = std::chrono::milliseconds(100);
constexpr auto kSubscribeEvery = std::chrono::milliseconds(500);
// Names are looked up again this often, so a sender that moves (a new port after a restart) is
// followed.
constexpr auto kResolveEvery = std::chrono::seconds(2);
// A listener that stops asking is dropped after this.
constexpr auto kSubscriberTimeout = std::chrono::seconds(3);
constexpr auto kReceivingTimeout = std::chrono::seconds(1);

bool sameName(const std::string &a, const char *b) {
   size_t i = 0;
   for (; i < a.size() && b[i]; ++i)
      if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i])))
         return false;
   return i == a.size() && !b[i];
}

} // namespace

bool Link::start(LinkState &state, uint64_t owner, const ImpairSettings &impair) {
   stop();
   state_ = &state;
   owner_ = owner;
   impair_ = impair;
   net::startup();
   bool opened = false;
   for (uint16_t port = kDefaultPort; port < kDefaultPort + 16 && !opened; ++port)
      opened = socket_.open(port);
   if (!opened)
      opened = socket_.open(0);
   if (!opened) {
      net::cleanup();
      return false;
   }
   state.port = socket_.localPort();
   Discovery::acquire();
   discoveryHeld_ = true;
   receiving_ = false;
   sendName_.clear();
   listenTarget_.clear();
   resolvedFor_.clear();
   target_ = {};
   subscribers_.clear();
   running_ = true;
   receiveThread_ = std::thread([this] { receiveLoop(); });
   sendThread_ = std::thread([this] { sendLoop(); });
   return true;
}

void Link::stop() {
   if (!running_ && !discoveryHeld_)
      return;
   running_ = false;
   if (state_)
      state_->outgoingReady.post();
   if (receiveThread_.joinable())
      receiveThread_.join();
   if (sendThread_.joinable())
      sendThread_.join();
   dropTarget();
   if (discoveryHeld_) {
      Discovery::instance().unpublish(owner_);
      Discovery::release();
      discoveryHeld_ = false;
   }
   socket_.close();
   net::cleanup();
   if (state_) {
      state_->listeners = 0;
      state_->connection = static_cast<int>(Connection::None);
      state_->port = 0;
   }
}

void Link::sendLoop() {
   std::vector<net::Address> to;
   OutPacket packet;
   // With an impairment, packets wait here until their release time.
   Impairment impairment(impair_);
   struct Held {
      Clock::time_point release;
      OutPacket packet;
   };
   std::deque<Held> held;
   constexpr size_t kMaxHeld = 2048;
   while (running_) {
      int waitMs = 100;
      if (!held.empty()) {
         const auto until = std::chrono::ceil<std::chrono::milliseconds>(held.front().release - Clock::now());
         waitMs = static_cast<int>(std::clamp<std::chrono::milliseconds::rep>(until.count(), 0, 100));
      }
      if (waitMs > 0)
         state_->outgoingReady.wait(waitMs);
      to.clear();
      {
         std::lock_guard<std::mutex> lock(subscribersMutex_);
         for (const Subscriber &s : subscribers_)
            to.push_back(s.address);
      }
      if (!impair_.enabled()) {
         while (state_->outgoing.pop(packet))
            for (const net::Address &address : to)
               socket_.sendTo(address, packet.data, packet.size);
         continue;
      }
      const Clock::time_point now = Clock::now();
      Clock::time_point release;
      while (state_->outgoing.pop(packet))
         if (impairment.schedule(now, release) && held.size() < kMaxHeld)
            held.push_back({release, packet});
      while (!held.empty() && held.front().release <= Clock::now()) {
         for (const net::Address &address : to)
            socket_.sendTo(address, held.front().packet.data, held.front().packet.size);
         held.pop_front();
      }
   }
}

void Link::receiveLoop() {
   alignas(8) uint8_t buffer[2048];
   auto lastHousekeeping = Clock::now() - kHousekeepingEvery;
   while (running_) {
      net::Address from;
      const int n = socket_.receive(buffer, sizeof(buffer), from, 50);
      const auto now = Clock::now();
      if (n > 0)
         handle(buffer, n, from, now);
      if (now - lastHousekeeping >= kHousekeepingEvery) {
         lastHousekeeping = now;
         housekeeping(now);
      }
   }
}

void Link::handle(const uint8_t *data, int size, const net::Address &from, Clock::time_point now) {
   switch (static_cast<PacketType>(packetType(data, static_cast<size_t>(size)))) {
   case PacketType::Audio: {
      if (!receiving_ || from != target_)
         return;
      AudioHeader header;
      static thread_local Frame frames[kMaxFramesPerPacket];
      int count = 0;
      if (!parseAudio(data, static_cast<size_t>(size), header, frames, count))
         return;
      const int64_t arrivalUs =
         std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count();
      for (int i = 0; i < count; ++i)
         state_->ring.store(header, frames[i], arrivalUs);
      lastAudio_ = now;
      return;
   }
   case PacketType::Subscribe: {
      char name[kMaxTunnelName + 1];
      if (receiving_ || sendName_.empty() || !parseSubscribe(data, static_cast<size_t>(size), name, sizeof(name)))
         return;
      // An empty name is a typed address: whatever tunnel is here.
      if (name[0] && !sameName(sendName_, name))
         return;
      std::lock_guard<std::mutex> lock(subscribersMutex_);
      auto it = std::find_if(subscribers_.begin(), subscribers_.end(),
                             [&](const Subscriber &s) { return s.address == from; });
      if (it != subscribers_.end())
         it->lastSeen = now;
      else
         subscribers_.push_back({from, now});
      state_->listeners = static_cast<int>(subscribers_.size());
      return;
   }
   case PacketType::Bye: {
      std::lock_guard<std::mutex> lock(subscribersMutex_);
      subscribers_.erase(std::remove_if(subscribers_.begin(), subscribers_.end(),
                                        [&](const Subscriber &s) { return s.address == from; }),
                         subscribers_.end());
      state_->listeners = static_cast<int>(subscribers_.size());
      return;
   }
   }
}

void Link::dropTarget() {
   if (target_.valid()) {
      uint8_t bye[16];
      if (const size_t n = writeBye(bye, sizeof(bye)))
         socket_.sendTo(target_, bye, n);
   }
   target_ = {};
   resolvedFor_.clear();
   if (state_) {
      state_->ring.clear();
      state_->source = 0;
      state_->sourcePort = 0;
   }
}

void Link::housekeeping(Clock::time_point now) {
   const bool receiving = state_->receiving.load();
   {
      std::lock_guard<std::mutex> lock(state_->mutex);
      sendName_ = state_->sendName;
      listenTarget_ = state_->listenTarget;
   }
   Discovery &discovery = Discovery::instance();

   if (receiving != receiving_) {
      receiving_ = receiving;
      std::lock_guard<std::mutex> lock(subscribersMutex_);
      subscribers_.clear();
      state_->listeners = 0;
   }

   if (!receiving_) {
      if (target_.valid())
         dropTarget();
      state_->connection = static_cast<int>(Connection::None);
      if (!sendName_.empty())
         discovery.publish(owner_, sendName_, socket_.localPort());
      else
         discovery.unpublish(owner_);
      std::lock_guard<std::mutex> lock(subscribersMutex_);
      subscribers_.erase(std::remove_if(subscribers_.begin(), subscribers_.end(),
                                        [&](const Subscriber &s) { return now - s.lastSeen > kSubscriberTimeout; }),
                         subscribers_.end());
      state_->listeners = static_cast<int>(subscribers_.size());
      return;
   }

   discovery.unpublish(owner_);
   if (listenTarget_.empty()) {
      if (target_.valid())
         dropTarget();
      state_->connection = static_cast<int>(Connection::None);
      return;
   }

   // Find (or follow) the sender: a typed address, or a tunnel name through discovery.
   if (listenTarget_ != resolvedFor_ || now - lastResolve_ >= kResolveEvery) {
      lastResolve_ = now;
      net::Address found;
      const bool ok = net::looksLikeAddress(listenTarget_) ? net::Address::parse(listenTarget_, found, kDefaultPort)
                                                           : discovery.resolve(listenTarget_, found);
      if (!ok || found != target_ || listenTarget_ != resolvedFor_) {
         if (target_.valid() && (!ok || found != target_))
            dropTarget();
         if (ok) {
            target_ = found;
            lastSubscribe_ = {};
            lastAudio_ = {};
            state_->source = target_.ip;
            state_->sourcePort = target_.port;
         }
      }
      resolvedFor_ = listenTarget_;
   }

   if (!target_.valid()) {
      state_->connection = static_cast<int>(Connection::Looking);
      return;
   }
   if (now - lastSubscribe_ >= kSubscribeEvery) {
      lastSubscribe_ = now;
      uint8_t packet[64];
      const std::string name = net::looksLikeAddress(listenTarget_) ? std::string() : listenTarget_;
      if (const size_t n = writeSubscribe(packet, sizeof(packet), name.c_str()))
         socket_.sendTo(target_, packet, n);
   }
   const bool flowing = lastAudio_ != Clock::time_point{} && now - lastAudio_ < kReceivingTimeout;
   state_->connection = static_cast<int>(flowing ? Connection::Receiving : Connection::Waiting);
}

} // namespace jtunnel

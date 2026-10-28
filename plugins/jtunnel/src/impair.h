// A test switch that makes the network worse on purpose, so a link can be tried against Wi-Fi-like
// trouble on a wired network or a single machine. A sender with JTUNNEL_IMPAIR set in its host's
// environment, such as
//
//    JTUNNEL_IMPAIR="jitter=6,stall=80,every=10,loss=1"
//
// holds each packet back a random time (exponential, `jitter` ms on average, delivered in order as
// Wi-Fi does), now and then stops everything for `stall` ms (on average every `every` seconds) and
// lets it out in a burst, and drops `loss` percent at random. Without it nothing changes.
#pragma once

#include <chrono>
#include <cstdint>
#include <random>
#include <string>

namespace jtunnel {

struct ImpairSettings {
   double jitterMs = 0, stallMs = 0, stallEverySeconds = 10, lossPercent = 0;
   bool enabled() const { return jitterMs > 0 || stallMs > 0 || lossPercent > 0; }
   // "jitter=6,stall=80,every=10,loss=1"; unknown keys and bad numbers are ignored.
   static ImpairSettings parse(const std::string &text);
   static ImpairSettings fromEnvironment();
   // "jitter 6 ms, 80 ms stalls every 10 s, 1% loss", for the panel.
   std::string describe() const;
};

class Impairment {
public:
   using Clock = std::chrono::steady_clock;

   explicit Impairment(const ImpairSettings &settings, uint32_t seed = std::random_device{}());

   // When a packet handed over at `now` should go out; false if it is lost.
   bool schedule(Clock::time_point now, Clock::time_point &release);

private:
   ImpairSettings settings_;
   std::mt19937 rng_;
   Clock::time_point lastRelease_{}, stallStart_{}, stallEnd_{};
   bool started_ = false;
};

} // namespace jtunnel

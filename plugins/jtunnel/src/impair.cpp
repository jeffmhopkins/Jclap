#include "impair.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace jtunnel {

ImpairSettings ImpairSettings::parse(const std::string &text) {
   ImpairSettings s;
   size_t pos = 0;
   while (pos < text.size()) {
      size_t end = text.find(',', pos);
      if (end == std::string::npos)
         end = text.size();
      const std::string item = text.substr(pos, end - pos);
      pos = end + 1;
      const size_t eq = item.find('=');
      if (eq == std::string::npos)
         continue;
      const std::string key = item.substr(0, eq);
      const std::string valueText = item.substr(eq + 1);
      char *stop = nullptr;
      const double value = std::strtod(valueText.c_str(), &stop);
      if (valueText.empty() || *stop || !(value >= 0))
         continue;
      if (key == "jitter")
         s.jitterMs = std::min(value, 1000.0);
      else if (key == "stall")
         s.stallMs = std::min(value, 5000.0);
      else if (key == "every" && value > 0)
         s.stallEverySeconds = value;
      else if (key == "loss")
         s.lossPercent = std::min(value, 100.0);
   }
   return s;
}

ImpairSettings ImpairSettings::fromEnvironment() {
   const char *text = std::getenv("JTUNNEL_IMPAIR");
   return text ? parse(text) : ImpairSettings{};
}

std::string ImpairSettings::describe() const {
   std::string out;
   char part[64];
   if (jitterMs > 0) {
      std::snprintf(part, sizeof(part), "jitter %g ms", jitterMs);
      out += part;
   }
   if (stallMs > 0) {
      std::snprintf(part, sizeof(part), "%s%g ms stalls every %g s", out.empty() ? "" : ", ", stallMs,
                    stallEverySeconds);
      out += part;
   }
   if (lossPercent > 0) {
      std::snprintf(part, sizeof(part), "%s%g%% loss", out.empty() ? "" : ", ", lossPercent);
      out += part;
   }
   return out;
}

Impairment::Impairment(const ImpairSettings &settings, uint32_t seed) : settings_(settings), rng_(seed) {}

bool Impairment::schedule(Clock::time_point now, Clock::time_point &release) {
   using Ms = std::chrono::duration<double, std::milli>;
   std::uniform_real_distribution<double> uniform(0.0, 1.0);
   if (!started_) {
      started_ = true;
      stallStart_ = stallEnd_ = now;
   }
   // The next stall starts a random (exponential) time after the last one ended.
   if (settings_.stallMs > 0 && now >= stallEnd_) {
      std::exponential_distribution<double> gap(1.0 / settings_.stallEverySeconds);
      stallStart_ = now + std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(gap(rng_)));
      stallEnd_ = stallStart_ + std::chrono::duration_cast<Clock::duration>(Ms(settings_.stallMs));
   }
   if (settings_.lossPercent > 0 && uniform(rng_) * 100.0 < settings_.lossPercent)
      return false;
   Clock::time_point at = now;
   if (settings_.jitterMs > 0) {
      std::exponential_distribution<double> delay(1.0 / settings_.jitterMs);
      at += std::chrono::duration_cast<Clock::duration>(Ms(delay(rng_)));
   }
   // Held during a stall, then all let out together when it ends.
   if (settings_.stallMs > 0 && at >= stallStart_ && at < stallEnd_)
      at = stallEnd_;
   // In order: a packet never overtakes the one before it.
   release = std::max(at, lastRelease_);
   lastRelease_ = release;
   return true;
}

} // namespace jtunnel

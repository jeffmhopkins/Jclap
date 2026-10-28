#include "receiver.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace jtunnel {
namespace {

// The controller acts every quarter second on the lead of the last second: the most audio that was
// queued in each window, which is what the least-delayed packets leave. That stays steady through
// jitter and stalls (they only dip the queue), so the clock can be followed without reacting to
// them.
constexpr double kWindowSeconds = 0.25;
// Rate correction = kP * error + integral, error in seconds of lead. kP alone settles a step in
// about 5 s and leaves only error = drift / kP behind (under a millisecond for 150 ppm); the
// integral takes that last bit out, and only learns while the error is small, so a change of
// target doesn't wind it up. The pitch change (at most 0.5%, usually a few hundred ppm) can't be
// heard.
constexpr double kP = 0.2;
constexpr double kI = 0.01;
constexpr double kIntegrateWithin = 0.002;
constexpr double kMaxIntegral = 0.002;
constexpr double kMaxCorrection = 0.005;
// The clock offset is measured over about this long, so it can follow a clock that wanders as
// the machine warms up.
constexpr double kDriftMemorySeconds = 600.0;
// Before this much measuring the figure is mostly start-up noise (tens of ppm on a busy network).
constexpr double kDriftShowSeconds = 15.0;
// Late frames concealed in a row before the stream counts as stopped.
constexpr double kStallSeconds = 0.3;
constexpr double kAutoStepMs = 5.0;
constexpr double kAutoMaxMs = 200.0;
// Quiet time before Auto Buffer starts giving back margin, then 1 ms every 5 s.
constexpr double kAutoQuietSeconds = 20.0;

} // namespace

bool Receiver::setup(double hostRate) {
   hostRate_ = hostRate;
   state_ = ReceiveState::Idle;
   cutoffRatio_ = 0.0;
   return decoder_.setup();
}

double Receiver::targetSamples() const {
   return (bufferMs_ + autoExtraMs_) * 0.001 * streamRate_;
}

void Receiver::begin(const StreamInfo &info) {
   releaseNotes(0);
   stream_ = info.streamId;
   codec_ = info.codec;
   streamRate_ = info.sampleRate;
   frameSamples_ = info.frameSamples;
   nominal_ = streamRate_ / hostRate_;
   if (nominal_ != cutoffRatio_) {
      resampler_.configureFor(nominal_);
      cutoffRatio_ = nominal_;
   }
   resampler_.reset();
   decoder_.reset();
   correction_ = integral_ = drift_ = 0.0;
   windowMax_ = windowMaxArrival_ = -1e18;
   segmentOpen_ = false;
   driftTT_ = driftTX_ = driftTime_ = measuredSeconds_ = 0.0;
   historyCount_ = historyNext_ = 0;
   windowElapsed_ = 0;
   dropoutInWindow_ = false;
   lateRun_ = 0;
   pendingCount_ = 0;
   startSeq_ = info.newest;
   state_ = ReceiveState::Buffering;
}

int Receiver::process(ReceiveRing &ring, float *left, float *right, uint32_t frames, OutMidi *midi, int capacity,
                      int64_t nowUs) {
   out_ = midi;
   outCount_ = 0;
   outCapacity_ = capacity;
   if (releasePending_)
      releaseNotes(0);

   const StreamInfo info = ring.info();
   if (restartRequested_) {
      restartRequested_ = false;
      releaseNotes(0);
      state_ = ReceiveState::Idle;
   }
   if (!info.active || info.frameSamples == 0) {
      if (state_ != ReceiveState::Idle)
         releaseNotes(0);
      state_ = ReceiveState::Idle;
      std::fill(left, left + frames, 0.0f);
      std::fill(right, right + frames, 0.0f);
      publish(0.0);
      return outCount_;
   }
   if (state_ == ReceiveState::Idle || info.streamId != stream_)
      begin(info);

   if (state_ == ReceiveState::Buffering) {
      const double have = (seqDiff(info.newest, startSeq_) + 1.0) * frameSamples_;
      if (have >= targetSamples()) {
         state_ = ReceiveState::Playing;
         nextSeq_ = startSeq_;
         ring.setReadPosition(stream_, nextSeq_);
      }
      else {
         std::fill(left, left + frames, 0.0f);
         std::fill(right, right + frames, 0.0f);
         publish(std::max(0.0, have));
         return outCount_;
      }
   }

   const double ratio = nominal_ * (1.0 + correction_);
   for (int guard = 0; guard < 256 && resampler_.needed(static_cast<int>(frames), ratio) > 0; ++guard)
      pullFrame(ring, info.newest);
   if (resampler_.needed(static_cast<int>(frames), ratio) > 0) {
      // Only if a host block were longer than the resampler can hold.
      std::fill(left, left + frames, 0.0f);
      std::fill(right, right + frames, 0.0f);
      return outCount_;
   }
   const double start = resampler_.position();
   resampler_.produce(left, right, static_cast<int>(frames), ratio);

   // MIDI that falls in this block, at the output sample its input position maps to.
   const double end = start + frames * ratio;
   int kept = 0;
   for (int i = 0; i < pendingCount_; ++i) {
      const PendingMidi &p = pending_[i];
      if (p.pos < end) {
         const double at = std::floor((p.pos - start) / ratio);
         emit(static_cast<uint32_t>(std::clamp(at, 0.0, frames - 1.0)), p.data);
      }
      else
         pending_[kept++] = p;
   }
   pendingCount_ = kept;

   // The margin: everything received but not yet played.
   const double fill = (seqDiff(info.newest, nextSeq_) + 1.0) * frameSamples_ + resampler_.buffered();
   windowMax_ = std::max(windowMax_, fill);
   // What has been played since the newest frame came in, added back.
   double sinceArrival = -1.0;
   if (nowUs >= 0) {
      const double now = nowUs * 1e-6;
      if (clockLocked_) {
         const double predicted = blockTime_ + lastFrames_ * samplePeriod_;
         const double error = now - predicted;
         if (std::abs(error) > 0.05) {
            clockLocked_ = false; // the host stopped for a while: start again
         }
         else {
            const double omega = 2.0 * 3.14159265358979 * 0.5 * lastFrames_ / hostRate_;
            blockTime_ = predicted + std::sqrt(2.0) * omega * error;
            samplePeriod_ += omega * omega * error / lastFrames_;
         }
      }
      if (!clockLocked_) {
         blockTime_ = now;
         samplePeriod_ = 1.0 / hostRate_;
         clockLocked_ = true;
      }
      lastFrames_ = frames;
      const double arrival = info.newestArrivalUs * 1e-6;
      if (info.newestArrivalUs >= 0 && blockTime_ >= arrival)
         sinceArrival = (blockTime_ - arrival) * streamRate_;
   }
   windowMaxArrival_ = std::max(windowMaxArrival_, sinceArrival >= 0.0 ? fill + sinceArrival : fill);
   windowElapsed_ += frames;
   if (windowElapsed_ >= kWindowSeconds * hostRate_)
      endWindow(info.newest);

   if (lateRun_ * frameSamples_ > kStallSeconds * streamRate_) {
      // The sender stopped (or the network did): let go of its notes and wait for it to come back,
      // then buffer up again from there.
      releaseNotes(frames - 1);
      dropoutInWindow_ = false;
      longestStall_ = 0;
      state_ = ReceiveState::Buffering;
      startSeq_ = info.newest + 1;
      lateRun_ = 0;
      resampler_.reset();
      decoder_.reset();
      pendingCount_ = 0;
   }
   publish(fill);
   return outCount_;
}

void Receiver::pullFrame(ReceiveRing &ring, uint32_t newest) {
   Frame frame;
   if (ring.take(stream_, nextSeq_, frame)) {
      decoder_.decode(codec_, frame.audio, frame.audioBytes, decoded_, frameSamples_);
      const double base = static_cast<double>(resampler_.pushed());
      for (int i = 0; i < frame.midiCount && pendingCount_ < kMaxPending; ++i) {
         PendingMidi &p = pending_[pendingCount_++];
         p.pos = base + frame.midi[i].offset;
         std::memcpy(p.data, frame.midi[i].data, 3);
      }
      ++nextSeq_;
      lateRun_ = 0;
   }
   else if (seqDiff(newest, nextSeq_) > 0) {
      // Later frames are here, so this one is lost (both copies, with redundancy on).
      decoder_.conceal(codec_, decoded_, frameSamples_);
      ++nextSeq_;
      ++lost_;
   }
   else {
      // Nothing newer yet: a stall. Fill in, and keep waiting for this frame.
      decoder_.conceal(codec_, decoded_, frameSamples_);
      ++lateRun_;
      ++concealed_;
      dropoutInWindow_ = true;
      longestStall_ = std::max(longestStall_, lateRun_);
   }
   ring.setReadPosition(stream_, nextSeq_);
   resampler_.pushInterleaved(decoded_, frameSamples_);
}

void Receiver::endWindow(uint32_t newest) {
   const double window = windowElapsed_ / hostRate_;
   windowElapsed_ = 0;
   const double windowMax = windowMax_;
   const double windowMaxArrival = windowMaxArrival_;
   windowMax_ = windowMaxArrival_ = -1e18;

   // A stall still going on is judged when it ends: if it turns out to be the sender stopping, it
   // says nothing about the network.
   if (dropoutInWindow_ && lateRun_ > 0)
      return;
   if (dropoutInWindow_) {
      // A dropout says the margin is too small, not that the clock moved: grow the margin, and
      // start the history again so the dropout's empty buffer doesn't also slow playback.
      // Grow by at least the stall just seen, so one step covers it next time.
      dropoutInWindow_ = false;
      const double stallMs = longestStall_ * frameSamples_ * 1000.0 / streamRate_;
      longestStall_ = 0;
      if (autoBuffer_)
         autoExtraMs_ = std::min(autoExtraMs_ + std::max(kAutoStepMs, stallMs + 2.0), kAutoMaxMs);
      quietSeconds_ = 0.0;
      historyCount_ = historyNext_ = 0;
      closeSegment();
      return;
   }
   quietSeconds_ += window;
   if (autoBuffer_ && autoExtraMs_ > 0.0 && quietSeconds_ >= kAutoQuietSeconds) {
      autoExtraMs_ = std::max(0.0, autoExtraMs_ - 1.0);
      quietSeconds_ -= 5.0;
   }

   history_[historyNext_] = windowMax;
   historyArrival_[historyNext_] = windowMaxArrival;
   historyNext_ = (historyNext_ + 1) % kHistory;
   historyCount_ = std::min(historyCount_ + 1, kHistory);
   if (historyCount_ < 2)
      return;
   double lead = 0.0;
   for (int i = 0; i < historyCount_; ++i)
      lead += history_[i];
   lead /= historyCount_;
   double arrivalLead = 0.0;
   for (int i = 0; i < historyCount_; ++i)
      arrivalLead += historyArrival_[i];
   arrivalLead /= historyCount_;

   const double target = targetSamples();
   const double error = (lead - target) / streamRate_;
   // Far too much queued (after a long stall, say): skip ahead rather than take minutes to drain.
   const double tooMuch = std::max(0.1, 2.0 * target / streamRate_);
   if (error > tooMuch) {
      const int32_t skip = static_cast<int32_t>((lead - target) / frameSamples_);
      const int32_t ahead = seqDiff(newest, nextSeq_);
      nextSeq_ += static_cast<uint32_t>(std::clamp(skip, 0, std::max(0, ahead)));
      historyCount_ = historyNext_ = 0;
      closeSegment();
      return;
   }
   // The lead changes by (drift - correction) per second, so over any stretch without a dropout
   // the sum of correction x time plus the lead change, x(t), grows at the drift. Its slope by
   // least squares, pooled over the stretches of the last minutes, sees through jitter, Auto Buffer
   // moving the target, and the lead finding its level at the start of a stretch (which taking
   // just the two ends would carry for minutes).
   if (!segmentOpen_) {
      segmentOpen_ = true;
      segmentLead_ = arrivalLead;
      segmentCorrection_ = segmentTime_ = 0.0;
      segN_ = segT_ = segX_ = segTT_ = segTX_ = 0.0;
   }
   else {
      segmentCorrection_ += correction_ * window;
      segmentTime_ += window;
   }
   const double x = segmentCorrection_ + (arrivalLead - segmentLead_) / streamRate_;
   segN_ += 1.0;
   segT_ += segmentTime_;
   segX_ += x;
   segTT_ += segmentTime_ * segmentTime_;
   segTX_ += segmentTime_ * x;
   const double forget = 1.0 - window / kDriftMemorySeconds;
   driftTT_ *= forget;
   driftTX_ *= forget;
   driftTime_ *= forget;
   const double tt = driftTT_ + (segTT_ - segT_ * segT_ / segN_);
   const double tx = driftTX_ + (segTX_ - segT_ * segX_ / segN_);
   measuredSeconds_ = driftTime_ + segmentTime_;
   if (measuredSeconds_ > 5.0 && tt > 0.0)
      drift_ = tx / tt;
   if (std::abs(error) < kIntegrateWithin)
      integral_ = std::clamp(integral_ + kI * error * window, -kMaxIntegral, kMaxIntegral);
   correction_ = std::clamp(kP * error + integral_, -kMaxCorrection, kMaxCorrection);
}

void Receiver::closeSegment() {
   // Its centred sums: what it says about the slope, weighted by how much it can say.
   if (segmentOpen_ && segN_ > 1.0) {
      driftTT_ += segTT_ - segT_ * segT_ / segN_;
      driftTX_ += segTX_ - segT_ * segX_ / segN_;
      driftTime_ += segmentTime_;
   }
   segmentOpen_ = false;
}

void Receiver::emit(uint32_t time, const uint8_t *data) {
   const uint8_t kind = data[0] & 0xF0;
   const int channel = data[0] & 0x0F;
   const int key = data[1] & 0x7F;
   if (kind == 0x90 && data[2] > 0)
      held_[channel][key] = true;
   else if (kind == 0x80 || kind == 0x90)
      held_[channel][key] = false;
   if (outCount_ < outCapacity_) {
      OutMidi &m = out_[outCount_++];
      m.time = time;
      std::memcpy(m.data, data, 3);
   }
}

void Receiver::releaseNotes(uint32_t time) {
   releasePending_ = false;
   for (int channel = 0; channel < 16; ++channel)
      for (int key = 0; key < 128; ++key) {
         if (!held_[channel][key])
            continue;
         if (outCount_ >= outCapacity_) {
            // The rest go out at the start of the next block.
            releasePending_ = true;
            return;
         }
         const uint8_t off[3] = {static_cast<uint8_t>(0x80 | channel), static_cast<uint8_t>(key), 0};
         emit(time, off);
      }
}

void Receiver::publish(double fill) {
   fillSmoothed_ += (fill - fillSmoothed_) * 0.05;
   const double rate = streamRate_ ? streamRate_ : 48000.0;
   stats_.state.store(static_cast<int>(state_), std::memory_order_relaxed);
   stats_.fillMs.store(static_cast<float>(fillSmoothed_ * 1000.0 / rate), std::memory_order_relaxed);
   stats_.targetMs.store(static_cast<float>(bufferMs_ + autoExtraMs_), std::memory_order_relaxed);
   // Shown once it has had long enough to mean something; NaN until then.
   stats_.driftPpm.store(measuredSeconds_ >= kDriftShowSeconds ? static_cast<float>(drift_ * 1e6)
                                                               : std::numeric_limits<float>::quiet_NaN(),
                         std::memory_order_relaxed);
   stats_.lost.store(lost_, std::memory_order_relaxed);
   stats_.concealed.store(concealed_, std::memory_order_relaxed);
   stats_.sampleRate.store(state_ == ReceiveState::Idle ? 0 : streamRate_, std::memory_order_relaxed);
   stats_.codec.store(static_cast<int>(codec_), std::memory_order_relaxed);
}

} // namespace jtunnel

// Polar XY's Touch Filter: cleans up a resistive XY pad's touches before they move the Orb. No CLAP
// dependencies so it can be unit tested.
//
// A resistive pad reads its position through the finger's contact, so while the finger is pressing
// down or lifting off, the contact is light and the readings are wrong: they skitter off toward an
// edge or the centre for a few milliseconds either side of a good touch. Averaging hides that by
// smearing it into every move. This instead finds the edges of each touch and throws them away:
//
// - X and Y that arrive within kTouchPair of each other are one reading, so a move never goes out as
//   the new X with the old Y.
// - A reading that jumps further than kTouchJump from the last, or lands somewhere new after a pause
//   of kTouchLift, starts a touch. For its first `window`, the press, nothing goes out. At the end of
//   it, the latest reading goes out if the last two agree (within kTouchAgree), and everything after
//   flows on as normal. A touch that never gets two readings (a lone blip as the finger lifts) never
//   goes out at all.
// - Everything is held for `window` before it goes out, so when a touch starts with a jump, the
//   readings still held from just before it (the last touch lifting off) are thrown away.
//
// So the cost is a constant `window` of latency (twice that for the start of a touch), with no
// smearing: whatever goes out goes out exactly, at the pace it came in.
//
// A pad that also sends a touch CC (on while a finger is down) makes the edges exact. Once one has
// arrived (see touch()), readings while lifted are ignored, a touch's press starts when it goes on,
// and everything still held when it goes off is thrown away: the lift-off, however gently it slid. A
// press then needs only one reading. The touch also comes out, as Down before a touch's first
// reading and Up when it lifts, so it can be sent on in step with the motion.
#pragma once

#include <cmath>
#include <cstddef>

namespace jclap {

// X and Y arriving this close together (seconds) are one reading.
constexpr double kTouchPair = 0.003;
// A step longer than this (on the -1..+1 pad, so about 16 CC steps) between readings is a touch
// landing or lifting, not a drag.
constexpr double kTouchJump = 0.25;
// A pause this long (seconds) and the next reading may be a new touch, if it isn't where the last was.
constexpr double kTouchLift = 0.05;
// Two readings this close (on the -1..+1 pad, about 4 CC steps) agree: the finger is down.
constexpr double kTouchAgree = 0.06;

// The windows Touch Filter offers (seconds), Off first.
constexpr double kTouchWindows[] = {0.0, 0.01, 0.02, 0.04};

class TouchFilter {
public:
   // What pop gives: a reading to move to, or the touch going down or up.
   enum class Event { Move, Down, Up };

   // Forgets everything, held readings included.
   void reset() { *this = TouchFilter{}; }

   // A reading (on the -1..+1 pad) arriving at `now` (seconds, always going forward), with the window
   // (seconds) as it is now.
   void push(double x, double y, double now, double window) {
      window_ = window;
      if (open_ && now - open_t_ < kTouchPair) {
         open_x_ = x;
         open_y_ = y;
         return;
      }
      if (open_)
         finalize();
      open_ = true;
      open_x_ = x;
      open_y_ = y;
      open_t_ = now;
   }

   // The touch CC: a finger down or up at `now`. From the first one on, the touch decides the edges.
   void touch(bool down, double now, double window) {
      window_ = window;
      gated_ = true;
      if (down == touched_)
         return;
      touched_ = down;
      open_ = false; // a reading half made across the edge belongs to neither side
      if (down) {
         settling_ = true;
         start_ = now;
         readings_ = 0;
         down_due_ = true;
         return;
      }
      // Everything still held is the lift-off. If that includes the touch's Down, it never went out,
      // so neither does its Up.
      bool down_held = false;
      for (std::size_t i = 0; i < count_; ++i)
         down_held = down_held || queue_[(head_ + i) % kCapacity].event == Event::Down;
      count_ = 0;
      settling_ = false;
      down_due_ = false;
      if (down_out_ && !down_held)
         hold(0.0, 0.0, now, Event::Up);
      down_out_ = false;
   }

   // The next event due to go out by `now`, oldest first. Call until it returns false.
   bool pop(double now, double window, double &x, double &y) {
      Event e;
      return pop(now, window, x, y, e);
   }

   bool pop(double now, double window, double &x, double &y, Event &event) {
      window_ = window;
      if (open_ && now - open_t_ >= kTouchPair)
         finalize();
      if (settling_ && now >= start_ + window_)
         settle();
      if (count_ == 0 || queue_[head_].due > now)
         return false;
      x = queue_[head_].x;
      y = queue_[head_].y;
      event = queue_[head_].event;
      head_ = (head_ + 1) % kCapacity;
      --count_;
      return true;
   }

   // Whether anything is still on its way out, so pop needs calling.
   bool busy() const { return open_ || settling_ || count_ > 0; }

private:
   // Up to this many readings held: a dense controller (1 ms apart) over the longest window.
   static constexpr std::size_t kCapacity = 64;

   struct Reading {
      double x, y, due;
      Event event;
   };

   void finalize() {
      open_ = false;
      const double x = open_x_, y = open_y_, t = open_t_;
      if (gated_ && !touched_)
         return; // no finger down: noise
      if (settling_ && t >= start_ + window_)
         settle();
      const double step = has_last_ ? std::hypot(x - last_x_, y - last_y_) : 0.0;
      const bool onset = !has_last_ || step > kTouchJump || (t - last_t_ >= kTouchLift && step > kTouchAgree);
      if (onset && window_ > 0.0) {
         count_ = 0; // what was still held was the last touch lifting off
         settling_ = true;
         start_ = t;
         readings_ = 0;
      }
      if (settling_) {
         // The press: kept aside, and only the last two count.
         prev_x_ = cand_x_;
         prev_y_ = cand_y_;
         cand_x_ = x;
         cand_y_ = y;
         ++readings_;
      } else {
         hold(x, y, t + window_);
      }
      has_last_ = true;
      last_x_ = x;
      last_y_ = y;
      last_t_ = t;
   }

   // The end of a touch's press: its latest reading goes out now, if the finger has come to rest
   // (with a touch CC, the finger is known to be down, so one reading will do).
   void settle() {
      settling_ = false;
      const bool rested = readings_ >= 2 && std::hypot(cand_x_ - prev_x_, cand_y_ - prev_y_) <= kTouchAgree;
      if (rested || (gated_ && readings_ >= 1))
         hold(cand_x_, cand_y_, start_ + window_);
   }

   void hold(double x, double y, double due, Event event = Event::Move) {
      // A touch's first reading goes out after its Down.
      if (event == Event::Move && down_due_) {
         down_due_ = false;
         down_out_ = true;
         hold(x, y, due, Event::Down);
      }
      if (count_ == kCapacity) { // full (a controller faster than 1 ms): the oldest is dropped
         head_ = (head_ + 1) % kCapacity;
         --count_;
      }
      queue_[(head_ + count_) % kCapacity] = {x, y, due, event};
      ++count_;
   }

   double window_ = 0.0;
   // The reading still taking X and Y.
   bool open_ = false;
   double open_x_ = 0.0, open_y_ = 0.0, open_t_ = 0.0;
   // The last whole reading.
   bool has_last_ = false;
   double last_x_ = 0.0, last_y_ = 0.0, last_t_ = 0.0;
   // A touch's press: when it started, how many readings it has had, and the last two.
   bool settling_ = false;
   double start_ = 0.0;
   int readings_ = 0;
   double cand_x_ = 0.0, cand_y_ = 0.0, prev_x_ = 0.0, prev_y_ = 0.0;
   // The touch CC: whether one has arrived, whether the finger is down, and whether its Down is
   // still to go out (before its first reading) or has gone out (so its Up goes too).
   bool gated_ = false, touched_ = false, down_due_ = false, down_out_ = false;
   // Readings waiting to go out, in order.
   Reading queue_[kCapacity] = {};
   std::size_t head_ = 0, count_ = 0;
};

} // namespace jclap

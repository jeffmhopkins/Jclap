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
// - A reading that jumps further than the jump limit (kTouchJump unless set) from the last, or lands
//   somewhere new after a pause of kTouchLift (without a touch CC), starts a touch. For its first stretch, the press
//   (`window` unless set apart), nothing goes out. At the end of it, the latest reading goes out if
//   the last two agree (within kTouchAgree unless set), and everything after flows on as normal. A
//   touch that never gets two readings (a lone blip as the finger lifts) never goes out at all.
// - Everything is held for `window` before it goes out, so when a touch starts with a jump, the
//   readings still held from just before it (the last touch lifting off) are thrown away.
//
// So the cost is a constant `window` of latency (plus the press for the start of a touch), with no
// smearing: whatever goes out goes out exactly, at the pace it came in.
//
// A pad that also sends a touch CC (on while a finger is down) makes the edges exact. Once one has
// arrived (see touch()), readings while lifted are ignored, a touch's press starts when it goes on,
// and everything still held when it goes off is thrown away: the lift-off, however gently it slid. A
// press it starts then needs only one reading; a jump later in the touch still needs two that agree.
// The touch also comes out, as Down before a touch's first reading and Up when it lifts, so it can be
// sent on in step with the motion.
#pragma once

#include <cmath>
#include <cstddef>

namespace jclap {

// X and Y arriving this close together (seconds) are one reading.
constexpr double kTouchPair = 0.003;
// By default, a step longer than this (on the -1..+1 pad, so about 16 CC steps) between readings is
// a touch landing or lifting, not a drag (see setJump).
constexpr double kTouchJump = 0.25;
// A pause this long (seconds) and the next reading may be a new touch, if it isn't where the last was.
constexpr double kTouchLift = 0.05;
// Two readings this close (on the -1..+1 pad, about 4 CC steps) agree: the finger is down.
constexpr double kTouchAgree = 0.06;

class TouchFilter {
public:
   // What pop gives: a reading to move to, or the touch going down or up.
   enum class Event { Move, Down, Up };

   // Forgets everything, held readings included (but not the tuning below).
   void reset() {
      const double press = press_, jump = jump_, agree = agree_;
      *this = TouchFilter{};
      press_ = press;
      jump_ = jump;
      agree_ = agree;
   }

   // Tuning, all optional. How long a press is held back before its reading goes out (seconds; below
   // 0, the window, as by default).
   void setPress(double press) { press_ = press; }
   // How close (on the -1..+1 pad) two readings must be to agree: kTouchAgree by default.
   void setAgree(double agree) { agree_ = std::fmax(agree, 0.0); }
   // How far one reading may step from the last before it counts as a jump, which only goes out once
   // a second reading agrees: kTouchJump by default. Kept above the agreement.
   void setJump(double jump) { jump_ = jump; }

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
      has_prev_ = false;
      open_ = false; // a reading half made across the edge belongs to neither side
      if (down) {
         settling_ = true;
         start_ = now;
         readings_ = 0;
         down_due_ = true;
         pressed_ = true;
         return;
      }
      // Everything still held is the lift-off. If that includes the touch's Down, it never went out,
      // so neither does its Up.
      bool down_held = false;
      for (std::size_t i = 0; i < count_; ++i)
         down_held = down_held || queue_[(head_ + i) % kCapacity].event == Event::Down;
      count_ = 0;
      settling_ = false;
      pressed_ = false;
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
      if (settling_ && now >= start_ + press())
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
      if (settling_ && t >= start_ + press())
         settle();
      const double step = has_last_ ? std::hypot(x - last_x_, y - last_y_) : 0.0;
      // How far off it is: from the last reading or from where the finger was headed (the last two
      // readings' velocity carried on, at most twice their gap), whichever is nearer. So a fast drag
      // that keeps its course isn't taken for a jump, and nor is a sharp turn back.
      double off = step;
      if (has_prev_ && t - last_t_ < kTouchLift && last_t_ > prev_t_) {
         const double k = std::fmin((t - last_t_) / (last_t_ - prev_t_), 2.0);
         off = std::fmin(step, std::hypot(x - (last_x_ + (last_x_ - prev_rx_) * k), y - (last_y_ + (last_y_ - prev_ry_) * k)));
      }
      // A pause before a new spot only means a new touch without the touch CC: with it, a finger can
      // rest and move on (a pad that only sends changes goes quiet while it rests).
      const bool paused = !gated_ && t - last_t_ >= kTouchLift && step > agree_;
      const bool onset = !has_last_ || off > std::fmax(jump_, agree_) || paused;
      if (onset && window_ > 0.0) {
         // Still the touch CC's press if nothing has arrived in it yet. A jump later in the touch
         // (often the finger starting to lift) needs two readings that agree, as without it.
         pressed_ = pressed_ && settling_ && readings_ == 0;
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
      // A jump starts the heading afresh.
      has_prev_ = has_last_ && !onset;
      prev_rx_ = last_x_;
      prev_ry_ = last_y_;
      prev_t_ = last_t_;
      has_last_ = true;
      last_x_ = x;
      last_y_ = y;
      last_t_ = t;
   }

   // The end of a touch's press: its latest reading goes out now, if the finger has come to rest
   // (in a press the touch CC started, the finger is known to be down, so one reading will do).
   void settle() {
      if (pressed_ && readings_ == 0)
         return; // the touch CC's press waits for its first reading, however late
      settling_ = false;
      const bool rested = readings_ >= 2 && std::hypot(cand_x_ - prev_x_, cand_y_ - prev_y_) <= agree_;
      if (rested || (pressed_ && readings_ >= 1))
         hold(cand_x_, cand_y_, start_ + press());
      pressed_ = false;
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

   double press() const { return press_ < 0.0 ? window_ : press_; }

   double window_ = 0.0;
   double press_ = -1.0, jump_ = kTouchJump, agree_ = kTouchAgree;
   // The whole reading before the last, for the heading.
   bool has_prev_ = false;
   double prev_rx_ = 0.0, prev_ry_ = 0.0, prev_t_ = 0.0;
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
   // Settling a press the touch CC started (so one reading will do), not a jump within the touch.
   bool pressed_ = false;
   // Readings waiting to go out, in order.
   Reading queue_[kCapacity] = {};
   std::size_t head_ = 0, count_ = 0;
};

} // namespace jclap

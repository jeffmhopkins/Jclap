#pragma once

// The engines JKit can make sounds with. Each downloads on its own, shows its own sizes, and can be
// removed on its own.

namespace jkit::gen {

enum class Engine {
   SaoSmall, // Stable Audio Open Small: neural, ONNX Runtime, about 2.2 GB
   Orb,      // ORB: a small planner and physical models, 20.8 MB
};
constexpr int kEngineCount = 2;

inline int engineIndex(Engine e) { return static_cast<int>(e); }
inline Engine engineAt(int i) { return i == 1 ? Engine::Orb : Engine::SaoSmall; }

} // namespace jkit::gen

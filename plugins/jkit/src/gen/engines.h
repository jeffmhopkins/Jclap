#pragma once

// The engines JKit can make sounds with. Each downloads on its own, shows its own sizes, and can be
// removed on its own.

namespace jkit::gen {

enum class Engine {
   SaoSmall, // Stable Audio Open Small: neural, ONNX Runtime, about 2.2 GB
   Orb,      // ORB: a small planner and physical models, 20.8 MB
   SaoOpen1, // Stable Audio Open 1.0: neural, ONNX Runtime (shared with Small), about 4.8 GB
};
constexpr int kEngineCount = 3;

inline int engineIndex(Engine e) { return static_cast<int>(e); }
inline Engine engineAt(int i) { return i == 1 ? Engine::Orb : i == 2 ? Engine::SaoOpen1 : Engine::SaoSmall; }
// Whether the engine runs on ONNX Runtime (and so shares its download).
inline bool usesRuntime(Engine e) { return e != Engine::Orb; }

} // namespace jkit::gen

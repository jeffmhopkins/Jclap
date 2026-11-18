#pragma once

// ONNX Runtime, loaded at run time from the library JKit downloads (never linked), with the few calls
// the generator needs wrapped so nothing else includes its header.
//
//    Runtime rt;
//    if (!rt.load(libraryPath, error)) ...
//    Session dit;
//    if (!dit.open(rt, modelPath, threads, error)) ...
//    dit.run({...inputs...}, {"v"}, outputs, error);

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

struct OrtApi;
struct OrtEnv;
struct OrtSession;
struct OrtMemoryInfo;

namespace jkit::gen {

class Runtime {
public:
   Runtime() = default;
   Runtime(const Runtime &) = delete;
   Runtime &operator=(const Runtime &) = delete;
   ~Runtime();

   // Opens the library and makes an environment. False, with `error` set, if it can't.
   bool load(const std::filesystem::path &library, std::string &error);
   bool loaded() const { return api_ != nullptr; }
   // The library's own version, e.g. "1.22.0".
   std::string version() const { return version_; }

   const OrtApi *api() const { return api_; }
   OrtEnv *env() const { return env_; }
   OrtMemoryInfo *cpu() const { return cpu_; }

private:
   void *library_ = nullptr;
   const OrtApi *api_ = nullptr;
   OrtEnv *env_ = nullptr;
   OrtMemoryInfo *cpu_ = nullptr;
   std::string version_;
};

// A tensor in or out: its shape and its data, as floats or int64s.
struct Tensor {
   std::string name;
   std::vector<int64_t> shape;
   std::vector<float> f;
   std::vector<int64_t> i;
   bool isInt = false;

   static Tensor floats(std::string name, std::vector<int64_t> shape, std::vector<float> data) {
      Tensor t;
      t.name = std::move(name);
      t.shape = std::move(shape);
      t.f = std::move(data);
      return t;
   }
   static Tensor ints(std::string name, std::vector<int64_t> shape, std::vector<int64_t> data) {
      Tensor t;
      t.name = std::move(name);
      t.shape = std::move(shape);
      t.i = std::move(data);
      t.isInt = true;
      return t;
   }
};

class Session {
public:
   Session() = default;
   Session(const Session &) = delete;
   Session &operator=(const Session &) = delete;
   ~Session();

   // Loads a model on the CPU with `threads` threads (0 lets the runtime choose).
   bool open(const Runtime &runtime, const std::filesystem::path &model, int threads, std::string &error);
   bool isOpen() const { return session_ != nullptr; }
   void close();

   // Runs the model. Each input's data is read in place; the named outputs come back as float tensors.
   // Inputs are taken by non-const reference because the runtime's tensors point into their data.
   bool run(std::vector<Tensor> &inputs, const std::vector<std::string> &outputNames, std::vector<Tensor> &outputs,
            std::string &error);

private:
   const Runtime *runtime_ = nullptr;
   OrtSession *session_ = nullptr;
};

// The runtime's file name for this system: libonnxruntime.dylib, onnxruntime.dll or libonnxruntime.so.
const char *runtimeFileName();
// Where that file is in Microsoft's NuGet package for this system, e.g.
// "runtimes/osx-arm64/native/libonnxruntime.dylib". Empty on a system with none.
std::string runtimePackagePath();

} // namespace jkit::gen

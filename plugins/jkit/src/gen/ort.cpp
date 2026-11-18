#include "gen/ort.h"

#include <onnxruntime_c_api.h>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace jkit::gen {
namespace {

void *openLibrary(const std::filesystem::path &path, std::string &error) {
#if defined(_WIN32)
   // The full path, and its own folder for anything it loads in turn, so Windows' own (older)
   // onnxruntime.dll in System32 is never picked up instead.
   HMODULE h = LoadLibraryExW(path.wstring().c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
   if (!h)
      error = "couldn't load " + path.u8string() + " (error " + std::to_string(GetLastError()) + ")";
   return reinterpret_cast<void *>(h);
#else
   void *h = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
   if (!h) {
      const char *e = dlerror();
      error = e ? e : "couldn't load " + path.string();
   }
   return h;
#endif
}

void *symbol(void *library, const char *name) {
#if defined(_WIN32)
   return reinterpret_cast<void *>(GetProcAddress(reinterpret_cast<HMODULE>(library), name));
#else
   return dlsym(library, name);
#endif
}

// Takes a status: false and its message if it's an error, and releases it either way.
bool ok(const OrtApi *api, OrtStatus *status, std::string &error) {
   if (!status)
      return true;
   error = api->GetErrorMessage(status);
   api->ReleaseStatus(status);
   return false;
}

} // namespace

const char *runtimeFileName() {
#if defined(_WIN32)
   return "onnxruntime.dll";
#elif defined(__APPLE__)
   return "libonnxruntime.dylib";
#else
   return "libonnxruntime.so";
#endif
}

std::string runtimePackagePath() {
#if defined(_WIN32) && (defined(_M_ARM64) || defined(__aarch64__))
   return "runtimes/win-arm64/native/onnxruntime.dll";
#elif defined(_WIN32) && (defined(_M_X64) || defined(__x86_64__))
   return "runtimes/win-x64/native/onnxruntime.dll";
#elif defined(__APPLE__) && defined(__aarch64__)
   return "runtimes/osx-arm64/native/libonnxruntime.dylib";
#elif defined(__APPLE__) && defined(__x86_64__)
   return "runtimes/osx-x64/native/libonnxruntime.dylib";
#elif defined(__linux__) && defined(__aarch64__)
   return "runtimes/linux-arm64/native/libonnxruntime.so";
#elif defined(__linux__) && defined(__x86_64__)
   return "runtimes/linux-x64/native/libonnxruntime.so";
#else
   return {};
#endif
}

Runtime::~Runtime() {
   if (api_) {
      if (cpu_)
         api_->ReleaseMemoryInfo(cpu_);
      if (env_)
         api_->ReleaseEnv(env_);
   }
   // The library stays loaded: its threads and exit handlers may outlive this.
}

bool Runtime::load(const std::filesystem::path &library, std::string &error) {
   if (api_)
      return true;
   library_ = openLibrary(library, error);
   if (!library_)
      return false;
   using GetApiBase = const OrtApiBase *(ORT_API_CALL *)();
   auto getBase = reinterpret_cast<GetApiBase>(symbol(library_, "OrtGetApiBase"));
   const OrtApiBase *base = getBase ? getBase() : nullptr;
   const OrtApi *api = base ? base->GetApi(ORT_API_VERSION) : nullptr;
   if (!api) {
      error = "the ONNX Runtime library is too old or isn't one";
      return false;
   }
   version_ = base->GetVersionString();
   if (!ok(api, api->CreateEnv(ORT_LOGGING_LEVEL_ERROR, "JKit", &env_), error))
      return false;
   if (!ok(api, api->CreateCpuMemoryInfo(OrtArenaAllocator, OrtMemTypeDefault, &cpu_), error))
      return false;
   api_ = api;
   return true;
}

Session::~Session() { close(); }

void Session::close() {
   if (session_ && runtime_)
      runtime_->api()->ReleaseSession(session_);
   session_ = nullptr;
}

bool Session::open(const Runtime &runtime, const std::filesystem::path &model, int threads, std::string &error) {
   close();
   const OrtApi *api = runtime.api();
   if (!api) {
      error = "ONNX Runtime isn't loaded";
      return false;
   }
   runtime_ = &runtime;
   OrtSessionOptions *options = nullptr;
   if (!ok(api, api->CreateSessionOptions(&options), error))
      return false;
   bool good = ok(api, api->SetSessionGraphOptimizationLevel(options, ORT_ENABLE_ALL), error);
   if (good && threads > 0)
      good = ok(api, api->SetIntraOpNumThreads(options, threads), error);
#if defined(_WIN32)
   const std::wstring path = model.wstring();
#else
   const std::string path = model.string();
#endif
   if (good)
      good = ok(api, api->CreateSession(runtime.env(), path.c_str(), options, &session_), error);
   api->ReleaseSessionOptions(options);
   if (!good)
      session_ = nullptr;
   return good;
}

bool Session::run(std::vector<Tensor> &inputs, const std::vector<std::string> &outputNames, std::vector<Tensor> &outputs,
                  std::string &error) {
   if (!session_) {
      error = "the model isn't loaded";
      return false;
   }
   const OrtApi *api = runtime_->api();
   std::vector<OrtValue *> in(inputs.size(), nullptr), out(outputNames.size(), nullptr);
   std::vector<const char *> inNames, outNames;
   bool good = true;
   for (size_t k = 0; k < inputs.size() && good; ++k) {
      Tensor &t = inputs[k];
      inNames.push_back(t.name.c_str());
      if (t.isInt)
         good = ok(api,
                   api->CreateTensorWithDataAsOrtValue(runtime_->cpu(), t.i.data(), t.i.size() * sizeof(int64_t),
                                                       t.shape.data(), t.shape.size(),
                                                       ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64, &in[k]),
                   error);
      else
         good = ok(api,
                   api->CreateTensorWithDataAsOrtValue(runtime_->cpu(), t.f.data(), t.f.size() * sizeof(float),
                                                       t.shape.data(), t.shape.size(),
                                                       ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, &in[k]),
                   error);
   }
   for (const std::string &n : outputNames)
      outNames.push_back(n.c_str());
   if (good)
      good = ok(api,
                api->Run(session_, nullptr, inNames.data(), in.data(), in.size(), outNames.data(), outNames.size(),
                         out.data()),
                error);
   outputs.clear();
   for (size_t k = 0; k < out.size() && good; ++k) {
      OrtTensorTypeAndShapeInfo *info = nullptr;
      good = ok(api, api->GetTensorTypeAndShape(out[k], &info), error);
      if (!good)
         break;
      size_t dims = 0, count = 0;
      Tensor t;
      t.name = outputNames[k];
      good = ok(api, api->GetDimensionsCount(info, &dims), error);
      t.shape.resize(dims);
      good = good && ok(api, api->GetDimensions(info, t.shape.data(), dims), error) &&
             ok(api, api->GetTensorShapeElementCount(info, &count), error);
      api->ReleaseTensorTypeAndShapeInfo(info);
      if (!good)
         break;
      float *data = nullptr;
      good = ok(api, api->GetTensorMutableData(out[k], reinterpret_cast<void **>(&data)), error);
      if (good)
         t.f.assign(data, data + count);
      outputs.push_back(std::move(t));
   }
   for (OrtValue *v : in)
      if (v)
         api->ReleaseValue(v);
   for (OrtValue *v : out)
      if (v)
         api->ReleaseValue(v);
   return good;
}

} // namespace jkit::gen

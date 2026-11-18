#include "gen/studio.h"

#include <jclap/ui/presets.h>

#include <miniz.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>

#include "gen/fetch.h"

namespace jkit::gen {
namespace {

// ONNX Runtime 1.22.0's NuGet package: every system's library in one zip.
constexpr const char *kRuntimeVersion = "1.22.0";
const Download kRuntimePackage = {
   "https://api.nuget.org/v3-flatcontainer/microsoft.ml.onnxruntime/1.22.0/microsoft.ml.onnxruntime.1.22.0.nupkg", {},
   121402697, "d571e63a2329baacb713f441e65ad75284de354db6e1ac435fe4bebbb417986a"};

// Stable Audio Open Small as ONNX (Stability AI Community License), pinned to one revision.
constexpr const char *kModelRepo =
   "https://huggingface.co/atauser001/stable-audio-open-small-onnx/resolve/8f9544df5933b9ed4a7e8ed7fa6f77b3dd48ddce/";
struct ModelFile {
   const char *name;
   uint64_t size;
   const char *sha256;
};
const ModelFile kModelFiles[] = {
   {"LICENSE", 11852, "d6f6b1a4dce5c852bd6d7d9482d002baf0ccdb71e662250b73be9eec8764ee8d"},
   {"NOTICE", 1814, "e504694067fded6569e7c101962f711b310bae9935a717f88292eb30a59fcf55"},
   {"tokenizer.json", 1389353, "d2acde0d8d71dd30a711834b07781b9c89feaac33fd332f60507699282740066"},
   {"cond.onnx", 439547211, "847822809975d0c36a665594fad0312c4c225147910657820e9936b09014629c"},
   {"dec.onnx", 312575029, "96029859b4d563f3aff266e97e715190e8d53278c6e27ab94095535a0da4c911"},
   {"dit.onnx", 1364171978, "8e9c8b14baac4a26159d5ecf3d365ef5a097f321541f9548d62d136354a23d8b"},
};

// ORB's planner (Apache-2.0), pinned to one revision.
constexpr const char *kOrbRepo =
   "https://huggingface.co/marcosmaceo/orb-planner/resolve/a86264977fff764e7fa2bd3eee534e3baa177ec5/";
const ModelFile kOrbFiles[] = {
   {"config.json", 318, "2d64dc76f3ab7ea9b83190f82d26ef95d3a2bbedc6cf43bae79ef954751eb753"},
   {"planner.safetensors", 20795000, "b14a6d1939f5b0407d9c9d97b9c4468546b0fb5f30a7fb91175f55d69432015d"},
};

struct Files {
   const char *repo;
   const ModelFile *begin, *end;
   bool runtime; // needs ONNX Runtime
};
Files filesOf(Engine e) {
   if (e == Engine::Orb)
      return {kOrbRepo, std::begin(kOrbFiles), std::end(kOrbFiles), false};
   return {kModelRepo, std::begin(kModelFiles), std::end(kModelFiles), true};
}

uint64_t folderBytes(const std::filesystem::path &dir) {
   uint64_t total = 0;
   std::error_code ec;
   if (!std::filesystem::is_directory(dir, ec))
      return 0;
   for (auto it = std::filesystem::recursive_directory_iterator(dir, ec);
        !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec))
      if (it->is_regular_file(ec))
         total += it->file_size(ec);
   return total;
}

std::filesystem::path runtimeFolder() { return Studio::folder() / "runtime" / (std::string("onnxruntime-") + kRuntimeVersion); }
std::filesystem::path runtimeLibrary() { return runtimeFolder() / runtimeFileName(); }

bool hasFile(const std::filesystem::path &p, uint64_t size) {
   std::error_code ec;
   return std::filesystem::is_regular_file(p, ec) && std::filesystem::file_size(p, ec) == size && !ec;
}

// Copies one entry of a zip on disk to `to`, written beside it and renamed in.
bool unzipOne(const std::filesystem::path &zip, const std::string &entry, const std::filesystem::path &to,
              std::string &error) {
   std::ifstream in(zip, std::ios::binary);
   std::vector<char> data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
   if (data.empty()) {
      error = "couldn't read the ONNX Runtime package";
      return false;
   }
   mz_zip_archive archive{};
   if (!mz_zip_reader_init_mem(&archive, data.data(), data.size(), 0)) {
      error = "the ONNX Runtime package isn't a zip";
      return false;
   }
   bool ok = false;
   const int index = mz_zip_reader_locate_file(&archive, entry.c_str(), nullptr, 0);
   size_t size = 0;
   void *bytes = index >= 0 ? mz_zip_reader_extract_to_heap(&archive, static_cast<mz_uint>(index), &size, 0) : nullptr;
   if (!bytes) {
      error = "the ONNX Runtime package has no " + entry;
   } else {
      std::filesystem::path part = to;
      part += ".part";
      std::error_code ec;
      std::filesystem::create_directories(to.parent_path(), ec);
      {
         std::ofstream out(part, std::ios::binary);
         out.write(static_cast<const char *>(bytes), static_cast<std::streamsize>(size));
         ok = static_cast<bool>(out);
      }
      if (ok) {
         std::filesystem::rename(part, to, ec);
         ok = !ec;
      }
      if (!ok) {
         error = "couldn't write " + to.u8string();
         std::filesystem::remove(part, ec);
      }
      mz_free(bytes);
   }
   mz_zip_reader_end(&archive);
   return ok;
}

} // namespace

std::filesystem::path Studio::folder() {
   // For tests and development: somewhere else than the user's own folder.
   if (const char *dir = std::getenv("JKIT_FOLDER"); dir && *dir)
      return std::filesystem::u8path(dir);
   return jclap::ui::userFolder("JClap");
}

std::filesystem::path Studio::modelFolder(Engine engine) {
   return folder() / "models" / (engine == Engine::Orb ? "orb-planner" : "stable-audio-open-small");
}

uint64_t Studio::downloadBytes(Engine engine) {
   const Files f = filesOf(engine);
   uint64_t total = f.runtime ? kRuntimePackage.size : 0;
   for (const ModelFile *m = f.begin; m != f.end; ++m)
      total += m->size;
   return total;
}

uint64_t Studio::diskBytes(Engine engine) {
   uint64_t total = folderBytes(modelFolder(engine));
   if (filesOf(engine).runtime)
      total += folderBytes(folder() / "runtime");
   return total;
}

void Studio::remove(Engine engine) {
   {
      std::lock_guard<std::mutex> lock(mutex_);
      // Stop whatever runs now only if it is this engine's (a download, a load or a job).
      const State st = status_[engineIndex(engine)].state;
      const auto running = jobs_.find(runningJob_);
      if (st == State::Downloading || st == State::Loading ||
          (running != jobs_.end() && running->second.request.engine == engine))
         cancel_.store(true);
      wantRemove_[engineIndex(engine)] = true;
      wantInstall_[engineIndex(engine)] = wantLoad_[engineIndex(engine)] = false;
   }
   wake_.notify_all();
}

void Studio::removeNow(Engine engine) {
   const int e = engineIndex(engine);
   std::error_code ec;
   if (engine == Engine::Orb) {
      orb_.unload();
   } else {
      sao_.unload();
      // The runtime stays loaded until the last JKit closes (a library can't safely be unloaded while
      // its sessions may linger), but its file goes (on Windows a loaded DLL can't be deleted, so it goes next time).
      std::filesystem::remove_all(folder() / "runtime", ec);
   }
   std::filesystem::remove_all(modelFolder(engine), ec);
   std::lock_guard<std::mutex> lock(mutex_);
   for (auto it = queue_.begin(); it != queue_.end();) {
      Job &job = jobs_[*it];
      if (job.request.engine == engine) {
         job.state = JobState::Failed;
         job.error = "the model was removed";
         it = queue_.erase(it);
      } else {
         ++it;
      }
   }
   status_[e].state = State::Missing;
   status_[e].error.clear();
   status_[e].received = status_[e].total = 0;
   received_[e].store(0);
}

std::mutex &Studio::instanceMutex() {
   static std::mutex m;
   return m;
}
Studio *&Studio::instance() {
   static Studio *s = nullptr;
   return s;
}
int &Studio::users() {
   static int n = 0;
   return n;
}

Studio::Handle::Handle() {
   std::lock_guard<std::mutex> lock(instanceMutex());
   if (!instance())
      instance() = new Studio();
   ++users();
   studio_ = instance();
}

Studio::Handle::~Handle() {
   std::lock_guard<std::mutex> lock(instanceMutex());
   if (--users() == 0) {
      delete instance();
      instance() = nullptr;
   }
}

Studio::Studio() {
   for (int e = 0; e < kEngineCount; ++e)
      status_[e].state = installedOnDisk(engineAt(e)) ? State::Installed : State::Missing;
   thread_ = std::thread([this] { run(); });
}

Studio::~Studio() {
   {
      std::lock_guard<std::mutex> lock(mutex_);
      quit_ = true;
   }
   cancel_.store(true);
   wake_.notify_all();
   if (thread_.joinable())
      thread_.join();
}

bool Studio::installedOnDisk(Engine engine) {
   const Files f = filesOf(engine);
   std::error_code ec;
   if (f.runtime && !std::filesystem::is_regular_file(runtimeLibrary(), ec))
      return false;
   for (const ModelFile *m = f.begin; m != f.end; ++m)
      if (!hasFile(modelFolder(engine) / m->name, m->size))
         return false;
   return true;
}

Studio::Status Studio::status(Engine engine) const {
   std::lock_guard<std::mutex> lock(mutex_);
   Status s = status_[engineIndex(engine)];
   s.received = received_[engineIndex(engine)].load();
   return s;
}

void Studio::setState(Engine engine, State s, const std::string &error) {
   std::lock_guard<std::mutex> lock(mutex_);
   status_[engineIndex(engine)].state = s;
   status_[engineIndex(engine)].error = error;
}

void Studio::install(Engine engine) {
   {
      std::lock_guard<std::mutex> lock(mutex_);
      const State st = status_[engineIndex(engine)].state;
      if (st != State::Missing && st != State::Failed)
         return;
      wantInstall_[engineIndex(engine)] = true;
   }
   wake_.notify_all();
}

void Studio::warmUp(Engine engine) {
   {
      std::lock_guard<std::mutex> lock(mutex_);
      const int e = engineIndex(engine);
      wantLoad_[e] = true;
      if (status_[e].state == State::Missing || status_[e].state == State::Failed)
         wantInstall_[e] = true;
   }
   wake_.notify_all();
}

void Studio::cancel() { cancel_.store(true); }

int Studio::submit(const Request &request) {
   int id;
   {
      std::lock_guard<std::mutex> lock(mutex_);
      id = nextJob_++;
      jobs_[id].request = request;
      queue_.push_back(id);
      const int e = engineIndex(request.engine);
      if (status_[e].state == State::Missing || status_[e].state == State::Failed)
         wantInstall_[e] = true;
   }
   wake_.notify_all();
   return id;
}

Studio::JobState Studio::jobState(int job) const {
   std::lock_guard<std::mutex> lock(mutex_);
   const auto it = jobs_.find(job);
   return it == jobs_.end() ? JobState::Unknown : it->second.state;
}

double Studio::jobProgress(int job) const {
   std::lock_guard<std::mutex> lock(mutex_);
   const auto it = jobs_.find(job);
   return it == jobs_.end() ? 0.0 : it->second.progress;
}

bool Studio::take(int job, std::vector<Sound> &out, std::string &error) {
   std::lock_guard<std::mutex> lock(mutex_);
   const auto it = jobs_.find(job);
   if (it == jobs_.end() || (it->second.state != JobState::Done && it->second.state != JobState::Failed))
      return false;
   out = std::move(it->second.sounds);
   error = it->second.error;
   const bool good = it->second.state == JobState::Done;
   jobs_.erase(it);
   if (!good && error.empty())
      error = "failed";
   return true;
}

void Studio::drop(int job) {
   std::lock_guard<std::mutex> lock(mutex_);
   queue_.erase(std::remove(queue_.begin(), queue_.end(), job), queue_.end());
   if (job == runningJob_)
      cancel_.store(true);
   jobs_.erase(job);
}

bool Studio::installNow(Engine engine, std::string &error) {
   const int e = engineIndex(engine);
   cancel_.store(false);
   received_[e].store(0);
   {
      std::lock_guard<std::mutex> lock(mutex_);
      status_[e].state = State::Downloading;
      status_[e].total = downloadBytes(engine);
      status_[e].error.clear();
   }
   const Files files = filesOf(engine);
   uint64_t done = 0;
   const auto progress = [&](uint64_t got) { received_[e].store(done + got); };
   if (files.runtime) {
      // The runtime: the package, then its one library for this system.
      if (!std::filesystem::exists(runtimeLibrary())) {
         const std::string entry = runtimePackagePath();
         if (entry.empty()) {
            error = "ONNX Runtime has no build for this system";
            return false;
         }
         Download d = kRuntimePackage;
         d.file = runtimeFolder() / "onnxruntime.nupkg";
         if (!fetch(d, progress, cancel_, error))
            return false;
         const bool ok = unzipOne(d.file, entry, runtimeLibrary(), error);
         std::error_code ec;
         std::filesystem::remove(d.file, ec);
         if (!ok)
            return false;
      }
      done += kRuntimePackage.size;
      received_[e].store(done);
   }
   for (const ModelFile *f = files.begin; f != files.end; ++f) {
      const std::filesystem::path file = modelFolder(engine) / f->name;
      if (!hasFile(file, f->size)) {
         Download d{std::string(files.repo) + f->name, file, f->size, f->sha256};
         if (!fetch(d, progress, cancel_, error))
            return false;
      }
      done += f->size;
      received_[e].store(done);
   }
   return true;
}

bool Studio::loaded(Engine engine) const { return engine == Engine::Orb ? orb_.loaded() : sao_.loaded(); }

bool Studio::loadNow(Engine engine, std::string &error) {
   if (loaded(engine))
      return true;
   setState(engine, State::Loading);
   if (engine == Engine::Orb)
      return orb_.load(modelFolder(engine), error);
   if (!runtime_.loaded() && !runtime_.load(runtimeLibrary(), error))
      return false;
   {
      std::lock_guard<std::mutex> lock(mutex_);
      status_[engineIndex(engine)].runtimeVersion = runtime_.version();
   }
   // Leave a core for the audio and the host.
   const int threads = std::max(1, static_cast<int>(std::thread::hardware_concurrency()) - 1);
   return sao_.load(runtime_, modelFolder(engine), threads, error);
}

void Studio::run() {
   for (;;) {
      int install = -1, load = -1, remove = -1;
      int job = 0;
      {
         std::unique_lock<std::mutex> lock(mutex_);
         const auto pending = [&](const bool (&want)[kEngineCount]) {
            for (int e = 0; e < kEngineCount; ++e)
               if (want[e])
                  return e;
            return -1;
         };
         wake_.wait(lock, [&] {
            return quit_ || pending(wantInstall_) >= 0 || pending(wantLoad_) >= 0 || pending(wantRemove_) >= 0 ||
                   !queue_.empty();
         });
         if (quit_)
            return;
         // Removing first, then downloads, then loads, then jobs.
         if ((remove = pending(wantRemove_)) >= 0) {
            wantRemove_[remove] = false;
         } else if ((install = pending(wantInstall_)) >= 0) {
            wantInstall_[install] = false;
         } else if ((load = pending(wantLoad_)) >= 0) {
            wantLoad_[load] = false;
         } else {
            job = queue_.front();
            queue_.pop_front();
            jobs_[job].state = JobState::Running;
            runningJob_ = job;
         }
      }
      if (remove >= 0) {
         removeNow(engineAt(remove));
         continue;
      }
      std::string error;
      if (install >= 0) {
         const Engine engine = engineAt(install);
         if (installNow(engine, error))
            setState(engine, State::Installed);
         else
            setState(engine, error == "cancelled" ? State::Missing : State::Failed, error);
         continue;
      }
      // Loading needs the files; a job that comes before them fails at once.
      const auto ready = [&](Engine engine) -> bool {
         const int e = engineIndex(engine);
         {
            std::lock_guard<std::mutex> lock(mutex_);
            const State st = status_[e].state;
            if (st == State::Missing || st == State::Failed || st == State::Downloading) {
               if (st != State::Failed || !installedOnDisk(engine)) {
                  error = st == State::Downloading ? "still downloading" : "the model isn't downloaded";
                  return false;
               }
            }
         }
         if (loadNow(engine, error)) {
            setState(engine, State::Ready);
            return true;
         }
         setState(engine, State::Failed, error);
         return false;
      };
      if (load >= 0) {
         ready(engineAt(load));
         continue;
      }
      if (!job)
         continue;
      cancel_.store(false);
      Request request;
      {
         std::lock_guard<std::mutex> lock(mutex_);
         const auto it = jobs_.find(job);
         if (it == jobs_.end()) {
            runningJob_ = 0;
            continue;
         }
         request = it->second.request;
      }
      std::vector<Sound> sounds;
      bool ok = ready(request.engine);
      const auto progress = [&](double p) {
         std::lock_guard<std::mutex> lock(mutex_);
         const auto it = jobs_.find(job);
         if (it != jobs_.end())
            it->second.progress = p;
      };
      if (ok)
         ok = request.engine == Engine::Orb ? orb_.generate(request, sounds, progress, cancel_, error)
                                            : sao_.generate(request, sounds, progress, cancel_, error);
      std::lock_guard<std::mutex> lock(mutex_);
      runningJob_ = 0;
      const auto it = jobs_.find(job);
      if (it != jobs_.end()) {
         it->second.state = ok ? JobState::Done : JobState::Failed;
         it->second.sounds = std::move(sounds);
         it->second.error = error;
         it->second.progress = 1.0;
      }
   }
}

} // namespace jkit::gen

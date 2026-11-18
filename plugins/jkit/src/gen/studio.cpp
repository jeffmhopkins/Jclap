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

std::filesystem::path Studio::modelFolder() { return folder() / "models" / "stable-audio-open-small"; }

uint64_t Studio::downloadBytes() {
   uint64_t total = kRuntimePackage.size;
   for (const ModelFile &f : kModelFiles)
      total += f.size;
   return total;
}

uint64_t Studio::diskBytes() {
   uint64_t total = 0;
   std::error_code ec;
   for (const std::filesystem::path &dir : {modelFolder(), folder() / "runtime"}) {
      if (!std::filesystem::is_directory(dir, ec))
         continue;
      for (auto it = std::filesystem::recursive_directory_iterator(dir, ec);
           !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec))
         if (it->is_regular_file(ec))
            total += it->file_size(ec);
   }
   return total;
}

void Studio::remove() {
   cancel_.store(true);
   {
      std::lock_guard<std::mutex> lock(mutex_);
      wantRemove_ = true;
      wantInstall_ = wantLoad_ = false;
   }
   wake_.notify_all();
}

void Studio::removeNow() {
   model_.unload();
   std::error_code ec;
   std::filesystem::remove_all(modelFolder(), ec);
   // The runtime stays loaded until the last JKit closes (a library can't safely be unloaded while
   // its sessions may linger), but its file goes (on Windows a loaded DLL can't be deleted, so it goes next time).
   std::filesystem::remove_all(folder() / "runtime", ec);
   std::lock_guard<std::mutex> lock(mutex_);
   for (int id : queue_) {
      jobs_[id].state = JobState::Failed;
      jobs_[id].error = "the model was removed";
   }
   queue_.clear();
   status_.state = State::Missing;
   status_.error.clear();
   status_.received = status_.total = 0;
   received_.store(0);
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
   status_.state = installedOnDisk() ? State::Installed : State::Missing;
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

bool Studio::installedOnDisk() const {
   std::error_code ec;
   if (!std::filesystem::is_regular_file(runtimeLibrary(), ec))
      return false;
   for (const ModelFile &f : kModelFiles)
      if (!hasFile(modelFolder() / f.name, f.size))
         return false;
   return true;
}

Studio::Status Studio::status() const {
   std::lock_guard<std::mutex> lock(mutex_);
   Status s = status_;
   s.received = received_.load();
   return s;
}

void Studio::setState(State s, const std::string &error) {
   std::lock_guard<std::mutex> lock(mutex_);
   status_.state = s;
   status_.error = error;
}

void Studio::install() {
   {
      std::lock_guard<std::mutex> lock(mutex_);
      if (status_.state != State::Missing && status_.state != State::Failed)
         return;
      wantInstall_ = true;
   }
   wake_.notify_all();
}

void Studio::warmUp() {
   {
      std::lock_guard<std::mutex> lock(mutex_);
      wantLoad_ = true;
      if (status_.state == State::Missing || status_.state == State::Failed)
         wantInstall_ = true;
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
      if (status_.state == State::Missing || status_.state == State::Failed)
         wantInstall_ = true;
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

bool Studio::installNow(std::string &error) {
   cancel_.store(false);
   received_.store(0);
   {
      std::lock_guard<std::mutex> lock(mutex_);
      status_.state = State::Downloading;
      status_.total = downloadBytes();
      status_.error.clear();
   }
   uint64_t done = 0;
   const auto progress = [&](uint64_t got) { received_.store(done + got); };
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
   received_.store(done);
   for (const ModelFile &f : kModelFiles) {
      const std::filesystem::path file = modelFolder() / f.name;
      if (!hasFile(file, f.size)) {
         Download d{std::string(kModelRepo) + f.name, file, f.size, f.sha256};
         if (!fetch(d, progress, cancel_, error))
            return false;
      }
      done += f.size;
      received_.store(done);
   }
   return true;
}

bool Studio::loadNow(std::string &error) {
   if (model_.loaded())
      return true;
   setState(State::Loading);
   if (!runtime_.loaded() && !runtime_.load(runtimeLibrary(), error))
      return false;
   {
      std::lock_guard<std::mutex> lock(mutex_);
      status_.runtimeVersion = runtime_.version();
   }
   // Leave a core for the audio and the host.
   const int threads = std::max(1, static_cast<int>(std::thread::hardware_concurrency()) - 1);
   return model_.load(runtime_, modelFolder(), threads, error);
}

void Studio::run() {
   for (;;) {
      bool install = false, load = false;
      int job = 0;
      {
         std::unique_lock<std::mutex> lock(mutex_);
         wake_.wait(lock, [&] { return quit_ || wantInstall_ || wantLoad_ || wantRemove_ || !queue_.empty(); });
         if (quit_)
            return;
         if (wantRemove_) {
            wantRemove_ = false;
            lock.unlock();
            removeNow();
            continue;
         }
         install = wantInstall_;
         load = wantLoad_;
         wantInstall_ = wantLoad_ = false;
         if (!install && !load && !queue_.empty()) {
            job = queue_.front();
            queue_.pop_front();
            jobs_[job].state = JobState::Running;
            runningJob_ = job;
         }
      }
      std::string error;
      if (install) {
         if (installNow(error))
            setState(State::Installed);
         else
            setState(error == "cancelled" ? State::Missing : State::Failed, error);
         continue;
      }
      // Loading needs the files; a job that comes before them fails at once.
      const auto ready = [&]() -> bool {
         {
            std::lock_guard<std::mutex> lock(mutex_);
            if (status_.state == State::Missing || status_.state == State::Failed || status_.state == State::Downloading) {
               if (status_.state != State::Failed || !installedOnDisk()) {
                  error = status_.state == State::Downloading ? "still downloading" : "the model isn't downloaded";
                  return false;
               }
            }
         }
         if (loadNow(error)) {
            setState(State::Ready);
            return true;
         }
         setState(State::Failed, error);
         return false;
      };
      if (load) {
         ready();
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
      bool ok = ready();
      if (ok)
         ok = model_.generate(
            request, sounds,
            [&](double p) {
               std::lock_guard<std::mutex> lock(mutex_);
               const auto it = jobs_.find(job);
               if (it != jobs_.end())
                  it->second.progress = p;
            },
            cancel_, error);
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

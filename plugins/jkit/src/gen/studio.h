#pragma once

// The generator, shared by every JKit in the process: it downloads each engine the first time it's
// wanted, loads it once (Stable Audio takes 2 GB of memory, so never once per instance), and works
// through generation jobs one at a time on its own thread, so neither the audio nor the panel ever
// waits on it. Each engine downloads, loads and is removed on its own.
//
// Files go in the user's JClap folder (~/Library/Application Support/JClap on macOS, %APPDATA%\JClap
// on Windows, ~/.config/JClap on Linux):
//
//    runtime/onnxruntime-1.22.0/   the ONNX Runtime library, from Microsoft's NuGet package
//    models/stable-audio-open-small/   tokenizer.json, cond.onnx, dit.onnx, dec.onnx, LICENSE, NOTICE
//    models/stable-audio-open-1.0/     tokenizer.json, cond.onnx, dit.onnx (+ .data), dec.onnx, LICENSE, NOTICE
//    models/orb-planner/               planner.safetensors, config.json (ORB needs no runtime)
//
// Both Stable Audios share the runtime: it downloads with the first and goes with the last.
//
// Every download is pinned to a SHA-256, checked as it comes in.
//
//    Studio::Handle studio;                 // one per plugin instance; the last one out stops it
//    studio->install(Engine::Orb);          // when the user asks for an engine
//    const int job = studio->submit(req);   // later
//    if (studio->take(job, sounds, error)) ...

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "gen/orb_engine.h"
#include "gen/sao.h"

namespace jkit::gen {

class Studio {
public:
   enum class State {
      Missing,     // not downloaded
      Downloading, // install() running
      Installed,   // on disk, not loaded
      Loading,     // loading into memory
      Ready,       // loaded
      Failed,      // the last install or load failed; error says why
   };

   struct Status {
      State state = State::Missing;
      // While downloading: bytes so far and in all.
      uint64_t received = 0, total = 0;
      std::string error;
      std::string runtimeVersion;
   };

   enum class JobState { Queued, Running, Done, Failed, Unknown };

   // Holds the studio while it lives: the first handle makes it, the last one stops its thread and
   // frees the model.
   class Handle {
   public:
      Handle();
      ~Handle();
      Handle(const Handle &) = delete;
      Handle &operator=(const Handle &) = delete;
      Studio *operator->() const { return studio_; }
      Studio &operator*() const { return *studio_; }

   private:
      Studio *studio_;
   };

   Status status(Engine engine) const;
   // An engine's whole download, for the button that starts it.
   static uint64_t downloadBytes(Engine engine);
   // The folder of the user's JClap files.
   static std::filesystem::path folder();
   static std::filesystem::path modelFolder(Engine engine);

   // Bytes an engine takes on disk now (whatever has been downloaded so far, with its runtime).
   static uint64_t diskBytes(Engine engine);

   // Starts downloading whatever the engine is missing (no-op if installed or already downloading).
   void install(Engine engine);
   // Unloads the engine and deletes its files (and its runtime), freeing the disk space. Stops its
   // download or a generation first; its queued jobs fail.
   void remove(Engine engine);
   // Stops a download, or the job running now.
   void cancel();
   // Loads the engine now, so the first generation doesn't wait for it. Installs it first if needed.
   void warmUp(Engine engine);

   // Queues a generation; returns its job id. It installs and loads the model first if it must.
   int submit(const Request &request);
   JobState jobState(int job) const;
   // 0..1, for a queued or running job (loading counts as 0).
   double jobProgress(int job) const;
   // Takes a finished job's sounds (or its error) and forgets it. False while it isn't finished.
   bool take(int job, std::vector<Sound> &out, std::string &error);
   // Drops a job: a queued one never runs, a running one stops.
   void drop(int job);

private:
   Studio();
   ~Studio();
   static std::mutex &instanceMutex();
   static Studio *&instance();
   static int &users();

   struct Job {
      Request request;
      JobState state = JobState::Queued;
      double progress = 0.0;
      std::vector<Sound> sounds;
      std::string error;
   };

   void run();
   bool installNow(Engine engine, std::string &error);
   bool loadNow(Engine engine, std::string &error);
   bool loaded(Engine engine) const;
   void removeNow(Engine engine);
   static bool installedOnDisk(Engine engine);
   void setState(Engine engine, State s, const std::string &error = {});

   mutable std::mutex mutex_;
   std::condition_variable wake_;
   std::thread thread_;
   bool quit_ = false;
   bool wantInstall_[kEngineCount] = {}, wantLoad_[kEngineCount] = {}, wantRemove_[kEngineCount] = {};
   Status status_[kEngineCount];
   std::atomic<bool> cancel_{false};
   std::atomic<uint64_t> received_[kEngineCount] = {};
   int nextJob_ = 1, runningJob_ = 0;
   std::deque<int> queue_;
   std::map<int, Job> jobs_;

   Runtime runtime_;
   StableAudio sao_{StableAudio::Model::Small}, sao1_{StableAudio::Model::Open1};
   OrbEngine orb_;
};

} // namespace jkit::gen

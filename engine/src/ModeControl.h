#pragma once

#include <atomic>
#include <mutex>
#include <string>
#include <thread>

enum class RuntimeAudioMode
{
  Surround = 0,
  Guitar,
  Starting,
  Stopping,
  Error
};

const char* RuntimeAudioModeName(RuntimeAudioMode mode);

// Sends one command to the already-running engine control pipe.
// Commands: "surround", "guitar", "status".
bool SendModeCommand(const std::string& command, std::string& response, unsigned timeoutMs = 2000);

class ModeControlServer
{
public:
  ModeControlServer() = default;
  ~ModeControlServer();

  bool Start(std::atomic<RuntimeAudioMode>* desired,
             std::atomic<RuntimeAudioMode>* current,
             std::string* lastError,
             std::mutex* errorMutex);
  void Stop();

private:
  void ThreadProc();

  std::atomic<RuntimeAudioMode>* desired_ = nullptr;
  std::atomic<RuntimeAudioMode>* current_ = nullptr;
  std::string* lastError_ = nullptr;
  std::mutex* errorMutex_ = nullptr;

  std::atomic_bool stop_{false};
  std::thread thread_;
};

// Native two-button controller built into engine.exe. It talks to the persistent
// background engine over the same named pipe used by --mode.
int RunModeSwitcherGui();

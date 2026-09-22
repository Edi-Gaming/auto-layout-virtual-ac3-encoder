#pragma once

#include "ModeControl.h"

#include <atomic>
#include <mutex>
#include <string>
#include <thread>

class TrayIcon
{
public:
  TrayIcon() = default;
  ~TrayIcon();

  bool Start(std::atomic<RuntimeAudioMode>* desired,
             std::atomic<RuntimeAudioMode>* current,
             std::string* lastError,
             std::mutex* errorMutex,
             std::atomic_bool* stopEngine,
             const std::string& logPath);
  void Stop();

private:
  void ThreadProc();
  void AddIcon();
  void RemoveIcon();
  void UpdateIcon(bool force = false);
  void ShowContextMenu();
  void LaunchSwitcher();
  void OpenLog();

  static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

  std::atomic<RuntimeAudioMode>* desired_ = nullptr;
  std::atomic<RuntimeAudioMode>* current_ = nullptr;
  std::string* lastError_ = nullptr;
  std::mutex* errorMutex_ = nullptr;
  std::atomic_bool* stopEngine_ = nullptr;
  std::string logPath_;

  std::atomic_bool stop_{false};
  std::thread thread_;

  HWND hwnd_ = nullptr;
  UINT taskbarCreatedMsg_ = 0;
  RuntimeAudioMode lastMode_ = RuntimeAudioMode::Error;
  bool iconAdded_ = false;
};

#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

struct MusicTelemetrySnapshot
{
  uint64_t sequence = 0;
  uint64_t engineFrameCounter = 0;
  float ambience = 0.0f;
  float centerConfidence = 0.0f;
  float spatialBinFraction = 0.0f;
  float transientConfidence = 0.0f;
  float rearOpen = 0.0f;
  float frontLockConfidence = 0.0f;
  float rearBudgetScale = 1.0f;
  std::array<float, 6> speakerRms{{0, 0, 0, 0, 0, 0}};
  std::array<float, 4> ownership{{0, 0, 0, 0}};
  std::array<float, 4> bandCenter{{0, 0, 0, 0}};
};

struct CaptureMetadata
{
  std::string buildSha = "unknown";
  std::string branch = "feature/auto-layout";
  std::string captureRoot;
  std::string sourceDevice;
  std::string outputDevice;
  std::string layout;
  std::string stereoProcessing;
  int sampleRate = 48000;
  int64_t bitRate = 640000;
  int processingLatencySamples = 0;
  std::array<double, 6> speakerDistancesInches{{0, 0, 0, 0, 0, 0}};
  std::array<double, 6> speakerTrims{{1, 1, 1, 1, 1, 1}};
  std::map<std::string, std::string> configSnapshot;
};

class MusicCaptureLogger
{
public:
  enum class State
  {
    Idle,
    Armed,
    Recording,
    Complete,
    Saving,
    Saved,
    Cancelled,
    Error
  };

  struct Status
  {
    State state = State::Idle;
    uint64_t frames = 0;
    uint64_t targetFrames = 0;
    int requestedSeconds = 0;
    std::string path;
    std::string error;

    std::string ToCompactString() const;
  };

  MusicCaptureLogger();
  ~MusicCaptureLogger();

  MusicCaptureLogger(const MusicCaptureLogger&) = delete;
  MusicCaptureLogger& operator=(const MusicCaptureLogger&) = delete;

  bool Configure(const CaptureMetadata& metadata);
  void SetUnavailable();
  bool Arm(int seconds);
  bool Cancel();
  bool WantsPacket() const;

  // Real-time path: only atomics, bounded memcpy and fixed-size struct writes.
  // Arm() preallocates every buffer before recording can begin.
  void PushPacket(const float* stereo,
                  const float* out51,
                  size_t frames,
                  const MusicTelemetrySnapshot& telemetry);

  Status GetStatus() const;
  std::string LatestPath() const;

private:
  struct TelemetryRow
  {
    uint64_t captureFrameStart = 0;
    MusicTelemetrySnapshot telemetry;
  };

  void WriterProc();
  bool WriteBundle(std::string& error);
  static const char* StateName(State state);

  std::atomic<State> state_{State::Idle};
  std::atomic_bool stopWriter_{false};
  std::atomic_bool configured_{false};
  std::atomic<uint32_t> pushInFlight_{0};
  std::atomic<uint64_t> capturedFrames_{0};
  std::atomic<uint64_t> telemetryCount_{0};

  uint64_t targetFrames_ = 0;
  int requestedSeconds_ = 0;

  std::vector<float> inputStereo_;
  std::vector<float> output51_;
  std::vector<TelemetryRow> telemetryRows_;

  mutable std::mutex metadataMutex_;
  CaptureMetadata configuredMetadata_;
  CaptureMetadata activeMetadata_;
  std::string captureStartUtc_;
  std::string activePath_;

  mutable std::mutex statusMutex_;
  std::string latestPath_;
  std::string lastError_;

  std::thread writerThread_;
};

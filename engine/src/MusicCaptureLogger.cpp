#include "MusicCaptureLogger.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <thread>

namespace {

constexpr uint16_t kWaveFormatIeeeFloat = 0x0003;
constexpr uint16_t kWaveFormatExtensible = 0xFFFE;

// FL | FR | FC | LFE | SL | SR (KSAUDIO_SPEAKER_5POINT1_SURROUND).
constexpr uint32_t kSpeakerMask51Surround = 0x0000060F;

void WriteU16(std::ofstream& f, uint16_t v)
{
  const unsigned char b[2] = {
      static_cast<unsigned char>(v & 0xff),
      static_cast<unsigned char>((v >> 8) & 0xff)};
  f.write(reinterpret_cast<const char*>(b), sizeof b);
}

void WriteU32(std::ofstream& f, uint32_t v)
{
  const unsigned char b[4] = {
      static_cast<unsigned char>(v & 0xff),
      static_cast<unsigned char>((v >> 8) & 0xff),
      static_cast<unsigned char>((v >> 16) & 0xff),
      static_cast<unsigned char>((v >> 24) & 0xff)};
  f.write(reinterpret_cast<const char*>(b), sizeof b);
}

bool WriteFloatWav(const std::filesystem::path& path,
                   const float* samples,
                   uint64_t frames,
                   uint16_t channels,
                   uint32_t sampleRate,
                   bool extensible)
{
  if (!samples || channels == 0 ||
      frames > 0xFFFFFFFFull / (static_cast<uint64_t>(channels) * sizeof(float)))
    return false;

  const uint32_t dataBytes = static_cast<uint32_t>(
      frames * static_cast<uint64_t>(channels) * sizeof(float));
  const uint16_t blockAlign = static_cast<uint16_t>(channels * sizeof(float));
  const uint32_t byteRate = sampleRate * blockAlign;

  std::ofstream f(path, std::ios::binary | std::ios::trunc);
  if (!f) return false;

  f.write("RIFF", 4);
  WriteU32(f, (extensible ? 60u : 36u) + dataBytes);
  f.write("WAVE", 4);
  f.write("fmt ", 4);

  if (!extensible)
  {
    WriteU32(f, 16);
    WriteU16(f, kWaveFormatIeeeFloat);
    WriteU16(f, channels);
    WriteU32(f, sampleRate);
    WriteU32(f, byteRate);
    WriteU16(f, blockAlign);
    WriteU16(f, 32);
  }
  else
  {
    WriteU32(f, 40);
    WriteU16(f, kWaveFormatExtensible);
    WriteU16(f, channels);
    WriteU32(f, sampleRate);
    WriteU32(f, byteRate);
    WriteU16(f, blockAlign);
    WriteU16(f, 32);
    WriteU16(f, 22); // cbSize
    WriteU16(f, 32); // valid bits
    WriteU32(f, kSpeakerMask51Surround);

    // KSDATAFORMAT_SUBTYPE_IEEE_FLOAT = 00000003-0000-0010-8000-00AA00389B71.
    const unsigned char guid[16] = {
        0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00,
        0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71};
    f.write(reinterpret_cast<const char*>(guid), sizeof guid);
  }

  f.write("data", 4);
  WriteU32(f, dataBytes);
  f.write(reinterpret_cast<const char*>(samples), dataBytes);
  return static_cast<bool>(f);
}

std::string JsonEscape(const std::string& s)
{
  std::ostringstream out;
  for (unsigned char c : s)
  {
    switch (c)
    {
      case '"': out << "\\\""; break;
      case '\\': out << "\\\\"; break;
      case '\b': out << "\\b"; break;
      case '\f': out << "\\f"; break;
      case '\n': out << "\\n"; break;
      case '\r': out << "\\r"; break;
      case '\t': out << "\\t"; break;
      default:
        if (c < 0x20)
          out << "\\u" << std::hex << std::setw(4) << std::setfill('0')
              << static_cast<int>(c) << std::dec;
        else
          out << static_cast<char>(c);
    }
  }
  return out.str();
}

std::string UtcStamp(bool compact)
{
  const auto now = std::chrono::system_clock::now();
  const std::time_t t = std::chrono::system_clock::to_time_t(now);
  std::tm tm{};
#ifdef _WIN32
  gmtime_s(&tm, &t);
#else
  gmtime_r(&t, &tm);
#endif
  char buf[64] = {};
  std::strftime(buf, sizeof buf, compact ? "%Y%m%d-%H%M%S" : "%Y-%m-%dT%H:%M:%SZ", &tm);
  return buf;
}

std::string SafeToken(std::string s)
{
  for (char& c : s)
  {
    const bool safe =
        (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') || c == '-' || c == '_';
    if (!safe) c = '_';
  }
  if (s.empty()) s = "unknown";
  return s;
}

std::string CsvFloat(float v)
{
  std::ostringstream out;
  out << std::fixed << std::setprecision(7) << v;
  return out.str();
}

} // namespace

MusicCaptureLogger::MusicCaptureLogger()
{
  writerThread_ = std::thread(&MusicCaptureLogger::WriterProc, this);
}

MusicCaptureLogger::~MusicCaptureLogger()
{
  stopWriter_.store(true, std::memory_order_release);
  if (writerThread_.joinable())
    writerThread_.join();
}

const char* MusicCaptureLogger::StateName(State state)
{
  switch (state)
  {
    case State::Idle: return "idle";
    case State::Armed: return "armed";
    case State::Recording: return "recording";
    case State::Complete: return "complete";
    case State::Saving: return "saving";
    case State::Saved: return "saved";
    case State::Cancelled: return "cancelled";
    case State::Error: return "error";
  }
  return "error";
}

std::string MusicCaptureLogger::Status::ToCompactString() const
{
  std::ostringstream out;
  out << "capture_state=" << MusicCaptureLogger::StateName(state)
      << ";frames=" << frames
      << ";target=" << targetFrames
      << ";seconds=" << std::fixed << std::setprecision(3)
      << (static_cast<double>(frames) / 48000.0)
      << ";requested_seconds=" << requestedSeconds;
  if (!path.empty()) out << ";path=" << path;
  if (!error.empty()) out << ";error=" << error;
  return out.str();
}

bool MusicCaptureLogger::Configure(const CaptureMetadata& metadata)
{
  const State state = state_.load(std::memory_order_acquire);
  if (state == State::Armed || state == State::Recording ||
      state == State::Complete || state == State::Saving)
    return false;

  std::lock_guard<std::mutex> lock(metadataMutex_);
  configuredMetadata_ = metadata;
  configured_.store(true, std::memory_order_release);
  return true;
}

void MusicCaptureLogger::SetUnavailable()
{
  configured_.store(false, std::memory_order_release);
  Cancel();
}

bool MusicCaptureLogger::Arm(int seconds)
{
  if (seconds < 1 || seconds > 60)
  {
    {
      std::lock_guard<std::mutex> lock(statusMutex_);
      lastError_ = "duration_must_be_1_to_60_seconds";
    }
    state_.store(State::Error, std::memory_order_release);
    return false;
  }

  if (!configured_.load(std::memory_order_acquire))
  {
    {
      std::lock_guard<std::mutex> lock(statusMutex_);
      lastError_ = "music_pipeline_unavailable";
    }
    state_.store(State::Error, std::memory_order_release);
    return false;
  }

  const State current = state_.load(std::memory_order_acquire);
  if (current == State::Armed || current == State::Recording ||
      current == State::Complete || current == State::Saving)
  {
    std::lock_guard<std::mutex> lock(statusMutex_);
    lastError_ = "capture_busy";
    return false;
  }

  while (pushInFlight_.load(std::memory_order_acquire) != 0)
    std::this_thread::yield();

  {
    std::lock_guard<std::mutex> lock(metadataMutex_);
    activeMetadata_ = configuredMetadata_;
  }

  if (activeMetadata_.sampleRate != 48000)
  {
    {
      std::lock_guard<std::mutex> lock(statusMutex_);
      lastError_ = "capture_requires_48000_hz";
    }
    state_.store(State::Error, std::memory_order_release);
    return false;
  }

  requestedSeconds_ = seconds;
  targetFrames_ =
      static_cast<uint64_t>(activeMetadata_.sampleRate) * static_cast<uint64_t>(seconds);
  const uint64_t maxPackets = (targetFrames_ + 1535) / 1536 + 1;

  try
  {
    inputStereo_.assign(static_cast<size_t>(targetFrames_) * 2, 0.0f);
    output51_.assign(static_cast<size_t>(targetFrames_) * 6, 0.0f);
    telemetryRows_.assign(static_cast<size_t>(maxPackets), TelemetryRow{});
  }
  catch (...)
  {
    {
      std::lock_guard<std::mutex> lock(statusMutex_);
      lastError_ = "capture_allocation_failed";
    }
    state_.store(State::Error, std::memory_order_release);
    return false;
  }

  capturedFrames_.store(0, std::memory_order_relaxed);
  telemetryCount_.store(0, std::memory_order_relaxed);
  captureStartUtc_ = UtcStamp(false);

  std::string shortSha = SafeToken(activeMetadata_.buildSha);
  if (shortSha.size() > 8) shortSha.resize(8);

  const std::filesystem::path root =
      activeMetadata_.captureRoot.empty()
          ? std::filesystem::path("captures")
          : std::filesystem::path(activeMetadata_.captureRoot);
  const std::string stem = UtcStamp(true) + "_" + shortSha;

  std::filesystem::path captureDir;
  std::error_code ec;
  for (unsigned suffix = 0; suffix < 100; ++suffix)
  {
    captureDir = root / (stem + (suffix == 0 ? "" : "_" + std::to_string(suffix + 1)));
    if (!std::filesystem::exists(captureDir, ec))
      break;
    ec.clear();
  }
  std::filesystem::create_directories(captureDir, ec);
  if (ec)
  {
    {
      std::lock_guard<std::mutex> lock(statusMutex_);
      lastError_ = "capture_directory_create_failed";
    }
    state_.store(State::Error, std::memory_order_release);
    return false;
  }

  activePath_ = captureDir.string();
  {
    std::lock_guard<std::mutex> lock(statusMutex_);
    lastError_.clear();
  }
  state_.store(State::Armed, std::memory_order_release);
  return true;
}

bool MusicCaptureLogger::Cancel()
{
  State state = state_.load(std::memory_order_acquire);
  while (state == State::Armed || state == State::Recording || state == State::Complete)
  {
    if (state_.compare_exchange_weak(
            state, State::Cancelled, std::memory_order_acq_rel, std::memory_order_acquire))
      return true;
  }
  return state == State::Cancelled;
}

bool MusicCaptureLogger::WantsPacket() const
{
  const State state = state_.load(std::memory_order_relaxed);
  return state == State::Armed || state == State::Recording;
}

void MusicCaptureLogger::PushPacket(const float* stereo,
                                    const float* out51,
                                    size_t frames,
                                    const MusicTelemetrySnapshot& telemetry)
{
  State state = state_.load(std::memory_order_relaxed);
  if (state == State::Armed)
  {
    State expected = State::Armed;
    state_.compare_exchange_strong(
        expected, State::Recording, std::memory_order_acq_rel, std::memory_order_relaxed);
    state = state_.load(std::memory_order_relaxed);
  }
  if (state != State::Recording || !stereo || !out51 || frames == 0)
    return;

  pushInFlight_.fetch_add(1, std::memory_order_acq_rel);
  if (state_.load(std::memory_order_acquire) != State::Recording)
  {
    pushInFlight_.fetch_sub(1, std::memory_order_acq_rel);
    return;
  }

  const uint64_t start = capturedFrames_.load(std::memory_order_relaxed);
  if (start >= targetFrames_)
  {
    State expected = State::Recording;
    state_.compare_exchange_strong(expected, State::Complete, std::memory_order_release);
    pushInFlight_.fetch_sub(1, std::memory_order_acq_rel);
    return;
  }

  const size_t copyFrames = static_cast<size_t>(
      std::min<uint64_t>(static_cast<uint64_t>(frames), targetFrames_ - start));

  std::memcpy(inputStereo_.data() + static_cast<size_t>(start) * 2,
              stereo,
              copyFrames * 2 * sizeof(float));
  std::memcpy(output51_.data() + static_cast<size_t>(start) * 6,
              out51,
              copyFrames * 6 * sizeof(float));

  const uint64_t rowIndex = telemetryCount_.load(std::memory_order_relaxed);
  if (rowIndex < telemetryRows_.size())
  {
    TelemetryRow& row = telemetryRows_[static_cast<size_t>(rowIndex)];
    row.captureFrameStart = start;
    row.telemetry = telemetry;
    telemetryCount_.store(rowIndex + 1, std::memory_order_relaxed);
  }

  const uint64_t end = start + copyFrames;
  capturedFrames_.store(end, std::memory_order_release);
  if (end >= targetFrames_)
  {
    State expected = State::Recording;
    state_.compare_exchange_strong(
        expected, State::Complete, std::memory_order_release, std::memory_order_relaxed);
  }

  pushInFlight_.fetch_sub(1, std::memory_order_acq_rel);
}

MusicCaptureLogger::Status MusicCaptureLogger::GetStatus() const
{
  Status out;
  out.state = state_.load(std::memory_order_acquire);
  out.frames = capturedFrames_.load(std::memory_order_acquire);
  out.targetFrames = targetFrames_;
  out.requestedSeconds = requestedSeconds_;

  std::lock_guard<std::mutex> lock(statusMutex_);
  out.path = out.state == State::Saved ? latestPath_ : activePath_;
  out.error = lastError_;
  return out;
}

std::string MusicCaptureLogger::LatestPath() const
{
  std::lock_guard<std::mutex> lock(statusMutex_);
  return latestPath_;
}

void MusicCaptureLogger::WriterProc()
{
  while (!stopWriter_.load(std::memory_order_acquire))
  {
    State expected = State::Complete;
    if (state_.compare_exchange_strong(
            expected, State::Saving, std::memory_order_acq_rel, std::memory_order_acquire))
    {
      while (pushInFlight_.load(std::memory_order_acquire) != 0)
        std::this_thread::yield();

      std::string error;
      if (WriteBundle(error))
      {
        {
          std::lock_guard<std::mutex> lock(statusMutex_);
          latestPath_ = activePath_;
          lastError_.clear();
        }
        state_.store(State::Saved, std::memory_order_release);
      }
      else
      {
        {
          std::lock_guard<std::mutex> lock(statusMutex_);
          lastError_ = error.empty() ? "capture_write_failed" : error;
        }
        state_.store(State::Error, std::memory_order_release);
      }
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
}

bool MusicCaptureLogger::WriteBundle(std::string& error)
{
  const uint64_t frames = capturedFrames_.load(std::memory_order_acquire);
  const uint64_t rows = telemetryCount_.load(std::memory_order_acquire);
  const std::filesystem::path dir(activePath_);

  if (!WriteFloatWav(
          dir / "input_stereo.wav",
          inputStereo_.data(),
          frames,
          2,
          static_cast<uint32_t>(activeMetadata_.sampleRate),
          false))
  {
    error = "input_wav_write_failed";
    return false;
  }

  if (!WriteFloatWav(
          dir / "output_5p1.wav",
          output51_.data(),
          frames,
          6,
          static_cast<uint32_t>(activeMetadata_.sampleRate),
          true))
  {
    error = "output_wav_write_failed";
    return false;
  }

  {
    std::ofstream csv(dir / "telemetry.csv", std::ios::trunc);
    if (!csv)
    {
      error = "telemetry_csv_open_failed";
      return false;
    }

    csv << "sequence,capture_frame_start,engine_frame_counter,ambience,center_confidence,"
           "spatial_bin_fraction,transient_confidence,rear_open,front_lock_confidence,"
           "rear_budget_scale,speaker_rms_fl,speaker_rms_fr,speaker_rms_c,speaker_rms_lfe,"
           "speaker_rms_sl,speaker_rms_sr,ownership_low,ownership_body,ownership_presence,"
           "ownership_air,center_low,center_body,center_presence,center_air\n";

    for (uint64_t i = 0; i < rows; ++i)
    {
      const TelemetryRow& row = telemetryRows_[static_cast<size_t>(i)];
      const auto& t = row.telemetry;
      csv << t.sequence << ','
          << row.captureFrameStart << ','
          << t.engineFrameCounter << ','
          << CsvFloat(t.ambience) << ','
          << CsvFloat(t.centerConfidence) << ','
          << CsvFloat(t.spatialBinFraction) << ','
          << CsvFloat(t.transientConfidence) << ','
          << CsvFloat(t.rearOpen) << ','
          << CsvFloat(t.frontLockConfidence) << ','
          << CsvFloat(t.rearBudgetScale);
      for (float value : t.speakerRms) csv << ',' << CsvFloat(value);
      for (float value : t.ownership) csv << ',' << CsvFloat(value);
      for (float value : t.bandCenter) csv << ',' << CsvFloat(value);
      csv << '\n';
    }

    if (!csv)
    {
      error = "telemetry_csv_write_failed";
      return false;
    }
  }

  {
    std::ofstream manifest(dir / "manifest.json", std::ios::trunc);
    if (!manifest)
    {
      error = "manifest_open_failed";
      return false;
    }

    const double latencyMs =
        1000.0 * static_cast<double>(activeMetadata_.processingLatencySamples) /
        static_cast<double>(activeMetadata_.sampleRate);

    manifest
        << "{\n"
        << "  \"format_version\": 1,\n"
        << "  \"build_sha\": \"" << JsonEscape(activeMetadata_.buildSha) << "\",\n"
        << "  \"branch\": \"" << JsonEscape(activeMetadata_.branch) << "\",\n"
        << "  \"capture_start_utc\": \"" << JsonEscape(captureStartUtc_) << "\",\n"
        << "  \"sample_rate\": " << activeMetadata_.sampleRate << ",\n"
        << "  \"input_channels\": [\"L\", \"R\"],\n"
        << "  \"output_channels\": [\"FL\", \"FR\", \"C\", \"LFE\", \"SL\", \"SR\"],\n"
        << "  \"captured_frames\": " << frames << ",\n"
        << "  \"requested_seconds\": " << requestedSeconds_ << ",\n"
        << "  \"processing_latency_samples\": "
        << activeMetadata_.processingLatencySamples << ",\n"
        << "  \"processing_latency_ms\": "
        << std::fixed << std::setprecision(4) << latencyMs << ",\n"
        << "  \"stereo_processing\": \""
        << JsonEscape(activeMetadata_.stereoProcessing) << "\",\n"
        << "  \"layout\": \"" << JsonEscape(activeMetadata_.layout) << "\",\n"
        << "  \"bitrate\": " << activeMetadata_.bitRate << ",\n"
        << "  \"source_device\": \"" << JsonEscape(activeMetadata_.sourceDevice) << "\",\n"
        << "  \"output_device\": \"" << JsonEscape(activeMetadata_.outputDevice) << "\",\n"
        << "  \"speaker_distances_inches\": [";

    for (size_t i = 0; i < activeMetadata_.speakerDistancesInches.size(); ++i)
    {
      if (i) manifest << ", ";
      manifest << activeMetadata_.speakerDistancesInches[i];
    }

    manifest << "],\n  \"speaker_trims\": [";
    for (size_t i = 0; i < activeMetadata_.speakerTrims.size(); ++i)
    {
      if (i) manifest << ", ";
      manifest << activeMetadata_.speakerTrims[i];
    }

    manifest << "],\n  \"config_snapshot\": {\n";
    size_t index = 0;
    for (const auto& kv : activeMetadata_.configSnapshot)
    {
      manifest
          << "    \"" << JsonEscape(kv.first) << "\": \""
          << JsonEscape(kv.second) << "\"";
      if (++index != activeMetadata_.configSnapshot.size()) manifest << ',';
      manifest << '\n';
    }
    manifest << "  }\n}\n";

    if (!manifest)
    {
      error = "manifest_write_failed";
      return false;
    }
  }

  {
    std::ofstream readme(dir / "README.txt", std::ios::trunc);
    if (!readme)
    {
      error = "readme_open_failed";
      return false;
    }

    readme
        << "OHL Music synchronized debug capture\n"
        << "Build SHA: " << activeMetadata_.buildSha << "\n"
        << "input_stereo.wav: exact 48 kHz float32 L/R passed to OHL Music.\n"
        << "output_5p1.wav: exact 48 kHz float32 FL/FR/C/LFE/SL/SR after OHL and before AC3.\n"
        << "telemetry.csv: packet-aligned classifier/router telemetry.\n"
        << "Output intentionally includes OHL processing latency; do not manually time-shift it.\n"
        << "Use manifest.json processing_latency_samples for aligned analysis.\n";
  }

  return true;
}

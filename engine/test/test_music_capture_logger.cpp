#include "doctest.h"

#include "MusicCaptureLogger.h"

#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace {

uint16_t ReadU16(const std::vector<unsigned char>& b, size_t p)
{
  return static_cast<uint16_t>(
      static_cast<uint16_t>(b[p]) |
      (static_cast<uint16_t>(b[p + 1]) << 8));
}

uint32_t ReadU32(const std::vector<unsigned char>& b, size_t p)
{
  return static_cast<uint32_t>(b[p]) |
         (static_cast<uint32_t>(b[p + 1]) << 8) |
         (static_cast<uint32_t>(b[p + 2]) << 16) |
         (static_cast<uint32_t>(b[p + 3]) << 24);
}

std::vector<unsigned char> ReadBytes(const std::filesystem::path& path)
{
  std::ifstream f(path, std::ios::binary);
  return std::vector<unsigned char>(
      std::istreambuf_iterator<char>(f),
      std::istreambuf_iterator<char>());
}

std::string ReadText(const std::filesystem::path& path)
{
  std::ifstream f(path);
  std::ostringstream out;
  out << f.rdbuf();
  return out.str();
}

bool WaitSaved(MusicCaptureLogger& logger)
{
  for (int i = 0; i < 300; ++i)
  {
    const auto status = logger.GetStatus();
    if (status.state == MusicCaptureLogger::State::Saved) return true;
    if (status.state == MusicCaptureLogger::State::Error) return false;
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  return false;
}

CaptureMetadata TestMetadata(const std::filesystem::path& root)
{
  CaptureMetadata metadata;
  metadata.buildSha = "0123456789abcdef";
  metadata.branch = "feature/auto-layout";
  metadata.captureRoot = root.string();
  metadata.sourceDevice = "test input";
  metadata.outputDevice = "test output";
  metadata.layout = "auto";
  metadata.stereoProcessing = "music";
  metadata.sampleRate = 48000;
  metadata.bitRate = 640000;
  metadata.processingLatencySamples = 512;
  metadata.speakerDistancesInches = {{33, 33, 30, 33, 27, 33}};
  metadata.speakerTrims = {{1, 1, 1, 1, 0.95, 1.05}};
  metadata.configSnapshot["music_front_lock"] = "0.88";
  metadata.configSnapshot["music_per_bin_routing"] = "0.55";
  return metadata;
}

} // namespace

TEST_CASE("MusicCaptureLogger writes exact synchronized bundle")
{
  const auto root =
      std::filesystem::temp_directory_path() /
      ("ohl-capture-test-" +
       std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::remove_all(root);

  {
    MusicCaptureLogger logger;
    CHECK(logger.Configure(TestMetadata(root)));
    CHECK(logger.Arm(1));

    constexpr size_t kFrames = 1536;
    std::vector<float> stereo(kFrames * 2);
    std::vector<float> out51(kFrames * 6);

    for (size_t i = 0; i < kFrames; ++i)
    {
      stereo[2 * i] = 0.125f + static_cast<float>(i) * 0.000001f;
      stereo[2 * i + 1] = -0.25f - static_cast<float>(i) * 0.000001f;
      for (size_t ch = 0; ch < 6; ++ch)
      {
        out51[6 * i + ch] =
            static_cast<float>(ch + 1) * 0.01f +
            static_cast<float>(i) * 0.0000001f;
      }
    }

    const std::vector<float> stereoBefore = stereo;
    const std::vector<float> outBefore = out51;

    MusicTelemetrySnapshot telemetry;
    uint64_t sequence = 1;
    uint64_t engineFrame = 0;

    while (logger.GetStatus().frames < 48000)
    {
      telemetry.sequence = sequence++;
      telemetry.engineFrameCounter = engineFrame;
      telemetry.ambience = 0.4f;
      telemetry.centerConfidence = 0.5f;
      telemetry.spatialBinFraction = 0.3f;
      telemetry.transientConfidence = 0.2f;
      telemetry.rearOpen = 0.6f;
      telemetry.frontLockConfidence = 0.7f;
      telemetry.rearBudgetScale = 0.8f;
      telemetry.speakerRms = {{0.1f, 0.2f, 0.3f, 0.0f, 0.4f, 0.5f}};
      telemetry.ownership = {{0.1f, 0.2f, 0.3f, 0.4f}};
      telemetry.bandCenter = {{0.9f, 0.8f, 0.7f, 0.6f}};

      logger.PushPacket(
          stereo.data(), out51.data(), kFrames, telemetry);
      engineFrame += kFrames;
    }

    CHECK(stereo == stereoBefore);
    CHECK(out51 == outBefore);
    CHECK(WaitSaved(logger));

    const auto status = logger.GetStatus();
    CHECK(status.state == MusicCaptureLogger::State::Saved);
    CHECK(status.frames == 48000);
    CHECK(status.targetFrames == 48000);

    const std::filesystem::path dir(status.path);

    const auto input = ReadBytes(dir / "input_stereo.wav");
    REQUIRE(input.size() == 44 + 48000u * 2u * sizeof(float));
    CHECK(ReadU16(input, 20) == 3);
    CHECK(ReadU16(input, 22) == 2);
    CHECK(ReadU32(input, 24) == 48000);
    CHECK(ReadU32(input, 40) == 48000u * 2u * sizeof(float));

    float firstL = 0.0f;
    float firstR = 0.0f;
    std::memcpy(&firstL, input.data() + 44, sizeof(float));
    std::memcpy(&firstR, input.data() + 48, sizeof(float));
    CHECK(firstL == doctest::Approx(stereoBefore[0]));
    CHECK(firstR == doctest::Approx(stereoBefore[1]));

    const auto output = ReadBytes(dir / "output_5p1.wav");
    REQUIRE(output.size() == 68 + 48000u * 6u * sizeof(float));
    CHECK(ReadU16(output, 20) == 0xFFFE);
    CHECK(ReadU16(output, 22) == 6);
    CHECK(ReadU32(output, 24) == 48000);
    CHECK(ReadU32(output, 40) == 0x0000060F);
    CHECK(ReadU32(output, 64) == 48000u * 6u * sizeof(float));

    float firstFl = 0.0f;
    float firstSr = 0.0f;
    std::memcpy(&firstFl, output.data() + 68, sizeof(float));
    std::memcpy(
        &firstSr,
        output.data() + 68 + 5 * sizeof(float),
        sizeof(float));
    CHECK(firstFl == doctest::Approx(outBefore[0]));
    CHECK(firstSr == doctest::Approx(outBefore[5]));

    const std::string csv = ReadText(dir / "telemetry.csv");
    CHECK(csv.find(
              "sequence,capture_frame_start,engine_frame_counter") == 0);
    CHECK(csv.find("\n1,0,0,") != std::string::npos);
    CHECK(csv.find("\n2,1536,1536,") != std::string::npos);

    const std::string manifest = ReadText(dir / "manifest.json");
    CHECK(manifest.find(
              "\"build_sha\": \"0123456789abcdef\"") != std::string::npos);
    CHECK(manifest.find(
              "\"captured_frames\": 48000") != std::string::npos);
    CHECK(manifest.find(
              "\"processing_latency_samples\": 512") != std::string::npos);
    CHECK(manifest.find(
              "\"music_front_lock\": \"0.88\"") != std::string::npos);
    CHECK(std::filesystem::exists(dir / "README.txt"));
  }

  std::filesystem::remove_all(root);
}

TEST_CASE("MusicCaptureLogger cancel and idle paths are non-destructive")
{
  const auto root =
      std::filesystem::temp_directory_path() /
      "ohl-capture-cancel-test";
  std::filesystem::remove_all(root);

  MusicCaptureLogger logger;
  CHECK(logger.Configure(TestMetadata(root)));

  std::vector<float> stereo(1536 * 2, 0.25f);
  std::vector<float> out51(1536 * 6, 0.5f);
  const auto stereoBefore = stereo;
  const auto outBefore = out51;
  MusicTelemetrySnapshot telemetry;

  logger.PushPacket(
      stereo.data(), out51.data(), 1536, telemetry);
  CHECK(logger.GetStatus().state == MusicCaptureLogger::State::Idle);
  CHECK(logger.GetStatus().frames == 0);

  CHECK(logger.Arm(1));
  CHECK(logger.Cancel());
  CHECK(logger.GetStatus().state == MusicCaptureLogger::State::Cancelled);
  logger.PushPacket(
      stereo.data(), out51.data(), 1536, telemetry);
  CHECK(logger.GetStatus().frames == 0);
  CHECK(stereo == stereoBefore);
  CHECK(out51 == outBefore);

  std::filesystem::remove_all(root);
}

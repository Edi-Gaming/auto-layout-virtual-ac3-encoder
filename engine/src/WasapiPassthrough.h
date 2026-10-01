// WasapiPassthrough.h
//
// The output / master-clock side of the engine. Opens the optical endpoint in EXCLUSIVE,
// event-driven IEC 61937 (Dolby Digital) mode and, on each render event, pulls PCM from the
// shared RingBuffer, encodes it to an AC3 / IEC 61937 burst and writes it to the device.
//
// In auto-layout mode two AC3 encoders stay hot: one 2.0 and one 5.1. The PCM packet is
// inspected before encoding; real activity outside FL/FR switches to 5.1 immediately, while
// sustained silence on all non-front channels switches back to 2.0. This lets an AVR see a
// genuine stereo Dolby Digital stream for stereo apps and regain its own PLII/A.F.D. modes.
//
// Clock drift between capture and output is absorbed by the ring buffer and corrected
// SoundPusher-style: every ~64 cycles, excess buffered frames are trimmed to bound latency;
// underruns emit AC3 silence so the receiver stays locked.
#pragma once

#include "ComUtil.h"
#include "RingBuffer.h"
#include "SpdifEncoder.h"
#include "OhlMusicUpmixer.h"
#include "WasapiCapture.h" // CaptureFormat

#include <audioclient.h>
#include <mmdeviceapi.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <thread>
#include <vector>

class WasapiPassthrough
{
public:
  struct Params
  {
    int64_t  bitRate = 640000;
    uint32_t safeFrames = 1536;    // target excess frames kept buffered (latency vs. safety)
    bool     upmixSurround = false; // fixed-5.1 stereo upmix via FFmpeg's `surround` filter

    // Auto AC3 payload layout. When enabled, endpoint channel count no longer decides whether
    // the AVR receives 2.0 or 5.1; actual PCM activity does.
    bool     autoLayout = true;
    double   autoThresholdDb = -60.0; // peak threshold on C/LFE/surround channels
    uint32_t autoHoldMs = 2000;       // quiet time before native 5.1 -> stereo policy

    // Optional OHL Music stereo policy. When false, auto-layout's stereo state remains genuine
    // AC3 2.0 for receiver-side PLII/A.F.D. When true, stereo is spatialized into discrete 5.1
    // with phantom center and a silent LFE; native multichannel input still bypasses it.
    bool musicStereo = false;
    double musicSurroundGain = 0.70;
    double musicWidthFloor = 0.16;
    double musicAmbienceLowWeight = 0.08;
    double musicAmbienceMidWeight = 0.46;
    double musicAmbienceHighWeight = 0.46;
    double musicAmbienceAttackMs = 100.0;
    double musicAmbienceReleaseMs = 520.0;
    double musicDiffuseThreshold = 0.10;
    double musicSpectralIntelligence = 0.90;
    double musicSpatialBinThreshold = 0.30;
    double musicFrontLock = 0.88;
    double musicRearBudget = 0.22;
    double musicDirectReject = 0.78;
    double musicDirectThreshold = 1.45;
    double musicDirectRecoveryMs = 18.0;
    double musicCenterTrebleGain = 0.18;
    double musicCenterTrebleHz = 2400.0;
    double musicCenterLowpassHz = 16000.0;
    double musicRearHighpassHz = 160.0;
    double musicRearLowpassHz = 18000.0;
    double musicRearLeftTrim = 1.0;
    double musicRearRightTrim = 1.0;
    std::array<double, 6> musicDistanceInches{{33.0, 33.0, 30.0, 33.0, 27.0, 33.0}};
  };

  WasapiPassthrough() = default;
  ~WasapiPassthrough();

  // Non-intrusive capability check: does `dev` accept AC3 / IEC 61937 in exclusive mode at
  // `rate`? Uses IsFormatSupported only (does not seize the device).
  static bool ProbeAc3(IMMDevice* dev, int rate);

  // dev    : optical output endpoint (must support AC3 passthrough in exclusive mode)
  // ring   : shared input ring (filled by WasapiCapture), holding capFmt frames
  // capFmt : capture format — defines the encoder input channels/layout/sample fmt/rate
  bool Init(IMMDevice* dev, RingBuffer* ring, const CaptureFormat& capFmt, const Params& p);
  bool Start();
  void Stop();

private:
  bool InitExclusive(int rate);
  void EncodeIntoBuffer(BYTE* out); // fills one full WASAPI buffer with bursts (+ stuffing)
  void ThreadProc();

  enum class AutoPayload
  {
    ReceiverStereo,
    Native51,
    Music51,
  };

  // Auto-layout helpers.
  void BuildActivityChannelList();
  bool PacketHasNonFrontActivity(const uint8_t* in, double& peak) const;
  AutoPayload SelectAutoPayload(const uint8_t* in, bool haveRealInput);
  void ExtractFrontStereoFloat(const uint8_t* in, float* stereo) const;

  ComPtr<IMMDevice>          dev_;
  ComPtr<IAudioClient>       client_;
  ComPtr<IAudioRenderClient> render_;

  RingBuffer*    ring_ = nullptr;
  CaptureFormat  capFmt_;
  size_t         capBytesPerFrame_ = 0;
  Params         params_;

  SpdifEncoder   enc51_;
  SpdifEncoder   encStereo_;
  SpdifEncoder   encMusic51_;
  OhlMusicUpmixer musicUpmixer_;
  int            framesPerPacket_ = 1536;
  static constexpr int kBurstBytes = SpdifEncoder::kMaxBytesPerPacket; // 6144
  static constexpr int kCarrierBytesPerFrame = 4; // 2ch * 16-bit IEC60958

  // Auto-layout state. Windows WAVEFORMATEXTENSIBLE interleaved channel order follows the
  // ascending set bits of dwChannelMask; when no mask is supplied we conservatively treat
  // channels 2..N-1 as non-front channels.
  std::vector<unsigned> activityChannels_;
  bool     activeIsSurround_ = false;
  uint32_t quietPackets_ = 0;
  uint32_t holdPackets_ = 1;
  double   thresholdLinear_ = 0.001; // -60 dBFS

  UINT32 bufferFrames_ = 0; // exclusive buffer size, in carrier frames
  int    burstsPerCycle_ = 0;

  HANDLE dataEvent_ = nullptr;
  HANDLE stopEvent_ = nullptr;
  std::thread thread_;
  std::atomic_bool running_{false};

  // drift tracking (consumer thread only)
  uint32_t cycle_ = 0;
  uint32_t minAvail_ = 0xFFFFFFFFu;

  std::vector<uint8_t> staging_; // one packet of capture frames
  std::vector<uint8_t> silence_; // same, zeroed
  std::vector<uint8_t> burst_;   // one IEC 61937 burst
  std::vector<float> musicStereo_; // one packet of extracted FL/FR float PCM
  std::vector<float> music51_;     // one packet of OHL Music 5.1 float PCM
};

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
#include "WasapiCapture.h" // CaptureFormat

#include <audioclient.h>
#include <mmdeviceapi.h>

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
    uint32_t autoHoldMs = 2000;       // quiet time before 5.1 -> 2.0
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

  // Auto-layout helpers.
  void BuildActivityChannelList();
  bool PacketHasNonFrontActivity(const uint8_t* in, double& peak) const;
  SpdifEncoder& SelectEncoder(const uint8_t* in, bool haveRealInput);

  ComPtr<IMMDevice>          dev_;
  ComPtr<IAudioClient>       client_;
  ComPtr<IAudioRenderClient> render_;

  RingBuffer*    ring_ = nullptr;
  CaptureFormat  capFmt_;
  size_t         capBytesPerFrame_ = 0;
  Params         params_;

  SpdifEncoder   enc51_;
  SpdifEncoder   encStereo_;
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
};

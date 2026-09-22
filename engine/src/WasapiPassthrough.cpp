// WasapiPassthrough.cpp — see WasapiPassthrough.h.
#include "WasapiPassthrough.h"

#include <mmreg.h>
#include <ks.h>
#include <ksmedia.h>
#include <avrt.h>

#include <algorithm>
#include <cmath>
#include <cstring>

extern "C" {
#include <libavutil/channel_layout.h>
#include <libavutil/samplefmt.h>
}

namespace {

AVSampleFormat MapSampleFmt(const CaptureFormat& f)
{
  if (f.isFloat && f.bits == 32) return AV_SAMPLE_FMT_FLT;
  if (!f.isFloat && f.bits == 16) return AV_SAMPLE_FMT_S16;
  if (!f.isFloat && f.bits == 32) return AV_SAMPLE_FMT_S32;
  return AV_SAMPLE_FMT_NONE;
}

// Build the AC3-over-S/PDIF carrier format. The endpoint is opened advertising the maximum
// encoded layout (5.1) so drivers that validate IEC61937 metadata continue to accept it.
// Auto-layout changes the AC3 payload's own acmod between 2.0 and 5.1; the IEC60958 carrier
// itself is always two-channel, 16-bit.
void FillAc3Format(WAVEFORMATEXTENSIBLE_IEC61937& w, int rate, bool extended)
{
  ZeroMemory(&w, sizeof w);
  WAVEFORMATEXTENSIBLE& x = w.FormatExt;
  x.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
  x.Format.nChannels = 2;                       // IEC 60958 carrier is 2-channel
  x.Format.nSamplesPerSec = rate;
  x.Format.wBitsPerSample = 16;
  x.Format.nBlockAlign = 4;
  x.Format.nAvgBytesPerSec = rate * 4;
  x.Samples.wValidBitsPerSample = 16;
  x.dwChannelMask = KSAUDIO_SPEAKER_5POINT1;    // maximum encoded capability hint
  x.SubFormat = KSDATAFORMAT_SUBTYPE_IEC61937_DOLBY_DIGITAL;
  if (extended)
  {
    x.Format.cbSize = sizeof(WAVEFORMATEXTENSIBLE_IEC61937) - sizeof(WAVEFORMATEX);
    w.dwEncodedSamplesPerSec = rate;
    w.dwEncodedChannelCount = 6;                // capability hint; payload may be AC3 2.0
    w.dwAverageBytesPerSec = 0;
  }
  else
  {
    x.Format.cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
  }
}

double PeakDb(double peak)
{
  if (peak <= 1.0e-12)
    return -240.0;
  return 20.0 * std::log10(peak);
}

} // namespace

WasapiPassthrough::~WasapiPassthrough()
{
  Stop();
  if (dataEvent_) CloseHandle(dataEvent_);
  if (stopEvent_) CloseHandle(stopEvent_);
}

bool WasapiPassthrough::ProbeAc3(IMMDevice* dev, int rate)
{
  ComPtr<IAudioClient> c;
  if (FAILED(dev->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, &c)))
    return false;
  WAVEFORMATEXTENSIBLE_IEC61937 w;
  FillAc3Format(w, rate, true);
  if (c->IsFormatSupported(AUDCLNT_SHAREMODE_EXCLUSIVE, &w.FormatExt.Format, nullptr) == S_OK)
    return true;
  FillAc3Format(w, rate, false);
  return c->IsFormatSupported(AUDCLNT_SHAREMODE_EXCLUSIVE, &w.FormatExt.Format, nullptr) == S_OK;
}

bool WasapiPassthrough::Init(IMMDevice* dev, RingBuffer* ring, const CaptureFormat& capFmt,
                             const Params& p)
{
  dev_ = dev;
  ring_ = ring;
  capFmt_ = capFmt;
  params_ = p;
  capBytesPerFrame_ = capFmt.bytesPerFrame();

  const int rate = static_cast<int>(capFmt.sampleRate);
  if (rate != 48000 && rate != 44100 && rate != 32000)
  {
    std::fprintf(stderr, "[WasapiPassthrough] capture rate %d unsupported for AC3 "
                         "(need 48000/44100/32000). Set the virtual device to 48 kHz.\n", rate);
    return false;
  }

  AVSampleFormat inFmt = MapSampleFmt(capFmt);
  if (inFmt == AV_SAMPLE_FMT_NONE)
  {
    std::fprintf(stderr, "[WasapiPassthrough] unsupported capture sample format (%u-bit %s)\n",
                 capFmt.bits, capFmt.isFloat ? "float" : "int");
    return false;
  }

  // Both encoders consume the exact same interleaved capture packet. Only their AC3 payload
  // layouts differ, so changing layout never requires reopening WASAPI or the optical endpoint.
  SpdifEncoder::Params ep;
  ep.sampleRate = rate;
  ep.bitRate = params_.bitRate;
  ep.inSampleFmt = inFmt;
  ep.upmix = params_.upmixSurround ? SpdifEncoder::Upmix::Surround : SpdifEncoder::Upmix::Off;
  if (capFmt.channelMask)
    av_channel_layout_from_mask(&ep.inLayout, capFmt.channelMask);
  else
    av_channel_layout_default(&ep.inLayout, static_cast<int>(capFmt.channels));

  ep.outputLayout = SpdifEncoder::OutputLayout::Surround51;
  bool encOk = enc51_.Init(ep);
  if (!encOk)
  {
    av_channel_layout_uninit(&ep.inLayout);
    return false;
  }
  framesPerPacket_ = enc51_.FramesPerPacket();

  if (params_.autoLayout)
  {
    // In auto mode stereo stays genuinely stereo. Do not run the FFmpeg surround upmixer here;
    // the point is to hand AC3 2.0 to the receiver and let its own A.F.D./PLII logic work.
    ep.outputLayout = SpdifEncoder::OutputLayout::Stereo;
    ep.upmix = SpdifEncoder::Upmix::Off;
    encOk = encStereo_.Init(ep);
    if (!encOk || encStereo_.FramesPerPacket() != framesPerPacket_)
    {
      std::fprintf(stderr, "[WasapiPassthrough] failed to initialize matching AC3 stereo encoder\n");
      av_channel_layout_uninit(&ep.inLayout);
      return false;
    }
  }
  av_channel_layout_uninit(&ep.inLayout);

  const double thresholdDb = std::clamp(params_.autoThresholdDb, -120.0, 0.0);
  thresholdLinear_ = std::pow(10.0, thresholdDb / 20.0);
  const uint64_t holdNumerator =
      static_cast<uint64_t>(params_.autoHoldMs) * static_cast<uint64_t>(rate);
  const uint64_t packetDenominator =
      static_cast<uint64_t>(framesPerPacket_) * static_cast<uint64_t>(1000);
  holdPackets_ = static_cast<uint32_t>(
      std::max<uint64_t>(1, (holdNumerator + packetDenominator - 1) / packetDenominator));

  BuildActivityChannelList();

  if (params_.autoLayout)
  {
    const double actualHoldMs =
        1000.0 * static_cast<double>(holdPackets_ * framesPerPacket_) / static_cast<double>(rate);
    std::printf("[AutoLayout] enabled: threshold %.1f dBFS, 5.1->2.0 hold %.0f ms, "
                "%zu non-front channel(s) monitored\n",
                thresholdDb, actualHoldMs, activityChannels_.size());
  }
  else
  {
    std::printf("[AutoLayout] disabled: fixed AC3 5.1 output\n");
  }

  if (!InitExclusive(rate))
    return false;

  const size_t pktBytes = static_cast<size_t>(framesPerPacket_) * capBytesPerFrame_;
  staging_.resize(pktBytes);
  silence_.assign(pktBytes, 0);
  burst_.resize(kBurstBytes);
  return true;
}

void WasapiPassthrough::BuildActivityChannelList()
{
  activityChannels_.clear();
  if (capFmt_.channels <= 2)
    return;

  std::vector<bool> isFront(capFmt_.channels, false);
  bool foundFl = false;
  bool foundFr = false;

  if (capFmt_.channelMask)
  {
    unsigned channelIndex = 0;
    for (unsigned bit = 0; bit < 32 && channelIndex < capFmt_.channels; ++bit)
    {
      const uint32_t speaker = uint32_t{1} << bit;
      if ((capFmt_.channelMask & speaker) == 0)
        continue;

      if (speaker == SPEAKER_FRONT_LEFT)
      {
        isFront[channelIndex] = true;
        foundFl = true;
      }
      else if (speaker == SPEAKER_FRONT_RIGHT)
      {
        isFront[channelIndex] = true;
        foundFr = true;
      }
      ++channelIndex;
    }
  }

  // A zero/odd speaker mask is uncommon for VB-CABLE, but the conventional interleaved order
  // still begins FL, FR. Falling back here is preferable to declaring stereo audio "surround"
  // merely because the endpoint did not publish a mask.
  if (!foundFl || !foundFr)
  {
    std::fill(isFront.begin(), isFront.end(), false);
    isFront[0] = true;
    if (capFmt_.channels > 1)
      isFront[1] = true;
    std::printf("[AutoLayout] channel mask unavailable/ambiguous; assuming channels 0/1 are FL/FR\n");
  }

  for (unsigned i = 0; i < capFmt_.channels; ++i)
    if (!isFront[i])
      activityChannels_.push_back(i);
}

bool WasapiPassthrough::PacketHasNonFrontActivity(const uint8_t* in, double& peak) const
{
  peak = 0.0;
  if (activityChannels_.empty())
    return false;

  const size_t bytesPerSample = capFmt_.bits / 8;
  if (bytesPerSample == 0)
    return false;

  for (int frame = 0; frame < framesPerPacket_; ++frame)
  {
    const size_t frameBase =
        static_cast<size_t>(frame) * static_cast<size_t>(capFmt_.channels) * bytesPerSample;

    for (unsigned ch : activityChannels_)
    {
      const uint8_t* p = in + frameBase + static_cast<size_t>(ch) * bytesPerSample;
      double value = 0.0;

      if (capFmt_.isFloat && capFmt_.bits == 32)
      {
        float s = 0.0f;
        std::memcpy(&s, p, sizeof s);
        if (std::isfinite(s))
          value = std::fabs(static_cast<double>(s));
      }
      else if (!capFmt_.isFloat && capFmt_.bits == 16)
      {
        int16_t s = 0;
        std::memcpy(&s, p, sizeof s);
        value = std::fabs(static_cast<double>(s) / 32768.0);
      }
      else if (!capFmt_.isFloat && capFmt_.bits == 32)
      {
        int32_t s = 0;
        std::memcpy(&s, p, sizeof s);
        value = std::fabs(static_cast<double>(s) / 2147483648.0);
      }

      if (value > peak)
        peak = value;
    }
  }

  return peak >= thresholdLinear_;
}

SpdifEncoder& WasapiPassthrough::SelectEncoder(const uint8_t* in, bool haveRealInput)
{
  if (!params_.autoLayout)
    return enc51_;

  if (haveRealInput)
  {
    double peak = 0.0;
    const bool surroundActive = PacketHasNonFrontActivity(in, peak);

    if (surroundActive)
    {
      quietPackets_ = 0;
      if (!activeIsSurround_)
      {
        activeIsSurround_ = true;
        std::printf("[AutoLayout] 2.0 -> 5.1 (non-front peak %.1f dBFS)\n", PeakDb(peak));
      }
    }
    else if (activeIsSurround_)
    {
      if (++quietPackets_ >= holdPackets_)
      {
        activeIsSurround_ = false;
        quietPackets_ = 0;
        std::printf("[AutoLayout] 5.1 -> 2.0 (non-front channels quiet for %u ms)\n",
                    params_.autoHoldMs);
      }
    }
  }

  return activeIsSurround_ ? enc51_ : encStereo_;
}

bool WasapiPassthrough::InitExclusive(int rate)
{
  HR_FAIL(dev_->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, &client_),
          "Activate IAudioClient");

  WAVEFORMATEXTENSIBLE_IEC61937 w;
  bool extended = true;
  FillAc3Format(w, rate, true);
  HRESULT hr = client_->IsFormatSupported(AUDCLNT_SHAREMODE_EXCLUSIVE, &w.FormatExt.Format, nullptr);
  if (hr != S_OK)
  {
    FillAc3Format(w, rate, false);
    extended = false;
    hr = client_->IsFormatSupported(AUDCLNT_SHAREMODE_EXCLUSIVE, &w.FormatExt.Format, nullptr);
  }
  if (hr != S_OK)
  {
    std::fprintf(stderr, "[WasapiPassthrough] device does not support AC3 passthrough "
                         "(IsFormatSupported = %s). Enable Dolby Digital / S-PDIF passthrough "
                         "for this output.\n", HrStr(hr).c_str());
    return false;
  }
  std::printf("[WasapiPassthrough] AC3 format accepted (%s WAVEFORMATEXTENSIBLE_IEC61937)\n",
              extended ? "extended" : "plain");

  REFERENCE_TIME defPeriod = 0;
  HR_FAIL(client_->GetDevicePeriod(&defPeriod, nullptr), "GetDevicePeriod");

  // One AC3 burst = 1536 carrier frames. Start from a one-burst buffer and let WASAPI's
  // alignment requirements adjust it.
  REFERENCE_TIME burstHns =
      static_cast<REFERENCE_TIME>(10000000.0 * framesPerPacket_ / rate + 0.5);
  REFERENCE_TIME bufferHns = burstHns > defPeriod ? burstHns : defPeriod;
  bool needNewClient = false;

  do
  {
    if (needNewClient)
      HR_FAIL(dev_->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                             reinterpret_cast<void**>(client_.ReleaseAndGetAddressOf())),
              "re-Activate IAudioClient");

    hr = client_->Initialize(AUDCLNT_SHAREMODE_EXCLUSIVE,
                             AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_NOPERSIST,
                             bufferHns, bufferHns, &w.FormatExt.Format, nullptr);

    if (hr == AUDCLNT_E_BUFFER_SIZE_NOT_ALIGNED)
    {
      UINT32 n = 0;
      HR_FAIL(client_->GetBufferSize(&n), "GetBufferSize (align)");
      bufferHns = static_cast<REFERENCE_TIME>(10000.0 * 1000 / rate * n + 0.5);
      needNewClient = true;
    }
    else if (hr == AUDCLNT_E_BUFFER_SIZE_ERROR || hr == AUDCLNT_E_INVALID_DEVICE_PERIOD ||
             hr == E_OUTOFMEMORY)
    {
      bufferHns -= defPeriod;
      needNewClient = false;
    }
    else
    {
      break;
    }
  } while (bufferHns >= defPeriod);

  if (FAILED(hr))
  {
    std::fprintf(stderr, "[WasapiPassthrough] exclusive Initialize failed: %s\n", HrStr(hr).c_str());
    return false;
  }

  HR_FAIL(client_->GetBufferSize(&bufferFrames_), "GetBufferSize");
  burstsPerCycle_ = static_cast<int>(bufferFrames_ / framesPerPacket_);
  if (burstsPerCycle_ < 1)
  {
    std::fprintf(stderr, "[WasapiPassthrough] buffer (%u frames) smaller than one AC3 burst\n",
                 bufferFrames_);
    return false;
  }
  if (bufferFrames_ % framesPerPacket_ != 0)
    std::fprintf(stderr, "[WasapiPassthrough] note: buffer %u not a multiple of %d; "
                         "remainder will be zero-stuffed\n", bufferFrames_, framesPerPacket_);

  dataEvent_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  stopEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  if (!dataEvent_ || !stopEvent_) return false;
  HR_FAIL(client_->SetEventHandle(dataEvent_), "SetEventHandle");
  HR_FAIL(client_->GetService(__uuidof(IAudioRenderClient), &render_), "GetService(IAudioRenderClient)");

  std::printf("[WasapiPassthrough] exclusive buffer = %u frames (%d burst(s)/cycle, ~%.1f ms)\n",
              bufferFrames_, burstsPerCycle_, 1000.0 * bufferFrames_ / rate);
  return true;
}

void WasapiPassthrough::EncodeIntoBuffer(BYTE* out)
{
  const size_t pktBytes = staging_.size();
  size_t outOff = 0;
  for (int b = 0; b < burstsPerCycle_; ++b)
  {
    const uint8_t* in;
    bool haveRealInput = false;
    if (ring_->BytesAvailable() >= pktBytes)
    {
      ring_->Read(staging_.data(), pktBytes);
      in = staging_.data();
      haveRealInput = true;
    }
    else
    {
      // Underruns must not count as "quiet" for auto-layout; otherwise a capture hiccup could
      // make the AVR switch formats. Preserve the current layout while emitting AC3 silence.
      in = silence_.data();
    }

    SpdifEncoder& enc = SelectEncoder(in, haveRealInput);
    int n = enc.EncodePacket(in, burst_.data(), static_cast<int>(burst_.size()));
    if (n <= 0)
    {
      std::memset(burst_.data(), 0, kBurstBytes); // priming / error: stuff zeros this slot
      n = kBurstBytes;
    }
    std::memcpy(out + outOff, burst_.data(), kBurstBytes);
    outOff += kBurstBytes;
  }

  const size_t totalBytes = static_cast<size_t>(bufferFrames_) * kCarrierBytesPerFrame;
  if (outOff < totalBytes)
    std::memset(out + outOff, 0, totalBytes - outOff); // inter-burst stuffing
}

bool WasapiPassthrough::Start()
{
  if (running_.exchange(true))
    return true;
  ResetEvent(stopEvent_);
  cycle_ = 0;
  minAvail_ = 0xFFFFFFFFu;
  activeIsSurround_ = false;
  quietPackets_ = 0;

  // Pre-fill the first buffer (primes the encoder and avoids an initial underrun) before Start.
  BYTE* out = nullptr;
  if (SUCCEEDED(render_->GetBuffer(bufferFrames_, &out)))
  {
    EncodeIntoBuffer(out);
    render_->ReleaseBuffer(bufferFrames_, 0);
  }

  HR_FAIL(client_->Start(), "client Start");
  thread_ = std::thread(&WasapiPassthrough::ThreadProc, this);
  return true;
}

void WasapiPassthrough::Stop()
{
  if (!running_.exchange(false))
    return;
  if (stopEvent_) SetEvent(stopEvent_);
  if (thread_.joinable()) thread_.join();
  if (client_) client_->Stop();
}

void WasapiPassthrough::ThreadProc()
{
  DWORD taskIndex = 0;
  HANDLE mmcss = AvSetMmThreadCharacteristicsW(L"Pro Audio", &taskIndex);

  const uint32_t required = static_cast<uint32_t>(burstsPerCycle_) * framesPerPacket_;
  const uint32_t desired = required + params_.safeFrames;
  HANDLE waits[2] = { dataEvent_, stopEvent_ };

  while (running_.load(std::memory_order_relaxed))
  {
    DWORD wr = WaitForMultipleObjects(2, waits, FALSE, 2000);
    if (wr == WAIT_OBJECT_0 + 1)
      break;
    if (wr == WAIT_TIMEOUT)
    {
      std::fprintf(stderr, "[WasapiPassthrough] render event timed out\n");
      continue;
    }

    // --- drift control (SoundPusher style) ---
    const uint32_t avail =
        static_cast<uint32_t>(ring_->BytesAvailable() / capBytesPerFrame_);
    if (avail < minAvail_)
      minAvail_ = avail;
    if (cycle_++ % 64 == 0)
    {
      if (minAvail_ != 0xFFFFFFFFu && minAvail_ > desired)
      {
        const uint32_t trim = minAvail_ - desired;
        ring_->Discard(static_cast<size_t>(trim) * capBytesPerFrame_);
      }
      minAvail_ = 0xFFFFFFFFu;
    }

    BYTE* out = nullptr;
    HRESULT hr = render_->GetBuffer(bufferFrames_, &out);
    if (FAILED(hr))
    {
      std::fprintf(stderr, "[WasapiPassthrough] GetBuffer failed: %s\n", HrStr(hr).c_str());
      break;
    }
    EncodeIntoBuffer(out);
    render_->ReleaseBuffer(bufferFrames_, 0);
  }

  if (mmcss) AvRevertMmThreadCharacteristics(mmcss);
}

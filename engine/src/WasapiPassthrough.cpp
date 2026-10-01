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

  // Surround Wizard always gets its own explicit interleaved-float 5.1 encoder. This keeps
  // diagnostics independent of both the native multichannel input path and OHL Music DSP.
  {
    SpdifEncoder::Params tp;
    tp.sampleRate = rate;
    tp.bitRate = params_.bitRate;
    tp.inSampleFmt = AV_SAMPLE_FMT_FLT;
    tp.upmix = SpdifEncoder::Upmix::Off;
    tp.outputLayout = SpdifEncoder::OutputLayout::Surround51;
    AVChannelLayout testLayout = AV_CHANNEL_LAYOUT_5POINT1_BACK;
    av_channel_layout_copy(&tp.inLayout, &testLayout);

    encOk = encTest51_.Init(tp);
    av_channel_layout_uninit(&tp.inLayout);
    if (!encOk || encTest51_.FramesPerPacket() != framesPerPacket_)
    {
      std::fprintf(stderr, "[SurroundWizard] failed to initialize diagnostic AC3 5.1 encoder\n");
      av_channel_layout_uninit(&ep.inLayout);
      return false;
    }
  }

  if (params_.autoLayout)
  {
    if (params_.musicStereo)
    {
      // OHL Music produces its own interleaved 5.1 float block. Keep that encoder completely
      // separate from the native multichannel encoder so the proven 5.1 path remains untouched.
      SpdifEncoder::Params mp;
      mp.sampleRate = rate;
      mp.bitRate = params_.bitRate;
      mp.inSampleFmt = AV_SAMPLE_FMT_FLT;
      mp.upmix = SpdifEncoder::Upmix::Off;
      mp.outputLayout = SpdifEncoder::OutputLayout::Surround51;
      AVChannelLayout musicLayout = AV_CHANNEL_LAYOUT_5POINT1_BACK;
      av_channel_layout_copy(&mp.inLayout, &musicLayout);

      encOk = encMusic51_.Init(mp);
      av_channel_layout_uninit(&mp.inLayout);
      if (!encOk || encMusic51_.FramesPerPacket() != framesPerPacket_)
      {
        std::fprintf(stderr, "[OhlMusic] failed to initialize dedicated AC3 5.1 encoder\n");
        av_channel_layout_uninit(&ep.inLayout);
        return false;
      }

      OhlMusicUpmixer::Params op;
      op.sampleRate = rate;
      op.surroundGain = static_cast<float>(params_.musicSurroundGain);
      op.widthFloor = static_cast<float>(params_.musicWidthFloor);
      op.ambienceLowWeight = static_cast<float>(params_.musicAmbienceLowWeight);
      op.ambienceMidWeight = static_cast<float>(params_.musicAmbienceMidWeight);
      op.ambienceHighWeight = static_cast<float>(params_.musicAmbienceHighWeight);
      op.ambienceAttackMs = static_cast<float>(params_.musicAmbienceAttackMs);
      op.ambienceReleaseMs = static_cast<float>(params_.musicAmbienceReleaseMs);
      op.diffuseThreshold = static_cast<float>(params_.musicDiffuseThreshold);
      op.spectralIntelligence = static_cast<float>(params_.musicSpectralIntelligence);
      op.spatialBinThreshold = static_cast<float>(params_.musicSpatialBinThreshold);
      op.perBinRouting = static_cast<float>(params_.musicPerBinRouting);
      op.spectralAcquireMs = static_cast<float>(params_.musicSpectralAcquireMs);
      op.spectralReleaseMs = static_cast<float>(params_.musicSpectralReleaseMs);
      op.dimension = static_cast<float>(params_.musicDimension);
      op.centerWidth = static_cast<float>(params_.musicCenterWidth);
      for (size_t i = 0; i < op.spectralSteering.size(); ++i)
      {
        op.spectralSteering[i] = static_cast<float>(params_.musicSpectralSteering[i]);
        op.spectralFrontLock[i] = static_cast<float>(params_.musicSpectralFrontLock[i]);
      }
      op.frontLock = static_cast<float>(params_.musicFrontLock);
      op.rearBudget = static_cast<float>(params_.musicRearBudget);
      op.directReject = static_cast<float>(params_.musicDirectReject);
      op.directThreshold = static_cast<float>(params_.musicDirectThreshold);
      op.directRecoveryMs = static_cast<float>(params_.musicDirectRecoveryMs);
      op.centerTrebleGain = static_cast<float>(params_.musicCenterTrebleGain);
      op.centerTrebleHz = static_cast<float>(params_.musicCenterTrebleHz);
      op.centerLowpassHz = static_cast<float>(params_.musicCenterLowpassHz);
      op.rearHighpassHz = static_cast<float>(params_.musicRearHighpassHz);
      op.rearLowpassHz = static_cast<float>(params_.musicRearLowpassHz);
      op.rearLeftTrim = static_cast<float>(params_.musicRearLeftTrim);
      op.rearRightTrim = static_cast<float>(params_.musicRearRightTrim);
      for (size_t i = 0; i < op.distanceInches.size(); ++i)
        op.distanceInches[i] = static_cast<float>(params_.musicDistanceInches[i]);

      if (!musicUpmixer_.Init(op))
      {
        std::fprintf(stderr, "[OhlMusic] invalid spatializer configuration\n");
        av_channel_layout_uninit(&ep.inLayout);
        return false;
      }

      const auto& d = musicUpmixer_.DelaySamples();
      std::printf("[OhlMusic] enabled: adaptive %.2f, side-width %.2f, bands %.2f/%.2f/%.2f, "
                  "ambience %.0f/%.0f ms diffuse-threshold %.2f, spectral %.2f bin-threshold %.2f, "
                  "front-lock %.2f, rear-budget %.2f, direct %.2f @ %.2f (%.0f ms), "
                  "center %.2f %.0f-%.0f Hz, "
                  "rear %.0f-%.0f Hz trims %.2f/%.2f, "
                  "delay samples FL=%d FR=%d C=%d LFE=%d SL=%d SR=%d\n",
                  params_.musicSurroundGain,
                  params_.musicWidthFloor,
                  params_.musicAmbienceLowWeight,
                  params_.musicAmbienceMidWeight,
                  params_.musicAmbienceHighWeight,
                  params_.musicAmbienceAttackMs,
                  params_.musicAmbienceReleaseMs,
                  params_.musicDiffuseThreshold,
                  params_.musicSpectralIntelligence,
                  params_.musicSpatialBinThreshold,
                  params_.musicFrontLock,
                  params_.musicRearBudget,
                  params_.musicDirectReject,
                  params_.musicDirectThreshold,
                  params_.musicDirectRecoveryMs,
                  params_.musicCenterTrebleGain,
                  params_.musicCenterTrebleHz,
                  params_.musicCenterLowpassHz,
                  params_.musicRearHighpassHz,
                  params_.musicRearLowpassHz,
                  params_.musicRearLeftTrim,
                  params_.musicRearRightTrim,
                  d[0], d[1], d[2], d[3], d[4], d[5]);
    }
    else
    {
      // Receiver policy: stereo stays genuinely stereo so the AVR can run PLII/A.F.D.
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
    std::printf("[AutoLayout] enabled: threshold %.1f dBFS, native-5.1 hold %.0f ms, "
                "%zu non-front channel(s) monitored, stereo policy=%s\n",
                thresholdDb, actualHoldMs, activityChannels_.size(),
                params_.musicStereo ? "OHL Music 5.1" : "receiver AC3 2.0");
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
  test51_.resize(static_cast<size_t>(framesPerPacket_) * 6);
  testFadeSamples_ = std::max(1, rate / 100); // 10 ms click-safe route crossfade
  if (params_.autoLayout && params_.musicStereo)
  {
    musicStereo_.resize(static_cast<size_t>(framesPerPacket_) * 2);
    music51_.resize(static_cast<size_t>(framesPerPacket_) * OhlMusicUpmixer::kChannels);
  }
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

void WasapiPassthrough::ExtractFrontStereoFloat(const uint8_t* in, float* stereo) const
{
  if (!in || !stereo || capFmt_.channels == 0)
    return;

  const size_t bytesPerSample = capFmt_.bits / 8;
  const unsigned rightChannel = capFmt_.channels > 1 ? 1u : 0u;

  auto readSample = [&](int frame, unsigned ch) -> float {
    const size_t frameBase =
        static_cast<size_t>(frame) * static_cast<size_t>(capFmt_.channels) * bytesPerSample;
    const uint8_t* p = in + frameBase + static_cast<size_t>(ch) * bytesPerSample;

    if (capFmt_.isFloat && capFmt_.bits == 32)
    {
      float s = 0.0f;
      std::memcpy(&s, p, sizeof s);
      return std::isfinite(s) ? s : 0.0f;
    }
    if (!capFmt_.isFloat && capFmt_.bits == 16)
    {
      int16_t s = 0;
      std::memcpy(&s, p, sizeof s);
      return static_cast<float>(static_cast<double>(s) / 32768.0);
    }
    if (!capFmt_.isFloat && capFmt_.bits == 32)
    {
      int32_t s = 0;
      std::memcpy(&s, p, sizeof s);
      return static_cast<float>(static_cast<double>(s) / 2147483648.0);
    }
    return 0.0f;
  };

  for (int frame = 0; frame < framesPerPacket_; ++frame)
  {
    stereo[2 * static_cast<size_t>(frame)] = readSample(frame, 0);
    stereo[2 * static_cast<size_t>(frame) + 1] = readSample(frame, rightChannel);
  }
}

WasapiPassthrough::AutoPayload
WasapiPassthrough::SelectAutoPayload(const uint8_t* in, bool haveRealInput)
{
  if (!params_.autoLayout)
    return AutoPayload::Native51;

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
        if (params_.musicStereo)
          std::fprintf(stderr,
                       "[AutoLayout] OHL Music -> native 5.1 (non-front peak %.1f dBFS)\n",
                       PeakDb(peak));
        else
          std::fprintf(stderr, "[AutoLayout] 2.0 -> 5.1 (non-front peak %.1f dBFS)\n",
                       PeakDb(peak));
        std::fflush(stderr);
      }
    }
    else if (activeIsSurround_)
    {
      if (++quietPackets_ >= holdPackets_)
      {
        activeIsSurround_ = false;
        quietPackets_ = 0;
        if (params_.musicStereo)
          std::fprintf(stderr,
                       "[AutoLayout] native 5.1 -> OHL Music (non-front channels quiet for %u ms)\n",
                       params_.autoHoldMs);
        else
          std::fprintf(stderr,
                       "[AutoLayout] 5.1 -> 2.0 (non-front channels quiet for %u ms)\n",
                       params_.autoHoldMs);
        std::fflush(stderr);
      }
    }
  }

  if (activeIsSurround_)
    return AutoPayload::Native51;
  return params_.musicStereo ? AutoPayload::Music51 : AutoPayload::ReceiverStereo;
}

bool WasapiPassthrough::RenderSurroundTest(float* out51)
{
  if (!out51 || !params_.surroundTestState)
    return false;

  const uint64_t revision = params_.surroundTestState->Revision();
  if (revision != testLastRevision_)
  {
    testLastRevision_ = revision;
    testFromRoute_ = testToRoute_;
    testToRoute_ = params_.surroundTestState->Route();
    testFrequencyHz_ = static_cast<double>(params_.surroundTestState->FrequencyHz());
    testAmplitude_ = std::pow(10.0, params_.surroundTestState->LevelDb() / 20.0);
    testFadePos_ = 0;
  }

  // After an OFF request has fully faded there is no override; normal program audio resumes.
  if (testFromRoute_ == SurroundTestRoute::Off &&
      testToRoute_ == SurroundTestRoute::Off &&
      testFadePos_ >= testFadeSamples_)
    return false;

  std::fill(out51, out51 + static_cast<size_t>(framesPerPacket_) * 6, 0.0f);

  const double rate = static_cast<double>(capFmt_.sampleRate);
  const double step = 2.0 * 3.14159265358979323846 * testFrequencyHz_ / rate;

  auto routeSample = [](SurroundTestRoute route, float gain, float sample, float* frame)
  {
    const float v = gain * sample;
    switch (route)
    {
      case SurroundTestRoute::FL: frame[0] += v; break;
      case SurroundTestRoute::FR: frame[1] += v; break;
      case SurroundTestRoute::C:  frame[2] += v; break;
      case SurroundTestRoute::LFE: frame[3] += v; break;
      case SurroundTestRoute::SL: frame[4] += v; break;
      case SurroundTestRoute::SR: frame[5] += v; break;
      case SurroundTestRoute::FL_LFE:
        frame[0] += v;
        frame[3] += v;
        break;
      case SurroundTestRoute::Off:
        break;
    }
  };

  for (int i = 0; i < framesPerPacket_; ++i)
  {
    float fromGain = 0.0f;
    float toGain = 1.0f;
    if (testFadePos_ < testFadeSamples_)
    {
      const float t = static_cast<float>(testFadePos_) /
                      static_cast<float>(testFadeSamples_);
      fromGain = 1.0f - t;
      toGain = t;
      ++testFadePos_;
      if (testFadePos_ >= testFadeSamples_)
        testFromRoute_ = testToRoute_;
    }

    const float sample =
        static_cast<float>(std::sin(testPhase_) * testAmplitude_);
    float* frame = out51 + static_cast<size_t>(i) * 6;
    routeSample(testFromRoute_, fromGain, sample, frame);
    routeSample(testToRoute_, toGain, sample, frame);

    testPhase_ += step;
    if (testPhase_ >= 2.0 * 3.14159265358979323846)
      testPhase_ = std::fmod(testPhase_, 2.0 * 3.14159265358979323846);
  }

  return true;
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

    SpdifEncoder* enc = nullptr;
    const uint8_t* encodeIn = in;

    const bool testOverride = RenderSurroundTest(test51_.data());
    if (testOverride)
    {
      enc = &encTest51_;
      encodeIn = reinterpret_cast<const uint8_t*>(test51_.data());
    }
    else
    {
      const AutoPayload payload = SelectAutoPayload(in, haveRealInput);
      switch (payload)
      {
      case AutoPayload::Native51:
        enc = &enc51_;
        break;

      case AutoPayload::ReceiverStereo:
        enc = &encStereo_;
        break;

      case AutoPayload::Music51:
        ExtractFrontStereoFloat(in, musicStereo_.data());
        musicUpmixer_.ProcessStereo(
            musicStereo_.data(), static_cast<size_t>(framesPerPacket_), music51_.data());

        MusicTelemetrySnapshot snapshot;
        snapshot.sequence = ++musicPacketSequence_;
        snapshot.engineFrameCounter = engineFrameCounter_;
        snapshot.ambience = musicUpmixer_.LastSpectralAmbience();
        snapshot.centerConfidence = musicUpmixer_.LastSpectralCenter();
        snapshot.spatialBinFraction = musicUpmixer_.LastSpatialBinFraction();
        snapshot.transientConfidence = musicUpmixer_.LastSpectralTransient();
        snapshot.rearOpen = musicUpmixer_.LastSurroundAmount();
        snapshot.frontLockConfidence = musicUpmixer_.LastFrontLockConfidence();
        snapshot.rearBudgetScale = musicUpmixer_.LastRearBudgetScale();

        const auto& own = musicUpmixer_.LastBandOwnership();
        const auto& ctr = musicUpmixer_.LastBandCenter();
        for (size_t metric = 0; metric < own.size(); ++metric)
        {
          snapshot.ownership[metric] = own[metric];
          snapshot.bandCenter[metric] = ctr[metric];
        }

        std::array<double, 6> channelEnergy{{0, 0, 0, 0, 0, 0}};
        for (int frame = 0; frame < framesPerPacket_; ++frame)
        {
          const size_t base = static_cast<size_t>(frame) * 6;
          for (size_t ch = 0; ch < channelEnergy.size(); ++ch)
          {
            const double sample = static_cast<double>(music51_[base + ch]);
            channelEnergy[ch] += sample * sample;
          }
        }

        const double rmsDenom = std::max(1, framesPerPacket_);
        for (size_t ch = 0; ch < channelEnergy.size(); ++ch)
        {
          snapshot.speakerRms[ch] =
              static_cast<float>(std::sqrt(channelEnergy[ch] / rmsDenom));
        }

        if (params_.musicTelemetry)
        {
          params_.musicTelemetry->ambience.store(snapshot.ambience);
          params_.musicTelemetry->center.store(snapshot.centerConfidence);
          params_.musicTelemetry->spatialBins.store(snapshot.spatialBinFraction);
          params_.musicTelemetry->transient.store(snapshot.transientConfidence);
          params_.musicTelemetry->surroundAmount.store(snapshot.rearOpen);
          params_.musicTelemetry->frontLock.store(snapshot.frontLockConfidence);
          params_.musicTelemetry->rearBudgetScale.store(snapshot.rearBudgetScale);

          for (size_t metric = 0; metric < snapshot.ownership.size(); ++metric)
          {
            params_.musicTelemetry->bandOwnership[metric].store(snapshot.ownership[metric]);
            params_.musicTelemetry->bandCenter[metric].store(snapshot.bandCenter[metric]);
          }
          for (size_t ch = 0; ch < snapshot.speakerRms.size(); ++ch)
            params_.musicTelemetry->speakerRms[ch].store(snapshot.speakerRms[ch]);

          params_.musicTelemetry->sequence.store(
              snapshot.sequence, std::memory_order_relaxed);
        }

        // These are the exact pre-OHL stereo samples and exact post-OHL 5.1 samples.
        // PushPacket performs no allocation or filesystem I/O.
        if (params_.musicCaptureLogger && params_.musicCaptureLogger->WantsPacket())
        {
          params_.musicCaptureLogger->PushPacket(
              musicStereo_.data(),
              music51_.data(),
              static_cast<size_t>(framesPerPacket_),
              snapshot);
        }

        encodeIn = reinterpret_cast<const uint8_t*>(music51_.data());
        enc = &encMusic51_;
        break;
      }
    }

    int n = enc->EncodePacket(encodeIn, burst_.data(), static_cast<int>(burst_.size()));
    if (n <= 0)
    {
      std::memset(burst_.data(), 0, kBurstBytes); // priming / error: stuff zeros this slot
      n = kBurstBytes;
    }
    std::memcpy(out + outOff, burst_.data(), kBurstBytes);
    outOff += kBurstBytes;
    engineFrameCounter_ += static_cast<uint64_t>(framesPerPacket_);
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
  engineFrameCounter_ = 0;
  musicPacketSequence_ = 0;
  testLastRevision_ = 0;
  testFromRoute_ = SurroundTestRoute::Off;
  testToRoute_ = SurroundTestRoute::Off;
  testFadePos_ = testFadeSamples_;
  testPhase_ = 0.0;
  if (params_.autoLayout && params_.musicStereo)
    musicUpmixer_.Reset();

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

#include "dsp/OpticalPath.h"
#include <algorithm>
#include <cmath>

namespace ia2a {
namespace {
lab2c::dsp::MidSideOpticalBridge::DomainSettings bridgeDomain(const DomainSettings& d) noexcept {
    return {{d.thresholdDb, d.ratio, d.kneeDb, d.attackMs, d.fastReleaseMs, d.slowReleaseMs, d.memory},
            d.hpfOn, d.hpfHz};
}
}

void OpticalPath::prepare(double sampleRate, int channels) noexcept {
    channels_ = std::clamp(channels,1,2);
    optical_.prepare(sampleRate);
    for (auto& shelf : air_) shelf.prepare(sampleRate);
    midSide_.prepare(sampleRate);
    modeFade_.prepare(sampleRate);
    inputTrim_.prepare(sampleRate);
    makeupGain_.prepare(sampleRate);
    msGains_.prepare(sampleRate);
    gainSmoothed_ = 1.0f;
    snapPending_ = true;
    const double seconds = std::max(1.0,sampleRate) * 0.020;
    gainStepCoefficient_ = static_cast<float>(std::exp(-1.0 / seconds));
    resetTransitionSamples_ = std::max(1,static_cast<int>(std::lround(seconds)));
}

void OpticalPath::reset() noexcept {
    optical_.resetDynamics(resetTransitionSamples_);
    midSide_.resetDynamics(resetTransitionSamples_);
}

void OpticalPath::setSettings(const Settings& s) noexcept {
    if (validSettings(s)) settings_ = s;
}

void OpticalPath::process(AudioBlockView block, GrMeters& meters) noexcept {
    meters = {};
    const int channels = std::min(channels_,block.channels);
    if (channels < 1 || block.samples < 1 || block.data[0] == nullptr ||
        (channels == 2 && block.data[1] == nullptr)) return;
    const auto& m = settings_.mid;
    optical_.setEnabled(settings_.enabled);
    optical_.setDynamics(m.thresholdDb,m.ratio,m.attackMs,m.fastReleaseMs,m.slowReleaseMs,m.memory,m.kneeDb);
    optical_.setLink(1.0f);
    optical_.snapLink();
    optical_.setSidechain(m.hpfOn,m.hpfHz);
    for (auto& shelf : air_) shelf.setTarget(m.airDb);
    if (channels == 2) {
        midSide_.setDomain(lab2c::dsp::MidSideOpticalBridge::kMid,bridgeDomain(m));
        midSide_.setDomain(lab2c::dsp::MidSideOpticalBridge::kSide,bridgeDomain(settings_.side));
    }
    inputTrim_.setTarget(settings_.inputTrimDb);
    makeupGain_.setTarget(m.makeupDb);
    msGains_.setTargets(m.airDb,settings_.side.airDb,settings_.side.makeupDb,true);
    meters.midSide = channels == 2 && settings_.midSide;
    modeFade_.setTarget(meters.midSide);
    const float outputTarget = std::pow(10.0f,settings_.outputGainDb * 0.05f);
    if (snapPending_) {
        gainSmoothed_ = outputTarget;
        optical_.snapWet();
        optical_.snapSidechain();
        for (auto& shelf : air_) shelf.snap();
        midSide_.snapSidechain();
        modeFade_.snap();
        inputTrim_.snap();
        makeupGain_.snap();
        msGains_.snap();
        snapPending_ = false;
    }

    auto* left = block.data[0];
    auto* right = channels > 1 ? block.data[1] : nullptr;
    for (int n = 0; n < block.samples; ++n) {
        const float wet = optical_.nextWet(), scMix = optical_.nextSidechainMix();
        gainSmoothed_ = outputTarget + gainStepCoefficient_ * (gainSmoothed_ - outputTarget);
        const float gIn = inputTrim_.next(), gMk = makeupGain_.next();
        const auto sideGains = msGains_.next();
        const bool neutral = gIn == 1.0f && gMk == 1.0f;
        auto mixDry = [wet,gMk](float dry, float processed) noexcept {
            return wet > 0.0f ? dry + (processed * gMk - dry) * wet : dry;
        };
        const float inL = left[n];
        float grL{};
        if (right == nullptr) {
            float mono;
            if (neutral) mono = optical_.processChannel(inL,0,wet,&grL,scMix);
            else {
                mono = mixDry(inL,optical_.processChannel(inL * gIn,0,1.0f,&grL,scMix));
                grL *= wet;
            }
            left[n] = air_[0].process(mono,wet) * gainSmoothed_;
            meters.reductionDb = std::max(meters.reductionDb,grL);
            continue;
        }

        const float inR = right[n];
        float l=inL, r=inR, ml=inL, mr=inR;
        float grR{}, grMid{}, grSide{};
        // Keep both engines warm. Only their mode crossfade decides what reaches the output.
        if (neutral) {
            optical_.processStereo(l,r,wet,&grL,&grR,scMix);
            midSide_.processStereo(ml,mr,wet,&grMid,&grSide);
        } else {
            l = ml = inL * gIn;
            r = mr = inR * gIn;
            optical_.processStereo(l,r,1.0f,&grL,&grR,scMix);
            midSide_.processStereo(ml,mr,1.0f,&grMid,&grSide);
            grL *= wet; grR *= wet; grMid *= wet; grSide *= wet;
            l = mixDry(inL,l); r = mixDry(inR,r);
            ml = mixDry(inL,ml); mr = mixDry(inR,mr);
        }
        const double sideAirDiff = msGains_.airDifference(inL,inR,ml,mr,gMk,sideGains.sideGain,wet);
        meters.midDb = std::max(meters.midDb,grMid);
        meters.sideDb = std::max(meters.sideDb,grSide);
        const float x = modeFade_.next();
        if (x > 0.0f) {
            l = lab2c::dsp::StereoModeCrossfade::mix(l,ml,x);
            r = lab2c::dsp::StereoModeCrossfade::mix(r,mr,x);
            const float lr = std::max(grL,grR), ms = std::max(grMid,grSide);
            grL = grR = x >= 1.0f ? ms : lr + x * (ms-lr);
        }
        l = air_[0].process(l,wet);
        r = air_[1].process(r,wet);
        if (x > 0.0f && sideGains.blend > 0.0f && sideAirDiff != 0.0) {
            const double correction = static_cast<double>(x) * static_cast<double>(sideGains.blend) * sideAirDiff;
            l = static_cast<float>(static_cast<double>(l)+correction);
            r = static_cast<float>(static_cast<double>(r)-correction);
        }
        left[n] = l * gainSmoothed_;
        right[n] = r * gainSmoothed_;
        meters.reductionDb = std::max({meters.reductionDb,grL,grR});
    }
}
}

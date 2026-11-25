#include "PluginProcessor.h"
#include "../Scenario.h"
#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace {
void set(LAB2CAudioProcessor& p, const char* name, float value) {
    auto* param = p.parameters.getParameter(name);
    if (!param) throw std::runtime_error(name);
    param->setValueNotifyingHost(param->convertTo0to1(value));
}

void configure(LAB2CAudioProcessor& p, int phase) {
    set(p, "dspEnabled", 1);
    set(p, "thresholdDb", -30); set(p, "ratio", 6); set(p, "attackMs", 4);
    set(p, "fastReleaseMs", 75); set(p, "slowReleaseMs", 900);
    set(p, "kneeDb", 4); set(p, "memory", 0.5f);
    set(p, "scHpfOn", 1); set(p, "scHpfFreq", 120);
    set(p, "inputTrimDb", phase == 0 ? 0 : 2); set(p, "outputGainDb", phase == 0 ? 0 : -2);
    set(p, "makeupGainDb", phase == 0 ? 0 : 3); set(p, "airGainDb", phase == 0 ? 0 : 2);
    set(p, "sideThresholdDb", -15); set(p, "sideRatio", 2); set(p, "sideAttackMs", 12);
    set(p, "sideFastReleaseMs", 110); set(p, "sideSlowReleaseMs", 1200);
    set(p, "sideMemory", 0.4f); set(p, "sideKneeDb", 8);
    set(p, "sideScHpfOn", 1); set(p, "sideScHpfFreq", 200);
    set(p, "sideAirGainDb", 5); set(p, "sideMakeupGainDb", -1);
    set(p, "msMode", phase >= 2 && phase <= 3);
    if (phase == 3) p.reset();
}
}

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    juce::ScopedJuceInitialiser_GUI init;
    for (int hz : scenario::rates) for (int channels : {1,2}) for (int block : scenario::blocks) {
        LAB2CAudioProcessor original;
        original.setPlayConfigDetails(channels, channels, hz, block);
        original.prepareToPlay(hz, block);
        std::ofstream out(std::filesystem::path(argv[1]) / scenario::filename(hz, channels, block), std::ios::binary);
        out.exceptions(std::ios::badbit | std::ios::failbit);
        juce::MidiBuffer midi;
        for (int phase = 0; phase < scenario::phases; ++phase) {
            configure(original, phase);
            for (int start = 0; start < scenario::segment; start += block) {
                int size = std::min(block, scenario::segment-start);
                juce::AudioBuffer<float> buffer(channels,size);
                for (int ch = 0; ch < channels; ++ch)
                    for (int i = 0; i < size; ++i) buffer.setSample(ch,i,scenario::input(ch,phase*scenario::segment+start+i));
                original.processBlock(buffer,midi);
                for (int i = 0; i < size; ++i) for (int ch = 0; ch < channels; ++ch) {
                    float x = buffer.getSample(ch,i);
                    out.write(reinterpret_cast<const char*>(&x),sizeof(x));
                }
            }
        }
    }
}

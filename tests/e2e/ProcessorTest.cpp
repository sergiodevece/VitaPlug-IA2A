#include "audio/PluginProcessor.h"
#include "tests/fixtures/Signals.h"
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>

using namespace ia2a;
static void require(bool yes,const char* message) {
    if(!yes) throw std::runtime_error(message);
}
int main() {
    juce::ScopedJuceInitialiser_GUI framework;
    try {
        Processor plugin;
        auto layout=plugin.getBusesLayout();
        layout.inputBuses.getReference(0)=layout.outputBuses.getReference(0)=juce::AudioChannelSet::mono();
        layout.inputBuses.getReference(1)=juce::AudioChannelSet::mono();
        require(plugin.setBusesLayout(layout),"bus negotiation");
        auto voice=fixtures::voice();
        constexpr int frames=256;
        plugin.prepareToPlay(voice.sampleRate,frames);
        juce::AudioBuffer<float> block(2,frames);
        juce::MidiBuffer midi;
        int cursor=0;
        auto process=[&](bool expectDry) {
            for(int n=0;n<frames;++n) {
                const auto pos=(cursor+n)%voice.track[0].size();
                block.setSample(0,n,voice.track[0][pos]);
                block.setSample(1,n,float(.3*std::sin(fixtures::tau*127*(cursor+n)/voice.sampleRate)));
            }
            plugin.processBlock(block,midi);
            if(expectDry)for(int n=0;n<frames;++n)
                require(block.getSample(0,n)==voice.track[0][(cursor+n)%voice.track[0].size()],"unexpected output before approval/preview");
            cursor+=frames;
        };
        process(true);
        juce::MemoryBlock original;plugin.getStateInformation(original);
        plugin.setReference(true); plugin.captureAudio();
        for(int n=0;n<1500;++n)process(true);
        for(int n=0;n<500&&!plugin.snapshot().hasClip;++n)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        require(plugin.snapshot().hasClip,"12s capture");
        plugin.submit(juce::String::fromUTF8("Ponla en primer plano y que quede integrada en la mezcla al mismo tiempo"));
        for(int n=0;n<6000&&plugin.snapshot().busy;++n) {
            process(false);std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        auto s=plugin.snapshot();
        std::cout<<s.provider<<" · "<<s.message<<std::endl;
        require(s.canApply&&s.preview,"candidate ready");
        juce::MemoryBlock before,after;plugin.getStateInformation(before);
        plugin.listen(false,true);process(false);plugin.listen(true,false);
        for(int n=0;n<50;++n)process(false);
        plugin.apply();plugin.getStateInformation(after);
        require(!plugin.snapshot().pending && after!=before,"apply confirms candidate");
        Processor reopened;reopened.setStateInformation(after.getData(),int(after.getSize()));
        juce::MemoryBlock restored;reopened.getStateInformation(restored);
        require(after==restored && !reopened.snapshot().pending,"offline project recall");
        plugin.undo();juce::MemoryBlock undone;plugin.getStateInformation(undone);
        require(undone==before,"undo restores complete state");
        plugin.captureAudio();plugin.releaseResources();
        require(!plugin.snapshot().hasClip&&!plugin.snapshot().pending,"invalidation on release");
        std::cout<<"ProcessorTest OK"<<std::endl;
    } catch(const std::exception& e){std::cerr<<e.what()<<std::endl;return 1;}
}

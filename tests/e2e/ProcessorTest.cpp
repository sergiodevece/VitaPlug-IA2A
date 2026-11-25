#include "audio/PluginProcessor.h"
#include "ui/Editor.h"
#include "tests/fixtures/Signals.h"
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>

using namespace ia2a;
static void require(bool yes,const char* message) {
    if(!yes) throw std::runtime_error(message);
}
int main(int argc,char**) {
    juce::ScopedJuceInitialiser_GUI framework;
    try {
        const bool demoMode=argc==1;
        Processor plugin(demoMode);
        plugin.updateTrackProperties({juce::String(demoMode?"Voz principal":"Audio 42"),{}});
        require(plugin.snapshot().source=="auto" && plugin.snapshot().effectiveSource==(demoMode?"voice":"other"),"track name informs source");

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
        const auto waitForTurn = [&] {
            const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(8);
            while(plugin.snapshot().busy && std::chrono::steady_clock::now()<end) {
                process(false);
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            require(!plugin.snapshot().busy,"conversation turn timed out");
        };
        if(!demoMode) {
            plugin.submit(juce::String::fromUTF8("El inicio me salta y luego se esconde. ¿Le puedes dar continuidad?"));
            waitForTurn();
            require(plugin.snapshot().needsClarification && !plugin.snapshot().canApply,"clarification for unknown material");
        }
        plugin.submit(juce::String::fromUTF8(demoMode
            ?"Ponla en primer plano y que quede integrada en la mezcla al mismo tiempo"
            :"Es la voz principal. Las palabras flojas casi desaparecen, pero quiero que siga respirando."));
        waitForTurn();
        auto s=plugin.snapshot();
        std::cout<<s.provider<<" · "<<s.message<<std::endl;
        require(s.canApply&&s.preview,"candidate ready");
        if(!demoMode) {
            plugin.submit("Controla el ataque del transitorio");
            waitForTurn();
            const auto failed=plugin.snapshot();
            require(failed.turnFailed && failed.provider=="claude" && failed.canApply,
                "a failed refinement must keep the connection identity and the previous proposal");
            Editor editor(plugin);
            auto* retry=dynamic_cast<juce::TextButton*>(editor.findChildWithID("send"));
            auto* title=dynamic_cast<juce::Label*>(editor.findChildWithID("status"));
            require(retry && retry->isEnabled() && retry->getButtonText()=="Reintentar",
                "failed turn must expose a retry in the editor");
            require(title && title->getText()=="Propuesta anterior","editor must identify the previous proposal");
            retry->onClick();
            waitForTurn();
            require(!plugin.snapshot().turnFailed && plugin.snapshot().canApply,
                "retry must restore the conversation without losing the previous proposal");
        }
        juce::MemoryBlock before,after;plugin.getStateInformation(before);
        plugin.listen(false,true);process(false);plugin.listen(true,false);
        for(int n=0;n<50;++n)process(false);
        plugin.apply();plugin.getStateInformation(after);
        require(!plugin.snapshot().pending && after!=before,"apply confirms candidate");
        if(!demoMode) {
            plugin.captureAudio();
            for(int n=0;n<1500;++n)process(false);
            for(int n=0;n<500&&!plugin.snapshot().hasClip;++n)
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            require(plugin.snapshot().hasClip,"recapture for refinement");
            plugin.submit(juce::String::fromUTF8("En este pasaje déjalo respirar como en el anterior."));
            waitForTurn();
            require(plugin.snapshot().message=="Conserva el ajuste aplicado en la voz.","history across confirmation and recapture");
        }
        Processor reopened;reopened.setStateInformation(after.getData(),int(after.getSize()));
        juce::MemoryBlock restored;reopened.getStateInformation(restored);
        require(after==restored && !reopened.snapshot().pending,"offline project recall");
        plugin.undo();juce::MemoryBlock undone;plugin.getStateInformation(undone);
        require(undone==before,"undo restores complete state");
        if(demoMode) {
            plugin.submit(juce::String::fromUTF8("quiero el umbral más bajo, que se note más la compresión y equilibra con el gain"));
            waitForTurn();
            if(!plugin.snapshot().canApply) throw std::runtime_error("direct proposal: "+plugin.snapshot().message.toStdString());
            juce::MemoryBlock stillApplied;
            plugin.getStateInformation(stillApplied);
            require(stillApplied==undone,"a direct request must not apply automatically");

            Processor directOnly(true);
            directOnly.prepareToPlay(48000,512);
            const auto waitForDirect = [&] {
                const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(2);
                while(directOnly.snapshot().busy && std::chrono::steady_clock::now()<end)
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                require(!directOnly.snapshot().hasClip && directOnly.snapshot().canApply,
                    "an exact adjustment can be staged without a captured fragment");
            };
            directOnly.submit("Pon el umbral en -25 dB");
            waitForDirect();
            directOnly.listen(false,true);
            directOnly.submit("Baja el umbral 3 dB");
            waitForDirect();
            directOnly.apply();
            juce::MemoryBlock selected;
            directOnly.getStateInformation(selected);
            juce::MemoryInputStream data(selected,false);
            data.skipNextBytes(20); // State header, flags and the two global gains.
            require(data.readFloat()==-21.f,"a relative edit while listening to Before uses the applied threshold");
        }
        plugin.captureAudio();plugin.releaseResources();
        require(!plugin.snapshot().hasClip&&!plugin.snapshot().pending,"invalidation on release");
        std::cout<<"ProcessorTest OK"<<std::endl;
    } catch(const std::exception& e){std::cerr<<e.what()<<std::endl;return 1;}
}

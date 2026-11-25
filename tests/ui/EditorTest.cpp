#include "ui/Editor.h"
#include "audio/PluginProcessor.h"
#include "tests/fixtures/Signals.h"
#include <iostream>
#include <thread>

namespace {
template<class T> T& widget(juce::Component& parent, const juce::String& id) {
    auto* item = dynamic_cast<T*>(parent.findChildWithID(id));
    if (item == nullptr) throw std::runtime_error("Missing widget: "+id.toStdString());
    return *item;
}
void check(bool condition,const char* reason) { if (!condition) throw std::runtime_error(reason); }
}
int main(int argc,char** argv) {
    juce::ScopedJuceInitialiser_GUI juce;
    try {
        ia2a::Processor processor(true);
        processor.prepareToPlay(32000,256);
        ia2a::Editor editor(processor);
        auto& apply = widget<juce::TextButton>(editor,"apply");
        auto& graph = widget<juce::TextButton>(editor,"graph");
        auto& before = widget<juce::TextButton>(editor,"before");
        auto& after = widget<juce::TextButton>(editor,"after");
        auto& source = widget<juce::ComboBox>(editor,"source");
        auto& match = widget<juce::ComboBox>(editor,"level");
        auto& ref = widget<juce::TextButton>(editor,"reference");
        auto& prompt = widget<juce::TextEditor>(editor,"prompt");
        auto& send = widget<juce::TextButton>(editor,"send");
        check(!apply.isEnabled() && !after.isEnabled(),"An empty session cannot accept or audition a candidate");
        check(prompt.getWantsKeyboardFocus() && graph.getWantsKeyboardFocus(),"Keyboard focus is missing");
        auto& signature = widget<juce::Label>(editor,"designer-credit");
        check(signature.isVisible() && signature.getText()=="designed by SerxDev","VitaPlug designer credit is missing");
        const auto signatureBounds = signature.getBounds();
        const int height = editor.getHeight();
        const auto initialBefore = before.getBounds();
        graph.onClick();
        check(editor.getHeight()>height && initialBefore==before.getBounds(),"Opening the graph moved the comparison controls");
        check(signature.isVisible() && signature.getBounds()==signatureBounds,"Opening the graph moved the designer credit");
        graph.onClick();
        check(editor.getHeight()==height,"The drawer did not close");
        source.setSelectedId(3,juce::sendNotificationSync);
        check(processor.snapshot().source=="drums","Source selection was not applied");
        ref.onClick();
        check(processor.snapshot().reference,"Reference selection was lost");
        ref.onClick();
        match.setSelectedId(2,juce::sendNotificationSync);
        check(!processor.snapshot().matched,"Requested final level was ignored");
        const auto drums=ia2a::fixtures::drums(true);
        const auto play=[&](int samples) {
            juce::AudioBuffer<float> block(2,256);
            juce::MidiBuffer midi;
            for (int start=0;start<samples;start+=256) {
                for (int ch=0;ch<2;++ch) for(int n=0;n<256;++n)
                    block.setSample(ch,n,drums.track[ch][(start+n)%drums.track[ch].size()]);
                processor.processBlock(block,midi);
            }
        };
        const auto awaitProposal=[&] {
            const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(50);
            while(std::chrono::steady_clock::now()<end) {
                juce::Timer::callPendingTimersSynchronously();
                if(processor.snapshot().canApply && apply.isEnabled()) return;
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            }
            throw std::runtime_error("Timed out waiting for proposal: "+processor.snapshot().message.toStdString());
        };
        prompt.setText("Quiero un cambio.",true);
        check(prompt.onReturnKey!=nullptr && send.onClick!=nullptr,"Enter or Send is missing");
        send.onClick();
        check(!apply.isEnabled(),"Sending without audio enabled Apply");
        check(processor.snapshot().recording,"Sending a first request should arm capture");
        play(32000*12+256);
        const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(2);
        while((processor.snapshot().busy || !processor.snapshot().hasClip) && std::chrono::steady_clock::now()<until)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        juce::Timer::callPendingTimersSynchronously();
        auto& status=widget<juce::Label>(editor,"status");
        check(status.getText()=="Falta un detalle","A clarification should be visible in the conversation");
        check(!apply.isEnabled() && !after.isEnabled(),"A clarification cannot be applied or previewed");
        prompt.setText(juce::String::fromUTF8("Los platos están muy sucios."),true);
        send.onClick();
        awaitProposal();
        before.onClick(); check(!processor.snapshot().preview,"Before did not select the approved path");
        after.onClick(); check(processor.snapshot().preview,"After did not select the proposal");
        match.setSelectedId(1,juce::sendNotificationSync);
        check(processor.snapshot().matched,"Level match was not selected");
        play(32000*12);
        auto saveSnapshot = [&](const char* path) {
            juce::File file(juce::String::fromUTF8(path));
            juce::FileOutputStream out(file);
            if(!out.openedOk()) throw std::runtime_error("Snapshot path unavailable");
            out.setPosition(0); out.truncate();
            juce::PNGImageFormat png;
            if(!png.writeImageToStream(editor.createComponentSnapshot(editor.getLocalBounds()),out))
                throw std::runtime_error("Snapshot failed");
        };
        if(argc>=2) saveSnapshot(argv[1]);
        if(argc>=3) {
            graph.onClick();
            saveSnapshot(argv[2]);
        }
        auto& drop=widget<juce::TextButton>(editor,"discard");
        drop.onClick(); check(!processor.snapshot().pending && !apply.isEnabled(),"Discard retained the candidate");
        prompt.setText(juce::String::fromUTF8("Los platos están muy sucios."),true);
        prompt.onReturnKey(); awaitProposal();
        apply.onClick(); check(!processor.snapshot().pending && processor.snapshot().canUndo,"Apply did not promote the candidate");
        auto& undo=widget<juce::TextButton>(editor,"undo"); undo.onClick();
        check(!processor.snapshot().canUndo,"Undo did not consume the previous setting");
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
    std::cout << "Editor controls, source, reference, level and inline drawer passed\n";
}

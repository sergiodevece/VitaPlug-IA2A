#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

namespace ia2a {
class Processor;
class Editor final : public juce::AudioProcessorEditor, private juce::Timer {
public:
    explicit Editor(Processor&);
    ~Editor() override;
    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void refresh();
    void submitPrompt();
    void renderPlate();

    class PanelLook final : public juce::LookAndFeel_V4 {
    public:
        juce::Font getTextButtonFont(juce::TextButton&,int height) override;
        juce::Font getComboBoxFont(juce::ComboBox&) override;
        void drawButtonBackground(juce::Graphics&,juce::Button&,const juce::Colour&,bool,bool) override;
        juce::Typeface::Ptr face;
    } look;
    Processor& processor;
    juce::ComboBox source, level;
    juce::TextButton reference, capture, send, before, after, apply, discard, undo, disclosure;
    juce::TextEditor prompt;
    juce::Label status, request, response, designerCredit;
    juce::Image material, plate;
    bool expanded{};
};
}

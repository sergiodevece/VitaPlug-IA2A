#include "ui/Editor.h"
#include "audio/PluginProcessor.h"
#include "assist/TrackSource.h"
#include <BinaryData.h>
#include <cmath>

namespace ia2a {
namespace {
using juce::Colour;
using juce::Rectangle;
constexpr int width = 1000, closedHeight = 498, graphHeight = 164;
const Rectangle<int> screen(51,93,687,210), bay(752,93,197,210), console(51,317,898,110);
const Rectangle<int> graph(51,482,898,150);
const Colour cream(0xffe5e4d3), green(0xffc8e9bc), phosphor(0xff93c3a3), amber(0xfff0af70);
juce::Font font(float size, juce::Typeface::Ptr face={}) {
    auto options = juce::FontOptions(size);
    if(face) options=options.withTypeface(face);
    return juce::Font(options);
}
void text(juce::Graphics& g,const juce::String& s,Rectangle<int> rect,juce::Font f,Colour c,
          juce::Justification just=juce::Justification::centredLeft) {
    g.setFont(f); g.setColour(c); g.drawText(s,rect,just,true);
}
void inset(juce::Graphics& g,Rectangle<int> area,Colour top,Colour bottom) {
    auto r=area.toFloat();
    g.setGradientFill(juce::ColourGradient(top,r.getX(),r.getY(),bottom,r.getRight(),r.getBottom(),false));
    g.fillRoundedRectangle(r,6);
    g.setColour(Colour(0xff080f0b)); g.drawRoundedRectangle(r.reduced(1),6,2);
    g.setColour(Colour(0x6095a491)); g.drawLine(r.getX()+5,r.getBottom(),r.getRight()-5,r.getBottom(),1);
}
void meter(juce::Graphics& g, int y, float db) {
    constexpr int count=17;
    const float fraction=juce::jlimit(0.f,1.f,(db+60.f)/60.f);
    for(int i=0;i<count;++i) {
        const bool lit = i < juce::roundToInt(fraction*count);
        g.setColour(lit ? (i>14 ? amber : Colour(0xff8bc98b)) : Colour(0xff293a2d));
        g.fillRect(806+i*7,y,5,13);
    }
}
}

juce::Font Editor::PanelLook::getTextButtonFont(juce::TextButton&,int height) {
    return font(juce::jmin(17.0f,height*.54f),face);
}
juce::Font Editor::PanelLook::getComboBoxFont(juce::ComboBox&) { return font(16,face); }
void Editor::PanelLook::drawButtonBackground(juce::Graphics& g,juce::Button& b,const Colour&,bool over,bool down) {
    bool accent=b.getComponentID()=="apply";
    bool active=b.getToggleState();
    Colour top=accent?Colour(0xfff4bd83):active?Colour(0xff3a6047):Colour(0xff434b43);
    Colour bottom=accent?Colour(0xffce7b3d):active?Colour(0xff1c3526):Colour(0xff1b231e);
    if(!b.isEnabled()) { top=Colour(0xff30362f); bottom=Colour(0xff212720); }
    if(over&&b.isEnabled()) top=top.brighter(.15f);
    if(down) std::swap(top,bottom);
    auto r=b.getLocalBounds().toFloat().reduced(1);
    g.setGradientFill(juce::ColourGradient(top,0,r.getY(),bottom,0,r.getBottom(),false));
    g.fillRoundedRectangle(r,4);
    g.setColour(Colour(0xff090e0a)); g.drawRoundedRectangle(r,4,1);
    g.setColour(active?Colour(0xff8bd3a1):Colour(0x60899482));
    g.drawLine(r.getX()+4,r.getY()+2,r.getRight()-4,r.getY()+2,1);
    if(b.hasKeyboardFocus(false)){g.setColour(amber);g.drawRoundedRectangle(r.reduced(2),3,1.6f);}
}

Editor::Editor(Processor& proc) : AudioProcessorEditor(proc), processor(proc) {
    material=juce::ImageCache::getFromMemory(BinaryData::iaq8_png,BinaryData::iaq8_pngSize);
    look.face=juce::Typeface::createSystemTypefaceFor(BinaryData::BarlowCondensedSemiBold_ttf,BinaryData::BarlowCondensedSemiBold_ttfSize);
    look.setColour(juce::ComboBox::backgroundColourId,Colour(0xff232e26));
    look.setColour(juce::ComboBox::outlineColourId,Colour(0xff758171));
    look.setColour(juce::ComboBox::textColourId,cream);
    setLookAndFeel(&look);
    auto add = [this](juce::Component& component,const char* id,const juce::String& title) {
        component.setComponentID(id); component.setTitle(title); component.setName(title);
        component.setWantsKeyboardFocus(true); addAndMakeVisible(component);
    };
    auto button=[&](juce::TextButton& b,const char* id,const juce::String& title) {
        add(b,id,title); b.setButtonText(title); b.setColour(juce::TextButton::textColourOffId,cream);
        b.setColour(juce::TextButton::textColourOnId,green);
    };
    add(source,"source","Fuente");
    int sourceItem = 1;
    for (const auto& option : trackSourceOptions())
        source.addItem(juce::String::fromUTF8(option.label), sourceItem++);
    button(reference,"reference","Referencia de mezcla");
    button(capture,"capture","Escuchar"); add(prompt,"prompt",juce::String::fromUTF8("Petición al asistente"));
    prompt.setMultiLine(false); prompt.setInputRestrictions(500);
    prompt.setFont(font(17)); prompt.setColour(juce::TextEditor::backgroundColourId,Colour(0xff09170f));
    prompt.setColour(juce::TextEditor::outlineColourId,Colour(0xff526b56));
    prompt.setColour(juce::TextEditor::focusedOutlineColourId,amber);
    prompt.setColour(juce::TextEditor::textColourId,green);
    prompt.setTextToShowWhenEmpty(juce::String::fromUTF8("Pide un cambio con tus palabras…"),phosphor);
    button(send,"send","Enviar"); button(before,"before","Antes"); button(after,"after",juce::String::fromUTF8("Después"));
    add(level,"level",juce::String::fromUTF8("Nivel de comparación")); level.addItem("Vol. igualado",1); level.addItem("Nivel final",2);
    button(undo,"undo","Deshacer"); button(discard,"discard","Descartar"); button(apply,"apply","Aplicar");
    button(disclosure,"graph",juce::String::fromUTF8("Ver reducción"));
    auto label=[this](juce::Label& l,const char* id,float size,Colour color) {
        l.setComponentID(id); l.setFont(font(size)); l.setColour(juce::Label::textColourId,color);
        l.setBorderSize({0,0,0,0}); l.setJustificationType(juce::Justification::topLeft); addAndMakeVisible(l);
    };
    label(status,"status",24,green); label(request,"request",17,phosphor); label(response,"response",17,cream);
    label(designerCredit,"designer-credit",16,cream);
    designerCredit.setText("designed by SerxDev",juce::dontSendNotification);
    designerCredit.setFont(font(16,look.face));
    designerCredit.setJustificationType(juce::Justification::centredRight);
    source.onChange = [this] {
        const int selected = source.getSelectedId() - 1;
        const auto options = trackSourceOptions();
        if (selected >= 0 && static_cast<std::size_t>(selected) < options.size())
            processor.setSource(options[static_cast<std::size_t>(selected)].id);
        refresh();
    };
    reference.onClick=[this] { processor.setReference(!processor.snapshot().reference); refresh(); };
    capture.onClick=[this] { processor.captureAudio(); refresh(); };
    prompt.onReturnKey=[this] { submitPrompt(); }; send.onClick=[this] { submitPrompt(); };
    prompt.onTextChange=[this] { refresh(); };
    before.onClick=[this] { processor.listen(false,processor.snapshot().matched); refresh(); };
    after.onClick=[this] { processor.listen(true,processor.snapshot().matched); refresh(); };
    level.onChange=[this] { processor.listen(processor.snapshot().preview,level.getSelectedId()==1); refresh(); };
    apply.onClick=[this] { processor.apply(); refresh(); };
    discard.onClick=[this] { processor.discard(); refresh(); }; undo.onClick=[this] { processor.undo(); refresh(); };
    disclosure.onClick=[this] {
        expanded=!expanded; disclosure.setToggleState(expanded,juce::dontSendNotification);
        disclosure.setButtonText(juce::String::fromUTF8(expanded?"Ocultar reducción":"Ver reducción"));
        setSize(width,closedHeight+(expanded?graphHeight:0)); repaint();
    };
    setSize(width,closedHeight);
    refresh(); startTimerHz(15);
}
Editor::~Editor() { stopTimer(); setLookAndFeel(nullptr); }

void Editor::submitPrompt() {
    const auto words=prompt.getText().trim();
    if(words.isEmpty()) return;
    processor.submit(words);
    prompt.clear();
    refresh();
}
void Editor::timerCallback() { refresh(); }
void Editor::refresh() {
    const auto s=processor.snapshot();
    int sourceItem = 1;
    for (const auto& option : trackSourceOptions()) {
        if (s.source == option.id) {
            source.setSelectedId(sourceItem, juce::dontSendNotification);
            break;
        }
        ++sourceItem;
    }
    juce::String sourceHelp = trackSourceLabel(s.effectiveSource);
    if (s.trackName.isNotEmpty()) sourceHelp = s.trackName + juce::String::fromUTF8(" · ") + sourceHelp;
    source.setTooltip(sourceHelp);
    reference.setToggleState(s.reference,juce::dontSendNotification);
    reference.setButtonText(juce::String::fromUTF8(s.reference?(s.auxActive?"Ref. mezcla activa":"Ref. sin señal"):"Ref. mezcla inactiva"));
    level.setSelectedId(s.matched?1:2,juce::dontSendNotification);
    level.setEnabled(!s.recording);
    before.setEnabled(s.pending); after.setEnabled(s.pending);
    before.setToggleState(!s.preview,juce::dontSendNotification); after.setToggleState(s.preview,juce::dontSendNotification);
    apply.setEnabled(s.canApply); discard.setEnabled(s.pending || s.busy); undo.setEnabled(s.canUndo);
    apply.setColour(juce::TextButton::textColourOffId,s.canApply?Colour(0xff2c190d):cream);
    capture.setButtonText(s.recording?"Cancelar":"Escuchar"); send.setEnabled(!s.recording&&!s.busy&&!prompt.isEmpty());
    const auto title= s.recording ? juce::String::fromUTF8("Escuchando la pista") : s.busy ? "Preparando la propuesta"
        : s.pending ? "Propuesta para escuchar" : s.needsClarification ? "Falta un detalle"
        : s.hasClip ? "Fragmento preparado" : juce::String::fromUTF8("¿Qué quieres cambiar?");
    status.setText(title,juce::dontSendNotification);
    request.setText(s.prompt.isEmpty()?juce::String():juce::String::fromUTF8("“")+s.prompt+juce::String::fromUTF8("”"),juce::dontSendNotification);
    response.setText(s.message.isEmpty()?juce::String::fromUTF8("Reproduce un fragmento en tu DAW y pulsa Escuchar."):s.message,juce::dontSendNotification);
    repaint();
}

void Editor::renderPlate() {
    plate=juce::Image(juce::Image::RGB,getWidth(),getHeight(),true);
    juce::Graphics g(plate);
    g.fillAll(Colour(0xff263b2d));
    if(material.isNull()) return;
    // Sample bare metal and rack ears from the approved IAQ8 panel at its native scale.
    const int w=plate.getWidth(), h=plate.getHeight(), ear=34;
    for(int y=0,row=0;y<h;y+=76,++row) for(int x=0,col=0;x<w;x+=405,++col) {
        const bool flipX=(col%2)!=0, flipY=(row%2)!=0;
        juce::Graphics::ScopedSaveState scope(g);
        g.addTransform(juce::AffineTransform::scale(flipX?-1.f:1.f,flipY?-1.f:1.f)
            .translated(float(x+(flipX?405:0)),float(y+(flipY?76:0))));
        g.drawImage(material,0,0,405,76,421,49,726,94);
    }
    g.drawImage(material,ear,0,w-2*ear,8,85,30,1500,14);
    g.drawImage(material,ear,h-9,w-2*ear,9,85,888,1500,14);
    g.drawImage(material,0,0,ear,h,4,33,77,867);
    g.drawImage(material,w-ear,0,ear,h,1591,33,77,867);
}

void Editor::resized() {
    renderPlate();
    source.setBounds(657,42,138,31); reference.setBounds(805,42,144,31);
    request.setBounds(74,151,642,32); status.setBounds(74,187,642,33); response.setBounds(74,224,644,68);
    capture.setBounds(67,331,83,37); prompt.setBounds(158,331,689,37); send.setBounds(855,331,78,37);
    before.setBounds(67,379,62,36); after.setBounds(134,379,77,36); level.setBounds(219,379,123,36);
    undo.setBounds(673,379,80,36); discard.setBounds(759,379,82,36); apply.setBounds(847,379,86,36);
    disclosure.setBounds(51,438,135,34);
    designerCredit.setBounds(749,438,200,34);
}
void Editor::paint(juce::Graphics& g) {
    const auto s=processor.snapshot();
    g.drawImageAt(plate,0,0);
    auto brand=font(44).boldened();
    text(g,"IA",{51,22,54,46},brand,cream);
    text(g,"2A",{91,22,78,46},brand,amber);
    text(g,"VitaPlug",{165,30,96,20},font(16,look.face),cream);
    text(g,"O P T I C A L   C O M P R E S S O R",{165,52,208,13},font(11,look.face),cream);
    const auto providerText = s.provider == "local-demo" ? juce::String::fromUTF8("Intérprete local de prueba")
        : s.provider == "http" ? "Servicio configurado" : s.provider == "claude" ? "Claude conectado"
        : s.provider.isEmpty() || s.provider == "disconnected" ? "Claude sin conectar" : s.provider;
    text(g,providerText,{204,439,390,32},font(15,look.face),cream);
    text(g,"FUENTE",{600,42,52,31},font(15,look.face),cream);
    inset(g,screen,Colour(0xff14291e),Colour(0xff07130f));
    inset(g,bay,Colour(0xff202b22),Colour(0xff101911));
    inset(g,console,Colour(0xff2a302a),Colour(0xff151d16));
    auto sourceLabel = trackSourceLabel(s.effectiveSource).toUpperCase();
    if (s.source == "auto")
        sourceLabel += juce::String::fromUTF8(" · AUTO");
    text(g,juce::String::fromUTF8("CONVERSACIÓN · ")+sourceLabel,{74,107,363,25},font(15,look.face),phosphor);
    auto tag=s.recording?juce::String(juce::roundToInt(s.progress*100))+" %":s.busy?"CALCULANDO":s.pending?"PROPUESTA":s.canUndo?"APLICADO":"";
    text(g,tag,{516,107,200,25},font(15,look.face),amber,juce::Justification::centredRight);
    g.setColour(Colour(0xff364c3c)); g.drawHorizontalLine(138,74,716);
    if(s.recording) { g.setColour(phosphor); g.fillRect(74,139,juce::roundToInt(642*juce::jlimit(0.f,1.f,s.progress)),2); }
    text(g,"NIVELES",{769,108,122,24},font(16,look.face),cream);
    text(g,"IN",{769,143,35,18},font(14,look.face),cream); meter(g,146,s.inputDb);
    text(g,"OUT",{769,178,35,18},font(14,look.face),cream); meter(g,181,s.outputDb);
    g.setColour(Colour(0xff435046)); g.drawHorizontalLine(221,769,933);
    text(g,juce::String::fromUTF8("REDUCCIÓN · MÁX."),{769,230,165,22},font(15,look.face),cream);
    text(g,juce::String(s.reductionDb,1).replaceCharacter('.',',')+" dB",{769,251,166,42},font(35,look.face),green);
    if(!expanded) return;
    inset(g,graph,Colour(0xff14291e),Colour(0xff07130f));
    const auto mode=s.channels==1?"MONO":s.midSide?"MID / SIDE":"L/R";
    text(g,juce::String::fromUTF8("REDUCCIÓN DE GANANCIA"),{73,489,330,24},font(15,look.face),phosphor);
    text(g,juce::String::fromUTF8("12 s  ·  ")+juce::String(mode),{710,489,215,24},font(15,look.face),amber,juce::Justification::centredRight);
    float ceiling=6;
    for(float v:s.grMain) ceiling=std::max(ceiling,v);
    if(s.midSide) for(float v:s.grSide) ceiling=std::max(ceiling,v);
    ceiling=std::ceil(ceiling/3)*3;
    Rectangle<float> axis(113,521,811,72);
    for(int i=0;i<4;++i) {
        int y=juce::roundToInt(axis.getY()+axis.getHeight()*i/3);
        g.setColour(Colour(0xff344d3b)); g.drawHorizontalLine(y,axis.getX(),axis.getRight());
        text(g,juce::String(-juce::roundToInt(ceiling*i/3))+" dB",{69,y-8,40,15},font(11),phosphor);
    }
    auto plot=[&](const std::array<float,120>& values,Colour colour) {
        juce::Path line;
        for(size_t i=0;i<values.size();++i) {
            float x=axis.getX()+axis.getWidth()*float(i)/float(values.size()-1);
            float y=axis.getY()+axis.getHeight()*juce::jlimit(0.f,1.f,values[i]/ceiling);
            if(i==0)line.startNewSubPath(x,y);else line.lineTo(x,y);
        }
        g.setColour(colour); g.strokePath(line,juce::PathStrokeType(2));
    };
    plot(s.grMain,green);
    if(s.midSide) plot(s.grSide,amber);
    text(g,s.midSide?juce::String::fromUTF8("━  Mid · centro"):s.channels==1?"Mono":"L/R enlazado",
         {112,598,155,21},font(14,look.face),green);
    if(s.midSide) text(g,juce::String::fromUTF8("━  Side · lados"),{263,598,150,21},font(14,look.face),amber);
    text(g,"Ahora",{819,598,107,21},font(13,look.face),phosphor,juce::Justification::centredRight);
}
}

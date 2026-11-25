#include "PluginProcessor.h"
#include "analysis/Features.h"
#include "calibration/Calibrator.h"
#include "ui/Editor.h"
#include <algorithm>
#include <cmath>

namespace ia2a {
Processor::Processor():AudioProcessor(BusesProperties()
    .withInput("Pista",juce::AudioChannelSet::stereo(),true)
    .withOutput("Salida",juce::AudioChannelSet::stereo(),true)
    .withInput("Referencia",juce::AudioChannelSet::stereo(),false)),
    provider(juce::SystemStats::getEnvironmentVariable("IA2A_TURN_URL","").isEmpty()?"local-demo":"http"),
    worker([this]{runWorker();}) {}
Processor::~Processor() {
    { std::lock_guard lock(control); closing=true; cancellation.request_stop(); }
    wake.notify_all(); worker.join();
}
bool Processor::isBusesLayoutSupported(const BusesLayout& l) const {
    auto main=l.getMainInputChannelSet();
    if(main!=l.getMainOutputChannelSet()||(main!=juce::AudioChannelSet::mono()&&main!=juce::AudioChannelSet::stereo()))return false;
    if(l.inputBuses.size()<2)return true;
    auto aux=l.getChannelSet(true,1);
    return aux.isDisabled()||aux==juce::AudioChannelSet::mono()||aux==juce::AudioChannelSet::stereo();
}
void Processor::invalidateLocked() {
    cancellation.request_stop(); job.reset(); clip.reset(); previousGoal=juce::var{}; history.clear();
    busy=pendingSend=false; session.invalidate(); compare.publish(session.monitor());
    knownRevision=capture.revision(); monitorRevision.store(knownRevision);
}
void Processor::prepareToPlay(double hz,int maximum) {
    std::lock_guard lock(control);
    channels=getMainBusNumInputChannels();
    capture.prepare(hz,channels,getChannelCountOfBus(true,1)); invalidateLocked();
    compare.prepare(hz,channels,maximum); expectedPosition.reset();previouslyPlaying=false;
    since=0; interval=std::max(1,int(hz/10));peakMain=peakSide=0;head=0;historySize=0;
}
void Processor::releaseResources() {
    std::lock_guard lock(control);capture.invalidate();invalidateLocked();
}
void Processor::processBlock(juce::AudioBuffer<float>& buffer,juce::MidiBuffer&) {
    juce::ScopedNoDenormals noDenormals;
    auto track=getBusBuffer(buffer,true,0);
    auto aux=getBusBuffer(buffer,true,1);
    const int count=track.getNumSamples(), n=track.getNumChannels();
    if(n<1 || n>2 || count==0)return;
    if(auto* play=getPlayHead()) if(auto p=play->getPosition()) {
        auto position=p->getTimeInSamples(); bool playing=p->getIsPlaying();
        const auto stage=capture.status();
        if(previouslyPlaying && (!playing || (position && expectedPosition && *position!=*expectedPosition))) {
            if(stage==Capture::Recording || stage==Capture::Armed) capture.invalidate();
            else capture.clearHistory();
        }
        if(playing && position)expectedPosition=*position+count; else expectedPosition.reset();
        previouslyPlaying=playing;
    }
    ConstBlockView dry{},ref{};AudioBlockView out{};
    dry.channels=out.channels=n;dry.samples=out.samples=ref.samples=count;
    ref.channels=std::min(2,aux.getNumChannels());
    for(int i=0;i<n;++i){dry.data[i]=track.getReadPointer(i);out.data[i]=track.getWritePointer(i);}
    for(int i=0;i<ref.channels;++i)ref.data[i]=aux.getReadPointer(i);
    capture.push(dry,ref);
    auxActive=ref.channels>0 && aux.getMagnitude(0,count)>0.00003f;
    meterIn=juce::Decibels::gainToDecibels(track.getMagnitude(0,count),-120.f);
    GrMeters gr{};
    compare.process(out,gr,monitorRevision.load()!=capture.revision());
    meterOut=juce::Decibels::gainToDecibels(track.getMagnitude(0,count),-120.f);
    ms=gr.midSide;meterGr=std::abs(gr.reductionDb);
    peakMain=std::max(peakMain,std::abs(gr.midSide?gr.midDb:gr.reductionDb));
    peakSide=std::max(peakSide,std::abs(gr.sideDb));
    since+=count;
    if(since>=interval) {
        int idx=head.load();
        mainHistory[idx]=peakMain;sideHistory[idx]=peakSide;
        head=(idx+1)%120;historySize=std::min(120,historySize.load()+1);
        since%=interval;peakMain=peakSide=0;
    }
}
void Processor::armLocked() {
    capture.arm(reference);invalidateLocked();
    message=juce::String::fromUTF8("Escuchando 12 segundos de la pista…");
}
void Processor::captureAudio() {
    std::lock_guard lock(control);
    if(capture.status()==Capture::Armed||capture.status()==Capture::Recording) {
        capture.invalidate();invalidateLocked();message="Captura cancelada.";
    } else armLocked();
}
void Processor::setSource(juce::String value) {
    if(value!="voice"&&value!="drums"&&value!="other")return;
    std::lock_guard lock(control);if(source==value)return;
    source=value;capture.invalidate();invalidateLocked();prompt.clear();
    message="Pulsa Escuchar para capturar la nueva fuente.";
}
void Processor::setReference(bool value) {
    std::lock_guard lock(control);if(reference==value)return;
    reference=value;capture.invalidate();invalidateLocked();
    message=value?"Asigna el bus auxiliar de mezcla y pulsa Escuchar.":"Pulsa Escuchar para analizar solo la pista.";
}
void Processor::queueLocked() {
    cancellation.request_stop();cancellation=std::stop_source{};
    Job j; j.ticket=session.begin();j.clip=clip;j.seed=session.seed();j.applied=session.applied();
    j.turn.requestId=juce::Uuid().toString();j.turn.source=source;j.turn.prompt=prompt;
    j.turn.channels=int(clip->track.size());j.turn.history=history;j.turn.previousGoal=previousGoal;
    j.turn.hasApplied=j.applied.enabled;j.turn.hasPending=session.pending();j.stop=cancellation.get_token();
    j.turn.appliedSettings=turnSettings(j.applied,j.turn.channels);
    if(j.turn.hasPending)j.turn.pendingSettings=turnSettings(j.seed,j.turn.channels);
    job=std::move(j);busy=true;pendingSend=false;message=juce::String::fromUTF8("Preparando una propuesta sobre tu pista…");wake.notify_one();
}
void Processor::submit(juce::String text) {
    text=text.trim().substring(0,500);if(text.isEmpty())return;
    std::lock_guard lock(control);prompt=text;
    if(!clip) {
        if(capture.status()==Capture::Idle) armLocked();
        pendingSend=true;message=juce::String::fromUTF8("Reproduce la pista. Al terminar la captura prepararé la propuesta.");
    } else queueLocked();
}
void Processor::runWorker() {
    Client client;
    std::uint64_t observedRevision=0;
    float lastProgress=-1;
    auto lastBlock=std::chrono::steady_clock::now();
    std::unique_lock lock(control);
    while(!closing) {
        wake.wait_for(lock,std::chrono::milliseconds(40),[this]{return closing||job.has_value();});
        if(closing)break;
        if(knownRevision!=capture.revision()) {
            invalidateLocked();message="Captura interrumpida. Reproduce un fragmento continuo y vuelve a escuchar.";
        }
        // Logic can suspend an insert once a short region and its tail end.
        // Finish the contiguous material already captured when callbacks cease.
        auto now=std::chrono::steady_clock::now();
        const auto progress=capture.progress();
        if(progress!=lastProgress || observedRevision!=capture.revision()) {
            lastProgress=progress; observedRevision=capture.revision(); lastBlock=now;
        } else if(capture.status()==Capture::Recording && now-lastBlock>std::chrono::seconds(2)) {
            capture.finishAvailable();
        }
        if(capture.status()==Capture::Ready) {
            if(auto result=capture.take()) {
                clip=std::make_shared<Clip>(std::move(*result));message=juce::String::fromUTF8("Fragmento listo. Describe el cambio que quieres oír.");
                if(pendingSend)queueLocked();
            }
        }
        if(!job)continue;
        Job j=std::move(*job);job.reset();lock.unlock();
        ClientReply reply;
        Calibration calibration;
        juce::String explanation;
        bool usable=false;
        try {
            auto f=analyse(*j.clip); usable=f.usable;
            if(!usable) explanation=juce::String::fromUTF8("La pista tiene poca señal útil. Captura un fragmento con voz o golpes.");
            else {
                j.turn.features=turnFeatures(f);
                reply=client.request(j.turn,j.stop);
                explanation=reply.text;
                if(reply.kind==ClientReply::Kind::goal && !j.stop.stop_requested()) {
                    calibration=calibrate(*j.clip,j.applied,j.seed,reply.goal,j.stop);
                    explanation=juce::String::fromUTF8(calibration.explanation.c_str());
                }
                if(!f.hasReferenceSignal && !j.clip->reference.empty())
                    explanation+=juce::String::fromUTF8(" · Referencia en silencio. Se ha analizado solo la pista.");
            }
        } catch(const std::exception&) {explanation="No se pudo preparar el ajuste. Puedes volver a pedirlo.";}
        lock.lock();
        if(j.stop.stop_requested()||!session.current(j.ticket)||j.clip->generation!=capture.revision())continue;
        if(reference&&j.clip->reference.empty())explanation+=juce::String::fromUTF8(" · Sin entrada de referencia. Análisis de la pista sola.");
        if(reply.kind==ClientReply::Kind::goal && calibration.outcome==Outcome::Candidate) {
            session.offer(j.ticket,calibration.settings,calibration.matchGainDb);
            auto monitor=session.monitor();session.listen(monitor.preview,matched);
            previousGoal=reply.wireGoal;
        } else session.finish(j.ticket);
        if(usable && (reply.kind==ClientReply::Kind::goal||reply.kind==ClientReply::Kind::question||reply.kind==ClientReply::Kind::keep)){
            history.add(j.turn.prompt.substring(0,220));history.add(explanation.substring(0,220));
            while(history.size()>4)history.remove(0);
        }
        busy=false;message=explanation;if(reply.provider.isNotEmpty())provider=reply.provider;compare.publish(session.monitor());
    }
}
void Processor::listen(bool preview,bool levelMatched) {
    std::lock_guard lock(control);matched=levelMatched;session.listen(preview,matched);compare.publish(session.monitor());
}
void Processor::apply() {
    std::lock_guard lock(control);
    if(session.apply()) {previousGoal=juce::var{};history.clear();message="Ajuste aplicado.";compare.publish(session.monitor());}
}
void Processor::discard() {
    std::lock_guard lock(control);cancellation.request_stop();job.reset();busy=false;pendingSend=false;
    session.discard();previousGoal=juce::var{};history.clear();message="Propuesta descartada.";compare.publish(session.monitor());
}
void Processor::undo() {
    std::lock_guard lock(control);if(session.undo()){cancellation.request_stop();job.reset();busy=false;
        previousGoal=juce::var{};history.clear();message="Has recuperado el ajuste anterior.";compare.publish(session.monitor());}
}
Snapshot Processor::snapshot() const {
    std::lock_guard lock(control);Snapshot s;
    s.source=source;s.prompt=prompt;s.message=message;s.provider=provider;s.reference=reference;
    s.auxActive=auxActive;s.recording=capture.status()==Capture::Armed||capture.status()==Capture::Recording;
    s.progress=capture.progress();s.busy=busy;s.pending=session.pending();s.canApply=session.canApply();
    s.canUndo=session.canUndo();s.preview=session.monitor().preview;s.matched=matched;s.hasClip=bool(clip);
    s.channels=channels;s.midSide=ms;s.inputDb=meterIn;s.outputDb=meterOut;s.reductionDb=meterGr;
    const int end=head.load(),length=historySize.load();
    for(int i=120-length;i<120;++i) {s.grMain[i]=mainHistory[(end+i)%120];s.grSide[i]=sideHistory[(end+i)%120];}
    return s;
}
juce::AudioProcessorEditor* Processor::createEditor(){return new Editor(*this);}
}
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter(){return new ia2a::Processor();}

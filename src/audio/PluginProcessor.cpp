#include "PluginProcessor.h"
#include "analysis/Features.h"
#include "calibration/Calibrator.h"
#include "ui/Editor.h"
#include "assist/TrackSource.h"
#include "assist/DirectRequest.h"
#include <algorithm>
#include <cmath>

namespace ia2a {
Processor::Processor(bool useDemo):AudioProcessor(BusesProperties()
    .withInput("Pista",juce::AudioChannelSet::stereo(),true)
    .withOutput("Salida",juce::AudioChannelSet::stereo(),true)
    .withInput("Referencia",juce::AudioChannelSet::stereo(),false)),
    provider(useDemo?"local-demo":juce::SystemStats::getEnvironmentVariable("IA2A_TURN_URL","").isEmpty()?"disconnected":"http"),
    demoMode(useDemo),
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
void Processor::invalidateLocked(bool resetConversation) {
    cancellation.request_stop(); job.reset(); clip.reset(); previousResult=juce::var{};
    if(resetConversation) {
        previousGoal=juce::var{};
        lastAction.clear();
        history.clear();
    }
    busy=pendingSend=needsClarification=turnFailed=usesPreviousProposal=false; session.invalidate(); compare.publish(session.monitor());
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
void Processor::updateTrackProperties(const TrackProperties& properties) {
    const auto name = properties.name.value_or(juce::String{}).trim();
    const auto detected = inferTrackSource(name);
    std::lock_guard lock(control);
    trackName = name;
    if(inferredSource==detected) return;
    inferredSource=detected;
    if(source=="auto") {
        capture.invalidate();
        invalidateLocked(true);
        prompt.clear();
        message=juce::String::fromUTF8("Fuente actualizada. Pulsa Escuchar para capturar la pista.");
    }
}
void Processor::setSource(juce::String value) {
    if(!isTrackSourceSelection(value))return;
    std::lock_guard lock(control);if(source==value)return;
    source=value;capture.invalidate();invalidateLocked(true);prompt.clear();
    message="Pulsa Escuchar para capturar la nueva fuente.";
}
void Processor::setReference(bool value) {
    std::lock_guard lock(control);if(reference==value)return;
    reference=value;capture.invalidate();invalidateLocked();
    message=value?"Asigna el bus auxiliar de mezcla y pulsa Escuchar.":"Pulsa Escuchar para analizar solo la pista.";
}
void Processor::queueLocked(std::optional<Goal> direct) {
    cancellation.request_stop();
    cancellation=std::stop_source{};
    Job j;
    j.ticket=session.begin();
    j.clip=clip;
    j.captureRevision=capture.revision();
    j.direct=std::move(direct);
    const auto monitor=session.monitor();
    j.applied=monitor.applied;
    j.seed=monitor.preview?monitor.candidate:monitor.applied;
    j.turn.auditioning=monitor.preview?"pending":"applied";
    j.turn.requestId=juce::Uuid().toString();
    j.turn.source=source=="auto"?inferredSource:source;
    j.turn.sourceOrigin=source!="auto"?"user":inferredSource!="other"?"host-name":"unknown";
    j.turn.prompt=prompt;
    j.turn.channels=clip?int(clip->track.size()):std::clamp(getMainBusNumInputChannels(),1,2);
    j.turn.history=history;
    j.turn.previousGoal=previousGoal;
    j.turn.previousResult=previousResult;
    j.turn.lastAction=lastAction;
    j.turn.hasApplied=j.applied!=Settings::transparent();
    j.turn.hasPending=monitor.hasCandidate;
    j.stop=cancellation.get_token();
    j.turn.appliedSettings=turnSettings(j.applied,j.turn.channels);
    if(j.turn.hasPending)j.turn.pendingSettings=turnSettings(monitor.candidate,j.turn.channels);
    job=std::move(j);
    busy=true;
    pendingSend=needsClarification=turnFailed=usesPreviousProposal=false;
    message=juce::String::fromUTF8("Preparando una propuesta sobre tu pista…");
    wake.notify_one();
}
void Processor::submit(juce::String text) {
    text=text.trim().substring(0,500);
    if(text.isEmpty()) return;
    auto direct=directRequest(text);
    std::lock_guard lock(control);
    prompt=text;
    const auto captureState=capture.status();
    const bool capturing=captureState==Capture::Armed || captureState==Capture::Recording;
    if(!clip && (!direct || direct->compensateGain || capturing)) {
        if(captureState==Capture::Idle) armLocked();
        pendingSend=true;
        message=juce::String::fromUTF8("Reproduce la pista. Al terminar la captura prepararé la propuesta.");
    } else {
        queueLocked(std::move(direct));
    }
}
void Processor::runWorker() {
    Client client(demoMode);
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
                if(pendingSend)queueLocked(directRequest(prompt));
            }
        }
        if(!job)continue;
        Job j=std::move(*job);job.reset();lock.unlock();
        ClientReply reply;
        Calibration calibration;
        juce::String explanation;
        bool attempted=false;
        try {
            if(j.direct) {
                Clip empty;
                empty.track.resize(static_cast<size_t>(j.turn.channels));
                attempted=true;
                reply.kind=ClientReply::Kind::goal;
                reply.goal=*j.direct;
                reply.wireGoal=encodeGoal(reply.goal);
                calibration=calibrate(j.clip?*j.clip:empty,j.applied,j.seed,reply.goal,j.stop);
                explanation=juce::String::fromUTF8(calibration.explanation.c_str());
            } else if(j.clip) {
                auto features=analyse(*j.clip);
                attempted=features.usable;
                if(!attempted) explanation=juce::String::fromUTF8("Reproduce un fragmento con señal para analizar el cambio que pides.");
                else {
                    j.turn.features=turnFeatures(features);
                    reply=client.request(j.turn,j.stop);
                    explanation=reply.text;
                    if(reply.kind==ClientReply::Kind::goal && !j.stop.stop_requested()) {
                        calibration=calibrate(*j.clip,j.applied,j.seed,reply.goal,j.stop);
                        explanation=juce::String::fromUTF8(calibration.explanation.c_str());
                        const bool reportsActualControls=!reply.goal.edits.empty() || reply.goal.compensateGain ||
                            std::any_of(reply.goal.priorities.begin(),reply.goal.priorities.end(),[](Priority priority) {
                                return priority.objective==Objective::BassWeight;
                            });
                        if(reply.provider=="claude" && calibration.outcome==Outcome::Candidate && !reportsActualControls)
                            explanation=reply.text;
                    }
                    if(!features.hasReferenceSignal && !j.clip->reference.empty())
                        explanation+=juce::String::fromUTF8(" · Referencia en silencio. Se ha analizado solo la pista.");
                }
            }
        } catch(const std::exception&) {
            attempted=true;
            reply.kind=ClientReply::Kind::error;
            explanation=juce::String::fromUTF8("IA2A no pudo preparar este ajuste. Puedes reintentarlo con la misma petición.");
        }
        lock.lock();
        if(j.stop.stop_requested()||!session.current(j.ticket)||j.captureRevision!=capture.revision())continue;
        if(reference&&j.clip&&j.clip->reference.empty())explanation+=juce::String::fromUTF8(" · Sin entrada de referencia. Análisis de la pista sola.");
        const bool candidate=reply.kind==ClientReply::Kind::goal && calibration.outcome==Outcome::Candidate;
        if(candidate && session.offer(j.ticket,calibration.settings,calibration.matchGainDb)) {
            lastAction.clear();
            auto monitor=session.monitor();session.listen(monitor.preview,matched);
        } else {
            if(candidate) {
                reply.kind=ClientReply::Kind::error;
                explanation=juce::String::fromUTF8("IA2A no pudo preparar los valores de este ajuste. Puedes reintentar la consulta.");
            }
            session.finish(j.ticket);
            usesPreviousProposal=session.pending();
        }
        if(reply.kind==ClientReply::Kind::goal) {
            previousGoal=reply.wireGoal;
            previousResult=turnResult(calibration);
        }
        if(attempted && (reply.kind==ClientReply::Kind::goal||reply.kind==ClientReply::Kind::question||reply.kind==ClientReply::Kind::keep)){
            history.add(j.turn.prompt.substring(0,400));
            history.add(explanation.substring(0,400));
            auto historyBytes = [&] {
                size_t bytes=0;
                for(const auto& line:history) bytes+=line.getNumBytesAsUTF8();
                return bytes;
            };
            while(history.size()>12 || historyBytes()>6000) {
                history.remove(0);
                history.remove(0);
            }
        }
        needsClarification=reply.kind==ClientReply::Kind::question ||
            (reply.kind==ClientReply::Kind::goal && calibration.outcome==Outcome::Clarify);
        turnFailed=attempted && reply.kind==ClientReply::Kind::error;
        busy=false;message=explanation;if(reply.provider.isNotEmpty())provider=reply.provider;compare.publish(session.monitor());
    }
}
void Processor::listen(bool preview,bool levelMatched) {
    std::lock_guard lock(control);matched=levelMatched;session.listen(preview,matched);compare.publish(session.monitor());
}
void Processor::apply() {
    std::lock_guard lock(control);
    if(session.apply()) {needsClarification=turnFailed=usesPreviousProposal=false;lastAction="applied";message="Ajuste aplicado.";compare.publish(session.monitor());}
}
void Processor::discard() {
    std::lock_guard lock(control);cancellation.request_stop();job.reset();busy=false;pendingSend=false;
    session.discard();needsClarification=turnFailed=usesPreviousProposal=false;lastAction="discarded";message="Propuesta descartada.";compare.publish(session.monitor());
}
void Processor::undo() {
    std::lock_guard lock(control);if(session.undo()){cancellation.request_stop();job.reset();busy=needsClarification=turnFailed=usesPreviousProposal=false;
        lastAction="undone";message="Has recuperado el ajuste anterior.";compare.publish(session.monitor());}
}
Snapshot Processor::snapshot() const {
    std::lock_guard lock(control);Snapshot s;
    s.source=source;s.effectiveSource=source=="auto"?inferredSource:source;s.trackName=trackName;s.prompt=prompt;s.message=message;s.provider=provider;s.reference=reference;
    s.auxActive=auxActive;s.recording=capture.status()==Capture::Armed||capture.status()==Capture::Recording;
    s.progress=capture.progress();s.busy=busy;s.pending=session.pending();s.canApply=session.canApply();
    s.needsClarification=needsClarification;s.turnFailed=turnFailed;s.usesPreviousProposal=usesPreviousProposal;
    s.canUndo=session.canUndo();s.preview=session.monitor().preview;s.matched=matched;s.hasClip=bool(clip);
    s.channels=channels;s.midSide=ms;s.inputDb=meterIn;s.outputDb=meterOut;s.reductionDb=meterGr;
    const int end=head.load(),length=historySize.load();
    for(int i=120-length;i<120;++i) {s.grMain[i]=mainHistory[(end+i)%120];s.grSide[i]=sideHistory[(end+i)%120];}
    return s;
}
juce::AudioProcessorEditor* Processor::createEditor(){return new Editor(*this);}
}
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter(){return new ia2a::Processor();}

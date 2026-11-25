#include "assist/Client.h"
#include "assist/Session.h"
#include "calibration/Calibrator.h"
#include "dsp/OpticalPath.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>

using namespace ia2a;
namespace fs=std::filesystem;
namespace {
using Samples=std::vector<std::vector<float>>;
using juce::var;
var object() { return new juce::DynamicObject(); }
void put(var& obj,const char* key,var value) { obj.getDynamicObject()->setProperty(key,std::move(value)); }
juce::File file(const fs::path& p) {return juce::File(juce::String::fromUTF8(fs::absolute(p).string().c_str()));}

Samples readWave(const fs::path& path,double& rate,int requiredFrames=0) {
    juce::WavAudioFormat wav;
    auto stream=file(path).createInputStream();
    if(!stream) throw std::runtime_error("No se pudo abrir "+path.string());
    std::unique_ptr<juce::AudioFormatReader> reader(wav.createReaderFor(stream.release(),true));
    if(!reader || reader->lengthInSamples<1 || reader->numChannels<1 || reader->numChannels>2 ||
       !std::isfinite(reader->sampleRate) || reader->sampleRate<1000 || reader->sampleRate>768000)
        throw std::runtime_error("Se requiere un WAV mono o estéreo válido.");
    if(requiredFrames && std::abs(rate-reader->sampleRate)>.01)
        throw std::runtime_error("La pista y su referencia deben tener la misma frecuencia de muestreo.");
    rate=reader->sampleRate;
    const int length=requiredFrames?requiredFrames:static_cast<int>(std::min<juce::int64>(reader->lengthInSamples,std::llround(rate*12)));
    Samples samples(reader->numChannels,std::vector<float>(length));
    std::array<float*,2> channels{samples[0].data(),samples.size()==2?samples[1].data():nullptr};
    juce::AudioBuffer<float> buffer(channels.data(),static_cast<int>(samples.size()),length);
    const int available=static_cast<int>(std::min<juce::int64>(length,reader->lengthInSamples));
    if(!reader->read(&buffer,0,available,0,true,true)) throw std::runtime_error("Lectura WAV incompleta.");
    return samples;
}
void writeWave(const fs::path& path,const Samples& samples,double rate) {
    juce::WavAudioFormat format;
    std::unique_ptr<juce::OutputStream> output=file(path).createOutputStream();
    if(!output) throw std::runtime_error("No se pudo crear el WAV.");
    // Floating point also preserves peaks above unity in a level-matched audition.
    auto options=juce::AudioFormatWriterOptions{}.withSampleRate(rate).withNumChannels(static_cast<int>(samples.size()))
        .withBitsPerSample(32).withSampleFormat(juce::AudioFormatWriterOptions::SampleFormat::floatingPoint);
    auto writer=format.createWriterFor(output,options);
    std::array<const float*,2> data{samples[0].data(),samples.size()==2?samples[1].data():nullptr};
    if(!writer || !writer->writeFromFloatArrays(data.data(),static_cast<int>(samples.size()),static_cast<int>(samples.front().size())) || !writer->flush())
        throw std::runtime_error("No se pudo escribir el WAV completo.");
}
var domainReport(const DomainSettings& s) {
    auto out=object();
    put(out,"thresholdDb",s.thresholdDb);put(out,"ratio",s.ratio);put(out,"attackMs",s.attackMs);
    put(out,"fastReleaseMs",s.fastReleaseMs);put(out,"slowReleaseMs",s.slowReleaseMs);put(out,"memory",s.memory);
    put(out,"kneeDb",s.kneeDb);put(out,"hpfOn",s.hpfOn);put(out,"hpfHz",s.hpfHz);put(out,"airDb",s.airDb);put(out,"makeupDb",s.makeupDb);
    return out;
}
var settingsReport(const Settings& s) {
    auto out=object();
    put(out,"enabled",s.enabled);put(out,"midSide",s.midSide);put(out,"inputTrimDb",s.inputTrimDb);put(out,"outputGainDb",s.outputGainDb);
    put(out,"mid",domainReport(s.mid));put(out,"side",domainReport(s.side));return out;
}
const char* decision(Outcome o) {
    switch(o) {
        case Outcome::Candidate:return "candidate";case Outcome::Keep:return "keep";
        case Outcome::Clarify:return "question";case Outcome::Cancelled:return "cancelled";
    }
    return "error";
}
std::map<std::string,std::string> options(int argc,char** argv) {
    std::map<std::string,std::string> args;
    const std::set<std::string> allowed{"--track","--ref","--source","--prompt","--out"};
    for(int i=1;i<argc;i+=2) {
        if(i+1>=argc || !allowed.contains(argv[i]) || !args.emplace(argv[i],argv[i+1]).second)
            throw std::runtime_error("Argumento inválido. Consulta --help.");
    }
    for(const auto opt:{"--track","--out","--prompt"}) if(!args.contains(opt)||args[opt].empty())
        throw std::runtime_error(std::string("Falta ")+opt);
    return args;
}
}

int main(int argc,char** argv) try {
    if(argc==2 && std::string(argv[1])=="--help") {
        std::cout<<"ia2a_eval --track toma.wav [--ref base.wav] [--source voice|drums|other] --prompt texto --out directorio\n"
            <<"Evalúa los primeros 12 s. La carpeta de salida debe ser nueva o estar vacía.\n";
        return 0;
    }
    auto args=options(argc,argv);
    const fs::path out=fs::absolute(args.at("--out"));
    if(fs::exists(out) && (!fs::is_directory(out)||!fs::is_empty(out))) throw std::runtime_error("La carpeta de salida ya contiene archivos.");
    Clip clip;
    clip.track=readWave(args.at("--track"),clip.sampleRate);
    if(args.contains("--ref")) clip.reference=readWave(args.at("--ref"),clip.sampleRate,static_cast<int>(clip.track[0].size()));
    Turn turn;
    turn.channels=static_cast<int>(clip.track.size());
    turn.source=args.contains("--source")?juce::String(args["--source"]):(turn.channels==1?"voice":"other");
    if(turn.source!="voice"&&turn.source!="drums"&&turn.source!="other") throw std::runtime_error("Fuente desconocida.");
    turn.requestId="eval-1";turn.prompt=juce::String::fromUTF8(args["--prompt"].c_str());
    const auto input=analyse(clip);
    turn.features=turnFeatures(input);
    if(!input.usable) throw std::runtime_error("Se requiere más señal útil en la captura.");
    const auto begin=std::chrono::steady_clock::now();
    Client client;
    const auto reply=client.request(turn);
    if(reply.kind==ClientReply::Kind::error || reply.kind==ClientReply::Kind::cancelled)
        throw std::runtime_error(reply.text.toStdString());
    Session session;
    auto ticket=session.begin();
    Calibration cal;
    cal.before=cal.after=input;
    auto result=reply.kind==ClientReply::Kind::keep?"keep":"question";
    juce::String explanation=reply.text;
    if(reply.kind==ClientReply::Kind::goal) {
        cal=calibrate(clip,session.applied(),session.seed(),reply.goal);
        result=decision(cal.outcome); explanation=juce::String::fromUTF8(cal.explanation.c_str());
        if(cal.outcome==Outcome::Candidate && !session.offer(ticket,cal.settings,cal.matchGainDb))
            throw std::runtime_error("No se pudo preparar la propuesta.");
    }
    const auto elapsed=std::chrono::duration<double>(std::chrono::steady_clock::now()-begin).count();
    auto report=object();
    put(report,"provider",reply.provider);put(report,"decision",result);put(report,"goal",reply.wireGoal);
    put(report,"source",turn.source);put(report,"explanation",explanation);put(report,"sampleRate",clip.sampleRate);
    put(report,"frames",static_cast<int>(clip.track[0].size()));put(report,"elapsedSeconds",elapsed);
    put(report,"renders",cal.renders);put(report,"matchGainDb",cal.matchGainDb);put(report,"applied",session.applied().enabled);
    put(report,"input",turn.features);put(report,"before",turnFeatures(cal.before));put(report,"after",turnFeatures(cal.after));
    put(report,"settings",settingsReport(session.seed()));
    std::array<float*,2> data{clip.track[0].data(),clip.track.size()==2?clip.track[1].data():nullptr};
    OpticalPath path;path.prepare(clip.sampleRate,turn.channels);path.setSettings(session.seed());
    const int frames=static_cast<int>(clip.track.front().size());
    GrMeters gr{};
    for(int n=0;n<frames;n+=512) {
        AudioBlockView block{{data[0]+n,data[1]?data[1]+n:nullptr},turn.channels,std::min(512,frames-n)};
        path.process(block,gr);
    }
    fs::create_directories(out.parent_path());
    const fs::path stage=out.parent_path()/(".ia2a-"+juce::Uuid().toString().toStdString());
    fs::create_directory(stage);
    try {
        writeWave(stage/"nivel-final.wav",clip.track,clip.sampleRate);
        const float gain=std::pow(10.f,cal.matchGainDb/20.f);
        for(auto& channel:clip.track) for(auto& sample:channel) sample*=gain;
        writeWave(stage/"volumen-igualado.wav",clip.track,clip.sampleRate);
        if(!file(stage/"informe.json").replaceWithText(juce::JSON::toString(report,true)))
            throw std::runtime_error("No se pudo escribir el informe.");
        fs::rename(stage,out);
    } catch(...) { fs::remove_all(stage); throw; }
    std::cout<<out.string()<<" ("<<result<<", "<<cal.renders<<" renders)\n";
    return 0;
} catch(const std::exception& e) { std::cerr<<"IA2A: "<<e.what()<<'\n';return 1; }

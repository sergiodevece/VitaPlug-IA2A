#include "PluginProcessor.h"

#include <algorithm>
#include <array>

namespace ia2a {
namespace {
constexpr int signature = 0x49413241, version = 2;
// Keep the first three codes compatible with version 1. UI ordering is independent.
constexpr std::array<const char*, 14> storedSources{
    "voice", "drums", "other", "auto", "kick", "snare", "percussion",
    "bass", "guitar", "keys", "synth", "strings", "brass", "mix"
};
void writeDomain(juce::MemoryOutputStream& out,const DomainSettings& d) {
    for(float value:{d.thresholdDb,d.ratio,d.attackMs,d.fastReleaseMs,d.slowReleaseMs,
                     d.memory,d.kneeDb,d.hpfHz,d.airDb,d.makeupDb}) out.writeFloat(value);
    out.writeBool(d.hpfOn);
}
DomainSettings readDomain(juce::MemoryInputStream& in) {
    DomainSettings d;
    for(auto* field:{&d.thresholdDb,&d.ratio,&d.attackMs,&d.fastReleaseMs,&d.slowReleaseMs,
                     &d.memory,&d.kneeDb,&d.hpfHz,&d.airDb,&d.makeupDb}) *field=in.readFloat();
    d.hpfOn=in.readBool();
    return d;
}
}
void Processor::getStateInformation(juce::MemoryBlock& data) {
    std::lock_guard lock(control);
    auto saved=session.applied();
    juce::MemoryOutputStream out(data,false);
    const auto selection = std::find_if(storedSources.begin(), storedSources.end(),
                                        [&](const char* id) { return source == id; });
    out.writeInt(signature);
    out.writeInt(version);
    out.writeByte(selection == storedSources.end() ? 2 : static_cast<char>(selection - storedSources.begin()));
    out.writeBool(reference);out.writeBool(saved.enabled);out.writeBool(saved.midSide);
    out.writeFloat(saved.inputTrimDb);out.writeFloat(saved.outputGainDb);
    writeDomain(out,saved.mid);writeDomain(out,saved.side);
}
void Processor::setStateInformation(const void* bytes,int size) {
    constexpr int serializedSize=2*4+4+2*4+2*(10*4+1);
    if(!bytes||size!=serializedSize)return;
    juce::MemoryInputStream in(bytes,size,false);
    if (in.readInt() != signature) return;
    const auto storedVersion = in.readInt();
    if (storedVersion != 1 && storedVersion != version) return;
    const int inputSource = in.readByte();
    const int sourceCount = storedVersion == 1 ? 3 : static_cast<int>(storedSources.size());
    if (inputSource < 0 || inputSource >= sourceCount) return;
    const bool ref=in.readBool();
    Settings saved;
    saved.enabled=in.readBool();saved.midSide=in.readBool();
    saved.inputTrimDb=in.readFloat();saved.outputGainDb=in.readFloat();
    saved.mid=readDomain(in);saved.side=readDomain(in);
    if(!validSettings(saved))return;
    std::lock_guard lock(control);
    capture.invalidate();invalidateLocked(true);
    session.restore(saved);source=storedSources[static_cast<std::size_t>(inputSource)];
    reference=ref; matched=true;prompt.clear();
    message=juce::String::fromUTF8("Ajuste de la sesión recuperado.");compare.publish(session.monitor());
}
}

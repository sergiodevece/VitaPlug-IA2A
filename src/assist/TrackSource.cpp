#include "TrackSource.h"

#include <algorithm>
#include <array>
#include <initializer_list>

namespace ia2a {
namespace {
constexpr std::array options{
    TrackSourceOption{"auto", "Automática"},
    TrackSourceOption{"voice", "Voz"},
    TrackSourceOption{"drums", "Bus de batería"},
    TrackSourceOption{"kick", "Bombo"},
    TrackSourceOption{"snare", "Caja"},
    TrackSourceOption{"percussion", "Percusión"},
    TrackSourceOption{"bass", "Bajo"},
    TrackSourceOption{"guitar", "Guitarra"},
    TrackSourceOption{"keys", "Piano y teclas"},
    TrackSourceOption{"synth", "Sintetizador"},
    TrackSourceOption{"strings", "Cuerdas"},
    TrackSourceOption{"brass", "Metales"},
    TrackSourceOption{"mix", "Mezcla / máster"},
    TrackSourceOption{"other", "Otra fuente"}
};

juce::String trackWords(const juce::String& name) {
    const auto folded = name.toLowerCase().replaceCharacters(juce::String::fromUTF8("áéíóúüñ"), "aeiouun");
    juce::String words{" "};
    bool gap = true;
    bool previousWasDigit = false;
    for (auto ch : folded) {
        if (ch >= 0x0300 && ch <= 0x036f) continue; // Combining accents in decomposed track names.
        if (juce::CharacterFunctions::isLetterOrDigit(ch)) {
            const bool digit = juce::CharacterFunctions::isDigit(ch);
            if (!gap && digit != previousWasDigit) words += " ";
            words += ch;
            gap = false;
            previousWasDigit = digit;
        } else if (!gap) {
            words += " ";
            gap = true;
        }
    }
    if (!gap) words += " ";
    return words;
}
}

std::span<const TrackSourceOption> trackSourceOptions() noexcept {
    return options;
}

bool isTrackSourceSelection(const juce::String& source) {
    return std::any_of(options.begin(), options.end(), [&](auto row) { return source == row.id; });
}

juce::String trackSourceLabel(const juce::String& source) {
    const auto match = std::find_if(options.begin(), options.end(), [&](auto row) { return source == row.id; });
    return juce::String::fromUTF8((match == options.end() ? options.back() : *match).label);
}

juce::String inferTrackSource(const juce::String& trackName) {
    const auto words = trackWords(trackName);
    const auto has = [&](std::initializer_list<const char*> aliases) {
        return std::any_of(aliases.begin(), aliases.end(), [&](const char* alias) {
            return words.contains(juce::String(" ") + alias + " ");
        });
    };

    if (has({"voz", "voces", "vocal", "vocals", "vox", "leadvox", "voice", "voiceover", "coro", "coros", "choir", "rap"}))
        return "voice";

    const bool kick = has({"kick", "kicks", "bombo", "bombos", "bd"});
    const bool snare = has({"snare", "snares", "caja", "cajas", "tarola", "redoblante"});
    if ((kick && snare) || has({"drum bus", "drum kit", "bus drums", "drums bus", "bus de bateria", "overheads"}))
        return "drums";
    if (kick)  return "kick";
    if (snare) return "snare";
    if (has({"drums", "drum", "drumkit", "bateria", "baterias", "drummer"}))
        return "drums";
    if (has({"perc", "percussion", "percusion", "tom", "toms", "hihat", "hihats", "hi hat", "hi hats", "hat", "hats", "cymbal", "cymbals", "plato", "platos", "shaker", "conga"}))
        return "percussion";
    if (has({"bass", "bassline", "bajo", "bajos", "contrabajo"})) return "bass";
    if (has({"guitar", "guitars", "gtr", "guitarra", "guitarras"})) return "guitar";
    if (has({"synth", "synths", "synthesiser", "synthesizer", "sintetizador", "pad", "pads"})) return "synth";
    if (has({"piano", "keys", "keyboards", "keyboard", "teclado", "teclas", "organ", "rhodes", "wurli"})) return "keys";
    if (has({"strings", "violin", "violines", "viola", "violas", "chelo", "cellos", "cello", "cuerdas"})) return "strings";
    if (has({"brass", "horn", "horns", "sax", "saxo", "saxophone", "saxofon", "trumpet", "trompeta", "trombone", "trombon", "metales"})) return "brass";
    if (has({"mix", "mezcla", "master", "stereo out", "salida estereo", "two bus", "2 bus"})) return "mix";
    return "other";
}
}

#include "assist/DirectRequest.h"

#include <cmath>
#include <iostream>
#include <stdexcept>

using namespace ia2a;

namespace {
Goal parse(const char* text, Control control, ControlChange change,
           std::optional<float> value = std::nullopt, ControlDomain domain = ControlDomain::Both) {
    auto result = directRequest(juce::String::fromUTF8(text));
    if (!result || result->edits.size() != 1)
        throw std::runtime_error(std::string("Expected one explicit edit: ") + text);
    const auto& edit = result->edits.front();
    if (edit.control != control || edit.change != change || edit.domain != domain
        || edit.value.has_value() != value.has_value() || (value && std::abs(*edit.value - *value) > 0.001f))
        throw std::runtime_error(std::string("Unexpected edit: ") + text);
    return *result;
}
}

int main() {
    try {
        const auto mixed = parse("quiero el umbral más bajo, que se note más la compresión y equilibra con el gain",
                                 Control::Threshold, ControlChange::Decrease);
        if (!mixed.compensateGain) throw std::runtime_error("Expected measured gain compensation");

        parse("Baja el umbral a −24 dB", Control::Threshold, ControlChange::Set, -24);
        parse("¿Puedes bajar el umbral 3 dB?", Control::Threshold, ControlChange::Decrease, 3);
        parse("Baja el umbral 3 dB", Control::Threshold, ControlChange::Decrease, 3);
        parse("Pon el umbral en -19,5 dB", Control::Threshold, ControlChange::Set, -19.5f);
        parse("Lower the side threshold by 2 dB", Control::Threshold, ControlChange::Decrease, 2, ControlDomain::Side);
        parse("Baja 3 dB el umbral del Mid", Control::Threshold, ControlChange::Decrease, 3, ControlDomain::Main);
        parse("Set the ratio to 4:1", Control::Ratio, ControlChange::Set, 4);
        parse("Pon el ataque en 5 ms", Control::Attack, ControlChange::Set, 5);
        parse("Aumenta el release lento a 1,5 segundos", Control::SlowRelease, ControlChange::Set, 1500);
        parse("Sube la ganancia de salida 1 dB", Control::OutputGain, ControlChange::Increase, 1);

        parse("Activa el filtro detector", Control::DetectorEnabled, ControlChange::Set, 1);
        parse("Desactiva el sidechain HPF", Control::DetectorEnabled, ControlChange::Set, 0);
        parse("SC HPF a 100 Hz", Control::DetectorFrequency, ControlChange::Set, 100);
        parse("Ponlo en M/S", Control::StereoMode, ControlChange::Set, 1);
        parse("Ponlo en modo L/R", Control::StereoMode, ControlChange::Set, 0);
        parse("Desactiva el compresor", Control::Enabled, ControlChange::Set, 0);

        const auto filter = directRequest("Activa el SC HPF a 120 Hz");
        if (!filter || filter->edits.size() != 2 || filter->edits.front().control != Control::DetectorFrequency
            || filter->edits.front().value != 120 || filter->edits.back().control != Control::DetectorEnabled
            || filter->edits.back().value != 1)
            throw std::runtime_error("Expected both the cutoff and filter enable edits");

        const auto compound = directRequest("Baja el umbral 3 dB y pon ataque a 10 ms");
        if (!compound || compound->edits.size() != 2)
            throw std::runtime_error("Expected both explicitly requested controls");

        for (auto* ambiguous : {"¿Qué hace el umbral?", "¿El umbral más bajo?", "Baja el umbral. Dame más pegada.", "Si bajamos el umbral, ¿qué ocurriría?",
                                "No bajes el umbral", "Baja el umbral y no toques las colas",
                                "Necesito más ataque en la voz", "Déjalo respirar",
                                "Baja el umbral y suaviza la cola de los platos",
                                "El umbral ya está muy bajo", "Baja el umbral y sube el umbral",
                                "Baja 3 dB el umbral pero sube 5 dB", "Desactiva el SC HPF a 120 Hz",
                                "Baja el umbral a -5 Hz", "Deshaz lo último"}) {
            if (directRequest(juce::String::fromUTF8(ambiguous)))
                throw std::runtime_error(std::string("Unsafe or ambiguous edit: ") + ambiguous);
        }
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
    return 0;
}

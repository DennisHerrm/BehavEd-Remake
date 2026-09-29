#include "bhed/efx/effect.h"


#include <cctype>
#include <string_view>

namespace bhed::efx {
namespace {

bool iequals(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) { return false;
}
    for (size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i]))) {
            return false;
}
    }
    return true;
}

struct TypeInfo {
    PrimitiveType type;
    const char* name;
    // efxed traegt hier noch eine Beschreibung fuer seine Oberflaeche mit.
    // behaved zeigt keine Effektfelder an - es zeichnet nur. Also weg
    // damit, statt eine zweite Uebersetzungstabelle mitzuschleppen.
};

// Reihenfolge und Beschreibungen wie im Originaleditor.
const TypeInfo kTypes[] = {
    {PrimitiveType::Particle, "Particle"},
    {PrimitiveType::Line, "Line"},
    {PrimitiveType::Tail, "Tail"},
    {PrimitiveType::Cylinder, "Cylinder"},
    {PrimitiveType::Emitter, "Emitter"},
    {PrimitiveType::Sound, "Sound"},
    {PrimitiveType::Decal, "Decal"},
    {PrimitiveType::OrientedParticle, "OrientedParticle"},
    {PrimitiveType::Electricity, "Electricity"},
    {PrimitiveType::FxRunner, "FxRunner"},
    {PrimitiveType::Light, "Light"},
    {PrimitiveType::CameraShake, "CameraShake"},
    {PrimitiveType::ScreenFlash, "Flash"},
};

}  // namespace

const char* typeName(PrimitiveType t) {
    for (const auto& info : kTypes) {
        if (info.type == t) { return info.name;
}
    }
    return "Particle";
}

std::optional<PrimitiveType> typeFromName(std::string_view name) {
    for (const auto& info : kTypes) {
        if (iequals(name, info.name)) { return info.type;
}
    }
    // Das Spiel kennt "cameraShake" auch klein; iequals deckt das ab.
    return std::nullopt;
}

const char* dialectName(Dialect d) {
    switch (d) {
        case Dialect::SP: return "Singleplayer";
        case Dialect::MP: return "Multiplayer";
        default: return "beide";
    }
}

const std::vector<FlagName>& flagNames() {
    static const std::vector<FlagName> kNames = {
        {"useModel", kFlagAttachedModel, Dialect::Both, ""},
        {"useBBox", kFlagUseBBox, Dialect::Both, ""},
        {"usePhysics", kFlagApplyPhysics, Dialect::Both, ""},
        {"expensivePhysics", kFlagExpensivePhysics, Dialect::Both, ""},
        // ghoul2Collision setzt drei Bits auf einmal - beim Zurueckschreiben
        // muss es deshalb vor den Einzelflags geprueft werden.
        {"ghoul2Collision",
         kFlagGhoul2Trace | kFlagApplyPhysics | kFlagExpensivePhysics,
         Dialect::Both, "teilt sich das Bit mit der size2-Kurve"},
        {"ghoul2Decals", kFlagGhoul2Decals, Dialect::Both,
         "teilt sich das Bit mit der size2-Kurve"},
        {"impactKills", kFlagKillOnImpact, Dialect::Both, ""},
        {"impactFx", kFlagImpactRunsFx, Dialect::Both, ""},
        {"deathFx", kFlagDeathRunsFx, Dialect::Both, ""},
        {"useAlpha", kFlagUseAlpha, Dialect::Both, ""},
        {"emitFx", kFlagEmitFx, Dialect::Both, ""},
        {"depthHack", kFlagDepthHack, Dialect::Both, ""},
        {"setShaderTime", kFlagSetShaderTime, Dialect::Both, ""},

        // Nur Multiplayer.
        {"relative", kFlagRelative, Dialect::MP, ""},
        {"paperPhysics", kFlagPaperPhysics, Dialect::MP,
         "nur Emitter, teilt sich das Bit mit size2 linear"},
        {"localizedFlash", kFlagLocalizedFlash, Dialect::MP,
         "nur Flash, teilt sich das Bit mit size2 linear"},
        {"playerView", kFlagPlayerView, Dialect::MP,
         "nur Spieleransicht, teilt sich das Bit mit size2 linear"},
    };
    return kNames;
}

const FlagName* findFlag(const std::vector<FlagName>& table,
                         std::string_view name) {
    for (const auto& entry : table) {
        if (iequals(name, entry.name)) { return &entry;
}
    }
    return nullptr;
}

const std::vector<FlagName>& spawnFlagNames() {
    static const std::vector<FlagName> kNames = {
        {"org2fromTrace", kSpawnOrg2FromTrace, Dialect::Both, ""},
        {"traceImpactFx", kSpawnTraceImpactFx, Dialect::Both, ""},
        {"org2isOffset", kSpawnOrg2IsOffset, Dialect::Both, ""},
        {"cheapOrgCalc", kSpawnCheapOrgCalc, Dialect::Both, ""},
        {"cheapOrg2Calc", kSpawnCheapOrg2Calc, Dialect::Both, ""},
        {"absoluteVel", kSpawnVelIsAbsolute, Dialect::Both, ""},
        {"absoluteAccel", kSpawnAccelIsAbsolute, Dialect::Both, ""},
        {"orgOnSphere", kSpawnOrgOnSphere, Dialect::Both, ""},
        {"orgOnCylinder", kSpawnOrgOnCylinder, Dialect::Both, ""},
        {"axisFromSphere", kSpawnAxisFromSphere, Dialect::Both, ""},
        {"randrotaroundfwd", kSpawnRandRotAroundFwd, Dialect::Both, ""},
        {"evenDistribution", kSpawnEvenDistribution, Dialect::Both, ""},
        {"rgbComponentInterpolation", kSpawnRgbComponentInterp, Dialect::Both, ""},

        // Nur Multiplayer: codemp/client/FxTemplate.cpp liest das Wort, der
        // Singleplayer-Parser kennt es nicht und ueberliest es stumm.
        {"affectedByWind", kSpawnAffectedByWind, Dialect::MP, ""},

        // Nur Singleplayer: umgekehrter Fall.
        {"lessAttenuation", kSpawnSoundLessAttenuation, Dialect::SP, ""},
    };
    return kNames;
}

Primitive freshPrimitive(PrimitiveType type) {
    Primitive out;
    out.type = type;

    switch (type) {
        case PrimitiveType::CameraShake:
            // Ohne diese drei tut ein neues Erschuetterungs-Segment gar
            // nichts. `elasticity` ist die Staerke, `radius` die Reichweite,
            // `life` die Dauer - so fuettert die Engine es:
            //
            //     CameraShake( origin, mElasticity, mRadius, mLife )
            out.elasticity = Range::single(8.0F);
            out.elasticity.set = true;
            out.elasticityAsIntensity = true;   // in der Datei heisst es "intensity"
            out.radius = Range::single(500.0F);
            out.radius.set = true;
            out.life = Range::single(500.0F);
            out.life.set = true;
            break;

        case PrimitiveType::Light:
            // Ein Licht ohne Radius leuchtet nicht.
            out.radius = Range::single(100.0F);
            out.radius.set = true;
            out.life = Range::single(500.0F);
            out.life.set = true;
            break;

        default:
            // Alles, was gezeichnet wird, braucht wenigstens eine Lebensdauer
            // und eine Groesse - sonst ist es unsichtbar und man sucht den
            // Fehler an der falschen Stelle.
            out.life = Range::single(1000.0F);
            out.life.set = true;
            out.size.present = true;
            out.size.start = Range::single(10.0F);
            out.size.start.set = true;
            break;
    }
    return out;
}

}  // namespace bhed::efx

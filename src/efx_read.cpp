// Von der GP2-Struktur zum Effektmodell.
//
// Vorbild ist CPrimitiveTemplate::ParsePrimitive aus code/cgame/FxTemplate.cpp.
// Dort gilt: unbekannte Schluessel brechen nichts ab, sie werden gemeldet und
// uebergangen. Genauso hier - sonst wuerde der Editor Dateien ablehnen, die im
// Spiel laufen.
#include <cctype>
#include <cstdlib>
#include <sstream>
#include <string>

#include "bhed/efx/io.h"
#include "bhed/efx/gp2.h"

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

// Zerlegt "1 0.5  -3" in Zahlen. Gibt zurueck, wie viele gelesen wurden.
size_t scanFloats(std::string_view text, float* out, size_t maxCount) {
    std::string buffer(text);
    std::istringstream stream(buffer);
    size_t count = 0;
    while (count < maxCount && (stream >> out[count])) { ++count;
}
    return count;
}

size_t scanWords(std::string_view text, std::vector<std::string>& out) {
    std::string buffer(text);
    std::istringstream stream(buffer);
    std::string word;
    while (stream >> word) { out.push_back(word);
}
    return out.size();
}

class Reader {
public:
    explicit Reader(ReadResult& result) : result_(result) {}

    void error(int line, std::string message) {
        result_.diagnostics.push_back({Severity::Error, line, std::move(message)});
    }
    void warn(int line, std::string message) {
        result_.diagnostics.push_back({Severity::Warning, line, std::move(message)});
    }

    // --- Skalare ---------------------------------------------------------

    // Das Spiel akzeptiert einen Wert (fest) oder zwei (Spanne).
    bool readRange(const gp2::Property& prop, Range& out) {
        if (prop.values.empty()) { return false;
}
        float v[2]{};
        size_t n = scanFloats(prop.values.front(), v, 2);
        if (n == 0) {
            warn(prop.line, "\"" + prop.name + "\" enthaelt keine Zahl");
            return false;
        }
        if (n == 1) {
            out = Range::single(v[0]);
        } else {
            out = Range::span(v[0], v[1]);
        }
        return true;
    }

    // Drei Werte (fest) oder sechs (Spanne je Achse).
    bool readVec3Range(const gp2::Property& prop, Vec3Range& out) {
        if (prop.values.empty()) { return false;
}
        float v[6]{};
        size_t n = scanFloats(prop.values.front(), v, 6);
        if (n != 3 && n != 6) {
            warn(prop.line, "\"" + prop.name + "\" braucht 3 oder 6 Zahlen, hat " +
                                std::to_string(n));
            return false;
        }
        for (int i = 0; i < 3; ++i) { out.min[i] = v[i];
}
        if (n == 6) {
            for (int i = 0; i < 3; ++i) { out.max[i] = v[3 + i];
}
            out.ranged = true;
        } else {
            out.max = out.min;
            out.ranged = false;
        }
        out.set = true;
        return true;
    }

    // --- Kurvenflags -----------------------------------------------------

    int readCurveFlags(const gp2::Property& prop,
                       std::vector<std::string>* wordsOut = nullptr) {
        std::vector<std::string> words;
        scanWords(prop.values.empty() ? std::string_view{} : std::string_view(prop.values.front()),
                  words);
        if (wordsOut != nullptr) { *wordsOut = words;
}
        int flags = 0;
        for (const auto& word : words) {
            if (iequals(word, "linear")) { flags |= kCurveLinear;
            } else if (iequals(word, "nonlinear")) { flags |= kCurveNonLinear;
            } else if (iequals(word, "wave")) { flags |= kCurveWave;
            } else if (iequals(word, "random")) { flags |= kCurveRandom;
            } else if (iequals(word, "clamp")) { flags |= kCurveClamp;
            } else { warn(prop.line, "Unbekannte Uebergangsart \"" + word + "\"");
}
        }
        return flags;
    }

    // --- Unterbloecke ----------------------------------------------------

    void readChannel(const gp2::Group& group, Channel& out, const char* what) {
        out.present = true;
        for (const auto& prop : group.properties) {
            if (iequals(prop.name, "start")) {
                readRange(prop, out.start);
            } else if (iequals(prop.name, "end")) {
                readRange(prop, out.end);
            } else if (iequals(prop.name, "parm") || iequals(prop.name, "parms")) {
                readRange(prop, out.parm);
                out.parmPlural = iequals(prop.name, "parms");
            } else if (iequals(prop.name, "flag") || iequals(prop.name, "flags")) {
                out.curveFlags = readCurveFlags(prop, &out.curveWords);
                out.flagsPlural = iequals(prop.name, "flags");
            } else {
                warn(prop.line, std::string("Unbekannter Schluessel \"") + prop.name +
                                    "\" im " + what + "-Block");
            }
        }
    }

    void readColorChannel(const gp2::Group& group, ColorChannel& out) {
        out.present = true;
        for (const auto& prop : group.properties) {
            if (iequals(prop.name, "start")) {
                readColorEndpoint(prop, out.start);
            } else if (iequals(prop.name, "end")) {
                readColorEndpoint(prop, out.end);
            } else if (iequals(prop.name, "parm") || iequals(prop.name, "parms")) {
                readRange(prop, out.parm);
                out.parmPlural = iequals(prop.name, "parms");
            } else if (iequals(prop.name, "flag") || iequals(prop.name, "flags")) {
                out.curveFlags = readCurveFlags(prop, &out.curveWords);
                out.flagsPlural = iequals(prop.name, "flags");
            } else {
                warn(prop.line, "Unbekannter Schluessel \"" + prop.name +
                                    "\" im rgb-Block");
            }
        }
    }

    // rgb erlaubt zusaetzlich eine einzelne Zahl als Graustufe, wie das Spiel.
    void readColorEndpoint(const gp2::Property& prop, Vec3Range& out) {
        if (prop.values.empty()) { return;
}
        float v[6]{};
        size_t n = scanFloats(prop.values.front(), v, 6);
        if (n == 1) {
            for (int i = 0; i < 3; ++i) { out.min[i] = v[0];
}
            out.max = out.min;
            out.set = true;
            out.ranged = false;
            return;
        }
        if (n == 2) {
            // Graustufenspanne.
            for (int i = 0; i < 3; ++i) {
                out.min[i] = v[0];
                out.max[i] = v[1];
            }
            out.set = true;
            out.ranged = true;
            return;
        }
        if (n != 3 && n != 6) {
            warn(prop.line, "\"" + prop.name +
                                "\" im rgb-Block braucht 1, 2, 3 oder 6 Zahlen");
            return;
        }
        for (int i = 0; i < 3; ++i) { out.min[i] = v[i];
}
        if (n == 6) {
            for (int i = 0; i < 3; ++i) { out.max[i] = v[3 + i];
}
            out.ranged = true;
        } else {
            out.max = out.min;
            out.ranged = false;
        }
        out.set = true;
    }

    // --- Flags -----------------------------------------------------------

    uint32_t readFlags(const gp2::Property& prop, const std::vector<FlagName>& table,
                       const char* what) {
        std::vector<std::string> words;
        scanWords(prop.values.empty() ? std::string_view{} : std::string_view(prop.values.front()),
                  words);
        uint32_t bits = 0;
        for (const auto& word : words) {
            bool found = false;
            for (const auto& entry : table) {
                if (iequals(word, entry.name)) {
                    bits |= entry.bits;
                    found = true;
                    break;
                }
            }
            if (!found) {
                warn(prop.line, std::string("Unbekanntes ") + what + " \"" + word +
                                    "\" - die Engine ueberliest es");
            }
        }
        return bits;
    }

    // --- Listen ----------------------------------------------------------

    void readList(const gp2::Property& prop, std::vector<std::string>& out) {
        if (prop.values.empty()) {
            warn(prop.line, "\"" + prop.name + "\" ist leer");
            return;
        }
        for (const auto& value : prop.values) { out.push_back(value);
}
    }

    // --- Primitive -------------------------------------------------------

    Primitive readPrimitive(const gp2::Group& group, PrimitiveType type) {
        Primitive prim;
        prim.type = type;

        for (const auto& prop : group.properties) {
            const std::string& key = prop.name;
            const std::string value = prop.values.empty() ? std::string{} : prop.values.front();

            if (iequals(key, "name")) {
                prim.name = value;
            } else if (iequals(key, "cullrange")) {
                prim.cullRange = std::atoi(value.c_str());
                prim.cullRangeSet = true;
            } else if (iequals(key, "count")) {
                readRange(prop, prim.count);
            } else if (iequals(key, "life")) {
                readRange(prop, prim.life);
            } else if (iequals(key, "delay")) {
                readRange(prop, prim.delay);
            } else if (iequals(key, "bounce") || iequals(key, "intensity")) {
                readRange(prop, prim.elasticity);
                prim.elasticityAsIntensity = iequals(key, "intensity");
            } else if (iequals(key, "min")) {
                readVec3Range(prop, prim.min);
            } else if (iequals(key, "max")) {
                readVec3Range(prop, prim.max);
            } else if (iequals(key, "angle") || iequals(key, "angles")) {
                readVec3Range(prop, prim.angles);
                prim.anglesSingular = iequals(key, "angle");
            } else if (iequals(key, "angleDelta")) {
                readVec3Range(prop, prim.anglesDelta);
            } else if (iequals(key, "velocity") || iequals(key, "vel")) {
                readVec3Range(prop, prim.velocity);
                prim.velocityShort = iequals(key, "vel");
            } else if (iequals(key, "acceleration") || iequals(key, "accel")) {
                readVec3Range(prop, prim.acceleration);
                prim.accelShort = iequals(key, "accel");
            } else if (iequals(key, "gravity")) {
                readRange(prop, prim.gravity);
            } else if (iequals(key, "density")) {
                readRange(prop, prim.density);
            } else if (iequals(key, "variance")) {
                readRange(prop, prim.variance);
            } else if (iequals(key, "origin")) {
                readVec3Range(prop, prim.origin);
            } else if (iequals(key, "origin2")) {
                readVec3Range(prop, prim.origin2);
            } else if (iequals(key, "radius")) {
                readRange(prop, prim.radius);
            } else if (iequals(key, "height")) {
                // Auf oberster Ebene ist "height" die Hoehe; im Unterblock die
                // Laenge. Das Spiel unterscheidet das genauso.
                readRange(prop, prim.height);
            } else if (iequals(key, "wind")) {
                readRange(prop, prim.windModifier);
            } else if (iequals(key, "rotation")) {
                readRange(prop, prim.rotation);
            } else if (iequals(key, "rotationDelta")) {
                readRange(prop, prim.rotationDelta);
            } else if (iequals(key, "flags") || iequals(key, "flag")) {
                prim.flags |= readFlags(prop, flagNames(), "Flag");
                prim.flagsSingular = iequals(key, "flag");
            } else if (iequals(key, "spawnFlags") || iequals(key, "spawnFlag")) {
                prim.spawnFlags |= readFlags(prop, spawnFlagNames(), "spawnFlag");
                prim.spawnFlagsSingular = iequals(key, "spawnFlag");
            } else if (iequals(key, "shaders") || iequals(key, "shader")) {
                readList(prop, prim.shaders);
                prim.shadersPlural = iequals(key, "shaders");
            } else if (iequals(key, "models") || iequals(key, "model")) {
                readList(prop, prim.models);
                prim.modelsPlural = iequals(key, "models");
            } else if (iequals(key, "sounds") || iequals(key, "sound")) {
                readList(prop, prim.sounds);
                prim.soundsPlural = iequals(key, "sounds");
            } else if (iequals(key, "impactfx")) {
                readList(prop, prim.impactFx);
            } else if (iequals(key, "deathfx")) {
                readList(prop, prim.deathFx);
            } else if (iequals(key, "emitfx")) {
                readList(prop, prim.emitFx);
            } else if (iequals(key, "playfx")) {
                readList(prop, prim.playFx);
            } else if (iequals(key, "materialImpact")) {
                // Nur Multiplayer, und der dortige Parser kennt genau einen
                // gueltigen Wert. Alles andere setzt er auf None zurueck.
                prim.materialImpactSet = true;
                if (iequals(value, "shellsound")) {
                    prim.materialImpact = MaterialImpact::ShellSound;
                } else {
                    prim.materialImpact = MaterialImpact::None;
                    warn(prop.line, "materialImpact kennt nur \"shellsound\", \"" +
                                        value + "\" wird zu None");
                }
            } else if (key == "forces" || key == "force") {
                // Der alte Editor kennt sie, die Engine nicht.
                //
                // In EffectsEd.exe steht eine Funktion `ParseForces` und die
                // Schluesselwoerter `force` und `forces` - im ausgelieferten
                // Quelltext von Jedi Academy kommt beides an keiner Stelle
                // vor. Der Block wird also gelesen und dann verworfen.
                //
                // "Unbekannter Schluessel" waere hier irrefuehrend: er ist ja
                // bekannt, nur wirkungslos. Wer eine alte Datei oeffnet, soll
                // erfahren, dass der Block nichts tut - nicht, dass er falsch
                // sei.
                warn(prop.line,
                     "\"" + key +
                         "\" kennt nur der alte Editor - die Engine wertet es "
                         "nicht aus");
            } else {
                warn(prop.line, "Unbekannter Schluessel \"" + key + "\" in " +
                                    typeName(type));
            }
        }

        for (const auto& sub : group.subGroups) {
            if (iequals(sub.name, "rgb")) {
                readColorChannel(sub, prim.rgb);
            } else if (iequals(sub.name, "alpha")) {
                readChannel(sub, prim.alpha, "alpha");
            } else if (iequals(sub.name, "size") || iequals(sub.name, "width")) {
                readChannel(sub, prim.size, "size");
                prim.sizeAsWidth = iequals(sub.name, "width");
            } else if (iequals(sub.name, "size2") || iequals(sub.name, "width2")) {
                readChannel(sub, prim.size2, "size2");
                prim.size2AsWidth = iequals(sub.name, "width2");
            } else if (iequals(sub.name, "length") || iequals(sub.name, "height")) {
                readChannel(sub, prim.length, "length");
                prim.lengthAsHeight = iequals(sub.name, "height");
            } else {
                warn(sub.line, "Unbekannter Block \"" + sub.name + "\" in " +
                                   typeName(type));
            }
        }

        return prim;
    }

private:
    ReadResult& result_;
};

}  // namespace

bool ReadResult::hasErrors() const {
    for (const auto& d : diagnostics) {
        if (d.severity == Severity::Error) { return true;
}
    }
    return false;
}

ReadResult read(std::string_view text) {
    ReadResult result;
    Reader reader(result);

    gp2::ParseResult parsed = gp2::parse(text);
    for (const auto& e : parsed.errors) {
        reader.error(e.line, e.message);
    }
    if (!parsed.ok()) { return result;
}

    for (const auto& prop : parsed.topLevel.properties) {
        if (iequals(prop.name, "repeatDelay")) {
            result.effect.repeatDelay =
                std::atoi(prop.values.empty() ? "0" : prop.values.front().c_str());
            result.effect.repeatDelaySet = true;
        } else {
            reader.warn(prop.line,
                        "Unbekannter Schluessel \"" + prop.name +
                            "\" auf oberster Ebene");
        }
    }

    for (const auto& group : parsed.topLevel.subGroups) {
        auto type = typeFromName(group.name);
        if (!type) {
            // Eine ANMERKUNG, kein Fehler - so macht es die Engine.
            //
            // code/cgame/FxScheduler.cpp, Zeile 527: dort steht eine Tabelle
            // mit genau dreizehn Typen, und dann
            //
            //     auto pos = primitiveTypes.find( primitiveGroup.GetName() );
            //     if( pos != primitiveTypes.end() )
            //     {
            //         ... Primitive anlegen
            //     }
            //
            // KEIN else. Unbekannte Gruppen werden stillschweigend
            // uebergangen; die Datei laedt trotzdem.
            //
            // Das ist kein Sonderfall: von den 255 .efx der Mod enthalten
            // 49 eine Gruppe "forceFeedback" - die stammt aus dem
            // _IMMERSION-Zweig des Spiels (TouchSense), den OpenJK nicht
            // baut. Als Fehler behandelt hiesse das, ein Fuenftel aller
            // Effekte abzulehnen, die im Spiel einwandfrei laufen.
            reader.warn(group.line,
                        "Primitivtyp \"" + group.name +
                            "\" ist der Engine unbekannt und wird uebergangen");
            continue;
        }
        result.effect.primitives.push_back(reader.readPrimitive(group, *type));
    }

    return result;
}

}  // namespace bhed::efx

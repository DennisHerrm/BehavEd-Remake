// roff.cpp - ROFF-Dateien lesen und abspielen, siehe bhed/roff.h
#include "bhed/roff.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace bhed {
namespace {

// Kopfgroessen: roff_hdr_t (char[4], int, float) und roff_hdr2_t (char[4],
// vier int), je Bild move_rotate_t (sechs float) und move_rotate2_t (sechs
// float, zwei int) - g_roff.h:36 ff.
constexpr std::size_t kKopf1 = 12;
constexpr std::size_t kKopf2 = 20;
constexpr std::size_t kBild1 = 24;
constexpr std::size_t kBild2 = 32;

// Little-Endian, wie LittleLong/LittleFloat es auf jeder Maschine liefern.
std::uint32_t u32(const std::string& d, std::size_t at) {
    return static_cast<std::uint32_t>(static_cast<unsigned char>(d[at])) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(d[at + 1])) << 8U) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(d[at + 2])) << 16U) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(d[at + 3])) << 24U);
}

std::int32_t i32(const std::string& d, std::size_t at) {
    const std::uint32_t u = u32(d, at);
    std::int32_t v = 0;
    std::memcpy(&v, &u, sizeof(v));
    return v;
}

float f32(const std::string& d, std::size_t at) {
    const std::uint32_t u = u32(d, at);
    float v = 0.0F;
    std::memcpy(&v, &u, sizeof(v));
    return v;
}

// Ein Spielbild, auf das ein Bild der Datei fallen kann: das naechste
// Vielfache von 50 ms, das nicht vor dem Abstand liegt.
double spielSchritt(const Roff& r) {
    if (r.bildMs <= 0) {
        return kRoffSpielBildMs;
    }
    return std::ceil(static_cast<double>(r.bildMs) / kRoffSpielBildMs) * kRoffSpielBildMs;
}

// Wie viele Bilder sind bis `ms` (einschliesslich) angewandt?
std::size_t angewandt(const Roff& r, double ms) {
    const double a0 = roffBildZeitMs(r, 0);
    if (ms < a0 - 1e-6) {
        return 0;
    }
    const double k = std::floor((ms - a0) / spielSchritt(r) + 1e-9) + 1.0;
    const auto n = r.bilder.size();
    return (k >= static_cast<double>(n)) ? n : static_cast<std::size_t>(k);
}

// LerpAngle (q_math.cpp): ueber den kuerzeren Weg.
float lerpWinkel(float von, float nach, float f) {
    float d = nach - von;
    if (d > 180.0F) {
        d -= 360.0F;
    }
    if (d < -180.0F) {
        d += 360.0F;
    }
    return von + f * d;
}

// AngleVectors (q_math.cpp).
void winkelAchsen(const float w[3], float vorn[3], float rechts[3], float oben[3]) {
    constexpr float kGrad = 3.14159265358979323846F * 2.0F / 360.0F;
    const float sy = std::sin(w[1] * kGrad);
    const float cy = std::cos(w[1] * kGrad);
    const float sp = std::sin(w[0] * kGrad);
    const float cp = std::cos(w[0] * kGrad);
    const float sr = std::sin(w[2] * kGrad);
    const float cr = std::cos(w[2] * kGrad);
    vorn[0] = cp * cy;
    vorn[1] = cp * sy;
    vorn[2] = -sp;
    rechts[0] = (-1.0F * sr * sp * cy + -1.0F * cr * -sy);
    rechts[1] = (-1.0F * sr * sp * sy + -1.0F * cr * cy);
    rechts[2] = -1.0F * sr * cp;
    oben[0] = (cr * sp * cy + -sr * -sy);
    oben[1] = (cr * sp * sy + -sr * cy);
    oben[2] = cr * cp;
}

// Ein Zeichen aus einem der Puffer des Rueckrufs. Die Engine liest dort aus
// null-vorbelegten Feldern (char argument[512]{} usw.) und laeuft an
// manchen Stellen ueber die Endnull hinaus - dort steht dann eine Null.
char zeichen(const std::string& s, std::size_t i) {
    return (i < s.size()) ? s[i] : '\0';
}

}  // namespace

bool leseRoff(const std::string& daten, Roff& out, std::string* fehler) {
    out = Roff{};
    const auto scheitern = [fehler](const std::string& grund) {
        if (fehler != nullptr) {
            *fehler = grund;
        }
        return false;
    };
    if (daten.size() < kKopf1) {
        return scheitern("zu kurz fuer einen ROFF-Kopf (" + std::to_string(daten.size()) + " Byte)");
    }
    // G_ValidRoff: strncmp( header->mHeader, "ROFF", 4 ). "ROFF" hat keine
    // Null, also ist das ein Vergleich aller vier Zeichen.
    if (std::memcmp(daten.data(), "ROFF", 4) != 0) {
        return scheitern("keine ROFF-Kennung");
    }
    const std::int32_t fassung = i32(daten, 4);
    std::size_t anzahl = 0;
    std::size_t kopf = 0;
    std::size_t bildGroesse = 0;
    std::int32_t notizen = 0;
    if (fassung == kRoffFassung2) {
        if (daten.size() < kKopf2) {
            return scheitern("zu kurz fuer den Kopf der Fassung 2");
        }
        const std::int32_t n = i32(daten, 8);
        if (n <= 0) {
            return scheitern("Fassung 2 mit " + std::to_string(n) + " Bildern (G_ValidRoff: > 0)");
        }
        const std::int32_t takt = i32(daten, 12);
        // mLerp = 1000 / mFrameRate: bei 0 teilt die Engine durch null. Ein
        // negativer Takt ergaebe eine rueckwaerts laufende Geschwindigkeit -
        // beides ist keine Bahn, die man zeigen kann.
        if (takt <= 0) {
            return scheitern("Fassung 2 mit Bildabstand " + std::to_string(takt) + " ms");
        }
        notizen = i32(daten, 16);
        anzahl = static_cast<std::size_t>(n);
        kopf = kKopf2;
        bildGroesse = kBild2;
        out.bildMs = takt;
        out.lerp = 1000 / takt;
        if (takt < 50) {
            // g_roff.cpp:378: gemeldet und per assert verlangt, gespielt
            // wird trotzdem - je Spielbild ein Bild, siehe roffBildZeitMs.
            out.hinweise.push_back("Bildabstand " + std::to_string(takt) +
                                   " ms unter 50 (die Engine meldet \"invalid ROFF framerate\")");
        }
    } else if (fassung == kRoffFassung1) {
        const float n = f32(daten, 8);
        // Als float geprueft, wie G_ValidRoff es ausdruecklich tut. NaN ist
        // nicht > 0 und faellt damit ebenfalls durch.
        if (!(n > 0.0F)) {
            return scheitern("Fassung 1 ohne Bilder");
        }
        // Vor der Umwandlung in eine ganze Zahl gegen die Laenge pruefen:
        // (int)1e30f waere undefiniert.
        const double passt = static_cast<double>((daten.size() - kKopf1) / kBild1);
        if (static_cast<double>(n) >= passt + 1.0) {
            return scheitern("kuerzer als die " + std::to_string(n) + " Bilder, die der Kopf nennt");
        }
        anzahl = static_cast<std::size_t>(static_cast<int>(n));   // count = (int)header->mCount
        kopf = kKopf1;
        bildGroesse = kBild1;
        out.bildMs = 100;   // "old school ones have a hard-coded frame time"
        out.lerp = 10;
        if (anzahl == 0) {
            // 0 < mCount < 1: gueltig, aber (int) macht 0 daraus. G_Roff
            // liest dann Bild 0 aus fremdem Speicher und hoert auf.
            out.hinweise.push_back("Fassung 1 mit Bildanzahl unter 1 - keine Bewegung");
        }
    } else {
        return scheitern("Fassung " + std::to_string(fassung) + " unbekannt (nur 1 und 2)");
    }
    out.fassung = fassung;

    // Passen die Bilder hinein? In 64 Bit gerechnet, damit eine riesige
    // Anzahl nicht ueberlaeuft.
    const std::uint64_t bis = static_cast<std::uint64_t>(kopf) +
                              static_cast<std::uint64_t>(anzahl) * static_cast<std::uint64_t>(bildGroesse);
    if (bis > static_cast<std::uint64_t>(daten.size())) {
        return scheitern("kuerzer als die " + std::to_string(anzahl) + " Bilder, die der Kopf nennt (" +
                         std::to_string(daten.size()) + " statt " + std::to_string(bis) + " Byte)");
    }
    out.bilder.resize(anzahl);
    bool unendlich = false;
    for (std::size_t k = 0; k < anzahl; ++k) {
        const std::size_t at = kopf + k * bildGroesse;
        RoffBild& b = out.bilder[k];
        for (int a = 0; a < 3; ++a) {
            b.ort[a] = f32(daten, at + static_cast<std::size_t>(a) * 4U);
            b.winkel[a] = f32(daten, at + 12U + static_cast<std::size_t>(a) * 4U);
            // Die Engine ruehrt eine NaN ungeprueft in den Ort. Hier waere
            // die Entity danach fuer immer verschwunden; eine Null zeigt
            // wenigstens den Rest der Bahn.
            if (!std::isfinite(b.ort[a])) {
                b.ort[a] = 0.0F;
                unendlich = true;
            }
            if (!std::isfinite(b.winkel[a])) {
                b.winkel[a] = 0.0F;
                unendlich = true;
            }
        }
        if (fassung == kRoffFassung2) {
            b.ersteNotiz = i32(daten, at + 24U);
            b.notizen = i32(daten, at + 28U);
        }
    }
    if (unendlich) {
        out.hinweise.push_back("Bilder mit NaN/unendlich - als 0 gelesen");
    }

    // Die Notizen: nullterminierte Zeichenketten hinter den Bildern
    // (g_roff.cpp:393 ff., dort mit strlen ohne Grenze).
    if (notizen < 0) {
        out.hinweise.push_back("negative Notizanzahl " + std::to_string(notizen) + " - keine Notizen");
    }
    std::size_t p = static_cast<std::size_t>(bis);
    for (std::int32_t i = 0; i < notizen; ++i) {
        if (p >= daten.size()) {
            out.hinweise.push_back("nur " + std::to_string(i) + " von " + std::to_string(notizen) +
                                   " Notizen vorhanden");
            break;
        }
        const std::size_t null = daten.find('\0', p);
        if (null == std::string::npos) {
            out.notizen.push_back(daten.substr(p));
            out.hinweise.push_back("letzte Notiz ohne Endnull");
            p = daten.size();
            continue;
        }
        out.notizen.push_back(daten.substr(p, null - p));
        p = null + 1;
    }
    return true;
}

std::string roffPfad(const std::string& name) {
    std::string n = name;
    std::replace(n.begin(), n.end(), '\\', '/');
    return "scripts/" + n + ".rof";
}

double roffBildZeitMs(const Roff& r, std::size_t k) {
    return kRoffSpielBildMs + static_cast<double>(k) * spielSchritt(r);
}

double roffLaufzeitMs(const Roff& r) {
    return r.bilder.empty() ? roffBildZeitMs(r, 0) : roffBildZeitMs(r, r.bilder.size() - 1);
}

RoffBahn roffBahn(const Roff& r, const float startOrt[3], const float startWinkel[3]) {
    RoffBahn b;
    b.stand.resize(r.bilder.size() + 1U);
    for (int a = 0; a < 3; ++a) {
        b.stand[0].ort[a] = startOrt[a];
        b.stand[0].winkel[a] = startWinkel[a];
    }
    for (std::size_t k = 0; k < r.bilder.size(); ++k) {
        for (int a = 0; a < 3; ++a) {
            // VectorAdd: in float, Bild fuer Bild.
            b.stand[k + 1].ort[a] = b.stand[k].ort[a] + r.bilder[k].ort[a];
            b.stand[k + 1].winkel[a] = b.stand[k].winkel[a] + r.bilder[k].winkel[a];
        }
    }
    return b;
}

RoffStand roffStand(const Roff& r, const RoffBahn& bahn, double msSeitPlay, bool figur) {
    if (bahn.stand.size() != r.bilder.size() + 1U) {
        return RoffStand{};   // Bahn gehoert nicht zu dieser Datei
    }
    const std::size_t n = r.bilder.size();
    if (n == 0 || msSeitPlay < 0.0) {
        return bahn.stand[0];
    }
    if (figur) {
        // Der Spielstand zu einer Zeit: alle bis dahin angewandten Bilder
        // (trBase += Versatz). Gezeigt wird zwischen dem Schnappschuss am
        // 50-ms-Raster davor und dem danach - die Figur setzt sich also
        // schon im Spielbild des Befehls in Bewegung, weil der naechste
        // Schnappschuss das erste Bild schon traegt.
        const double vor = std::floor(msSeitPlay / kRoffSpielBildMs + 1e-9) * kRoffSpielBildMs;
        const auto f = static_cast<float>((msSeitPlay - vor) / kRoffSpielBildMs);
        const RoffStand& a = bahn.stand[angewandt(r, vor)];
        const RoffStand& b = bahn.stand[angewandt(r, vor + kRoffSpielBildMs)];
        RoffStand s;
        for (int i = 0; i < 3; ++i) {
            s.ort[i] = a.ort[i] + f * (b.ort[i] - a.ort[i]);
            s.winkel[i] = lerpWinkel(a.winkel[i], b.winkel[i], f);
        }
        return s;
    }
    // Mover: Bild k lief zuletzt an. Es setzte trBase = pos1 (Stand VOR
    // Bild k), trDelta = Versatz * mLerp, trTime = jetzt. Beim letzten Bild
    // ist trDelta gleich wieder null. Vor dem ersten Bild steht er.
    const std::size_t schon = angewandt(r, msSeitPlay);
    if (schon == 0) {
        return bahn.stand[0];
    }
    const std::size_t k = schon - 1U;
    if (k + 1U >= n) {
        return bahn.stand[n - 1U];
    }
    const RoffStand& basis = bahn.stand[k];
    // EvaluateTrajectory, TR_LINEAR (bg_misc.cpp):
    //     deltaTime = ( atTime - tr->trTime ) * 0.001;
    //     result = trBase + deltaTime * trDelta
    const auto dt = static_cast<float>((msSeitPlay - roffBildZeitMs(r, k)) * 0.001);
    const auto lerp = static_cast<float>(r.lerp);
    RoffStand s;
    for (int i = 0; i < 3; ++i) {
        s.ort[i] = basis.ort[i] + (r.bilder[k].ort[i] * lerp) * dt;
        s.winkel[i] = basis.winkel[i] + (r.bilder[k].winkel[i] * lerp) * dt;
    }
    return s;
}

RoffStand roffEndstand(const Roff& r, const RoffBahn& bahn, bool figur) {
    if (bahn.stand.size() != r.bilder.size() + 1U) {
        return RoffStand{};
    }
    const std::size_t n = r.bilder.size();
    if (figur || n == 0) {
        return bahn.stand[n];
    }
    return bahn.stand[n - 1U];
}

RoffStand roffStandVorBild(const Roff& r, const RoffBahn& bahn, std::size_t k, bool figur) {
    if (bahn.stand.size() != r.bilder.size() + 1U) {
        return RoffStand{};
    }
    const std::size_t n = r.bilder.size();
    if (figur) {
        return bahn.stand[std::min(k, n)];
    }
    return bahn.stand[(k == 0) ? 0 : std::min(k - 1U, n)];
}

std::vector<RoffAusloesung> roffAusloesungen(const Roff& r) {
    std::vector<RoffAusloesung> out;
    for (std::size_t k = 0; k < r.bilder.size(); ++k) {
        const RoffBild& b = r.bilder[k];
        // if ( data->mStartNote != -1 || data->mNumNotes )   (g_roff.cpp:515)
        if (b.ersteNotiz == -1 && b.notizen == 0) {
            continue;
        }
        if (b.ersteNotiz < 0 || static_cast<std::size_t>(b.ersteNotiz) >= r.notizen.size()) {
            continue;   // die Engine laese hier fremden Speicher
        }
        out.push_back(RoffAusloesung{k, roffBildZeitMs(r, k), static_cast<std::size_t>(b.ersteNotiz)});
    }
    return out;
}

RoffNotizBefehl zerlegeRoffNotiz(const std::string& text) {
    RoffNotizBefehl out;
    std::size_t i = 0;
    // Der Typ: bis zum ersten Leerzeichen.
    while (zeichen(text, i) != '\0' && zeichen(text, i) != ' ') {
        out.typ += text[i];
        ++i;
    }
    if (zeichen(text, i) != ' ') {
        return out;   // "didn't pass in a valid notetrack type"
    }
    ++i;
    // Das Argument: bis zum naechsten Leerzeichen, ohne Zeilenenden.
    std::string argument;
    while (zeichen(text, i) != '\0' && zeichen(text, i) != ' ') {
        if (text[i] != '\n' && text[i] != '\r') {
            argument += text[i];
        }
        ++i;
    }
    if (argument.empty()) {
        return out;
    }
    bool weitere = false;
    std::string rest;
    if (zeichen(text, i) == ' ') {
        weitere = true;
        rest = text.substr(i + 1);
        // Der Rest endet an der ersten Null wie jede C-Zeichenkette.
        const std::size_t null = rest.find('\0');
        if (null != std::string::npos) {
            rest.resize(null);
        }
    }

    if (out.typ == "effect") {
        // Versatz "vorn+rechts+oben", getrennt durch '+' oder ' '.
        std::size_t j = 0;
        bool abgebrochen = false;
        if (weitere) {
            int n = 0;
            while (n < 3) {
                std::string t;
                while (zeichen(rest, j) != '\0' && zeichen(rest, j) != '+' && zeichen(rest, j) != ' ') {
                    t += rest[j];
                    ++j;
                }
                ++j;
                if (t.empty()) {
                    // "failure..": Versatz null, j = 0 - und weitere BLEIBT
                    // gesetzt. Die Winkel werden dann ab Zeichen 1 gelesen.
                    for (float& v : out.versatz) { v = 0.0F; }
                    j = 0;
                    abgebrochen = true;
                    break;
                }
                out.versatz[n] = static_cast<float>(std::atof(t.c_str()));
                ++n;
            }
            if (!abgebrochen) {
                --j;
                if (zeichen(rest, j) != ' ') {
                    weitere = false;
                }
            }
        }
        // defaultoffsetposition: ein fuehrendes "/" weg; nennt der erste
        // Ordner "effects", faellt er weg - die Engine setzt ihn selbst.
        std::size_t r = 0;
        if (zeichen(argument, r) == '/') {
            ++r;
        }
        std::string ordner;
        while (zeichen(argument, r) != '\0' && zeichen(argument, r) != '/') {
            ordner += argument[r];
            ++r;
        }
        if (!ordner.empty() && ordner.find("effects") != std::string::npos) {
            ++r;
            argument = (r < argument.size()) ? argument.substr(r) : std::string{};
        }
        // G_EffectIndex( "" ) liefert 0, und bei 0 spielt nichts.
        if (argument.empty()) {
            return out;
        }
        out.art = RoffNotizBefehl::Art::Effekt;
        out.datei = argument;
        if (weitere) {
            // Winkel "nicken-gieren-rollen": nur alle drei oder keiner.
            ++j;
            int n = 0;
            float w[3]{};
            while (n < 3) {
                std::string t;
                while (zeichen(rest, j) != '\0' && zeichen(rest, j) != '-') {
                    t += rest[j];
                    ++j;
                }
                ++j;
                if (t.empty()) {
                    n = 0;
                    break;
                }
                w[n] = static_cast<float>(std::atof(t.c_str()));
                ++n;
            }
            if (n != 0) {
                out.eigeneWinkel = true;
                for (int a = 0; a < 3; ++a) { out.winkel[a] = w[a]; }
            }
        }
        return out;
    }
    if (out.typ == "sound") {
        out.art = RoffNotizBefehl::Art::Klang;
        out.datei = argument;
        return out;
    }
    out.art = RoffNotizBefehl::Art::Unbekannt;
    out.datei = argument;
    return out;
}

void roffEffektOrt(const RoffNotizBefehl& b, const float basisOrt[3], const float basisWinkel[3],
                   float ort[3], float winkel[3]) {
    for (int a = 0; a < 3; ++a) {
        winkel[a] = b.eigeneWinkel ? b.winkel[a] : basisWinkel[a];
    }
    float vorn[3];
    float rechts[3];
    float oben[3];
    winkelAchsen(winkel, vorn, rechts, oben);
    for (int a = 0; a < 3; ++a) {
        ort[a] = basisOrt[a] + vorn[a] * b.versatz[0] + rechts[a] * b.versatz[1] + oben[a] * b.versatz[2];
    }
}

}  // namespace bhed

// efxdraw.cpp - aus einem Effekt werden Vierecke.
#include <array>
#include <atomic>
#include "bhed/efxdraw.h"

#include <cctype>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>

#include "bhed/efx/curve.h"
#include "bhed/efx/sim.h"

namespace bhed {

namespace {

using V3 = efx::camera::Vec3;

// --- Die Achse eines fx_runner -----------------------------------------
//
// MakeNormalVectors (q_math.c:1292), woertlich. Die Vertauschung der
// Komponenten sieht willkuerlich aus und ist es auch - sie sorgt nur
// dafuer, dass der Hilfsvektor nie parallel zu `forward` liegt. Wer sie
// "aufraeumt", bekommt eine andere Achse und damit andere Richtungen.
void achseAus(const float forward[3], float right[3], float up[3]) {
    right[1] = -forward[0];
    right[2] = forward[1];
    right[0] = forward[2];
    const float d = right[0] * forward[0] + right[1] * forward[1] +
                    right[2] * forward[2];
    for (int k = 0; k < 3; ++k) {
        right[k] -= d * forward[k];
    }
    const float l = std::sqrt(right[0] * right[0] + right[1] * right[1] +
                              right[2] * right[2]);
    if (l > 0.0001F) {
        for (int k = 0; k < 3; ++k) {
            right[k] /= l;
        }
    }
    // CrossProduct( right, forward, up )
    up[0] = right[1] * forward[2] - right[2] * forward[1];
    up[1] = right[2] * forward[0] - right[0] * forward[2];
    up[2] = right[0] * forward[1] - right[1] * forward[0];
}

// --- Oertlich oder absolut? --------------------------------------------
//
// FxScheduler.cpp:1566:
//
//     if ( fx->mSpawnFlags & FX_VEL_IS_ABSOLUTE || flags & FX_RELATIVE ) {
//         VectorSet( vel, mVelX, mVelY, mVelZ );
//     } else {
//         VectorScale( ax[0], mVelX, vel );
//         VectorMA( vel, mVelY, ax[1], vel );
//         VectorMA( vel, mVelZ, ax[2], vel );
//     }
//
// Ohne `absoluteVel` ist die Geschwindigkeit also OERTLICH: x zeigt in die
// Richtung des Runners, y und z quer dazu.
//
// behaved hat sie bis rc362 immer als absolut genommen. Das Flag wurde
// gelesen (efx_effect.cpp:123) und nirgends benutzt.
//
// Was das ausmacht: `mustafar/volcano.efx` hat KEIN `absoluteVel` und
// `velocity 1200 -180 -180 975 180 180`. Ein Vulkan zeigt nach oben, also
// sind das 975 bis 1200 Einheiten je Sekunde Steighoehe. Als Weltrichtung
// gelesen schiessen die Tropfen stattdessen nach +x - gemeldet als "die
// Spritzer sind nicht so hoch".
//
// Die Gegenprobe steht daneben: `md2/lava_splash.efx` HAT `absoluteVel`
// und darf sich nicht aendern.
V3 inAchse(const V3& v, const float fwd[3], bool absolut) {
    if (absolut) {
        return v;
    }
    float right[3];
    float up[3];
    achseAus(fwd, right, up);
    return V3{fwd[0] * v.x + right[0] * v.y + up[0] * v.z,
              fwd[1] * v.x + right[1] * v.y + up[1] * v.z,
              fwd[2] * v.x + right[2] * v.y + up[2] * v.z};
}

// RotatePointAroundVector (q_math.c:578): `punkt` um die Einheitsachse
// `achse` drehen, rechtshaendig, in Grad. Die Engine baut dafuer eine
// Matrix mit negiertem Winkel und VectorRotate - ausmultipliziert ist das
// genau die Formel von Rodrigues.
void drehePunkt(float aus[3], const float achse[3], const float punkt[3],
                float grad) {
    const float w = grad * std::numbers::pi_v<float> / 180.0F;
    const float c = std::cos(w);
    const float sn = std::sin(w);
    const float dp = achse[0] * punkt[0] + achse[1] * punkt[1] + achse[2] * punkt[2];
    const float kx[3] = {achse[1] * punkt[2] - achse[2] * punkt[1],
                         achse[2] * punkt[0] - achse[0] * punkt[2],
                         achse[0] * punkt[1] - achse[1] * punkt[0]};
    for (int k = 0; k < 3; ++k) {
        aus[k] = punkt[k] * c + kx[k] * sn + achse[k] * dp * (1.0F - c);
    }
}

// PerpendicularVector (q_math.c:1328): die Achse mit dem KLEINSTEN Anteil
// nehmen, auf die Ebene projizieren, normieren.
void senkrechtZu(float aus[3], const float n[3]) {
    int pos = 0;
    float kleinst = 1.0F;
    for (int i = 0; i < 3; ++i) {
        if (std::fabs(n[i]) < kleinst) {
            pos = i;
            kleinst = std::fabs(n[i]);
        }
    }
    float t[3] = {0.0F, 0.0F, 0.0F};
    t[pos] = 1.0F;
    const float nn = n[0] * n[0] + n[1] * n[1] + n[2] * n[2];
    const float d = (nn > 0.0F) ? (t[pos] * n[pos]) / nn : 0.0F;
    for (int k = 0; k < 3; ++k) {
        aus[k] = t[k] - n[k] * d;
    }
    const float l = std::sqrt(aus[0] * aus[0] + aus[1] * aus[1] + aus[2] * aus[2]);
    if (l > 0.0F) {
        for (int k = 0; k < 3; ++k) {
            aus[k] /= l;
        }
    }
}

// Ein Vektor in der VOLLEN Achse eines Teilchens: ax[0]*x + ax[1]*y + ax[2]*z
// (VectorScale/VectorMA in CreateEffect, FxScheduler.cpp:1558 ff.).
V3 inAchse3(const V3& v, const float ax[3][3]) {
    return V3{ax[0][0] * v.x + ax[1][0] * v.y + ax[2][0] * v.z,
              ax[0][1] * v.x + ax[1][1] * v.y + ax[2][1] * v.z,
              ax[0][2] * v.x + ax[1][2] * v.y + ax[2][2] * v.z};
}

std::string klein(std::string s) {
    for (char& ch : s) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return s;
}

// Ein Vec3Range ist min UND max je Achse - gewuerfelt wird je Achse
// zwischen beiden, wenn ranged gesetzt ist.
//
// ACHTUNG, und es ist hier dreimal schiefgegangen: `Range` hat vier
// Felder, nicht zwei. `efx::Range{min, max}` laesst `set` auf false
// stehen - und `Random::pick` gibt fuer ein ungesetztes Range **null**
// zurueck (efx_sim.cpp:19). Deshalb wird das Range hier Feld fuer Feld
// gebaut, und fuer "gleichverteilt zwischen a und b" steht unten
// `Random::range`, nie `pick(Range{a, b})`.
V3 pickVec(efx::sim::Random& rnd, const efx::Vec3Range& vr) {
    V3 o;
    float* out[3] = {&o.x, &o.y, &o.z};
    for (int c = 0; c < 3; ++c) {
        efx::Range r;
        r.min = vr.min[c];
        r.max = vr.max[c];
        r.set = vr.set;
        r.ranged = vr.ranged;
        *out[c] = rnd.pick(r);
    }
    return o;
}

// --- Aus einer Liste waehlen, wie CMediaHandles::GetHandle -------------
//
// FxScheduler.h:92:
//
//     return mMediaList[irand(0, mMediaList.size()-1)];
//
// Je Teilchen neu, nicht je Effekt. Bei einem Eintrag wird NICHT
// gewuerfelt - so bleibt die Zugfolge fuer die vielen Bausteine mit nur
// einem Shader dieselbe wie vor dieser Runde.
std::size_t waehle(efx::sim::Random& rnd, std::size_t n) {
    if (n <= 1U) {
        return 0U;
    }
    return static_cast<std::size_t>(
        static_cast<int>(rnd.next() * static_cast<float>(n)) %
        static_cast<int>(n));
}

}  // namespace
namespace {

// Nur diese drei Arten haben eine flaechige Gestalt, die sich als Viereck
// zeichnen laesst.
bool quadLike(efx::PrimitiveType t) {
    return t == efx::PrimitiveType::Particle ||
           t == efx::PrimitiveType::OrientedParticle ||
           // Ein Decal ist auch ein Viereck - nur an der Flaechennormalen
           // ausgerichtet statt an der Kamera. Es muss hier stehen, damit
           // Groesse, Farbe und Lebensdauer denselben Weg gehen.
           t == efx::PrimitiveType::Decal;
}

// Ein Band zwischen zwei Punkten.
//
// Line spannt zwischen org und org2 (FxScheduler.cpp, case Line:
// FX_AddLine( clientID, org, org2, ... )). Electricity hat dieselben zwei
// Enden, wird aber gezackt gezeichnet - siehe blitz() weiter unten.
bool lineLike(efx::PrimitiveType t) {
    return t == efx::PrimitiveType::Line ||
           t == efx::PrimitiveType::Electricity ||
           // --- Ein Tail ist ein Band, kein Viereck ---------------------
           //
           // Er stand bis rc365 bei den Vierecken und wurde als
           // kamerazugewandtes Plaettchen der Groesse `size` gezeichnet -
           // bei `volcano.efx` also 15 bis 100 Einheiten, ein Fleck.
           //
           // In der Engine ist er RT_LINE (FxPrimitives.h:466), also
           // dasselbe Band wie eine Line. Der Unterschied liegt nur darin,
           // WOHER das zweite Ende kommt: eine Line hat org2, ein Tail
           // rechnet es aus Flugrichtung und LAENGE
           // (CTail::CalcNewEndpoint) - siehe unten.
           //
           // `volcano.efx` hat `length end -280 -200`. Genau diese Schweife
           // sind die Saeule; die Tropfen allein sind winzig. Gemeldet als
           // "unsere sind dagegen winzig".
           t == efx::PrimitiveType::Tail;
}

// Zusammen: was ueberhaupt eine Gestalt hat, die wir ins Effektnetz legen.
// (Der Flash kommt ueber appendScreenFlashes, das Emittermodell ueber
// emitterModelsAt - beide haben einen eigenen Weg.)
bool drawable(efx::PrimitiveType t) {
    return quadLike(t) || lineLike(t) ||
           t == efx::PrimitiveType::Cylinder;
}

// Die Typen, fuer die CreateEffect Geschwindigkeit und Beschleunigung
// ueberhaupt ausrechnet (FxScheduler.cpp:1622):
//
//     if ( fx->mType == Particle || fx->mType == OrientedParticle ||
//          fx->mType == Tail || fx->mType == Emitter )
//
// Alle anderen bleiben an ihrem Ort stehen. Hier lief bis rc568 JEDER Typ
// ueber positionAt - eine Line mit `velocity`-Block wanderte davon.
bool bewegt(efx::PrimitiveType t) {
    return t == efx::PrimitiveType::Particle ||
           t == efx::PrimitiveType::OrientedParticle ||
           t == efx::PrimitiveType::Tail ||
           t == efx::PrimitiveType::Emitter;
}

// Ein Kanal, mit der VORGABE der Engine, wenn er in der Datei fehlt.
//
// code/cgame/FxTemplate.cpp, CPrimitiveTemplate::CPrimitiveTemplate():
//
//     mRedStart.SetRange( 1.0f, 1.0f );   ... Gruen, Blau ebenso
//     mAlphaStart.SetRange( 1.0f, 1.0f );
//     mAlphaEnd.SetRange( 1.0f, 1.0f );
//     mSizeStart.SetRange( 1.0f, 1.0f );
//     mSizeEnd.SetRange( 1.0f, 1.0f );
//     mSize2Start / mLengthStart ebenso
//
// Fehlt ein Block, gilt also 1.0 - NICHT null. Hier stand vorher schlicht
// der gelesene Wert, und der ist bei einem fehlenden Block null. Ergebnis:
// Partikel ohne alpha-Block wurden schwarz (Farbe mal null) und Partikel
// ohne size-Block verschwanden ganz. Auf shanks Bildschirm waren das die
// schwarzen Flecken auf dem Hangarboden.
efx::curve::Curve curveOf(const efx::Channel& ch, efx::sim::Random& rnd,
                          float wennFehlt = 1.0F) {
    efx::curve::Curve c;
    if (!ch.present) {
        c.start = wennFehlt;
        c.end = wennFehlt;
        return c;
    }
    c.start = ch.start.set ? rnd.pick(ch.start) : wennFehlt;
    c.end = ch.end.set ? rnd.pick(ch.end) : wennFehlt;
    c.parm = rnd.pick(ch.parm);
    c.flags = efx::curve::ausDatei(ch.curveFlags);
    return c;
}

// Ein Kurvenwert zum Alter `alter` (ms seit der Geburt).
float kurveBei(const efx::curve::Curve& c, float alter, float life) {
    const float parm = efx::curve::resolveParm(c, 0.0F, life);
    return efx::curve::evaluate(c, alter, 0.0F, life, parm);
}

// Ein Viereck aus vier Ecken, zwei Dreiecke.
//
// Der Umlauf ist der von JKA: entgegen der Normalen. Das ist nicht Geschmack,
// sondern die Regel aus rc103 - der Zeichner keult "area < 0" weg, weil
// GL_Cull( GL_FRONT ) die Engine so haelt (tr_backend.cpp). Wer hier
// andersherum wickelt, bekommt unsichtbare Partikel und sucht lange.
void addQuad(BspMesh& mesh, const float centre[3], const float right[3],
             const float up[3], float half, const std::uint8_t rgba[4]) {
    const auto base = static_cast<std::uint32_t>(mesh.verts.size());
    const float sx[4] = {-1.0F, 1.0F, 1.0F, -1.0F};
    const float sy[4] = {-1.0F, -1.0F, 1.0F, 1.0F};
    for (int k = 0; k < 4; ++k) {
        BspVertex v;
        for (int c = 0; c < 3; ++c) {
            v.xyz[c] = centre[c] + right[c] * (sx[k] * half) +
                       up[c] * (sy[k] * half);
            // Die Normale zeigt zur Kamera: right x up. Damit faellt die
            // Schattierung nach Normale voll aus, und die Farbe kommt
            // unverfaelscht durch.
            v.normal[c] = 0.0F;
        }
        v.normal[0] = right[1] * up[2] - right[2] * up[1];
        v.normal[1] = right[2] * up[0] - right[0] * up[2];
        v.normal[2] = right[0] * up[1] - right[1] * up[0];
        v.st[0] = (sx[k] + 1.0F) * 0.5F;
        v.st[1] = (sy[k] + 1.0F) * 0.5F;
        for (int c = 0; c < 4; ++c) {
            v.colour[c] = rgba[c];
        }
        mesh.verts.push_back(v);
    }
    const std::uint32_t idx[6] = {0, 2, 1, 0, 3, 2};
    for (const std::uint32_t i : idx) {
        mesh.indexes.push_back(base + i);
    }
}

// Ein Band von a nach b mit fester Breitenrichtung `w` (Einheitsvektor),
// verschieden breit an beiden Enden - DoLine2 (tr_surface.cpp:301):
//
//     xyz[0] = start + spanWidth  * up    st = (0, tcStart)
//     xyz[1] = start - spanWidth  * up    st = (1, tcStart)
//     xyz[2] = end   + spanWidth2 * up    st = (0, tcEnd)
//     xyz[3] = end   - spanWidth2 * up    st = (1, tcEnd)
//
// Der UMLAUF muss derselbe sein wie bei addBand ohne Auge, sonst keult der
// Zeichner jedes zweite Blitzstueck weg: ApplyShape ruft seine Stuecke mal
// vorwaerts, mal rueckwaerts (DoBoltSeg reicht `cur, old` herein), und die
// Breitenrichtung ist fuer den ganzen Blitz dieselbe. Also wird `w` je
// Stueck so gedreht, dass es in dieselbe Halbebene zeigt wie d x viewDir.
// Das Band liegt dadurch an genau derselben Stelle; nur die Reihenfolge der
// Ecken (und damit die Seite der Textur) spiegelt sich.
void addBand2(BspMesh& mesh, const float a[3], const float b[3],
              const float w[3], float halbA, float halbB, float tcA, float tcB,
              const std::uint8_t rgba[4], const float viewDir[3]) {
    const float d[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]};
    const float ref[3] = {d[1] * viewDir[2] - d[2] * viewDir[1],
                          d[2] * viewDir[0] - d[0] * viewDir[2],
                          d[0] * viewDir[1] - d[1] * viewDir[0]};
    const float s =
        (ref[0] * w[0] + ref[1] * w[1] + ref[2] * w[2] < 0.0F) ? -1.0F : 1.0F;
    const auto base = static_cast<std::uint32_t>(mesh.verts.size());
    const float* ends[4] = {a, a, b, b};
    const float breite[4] = {halbA, halbA, halbB, halbB};
    const float sign[4] = {-1.0F, 1.0F, 1.0F, -1.0F};
    const float tv[4] = {tcA, tcA, tcB, tcB};
    for (int k = 0; k < 4; ++k) {
        BspVertex v;
        for (int c = 0; c < 3; ++c) {
            v.xyz[c] = ends[k][c] + w[c] * (s * sign[k] * breite[k]);
            v.normal[c] = -viewDir[c];
        }
        v.st[0] = sign[k] > 0.0F ? 1.0F : 0.0F;
        v.st[1] = tv[k];
        for (int c = 0; c < 4; ++c) {
            v.colour[c] = rgba[c];
        }
        mesh.verts.push_back(v);
    }
    const std::uint32_t idx[6] = {0, 2, 1, 0, 3, 2};
    for (const std::uint32_t i : idx) {
        mesh.indexes.push_back(base + i);
    }
}

// Ein Band von a nach b, der Kamera zugewandt.
//
// Die Breitenrichtung ist die des Zeichners, tr_surface.cpp,
// RB_SurfaceLine():
//
//     VectorSubtract( start, viewParms.ori.origin, v1 );
//     VectorSubtract( end,   viewParms.ori.origin, v2 );
//     CrossProduct( v1, v2, right );
//     VectorNormalize( right );
//
// Also senkrecht auf der Ebene, die die beiden Enden mit dem Auge
// aufspannen - so bleibt das Band immer gleich breit zu sehen, egal wie es
// im Raum liegt. Ohne Auge tut es die Kamerarichtung: right x up ist die
// Blickrichtung, und das genuegt fuer dieselbe Wirkung.
void addBand(BspMesh& mesh, const float a[3], const float b[3],
             const float viewDir[3], float half, const std::uint8_t rgba[4],
             const float* auge = nullptr) {
    const float d[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]};
    float w[3] = {d[1] * viewDir[2] - d[2] * viewDir[1],
                  d[2] * viewDir[0] - d[0] * viewDir[2],
                  d[0] * viewDir[1] - d[1] * viewDir[0]};
    // Mit dem Auge genau wie RB_SurfaceLine: cross(start - auge, ende -
    // auge). Die Naeherung ueber die Blickrichtung macht lange Saeulen am
    // Bildrand zu schmal oder verdreht sie.
    if (auge != nullptr) {
        const float v1[3] = {a[0] - auge[0], a[1] - auge[1], a[2] - auge[2]};
        const float v2[3] = {b[0] - auge[0], b[1] - auge[1], b[2] - auge[2]};
        const float we[3] = {v1[1] * v2[2] - v1[2] * v2[1],
                             v1[2] * v2[0] - v1[0] * v2[2],
                             v1[0] * v2[1] - v1[1] * v2[0]};
        // --- Das Vorzeichen: dieselbe Seite wie ohne Auge ---------------
        //
        // (a - auge) x (b - auge) zeigt ENTGEGEN d x viewDir, sobald das
        // Band vor der Kamera liegt - nachgerechnet: (a-e) ist dort etwa
        // L*viewDir, also (a-e) x d = -L * (d x viewDir). Die Breite ist
        // dieselbe, der Umlauf der Ecken aber gespiegelt, und ein Shader
        // ohne `cull disable` keult das Band dann weg. Solange niemand das
        // Auge uebergab, fiel es nicht auf; seit die Ansicht es tut, schon.
        const float dp = we[0] * w[0] + we[1] * w[1] + we[2] * w[2];
        const float s = (dp < 0.0F) ? -1.0F : 1.0F;
        for (int c = 0; c < 3; ++c) {
            w[c] = we[c] * s;
        }
    }
    const float wl = std::sqrt(w[0] * w[0] + w[1] * w[1] + w[2] * w[2]);
    if (wl < 0.0001F) {
        return;   // Band genau in Blickrichtung: unsichtbar duenn
    }
    for (float& c : w) {
        c = c / wl * half;
    }
    const auto base = static_cast<std::uint32_t>(mesh.verts.size());
    const float* ends[4] = {a, a, b, b};
    const float sign[4] = {-1.0F, 1.0F, 1.0F, -1.0F};
    const float tv[4] = {0.0F, 0.0F, 1.0F, 1.0F};
    for (int k = 0; k < 4; ++k) {
        BspVertex v;
        for (int c = 0; c < 3; ++c) {
            v.xyz[c] = ends[k][c] + w[c] * sign[k];
            v.normal[c] = -viewDir[c];
        }
        v.st[0] = sign[k] > 0.0F ? 1.0F : 0.0F;
        v.st[1] = tv[k];
        for (int c = 0; c < 4; ++c) {
            v.colour[c] = rgba[c];
        }
        mesh.verts.push_back(v);
    }
    const std::uint32_t idx[6] = {0, 2, 1, 0, 3, 2};
    for (const std::uint32_t i : idx) {
        mesh.indexes.push_back(base + i);
    }
}

// Ein Zylinder als Ring von Baendern.
//
// FxScheduler.cpp, case Cylinder: FX_AddCylinder( clientID, org, ax[0],
// size, size2, length, ... ). Der Zeichner nimmt bis zu vierzig Abschnitte
// (NUM_CYLINDER_SEGMENTS in tr_surface.cpp); zwoelf reichen fuer eine
// Vorschau und sparen das Dreifache an Dreiecken.
//
// r0 gilt am Fuss (`base`), r1 an der Spitze (base + axis*height).
void addCylinder(BspMesh& mesh, const float base[3], const float axis[3],
                 float r0, float r1, float height, const std::uint8_t rgba[4]) {
    constexpr int kSegments = 12;
    // Zwei Achsen quer zur Zylinderachse.
    float u[3] = {0.0F, 0.0F, 1.0F};
    if (std::fabs(axis[2]) > 0.9F) {
        u[0] = 1.0F;
        u[2] = 0.0F;
    }
    float e1[3] = {axis[1] * u[2] - axis[2] * u[1], axis[2] * u[0] - axis[0] * u[2],
                   axis[0] * u[1] - axis[1] * u[0]};
    const float l1 = std::sqrt(e1[0] * e1[0] + e1[1] * e1[1] + e1[2] * e1[2]);
    if (l1 < 0.0001F) {
        return;
    }
    for (float& c : e1) { c /= l1; }
    const float e2[3] = {axis[1] * e1[2] - axis[2] * e1[1],
                         axis[2] * e1[0] - axis[0] * e1[2],
                         axis[0] * e1[1] - axis[1] * e1[0]};

    const auto first = static_cast<std::uint32_t>(mesh.verts.size());
    for (int k = 0; k <= kSegments; ++k) {
        const float a = 6.2831853F * static_cast<float>(k) /
                        static_cast<float>(kSegments);
        const float cs = std::cos(a);
        const float sn = std::sin(a);
        for (int ring = 0; ring < 2; ++ring) {
            const float r = (ring == 0) ? r0 : r1;
            const float h = (ring == 0) ? 0.0F : height;
            BspVertex v;
            for (int c = 0; c < 3; ++c) {
                v.xyz[c] = base[c] + axis[c] * h + (e1[c] * cs + e2[c] * sn) * r;
                v.normal[c] = e1[c] * cs + e2[c] * sn;
            }
            v.st[0] = static_cast<float>(k) / static_cast<float>(kSegments);
            v.st[1] = static_cast<float>(ring);
            for (int c = 0; c < 4; ++c) { v.colour[c] = rgba[c]; }
            mesh.verts.push_back(v);
        }
    }
    for (int k = 0; k < kSegments; ++k) {
        const auto o = first + static_cast<std::uint32_t>(k) * 2U;
        // Umlauf entgegen der Normalen, wie ueberall in JKA.
        const std::uint32_t idx[6] = {o, o + 1U, o + 2U, o + 1U, o + 3U, o + 2U};
        for (const std::uint32_t i : idx) {
            mesh.indexes.push_back(i);
        }
    }
}

// =========================================================================
// Ein Teilchen, gewuerfelt wie beim Erzeugen
// =========================================================================
//
// Bis rc568 wuerfelten drei Stellen jede fuer sich: das Netz je Teilchen,
// die Emittermodelle und die Folgeeffekte je PRIMITIVE mit dem Samen des
// ganzen Effekts. Der Kommentar dort sagte "dieselbe Zufallsfolge wie beim
// Zeichnen" - das stimmte nicht mehr, seit das Netz je Teilchen wuerfelt.
// Folge: ein Funke prallte an einer anderen Stelle ab als der, den man
// sah, und ein Effekt mit count 20 loeste genau EINEN Aufprall aus.
//
// Jetzt gibt es EINEN Weg vom Zeitplan zum Teilchen, und alle drei gehen
// ihn. Die Zugfolge des gemeinsamen Anfangs ist fest:
//
//     Medium (Shader bzw. Modell), life, origin, velocity, acceleration,
//     gravity, size, alpha, bounce
//
// Danach zieht jeder Verbraucher, was nur er braucht (Farbe, org2, Laenge
// ... bzw. Drehung, Dichte, Folgeeffekt). Was im gemeinsamen Anfang
// steht, ist bei allen gleich - also fliegt das gezeichnete Teilchen
// genau dort, wo seine Folgeeffekte entstehen.

// --- Die Flags, die WIRKEN, nicht die, die in der Datei stehen ---------
//
// ParseMin und ParseMax (FxTemplate.cpp:354 und :380):
//
//     // We assume that if a min is being set that we are using physics
//     // and a bounding box
//     mFlags |= FX_USE_BBOX | FX_APPLY_PHYSICS;
//
// Wer eine Box angibt, bekommt Physik und Kastenspurtest dazu, ob
// `usePhysics` dasteht oder nicht. Der Leser (efx_read.cpp) laesst die
// Flags wie geschrieben - er soll die Datei unveraendert zurueckschreiben
// koennen. Deshalb wird es hier nachgeholt.
std::uint32_t wirksameFlags(const efx::Primitive& pr) {
    std::uint32_t f = pr.flags;
    if (pr.min.set || pr.max.set) {
        f |= efx::kFlagUseBBox | efx::kFlagApplyPhysics;
    }
    return f;
}

// Wie lange lebt der Effekt hoechstens - vom Ausloesen bis das letzte
// Teilchen stirbt?
double laengsteMs(const efx::Effect& ef) {
    double laengste = 0.0;
    for (const efx::Primitive& pr : ef.primitives) {
        // `life 5000 4000` steht oft mit dem groesseren Wert ZUERST -
        // readRange behaelt die Reihenfolge der Datei.
        const double ende =
            static_cast<double>(std::max(pr.delay.min, pr.delay.max)) +
            static_cast<double>(pr.life.set
                                    ? std::max(pr.life.min, pr.life.max)
                                    : 50.0F);
        laengste = std::max(laengste, ende);
    }
    return laengste;
}

// --- ALLE Durchgaenge, die noch leben ------------------------------------
//
// Hier stand: "den zuletzt faelligen Durchgang nehmen". Das ist falsch,
// sobald die Teilchen laenger leben als der Abstand zwischen zwei
// Durchgaengen - und genau das ist der Normalfall.
//
// `mustafar/volcano.efx`: repeatDelay 300, aber `life 5800 4200`. Im Spiel
// ueberlappen sich dort gut dreissig Durchgaenge. behaved zeigte immer nur
// den neuesten, also Teilchen im Alter 0..300 ms.
//
// Gemessen: die Saeule war 578 Einheiten hoch und zwar bei JEDEM Zeitpunkt
// gleich - waehrend die Bahn allein (v = 1000, g = -525) einen Scheitel bei
// rund 950 Einheiten ergibt. Dass sich die Zahl mit der Zeit nicht aendert,
// war der Hinweis: es lag nicht an der Bahn, sondern daran, dass kein
// Teilchen alt genug wurde.
//
// Die Obergrenze ist kein Geschmack, sondern Schutz: ein Effekt mit
// repeatDelay 10 und life 20000 ergaebe zweitausend Durchgaenge.
//
// Die Engine begrenzt nicht die Durchgaenge, sondern den Teilchenvorrat:
// MAX_EFFECTS 1200 (FxPrimitives.h:31) fuer ALLE Effekte zusammen. Das hier
// ist die grobere Schranke - sie greift frueher und je Runner, aber sie
// greift. 64 reicht fuer alles in den Effektdateien: volcano.efx braucht
// 10000/300 = 34, und das ist der laengste.
//
// `rueckblickMs`: wie weit zurueck ein Durchgang noch zaehlt. Das Netz
// nimmt die laengste Lebensdauer; die Folgeeffekte brauchen mehr, denn ein
// Todeseffekt beginnt erst, wenn das Teilchen stirbt.
//
// Die NEUESTEN zuerst: greift eine Obergrenze, fallen die aeltesten weg -
// wie in der Engine, die bei vollem Vorrat den aeltesten Platz raeumt
// (FxUtil.cpp:164).
std::vector<double> durchgaenge(const EffectInstance& inst, double nowMs,
                                double rueckblickMs) {
    std::vector<double> starts;
    if (inst.loops && inst.intervalMs > 1.0F) {
        const double since = nowMs - inst.startMs;
        if (since >= 0.0) {
            const auto iv = static_cast<double>(inst.intervalMs);
            const double n = std::floor(since / iv);
            constexpr int kMaxDurchgaenge = 64;
            const auto zurueck = static_cast<int>(
                std::min(static_cast<double>(kMaxDurchgaenge - 1),
                         std::floor(rueckblickMs / iv)));
            for (int k = 0; k <= zurueck; ++k) {
                const double b0 =
                    inst.startMs + (n - static_cast<double>(k)) * iv;
                if (b0 >= inst.startMs && b0 <= nowMs &&
                    (inst.endMs < 0.0 || b0 < inst.endMs)) {
                    starts.push_back(b0);
                }
            }
        }
    } else if (nowMs >= inst.startMs) {
        starts.push_back(inst.startMs);
    }
    return starts;
}

// Derselbe Ausgangswert ergibt dasselbe Bild.
//
// Das ist keine Bequemlichkeit: eine Vorschau, die beim Zurueckspulen
// anders aussieht, taugt nicht zum Abstimmen einer Zwischensequenz. Der
// Wert haengt am Effekt UND am Durchgang, damit zwei Wiederholungen nicht
// identisch aussehen.
//
// Ueber long long: ein Durchgang vor null (startMs -1 bei einem wartenden
// Runner) waere als unsigned-Umwandlung eines negativen double undefiniert.
unsigned mischeDurchgang(double began) {
    return static_cast<unsigned>(static_cast<long long>(began));
}
unsigned planSamen(const EffectInstance& inst, double began) {
    return inst.seed ^ (mischeDurchgang(began) * 2654435761U) ^ 1U;
}
// --- Ein Zufallsstrom JE TEILCHEN --------------------------------------
//
// Die Engine wuerfelt jedes Teilchen EINMAL beim Erzeugen (CreateEffect,
// FxScheduler.cpp). Hier lief frueher ein Strom fuer den ganzen Durchgang,
// und ein totes Teilchen zog weniger Werte als ein lebendes. Starb eines,
// bekamen alle folgenden neue Werte - Breite, Farbe, Laenge, Bild: die
// Geysire sprangen.
unsigned teilchenSamen(const EffectInstance& inst, double began,
                       std::size_t nummer) {
    return inst.seed ^ (mischeDurchgang(began) * 2654435761U) ^
           ((static_cast<unsigned>(nummer) + 1U) * 0x9E3779B9U) ^ 0x5BD1E995U;
}

// Wo der Anker (der Effekt selbst) zu einem Zeitpunkt steht und wohin er
// zeigt. Ohne EffectInstance::anker sind das `origin` und `normal`.
struct Anker {
    float org[3]{};
    float fwd[3]{0.0F, 0.0F, 1.0F};
};
Anker ankerBei(const EffectInstance& inst, double ms) {
    Anker a;
    for (int k = 0; k < 3; ++k) {
        a.org[k] = inst.origin[k];
        a.fwd[k] = inst.normal[k];
    }
    if (inst.anker) {
        float o[3] = {a.org[0], a.org[1], a.org[2]};
        float f[3] = {a.fwd[0], a.fwd[1], a.fwd[2]};
        if (inst.anker(ms, o, f)) {
            for (int k = 0; k < 3; ++k) {
                a.org[k] = o[k];
                a.fwd[k] = f[k];
            }
        }
    }
    return a;
}

struct Teilchen {
    const efx::Primitive* pr = nullptr;
    std::size_t primitiv = 0;
    std::size_t nummer = 0;       // Stelle im Zeitplan des Durchgangs
    double durchgangMs = 0.0;     // Beginn des Durchgangs, Zeitleiste
    double geborenMs = 0.0;       // Geburt, Zeitleiste
    std::uint32_t flags = 0;      // wirksame, siehe wirksameFlags
    bool relativ = false;         // FX_RELATIVE
    std::size_t medium = 0;       // gewaehlter Shader bzw. Modell
    float life = 50.0F;
    Anker geburt;                 // wo der Effekt bei der Geburt stand
    // Die Achse DIESES Teilchens (ax in CreateEffect): aus der Richtung des
    // Effekts, dann gedreht von randRotAroundFwd und ersetzt von
    // axisFromSphere. ax[0] ist die Richtung, die an Decal, Zylinder,
    // ausgerichtete Partikel, Emitterwinkel und Folgeeffekte weitergeht.
    float ax[3][3]{{0.0F, 0.0F, 1.0F}, {1.0F, 0.0F, 0.0F}, {0.0F, -1.0F, 0.0F}};
    // Bei relativ ROH (so steht es in der Datei), sonst schon in die Achse
    // des Effekts gedreht. `org` ist der Versatz zum Anker.
    V3 org;
    V3 vel;
    V3 acc;
    float grav = 0.0F;
    efx::curve::Curve sizeC;
    efx::curve::Curve alphaC;
    float bounce = 0.0F;          // elasticity - beim Blitz das Chaos
};

// Der gemeinsame Anfang, siehe oben. `rnd` steht danach bereit fuer das,
// was nur der Aufrufer braucht.
Teilchen ziehe(const EffectInstance& inst, std::size_t primitiv,
               std::size_t nummer, double durchgang, float delayMs,
               efx::sim::Random& rnd) {
    Teilchen t;
    const efx::Primitive& pr = inst.effect->primitives[primitiv];
    t.pr = &pr;
    t.primitiv = primitiv;
    t.nummer = nummer;
    t.durchgangMs = durchgang;
    t.geborenMs = durchgang + static_cast<double>(delayMs);
    t.flags = wirksameFlags(pr);
    t.relativ = inst.relativ && static_cast<bool>(inst.anker);
    // Ein Emitter waehlt sein MODELL aus derselben Liste der Medien
    // (FxScheduler.cpp:1839: emitter_model = mMediaHandles.GetHandle()).
    t.medium = waehle(rnd, pr.type == efx::PrimitiveType::Emitter
                               ? pr.models.size()
                               : pr.shaders.size());
    // Ohne `life` gilt 50 ms (CPrimitiveTemplate, FxTemplate.cpp:53).
    t.life = pr.life.set ? std::max(rnd.pick(pr.life), 1.0F) : 50.0F;
    t.geburt = ankerBei(inst, t.geborenMs);
    // --- Die Achse: AxisCopy( axis, ax ) ----------------------------------
    //
    // Der Effekt kommt mit EINER Richtung (fx_runner, Aufprallnormale);
    // ax[1] und ax[2] macht PlayEffect daraus mit MakeNormalVectors.
    for (int k = 0; k < 3; ++k) {
        t.ax[0][k] = t.geburt.fwd[k];
    }
    achseAus(t.ax[0], t.ax[1], t.ax[2]);
    // --- FX_RAND_ROT_AROUND_FWD (FxScheduler.cpp:1546) --------------------
    //
    //     RotatePointAroundVector( ax[1], ax[0], axis[1], Q_flrand(0,1) * 360 );
    //     CrossProduct( ax[0], ax[1], ax[2] );
    //
    // Gezogen wird NUR, wenn das Flag steht - so bleibt die Zugfolge aller
    // anderen Teilchen unberuehrt.
    if ((pr.spawnFlags & efx::kSpawnRandRotAroundFwd) != 0U) {
        const float alt1[3] = {t.ax[1][0], t.ax[1][1], t.ax[1][2]};
        drehePunkt(t.ax[1], t.ax[0], alt1, rnd.next() * 360.0F);
        t.ax[2][0] = t.ax[0][1] * t.ax[1][2] - t.ax[0][2] * t.ax[1][1];
        t.ax[2][1] = t.ax[0][2] * t.ax[1][0] - t.ax[0][0] * t.ax[1][2];
        t.ax[2][2] = t.ax[0][0] * t.ax[1][1] - t.ax[0][1] * t.ax[1][0];
    }
    // origin ist OERTLICH wie die Geschwindigkeit, ausser mit
    // cheapOrgCalc (FxScheduler.cpp:1553) - oder FX_RELATIVE, dann bleibt
    // alles roh und wird erst beim Zeichnen mit der AKTUELLEN Achse gedreht.
    const bool roh = t.relativ;
    const V3 o = pickVec(rnd, pr.origin);
    t.org = (roh || (pr.spawnFlags & efx::kSpawnCheapOrgCalc) != 0U)
                ? o
                : inAchse3(o, t.ax);
    // --- orgOnSphere / orgOnCylinder / axisFromSphere ---------------------
    //
    // FxScheduler.cpp:1571 ff. Bis rc569 ueberlesen - 10 Blitzdateien der
    // Mod stehen auf `orgOnCylinder axisFromSphere`, die Machtblitzketten von
    // Exegol auf `orgOnSphere axisFromSphere`. Ohne das schossen alle ihre
    // Blitze in DIESELBE Richtung (die des Runners), statt nach allen
    // Seiten.
    if ((pr.spawnFlags & efx::kSpawnOrgOnSphere) != 0U) {
        //     x = DEG2RAD( Q_flrand(0,1) * 360 );  y = DEG2RAD( Q_flrand(0,1) * 180 );
        //     temp = ( sin(x)*width*sin(y), cos(x)*width*sin(y), cos(y)*height )
        const float x = rnd.next() * 2.0F * std::numbers::pi_v<float>;
        const float y = rnd.next() * std::numbers::pi_v<float>;
        const float breite = rnd.pick(pr.radius);
        const float hoehe = rnd.pick(pr.height);
        const float temp[3] = {std::sin(x) * breite * std::sin(y),
                               std::cos(x) * breite * std::sin(y),
                               std::cos(y) * hoehe};
        t.org = t.org + V3{temp[0], temp[1], temp[2]};
        if ((pr.spawnFlags & efx::kSpawnAxisFromSphere) != 0U) {
            // VectorNormalize2( temp, ax[0] ); MakeNormalVectors( ax[0], ... )
            const float l = std::sqrt(temp[0] * temp[0] + temp[1] * temp[1] +
                                      temp[2] * temp[2]);
            if (l > 0.0F) {
                for (int k = 0; k < 3; ++k) {
                    t.ax[0][k] = temp[k] / l;
                }
                achseAus(t.ax[0], t.ax[1], t.ax[2]);
            }
        }
    } else if ((pr.spawnFlags & efx::kSpawnOrgOnCylinder) != 0U) {
        //     pt = ax[1] * radius + ax[0] * Q_flrand(-1,1) * 0.5 * height
        //     RotatePointAroundVector( temp, ax[0], pt, Q_flrand(0,1) * 360 )
        const float halbmesser = rnd.pick(pr.radius);
        const float lage = rnd.range(-1.0F, 1.0F);
        const float hoehe = rnd.pick(pr.height);
        float pt[3];
        for (int k = 0; k < 3; ++k) {
            pt[k] = t.ax[1][k] * halbmesser + t.ax[0][k] * lage * 0.5F * hoehe;
        }
        float temp[3];
        const float fwd0[3] = {t.ax[0][0], t.ax[0][1], t.ax[0][2]};
        drehePunkt(temp, fwd0, pt, rnd.next() * 360.0F);
        t.org = t.org + V3{temp[0], temp[1], temp[2]};
        if ((pr.spawnFlags & efx::kSpawnAxisFromSphere) != 0U) {
            const float l = std::sqrt(temp[0] * temp[0] + temp[1] * temp[1] +
                                      temp[2] * temp[2]);
            if (l > 0.0F) {
                for (int k = 0; k < 3; ++k) {
                    t.ax[0][k] = temp[k] / l;
                }
                // Anders als bei der Kugel NICHT MakeNormalVectors:
                //     up = (0,0,1), bei ax[0][2] == 1 stattdessen (0,1,0)
                //     ax[1] = up x ax[0];  ax[2] = ax[0] x ax[1]
                float hoch[3] = {0.0F, 0.0F, 1.0F};
                if (t.ax[0][2] == 1.0F) {
                    hoch[1] = 1.0F;
                    hoch[2] = 0.0F;
                }
                t.ax[1][0] = hoch[1] * t.ax[0][2] - hoch[2] * t.ax[0][1];
                t.ax[1][1] = hoch[2] * t.ax[0][0] - hoch[0] * t.ax[0][2];
                t.ax[1][2] = hoch[0] * t.ax[0][1] - hoch[1] * t.ax[0][0];
                t.ax[2][0] = t.ax[0][1] * t.ax[1][2] - t.ax[0][2] * t.ax[1][1];
                t.ax[2][1] = t.ax[0][2] * t.ax[1][0] - t.ax[0][0] * t.ax[1][2];
                t.ax[2][2] = t.ax[0][0] * t.ax[1][1] - t.ax[0][1] * t.ax[1][0];
            }
        }
    }
    const V3 v = pickVec(rnd, pr.velocity);
    t.vel = (roh || (pr.spawnFlags & efx::kSpawnVelIsAbsolute) != 0U)
                ? v
                : inAchse3(v, t.ax);
    const V3 a = pickVec(rnd, pr.acceleration);
    t.acc = (roh || (pr.spawnFlags & efx::kSpawnAccelIsAbsolute) != 0U)
                ? a
                : inAchse3(a, t.ax);
    t.grav = rnd.pick(pr.gravity);
    t.sizeC = curveOf(pr.size, rnd);
    t.alphaC = curveOf(pr.alpha, rnd);
    // Immer gezogen, nicht nur mit Physik: sonst haette ein Teilchen mit
    // und ohne `usePhysics` verschiedene Farben.
    t.bounce = rnd.pick(pr.elasticity);
    return t;
}

// --- Die Farbe, wie CreateEffect sie wuerfelt --------------------------
//
// Start- UND Endfarbe, dazwischen die Kurve des rgb-Blocks
// (CParticle::UpdateRGB, FxPrimitives.cpp:437). Mit
// rgbComponentInterpolation EIN Anteil fuer alle drei Kanaele
// (FxScheduler.cpp:1738): die Farbe liegt auf der Linie zwischen den
// beiden Farben, nicht irgendwo im Wuerfel. Fehlt der rgb-Block, ist die
// Farbe WEISS (mRedStart und die beiden anderen stehen auf 1.0).
struct Farbe {
    V3 start{1.0F, 1.0F, 1.0F};
    V3 ende{1.0F, 1.0F, 1.0F};
    efx::curve::Curve kurve;
    bool da = false;
};
Farbe zieheFarbe(const efx::Primitive& pr, efx::sim::Random& rnd) {
    Farbe f;
    if (!pr.rgb.present) {
        return f;
    }
    f.da = true;
    const bool gemeinsam =
        (pr.spawnFlags & efx::kSpawnRgbComponentInterp) != 0U;
    const float anteil = gemeinsam ? rnd.next() : 0.0F;
    auto farbe = [&](const efx::Vec3Range& vr) {
        V3 c{1.0F, 1.0F, 1.0F};
        if (!vr.set) {
            return c;
        }
        if (!gemeinsam) {
            return pickVec(rnd, vr);
        }
        float* aus[3] = {&c.x, &c.y, &c.z};
        for (int k = 0; k < 3; ++k) {
            const float lo = vr.min[k];
            const float hi = vr.ranged ? vr.max[k] : vr.min[k];
            *aus[k] = lo + (hi - lo) * anteil;
        }
        return c;
    };
    f.start = farbe(pr.rgb.start);
    f.ende = farbe(pr.rgb.end);
    f.kurve.flags = efx::curve::ausDatei(pr.rgb.curveFlags);
    f.kurve.parm = rnd.pick(pr.rgb.parm);
    return f;
}
V3 farbeBei(const Farbe& f, float alter, float life) {
    if (!f.da) {
        return f.start;
    }
    const float rp = efx::curve::resolveParm(f.kurve, 0.0F, life);
    const float bb = efx::curve::bias(f.kurve, alter, 0.0F, life, rp);
    return V3{f.start.x * bb + f.ende.x * (1.0F - bb),
              f.start.y * bb + f.ende.y * (1.0F - bb),
              f.start.z * bb + f.ende.z * (1.0F - bb)};
}

// --- Der Spurtest eines Teilchens --------------------------------------
//
// Ohne `usePhysics` oder ohne Karte gar keiner. Mit `useBBox` (121 mal in
// den Effektdateien, fast immer an Truemmern) spurt die Engine mit einem
// KASTEN (CParticle::UpdateOrigin, FxPrimitives.cpp:285):
//
//     theFxHelper.Trace( &trace, mOrigin1, mMin, mMax, new_origin, ... );
//
// Ein Kastentest gegen die Dreiecke der Karte gibt es in behaved nicht.
// Nachgebildet wird, was man davon SIEHT: das Teilchen bleibt um seine
// Ausdehnung vor der Flaeche stehen, statt mit dem Mittelpunkt darin zu
// stecken - ein Brocken mit `min -3 -3 -3` liegt auf dem Boden, nicht zur
// Haelfte darin. Dazu wird der Strahl in Flugrichtung um so viel
// verlaengert, wie der Kasten in diese Richtung ueber seinen Mittelpunkt
// hinausreicht, und der Treffer um genau so viel zurueckgenommen.
//
// NAEHERUNG: an einer Kante, die der Strahl knapp verfehlt, haengt ein
// echter Kasten trotzdem - dieser hier nicht.
efx::sim::TraceFn spurFuer(const BspGeometry* geo, const efx::Primitive& pr,
                           std::uint32_t flags) {
    if (geo == nullptr || (flags & efx::kFlagApplyPhysics) == 0U) {
        return {};
    }
    auto strahl = [geo](const V3& von, const V3& bis, V3& punkt, V3& normale,
                        float& anteil) {
        const float a[3] = {von.x, von.y, von.z};
        const float b[3] = {bis.x, bis.y, bis.z};
        const TraceTreffer t = traceRay(*geo, a, b);
        if (!t.hit) {
            return false;
        }
        punkt = V3{t.point[0], t.point[1], t.point[2]};
        normale = V3{t.normal[0], t.normal[1], t.normal[2]};
        anteil = t.fraction;
        return true;
    };
    if ((flags & efx::kFlagUseBBox) == 0U) {
        return [strahl](const V3& von, const V3& bis, V3& punkt, V3& normale) {
            float anteil = 1.0F;
            return strahl(von, bis, punkt, normale, anteil);
        };
    }
    const std::array<float, 3> lo{pr.min.min[0], pr.min.min[1], pr.min.min[2]};
    const std::array<float, 3> hi{pr.max.min[0], pr.max.min[1], pr.max.min[2]};
    return [strahl, lo, hi](const V3& von, const V3& bis, V3& punkt,
                            V3& normale) {
        const V3 d = bis - von;
        const float len = efx::camera::length(d);
        float anteil = 1.0F;
        if (len < 1e-6F) {
            return strahl(von, bis, punkt, normale, anteil);
        }
        const V3 dir = d * (1.0F / len);
        // Wie weit reicht der Kasten in Flugrichtung ueber den Mittelpunkt?
        const float komp[3] = {dir.x, dir.y, dir.z};
        float weit = 0.0F;
        for (std::size_t k = 0; k < 3; ++k) {
            weit += komp[k] * (komp[k] > 0.0F ? hi[k] : lo[k]);
        }
        if (weit <= 0.0F) {
            return strahl(von, bis, punkt, normale, anteil);
        }
        const V3 lang = bis + dir * weit;
        if (!strahl(von, lang, punkt, normale, anteil)) {
            return false;
        }
        const float zurueck = std::max(0.0F, anteil * (len + weit) - weit);
        punkt = von + dir * zurueck;
        return true;
    };
}

// --- Der Merkzettel fuer Bahnen ---------------------------------------
//
// Gemessen an md_ga2_jedi (drei rocket/explosion.efx-Runner, "Chunkies":
// je 15 bis 25 Truemmer mit `life 1e+004 1.9e+004` und usePhysics) und
// md_ch_palp_ep9 (exegol/fogsprites.efx: Nebelsprites mit Modell,
// `life 1.6e+004`, sinken langsam): mit jedem Truemmerstueck und jedem
// Sprite als eigenem Teilchen standen hunderte bis tausende Bahnen im Bild,
// und JEDE wurde je Bild vom Ausloesen an neu geschritten - 200 bis 400 ms
// allein fuer die Modelle.
//
// flugbahn ist eine reine Funktion seiner Eingaben. Also wird je Bahn der
// Stand nach dem letzten vollen Schritt gemerkt (efx::sim::FlugZustand),
// und das naechste Bild setzt dort fort - bitgleich, keine Naeherung: die
// Proben in efxtest.cpp rechnen dieselbe Bahn einmal aus dem Merkzettel und
// einmal frisch. Wer liegt oder an der Wand gestorben ist, kostet danach
// gar nichts mehr.
//
// Der Schluessel sind die Bits ALLER Eingaben samt Kastenmass und einem
// Fingerabdruck der Karte - eine andere Karte an derselben Adresse
// (App::geo wird wiederverwendet) darf keinen Treffer ergeben. Voll wird
// er nie: bei zu vielen Eintraegen wird er geleert.
struct FlugSchluessel {
    std::array<std::uint32_t, 22> w{};
    bool operator==(const FlugSchluessel& o) const { return w == o.w; }
};
struct FlugSchluesselHash {
    std::size_t operator()(const FlugSchluessel& k) const {
        std::uint64_t h = 1469598103934665603ULL;
        for (const std::uint32_t x : k.w) {
            h ^= x;
            h *= 1099511628211ULL;
        }
        return static_cast<std::size_t>(h);
    }
};
std::uint32_t bitsVon(float f) {
    std::uint32_t u = 0;
    static_assert(sizeof(u) == sizeof(f));
    std::memcpy(&u, &f, sizeof(u));
    return u;
}
// Die Eintraege liegen als unique_ptr im Merkzettel: ein Zustand traegt
// jetzt sein Spurbuch (efx::sim::FlugZustand) und waere zu teuer, um ihn je
// Aufruf hinein- und herauszukopieren. Wer rechnet, nimmt den Eintrag unter
// der Sperre HERAUS und legt ihn danach zurueck - so rechnet nie ein Faden
// an einem Zustand, den ein anderer gerade fortschreibt. Fragt ein zweiter
// Faden dieselbe Bahn, waehrend sie draussen ist, faengt er mit einem
// leeren Zustand an: langsamer, aber dasselbe Ergebnis.
std::mutex g_flugMerkSperre;
std::unordered_map<FlugSchluessel, std::unique_ptr<efx::sim::FlugZustand>,
                   FlugSchluesselHash>
    g_flugMerk;

FlugSchluessel flugSchluessel(const V3& start, const Teilchen& t,
                              const BspGeometry* geo) {
    const bool toetet = (t.flags & efx::kFlagKillOnImpact) != 0U;
    FlugSchluessel k;
    const float roh[] = {start.x, start.y, start.z, t.vel.x, t.vel.y, t.vel.z,
                         t.acc.x, t.acc.y, t.acc.z, t.grav,  t.bounce,
                         t.pr->min.min[0], t.pr->min.min[1], t.pr->min.min[2],
                         t.pr->max.min[0], t.pr->max.min[1], t.pr->max.min[2]};
    std::size_t i = 0;
    for (const float f : roh) {
        k.w[i++] = bitsVon(f);
    }
    k.w[i++] = (toetet ? 1U : 0U) | ((t.flags & efx::kFlagUseBBox) != 0U ? 2U : 0U);
    const auto adr = reinterpret_cast<std::uintptr_t>(geo);
    k.w[i++] = static_cast<std::uint32_t>(adr);
    k.w[i++] = static_cast<std::uint32_t>(static_cast<std::uint64_t>(adr) >> 32U);
    k.w[i++] = static_cast<std::uint32_t>(geo->verts.size());
    k.w[i] = static_cast<std::uint32_t>(geo->surfaces.size()) ^
             (geo->verts.empty() ? 0U : bitsVon(geo->verts[0].xyz[0]));
    return k;
}

std::unique_ptr<efx::sim::FlugZustand> holeFlug(const FlugSchluessel& k) {
    {
        const std::lock_guard<std::mutex> sperre(g_flugMerkSperre);
        const auto hat = g_flugMerk.find(k);
        if (hat != g_flugMerk.end() && hat->second) {
            return std::move(hat->second);
        }
    }
    return std::make_unique<efx::sim::FlugZustand>();
}

void gibFlug(const FlugSchluessel& k, std::unique_ptr<efx::sim::FlugZustand> z) {
    const std::lock_guard<std::mutex> sperre(g_flugMerkSperre);
    constexpr std::size_t kMaxMerk = 65536;
    if (g_flugMerk.size() >= kMaxMerk) {
        g_flugMerk.clear();
    }
    auto& platz = g_flugMerk[k];
    // Hat ein anderer Faden inzwischen weiter gerechnet, gewinnt das
    // laengere Buch.
    if (!platz || platz->bekannt < z->bekannt) {
        platz = std::move(z);
    }
}

efx::sim::FlugErgebnis flugMitMerk(const V3& start, const Teilchen& t, float ts,
                                   const efx::sim::TraceFn& spur,
                                   const BspGeometry* geo) {
    const bool toetet = (t.flags & efx::kFlagKillOnImpact) != 0U;
    const FlugSchluessel k = flugSchluessel(start, t, geo);
    std::unique_ptr<efx::sim::FlugZustand> z = holeFlug(k);
    const efx::sim::FlugErgebnis e = efx::sim::flugbahnWeiter(
        start, t.vel, t.acc, t.grav, ts, t.bounce, toetet, spur, *z);
    gibFlug(k, std::move(z));
    return e;
}

// --- Wo ein Teilchen jetzt ist ----------------------------------------
struct Lage {
    V3 welt;                      // Weltlage
    bool tot = false;             // killOnImpact hat zugeschlagen
    efx::sim::FlugErgebnis bahn;  // bei bewegten Typen; Punkte in WELT
    V3 richtung;                  // Flugrichtung in Welt (fuer den Tail)
};

// `jetztMs` ist die Zeitleiste (fuer den Anker eines relativen Teilchens),
// `alterMs` das Alter des Teilchens.
Lage lageBei(const Teilchen& t, const EffectInstance& inst, double jetztMs,
             float alterMs, const BspGeometry* geo) {
    Lage l;
    const efx::Primitive& pr = *t.pr;
    const float ts = alterMs * 0.001F;
    if (bewegt(pr.type)) {
        if (t.relativ && pr.type != efx::PrimitiveType::Emitter) {
            // --- FX_RELATIVE: am AKTUELLEN Anker -----------------------
            //
            // CParticle::Update (FxPrimitives.cpp:166 ff.), geschlossen:
            //
            //     org      = bolt + ax * mOrgOffset
            //     real_vel = ax * mVel;  real_vel[2] += 0.5 * mGravity * t
            //     real_vel += t * (ax * mAccel)
            //     mOrigin1 = org + t * real_vel
            //
            // mAccel traegt die Schwerkraft schon (CreateEffect:
            // accel[2] += mGravity, im ROHEN System, also entlang der
            // dritten Achse des Ankers), und mGravity wirkt ein zweites
            // Mal nach Welt-z. Das sieht nach einem Versehen aus und ist
            // eines - aber es ist das, was das Spiel zeigt. Kein
            // Spurtest: der relative Zweig ruft UpdateOrigin nie auf.
            const Anker a = ankerBei(inst, jetztMs);
            V3 acc2 = t.acc;
            acc2.z += t.grav;
            const V3 lokal = t.org + t.vel * ts + acc2 * (ts * ts);
            const V3 w = inAchse(lokal, a.fwd, false);
            l.welt = V3{a.org[0] + w.x, a.org[1] + w.y,
                        a.org[2] + w.z + 0.5F * t.grav * ts * ts};
            const V3 dl = inAchse(t.vel + acc2 * (2.0F * ts), a.fwd, false);
            l.richtung = V3{dl.x, dl.y, dl.z + t.grav * ts};
            l.bahn.position = l.welt;
            return l;
        }
        // Ein Emitter kennt FX_RELATIVE nicht - CEmitter::Update hat den
        // Zweig auskommentiert ("FIXME: Handle Relative and Bolted
        // Effects", FxPrimitives.cpp:1499). CreateEffect hat ihm aber schon
        // den Ursprung der WELT als Bezug gegeben (angeheftete Effekte
        // kommen mit vec3_origin, FxScheduler.cpp:1473). Er startet dann
        // also bei (0,0,0) plus Versatz - so im Spiel, so hier.
        V3 start = t.org;
        if (!t.relativ) {
            start = start + V3{t.geburt.org[0], t.geburt.org[1],
                               t.geburt.org[2]};
        }
        // --- Die Bahn in WELTkoordinaten ------------------------------
        //
        // Bis rc568 bekam flugbahn den VERSATZ zum Runner als Startpunkt,
        // und der Spurtest lief damit um den Nullpunkt der Karte herum,
        // nicht am Effekt: Funken prallten an Waenden ab, die dort stehen,
        // wo die Karte ihren Ursprung hat - oder fielen durch den Boden.
        // Jetzt startet die Bahn an der Weltlage.
        const efx::sim::TraceFn spur = spurFuer(geo, pr, t.flags);
        if (spur) {
            l.bahn = flugMitMerk(start, t, ts, spur, geo);
            l.tot = l.bahn.gestorben;
        } else {
            l.bahn.position =
                efx::sim::positionAt(start, t.vel, t.acc, t.grav, ts);
        }
        l.welt = l.bahn.position;
        // Die Bewegungsrichtung an dieser Stelle. Die Engine nimmt die
        // Strecke seit dem letzten Bild; wir haben keine Bilder, also die
        // Geschwindigkeit samt Beschleunigung zum jetzigen Zeitpunkt.
        l.richtung = V3{t.vel.x + t.acc.x * ts, t.vel.y + t.acc.y * ts,
                        t.vel.z + (t.acc.z + t.grav) * ts};
        return l;
    }
    // --- Die stehenden Typen --------------------------------------------
    if (t.relativ) {
        const bool haengt = pr.type == efx::PrimitiveType::Line ||
                            pr.type == efx::PrimitiveType::Electricity ||
                            pr.type == efx::PrimitiveType::Cylinder;
        if (haengt) {
            // CLine::Update (:817), CElectricity::Update (:938),
            // CCylinder::Update (:1319): VectorAdd( mOrigin1, mOrgOffset )
            // - der Versatz wird hier NICHT gedreht, anders als beim
            // Teilchen.
            const Anker a = ankerBei(inst, jetztMs);
            l.welt = V3{a.org[0] + t.org.x, a.org[1] + t.org.y,
                        a.org[2] + t.org.z};
        } else {
            // Decal, Light, Sound, Flash, FxRunner: kein relativer Zweig,
            // also der Ursprung der Welt als Bezug (siehe oben).
            l.welt = t.org;
        }
    } else {
        l.welt = V3{t.geburt.org[0] + t.org.x, t.geburt.org[1] + t.org.y,
                    t.geburt.org[2] + t.org.z};
    }
    l.bahn.position = l.welt;
    return l;
}

// --- Die Bahnen VORAB rechnen, auf allen Kernen ----------------------
//
// Nach Spurbuch und SpurHilfe (bspgeo.cpp) blieb an md_ta_sith (intro_sith)
// eine Last, die sich nicht wegrechnen laesst: jedes Teilchen muss jeden
// seiner Schritte EINMAL spuren. Der Reihe nach abgespielt sind das je
// 500-ms-Bild rund 400000 Spurtests (7000 fliegende Funken zu je 250
// Schritten), und wer mitten in die Szene springt, zahlt fuer jedes lebende
// Teilchen die ganze Bahn seit seiner Geburt: gemessen bis 1,1 s fuer EIN
// Bild.
//
// Diese Arbeit ist aber je Bahn unabhaengig - eine reine Funktion ihrer
// Eingaben, abgelegt unter ihrem Schluessel im Merkzettel. Deshalb sammelt
// vorrechnen() vor dem eigentlichen Durchlauf alle Bahnen ein, die der
// Durchlauf gleich brauchen wird und die noch Spurtests kosten
// (flugOhneSpur), und rechnet sie auf mehreren Faeden bis zu ihrem Alter.
// Der Durchlauf selbst bleibt unveraendert und der Reihe nach: er findet
// seine Bahnen fertig im Merkzettel. Am Bild aendert das nichts - welcher
// Faden eine Bahn gerechnet hat, steht nirgends drin, und die Reihenfolge
// der Vierecke macht weiterhin allein der Durchlauf.
//
// Gemessen an md_ta_sith (intro_sith, 134 Bilder im Abstand von 500 ms,
// i9-14900HX, alle drei Umbauten zusammen): der Reihe nach hoechstens
// 27 ms je Bild statt 1331 ms, in zufaelliger Reihenfolge hoechstens
// 118 ms statt 6232 ms - und jedes Netz Byte fuer Byte wie vorher.
struct Vorrechnung {
    V3 start;
    Teilchen t;
    float ts = 0.0F;   // Alter in Sekunden, genau wie lageBei es rechnet
};

// Eine Bahn fuer lageBei(t, ..., alterMs) vormerken - dieselben Bedingungen
// wie dort (bewegt, nicht relativ bzw. Emitter, mit Spurtest).
void merkeVor(const Teilchen& t, float alterMs, const BspGeometry* geo,
              std::vector<Vorrechnung>& aus) {
    const efx::Primitive& pr = *t.pr;
    if (geo == nullptr || !bewegt(pr.type) ||
        (t.relativ && pr.type != efx::PrimitiveType::Emitter) ||
        (t.flags & efx::kFlagApplyPhysics) == 0U) {
        return;
    }
    V3 start = t.org;
    if (!t.relativ) {
        start = start + V3{t.geburt.org[0], t.geburt.org[1], t.geburt.org[2]};
    }
    aus.push_back(Vorrechnung{start, t, alterMs * 0.001F});
}

// Alle Teilchen, fuer die buildEffectMesh (drawable) oder emitterModelsAt
// (Emitter mit Modell) zu `nowMs` lageBei aufruft - in derselben Zugfolge
// wie dort, damit Schluessel und Alter dieselben sind.
void sammleBahnen(const std::vector<EffectInstance>& live, double nowMs,
                  const BspGeometry* geo, bool nurModelle,
                  std::vector<Vorrechnung>& aus) {
    for (const EffectInstance& inst : live) {
        if (inst.effect == nullptr) {
            continue;
        }
        bool braucht = false;
        for (const efx::Primitive& pr : inst.effect->primitives) {
            braucht = braucht || (bewegt(pr.type) &&
                                  (wirksameFlags(pr) & efx::kFlagApplyPhysics) != 0U);
        }
        if (!braucht) {
            continue;
        }
        for (const double began : durchgaenge(inst, nowMs, laengsteMs(*inst.effect))) {
            efx::sim::Random planRnd(planSamen(inst, began));
            const std::vector<efx::sim::Spawn> spawns =
                efx::sim::schedule(*inst.effect, planRnd);
            for (std::size_t si = 0; si < spawns.size(); ++si) {
                const auto at = static_cast<std::size_t>(spawns[si].primitiveIndex);
                if (at >= inst.effect->primitives.size()) {
                    continue;
                }
                const efx::Primitive& pr = inst.effect->primitives[at];
                const bool modell = pr.type == efx::PrimitiveType::Emitter &&
                                    (pr.flags & efx::kFlagAttachedModel) != 0U &&
                                    !pr.models.empty();
                if (!(modell || (!nurModelle && drawable(pr.type))) || !bewegt(pr.type)) {
                    continue;
                }
                efx::sim::Random rnd(teilchenSamen(inst, began, si));
                const Teilchen t = ziehe(inst, at, si, began, spawns[si].timeMs, rnd);
                const double age = nowMs - t.geborenMs;
                if (age < 0.0 || age > static_cast<double>(t.life)) {
                    continue;
                }
                merkeVor(t, static_cast<float>(age), geo, aus);
            }
        }
    }
}

// Rechnet die gesammelten Bahnen, die noch Spurtests kosten, auf
// Hilfsfaeden. Ohne Arbeit oder mit wenig davon bleibt alles auf dem
// aufrufenden Faden - einen Faden anzulegen kostet mehr als ein paar
// Bahnen.
void vorrechnen(const std::vector<Vorrechnung>& alle, const BspGeometry* geo) {
    if (geo == nullptr || alle.empty()) {
        return;
    }
    std::vector<const Vorrechnung*> offen;
    {
        const std::lock_guard<std::mutex> sperre(g_flugMerkSperre);
        for (const Vorrechnung& v : alle) {
            const auto hat = g_flugMerk.find(flugSchluessel(v.start, v.t, geo));
            if (hat != g_flugMerk.end() && hat->second &&
                efx::sim::flugOhneSpur(*hat->second, v.ts)) {
                continue;
            }
            offen.push_back(&v);
        }
    }
    // Dieselbe Bahn kann mehrfach gesammelt sein (Durchlauf und Modell);
    // doppelt zu rechnen waere nur Verschwendung, falsch waere es nicht.
    auto rechne = [&](const Vorrechnung& v) {
        const efx::sim::TraceFn spur = spurFuer(geo, *v.t.pr, v.t.flags);
        if (spur) {
            (void)flugMitMerk(v.start, v.t, v.ts, spur, geo);
        }
    };
    constexpr std::size_t kJeFaden = 64;   // darunter lohnt kein Faden
    const unsigned kerne = std::max(1U, std::thread::hardware_concurrency());
    const auto faeden = static_cast<unsigned>(
        std::min<std::size_t>(std::min(kerne, 32U), offen.size() / kJeFaden));
    if (faeden <= 1U) {
        for (const Vorrechnung* v : offen) {
            rechne(*v);
        }
        return;
    }
    std::atomic<std::size_t> naechste{0};
    auto arbeite = [&]() {
        for (;;) {
            constexpr std::size_t kHappen = 8;
            const std::size_t von = naechste.fetch_add(kHappen);
            if (von >= offen.size()) {
                return;
            }
            const std::size_t bis = std::min(offen.size(), von + kHappen);
            for (std::size_t i = von; i < bis; ++i) {
                rechne(*offen[i]);
            }
        }
    };
    std::vector<std::thread> mannschaft;
    mannschaft.reserve(faeden - 1U);
    for (unsigned i = 1; i < faeden; ++i) {
        mannschaft.emplace_back(arbeite);
    }
    arbeite();
    for (std::thread& th : mannschaft) {
        th.join();
    }
}

// --- Das zweite Ende einer Line oder eines Blitzes ----------------------
//
// FxScheduler.cpp:1675 ff., in dieser Reihenfolge:
//
//   org2FromTrace   von org aus FX_MAX_TRACE_DIST (32768) entlang ax[0]
//                   spuren, das Ende ist der Treffer. Mit org2isOffset
//                   wird origin2 VOR dem Spurtest auf das Ziel addiert.
//                   traceImpactFx spielt dort den Aufpralleffekt.
//   cheapOrg2Calc   origin2 roh - und zwar ALS WELTLAGE, ohne den Ort des
//                   Effekts (VectorSet ohne VectorAdd). Gedacht fuer
//                   Code, der die Vorlage kopiert und origin2 selbst
//                   einsetzt (fx_target_beam); als Runner gespielt zeigt
//                   die Line auf den Nullpunkt der Karte, im Spiel wie hier.
//   sonst           origin2 in die Achse gedreht, plus Ort des Effekts.
//
// Bis rc568 stand hier origin2 roh plus Ort - fuer einen nach oben
// zeigenden Runner (die Vorgabe) zeigte "100 0 0" damit nach +x statt nach
// oben.
//
// FX_RELATIVE (CLine::Update, FxPrimitives.cpp:855): Ende = Anfang +
// aktuelle Achse * origin2.
struct Ende2 {
    float welt[3]{};
    bool traf = false;            // nur org2FromTrace
    float normale[3]{0.0F, 0.0F, 1.0F};
};
Ende2 zieheEnde2(const Teilchen& t, const EffectInstance& inst, double jetztMs,
                 const float start[3], efx::sim::Random& rnd,
                 const BspGeometry* geo) {
    Ende2 e;
    const efx::Primitive& pr = *t.pr;
    const V3 o2 = pickVec(rnd, pr.origin2);
    if (t.relativ) {
        const Anker a = ankerBei(inst, jetztMs);
        const V3 w = inAchse(o2, a.fwd, false);
        e.welt[0] = start[0] + w.x;
        e.welt[1] = start[1] + w.y;
        e.welt[2] = start[2] + w.z;
        return e;
    }
    const bool billig = (pr.spawnFlags & efx::kSpawnCheapOrg2Calc) != 0U;
    if ((pr.spawnFlags & efx::kSpawnOrg2FromTrace) != 0U) {
        constexpr float kMaxSpur = 32768.0F;   // FX_MAX_TRACE_DIST, FxScheduler.h:41
        const float* f = t.ax[0];
        float ziel[3] = {start[0] + f[0] * kMaxSpur, start[1] + f[1] * kMaxSpur,
                         start[2] + f[2] * kMaxSpur};
        if ((pr.spawnFlags & efx::kSpawnOrg2IsOffset) != 0U) {
            const V3 w = billig ? o2 : inAchse3(o2, t.ax);
            ziel[0] += w.x;
            ziel[1] += w.y;
            ziel[2] += w.z;
        }
        for (int k = 0; k < 3; ++k) {
            e.welt[k] = ziel[k];
        }
        if (geo != nullptr) {
            const TraceTreffer tr = traceRay(*geo, start, ziel);
            if (tr.hit) {
                e.traf = true;
                for (int k = 0; k < 3; ++k) {
                    e.welt[k] = tr.point[k];
                    e.normale[k] = tr.normal[k];
                }
            }
        }
        return e;
    }
    if (billig) {
        e.welt[0] = o2.x;
        e.welt[1] = o2.y;
        e.welt[2] = o2.z;
        return e;
    }
    const V3 w = inAchse3(o2, t.ax);
    e.welt[0] = t.geburt.org[0] + w.x;
    e.welt[1] = t.geburt.org[1] + w.y;
    e.welt[2] = t.geburt.org[2] + w.z;
    return e;
}

// --- vectoangles und AngleVectors (q_math.c), fuer das Emittermodell ----
void vectoangles(const float v[3], float ang[3]) {
    float yaw = 0.0F;
    float pitch = 0.0F;
    if (v[1] == 0.0F && v[0] == 0.0F) {
        pitch = (v[2] > 0.0F) ? 90.0F : 270.0F;
    } else {
        if (v[0] != 0.0F) {
            yaw = std::atan2(v[1], v[0]) * 180.0F / std::numbers::pi_v<float>;
        } else {
            yaw = (v[1] > 0.0F) ? 90.0F : 270.0F;
        }
        if (yaw < 0.0F) {
            yaw += 360.0F;
        }
        const float vorn = std::sqrt(v[0] * v[0] + v[1] * v[1]);
        pitch = std::atan2(v[2], vorn) * 180.0F / std::numbers::pi_v<float>;
        if (pitch < 0.0F) {
            pitch += 360.0F;
        }
    }
    ang[0] = -pitch;
    ang[1] = yaw;
    ang[2] = 0.0F;
}
void vorneAus(const float ang[3], float fwd[3]) {
    const float p = ang[0] * std::numbers::pi_v<float> / 180.0F;
    const float y = ang[1] * std::numbers::pi_v<float> / 180.0F;
    fwd[0] = std::cos(p) * std::cos(y);
    fwd[1] = std::cos(p) * std::sin(y);
    fwd[2] = -std::sin(p);
}

// --- Die Drehung eines Emitters -----------------------------------------
//
// CreateEffect, case Emitter (FxScheduler.cpp:1831):
//
//     VectorSet( ang, mAngle1.GetVal(), mAngle2.GetVal(), mAngle3.GetVal() );
//     vectoangles( ax[0], temp );
//     VectorAdd( ang, temp, ang );
//     VectorSet( ang_delta, mAngle1Delta..., mAngle2Delta..., ... );
//
// Und CEmitter::UpdateAngles (FxPrimitives.cpp:1542):
//
//     VectorMA( mAngles, mFrameTime * 0.01f, mAngleDelta, mAngles );
//
// Ueber die Zeit also angleDelta * 0.01 * alterInMs. Mit einer Ausnahme:
// "If the thing is no longer moving, kill the angle delta" (ebenda:1525) -
// sobald sich der Ort nicht mehr aendert, wird angleDelta JE BILD mit 0.6
// multipliziert. Aufsummiert laeuft die Drehung noch 0.6 + 0.36 + ... =
// 1.5 Bilder weiter; bei den 60 Bildern je Sekunde, von denen die Engine
// dort ausgeht ("we think at about a 60hz rate", :1406), sind das 25 ms.
struct Dreh {
    float start[3]{};
    float delta[3]{};
};
Dreh zieheDreh(const Teilchen& t, efx::sim::Random& rnd) {
    Dreh d;
    const V3 a = pickVec(rnd, t.pr->angles);
    const V3 da = pickVec(rnd, t.pr->anglesDelta);
    float basis[3];
    vectoangles(t.ax[0], basis);
    d.start[0] = a.x + basis[0];
    d.start[1] = a.y + basis[1];
    d.start[2] = a.z + basis[2];
    d.delta[0] = da.x;
    d.delta[1] = da.y;
    d.delta[2] = da.z;
    return d;
}
void drehungBei(const Dreh& d, const Teilchen& t, const Lage& l, float alterMs,
                float ang[3]) {
    constexpr float kNachlaufMs = 25.0F;   // 1.5 Bilder bei 60 Hz, s.o.
    float ms = alterMs;
    if (l.bahn.liegt) {
        ms = std::min(alterMs, l.bahn.liegtSekunde * 1000.0F + kNachlaufMs);
    } else if (t.vel.x == 0.0F && t.vel.y == 0.0F && t.vel.z == 0.0F &&
               t.acc.x == 0.0F && t.acc.y == 0.0F && t.acc.z == 0.0F &&
               t.grav == 0.0F) {
        // Steht von Anfang an still: VectorCompare trifft im ersten Bild.
        ms = std::min(alterMs, kNachlaufMs);
    }
    for (int k = 0; k < 3; ++k) {
        ang[k] = d.start[k] + d.delta[k] * 0.01F * ms;
    }
}

// =========================================================================
// Electricity: der gezackte Blitz
// =========================================================================
//
// Die Engine zeichnet ihn nicht in FxPrimitives.cpp, sondern im Zeichner:
// CElectricity::Draw reicht Anfang, Ende, mChaos (angles[0]) und die
// Lebensdauer (angles[1]) als RT_ELECTRICITY weiter, und
// RB_SurfaceElectricity (tr_surface.cpp:860) baut daraus die Zacken.
//
// Nachgebaut Zeile fuer Zeile - DoBoltSeg (:768), ApplyShape (:723),
// CreateShape (:709), DoLine2 (:301). Die Zahlen darin (16er-Schritte,
// 3/7 fuer die Abweichung, 0.93 und 0.8 fuer die Verzweigung, hoechstens
// drei Aeste, 80 Einheiten Aststreuung, Tiefe 2) sind die der Engine.
//
// DER ZUFALL, und darauf kommt es an:
//
//   * Die Bahn des Blitzes zieht aus `e->frame` mit Q_random/Q_crandom
//     (q_math.c:197 ff.: seed = 69069 * seed + 1). `frame` wird einmal beim
//     Erzeugen gewuerfelt (CElectricity::Initialize, FxPrimitives.cpp:890:
//     Q_flrand(0,1) * 1265536) - im Spiel steht die Grobform eines Blitzes
//     also ueber sein ganzes Leben. Genau so hier: der Samen kommt aus dem
//     Zufallsstrom des Teilchens.
//   * Die Feinzacken (CreateShape) zieht die Engine dagegen aus dem
//     GLOBALEN Q_flrand - sie flackern im Spiel in jedem Bild neu. Eine
//     Vorschau, die beim Zurueckspulen dasselbe zeigen muss, kann das nicht
//     nachmachen. Sie bekommen deshalb einen eigenen Strom vom selben
//     Samen: dieselbe Verteilung, dieselben Groessen, aber stehend. Das ist
//     die EINE bewusste Abweichung.
//
// RF_TAPERED, RF_FORKED und RF_GROW kommen aus FX_TAPER, FX_BRANCH und
// FX_GROW - in der Datei "useModel", "usePhysics" und "useBBox" (siehe
// kFlagElectricity* in effect.h).

// Q_rand/Q_random/Q_crandom mit Samen. Unsigned statt int: die unteren
// 16 Bit sind bei Ueberlauf dieselben, und nur die werden gelesen.
struct BlitzZufall {
    std::uint32_t s = 1U;
    float random() {
        s = 69069U * s + 1U;
        return static_cast<float>(s & 0xffffU) / 65536.0F;
    }
    float crandom() { return 2.0F * (random() - 0.5F); }
};

struct Blitz {
    BspMesh* mesh = nullptr;
    const float* viewDir = nullptr;
    const std::uint8_t* rgba = nullptr;
    int* quads = nullptr;
    int grenze = kMaxQuadsPerEffect;
    float rechts[3]{};        // EINE Breitenrichtung fuer den ganzen Blitz
    float ende[3]{};          // e->oldorigin: das (gewachsene) Ende
    float chaos = 0.0F;       // e->angles[0]
    bool verjuengt = false;   // RF_TAPERED
    bool gegabelt = false;    // RF_FORKED
    int aeste = 3;            // f_count
    BlitzZufall bahn;         // &e->frame
    BlitzZufall form;         // statt Q_flrand, siehe oben
    // sh1/sh2 sind in der Engine `static` - und ApplyShape liest sh2 NACH
    // dem rekursiven Aufruf, der sie inzwischen neu gewuerfelt hat. Das
    // gehoert zum Aussehen, also hier genauso geteilt.
    float sh1[3]{};
    float sh2[3]{};

    void createShape() {
        sh1[0] = 0.66F;
        sh1[1] = 0.08F + form.crandom() * 0.02F;
        sh1[2] = 0.08F + form.crandom() * 0.02F;
        // "a point on one side of the ideal line, then the other point on
        // the other side" (tr_surface.cpp:716)
        sh2[0] = 0.33F;
        sh2[1] = -sh1[1] + form.crandom() * 0.02F;
        sh2[2] = -sh1[2] + form.crandom() * 0.02F;
    }

    void doLine2(const float a[3], const float b[3], float wa, float wb,
                 float tca, float tcb) {
        if (*quads >= grenze) {
            return;
        }
        addBand2(*mesh, a, b, rechts, wa, wb, tca, tcb, rgba, viewDir);
        ++*quads;
    }

    void applyShape(const float start[3], const float end[3], float sradius,
                    float eradius, int count, float startPerc, float endPerc) {
        if (count < 1) {
            doLine2(start, end, sradius, eradius, startPerc, endPerc);
            return;
        }
        createShape();
        float fwd[3] = {end[0] - start[0], end[1] - start[1], end[2] - start[2]};
        const float l = std::sqrt(fwd[0] * fwd[0] + fwd[1] * fwd[1] +
                                  fwd[2] * fwd[2]);
        if (l > 0.0F) {
            for (float& c : fwd) {
                c /= l;
            }
        }
        const float dis = l * 0.7F;
        float rt[3];
        float up[3];
        achseAus(fwd, rt, up);
        float point1[3];
        float perc = sh1[0];
        for (int k = 0; k < 3; ++k) {
            point1[k] = start[k] * perc + end[k] * (1.0F - perc) +
                        rt[k] * dis * sh1[1] + up[k] * dis * sh1[2];
        }
        const float rads1 = sradius * 0.666F + eradius * 0.333F;
        const float rads2 = sradius * 0.333F + eradius * 0.666F;
        applyShape(start, point1, sradius, rads1, count - 1, startPerc,
                   startPerc * 0.666F + endPerc * 0.333F);
        perc = sh2[0];
        float point2[3];
        for (int k = 0; k < 3; ++k) {
            point2[k] = start[k] * perc + end[k] * (1.0F - perc) +
                        rt[k] * dis * sh2[1] + up[k] * dis * sh2[2];
        }
        applyShape(point2, point1, rads1, rads2, count - 1,
                   startPerc * 0.333F + endPerc * 0.666F,
                   startPerc * 0.666F + endPerc * 0.333F);
        applyShape(point2, end, rads2, eradius, count - 1,
                   startPerc * 0.333F + endPerc * 0.666F, endPerc);
    }

    void doBoltSeg(const float start[3], const float end[3], float radius) {
        float fwd[3] = {end[0] - start[0], end[1] - start[1], end[2] - start[2]};
        float dis = std::sqrt(fwd[0] * fwd[0] + fwd[1] * fwd[1] +
                              fwd[2] * fwd[2]);
        if (dis > 0.0F) {
            for (float& c : fwd) {
                c /= dis;
            }
        }
        if (dis > 2000.0F) {   // "freaky long"
            dis = 2000.0F;
        }
        float rt[3];
        float up[3];
        achseAus(fwd, rt, up);
        float old[3] = {start[0], start[1], start[2]};
        float off[3] = {10.0F, 10.0F, 10.0F};
        float oldPerc = 0.0F;
        float newRadius = radius;
        float oldRadius = radius;
        // `int i` gegen `float dis`, wie in der Engine: ein Blitz unter 16
        // Einheiten zeichnet gar nichts.
        for (int i = 16; static_cast<float>(i) <= dis; i += 16) {
            if (*quads >= grenze) {
                return;
            }
            // "because of our large step size, we may not actually draw to
            // the end" - der letzte Schritt landet genau auf dem Ende.
            const float perc = (static_cast<float>(i + 16) > dis)
                                   ? 1.0F
                                   : static_cast<float>(i) / dis;
            float temp[3];
            const float r0 = bahn.crandom() * 3.0F;
            for (int k = 0; k < 3; ++k) {
                temp[k] = fwd[k] * r0;
            }
            const float r1 = bahn.crandom() * 7.0F * chaos;
            for (int k = 0; k < 3; ++k) {
                temp[k] += rt[k] * r1;
            }
            const float r2 = bahn.crandom() * 7.0F * chaos;
            for (int k = 0; k < 3; ++k) {
                temp[k] += up[k] * r2;
                off[k] += temp[k];
            }
            float cur[3];
            for (int k = 0; k < 3; ++k) {
                cur[k] = (start[k] + off[k]) * (1.0F - perc) + end[k] * perc;
            }
            if (verjuengt) {
                // "by using one minus the square, the radius stays fairly
                // constant, then drops off quickly at the very point"
                oldRadius = radius * (1.0F - oldPerc * oldPerc);
                newRadius = radius * (1.0F - perc * perc);
            }
            // r_lodbias steht auf 0, also Tiefe 2 - neun Stuecke je Schritt.
            applyShape(cur, old, newRadius, oldRadius, 2, 0.0F, 1.0F);
            // Die Reihenfolge der Bedingungen ist die der Engine: Q_random
            // wird NUR gezogen, wenn es gegabelt ist und noch Aeste frei
            // sind (&& bricht vorher ab). Sonst liefe der Samen anders.
            if (gegabelt && aeste > 0 && bahn.random() > 0.93F &&
                1.0F - perc > 0.8F) {
                --aeste;
                float ziel[3];
                for (int k = 0; k < 3; ++k) {
                    ziel[k] = (cur[k] + ende[k]) * 0.5F;
                }
                for (float& z : ziel) {
                    z += bahn.crandom() * 80.0F;
                }
                doBoltSeg(cur, ziel, newRadius);
            }
            for (int k = 0; k < 3; ++k) {
                old[k] = cur[k];
            }
            oldPerc = perc;
        }
    }
};

// RB_SurfaceElectricity (tr_surface.cpp:860). `wachsen` ist der Anteil
// fuer RF_GROW: 1 - (endTime - jetzt) / Lebensdauer = Alter / Lebensdauer.
void blitz(BspMesh& mesh, const float start[3], const float ziel[3],
           const float* auge, const float viewDir[3], float radius,
           const std::uint8_t rgba[4], std::uint32_t flags, float chaos,
           int frame, float wachsen, int& quads) {
    float fwd[3] = {ziel[0] - start[0], ziel[1] - start[1], ziel[2] - start[2]};
    const float dis = std::sqrt(fwd[0] * fwd[0] + fwd[1] * fwd[1] +
                                fwd[2] * fwd[2]);
    if (dis > 0.0F) {
        for (float& c : fwd) {
            c /= dis;
        }
    }
    float perc = 1.0F;
    if ((flags & efx::kFlagElectricityGrow) != 0U) {
        perc = std::clamp(wachsen, 0.0F, 1.0F);
    }
    Blitz b;
    for (int k = 0; k < 3; ++k) {
        b.ende[k] = start[k] + fwd[k] * perc * dis;
    }
    // Die Breitenrichtung EINMAL fuer den ganzen Blitz, wie
    // RB_SurfaceLine: cross(start - auge, ende - auge). Ohne Auge die
    // Blickrichtung. Das Vorzeichen richtet addBand2 je Stueck.
    float r[3];
    if (auge != nullptr) {
        const float v1[3] = {start[0] - auge[0], start[1] - auge[1],
                             start[2] - auge[2]};
        const float v2[3] = {b.ende[0] - auge[0], b.ende[1] - auge[1],
                             b.ende[2] - auge[2]};
        r[0] = v1[1] * v2[2] - v1[2] * v2[1];
        r[1] = v1[2] * v2[0] - v1[0] * v2[2];
        r[2] = v1[0] * v2[1] - v1[1] * v2[0];
    } else {
        const float d[3] = {b.ende[0] - start[0], b.ende[1] - start[1],
                            b.ende[2] - start[2]};
        r[0] = d[1] * viewDir[2] - d[2] * viewDir[1];
        r[1] = d[2] * viewDir[0] - d[0] * viewDir[2];
        r[2] = d[0] * viewDir[1] - d[1] * viewDir[0];
    }
    const float rl = std::sqrt(r[0] * r[0] + r[1] * r[1] + r[2] * r[2]);
    if (rl < 1e-6F) {
        return;   // genau in Blickrichtung - kein sichtbares Band
    }
    for (int k = 0; k < 3; ++k) {
        b.rechts[k] = r[k] / rl;
    }
    b.mesh = &mesh;
    b.viewDir = viewDir;
    b.rgba = rgba;
    b.quads = &quads;
    b.chaos = chaos;
    b.verjuengt = (flags & efx::kFlagElectricityTaper) != 0U;
    b.gegabelt = (flags & efx::kFlagElectricityBranch) != 0U;
    b.aeste = 3;   // "allow no more than three branches"
    b.bahn.s = static_cast<std::uint32_t>(frame);
    b.form.s = (static_cast<std::uint32_t>(frame) * 2654435761U) ^ 0x2545F491U;
    const float endeKopie[3] = {b.ende[0], b.ende[1], b.ende[2]};
    b.doBoltSeg(start, endeKopie, radius);
}

}  // namespace

void vergissEffektBahnen() {
    const std::lock_guard<std::mutex> sperre(g_flugMerkSperre);
    g_flugMerk.clear();
}

BspMesh buildEffectMesh(const std::vector<EffectInstance>& live, double nowMs,
                        const float right[3], const float up[3],
                        const EffectShaderSlots* slots, const BspGeometry* geo,
                        const EffectCull* cull) {
    BspMesh mesh;
    // --- Der Cluster der Kamera, einmal je Bild --------------------------
    //
    // Siehe EffectCull in bhed/efxdraw.h fuer die Messung, die das noetig
    // gemacht hat.
    int sichtCluster = -1;
    if (cull != nullptr && cull->haveViewer && cull->sichtTabelle &&
        geo != nullptr && !geo->vis.empty()) {
        const int leaf = geo->leafAt(cull->viewer);
        if (leaf >= 0 && static_cast<std::size_t>(leaf) < geo->leafs.size()) {
            sichtCluster = geo->leafs[static_cast<std::size_t>(leaf)].cluster;
        }
    }
    int quads = 0;
    {
        std::vector<Vorrechnung> vor;
        sammleBahnen(live, nowMs, geo, false, vor);
        vorrechnen(vor, geo);
    }

    // Je Shader ein eigener Zeichenaufruf.
    //
    // Vorher lag alles in EINEM Batch mit Shader 0 - also ohne Bild und mit
    // einer einzigen Mischart fuer Rauch, Feuer und Blitze gleichzeitig.
    // Ein Batch wird geschlossen, sobald das naechste Primitiv einen
    // anderen Shader nennt; die Primitive kommen ohnehin gruppiert.
    int aktuellerSlot = 0;
    std::uint32_t batchBeginn = 0;
    // Fuer `depthHack`: gilt fuer den gerade offenen Stapel. Wechselt es,
    // muss der Stapel geschlossen werden - sonst bekaemen Bausteine mit
    // und ohne das Flag dieselbe Tiefe.
    float aktuellerDepthScale = 1.0F;
    // --- setShaderTime: der Zeitursprung dieses Stapels ---------------
    //
    // 41 Vorkommen in 32 der 391 Effektdateien.
    //
    // Ohne ihn laeuft alles Zeitabhaengige des Shaders gegen die absolute
    // Zeit. Eine Explosion, die zwei Sekunden nach dem Start ausgeloest
    // wird, faengt ihre Bildfolge dann irgendwo in der Mitte an - je
    // nachdem, wann man das Skript abspielt.
    //
    // Die Engine setzt bei FX_SET_SHADER_TIME den Ursprung auf den Beginn
    // des Effekts; alles Weitere rechnet gegen tess.shaderTime. Genau
    // dieselbe Zahl reicht der Stapel jetzt an den Zeichner weiter - siehe
    // BspMesh::Batch::shaderTime.
    //
    // Am Stapel, nicht an der Textur: eine Textur wird zwischen allen
    // Effekten geteilt, ein Zeichenaufruf gehoert genau einem. Und deshalb
    // muss der Stapel schliessen, wenn der Ursprung wechselt - sonst
    // bekaemen zwei Effekte denselben.
    float aktuelleShaderTime = 0.0F;
    auto batchSchliessen = [&]() {
        const auto jetzt = static_cast<std::uint32_t>(mesh.indexes.size());
        if (jetzt > batchBeginn) {
            BspMesh::Batch b;
            b.lightmap = -1;   // Partikel haben kein gebackenes Licht
            // Die Farbe steckt in den Ecken und ist bereits fertig - siehe
            // BspMesh::Batch::vertexColour. Ohne das nimmt der Zeichner
            // einen festen Grauwert und die Farbe des Effekts ist weg.
            b.vertexColour = true;
            b.shader = aktuellerSlot;
            b.firstIndex = batchBeginn;
            b.numIndexes = jetzt - batchBeginn;
            b.depthScale = aktuellerDepthScale;
            b.shaderTime = aktuelleShaderTime;
            mesh.batches.push_back(b);
        }
        batchBeginn = jetzt;
    };

    // Blickrichtung und Auge fuer das Wegkeulen (CParticle::Cull,
    // CTail::Cull, FxPrimitives.cpp). vorn = hoch x rechts.
    const float vorn[3] = {up[1] * right[2] - up[2] * right[1],
                           up[2] * right[0] - up[0] * right[2],
                           up[0] * right[1] - up[1] * right[0]};
    // Blickrichtung fuer die Baender: right x up (= -vorn). Beide stehen
    // senkrecht aufeinander, das Kreuzprodukt ist also die Achse, um die
    // sich Baender drehen sollen.
    const float viewDir[3] = {right[1] * up[2] - right[2] * up[1],
                              right[2] * up[0] - right[0] * up[2],
                              right[0] * up[1] - right[1] * up[0]};
    const float* auge =
        (cull != nullptr && cull->haveViewer) ? cull->viewer : nullptr;
    for (const EffectInstance& inst : live) {
        if (inst.effect == nullptr) {
            continue;
        }
        // Die Obergrenze gilt JE EFFEKT, nicht fuer alle zusammen. Vorher
        // zaehlte `quads` ueber alle Runner, und die Durchgaenge liefen vom
        // aeltesten an - wer zuletzt kam, fiel weg: die neuesten Geysire.
        quads = 0;
        // --- Kann dieser Runner ueberhaupt gesehen werden? -------------
        //
        // Der Test ist der billigste, der etwas taugt: der Cluster des
        // Runners gegen den der Kamera. Er sagt nichts ueber das Blickfeld -
        // ein Effekt hinter der Kamera, aber im selben Raum, wird weiterhin
        // gebaut. Das ist Absicht: die Sichtbarkeitstabelle ist die Auskunft
        // der KARTE und stimmt immer, ein Blickfeldtest muesste die
        // Ausdehnung der Teilchenwolke kennen, und die kennt man erst,
        // nachdem man sie gebaut hat.
        if (sichtCluster >= 0) {
            float d2 = 0.0F;
            for (int k = 0; k < 3; ++k) {
                const float dd = inst.origin[k] - cull->viewer[k];
                d2 += dd * dd;
            }
            if (d2 > cull->alwaysRadius * cull->alwaysRadius) {
                const int leaf = geo->leafAt(inst.origin);
                const int c =
                    (leaf >= 0 &&
                     static_cast<std::size_t>(leaf) < geo->leafs.size())
                        ? geo->leafs[static_cast<std::size_t>(leaf)].cluster
                        : -1;
                if (c >= 0 && !geo->clusterVisible(sichtCluster, c)) {
                    continue;
                }
            }
        }

        const std::vector<double> starts =
            durchgaenge(inst, nowMs, laengsteMs(*inst.effect));
        for (const double began : starts) {
        efx::sim::Random planRnd(planSamen(inst, began));
        const std::vector<efx::sim::Spawn> spawns =
            efx::sim::schedule(*inst.effect, planRnd);

        for (std::size_t si = 0; si < spawns.size(); ++si) {
            const efx::sim::Spawn& sp = spawns[si];
            if (quads >= kMaxQuadsPerEffect) {
                break;
            }
            const auto at = static_cast<std::size_t>(sp.primitiveIndex);
            if (at >= inst.effect->primitives.size()) {
                continue;
            }
            const efx::Primitive& pr = inst.effect->primitives[at];
            if (!drawable(pr.type)) {
                continue;
            }
            efx::sim::Random rnd(teilchenSamen(inst, began, si));
            const Teilchen t = ziehe(inst, at, si, began, sp.timeMs, rnd);

            // Der Shader dieses Primitivs. Wechselt er, faengt ein neuer
            // Zeichenaufruf an - sonst traegen Rauch und Feuer dasselbe
            // Bild und dieselbe Mischart.
            int slot = 0;
            if (slots != nullptr && !pr.shaders.empty()) {
                // --- Nennt ein Baustein mehrere Texturen, wird gewuerfelt -
                //
                // Siehe waehle(): je Teilchen neu. behaved nahm bis rc367
                // immer die erste. Bei `volcano.efx` heisst das: der Dampf
                // zeigt nur `gfx/misc/steam`, obwohl die Datei `steam`,
                // `steam2` und `steam3` nennt. Gemessen ueber die 391
                // Effektdateien: von 1220 shaders-Bloecken nennen 323 mehr
                // als eine Textur.
                const std::size_t wahl = std::min(t.medium, pr.shaders.size() - 1U);
                auto found = slots->find(klein(pr.shaders[wahl]));
                if (found == slots->end() && wahl != 0U) {
                    // --- Kein Platz? Dann die erste, NICHT die null ------
                    //
                    // `slot` steht auf 0, und 0 ist nicht "ohne Bild" - es
                    // ist die ERSTE Textur der Karte. In md_am_sith die
                    // Lava. Genau das ist in rc367 passiert: der Zeichner
                    // wuerfelte unter allen Namen, eingetragen war aber nur
                    // der erste, und die uebrigen landeten auf der Lava.
                    // Im Bild: riesige Lavavierecke, gestapelt.
                    //
                    // Seit dieser Runde traegt der Lader alle Namen ein, der
                    // Fall sollte also nicht mehr auftreten. Der Rueckfall
                    // steht trotzdem hier: ein fehlender Eintrag darf eine
                    // Kartentextur ergeben, aber nur wenn er ausdruecklich
                    // dorthin zeigt - nicht durch Zufall.
                    found = slots->find(klein(pr.shaders[0]));
                }
                if (found != slots->end()) {
                    slot = found->second;
                }
            }
            // --- depthHack gehoert zum Stapel, nicht zum Baustein ------
            //
            // 5 Vorkommen in den 376 Effektdateien - selten, aber
            // auffaellig, wenn es fehlt: ein Muendungsblitz verschwindet
            // dann hinter der Wand, obwohl er davor liegen soll.
            //
            // Die Engine staucht dafuer den Tiefenbereich auf die vorderen
            // 30 Prozent (tr_backend.cpp:818). Bei uns steht der Faktor am
            // Stapel - und wechselt er, muss der Stapel geschlossen werden,
            // sonst bekaemen Bausteine mit und ohne das Flag dieselbe
            // Tiefe.
            const float willDepth =
                ((pr.flags & efx::kFlagDepthHack) != 0U) ? 0.3F : 1.0F;
            // Der Zeitursprung dieses Bausteins: bei `setShaderTime` der
            // Beginn DIESES Durchgangs, sonst null (absolute Zeit).
            const float willTime =
                ((pr.flags & efx::kFlagSetShaderTime) != 0U)
                    ? static_cast<float>(began * 0.001)
                    : 0.0F;
            if (slot != aktuellerSlot || willDepth != aktuellerDepthScale ||
                willTime != aktuelleShaderTime) {
                batchSchliessen();
                aktuellerSlot = slot;
                aktuellerDepthScale = willDepth;
                aktuelleShaderTime = willTime;
            }

            const float life = t.life;
            const double age = nowMs - t.geborenMs;
            if (age < 0.0 || age > static_cast<double>(life)) {
                continue;   // noch nicht da oder schon vorbei
            }
            const auto alter = static_cast<float>(age);

            // --- Wo es ist, mit Aufprall, wenn die .efx es verlangt ------
            //
            // Gemessen (rc319): `usePhysics` steht 166 mal in den 376
            // Effektdateien - der groesste einzelne Posten. Ohne ihn
            // fallen Funken durch den Boden und Truemmer durch die Mauer.
            const Lage lage = lageBei(t, inst, nowMs, alter, geo);
            if (lage.tot) {
                continue;   // killOnImpact: es ist vorbei
            }

            const float size = kurveBei(t.sizeC, alter, life);
            if (size <= 0.0F && pr.type != efx::PrimitiveType::Cylinder) {
                continue;
            }
            const float alpha =
                std::clamp(kurveBei(t.alphaC, alter, life), 0.0F, 1.0F);

            // Die Farbe: rgb liegt in 0..1, das Netz will 0..255.
            const Farbe farbe = zieheFarbe(pr, rnd);
            const V3 col = farbeBei(farbe, alter, life);
            // --- useAlpha: die Blende in den ALPHAKANAL ---------------
            //
            // FxPrimitives.cpp:599 ff.:
            //
            //     if ( mFlags & FX_USE_ALPHA ) {
            //         ClampVec( mRefEnt.angles, shaderRGBA );   // Farbe VOLL
            //         shaderRGBA[3] = (byte)(perc1 * 0xff);     // Blende
            //     } else {
            //         VectorScale( mRefEnt.angles, perc1, ... ); // Farbe
            //     }
            //
            // Der `else`-Zweig ist fuer die Mehrheit richtig: bei additivem
            // Mischen ist die Farbe zu skalieren dasselbe wie durchsichtiger
            // zu werden. Bei `useAlpha` (215 Vorkommen) hat das Bild einen
            // eigenen Alphakanal, und die Farbe soll voll bleiben.
            const bool nimmtAlpha = (pr.flags & efx::kFlagUseAlpha) != 0U;
            const std::array<std::uint8_t, 4> rgba =
                nimmtAlpha
                    ? std::array<std::uint8_t, 4>{
                          static_cast<std::uint8_t>(std::clamp(col.x, 0.0F, 1.0F) * 255.0F),
                          static_cast<std::uint8_t>(std::clamp(col.y, 0.0F, 1.0F) * 255.0F),
                          static_cast<std::uint8_t>(std::clamp(col.z, 0.0F, 1.0F) * 255.0F),
                          static_cast<std::uint8_t>(
                              std::clamp(alpha, 0.0F, 1.0F) * 255.0F)}
                    // Erst mit alpha skalieren, DANN klemmen (ClampVec
                    // nach VectorScale, FxPrimitives.cpp:599 ff.).
                    : std::array<std::uint8_t, 4>{
                          static_cast<std::uint8_t>(std::clamp(col.x * alpha, 0.0F, 1.0F) * 255.0F),
                          static_cast<std::uint8_t>(std::clamp(col.y * alpha, 0.0F, 1.0F) * 255.0F),
                          static_cast<std::uint8_t>(std::clamp(col.z * alpha, 0.0F, 1.0F) * 255.0F),
                          255};

            const float centre[3] = {lage.welt.x, lage.welt.y, lage.welt.z};

            // --- Wegkeulen wie die Engine ---------------------------------
            //
            // CParticle::Cull und CTail::Cull (FxPrimitives.cpp:117, 988):
            // hinter dem Auge ist weg, ein Teilchen naeher als 16 Einheiten
            // ebenso. Ein Geysir, dessen Fuss hinter der Kamera steht,
            // verschwindet im Spiel ganz - bei uns stand er weiter im Bild.
            if (auge != nullptr &&
                (quadLike(pr.type) || pr.type == efx::PrimitiveType::Tail)) {
                const float dx = centre[0] - auge[0];
                const float dy = centre[1] - auge[1];
                const float dz = centre[2] - auge[2];
                if (dx * vorn[0] + dy * vorn[1] + dz * vorn[2] < 0.0F) {
                    continue;
                }
                if (pr.type != efx::PrimitiveType::Tail &&
                    pr.type != efx::PrimitiveType::Decal &&
                    dx * dx + dy * dy + dz * dz < 16.0F * 16.0F) {
                    continue;
                }
            }

            if (quadLike(pr.type)) {
                // size ist der HALBE Rand, nicht der ganze.
                //
                // tr_surface.cpp, RB_SurfaceSprite():
                //     radius = e.radius;
                //     VectorScale( ori.axis[1], radius, left );
                //     VectorScale( ori.axis[2], radius, up );
                // und RB_AddQuadStampExt():
                //     xyz[ndx] = origin + left + up;
                //
                // Die Ecken liegen also bei origin +/- size, das Viereck ist
                // 2*size breit.
                //
                // --- Ein Decal liegt FLACH AUF der Flaeche ------------------
                //
                // Die Engine legt ein Quadrat der Kantenlaenge 2*size um den
                // Auftreffpunkt, senkrecht zur Flaechennormalen ax[0], und
                // projiziert es auf die Geometrie (CG_ImpactMark,
                // cg_marks.cpp:160 ff.):
                //
                //     axis[0] = normale
                //     PerpendicularVector( axis[1], axis[0] );
                //     CrossProduct( axis[0], axis[2], axis[1] );
                //     ecke = origin +/- radius*axis[1] +/- radius*axis[2]
                //
                // **NAEHERUNG:** die Engine SCHNEIDET das Quadrat danach gegen
                // die getroffenen Flaechen (CM_MarkFragments). Das tut behaved
                // nicht - hier bleibt es ein flaches Quadrat.
                //
                // Der Versatz von zwei Einheiten entlang der Normalen ist
                // derselbe wie beim Aufpralleffekt aus rc287: ohne ihn streiten
                // Decal und Wand um dieselbe Tiefe und es flimmert.
                // --- Die Drehung (rotation, rotationDelta) ---------------
                //
                // FX_AddParticle bekommt mRotation.GetVal() und
                // mRotationDelta.GetVal(); CParticle::UpdateRotation
                // (FxPrimitives.h:352) rechnet je Bild
                //
                //     mRefEnt.rotation += mFrameTime * 0.01f * mRotationDelta;
                //
                // ueber die Zeit also rotation + delta * 0.01 * alterInMs.
                // Bis rc569 drehte sich nichts - Rauchwolken mit
                // `rotationDelta -30 30` standen still. Ein Decal bekommt
                // nur den Startwinkel (CG_ImpactMark, "orientation").
                float drehung = rnd.pick(pr.rotation);
                if (pr.type != efx::PrimitiveType::Decal) {
                    drehung += rnd.pick(pr.rotationDelta) * 0.01F * alter;
                }
                const float dw = drehung * std::numbers::pi_v<float> / 180.0F;
                const float dc = std::cos(dw);
                const float ds = std::sin(dw);
                // --- Ein Decal liegt FLACH AUF der Flaeche ------------------
                //
                // CG_ImpactMark (cg_marks.cpp:168 ff.):
                //
                //     VectorNormalize2( dir, axis[0] );
                //     PerpendicularVector( axis[1], axis[0] );
                //     RotatePointAroundVector( axis[2], axis[0], axis[1], orientation );
                //     CrossProduct( axis[0], axis[2], axis[1] );
                //     ecke = origin -/+ radius*axis[1] -/+ radius*axis[2]
                //
                // **NAEHERUNG:** die Engine SCHNEIDET das Quadrat danach gegen
                // die getroffenen Flaechen (CM_MarkFragments). Das tut behaved
                // nicht - hier bleibt es ein flaches Quadrat.
                //
                // Der Versatz von zwei Einheiten entlang der Normalen ist
                // derselbe wie beim Aufpralleffekt aus rc287: ohne ihn streiten
                // Decal und Wand um dieselbe Tiefe und es flimmert.
                //
                // Uebergeben wird (axis[1], -axis[2]): dieselben vier Ecken,
                // und right x up zeigt wie bisher entlang der Normalen - mit
                // dem Umlauf, den der Zeichner sehen will (siehe addQuad).
                if (pr.type == efx::PrimitiveType::Decal) {
                    float n[3] = {t.ax[0][0], t.ax[0][1], t.ax[0][2]};
                    const float nl =
                        std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
                    if (nl > 1e-6F) {
                        for (int q = 0; q < 3; ++q) {
                            n[q] /= nl;
                        }
                    } else {
                        n[0] = 0.0F;
                        n[1] = 0.0F;
                        n[2] = 1.0F;
                    }
                    float a1[3];
                    senkrechtZu(a1, n);
                    float a2[3];
                    drehePunkt(a2, n, a1, drehung);
                    // axis[1] = axis[0] x axis[2]
                    a1[0] = n[1] * a2[2] - n[2] * a2[1];
                    a1[1] = n[2] * a2[0] - n[0] * a2[2];
                    a1[2] = n[0] * a2[1] - n[1] * a2[0];
                    const float minusA2[3] = {-a2[0], -a2[1], -a2[2]};
                    const float versetzt[3] = {centre[0] + n[0] * 2.0F,
                                               centre[1] + n[1] * 2.0F,
                                               centre[2] + n[2] * 2.0F};
                    addQuad(mesh, versetzt, a1, minusA2, size, rgba.data());
                    ++quads;
                    continue;
                }
                // --- Ausgerichtet: flach, senkrecht zu ax[0] --------------
                //
                // COrientedParticle::Draw reicht mNormal als axis[0] weiter
                // (FxPrimitives.cpp:655), und RB_SurfaceOrientedQuad
                // (tr_surface.cpp:188) spannt das Viereck mit
                // MakeNormalVectors( axis[0], left, up ) auf, gedreht um
                // rotation. Bis rc569 stand es wie ein gewoehnliches
                // Partikel zur Kamera - eine Druckwelle, die flach auf dem
                // Boden liegen soll, stand aufrecht im Bild.
                if (pr.type == efx::PrimitiveType::OrientedParticle) {
                    float links[3];
                    float hoch[3];
                    achseAus(t.ax[0], links, hoch);
                    float l2[3];
                    float u2[3];
                    for (int k = 0; k < 3; ++k) {
                        l2[k] = links[k] * dc - hoch[k] * ds;
                        u2[k] = hoch[k] * dc + links[k] * ds;
                    }
                    // right = -left, wie beim Sprite (siehe unten).
                    const float r2[3] = {-l2[0], -l2[1], -l2[2]};
                    addQuad(mesh, centre, r2, u2, size, rgba.data());
                    ++quads;
                    continue;
                }
                // --- Das Sprite, gedreht (RB_SurfaceSprite, :156) ---------
                //
                //     left = viewaxis[1] * c - viewaxis[2] * s
                //     up   = viewaxis[2] * c + viewaxis[1] * s
                //
                // viewaxis[1] ist LINKS, also -right. Bei Drehung null ist es
                // genau das Viereck von vorher.
                float r2[3];
                float u2[3];
                for (int k = 0; k < 3; ++k) {
                    r2[k] = right[k] * dc + up[k] * ds;
                    u2[k] = up[k] * dc - right[k] * ds;
                }
                addQuad(mesh, centre, r2, u2, size, rgba.data());
                ++quads;
                continue;
            }

            if (pr.type == efx::PrimitiveType::Electricity) {
                // Die Zugfolge nach der Farbe: org2, dann der Samen des
                // Blitzes (CElectricity::Initialize wuerfelt `frame` erst
                // nach dem Anlegen).
                const Ende2 e2 = zieheEnde2(t, inst, nowMs, centre, rnd, geo);
                const int frame = static_cast<int>(rnd.next() * 1265536.0F);
                blitz(mesh, centre, e2.welt, auge, viewDir, size, rgba.data(),
                      t.flags, t.bounce, frame, alter / life, quads);
                continue;
            }

            if (lineLike(pr.type)) {
                // --- Ein Tail ist KEINE Linie -----------------------------
                //
                // Beide sind Baender, aber ihr zweites Ende kommt von ganz
                // verschiedenen Stellen.
                //
                // Eine Line hat zwei Punkte: FxScheduler.cpp reicht org und
                // org2 getrennt an FX_AddLine weiter (siehe zieheEnde2).
                //
                // Ein Tail hat nur EINEN Punkt und eine LAENGE. Sein
                // zweites Ende wird jedes Bild aus der Flugrichtung
                // gerechnet (CTail::CalcNewEndpoint, FxPrimitives.cpp):
                //
                //     VectorSubtract( mOldOrigin, mOrigin1, temp );
                //     VectorNormalize( temp );
                //     VectorMA( mOrigin1, mLength, temp, oldorigin );
                //
                // `temp` zeigt also NACH HINTEN, entgegen der Bewegung -
                // und deshalb sind die Laengen in den Dateien negativ.
                // `volcano.efx` hat `length start -4 -2 end -280 -200`: der
                // Schweif waechst auf 280 Einheiten.
                float ende[3] = {centre[0], centre[1], centre[2]};
                if (pr.type == efx::PrimitiveType::Tail) {
                    const efx::curve::Curve lC = curveOf(pr.length, rnd);
                    const float laenge = kurveBei(lC, alter, life);
                    float dir[3] = {lage.richtung.x, lage.richtung.y,
                                    lage.richtung.z};
                    const float dl = std::sqrt(dir[0] * dir[0] +
                                               dir[1] * dir[1] +
                                               dir[2] * dir[2]);
                    if (dl > 0.0001F) {
                        // --- Das Vorzeichen, zweimal nachgerechnet -------
                        //
                        //     temp = mOldOrigin - mOrigin1
                        //     oldorigin = mOrigin1 + mLength * temp^
                        //
                        // `mOldOrigin` ist die Stelle von VORHER, also
                        // zeigt `temp` ENTGEGEN der Bewegung: temp^ = -v^.
                        // Damit ist oldorigin = origin - mLength * v^, und
                        // bei mLength = -280 liegt das zweite Ende 280
                        // Einheiten VORAUS, nicht dahinter. Zwei
                        // Minuszeichen, die sich aufheben - man muss die
                        // Zeile wirklich lesen.
                        for (int k = 0; k < 3; ++k) {
                            ende[k] = centre[k] + dir[k] / dl * (-laenge);
                        }
                    }
                } else {
                    const Ende2 e2 = zieheEnde2(t, inst, nowMs, centre, rnd, geo);
                    for (int k = 0; k < 3; ++k) {
                        ende[k] = e2.welt[k];
                    }
                }
                // Auch hier ist size die HALBE Breite: DoLine() in
                // tr_surface.cpp setzt die eine Kante auf +spanWidth, die
                // andere auf -spanWidth.
                addBand(mesh, centre, ende, viewDir, size, rgba.data(), auge);
                ++quads;
                continue;
            }

            if (pr.type == efx::PrimitiveType::Cylinder) {
                // --- Achse und Halbmesser wie RB_SurfaceCylinder ----------
                //
                // FX_AddCylinder( clientID, org, ax[0], size, size2,
                // length ... ): die Achse ist die RICHTUNG DES EFFEKTS
                // (ax[0]), nicht der angles-Block. Bis rc568 stand hier
                // der angles-Block - den liest CreateEffect beim Zylinder
                // gar nicht.
                //
                // Und welcher Halbmesser wo: CCylinder::Draw setzt
                // origin = org und oldorigin = org + length * axis;
                // RB_SurfaceCylinder (tr_surface.cpp:626 ff.) legt den
                // Ring mit `backlerp` (size2) um origin und den mit
                // `radius` (size) um oldorigin. size2 unten, size oben -
                // hier stand es vertauscht.
                const efx::curve::Curve s2C = curveOf(pr.size2, rnd);
                const efx::curve::Curve lenC = curveOf(pr.length, rnd);
                const float r2 = kurveBei(s2C, alter, life);
                const float hgt = kurveBei(lenC, alter, life);
                if (hgt <= 0.0F || std::max(size, r2) <= 0.0F) {
                    continue;
                }
                const Anker a = ankerBei(inst, nowMs);
                float axis[3] = {t.ax[0][0], t.ax[0][1], t.ax[0][2]};
                if (t.relativ) {
                    for (int k = 0; k < 3; ++k) {
                        axis[k] = a.fwd[k];
                    }
                }
                const float al = std::sqrt(axis[0] * axis[0] +
                                           axis[1] * axis[1] +
                                           axis[2] * axis[2]);
                if (al < 1e-6F) {
                    continue;
                }
                for (float& c : axis) {
                    c /= al;
                }
                addCylinder(mesh, centre, axis, std::max(r2, 0.0F),
                            std::max(size, 0.0F), hgt, rgba.data());
                quads += 12;
                continue;
            }
            ++quads;
        }
        }   // Ende: alle lebenden Durchgaenge
    }

    batchSchliessen();
    return mesh;
}

std::vector<EmitterModelDraw> emitterModelsAt(
    const std::vector<EffectInstance>& live, double nowMs,
    const BspGeometry* geo) {
    // --- JEDER Emitter, nicht einer je Primitive -------------------------
    //
    // Bis rc568 gab es hier genau ein Modell je Emitter-Primitive: immer
    // das erste der Liste, immer mit dem Samen des ganzen Effekts, ohne
    // `delay`, ohne `count`, und mit der Startgroesse statt der Kurve.
    // rocket/explosion.efx ("Chunkies": count 15 25, vier Modelle) warf
    // damit EINEN Brocken, und SphereOfDoom in thermal/explosion.efx
    // (`size start 0 end 5 flags linear`) blieb in Groesse eins stehen.
    //
    // Jetzt geht jedes Teilchen des Zeitplans durch ziehe() - dasselbe
    // Teilchen, das auch seine Rauchfahne und seinen Aufprall ausloest.
    std::vector<EmitterModelDraw> aus;
    {
        std::vector<Vorrechnung> vor;
        sammleBahnen(live, nowMs, geo, true, vor);
        vorrechnen(vor, geo);
    }
    for (const EffectInstance& inst : live) {
        if (inst.effect == nullptr) {
            continue;
        }
        bool hatModell = false;
        for (const efx::Primitive& pr : inst.effect->primitives) {
            hatModell = hatModell ||
                        (pr.type == efx::PrimitiveType::Emitter &&
                         (pr.flags & efx::kFlagAttachedModel) != 0U &&
                         !pr.models.empty());
        }
        if (!hatModell) {
            continue;
        }
        for (const double began : durchgaenge(inst, nowMs,
                                              laengsteMs(*inst.effect))) {
            efx::sim::Random planRnd(planSamen(inst, began));
            const std::vector<efx::sim::Spawn> spawns =
                efx::sim::schedule(*inst.effect, planRnd);
            for (std::size_t si = 0; si < spawns.size(); ++si) {
                const auto at = static_cast<std::size_t>(spawns[si].primitiveIndex);
                if (at >= inst.effect->primitives.size()) {
                    continue;
                }
                const efx::Primitive& pr = inst.effect->primitives[at];
                // "Emitters don't draw themselves, but they may need to add
                // an attached model" (CEmitter::Draw, FxPrimitives.cpp:1384)
                if (pr.type != efx::PrimitiveType::Emitter ||
                    (pr.flags & efx::kFlagAttachedModel) == 0U ||
                    pr.models.empty()) {
                    continue;
                }
                efx::sim::Random rnd(teilchenSamen(inst, began, si));
                const Teilchen t =
                    ziehe(inst, at, si, began, spawns[si].timeMs, rnd);
                const double age = nowMs - t.geborenMs;
                if (age < 0.0 || age > static_cast<double>(t.life)) {
                    continue;   // noch nicht da oder schon vorbei
                }
                const auto alter = static_cast<float>(age);
                const Lage lage = lageBei(t, inst, nowMs, alter, geo);
                if (lage.tot) {
                    continue;
                }
                // Die Groesse ist der MASSSTAB: CEmitter::Draw skaliert die
                // Achsen mit mRefEnt.radius (:1394), und das setzt UpdateSize aus
                // der size-Kurve. Bei null ist das Modell unsichtbar.
                const float groesse = kurveBei(t.sizeC, alter, t.life);
                if (groesse <= 0.0F) {
                    continue;
                }
                const Dreh dreh = zieheDreh(t, rnd);
                EmitterModelDraw d;
                d.modelPath = klein(pr.models[std::min(t.medium, pr.models.size() - 1U)]);
                d.origin[0] = lage.welt.x;
                d.origin[1] = lage.welt.y;
                d.origin[2] = lage.welt.z;
                drehungBei(dreh, t, lage, alter, d.angles);
                d.scale = groesse;
                aus.push_back(std::move(d));
            }
        }
    }
    return aus;
}

namespace {

bool hatFolgen(const efx::Effect& ef) {
    for (const efx::Primitive& pr : ef.primitives) {
        if (!pr.impactFx.empty() || !pr.deathFx.empty() || !pr.emitFx.empty() ||
            (pr.type == efx::PrimitiveType::FxRunner && !pr.playFx.empty())) {
            return true;
        }
    }
    return false;
}

unsigned folgeSamen(unsigned basis, EffectImpact::Herkunft h, int nummer) {
    unsigned x = basis ^ ((static_cast<unsigned>(h) + 1U) * 0x85EBCA6BU) ^
                 ((static_cast<unsigned>(nummer) + 1U) * 0xC2B2AE35U);
    x ^= x >> 16U;
    x *= 0x7FEB352DU;
    x ^= x >> 15U;
    return x | 1U;
}

// Alle Folgeeffekte EINES Effekts (eine Stufe), Teilchen fuer Teilchen.
//
// `rueckblickMs`: wie weit zurueck ein Durchgang noch zaehlt - ein
// Todeseffekt beginnt erst am Lebensende und laeuft danach weiter.
//
// `nachlaufMs[i]`: wie lange die Folgeeffekte von Primitive i hoechstens
// etwas zeigen. Alles, was ein Teilchen ausloest (Aufprall, Fahne, Tod),
// liegt vor oder auf seinem Lebensende. Ist das Ende laenger her als der
// Nachlauf, kann keiner seiner Folgeeffekte mehr sichtbar sein - dann wird
// die Bahn gar nicht erst gerechnet. Das ist keine Kosmetik: ein
// wiederholender Funkenrunner mit usePhysics haette sonst je Bild fuer
// jedes laengst verloschene Teilchen seine ganze Bahn samt Spurtests neu
// gerechnet.
void folgeEffekte(const EffectInstance& inst, double nowMs,
                  const BspGeometry* geo, double rueckblickMs,
                  const std::vector<double>& nachlaufMs,
                  std::vector<EffectImpact>& aus) {
    const efx::Effect& ef = *inst.effect;
    for (const double began : durchgaenge(inst, nowMs, rueckblickMs)) {
        efx::sim::Random planRnd(planSamen(inst, began));
        const std::vector<efx::sim::Spawn> spawns =
            efx::sim::schedule(ef, planRnd);
        for (std::size_t si = 0; si < spawns.size(); ++si) {
            const auto at = static_cast<std::size_t>(spawns[si].primitiveIndex);
            if (at >= ef.primitives.size()) {
                continue;
            }
            const efx::Primitive& pr = ef.primitives[at];
            const std::uint32_t flags = wirksameFlags(pr);
            const bool beweglich = bewegt(pr.type);
            // --- FxRunner: spielt einfach einen anderen Effekt ---------
            //
            // FxScheduler.cpp:1953, die ganze Behandlung:
            //
            //     case FxRunner:
            //         PlayEffect( fx->mPlayFxHandles.GetHandle(), org, ax );
            //
            // Kein Bild, keine Bahn - aber wie jedes Primitiv `count`-mal
            // und mit `delay` (PlayEffect, FxScheduler.cpp:1195 ff.). Hier
            // stand bis rc568, das spiele keine Rolle; die Engine plant
            // einen FxRunner genauso wie einen Partikel.
            const bool willRunner = pr.type == efx::PrimitiveType::FxRunner &&
                                    !pr.playFx.empty();
            // --- Aufprall: bei JEDEM Treffer (FxPrimitives.cpp:318) ------
            const bool willImpact =
                beweglich && geo != nullptr && !pr.impactFx.empty() &&
                (flags & efx::kFlagImpactRunsFx) != 0U &&
                (flags & efx::kFlagApplyPhysics) != 0U;
            // --- Tod am Lebensende (CParticle::Die, FxPrimitives.cpp:102) -
            //
            //     if ( mFlags & FX_DEATH_RUNS_FX && !(mFlags & FX_KILL_ON_IMPACT) )
            //
            // Das sieht aus wie "killOnImpact bekommt nie einen
            // Todeseffekt" - so stand es hier bis rc568. Aber FX_Add
            // (FxUtil.cpp:241) LOESCHT das Flag, wenn die Lebenszeit
            // regulaer ablaeuft (ClearFlags( FX_KILL_ON_IMPACT )). Kein
            // Todeseffekt gibt es also nur fuer den, der WIRKLICH an der
            // Wand gestorben ist.
            const bool willTod = beweglich && !pr.deathFx.empty() &&
                                 (flags & efx::kFlagDeathRunsFx) != 0U;
            // --- Was ein Emitter unterwegs ausstoesst -------------------
            //
            // CEmitter::Draw (FxPrimitives.cpp:1404 ff.): alle TRAIL_RATE =
            // 8 ms wird geprueft, ob die Bahn seit dem letzten Ausstoss
            // mindestens `step` zurueckgelegt hat:
            //
            //     step = mDensity + Q_flrand(-1,1) * mVariance;  step *= step;
            //     if ( DistanceSquared( org, mOldOrigin ) >= step )
            //         theFxScheduler.PlayEffect( mEmitterFxID, org, mRefEnt.axis );
            const bool willEmit = pr.type == efx::PrimitiveType::Emitter &&
                                  !pr.emitFx.empty() &&
                                  (flags & efx::kFlagEmitFx) != 0U;
            // --- Der Aufprall am Ende einer gespurten Line ------------
            //
            // org2fromTrace + traceImpactFx (FxScheduler.cpp:1712): der
            // Aufpralleffekt dort, wo der Strahl endet - beim Blitz der
            // Machtblitze der Einschlag an der Wand.
            const bool willSpur =
                (pr.type == efx::PrimitiveType::Line ||
                 pr.type == efx::PrimitiveType::Electricity) &&
                (pr.spawnFlags & efx::kSpawnOrg2FromTrace) != 0U &&
                (pr.spawnFlags & efx::kSpawnTraceImpactFx) != 0U &&
                !pr.impactFx.empty();
            if (!willRunner && !willImpact && !willTod && !willEmit &&
                !willSpur) {
                continue;
            }
            efx::sim::Random rnd(teilchenSamen(inst, began, si));
            const Teilchen t = ziehe(inst, at, si, began, spawns[si].timeMs, rnd);
            if (t.geborenMs > nowMs) {
                continue;   // noch nicht erzeugt
            }
            const double nachlauf =
                at < nachlaufMs.size() ? nachlaufMs[at] : 0.0;
            if (nowMs - (t.geborenMs + static_cast<double>(t.life)) > nachlauf) {
                continue;   // alles, was es ausgeloest hat, ist vorbei
            }
            const unsigned basis = teilchenSamen(inst, began, si) ^ 0x68E31DA4U;
            auto melde = [&](const std::string& name, const float org[3],
                             const float n[3], double startMs,
                             EffectImpact::Herkunft h, int nummer) {
                EffectImpact d;
                d.effectName = name;
                d.herkunft = h;
                for (int k = 0; k < 3; ++k) {
                    d.origin[k] = org[k];
                    d.normal[k] = n[k];
                }
                d.startMs = startMs;
                d.seed = folgeSamen(basis, h, nummer);
                aus.push_back(std::move(d));
            };

            if (willRunner) {
                // An SEINEM Ort (origin der Primitive eingerechnet) und in
                // der Richtung des Effekts (ax[0]).
                const Lage l = lageBei(t, inst, t.geborenMs, 0.0F, geo);
                const float org[3] = {l.welt.x, l.welt.y, l.welt.z};
                const std::size_t w = waehle(rnd, pr.playFx.size());
                melde(pr.playFx[w], org, t.ax[0], t.geborenMs,
                      EffectImpact::Herkunft::Ausstoss, 0);
                continue;
            }
            if (willSpur) {
                // Dieselbe Zugfolge wie im Netz: Farbe, dann org2.
                (void)zieheFarbe(pr, rnd);
                const Lage l = lageBei(t, inst, t.geborenMs, 0.0F, geo);
                const float start[3] = {l.welt.x, l.welt.y, l.welt.z};
                const Ende2 e2 = zieheEnde2(t, inst, t.geborenMs, start, rnd, geo);
                // Ohne Treffer laege der Effekt 32768 Einheiten entfernt -
                // unsichtbar und nur teuer.
                if (e2.traf) {
                    const std::size_t w = waehle(rnd, pr.impactFx.size());
                    melde(pr.impactFx[w], e2.welt, e2.normale, t.geborenMs,
                          EffectImpact::Herkunft::Aufprall, 0);
                }
                continue;
            }

            // --- Bewegte Teilchen: Aufprall, Tod, Fahne ------------------
            Dreh dreh;
            float dichte = 0.0F;
            float streu = 0.0F;
            if (pr.type == efx::PrimitiveType::Emitter) {
                // Dieselbe Zugfolge wie emitterModelsAt: erst die Drehung.
                dreh = zieheDreh(t, rnd);
                dichte = rnd.pick(pr.density);
                streu = rnd.pick(pr.variance);
            }
            // Je Teilchen EIN Folgeeffekt je Art - CreateEffect waehlt die
            // Nummern beim Erzeugen (mImpactFxHandles.GetHandle() usw.).
            const std::size_t wAufprall = waehle(rnd, pr.impactFx.size());
            const std::size_t wTod = waehle(rnd, pr.deathFx.size());
            const std::size_t wAus = waehle(rnd, pr.emitFx.size());

            const double age = nowMs - t.geborenMs;
            const float bis =
                static_cast<float>(std::min(age, static_cast<double>(t.life)));
            const Lage l = lageBei(t, inst, t.geborenMs + static_cast<double>(bis),
                                   bis, geo);

            if (willImpact && !t.relativ) {
                for (int k = 0; k < l.bahn.aufpralle; ++k) {
                    const auto kk = static_cast<std::size_t>(k);
                    const float p[3] = {l.bahn.aufprallPunkt[kk].x,
                                        l.bahn.aufprallPunkt[kk].y,
                                        l.bahn.aufprallPunkt[kk].z};
                    const float n[3] = {l.bahn.aufprallNormale[kk].x,
                                        l.bahn.aufprallNormale[kk].y,
                                        l.bahn.aufprallNormale[kk].z};
                    melde(pr.impactFx[wAufprall], p, n,
                          t.geborenMs +
                              static_cast<double>(l.bahn.aufprallSekunde[kk]) *
                                  1000.0,
                          EffectImpact::Herkunft::Aufprall, k);
                }
            }

            if (willTod && age >= static_cast<double>(t.life) && !l.tot) {
                // Eine ZUFAELLIGE Richtung, wie in der Engine - nicht etwa
                // nach oben. Aus dem Strom des Teilchens, damit sie bei
                // jedem Bild dieselbe ist.
                //
                // Hier stand `rnd.pick(efx::Range{-1.0F, 1.0F})` - ein
                // ungesetztes Range, also immer null, und die Richtung fiel
                // jedes Mal auf den Rueckfall "nach oben".
                float n[3] = {rnd.range(-1.0F, 1.0F), rnd.range(-1.0F, 1.0F),
                              rnd.range(-1.0F, 1.0F)};
                const float len =
                    std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
                if (len > 1e-6F) {
                    for (float& c : n) {
                        c /= len;
                    }
                } else {
                    n[0] = 0.0F;
                    n[1] = 0.0F;
                    n[2] = 1.0F;
                }
                const float p[3] = {l.welt.x, l.welt.y, l.welt.z};
                melde(pr.deathFx[wTod], p, n,
                      t.geborenMs + static_cast<double>(t.life),
                      EffectImpact::Herkunft::Tod, 0);
            }

            if (willEmit) {
                // Die Fahne entlang der Bahn, in EINEM Durchlauf (siehe
                // flugbahnAbtasten). Eine Obergrenze braucht es, sonst frisst
                // eine lange Fahne das Bild; die Engine hat sie nicht, aber
                // sie rechnet je Bild WEITER, waehrend wir jedes Mal von vorn
                // anfangen - der Preis dafuer, dass dasselbe Alter immer
                // dasselbe Bild gibt.
                //
                // Von vorn heisst seit dem Spurbuch nur noch: v und p
                // fortschreiben. Die Spurtests kommen aus demselben
                // Merkzettel wie die Bahn des Emitters selbst (gleicher
                // Schluessel - lageBei oben hat ihn gerade bis `bis`
                // gefuellt), die Fahne kostet also keinen einzigen mehr.
                constexpr int kMaxAusstoss = 400;
                constexpr float kTrailRateS = 0.008F;   // TRAIL_RATE
                V3 start = t.org;
                if (!t.relativ) {
                    start = start + V3{t.geburt.org[0], t.geburt.org[1],
                                       t.geburt.org[2]};
                }
                bool erster = true;
                V3 letzter = start;
                float schritt = 0.0F;
                int wieviele = 0;
                const efx::sim::TraceFn spur = spurFuer(geo, pr, flags);
                FlugSchluessel schluessel;
                std::unique_ptr<efx::sim::FlugZustand> zustand;
                if (spur) {
                    schluessel = flugSchluessel(start, t, geo);
                    zustand = holeFlug(schluessel);
                }
                (void)efx::sim::flugbahnAbtasten(
                    start, t.vel, t.acc, t.grav, bis * 0.001F, kTrailRateS,
                    t.bounce, (flags & efx::kFlagKillOnImpact) != 0U, spur,
                    [&](float sek, const V3& p) {
                        if (erster) {
                            erster = false;
                            letzter = p;
                            schritt = dichte + rnd.range(-1.0F, 1.0F) * streu;
                            return true;
                        }
                        const V3 dd = p - letzter;
                        if (efx::camera::dot(dd, dd) < schritt * schritt) {
                            return true;
                        }
                        // Die Achse des Emitters zu diesem Zeitpunkt
                        // (mRefEnt.axis aus UpdateAngles) - sie geht an den
                        // Folgeeffekt, hier stand fest "nach oben".
                        float ang[3];
                        drehungBei(dreh, t, l, sek * 1000.0F, ang);
                        float fwd[3];
                        vorneAus(ang, fwd);
                        const float o[3] = {p.x, p.y, p.z};
                        melde(pr.emitFx[wAus], o, fwd,
                              t.geborenMs + static_cast<double>(sek) * 1000.0,
                              EffectImpact::Herkunft::Ausstoss, wieviele);
                        letzter = p;
                        schritt = dichte + rnd.range(-1.0F, 1.0F) * streu;
                        ++wieviele;
                        return wieviele < kMaxAusstoss;
                    },
                    zustand.get());
                if (zustand) {
                    gibFlug(schluessel, std::move(zustand));
                }
            }
        }
    }
}

// Die Bahnen, fuer die folgeEffekte gleich lageBei rechnet (Aufprall, Tod,
// Fahne) - mit denselben Bedingungen und derselben Zugfolge, fuer
// vorrechnen(). Runner und gespurte Lines brauchen keine Bahn.
void sammleFolgeBahnen(const EffectInstance& inst, double nowMs,
                       const BspGeometry* geo, double rueckblickMs,
                       const std::vector<double>& nachlaufMs,
                       std::vector<Vorrechnung>& aus) {
    const efx::Effect& ef = *inst.effect;
    for (const double began : durchgaenge(inst, nowMs, rueckblickMs)) {
        efx::sim::Random planRnd(planSamen(inst, began));
        const std::vector<efx::sim::Spawn> spawns = efx::sim::schedule(ef, planRnd);
        for (std::size_t si = 0; si < spawns.size(); ++si) {
            const auto at = static_cast<std::size_t>(spawns[si].primitiveIndex);
            if (at >= ef.primitives.size()) {
                continue;
            }
            const efx::Primitive& pr = ef.primitives[at];
            const std::uint32_t flags = wirksameFlags(pr);
            if (!bewegt(pr.type) || (flags & efx::kFlagApplyPhysics) == 0U) {
                continue;
            }
            const bool willImpact = geo != nullptr && !pr.impactFx.empty() &&
                                    (flags & efx::kFlagImpactRunsFx) != 0U;
            const bool willTod = !pr.deathFx.empty() && (flags & efx::kFlagDeathRunsFx) != 0U;
            const bool willEmit = pr.type == efx::PrimitiveType::Emitter &&
                                  !pr.emitFx.empty() && (flags & efx::kFlagEmitFx) != 0U;
            if (!willImpact && !willTod && !willEmit) {
                continue;
            }
            efx::sim::Random rnd(teilchenSamen(inst, began, si));
            const Teilchen t = ziehe(inst, at, si, began, spawns[si].timeMs, rnd);
            if (t.geborenMs > nowMs) {
                continue;
            }
            const double nachlauf = at < nachlaufMs.size() ? nachlaufMs[at] : 0.0;
            if (nowMs - (t.geborenMs + static_cast<double>(t.life)) > nachlauf) {
                continue;
            }
            const double age = nowMs - t.geborenMs;
            merkeVor(t, static_cast<float>(std::min(age, static_cast<double>(t.life))),
                     geo, aus);
        }
    }
}

// Wie lange ein Effekt samt ALLER seiner Folgen etwas zeigen kann.
double wirkdauer(const efx::Effect& ef, const EffectLookup& lookup,
                 std::unordered_map<const efx::Effect*, double>& merk,
                 int tiefe) {
    const auto hat = merk.find(&ef);
    if (hat != merk.end()) {
        return hat->second;
    }
    double nach = 0.0;
    if (tiefe > 0 && lookup) {
        merk[&ef] = laengsteMs(ef);   // gegen Kreise: A startet B startet A
        for (const efx::Primitive& pr : ef.primitives) {
            for (const auto* liste :
                 {&pr.impactFx, &pr.deathFx, &pr.emitFx, &pr.playFx}) {
                for (const std::string& name : *liste) {
                    const efx::Effect* kind = lookup(name);
                    if (kind != nullptr && kind != &ef) {
                        nach = std::max(nach,
                                        wirkdauer(*kind, lookup, merk, tiefe - 1));
                    }
                }
            }
        }
    }
    const double gesamt = laengsteMs(ef) + nach;
    merk[&ef] = gesamt;
    return gesamt;
}

}  // namespace

std::vector<EffectImpact> effectImpactsAt(
    const std::vector<EffectInstance>& live, double nowMs,
    const BspGeometry* geo) {
    // KEIN frueher Ausstieg ohne Geometrie: ein Partikel stirbt am Ende
    // seines Lebens, ob eine Wand in der Naehe ist oder nicht.
    //
    // Ohne Nachschlagen weiss man nicht, wie lange ein Folgeeffekt laeuft;
    // fuenf Sekunden ueber das Leben hinaus sind eine grobe Schranke.
    // effectFollowUpsAt kennt die Folgeeffekte und rechnet es genau.
    constexpr double kNachlaufMs = 5000.0;
    std::vector<EffectImpact> aus;
    for (const EffectInstance& inst : live) {
        if (inst.effect == nullptr || !hatFolgen(*inst.effect)) {
            continue;
        }
        const std::vector<double> nachlauf(inst.effect->primitives.size(),
                                           kNachlaufMs);
        folgeEffekte(inst, nowMs, geo, laengsteMs(*inst.effect) + kNachlaufMs,
                     nachlauf, aus);
    }
    return aus;
}

std::vector<EffectInstance> effectFollowUpsAt(
    const std::vector<EffectInstance>& live, double nowMs,
    const BspGeometry* geo, const EffectLookup& lookup, int maxTiefe,
    std::size_t maxEffekte) {
    std::vector<EffectInstance> aus;
    if (!lookup) {
        return aus;
    }
    std::unordered_map<const efx::Effect*, double> merk;
    std::vector<EffectInstance> stufe;
    for (const EffectInstance& inst : live) {
        if (inst.effect != nullptr && hatFolgen(*inst.effect)) {
            stufe.push_back(inst);
        }
    }
    // Je Primitive: wie lange ihre Folgeeffekte (samt deren Folgen)
    // hoechstens laufen.
    auto nachlaufVon = [&](const EffectInstance& inst,
                           std::unordered_map<const efx::Effect*, double>& memo) {
        std::vector<double> nachlauf;
        nachlauf.reserve(inst.effect->primitives.size());
        for (const efx::Primitive& pr : inst.effect->primitives) {
            double n = 0.0;
            for (const auto* liste :
                 {&pr.impactFx, &pr.deathFx, &pr.emitFx, &pr.playFx}) {
                for (const std::string& name : *liste) {
                    const efx::Effect* kind = lookup(name);
                    if (kind != nullptr) {
                        n = std::max(n, wirkdauer(*kind, lookup, memo, maxTiefe));
                    }
                }
            }
            nachlauf.push_back(n);
        }
        return nachlauf;
    };
    for (int tiefe = 1; tiefe <= maxTiefe && !stufe.empty(); ++tiefe) {
        // Die Bahnen dieser Stufe vorab auf allen Kernen (siehe
        // vorrechnen). Mit einer KOPIE des Merkers fuer wirkdauer: der
        // haengt bei Kreisen an der Reihenfolge der Aufrufe, und die des
        // Durchlaufs unten soll genau die alte bleiben. Weicht die Kopie ab,
        // wird nur anders vorgerechnet - gezeichnet wird, was unten steht.
        {
            std::unordered_map<const efx::Effect*, double> memo = merk;
            std::vector<Vorrechnung> vor;
            for (const EffectInstance& inst : stufe) {
                const std::vector<double> nachlauf = nachlaufVon(inst, memo);
                sammleFolgeBahnen(inst, nowMs, geo,
                                  wirkdauer(*inst.effect, lookup, memo, maxTiefe),
                                  nachlauf, vor);
            }
            vorrechnen(vor, geo);
        }
        std::vector<EffectInstance> naechste;
        for (const EffectInstance& inst : stufe) {
            std::vector<EffectImpact> folgen;
            const std::vector<double> nachlauf = nachlaufVon(inst, merk);
            folgeEffekte(inst, nowMs, geo,
                         wirkdauer(*inst.effect, lookup, merk, maxTiefe),
                         nachlauf, folgen);
            for (const EffectImpact& f : folgen) {
                if (f.startMs > nowMs) {
                    continue;
                }
                const efx::Effect* ef = lookup(f.effectName);
                if (ef == nullptr || ef->primitives.empty()) {
                    continue;
                }
                // Laengst vorbei - samt allem, was er noch ausloesen
                // koennte? Dann nicht mehr bauen.
                if (nowMs - f.startMs > wirkdauer(*ef, lookup, merk, maxTiefe)) {
                    continue;
                }
                EffectInstance e;
                e.effect = ef;
                for (int k = 0; k < 3; ++k) {
                    e.origin[k] = f.origin[k];
                    // Die Flaechennormale mitgeben - ohne sie weiss ein
                    // Decal nicht, wie es auf der Wand liegen soll.
                    e.normal[k] = f.normal[k];
                }
                if (e.normal[0] == 0.0F && e.normal[1] == 0.0F &&
                    e.normal[2] == 0.0F) {
                    e.normal[2] = 1.0F;
                }
                e.startMs = f.startMs;
                e.seed = f.seed;
                aus.push_back(e);
                if (aus.size() >= maxEffekte) {
                    return aus;
                }
                if (hatFolgen(*ef)) {
                    naechste.push_back(std::move(e));
                }
            }
        }
        stufe = std::move(naechste);
    }
    return aus;
}

std::vector<ScreenFlash> effectScreenFlashesAt(
    const std::vector<EffectInstance>& live, double nowMs, const float eye[3],
    const float fwd[3]) {
    std::vector<ScreenFlash> aus;
    for (const EffectInstance& inst : live) {
        if (inst.effect == nullptr) {
            continue;
        }
        bool hatBlitz = false;
        for (const efx::Primitive& pr : inst.effect->primitives) {
            hatBlitz = hatBlitz || pr.type == efx::PrimitiveType::ScreenFlash;
        }
        if (!hatBlitz) {
            continue;
        }
        for (const double began :
             durchgaenge(inst, nowMs, laengsteMs(*inst.effect))) {
            efx::sim::Random planRnd(planSamen(inst, began));
            const std::vector<efx::sim::Spawn> spawns =
                efx::sim::schedule(*inst.effect, planRnd);
            for (std::size_t si = 0; si < spawns.size(); ++si) {
                const auto at = static_cast<std::size_t>(spawns[si].primitiveIndex);
                if (at >= inst.effect->primitives.size() ||
                    inst.effect->primitives[at].type !=
                        efx::PrimitiveType::ScreenFlash) {
                    continue;
                }
                const efx::Primitive& pr = inst.effect->primitives[at];
                efx::sim::Random rnd(teilchenSamen(inst, began, si));
                const Teilchen t = ziehe(inst, at, si, began, spawns[si].timeMs, rnd);
                const double age = nowMs - t.geborenMs;
                if (age < 0.0 || age > static_cast<double>(t.life)) {
                    continue;
                }
                const Lage l = lageBei(t, inst, nowMs, static_cast<float>(age), nullptr);
                const Farbe f = zieheFarbe(pr, rnd);
                // --- CFlash::Init (FxPrimitives.cpp:2258) ----------------
                float dif[3] = {l.welt.x - eye[0], l.welt.y - eye[1],
                                l.welt.z - eye[2]};
                const float dis =
                    std::sqrt(dif[0] * dif[0] + dif[1] * dif[1] + dif[2] * dif[2]);
                if (dis > 0.0F) {
                    for (float& c : dif) {
                        c /= dis;
                    }
                }
                float mod = dif[0] * fwd[0] + dif[1] * fwd[1] + dif[2] * fwd[2];
                if (dis > 600.0F || (mod < 0.5F && dis > 100.0F)) {
                    mod = 0.0F;
                } else if (mod < 0.5F && dis <= 100.0F) {
                    mod += 1.1F;
                }
                mod *= 1.0F - dis * dis / (600.0F * 600.0F);
                if (mod <= 0.0F) {
                    continue;   // nicht im Blick: kein Blitz
                }
                Farbe skaliert = f;
                skaliert.start = f.start * mod;
                skaliert.ende = f.ende * mod;
                // --- CLight::UpdateRGB, dann CFlash::Draw ----------------
                //
                // Die Kurve wie bei jedem rgb-Block; danach klemmt Draw
                // jeden Kanal auf 0..1 (FxPrimitives.cpp:2292).
                const V3 c = farbeBei(skaliert, static_cast<float>(age), t.life);
                ScreenFlash b;
                b.rgb[0] = std::clamp(c.x, 0.0F, 1.0F);
                b.rgb[1] = std::clamp(c.y, 0.0F, 1.0F);
                b.rgb[2] = std::clamp(c.z, 0.0F, 1.0F);
                b.origin[0] = l.welt.x;
                b.origin[1] = l.welt.y;
                b.origin[2] = l.welt.z;
                if (!pr.shaders.empty()) {
                    b.shader = klein(
                        pr.shaders[std::min(t.medium, pr.shaders.size() - 1U)]);
                }
                aus.push_back(std::move(b));
            }
        }
    }
    return aus;
}

void appendScreenFlashes(BspMesh& mesh, const std::vector<ScreenFlash>& blitze,
                         const float eye[3], const float fwd[3],
                         const float right[3], const float up[3],
                         float fovXGrad, const EffectShaderSlots* slots) {
    if (blitze.empty()) {
        return;
    }
    // "Interestingly, if znear is set > than this, then the flash doesn't
    // appear at all." (CFlash::Draw) - die Ansicht nimmt 4 als Nahebene,
    // wie r_znear.
    constexpr float kAbstand = 8.0F;   // FLASH_DISTANCE_FROM_VIEWER
    // "This is assuming that the screen is wider than it is tall."
    const float halb = kAbstand * std::tan(std::clamp(fovXGrad, 1.0F, 179.0F) *
                                           std::numbers::pi_v<float> / 360.0F);
    const float mitte[3] = {eye[0] + fwd[0] * kAbstand, eye[1] + fwd[1] * kAbstand,
                            eye[2] + fwd[2] * kAbstand};
    for (const ScreenFlash& b : blitze) {
        int slot = 0;
        if (slots != nullptr && !b.shader.empty()) {
            const auto f = slots->find(b.shader);
            if (f != slots->end()) {
                slot = f->second;
            }
        }
        const auto beginn = static_cast<std::uint32_t>(mesh.indexes.size());
        const std::uint8_t rgba[4] = {
            static_cast<std::uint8_t>(b.rgb[0] * 255.0F),
            static_cast<std::uint8_t>(b.rgb[1] * 255.0F),
            static_cast<std::uint8_t>(b.rgb[2] * 255.0F), 255};
        addQuad(mesh, mitte, right, up, halb, rgba);
        BspMesh::Batch bt;
        bt.lightmap = -1;
        bt.vertexColour = true;
        bt.shader = slot;
        bt.firstIndex = beginn;
        bt.numIndexes = static_cast<std::uint32_t>(mesh.indexes.size()) - beginn;
        mesh.batches.push_back(bt);
    }
}

ScreenFlashOverlay screenFlashOverlay(const std::vector<ScreenFlash>& blitze) {
    ScreenFlashOverlay o;
    for (const ScreenFlash& b : blitze) {
        for (int k = 0; k < 3; ++k) {
            o.rgb[k] = std::min(1.0F, o.rgb[k] + b.rgb[k]);
        }
    }
    o.aktiv = o.rgb[0] > 0.0F || o.rgb[1] > 0.0F || o.rgb[2] > 0.0F;
    return o;
}

std::vector<EffectLight> effectLightsAt(const std::vector<EffectInstance>& live,
                                        double nowMs) {
    // --- Wie CLight in der Engine (FxPrimitives.cpp, CLight::Update*) -----
    //
    // * JEDER Durchgang bringt sein eigenes Licht mit. Ein Runner mit
    //   delay 200 und einem Licht mit life 5000 hat im Spiel rund 25
    //   Lichter gleichzeitig - hier stand fmod(), also immer nur eins.
    // * Radius und Farbe bleiben auf `start`, solange kein Flag etwas
    //   anderes sagt; ohne rgb-Block ist das Licht weiss (Vorgaben aus
    //   CPrimitiveTemplate, FxTemplate.cpp:53 ff.).
    // * origin ist oertlich wie bei den Teilchen, ausser mit cheapOrgCalc.
    // Die Spannen nehmen weiter die Mitte: die Vorschau soll beim
    // Zurueckspulen dasselbe zeigen.
    constexpr std::size_t kMaxLichter = 32;   // MAX_DLIGHTS
    auto mitte = [](const efx::Range& r, float wennFehlt) {
        return r.set ? (r.ranged ? (r.min + r.max) * 0.5F : r.min) : wennFehlt;
    };
    std::vector<EffectLight> out;
    for (const EffectInstance& inst : live) {
        if (inst.effect == nullptr) {
            continue;
        }
        for (const efx::Primitive& pr : inst.effect->primitives) {
            if (pr.type != efx::PrimitiveType::Light) {
                continue;
            }
            const float leben = mitte(pr.life, 50.0F);
            if (leben <= 0.0F) {
                continue;
            }
            const float verz = mitte(pr.delay, 0.0F);
            // Die Durchgaenge, deren Licht noch brennt.
            std::vector<double> seiten;
            const double seit0 = nowMs - inst.startMs;
            if (seit0 < 0.0) {
                continue;
            }
            if (inst.loops && inst.intervalMs > 1.0F) {
                const double iv = static_cast<double>(inst.intervalMs);
                const double n = std::floor(seit0 / iv);
                for (double k = n; k >= 0.0 && seiten.size() < kMaxLichter; k -= 1.0) {
                    const double s = seit0 - k * iv;
                    if (s - static_cast<double>(verz) > static_cast<double>(leben)) {
                        continue;
                    }
                    seiten.push_back(s);
                }
            } else {
                seiten.push_back(seit0);
            }
            efx::camera::Vec3 org{0.0F, 0.0F, 0.0F};
            if (pr.origin.set) {
                org = efx::camera::Vec3{
                    (pr.origin.min[0] + (pr.origin.ranged ? pr.origin.max[0] : pr.origin.min[0])) * 0.5F,
                    (pr.origin.min[1] + (pr.origin.ranged ? pr.origin.max[1] : pr.origin.min[1])) * 0.5F,
                    (pr.origin.min[2] + (pr.origin.ranged ? pr.origin.max[2] : pr.origin.min[2])) * 0.5F};
                org = inAchse(org, inst.normal,
                              (pr.spawnFlags & efx::kSpawnCheapOrgCalc) != 0U);
            }
            for (const double s : seiten) {
                const double alter = s - static_cast<double>(verz);
                if (alter < 0.0 || alter > static_cast<double>(leben)) {
                    continue;
                }
                const auto jetzt = static_cast<float>(alter);
                EffectLight l;
                l.origin[0] = inst.origin[0] + org.x;
                l.origin[1] = inst.origin[1] + org.y;
                l.origin[2] = inst.origin[2] + org.z;
                efx::curve::Curve sc;
                sc.start = pr.size.present ? mitte(pr.size.start, 1.0F) : 1.0F;
                sc.end = pr.size.present ? mitte(pr.size.end, 1.0F) : 1.0F;
                sc.parm = mitte(pr.size.parm, 0.0F);
                sc.flags = efx::curve::ausDatei(pr.size.curveFlags);
                l.radius = efx::curve::evaluate(
                    sc, jetzt, 0.0F, leben, efx::curve::resolveParm(sc, 0.0F, leben));
                if (l.radius <= 0.0F) {
                    continue;
                }
                efx::curve::Curve rc;
                rc.parm = mitte(pr.rgb.parm, 0.0F);
                rc.flags = efx::curve::ausDatei(pr.rgb.curveFlags);
                const float bb = efx::curve::bias(
                    rc, jetzt, 0.0F, leben, efx::curve::resolveParm(rc, 0.0F, leben));
                for (int k = 0; k < 3; ++k) {
                    auto kanal = [&](const efx::Vec3Range& vr) {
                        if (!pr.rgb.present || !vr.set) {
                            return 1.0F;
                        }
                        return (vr.min[k] + (vr.ranged ? vr.max[k] : vr.min[k])) * 0.5F;
                    };
                    const float a = kanal(pr.rgb.start);
                    const float b = kanal(pr.rgb.end);
                    l.color[k] = std::clamp(a * bb + b * (1.0F - bb), 0.0F, 1.0F);
                }
                out.push_back(l);
            }
        }
    }
    return out;
}

void addDynamicLights(GridLight& licht, const float origin[3],
                      const std::vector<EffectLight>& lichter) {
    if (lichter.empty()) {
        return;
    }
    // Die Richtung wird MIT dem Betrag des gerichteten Lichts gefuehrt -
    // so steht es in der Engine (tr_light.cpp:432 f.):
    //
    //     d = VectorLength( ent->directedLight );
    //     VectorScale( ent->lightDir, d, lightDir );
    //
    // Sonst zoege ein schwaches Licht die Richtung genauso stark wie ein
    // starkes.
    const float betrag = std::sqrt(licht.directed[0] * licht.directed[0] +
                                   licht.directed[1] * licht.directed[1] +
                                   licht.directed[2] * licht.directed[2]);
    float richtung[3] = {licht.dir[0] * betrag, licht.dir[1] * betrag,
                         licht.dir[2] * betrag};

    constexpr float kAtRadius = 16.0F;      // DLIGHT_AT_RADIUS
    constexpr float kMinRadius = 16.0F;     // DLIGHT_MINIMUM_RADIUS
    for (const EffectLight& l : lichter) {
        float dir[3] = {l.origin[0] - origin[0], l.origin[1] - origin[1],
                        l.origin[2] - origin[2]};
        float d = std::sqrt(dir[0] * dir[0] + dir[1] * dir[1] +
                            dir[2] * dir[2]);
        if (d > 1e-4F) {
            for (int k = 0; k < 3; ++k) {
                dir[k] /= d;
            }
        }
        const float power = kAtRadius * l.radius * l.radius;
        if (d < kMinRadius) {
            d = kMinRadius;
        }
        const float staerke = power / (d * d);
        for (int k = 0; k < 3; ++k) {
            licht.directed[k] += staerke * l.color[k] * 255.0F;
            richtung[k] += staerke * dir[k];
        }
        licht.ok = true;
    }
    const float len = std::sqrt(richtung[0] * richtung[0] +
                                richtung[1] * richtung[1] +
                                richtung[2] * richtung[2]);
    if (len > 1e-6F) {
        for (int k = 0; k < 3; ++k) {
            licht.dir[k] = richtung[k] / len;
        }
    }
}


float effectShakeAt(const std::vector<EffectInstance>& live, double nowMs,
                    const float camPos[3]) {
    float staerkste = 0.0F;
    for (const EffectInstance& inst : live) {
        if (inst.effect == nullptr) {
            continue;
        }
        for (const efx::Primitive& pr : inst.effect->primitives) {
            if (pr.type != efx::PrimitiveType::CameraShake) {
                continue;
            }
            const float leben = (pr.life.min + pr.life.max) * 0.5F;
            if (leben <= 0.0F) {
                continue;
            }
            double seit = nowMs - inst.startMs;
            if (seit < 0.0) {
                continue;
            }
            if (inst.loops && inst.intervalMs > 1.0F) {
                seit = std::fmod(seit, static_cast<double>(inst.intervalMs));
            }
            if (seit > static_cast<double>(leben)) {
                continue;
            }
            // "elasticity is actually the intensity" - so steht es im
            // Quelltext ueber dem Aufruf (FxScheduler.cpp:1907).
            const float staerke =
                (pr.elasticity.min + pr.elasticity.max) * 0.5F;
            const float radius = (pr.radius.min + pr.radius.max) * 0.5F;
            if (staerke <= 0.0F || radius <= 0.0F) {
                continue;
            }
            const float dx = camPos[0] - inst.origin[0];
            const float dy = camPos[1] - inst.origin[1];
            const float dz = camPos[2] - inst.origin[2];
            const float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
            if (dist > radius) {
                continue;   // ausserhalb, gar kein Wackeln
            }
            const float hier = staerke * (1.0F - dist / radius);
            // Die STAERKSTE gewinnt, sie summieren sich nicht. In der
            // Engine gibt es nur EIN client_camera.shake_intensity, und
            // jeder Aufruf ueberschreibt es - wer addiert, bekommt bei
            // zwei Explosionen ein doppelt so heftiges Bild wie das Spiel.
            staerkste = std::max(staerkste, hier);
        }
    }
    return staerkste;
}

}  // namespace bhed

// app_wetter.cpp - Regen, Schnee, Staub und Nebelwolken wie im Spiel
//
// Die Pruefung "was zeigt das Spiel, was behaved nicht" (11.10.2026) fand:
// fx_rain, fx_snow, fx_wind und fx_spacedust standen nur als Entity-
// Definitionen da. Im Spiel melden sie Befehle an das Wettersystem des
// Renderers (g_fx.cpp: SP_CreateRain/Snow/Wind, G_FindConfigstringIndex in
// CS_WORLD_FX), und tr_WorldEffects.cpp macht daraus Partikelwolken um die
// Kamera:
//
//   lightrain   500 Tropfen, gfx/world/rain.jpg, 80 lang, 1,2 breit, 2000/s
//   rain       1000 Tropfen, sonst wie lightrain
//   heavyrain  1000 Tropfen, 2800/s
//   acidrain   1000, 2 breit, gruenlich (0.34 0.70 0.34)
//   snow       1000 Flocken, gfx/effects/snowflake1.tga, 300/s
//   spacedust  <count> Staub, gfx/effects/snowpuff1.tga, schwebt
//   fog         60 Wolken, gfx/effects/alpha_smoke2b.tga, 70 gross, 0.2 hell
//   wind / constantwind ( x y z )   treibt alles
//
// "lightsnow" und "heavysnow" kennt der Renderer NICHT (keine Zeile dafuer in
// tr_WorldEffects.cpp) - im Spiel schneit es dann nicht, hier also auch nicht.
//
// Wo es regnet: die Engine fragt Innen-/Aussen-Brushes ab; ohne solche gilt
// alles als draussen ausser festem Material. behaved hat die Brushes nicht -
// hier zaehlt als draussen, wo ein Strahl nach oben auf Himmel oder nichts
// trifft (zwischengespeichert je 64er-Zelle).
//
// Die Teilchen sind aus der Zeit GERECHNET, nicht fortgeschrieben: angehalten
// steht der Regen still, beim Scrubben laeuft er mit, und dieselbe Zeit gibt
// dasselbe Bild.
#include "app_internal.h"
#include "gpumap.h"
#include "bhed/gpuskin.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <unordered_map>
#include <vector>

namespace bhed::gui {

TextureSet::Tex shrink(const image::Image& im, int maxSize);

namespace {

struct Wolke {
    int anzahl = 0;
    std::string bild;
    float hoehe = 1.0F;      // mHeight
    float breite = 1.0F;     // mWidth
    float fall = 300.0F;     // mGravity, Einheiten je Sekunde
    float farbe[3] = {1.0F, 1.0F, 1.0F};
    bool streifen = false;   // mOrientWithVelocity: Band entlang der Fallrichtung
    float bereich = 625.0F;  // halbe Kantenlaenge des Wuerfels um die Kamera
    float bereichZ = 625.0F;
    float schweben = 0.0F;   // seitliches Taumeln (Schnee, Staub)
};

struct Wetter {
    std::string schluessel;          // Karte + Anzahl Entities
    std::vector<Wolke> wolken;
    float wind[3] = {0.0F, 0.0F, 0.0F};
};
Wetter g_wetter;
std::unordered_map<std::int64_t, bool> g_draussen;

int flags(const MapEntity& e) {
    const std::string* f = e.find("spawnflags");
    return f != nullptr ? std::atoi(f->c_str()) : 0;
}

Wolke regen(int anzahl, float fall) {
    Wolke w;
    w.anzahl = anzahl;
    w.bild = "gfx/world/rain";
    w.hoehe = 80.0F;
    w.breite = 1.2F;
    w.fall = fall;
    w.farbe[0] = w.farbe[1] = w.farbe[2] = 0.5F;
    w.streifen = true;
    return w;
}

Wolke nebel(float hell) {
    Wolke w;
    w.anzahl = 60;
    w.bild = "gfx/effects/alpha_smoke2b";
    w.hoehe = 70.0F;
    w.breite = 70.0F;
    w.fall = 0.0F;
    w.farbe[0] = w.farbe[1] = w.farbe[2] = hell;
    w.bereichZ = 150.0F;
    w.schweben = 40.0F;
    return w;
}

void wetterBauen() {
    // Die Wolken haengen an den Entities (Flaggen, count, Wind): jede
    // Aenderung zaehlt (entStand). Der Draussen-Speicher haengt dagegen an der
    // GEOMETRIE - er bleibt beim Bearbeiten und geht erst mit einer neu
    // gelesenen Karte (Code-Pruefung rc592: vorher wirkte eine geaenderte
    // Flagge gar nicht, und eine neu kompilierte Karte behielt ihn).
    const std::string schl = g_app->map.path + "|" + std::to_string(g_app->map.entities.size()) + "|" +
                             std::to_string(g_app->entStand);
    if (schl == g_wetter.schluessel) { return; }
    static const void* geoStand = nullptr;
    static std::size_t geoFlaechen = 0;
    if (geoStand != g_app->geo.surfaces.data() || geoFlaechen != g_app->geo.surfaces.size()) {
        geoStand = g_app->geo.surfaces.data();
        geoFlaechen = g_app->geo.surfaces.size();
        g_draussen.clear();
    }
    g_wetter = Wetter{};
    g_wetter.schluessel = schl;
    for (const MapEntity& e : g_app->map.entities) {
        const int f = flags(e);
        if (e.classname == "fx_rain") {
            if ((f & 1) != 0) {
                g_wetter.wolken.push_back(regen(500, 2000.0F));
            } else if ((f & 2) != 0) {
                g_wetter.wolken.push_back(regen(1000, 2000.0F));
            } else if ((f & 4) != 0) {
                g_wetter.wolken.push_back(regen(1000, 2800.0F));
                g_wetter.wolken.push_back(nebel(0.2F));   // heavyrainfog
            } else if ((f & 8) != 0) {
                Wolke w = regen(1000, 2000.0F);
                w.breite = 2.0F;
                w.farbe[0] = 0.34F;
                w.farbe[1] = 0.70F;
                w.farbe[2] = 0.34F;
                g_wetter.wolken.push_back(w);
            } else {
                g_wetter.wolken.push_back(regen(500, 2000.0F));   // lightrain + wind
                g_wetter.wind[0] += 120.0F;
            }
            if ((f & 32) != 0) { g_wetter.wolken.push_back(nebel(0.2F)); }
        } else if (e.classname == "fx_snow") {
            // lightsnow (1) und heavysnow (4) kennt der Renderer nicht.
            if ((f & 1) == 0 && (f & 4) == 0) {
                Wolke w;
                w.anzahl = 1000;
                w.bild = "gfx/effects/snowflake1";
                w.hoehe = 2.0F;
                w.breite = 2.0F;
                w.fall = 300.0F * 0.25F;   // gebremst durch die Reibung (mFrictionInverse)
                w.farbe[0] = w.farbe[1] = w.farbe[2] = 0.75F;
                w.schweben = 30.0F;
                g_wetter.wolken.push_back(w);
                if ((f & 2) == 0) { g_wetter.wolken.push_back(nebel(0.2F)); }   // snow + fog
            }
            if ((f & 8) != 0) { g_wetter.wolken.push_back(nebel(0.2F)); }
        } else if (e.classname == "fx_spacedust") {
            Wolke w;
            const std::string* c = e.find("count");
            w.anzahl = std::clamp(c != nullptr ? std::atoi(c->c_str()) : 1000, 0, 8000);
            w.bild = "gfx/effects/snowpuff1";
            w.hoehe = 1.2F;
            w.breite = 1.2F;
            w.fall = 0.0F;
            w.farbe[0] = w.farbe[1] = w.farbe[2] = 0.75F;
            w.bereich = 1500.0F;
            w.bereichZ = 1500.0F;
            w.schweben = 20.0F;
            g_wetter.wolken.push_back(w);
        } else if (e.classname == "fx_wind") {
            if ((f & 1) != 0) { g_wetter.wind[0] += 100.0F; }   // leichter Wind
            if ((f & 2) != 0) {
                // constantwind ( AngleVectors(angles) * speed )
                float ang[3] = {0.0F, 0.0F, 0.0F};
                if (const std::string* a = e.find("angles")) {
                    (void)std::sscanf(a->c_str(), "%f %f %f", &ang[0], &ang[1], &ang[2]);
                } else if (const std::string* a1 = e.find("angle")) {
                    ang[1] = static_cast<float>(std::atof(a1->c_str()));
                }
                const std::string* sp = e.find("speed");
                const float tempo = sp != nullptr ? static_cast<float>(std::atof(sp->c_str())) : 500.0F;
                const float p = ang[0] * 3.14159265F / 180.0F;
                const float y = ang[1] * 3.14159265F / 180.0F;
                g_wetter.wind[0] += std::cos(p) * std::cos(y) * tempo;
                g_wetter.wind[1] += std::cos(p) * std::sin(y) * tempo;
                g_wetter.wind[2] += -std::sin(p) * tempo;
            }
            if ((f & 32) != 0) { g_wetter.wolken.push_back(nebel(0.2F)); }
            if ((f & 64) != 0) { g_wetter.wolken.push_back(nebel(0.1F)); }
        }
    }
    if (!g_wetter.wolken.empty()) {
        diag::detail("Wetter: " + std::to_string(g_wetter.wolken.size()) + " Teilchenwolken aus der Karte");
    }
}

const TextureSet::Tex* bild(const std::string& pfad) {
    const auto have = g_app->texCache.find(pfad);
    if (have != g_app->texCache.end()) { return have->second.empty() ? nullptr : &have->second; }
    TextureSet::Tex t;
    for (const std::string& versuch : textureCandidates(pfad)) {
        std::string data;
        if (!readFromArchives(versuch, data)) { continue; }
        const image::Image im = image::decode(reinterpret_cast<const unsigned char*>(data.data()), data.size());
        if (!im.ok || im.width <= 0 || im.height <= 0) { continue; }
        t = shrink(im, 128);
        break;
    }
    const auto put = g_app->texCache.emplace(pfad, std::move(t));
    return put.first->second.empty() ? nullptr : &put.first->second;
}

// Draussen? Strahl nach oben: Himmel oder nichts.
bool draussen(const float p[3]) {
    const auto zelle = [](float v) { return static_cast<std::int64_t>(std::floor(v / 64.0F)); };
    const std::int64_t schl = (zelle(p[0]) & 0xFFFFF) | ((zelle(p[1]) & 0xFFFFF) << 20) | ((zelle(p[2]) & 0xFFFFF) << 40);
    const auto it = g_draussen.find(schl);
    if (it != g_draussen.end()) { return it->second; }
    const float a[3] = {std::floor(p[0] / 64.0F) * 64.0F + 32.0F, std::floor(p[1] / 64.0F) * 64.0F + 32.0F,
                        std::floor(p[2] / 64.0F) * 64.0F + 32.0F};
    const float b[3] = {a[0], a[1], a[2] + 16384.0F};
    const TraceTreffer t = traceRay(g_app->geo, a, b);
    bool aus = !t.hit;
    if (t.hit && t.surface >= 0 && static_cast<std::size_t>(t.surface) < g_app->geo.surfaces.size()) {
        const int sh = g_app->geo.surfaces[static_cast<std::size_t>(t.surface)].shader;
        aus = sh >= 0 && static_cast<std::size_t>(sh) < g_app->geo.shaders.size() &&
              g_app->geo.shaders[static_cast<std::size_t>(sh)].isSky();
    }
    if (g_draussen.size() > 200000U) { g_draussen.clear(); }
    g_draussen.emplace(schl, aus);
    return aus;
}

float zufall(std::uint32_t& s) {
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return static_cast<float>(s & 0xFFFFFFU) / 16777216.0F;
}

}  // namespace

int wetterTeilchen(std::vector<gpu::KlingenQuad>& aus, const Camera& cam) {
    g_app->wetterTeilchenZahl = 0;   // auch, wenn gleich nichts kommt
    if (!g_app->zeigeWetter || g_app->geo.empty()) { return 0; }
    wetterBauen();
    if (g_wetter.wolken.empty()) { return 0; }
    float rechts[3];
    float hoch[3];
    cam.right(rechts);
    cam.up(hoch);
    const double t = std::max(0.0, g_app->playMs) / 1000.0;
    int n = 0;
    for (std::size_t wi = 0; wi < g_wetter.wolken.size(); ++wi) {
        const Wolke& w = g_wetter.wolken[wi];
        const TextureSet::Tex* tex = bild(w.bild);
        const float B = w.bereich * 2.0F;
        const float BZ = w.bereichZ * 2.0F;
        const float vel[3] = {g_wetter.wind[0] * 0.5F, g_wetter.wind[1] * 0.5F, g_wetter.wind[2] * 0.5F - w.fall};
        const float vlen = std::sqrt(vel[0] * vel[0] + vel[1] * vel[1] + vel[2] * vel[2]);
        std::uint32_t saat = 0x9E3779B9U ^ static_cast<std::uint32_t>(wi * 7919U + 1U);
        for (int i = 0; i < w.anzahl; ++i) {
            // Ort im Weltraster (die Wolke bleibt in der Welt stehen, wenn die
            // Kamera sich bewegt), um die Kamera herumgefaltet.
            const float s0 = zufall(saat);
            const float s1 = zufall(saat);
            const float s2 = zufall(saat);
            const float masse = 0.7F + 0.6F * zufall(saat);   // mMass 5..10, gemittelt
            float p[3];
            const float basis[3] = {s0 * B, s1 * B, s2 * BZ};
            for (int k = 0; k < 3; ++k) {
                const float lang = (k == 2) ? BZ : B;
                const float halb = (k == 2) ? w.bereichZ : w.bereich;
                float v = basis[k] + vel[k] * static_cast<float>(t) * masse;
                if (w.schweben > 0.0F && k < 2) {
                    v += std::sin(static_cast<float>(t) * (0.8F + s0) + s1 * 6.28F + static_cast<float>(k)) * w.schweben;
                }
                // in [cam - halb, cam + halb) falten
                float rel = std::fmod(v - (cam.pos[k] - halb), lang);
                if (rel < 0.0F) { rel += lang; }
                p[k] = cam.pos[k] - halb + rel;
            }
            if (!draussen(p)) { continue; }
            // Am Rand des Wuerfels ausblenden, sonst ploppen sie auf.
            const float dx = (p[0] - cam.pos[0]) / w.bereich;
            const float dy = (p[1] - cam.pos[1]) / w.bereich;
            const float dz = (p[2] - cam.pos[2]) / w.bereichZ;
            const float rand = std::max({std::fabs(dx), std::fabs(dy), std::fabs(dz)});
            const float staerke = std::clamp((1.0F - rand) * 3.0F, 0.0F, 1.0F);
            if (staerke <= 0.01F) { continue; }
            gpu::KlingenQuad q;
            if (w.streifen && vlen > 1.0F) {
                float oben[3];
                for (int k = 0; k < 3; ++k) { oben[k] = p[k] - vel[k] / vlen * w.hoehe; }
                gpu::baueKlingenband(oben, p, cam.pos, rechts, w.breite * 0.5F, q.ecke);
            } else {
                // Zur Kamera gedrehtes Quadrat.
                const float hb = w.breite * 0.5F;
                const float hh = w.hoehe * 0.5F;
                for (int k = 0; k < 3; ++k) {
                    q.ecke[0][k] = p[k] - rechts[k] * hb + hoch[k] * hh;
                    q.ecke[1][k] = p[k] + rechts[k] * hb + hoch[k] * hh;
                    q.ecke[2][k] = p[k] + rechts[k] * hb - hoch[k] * hh;
                    q.ecke[3][k] = p[k] - rechts[k] * hb - hoch[k] * hh;
                }
            }
            for (int k = 0; k < 3; ++k) { q.farbe[k] = w.farbe[k] * 255.0F; }
            q.tex = tex;
            q.spur = true;   // eingefaerbt und ausgeblendet, additiv
            q.staerke = staerke;
            aus.push_back(q);
            ++n;
        }
    }
    g_app->wetterTeilchenZahl = n;
    return n;
}

// --- Lens-Flares (rc592) -------------------------------------------------------------
//
// Bis rc591 uebersprungen ("Flares und Unfug"). Im Spiel (tr_surface.cpp,
// RB_SurfaceFlare; r_flares steht auf 1): ein Schild zur Kamera, 3 Einheiten
// vor der Flaeche entlang ihrer Normalen, Radius 30 (oder portalRange des
// Shaders), naeher als 512 Einheiten entsprechend kleiner, mindestens 5; hell
// nach dem Winkel zur Flaeche; nur, wenn der Punkt nicht verdeckt ist
// (RB_TestZFlare - hier ein Strahl von der Kamera).
int flareSchilde(std::vector<gpu::KlingenQuad>& aus, const Camera& cam, float aspekt) {
    g_app->flareZahl = 0;   // auch, wenn gleich nichts kommt
    if (!g_app->zeigeFlares || g_app->geo.empty()) { return 0; }
    float links[3];
    float oben[3];
    float f[3];
    cam.right(links);
    cam.up(oben);
    cam.forward(f);
    // Halbe Blickwinkel als Tangens - fuer den Test, ob das Schild ueberhaupt
    // im Bild liegt (siehe unten).
    const float tanY = std::tan(cam.fovY * 3.14159265F / 360.0F);
    const float tanX = tanY * (aspekt > 0.0F ? aspekt : 1.0F);
    // Neu holen, sobald texCache geleert wurde (texCacheStand) - die Adresse
    // von texCache bleibt dabei gleich, der Zeiger hing sonst ins Leere.
    static const TextureSet::Tex* ersatz = nullptr;
    static int ersatzStand = -1;
    if (ersatzStand != g_app->texCacheStand) {
        ersatz = bild("gfx/misc/flare");
        ersatzStand = g_app->texCacheStand;
    }
    // Erst die Kandidaten (im Bild, vor der Kamera), dann ihre Strahlen auf
    // alle Kerne verteilt (rc594), dann die Schilde in derselben Reihenfolge
    // wie vorher.
    struct Kandidat {
        std::size_t flaeche;
        float o[3];
        float dir[3];
        float radius;
        bool frei;
    };
    std::vector<Kandidat> kandidaten;
    for (std::size_t i = 0; i < g_app->geo.surfaces.size(); ++i) {
        const BspSurface& s = g_app->geo.surfaces[i];
        if (s.type != BspSurface::Type::Flare) { continue; }
        float o[3];
        for (int k = 0; k < 3; ++k) { o[k] = s.flareOrt[k] + s.flareNormale[k] * 3.0F; }
        float dir[3] = {o[0] - cam.pos[0], o[1] - cam.pos[1], o[2] - cam.pos[2]};
        const float dist = std::sqrt(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
        if (dist < 1.0F || dist > 16384.0F) { continue; }
        for (float& v : dir) { v /= dist; }
        // vor der Kamera?
        if (dir[0] * f[0] + dir[1] * f[1] + dir[2] * f[2] < 0.05F) { continue; }
        float radius = 30.0F;
        if (dist < 512.0F) { radius = radius * dist / 512.0F; }
        radius = std::max(radius, 5.0F);
        // --- Liegt das Schild im Bild? (rc592, Pruefung) ------------------
        //
        // Das Schild steht zur Kamera gedreht, alle Ecken in derselben Tiefe
        // z; seine Ausdehnung quer ist +-radius. Liegt es ganz ausserhalb des
        // Blickkegels, ist es unsichtbar - dann braucht es auch den Strahl
        // nicht, der quer durch die Karte geht. hoth2 hat 98 Flares; der
        // Strahl fuer jedes kostete rund 8 ms je Bild.
        {
            const float v[3] = {o[0] - cam.pos[0], o[1] - cam.pos[1], o[2] - cam.pos[2]};
            const float z = v[0] * f[0] + v[1] * f[1] + v[2] * f[2];
            const float x = v[0] * links[0] + v[1] * links[1] + v[2] * links[2];
            const float y = v[0] * oben[0] + v[1] * oben[1] + v[2] * oben[2];
            const float rand = radius * 1.05F + 1.0F;
            if (std::fabs(x) - rand > z * tanX || std::fabs(y) - rand > z * tanY) { continue; }
        }
        kandidaten.push_back({i, {o[0], o[1], o[2]}, {dir[0], dir[1], dir[2]}, radius, false});
    }
    // verdeckt?
    spurenVerteilt(kandidaten.size(), [&](std::size_t k) {
        const TraceTreffer t = traceRay(g_app->geo, cam.pos, kandidaten[k].o);
        kandidaten[k].frei = !(t.hit && t.fraction < 0.98F);
    });
    int n = 0;
    for (const Kandidat& kd : kandidaten) {
        if (!kd.frei) { continue; }
        const BspSurface& s = g_app->geo.surfaces[kd.flaeche];
        const float* o = kd.o;
        const float* dir = kd.dir;
        const float radius = kd.radius;
        float d = -(dir[0] * s.flareNormale[0] + dir[1] * s.flareNormale[1] + dir[2] * s.flareNormale[2]);
        if (d < 0.0F) { d = -d; }
        gpu::KlingenQuad q;
        for (int k = 0; k < 3; ++k) {
            q.ecke[0][k] = o[k] - links[k] * radius + oben[k] * radius;
            q.ecke[1][k] = o[k] + links[k] * radius + oben[k] * radius;
            q.ecke[2][k] = o[k] + links[k] * radius - oben[k] * radius;
            q.ecke[3][k] = o[k] - links[k] * radius - oben[k] * radius;
            // Das Bild des Flare-Shaders (rgbGen vertex): grau nach Winkel,
            // dazu die Farbe der Flaeche, wo q3map2 sie gesetzt hat.
            const float fc = (s.flareFarbe[0] + s.flareFarbe[1] + s.flareFarbe[2]) > 0.01F ? s.flareFarbe[k] : 1.0F;
            q.farbe[k] = std::clamp(d * fc * 255.0F, 0.0F, 255.0F);
        }
        const TextureSet::Tex* tex = nullptr;
        if (s.shader >= 0 && static_cast<std::size_t>(s.shader) < g_app->textures.byShader.size() &&
            !g_app->textures.byShader[static_cast<std::size_t>(s.shader)].empty()) {
            tex = &g_app->textures.byShader[static_cast<std::size_t>(s.shader)];
        }
        q.tex = tex != nullptr ? tex : ersatz;
        q.spur = true;
        q.staerke = 1.0F;
        aus.push_back(q);
        ++n;
    }
    g_app->flareZahl = n;
    return n;
}

}  // namespace bhed::gui

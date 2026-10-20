// app_ton.cpp - der Vorschauton wie in der Engine (rc601, Paket "ton")
//
// Gemeldet (Tester, Movie Duels): "You can hear everything on the map,
// including all the invisible NPCs sabers".
//
// Die Entfernungsformel war schon richtig (sound::raumlaut = S_SpatializeOrigin).
// Falsch war alles darum herum:
//
//   * Der Hoerer war die freie Editorkamera, nicht die Kamera, die die
//     Ansicht zeigt. Die Engine hoert von cg.refdef.vieworg mit
//     cg.refdef.viewaxis (cg_view.cpp:3307 MD, OpenJK :2243, cgi_S_Respatialize), im
//     Kameramodus also von der Skriptkamera.
//   * Jeder Klang bekam seine Lautstaerke EINMAL beim Start. Die Engine
//     rechnet jeden laufenden Kanal in jedem Bild neu und baut die
//     Schleifen jedes Bild frisch (S_Respatialize, Movie Duels
//     code/client/snd_dma.cpp:2571 ff., S_AddLoopSounds :2082 ff.).
//   * Nichts fragte, ob die Quelle im Snapshot ist (PVS). Schleifen, Tueren,
//     fx_runner und G_Sound kommen nur ueber den Snapshot beim Client an
//     (SV_AddEntitiesVisibleFromPoint, sv_snapshot.cpp:373 ff. MD).
//   * Schwertklaenge auch fuer unsichtbare Figuren, dazu ein soundOff, das
//     es in der Engine bei SET_SABERACTIVE gar nicht gibt.
//   * Die Mischpegel: alles mit 1.0, die Engine mischt Effekte mit
//     s_volume 0.5 und Musik mit 0.25 gegen die Stimmen mit 1.0.
//
// Diese Datei fuehrt deshalb selbst Buch ueber alles, was klingt: jeder
// Einmalklang mit Ort oder Figur und Kanal, je Datei ein Schleifenkanal, je
// Figur der Stand der Klingen. tonBild() regelt einmal je Bild alles nach.
// Der Mischer bekommt nur noch Nummern und Pegel (sound::Mixer::starte,
// setzePegel, stoppe).
//
// Stumm (Selbsttest, g_app->stumm) laeuft alles genauso, nur ohne Mischer -
// die Mitschrift (klangMitschrift) zeigt, was gespielt worden waere.
#include "app_internal.h"
#include "bhed/saberkontakt.h"   // rc601 integ: saberZuendTeiler, saberSuperBruchSieg

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace bhed::gui {

// --- rc601 ton: Mitschrift fuer den Selbsttest -------------------------------
namespace {
std::vector<KlangMitschrift> g_tonMitschrift;
bool g_tonMitschriftAn = false;
// Eine Obergrenze, damit ein vergessenes "an" den Speicher nicht fuellt.
constexpr std::size_t kTonMitschriftMax = 50000;
}  // namespace

std::vector<KlangMitschrift>& klangMitschrift() { return g_tonMitschrift; }

void klangMitschriftAn(bool an) { g_tonMitschriftAn = an; }

void klangMitschreiben(const KlangMitschrift& m) {
    if (!g_tonMitschriftAn || g_tonMitschrift.size() >= kTonMitschriftMax) {
        return;
    }
    g_tonMitschrift.push_back(m);
}

void klangMitschreiben(char art, const std::string& quelle, const std::string& datei, float links,
                       float rechts, std::uint64_t id, double abMs) {
    if (!g_tonMitschriftAn) {
        return;
    }
    KlangMitschrift m;
    m.art = art;
    m.quelle = quelle;
    m.datei = datei;
    m.links = links;
    m.rechts = rechts;
    m.id = id;
    m.ms = (g_app != nullptr) ? g_app->playMs : 0.0;
    m.abMs = abMs;
    klangMitschreiben(m);
}

// --- rc601 ton: der Raumklang ------------------------------------------------
namespace {

// Das feste Ausgabeformat, wie playSound/klangFuerWiedergabe (app.cpp).
constexpr int kTonRate = 44100;
constexpr int kTonKanaele = 2;

// Der Kasten eines NPC oder des Spielers: DEFAULT_MINS_0/1 = -16,
// DEFAULT_MAXS_0/1 = 16 (Movie Duels g_client.cpp:92 ff.), DEFAULT_MINS_2 =
// -24, DEFAULT_MAXS_2 = 40 (bg_public.h:75 f.).
constexpr float kFigurMin[3] = {-16.0F, -16.0F, -24.0F};
constexpr float kFigurMax[3] = {16.0F, 16.0F, 40.0F};
// Punktentities (target_speaker, fx_runner, Temp-Entities) haben mins =
// maxs = 0 - SV_LinkEntity macht daraus den Kasten Ursprung -1..+1.
constexpr float kPunkt[3] = {0.0F, 0.0F, 0.0F};

// Ein laufender Einmalklang - das, was ein channel_t der Engine ist.
struct LaufenderKlang {
    std::uint64_t id = 0;          // eigene Nummer (Mitschrift)
    std::uint64_t stimme = 0;      // Stimme im Mischer, 0: keine (stumm, kein Geraet)
    std::string datei;
    std::string quelle;
    std::string figur;             // folgt dieser Figur (s_entityPosition)
    int moverModell = -1;          // folgt diesem Brush-Modell
    float ort[3]{};                // fester Ort bzw. zuletzt bekannter Ort
    sound::Kanal kanal = sound::Kanal::Auto;
    bool amHoerer = false;         // ch->entnum == listener_number
    std::uint64_t schluessel = 0;
    double endeMs = 0.0;           // ab hier ist er durch
    float links = -1.0F;           // zuletzt in den Mischer gegeben
    float rechts = -1.0F;
};

// Je Datei EIN Schleifenkanal (S_AddLoopSounds).
struct SchleifenKanal {
    std::uint64_t id = 0;
    std::uint64_t stimme = 0;
    float links = -1.0F;
    float rechts = -1.0F;
};

// Die Laenge jeder Klinge als Anteil von lengthMax - so wie CG_Player sie
// fuehrt: sie waechst und schrumpft nur, waehrend die Figur gezeichnet wird.
struct KlingenStand {
    std::array<std::array<float, kMaxKlingen>, 2> anteil{};
};

struct TonWelt {
    std::vector<LaufenderKlang> laufend;
    std::map<std::string, SchleifenKanal> schleifen;
    std::map<std::string, std::map<std::string, sound::Raumlaut>> beitrag;   // Datei -> Quelle -> Raumlaut (Mitschrift)
    std::map<std::string, KlingenStand> klingen;
    bool klingenBereit = false;
    double klingenMs = 0.0;
    // ImGui-Bild des letzten tonBild: nur in diesem Bild gemeldete
    // Schleifen leben (tonWache).
    int bildNr = -1;
    std::set<std::string> nichtGefunden;
    std::uint64_t naechsteId = 1;
    bool keinGeraetGemeldet = false;
    double naechsterVersuch = 0.0;   // ImGui::GetTime(), siehe tonGeraet
};
TonWelt g_ton;

std::string tonKlein(std::string s) {
    for (char& c : s) { c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }
    return s;
}

bool istSpieler(const std::string& wer) { return tonKlein(wer) == "player"; }

bool tonNaheGleich(float a, float b) { return std::fabs(a - b) <= 1e-4F; }

// Der Puffer einer Datei fuer den Mischer - OHNE Kopie. klangFuerWiedergabe
// hat ihn schon entpackt und umgerechnet; alle Stimmen lesen direkt diesen
// Eintrag in klangPuffer.
//
// rc601 ton (Review): "tonDaten copies every played sound again (make_shared
// copy of *p); g_ton.daten is never released ... every preview sound stays in
// memory twice for the session (~106 MB per 10 min of dialogue)". Jetzt der
// Aliasing-Konstruktor von shared_ptr OHNE Besitz: er zeigt auf den Eintrag,
// gibt ihn aber nie frei. Das traegt, weil
//   * klangPuffer nur waechst - klangFuerWiedergabe legt mit emplace an,
//     nichts loescht oder ersetzt einen Eintrag - und die Knoten einer
//     std::map ihre Adresse behalten;
//   * der Mischer im Hauptfaden mischt (Audio::update), kein zweiter Faden
//     liest mit;
//   * App::klangPuffer VOR App::audio steht, also erst NACH dem Tongeraet
//     abgebaut wird.
// Der Selbsttest ersetzt und loescht Eintraege - aber stumm, ohne Mischer.
sound::Mixer::Daten tonDaten(const std::string& datei) {
    const std::vector<std::int16_t>* p = klangFuerWiedergabe(datei);
    if (p == nullptr || p->empty()) {
        // Wie playSound: in die Statusliste, je Datei einmal.
        if (g_ton.nichtGefunden.insert(datei).second) {
            char msg[300];
            std::snprintf(msg, sizeof(msg), tr(Str::SoundNotFound), datei.c_str());
            addStatus(msg);
        }
        return {};
    }
    return sound::Mixer::Daten(sound::Mixer::Daten{}, p);
}

double tonLaengeMs(const sound::Mixer::Daten& d) {
    return d ? static_cast<double>(d->size()) / kTonKanaele / kTonRate * 1000.0 : 0.0;
}

// Das Tongeraet, oder nullptr (stumm, oder es laesst sich nicht oeffnen).
Audio* tonGeraet() {
    if (g_app->stumm) {
        return nullptr;
    }
    tonUebernehmen(true);
    if (g_app->audio.ready()) {
        return &g_app->audio;
    }
    // Ohne Geraet nicht in JEDEM Bild neu versuchen (die Schleifen fragen
    // jedes Bild) - hoechstens alle zwei Sekunden.
    const double jetzt = ImGui::GetTime();
    if (jetzt < g_ton.naechsterVersuch) {
        return nullptr;
    }
    if (!g_app->audio.open(kTonRate, kTonKanaele)) {
        g_ton.naechsterVersuch = jetzt + 2.0;
        if (!g_ton.keinGeraetGemeldet) {
            g_ton.keinGeraetGemeldet = true;
            addStatus(tr(Str::SoundNoDevice));
        }
        return nullptr;
    }
    return &g_app->audio;
}

// Kaeme eine Entity mit diesem Kasten in den Snapshot? Vom Hoerer aus: im
// Kameramodus nimmt der Server den Ort der Skriptkamera (CG_CAMERA_POS,
// sv_snapshot.cpp:607 MD), und die Ansicht zeigt dieselbe Kamera.
bool tonKastenSichtbar(const float auge[3], const float ort[3], const float mins[3], const float maxs[3]) {
    float mn[3];
    float mx[3];
    for (int k = 0; k < 3; ++k) {
        // SV_LinkEntity: absmin/absmax um eine Einheit erweitert
        // (sv_world.cpp:296 ff. MD).
        mn[k] = ort[k] + mins[k] - 1.0F;
        mx[k] = ort[k] + maxs[k] + 1.0F;
    }
    return kastenImSnapshot(g_app->geo, auge, mn, mx);
}

// Ein Brush-Modell im Snapshot? Mit seinem Kasten am aktuellen Ort; ein
// gedrehtes nimmt wie SV_LinkEntity den Radius (sv_world.cpp:277 ff. MD). Ein
// unsichtbares (SVF_NOCLIENT, zerbrochen) wird nicht geschickt.
bool tonMoverSichtbar(const float auge[3], int modell, double ms) {
    if (modell <= 0 || static_cast<std::size_t>(modell) >= g_app->geo.models.size()) {
        return true;
    }
    const BspGeometry::SubModel& sm = g_app->geo.models[static_cast<std::size_t>(modell)];
    const MoverSim::Stand st = g_app->moverSim.at(modell, ms);
    if (st.bekannt && !st.sichtbar) {
        return false;
    }
    float ort[3] = {0.0F, 0.0F, 0.0F};
    float mn[3];
    float mx[3];
    for (int k = 0; k < 3; ++k) {
        ort[k] = st.bekannt ? st.origin[k] : 0.0F;
        mn[k] = sm.mins[k];
        mx[k] = sm.maxs[k];
    }
    if (st.bekannt && (st.angles[0] != 0.0F || st.angles[1] != 0.0F || st.angles[2] != 0.0F)) {
        float r = 0.0F;
        for (int k = 0; k < 3; ++k) {
            const float a = std::max(std::fabs(mn[k]), std::fabs(mx[k]));
            r += a * a;
        }
        r = std::sqrt(r);   // RadiusFromBounds
        for (int k = 0; k < 3; ++k) {
            mn[k] = -r;
            mx[k] = r;
        }
    }
    return tonKastenSichtbar(auge, ort, mn, mx);
}

// Die Lautstaerke je Ohr VOR dem Mischpegel, wie S_Respatialize sie setzt:
// auf der Hoerer-Entity und fuer CHAN_VOICE_GLOBAL/CHAN_ANNOUNCER voll mit
// master_vol (snd_dma.cpp:2698 ff. MD), sonst S_SpatializeOrigin.
sound::Raumlaut tonRoh(const float ort[3], const float ohr[3], const float rechts[3], sound::Kanal k,
                       bool amHoerer) {
    const float master = (amHoerer && static_cast<int>(k) < static_cast<int>(sound::Kanal::Ambient))
                             ? sound::kSoundFMaxVol
                             : 1.0F;
    if (amHoerer || k == sound::Kanal::VoiceGlobal || k == sound::Kanal::Announcer) {
        return sound::Raumlaut{master, master};
    }
    sound::Raumlaut r = sound::raumlaut(ort, ohr, rechts, k);
    r.links *= master;
    r.rechts *= master;
    return r;
}

// Was in den Mischer geht: roh mal s_volume bzw. s_volumeVoice. SOUND_FMAXVOL
// steckt schon in tonRoh (master_vol).
sound::Raumlaut tonGemischt(const sound::Raumlaut& roh, sound::Kanal k) {
    const float p = sound::kanalPegel(k, false, tonPegel());
    return sound::Raumlaut{roh.links * p, roh.rechts * p};
}

// Wo eine Figur steht. Die Engine nimmt s_entityPosition, und das wird nur
// fuer Entities im Snapshot nachgefuehrt (CG_SetEntitySoundPosition aus
// CG_EntityEffects, cg_ents.cpp:149 ff. MD) - der Spieler (Entity 0) ist
// immer dabei. Ausserhalb bleibt der zuletzt bekannte Ort stehen.
void tonFigurOrt(LaufenderKlang& k, const float ohr[3], double ms) {
    if (!k.figur.empty()) {
        if (const Actor* a = g_app->scene.find(k.figur); a != nullptr) {
            const ActorState st = a->at(ms);
            if (istSpieler(k.figur) || tonKastenSichtbar(ohr, st.pos, kFigurMin, kFigurMax)) {
                for (int i = 0; i < 3; ++i) { k.ort[i] = st.pos[i]; }
            }
        }
    } else if (k.moverModell > 0) {
        // CG_SetEntitySoundPosition fuer SOLID_BMODEL: Mitte des Modells am
        // aktuellen Ort (MoverSim::klangOrt).
        if (tonMoverSichtbar(ohr, k.moverModell, ms)) {
            g_app->moverSim.klangOrt(k.moverModell, ms, k.ort);
        }
    }
}

void tonStimmeStoppen(std::uint64_t stimme) {
    if (stimme != 0 && !g_app->stumm) {
        g_app->audio.stoppe(stimme);
    }
}

// --- Die Schleifen eines Bildes (S_AddLoopSounds) ------------------------
//
// Jedes Bild frisch: wer jetzt eine Schleife hat, meldet sie mit Ort und
// Kanal; alle derselben Datei werden EIN Kanal, je Ohr summiert und gekappt.
struct SchleifenQuelle {
    std::string datei;
    std::string quelle;
    float ort[3]{};
    sound::Kanal kanal = sound::Kanal::Auto;
};

void tonSchleifen(const float ohr[3], const float rechts[3], const std::vector<SchleifenQuelle>& quellen) {
    // Je Datei die Beitraege; als ganze Zahlen fuer die Stille-Probe
    // (left_total/right_total sind int in S_AddLoopSounds).
    struct Summe {
        std::vector<sound::Raumlaut> beitraege;
        int linksGanz = 0;
        int rechtsGanz = 0;
    };
    std::map<std::string, Summe> jeDatei;
    std::map<std::string, std::map<std::string, sound::Raumlaut>> beitrag;
    for (const SchleifenQuelle& q : quellen) {
        if (q.datei.empty()) {
            continue;
        }
        // loopSounds[].volume = SOUND_MAXVOL (snd_dma.cpp:2011 MD): keine
        // Hoerer-Ausnahme, jede Schleife wird raeumlich gerechnet.
        const sound::Raumlaut r = sound::raumlaut(q.ort, ohr, rechts, q.kanal);
        Summe& s = jeDatei[q.datei];
        s.beitraege.push_back(r);
        s.linksGanz += static_cast<int>(255.0F * r.links);
        s.rechtsGanz += static_cast<int>(255.0F * r.rechts);
        beitrag[q.datei][q.quelle] = r;
    }
    // Die Beitraege mitschreiben, wenn sie sich aendern.
    for (const auto& [datei, je] : beitrag) {
        for (const auto& [quelle, r] : je) {
            const auto alt = g_ton.beitrag[datei].find(quelle);
            if (alt == g_ton.beitrag[datei].end() || !tonNaheGleich(alt->second.links, r.links) ||
                !tonNaheGleich(alt->second.rechts, r.rechts)) {
                klangMitschreiben('B', quelle, datei, r.links, r.rechts, 0);
            }
        }
    }
    g_ton.beitrag = std::move(beitrag);

    Audio* geraet = nullptr;
    bool geraetGefragt = false;
    for (auto it = g_ton.schleifen.begin(); it != g_ton.schleifen.end();) {
        const auto s = jeDatei.find(it->first);
        // "if (left_total == 0 && right_total == 0) continue; // not audible"
        if (s == jeDatei.end() || (s->second.linksGanz <= 0 && s->second.rechtsGanz <= 0)) {
            tonStimmeStoppen(it->second.stimme);
            klangMitschreiben('X', std::string{}, it->first, 0.0F, 0.0F, it->second.id);
            it = g_ton.schleifen.erase(it);
        } else {
            ++it;
        }
    }
    for (const auto& [datei, s] : jeDatei) {
        if (s.linksGanz <= 0 && s.rechtsGanz <= 0) {
            continue;
        }
        // Der Kanal kommt aus S_PickChannel(0, 0) und Channel_Clear setzt
        // entchannel 0 = CHAN_AUTO: er mischt mit s_volume.
        const sound::Raumlaut g = tonGemischt(sound::schleifenSumme(s.beitraege), sound::Kanal::Auto);
        auto k = g_ton.schleifen.find(datei);
        if (k == g_ton.schleifen.end()) {
            SchleifenKanal neu;
            neu.id = g_ton.naechsteId++;
            neu.links = g.links;
            neu.rechts = g.rechts;
            if (!geraetGefragt) {
                geraet = tonGeraet();
                geraetGefragt = true;
            }
            if (geraet != nullptr) {
                if (sound::Mixer::Daten d = tonDaten(datei)) {
                    sound::Mixer::Start st;
                    st.links = g.links;
                    st.rechts = g.rechts;
                    st.schleife = true;
                    neu.stimme = geraet->starte(std::move(d), st);
                }
            } else if (!g_app->stumm) {
                continue;   // kein Geraet: nicht als laufend fuehren
            } else {
                (void)tonDaten(datei);   // stumm: nur "nicht gefunden" melden
            }
            klangMitschreiben('S', std::string{}, datei, g.links, g.rechts, neu.id);
            g_ton.schleifen.emplace(datei, neu);
            continue;
        }
        SchleifenKanal& kanal = k->second;
        // Eine Stimme, die der Mischer verdraengt hat, kommt neu - im
        // gemeinsamen Takt, also an derselben Stelle.
        if (!g_app->stumm && kanal.stimme != 0 && !g_app->audio.laeuft(kanal.stimme)) {
            kanal.stimme = 0;
            if (sound::Mixer::Daten d = tonDaten(datei)) {
                sound::Mixer::Start st;
                st.links = g.links;
                st.rechts = g.rechts;
                st.schleife = true;
                kanal.stimme = g_app->audio.starte(std::move(d), st);
            }
        }
        if (!tonNaheGleich(kanal.links, g.links) || !tonNaheGleich(kanal.rechts, g.rechts)) {
            kanal.links = g.links;
            kanal.rechts = g.rechts;
            if (kanal.stimme != 0 && !g_app->stumm) {
                (void)g_app->audio.setzePegel(kanal.stimme, g.links, g.rechts);
            }
            klangMitschreiben('P', std::string{}, datei, g.links, g.rechts, kanal.id);
        }
    }
}

// rc601 ton (Review): alle Schleifenkanaele anhalten - so, als meldete in
// diesem Bild niemand eine Schleife (S_ClearLoopingSounds ohne folgendes
// S_AddLoopingSound). Die Einmalklaenge bleiben unberuehrt.
void tonSchleifenAnhalten() {
    for (const auto& [datei, kanal] : g_ton.schleifen) {
        tonStimmeStoppen(kanal.stimme);
        klangMitschreiben('X', std::string{}, datei, 0.0F, 0.0F, kanal.id);
    }
    g_ton.schleifen.clear();
    g_ton.beitrag.clear();
}

// --- Die Klingen: soundOn wie CG_Player ----------------------------------
//
// Q3_SetSaberActive (Q3_Interface.cpp:6852 ff. MD) ruft nur
// ps.SaberActivate()/SaberDeactivate() - KEIN Klang, auch kein soundOff. Den
// Zuendklang spielt CG_Player (cg_players.cpp:16525 ff. MD), und zwar fuer
// jede Klinge, die an ist und bei Laenge 0 anfaengt; die Laenge waechst und
// schrumpft dort mit lengthMax/10 je 100 ms (cg_ignitionSpeed 1; rc601 integ:
// das Wachsen mit dem Teiler des Pakets saber, saberZuendTeiler - Movie Duels
// SS_FAST /5 - und PM_SuperBreakWinAnim gleich ganz lang). CG_Player
// laeuft aber nur fuer Entities im Snapshot, und es kehrt zurueck bei
// EF_NODRAW (:15832) und fuer den Spieler im Kameramodus (:15859). Also:
//   - unsichtbar, nicht gespawnt oder ausserhalb des PVS: kein Klang, und
//     die Laenge bleibt bei 0 - der Klang kommt im ersten Bild, in dem die
//     Figur gezeichnet wird (aufgeschoben, nicht verschluckt);
//   - ein zweites "true" bei brennender Klinge: still (Laenge > 0);
//   - wieder an, bevor die Klinge ganz eingefahren ist: still.
// Einmal je Bild und Figur (cg_saberOnSoundTime).
void tonKlingen(const float ohr[3], double jetzt, double vorher, const std::vector<ActorState>& zustaende) {
    const double dt = std::max(0.0, jetzt - vorher);
    for (std::size_t ai = 0; ai < g_app->scene.actors.size() && ai < zustaende.size(); ++ai) {
        const Actor& a = g_app->scene.actors[ai];
        const ActorState& st = zustaende[ai];
        // "Handle saber": weapon == WP_SABER (haeltSaber), nicht entfernt,
        // gezeichnet (visible deckt SET_INVISIBLE, das Spawnen und den
        // Spieler im Kameramodus ab), und im Snapshot.
        const bool gezeichnet = st.visible && !st.removed && haeltSaber(st) &&
                                (istSpieler(a.name) || tonKastenSichtbar(ohr, st.pos, kFigurMin, kFigurMax));
        if (!gezeichnet) {
            continue;
        }
        KlingenStand& ks = g_ton.klingen[a.name];
        const int schwerter = st.dualSabers ? 2 : 1;
        // rc601 integ: Ausfahrrate wie die Zeichnung (Paket saber).
        const double zuendTeiler = saberZuendTeiler(saberStilJetzt(a, st));
        std::string torso = st.torsoAnimation.empty() ? st.animation : st.torsoAnimation;
        for (char& c : torso) { c = static_cast<char>(std::toupper(static_cast<unsigned char>(c))); }
        const bool superSieg = saberSuperBruchSieg(torso);
        bool gespielt = false;
        for (int n = 0; n < schwerter; ++n) {
            const SaberZustand& sz = st.saber[static_cast<std::size_t>(n)];
            for (int b = 0; b < sz.numBlades && b < kMaxKlingen; ++b) {
                float& anteil = ks.anteil[static_cast<std::size_t>(n)][static_cast<std::size_t>(b)];
                if (!sz.klinge[static_cast<std::size_t>(b)].an) {
                    anteil = std::max(0.0F, anteil - static_cast<float>(dt / 1000.0));
                    continue;
                }
                if (anteil >= 1.0F) {
                    continue;
                }
                if (anteil <= 0.0F && !gespielt) {
                    gespielt = true;
                    const int art = sz.art;
                    if (art >= 0 && static_cast<std::size_t>(art) < a.saberArten.size()) {
                        const std::string& datei = a.saberArten[static_cast<std::size_t>(art)].soundOn;
                        if (!datei.empty()) {
                            // cgi_S_StartSound(NULL, cent->currentState.number,
                            // CHAN_AUTO, saber_on_sound): auf der Entity.
                            KlangWunsch w;
                            w.datei = datei;
                            w.quelle = a.name;
                            w.figur = a.name;
                            for (int i = 0; i < 3; ++i) { w.ort[i] = st.pos[i]; }
                            w.kanal = sound::Kanal::Auto;
                            w.amHoerer = istSpieler(a.name);
                            (void)tonStarte(w);
                        }
                    }
                }
                // rc601 integ: wie saberWandTakt - "just keep it full length!"
                // bei PM_SuperBreakWinAnim, sonst lengthMax/Teiler je 100 ms.
                if (superSieg) {
                    anteil = 1.0F;
                } else {
                    anteil = std::min(1.0F, anteil + static_cast<float>(dt / (100.0 * zuendTeiler)));
                }
            }
        }
    }
}

// Die Klingen beim Einstieg: so lang, wie die Szene sie zur Zeit `ms` hat.
void tonKlingenAnfang(double ms) {
    g_ton.klingen.clear();
    for (const Actor& a : g_app->scene.actors) {
        const ActorState st = a.at(ms);
        KlingenStand& ks = g_ton.klingen[a.name];
        for (int n = 0; n < 2; ++n) {
            const SaberZustand& sz = st.saber[static_cast<std::size_t>(n)];
            for (int b = 0; b < sz.numBlades && b < kMaxKlingen; ++b) {
                const float mx = sz.klinge[static_cast<std::size_t>(b)].max;
                ks.anteil[static_cast<std::size_t>(n)][static_cast<std::size_t>(b)] =
                    (mx > 0.0F) ? std::clamp(klingenLaenge(st, n, b, ms) / mx, 0.0F, 1.0F) : 0.0F;
            }
        }
    }
}

// Der Ort einer Entity, wie G_Sound ihn nimmt: ent->currentOrigin. Eine
// Figur steht, wo die Szene sie hat; eine Kartenentity auf ihrem "origin" -
// und ein Brush ohne Origin-Brush auf (0,0,0), denn seine Geometrie steht in
// Weltkoordinaten. Kein Ersatz "voll und ohne Ort" mehr.
void tonEntityOrt(const std::string& wer, double ms, float ort[3], std::string* klasse = nullptr) {
    ort[0] = ort[1] = ort[2] = 0.0F;
    if (wer.empty()) {
        return;   // sonst traefe der leere Name die erste Entity ohne targetname
    }
    if (const Actor* a = g_app->scene.find(wer); a != nullptr) {
        const ActorState st = a->at(ms);
        for (int i = 0; i < 3; ++i) { ort[i] = st.pos[i]; }
        return;
    }
    const std::string kw = tonKlein(wer);
    for (const MapEntity& e : g_app->map.entities) {
        const std::string* st = e.find("script_targetname");
        if (!((st != nullptr && tonKlein(*st) == kw) || tonKlein(e.targetname) == kw)) {
            continue;
        }
        if (klasse != nullptr) { *klasse = e.classname; }
        if (!e.origin.empty()) {
            std::istringstream is(e.origin);
            is >> ort[0] >> ort[1] >> ort[2];
        }
        return;
    }
}

}  // namespace

sound::Mischpegel tonPegel() {
    // Aus den Einstellungen (Einstellungen -> Lautstaerke der Vorschau),
    // Vorgaben wie die Cvars der Engine.
    sound::Mischpegel p;
    if (g_app != nullptr) {
        p.effekte = std::clamp(g_app->settings.tonEffekte, 0.0F, 1.0F);
        p.stimmen = std::clamp(g_app->settings.tonStimmen, 0.0F, 1.0F);
        p.musik = std::clamp(g_app->settings.tonMusik, 0.0F, 1.0F);
    }
    return p;
}

float tonMusikPegel() { return tonPegel().musik; }

bool tonImSnapshot(const float ort[3], const float mins[3], const float maxs[3]) {
    float ohr[3];
    float rechts[3];
    tonHoerer(ohr, rechts);
    return tonKastenSichtbar(ohr, ort, mins, maxs);
}

std::uint64_t tonStarte(const KlangWunsch& w) {
    if (w.datei.empty() || g_app == nullptr) {
        return 0;
    }
    float ohr[3];
    float rechts[3];
    tonHoerer(ohr, rechts);
    LaufenderKlang k;
    k.datei = w.datei;
    k.quelle = w.quelle;
    k.figur = w.figur;
    k.moverModell = w.moverModell;
    for (int i = 0; i < 3; ++i) { k.ort[i] = w.ort[i]; }
    k.kanal = w.kanal;
    k.amHoerer = w.amHoerer;
    k.schluessel = w.schluessel;
    tonFigurOrt(k, ohr, g_app->playMs);
    const sound::Raumlaut roh = tonRoh(k.ort, ohr, rechts, k.kanal, k.amHoerer);
    const sound::Raumlaut g = tonGemischt(roh, k.kanal);
    const bool stimmkanal = sound::istStimmkanal(k.kanal);
    k.id = g_ton.naechsteId++;
    // Channel-Stomp: ein neuer Satz derselben Figur schneidet den laufenden
    // ab (S_PickChannel mit S_CheckChannelStomp) - auch in der Buchfuehrung.
    if (k.schluessel != 0) {
        std::erase_if(g_ton.laufend, [&](const LaufenderKlang& x) {
            if (x.schluessel != k.schluessel) {
                return false;
            }
            tonStimmeStoppen(x.stimme);
            return true;
        });
    }
    const sound::Mixer::Daten d = tonDaten(k.datei);
    if (!d) {
        klangMitschreiben('E', k.quelle, k.datei, 0.0F, 0.0F, k.id, w.abMs);
        return 0;
    }
    const double laenge = tonLaengeMs(d);
    if (w.abMs >= laenge) {
        return 0;   // laengst vorbei
    }
    klangMitschreiben('E', k.quelle, k.datei, g.links, g.rechts, k.id, w.abMs);
    // Ein Nicht-Stimmkanal, der schon beim Start stumm ist, loescht
    // S_Respatialize im selben Bild (snd_dma.cpp:2721 ff. MD) - gar nicht
    // erst anlegen. Stimmen laufen still weiter, "so that tasks waiting for
    // sound completion keep proper timing".
    if (!stimmkanal && sound::engineStumm(roh, k.amHoerer)) {
        return 0;
    }
    k.links = g.links;
    k.rechts = g.rechts;
    k.endeMs = g_app->playMs + (laenge - w.abMs);
    if (Audio* geraet = tonGeraet(); geraet != nullptr) {
        sound::Mixer::Start st;
        st.links = g.links;
        st.rechts = g.rechts;
        st.schluessel = k.schluessel;
        st.abWert = static_cast<std::size_t>(w.abMs / 1000.0 * kTonRate) * kTonKanaele;
        st.stummErlaubt = stimmkanal;
        st.amHoerer = k.amHoerer;
        k.stimme = geraet->starte(d, st);
        if (k.stimme == 0) {
            return 0;   // alle 32 belegt, nichts verdraengbar
        }
    } else if (!g_app->stumm) {
        return 0;
    }
    g_ton.laufend.push_back(k);
    return k.id;
}

std::uint64_t spieleRaumKlang(const std::string& datei, const float ort[3], sound::Kanal kanal, double ms,
                              bool imSnapshotPruefen) {
    if (g_app == nullptr || datei.empty()) {
        return 0;
    }
    KlangWunsch w;
    w.datei = datei;
    w.kanal = kanal;
    for (int i = 0; i < 3; ++i) { w.ort[i] = ort[i]; }
    // Liegt der Zeitpunkt VOR dem laufenden Abspielfenster (Wiedereinstieg,
    // spaet gemeldet), beginnt der Klang an der passenden Stelle; im Fenster
    // selbst von vorn - so wie alle Ereignisse der Zeitleiste.
    w.abMs = (ms < g_app->audioUpTo) ? std::max(0.0, g_app->playMs - ms) : 0.0;
    if (imSnapshotPruefen && !tonImSnapshot(ort, kPunkt, kPunkt)) {
        klangMitschreiben('E', std::string{}, datei, 0.0F, 0.0F, 0, w.abMs);
        return 0;
    }
    return tonStarte(w);
}

void tonSkriptKlang(const std::string& wer, const std::string& datei, sound::Kanal k, std::uint64_t schluessel,
                    double ms, double abMs) {
    if (g_app == nullptr || datei.empty()) {
        return;
    }
    // PlayIcarusSound (Movie Duels Q3_Interface.cpp:8449 ff.):
    //   bBroadcast = CHAN_ANNOUNCER oder classname "target_scriptrunner"
    //   (oder CHAN_VOICE_GLOBAL); Stimmkanaele -> G_SoundOnEnt(ent, chan);
    //   sonst bBroadcast ? G_SoundBroadcast : G_Sound.
    KlangWunsch w;
    w.datei = datei;
    w.quelle = wer;
    w.abMs = abMs;
    if (wer.empty()) {
        // Kein Traeger bekannt (Skript ohne Karte, oberste Ebene): im Spiel
        // laeuft ein Skript immer auf einer Entity, hier gibt es keine. Dann
        // am Hoerer, mit den Pegeln der Engine - kein Ort ist erfunden.
        w.kanal = sound::istStimmkanal(k) ? k : sound::Kanal::Auto;
        w.schluessel = sound::istStimmkanal(k) ? schluessel : 0;
        w.amHoerer = true;
        (void)tonStarte(w);
        return;
    }
    std::string klasse;
    float ort[3];
    tonEntityOrt(wer, ms, ort, &klasse);
    const bool figur = g_app->scene.find(wer) != nullptr;
    for (int i = 0; i < 3; ++i) { w.ort[i] = ort[i]; }
    if (sound::istStimmkanal(k)) {
        // G_SoundOnEnt (g_utils.cpp:295 ff. MD): cgi_S_UpdateEntityPosition
        // und cgi_S_StartSound auf der Entity - DIREKT, nicht ueber den
        // Snapshot. Der Klang folgt der Figur; auf dem Spieler (der
        // Hoerer-Entity) voll mit SOUND_FMAXVOL. Ein Runner wird nicht voll:
        // er spricht an seinem Ort.
        w.kanal = k;
        w.schluessel = schluessel;
        w.amHoerer = istSpieler(wer);
        if (figur) {
            w.figur = wer;
        }
        (void)tonStarte(w);
        return;
    }
    if (k == sound::Kanal::Announcer || (!figur && tonKlein(klasse) == "target_scriptrunner")) {
        // G_SoundBroadcast (g_utils.cpp:1565 ff. MD): EV_GLOBAL_SOUND mit
        // SVF_BROADCAST, gespielt auf cg.snap->ps.clientNum mit CHAN_AUTO
        // (cg_event.cpp:1024 ff. MD) - also am Hoerer, 0.75 * s_volume.
        w.kanal = sound::Kanal::Auto;
        w.amHoerer = true;
        (void)tonStarte(w);
        return;
    }
    // G_Sound (g_utils.cpp:1529 ff. MD): G_TempEntity(ent->currentOrigin,
    // EV_GENERAL_SOUND) - der Ort mit SnapVector abgeschnitten (q_math.c:1219
    // ff. MD, 64 Bit: (int)), CHAN_AUTO auf der Temp-Entity, und nur, wenn sie
    // im Snapshot ist. Der eigene Kanal und sein Abschneiden gehen dabei
    // verloren - auch beim Spieler (vorher: voll und ohne Ort).
    for (float& x : w.ort) { x = std::trunc(x); }
    w.kanal = sound::Kanal::Auto;
    float ohr[3];
    float rechts[3];
    tonHoerer(ohr, rechts);
    if (!tonKastenSichtbar(ohr, w.ort, kPunkt, kPunkt)) {
        klangMitschreiben('E', wer, datei, 0.0F, 0.0F, 0, abMs);
        return;
    }
    (void)tonStarte(w);
}

void tonZuruecksetzen() {
    for (const LaufenderKlang& k : g_ton.laufend) {
        tonStimmeStoppen(k.stimme);
    }
    for (const auto& [datei, kanal] : g_ton.schleifen) {
        tonStimmeStoppen(kanal.stimme);
    }
    g_ton.laufend.clear();
    g_ton.schleifen.clear();
    g_ton.beitrag.clear();
    g_ton.klingen.clear();
    g_ton.klingenBereit = false;
}

// --- rc601 ton (Review): Einblick fuer Selbsttest und Protokoll --------------
std::size_t tonSchleifenZahl(const std::string& datei) {
    return datei.empty() ? g_ton.schleifen.size() : g_ton.schleifen.count(datei);
}

std::size_t tonEinmalZahl(const std::string& datei) {
    if (datei.empty()) {
        return g_ton.laufend.size();
    }
    return static_cast<std::size_t>(std::count_if(g_ton.laufend.begin(), g_ton.laufend.end(),
                                                  [&](const LaufenderKlang& k) { return k.datei == datei; }));
}

sound::Mixer::Daten tonMischerDaten(const std::string& datei) { return tonDaten(datei); }

// --- rc601 ton (Review): Schleifen nur, solange tonBild sie meldet -----------
//
// Gefunden im Review: "loop sounds (saber hums, looping target_speakers, mover
// MID loops) never stop when playback stops through a path that does not call
// tonZuruecksetzen - most commonly the timeline reaching its end". Die
// Schleifenstimmen des Mischers laufen endlos (nur stoppe haelt sie an),
// tonBild laeuft aber nur beim Abspielen - und mehrere Stellen setzen nur
// playing = false: das Ende der Zeitleiste (drawTimeline), der
// Reiterwechsel, die Spruenge zu Animations- und Kameraschluesseln, Lineal,
// Bloecke und Ende der Spurenansicht. Vor diesem Paket endeten die
// Schleifen nach ihren 120 s von selbst.
//
// Die Engine hat diese Luecke nicht: cgame meldet jede Schleife JEDES Bild
// neu - cgi_S_ClearLoopingSounds am Anfang jedes Bildes (cg_view.cpp:2871 MD,
// "Any looped sounds will be respecified as entities are added to the render
// list"), S_ClearLoopingSounds setzt numLoopSounds = 0 (snd_dma.cpp:1951 ff.
// MD), und S_AddLoopSounds (snd_dma.cpp:2082 ff. MD) mischt nur, was in
// diesem Bild gemeldet wurde. Dieselbe Regel hier, an EINER Stelle statt an
// jedem Weg, der anhaelt: eine Schleife lebt nur, solange tonBild sie im
// laufenden Bild gemeldet hat. Die Einmalklaenge bleiben - ein Satz, der ueber
// das Ende der Zeitleiste hinaus laeuft, spricht zu Ende wie bisher
// (tonZuruecksetzen wuerde ihn abschneiden).
void tonWache() {
    if (g_app == nullptr || g_ton.schleifen.empty()) {
        return;
    }
    if (g_app->playing && g_ton.bildNr == ImGui::GetFrameCount()) {
        return;   // in diesem Bild frisch gemeldet
    }
    tonSchleifenAnhalten();
}

void tonBild() {
    if (g_app == nullptr) {
        return;
    }
    g_ton.bildNr = ImGui::GetFrameCount();   // rc601 ton (Review), siehe tonWache
    float ohr[3];
    float rechts[3];
    tonHoerer(ohr, rechts);
    const double jetzt = g_app->playMs;

    // --- 1. Die Klingen (vor den Einmalklaengen: ein soundOn dieses Bildes
    //        wird unten gleich mitgeregelt) ---------------------------------
    if (!g_ton.klingenBereit || jetzt < g_ton.klingenMs || jetzt - g_ton.klingenMs > 1000.0) {
        // Erster Lauf oder ein Sprung: vom Stand VOR diesem Bild ausgehen
        // (audioUpTo ist das Ende des vorigen Fensters) - so zuendet eine
        // Klinge, die genau jetzt angeht, und eine laengst brennende nicht.
        const double vorher = std::min(jetzt, g_app->audioUpTo);
        tonKlingenAnfang(vorher);
        g_ton.klingenBereit = true;
        g_ton.klingenMs = vorher;
    }
    // Der Zustand jeder Figur EINMAL je Bild (Actor::at spielt alle Schritte
    // nach) - fuer die Klingen und das Summen unten.
    std::vector<ActorState> zustaende;
    zustaende.reserve(g_app->scene.actors.size());
    for (const Actor& a : g_app->scene.actors) {
        zustaende.push_back(a.at(jetzt));
    }
    tonKlingen(ohr, jetzt, g_ton.klingenMs, zustaende);
    g_ton.klingenMs = jetzt;

    // --- 2. Jeder laufende Einmalklang neu (S_Respatialize) ----------------
    for (auto it = g_ton.laufend.begin(); it != g_ton.laufend.end();) {
        LaufenderKlang& k = *it;
        const bool weg = jetzt >= k.endeMs ||
                         (!g_app->stumm && k.stimme != 0 && !g_app->audio.laeuft(k.stimme));
        if (weg) {
            it = g_ton.laufend.erase(it);
            continue;
        }
        tonFigurOrt(k, ohr, jetzt);
        const sound::Raumlaut roh = tonRoh(k.ort, ohr, rechts, k.kanal, k.amHoerer);
        if (!sound::istStimmkanal(k.kanal) && sound::engineStumm(roh, k.amHoerer)) {
            tonStimmeStoppen(k.stimme);
            klangMitschreiben('X', k.quelle, k.datei, 0.0F, 0.0F, k.id);
            it = g_ton.laufend.erase(it);
            continue;
        }
        const sound::Raumlaut g = tonGemischt(roh, k.kanal);
        if (!tonNaheGleich(k.links, g.links) || !tonNaheGleich(k.rechts, g.rechts)) {
            k.links = g.links;
            k.rechts = g.rechts;
            if (k.stimme != 0 && !g_app->stumm) {
                (void)g_app->audio.setzePegel(k.stimme, g.links, g.rechts);
            }
            klangMitschreiben('P', k.quelle, k.datei, g.links, g.rechts, k.id);
        }
        ++it;
    }

    // --- 3. Die Schleifen, jedes Bild frisch ---------------------------------
    std::vector<SchleifenQuelle> quellen;
    // Das Summen: CG_StopWeaponSounds (cg_players.cpp:7268 ff. MD), aus
    // CG_Player - also nur gezeichnet (nicht EF_NODRAW, nicht der Spieler im
    // Kameramodus) und im Snapshot; am Ursprung der Figur (lerpOrigin) auf
    // CHAN_AUTO (Vorgabe von cgi_S_AddLoopingSound, cg_local.h).
    for (std::size_t ai = 0; ai < g_app->scene.actors.size() && ai < zustaende.size(); ++ai) {
        const Actor& a = g_app->scene.actors[ai];
        const ActorState& st = zustaende[ai];
        if (!st.visible || st.removed || st.saberLoop.empty()) {
            continue;
        }
        if (!istSpieler(a.name) && !tonKastenSichtbar(ohr, st.pos, kFigurMin, kFigurMax)) {
            continue;
        }
        SchleifenQuelle q;
        q.datei = st.saberLoop;
        q.quelle = a.name;
        for (int i = 0; i < 3; ++i) { q.ort[i] = st.pos[i]; }
        quellen.push_back(std::move(q));
    }
    // rc604: misc_trip_mine - s.loopSound hum_loop.wav ab dem Scharfmachen
    // (laserTrapThink, wp_trip_mine.cpp:140-146 OpenJK), gespielt von
    // CG_EntityEffects fuer die Falle, solange sie im Snapshot ist (PVS).
    for (const KartenKlang& kk : g_app->fallenKlaenge) {
        if (!kk.schleife || kk.datei.empty() || kk.ms > jetzt || (kk.bisMs >= 0.0 && kk.bisMs <= jetzt)) {
            continue;
        }
        if (!tonKastenSichtbar(ohr, kk.ort, kPunkt, kPunkt)) {
            continue;
        }
        SchleifenQuelle q;
        q.datei = kk.datei;
        q.quelle = "misc_trip_mine #" + std::to_string(kk.entity);
        for (int i = 0; i < 3; ++i) { q.ort[i] = kk.ort[i]; }
        quellen.push_back(std::move(q));
    }
    // target_speaker mit spawnflags 1/2: s.loopSound am Lautsprecher bzw. am
    // Ausloeser (Use_Target_Speaker, g_target.cpp:208 ff. MD) - gespielt von
    // CG_EntityEffects (cg_ents.cpp:198 ff. MD) fuer jede Entity im Snapshot,
    // die nicht EF_NODRAW ist.
    for (const KartenKlang& kk : g_app->moverSim.lautsprecher()) {
        if (!kk.schleife || kk.datei.empty() || kk.ms > jetzt || (kk.bisMs >= 0.0 && kk.bisMs <= jetzt)) {
            continue;
        }
        SchleifenQuelle q;
        q.datei = kk.datei;
        q.quelle = "target_speaker #" + std::to_string(kk.entity);
        for (int i = 0; i < 3; ++i) { q.ort[i] = kk.ort[i]; }
        bool sichtbar = true;
        if (kk.amAusloeser) {
            if (const Actor* a = g_app->scene.find(kk.ausloeser); a != nullptr) {
                const ActorState st = a->at(jetzt);
                for (int i = 0; i < 3; ++i) { q.ort[i] = st.pos[i]; }
                sichtbar = st.visible && !st.removed &&
                           (istSpieler(kk.ausloeser) || tonKastenSichtbar(ohr, st.pos, kFigurMin, kFigurMax));
            }
        } else {
            // spawnflags 4 (global) gibt SVF_BROADCAST (SP_target_speaker,
            // g_target.cpp:301 f. MD): dann immer im Snapshot.
            int flags = 0;
            if (kk.entity < g_app->map.entities.size()) {
                if (const std::string* sf = g_app->map.entities[kk.entity].find("spawnflags")) {
                    flags = std::atoi(sf->c_str());
                }
            }
            sichtbar = (flags & 4) != 0 || tonKastenSichtbar(ohr, kk.ort, kPunkt, kPunkt);
        }
        if (sichtbar) {
            quellen.push_back(std::move(q));
        }
    }
    // Die Fahrschleife der Mover (G_PlayDoorLoopSound, g_mover.cpp:66 ff.
    // MD: s.loopSound = BMS_MID), an der Mitte des Modells.
    for (const MoverKlang& mk : g_app->moverKlaenge) {
        if (mk.stufe != 1 || mk.ms > jetzt || mk.bisMs <= jetzt) {
            continue;
        }
        const auto set = g_app->bmodelSets.find(tonKlein(mk.soundSet));
        if (set == g_app->bmodelSets.end() || set->second.size() < 2U || set->second[1].empty()) {
            continue;   // AS_GetBModelSound: -1, kein Klang
        }
        if (!tonMoverSichtbar(ohr, mk.modell, jetzt)) {
            continue;
        }
        SchleifenQuelle q;
        q.datei = set->second[1];
        q.quelle = "mover *" + std::to_string(mk.modell);
        g_app->moverSim.klangOrt(mk.modell, jetzt, q.ort);
        quellen.push_back(std::move(q));
    }
    // rc604 fix604b (A2): ROFF-Notiz "loop sfx <sound/...>" setzt s.loopSound
    // der Entity, "loop sfx kill" loescht ihn (code/game/g_roff.cpp:380-430
    // OpenJK) - gespielt von CG_EntityEffects fuer die Entity im Snapshot.
    // Je Entity gilt die letzte Notiz bis jetzt (nur OpenJK erzeugt sie).
    {
        std::map<std::string, const RoffEreignis*> letzte;
        for (const RoffEreignis& re : g_app->scene.roffEreignisse) {
            if (re.ms > jetzt) { break; }
            if (re.befehl.art == RoffNotizBefehl::Art::Schleife && re.befehl.schleife == "sfx") { letzte[re.figur] = &re; }
        }
        for (const auto& [name, re] : letzte) {
            if (re->befehl.schleifeAus || re->befehl.datei.empty()) { continue; }
            SchleifenQuelle q;
            q.datei = re->befehl.datei;
            q.quelle = name;
            for (int i = 0; i < 3; ++i) { q.ort[i] = re->ort[i]; }
            bool sichtbar = tonKastenSichtbar(ohr, re->ort, kPunkt, kPunkt);
            if (const Actor* a = g_app->scene.find(name); a != nullptr) {
                const ActorState st = a->at(jetzt);
                for (int i = 0; i < 3; ++i) { q.ort[i] = st.pos[i]; }
                sichtbar = st.visible && !st.removed &&
                           (istSpieler(name) || tonKastenSichtbar(ohr, st.pos, kFigurMin, kFigurMax));
            }
            if (sichtbar) { quellen.push_back(std::move(q)); }
        }
    }
    tonSchleifen(ohr, rechts, quellen);
}

void tonEreignisse(double vonMs, double bisMs) {
    if (g_app == nullptr) {
        return;
    }
    float ohr[3];
    float rechts[3];
    tonHoerer(ohr, rechts);

    // --- target_speaker, Einmalklang (Use_Target_Speaker, g_target.cpp:194
    //     ff. MD). Vorher spielte jedes use den "noise" der ERSTEN Entity
    //     mit dem Namen, und ohne origin voll und ohne Ort. -----------------
    for (const KartenKlang& kk : g_app->moverSim.lautsprecher()) {
        if (kk.ms > bisMs) {
            break;   // nach Zeit geordnet
        }
        if (kk.ms <= vonMs || kk.schleife || kk.datei.empty()) {
            continue;
        }
        KlangWunsch w;
        w.datei = kk.datei;
        w.quelle = "target_speaker #" + std::to_string(kk.entity);
        w.kanal = sound::Kanal::Auto;
        for (int i = 0; i < 3; ++i) { w.ort[i] = kk.ort[i]; }
        if (kk.global) {
            // EV_GLOBAL_SOUND: cgi_S_StartSound(NULL, cg.snap->ps.clientNum,
            // CHAN_AUTO, ...) - "play from the player's head so it never
            // diminishes" (cg_event.cpp:1024 ff. MD).
            w.amHoerer = true;
        } else if (kk.amAusloeser) {
            // G_AddEvent(activator, EV_GENERAL_SOUND): auf dem Ausloeser.
            if (istSpieler(kk.ausloeser)) {
                w.amHoerer = true;
            } else if (const Actor* a = g_app->scene.find(kk.ausloeser); a != nullptr) {
                const ActorState st = a->at(kk.ms);
                if (!tonKastenSichtbar(ohr, st.pos, kFigurMin, kFigurMax)) {
                    klangMitschreiben('E', w.quelle, w.datei, 0.0F, 0.0F, 0);
                    continue;
                }
                w.figur = a->name;
                for (int i = 0; i < 3; ++i) { w.ort[i] = st.pos[i]; }
            }
        } else if (!tonKastenSichtbar(ohr, kk.ort, kPunkt, kPunkt)) {
            // EV_GENERAL_SOUND am Lautsprecher: nur, wenn er im Snapshot ist.
            klangMitschreiben('E', w.quelle, w.datei, 0.0F, 0.0F, 0);
            continue;
        }
        (void)tonStarte(w);
    }

    // --- rc604: misc_trip_mine wird scharf - G_Sound(warning.wav), eine
    //     Temp-Entity EV_GENERAL_SOUND an der Falle (laserTrapThink,
    //     wp_trip_mine.cpp:143 OpenJK), nur im Snapshot zu hoeren. ----------
    for (const KartenKlang& kk : g_app->fallenKlaenge) {
        if (kk.schleife || kk.datei.empty() || kk.ms <= vonMs || kk.ms > bisMs) {
            continue;
        }
        KlangWunsch w;
        w.datei = kk.datei;
        w.quelle = "misc_trip_mine #" + std::to_string(kk.entity);
        w.kanal = sound::Kanal::Auto;
        for (int i = 0; i < 3; ++i) { w.ort[i] = kk.ort[i]; }
        if (!tonKastenSichtbar(ohr, kk.ort, kPunkt, kPunkt)) {
            klangMitschreiben('E', w.quelle, w.datei, 0.0F, 0.0F, 0);
            continue;
        }
        (void)tonStarte(w);
    }

    // --- Tueren und Co.: START und END (G_PlayDoorSound, g_mover.cpp:88 ff.
    //     MD) - ein EV_BMODEL_SOUND auf dem Mover, CHAN_AUTO, der Klang folgt
    //     der Mitte des Modells. Die Schleife (BMS_MID) steht in tonBild. ----
    for (const MoverKlang& mk : g_app->moverKlaenge) {
        if (mk.ms > bisMs) {
            break;
        }
        if (mk.ms <= vonMs || mk.stufe == 1) {
            continue;
        }
        const auto set = g_app->bmodelSets.find(tonKlein(mk.soundSet));
        if (set == g_app->bmodelSets.end() || static_cast<std::size_t>(mk.stufe) >= set->second.size() ||
            set->second[static_cast<std::size_t>(mk.stufe)].empty()) {
            continue;   // AS_GetBModelSound: -1, kein Klang
        }
        KlangWunsch w;
        w.datei = set->second[static_cast<std::size_t>(mk.stufe)];
        w.quelle = "mover *" + std::to_string(mk.modell);
        if (!tonMoverSichtbar(ohr, mk.modell, mk.ms)) {
            klangMitschreiben('E', w.quelle, w.datei, 0.0F, 0.0F, 0);
            continue;
        }
        w.moverModell = mk.modell;
        g_app->moverSim.klangOrt(mk.modell, mk.ms, w.ort);
        (void)tonStarte(w);
    }

    // --- ROFF-Notizen "sound" (G_RoffNotetrackCallback): Movie Duels spielt
    //     sie IMMER fest am Ort (OpenJK siehe unten, rc601 integ), cgi_S_StartSound(cent->s.pos.trBase, ...,
    //     CHAN_BODY, ...) (g_roff.cpp:262 ff. MD; OpenJK nimmt fuer
    //     Nicht-Mover G_SoundOnEnt, g_roff.cpp:298 ff.). Direkt an den
    //     Klang, NICHT ueber den Snapshot - also ohne PVS. ------------------
    for (const RoffEreignis& re : g_app->scene.roffEreignisse) {
        if (re.ms > bisMs) {
            break;
        }
        if (re.ms <= vonMs || re.befehl.art != RoffNotizBefehl::Art::Klang || re.befehl.datei.empty()) {
            continue;
        }
        KlangWunsch w;
        w.datei = re.befehl.datei;
        w.quelle = re.figur;
        w.kanal = sound::Kanal::Body;
        for (int i = 0; i < 3; ++i) { w.ort[i] = re.ort[i]; }
        // rc601 integ: unter OpenJK geht der Klang einer Figur (kein
        // ET_MOVER) mit ihr - G_SoundOnEnt(ent, CHAN_BODY, argument)
        // (g_roff.cpp:303-306 OpenJK), wie die Stimmkanaele oben: Figur
        // folgen, Channel-Stomp auf (Entity, CHAN_BODY), auf dem Spieler voll.
        // Brush-Mover (ET_MOVER) bleiben in beiden Regelwerken fest am Ort.
        // Andere Nicht-Mover ohne Figur (misc_model u. a.) kennt der Ton nicht
        // als bewegte Quelle - sie bleiben am Ort des Ereignisses.
        const bool figur = g_app->scene.find(re.figur) != nullptr;
        // rc604 fix604 (JK2 Stufe 3): Jedi Outcast spielt ihn wie Movie Duels
        // fest am Ort, cgi_S_StartSound(cent->s.pos.trBase, ...) (codeJK2/game/
        // g_roff.cpp:242-246) - und nur die erste Notiz eines Bilds (:510-513).
        if (figur && roffKlangFolgtEntity(regelwerkMovieDuels() || regelwerkJediOutcast(), false)) {
            w.figur = re.figur;
            w.schluessel = tonKanalSchluessel(re.figur, sound::Kanal::Body);
            w.amHoerer = istSpieler(re.figur);
        }
        (void)tonStarte(w);
    }

    // --- Zerbrechen. Die Materialklaenge und der eigene "noise" kommen aus
    //     CG_Chunks, das der Spielcode DIREKT ruft (g_breakable.cpp:198, 277
    //     MD): cgi_S_StartSound(NULL, owner, CHAN_BODY, ...) - ohne PVS.
    //     cargoexplode und TIEexplode sind dagegen G_Sound bzw. eine
    //     Temp-Entity mit EV_GENERAL_SOUND (g_breakable.cpp:187 f., 647 ff.
    //     MD): nur im Snapshot, auf CHAN_AUTO. Vorher hoerte das Zerbrechen
    //     von der freien Kamera aus und ohne links/rechts (distanceVolume). --
    for (const KartenKlang& bk : g_app->moverSim.bruchKlaenge()) {
        if (bk.ms > bisMs) {
            break;
        }
        if (bk.ms <= vonMs || bk.datei.empty()) {
            continue;
        }
        KlangWunsch w;
        w.datei = bk.datei;
        w.quelle = "bruch #" + std::to_string(bk.entity);
        for (int i = 0; i < 3; ++i) { w.ort[i] = bk.ort[i]; }
        if (bk.tempEntity) {
            w.kanal = sound::Kanal::Auto;
            if (!tonKastenSichtbar(ohr, bk.ort, kPunkt, kPunkt)) {
                klangMitschreiben('E', w.quelle, w.datei, 0.0F, 0.0F, 0);
                continue;
            }
        } else {
            w.kanal = sound::Kanal::Body;
        }
        (void)tonStarte(w);
    }

    // --- Effekte (fx_runner): fx_runner_think schickt EV_PLAY_EFFECT auf
    //     dem Laeufer (g_fx.cpp:66 MD) - also nur im Snapshot. Die
    //     Klangprimitive spielt FxScheduler.cpp:1954 ff. (MD) fest am Ort, mit
    //     FX_SND_LESS_ATTENUATION auf CHAN_LESS_ATTEN, sonst CHAN_AUTO. Die
    //     Wiederholungen nach derselben Regel wie die Zeichnung (klangStarts,
    //     auch endMs: ein abgeschalteter START_OFF-Laeufer klingt nicht
    //     weiter). ---------------------------------------------------------
    for (const EffectInstance& fx : g_app->effectRunners) {
        if (fx.effect == nullptr) {
            continue;
        }
        const std::vector<double> starts = klangStarts(fx, vonMs, bisMs);
        if (starts.empty()) {
            continue;
        }
        bool hatKlang = false;
        for (const efx::Primitive& pr : fx.effect->primitives) {
            if (pr.type == efx::PrimitiveType::Sound && !pr.sounds.empty()) {
                hatKlang = true;
            }
        }
        if (!hatKlang) {
            continue;
        }
        float ort[3] = {fx.origin[0], fx.origin[1], fx.origin[2]};
        if (fx.anker) {
            // An einer Entity: dort, wo sie beim Start steht (die Engine
            // spielt ihn auf der Entity, FxScheduler.cpp:1945 ff. MD).
            float fwd[3];
            (void)fx.anker(starts.front(), ort, fwd);
        } else if (!tonKastenSichtbar(ohr, ort, kPunkt, kPunkt)) {
            continue;
        }
        for (const double b : starts) {
            const auto n = (fx.loops && fx.intervalMs > 1.0F)
                               ? static_cast<unsigned>(std::lround((b - fx.startMs) / fx.intervalMs))
                               : 0U;
            for (const efx::Primitive& pr : fx.effect->primitives) {
                if (pr.type != efx::PrimitiveType::Sound || pr.sounds.empty()) {
                    continue;
                }
                // Die Engine waehlt zufaellig; hier ueber Saat und
                // Wiederholungsnummer, damit dieselbe Stelle dasselbe spielt.
                const std::size_t wahl = (fx.seed + n) % pr.sounds.size();
                KlangWunsch w;
                w.datei = pr.sounds[wahl];
                w.quelle = "fx_runner";
                w.kanal = ((pr.spawnFlags & efx::kSpawnSoundLessAttenuation) != 0U) ? sound::Kanal::LessAtten
                                                                                     : sound::Kanal::Auto;
                for (int i = 0; i < 3; ++i) { w.ort[i] = ort[i]; }
                (void)tonStarte(w);
                // Ins Protokoll, damit sich nachvollziehen laesst, WARUM ein
                // Klang laut oder leise war (wie bisher, jetzt vom echten
                // Hoerer aus).
                const sound::Raumlaut r = sound::raumlaut(ort, ohr, rechts, w.kanal);
                const float laut = std::max(r.links, r.rechts);
                if (laut > 0.001F) {
                    const float dx = ort[0] - ohr[0];
                    const float dy = ort[1] - ohr[1];
                    const float dz = ort[2] - ohr[2];
                    char kz[220];
                    std::snprintf(kz, sizeof(kz), "Effektklang \"%s\": %.0f Einheiten entfernt, Lautstaerke %.0f %%",
                                  w.datei.c_str(), static_cast<double>(std::sqrt(dx * dx + dy * dy + dz * dz)),
                                  static_cast<double>(laut) * 100.0);
                    diag::detail(kz);
                }
            }
        }
    }
}

void tonWiedereinstieg() {
    if (g_app == nullptr) {
        return;
    }
    // Alles Alte weg, dann jeden Satz, der an der Zeitmarke noch laeuft, an
    // seiner Stelle wieder einsetzen - ueber dieselben Regeln wie beim
    // Ausloesen (Ort, Kanal, PVS, Pegel). Vorher spielte nur der letzte, voll
    // und ohne Ort. In Startreihenfolge, damit der Channel-Stomp gilt.
    tonZuruecksetzen();
    struct Satz {
        const TimelineTrack* spur;
        const TimelineEvent* e;
    };
    std::vector<Satz> saetze;
    for (const TimelineTrack& track : g_app->timeline.tracks) {
        for (const TimelineEvent& e : track.events) {
            // Nur, was die Fenster schon hinter sich haben (bis audioUpTo):
            // alles danach loest das naechste Fenster ohnehin aus - sonst
            // klaenge ein Satz, der genau an der Zeitmarke beginnt (Abspielen
            // ab 0 ms), zweimal.
            if (e.kind != TimelineEvent::Kind::Sound || e.sound.empty() || e.startMs > g_app->audioUpTo) {
                continue;
            }
            const double ende = e.startMs + soundLengthMs(e.sound);
            if (ende > g_app->playMs) {
                saetze.push_back(Satz{&track, &e});
            }
        }
    }
    std::stable_sort(saetze.begin(), saetze.end(),
                     [](const Satz& a, const Satz& b) { return a.e->startMs < b.e->startMs; });
    for (const Satz& s : saetze) {
        spieleSkriptKlang(s.spur->entity, s.e->sound, s.e->kanal, s.e->startMs, g_app->playMs - s.e->startMs);
    }
}

void tonVorladen() {
    if (g_app == nullptr) {
        return;
    }
    // Was die Szene beim Abspielen braucht, schon vor dem Start der Uhr
    // entpacken - sonst stockt das Bild beim ersten Summen (klaengeVorladen
    // kannte nur die sound-Befehle und use).
    std::set<std::string> dateien;
    for (const Actor& a : g_app->scene.actors) {
        for (const SaberArt& s : a.saberArten) {
            dateien.insert(s.soundOn);
            dateien.insert(s.soundLoop);
        }
    }
    for (const KartenKlang& kk : g_app->moverSim.lautsprecher()) { dateien.insert(kk.datei); }
    for (const KartenKlang& kk : g_app->fallenKlaenge) { dateien.insert(kk.datei); }   // rc604
    for (const KartenKlang& bk : g_app->moverSim.bruchKlaenge()) { dateien.insert(bk.datei); }
    for (const MoverKlang& mk : g_app->moverKlaenge) {
        const auto set = g_app->bmodelSets.find(tonKlein(mk.soundSet));
        if (set != g_app->bmodelSets.end() && static_cast<std::size_t>(mk.stufe) < set->second.size()) {
            dateien.insert(set->second[static_cast<std::size_t>(mk.stufe)]);
        }
    }
    for (const std::string& d : dateien) {
        if (!d.empty()) {
            (void)klangFuerWiedergabe(d);
        }
    }
}

}  // namespace bhed::gui

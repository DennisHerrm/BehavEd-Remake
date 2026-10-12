// app_kamera.cpp - Kameraeinstellungen bearbeiten wie in Unreal und Blender
//
// Gewuenscht (11.10.2026): "passen die Kamera sachen eigentlich? ist die
// Kamera editieren und erstellen intuitiv und modern? auch mit dem Gizmo passt
// da alles?" - und auf die Antwort hin "mach das bitte. so das es moderner
// wirkt".
//
// Bis rc587 war die Kamera anders zu bedienen als die Entities: das Gizmo kam
// erst ueber das Rechtsklickmenue, eine Verschiebung galt erst nach einem
// kleinen "Apply" unten im Band, beim Ueberfahren geschah nichts, und neue
// Einstellungen gab es nur ueber einen Textknopf. Jetzt:
//
//   - ausgewaehlt = Gizmo da (Verschieben), Loslassen schreibt ins Skript,
//     ein Strg+Z je Zug - wie bei den Entities seit rc586
//   - Ueberfahren hebt die Kamera hervor und nennt die Einstellung
//   - eine Leiste in der Ansicht (wie Unreals Viewport-Werkzeuge):
//     Verschieben/Drehen, Welt/Lokal, vorige/naechste Einstellung,
//     "Auf Ansicht setzen", "Steuern" (Pilot), Durchblick, neu, loeschen
//   - der Inspektor links zeigt die Einstellungen der Kamera mit Ort, Winkeln
//     und Dauer zum Eintippen
//   - Einstellungen auf einem ref_tag (tag( "cam1", ORIGIN )) verschieben den
//     ref_tag selbst - so arbeitet das Spiel damit
#include "app_internal.h"
#include "gpumap.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace bhed::gui {
namespace {

std::string klein(std::string s) {
    for (char& c : s) { c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }
    return s;
}

// Wie ortText im Missionsbau: ganze Einheiten, so stehen Orte in den .ent.
std::string ganzText(const float p[3]) {
    char b[96];
    std::snprintf(b, sizeof(b), "%d %d %d", static_cast<int>(std::lround(p[0])), static_cast<int>(std::lround(p[1])),
                  static_cast<int>(std::lround(p[2])));
    return b;
}

std::string vek(const float v[3]) {
    char b[96];
    std::snprintf(b, sizeof(b), "%.3f %.3f %.3f", static_cast<double>(v[0]), static_cast<double>(v[1]),
                  static_cast<double>(v[2]));
    return b;
}

// Die Entity, die als Marke dient (ref_tag oder waypoint_navgoal), -1 keine.
int markenEntity(const std::string& name) {
    const std::string n = klein(name);
    for (std::size_t i = 0; i < g_app->map.entities.size(); ++i) {
        const MapEntity& e = g_app->map.entities[i];
        if (klein(e.targetname) == n && (e.classname == "ref_tag" || e.classname.rfind("waypoint_navgoal", 0) == 0)) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

// Je Bild einmal: Ueberlagerung, Vorschau und Inspektor fragten jede fuer
// sich - und jede Frage geht ueber alle Kamerabefehle samt Markensuche in den
// Entities (Code-Pruefung rc592). Eine Aenderung im selben Bild (Skript- oder
// Entity-Stand) rechnet sofort neu.
std::vector<KameraSchluessel> schluessel() {
    struct Merk {
        int bild = -1;
        std::uint64_t skript = 0;
        std::uint64_t ents = 0;
        int reiter = -2;
        const Script* wo = nullptr;
        std::vector<KameraSchluessel> ks;
    };
    static Merk m;
    const int bild = ImGui::GetCurrentContext() != nullptr ? ImGui::GetFrameCount() : -1;
    const std::uint64_t skript = g_app->doc.stand();
    const std::uint64_t ents = static_cast<std::uint64_t>(g_app->entStand);
    const Script* wo = &g_app->doc.script();
    if (bild < 0 || bild != m.bild || skript != m.skript || ents != m.ents || g_app->activeTab != m.reiter ||
        wo != m.wo) {
        m.ks = kameraSchluessel(g_app->doc.script());
        m.bild = bild;
        m.skript = skript;
        m.ents = ents;
        m.reiter = g_app->activeTab;
        m.wo = wo;
    }
    return m.ks;
}

bool gueltig(const std::vector<KameraSchluessel>& ks, int k) {
    return k >= 0 && static_cast<std::size_t>(k) < ks.size();
}

// Wann eine Einstellung beginnt (aus der Kamerabahn), -1 unbekannt.
double startZeit(const Path& p) {
    for (const CamSegment& c : g_app->camTrack.segments) {
        if (c.kind == CamSegment::Kind::Move && c.fremd.empty() && c.path == p) { return c.startMs; }
    }
    return -1.0;
}

// Die Dauer im MOVE (camera ( MOVE, <ort>, dauer )).
double moveDauer(const Path& p) {
    const Node* n = nodeAt(g_app->doc.script(), p);
    if (n == nullptr || n->args.size() < 3) { return 0.0; }
    return std::atof(n->args[2].text.c_str());
}

void vorschauVerwerfen() {
    for (int k = 0; k < 3; ++k) {
        g_app->gizmoOffset[k] = 0.0F;
        g_app->gizmoAngles[k] = 0.0F;
    }
}

void nachSkriptAenderung() {
    g_app->kameraSchriebSkript = true;
    rebuildTree();
    g_app->camTrackValid = false;
    g_app->mapDirty = true;
}

// Einen ref_tag verschieben / drehen (Entity-Rueckgaengig).
bool markeOrt(const std::string& name, const float ort[3]) {
    const int i = markenEntity(name);
    if (i < 0) { return false; }
    return entityKeySetzen(i, "origin", ganzText(ort));
}

bool markeWinkel(const std::string& name, const float ang[3]) {
    const int i = markenEntity(name);
    if (i < 0) { return false; }
    const MapEntity& e = g_app->map.entities[static_cast<std::size_t>(i)];
    // Ein ref_tag mit target schaut IMMER zu seinem Ziel (g_ref.cpp ref_link) -
    // ein eigener Winkel wuerde im Spiel ueberschrieben.
    if (const std::string* t = e.find("target"); t != nullptr && !t->empty()) {
        char z[300];
        std::snprintf(z, sizeof(z), tr(Str::CamTagAimed), name.c_str(), t->c_str());
        meldung(z, "", true);
        return false;
    }
    char b[96];
    std::snprintf(b, sizeof(b), "%.1f %.1f %.1f", static_cast<double>(ang[0]), static_cast<double>(ang[1]),
                  static_cast<double>(ang[2]));
    return entityKeySetzen(i, "angles", b);
}

}  // namespace

// --- Auto-Key: einen Schluessel mitten in einer Fahrt (rc597) ---------------
//
// shank: "wenn ich die Timeline bewege und die Kamera zwischen 2 Punkten ist
// und ich drehe die Kamera oder bewege sie, wird an dem Punkt ein neuer
// erstellt und nicht der Anfang geaendert - so als haette ich da animiert".
// Wie Auto-Key in Unreal. Nur auf oberster Ebene, dort haben die Zeilen Zeiten.
namespace {
const CamSegment* zwischenFahrt(double ms) {
    if (!g_app->camTrackValid) { return nullptr; }
    for (const CamSegment& c : g_app->camTrack.segments) {
        if (c.kind != CamSegment::Kind::Move || !c.fremd.empty() || c.path.size() != 1U) { continue; }
        if (ms > c.startMs + 50.0 && ms < c.endMs - 25.0) { return &c; }
    }
    return nullptr;
}

std::string zahl3(double v) {
    char t[48];
    std::snprintf(t, sizeof(t), "%.3f", v);
    return t;
}

std::string vektor3(const float* v) {
    char t[96];
    std::snprintf(t, sizeof(t), "%.3f %.3f %.3f", static_cast<double>(v[0]), static_cast<double>(v[1]),
                  static_cast<double>(v[2]));
    return t;
}
}  // namespace

int kameraZwischenSchluesselNr(double ms) {
    const CamSegment* c = zwischenFahrt(ms);
    if (c == nullptr) { return -1; }
    const auto ks = kameraSchluessel(g_app->doc.script());
    for (std::size_t i = 0; i < ks.size(); ++i) {
        if (ks[i].path == c->path) { return static_cast<int>(i); }
    }
    return -1;
}

bool kameraSchluesselEinfuegen(double ms) {
    const CamSegment* c = zwischenFahrt(ms);
    if (c == nullptr) { return false; }
    const double t0 = c->startMs;
    const double a = ms - t0;           // Fahrt zum neuen Schluessel
    const double rest = c->endMs - ms;  // vom neuen zum alten Ziel
    Script s = g_app->doc.script();
    std::vector<Node>& top = s.nodes;
    const std::size_t i = c->path[0];
    if (i >= top.size() || top[i].args.size() < 3) { return false; }
    // Der PAN, der mit diesem MOVE beginnt (vor dem naechsten wait/MOVE).
    std::size_t panNr = top.size();
    std::size_t waitNr = top.size();
    for (std::size_t j = i + 1; j < top.size(); ++j) {
        const Node& n = top[j];
        if (n.name == "waitsignal" || n.name == "dowait") { break; }
        if (n.name == "wait") {
            if (!n.args.empty() && n.args[0].kind == Arg::Kind::Number) { waitNr = j; }
            break;
        }
        if (n.name == "camera" && !n.args.empty()) {
            if (n.args[0].text == "MOVE") { break; }
            if (n.args[0].text == "PAN" && panNr == top.size()) { panNr = j; }
        }
    }
    double panDauer = 0.0;
    bool panTeilen = false;
    if (panNr < top.size()) {
        const Node& p = top[panNr];
        panDauer = std::atof(p.args.back().text.c_str());
        panTeilen = p.args.size() >= 4 && p.args[1].kind == Arg::Kind::Vector && panDauer > a + 1.0;
    }
    // Das wait danach muss die Fahrt zum neuen Schluessel aufnehmen koennen -
    // sonst kaeme der Rest hinter den naechsten Befehl.
    double waitWert = 0.0;
    if (waitNr < top.size()) {
        waitWert = std::atof(top[waitNr].args[0].text.c_str());
        if (waitWert + 1e-3 < a) { return false; }
    }
    // Das Spiel beendet ein wait ein Bild (50 ms) spaeter - der Ablauf, aus
    // dem die Kamerabahn entsteht, rechnet genauso (siehe auch das Ziehen an
    // einer wait-Kante in der Zeitleiste, das 50 ms abzieht).
    Node probe;
    probe.kind = Node::Kind::Command;
    probe.name = "wait";
    Arg pa;
    pa.kind = Arg::Kind::Number;
    pa.text = "0.000";
    probe.args = {pa};
    constexpr double zuschlag = 50.0;
    if (a - zuschlag < 1.0) { return false; }

    const CameraState cs = camStateAt(ms);
    Node moveK = top[i];
    moveK.kennung = 0;
    moveK.args[1].text = vektor3(cs.pos);
    moveK.args[2].text = zahl3(a);
    Node moveB = top[i];
    moveB.args[2].text = zahl3(rest);
    Node panK;
    Node panB;
    if (panTeilen) {
        panB = top[panNr];
        panK = panB;
        panK.kennung = 0;
        panK.args[1].text = vektor3(cs.angles);
        panK.args.back().text = zahl3(a);
        panB.args.back().text = zahl3(panDauer - a);
    }
    Node warte = probe;
    warte.args[0].text = zahl3(a - zuschlag);
    std::vector<Node> neu;
    neu.reserve(top.size() + 3U);
    for (std::size_t k = 0; k < i; ++k) { neu.push_back(top[k]); }
    neu.push_back(moveK);
    if (panTeilen) { neu.push_back(panK); }
    const std::size_t bis = waitNr < top.size() ? waitNr : top.size();
    for (std::size_t k = i + 1; k < bis; ++k) {
        if (panTeilen && k == panNr) { continue; }
        neu.push_back(top[k]);
    }
    neu.push_back(warte);
    neu.push_back(moveB);
    if (panTeilen) { neu.push_back(panB); }
    if (waitNr < top.size()) {
        Node w = top[waitNr];
        w.args[0].text = zahl3(waitWert - a);
        neu.push_back(w);
        for (std::size_t k = waitNr + 1; k < top.size(); ++k) { neu.push_back(top[k]); }
    }
    top = std::move(neu);
    g_app->doc.ersetzeSkript(std::move(s), tr(Str::CamAutoKey));
    nachSkriptAenderung();
    const auto ks = kameraSchluessel(g_app->doc.script());
    for (std::size_t k = 0; k < ks.size(); ++k) {
        if (ks[k].path == Path{i}) { g_app->selectedKey = static_cast<int>(k); }
    }
    vorschauVerwerfen();
    return true;
}

// --- Schreiben -------------------------------------------------------------------

bool kameraVorschauSchreiben() {
    const bool verschoben =
        g_app->gizmoOffset[0] != 0.0F || g_app->gizmoOffset[1] != 0.0F || g_app->gizmoOffset[2] != 0.0F;
    const bool gedreht =
        g_app->gizmoAngles[0] != 0.0F || g_app->gizmoAngles[1] != 0.0F || g_app->gizmoAngles[2] != 0.0F;
    if (!verschoben && !gedreht) { return false; }
    const std::vector<KameraSchluessel> ks = schluessel();
    if (!gueltig(ks, g_app->selectedKey)) {
        vorschauVerwerfen();
        return false;
    }
    const KameraSchluessel& k = ks[static_cast<std::size_t>(g_app->selectedKey)];
    bool ok = true;
    if (verschoben) {
        if (!k.marke.empty()) {
            float neu[3];
            for (int a = 0; a < 3; ++a) { neu[a] = k.pos[a] + g_app->gizmoOffset[a]; }
            ok = markeOrt(k.marke, neu);
            for (int a = 0; a < 3; ++a) { g_app->gizmoOffset[a] = 0.0F; }
        } else {
            ok = gizmoWriteBack();
        }
    }
    if (gedreht) {
        if (!k.panMarke.empty()) {
            float neu[3];
            for (int a = 0; a < 3; ++a) { neu[a] = k.ang[a] + g_app->gizmoAngles[a]; }
            ok = markeWinkel(k.panMarke, neu) && ok;
            for (int a = 0; a < 3; ++a) { g_app->gizmoAngles[a] = 0.0F; }
        } else {
            ok = gizmoWriteAngles() && ok;
        }
    }
    vorschauVerwerfen();
    const bool nurMarke = (!verschoben || !k.marke.empty()) && (!gedreht || !k.panMarke.empty());
    if (nurMarke) {
        rebuildTree();
        g_app->camTrackValid = false;
        g_app->mapDirty = true;
    } else {
        nachSkriptAenderung();
    }
    if (ok) {
        g_app->gizmoDoneFade = 4.5F;
        setStatus(tr(Str::GizmoWrote), 0, 0);
    } else {
        setStatus(tr(Str::GizmoNoMove), 0, 0);
    }
    return ok;
}

bool kameraOrtSetzen(int key, const float ort[3]) {
    const std::vector<KameraSchluessel> ks = schluessel();
    if (!gueltig(ks, key)) { return false; }
    const KameraSchluessel& k = ks[static_cast<std::size_t>(key)];
    const int vorher = g_app->selectedKey;
    g_app->selectedKey = key;
    vorschauVerwerfen();
    for (int a = 0; a < 3; ++a) { g_app->gizmoOffset[a] = ort[a] - k.pos[a]; }
    const bool ok = kameraVorschauSchreiben();
    g_app->selectedKey = vorher;
    return ok;
}

bool kameraWinkelSetzen(int key, const float ang[3]) {
    const std::vector<KameraSchluessel> ks = schluessel();
    if (!gueltig(ks, key)) { return false; }
    const KameraSchluessel& k = ks[static_cast<std::size_t>(key)];
    const int vorher = g_app->selectedKey;
    g_app->selectedKey = key;
    vorschauVerwerfen();
    for (int a = 0; a < 3; ++a) { g_app->gizmoAngles[a] = ang[a] - (k.hasAng ? k.ang[a] : 0.0F); }
    // Genau 0 hiesse "nichts zu tun" - ein PAN fehlt aber vielleicht noch.
    if (!k.hasAng && g_app->gizmoAngles[0] == 0.0F && g_app->gizmoAngles[1] == 0.0F &&
        g_app->gizmoAngles[2] == 0.0F) {
        g_app->gizmoAngles[1] = 0.0001F;
    }
    const bool ok = kameraVorschauSchreiben();
    g_app->selectedKey = vorher;
    return ok;
}

bool kameraDauerSetzen(int key, double ms) {
    const std::vector<KameraSchluessel> ks = schluessel();
    if (!gueltig(ks, key)) { return false; }
    const Node* alt = nodeAt(g_app->doc.script(), ks[static_cast<std::size_t>(key)].path);
    if (alt == nullptr || alt->args.size() < 3) { return false; }
    Node neu = *alt;
    char b[32];
    std::snprintf(b, sizeof(b), "%.3f", std::max(0.0, ms));
    if (neu.args[2].text == b) { return false; }
    neu.args[2].text = b;
    if (!g_app->doc.replaceAt(ks[static_cast<std::size_t>(key)].path, neu)) { return false; }
    nachSkriptAenderung();
    return true;
}

// Die Einstellung dorthin, wo man gerade hinschaut. Beides in EINEM Schritt,
// wenn MOVE und PAN Zahlen haben.
bool kameraAufAnsicht(int key) {
    const std::vector<KameraSchluessel> ks = schluessel();
    if (!gueltig(ks, key)) { return false; }
    const KameraSchluessel& k = ks[static_cast<std::size_t>(key)];
    const Node* mv = nodeAt(g_app->doc.script(), k.path);
    const Node* pn = k.panPath.empty() ? nullptr : nodeAt(g_app->doc.script(), k.panPath);
    if (k.marke.empty() && k.panMarke.empty() && mv != nullptr && pn != nullptr && mv->args.size() >= 2 &&
        pn->args.size() >= 2) {
        Node m2 = *mv;
        Node p2 = *pn;
        m2.args[1].kind = Arg::Kind::Vector;
        m2.args[1].text = vek(g_app->cam.pos);
        p2.args[1].kind = Arg::Kind::Vector;
        p2.args[1].text = vek(g_app->cam.angles);
        if (!g_app->doc.replaceMany({{k.path, m2}, {k.panPath, p2}})) { return false; }
        vorschauVerwerfen();
        nachSkriptAenderung();
        g_app->gizmoDoneFade = 4.5F;
        setStatus(tr(Str::GizmoWrote), 0, 0);
        return true;
    }
    const bool a = kameraOrtSetzen(key, g_app->cam.pos);
    const bool b = kameraWinkelSetzen(key, g_app->cam.angles);
    return a || b;
}

// --- Auswaehlen ----------------------------------------------------------------

void kameraSchluesselWaehlen(int key, bool zeitMit) {
    const std::vector<KameraSchluessel> ks = schluessel();
    if (!gueltig(ks, key)) { return; }
    g_app->cameraSelected = true;
    g_app->selectedActor = -1;
    g_app->pickedEntity = -1;
    g_app->selectedKey = key;
    vorschauVerwerfen();
    if (zeitMit) {
        const double t = startZeit(ks[static_cast<std::size_t>(key)].path);
        if (t >= 0.0) {
            // Ans ENDE der Fahrt: dort steht die Kamera auf der Einstellung.
            g_app->playMs = t + moveDauer(ks[static_cast<std::size_t>(key)].path);
            g_app->playing = false;
        }
    }
    g_app->gizmoZeitBezug = g_app->playMs;
    selectByPath(ks[static_cast<std::size_t>(key)].path);
    g_app->mapDirty = true;
}

void kameraNeueEinstellung() {
    // Hinter die gewaehlte Einstellung (hinter ihr PAN), sonst hinter die
    // gewaehlte Zeile - wie bisher.
    const std::vector<KameraSchluessel> vorher = schluessel();
    if (g_app->cameraSelected && gueltig(vorher, g_app->selectedKey)) {
        const KameraSchluessel& k = vorher[static_cast<std::size_t>(g_app->selectedKey)];
        g_app->selectedPath = k.panPath.empty() ? k.path : k.panPath;
    }
    const Path alt = g_app->selectedPath;
    insertCameraHere();
    if (g_app->selectedPath == alt) { return; }   // nichts eingefuegt
    const std::vector<KameraSchluessel> ks = schluessel();
    for (std::size_t i = 0; i < ks.size(); ++i) {
        if (ks[i].panPath == g_app->selectedPath) {
            g_app->cameraSelected = true;
            g_app->selectedActor = -1;
            g_app->pickedEntity = -1;
            g_app->selectedKey = static_cast<int>(i);
            g_app->gizmoZeitBezug = g_app->playMs;
            break;
        }
    }
    vorschauVerwerfen();
    nachSkriptAenderung();
    meldung(tr(Str::CamShotAdded), "");
}

bool kameraEinstellungLoeschen(int key) {
    const std::vector<KameraSchluessel> ks = schluessel();
    if (!gueltig(ks, key)) { return false; }
    const KameraSchluessel& k = ks[static_cast<std::size_t>(key)];
    std::vector<Path> weg{k.path};
    if (!k.panPath.empty()) { weg.push_back(k.panPath); }
    if (!g_app->doc.removeAll(weg)) { return false; }
    g_app->selectedPath.clear();
    g_app->selection.clear();
    const int rest = static_cast<int>(ks.size()) - 1;
    g_app->selectedKey = rest <= 0 ? -1 : std::min(key, rest - 1);
    if (rest <= 0) { g_app->cameraSelected = false; }
    vorschauVerwerfen();
    nachSkriptAenderung();
    return true;
}

// --- Steuern (Pilot) -------------------------------------------------------------
//
// Wie "Pilot" in Unreal und "Kamera an Ansicht binden" in Blender: man sitzt in
// der Einstellung, fliegt frei (rechte Maustaste + WASD) und uebernimmt dann.

void kameraPilotStarten() {
    const std::vector<KameraSchluessel> ks = schluessel();
    if (!gueltig(ks, g_app->selectedKey)) { return; }
    const KameraSchluessel& k = ks[static_cast<std::size_t>(g_app->selectedKey)];
    g_app->pilotVorher = g_app->cam;
    g_app->pilotSchluessel = g_app->selectedKey;
    for (int a = 0; a < 3; ++a) {
        g_app->cam.pos[a] = k.pos[a];
        g_app->cam.angles[a] = k.hasAng ? k.ang[a] : g_app->cam.angles[a];
    }
    g_app->throughCamera = false;
    g_app->kameraPilot = true;
    vorschauVerwerfen();
    g_app->mapDirty = true;
}

// --- Die Kamerabahn, wie die der Figuren (rc597) -----------------------------
//
// shank: "welche Bahn die Kamera entlang faehrt ist nicht so gut wie bei den
// Figuren". Bisher eine duenne gruene Linie im Rasterbild. Jetzt dieselbe
// Darstellung wie bei Figuren und Schiffen: kraeftig, obenauf, grau voraus und
// gelb, was bis zur eingestellten Zeit schon gefahren ist; die Schluessel als
// Quadrate (der gewaehlte gelb).
void kameraBahnZeichnen() {
    g_app->kamBahnFarbig = 0;
    g_app->kamBahnGrau = 0;
    if (!g_app->cameraSelected || g_app->camViewActive || !g_app->camTrackValid || g_app->camTrack.durationMs <= 1.0) {
        return;
    }
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->PushClipRect(ImVec2{g_app->kartenAnsicht[0], g_app->kartenAnsicht[1]},
                     ImVec2{g_app->kartenAnsicht[2], g_app->kartenAnsicht[3]}, true);
    const float px = std::max(1.0F, ImGui::GetFontSize() / 13.0F);
    constexpr int kSchritte = 240;
    double w0 = 0.0;
    double w1 = g_app->camTrack.durationMs;
    // "Aktiv": nur die Fahrt zum aktuellen Schluessel und die zum naechsten -
    // "show all und active, weil das zu unuebersichtlich wird".
    const CamSegment* vorSeg = nullptr;
    const CamSegment* nachSeg = nullptr;
    if (g_app->kamBahnNurAktiv) {
        for (const CamSegment& c : g_app->camTrack.segments) {
            if (c.kind != CamSegment::Kind::Move || !c.fremd.empty()) { continue; }
            if (c.startMs <= g_app->playMs + 0.5) {
                if (vorSeg == nullptr || c.startMs >= vorSeg->startMs) { vorSeg = &c; }
            } else if (nachSeg == nullptr || c.startMs < nachSeg->startMs) {
                nachSeg = &c;
            }
        }
        w0 = vorSeg != nullptr ? vorSeg->startMs : 0.0;
        w1 = nachSeg != nullptr ? nachSeg->endMs : (vorSeg != nullptr ? vorSeg->endMs : w1);
        if (w1 <= w0) { w1 = w0 + 1.0; }
    }
    const double dauer = w1 - w0;
    ImVec2 vor;
    bool hatVor = false;
    double vorMs = 0.0;
    for (int i = 0; i <= kSchritte; ++i) {
        const double ms = w0 + dauer * static_cast<double>(i) / static_cast<double>(kSchritte);
        const CameraState st = camStateAt(ms);
        ImVec2 s;
        if (!st.enabled || !kartenPunktAufSchirm(st.pos, s)) {
            hatVor = false;
            continue;
        }
        if (hatVor) {
            const bool gefahren = vorMs < g_app->playMs;
            dl->AddLine(vor, s, IM_COL32(10, 12, 16, 170), 4.5F * px);
            dl->AddLine(vor, s, gefahren ? IM_COL32(240, 200, 60, 255) : IM_COL32(170, 175, 185, 230), 2.2F * px);
            ++(gefahren ? g_app->kamBahnFarbig : g_app->kamBahnGrau);
        }
        vor = s;
        vorMs = ms;
        hatVor = true;
    }
    const std::vector<KameraSchluessel> ks = kameraSchluessel(g_app->doc.script());
    const float r = 5.0F * px;
    for (std::size_t i = 0; i < ks.size(); ++i) {
        ImVec2 s;
        if (!kartenPunktAufSchirm(ks[i].pos, s)) { continue; }
        const bool gewaehlt = static_cast<int>(i) == g_app->selectedKey;
        if (g_app->kamBahnNurAktiv && !gewaehlt) {
            // Nur die Schluessel dieser Fahrten: ihr Anfang und ihre Ziele.
            bool dabei = false;
            for (const CamSegment* c : {vorSeg, nachSeg}) {
                if (c == nullptr) { continue; }
                dabei = dabei || ks[i].path == c->path;
                const float d = std::fabs(ks[i].pos[0] - c->from[0]) + std::fabs(ks[i].pos[1] - c->from[1]) +
                                std::fabs(ks[i].pos[2] - c->from[2]);
                dabei = dabei || d < 0.5F;
            }
            if (!dabei) { continue; }
        }
        dl->AddRectFilled(ImVec2{s.x - r, s.y - r}, ImVec2{s.x + r, s.y + r},
                          gewaehlt ? IM_COL32(240, 200, 60, 255) : IM_COL32(96, 170, 255, 255));
        dl->AddRect(ImVec2{s.x - r, s.y - r}, ImVec2{s.x + r, s.y + r}, IM_COL32(20, 22, 28, 230), 0.0F, 0, 1.2F * px);
    }
    // --- Der Gizmo, wie der der Figuren (rc597) ----------------------------
    //
    // shank: "wenn der Bewegungs- und Rotations-Gizmo noch so gut ist wie bei
    // den Charakteren, dann ist es fast perfekt". Kraeftige Achsen mit Spitze
    // in Rot/Gruen/Blau, Kaestchen in der Mitte, der Ring um die Hochachse
    // schon im Verschieben; beim Drehen ein gefuelltes Tortenstueck. Die Lage
    // kommt aus derselben Rechnung wie Ueberfahren und Ziehen (gizmoSx,
    // gizmoRingSx, Rasterpunkte).
    if (g_app->gizmoShown && !g_app->kameraPilot) {
        const float skal = g_app->mapImage.width > 0
                               ? static_cast<float>(g_app->mapImage.width) /
                                     std::max(1.0F, g_app->kartenAnsicht[2] - g_app->kartenAnsicht[0])
                               : 1.0F;
        const auto schirm = [&](float x, float y) {
            return ImVec2{g_app->kartenAnsicht[0] + x / skal, g_app->kartenAnsicht[1] + y / skal};
        };
        const ImU32 achsFarbe[3] = {IM_COL32(235, 70, 70, 255), IM_COL32(80, 210, 90, 255), IM_COL32(70, 130, 255, 255)};
        const ImU32 hell = IM_COL32(255, 240, 120, 255);
        const auto ring = [&](int ax, ImU32 f, float dicke) {
            for (int t = 0; t + 1 < App::kRingPts; ++t) {
                if (g_app->gizmoRingSx[ax][t] < 0.0F || g_app->gizmoRingSx[ax][t + 1] < 0.0F) { continue; }
                dl->AddLine(schirm(g_app->gizmoRingSx[ax][t], g_app->gizmoRingSy[ax][t]),
                            schirm(g_app->gizmoRingSx[ax][t + 1], g_app->gizmoRingSy[ax][t + 1]), f, dicke);
            }
        };
        if (g_app->gizmoMode == App::GizmoMode::Rotate) {
            for (int ax = 0; ax < 3; ++ax) {
                if (g_app->kamRingAusMove && ax != 2) { continue; }   // nur der gegriffene Ring
                const bool aktiv = (g_app->gizmoRotating && g_app->gizmoAxis == ax) ||
                                   (!g_app->gizmoRotating && g_app->gizmoHover == ax);
                ring(ax, aktiv ? hell : achsFarbe[ax], (aktiv ? 4.0F : 2.5F) * px);
            }
            // Das Tortenstueck: wie weit gedreht wurde.
            const int ax = g_app->gizmoAxis;
            if (g_app->gizmoRotating && ax >= 0 && ax < 3) {
                ImVec2 mitte{0.0F, 0.0F};
                int n = 0;
                for (int t = 0; t + 1 < App::kRingPts; ++t) {
                    if (g_app->gizmoRingSx[ax][t] < 0.0F) { continue; }
                    const ImVec2 q = schirm(g_app->gizmoRingSx[ax][t], g_app->gizmoRingSy[ax][t]);
                    mitte.x += q.x;
                    mitte.y += q.y;
                    ++n;
                }
                const float grad = g_app->gizmoAngles[ax] - g_app->gizmoRotStart[ax];
                const int teile = std::min(App::kRingPts - 1, static_cast<int>(std::fabs(grad) / 7.5F));
                if (n > 0 && teile > 0) {
                    mitte.x /= static_cast<float>(n);
                    mitte.y /= static_cast<float>(n);
                    for (int q = 0; q < teile; ++q) {
                        const int i0 = grad >= 0.0F ? q : (App::kRingPts - 1 - q) % (App::kRingPts - 1);
                        const int i1 = grad >= 0.0F ? q + 1 : (App::kRingPts - 2 - q + (App::kRingPts - 1)) % (App::kRingPts - 1);
                        if (g_app->gizmoRingSx[ax][i0] < 0.0F || g_app->gizmoRingSx[ax][i1] < 0.0F) { continue; }
                        dl->AddTriangleFilled(mitte, schirm(g_app->gizmoRingSx[ax][i0], g_app->gizmoRingSy[ax][i0]),
                                              schirm(g_app->gizmoRingSx[ax][i1], g_app->gizmoRingSy[ax][i1]),
                                              IM_COL32(255, 230, 120, 90));
                    }
                }
            }
        } else {
            if (g_app->kamYawRingInMove) {
                const bool aktiv = g_app->gizmoHover == App::kGizmoYawRing;
                ring(2, aktiv ? hell : IM_COL32(240, 200, 60, 200), (aktiv ? 3.5F : 2.0F) * px);
            }
            for (int ax = 0; ax < 3; ++ax) {
                if (g_app->gizmoSx[ax][0] < 0.0F || g_app->gizmoSx[ax][1] < 0.0F) { continue; }
                const ImVec2 a = schirm(g_app->gizmoSx[ax][0], g_app->gizmoSy[ax][0]);
                const ImVec2 b = schirm(g_app->gizmoSx[ax][1], g_app->gizmoSy[ax][1]);
                const bool aktiv = g_app->gizmoAxis == ax || (g_app->gizmoAxis < 0 && g_app->gizmoHover == ax);
                const ImU32 f = aktiv ? hell : achsFarbe[ax];
                dl->AddLine(a, b, f, (aktiv ? 4.5F : 3.0F) * px);
                const float lx = b.x - a.x;
                const float ly = b.y - a.y;
                const float l = std::sqrt(lx * lx + ly * ly);
                if (l > 4.0F) {
                    const ImVec2 d{lx / l, ly / l};
                    const ImVec2 nrm{-d.y, d.x};
                    const float sp = (aktiv ? 11.0F : 9.0F) * px;
                    dl->AddTriangleFilled(ImVec2{b.x + d.x * sp, b.y + d.y * sp},
                                          ImVec2{b.x - d.x * sp * 0.2F + nrm.x * sp * 0.55F, b.y - d.y * sp * 0.2F + nrm.y * sp * 0.55F},
                                          ImVec2{b.x - d.x * sp * 0.2F - nrm.x * sp * 0.55F, b.y - d.y * sp * 0.2F - nrm.y * sp * 0.55F}, f);
                }
            }
            if (g_app->gizmoSx[0][0] >= 0.0F) {
                const ImVec2 m = schirm(g_app->gizmoSx[0][0], g_app->gizmoSy[0][0]);
                const bool aktiv = g_app->gizmoAxis == App::kGizmoFree ||
                                   (g_app->gizmoAxis < 0 && g_app->gizmoHover == App::kGizmoFree);
                const float h = (aktiv ? 7.0F : 5.5F) * px;
                dl->AddRectFilled(ImVec2{m.x - h, m.y - h}, ImVec2{m.x + h, m.y + h},
                                  aktiv ? IM_COL32(255, 240, 120, 230) : IM_COL32(235, 235, 240, 160));
                dl->AddRect(ImVec2{m.x - h, m.y - h}, ImVec2{m.x + h, m.y + h}, IM_COL32(20, 22, 28, 230), 0.0F, 0, 1.2F * px);
            }
        }
    }
    dl->PopClipRect();
}

bool kameraPilotSchluesselSetzen() {
    if (!g_app->kameraPilot) { return false; }
    int key = -1;
    if (kameraZwischenSchluesselNr(g_app->playMs) >= 0 && kameraSchluesselEinfuegen(g_app->playMs)) {
        key = g_app->selectedKey;
    } else {
        key = schluesselFuerZeit(g_app->playMs);
    }
    if (key < 0) { return false; }
    kameraAufAnsicht(key);
    g_app->selectedKey = key;
    g_app->pilotSchluessel = key;
    char t[128];
    std::snprintf(t, sizeof(t), tr(Str::CamPilotKeySet), g_app->playMs / 1000.0);
    addStatus(t);
    return true;
}

void kameraPilotBeenden(bool uebernehmen) {
    if (!g_app->kameraPilot) { return; }
    g_app->kameraPilot = false;
    if (uebernehmen) {
        kameraAufAnsicht(g_app->pilotSchluessel);
        g_app->selectedKey = g_app->pilotSchluessel;
    } else {
        g_app->cam = g_app->pilotVorher;
    }
    g_app->mapDirty = true;
}

// --- In der Ansicht: Hervorheben, Namensschild, Leiste --------------------------

namespace {

void schild(ImDrawList* dl, ImVec2 an, const std::string& oben, const std::string& unten, ImU32 farbe) {
    const float fs = ImGui::GetFontSize();
    const ImVec2 t1 = ImGui::CalcTextSize(oben.c_str());
    const ImVec2 t2 = unten.empty() ? ImVec2{0.0F, 0.0F} : ImGui::CalcTextSize(unten.c_str());
    const float b = std::max(t1.x, t2.x * 0.85F) + fs * 0.8F;
    const float h = t1.y + (unten.empty() ? 0.0F : t2.y * 0.85F) + fs * 0.4F;
    const ImVec2 a{an.x - b * 0.5F, an.y - h - fs * 1.6F};
    dl->AddRectFilled(a, ImVec2{a.x + b, a.y + h}, IM_COL32(20, 22, 28, 215), fs * 0.3F);
    dl->AddRect(a, ImVec2{a.x + b, a.y + h}, farbe, fs * 0.3F);
    dl->AddText(ImVec2{a.x + fs * 0.4F, a.y + fs * 0.2F}, farbe, oben.c_str());
    if (!unten.empty()) {
        dl->AddText(ImGui::GetFont(), fs * 0.85F, ImVec2{a.x + fs * 0.4F, a.y + fs * 0.2F + t1.y},
                    IM_COL32(190, 195, 205, 255), unten.c_str());
    }
}

std::string einstellungsName(int key, int anzahl) {
    char t[96];
    std::snprintf(t, sizeof(t), tr(Str::CamShotOf), key + 1, anzahl);
    return t;
}

}  // namespace

void kameraUeberlagerung(float rasterSkal) {
    const ImGuiIO& io = ImGui::GetIO();
    const float x0 = g_app->kartenAnsicht[0];
    const float y0 = g_app->kartenAnsicht[1];
    const float x1 = g_app->kartenAnsicht[2];
    const float y1 = g_app->kartenAnsicht[3];
    const bool imBild = io.MousePos.x >= x0 && io.MousePos.x < x1 && io.MousePos.y >= y0 && io.MousePos.y < y1 &&
                        ImGui::IsWindowHovered();
    const float fs = ImGui::GetFontSize();
    const float skal = rasterSkal > 0.0F ? rasterSkal : 1.0F;
    const ImVec2 kamSchirm{x0 + g_app->cameraSx / skal, y0 + g_app->cameraSy / skal};
    const std::vector<KameraSchluessel> ks = schluessel();

    // --- Ueberfahren ------------------------------------------------------
    bool hover = false;
    if (imBild && g_app->cameraSx >= 0.0F && !g_app->camViewActive && !ImGui::IsMouseDown(ImGuiMouseButton_Right) &&
        g_app->gizmoAxis < 0 && !g_app->gizmoRotating && g_app->entHover < 0 && g_app->entGizmoHover == 0 &&
        !(g_app->gizmoShown && g_app->gizmoHover >= 0)) {
        const float dx = io.MousePos.x - kamSchirm.x;
        const float dy = io.MousePos.y - kamSchirm.y;
        const float weit = fs * 1.2F * 2.5F / skal;
        hover = dx * dx + dy * dy < weit * weit;
    }
    if (hover != g_app->kameraHover) {
        g_app->kameraHover = hover;
        g_app->mapDirty = true;
    }
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->PushClipRect(ImVec2{x0, y0}, ImVec2{x1, y1}, true);
    if ((hover || (g_app->cameraSelected && g_app->gizmoAxis < 0 && !g_app->gizmoRotating)) &&
        g_app->cameraSx >= 0.0F && !g_app->camViewActive && !ks.empty()) {
        const int k = g_app->cameraSelected && gueltig(ks, g_app->selectedKey) ? g_app->selectedKey
                                                                                : schluesselFuerZeit(g_app->playMs);
        std::string unten;
        if (gueltig(ks, k)) {
            const KameraSchluessel& s = ks[static_cast<std::size_t>(k)];
            char z[160];
            std::snprintf(z, sizeof(z), "%.0f %.0f %.0f", static_cast<double>(s.pos[0]), static_cast<double>(s.pos[1]),
                          static_cast<double>(s.pos[2]));
            unten = z;
            if (!s.marke.empty()) { unten += "  \xC2\xB7  ref_tag " + s.marke; }
        }
        schild(dl, kamSchirm, std::string(tr(Str::CamName)) + "  \xC2\xB7  " + einstellungsName(std::max(k, 0), static_cast<int>(ks.size())),
               unten, g_app->cameraSelected ? IM_COL32(255, 165, 40, 255) : IM_COL32(235, 238, 245, 255));
    }
    dl->PopClipRect();

    // --- Pilot: Hinweisband oben -------------------------------------------
    if (g_app->kameraPilot) {
        const ImVec2 merk = ImGui::GetCursorScreenPos();
        const std::string text = std::string(tr(Str::CamPiloting)) + "  " +
                                 einstellungsName(g_app->pilotSchluessel, static_cast<int>(ks.size()));
        const float b = ImGui::CalcTextSize(text.c_str()).x + fs * 14.0F;
        ImGui::SetCursorScreenPos(ImVec2{(x0 + x1 - b) * 0.5F, y0 + fs * 0.6F});
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4{0.55F, 0.20F, 0.55F, 0.92F});
        if (ImGui::BeginChild("##kamerapilot", ImVec2{b, 0.0F}, ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_Borders,
                              ImGuiWindowFlags_NoScrollbar)) {
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(text.c_str());
            if (ImGui::IsItemHovered()) { ImGui::SetTooltip("%s", tr(Str::CamPilotHint)); }
            ImGui::SameLine();
            if (iconTextKnopf(tr(Str::CamPilotKey), UiIcon::Plus, tr(Str::CamPilotKeyHint)) ||
                (ImGui::IsKeyPressed(ImGuiKey_Enter, false) && !ImGui::GetIO().WantTextInput)) {
                kameraPilotSchluesselSetzen();
            }
            ImGui::SameLine();
            if (iconTextKnopf(tr(Str::CamPilotApply), UiIcon::Haken, tr(Str::CamPilotHint))) { kameraPilotBeenden(true); }
            ImGui::SameLine();
            if (iconTextKnopf(tr(Str::CamPilotCancel), UiIcon::Schliessen, nullptr) ||
                ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
                kameraPilotBeenden(false);
            }
        }
        ImGui::EndChild();
        ImGui::PopStyleColor();
        ImGui::SetCursorScreenPos(merk);
        ImGui::Dummy(ImVec2{0.0F, 0.0F});   // Anker: ImGui will nach dem Cursorsprung ein Element
        return;
    }

    // --- Die Leiste der gewaehlten Kamera ------------------------------------
    if (!g_app->cameraSelected || ks.empty()) { return; }
    const ImVec2 merk = ImGui::GetCursorScreenPos();
    ImGui::SetCursorScreenPos(ImVec2{x0 + fs * 0.5F, y0 + fs * 0.5F});
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4{0.10F, 0.11F, 0.14F, 0.88F});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{fs * 0.3F, fs * 0.25F});
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{fs * 0.2F, fs * 0.2F});
    if (ImGui::BeginChild("##kameraleiste", ImVec2{0.0F, 0.0F},
                          ImGuiChildFlags_AutoResizeX | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_Borders |
                              ImGuiChildFlags_AlwaysUseWindowPadding,
                          ImGuiWindowFlags_NoScrollbar)) {
        const float luecke = fs * 0.6F;
        const bool dreht = g_app->gizmoMode == App::GizmoMode::Rotate;
        if (iconKnopf(tr(Str::GizmoMove), UiIcon::Verschieben, tr(Str::CamMoveHint),
                      g_app->gizmoMode == App::GizmoMode::Move, true, true)) {
            g_app->gizmoMode = App::GizmoMode::Move;
            g_app->mapDirty = true;
        }
        ImGui::SameLine();
        if (iconKnopf(tr(Str::GizmoRotate), UiIcon::Drehen, tr(Str::CamRotateHint), dreht, true, true)) {
            g_app->gizmoMode = App::GizmoMode::Rotate;
            g_app->mapDirty = true;
        }
        ImGui::SameLine();
        {
            App::GizmoSpace& raum = dreht ? g_app->gizmoRotSpace : g_app->gizmoMoveSpace;
            const bool welt = raum == App::GizmoSpace::World;
            if (ImGui::Button(tr(welt ? Str::GizmoSpaceWorld : Str::GizmoSpaceLocal))) {
                raum = welt ? App::GizmoSpace::Local : App::GizmoSpace::World;
                g_app->mapDirty = true;
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) { ImGui::SetTooltip("%s", tr(Str::CamSpaceHint)); }
        }
        ImGui::SameLine(0.0F, luecke);
        const int n = static_cast<int>(ks.size());
        const int k = std::clamp(g_app->selectedKey, 0, n - 1);
        if (iconKnopf(tr(Str::CamPrevShot), UiIcon::Zurueck, nullptr, false, k > 0)) { kameraSchluesselWaehlen(k - 1, true); }
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(einstellungsName(k, n).c_str());
        ImGui::SameLine();
        if (iconKnopf(tr(Str::CamNextShot), UiIcon::Vor, nullptr, false, k + 1 < n)) { kameraSchluesselWaehlen(k + 1, true); }
        ImGui::SameLine(0.0F, luecke);
        if (iconKnopf(tr(Str::CamSetToView), UiIcon::Zielen, tr(Str::CamSetToViewHint), false, !g_app->camViewActive)) {
            kameraAufAnsicht(k);
        }
        ImGui::SameLine();
        if (iconKnopf(tr(Str::CamPilot), UiIcon::Steuern, tr(Str::CamPilotHint))) {
            g_app->selectedKey = k;
            kameraPilotStarten();
        }
        ImGui::SameLine();
        if (iconKnopf(tr(Str::MapThroughCam), UiIcon::Kamera, tr(Str::MapThroughCamHint), g_app->throughCamera, true, true)) {
            g_app->throughCamera = !g_app->throughCamera;
            g_app->mapDirty = true;
        }
        ImGui::SameLine();
        if (iconKnopf(tr(Str::CamPreview), UiIcon::Bildschirm, tr(Str::CamPreviewHint), g_app->kameraBildImBild, true,
                      true)) {
            g_app->kameraBildImBild = !g_app->kameraBildImBild;
            g_app->mapDirty = true;
        }
        ImGui::SameLine(0.0F, luecke);
        if (iconKnopf(tr(Str::CamNewShot), UiIcon::Plus, tr(Str::CamNewShotHint))) { kameraNeueEinstellung(); }
        ImGui::SameLine();
        if (iconKnopf(tr(Str::CamDeleteShot), UiIcon::Loeschen, nullptr)) { kameraEinstellungLoeschen(k); }
    }
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
    ImGui::SetCursorScreenPos(merk);
    ImGui::Dummy(ImVec2{0.0F, 0.0F});   // Anker: ImGui will nach dem Cursorsprung ein Element
}

// --- Bild-im-Bild (rc592) ------------------------------------------------------
//
// Unten rechts in der Ansicht, mit dem Rahmen in der Farbe der gewaehlten
// Kamera. Ein Klick schaut durch die Kamera (wie der Knopf daneben).
void kameraVorschauZeigen() {
    if (!g_app->vorschauGezeigt || g_app->camViewActive) { return; }
    void* tex = gpu::zielTexturSatz(1);
    if (tex == nullptr || g_app->vorschauW <= 0 || g_app->vorschauH <= 0) { return; }
    const float x0 = g_app->kartenAnsicht[0];
    const float y0 = g_app->kartenAnsicht[1];
    const float x1 = g_app->kartenAnsicht[2];
    const float y1 = g_app->kartenAnsicht[3];
    const float fs = ImGui::GetFontSize();
    // Auf dem Schirm so breit wie ein Drittel der Ansicht - unabhaengig
    // davon, wie fein gerastert wurde.
    const float b = std::clamp((x1 - x0) / 3.0F, 120.0F, 640.0F);
    const float h = b * static_cast<float>(g_app->vorschauH) / static_cast<float>(g_app->vorschauW);
    if (b > (x1 - x0) * 0.6F || h > (y1 - y0) * 0.6F) { return; }   // zu wenig Platz
    const ImVec2 p1{x1 - fs * 0.6F, y1 - fs * 0.6F};
    const ImVec2 p0{p1.x - b, p1.y - h};
    // Ein eigenes Kindfenster wie die Kameraleiste: solange die Maus darauf
    // steht, gilt die Kartenansicht als nicht ueberfahren - ein Klick waehlt
    // dann nicht zugleich etwas dahinter aus.
    const ImVec2 merk = ImGui::GetCursorScreenPos();
    ImGui::SetCursorScreenPos(p0);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{0.0F, 0.0F});
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 0.0F);
    if (ImGui::BeginChild("##kameravorschau", ImVec2{b, h}, ImGuiChildFlags_None,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
        ImGui::Image(reinterpret_cast<ImTextureID>(tex), ImVec2{b, h});
        if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
            g_app->throughCamera = true;
            g_app->mapDirty = true;
        }
        if (ImGui::IsItemHovered()) { ImGui::SetTooltip("%s", tr(Str::CamPreviewClick)); }
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRect(ImVec2{p0.x + 1.0F, p0.y + 1.0F}, ImVec2{p1.x - 1.0F, p1.y - 1.0F}, IM_COL32(255, 165, 40, 255), 3.0F,
                    0, 2.0F);
        const std::vector<KameraSchluessel> ks = schluessel();
        const int k = gueltig(ks, g_app->selectedKey) ? g_app->selectedKey : schluesselFuerZeit(g_app->playMs);
        const std::string titel = std::string(tr(Str::CamName)) + "  \xC2\xB7  " +
                                  einstellungsName(std::max(k, 0), static_cast<int>(ks.size()));
        const ImVec2 ts = ImGui::CalcTextSize(titel.c_str());
        dl->AddRectFilled(ImVec2{p0.x + 2.0F, p0.y + 2.0F}, ImVec2{p0.x + ts.x + fs * 0.8F, p0.y + ts.y + fs * 0.4F},
                          IM_COL32(12, 13, 16, 200));
        dl->AddText(ImVec2{p0.x + fs * 0.4F, p0.y + fs * 0.2F}, IM_COL32(255, 200, 120, 255), titel.c_str());
    }
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::SetCursorScreenPos(merk);
    ImGui::Dummy(ImVec2{0.0F, 0.0F});   // Anker nach dem Cursorsprung
}

// --- Inspektor -----------------------------------------------------------------

void drawKameraInspector(float hoehe) {
    ImGui::BeginChild("kamerainspektor", ImVec2{-1.0F, hoehe}, ImGuiChildFlags_Borders,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    const std::vector<KameraSchluessel> ks = schluessel();
    const int n = static_cast<int>(ks.size());
    const int k = n == 0 ? -1 : std::clamp(g_app->selectedKey, 0, n - 1);
    const float g = ImGui::GetFontSize();
    const ImVec4 farbe = uiIconFarbe(UiIcon::Kamera);
    {
        const ImVec2 q = ImGui::GetCursorScreenPos();
        uiIcon(ImGui::GetWindowDrawList(), UiIcon::Kamera, q, g, ImGui::GetColorU32(farbe));
        ImGui::Dummy(ImVec2{g, g});
        ImGui::SameLine(0.0F, ImGui::GetStyle().ItemInnerSpacing.x);
    }
    ImGui::TextColored(farbe, "%s", tr(Str::CamName));
    if (k >= 0) {
        ImGui::SameLine();
        ImGui::TextDisabled("%s", einstellungsName(k, n).c_str());
    }
    // Icon-Leiste wie beim Entity-Inspektor.
    {
        const float sp = ImGui::GetStyle().ItemSpacing.x;
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{sp * 0.4F, ImGui::GetStyle().ItemSpacing.y});
        if (iconKnopf(tr(Str::CamFlyToShot), UiIcon::Hinfliegen, nullptr, false, k >= 0)) {
            const KameraSchluessel& s = ks[static_cast<std::size_t>(k)];
            for (int a = 0; a < 3; ++a) {
                g_app->cam.pos[a] = s.pos[a];
                if (s.hasAng) { g_app->cam.angles[a] = s.ang[a]; }
            }
            g_app->throughCamera = false;
            g_app->mapDirty = true;
        }
        ImGui::SameLine();
        if (iconKnopf(tr(Str::CamSetToView), UiIcon::Zielen, tr(Str::CamSetToViewHint), false, k >= 0)) { kameraAufAnsicht(k); }
        ImGui::SameLine();
        if (iconKnopf(tr(Str::CamPilot), UiIcon::Steuern, tr(Str::CamPilotHint), false, k >= 0)) {
            g_app->selectedKey = k;
            kameraPilotStarten();
        }
        ImGui::SameLine(0.0F, sp * 1.2F);
        if (iconKnopf(tr(Str::CamNewShot), UiIcon::Plus, tr(Str::CamNewShotHint))) { kameraNeueEinstellung(); }
        ImGui::SameLine();
        if (iconKnopf(tr(Str::CamDeleteShot), UiIcon::Loeschen, nullptr, false, k >= 0)) { kameraEinstellungLoeschen(k); }
        // Welche Bahn die Ansicht zeigt (rc597): alle Fahrten oder nur die
        // um die eingestellte Zeit.
        ImGui::SameLine(0.0F, sp * 2.5F);
        const auto schalter = [&](Str text, bool an) {
            ImGui::PushStyleColor(ImGuiCol_Button, an ? ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive)
                                                      : ImGui::GetStyleColorVec4(ImGuiCol_FrameBg));
            const bool k2 = ImGui::SmallButton(tr(text));
            ImGui::PopStyleColor();
            if (ImGui::IsItemHovered()) { ImGui::SetTooltip("%s", tr(Str::CamPathHint)); }
            return k2;
        };
        if (schalter(Str::CamPathAll, !g_app->kamBahnNurAktiv)) {
            g_app->kamBahnNurAktiv = false;
            g_app->mapDirty = true;
        }
        ImGui::SameLine(0.0F, 0.0F);
        if (schalter(Str::CamPathActive, g_app->kamBahnNurAktiv)) {
            g_app->kamBahnNurAktiv = true;
            g_app->mapDirty = true;
        }
        ImGui::PopStyleVar();
    }
    ImGui::Separator();
    ImGui::BeginChild("kamerafelder", ImVec2{-1.0F, -1.0F}, ImGuiChildFlags_None);
    // --- Einstellungen ------------------------------------------------------
    if (ImGui::CollapsingHeader(tr(Str::CamShots), ImGuiTreeNodeFlags_DefaultOpen)) {
        for (int i = 0; i < n; ++i) {
            const KameraSchluessel& s = ks[static_cast<std::size_t>(i)];
            const double t = startZeit(s.path);
            const double d = moveDauer(s.path);
            char z[200];
            int len = std::snprintf(z, sizeof(z), "%s", einstellungsName(i, n).c_str());
            if (t >= 0.0) {
                const int zs = static_cast<int>(t / 100.0);
                len += std::snprintf(z + len, sizeof(z) - static_cast<std::size_t>(len), "   %02d:%02d.%d", zs / 600,
                                     (zs / 10) % 60, zs % 10);
            }
            if (d > 0.5) {
                len += std::snprintf(z + len, sizeof(z) - static_cast<std::size_t>(len), tr(Str::CamMoving), d / 1000.0);
            }
            if (!s.marke.empty()) {
                std::snprintf(z + len, sizeof(z) - static_cast<std::size_t>(len), "   ref_tag %s", s.marke.c_str());
            }
            ImGui::PushID(i);
            if (ImGui::Selectable(z, i == k)) { kameraSchluesselWaehlen(i, true); }
            ImGui::PopID();
        }
        if (n == 0) {
            ImGui::PushTextWrapPos(0.0F);
            ImGui::TextDisabled("%s", tr(Str::CamNoShots));
            ImGui::PopTextWrapPos();
        }
    }
    // --- Die gewaehlte Einstellung --------------------------------------------
    if (k >= 0 && ImGui::CollapsingHeader(tr(Str::CamSelectedShot), ImGuiTreeNodeFlags_DefaultOpen)) {
        const KameraSchluessel& s = ks[static_cast<std::size_t>(k)];
        const float lw = g * 5.5F;
        // Werte bleiben waehrend des Ziehens hier stehen und gehen erst beim
        // Loslassen ins Skript - ein Rueckgaengig-Schritt je Aenderung.
        static float ort[3];
        static float ang[3];
        static float dauer = 0.0F;
        static int fuer = -1;
        static bool ortAktiv = false;
        static bool angAktiv = false;
        static bool dauerAktiv = false;
        if (fuer != k || !ortAktiv) { std::copy(s.pos, s.pos + 3, ort); }
        if (fuer != k || !angAktiv) { std::copy(s.ang, s.ang + 3, ang); }
        if (fuer != k || !dauerAktiv) { dauer = static_cast<float>(moveDauer(s.path) / 1000.0); }
        fuer = k;
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(tr(Str::CamPosition));
        ImGui::SameLine(lw);
        ImGui::SetNextItemWidth(-1.0F);
        ImGui::DragFloat3("##ort", ort, 1.0F, 0.0F, 0.0F, "%.0f");
        ortAktiv = ImGui::IsItemActive();
        if (ImGui::IsItemDeactivatedAfterEdit()) { kameraOrtSetzen(k, ort); }
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(tr(Str::CamAngles));
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) { ImGui::SetTooltip("%s", tr(Str::CamAnglesHint)); }
        ImGui::SameLine(lw);
        ImGui::SetNextItemWidth(-1.0F);
        ImGui::DragFloat3("##winkel", ang, 0.5F, 0.0F, 0.0F, "%.1f");
        angAktiv = ImGui::IsItemActive();
        if (ImGui::IsItemDeactivatedAfterEdit()) { kameraWinkelSetzen(k, ang); }
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(tr(Str::CamDuration));
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) { ImGui::SetTooltip("%s", tr(Str::CamDurationHint)); }
        ImGui::SameLine(lw);
        ImGui::SetNextItemWidth(-1.0F);
        ImGui::BeginDisabled(!s.marke.empty() && nodeAt(g_app->doc.script(), s.path) == nullptr);
        ImGui::DragFloat("##dauer", &dauer, 0.05F, 0.0F, 120.0F, "%.2f s");
        dauerAktiv = ImGui::IsItemActive();
        if (ImGui::IsItemDeactivatedAfterEdit()) { kameraDauerSetzen(k, static_cast<double>(dauer) * 1000.0); }
        ImGui::EndDisabled();
        if (!s.marke.empty()) {
            ImGui::PushTextWrapPos(0.0F);
            ImGui::TextDisabled(tr(Str::CamOnTag), s.marke.c_str());
            ImGui::PopTextWrapPos();
        }
    }
    ImGui::EndChild();
    ImGui::EndChild();
}

}  // namespace bhed::gui

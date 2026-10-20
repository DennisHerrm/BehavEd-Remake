// app_regelwerk.cpp - rc601 integ: der zentrale Schalter "OpenJK / Movie
// Duels" in der Oberflaeche. rc604: dazu das echte Jedi Outcast (JK2).
//
// Der Betreuer: die Vorschau soll genau so laufen wie im Spiel, und zwar in
// BEIDEN Spielen - OpenJK (reines Jedi Academy) und Movie Duels. Gewaehlt
// wird in den Einstellungen ("Spielregeln"); automatisch heisst: Movie Duels,
// wenn ein Movie-Duels-Ordner unter den Spielordnern ist (movieDuelsOrdner,
// dieselbe Erkennung wie Spielfenster und Ziel-pk3), sonst OpenJK.
//
// rc604 (Betreuer: "echtes Jedi Outcast" als drittes Regelwerk, Vorlage der
// JK2-Spielcode in OpenJK, codeJK2/; im Zweifel bleibt OpenJK): Jedi Outcast
// hat EIGENE Spielordner (Settings::gamePathsJk2). Beide Spiele haben gleich
// benannte Dateien mit anderem Inhalt - models/players/_humanoid/
// _humanoid.gla hat in JK2 72 Knochen, in JKA 53 -, gemischt ueberdeckte
// eines das andere. Welche Liste gilt, folgt dem Regelwerk
// (eingerichteteSpielordner); "Load mission" setzt Ordner nur fuer die
// Sitzung (rc603), und "automatisch" erkennt das Spiel dann an diesen.
//
// regelwerkPruefen laeuft jedes Bild (ein Vergleich weniger Zahlen und der
// Spielordnernamen; die JK2-Erkennung je Ordner ist zwischengespeichert) - so
// wirkt jede Aenderung sofort, ob sie aus dem Einstellungsfenster, einem
// Abbrechen dort (prefsZurueck), neuen Spielordnern oder einem Selbsttest
// kommt.
#include "app_internal.h"

#include "bhed/diag.h"
#include "bhed/pk3.h"
#include "bhed/regelwerk.h"

#include <cctype>
#include <filesystem>
#include <map>
#include <string>
#include <system_error>
#include <vector>

namespace bhed::gui {

namespace {

// Ergebnis der JK2-Erkennung je Ordner (Dateisystem und .pk3-Verzeichnisse
// werden nur einmal gelesen).
std::map<std::string, bool> g_jk2Ordner;

std::string kleinPfad(std::string s) {
    for (char& c : s) {
        c = c == '\\' ? '/' : static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    while (s.size() > 1 && s.back() == '/') { s.pop_back(); }
    return s;
}

// Das Spiel nur nach den Einstellungen (ohne Sitzung): eine ausdrueckliche
// Wahl, sonst die eingerichteten Listen - die von Jedi Academy / Movie Duels
// zuerst, Jedi Outcast nur, wenn es keine JKA-Ordner gibt.
int spielOhneSitzung() {
    const Settings& st = g_app->settings;
    if (st.regelwerk != kRegelwerkAuto) { return regelwerkSpiel(st.regelwerk, false, false); }
    bool md = false;
    for (const std::string& o : st.gamePaths) { md = md || movieDuelsOrdner(o); }
    return regelwerkSpiel(kRegelwerkAuto, md, st.gamePaths.empty() && !st.gamePathsJk2.empty());
}

}  // namespace

bool jediOutcastOrdner(const std::string& verzeichnis) {
    namespace fs = std::filesystem;
    if (verzeichnis.empty() || movieDuelsOrdner(verzeichnis)) { return false; }
    const std::string schluessel = kleinPfad(verzeichnis);
    const auto it = g_jk2Ordner.find(schluessel);
    if (it != g_jk2Ordner.end()) { return it->second; }
    std::vector<std::string> dateien;
    bool jk2Karte = false;
    std::error_code ec;
    const fs::path ordner = fs::u8path(verzeichnis);
    for (const fs::path& o : {ordner, ordner.parent_path()}) {
        for (const auto& e : fs::directory_iterator(o, ec)) {
            std::string n = e.path().filename().string();
            for (char& c : n) { c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }
            dateien.push_back(n);
            // Nur im Ordner selbst: eine .pk3 mit einer Karte, die es nur in
            // JK2 gibt (die erste Mission, kejim_post).
            if (o == ordner && !jk2Karte && n.size() > 4 && n.compare(n.size() - 4, 4, ".pk3") == 0) {
                Pk3 a;
                const std::u8string p8 = e.path().u8string();
                if (readPk3Directory(std::string(p8.begin(), p8.end()), a, nullptr)) {
                    for (const Pk3Entry& pe : a.entries) {
                        std::string k = pe.name;
                        for (char& c : k) { c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }
                        if (k == "maps/kejim_post.bsp") {
                            jk2Karte = true;
                            break;
                        }
                    }
                }
            }
        }
    }
    const bool jk2 = jediOutcastHinweis(dateien, jk2Karte);
    g_jk2Ordner[schluessel] = jk2;
    diag::info("Spielordner " + verzeichnis + ": " + (jk2 ? "Jedi Outcast" : "kein Jedi Outcast"));
    return jk2;
}

bool regelwerkOrdnerErkannt() {
    for (const GamePath& gp : g_app->gamePaths) {
        if (movieDuelsOrdner(gp.directory)) { return true; }
    }
    return false;
}

int regelwerkSpielErmitteln() {
    if (g_app == nullptr) { return kRegelwerkOpenJK; }
    if (g_app->sitzungsSpielordner.empty()) { return spielOhneSitzung(); }
    // Ordner einer Sitzung (Load mission): das Spiel an ihnen erkennen.
    bool md = false;
    bool jk2 = false;
    for (const std::string& o : g_app->sitzungsSpielordner) {
        md = md || movieDuelsOrdner(o);
        jk2 = jk2 || jediOutcastOrdner(o);
    }
    return regelwerkSpiel(g_app->settings.regelwerk, md, jk2);
}

const std::vector<std::string>& eingerichteteSpielordner() {
    return spielOhneSitzung() == kRegelwerkJediOutcast ? g_app->settings.gamePathsJk2 : g_app->settings.gamePaths;
}

void befehlssatzFuerRegelwerk() {
    const bool jk2 = regelwerkJediOutcast();
    // Eine eigene .bhc aus den Einstellungen gilt immer (loadModel).
    if (!g_app->settings.commandFile.empty() || g_app->dataDir.empty()) { return; }
    std::string ordner = g_app->dataDir;
    if (jk2) {
        // data/jk2 liegt neben data/base (auch ausgepackt, eingebaut.cpp).
        const std::string jk2Ordner = directoryOf(g_app->dataDir) + "/jk2";
        std::error_code ec;
        if (std::filesystem::is_regular_file(std::filesystem::u8path(jk2Ordner + "/behaved.bhc"), ec)) {
            ordner = jk2Ordner;
        } else {
            diag::warn("Befehlssatz Jedi Outcast fehlt (" + jk2Ordner + "), nehme den von Jedi Academy");
        }
    }
    const std::string bisher = g_app->befehlssatzOrdner.empty() ? g_app->dataDir : g_app->befehlssatzOrdner;
    if (ordner == bisher) { return; }
    const std::string kopf = g_app->settings.sourcePath.empty() ? ordner : g_app->settings.sourcePath;
    CommandDb neu;
    std::vector<LoadDiag> ld;
    if (!loadCommandDb(ordner + "/behaved.bhc", kopf, neu, ld)) {
        diag::warn("Befehlssatz nicht ladbar: " + ordner);
        return;
    }
    (void)loadCommandDb(ordner + "/supplement.bhc", kopf, neu, ld);
    g_app->db = std::move(neu);
    g_app->befehlssatzOrdner = ordner;
    rebuildTree();
    const bool istJk2 = ordner != g_app->dataDir;
    diag::info("Befehlssatz: " + ordner + " (" + std::to_string(g_app->db.commands.size()) + " Befehle)");
    addStatus(tr(istJk2 ? Str::MsgBefehlssatzJk2 : Str::MsgBefehlssatzJka));
}

void regelwerkAnwendenJetzt() {
    const int spiel = regelwerkSpielErmitteln();
    const int einstellung = g_app->settings.schwierigkeit;
    const bool ersterStand = g_app->regelwerkMdStand < 0;
    const int stufeVorher = regelwerkSpielstufe();
    const int spielVorher = g_app->regelwerkSpielStand;
    regelwerkAnwendenSpiel(spiel, einstellung);
    g_app->regelwerkMdStand = spiel == kRegelwerkMovieDuels ? 1 : 0;
    g_app->regelwerkSpielStand = spiel;
    g_app->regelwerkSchwierigkeitStand = einstellung;
    if (ersterStand || spielVorher != spiel || stufeVorher != regelwerkSpielstufe()) {
        const char* name = spiel == kRegelwerkMovieDuels    ? "Movie Duels"
                           : spiel == kRegelwerkJediOutcast ? "Jedi Outcast"
                                                            : "OpenJK";
        diag::info(std::string("Regelwerk: ") + name + " (Einstellung " + regelwerkSchluessel(g_app->settings.regelwerk) +
                   ", Movie-Duels-Ordner " + (regelwerkOrdnerErkannt() ? "erkannt" : "nicht erkannt") +
                   "), g_spskill " + std::to_string(regelwerkSpielstufe()));
        // Neu aufbauen: yawSpeed je Klasse und Schwierigkeit (NPC_Begin),
        // Schwertregeln, Spielfenster - alles, was am Regelwerk haengt.
        g_app->camTrackValid = false;
        g_app->mapDirty = true;
    }
    if (ersterStand || (spielVorher == kRegelwerkJediOutcast) != (spiel == kRegelwerkJediOutcast)) {
        befehlssatzFuerRegelwerk();
    }
}

bool regelwerkPruefen() {
    if (g_app == nullptr) { return false; }
    const int spiel = regelwerkSpielErmitteln();
    const int einstellung = g_app->settings.schwierigkeit;
    if (spiel == g_app->regelwerkSpielStand && einstellung == g_app->regelwerkSchwierigkeitStand) { return false; }
    // rc604: wechselt es zwischen Jedi Outcast und Jedi Academy, gelten
    // andere Spielordner und ein anderes Skelett - alles aus den Archiven neu
    // (rescanGamePaths wendet das Regelwerk dabei zuerst an).
    const bool jk2Wechsel = g_app->regelwerkSpielStand >= 0 &&
                            (g_app->regelwerkSpielStand == kRegelwerkJediOutcast) != (spiel == kRegelwerkJediOutcast);
    if (jk2Wechsel) {
        rescanGamePaths();
        refreshPk3List();
        return true;
    }
    regelwerkAnwendenJetzt();
    return true;
}

int schwierigkeitJetzt() { return schwierigkeitWirksam(g_app->settings.schwierigkeit, regelwerkMovieDuels()); }

std::string regelwerkStatusZeile() {
    const bool md = regelwerkMovieDuels();
    const bool jk2 = regelwerkJediOutcast();
    const char* name = md ? "Movie Duels" : jk2 ? "Jedi Outcast" : "OpenJK";
    char buf[256];
    if (g_app->settings.regelwerk == kRegelwerkAuto) {
        std::snprintf(buf, sizeof(buf),
                      tr(md ? Str::RegelStatusAutoMd : jk2 ? Str::RegelStatusAutoJk2 : Str::RegelStatusAutoOjk), name);
    } else {
        std::snprintf(buf, sizeof(buf), tr(Str::RegelStatusFest), name);
    }
    return buf;
}

}  // namespace bhed::gui

// app_regelwerk.cpp - rc601 integ: der zentrale Schalter "OpenJK / Movie
// Duels" in der Oberflaeche.
//
// Der Betreuer: die Vorschau soll genau so laufen wie im Spiel, und zwar in
// BEIDEN Spielen - OpenJK (reines Jedi Academy) und Movie Duels. Gewaehlt
// wird in den Einstellungen ("Spielregeln"); automatisch heisst: Movie Duels,
// wenn ein Movie-Duels-Ordner unter den Spielordnern ist (movieDuelsOrdner,
// dieselbe Erkennung wie Spielfenster und Ziel-pk3), sonst OpenJK.
//
// regelwerkPruefen laeuft jedes Bild (ein Vergleich weniger Zahlen und der
// Spielordnernamen) - so wirkt jede Aenderung sofort, ob sie aus dem
// Einstellungsfenster, einem Abbrechen dort (prefsZurueck), neuen
// Spielordnern oder einem Selbsttest kommt.
#include "app_internal.h"

#include "bhed/diag.h"
#include "bhed/regelwerk.h"

#include <string>

namespace bhed::gui {

bool regelwerkOrdnerErkannt() {
    for (const GamePath& gp : g_app->gamePaths) {
        if (movieDuelsOrdner(gp.directory)) { return true; }
    }
    return false;
}

bool regelwerkPruefen() {
    if (g_app == nullptr) { return false; }
    const bool md = regelwerkAufloesen(g_app->settings.regelwerk, regelwerkOrdnerErkannt());
    const int einstellung = g_app->settings.schwierigkeit;
    const int mdStand = md ? 1 : 0;
    if (mdStand == g_app->regelwerkMdStand && einstellung == g_app->regelwerkSchwierigkeitStand) { return false; }
    const bool ersterStand = g_app->regelwerkMdStand < 0;
    const int stufeVorher = regelwerkSpielstufe();
    const bool mdVorher = regelwerkMovieDuels();
    regelwerkAnwenden(md, einstellung);
    g_app->regelwerkMdStand = mdStand;
    g_app->regelwerkSchwierigkeitStand = einstellung;
    if (ersterStand || mdVorher != md || stufeVorher != regelwerkSpielstufe()) {
        diag::info(std::string("Regelwerk: ") + (md ? "Movie Duels" : "OpenJK") + " (Einstellung " +
                   regelwerkSchluessel(g_app->settings.regelwerk) + ", Movie-Duels-Ordner " +
                   (regelwerkOrdnerErkannt() ? "erkannt" : "nicht erkannt") + "), g_spskill " +
                   std::to_string(regelwerkSpielstufe()));
        // Neu aufbauen: yawSpeed je Klasse und Schwierigkeit (NPC_Begin),
        // Schwertregeln, Spielfenster - alles, was am Regelwerk haengt.
        g_app->camTrackValid = false;
        g_app->mapDirty = true;
    }
    return true;
}

int schwierigkeitJetzt() { return schwierigkeitWirksam(g_app->settings.schwierigkeit, regelwerkMovieDuels()); }

std::string regelwerkStatusZeile() {
    const bool md = regelwerkMovieDuels();
    const char* name = md ? "Movie Duels" : "OpenJK";
    char buf[256];
    if (g_app->settings.regelwerk == kRegelwerkAuto) {
        std::snprintf(buf, sizeof(buf), tr(md ? Str::RegelStatusAutoMd : Str::RegelStatusAutoOjk), name);
    } else {
        std::snprintf(buf, sizeof(buf), tr(Str::RegelStatusFest), name);
    }
    return buf;
}

}  // namespace bhed::gui

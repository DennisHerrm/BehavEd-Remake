// roff.h - ROFF-Dateien (*.rof): aufgezeichnete Bahnen fuer Entities
//
// `play ( "PLAY_ROFF", "kor2/roffs/boulder" );` laesst eine Entity einer
// Bahn folgen, die in 3ds max aufgezeichnet wurde: je Bild ein Versatz im
// Ort und einer in den Winkeln. So fliegen die Schiffe in t2_wedge, so
// stuerzen die Deckenteile in vjun3, so rollt der Felsbrocken in kor2. In
// den Skripten, die Movie Duels mitbringt, steht PLAY_ROFF 115 mal - bisher
// ueberging behaved `play` ganz, und alles davon stand still.
//
// Nachgebaut nach code/game/g_roff.cpp (Movie-Duels-Quelltext):
//
//   G_LoadRoff    Pfad "scripts/<name>.rof" (Q3_SCRIPT_DIR, Q3_Interface.h:873)
//   G_ValidRoff   Kennung "ROFF", Fassung 1 mit float-Anzahl > 0 oder
//                 Fassung 2 mit int-Anzahl > 0
//   G_InitRoff    Fassung 1: 12 Byte Kopf, je Bild 24 Byte, fest 100 ms
//                 Fassung 2: 20 Byte Kopf, je Bild 32 Byte, dann die
//                 Notizen als nullterminierte Zeichenketten
//   G_Roff        je Bild EIN Versatz, angewandt im Takt der Spiellogik
//
// Die Engine prueft beim Lesen KEINE Groesse - sie liest ueber das Ende
// hinaus, wenn die Datei luegt. Hier wird jede Angabe gegen die Laenge der
// Daten geprueft, bevor sie benutzt wird.
#ifndef BHED_ROFF_H
#define BHED_ROFF_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace bhed {

// ROFF_VERSION / ROFF_VERSION2, g_roff.h:29 f.
inline constexpr int kRoffFassung1 = 1;
inline constexpr int kRoffFassung2 = 2;
// Ein Bild der Spiellogik (sv_fps 20). G_Roff laeuft einmal je Bild aus
// G_RunFrame (g_main.cpp:2350) - schneller als so kann keine Bahn laufen,
// auch wenn die Datei es verlangt.
inline constexpr double kRoffSpielBildMs = 50.0;

struct RoffBild {
    float ort[3]{};       // origin_delta
    float winkel[3]{};    // rotate_delta (Nicken, Gieren, Rollen)
    // Nur Fassung 2: mStartNote / mNumNotes. -1/0 heisst: keine Notiz.
    int ersteNotiz = -1;
    int notizen = 0;
};

struct Roff {
    int fassung = 0;                 // 1 oder 2
    std::vector<RoffBild> bilder;
    // mFrameTime: Abstand zweier Bilder in ms. Fassung 1 fest 100
    // (g_roff.cpp:329), Fassung 2 aus dem Feld mFrameRate - das trotz
    // seines Namens Millisekunden je Bild traegt, keine Bilder je Sekunde.
    int bildMs = 100;
    // mLerp = 1000 / mFrameTime, GANZZAHLIG geteilt (g_roff.cpp:374). Damit
    // rechnet G_Roff die Geschwindigkeit der Mover - bei 42 ms kommt 23
    // heraus statt 23,8, und das bleibt so.
    int lerp = 10;
    // mNoteTrackIndexes: die Notizen, in Dateireihenfolge.
    std::vector<std::string> notizen;
    // Auffaelligkeiten, die die Engine hinnimmt (Takt unter 50 ms, eine
    // Notiz ohne Endnull, Zahlen, die keine sind). Zum Melden.
    std::vector<std::string> hinweise;
};

// Aus den Bytes einer .rof lesen. false mit Grund in `fehler`, wenn die
// Engine sie ablehnen wuerde (Kennung, Fassung, Anzahl <= 0) oder wenn sie
// kuerzer ist, als ihr Kopf behauptet.
[[nodiscard]] bool leseRoff(const std::string& daten, Roff& out, std::string* fehler = nullptr);

// Ein Stand der Entity: Ort und Winkel, absolut.
struct RoffStand {
    float ort[3]{};
    float winkel[3]{};
};

// Die Bahn einer Entity: der Stand VOR jedem Bild, so aufaddiert, wie die
// Engine es tut - Bild fuer Bild in float, ausgehend vom ABSOLUTEN Stand
// beim play-Befehl (VectorAdd( ent->pos1, org, ent->pos1 ),
// g_roff.cpp:593; bei Figuren auf s.pos.trBase, ebenda:549).
//
// Warum nicht einmal die Versaetze summieren und den Start dazulegen? Weil
// float bei 7000 Einheiten nur noch auf rund ein Tausendstel genau ist -
// ueber die 3800 Bilder von skiff_roff1 laeuft eine andere Rundung sonst
// sichtbar auseinander. So rundet es genau wie im Spiel.
//
// stand[k] ist der Stand vor Bild k, stand[0] der Start; also
// bilder.size() + 1 Eintraege.
struct RoffBahn {
    std::vector<RoffStand> stand;
};
[[nodiscard]] RoffBahn roffBahn(const Roff& r, const float startOrt[3], const float startWinkel[3]);

// Der Archivpfad zu einem Namen aus dem Skript:
//     sprintf( file, "%s/%s.rof", Q3_SCRIPT_DIR, fileName );   (g_roff.cpp:437)
// Ohne jede Aufbereitung - ein Name mit ".rof" am Ende sucht auch in der
// Engine "x.rof.rof". Nur '\\' wird zu '/', wie es das Dateisystem der
// Engine beim Suchen ohnehin tut (FS_ReplaceSeparators).
[[nodiscard]] std::string roffPfad(const std::string& name);

// --- Der Zeitplan ----------------------------------------------------------
//
// Alle Zeiten in ms NACH dem play-Befehl.
//
// Play setzt next_roff_time = level.time (Q3_Interface.cpp:10701). Der
// Befehl laeuft aber im ICARUS-Schritt der Entity, und der kommt in
// G_RunFrame NACH G_Roff (g_main.cpp:2350 gegen 2586 bzw. G_RunThink
// 2601). Das erste Bild faellt deshalb ins NAECHSTE Spielbild: 50 ms.
// Danach next_roff_time = level.time + mFrameTime, gefragt wird je
// Spielbild - ein Bild kommt also im ersten Spielbild, das nicht vor
// seiner Zeit liegt. Bei 50 ms genau je Spielbild eines, bei 42 ms
// (md_arena/roll.rof) ebenso, bei 66 ms nur jedes zweite.
[[nodiscard]] double roffBildZeitMs(const Roff& r, std::size_t k);

// Wann das Bild angewandt wird, mit dem die Bahn endet. In genau diesem
// Spielbild ruft G_Roff Q3_TaskIDComplete( ent, TID_MOVE_NAV ) - danach
// geht ein dowait auf die Aufgabe weiter. Ohne Bilder: das erste
// Spielbild.
[[nodiscard]] double roffLaufzeitMs(const Roff& r);

// Der Stand, so wie ihn das Bild zeigt, `msSeitPlay` nach dem Befehl.
// `bahn` kommt aus roffBahn() fuer dieselbe Datei. Vor dem ersten Bild und
// ohne Bilder: der Start.
//
// Die Engine hat ZWEI Wege (g_roff.cpp, G_Roff):
//
//   figur = false   Mover, Modelle, alles ohne client. s.pos/s.apos werden
//                   TR_LINEAR ab pos1/pos2 mit trDelta = Versatz * mLerp;
//                   die cgame rechnet das zu jeder Zeit aus
//                   (EvaluateTrajectory). Beim LETZTEN Bild setzt G_Roff
//                   trBase auf den Stand VOR diesem Bild und loescht
//                   trDelta gleich wieder - der letzte Versatz wird also
//                   nie sichtbar. So steht es da, und so bleibt es hier.
//   figur = true    NPC und Spieler. trBase += Versatz, TR_INTERPOLATE:
//                   ein Sprung je Bild, den die cgame zwischen zwei
//                   Schnappschuessen glaettet (CG_CalcEntityLerpPositions,
//                   cg_ents.cpp:2037 ff., cg.time liegt zwischen snap und
//                   nextSnap). Hier: der Spielstand am 50-ms-Raster davor
//                   und danach, dazwischen geradlinig.
[[nodiscard]] RoffStand roffStand(const Roff& r, const RoffBahn& bahn, double msSeitPlay, bool figur);

// Der Stand, bei dem die Bahn stehen bleibt: bei Movern der vor dem
// letzten Bild (siehe oben), bei Figuren der nach dem letzten.
[[nodiscard]] RoffStand roffEndstand(const Roff& r, const RoffBahn& bahn, bool figur);

// Wo die Entity steht, wenn G_Roff die Notiz von Bild k auswertet: der
// Rueckruf kommt VOR dem Weiterstellen und liest s.pos.trBase /
// s.apos.trBase (g_roff.cpp:517). Bei Movern ist das der Stand vor Bild
// k-1 (trBase wurde im vorigen Bild auf pos1 VOR dessen Versatz gesetzt),
// bei Figuren der vor Bild k.
[[nodiscard]] RoffStand roffStandVorBild(const Roff& r, const RoffBahn& bahn, std::size_t k, bool figur);

// --- Notizen ---------------------------------------------------------------
//
// Welche Notiz in welchem Bild ausgeloest wird. G_Roff ruft den Rueckruf
// genau EINMAL je Bild, und zwar mit mNoteTrackIndexes[mStartNote] - ein
// mNumNotes > 1 wird ignoriert (g_roff.cpp:515 ff.). Bedingung ist
// "mStartNote != -1 || mNumNotes". Zeigt mStartNote ausserhalb der Liste,
// liest die Engine fremden Speicher; hier faellt die Notiz weg.
struct RoffAusloesung {
    std::size_t bild = 0;
    double ms = 0.0;        // nach dem play-Befehl
    std::size_t notiz = 0;  // Index in Roff::notizen
};
[[nodiscard]] std::vector<RoffAusloesung> roffAusloesungen(const Roff& r);

// Eine Notiz, zerlegt wie in G_RoffNotetrackCallback (g_roff.cpp:37 ff.):
//
//     effect <datei> [<vorn>+<rechts>+<oben> [<nicken>-<gieren>-<rollen>]]
//     sound <datei>
//
// Mit allen Eigenheiten des Originals: Winkel trennt '-', ein negativer
// Winkel geht darum nicht; fehlt einer der drei, gelten die Winkel der
// Entity; ein fuehrendes "/" und ein erster Ordner, der "effects" enthaelt,
// fallen vom Effektnamen weg (den setzt die Engine selbst davor).
struct RoffNotizBefehl {
    enum class Art : std::uint8_t {
        Keine,       // kein Leerzeichen nach dem Typ oder leeres Argument: nichts
        Effekt,      // G_PlayEffect
        Klang,       // cgi_S_StartSound auf CHAN_BODY
        Unbekannt,   // "is an invalid ROFF notetrack function"
    };
    Art art = Art::Keine;
    std::string typ;
    // Effekt: der Name, wie G_EffectIndex ihn bekommt. Klang: der Name fuer
    // G_SoundIndex.
    std::string datei;
    float versatz[3]{};        // vorn, rechts, oben - in den Achsen der Winkel
    bool eigeneWinkel = false; // sonst die Winkel der Entity
    float winkel[3]{};
};
[[nodiscard]] RoffNotizBefehl zerlegeRoffNotiz(const std::string& text);

// Wo und wie ein Effekt spielt: der Ursprung plus Versatz entlang
// AngleVectors der benutzten Winkel (g_roff.cpp:268 ff.).
void roffEffektOrt(const RoffNotizBefehl& b, const float basisOrt[3], const float basisWinkel[3],
                   float ort[3], float winkel[3]);

}  // namespace bhed

#endif

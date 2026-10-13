// roffanim.h - eigene ROFF-Bahnen: Schluessel im Skript <-> .rof-Datei
//
// Gewuenscht (13.10.2026): "ich will das man auch direkt eine Roff datei
// bauen und aendern kann".
//
// Die Datei selbst traegt nur Versaetze je Bild - zum Bearbeiten braucht es
// die Schluessel. Die stehen als Kommentarzeile im Skript, direkt vor dem
// Befehl, der die Bahn abspielt:
//
//     //(BehavEd) roff "md_x/roffs/schiff" weich 50 | 0 0 0 0 0 0 0 | 2000 300 0 40 0 90 0
//     affect ( "schiff", /*@AFFECT_TYPE*/ FLUSH )
//     {
//         wait ( 1450.000 );
//         play ( /*@PLAY_TYPES*/ "PLAY_ROFF", "md_x/roffs/schiff" );
//     }
//
// Je Schluessel sieben Zahlen: ms ab dem ersten Bild, Ort und Winkel
// RELATIV zum Start - die Bahn beginnt im Spiel dort, wo die Entity beim
// play steht (g_roff.cpp, pos1 = currentOrigin), und so verschiebt sie
// sich mit ihr. Notizen folgen als  note <ms> "<text>".
//
// Die .rof wird aus dieser Zeile gebacken (roffAusAnim). Damit gehoert die
// Bahn zum Skript: Rueckgaengig, Kopieren und Speichern nehmen sie mit,
// und beim Speichern entsteht die Datei daraus. Das Spiel liest den
// Kommentar nicht; IBIze und behaved ueberspringen ihn.
//
// Zwei Formen:
//   * der eigene Block (oben) - eine neue Bahn fuer eine Entity, am Anfang
//     des Skripts wie die move-Animation (bhed/moveranim.h)
//   * die Marke vor einem vorhandenen play irgendwo im Skript - eine
//     bearbeitete Kopie einer fertigen Datei; das play zeigt dann auf sie
#ifndef BHED_ROFFANIM_H
#define BHED_ROFFANIM_H

#include "bhed/roff.h"
#include "bhed/script.h"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace bhed {

struct RoffAnim {
    std::string datei;   // wie im play-Befehl, ohne scripts/ und .rof
    bool weich = true;
    int bildMs = 50;
    // ms ab dem ersten Bild; Ort/Winkel relativ zum Start (der erste
    // Schluessel steht normalerweise auf 0).
    std::vector<RoffSchluessel> schluessel;
    std::vector<RoffNotiz> notizen;
};

// Die Kommentarzeile (ohne Einzug).
[[nodiscard]] std::string roffMarke(const RoffAnim& a);
// Eine Zeile lesen (Einzug erlaubt). false, wenn es keine Marke ist.
[[nodiscard]] bool leseRoffMarke(const std::string& zeile, RoffAnim& out);

// Die Datei aus den Schluesseln.
[[nodiscard]] Roff roffAusAnim(const RoffAnim& a);

// --- Der eigene Block ---------------------------------------------------------
// playMs: wann (ab Skriptstart) das play laeuft. Das erste Bild folgt ein
// Spielbild spaeter (roffBildZeitMs).
[[nodiscard]] std::vector<Node> baueRoffBlock(const std::string& entity, double playMs, const RoffAnim& a);
// Index der Marke auf oberster Ebene, hinter der ein affect auf `entity`
// mit dem play ihrer Datei steht.
[[nodiscard]] std::optional<std::size_t> findeRoffBlock(const Script& s, const std::string& entity);
[[nodiscard]] bool leseRoffBlock(const Script& s, std::size_t index, RoffAnim& out, double& playMs);
// Neues Skript mit dem Block (ersetzt den alten oder steht am Anfang);
// a == nullptr: der Block faellt weg.
[[nodiscard]] Script mitRoffBlock(const Script& s, const std::string& entity, double playMs, const RoffAnim* a);
// Derselbe Block, gesucht nach seiner DATEI statt nach dem Namen der Entity -
// heisst die Entity inzwischen anders, bleibt er so auffindbar (rc599).
[[nodiscard]] std::optional<std::size_t> findeRoffBlockNachDatei(const Script& s, const std::string& datei);
// Den Block bei `index` (Marke + affect) ersetzen - mit dem jetzigen Namen
// der Entity; a == nullptr: entfernen.
[[nodiscard]] Script mitRoffBlockBei(const Script& s, std::size_t index, const std::string& entity, double playMs,
                                     const RoffAnim* a);

// --- Marken irgendwo im Skript ------------------------------------------------
struct RoffMarkeFund {
    Path path;   // die Kommentarzeile
    RoffAnim anim;
};
[[nodiscard]] std::vector<RoffMarkeFund> alleRoffMarken(const Script& s);
// Nach der Datei, ohne Gross/Klein (und mit \ wie /).
[[nodiscard]] std::optional<RoffMarkeFund> findeRoffMarke(const Script& s, const std::string& datei);
// Die Marke vor das play bei `playPfad` setzen (oder die schon davor
// stehende ersetzen) und das play auf a.datei zeigen lassen.
[[nodiscard]] Script mitRoffMarkeVorPlay(const Script& s, const Path& playPfad, const RoffAnim& a);

// Gleiche Dateinamen? (wie die Engine sucht: ohne Gross/Klein, \ = /)
[[nodiscard]] bool gleicheRoff(const std::string& a, const std::string& b);

}  // namespace bhed
#endif

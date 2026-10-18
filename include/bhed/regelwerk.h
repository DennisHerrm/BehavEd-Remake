// regelwerk.h - rc601 integ: der zentrale Schalter "OpenJK / Movie Duels".
//
// Der Betreuer: "es soll genauso laufen wie im Spiel" - und gespielt wird
// beides: reines Jedi Academy mit OpenJK und Movie Duels
// (MovieDuels-SJE-SP-Rend2), dessen Spielcode an vielen Stellen anders
// rechnet. Entschieden: BEIDE Regelwerke, waehlbar in den Einstellungen;
// die Vorgabe "automatisch" nimmt Movie Duels, wenn ein Movie-Duels-Ordner
// unter den Spielordnern ist, sonst OpenJK (im Zweifel OpenJK).
//
// Diese Datei ist die EINE Stelle, an der das Regelwerk gilt. Die Pakete
// fragen ihre bisherigen Schalter (saberMovieDuelsRegeln, navMovieDuelsRegeln)
// weiter ab - regelwerkAnwenden setzt sie alle zugleich. Was sich je
// Regelwerk unterscheidet, steht in NOTIZEN_integ.md (§ Regelwerk) mit den
// Engine-Stellen.
#ifndef BHED_REGELWERK_H
#define BHED_REGELWERK_H

#include <string>

namespace bhed {

// Die Einstellung (Settings::regelwerk, Schluessel "regelwerk").
inline constexpr int kRegelwerkAuto = 0;         // "auto": nach den Spielordnern
inline constexpr int kRegelwerkOpenJK = 1;       // "openjk"
inline constexpr int kRegelwerkMovieDuels = 2;   // "movieduels"

// "auto", "openjk", "movieduels" - so steht es in behaved.cfg.
[[nodiscard]] const char* regelwerkSchluessel(int wahl) noexcept;
// Umgekehrt; false fuer alles andere (dann bleibt die Vorgabe).
[[nodiscard]] bool regelwerkAusSchluessel(const std::string& text, int& wahl) noexcept;

// Die Wahl aufloesen: true = Movie Duels. "auto" folgt der Erkennung des
// Spielordners; ist keiner erkannt (unklar), gilt OpenJK.
[[nodiscard]] bool regelwerkAufloesen(int wahl, bool movieDuelsErkannt) noexcept;

// Der aktive Stand. Ohne Aufruf von regelwerkAnwenden: Movie Duels (die
// Unit-Tests der Pakete rechnen damit).
[[nodiscard]] bool regelwerkMovieDuels() noexcept;
// Setzt das Regelwerk fuer alle Pakete (saber, bewegung) und die wirksame
// Schwierigkeit (g_spskill, siehe schwierigkeitWirksam) zugleich.
void regelwerkAnwenden(bool movieDuels, int schwierigkeitEinstellung) noexcept;
// g_spskill, wie es gerade gilt (0..3).
[[nodiscard]] int regelwerkSpielstufe() noexcept;

// --- Was je Regelwerk anders vorgegeben ist -------------------------------

// g_spskill: Movie Duels "2" (g_main.cpp:752), OpenJK "0" (g_main.cpp:631).
// Die Einstellung -1 heisst "nie gewaehlt" - dann gilt die Vorgabe des
// Regelwerks; eine ausdrueckliche Wahl 0..3 bleibt.
[[nodiscard]] int schwierigkeitWirksam(int einstellung, bool movieDuels) noexcept;

// cg_fovAspectAdjust: Movie Duels "1" (cg_main.cpp:555), OpenJK "0"
// (cg_main.cpp:355). einstellung -1 = automatisch (nach dem Regelwerk).
[[nodiscard]] bool fovAspectAdjustWirksam(int einstellung, bool movieDuels) noexcept;

// Seitenverhaeltnis des Spielfensters. einstellung > 0 gilt wie gewaehlt;
// 0 = wie das Spiel ab Werk: Movie Duels r_mode "-2" (Desktopaufloesung,
// sdl_window.cpp:779 MD) -> bildschirm; OpenJK r_mode "4" (800x600,
// sdl_window.cpp:746) -> 4:3.
[[nodiscard]] float spielSeitenWirksam(float einstellung, bool movieDuels, float bildschirm) noexcept;

// ROFF-Notiz "sound" (G_RoffNotetrackCallback): Movie Duels spielt sie immer
// fest am Ort, cgi_S_StartSound(cent->s.pos.trBase, ...) (g_roff.cpp:262-266
// MD). OpenJK nur fuer Mover (ET_MOVER, g_roff.cpp:298-302), sonst
// G_SoundOnEnt (g_roff.cpp:305) - der Klang geht mit der Entity.
[[nodiscard]] bool roffKlangFolgtEntity(bool movieDuels, bool mover) noexcept;

}  // namespace bhed

#endif

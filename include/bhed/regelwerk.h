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
#include <vector>

namespace bhed {

// Die Einstellung (Settings::regelwerk, Schluessel "regelwerk").
inline constexpr int kRegelwerkAuto = 0;         // "auto": nach den Spielordnern
inline constexpr int kRegelwerkOpenJK = 1;       // "openjk"
inline constexpr int kRegelwerkMovieDuels = 2;   // "movieduels"
// rc604: das echte Jedi Outcast (JK2) - Vorlage ist der JK2-Spielcode in
// OpenJK (codeJK2/). Eigene Befehlsliste (data/jk2), eigene Spielordner,
// das JK2-Skelett (72 Knochen), ext_data/NPCs.cfg, Schwerter ohne .sab und
// die SP_NPC_*-Klassen von codeJK2. Laufen, Wege und Kamera rechnen
// vorerst wie OpenJK (Stufe 3 - siehe NOTIZEN_jk2.md).
inline constexpr int kRegelwerkJediOutcast = 3;  // "jedioutcast"

// "auto", "openjk", "movieduels" - so steht es in behaved.cfg.
[[nodiscard]] const char* regelwerkSchluessel(int wahl) noexcept;
// Umgekehrt; false fuer alles andere (dann bleibt die Vorgabe).
[[nodiscard]] bool regelwerkAusSchluessel(const std::string& text, int& wahl) noexcept;

// Die Wahl aufloesen: true = Movie Duels. "auto" folgt der Erkennung des
// Spielordners; ist keiner erkannt (unklar), gilt OpenJK.
[[nodiscard]] bool regelwerkAufloesen(int wahl, bool movieDuelsErkannt) noexcept;
// rc604: dasselbe mit drei Spielen - kRegelwerkOpenJK, kRegelwerkMovieDuels
// oder kRegelwerkJediOutcast. "auto": Movie Duels vor Jedi Outcast (ein
// Movie-Duels-Ordner ist nie JK2), unklar = OpenJK.
[[nodiscard]] int regelwerkSpiel(int wahl, bool movieDuelsErkannt, bool jediOutcastErkannt) noexcept;

// rc604: ist ein Spielordner Jedi Outcast? `dateien` sind die Dateinamen
// (klein, ohne Pfad) im Ordner UND im Ordner darueber (GameData), `jk2Karte`:
// eine .pk3 des Ordners hat eine Karte, die es nur in JK2 gibt
// (maps/kejim_post.bsp). Ein Programm von JK2 (jk2sp.exe, jk2mp.exe,
// openjo_sp.*) entscheidet; eines von Jedi Academy (jasp.exe, jamp.exe,
// openjk_sp.*, openjk.*) schliesst JK2 aus - eine JKA-Installation mit
// nachgebauten JK2-Karten bleibt JKA. Sonst die Karte.
[[nodiscard]] bool jediOutcastHinweis(const std::vector<std::string>& dateien, bool jk2Karte);

// Der aktive Stand. Ohne Aufruf von regelwerkAnwenden: Movie Duels (die
// Unit-Tests der Pakete rechnen damit).
[[nodiscard]] bool regelwerkMovieDuels() noexcept;
// Setzt das Regelwerk fuer alle Pakete (saber, bewegung) und die wirksame
// Schwierigkeit (g_spskill, siehe schwierigkeitWirksam) zugleich.
// rc604: setzt Jedi Outcast zurueck (siehe regelwerkAnwendenSpiel).
void regelwerkAnwenden(bool movieDuels, int schwierigkeitEinstellung) noexcept;
// rc604: mit dem aufgeloesten Spiel (regelwerkSpiel). Jedi Outcast rechnet
// in den Paketen wie OpenJK (JK2_MODE-Unterschiede: Stufe 3) und schaltet
// das Ghoul2-Skelett auf JK2 (setzeGhoul2Jk2, bhed/glm.h).
void regelwerkAnwendenSpiel(int spiel, int schwierigkeitEinstellung) noexcept;
// rc604: gilt gerade Jedi Outcast? Ohne regelwerkAnwendenSpiel: nein.
[[nodiscard]] bool regelwerkJediOutcast() noexcept;
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
// rc604 fix604b (A2): ROFF-Notizen wie OpenJK - alle Notizen eines Bilds, dazu
// "USE" und "loop" (code/game/g_roff.cpp:308-438, 746-752 OpenJK). Movie Duels
// und Jedi Outcast feuern nur die erste und kennen nur effect/sound.
[[nodiscard]] bool roffNotizenOpenJk() noexcept;

}  // namespace bhed

#endif

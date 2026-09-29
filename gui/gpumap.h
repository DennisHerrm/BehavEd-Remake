// gpumap.h - die Karte ueber die Grafikkarte zeichnen
//
// Der Zustand
// -----------
// Von den fuenf Teilen des GPU-Wegs sind vier geprueft (gpustate, gpushader,
// gpudraw, gpuconst). Der fuenfte - gui/gpumap_win32.cpp - fasst Direct3D an
// und ist beim Schreiben weder uebersetzt noch ausgefuehrt worden.
//
// Deshalb: `verfuegbar()` sagt, ob ueberhaupt ein Geraet dasteht, und der
// Aufrufer entscheidet. Ein GPU-Weg, der sich selbst einschaltet, waere
// nicht abnehmbar - man saehe ein Bild und wuesste nicht, ob es richtig ist.
#ifndef BHED_GUI_GPUMAP_H
#define BHED_GUI_GPUMAP_H

#include <cstdint>
#include <string>
#include <vector>

#include "bhed/bspgeo.h"
#include "bhed/gla.h"
#include "bhed/glm.h"
#include "bhed/gpudraw.h"
#include "bhed/mapview.h"

namespace bhed::gpu {

// Steht ein Direct3D-Geraet bereit? false unter OpenGL und vor dem Start.
[[nodiscard]] bool verfuegbar();

// Alle angelegten Puffer, Shader und Zustaende freigeben. Darf mehrfach
// gerufen werden.
void shutdown();

// Alles vergessen, was auf der Grafikkarte an der KARTE haengt: Texturen
// und Lightmaps (nach NUMMER abgelegt - nach einem Wechsel meint dieselbe
// Nummer ein anderes Bild), den Himmel und die Puffer der Mover-Netze. Zu
// rufen, wann immer textures oder geo neu gebaut werden.
void vergissKarte();

// Nur die Puffer der Mover-Netze freigeben (Schluessel ist die Adresse des
// Netzes). Zu rufen, wann immer brushMeshes geleert wird - sonst bleiben
// die alten Puffer bis zum Beenden liegen.
void vergissMoverNetze();

// Den Puffer EINES Netzes freigeben - fuer die Kartenmodelle (md3Meshes),
// die bei jedem Szenenaufbau neu entstehen, also auch nach einer
// Skriptaenderung, nicht nur beim Kartenwechsel.
void vergissNetz(const BspMesh* netz);

// Wie oft seit dem Start ein Karten-Bild im Speicher lag, dessen Quelle
// nicht mehr passte (dann wird neu hochgeladen). Mit vergissKarte() an den
// richtigen Stellen bleibt das 0 - der Selbsttest (allekarten) prueft es,
// damit ein vergessener Aufruf auffaellt.
[[nodiscard]] int veralteteKartenBilder();

// Wie viel gerade belegt ist - fuer den Selbsttest, der ueber alle Karten
// nachsieht, ob etwas mit jeder Karte waechst.
struct SpeicherStand {
    int bilder = 0;          // Karten-Texturen
    int lightmaps = 0;
    int moverNetze = 0;
    int figuren = 0;         // Figurenpuffer (nach Modell)
    int figurBilder = 0;
    long long grafikBytes = -1;    // DXGI: belegter Grafikspeicher, -1 = unbekannt
    long long prozessBytes = -1;   // private Bytes des Prozesses, -1 = unbekannt
};
[[nodiscard]] SpeicherStand speicherStand();

// Die Karte zeichnen.
//
// Gibt zurueck, wie viele Aufrufe abgesetzt wurden - 0 heisst, es ist nichts
// passiert (kein Geraet, kein Netz, oder ein Shader liess sich nicht
// uebersetzen). `fehler` bekommt dann eine Meldung, falls angegeben.
//
// `viewProj` ist die fertige Matrix, zeilenweise (16 float). Sie wird HIER
// nicht gerechnet: die Kameramathematik steckt im Rasterer, und zwei
// Fassungen davon waeren zwei Bilder.
//
// Die Rueckgabe ist wichtig: ein GPU-Weg, der still nichts tut, sieht aus
// wie eine schwarze Karte, und dann sucht man am falschen Ende.
//  wird fuer die LIGHTMAPS gebraucht: die liegen in
// BspGeometry::lightmaps, nicht im Texturensatz. Die Verwechslung war der
// erste Fehler des GPU-Wegs - jede Flaeche mit Lightmap kam schwarz heraus.
// Einen Mover zeichnen - Tuer, Plattform, Schiff.
//
// `welt` ist die Stellungsmatrix aus gpu::baueMoverWelt, zeilenweise. Muss
// NACH der Karte kommen, damit die Tiefe stimmt.
//
// Mover waren beim GPU-Umbau uebersehen worden: sie stehen in renderMap als
// zusaetzliche Quellen, und nachgebaut hatte ich nur den Hauptdurchgang.
// Gemeldet als "die Tueren verschwinden".
// Die Kameraachsen muessen mit, obwohl ein Mover sie nicht braucht:
// `JeBild` ist EIN Puffer fuer alle Aufrufe eines Bildes, und wer sie
// weglaesst, LOESCHT sie fuer die folgenden Durchgaenge.
// `lage` teilt das Bild in zwei Durchgaenge: erst alles Deckende ueber ALLE
// Quellen, dann alles Gemischte. Ohne das stehen die gemischten
// Kartenflaechen vor den deckenden Tueren - siehe Lage in gpudraw.h.
// Lage::Alles ist der alte Ablauf und bleibt zum Vergleichen erhalten.
int zeichneMover(const BspMesh& mesh, const TextureSet* textures,
                 const BspGeometry* geo, const float* viewProj,
                 const float* welt, const float* kameraPos,
                 const float* vorn, const float* rechts, const float* hoch,
                 float fokus, float zeitSekunden, float helligkeit,
                 float grundlicht, std::string* fehler,
                 int* uebersprungenAus = nullptr,
                 Lage lage = Lage::Alles);

int zeichneKarte(const BspMesh& mesh, const TextureSet* textures,
                 const BspGeometry* geo, const float* viewProj,
                 const float* kameraPos, float zeitSekunden,
                 float helligkeit, float grundlicht, int netzId,
                 const float* vorn, const float* rechts, const float* hoch,
                 float fokus, std::string* fehler,
                 int* uebersprungenAus = nullptr,
                 Lage lage = Lage::Alles);

// Eine Figur zeichnen.
//
// Muss NACH zeichneKarte kommen: das Renderziel wird nur dort geloescht, und
// die Figur soll auf die Tiefe der Karte treffen.
//
// `welt` ist die 4x4-Matrix, die die Figur in der Welt stellt (Drehung um
// die Hochachse und Verschiebung), zeilenweise. Sie wird HIER nicht
// gerechnet - der Rasterer hat seine eigene Fassung, und zwei davon waeren
// zwei Stellungen.
//
// Gibt die Zahl der Zeichenaufrufe zurueck, 0 bei einem Fehler.
//
// `licht`: das Licht an der Stelle der Figur, schon mal Helligkeit und auf
// 0..1 gebracht. nullptr oder `gitter == false`: die Schattierung aus der
// Normalen, wie ohne Lichtgitter im Rasterer.
// `kappen`: auch die Kappenflaechen (_cap_*) zeigen - App::showCaps.
struct FigurLicht {
    float umgebung[3]{};
    float gerichtet[3]{};
    float richtung[3]{0.0F, 0.0F, 1.0F};
    bool gitter = false;
};
int zeichneFigur(const GlmModel& model, const ModelTextures* textures,
                 const std::vector<BoneMatrix>& knochen, const float* welt,
                 const float* viewProj, std::string* fehler,
                 const FigurLicht* licht = nullptr, bool kappen = false);

// Die Klingen der Lichtschwerter, nach allen Figuren: additiv, testen die
// Tiefe, schreiben sie nicht (wie im Rasterer und in CG_DoSaber).
struct KlingenQuad {
    float ecke[4][3]{};
    float farbe[3]{};                       // 0..255, wenn das Bild fehlt
    const TextureSet::Tex* tex = nullptr;   // blue_glow2 / blue_line
};
int zeichneKlingen(const std::vector<KlingenQuad>& quads, const float* viewProj,
                   std::string* fehler);

// --- Das Modellfenster ----------------------------------------------------
//
// Ein EIGENES Renderziel, denn beide Bilder stehen im selben Bild auf dem
// Schirm. Bis hierher zeichnete das Modellfenster der Rasterer
// (renderModel); der ist entfernt.
[[nodiscard]] bool bereiteModellZiel(int breite, int hoehe);
[[nodiscard]] void* modellZielTextur();
// Hintergrund, dann das Modell. Flaechen ohne Bild bekommen die helle
// Ersatzfarbe des alten Betrachters (226, 220, 208).
int zeichneModellAnsicht(const GlmModel& model, const ModelTextures* textures,
                         const std::vector<BoneMatrix>& knochen,
                         const float* viewProj, bool kappen, std::string* fehler);

// Das Gluehen auflegen.
//
// Zeichnet die leuchtenden Stapel ein zweites Mal in einen kleinen Puffer,
// zeichnet ihn weich und legt ihn auf das Renderziel. Muss NACH allem
// anderen kommen.
//
// `weich` entspricht dem `soft` des Rasterers (src/glow.cpp:255).
int zeichneGluehen(const BspMesh& mesh, const TextureSet* textures,
                   const BspGeometry* geo, const float* viewProj,
                   float zeitSekunden, int breite, int hoehe, bool weich,
                   std::string* fehler);

// Das Renderziel auf die gewuenschte Groesse bringen. Legt beim ersten Mal
// und bei jeder Groessenaenderung neu an.
//
// Getrennt von zeichneKarte, weil die Groesse an der Ansicht haengt und
// nicht am Bild: waehrend der Bewegung wird groeber gerastert
// (App::mapScale), und dann aendert sich die Groesse mehrfach je Sekunde.
[[nodiscard]] bool bereiteZiel(int breite, int hoehe);

// Die Textur des Renderziels, zum Anzeigen. nullptr, wenn keins da ist.
//
// Als void*, damit dieser Kopf ohne Direct3D auskommt - er wird auch unter
// Linux gelesen.
[[nodiscard]] void* zielTextur();

// Den Tiefenpuffer des letzten Bildes lesen, umgerechnet in den ABSTAND zur
// Kamera entlang der Blickrichtung - dieselbe Groesse, die der Rasterer in
// MapImage::depth ablegt. Fuer die Ueberlagerung (Gizmos, Entitykreuze), die
// auf dem GPU-Weg sonst durch Waende scheinen wuerde. false, wenn nichts da
// ist oder die Groesse nicht passt.
[[nodiscard]] bool leseTiefe(std::vector<float>& abstand, int breite, int hoehe,
                             float nah, float fern);

// Das Farbbild des letzten Kartenbildes lesen (RGBA, Zeile fuer Zeile). Fuer
// den Selbsttest (ablauf), der das Bild nach einer Folge von Schritten mit
// dem nach frischem Laden vergleicht. false, wenn kein Bild da ist.
[[nodiscard]] bool leseFarbe(std::vector<std::uint8_t>& rgba, int& breite, int& hoehe);

// --- Zeitmarken auf der Grafikkarte -------------------------------------
//
// Die Zahl in der Statuszeile sagt, wie lange ein BILD dauert. Sie sagt
// nicht, WO. Bei fuenf Durchgaengen - Karte, Mover, Figuren, Effekte,
// Gluehen - ist das zu grob, um etwas zu entscheiden; die Frage "warum
// sehen die Effekte anders aus als im Rasterer" liess sich deshalb nur mit
// einem Eindruck beantworten, nicht mit einer Zahl.
//
// Gemessen wird mit D3D11_QUERY_TIMESTAMP: die Karte liest ihre eigene Uhr,
// wenn sie an dieser Stelle des Befehlsstroms ankommt. Dazu je Bild EIN
// D3D11_QUERY_TIMESTAMP_DISJOINT - der liefert die Taktfrequenz zum
// Umrechnen UND sagt, ob die Uhr zwischendurch verstellt wurde. Wurde sie
// das, sind die Werte des Bildes wertlos und werden verworfen.
//
// Die wichtigste Eigenschaft: die Werte stehen erst bereit, wenn die Karte
// den Abschnitt wirklich abgearbeitet hat - typisch zwei bis drei Bilder
// spaeter. Wer im selben Bild darauf wartet, misst das Warten und bremst
// nebenbei alles aus. Deshalb laeuft hier ein Ring ueber drei Bilder, und
// abgeholt wird nur, was schon fertig ist (GetData mit DO_NOT_FLUSH).
//
// Ohne Direct3D und auf Geraeten ohne Zeitmarken meldet `millisekunden()`
// einfach -1, und die Anzeige laesst die Spalte weg.
// Je Quelle ZWEI Abschnitte, weil das Bild seit rc432 zweimal ueber alle
// Quellen laeuft (deckend, dann gemischt). Ein Marken-Paar ueber beide
// Durchgaenge wuerde alles messen, was dazwischen liegt - die Zahl saehe
// plausibel aus und waere falsch.
//
// Die Anzeige zaehlt die beiden je Quelle zusammen; getrennt braucht sie
// niemand, aber gemessen werden muessen sie es.
enum class Abschnitt : int {
    KarteDeckend = 0,
    KarteGemischt,
    EffekteDeckend,
    EffekteGemischt,
    MoverDeckend,
    MoverGemischt,
    Figuren,
    Gluehen,
    kAnzahl
};

// Die beiden Lagen einer Quelle zusammengezaehlt. -1, wenn keine von beiden
// einen Wert hat; sonst die Summe der vorhandenen.
[[nodiscard]] float millisekundenBeide(Abschnitt a, Abschnitt b);

// Welche der beiden Zustandsarten aus rc434 sind eingeschaltet?
// Steuerbar ueber BHED_D3D_STATES (0 keine, 1 Tiefe, 2 Keulen, 3 beide).
// Gehoert in die Anzeige: ein Bild ohne diese Zahl kostet eine Runde.
[[nodiscard]] const char* zustandsLage();

// --- Was der Bericht braucht -------------------------------------------
//
// Jede Runde dieser Sitzung begann damit, dass ich shank um eine Zahl
// gebeten habe: welche Zustaende laufen, wie viele Aufrufe, was sagt die
// Debugschicht. Das gehoert in EINE Datei, die er anhaengen kann.

// Die Meldungen der Debugschicht, seit dem letzten Abruf.
//
// Die Schicht laeuft seit rc425 immer mit - GELESEN hat ihre Warteschlange
// bis rc447 niemand. Sie schreibt an den Windows-Debugger, und den sieht
// shank nicht. Jede Beanstandung von Direct3D seit rc425 war damit
// unsichtbar, obwohl sie erzeugt wurde.
//
// Gleiche Meldungen werden nur EINMAL gemeldet: die Schicht wiederholt
// dieselbe Beanstandung in jedem Bild, und ein Protokoll mit
// zweihunderttausend gleichen Zeilen ist so wertlos wie keines.
[[nodiscard]] std::vector<std::string> debugMeldungen();

// Welche Grafikkarte, wieviel Speicher, welche Merkmalsstufe.
//
// Der haeufigste Grund, warum ein Bild bei shank anders aussieht als
// erwartet, ist der Treiber - und dazu steht bisher nirgends etwas.
[[nodiscard]] std::string geraetInfo();

// Der GPU-Teil des Berichts: Geraet, Zustaende, Zaehler, Zeiten, Shader.
[[nodiscard]] std::string berichtText();

// --- Was der letzte Zeichendurchgang an BESONDEREN Stapeln hatte --------
//
// Bis rc456 sagte der Bericht viel ueber die Figuren und nichts ueber die
// Karte. Die schwarzen Vierecke auf dem Hologrammtisch waren aber
// KARTENflaechen (`holo1`, `holo11`, `holo111`) - drei Runden lang habe ich
// bei den Figuren gesucht, weil dort die Liste war.
//
// Gesammelt wird je Stapel, der eine der auffaelligen Eigenschaften traegt:
// autosprite, gluehend, oder eine Mischart ausserhalb der sieben bekannten.
// Alles andere ist gewoehnlich und fuellt den Bericht nur.
struct StapelNotiz {
    std::string shader;
    std::string was;      // "autosprite", "glow", "unbekannte Mischart"
    int anzahl = 0;
};
[[nodiscard]] std::vector<StapelNotiz> auffaelligeStapel();

// Name eines Abschnitts, fuer die Anzeige. Immer gueltig, auch fuer
// ungueltige Werte - eine Anzeige soll nicht abstuerzen koennen.
[[nodiscard]] const char* abschnittName(Abschnitt a);

// Klammer um ein Bild. `bildBeginnt` VOR dem ersten `beginneMessung`,
// `bildFertig` NACH dem letzten `beendeMessung`. Ohne diese Klammer wird
// gar nicht gemessen - es entsteht kein halber Messwert.
void bildBeginnt();
void bildFertig();

// Ein Abschnitt, paarweise zu rufen. Ein Abschnitt, der in diesem Bild
// nicht gezeichnet wird, bleibt einfach aus: er behaelt dann seinen letzten
// Wert, statt eine Null vorzutaeuschen.
void beginneMessung(Abschnitt a);
void beendeMessung(Abschnitt a);

// Die zuletzt vollstaendig gemessene Dauer in Millisekunden.
// -1, solange nichts Brauchbares vorliegt.
[[nodiscard]] float millisekunden(Abschnitt a);

}  // namespace bhed::gpu

#endif  // BHED_GUI_GPUMAP_H

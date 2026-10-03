// app_internal.h - der gemeinsame Zustand der Oberflaeche
//
// gui/app.cpp war 5138 Zeilen lang. Das ist die eine Stelle, an der jeder
// Aussenstehende zuerst haengenbleibt - und zu Recht: eine Datei, die alles
// enthaelt, laesst sich weder ueberblicken noch getrennt uebersetzen.
//
// Diese Kopfdatei haelt, was mehrere Teile brauchen: den Zustand (App), den
// Zeiger darauf und die Deklarationen der Funktionen, die ueber Dateigrenzen
// hinweg aufgerufen werden. Sie ist bewusst NICHT oeffentlich - nur die
// Dateien unter gui/ binden sie ein.
//
// Der Zustand bleibt ein einfacher Verbund mit einem globalen Zeiger. Das
// ist kein Versehen: das Programm hat genau ein Dokument, wie das Original,
// und eine Abhaengigkeitsspritze dafuer waere Zeremonie ohne Gewinn.
#ifndef BHED_GUI_APP_INTERNAL_H
#define BHED_GUI_APP_INTERNAL_H

// SplitterBehavior, ImRect und GetCurrentWindow stehen in der INTERNEN
// Kopfdatei von ImGui. Sie zu benutzen ist die Loesung, die ocornut selbst
// im Fehlerverzeichnis empfiehlt (Ausgabe 4357) - fuer Trenner gibt es
// keine oeffentliche Entsprechung.
//
// Seit rc499 auch fuer `ImGuiTable::ResizedColumn` (imgui_internal.h:3134,
// "Index of column being resized"). Drei Anlaeufe haben versucht, das
// Ziehen an einer Spaltengrenze zu ERRATEN - IsMouseDown, ein
// Breitenvergleich, ein Fenstergroessenvergleich. Alle drei waren Ersatz
// fuer eine Frage, die ImGui genau beantworten kann.
#include "imgui.h"
#include "imgui_internal.h"

#include "backend.h"
#include "audio_win32.h"
#include "icons_gen.h"
#include "platform.h"
#include "bhed/diag.h"
#include "bhed/ablauf.h"
#include "bhed/bsp.h"
#include "bhed/camtrack.h"
#include "bhed/mission.h"
#include "bhed/scene.h"
#include "bhed/timeline.h"
#include "bhed/image.h"
#include "bhed/gla.h"
#include "bhed/glm.h"
#include "bhed/mapview.h"
#include "bhed/efxdraw.h"
#include "bhed/mover.h"
#include "bhed/md3.h"
#include "bhed/efx/io.h"
#include "bhed/shaderscript.h"
#include "bhed/keys.h"
#include "bhed/pk3.h"
#include "bhed/roff.h"
#include "bhed/sound.h"
#include "bhed/edit.h"
#include "bhed/interplay.h"
#include "bhed/keys.h"
#include "bhed/settings.h"
#include "bhed/i18n.h"
#include "bhed/ibi.h"
#include "bhed/theme.h"
#include "bhed/tree.h"
#include "bhed/validate.h"
#include "imgui.h"
#include <algorithm>
#include <cfloat>
#include <functional>
#include <utility>
#include <cmath>
#include <cstdio>
#include <vector>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <cstring>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <vector>

namespace bhed::gui {

using i18n::Str;
using i18n::tr;

// --- Masse aus der Ressource ------------------------------------------


// --- Dialogeinheiten aus der Ressource --------------------------------
constexpr float kDluX = 1.5F;    // MS Sans Serif 8pt: baseunit 6 -> 6/4
constexpr float kDluY = 1.625F;  // baseunit 13 -> 13/8

struct Rect { float x, y, w, h; };

// Dialog 102, ausgelesen mit tools/dlgdump.py
// Aus Dialog 102 stammen jetzt nur noch die VERHAELTNISSE, nicht die
// absoluten Masse - siehe computeLayout(). Die Werte zum Nachrechnen:
//   Events      x=7   breit 191   ->  28,8 % von 663
//   Script Flow x=206 breit 372   ->  56,1 %
//   Knoepfe     x=585 breit  71   ->  10,7 %
//   Status      y=381 hoch   62   ->  13,8 % von 449
constexpr float kDialogW = 663;
constexpr float kDialogH = 449;

// Event-Editor, am Original gemessen
constexpr float kEdLeft = 100;
constexpr float kEdFieldW = 167;
constexpr float kEdFieldH = 14;
constexpr float kEdGap = 9;
constexpr float kEdBtnRow = 88;
constexpr float kEdOkW = 67;
constexpr float kEdOkPitch = 77;


// Zustand der Oberflaeche. Bewusst ein einfacher Verbund - das Programm hat
// genau ein Dokument, wie das Original.
struct App {
    Document doc{Script{}};
    CommandDb db;
    std::string path;           // leer = noch nie gespeichert
    // Der ICARUS-Pfad des offenen Skripts ("md_ga/intro_jedi"), wenn es aus
    // einem Archiv kam. Sonst wird er aus `path` abgeleitet
    // (aktiverSkriptPfad). Damit findet der Nachbau den Traeger in der Karte.
    std::string skriptPfad;
    // Der ICARUS-Nachbau des offenen Skripts (bhed/ablauf.h): daraus
    // entstehen Kamerabahn, Zeitleiste und Szene.
    Ablauf ablauf;
    // Gelesene Skripte fuer run/use/spawnscript, nach Pfad (klein).
    std::map<std::string, std::unique_ptr<Script>> ablaufSkripte;
    // SET_CAMERA_GROUP aus dem Ablauf: Entity -> (ab ms, Gruppe), geordnet.
    std::map<std::string, std::vector<std::pair<double, std::string>>> kameraGruppen;
    // Gelesene ROFF-Bahnen fuer play ( "PLAY_ROFF", ... ), nach dem
    // Archivpfad (klein). nullptr heisst: gesucht und nicht gefunden oder
    // nicht lesbar - auch das gemerkt, weil jeder Szenenaufbau fragt.
    std::map<std::string, std::shared_ptr<const Roff>> roffs;
    Settings settings;
    // Meldungen der Statusliste. Das Original nummeriert sie:
    //     (0) : ( Exported )
    //     (1) : ( Exported )
    std::vector<std::string> statusLines;
    MapData map;
    // Kartenansicht
    BspGeometry geo;
    BspMesh mesh;
    Camera cam;
    Orbit orbit;
    MapImage mapImage;
    // Linke Spalte: 0 Ereignisse, 1 Karte, 2 Modell
    int leftMode = 0;
    // Der Modus des vorigen Bildes.
    //
    // Beim Wechsel hat die Tabelle des neuen Modus noch keine
    // Spaltenbreiten (jeder Modus hat seit rc501 eine eigene ID). Fuer EIN
    // Bild ist die Inhaltszelle deshalb winzig - shanks Protokoll zeigt
    // ZelleW 223 statt 1932 und BildW 64 statt 1584.
    //
    // Sichtbar ist das als kurzes Zucken; schlimmer ist, dass fuer dieses
    // eine Bild ein 64 Punkte grosses Renderziel angelegt und sofort
    // wieder verworfen wird.
    int letzterModus = -1;
    // Wahr genau in dem Bild, in dem der Modus gewechselt hat.
    //
    // Gesetzt in der Aufteilungsberechnung, die in JEDEM Modus laeuft -
    // nicht in drawMapView, die nur im Karten-Modus laeuft.
    bool moduswechselBild = false;
    // Zuletzt aufgenommener Ziehweg - damit die Zeile nur bei Wechsel kommt.
    std::vector<std::size_t> dbgZiehWeg;
    GlmModel model;
    std::string modelPath;
    Camera modelCam;
    Orbit modelOrbit;
    MapImage modelImage;
    bool modelDirty = true;
    bool showCaps = false;
    // Skelett und Bewegung
    GlaAnimation anim;
    std::vector<AnimEntry> animList;
    int animIndex = -1;
    int animFrame = 0;
    bool animPlaying = false;
    float animTime = 0.0F;
    char animFilter[48] = {};
    // Der Text im Animationsfeld. Gehoert hierher, nicht in eine oertliche
    // Variable: sonst ist jede Eingabe im naechsten Bild wieder weg.
    std::string animText;
    // 0 = wie in der .cfg, 1 = immer, 2 = einmal
    int loopMode = 0;
    float animSpeed = 1.0F;
    ModelTextures modelTextures;
    // Die Haeute, die neben dem Modell liegen, und die gewaehlte.
    //
    // Ein Modell bringt oft mehrere mit: model_default.skin, model_red.skin,
    // model_blue.skin. Sie entscheiden nicht nur ueber Texturen, sondern
    // auch darueber, WELCHE Flaechen sichtbar sind - eine Zeile "kopf_b,*off"
    // schaltet eine Variante ab. Ohne Auswahl werden alle Varianten
    // gleichzeitig gezeichnet.
    std::vector<std::string> modelSkins;
    int modelSkinIndex = -1;
    std::string modelDir;
    bool showModelTextures = true;
    float lastModelCam[7]{};
    // --- Feinheit des Netzes ---------------------------------------------
    //
    // Stand auf 4. Das war eine Ruecksicht auf den Software-Rasterer: bei
    // 10 hat md_am_sith 325787 Dreiecke statt 106318, und die wollte er
    // nicht dreissigmal je Sekunde durchgehen.
    //
    // Seit dem GPU-Umbau kostet es nichts - shank misst konstant 120 fps
    // bei 10. Und bei 4 fehlt Geometrie, die man beim Vergleichen sucht:
    // die schwarzen Flaechen auf einem der Bilder zu rc436 waren genau das.
    //
    // Wer den Rasterer bei einer grossen Karte benutzt, stellt es herunter;
    // die Vorgabe soll das zeigen, was da ist.
    int meshDetail = 10;
    // Nur neu zeichnen, wenn sich etwas geaendert hat. Ein Editor steht die
    // meiste Zeit still; jedes Bild neu zu rechnen kostet Strom und macht
    // den Luefter laut, ohne dass sich etwas bewegt.
    bool mapDirty = true;
    TextureSet textures;
    bool showEntities = true;
    // Himmelsflaechen umschliessen die Karte; ausgeblendet sieht man von
    // aussen hinein. Zum Beurteilen einer Kameraeinstellung im Freien will
    // man sie aber sehen.
    // Die Himmelsflaechen zeichnen.
    //
    // Stand bis rc355 auf AUS, und das war richtig: die Flaechen umschliessen
    // die Karte wie ein Kasten, und ohne Himmelsbox sah man nur eine graue
    // Schale, die alles verdeckte.
    //
    // Seit die Box gezeichnet wird, ist es umgekehrt: ohne sie fehlt der
    // Himmel ganz und man sieht den Verlaufshintergrund. 589 Flaechen in den
    // 43 Karten sind betroffen.
    bool showSky = true;
    // Durch die gewaehlte Kamera schauen statt frei zu fliegen.
    bool throughCamera = false;
    // Kamerabahn und Abspielzustand
    CameraTrack camTrack;
    bool camTrackValid = false;
    // Die Kamera, mit der die Kartenansicht im letzten Bild WIRKLICH
    // gezeichnet wurde (freie Kamera, Kamerabahn oder "Through camera") -
    // und das Rechteck der Ansicht auf dem Schirm. Fuer den Selbsttest: er
    // prueft das Bild, nicht nur die Rechnung.
    Camera gezeigteKamera;
    float kartenAnsicht[4] = {0, 0, 0, 0};   // x0, y0, x1, y1
    double playMs = 0.0;
    bool playing = false;
    bool followCam = true;
    // Abspieltempo (0.25 .. 2) und Endlosschleife - wie im Sequencer. Bei
    // einem anderen Tempo als 1 bleibt der Ton aus: ein Klang liesse sich
    // nicht mitverlangsamen und liefe dem Bild davon.
    float abspielTempo = 1.0F;
    // Steht die Maus ueber der Zeitleiste? Dann gehoeren ihr die Pfeiltasten
    // (Bildschritt), nicht dem Baum. Aus dem vorigen Bild.
    bool tlUeberMaus = false;
    // Welche Dauerklaenge (Schwertsummen, Lautsprecherschleifen) gerade
    // laufen - Schluessel des Mischers. Leer nach jedem stopAll.
    std::set<std::uint64_t> laufendeSchleifen;
    bool schleife = false;
    // Figuren in der Karte
    Scene scene;
    bool showActors = true;
    // Effekte zeigen. An, weil eine Karte ohne ihren Rauch anders aussieht
    // als im Spiel - und genau darum geht es bei einer Vorschau.
    bool showEffects = true;
    // Das Gluehen zeigen - der zweite Durchgang ueber die Stufen mit `glow`.
    //
    // An, wie r_DynamicGlow es in Movie Duels ist. Ohne das Gluehen sieht
    // eine Karte flacher aus als im Spiel: 1026 der 4107 Shader tragen
    // `glow`, bei MD_Maps_Ep3 sind es 342 von 736 - Lichtsaeber, Konsolen,
    // Lava, Triebwerke, der Holotisch in md_am_sith.
    //
    // Abschaltbar, weil es einen zweiten Zeichendurchgang kostet und weil
    // man beim Suchen eines Fehlers das rohe Bild sehen will.
    bool showGlow = true;
    // Die Schattenart wie cg_shadows: 0 aus, 1 der runde Fleck (markShadow,
    // ab Werk), 2 Schattenvolumen im Stencil-Puffer, 3 die Figur schwarz auf
    // den Boden gedrueckt - siehe gpu::zeichneLichtUndSchatten und
    // gpu::zeichneFigurSchatten. Dazu dynamisches Licht auf Waenden und Boden
    // (r_dynamiclight), ebenfalls ab Werk an.
    int schattenArt = 1;
    bool showDynLights = true;
    // Nebel aus der Karte (r_drawfog 1, RB_FogPass) - siehe gpu::zeichneNebel.
    bool showFog = true;
    int nebelAufrufe = 0;
    // Die Schwertlichter des letzten Bildes. Die Figuren bekommen ihr Licht
    // VOR dem Zeichnen, die Klingen stehen erst danach fest - ein Bild
    // Verzug, den niemand sieht. Die Engine rechnet alle Lichter der Szene
    // auf jede Figur (R_SetupEntityLighting), also auch das eigene Schwert.
    std::vector<EffectLight> schwertLichter;
    // Fuer die Seitenleiste: wie viele Lichter und Schatten im letzten Bild.
    int lichtAufrufe = 0;
    int schattenAnzahl = 0;


    // Welche Liste hat die Auswahl zuletzt bekommen?
    //
    // 0 = keine, 1 = Ereignisliste, 2 = Skriptbaum.
    //
    // Windows zeigt die Auswahl einer Liste, die NICHT den Fokus hat,
    // gedaempft statt in der Betonungsfarbe. Genau das fehlte hier: wer in
    // der Ereignisliste etwas anklickte und danach im Baum arbeitete, sah
    // beide Markierungen gleich kraeftig und wusste nicht mehr, welche
    // gilt.
    int selectionOwner = 0;
    // Diagnose: fester Zuschlag auf die Blickrichtung der Figuren, in Grad.
    // Warum es ihn gibt, steht bei ActorDraw::yawOffset in bhed/mapview.h.
    int actorYaw = 0;
    // Die im Bild angeklickte Entity.
    //
    // SomaZ' BSP-Entity-Edit kann das ueber einen Nebenpuffer mit Kennungen
    // (ogl_fbo.py). Unser Zeichner ist Software, da genuegt es, die Marken
    // beim Zeichnen samt Bildschirmort zu merken und beim Klick die
    // naechstgelegene zu nehmen.
    struct PickedMarker {
        float sx = 0.0F;
        float sy = 0.0F;
        std::size_t entity = 0;
    };
    std::vector<PickedMarker> markers;
    // Namen an Entities und Figuren in der Ansicht (wie die Beschriftungen
    // im Unreal-Viewport). Die Figuren-Stellen entstehen beim Zeichnen.
    bool zeigeNamen = false;
    struct NamensMarke {
        float sx = 0.0F;
        float sy = 0.0F;
        std::string name;
    };
    std::vector<NamensMarke> figurNamen;

    // Ist die Kamera angeklickt?
    //
    // Ist sie es nicht, steht sie einfach da, wo sie zur eingestellten Zeit
    // ist - EINE Kamera, wie in 3ds Max oder Maya. Erst wenn man sie
    // anklickt, erscheint ihre Bahn mit den Schluesselstellen. Genau so
    // macht es Max: "By default, an object's trajectory is visible in the
    // viewports only when the object is selected."
    bool cameraSelected = false;
    // Welcher Schluessel auf der Bahn ist angeklickt? -1 = keiner.
    //
    // Ein Schluessel ist ein weisses Quadrat auf der Bahn und entspricht
    // GENAU einer Zeile im Skript: dem camera(MOVE)-Befehl, dessen Pfad im
    // Shot mitgefuehrt wird. Diese Zuordnung ist die Grundlage fuer alles
    // Weitere - ohne sie kann man zwar etwas verschieben, aber nicht
    // sagen, was danach im Skript stehen soll.
    int selectedKey = -1;
    // Welcher Schluessel wurde zuletzt ins Detailprotokoll geschrieben?
    // Verhindert, dass die Zeilen jedes Bild neu anfallen.
    int loggedKey = -2;
    // Wo die Schluessel zuletzt auf dem Bildschirm sassen.
    struct KeyMark {
        float sx = 0.0F;
        float sy = 0.0F;
        bool imBild = true;   // ausserhalb: nicht anklickbar, Platz bleibt
        Path path;
    };
    // Die Zeit, zu der der bearbeitete Schluessel zuletzt gewaehlt wurde.
    // Bewegt sich die Zeitleiste davon weg, geht der Schluessel mit (siehe
    // schluesselMitZeit) - ausser es steht eine ungeschriebene Aenderung an.
    double gizmoZeitBezug = -1.0;
    // Stand im letzten Bild eine Geisterkamera am Zielpunkt? (Selbsttest)
    bool geisterKamera = false;
    std::vector<KeyMark> keyMarks;

    // --- Das Gizmo -------------------------------------------------------
    //
    // Drei Achsen am ausgewaehlten Schluessel, wie in 3ds Max: X rot, Y
    // gruen, Z blau. Ziehen an einer Achse verschiebt NUR entlang dieser
    // Achse.
    //
    // Was hier NOCH NICHT geschieht: ins Skript zurueckschreiben. Die
    // Verschiebung steht bis auf Weiteres nur in gizmoOffset und ist
    // sichtbar, aber folgenlos. Das ist Absicht - erst soll sich das
    // Anfassen richtig anfuehlen, und erst danach fasse ich das
    // Bearbeitungsmodell an, an dem roundtrip und edittest haengen.
    // Was tut ein Klick? Wie in 3ds Max drei Betriebsarten, umschaltbar
    // ueber das Rechtsklickmenue.
    //
    //   Select      nur auswaehlen, kein Gizmo im Bild
    //   Move        die drei Achsen, Ziehen verschiebt
    //   Rotate      spaeter - Drehen heisst den PAN-Befehl aendern, und
    //               der kommt zusammen mit dem Rueckschreiben
    enum class GizmoMode : std::uint8_t { Select, Move, Rotate };

    // Bezugssystem der Griffe - wie die "Reference Coordinate System"-Liste
    // in 3ds Max.
    //
    //   Welt  - die Achsen liegen fest an X, Y, Z der Karte. Zum Setzen
    //           einer Kamera an einen bestimmten Ort ist das das
    //           natuerliche System: "zehn Einheiten weiter nach Norden".
    //   Lokal - die Achsen haengen an der Kamera: vorwaerts, rechts, oben.
    //           Zum Nachjustieren einer Einstellung besser: "einen Schritt
    //           zurueck", "etwas nach oben schwenken".
    //
    // Max fuehrt fuer JEDE Umformung eine eigene Einstellung ("If you
    // change this pull down list for move, that does not change it for
    // rotate"). Deshalb hier zwei Felder und nicht eines.
    enum class GizmoSpace : std::uint8_t { World, Local };
    // Vorgabe: AUSWAEHLEN.
    //
    // Mit Verschieben als Vorgabe ist jeder Klick in die Naehe einer Achse
    // ein Verschieben, bevor man ueberhaupt etwas ausgewaehlt hat. Max
    // startet ebenfalls mit dem Auswahlwerkzeug.
    GizmoMode gizmoMode = GizmoMode::Select;
    // Verschieben in Weltachsen - so war es bisher, und so erwartet man es
    // beim Setzen eines Ortes.
    GizmoSpace gizmoMoveSpace = GizmoSpace::World;
    // Drehen um die Kameraachsen - das entspricht pitch/yaw/roll des
    // PAN-Befehls unmittelbar, ohne Umrechnung.
    GizmoSpace gizmoRotSpace = GizmoSpace::Local;
    // Die drei Achsenrichtungen, EINGEFROREN beim Anfassen. Im Weltsystem
    // sind es X, Y, Z; im lokalen rechts, oben, vorwaerts. Eingefroren aus
    // demselben Grund wie in rc156: eine Achse, die mit dem gezogenen Punkt
    // mitwandert, koppelt zurueck.
    float gizmoAxisDir[3][3]{};
    // Dieselben Richtungen, wie sie zuletzt GEZEICHNET wurden, und die
    // Winkel des gewaehlten Schluessels dazu. Beim Anfassen wird daraus
    // eingefroren - dieselbe Arbeitsteilung wie bei gizmoSx/gizmoRingSx:
    // das Zeichnen weiss, wo etwas liegt, der Griff liest es ab.
    float gizmoDirNow[3][3]{};
    float gizmoAngNow[3]{};
    // Die Winkel der Kamera beim Anfassen - Grundlage der Weltdrehung.
    float gizmoAngBase[3]{};
    int gizmoAxis = -1;            // -1 = wird nicht gezogen, sonst 0/1/2
    float gizmoOffset[3]{};        // Vorschau-Verschiebung des Schluessels
    // Wo die Achsen zuletzt auf dem Bildschirm lagen - fuer den Griff und
    // fuer die Umrechnung Mausweg -> Weltweg.
    float gizmoSx[3][2]{};
    float gizmoSy[3][2]{};
    float gizmoWorldLen = 0.0F;    // Laenge einer Achse in Welteinheiten
    // Die Drehringe, auf den Bildschirm projiziert - ein Ring je Achse,
    // 48 Teile plus Schlusspunkt. Der Ueberfahr-Test im Drehmodus misst
    // gegen DIESE Punkte. Vorher mass er gegen die Achsenstrecken des
    // Verschiebens, die im Drehmodus gar nicht gezeichnet werden - deshalb
    // war der Ring "schwer greifbar": getroffen wurde eine unsichtbare,
    // veraltete Linie. Ungueltige Punkte stehen auf -1.
    static constexpr int kRingPts = 49;
    float gizmoRingSx[3][kRingPts]{};
    float gizmoRingSy[3][kRingPts]{};
    bool gizmoShown = false;       // steht ueberhaupt eines im Bild?
    float gizmoDragSx = 0.0F;      // Mausort beim Griff
    float gizmoDragSy = 0.0F;
    float gizmoStart[3]{};         // gizmoOffset beim Griff
    // Die Achse, EINGEFROREN beim Griff.
    //
    // Das ist die Behebung des Fehlers aus dem GIF: "es gibt beim moven
    // auch einen gewissen punkt wo es out of range ist und sich nicht mehr
    // zurueck bewegen laesst."
    //
    // Vorher rechnete das Ziehen mit den Achsen, WIE SIE GERADE STEHEN. Die
    // wandern aber mit dem Punkt mit - eine Rueckkopplung: der Punkt
    // bewegt sich, die Achse dreht und verkuerzt sich in der Perspektive,
    // derselbe Mausweg bedeutet plotzlich mehr, der Punkt bewegt sich
    // weiter. Und laeuft er hinter die Kamera, misslingt das Projizieren,
    // die Achse hat keine Bildlaenge mehr - und nichts geht mehr, auch
    // nicht zurueck.
    //
    // 3ds Max macht es anders und richtig: der Bezug wird beim Anfassen
    // festgelegt und gilt fuer den ganzen Zug. Genau das steht hier.
    float gizmoFixSx[2]{};
    float gizmoFixSy[2]{};
    float gizmoFixLen = 0.0F;
    // Welche Achse? 0/1/2, und 3 = das Kaestchen in der Mitte.
    //
    // Max nennt es "center box": "You can constrain translation to the
    // viewport plane by dragging the center box." Damit bewegt man in der
    // Bildebene, also in alle Richtungen zugleich - das war der Wunsch.
    static constexpr int kGizmoFree = 3;
    // Rechts und oben der Kamera beim Griff, fuer die freie Bewegung.
    float gizmoFreeRight[3]{};
    float gizmoFreeUp[3]{};
    float gizmoFreeScale = 0.0F;   // Welteinheiten je Bildpunkt
    // Ueber welcher Achse steht die Maus gerade? Nur zum Aufleuchten.
    int gizmoHover = -1;
    // Die PAN-Werte des ausgewaehlten Schluessels, als Vorschau.
    //
    // Wie gizmoOffset, aber fuer das Drehen: drei Winkel, die auf die
    // bestehenden addiert werden.
    float gizmoAngles[3]{};
    // Wird gerade gedreht? Dann der Mausort und der Ausgangswert.
    bool gizmoRotating = false;
    float gizmoRotStartSx = 0.0F;
    float gizmoRotStartSy = 0.0F;
    float gizmoRotStart[3]{};
    // Wo die Kamera zuletzt auf dem Bildschirm sass - fuer den Klick.
    float cameraSx = -1.0F;
    float cameraSy = -1.0F;
    int pickedEntity = -1;
    // Ob gerade durch die Kamera geschaut wird, und welcher Befehl dahinter
    // steht. Wird beim Zeichnen der Ansicht ermittelt und im Band angezeigt
    // - deshalb im Programmzustand und nicht oertlich.
    bool camViewActive = false;
    std::string camViewLabel;
    // Warum keine Figurenmodelle da sind - im Klartext, nicht als pauschaler
    // Hinweis. Die Kette hat vier Glieder, und jedes kann reissen.
    std::string actorWhy;
    float camViewFov = 90.0F;

    // Sichtbarkeit einzelner Entities und ganzer Klassen.
    //
    // Nach dem Vorbild des Ebenen-Verwalters von 3ds Max: eine Klasse ist
    // eine Ebene, das Auge davor schaltet sie um, und ein Alt-Klick stellt
    // sie allein (Solo). Eine einzelne Entity kann zusaetzlich fuer sich
    // ausgeblendet werden - versteckt eine Ebene, gilt das fuer alle ihre
    // Mitglieder, wie in Max auch.
    std::set<std::string> hiddenClasses;
    std::set<std::size_t> hiddenEntities;
    std::string expandedClass;

    [[nodiscard]] bool entityVisible(std::size_t index,
                                     const std::string& classname) const {
        return hiddenClasses.count(classname) == 0 &&
               hiddenEntities.count(index) == 0;
    }
    // Je Figur ein geladenes Modell samt Texturen. Der Index passt zu
    // scene.actors.
    // Skelett und Abschnittsliste einer .gla.
    //
    // Getrennt von den Figuren, weil sich fast alle dieselbe teilen:
    // Spielermodelle zeigen samt und sonders auf
    // models/players/_humanoid/_humanoid. Einmal lesen genuegt.
    struct Skeleton {
        GlaAnimation anim;
        std::vector<AnimEntry> sections;   // aus der animation.cfg
        // Die Abschnitte des Kino-Skeletts der Karte haengen HINTER den
        // eigenen (siehe GlaAnimation::anhang). Wie viele eigene es sind und
        // fuer welche Karte der Anhang gerade gilt.
        std::size_t grundAbschnitte = static_cast<std::size_t>(-1);
        std::string kinoKarte;
    };
    // Schluessel ist der Pfad aus dem Modellkopf, ohne Endung.
    std::map<std::string, Skeleton> skeletons;
    // Schon gemeldete Paare aus Skelett und unbekanntem Animationsnamen.
    // Ohne dieses Gedaechtnis stuende die Meldung in JEDEM Bild im
    // Protokoll - sechzig Zeilen je Sekunde.
    std::set<std::string> unknownAnims;
    // Entschluesselte Klaenge, fuer die Mundanimation.
    //
    // playSound() entschluesselt bei jedem Abspielen neu - das geht,
    // solange es einmal je Klang passiert. Der Mund braucht die
    // Abtastwerte aber in JEDEM Bild, und eine .wav je Bild neu zu
    // entschluesseln waere nicht tragbar.
    //
    // Ein Eintrag ohne ok heisst: schon versucht, geht nicht - dann wird
    // es nicht in jedem Bild erneut versucht.
    std::map<std::string, sound::Sound> soundCache;
    // Dieselben Klaenge, schon auf das Ausgabeformat (44,1 kHz Stereo)
    // umgerechnet. playSound entpackte und rechnete bisher bei JEDEM
    // Abspielen neu - 130 bis 215 ms je Klang, waehrend das Bild stand.
    std::map<std::string, std::vector<std::int16_t>> klangPuffer;
    // So viele Bilder zaehlen nach dem Start des Abspielens nicht als
    // Abspielzeit: die Zeit fuers Vorladen der Klaenge wuerde sonst auf
    // einmal draufgeschlagen, und der Anfang der Sequenz waere uebersprungen.
    int uhrAussetzen = 0;
    // Bis wann der Compile-Knopf "Compiled" (gruen, Haken) bzw. "Failed"
    // (rot) zeigt - ImGui::GetTime(). shank, 27.09.: Uebersetzen geht so
    // schnell, dass man nicht sieht, ob es passiert ist.
    double kompiliertBis = 0.0;
    double kompiliertFehlerBis = 0.0;

    // Die Netze der bewegten Brush-Modelle, nach Untermodellnummer.
    // Einmal gebaut, dann je Bild nur noch verschoben.
    std::map<int, BspMesh> brushMeshes;

    // Die Effekte der Karte: fx_runner-Entities und ihre .efx.
    //
    // Ueber die 23 Karten von MD_Maps_Ep1/2/4/6 sind das 374 Stueck - nach
    // waypoint und info_null die haeufigste Entity ueberhaupt. Rauch,
    // Funken, Dampf; ohne sie sieht ein Hangar aus wie ein leerer Raum.
    std::map<std::string, efx::Effect> effects;   // Pfad -> Inhalt
    std::vector<EffectInstance> effectRunners;    // was in DIESER Karte laeuft
    // Wie viele davon beim Zerbrechen spielen (MoverSim::bruchEffekte) -
    // sie haengen wie die per use gestarteten fx_runner am Skript.
    std::size_t bruchFxZahl = 0;
    // Tueren, Plattformen, func_wall & Co. - siehe bhed/mover.h. Wird in
    // prepareScene aus den use-Ereignissen und den Figurenwegen gebaut.
    MoverSim moverSim;
    // Die Klaenge der Mover (soundSet) und die bmodelSets aus
    // sound/sound.txt, die sie auf Dateien abbilden. Die Saetze werden
    // einmal gelesen; leer und `bmodelSetsGelesen` heisst: nicht gefunden.
    std::vector<MoverKlang> moverKlaenge;
    std::map<std::string, std::vector<std::string>> bmodelSets;
    bool bmodelSetsGelesen = false;
    // Wie viele Effektlichter zuletzt brannten. Nur, damit die Meldung
    // darueber bei jeder AENDERUNG kommt und nicht in jedem Bild - bei
    // sechzig Bildern je Sekunde waere sonst das Protokoll unlesbar.
    std::size_t lastFxLightCount = 0;
    // Ebenso fuer die Schusseffekte - Meldung bei jeder Aenderung, nicht
    // in jedem Bild.
    std::size_t lastShotFxCount = 0;

    // Die zuletzt gemeldete Gesamtzeit je Bild, in Millisekunden.
    //
    // Gefragt: "hast du jetzt eingebaut im Debug-Log, was wie viel kostet
    // und warum?"
    //
    // Bis rc299 nicht - die Aufschluesselung stand nur im
    // Mauszeiger-Kasten und hinter einem Menuepunkt. Damit muss man beim
    // Messen gleichzeitig hinsehen, und beim Abspielen geht das schlecht.
    //
    // Jetzt schreibt sie sich selbst, sobald sich die Gesamtzeit deutlich
    // aendert.
    float lastFrameMsLogged = -1.0F;
    // Die zuletzt gemeldete Effektuebersicht. Nur, damit sie bei einer
    // AENDERUNG erscheint und nicht bei jedem Aufbau der Ansicht.
    std::string lastEffectSummary;

    // --- Wohin die Zeit je Bild geht --------------------------------------
    //
    // Gemeldet: "ich merke, dass ich beim Abspielen nur 30 fps habe. Koennen
    // wir debuggen beim Laufen, was so viel kostet?"
    //
    // Bisher gab es nur die Gesamtzeit je Bild. Damit weiss man, DASS es
    // langsam ist, aber nicht WO - und raten hilft beim Beschleunigen noch
    // weniger als beim Suchen von Fehlern.
    //
    // Alle Werte in Millisekunden, geglaettet wie die Gesamtzeit: eine
    // ungeglaettete Anzeige springt zu stark, um etwas abzulesen.
    struct FrameCost {
        float scene = 0.0F;      // Szene und Figurenzustaende aufbauen
        float effects = 0.0F;    // Effektnetz bauen
        float map = 0.0F;        // Karte zeichnen
        float actors = 0.0F;     // Figuren zeichnen
        float glow = 0.0F;       // zweiter Durchgang und Weichzeichnen
        float gizmos = 0.0F;     // Gizmos und Blende
        float total = 0.0F;
        // Was die Oberflaeche wirklich braucht, aus ImGui. Steht daneben,
        // damit die Luecke zwischen Summe und Wirklichkeit sichtbar bleibt
        // statt sich zu verstecken.
        float frame = 0.0F;
        // --- Dieselben Zahlen UNGEGLAETTET ------------------------------
        //
        // Die geglaetteten oben sind fuer die Anzeige da (eine springende
        // Zahl liest niemand). Zum MESSEN taugen sie nicht, und das hat
        // eine Runde gekostet:
        //
        //     #20 Karte  91,1    #40 152,1    #60 181,9
        //
        // Das sah nach einer Ansammlung aus - als wuerde sich etwas
        // aufstauen. Es war die Glaettung beim Einschwingen: die Zuwaechse
        // halbieren sich (61,0 dann 29,8), der Grenzwert liegt bei rund
        // 210 ms. Nach zwanzig Bildern steht dort erst die Haelfte des
        // wahren Werts.
        //
        // Wer eine geglaettete Zahl protokolliert, misst seine eigene
        // Glaettung mit.
        float rawMap = 0.0F;
        float rawTotal = 0.0F;
    };
    FrameCost frameCost;
    // --- Ist die Karte fertig geladen? ----------------------------------
    //
    // Der GPU-Weg darf das Netz nur anfassen, wenn niemand mehr daran baut.
    // Eine Pruefung auf "nicht leer" reicht dafuer NICHT: `g_app->mesh` wird
    // an mehreren Stellen im laufenden Betrieb neu zugewiesen
    // (app.cpp:7863, app_view3d.cpp:8786 und 8947), und zwischen dem Blick
    // auf `empty()` und dem Lesen der Stapel kann genau das passieren.
    //
    // Gemeldet: Absturz beim Laden, aber NUR wenn der Schalter vorher schon
    // an war. Mit ausgeschaltetem Schalter, oder wenn er nach dem Laden
    // umgelegt wird, laeuft es. Genau dieses Zeitfenster.
    //
    // Die Marke wird bewusst NICHT aus dem Netz abgeleitet, sondern an den
    // Stellen gesetzt, die das Netz austauschen. Was man aus dem Zustand
    // erraten kann, kann man auch falsch erraten.
    bool mapBereit = false;
    // Was der letzte Versuch gemeldet hat. Leer heisst: kein Fehler.
    std::string gpuFehler;
    // Wie viele Aufrufe zuletzt abgesetzt wurden. 0 bei eingeschaltetem
    // Schalter heisst: es ist nichts passiert - und das sieht aus wie eine
    // schwarze Karte, also gehoert es angezeigt.
    int gpuAufrufe = 0;
    // Wie viele Stapel uebersprungen wurden - fehlende Textur oder Shader.
    // Ein Loch in der Karte hat genau hier seine Ursache.
    int gpuUebersprungen = 0;
    // --- Getrennt je Durchgang -------------------------------------------
    //
    // `gpuAufrufe` mischte Karte, Mover und Figuren in einer Zahl. Getrennt
    // sieht man sofort, welcher Durchgang gar nicht laeuft - bei "die
    // Charaktere fehlen komplett" ist das der Unterschied zwischen "wird
    // nicht gerufen" und "scheitert beim Zeichnen".
    int gpuKarte = 0;
    int gpuMover = 0;
    int gpuFiguren2 = 0;
    // Was die Debugschicht seit dem Start gemeldet hat, jede Meldung
    // einmal. Bis rc447 ging sie an den Windows-Debugger und war damit
    // unsichtbar; jetzt steht sie im Bericht.
    std::vector<std::string> d3dMeldungen;
    // Was auf den FIGUREN liegt: Flaechenname, Shader, Mischart.
    //
    // Die schwarzen Vierecke auf dem Hologrammtisch sind Figurenflaechen -
    // welche, war aus dem Bild nicht zu entscheiden, und geraten habe ich
    // in dieser Sache schon zweimal. Der Bericht sagt es jetzt.
    // Je KOMBINATION aus Shader, Skin und Mischart, mit Anzahl.
    //
    // Die erste Fassung zaehlte jede Flaeche einzeln auf und brach bei 60
    // ab - ein einziger Battledroid hat 26, und die Hologramme, wegen derer
    // die Liste gebaut wurde, standen gar nicht mehr drin.
    std::map<std::string, int> figurArten;
    int gpuGluehen = 0;
    // Das Effektnetz wurde bisher nur zu gpuAufrufe addiert und nie einzeln
    // gezaehlt - in der Aufstellung fehlte es damit.
    int gpuEffekte = 0;

    // Die Fenstergroesse, getrennt von der Rastergroesse in mapImage.
    int mapViewW = 0;
    int mapViewH = 0;
    // Wie viele Bilder seit dem letzten Stillstand vergangen sind. 0 heisst
    // "bewegt sich gerade".
    int stillCount = 0;
    // Die Kamera des letzten Bildes, um Bewegung zu erkennen.
    float lastCamPos[3]{};
    float lastCamAng[3]{};
    bool haveLastCam = false;

    bool logFrameCost = false;
    // Nicht jedes Bild eine Zeile: bei 60 Bildern je Sekunde waere das
    // Protokoll in einer Minute unlesbar. Jedes zwanzigste genuegt, um
    // einen Verlauf zu sehen.
    int frameCostTick = 0;
    BspMesh effectMesh;                           // je Bild neu

    // --- .md3-Modelle der Karte ------------------------------------------
    //
    // 1549 Entities in den Karten der Mod zeigen auf eine .md3 - Konsolen,
    // Kisten, Rohre, Raumschiffe. Ohne sie fehlt der halbe Raum.
    //
    // Gehalten wird je Datei EINMAL das gelesene Modell, dazu je gebrauchtem
    // Bild ein fertiges Netz. Ein Modell mit dreissig Bildern, das
    // abgespielt wird, kostet damit dreissig Netze - aber nur die, die
    // wirklich vorkommen.
    struct MapModel {
        std::string path;
        int frames = 1;
        float origin[3]{};
        // ALLE drei Winkel, wie CG_CreateMiscEntFromGent sie mit
        // AnglesToAxis anwendet - vorher nur die Gier.
        float angles[3]{};
        // Je Achse: "modelscale_vec", darueber "modelscale" (G_SpawnFloat,
        // ungleich null). Vorher wurde die Groesse gelesen und nie benutzt.
        float scale[3]{1.0F, 1.0F, 1.0F};
        int shaderBase = 0;   // erste Nummer in textures.byShader
        // Die Nummer in map.entities - damit ein misc_model_breakable
        // nach seinem Bruch das Schadensmodell zeigen oder verschwinden
        // kann (MoverSim::modellAt).
        std::size_t entity = 0;
        // Eine MD3-Figur (NpcDef::legsModel): steht im ersten Bild, statt
        // alle Bilder der Datei durchzulaufen.
        bool standbild = false;
    };
    std::map<std::string, Md3Model> md3Files;          // Pfad -> Modell
    std::map<std::string, BspMesh> md3Meshes;          // "Pfad#Bild" -> Netz
    std::vector<MapModel> mapModels;
    // Je Kartenmodell die Figur der Szene, die es bewegt (move, rotate,
    // PLAY_ROFF, SET_INVISIBLE, remove) - oder -1. Nach jedem Szenenaufbau.
    std::vector<int> modellFigur;
    // .md3, die erst ein use braucht: Schadens- und Benutzmodelle (_d1/_u1)
    // und die Bruchstuecke (MoverSim::nebenModelle). Pfad -> erste Nummer
    // in textures.byShader; das Modell selbst steht in md3Files.
    std::map<std::string, int> nebenModelle;
    // Was beim Laden der Karte NICHT gefunden wurde - fuer den Selbsttest
    // (allekarten), der prueft, ob es doch irgendwo in den Archiven liegt.
    std::vector<std::string> fehlendeTexturen;
    std::vector<std::string> fehlendeModelle;

    // --- Modelle, die an einem Emitter haengen --------------------------
    //
    // "Emitters don't draw themselves, but they may need to add an
    // attached model" (FxPrimitives.cpp:1375). Der Rauch war rc323; hier
    // kommt der Koerper dazu - bei probehead.efx der fliegende
    // Droidenkopf selbst.
    //
    // 70 Vorkommen in den 376 Effektdateien.
    //
    // Geladen wird beim Missionsstart, nicht beim ersten Gebrauch: sonst
    // faellt eine .md3 mitten in ein Bild, und das haben wir in rc307 bis
    // rc311 ausfuehrlich gehabt.
    struct EmitterModel {
        std::string path;
        int shaderBase = 0;
    };
    std::map<std::string, EmitterModel> emitterModels;   // Pfad -> Angaben
    // Was gerade fliegt, je Bild neu bestimmt. Steht hier, weil die Liste
    // der lebenden Effekte in einem anderen Block gerechnet wird als der,
    // in dem gezeichnet wird.
    std::vector<EmitterModelDraw> emitterFlug;

    // Shadername -> Nummer in textures.byShader, fuer die Effekte.
    //
    // Die .efx nennen ihre Shader beim Namen; ohne diese Zuordnung
    // zeichneten wir Partikel als farbige Vierecke ohne Bild. Sie stehen
    // hinten in derselben Liste wie die Texturen der Karte und der
    // .md3-Modelle - eine Liste, ein Weg, eine Stelle zum Nachsehen.
    EffectShaderSlots effectShaders;

    struct ActorAssets {
        GlmModel model;
        ModelTextures textures;
        // Zeigt in skeletons. std::map haelt seine Knoten an Ort und
        // Stelle, also bleibt der Zeiger gueltig, auch wenn spaeter weitere
        // Skelette dazukommen - bei std::vector waere er es nicht.
        const Skeleton* skeleton = nullptr;
        bool tried = false;
    };
    std::vector<ActorAssets> actorAssets;
    NpcMap npcMap;
    // Die Klingen aus ext_data/sabers/*.sab. Schluessel ist der Eintragname
    // aus dem `saber`-Schluessel einer .npc-Datei.
    SaberMap saberMap;
    // Die Waffen aus ext_data/weapons.dat. Schluessel ist der weapontype
    // aus dem `weapon`-Schluessel einer .npc-Datei, z.B. "WP_BLASTER".
    WeaponMap weaponMap;
    // Die Waffenmodelle, gepuffert. Schluessel ist der volle .md3-Pfad aus
    // weapons.dat. Gebraucht wegen ihres Tags "tag_flash" - dort sitzt die
    // Muendung.
    std::map<std::string, Md3Model> weaponModels;
    // Die Missionen des zuletzt gewaehlten Archivs.
    std::vector<Mission> missions;
    bool missionPickOpen = false;
    std::string missionArchive;
    bool npcMapRead = false;

    // Die Shaderskripte EINMAL fuer die ganze Sitzung.
    //
    // Vorher lag die Tabelle als oertliche Veraenderliche in
    // texturesFor(), also wurde sie JE MODELL neu aufgebaut: alle
    // .shader-Dateien aus allen Archiven lesen und zerlegen. Bei shanks
    // Movie-Duels-Installation sind das 40 Archive und 9014 Eintraege - und
    // die Mission hat 64 Figuren. Das Protokoll zeigte den Satz "9014
    // Shadereintraege gelesen" dutzendfach; das Programm schien zu haengen,
    // rechnete aber dieselbe Tabelle immer wieder.
    ShaderMap shaderMap;
    // Beim Beenden: der Reiter, in dem man vor der ersten Speichernfrage
    // stand - dorthin bei "Abbrechen" (confirmQuit).
    int quitZurueck = -1;
    // fogparms aller Nebelshader - mit shaderMap zusammen gelesen.
    NebelMap nebelMap;
    // --- Bilder nur EINMAL lesen und entpacken --------------------------
    //
    // Gemessen in rc186: von 6,2 s beim Laden gingen 5,0 s in das Aufloesen
    // der Figurentexturen - 81 Prozent. Der Grund: texturesFor() lief JE
    // MODELL und JE FLAECHE los, las die Datei aus dem Archiv (also mit
    // Entpacken) und entzifferte das Bild neu. Dieselbe Ruestung an zehn
    // Sturmtrupplern hiess zehnmal dieselbe Arbeit.
    //
    // Die Engine macht es genauso wie hier: sie fuehrt eine Tabelle nach
    // NAMEN (AllocatedImages in tr_image.cpp) und gibt ein schon geladenes
    // Bild einfach zurueck. Der Schluessel wird dort ueber
    // GenerateImageMappingName gebildet - klein geschrieben, ohne Endung,
    // Schraegstriche vereinheitlicht. Genau das tut texKey() unten.
    std::unordered_map<std::string, TextureSet::Tex> texCache;
    // Welche Eintraege von texCache die AKTUELLE Karte benutzt hat (ueber
    // loadTextureFor: Kartenflaechen, Himmel, Stufen, Effekte, Kartenmodelle).
    // Beim naechsten Kartenwechsel fliegen die alten heraus, die die neue
    // Karte nicht wieder braucht - wie RE_RegisterImages_LevelLoadEnd, das
    // Bilder ohne Verwendung im neuen Level loescht. Vorher wuchs texCache
    // mit jeder Karte (Kartenpruefung 27.09.: 8 Karten, 91 -> 857 MB).
    // Figuren (@256) und Klingenbilder stehen nie hier drin.
    std::unordered_set<std::string> kartenBilder;
    // Welcher Knoten wurde zuletzt durch EINFUEGEN angewaehlt (nicht durch
    // einen Klick)? Ein angewaehlter Block nimmt neue Befehle nur auf, wenn
    // DU ihn angeklickt hast - sonst liefe der naechste Klick in der
    // Ereignisliste in den gerade angelegten Block (die "Klebrigkeit" aus
    // rc532). Ein Klick in den Baum leert es.
    Path auswahlDurchEinfuegen;
    // "Last compile: ..." in der Statuszeile (leer ohne .ibi) - hier, damit
    // der Selbsttest es lesen kann.
    std::string letzteKompilierung;
    bool shaderMapRead = false;

    // Schon geladene Modelle, damit gleiche Figuren nicht mehrfach kosten.
    //
    // In einer Massenszene sind die meisten Figuren derselbe Sturmtruppler.
    // Das Protokoll zeigte rund zwanzigmal hintereinander "42 gefunden, 0
    // nicht gefunden" - jedes Mal dasselbe Modell samt Texturen neu
    // eingelesen und verkleinert.
    //
    // Schluessel ist der Modellordner: models/players/<name>.
    std::map<std::string, ActorAssets> modelCache;
    // Die Modelle der Schwertgriffe. Schluessel ist der VOLLE Pfad aus der
    // .sab-Datei - anders als bei Figuren, die immer unter
    // models/players/<name>/model.glm liegen.
    std::map<std::string, ActorAssets> hiltCache;
    Timeline timeline;
    bool showTracks = true;
    // Klaenge beim Abspielen mitspielen. Bis wohin sie schon ausgeloest
    // wurden, damit keiner zweimal kommt.
    bool playAudio = true;
    bool stumm = false;
    // Die Ueberlagerung des GPU-Wegs: Entitykreuze, Kamera, Bahnen, Gizmos
    // und die Blende als durchsichtiges Bild ueber dem GPU-Bild.
    MapImage gizmoBild;
    void* gizmoTexture = nullptr;
    int gizmoTexW = 0;
    int gizmoTexH = 0;
    bool gizmoZeigen = false;   // im letzten Bild ueber das GPU-Bild gelegt?   // Selbsttest: Klaenge verarbeiten, aber nicht ausgeben
    double audioUpTo = -1.0;
    float lastCam[7]{};
    float mapBrightness = 2.0F;
    float mapMinLight = 0.10F;
    // Zeit fuers letzte Bild, geglaettet - eine ungeglaettete Anzeige
    // springt so, dass man sie nicht lesen kann.
    float mapFrameMs = 0.0F;
    std::string mapBytes;      // fuer den erneuten Netzbau bei anderer Feinheit
    // .pk3-Blaetterei
    std::vector<GamePath> gamePaths;
    std::vector<FoundFile> pk3Files;
    // Kopf und Oberkoerper dem Blickziel nachdrehen.
    //
    // Gemeldet: "immer noch zu weit gedreht." Mit diesem Schalter laesst
    // sich das in einem Klick entscheiden: AUS zeigt allein die Animation,
    // also die Pose, wie sie in der .gla steht. Sieht die Figur dann
    // richtig aus, liegt es an der zusaetzlichen Drehung; sieht sie
    // genauso aus, liegt es an der Pose oder am Standwinkel.
    //
    // Eine Vorschau darf so einen Schalter haben - sie soll ja Fragen
    // beantworten, nicht nur huebsch sein.
    bool lookRotation = true;
    Audio audio;
    // Ein ZWEITES Geraet, nur fuer die Musik.
    //
    // Gemeldet: "jetzt hoere ich die Musik, aber die Stimmen nicht mehr."
    //
    // Der Grund steht in der Windows-Schnittstelle: waveOutWrite auf EIN
    // Geraet reiht die Puffer hintereinander - es mischt sie nicht. Die
    // Musik von md_ga_jedi laeuft ueber eine Minute; alles, was danach
    // geschrieben wird, wartet, bis sie durch ist.
    //
    // Vor rc255 gab es keine Musik, also fiel es nicht auf: die Stimmen
    // ueberlappen sich in diesen Skripten nicht, und kurze Klaenge
    // hintereinander klingen wie gleichzeitige.
    //
    // Ein zweites Geraet ist auch das, was die Engine tut - Musik ist dort
    // kein Klangkanal, sondern ein eigener Strom
    // (S_StartBackgroundTrack, cg_main.cpp:2965). Windows mischt zwei
    // offene WAVE_MAPPER-Geraete selbst.
    Audio musicAudio;
    // Laenge je Klangdatei in Millisekunden, -1 = nicht gefunden.
    std::unordered_map<std::string, double> soundLen;
    // Nach dem Schreiben: wie lange steht die Bestaetigung noch, und wie
    // hell? Sekunden, laeuft herunter.
    float gizmoDoneFade = 0.0F;
    bool pk3Open = false;
    // 0 = Skripte, 1 = Karten, 2 = Modelle
    int pk3Kind = 0;
    char pk3Filter[64] = {};
    // Nach welchem Archiv gefiltert wird. Leer heisst: alle.
    //
    // Bei einer vollstaendigen Installation sind das vierzig Archive mit
    // Hunderten Dateien - ohne diesen Filter sucht man in einer Liste, in
    // der assets1.pk3 und der eigene Mod durcheinanderstehen.
    std::string pk3Archive;
    std::string settingsFile;
    std::string dataDir;

    const theme::Theme* activeTheme = nullptr;
    // Die Farbgebung darf erst angewendet werden, wenn ImGui einen Kontext
    // hat. createApp() und loadSettings() laufen aber VOR CreateContext() -
    // ein ImGui::GetStyle() dort loest die Zusicherung
    // "No current context" aus und das Programm bricht beim Start ab.
    // Deshalb nur vormerken und im ersten Bild anwenden.
    bool themePending = true;
    float uiScale = 1.0F;    // vom Nutzer gewaehlt (Strg+Mausrad, Menue)
    float dpiScale = 1.0F;   // vom Bildschirm, wird von aussen gesetzt
    float appliedScale = -1.0F;  // was zuletzt tatsaechlich angewendet wurde
    TreeOptions treeOpt;
    Expanded expanded;
    std::vector<Row> rows;
    int selected = -1;
    int selectedCommand = -1;   // Zeile in der Ereignisliste links
    Path selectedPath;
    // Mehrfachauswahl. selectedPath bleibt der ZULETZT angeklickte Knoten -
    // an ihm haengen Einfuegen und der Bezug fuer Umschalt-Klick.
    std::vector<Path> selection;
    // --- Mehrere Skripte gleichzeitig -------------------------------------
    //
    // Der Aufbau ist bewusst schlicht: es gibt weiterhin GENAU EIN lebendes
    // Dokument (doc, path, selection, selectedPath, selected, expanded).
    // Alles im Programm arbeitet damit weiter wie bisher - hunderte
    // Zugriffsstellen bleiben unangetastet.
    //
    // Die anderen offenen Skripte liegen daneben GEPARKT. Beim Umschalten
    // wird das lebende hineingelegt und das gewaehlte herausgeholt.
    //
    // Warum so und nicht "ueberall das aktive Dokument nachschlagen": das
    // waere der saubere Aufbau, aber er beruehrt jede Stelle in app.cpp, die
    // g_app->doc anfasst. Ein Tausch an EINER Stelle ist weniger elegant und
    // sehr viel weniger riskant - und von aussen nicht zu unterscheiden.
    // Wie heisst das aktive Skript auf seinem Reiter?
    //
    // Aus einem Archiv geladene Skripte haben KEINEN Pfad - dorthin laesst
    // sich nicht zurueckspeichern. Ohne einen eigenen Namen hiessen dann
    // alle Reiter "unnamed.icarus", und gerade beim Laden einer Mission mit
    // zehn Skripten waere die Leiste wertlos.
    std::string shownName;

    static constexpr int kMaxSplit = 4;

    struct Parked {
        // Document hat keinen Standardkonstruktor - ein leeres Skript ist
        // ein gueltiger Anfangszustand und genau das, was ein neuer Reiter
        // zeigt.
        Document doc{Script{}};
        // Feste Kennung des Reiters, vergeben beim ersten Gebrauch
        // (tabKennung). Die NUMMER eines Reiters aendert sich, sobald ein
        // anderer davor geschlossen wird - wer daran etwas merkt (die
        // Rollposition), gibt es sonst dem Nachruecker weiter.
        int uid = 0;
        std::string path;
        std::vector<Path> selection;
        Path selectedPath;
        int selected = -1;
        Expanded expanded;
        std::string shownName;
        std::string skriptPfad;

        // Die Feldaufteilung DIESES Reiters: wie viele Felder, welcher
        // Reiter in welchem steht, wo der Fokus lag. Jeder Reiter hat sein
        // eigenes Fenster - "wenn ich im ersten tab 4 reiter offen habe
        // und dann in den nebenan gehe, dann sollte da wieder ein
        // einzelner sein und nicht global ueber alle verteilt". Ein
        // frischer Reiter beginnt mit EINEM Feld.
        int splitCount = 1;
        int splitTabs[kMaxSplit] = {0, 0, 0, 0};
        int splitFocus = 0;

        // Welche Karte gehoert zu diesem Reiter?
        //
        // NICHT die Karte selbst. Eine geladene Karte belegt mit Geometrie,
        // Lightmaps und Texturen leicht hundert Megabyte; zehn Reiter mit
        // je einer Kopie waeren ein Gigabyte, und beim Laden einer Mission
        // zeigen ohnehin alle zehn Skripte auf DIESELBE Karte.
        //
        // Deshalb nur der Pfad. Beim Umschalten wird verglichen: ist es
        // dieselbe Karte, bleibt sie stehen und es kostet nichts. Ist es
        // eine andere, wird sie geladen; ist keine gesetzt, wird die alte
        // weggeraeumt.
        //
        // Das ist der Grund, warum ein neuer Reiter jetzt leer ist statt
        // die Karte des vorigen zu erben.
        std::string mapPath;
        // Wie die Karte HEISST, nicht nur wo sie liegt.
        //
        // Missionen kommen aus einem Archiv und haben deshalb GAR KEINEN
        // Pfad auf der Platte - mapPath ist dann leer. Der Reiterwechsel
        // verglich aber genau diesen Pfad, also war "leer gegen leer" immer
        // gleich, und die Karte wurde nie gewechselt. Genau so wurde es
        // gemeldet: "die map ist nicht pro tab".
        //
        // MapData::path traegt die Kennung (etwa "maps/md_am_sith.bsp"),
        // auch bei einer Karte aus dem Archiv.
        std::string mapId;
        // Hat dieser Reiter eine EIGENE Karte?
        //
        // Wahr nur, wenn fuer ihn ausdruecklich eine geladen wurde - eine
        // Mission oder ein "Karte oeffnen". Ein Skript, das man einfach so
        // aufmacht, bekommt sie nicht: es teilt sich die des
        // Arbeitsbereichs.
        //
        // Ohne diese Unterscheidung schreibt sich beim Parken JEDER Reiter
        // die gerade geladene Karte zu, und aus dem Mitbenutzen wird wieder
        // ein Besitz.
        bool ownMap = false;
        // Wo die ANSICHT stand - Ort, Blickrichtung, Zeitpunkt.
        //
        // Gemeldet: "er speichert nur nicht die kamera position wo ich
        // war". Ohne das faengt man nach jedem Reiterwechsel wieder am
        // Ausgangspunkt an und sucht die Stelle, an der man gearbeitet hat.
        //
        // Gehoert zum Reiter und nicht zum Programm: zwei Skripte spielen
        // an verschiedenen Ecken derselben Karte, und jedes will seine
        // eigene Sicht.
        float camPos[3]{};
        float camAng[3]{};
        double playMs = 0.0;
        bool camSaved = false;
    };
    // Die offenen Skripte. Der aktive Eintrag ist ein PLATZHALTER - seine
    // Felder stehen in doc/path/... und werden beim Umschalten
    // zurueckgelegt.
    std::vector<Parked> tabs;
    int activeTab = 0;

    // --- Geteilte Ansicht ------------------------------------------------
    //
    // Rechts neben dem Skriptbaum ein ZWEITER, der einen anderen Reiter
    // zeigt. Gedacht zum Vergleichen und zum Uebertragen: "this is how I
    // usually work with multiple scripts open where I copy and paste
    // commands between them."
    //
    // Die rechte Haelfte ist absichtlich nur zum LESEN und Kopieren. Sie
    // hat kein eigenes Dokument im Sinne von Bearbeiten - das lebende
    // Dokument bleibt eines, und damit bleiben Rueckgaengig, Einfuegen und
    // alles Uebrige eindeutig. Wer rechts etwas aendern will, wechselt den
    // Reiter; dann ist es links.
    // Wie viele Spalten insgesamt? 1 = nur der Hauptbaum, bis 4.
    //
    // Wie in VS Code, wo man den Bereich in bis zu vier Gruppen teilt. Die
    // ERSTE ist immer der bearbeitbare Hauptbaum; die weiteren sind
    // Vergleichsspalten und nur zum Lesen - dieselbe Begruendung wie in
    // rc150: an rows/selection/selected haengt jede Bearbeitung, und die
    // wollen wir nicht vervierfachen.
    int splitCount = 1;

    // Je Vergleichsspalte ein eigener Satz. Drei davon, weil die erste
    // Spalte der Hauptbaum ist.
    struct SplitPane {
        int tab = 0;                 // welcher Reiter darin steht
        std::vector<Row> rows;       // sein Baum
        int selected = -1;           // dort angeklickte Zeile
        Path path;                   // ihr Weg
        bool dirty = true;           // neu bauen?
        Expanded expanded;           // eigener Klappzustand
        std::vector<std::uint8_t> marke;   // Aenderungsrand je Zeile
    };
    // Ein Satz je Feld - jetzt auch fuer das erste.
    //
    // Bis rc161 war das erste Feld eine Sonderrolle: es zeigte immer das
    // lebende Dokument, die anderen ihre geparkten. Damit war nur das erste
    // bearbeitbar.
    //
    // Der Umbau kommt OHNE ein zweites lebendes Dokument aus, und das ist
    // der Kern der Sache: das Feld mit dem FOKUS wird das lebende. Klickt
    // man in ein anderes, wird das bisherige geparkt und dessen Reiter
    // hervorgeholt - genau der Tausch, den es seit rc148 schon gibt.
    //
    // Damit bleibt es bei EINEM Dokument, das bearbeitet wird. Rueckgaengig,
    // Einfuegen, Loeschen und alles Uebrige bleiben unangetastet; keine der
    // hunderten Zugriffsstellen muss fragen, welches Feld gemeint ist. Und
    // von aussen sieht es aus, als waeren alle vier bearbeitbar - denn das
    // sind sie, eines nach dem anderen, so wie in VS Code auch immer nur
    // eine Gruppe den Schreibzeiger hat.
    SplitPane splitPanes[kMaxSplit];
    // Wo jeder REITER zuletzt stand, senkrecht.
    //
    // Gemeldet: "jedes Mal, wenn ich wieder irgendwo in ein anderes Skript
    // klicke, springt das erste Fenster ungewollt umher."
    //
    // Der Grund ist strukturell: das Feld mit dem Fokus wird als HAUPTBAUM
    // gezeichnet, die anderen als Vergleichsbaeume - das sind verschiedene
    // Fenster mit eigener Rollposition. Beim Fokuswechsel wandert ein Skript
    // also von einem Fenster ins andere und landet dort, wo dieses zuletzt
    // stand.
    //
    // Gemerkt wird deshalb je REITER, nicht je Feld: die Position gehoert
    // zum Skript, nicht zu dem Kasten, in dem es gerade steht.
    std::map<int, float> tabScroll;   // Schluessel: Parked::uid, nicht die Nummer
    // --- Lesezeichen und Aenderungsrand (wie Notepad++) -------------------
    //
    // Beides je Reiter an seiner festen Kennung (Parked::uid) und je Knoten
    // an SEINER Kennung (Node::kennung) - Wege aendern sich bei jeder
    // Bearbeitung, Kennungen nicht, auch nicht ueber Rueckgaengig.
    struct AenderungsStand {
        // Knoten in Dateireihenfolge mit ihrem eigenen Text: beim Oeffnen
        // und beim letzten Speichern.
        std::vector<std::pair<Kennung, std::string>> geladen, gespeichert;
        // Die Befehle selbst, wie sie beim Oeffnen waren (ohne Blockinhalt) -
        // fuer "Revert to original" im Rechtsklickmenue.
        std::map<Kennung, Node> original;
    };
    std::map<int, AenderungsStand> aenderungen;
    std::map<int, std::set<Kennung>> lesezeichen;
    std::vector<std::uint8_t> zeilenMarke;   // parallel zu rows
    float rinneX0 = 0.0F;                    // linke Kante des Randes (Selbsttest)
    int naechsteTabUid = 1;
    // Welchen Reiter jeder Kasten zuletzt zeigte - daran erkennt man den
    // Wechsel, bei dem die Rollposition wiederhergestellt werden muss.
    std::map<std::string, int> lastTabInBox;
    // Welches Feld hat den Fokus? Dieses zeigt das lebende Dokument.
    int focusPane = 0;

    // --- Wem gehoert die Aufteilung? -------------------------------------
    //
    // Gemeldet: "wenn ich alle 4 nebeneinander oeffne und dann ins zweite
    // Fenster klicke, bin ich ploetzlich in einem anderen Tab... dabei
    // sollte er das nur im ersten anzeigen, weil ich die DA gesplittet
    // habe."
    //
    // Genau richtig gedacht. Die Aufteilung hing bisher am AKTIVEN Reiter -
    // und der wandert beim Klick in ein Feld mit, weil dort ein anderes
    // Skript steht. Also wanderte die Aufteilung mit und wurde beim
    // naechsten Reiterwechsel in einen Reiter geschrieben, der nie geteilt
    // wurde.
    //
    // Jetzt gibt es einen BESITZER: den Reiter, in dem geteilt wurde. Er
    // ist im Reiterband hervorgehoben und behaelt die Aufteilung. Welches
    // Skript gerade bearbeitet wird, sagt der leuchtende Rahmen um das
    // Feld - dafuer ist er da.
    //
    // Damit sind zwei Fragen getrennt, die vorher eine waren:
    //   "In welchem Arbeitsbereich bin ich?"  -> homeTab
    //   "Welches Skript aendere ich gerade?"  -> activeTab
    int homeTab = 0;

    // --- Ziehen zwischen den Feldern --------------------------------------
    //
    // Die Nutzlast, die ImGui traegt. Nur kleine Werte: ImGui kopiert sie
    // sofort in einen eigenen Puffer, und ein Zeiger darin waere spaetestens
    // beim Loslassen ungueltig (so steht es in der Doku zu
    // SetDragDropPayload).
    //
    // Deshalb NICHT der Knoten und nicht sein Weg, sondern nur: aus welchem
    // Feld, aus welchem Reiter, welche Zeile. Den Rest sucht das Ziel selbst
    // heraus - es hat beides ohnehin vorliegen.
    struct DragNode {
        int pane = -1;
        int tab = -1;
        int row = -1;
    };

    // --- Protokoll ueber Reiter, Felder und Groessen ----------------------
    //
    // Gewuenscht, um sicherzugehen, dass mehrere gleichzeitig geoeffnete
    // Skripte und das Verstellen der Fenstergroessen wirklich tun, was sie
    // sollen. In dieser Sitzung kamen die meisten Ueberraschungen aus genau
    // diesen beiden Ecken.
    //
    // Geschrieben wird nur bei einer AENDERUNG - sonst faellt es jedes Bild
    // an. Dafuer merkt sich diese Aufnahme den zuletzt geschriebenen Stand.
    struct LayoutLog {
        int tabs = -1;
        int activeTab = -1;
        int splitCount = -1;
        int focusPane = -1;
        int paneTabs[kMaxSplit] = {-1, -1, -1, -1};
        int frameW = -1;
        int frameH = -1;
        int eventsW = -1;
        int flowW = -1;
        int toolH = -1;
        // Das Fenster war zu klein fuer alles, was hineinsoll - siehe
        // Layout::tooSmall in app.cpp.
        bool tooSmall = false;
        int fracX = -1;   // in Promille, damit der Vergleich ganzzahlig ist
        int fracY = -1;
        int tlFrac = -1;
    };
    LayoutLog lastLayoutLog;

    // Wie tief war der Rueckgaengig-Stapel zuletzt, und in welchem Reiter?
    //
    // Aendert sich die Tiefe, wurde BEARBEITET - und dann soll im Protokoll
    // stehen, WELCHES Skript es getroffen hat. Genau das ist die Frage beim
    // Arbeiten an vier Skripten nebeneinander: landet die Aenderung dort,
    // wo ich hingeklickt habe?
    std::size_t lastUndoDepth = 0;
    int lastEditTab = -1;
    // Fokuswechsel, angefordert waehrend des Zeichnens.
    //
    // Nicht sofort ausgefuehrt: mitten im Zeichnen das Dokument zu
    // tauschen zoege dem gerade gezeichneten Baum den Boden weg.
    int focusRequest = -1;

    // --- Anordnung der Kartenansicht ------------------------------------
    //
    // Die Einstellungen der Ansicht standen als drei Zeilen Kaestchen UNTER
    // dem Bild und nahmen dort dauerhaft Platz weg - Platz, den die
    // Zeitleiste braucht. Jetzt stehen sie in einer Spalte am rechten Rand
    // der Ansicht, die man ein- und ausklappen kann. So macht es Blender
    // mit seiner Seitenleiste (N).
    bool mapSidebar = true;
    float mapSidebarW = 0.0F;   // gemessen, nicht geraten
    // Hoehe der Zeitleiste, als Anteil der Kartenspalte. Ziehbar am oberen
    // Rand, wie in Blender.
    float timelineFrac = 0.22F;
    // Zeigt die Zeitleiste Bilder oder Sekunden? In 3ds Max laesst sich das
    // umschalten, und beides ist gebraucht: Sekunden fuer die Laenge einer
    // Einstellung, Bilder fuer das genaue Setzen eines Schluessels.
    bool timelineFrames = false;
    // Bilder je Sekunde fuer die Umrechnung. JKA rechnet intern in
    // Millisekunden; 30 ist das, womit die Filmleute der Mod arbeiten.
    int timelineFps = 30;
    // Die Spurenliste aufgeklappt? Blender nennt das den Kanalbereich.
    bool timelineChannels = true;
    // --- Ausschnitt der Zeitleiste ---------------------------------------
    //
    // Bei 62 Sekunden Skript ist ein wait von 200 ms drei Bildpunkte breit -
    // sichtbar, aber nicht zu treffen und schon gar nicht zu lesen. Mit
    // Zoom wird aus der Uebersicht ein Werkzeug.
    //
    // Gespeichert wird der ANFANG in Millisekunden und die Vergroesserung;
    // die sichtbare Spanne ist Dauer/Vergroesserung. Nicht Anfang und Ende,
    // weil sonst beim Zoomen zwei Werte gleichzeitig wandern und man sich
    // beim Klemmen leicht vertut.
    double timelineViewStartMs = 0.0;
    double timelineZoom = 1.0;
    // Wie weit hinein? 32-fach heisst bei einer Minute Skript knapp zwei
    // Sekunden im Bild - das ist die Aufloesung, in der man Schluessel
    // setzt. Vorher waren 400 erlaubt: 0,15 Sekunden im Bild, und die
    // Zahlen im Lineal standen uebereinander. "so klein brauche ich es
    // nicht" - stimmt, und es war auch nicht lesbar.
    //
    // EIN Wert, statt der Zahl an sechs Stellen: sechs Abschriften einer
    // Grenze laufen auseinander, und dann klemmt eine Stelle anders als
    // die andere.
    static constexpr double kMaxZoom = 32.0;

    // --- Eine Dauer in der Zeitleiste ziehen ------------------------------
    //
    // Gezogen wird die RECHTE KANTE eines Blocks; das aendert seine DAUER.
    // Den Block als Ganzes zu verschieben ergibt in ICARUS keinen Sinn: ein
    // Skript laeuft der Reihe nach, der Anfang eines Befehls ergibt sich
    // aus allem davor. Was man wirklich einstellt, ist "wie lange".
    //
    // Waehrend des Ziehens wird NICHTS geschrieben - nur die Vorschau
    // wandert. Erst beim Loslassen geht es ueber replaceAt() ins Skript und
    // damit in den Rueckgaengig-Speicher. Dieselbe Regel wie beim Gizmo.
    // Eigene Namen, NICHT dragFrom/dragging - die gehoeren dem Ziehen im
    // Skriptbaum. Zwei Vorgaenge, die sich ein Merkmal teilen, stoeren
    // einander frueher oder spaeter.
    // Wo die Zeitleiste gerade liegt - fuer den Selbsttest (zeitleiste),
    // der echte Mauszuege auf Bloecke, Kanten und Namen setzt.
    float tlFeldX = 0.0F;        // linke Kante der Zeitachse (Bildschirm)
    float tlFeldW = 0.0F;        // ihre Breite
    double tlSichtStart = 0.0;   // sichtbarer Ausschnitt in ms
    double tlSichtSpanne = 1.0;
    float tlLinealY = 0.0F;      // Mitte des Lineals
    std::vector<std::pair<std::string, float>> tlZeilenY;   // Name -> Mitte der Zeile
    // Was in DIESEM Bild unter der Maus angeklickt wurde. Entschieden wird
    // erst am Ende der Spuren: dann gewinnt der OBEN liegende (zuletzt
    // gezeichnete) Block. Vorher griff der erste - in der Script-Zeile der
    // Kamerablock UNTER einem wait, das man sah (Zeitleistentest 27.09.).
    // Einen Block der Zeitleiste greifen und verschieben (wie Ripple-Edit
    // im Sequencer): der Pfad des Befehls, wo die Maus anfing, sein Anfang,
    // der Massstab, und ob schon gezogen wird (erst ab 4 Bildpunkten).
    Path tlZiehPfad;
    float tlZiehMausX = 0.0F;
    double tlZiehStartMs = 0.0;
    double tlZiehMsJePx = 1.0;
    bool tlZiehAktiv = false;
    double tlZiehDelta = 0.0;
    struct TlKandidat {
        bool da = false;
        Path pfad;
        double startMs = 0.0;
        double endMs = 0.0;
        bool kamera = false;   // Kamerazeile (sonst eine Spur)
        int figur = -1;        // Figur der Spur, -1 = keine
    };
    // Die drei Hauptspalten (links | Script Flow | Actions) im letzten Bild:
    // tatsaechliche Breiten, Grenzen und die Lage der zwei Teiler - fuer den
    // Selbsttest (fenster), der die Teiler bis an die Enden zieht.
    float spaltenB[3]{};
    float spaltenMin[3]{};
    float spaltenMax[3]{};
    float teilerX[2]{};
    // Beim Anfassen eines Spaltenteilers: Mausposition und Breite der
    // gezogenen Spalte (in den Einheiten von spaltenZiel). Gezogen wird
    // ABSOLUT von dort aus - siehe die Griffe nach EndTable.
    float spaltenLuecke = 0.0F;   // frei rechts neben der letzten Hauptspalte
    // --- Was Undo/Redo zuletzt getan hat (Statuszeile) und die Undo-Liste
    //
    // shank: "When I undo, it isn't clear what was changed. Maybe it could
    // scroll to what was changed or have some sort of message popup at the
    // bottom like 'undo: wait 1000'?" - und eine Undo-Tabelle wie in 3ds Max.
    std::string undoMeldung;             // leer = keine
    bool undoMeldungRedo = false;        // stammt sie von einem Redo?
    std::size_t undoMeldungTiefe = 0;    // undoDepth danach - aendert sich das, ist sie veraltet
    int undoListeAnfrage = 0;            // 0 nichts, 1 Undo-Liste oeffnen, 2 Redo-Liste
    bool undoListeRedo = false;          // zeigt die offene Liste Redo-Schritte?
    std::vector<std::string> undoListeEintraege;   // neuester zuerst
    int undoListeMarke = 0;              // bis zu welchem Eintrag (einschliesslich)
    std::string undoStatusText;          // was unten gerade steht (fuer den Selbsttest)
    // Fuer den Selbsttest: zu welcher Zeile zuletzt der Randhinweis stand.
    int randVorschauZeile = -1;
    // Mitte des Randes je Zeile im letzten Bild (Bildschirm), und wie oft
    // die Original-Vorschau gezeichnet wurde - beides fuer den Selbsttest.
    std::vector<ImVec2> randMitte;
    int vorschauGezeichnet = 0;
    float teilerStartMaus = 0.0F;
    float teilerStartBreite = 0.0F;
    // Die Zeitleiste: ihre Hoehe im letzten Bild, der Platz, aus dem sie als
    // Anteil entsteht, und beim Anfassen der Kante Mausposition und Hoehe.
    float tlHoeheIst = 0.0F;
    float tlPlatz = 0.0F;
    float tlKanteStartMaus = 0.0F;
    float tlKanteStartHoehe = 0.0F;
    float tlKanteX = 0.0F;   // Mitte der Ziehkante, fuer den Selbsttest
    float tlKanteY = 0.0F;
    float teilerY = 0.0F;
    float spaltenReq[3]{};    // Diagnose: WidthRequest / WidthGiven / Ziel
    float spaltenGiven[3]{};
    float spaltenZiel[3]{};
    float tabelleOben = 0.0F;
    float tabelleUnten = 0.0F;
    float spaltenGesamt = 0.0F;
    // Was Zellraender und Trennlinien der Hauptspalten kosten (gemessen).
    float spaltenUeberhang = 40.0F;
    int teilerAnzahl = 0;
    // Ruhige Spuren zeigen (nach dem Anfang passiert nichts mehr darin).
    bool zeigeRuhigeSpuren = false;
    int tlUeberlappungen = 0;     // im letzten Bild rot markierte Bloecke (Selbsttest)
    TlKandidat tlGriffKandidat;   // Kante gepackt
    TlKandidat tlKlickKandidat;   // Block angeklickt
    Path tlDragPath;             // welcher Block, leer = keiner
    double tlDragStartMs = 0.0;  // sein Anfang - der bleibt stehen
    double tlDragEndMs = 0.0;    // sein Ende, waehrend des Ziehens
    bool tlDragging = false;

    // Wo die Trennlinien des Rasters liegen, als Anteil - senkrecht und
    // waagerecht. Ziehbar, wie die Kanten zwischen Bereichen in Blender.
    // "I want to be able to change the size of each script flow view".
    // Welche Figur ist angewaehlt? -1 heisst keine.
    //
    // Dieselbe Rolle wie cameraSelected fuer die Kamera: erst die Auswahl
    // bringt die Bahn ins Bild. "ich will den charakter anklicken um zu
    // sehen wo er hinlaeuft, aehnlich wie bei der Kamera".
    int selectedActor = -1;
    // Wo die Figuren gerade auf dem BILDSCHIRM stehen - fuer den Klick.
    // Dieselbe Arbeitsteilung wie bei cameraSx/cameraSy: das Zeichnen
    // weiss es, der Klick liest es ab. Leer heisst: nicht im Bild.
    struct ActorMark {
        int index = -1;
        float sx = -1.0F;
        float sy = -1.0F;
    };
    std::vector<ActorMark> actorMarks;

    float splitFracX = 0.5F;
    float splitFracY = 0.5F;
    // Reiterwechsel aus dem Klappfeld des FOKUSSIERTEN Feldes - aus
    // demselben Grund angefordert statt sofort ausgefuehrt.
    int tabRequest = -1;

    // Der Doppelklick zeigt, was in einer Zeile steht.
    // Der volle Text einer Zeile, den ein Doppelklick rechts zeigt.
    std::string splitShowText;

    // Ziehen im Baum: welcher Knoten haengt an der Maus?
    Path dragFrom;
    // Aus WELCHEM Reiter und Feld der gezogene Knoten stammt.
    //
    // Der Weg allein genuegt nicht mehr, seit man zwischen Skripten ziehen
    // kann: derselbe Weg meint in einem anderen Dokument etwas anderes.
    // -1 im Feld heisst "aus dem Hauptbaum".
    int dragFromTab = -1;
    int dragFromPane = -1;
    bool dragging = false;
    // Nach einem Tastendruck muss die Zeile in den sichtbaren Bereich.
    bool scrollToSelected = false;      // Weg des ausgewaehlten Knotens, leer = keiner
    std::vector<Issue> issues;

    // Event-Editor
    bool editorOpen = false;
    // Die MITTE des Ereignisfensters. Jedes Bild wird das Fenster mit dem
    // Drehpunkt 0.5/0.5 dorthin gesetzt; waechst es, waechst es in alle
    // Richtungen gleich. Nur Ziehen mit der Maus verschiebt sie. Negativ =
    // beim naechsten Bild auf die Mitte der Arbeitsflaeche setzen.
    ImVec2 editorMitte{-1.0F, -1.0F};
    // Wie breit die Feldreihe im Ereignisfenster GEMESSEN war. Nur fuers
    // Protokoll - die Breite, mit der gerechnet wird, steht seit rc568 vor
    // `Begin` fest (`reiheW` in drawEditor). Siehe dort, warum.
    float editorRowW = 0.0F;
    const Command* editorCmd = nullptr;
    std::vector<std::string> editorValues;
    std::vector<char> editorIsExpr;   // Feld wurde auf Ausdruck umgestellt
    // Wie `editorIsExpr` beim OEFFNEN aussah.
    //
    // Nur damit laesst sich sagen, ob der Benutzer den Ausdrucksmodus
    // ANGEFASST hat. `argForParam` gibt das alte Argument unveraendert
    // zurueck, solange der Text derselbe ist - samt seiner Art. Nach einem
    // Revert stand deshalb beim naechsten Oeffnen wieder "Expr!".
    std::vector<char> editorWarExpr;
    // Welche Felder nach einer Aenderung neu zu befuellen sind.
    //
    // Anlass: wer die Auswahlliste von SET_PARM1 auf SET_HEALTH stellt,
    // bekommt zwar ein Zahlenfeld statt eines Textfeldes - im Feld stand
    // aber weiter "DEFAULT". Die Feldbeschreibungen wurden jedes Bild neu
    // berechnet, die WERTE nicht.
    //
    // -1 heisst: nichts zu tun. Sonst die Stelle, ab der es weitergeht.
    int editorRefreshFrom = -1;
    // Der Beenden-Knopf hat gedrueckt. Die Fensterschicht liest es und
    // schliesst - der Knopf selbst kann das nicht, er kennt kein Fenster.
    bool wantQuit = false;
    // Wurde schon gefragt?
    //
    // Der Beenden-Knopf fragt selbst und setzt dann wantQuit. Die
    // Fensterschicht schickt daraufhin WM_CLOSE - und DAS fragt seit rc145
    // ebenfalls. Ergebnis: zweimal derselbe Dialog, gemeldet als "I have to
    // click No twice for it to close".
    //
    // Also merken, dass die Frage schon beantwortet ist.
    bool quitConfirmed = false;

    // --- Die Frage "Speichern?" im eigenen Stil ---------------------------
    //
    // Vorher war es ein Windows-Systemdialog (MessageBox). Der sieht aus wie
    // aus einem anderen Programm - graue Knoepfe, eigene Schrift, eigene
    // Farben - und genau das war der Einwand.
    //
    // Ihn nachzubauen ist mehr als Kosmetik: eine MessageBox ANTWORTET
    // SOFORT, ein ImGui-Fenster spannt sich ueber mehrere Bilder. Aus
    //
    //     if (confirmDiscard()) { oeffnen(); }
    //
    // wird deshalb
    //
    //     withUnsaved([] { oeffnen(); });
    //
    // Was nach der Antwort geschehen soll, wird MITGEGEBEN und aufgehoben,
    // bis der Anwender geklickt hat.
    // Eine Ja/Nein-Frage im Stil einer MessageBox - "Open?", "Exit?",
    // "Copy entire script?", "You seem to be pasting ...". Die Antwort wird
    // erst nach dem Schliessen ausgefuehrt (drawFrage).
    bool frageOffen = false;
    std::string frageText;
    std::function<void()> frageJa;
    std::function<void()> frageNein;
    // Der Text, den behaved zuletzt selbst in die Windows-Zwischenablage
    // gelegt hat. Steht er noch drin, gilt die interne Ablage (samt allem,
    // was ein Text nicht traegt).
    std::string ablageText;
    bool askSaveOpen = false;
    std::string askSaveName;
    std::function<void()> askSaveThen;
    // Was bei "Abbrechen" geschehen soll. Meist nichts - beim Beenden aber
    // doch: dort muss die angefangene Kette zurueckgesetzt werden.
    std::function<void()> askSaveCancel;
    // Ab welchem Reiter beim Beenden noch zu fragen ist.
    //
    // Ohne das fragte die Kette ewig: "Nein" heisst wegwerfen, aber es
    // macht das Dokument nicht sauber - also fand die naechste Runde
    // DENSELBEN Reiter wieder und fragte erneut. Gemeldet als "Clicking No
    // when asked to save before closing never closes the program".
    //
    // Jetzt merkt sich die Kette, wie weit sie gekommen ist. Bei
    // "Abbrechen" faengt sie von vorn an.
    std::size_t quitAskFrom = 0;
    bool editorForceRefresh = false;   // "Neu auswerten" - alles zuruecksetzen
    std::vector<int> editorKinds;      // Feldarten des vorigen Bildes
    std::vector<std::string> editorDefs;
    // Zustand der Helferzeilen. Einer fuer alle Felder: im Original klappen
    // sie auch nur unter EINEM Feld auf.
    // --- Der Ausdruckshelfer JE SPALTE ------------------------------------
    //
    // Im Original (Bild 2 von shank) hat jede Spalte ihren eigenen Satz aus
    // FLOAT / SET_PARM1+Get / ORIGIN+Tag / Bereich+Rnd, und alle sind
    // gleichzeitig zu sehen. Hier gab es sie EINMAL, und ein eigenes Feld
    // bestimmte, unter welchem Feld sie standen: "when you press the Expr!
    // button, it can only show one at a time."
    //
    // Also je Feld ein eigener Satz. Die Reihen wachsen mit der Zahl der
    // Felder mit; `helferGross()` sorgt dafuer.
    std::vector<std::string> helpGetType;
    std::vector<std::string> helpGetName;
    std::vector<std::string> helpTagName;
    std::vector<std::string> helpTagType;
    // Wo die Felder der Reihe stehen - fuer die Spalten darunter. Wird beim
    // Zeichnen der Reihe vermerkt, damit die Helfer genau unter ihrem Feld
    // sitzen und nicht unter einer geschaetzten Stelle.
    bool dumpAngefordert = false;  // Debug > Alles ins Protokoll
    bool layoutOpen = false;   // Debug > Aufteilung zeigen
    // Gemessene Breiten der Kartenansicht, fuer denselben Auszug. Sie
    // entstehen tief in `drawEventsList` und `drawMapView`; ohne sie muesste
    // man die Luecke zwischen Karte und Skriptbaum wieder schaetzen.
    // Fensterbreite des VORIGEN Bildes.
    //
    // Eine feste Tabellenspalte behaelt beim Vergroessern des Fensters ihre
    // Pixelbreite - der Anteil aendert sich also von allein. Ohne diesen
    // Vergleich ist eine Fenstergroessenaenderung von einem Ziehen nicht zu
    // unterscheiden, und der gespeicherte Wert wandert bei jedem Ziehen am
    // Fensterrand mit.
    float letzteFensterB = 0.0F;
    // Wie viele Bilder die Fensterbreite schon gleich geblieben ist.
    //
    // Eine ImGui-Tabelle uebernimmt eine neue Spaltenbreite erst im
    // NAECHSTEN Bild. Ein Bild Ruhe reicht deshalb nicht - siehe die
    // Begruendung bei `fensterStabil` in drawMain().
    int fensterStabilSeit = 0;
    // --- Gemessene Kanten des Ereignisfensters --------------------------
    //
    // Sechs Runden lang habe ich die Ausrichtung der beiden Zeilen geraten.
    // Beim Hauptfenster war der Wendepunkt, dass die Zahlen sichtbar wurden
    // (rc491) - hier waren sie es nie.
    //
    // Gemessen wird die RECHTE KANTE beider Zeilen in derselben Spalte.
    // Sind sie gleich, stimmt die Ausrichtung; ist die untere groesser,
    // ragen die Helfer hinaus.
    float dbgZelle0 = 0.0F;    // Zellbreite der ersten Spalte
    float dbgFeldEnde = 0.0F;  // rechte Kante der Feldzeile
    float dbgHelferEnde = 0.0F;  // rechte Kante der Helferzeilen
    // Was die Spalte BESTELLT bekam, gegen das, was die Zelle liefert.
    // Weichen beide auseinander, hat ImGui die feste Spalte vergroessert -
    // und genau daraus entstand die Rueckkopplung in rc549.
    float dbgBestellt0 = 0.0F;
    float dbgKnopfB = 0.0F;    // knopfB der Feldzeile
    float dbgKnopfW = 0.0F;    // knopfW der Helferzeilen
    float dbgLetzteDiff = -9999.0F;  // zuletzt protokollierte Differenz
    float dbgComboMinB = 0.0F; // Mindestbreite des letzten Klappfensters
    // Hoehe, die das Kindfenster der Klappliste bekommt. Ausdruecklich
    // gerechnet, damit Klappfenster und Kind sich nicht gegenseitig
    // aufschaukeln - siehe comboEdit().
    float dbgComboListenH = 0.0F;
    float modeBarW = 0.0F;     // gemessene Breite der Modusleiste
    float dbgZelleW = 0.0F;    // die Tabellenzelle
    float dbgRestW = 0.0F;     // was nach der Ebenenliste uebrig ist
    float dbgBildW = 0.0F;     // die Kartenflaeche selbst
    std::vector<float> editorFeldX;
    // Und wie BREIT jedes Feld ist. Die Helferspalte darunter richtet sich
    // danach - vorher nahm sie eine feste Zahl und war breiter als ihr
    // Feld. Die Bloecke ueberlappten sich, und wer oben lag, bekam den
    // Klick: shank konnte nur den ERSTEN Parameter aendern.
    std::vector<float> editorFeldW;
    std::vector<float> helpRangeLow;
    std::vector<float> helpRangeHigh;
    Path editorPath;        // leer = neuer Knoten, sonst der zu aendernde
    bool editorInsert = false;

    // Suchen
    bool prefsOpen = false;
    // Stand der Einstellungen beim Oeffnen des Fensters - "Cancel" (und das
    // X) spielen ihn zurueck. Das Original hat OK und Cancel.
    Settings prefsVorher;
    bool prefsHatVorher = false;
    // Alt+M: die MRU-Liste oeffnen (das Klappfenster gehoert dem Knopf).
    bool mruOeffnen = false;
    bool aboutOpen = false;
    bool findOpen = false;
    // Das Meldungsfenster. Die Statuszeile zaehlt nur; wer nachlesen will,
    // klickt den Zaehler an.
    bool messagesOpen = false;
    // Eigene Fenster fuer Tastenkuerzel und Spielordner.
    //
    // Vorher steckte alles im Einstellungsfenster untereinander - man
    // musste an den Ordnern vorbeiscrollen, um an die Tasten zu kommen, und
    // umgekehrt. Drei Sachen, drei Fenster.
    bool keysOpen = false;
    // Eine Zeile im Kuerzelfenster wartet auf eine Taste - dann loest kein
    // Kuerzel etwas aus, sonst wirkte derselbe Druck doppelt.
    bool kuerzelWartet = false;
    bool pathsOpen = false;
    // Die Uebersicht ueber das Zusammenspiel der offenen Skripte.
    bool interplayOpen = false;

    // War beim BEGINN dieses Bildes ein Dialog offen?
    //
    // Die Schalter oben taugen dafuer nicht: ein Dialog, der mit Eingabe
    // schliesst, setzt seinen Schalter mitten im Bild auf false. Die
    // Tastenbehandlung laeuft danach, sieht "kein Dialog offen" und
    // verarbeitet dieselbe Eingabetaste ein zweites Mal - der Editor ging
    // zu und sofort wieder auf.
    //
    // Die Regel lautet: wer beim Bildanfang offen war, hat die Taste
    // bereits bekommen. Sie darf im selben Bild nicht noch einmal wirken.
    bool dialogAtFrameStart = false;
    // "Whole-string match only" ist im Original vorgewaehlt.
    bool findWhole = true;
    // Ersetzen im Find-Fenster (wie Notepad++): nur innerhalb der Auswahl?
    bool findInSelection = false;
    // Find war zu Beginn des Bildes offen (wie dialogAtFrameStart, aber
    // getrennt: bei Find gehen Rueckgaengig/Wiederholen durch).
    bool findAtFrameStart = false;
    // Der Ersatztext - fuer den Selbsttest von aussen setzbar.
    std::string findErsatz;
    // "Find similar" im Rechtsklickmenue: das Suchfenster beim naechsten
    // Zeichnen mit diesen Angaben fuellen.
    bool findVorbelegen = false;
    std::string findVorName;
    std::string findVorText;
    // Welches Kuerzel zuletzt ausgeloest hat, und wie oft ueberhaupt eins -
    // fuer den Selbsttest, der jede Belegung aendert und ausprobiert.
    keys::Action letzteKuerzelAktion = keys::Action::Unhandled;
    int kuerzelZaehler = 0;
    // Nur fuer den Selbsttest: Open, Save as und Save all melden, statt sie
    // auszufuehren. Die ersten beiden oeffnen einen Windows-Dialog, das
    // dritte saehe auch andere Reiter - beides darf ein Test nicht.
    bool kuerzelNurMelden = false;
    // Wie viele Zeilen im letzten Bild als "gleich" hinterlegt waren - fuer
    // den Selbsttest.
    int gleicheSichtbar = 0;
    // Die zuletzt gesuchten Angaben - F3 und Umschalt+F3 suchen damit
    // weiter, auch wenn das Suchfenster zu ist.
    FindOptions findLetzte;
    std::vector<Path> findHits;
    int findAt = 0;
};

// Der eine Zustand. Definiert in app.cpp.
extern App* g_app;

// Masseinheiten des Originaldialogs.
float dx(float v);
float dy(float v);

// --- Ueber Dateigrenzen hinweg benutzt --------------------------------
//
// Nur, was wirklich woanders gebraucht wird. Alles Uebrige bleibt in
// seiner Datei und in einer anonymen Namensgruppe.
std::string slurp(const std::string& path);
std::string directoryOf(const std::string& path);
std::string fileName(const std::string& path);
// Eine Datei aus den Spielordnern lesen.
//
// fromArchive nimmt, wenn es nicht null ist, den Pfad des Archivs auf, aus
// dem die Datei kam. Das ist keine Spielerei: Movie Duels ERSETZT Dateien
// des Originalspiels unter demselben Namen, und ohne diese Angabe sieht man
// im Protokoll nicht, welche Fassung man geladen hat.
bool readFromArchives(const std::string& name, std::string& out,
                      std::string* fromArchive = nullptr);
void rescanGamePaths();
void refreshPk3List();
void rebuildTree();
void setStatus(const char* fmt, int a, int b);
// Steht das Skript schon in einem Reiter? -1 = nein (gui/app.cpp).
int reiterMitPfad(const std::string& p);
int reiterMitSkript(const std::string& name);
void oeffneSkriptAusSpeicher(const std::string& data, const std::string& name);
void addStatus(const std::string& line);

// Schreibt den Bericht INS PROTOKOLL - Grafikkarte, Zustaende, Zaehler,
// Zeiten, Shader und die Meldungen der Debugschicht.
//
// Ohne Zutun aufgerufen: beim Beenden und beim Absturz. Es gibt keinen
// Knopf dafuer, und das ist Absicht - ein Werkzeug, das man erst
// einschalten muss, ist genau dann aus, wenn man es braucht.
void schreibeBericht();
// abMs: an welcher Stelle IM KLANG begonnen wird. Null heisst von vorn.
// Gebraucht, wenn der Ton mitten im Abspielen wieder eingeschaltet wird.
// Einen Klang abspielen. `background` heisst Musik: eigenes Geraet, und ein
// neues Stueck ersetzt das laufende - siehe musicAudio.
// `lautstaerke` ist 1 fuer alles, was ohne Ort gespielt wird (Sprache,
// Musik, Vorschau). Effektklaenge geben die Entfernungsdaempfung mit -
// siehe lautstaerkeAus in app.cpp.
void playSound(const std::string& name, double abMs = 0.0,
               bool background = false, float lautstaerke = 1.0F,
               float rechts = -1.0F, std::uint64_t schluessel = 0);

// Einen Klang als SCHLEIFE fuer `dauerMs` spielen - die Fahrgeraeusche der
// Mover (loopSound, BMS_MID). Der Puffer wird bis zur Dauer wiederholt.
void playSoundSchleife(const std::string& name, double dauerMs, float lautstaerke, float rechts = -1.0F,
                       std::uint64_t schluessel = 0);
// Musik mit Intro und Schleife (S_StartBackgroundTrack), ab abMs.
void spieleMusik(const std::string& spec, double abMs);
// Die Levelmusik aus dem worldspawn ("music"), leer wenn keine.
std::string weltMusik();

double soundLengthMs(const std::string& name);
// Klaenge am Ort, wie die Engine sie hoert (app_view3d.cpp)
void spieleSkriptKlang(const std::string& werSpur, const std::string& datei, const std::string& kanalName,
                       double ms);
void spieleLautsprecher(const std::string& name, const std::string& datei);
// ICARUS-Nachbau (app_view3d.cpp)
std::string aktiverSkriptPfad();
const Script* skriptFuerAblauf(const std::string& pfad);
std::shared_ptr<const Roff> roffFuer(const std::string& name);
AblaufWelt ablaufWelt();
void pfadeAufsSkript();
void kinoSkeletteAbgleichen();
// Gruppen der Entity-Liste (und Farben der Marken in der Ansicht).
struct EntityKat {
    Str titel;
    std::uint8_t r;
    std::uint8_t g;
    std::uint8_t b;
};
int entityKategorien();
const EntityKat& entityKategorieInfo(int k);
int entityKategorie(const MapEntity& e);
std::string entitySkriptName(const MapEntity& e);
bool entityOrt(const MapEntity& e, float out[3]);
void zurEntityFliegen(std::size_t i);
void entityMenue(std::size_t i);
bool verschiebeImAblauf(const Path& p, double delta, bool nurDieser);
void openEditorForNode(const Path& p);
std::vector<Path> selectionOrCurrent();
bool isSelected(const Path& p);
void selectRow(std::size_t rowIndex, bool ctrl, bool shift);
void doLoadMap();

// Die drei Ansichten der linken Spalte.
void drawMapToolbar();
void drawPickedEntity();
void drawEntityLayers(float height);
CameraState camStateAt(double ms);
// Die MOVE-Schluessel des Skripts, in der Reihenfolge, in der App::selectedKey
// zaehlt: Ort, Winkel (erstes PAN danach, sonst das vorige) und Zeile.
struct KameraSchluessel {
    float pos[3]{};
    float ang[3]{};
    bool hasAng = false;
    Path path;
};
std::vector<KameraSchluessel> kameraSchluessel(const Script& script);
// Der Schluessel, der zur Zeit ms gilt (letzter begonnener MOVE), oder -1.
int schluesselFuerZeit(double ms);
// Eine Karte ueber ihre Kennung aus den Archiven holen (Mission aus .pk3).
bool loadMapFromArchive(const std::string& id);
void selectByPath(const Path& p);
void drawMapPanel();
void drawMapSidebar();
void drawMapView();
void drawModelToolbar();
void drawModelPanel();
void drawModelView();
void drawTimeline();
void drawCameraGizmos();

// Kartendaten laden - aus einer Datei oder aus einem Archiv.
void loadMap(const std::string& path);
void loadMapFromBytes(const std::string& bytes, const std::string& shownPath);
// Darf beendet werden? Prueft ALLE Reiter, nicht nur den aktiven.
bool confirmQuit();
// Die Gizmo-Vorschau ins Skript schreiben - ueber replaceAt, also mit
// Rueckgaengig.
bool gizmoWriteBack();
// Die Drehung ins Skript schreiben - sie gehoert zum PAN, nicht zum MOVE.
bool gizmoWriteAngles();
// Vor dem Wegwerfen fragen und DANACH weitermachen - siehe app.cpp.
void withUnsaved(std::function<void()> then,
                 std::function<void()> onCancel = nullptr);
void drawAskSave();
void loadMapTextures();
// Die Textur zu EINEM Shadernamen. Auch die .md3-Modelle gehen diesen Weg -
// zwei getrennte waeren zwei Gelegenheiten, verschieden falsch zu liegen.
bool loadTextureFor(const std::string& shaderName, TextureSet::Tex& out);
// Sagt im Detailprotokoll, WO eine fehlende Datei stattdessen liegt - oder
// dass ihr Name in keinem Archiv vorkommt. Ohne das heisst "nicht
// gefunden" nur "such weiter, irgendwo".
void reportMissingTexture(const std::string& name);
// In app_view3d.cpp definiert.
//
// WICHTIG: diese Deklarationen gehoeren HIERHER, nicht in den anonymen
// Namensraum von app.cpp. Ein anonymer Namensraum ist je Uebersetzungseinheit
// ein eigener - eine Deklaration darin verspricht eine Definition in
// DERSELBEN Datei. Steht sie woanders, meldet der Uebersetzer eine
// deklarierte, aber nie definierte Funktion, und das ist genau richtig so.
struct ComboEditResult {
    bool edited = false;
    bool picked = false;
};
ComboEditResult comboEdit(const char* id, std::string& value,
                          const std::vector<TypeEntry>* entries, float width);
void loadModelTextures();
void applySkinByIndex(int index);
void loadActorModels();
void openScriptFromMemory(const std::string& data, const std::string& shownName);
// Ein Modell aus den Archiven ins Modellfenster laden (pk3-Browser, Selbsttest).
bool oeffneModellAusArchiv(const std::string& name, std::string* fehler);
// Selbsttest (BHED_EDITORTEST): Ruhe des Ereigniseditors und Klickpruefung
// der Knoepfe. Einmal je Bild am Ende von draw(); ohne die Variable leer.
void selbsttest();
// Ja/Nein-Frage stellen; `ja`/`nein` laufen nach dem Schliessen.
void frage(const std::string& text, std::function<void()> ja,
           std::function<void()> nein = {});
void drawFrage();
// Welcher Knopf neben einem Feld des Ereigniseditors steht - wie im
// Original (Abgleich 27.09., jeder Befehl geoeffnet):
//   Keiner  die fuehrende Klappliste (SET_TYPES, CHANNELS, PLAY_TYPES,
//           DECLARE_TYPE, CAMERA_COMMANDS) - sie ist die Auswahl selbst;
//   Helfer  jedes Feld HINTER einer fuehrenden Klappliste: "Helper", nur
//           die Helferzeilen seines Typs (str: Get, Zahl: Get + Rnd,
//           vec: Get + Tag), zurueck mit "Revert";
//   Expr    alle Felder der uebrigen Befehle, auch Klapplisten wie
//           FLUSH/INSERT bei affect: "Expr!", volle Helferzeilen.
enum class FeldKnopf : std::uint8_t { Keiner, Expr, Helfer };
FeldKnopf feldKnopf(const Command& c, std::size_t feld);
// Ist die Klappliste eine Auswahl, deren Eintraege eigene Felder mitbringen
// (SET_TYPES: SET_HEALTH will <int>, SET_ORIGIN <vec>)?
bool auswahlMitFeldern(const CommandDb& db, const std::string& typeset);
// Verschobenes wiederfinden: Kennungen vor der Bewegung merken, danach die
// Knoten mit diesen Kennungen markieren (und ihre Bloecke aufklappen).
std::vector<Kennung> kennungenVon(const std::vector<Path>& wege);

// Ein schwebendes Fenster zeigen UND nach vorn holen.
//
// `id` ist die feste Kennung aus dem Titel ("###find"). Nur den Merker zu
// setzen reicht nicht: stand das Fenster schon offen, aber verdeckt, blieb
// es verdeckt - gemeldet als "I am unable to open it again".
void fensterZeigen(bool& offen, const char* id);

// Kamera, Zeitleiste, Szene, Mover und Effekte aus dem aktiven Skript neu
// aufbauen (app_view3d.cpp).
void prepareScene();

// Ersetzen aus dem Find-Fenster (siehe ersetzeInKnoten in edit.h).
// ersetzeEinen: steht die Auswahl auf einem Treffer, wird er ersetzt, dann
// geht es zum naechsten - wie "Replace" in Notepad++. ersetzeAlle: alle
// Treffer, je Dokument EIN Rueckgaengig-Schritt; alleDokumente nimmt die
// anderen Reiter mit. Rueckgabe: Zahl der ersetzten Felder.
int ersetzeEinen();
int ersetzeAlle(bool alleDokumente);

// Die wirkende Belegung einer Aktion (Vorgabe oder eigene) und ihr Name -
// fuer den Selbsttest; in app.cpp heissen sie chordFor/chordName.
ImGuiKeyChord kuerzelVon(keys::Action a);
const char* kuerzelName(keys::Action a);
// Feste Kennung eines Reiters (Parked::uid), -1 fuer eine ungueltige Nummer.
int tabKennung(int tab);
// Einen Reiter schliessen wie mit dem x am Reiter (fragt bei Aenderungen).
void reiterSchliessen(int index);
// Nur Schritt i des Undo-Stapels zuruecknehmen (0 = aeltester), die spaeteren
// bleiben. Ein neuer Schritt. `bericht`: was zurueckgenommen wurde.
bool einzelnenSchrittZuruecknehmen(std::size_t i, std::string* bericht);
// Aenderungsrand je Zeile: 0 nichts, 1 geaendert (orange), 2 geaendert und
// gespeichert (gruen), 3 nach dem Speichern wieder wie beim Oeffnen (blau).
enum : std::uint8_t { kMarkeKeine = 0, kMarkeGeaendert = 1, kMarkeGespeichert = 2, kMarkeZurueck = 3 };
void aenderungsStandNeu();         // aktiver Reiter: Originalstand = jetzt
// "Revert to original": die inhaltlich geaenderten unter `wege`, mit ihrem Original.
std::vector<std::pair<Path, Node>> zuruecksetzbar(const std::vector<Path>& wege);
// Die Vorschau dazu: wie sah dieser Befehl beim Oeffnen aus? Leer, wenn es
// nichts zurueckzusetzen gibt. `neu` wird gesetzt, wenn der Befehl erst
// nach dem Oeffnen entstanden ist (dann gibt es kein Original).
std::string originalZeile(const Path& weg, bool* neu = nullptr);
// Die Vorschau als EINE Zeile: "wait ( 3000 )", bei mehreren "... (+2)",
// lange Zeilen gekuerzt. Leer, wenn nichts zurueckzusetzen ist.
std::string originalKurz(const std::vector<std::pair<Path, Node>>& zurueck);
// Zeichnet sie in einen offenen Hinweis: grau "Original:", daneben der Befehl.
void originalVorschau(const std::vector<std::pair<Path, Node>>& zurueck);
void aenderungenGespeichert();     // aktiver Reiter: gespeicherter Stand = jetzt
std::vector<std::uint8_t> zeilenMarken(int tab, const Script& s, const std::vector<Row>& rows);
bool hatLesezeichen(int tab, Kennung k);
void lesezeichenUmschalten(const std::vector<Path>& wege);
void lesezeichenSpringen(bool vor);
void lesezeichenAlleWeg();
void lesezeichenLaden();           // aktiver Reiter, aus den Einstellungen
void lesezeichenMerken();          // aktiver Reiter, in die Einstellungen
// Ein Befehlssymbol als Vektorzeichnung (gui/vektoricons.cpp). false, wenn
// es das Symbol nicht als Zeichnung gibt.
bool vektorIcon(ImDrawList* dl, const char* name, ImVec2 pos, float groesse, bool alt);
void markiereKennungen(const std::vector<Kennung>& ks);
// Zwischenablage wie im Original: intern UND als Text "//(BHVD)" + Skript
// in der Windows-Zwischenablage.
void ablageNachWindows();
void kopieren(bool ausschneiden);                   // die Auswahl
void kopiereWeg(const Path& weg, bool ausschneiden); // eine Zeile (Rechtsklick)
void einfuegen(const Path& hinter);
// Zwischen ImGui_ImplWin32_NewFrame und ImGui::NewFrame: meldet die
// simulierte Maus des Selbsttests als LETZTE Stelle. Sonst leer.
void selbsttestVorBild();
// Wie ein Klick in die Ereignisliste: Befehl hinter die Auswahl setzen.
void fuegeBefehlEin(const Command& c);
void neuerReiter();
// Wie "Open" nach dem Dateidialog.
void ladeSkriptDatei(const std::string& pfad);
void selbsttestMerkeElement(ImGuiContext* ctx, ImGuiID id, const ImRect& bb);
void selbsttestMerkeText(ImGuiContext* ctx, ImGuiID id, const char* label);

// Auf einen Reiter wechseln, von aussen.
//
// switchTab() selbst hat Voreinstellungen und bleibt in app.cpp; sie hier
// noch einmal hinzuschreiben waere eine zweite Wahrheit, und genau daran
// ist in dieser Sitzung schon mehr als einmal etwas auseinandergelaufen.
void activateTab(int index);
// Vor dem Wegwerfen fragen und DANACH weitermachen.
//
// Kein bool mehr: die Frage steht im eigenen Stil im Bild und dauert
// mehrere Bilder. Was danach geschehen soll, wird mitgegeben.
void withUnsaved(std::function<void()> then,
                 std::function<void()> onCancel);
int frameForAnimation(const std::string& name, double sinceMs, bool hold);
// Schreibt fuer jede Figur eine Zeile ins Detailprotokoll: was sie an der
// Zeitmarke gerade tut. Auf Zuruf ueber das Ansichtsmenue.
void dumpActorsAtPlayhead();
// Einen Klang entschluesselt und gepuffert holen, fuer die Mundanimation.
// Gibt nullptr, wenn er nicht gefunden oder nicht lesbar ist.
const sound::Sound* soundFor(const std::string& name);
// Ein Klang fertig fuer die Ausgabe (44,1 kHz Stereo), zwischengespeichert.
const std::vector<std::int16_t>* klangFuerWiedergabe(const std::string& name);
// Alle Klaenge der Zeitleiste vorladen - vor dem Start der Uhr.
void klaengeVorladen();
ModelTextures texturesFor(const GlmModel& model);
void insertCameraHere();
void loadSkeletonForModel();
void openModelFile();
void openSkinFile();
void openSkeletonFile();
void openEntFile();
void addGamePathDirectory(const std::string& dir);
void loadMissionArchive();
// Die Missionen eines Archivs suchen und die Auswahl zeigen - das, was
// "Load mission..." nach dem Dateidialog tut.
void missionenAusArchiv(const std::string& archiv);
void loadMission(const Mission& m);
void drawMissionPicker();

}  // namespace bhed::gui
#endif

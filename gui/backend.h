// backend.h - Grafikschnittstelle, austauschbar
//
// Zwei Umsetzungen, wie bei efxed: Direct3D 11 und OpenGL 3.3. Der Rueckfall
// ist kein Uebereifer - auf Rechnern ohne Hardware-Direct3D (etwa in einer
// virtuellen Maschine oder mit reinem WARP) startet die D3D-Fassung entweder
// gar nicht oder zeichnet unertraeglich langsam.
#ifndef BHED_BACKEND_H
#define BHED_BACKEND_H

#include <cstdint>
#include <string>

struct HWND__;
using HWNDHandle = HWND__*;

namespace bhed::render {

enum class Backend : std::uint8_t {
    Direct3D11,
    OpenGL3,
};

// Was tatsaechlich lief - fuer die Anzeige und das Fehlerprotokoll.
const char* backendName(Backend b);

// Startet die Schnittstelle am Fenster. false, wenn sie nicht verfuegbar ist;
// dann darf der Aufrufer die andere versuchen.
bool init(Backend b, HWNDHandle hwnd);
void shutdown();

void newFrame();
// Zeichnet die Daten von ImGui::GetDrawData() und tauscht die Puffer.
void present(float r, float g, float bl);
void resize(int width, int height);

// --- Zugang zum Direct3D-Geraet ----------------------------------------
//
// Als void*, damit dieser Kopf ohne Windows-Kopfdateien auskommt - er wird
// auch auf Linux gelesen (tools/check_gui.sh uebersetzt die Oberflaeche mit
// g++). Der Aufrufer castet auf ID3D11Device* bzw. ID3D11DeviceContext*.
//
// Gibt nullptr zurueck, wenn nicht Direct3D laeuft (also unter OpenGL oder
// vor init). Wer das nicht prueft, stuerzt beim Wechsel der Schnittstelle
// ab - und der Wechsel passiert automatisch, wenn D3D nicht verfuegbar ist
// (siehe die Notiz oben zu WARP).
[[nodiscard]] void* d3dDevice();
[[nodiscard]] void* d3dContext();

// Eine eigene Textur fuer die Symbolleiste anlegen. RGBA, 8 Bit je Kanal.
//
// Bewusst NICHT ueber ImFontAtlas::AddCustomRect: das ist in ImGui 1.92 als
// "[ALPHA] Custom Rectangles/Glyphs API" gekennzeichnet, GetCustomRect
// dereferenziert TexData ungeprueft, und ein io.Fonts->Clear() beim
// Schriftneubau macht alle Kennungen ungueltig. Eine eigene Textur ueberlebt
// beides und haengt an nichts, was sich zwischen zwei Fassungen aendert.
[[nodiscard]] void* createTexture(const unsigned char* rgba, int width, int height);

// Inhalt einer bestehenden Textur ersetzen. Fuer die Kartenansicht: dort
// entsteht jedes Bild neu, und jedes Mal eine Textur anzulegen und die alte
// wegzuwerfen waere Verschwendung.
void updateTexture(void* handle, const unsigned char* rgba, int width, int height);
void destroyTextures();

Backend current();
bool ready();

// Selbsttest: das naechste fertig gezeichnete Bild als BMP speichern (nur
// Direct3D 11). Liest den eigenen Grafikpuffer - also genau das, was
// behaved zeichnet, und nichts vom restlichen Bildschirm.
void fotoAnfordern(const std::string& pfad);

}  // namespace bhed::render
#endif

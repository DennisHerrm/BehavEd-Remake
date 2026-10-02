// gpumap_win32.cpp - die Karte ueber Direct3D 11 zeichnen
//
// ACHTUNG: UNGEPRUEFT
// -------------------
// Diese Datei ist die einzige im GPU-Weg, die ich beim Schreiben weder
// uebersetzen noch ausfuehren konnte - auf dem Rechner, auf dem sie
// entstanden ist, gibt es kein Direct3D und keine Grafikkarte. Jede Zeile
// hier ist eine Vermutung, bis sie auf einem Windows-Rechner gelaufen ist.
//
// Deshalb ist sie so duenn wie moeglich gehalten. Alles, was sich pruefen
// laesst, steht woanders und ist dort abgesichert:
//
//   src/gpustate.cpp   Shaderzustand -> Zustand der Grafikkarte
//   src/gpushader.cpp  HLSL aus den Merkmalsbits
//   src/gpudraw.cpp    Reihenfolge und Zusammenfassen der Aufrufe
//   src/gpuconst.cpp   das Packen der Konstanten
//
// Diese Datei tut nur noch: Puffer anlegen, Shader uebersetzen, Zustaende
// setzen, DrawIndexed. Wenn das Bild falsch aussieht, liegt der Fehler mit
// hoher Wahrscheinlichkeit hier - die anderen vier sind geprueft.
//
// Wie abgenommen wird
// -------------------
// Als SCHALTER neben dem Rasterer, nicht als Ersatz. Nur so laesst sich im
// selben Bau umschalten und vergleichen. Ein GPU-Weg, der den Rasterer
// ersetzt, waere nicht abnehmbar: man saehe ein Bild und wuesste nicht, ob
// es richtig ist.

#include "gpumap.h"
#include "bhed/diag.h"

#ifdef BHED_WITH_D3D11

#include <d3d11.h>
#include <dxgi1_4.h>
#include <psapi.h>
#include <d3dcompiler.h>
#include <d3d11shader.h>

#include <array>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <map>
#include <string>
#include <vector>

#include "backend.h"
#include "bhed/gpuconst.h"
#include "bhed/gpudraw.h"
#include "bhed/gpushader.h"
#include "bhed/gpucam.h"
#include "bhed/gpuskin.h"

namespace bhed::gpu {

namespace {

// Ein uebersetztes Shaderpaar, nach Merkmalsbits abgelegt.
//
// Der Name aus shaderName() ist der Schluessel: gleiche Bits heissen
// derselbe Shader. Ohne das Wiederverwenden wuerden aus zwei Dutzend
// Zustaenden hunderte Uebersetzungen je Bild.
struct ShaderPaar {
    ID3D11VertexShader* vs = nullptr;
    ID3D11PixelShader* ps = nullptr;
    ID3D11InputLayout* layout = nullptr;
};

std::map<std::string, ShaderPaar> g_shader;
// --- Puffer je NETZ, nicht global ------------------------------------
//
// Es gibt zwei: die Karte (aendert sich nur beim Kartenwechsel) und das
// Effektnetz (aendert sich JEDES Bild). Mit einem gemeinsamen Puffer wuerde
// jedes Bild die Karte neu hochladen - 106318 Dreiecke, weil daneben ein
// paar hundert Teilchen stehen.
struct Netzpuffer {
    ID3D11Buffer* vb = nullptr;
    ID3D11Buffer* ib = nullptr;
    std::size_t vbGroesse = 0;
    std::size_t ibGroesse = 0;
    // Die Schrittweite einer Ecke.
    //
    // `Ecke` ist eine lokale Struktur in `zeichneKarte`; wer den Puffer
    // sonst binden will, kaeme an ihre Groesse nicht heran und muesste sie
    // nachbauen - zwei Fassungen derselben Zahl, und die eine laeuft
    // irgendwann von der anderen weg. Der Puffer weiss es selbst.
    UINT schrittweite = 0;
    // --- Woher der Puffer stammt ------------------------------------------
    //
    // Die Groesse allein reicht NICHT als Kennzeichen. Das Netz wird neu
    // gebaut, wenn sich die Detailstufe aendert, wenn eine Tuer aufgeht,
    // wenn die Zeitleiste laeuft - und dabei kann es gleich gross bleiben
    // oder kleiner werden.
    //
    // Vorher wurde nur bei ZU KLEINEM Puffer neu uebertragen. Die Folge:
    // "wenn ich den Detailgrad auf 10 drehe verschwinden die Tueren" - im
    // Puffer standen noch die Ecken des alten Netzes, waehrend die Indizes
    // schon zum neuen gehoerten.
    //
    // Zeiger und Zahlen zusammen: derselbe Zeiger mit derselben Groesse ist
    // dasselbe Netz. Ein neu gebautes Netz liegt fast immer woanders, und
    // wenn nicht, unterscheiden sich die Zahlen.
    const void* quelleVerts = nullptr;
    const void* quelleIdx = nullptr;
    std::size_t nVerts = 0;
    std::size_t nIdx = 0;
    // Wieviele Bytes zuletzt drin standen - bei DYNAMIC wird ueberschrieben,
    // nicht neu angelegt.
    bool dynamisch = false;
    // Welche Stapel Autosprite sind (Summe der Nummern + Anzahl). Die
    // Shader kommen mit den Texturen, und die koennen NACH dem ersten
    // Hochladen kommen oder neu geladen werden - dann muss der Puffer neu.
    std::size_t spriteZeichen = 0;
};
// Drei Plaetze: Karte, Effektnetz, Mover. Mover teilen sich einen Platz -
// sie werden nacheinander gezeichnet, und jeder bringt seine eigenen Ecken
// mit. Ein Platz je Mover waere bei zwoelf Modellen zwoelfmal derselbe
// Aufwand fuer Netze, die sich nie aendern.
// Platz 0 = Karte, Platz 1 = Effektnetz. Platz 2 wird NICHT mehr benutzt.
Netzpuffer g_netz[2];

// --- Ein Puffer JE MOVER-NETZ ------------------------------------------
//
// Vorher teilten sich alle Mover einen Platz. Der wurde je Mover
// freigegeben und neu angelegt - zwoelfmal je Bild -, und dabei ging einer
// davon daneben:
//
//     GPU: 811 Aufrufe, 0 uebersprungen
//     GPU: Ecken fehlen - die beiden Bedingungen sind auseinandergelaufen
//
// Kein Stapel wurde ausgelassen, der ganze Mover fiel aus. Genau die halbe
// Tuer.
//
// Derselbe Aufbau wie bei den Figuren (g_figuren): Schluessel ist das Netz,
// angelegt beim ersten Mal, danach nur noch gebunden. Mover-Netze aendern
// sich nicht - nur ihre Stellung, und die steckt in der Matrix.
//
// Das ist nebenbei schneller: kein Neuanlegen je Bild.
std::map<const BspMesh*, Netzpuffer> g_moverNetze;
ID3D11Buffer* g_cbBild = nullptr;
ID3D11Buffer* g_cbStapel = nullptr;
ID3D11SamplerState* g_sampler = nullptr;
// --- Ein zweiter Abtaster fuer die Vollbild-Durchgaenge ----------------
//
// `g_sampler` steht auf WRAP. Das ist fuer Kartentexturen richtig - sie
// kacheln, und ohne Wiederholung waere jede Wand einmal texturiert und
// danach verschmiert.
//
// Fuer einen VOLLBILDdurchgang ist es falsch. Der Gluehdurchgang zeichnet
// fuenfmal weich und greift dabei um bis zu drei Bildpunkte ueber den Rand
// hinaus. Mit WRAP holt er sich dort die GEGENUEBERLIEGENDE Kante: shank
// steht vor einem Lavastrom am unteren Bildrand, und quer ueber den OBEREN
// Rand liegt ein leuchtender Streifen.
//
// CLAMP holt stattdessen den Randbildpunkt selbst - genau das, was ein
// Weichzeichner am Rand tun soll.
ID3D11SamplerState* g_samplerRand = nullptr;
std::map<std::uint32_t, ID3D11BlendState*> g_blend;
std::map<std::uint32_t, ID3D11DepthStencilState*> g_depth;
std::map<std::uint32_t, ID3D11RasterizerState*> g_raster;

// --- Das Renderziel ---------------------------------------------------
//
// Der Rasterer schreibt in ein Bild im Arbeitsspeicher, das danach als
// Textur hochgeladen wird. Der GPU-Weg zeichnet stattdessen direkt in eine
// Textur - dieselbe, die ImGui dann anzeigt. Deshalb braucht er ein eigenes
// Ziel samt Tiefenpuffer.
ID3D11Texture2D* g_zielTex = nullptr;
ID3D11RenderTargetView* g_zielRtv = nullptr;
ID3D11ShaderResourceView* g_zielSrv = nullptr;
ID3D11Texture2D* g_tiefeTex = nullptr;
ID3D11Texture2D* g_tiefeLesen = nullptr;   // Kopie zum Zuruecklesen (leseTiefe)
ID3D11DepthStencilView* g_tiefeDsv = nullptr;
int g_zielW = 0;
int g_zielH = 0;

// Hochgeladene Karten-Texturen, nach Platz im Texturensatz.
//
// Ohne diesen Zwischenspeicher wuerde jede Textur JE BILD neu hochgeladen -
// bei 102 Texturen und 60 Bildern je Sekunde waere das mehr Arbeit als der
// Rasterer je hatte.
std::map<int, ID3D11ShaderResourceView*> g_bilder;
// Woraus jedes Bild hochgeladen wurde (Zeiger auf die Pixel, Breite, Hoehe).
// Zweite Sicherung neben vergissKarte(): passt die Quelle nicht mehr,
// wird neu hochgeladen statt das alte Bild zu zeigen.
struct BildQuelle {
    const void* px = nullptr;
    int w = 0;
    int h = 0;
    bool operator==(const BildQuelle& o) const { return px == o.px && w == o.w && h == o.h; }
};
std::map<int, BildQuelle> g_bildQuelle;
std::map<int, BildQuelle> g_lightmapQuelle;
int g_veraltet = 0;

// Die Lightmaps, getrennt.
//
// Sie liegen in `BspGeometry::lightmaps`, NICHT im Texturensatz - und genau
// diese Verwechslung war der erste Fehler des GPU-Wegs: `Batch::lightmap`
// wurde in `textures->byShader` nachgeschlagen. Das gab meist nichts, der
// Shader multiplizierte mit einer leeren Textur, und jede Flaeche MIT
// Lightmap kam schwarz heraus. Die Lava - ohne Lightmap - sah richtig aus.
//
// Ein Bild, auf dem genau ein Ding stimmt, ist der beste Hinweis: es zeigt,
// welche Eigenschaft die schwarzen Flaechen von der richtigen unterscheidet.
std::map<int, ID3D11ShaderResourceView*> g_lightmaps;

ID3D11Device* dev() {
    return static_cast<ID3D11Device*>(render::d3dDevice());
}
ID3D11DeviceContext* ctx() {
    return static_cast<ID3D11DeviceContext*>(render::d3dContext());
}

// --- Zeitmessung auf der Grafikkarte ----------------------------------
//
// Drei Saetze Abfragen im Ring. Geschrieben wird in `g_messJetzt`,
// abgeholt wird der AELTESTE - der ist dann zwei Bilder alt und mit
// grosser Wahrscheinlichkeit fertig. Ist er es nicht, wird nichts
// abgeholt und nichts gewartet.
//
// Warum nicht ein Satz? Weil GetData auf einen Wert aus DEMSELBEN Bild
// die CPU anhaelt, bis die Karte durch ist. Das misst dann das Warten
// und macht aus 300 fps 60.
//
// D3D11_ASYNC_GETDATA_DONOTFLUSH ist kein Beiwerk: ohne das Wort schiebt
// jede Abfrage den Befehlsstrom vorzeitig los.
constexpr int kMessTiefe = 5;
constexpr int kAbschnitte = static_cast<int>(Abschnitt::kAnzahl);

struct MessSatz {
    ID3D11Query* disjoint = nullptr;
    ID3D11Query* anfang[kAbschnitte] = {nullptr};
    ID3D11Query* ende[kAbschnitte] = {nullptr};
    // Welche Abschnitte in DIESEM Bild wirklich beide Marken bekommen
    // haben. Ein Abschnitt, der nur begonnen wurde, darf nicht abgeholt
    // werden - GetData auf eine Abfrage, die nie mit End() geschlossen
    // wurde, wird niemals fertig.
    bool vollstaendig[kAbschnitte] = {false};
    bool begonnen[kAbschnitte] = {false};
    bool offen = false;   // zwischen bildBeginnt und bildFertig
    bool abzuholen = false;
};

MessSatz g_mess[kMessTiefe];
int g_messJetzt = 0;
bool g_messGeht = false;      // Zeitmarken vom Geraet unterstuetzt?
bool g_messVersucht = false;
// -1 heisst "nicht gemessen". Das muss fuer JEDEN Abschnitt gelten.
//
// Vorher stand hier `float g_messWert[kAbschnitte] = {-1.0F};`. Das belegt
// nur Element 0 mit -1; die uebrigen sieben bekommen 0. In shanks
// rc530-Protokoll stand deshalb bei 0 Zeichenaufrufen:
//
//     Karte deckend   -1.000      <- ehrlich
//     Karte gemischt   0.000      <- gelogen, es wurde nie gemessen
//     ... sechs weitere 0.000
//
// Eine 0,000 ms liest sich wie "gemessen und schnell". Kein Uebersetzer
// meldet das, auch nicht mit -Wall -Wextra -Wmissing-field-initializers.
// `tools/lint_feldinit.py` prueft es jetzt.
//
// Dieselbe Lehre wie bei den Spaltenbreiten und bei den Protokolloechern:
// eine Zahl, die man nicht gesetzt hat, ist keine Zahl, die stimmt.
constexpr std::array<float, kAbschnitte> keineMessungen() {
    std::array<float, kAbschnitte> a{};
    for (float& w : a) { w = -1.0F; }
    return a;
}
std::array<float, kAbschnitte> g_messWert = keineMessungen();

void gibMessungenFrei() {
    for (MessSatz& m : g_mess) {
        if (m.disjoint != nullptr) { m.disjoint->Release(); m.disjoint = nullptr; }
        for (int i = 0; i < kAbschnitte; ++i) {
            if (m.anfang[i] != nullptr) { m.anfang[i]->Release(); m.anfang[i] = nullptr; }
            if (m.ende[i] != nullptr) { m.ende[i]->Release(); m.ende[i] = nullptr; }
            m.vollstaendig[i] = false;
            m.begonnen[i] = false;
        }
        m.offen = false;
        m.abzuholen = false;
    }
    g_messJetzt = 0;
    g_messGeht = false;
    g_messVersucht = false;
    for (float& w : g_messWert) { w = -1.0F; }
}

// Legt die Abfragen an, einmal. Schlaegt das fehl - Merkmalsstufe 9.x
// kennt keine Zeitmarken -, wird nicht erneut versucht und nicht gemessen.
bool messungBereit() {
    if (g_messVersucht) { return g_messGeht; }
    g_messVersucht = true;
    ID3D11Device* d = dev();
    if (d == nullptr) { return false; }
    for (MessSatz& m : g_mess) {
        D3D11_QUERY_DESC qd{};
        qd.Query = D3D11_QUERY_TIMESTAMP_DISJOINT;
        if (FAILED(d->CreateQuery(&qd, &m.disjoint))) {
            gibMessungenFrei();
            g_messVersucht = true;
            return false;
        }
        qd.Query = D3D11_QUERY_TIMESTAMP;
        for (int i = 0; i < kAbschnitte; ++i) {
            if (FAILED(d->CreateQuery(&qd, &m.anfang[i])) ||
                FAILED(d->CreateQuery(&qd, &m.ende[i]))) {
                gibMessungenFrei();
                g_messVersucht = true;
                return false;
            }
        }
    }
    g_messGeht = true;
    return true;
}

// Den aeltesten Satz abholen, wenn er fertig ist.
void holeMessung() {
    ID3D11DeviceContext* c = ctx();
    if (c == nullptr) { return; }
    MessSatz& m = g_mess[(g_messJetzt + 1) % kMessTiefe];
    if (!m.abzuholen) { return; }

    D3D11_QUERY_DATA_TIMESTAMP_DISJOINT dj{};
    if (c->GetData(m.disjoint, &dj, sizeof(dj),
                   D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK) {
        return;   // noch nicht fertig - naechstes Bild wieder
    }
    m.abzuholen = false;
    // Die Uhr wurde waehrenddessen verstellt (Energiesparen, ein anderer
    // Prozess). Die Werte dieses Bildes sind wertlos; die alten bleiben
    // stehen, statt dass eine Zahl springt.
    if (dj.Disjoint != 0 || dj.Frequency == 0) { return; }

    for (int i = 0; i < kAbschnitte; ++i) {
        if (!m.vollstaendig[i]) { continue; }
        UINT64 a = 0;
        UINT64 e = 0;
        if (c->GetData(m.anfang[i], &a, sizeof(a),
                       D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK ||
            c->GetData(m.ende[i], &e, sizeof(e),
                       D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK) {
            continue;
        }
        if (e < a) { continue; }
        g_messWert[i] = static_cast<float>(
            (static_cast<double>(e - a) * 1000.0) /
            static_cast<double>(dj.Frequency));
    }
}

// Ein Shaderpaar besorgen - uebersetzen, falls es noch keins gibt.
//
// Fehler beim Uebersetzen geben nullptr zurueck und werden NICHT still
// verschluckt: ein Stapel, der ohne Shader gezeichnet wird, ist unsichtbar,
// und ein unsichtbarer Stapel sieht aus wie ein Loch in der Karte. Der
// Aufrufer soll das melden koennen.
// --- Shader mit Debugangaben uebersetzen -------------------------------
//
// RenderDoc kann einen Shader nur dann schrittweise verfolgen, wenn er die
// Namen der Konstanten und die Quellzeilen mitbekommen hat. Dafuer sind
// zwei Fahnen zustaendig (RenderDoc, "How do I debug a shader?"):
//
//   D3DCOMPILE_DEBUG              Namen und Quellzeilen bleiben drin
//   D3DCOMPILE_SKIP_OPTIMIZATION  ohne sie springt der Ablauf wild
//
// NICHT immer an: der Shader wird groesser und langsamer, und behaved soll
// im Alltag schnell sein. `BHED_D3D_SHADERDEBUG=1` schaltet sie ein, wenn
// jemand tatsaechlich eine Aufnahme macht.
UINT uebersetzFahnen() {
    static UINT wert = 0xFFFFFFFFU;
    if (wert == 0xFFFFFFFFU) {
        const char* e = std::getenv("BHED_D3D_SHADERDEBUG");
        wert = (e != nullptr && e[0] == '1')
                   ? (D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION)
                   : 0U;
    }
    return wert;
}

// --- Die Lagen im Stapelpuffer AUS DEM SHADER lesen ---------------------
//
// gpuconst.h rechnet die Lagen von Hand nach den HLSL-Packregeln aus. Einmal
// lag die Rechnung daneben: nach `float gDeform[4]` belegt das LETZTE
// Element nur .x seines Viererblocks, und `gDeformSpread` rutscht in .y
// dahinter - Lage 29, nicht 32. Der Shader las fuer den Ortsanteil der
// Welle eine Null, und der Lavafall in md_am_sith "pulsierte nur".
//
// Deshalb fragt der erste uebersetzte Vertexshader MIT Welle (nur dort ist
// der ganze Block in Gebrauch; sonst streicht der Uebersetzer ihn) den
// Uebersetzer selbst
// (D3DReflect) und vergleicht mit den Konstanten. Eine Abweichung steht im
// Protokoll, statt als falsches Bild aufzufallen.
void pruefeStapelLagen(ID3DBlob* vsBlob) {
    static bool geprueft = false;
    if (geprueft || vsBlob == nullptr) {
        return;
    }
    ID3D11ShaderReflection* r = nullptr;
    if (FAILED(D3DReflect(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(),
                          __uuidof(ID3D11ShaderReflection),
                          reinterpret_cast<void**>(&r))) || r == nullptr) {
        return;
    }
    geprueft = true;
    ID3D11ShaderReflectionConstantBuffer* cb = r->GetConstantBufferByName("JeStapel");
    D3D11_SHADER_BUFFER_DESC bd{};
    if (cb != nullptr && SUCCEEDED(cb->GetDesc(&bd))) {
        struct Soll { const char* name; std::size_t at; };
        const Soll soll[] = {{"gTexMatrix", kTexMatrixAt}, {"gTexOffTurb", kTexOffTurbAt},
                             {"gRgb", kRgbAt}, {"gAlphaSchwelle", kAlphaAt},
                             {"gDeform", kDeformAt}, {"gDeformSpread", kDeformSpreadAt}};
        std::string z = "GPU-Stapelpuffer (aus dem Shader gelesen):";
        bool gut = true;
        for (const Soll& s : soll) {
            ID3D11ShaderReflectionVariable* v = cb->GetVariableByName(s.name);
            D3D11_SHADER_VARIABLE_DESC vd{};
            if (v == nullptr || FAILED(v->GetDesc(&vd))) {
                continue;
            }
            const std::size_t ist = vd.StartOffset / 4U;
            z += " " + std::string(s.name) + "@" + std::to_string(ist);
            if (ist != s.at) {
                z += "(soll " + std::to_string(s.at) + ")";
                gut = false;
            }
        }
        z += gut ? " - passt" : " - WEICHT AB";
        diag::detail(z);
        if (!gut) {
            diag::write(diag::Level::Error, z);
        }
    }
    r->Release();
}

ShaderPaar* holeShader(std::uint32_t features, int numTexMods,
                       std::string* fehler) {
    const std::string name = shaderName(features, numTexMods);
    const auto it = g_shader.find(name);
    if (it != g_shader.end()) {
        return &it->second;
    }
    ID3D11Device* d = dev();
    if (d == nullptr) {
        return nullptr;
    }

    const std::string vsSrc = vertexShaderHlsl(features, numTexMods);
    const std::string psSrc = pixelShaderHlsl(features);

    ID3DBlob* vsBlob = nullptr;
    ID3DBlob* psBlob = nullptr;
    ID3DBlob* err = nullptr;
    auto meldung = [&](const char* was) {
        if (fehler != nullptr) {
            *fehler = std::string(was) + " (" + name + ")";
            if (err != nullptr) {
                *fehler += ": ";
                *fehler += static_cast<const char*>(err->GetBufferPointer());
            }
        }
    };

    if (FAILED(D3DCompile(vsSrc.data(), vsSrc.size(), name.c_str(), nullptr,
                          nullptr, "main", "vs_4_0", uebersetzFahnen(), 0, &vsBlob, &err))) {
        meldung("Vertex-Shader nicht uebersetzbar");
        if (err != nullptr) { err->Release(); }
        return nullptr;
    }
    if ((features & kDeform) != 0U) {
        pruefeStapelLagen(vsBlob);
    }
    if (FAILED(D3DCompile(psSrc.data(), psSrc.size(), name.c_str(), nullptr,
                          nullptr, "main", "ps_4_0", uebersetzFahnen(), 0, &psBlob, &err))) {
        meldung("Pixel-Shader nicht uebersetzbar");
        if (err != nullptr) { err->Release(); }
        vsBlob->Release();
        return nullptr;
    }

    ShaderPaar p;
    d->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(),
                          nullptr, &p.vs);
    d->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(),
                         nullptr, &p.ps);

    // Der Aufbau einer Ecke. Muss zu VSIn in gpushader.cpp passen - und zu
    // dem, was gleich in den Vertexpuffer geschrieben wird. Drei Stellen,
    // eine Wahrheit; wenn hier etwas verrutscht, sieht man verzerrte
    // Geometrie.
    const D3D11_INPUT_ELEMENT_DESC ein[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
         D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12,
         D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24,
         D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 1, DXGI_FORMAT_R32G32_FLOAT, 0, 32,
         D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 40,
         D3D11_INPUT_PER_VERTEX_DATA, 0},
    };
    // --- Die Zahl aus dem Feld nehmen, nicht danebenschreiben ------------
    //
    // Hier stand eine 5, waehrend das Feld sechs Eintraege hatte: ein
    // abgebrochener Lauf hatte `TEXCOORD 2` fuer die Autosprite-Mitte
    // ergaenzt. In rc457 habe ich das Feld aus `Ecke` wieder entfernt und
    // diesen Eintrag uebersehen - der Vertexshader kennt kein TEXCOORD2,
    // `CreateInputLayout` schlug fehl, `p.layout` blieb null, und JEDER
    // Zeichenaufruf wurde uebersprungen: "0 Aufrufe, 938 uebersprungen".
    //
    // Eine Zahl, die neben dem Feld steht, das sie zaehlt, laeuft irgendwann
    // davon. `std::size` kann das nicht.
    d->CreateInputLayout(ein, static_cast<UINT>(std::size(ein)),
                         vsBlob->GetBufferPointer(),
                         vsBlob->GetBufferSize(), &p.layout);

    vsBlob->Release();
    psBlob->Release();

    if (p.vs == nullptr || p.ps == nullptr || p.layout == nullptr) {
        meldung("Shader angelegt, aber unvollstaendig");
        return nullptr;
    }
    g_shader[name] = p;
    return &g_shader[name];
}

// Die Mischformel als Zustand. Nach Blend abgelegt, damit jeder nur einmal
// angelegt wird.
// --- Tiefe und Keulen: berechnet, aber nie gesetzt ---------------------
//
// `PipelineState` traegt seit rc39x `depthWrite`, `depthTestEqual` und
// `depthTestOff`, und die Merkmalsbits `kCullNone`/`kCullBack` stehen
// ebenfalls fest. Angewandt wurde nichts davon: `g_depth` und `g_raster`
// wurden angelegt und beim Aufraeumen freigegeben, aber nie gefuellt und
// nie gesetzt. Direct3D nahm also durchweg seine Vorgaben -
// `DepthFunc = LESS`, `DepthWriteMask = ALL`, `CullMode = BACK`.
//
// Was daraus folgte:
//
//   * Gemischte Flaechen SCHRIEBEN Tiefe. Was hinter Glas oder Rauch
//     liegt, wurde danach verworfen.
//   * `depthFunc equal` - die uebliche Redewendung fuer eine zweite Stufe
//     auf derselben Flaeche - wurde als LESS gerechnet und verwarf alles.
//   * `cull twosided` (789 Vorkommen) und `cull back` galten nicht;
//     zweiseitige Flaechen zeigten nur eine Seite.
//
// Der Schluessel ist der Zustand selbst, nicht sein Name: gleiche
// Einstellungen teilen sich einen Zustand, und mehr als eine Handvoll
// verschiedene kommen nicht vor.
// --- Ein Schalter zum Halbieren des Suchraums --------------------------
//
// rc434 brachte zwei neue Zustandsarten auf einmal, und shanks Bild war
// danach falsch. Welche der beiden es war, liess sich von hier aus nicht
// entscheiden - und Raten hat in diesem Projekt jedes Mal eine Runde
// gekostet.
//
// Deshalb: EIN Bau, drei Laeufe.
//
//     BHED_D3D_STATES=0   keine der beiden (Stand vor rc434)
//     BHED_D3D_STATES=1   nur Tiefe
//     BHED_D3D_STATES=2   nur Keulen
//     (nicht gesetzt)     beide - die Vorgabe
//
// Die Zahl steht auch im Absturzbericht, damit ein Bild nicht ohne sie
// zurueckkommt.
int zustandsSchalter() {
    static int wert = -1;
    if (wert < 0) {
        const char* e = std::getenv("BHED_D3D_STATES");
        wert = (e != nullptr) ? std::atoi(e) : 3;
        if (wert < 0 || wert > 3) { wert = 3; }
    }
    return wert;
}

bool tiefeAn() { return (zustandsSchalter() & 1) != 0; }
bool keulenAn() { return (zustandsSchalter() & 2) != 0; }

ID3D11DepthStencilState* holeTiefe(const PipelineState& p) {
    // Der Schluessel muss die Mischart mitfuehren: sie entscheidet ueber das
    // Schreiben mit. Ohne sie teilten sich eine deckende und eine gemischte
    // Flaeche mit gleichem depthWrite einen Zustand.
    const std::uint32_t schluessel =
        (p.depthWrite ? 1U : 0U) | (p.depthTestEqual ? 2U : 0U) |
        (p.depthTestOff ? 4U : 0U) |
        ((p.blend == Blend::Opaque) ? 8U : 0U);
    const auto it = g_depth.find(schluessel);
    if (it != g_depth.end()) {
        return it->second;
    }
    ID3D11Device* d = dev();
    if (d == nullptr) {
        return nullptr;
    }
    D3D11_DEPTH_STENCIL_DESC dd{};
    dd.DepthEnable = TRUE;
    // --- DECKEND schreibt IMMER Tiefe ---------------------------------
    //
    // `PipelineState::depthWrite` kommt aus dem Shaderskript und steht dort
    // auf FALSE, solange das Wort `depthwrite` nicht ausdruecklich
    // dasteht - also bei fast jeder Flaeche der Karte.
    //
    // Die erste Fassung von rc434 nahm das Feld allein als Antwort. Damit
    // schrieb NICHTS mehr Tiefe, der Tiefenpuffer blieb auf eins, und alles
    // zeichnete uebereinander: shanks Bild zeigte die Lavalandschaft durch
    // das ganze Gebaeude hindurch.
    //
    // Der Rasterer stellt die Frage anders (mapview.cpp:3012):
    //
    //     if (p.blend == BlendMode::Opaque || p.depthWrite) { depth = z; }
    //
    // Deckend schreibt immer; gemischt nur mit ausdruecklichem
    // `depthwrite`. Genau so macht es die Engine - sie loescht die
    // Tiefenmaske fuer jede Stufe mit blendFunc, ausser das Wort stand da
    // (tr_shader.cpp:1444).
    const bool schreibt = (p.blend == Blend::Opaque) || p.depthWrite;
    dd.DepthWriteMask = schreibt ? D3D11_DEPTH_WRITE_MASK_ALL
                                 : D3D11_DEPTH_WRITE_MASK_ZERO;
    if (p.depthTestOff) {
        // `depthFunc disable` heisst in der Engine: immer zeichnen. Der
        // Test faellt weg, das Schreiben nicht zwangslaeufig.
        dd.DepthFunc = D3D11_COMPARISON_ALWAYS;
    } else if (p.depthTestEqual) {
        // `depthFunc equal`. NICHT D3D11_COMPARISON_EQUAL: die Tiefe wurde
        // im ersten Durchgang mit einer anderen Rechnung erzeugt, und ein
        // Vergleich auf Gleichheit bei Fliesskomma trifft dann fast nie.
        // Die Engine meint "auf der schon gezeichneten Flaeche liegend",
        // und das ist LESS_EQUAL - genau das, was der Rasterer als
        // `folgestufe` fuehrt (mapview.cpp:2639, `z > depth[at]` statt
        // `z >= depth[at]`).
        dd.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
    } else {
        // Der Rasterer hat als Vorgabe LEqual (mapview.cpp:1051), nicht
        // Less. Bei zwei Flaechen auf derselben Tiefe gewinnt damit die
        // spaeter gezeichnete - das ist gewollt und die Voraussetzung
        // dafuer, dass Zusatzstufen ueberhaupt sichtbar werden koennen.
        dd.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
    }
    dd.StencilEnable = FALSE;
    ID3D11DepthStencilState* st = nullptr;
    if (FAILED(d->CreateDepthStencilState(&dd, &st))) {
        return nullptr;
    }
    g_depth[schluessel] = st;
    return st;
}

ID3D11RasterizerState* holeRaster(std::uint32_t features) {
    // Die Keulbits UND polygonOffset - alles andere aendert den
    // Rasterzustand nicht.
    const std::uint32_t schluessel =
        features & (kCullNone | kCullBack | kPolygonOffset);
    const auto it = g_raster.find(schluessel);
    if (it != g_raster.end()) {
        return it->second;
    }
    ID3D11Device* d = dev();
    if (d == nullptr) {
        return nullptr;
    }
    D3D11_RASTERIZER_DESC rd{};
    rd.FillMode = D3D11_FILL_SOLID;
    if ((schluessel & kCullNone) != 0U) {
        rd.CullMode = D3D11_CULL_NONE;
    } else if ((schluessel & kCullBack) != 0U) {
        // `cull back` der Engine wirft die VORDERE Seite weg.
        //
        // Das ist keine Verwechslung: in Quake-III-Shadern heisst
        // `cull back` "die Rueckseite ist die sichtbare", also wird die
        // Vorderseite verworfen (tr_shade.c, GL_CULL mit CT_BACK_SIDED ->
        // glCullFace(GL_FRONT)).
        rd.CullMode = D3D11_CULL_FRONT;
    } else {
        rd.CullMode = D3D11_CULL_BACK;
    }
    // Die Ecken kommen im Uhrzeigersinn aus dem BSP.
    rd.FrontCounterClockwise = FALSE;
    rd.DepthClipEnable = TRUE;
    // --- polygonOffset ------------------------------------------------
    //
    // 173 Vorkommen in den Shadern: Brandspuren, Schilder, Blut - alles,
    // was FLACH AUF einer Wand liegt. Ohne Vorsprung kaempfen die beiden
    // Flaechen um dieselbe Tiefe und flimmern gegeneinander.
    //
    // `kPolygonOffset` war gesetzt, gepackt und in den Shadernamen
    // aufgenommen - angewandt wurde es NIRGENDS. Weder im Shader noch im
    // Rasterzustand; `D3D11_RASTERIZER_DESC rd{}` setzt DepthBias auf null,
    // und dabei blieb es.
    //
    // Die Engine ruft `qglPolygonOffset(r_offsetFactor, r_offsetUnits)` mit
    // Faktor -1 und 2 Einheiten (tr_shade.c, r_offsetfactor/-units). In
    // Direct3D sind das dieselben zwei Zahlen, nur mit umgekehrtem
    // Vorzeichen: hier zieht ein NEGATIVER Wert zur Kamera.
    //
    // `SlopeScaledDepthBias` ist der wichtigere der beiden - er waechst mit
    // der Schraege der Flaeche. Genau darum geht es auch dem Rasterer: "in
    // der Ferne muss der Vorsprung groesser sein als in der Naehe"
    // (mapview.cpp:2101).
    if ((schluessel & kPolygonOffset) != 0U) {
        rd.DepthBias = -2;
        rd.SlopeScaledDepthBias = -1.0F;
        rd.DepthBiasClamp = 0.0F;
    }
    ID3D11RasterizerState* st = nullptr;
    if (FAILED(d->CreateRasterizerState(&rd, &st))) {
        return nullptr;
    }
    g_raster[schluessel] = st;
    return st;
}

// Ein Faktor aus dem Shaderskript in den von Direct3D.
//
// Direct3D kann JEDES dieser Paare unmittelbar - anders als der Rasterer,
// der nur fuenf Schubladen hat. Deshalb gibt es hier keinen "unbekannten"
// Fall mehr, sondern die Faktoren selbst.
D3D11_BLEND ausFaktor(BlendFactor f) {
    switch (f) {
        case BlendFactor::One:              return D3D11_BLEND_ONE;
        case BlendFactor::Zero:             return D3D11_BLEND_ZERO;
        case BlendFactor::SrcColor:         return D3D11_BLEND_SRC_COLOR;
        case BlendFactor::OneMinusSrcColor: return D3D11_BLEND_INV_SRC_COLOR;
        case BlendFactor::SrcAlpha:         return D3D11_BLEND_SRC_ALPHA;
        case BlendFactor::OneMinusSrcAlpha: return D3D11_BLEND_INV_SRC_ALPHA;
        case BlendFactor::DstColor:         return D3D11_BLEND_DEST_COLOR;
        case BlendFactor::OneMinusDstColor: return D3D11_BLEND_INV_DEST_COLOR;
        case BlendFactor::DstAlpha:         return D3D11_BLEND_DEST_ALPHA;
        case BlendFactor::OneMinusDstAlpha: return D3D11_BLEND_INV_DEST_ALPHA;
        // Siehe shaderscript.h: wie SrcAlpha behandelt, kommt in den sechs
        // pk3 kein einziges Mal vor.
        case BlendFactor::SrcAlphaSaturate: return D3D11_BLEND_SRC_ALPHA;
    }
    return D3D11_BLEND_ONE;
}

ID3D11BlendState* holeBlend(Blend b, BlendFactor src = BlendFactor::One,
                            BlendFactor dst = BlendFactor::Zero) {
    // Der Schluessel muss die Faktoren mitfuehren: zwei verschiedene
    // unbekannte Paare sind zwei verschiedene Zustaende.
    const auto schluessel =
        static_cast<std::uint32_t>(b) |
        (static_cast<std::uint32_t>(src) << 8) |
        (static_cast<std::uint32_t>(dst) << 16);
    const auto it = g_blend.find(schluessel);
    if (it != g_blend.end()) {
        return it->second;
    }
    ID3D11Device* d = dev();
    if (d == nullptr) {
        return nullptr;
    }
    D3D11_BLEND_DESC bd{};
    D3D11_RENDER_TARGET_BLEND_DESC& rt = bd.RenderTarget[0];
    // --- Nur Farbe schreiben, den Alphakanal stehen lassen ---------------
    //
    // Das Bild wird geloescht mit Alpha 1, und dabei muss es bleiben: Es
    // wird mit ImGui::Image ueber das Fenster gelegt, und ImGui mischt mit
    // dem Alphakanal. Vorher schrieb jede gemischte Flaeche ihr eigenes
    // Alpha hinein (SrcBlendAlpha ONE, DestBlendAlpha ZERO). Die Hologramme
    // ueber dem Holotisch in md_am_sith haben am Rand fast Alpha 0 - dort
    // schien der Fensterhintergrund als dunkles Quadrat durch. Mit Gluehen
    // an fiel es nicht auf, weil das Auflegen (glowcomp) Alpha 1 schreibt.
    //
    // Der Rasterer rechnet ausdruecklich so: "Der Zielalphakanal ist bei
    // uns immer voll (der Bildspeicher ist deckend), deshalb DstAlpha = 1"
    // (mapview.cpp). In der Engine ist es genauso egal - der Alphakanal des
    // Bildspeichers wird nicht angezeigt.
    rt.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_RED |
                               D3D11_COLOR_WRITE_ENABLE_GREEN |
                               D3D11_COLOR_WRITE_ENABLE_BLUE;
    rt.BlendOp = D3D11_BLEND_OP_ADD;
    rt.BlendOpAlpha = D3D11_BLEND_OP_ADD;
    rt.SrcBlendAlpha = D3D11_BLEND_ONE;
    rt.DestBlendAlpha = D3D11_BLEND_ZERO;
    switch (b) {
        case Blend::Opaque:
            rt.BlendEnable = FALSE;
            rt.SrcBlend = D3D11_BLEND_ONE;
            rt.DestBlend = D3D11_BLEND_ZERO;
            break;
        case Blend::Add:
            rt.BlendEnable = TRUE;
            rt.SrcBlend = D3D11_BLEND_ONE;
            rt.DestBlend = D3D11_BLEND_ONE;
            break;
        case Blend::AlphaBlend:
            rt.BlendEnable = TRUE;
            rt.SrcBlend = D3D11_BLEND_SRC_ALPHA;
            rt.DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
            break;
        case Blend::Filter:
            rt.BlendEnable = TRUE;
            rt.SrcBlend = D3D11_BLEND_DEST_COLOR;
            rt.DestBlend = D3D11_BLEND_ZERO;
            break;
        case Blend::AddAlpha:
            rt.BlendEnable = TRUE;
            rt.SrcBlend = D3D11_BLEND_SRC_ALPHA;
            rt.DestBlend = D3D11_BLEND_ONE;
            break;
        case Blend::InvSrcColor:
            rt.BlendEnable = TRUE;
            rt.SrcBlend = D3D11_BLEND_ZERO;
            rt.DestBlend = D3D11_BLEND_INV_SRC_COLOR;
            break;
        case Blend::OneSrcAlpha:
            rt.BlendEnable = TRUE;
            rt.SrcBlend = D3D11_BLEND_ONE;
            rt.DestBlend = D3D11_BLEND_SRC_ALPHA;
            break;
        case Blend::Unbekannt:
            // --- NICHT mehr deckend --------------------------------------
            //
            // Hier stand: "Bewusst DECKEND und nicht irgendwas. Ein
            // unbekannter Fall soll auffallen, und deckend faellt am
            // staerksten auf."
            //
            // Der Gedanke war richtig, die Folgerung nicht. `shaderscript`
            // haelt ausdruecklich fest: "Die Vorgabe ist DURCHSCHEINEND,
            // nicht deckend. Wer ein blendFunc schreibt, will mischen ...
            // sie deckend zu zeichnen ist der sichtbarste Fehler." Der
            // Rasterer folgt dem und mischt. Der GPU-Weg tat das Gegenteil.
            //
            // Und "auffallen" war gar nicht noetig: Direct3D kann das Paar
            // unmittelbar einstellen. `ausFaktoren` meldet weiter
            // `Unbekannt` - fuer die Diagnose und die Gruppierung -, aber
            // gezeichnet wird mit den ECHTEN Faktoren.
            //
            // Gefunden an textures/plasma_mustafar/lava, zweite Stufe:
            //     blendFunc GL_DST_COLOR GL_SRC_ALPHA
            // Deckend gezeichnet ueberdeckte sie die leuchtende erste Stufe
            // mit einer flachen Kopie. Das war die zu dunkle Lava und der
            // falsche Wasserfall.
            rt.BlendEnable = TRUE;
            rt.SrcBlend = ausFaktor(src);
            rt.DestBlend = ausFaktor(dst);
            break;
    }
    ID3D11BlendState* s = nullptr;
    if (FAILED(d->CreateBlendState(&bd, &s))) {
        return nullptr;
    }
    g_blend[schluessel] = s;
    return s;
}

// Ein Bild MIT seiner Bildpyramide hochladen.
//
// Hier stand "Direct3D rechnet seine eigenen, sobald es sie braucht" - das
// tut es nicht: mit MipLevels = 1 gibt es genau eine Stufe, und alles in der
// Ferne flimmerte, weil der Sampler zwischen weit auseinanderliegenden
// Bildpunkten sprang. Die Engine laedt jede Textur mit Pyramide
// (R_MipMap, tr_image.cpp:466), und genau die rechnet Tex::buildMips nach.
// Fehlt sie (Figurenbilder), wird sie hier auf einer Kopie gebaut.
ID3D11ShaderResourceView* ladeMitStufen(const TextureSet::Tex& quelle) {
    ID3D11Device* d = dev();
    if (d == nullptr || quelle.width <= 0 || quelle.height <= 0 || quelle.rgba.empty()) {
        return nullptr;
    }
    const TextureSet::Tex* t = &quelle;
    TextureSet::Tex kopie;
    if (quelle.mips.empty() && (quelle.width > 1 || quelle.height > 1)) {
        kopie.width = quelle.width;
        kopie.height = quelle.height;
        kopie.rgba = quelle.rgba;
        kopie.buildMips();
        t = &kopie;
    }
    const UINT stufen = 1U + static_cast<UINT>(t->mips.size());
    std::vector<D3D11_SUBRESOURCE_DATA> sd(stufen);
    for (UINT s = 0; s < stufen; ++s) {
        int w = 0;
        int h = 0;
        const std::uint8_t* px = t->levelData(static_cast<int>(s), w, h);
        sd[s].pSysMem = px;
        sd[s].SysMemPitch = static_cast<UINT>(w) * 4U;
    }
    D3D11_TEXTURE2D_DESC td{};
    td.Width = static_cast<UINT>(t->width);
    td.Height = static_cast<UINT>(t->height);
    td.MipLevels = stufen;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_IMMUTABLE;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    ID3D11Texture2D* tex = nullptr;
    if (FAILED(d->CreateTexture2D(&td, sd.data(), &tex)) || tex == nullptr) {
        return nullptr;
    }
    ID3D11ShaderResourceView* srv = nullptr;
    d->CreateShaderResourceView(tex, nullptr, &srv);
    tex->Release();
    return srv;
}

// Eine Textur des Satzes hochladen, falls noch nicht geschehen - mit
// Bildpyramide, siehe ladeMitStufen.
ID3D11ShaderResourceView* holeBild(const TextureSet* textures, int platz) {
    if (textures == nullptr || platz < 0 ||
        static_cast<std::size_t>(platz) >= textures->byShader.size()) {
        return nullptr;
    }
    const TextureSet::Tex& t = textures->byShader[static_cast<std::size_t>(platz)];
    const BildQuelle quelle{t.rgba.data(), t.width, t.height};
    const auto it = g_bilder.find(platz);
    if (it != g_bilder.end()) {
        if (g_bildQuelle[platz] == quelle) {
            return it->second;
        }
        ++g_veraltet;
        if (it->second != nullptr) { it->second->Release(); }
        g_bilder.erase(it);
    }
    ID3D11ShaderResourceView* srv = ladeMitStufen(t);
    if (srv == nullptr) {
        return nullptr;
    }
    g_bilder[platz] = srv;
    g_bildQuelle[platz] = quelle;
    return srv;
}

// Eine Lightmap hochladen. 128x128, DREI Bytes je Bildpunkt.
//
// Direct3D kennt kein R8G8B8, deshalb wird auf vier Bytes aufgefuellt. Wer
// die drei Bytes direkt als Zeilenschritt uebergibt, bekommt ein Bild, das
// sich je Zeile weiter verschiebt - schraege Streifen statt Licht.
ID3D11ShaderResourceView* holeLightmap(const BspGeometry* geo, int platz) {
    if (geo == nullptr || platz < 0 ||
        static_cast<std::size_t>(platz) >= geo->lightmaps.size()) {
        return nullptr;
    }
    const std::vector<std::uint8_t>& src =
        geo->lightmaps[static_cast<std::size_t>(platz)];
    const BildQuelle quelle{src.data(), geo->lightmapW(static_cast<std::size_t>(platz)),
                            geo->lightmapH(static_cast<std::size_t>(platz))};
    const auto it = g_lightmaps.find(platz);
    if (it != g_lightmaps.end()) {
        if (g_lightmapQuelle[platz] == quelle) {
            return it->second;
        }
        ++g_veraltet;
        if (it->second != nullptr) { it->second->Release(); }
        g_lightmaps.erase(it);
    }
    // Die aus der .bsp sind 128x128, externe haben ihre eigene Groesse
    // (BspGeometry::lightmapBreite, ladeExterneLightmaps).
    const int n = geo->lightmapW(static_cast<std::size_t>(platz));
    const int nh = geo->lightmapH(static_cast<std::size_t>(platz));
    if (src.size() < static_cast<std::size_t>(n) * static_cast<std::size_t>(nh) * 3U) {
        return nullptr;
    }
    ID3D11Device* d = dev();
    if (d == nullptr) {
        return nullptr;
    }
    std::vector<std::uint8_t> vier(static_cast<std::size_t>(n) * static_cast<std::size_t>(nh) * 4U, 255U);
    for (std::size_t i = 0; i < static_cast<std::size_t>(n) * static_cast<std::size_t>(nh); ++i) {
        vier[i * 4U + 0] = src[i * 3U + 0];
        vier[i * 4U + 1] = src[i * 3U + 1];
        vier[i * 4U + 2] = src[i * 3U + 2];
    }
    D3D11_TEXTURE2D_DESC td{};
    td.Width = static_cast<UINT>(n);
    td.Height = static_cast<UINT>(nh);
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_IMMUTABLE;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA sd{};
    sd.pSysMem = vier.data();
    sd.SysMemPitch = static_cast<UINT>(n) * 4U;
    ID3D11Texture2D* tex = nullptr;
    if (FAILED(d->CreateTexture2D(&td, &sd, &tex)) || tex == nullptr) {
        return nullptr;
    }
    ID3D11ShaderResourceView* srv = nullptr;
    d->CreateShaderResourceView(tex, nullptr, &srv);
    tex->Release();
    g_lightmaps[platz] = srv;
    g_lightmapQuelle[platz] = quelle;
    return srv;
}

// Die sechs Himmelsseiten als Texturfeld.
//
// Alle sechs muessen dieselbe Groesse haben - ein Feld hat EINE Groesse. Die
// Seiten kommen aus dem Texturensatz und sind dort ueblicherweise gleich
// gross; ist eine anders, wird der Himmel weggelassen statt verzerrt.
// --- Figuren ---------------------------------------------------------
//
// Der Eckenpuffer haengt am MODELL, nicht an der Figur: zwei Sturmtruppler
// teilen sich ein Modell und unterscheiden sich nur in den Knochenmatrizen.
// Ein Puffer je Figur waere bei 27 Figuren siebenundzwanzigmal dieselben
// Ecken.
struct FigurTeil {
    std::size_t surface = 0;
    std::uint32_t firstIndex = 0;
    std::uint32_t numIndexes = 0;
    bool kappe = false;   // nur mit App::showCaps zu sehen
};
struct FigurPuffer {
    ID3D11Buffer* vb = nullptr;
    ID3D11Buffer* ib = nullptr;
    std::vector<FigurTeil> teile;
    // Wovon der Puffer gebaut wurde - siehe figurSignatur.
    std::uint64_t signatur = 0;
};
std::map<const GlmModel*, FigurPuffer> g_figuren;
std::map<const void*, ID3D11ShaderResourceView*> g_figurBilder;
// Zu jedem Bild: die Adresse der Bildpunkte, aus denen es hochgeladen wurde.
std::map<const void*, const void*> g_figurBildDaten;

// --- Der Schluessel ist eine ADRESSE - der Inhalt kann wechseln ------------
//
// Das Modellfenster haelt EIN GlmModel (App::model) und laedt neue Modelle
// in dasselbe Objekt; eine andere Haut schaltet Flaechen an und aus. Mit der
// Adresse allein bekam das zweite Modell den Puffer des ersten. Deshalb
// wird mitgemerkt, woraus gebaut wurde: Flaechenzahl, Ecken, Indizes,
// sichtbar/Kappe und die Adresse der Eckdaten (die wechselt bei jedem Laden).
std::uint64_t figurSignatur(const GlmModel& model) {
    std::uint64_t h = 1469598103934665603ULL;
    const auto misch = [&](std::uint64_t v) {
        h ^= v;
        h *= 1099511628211ULL;
    };
    misch(model.surfaces.size());
    for (const GlmSurface& sf : model.surfaces) {
        misch(sf.verts.size());
        misch(sf.indexes.size());
        misch(sf.isVisible(true) ? 1U : 0U);
        misch(sf.isVisible(false) ? 2U : 0U);
        misch(reinterpret_cast<std::uintptr_t>(sf.verts.data()));
    }
    return h;
}
// Wie viele Figurenflaechen im letzten Bild kein Bild hatten.
//
// Sie werden uebersprungen - ein Loch in der Figur. Bis rc452 sah man
// nirgends, dass es eines gab.
int g_figurFlaechenOhneBild = 0;

// --- Auffaellige Kartenstapel des letzten Bildes -----------------------
//
// Der Bericht sagte bis rc456 viel ueber die Figuren und nichts ueber die
// Karte. Die schwarzen Vierecke auf dem Hologrammtisch waren aber
// KARTENflaechen - drei Runden lang habe ich bei den Figuren gesucht, weil
// dort die Liste war.
//
// Gesammelt wird nur, was auffaellt: autosprite, gluehend, oder eine
// Mischart ausserhalb der sieben bekannten. Alles andere fuellt den Bericht
// nur.
std::map<std::string, int> g_auffaellig;

// --- Jedem Direct3D-Gegenstand einen Namen geben -----------------------
//
// Die Debugschicht und RenderDoc nennen einen unbenannten Gegenstand
// `<unnamed>`. Genau daran hat rc450 eine Runde gekostet: die Meldung
//
//     DrawIndexed: Index buffer has not enough space!
//
// sagte nicht, WELCHER Indexpuffer - Karte, Effekte, Mover oder Figur, und
// die vier haben verschiedene. Ich habe daraufhin `setzeSchritt` an jede
// Meldung gehaengt; das war richtig, aber es beantwortet nur die Frage
// "wann", nicht "welcher".
//
// `WKPDID_D3DDebugObjectName` ist der dafuer vorgesehene Weg (Microsoft,
// "Object Naming"). Der Name kostet nichts, wenn die Debugschicht aus ist,
// und er steht danach in JEDER Meldung und in jeder RenderDoc-Aufnahme.
// `d3dcommon.h` ERKLAERT WKPDID_D3DDebugObjectName nur; die Definition
// liegt in `dxguid.lib`. MinGW hat sie in seinen Bibliotheken und band
// klaglos - MSVC nicht:
//
//     error LNK2019: nicht aufgeloestes externes Symbol
//                    "WKPDID_D3DDebugObjectName"
//
// Mein Pruefzug bindet mit MinGW; dass es dort geht, sagt also nichts
// darueber, ob es bei shank geht. Das ist eine Luecke im Pruefzug und
// steht in DEBUGGEN.md.
//
// Statt eine weitere Bibliothek zu binden, steht der Wert hier. Er ist
// aus `d3dcommon.h:934` der Werkzeugkette abgeschrieben, nicht aus dem
// Gedaechtnis:
//
//     DEFINE_GUID(WKPDID_D3DDebugObjectName, 0x429b8c22, 0x9188, 0x4b0c,
//                 0x87, 0x42, 0xac, 0xb0, 0xbf, 0x85, 0xc2, 0x00);
const GUID kNameGuid = {
    0x429b8c22, 0x9188, 0x4b0c,
    {0x87, 0x42, 0xac, 0xb0, 0xbf, 0x85, 0xc2, 0x00}};

void benenne(ID3D11DeviceChild* g, const char* name) {
    if (g == nullptr || name == nullptr) {
        return;
    }
    g->SetPrivateData(kNameGuid, static_cast<UINT>(std::strlen(name)), name);
}
ID3D11Buffer* g_cbFigur = nullptr;

// --- Der zuletzt geschriebene Inhalt von JeBild -----------------------
//
// `b0` ist EIN Puffer fuer alle Aufrufe eines Bildes, und Map mit
// WRITE_DISCARD verwirft immer den ganzen Inhalt. Wer nur die Matrix
// austauschen will, muss den Rest kennen - also merken wir ihn uns.
//
// Gebraucht wird das, weil `zeichneMover` die Moverstellung IN die
// Kameramatrix rechnet (rc421). Danach steht in gViewProj
// `Kamera x Moverstellung`, und die Figuren bekamen die Stellung des
// zuletzt gezeichneten Movers zusaetzlich aufgerechnet.
//
// Genau das Bild: richtig texturiert, richtig animiert, in plausiblen
// Abstaenden zueinander - aber alle zusammen an der falschen Stelle
// ausserhalb der Karte.
float g_bildDaten[40] = {0.0F};
bool g_bildDatenDa = false;
ShaderPaar g_figurShader;
bool g_figurShaderVersucht = false;
// Die Klingen: eigener kleiner Shader, ein Eckenpuffer, der mitwaechst.
ShaderPaar g_klingeShader;
bool g_klingeShaderVersucht = false;
ID3D11Buffer* g_klingeVb = nullptr;
std::size_t g_klingeVbEcken = 0;
// Die Ersatzfarbe fuer Flaechen ohne Bild im Modellfenster.
ID3D11ShaderResourceView* g_ersatzBild = nullptr;
// Das Modellfenster: eigenes Ziel, eigene Tiefe, eigener Hintergrund.
ID3D11Texture2D* g_modellTex = nullptr;
ID3D11RenderTargetView* g_modellRtv = nullptr;
ID3D11ShaderResourceView* g_modellSrv = nullptr;
ID3D11Texture2D* g_modellTiefeTex = nullptr;
ID3D11DepthStencilView* g_modellDsv = nullptr;
int g_modellW = 0;
int g_modellH = 0;
ID3D11VertexShader* g_modellHgVs = nullptr;
ID3D11PixelShader* g_modellHgPs = nullptr;
bool g_modellHgVersucht = false;

void gibModellFrei() {
    if (g_modellSrv != nullptr) { g_modellSrv->Release(); g_modellSrv = nullptr; }
    if (g_modellRtv != nullptr) { g_modellRtv->Release(); g_modellRtv = nullptr; }
    if (g_modellTex != nullptr) { g_modellTex->Release(); g_modellTex = nullptr; }
    if (g_modellDsv != nullptr) { g_modellDsv->Release(); g_modellDsv = nullptr; }
    if (g_modellTiefeTex != nullptr) { g_modellTiefeTex->Release(); g_modellTiefeTex = nullptr; }
    g_modellW = 0;
    g_modellH = 0;
}

void gibKlingenFrei() {
    if (g_klingeShader.vs != nullptr) { g_klingeShader.vs->Release(); }
    if (g_klingeShader.ps != nullptr) { g_klingeShader.ps->Release(); }
    if (g_klingeShader.layout != nullptr) { g_klingeShader.layout->Release(); }
    g_klingeShader = ShaderPaar{};
    g_klingeShaderVersucht = false;
    if (g_klingeVb != nullptr) { g_klingeVb->Release(); g_klingeVb = nullptr; }
    g_klingeVbEcken = 0;
    if (g_ersatzBild != nullptr) { g_ersatzBild->Release(); g_ersatzBild = nullptr; }
    if (g_modellHgVs != nullptr) { g_modellHgVs->Release(); g_modellHgVs = nullptr; }
    if (g_modellHgPs != nullptr) { g_modellHgPs->Release(); g_modellHgPs = nullptr; }
    g_modellHgVersucht = false;
}

// --- Das Gluehen ------------------------------------------------------
//
// Drei Puffer: einer in voller Groesse (dort wird gezeichnet und spaeter
// aufgelegt) und zwei kleine, zwischen denen das Weichzeichnen hin und her
// laeuft. Zwei kleine, weil ein Durchgang nicht aus derselben Textur lesen
// und in sie schreiben kann.
ID3D11Texture2D* g_glowVollTex = nullptr;
ID3D11RenderTargetView* g_glowVollRtv = nullptr;
ID3D11ShaderResourceView* g_glowVollSrv = nullptr;
ID3D11Texture2D* g_glowTex[2] = {nullptr, nullptr};
ID3D11RenderTargetView* g_glowRtv[2] = {nullptr, nullptr};
ID3D11ShaderResourceView* g_glowSrv[2] = {nullptr, nullptr};
ID3D11Buffer* g_cbGlow = nullptr;
ID3D11VertexShader* g_vollbildVs = nullptr;
ID3D11PixelShader* g_glowShrinkPs = nullptr;
ID3D11PixelShader* g_glowBlurPs = nullptr;
ID3D11PixelShader* g_glowCompPs = nullptr;
bool g_glowShaderVersucht = false;
int g_glowW = 0;
int g_glowH = 0;
// Die Groesse, mit der der volle Gluehpuffer angelegt wurde. Sie haengt am
// Renderziel, nicht an der Gluehgroesse - und die beiden aendern sich
// unabhaengig voneinander.
int g_glowVollW = 0;
int g_glowVollH = 0;

ID3D11ShaderResourceView* g_himmel = nullptr;
// Die sechs Plaetze, aus denen der Himmel gebaut wurde - als INHALT, nicht
// als Zeiger auf den Vektor: nach einem Kartenwechsel kann der neue Vektor
// an derselben Adresse liegen, und der alte Himmel blieb stehen.
std::vector<int> g_himmelQuelle;

ID3D11ShaderResourceView* holeHimmel(const TextureSet* textures,
                                     const std::vector<int>* faces) {
    if (textures == nullptr || faces == nullptr || faces->size() != 6U) {
        return nullptr;
    }
    if (g_himmel != nullptr && g_himmelQuelle == *faces) {
        return g_himmel;
    }
    // Hier stand (schon in rc567) ein Block, der bei JEDEM Himmelswechsel
    // alle Figurenpuffer, den Figuren-Shader und die Gluehen-Ziele freigab -
    // offenbar ein verrutschtes Stueck aus shutdown(). Mit zwei Himmel-
    // Shadern in einer Karte geschah das in jedem Bild. Die Karte vergisst
    // jetzt vergissKarte(); Figuren haengen nicht an der Karte.
    if (g_himmel != nullptr) { g_himmel->Release(); g_himmel = nullptr; }
    ID3D11Device* d = dev();
    if (d == nullptr) {
        return nullptr;
    }
    int w = 0;
    int h = 0;
    for (int i = 0; i < 6; ++i) {
        const int platz = (*faces)[static_cast<std::size_t>(i)];
        if (platz < 0 ||
            static_cast<std::size_t>(platz) >= textures->byShader.size()) {
            return nullptr;
        }
        const TextureSet::Tex& t =
            textures->byShader[static_cast<std::size_t>(platz)];
        if (t.width <= 0 || t.height <= 0 || t.rgba.empty()) {
            return nullptr;
        }
        if (i == 0) {
            w = t.width;
            h = t.height;
        } else if (t.width != w || t.height != h) {
            // Verschiedene Groessen. Lieber gar kein Himmel als ein
            // verzerrter - ein fehlender faellt auf, ein verzerrter wird
            // fuer Absicht gehalten.
            return nullptr;
        }
    }
    D3D11_TEXTURE2D_DESC td{};
    td.Width = static_cast<UINT>(w);
    td.Height = static_cast<UINT>(h);
    td.MipLevels = 1;
    td.ArraySize = 6;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_IMMUTABLE;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA sd[6]{};
    for (int i = 0; i < 6; ++i) {
        const TextureSet::Tex& t = textures->byShader[
            static_cast<std::size_t>((*faces)[static_cast<std::size_t>(i)])];
        sd[i].pSysMem = t.rgba.data();
        sd[i].SysMemPitch = static_cast<UINT>(w) * 4U;
    }
    ID3D11Texture2D* tex = nullptr;
    if (FAILED(d->CreateTexture2D(&td, sd, &tex)) || tex == nullptr) {
        return nullptr;
    }
    d->CreateShaderResourceView(tex, nullptr, &g_himmel);
    tex->Release();
    g_himmelQuelle = *faces;
    return g_himmel;
}

// Die Ecken eines Modells hochladen - einmal je Modell.
FigurPuffer* holeFigurPuffer(const GlmModel& model, std::string* fehler) {
    const std::uint64_t sig = figurSignatur(model);
    const auto it = g_figuren.find(&model);
    if (it != g_figuren.end()) {
        if (it->second.signatur == sig) {
            return &it->second;
        }
        // Ein anderes Modell (oder eine andere Haut) an derselben Adresse.
        if (it->second.vb != nullptr) { it->second.vb->Release(); }
        if (it->second.ib != nullptr) { it->second.ib->Release(); }
        g_figuren.erase(it);
    }
    ID3D11Device* d = dev();
    if (d == nullptr) {
        return nullptr;
    }
    FigurPuffer fp;
    std::vector<SkinVertex> ecken;
    std::vector<std::uint32_t> indizes;
    for (std::size_t i = 0; i < model.surfaces.size(); ++i) {
        const GlmSurface& sf = model.surfaces[i];
        // MIT den Kappen in den Puffer - ob sie gezeichnet werden, sagt
        // jedes Bild neu (showCaps); der Puffer haengt am Modell.
        if (!sf.isVisible(true) || sf.indexes.empty()) {
            continue;
        }
        FigurTeil t;
        t.surface = i;
        t.kappe = !sf.isVisible(false);
        t.firstIndex = static_cast<std::uint32_t>(indizes.size());
        t.numIndexes = static_cast<std::uint32_t>(sf.indexes.size());
        // Die Indizes sind je Flaeche ab null gezaehlt - beim Zusammenlegen
        // muss der Versatz dazu. Wer das vergisst, bekommt eine Figur, bei
        // der jede Flaeche die Ecken der ersten benutzt.
        const auto basis = static_cast<std::uint32_t>(ecken.size());
        for (const auto idx : sf.indexes) {
            indizes.push_back(basis + static_cast<std::uint32_t>(idx));
        }
        packeEcken(sf, ecken);
        fp.teile.push_back(t);
    }
    if (ecken.empty() || indizes.empty()) {
        if (fehler != nullptr) { *fehler = "Modell ohne sichtbare Flaechen"; }
        return nullptr;
    }
    D3D11_BUFFER_DESC bd{};
    bd.ByteWidth = static_cast<UINT>(ecken.size() * sizeof(SkinVertex));
    bd.Usage = D3D11_USAGE_IMMUTABLE;
    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA sd{};
    sd.pSysMem = ecken.data();
    if (FAILED(d->CreateBuffer(&bd, &sd, &fp.vb))) {
        if (fehler != nullptr) { *fehler = "Figur-Vertexpuffer nicht anlegbar"; }
        return nullptr;
    }
    bd.ByteWidth = static_cast<UINT>(indizes.size() * sizeof(std::uint32_t));
    bd.BindFlags = D3D11_BIND_INDEX_BUFFER;
    sd.pSysMem = indizes.data();
    if (FAILED(d->CreateBuffer(&bd, &sd, &fp.ib))) {
        fp.vb->Release();
        if (fehler != nullptr) { *fehler = "Figur-Indexpuffer nicht anlegbar"; }
        return nullptr;
    }
    fp.signatur = sig;
    g_figuren[&model] = fp;
    return &g_figuren[&model];
}

// Eine Figurentextur hochladen. Der Schluessel ist die Adresse des
// Texturensatzeintrags - je Modell und Flaeche eine.
ID3D11ShaderResourceView* holeTexBild(const TextureSet::Tex* t);

ID3D11ShaderResourceView* holeFigurBild(const ModelTextures* textures,
                                        std::size_t surface) {
    if (textures == nullptr || surface >= textures->bySurface.size()) {
        return nullptr;
    }
    const TextureSet::Tex& t = textures->bySurface[surface];
    if (t.width <= 0 || t.height <= 0 || t.rgba.empty()) {
        return nullptr;
    }
    return holeTexBild(&t);
}

// Ein Bildpunkt in der Ersatzfarbe des alten Modellbetrachters
// (renderModel: 226, 220, 208) - fuer Flaechen ohne Textur.
ID3D11ShaderResourceView* holeErsatzBild() {
    if (g_ersatzBild != nullptr) {
        return g_ersatzBild;
    }
    ID3D11Device* d = dev();
    if (d == nullptr) {
        return nullptr;
    }
    const std::uint8_t punkt[4] = {226U, 220U, 208U, 255U};
    D3D11_TEXTURE2D_DESC td{};
    td.Width = 1;
    td.Height = 1;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_IMMUTABLE;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA sd{};
    sd.pSysMem = punkt;
    sd.SysMemPitch = 4;
    ID3D11Texture2D* tex = nullptr;
    if (FAILED(d->CreateTexture2D(&td, &sd, &tex)) || tex == nullptr) {
        return nullptr;
    }
    d->CreateShaderResourceView(tex, nullptr, &g_ersatzBild);
    tex->Release();
    return g_ersatzBild;
}

// Ein einzelnes Bild aus einem TextureSet::Tex - fuer die Klingen. Derselbe
// Zwischenspeicher wie die Figurenbilder (Schluessel: die Adresse).
ID3D11ShaderResourceView* holeTexBild(const TextureSet::Tex* t) {
    if (t == nullptr || t->width <= 0 || t->height <= 0 || t->rgba.empty()) {
        return nullptr;
    }
    const auto it = g_figurBilder.find(t);
    if (it != g_figurBilder.end()) {
        // Dieselbe Adresse, aber neue Bildpunkte (neu geladen, andere
        // Haut)? Dann neu hochladen - siehe figurSignatur.
        const auto d2 = g_figurBildDaten.find(t);
        if (d2 != g_figurBildDaten.end() && d2->second == t->rgba.data()) {
            return it->second;
        }
        if (it->second != nullptr) { it->second->Release(); }
        g_figurBilder.erase(it);
    }
    ID3D11ShaderResourceView* srv = ladeMitStufen(*t);
    if (srv == nullptr) {
        return nullptr;
    }
    g_figurBilder[t] = srv;
    g_figurBildDaten[t] = t->rgba.data();
    return srv;
}

// Der Figuren-Shader. Nur EINER - er hat keine Merkmalsvarianten.
ShaderPaar* holeFigurShader(std::string* fehler) {
    if (g_figurShader.vs != nullptr) {
        return &g_figurShader;
    }
    if (g_figurShaderVersucht) {
        // Nicht bei jedem Bild neu versuchen: ein Shader, der sich einmal
        // nicht uebersetzen laesst, tut es auch beim zweiten Mal nicht - und
        // D3DCompile kostet Millisekunden.
        if (fehler != nullptr) { *fehler = "Figuren-Shader nicht uebersetzbar"; }
        return nullptr;
    }
    g_figurShaderVersucht = true;
    ID3D11Device* d = dev();
    if (d == nullptr) {
        return nullptr;
    }
    const std::string vsSrc = figurVertexShaderHlsl(kNichts);
    // --- kRohEcke fuer Figuren -------------------------------------------
    //
    // Der Rasterer rechnet fuer eine Figur schlicht `Textur x shade`
    // (mapview.cpp:3382) - OHNE `opt.brightness`. Die Helligkeit gilt der
    // Karte, nicht den Figuren.
    //
    // Der Pixelshader mit `kNichts` multipliziert dagegen mit
    // `gHelligkeit`, bei shanks Einstellung also mit zwei. Die Schattierung
    // laeuft von 0,22 bis 1,0; alles ueber 0,5 wird damit auf Weiss
    // geklemmt, und ein Battledroid ist zur Haelfte reinweiss. rc454 hat
    // die Beleuchtung eingebaut und trotzdem nichts geaendert - weil sie
    // hinter dem Faktor zwei verschwand.
    //
    // `kRohEcke` heisst genau das: diese Eckenfarbe ist fertig und darf
    // nicht aufgehellt werden. Sie war fuer die rohen Effektfarben gedacht
    // (rc433) und passt hier ohne Zusatz.
    const std::string psSrc = pixelShaderHlsl(kRohEcke);
    ID3DBlob* vsBlob = nullptr;
    ID3DBlob* psBlob = nullptr;
    ID3DBlob* err = nullptr;
    if (FAILED(D3DCompile(vsSrc.data(), vsSrc.size(), "figur", nullptr,
                          nullptr, "main", "vs_4_0", uebersetzFahnen(), 0, &vsBlob, &err))) {
        if (fehler != nullptr && err != nullptr) {
            *fehler = std::string("Figuren-Vertex-Shader: ") +
                      static_cast<const char*>(err->GetBufferPointer());
        }
        if (err != nullptr) { err->Release(); }
        return nullptr;
    }
    if (FAILED(D3DCompile(psSrc.data(), psSrc.size(), "figur", nullptr,
                          nullptr, "main", "ps_4_0", uebersetzFahnen(), 0, &psBlob, &err))) {
        if (fehler != nullptr && err != nullptr) {
            *fehler = std::string("Figuren-Pixel-Shader: ") +
                      static_cast<const char*>(err->GetBufferPointer());
        }
        if (err != nullptr) { err->Release(); }
        vsBlob->Release();
        return nullptr;
    }
    d->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(),
                          nullptr, &g_figurShader.vs);
    d->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(),
                         nullptr, &g_figurShader.ps);
    // Muss zu SkinVertex passen (bhed/gpuskin.h) - dort wird sizeof geprueft.
    const D3D11_INPUT_ELEMENT_DESC ein[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
         D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12,
         D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24,
         D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"BLENDINDICES", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 32,
         D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"BLENDWEIGHT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 48,
         D3D11_INPUT_PER_VERTEX_DATA, 0},
    };
    // Auch hier die Zahl aus dem Feld - siehe die Erklaerung beim
    // Kartenlayout.
    d->CreateInputLayout(ein, static_cast<UINT>(std::size(ein)),
                         vsBlob->GetBufferPointer(),
                         vsBlob->GetBufferSize(), &g_figurShader.layout);
    vsBlob->Release();
    psBlob->Release();
    if (g_figurShader.vs == nullptr || g_figurShader.layout == nullptr) {
        if (fehler != nullptr) { *fehler = "Figuren-Shader unvollstaendig"; }
        return nullptr;
    }
    return &g_figurShader;
}

// Die drei Gluehpuffer anlegen.
bool bereiteGluehpuffer(int breite, int hoehe) {
    ID3D11Device* d = dev();
    if (d == nullptr || breite <= 0 || hoehe <= 0) {
        return false;
    }
    // Der volle Puffer haengt an der Zielgroesse, die kleinen an breite/hoehe.
    // --- Auch die ZIELgroesse zaehlt --------------------------------------
    //
    // Der volle Gluehpuffer wird mit `g_zielW/g_zielH` angelegt, geprueft
    // wurde aber nur `breite/hoehe`. Beim Bewegen der Kamera aendert sich
    // die Zielgroesse (App::mapScale), die Gluehgroesse bleibt gleich - der
    // Puffer blieb also alt.
    //
    // Direct3D verlangt, dass Renderziel und Tiefenpuffer dieselbe Groesse
    // haben. Passt es nicht, wird der Durchgang verworfen - stillschweigend.
    // Gemeldet als: "wenn ich die Kamera bewege verschwinden die Tueren bis
    // ich wieder stehe".
    const bool passt = (g_glowVollRtv != nullptr && g_glowW == breite &&
                        g_glowH == hoehe && g_glowVollW == g_zielW &&
                        g_glowVollH == g_zielH);
    if (passt) {
        return true;
    }
    if (g_glowVollSrv != nullptr) { g_glowVollSrv->Release(); g_glowVollSrv = nullptr; }
    if (g_glowVollRtv != nullptr) { g_glowVollRtv->Release(); g_glowVollRtv = nullptr; }
    if (g_glowVollTex != nullptr) { g_glowVollTex->Release(); g_glowVollTex = nullptr; }
    for (int i = 0; i < 2; ++i) {
        if (g_glowSrv[i] != nullptr) { g_glowSrv[i]->Release(); g_glowSrv[i] = nullptr; }
        if (g_glowRtv[i] != nullptr) { g_glowRtv[i]->Release(); g_glowRtv[i] = nullptr; }
        if (g_glowTex[i] != nullptr) { g_glowTex[i]->Release(); g_glowTex[i] = nullptr; }
    }
    auto lege = [&](int w, int h, ID3D11Texture2D** tex,
                    ID3D11RenderTargetView** rtv,
                    ID3D11ShaderResourceView** srv) {
        D3D11_TEXTURE2D_DESC td{};
        td.Width = static_cast<UINT>(w);
        td.Height = static_cast<UINT>(h);
        td.MipLevels = 1;
        td.ArraySize = 1;
        td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_DEFAULT;
        td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        if (FAILED(d->CreateTexture2D(&td, nullptr, tex))) {
            return false;
        }
        return SUCCEEDED(d->CreateRenderTargetView(*tex, nullptr, rtv)) &&
               SUCCEEDED(d->CreateShaderResourceView(*tex, nullptr, srv));
    };
    if (!lege(g_zielW, g_zielH, &g_glowVollTex, &g_glowVollRtv,
              &g_glowVollSrv)) {
        return false;
    }
    for (int i = 0; i < 2; ++i) {
        if (!lege(breite, hoehe, &g_glowTex[i], &g_glowRtv[i],
                  &g_glowSrv[i])) {
            return false;
        }
    }
    if (g_cbGlow == nullptr) {
        D3D11_BUFFER_DESC bd{};
        bd.ByteWidth = 8U * 4U;   // zwei float4
        bd.Usage = D3D11_USAGE_DYNAMIC;
        bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        if (FAILED(d->CreateBuffer(&bd, nullptr, &g_cbGlow))) {
            return false;
        }
    }
    g_glowW = breite;
    g_glowH = hoehe;
    g_glowVollW = g_zielW;
    g_glowVollH = g_zielH;
    return true;
}

// Die Aufrufe einer Liste absetzen - gemeinsam von Haupt- und
// Gluehdurchgang benutzt, damit beide dieselbe Bindung machen.
int zeichneAufrufe(const std::vector<DrawCall>& calls, const BspMesh& mesh,
                   const TextureSet* textures, const BspGeometry* geo,
                   float zeitSekunden, std::string* fehler) {
    ID3D11DeviceContext* c = ctx();
    if (c == nullptr) {
        return 0;
    }
    int abgesetzt = 0;
    const PipelineState* letzter = nullptr;
    for (const DrawCall& call : calls) {
        // --- Auffaelliges vermerken ---------------------------------
        //
        // Der Shadername kommt aus der Kartengeometrie, nicht aus dem
        // erzeugten Shader: `fx_tc_rw1` sagt, WIE gezeichnet wird,
        // `textures/plasma_mustafar/holo1` sagt, WAS.
        if (geo != nullptr && call.batch < mesh.batches.size()) {
            const int si = mesh.batches[call.batch].shader;
            if (si >= 0 && static_cast<std::size_t>(si) < geo->shaders.size()) {
                const std::string& nm = geo->shaders[
                    static_cast<std::size_t>(si)].name;
                const std::uint32_t f = call.state.features;
                if ((f & kAutosprite) != 0U) {
                    ++g_auffaellig[nm + "  autosprite"];
                }
                if ((f & kGlow) != 0U) {
                    ++g_auffaellig[nm + "  glow"];
                }
                if (call.state.blend == Blend::Unbekannt) {
                    ++g_auffaellig[nm + "  unbekannte Mischart"];
                }
            }
        }
        std::string sfehler;
        ShaderPaar* sp = holeShader(call.state.features, call.state.numTexMods,
                                    &sfehler);
        if (sp == nullptr) {
            if (fehler != nullptr && fehler->empty()) { *fehler = sfehler; }
            continue;
        }
        if (letzter == nullptr || !letzter->gleichWie(call.state)) {
            c->VSSetShader(sp->vs, nullptr, 0);
            c->PSSetShader(sp->ps, nullptr, 0);
            c->IASetInputLayout(sp->layout);
            ID3D11BlendState* bs = holeBlend(call.state.blend,
                                             call.state.srcFactor,
                                             call.state.dstFactor);
            if (bs != nullptr) {
                const float faktor[4] = {0.0F, 0.0F, 0.0F, 0.0F};
                c->OMSetBlendState(bs, faktor, 0xFFFFFFFFU);
            }
            if (tiefeAn()) {
                ID3D11DepthStencilState* ds = holeTiefe(call.state);
                if (ds != nullptr) { c->OMSetDepthStencilState(ds, 0); }
            }
            if (keulenAn()) {
                ID3D11RasterizerState* rs = holeRaster(call.state.features);
                if (rs != nullptr) { c->RSSetState(rs); }
            }
            letzter = &call.state;
        }
        // --- Die Konstanten gehoeren zum AUFRUF, nicht zum Stapel -------
        //
        // `call.bild` zeigt bei einer Zusatzstufe auf ihren eigenen Platz;
        // hier stand `mesh.batches[call.batch].shader`, also immer der der
        // Grundstufe. Die Zusatzstufe haette damit ihr eigenes BILD
        // bekommen, aber die tcMod-Werte, die Alphaschwelle und die
        // rgbGen-Farbe der Grundstufe - ein Leuchten, das mit der falschen
        // Geschwindigkeit scrollt und in der falschen Farbe.
        //
        // Der Rasterer macht es genauso: `texId = jobs[jobIdx].stage`, wenn
        // eine Stufe vorliegt (mapview.cpp:1396).
        const int platz = (call.bild >= 0) ? call.bild
                                           : mesh.batches[call.batch].shader;
        // --- Die Zeit DIESES Stapels, nicht die absolute ----------------
        //
        // Ein Effekt mit `setShaderTime` bringt seinen eigenen Ursprung mit
        // (BspMesh::Batch::shaderTime). Alles Zeitabhaengige haengt daran:
        // die Bildfolge, rgbGen wave, deformVertexes wave und die
        // tcMod-Kette. Die Engine rechnet gegen tess.shaderTime, nicht
        // gegen refdef.floatTime; der Rasterer ebenso (mapview.cpp:1394).
        //
        // Der GPU-Weg reichte die absolute Zeit durch. Fuer die Karte ist
        // das dasselbe - dort ist shaderTime null -, fuer alles mit eigenem
        // Ursprung nicht: es lief mit falschem Versatz.
        const float stapelZeit =
            zeitSekunden - mesh.batches[call.batch].shaderTime;
        const BatchState bs2 = batchStateFor(textures, platz, stapelZeit);
        D3D11_MAPPED_SUBRESOURCE m{};
        if (SUCCEEDED(c->Map(g_cbStapel, 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) {
            auto* f2 = static_cast<float*>(m.pData);
            packeKonstanten(bs2, f2, stapelZeit);
            c->Unmap(g_cbStapel, 0);
        }
        c->VSSetConstantBuffers(1, 1, &g_cbStapel);
        c->PSSetConstantBuffers(1, 1, &g_cbStapel);
        // animMap: das Bild der Zeit, wie im Rasterer (animBildFuer). Die
        // Bilder der Folge tragen dieselben Eigenschaften wie die Stufe,
        // die Konstanten oben bleiben also richtig.
        ID3D11ShaderResourceView* bild =
            holeBild(textures, animBildFuer(textures, call.bild, stapelZeit));
        if (bild == nullptr) {
            continue;
        }
        ID3D11ShaderResourceView* lm =
            (call.lightmap >= 0) ? holeLightmap(geo, call.lightmap) : nullptr;
        ID3D11ShaderResourceView* himmel = nullptr;
        if ((call.state.features & kSky) != 0U) {
            himmel = holeHimmel(textures, bs2.skyFaces);
        }
        ID3D11ShaderResourceView* srvs[3] = {bild, lm, himmel};
        c->PSSetShaderResources(0, 3, srvs);
        c->DrawIndexed(call.numIndexes, call.firstIndex, 0);
        ++abgesetzt;
    }
    return abgesetzt;
}

// Auf den Vollbild-Weg umschalten: ein Dreieck ohne Puffer.
bool wechsleAufVollbild() {
    ID3D11Device* d = dev();
    ID3D11DeviceContext* c = ctx();
    if (d == nullptr || c == nullptr) {
        return false;
    }
    if (g_vollbildVs == nullptr && !g_glowShaderVersucht) {
        g_glowShaderVersucht = true;
        // --- Die eigene Ressource freigeben, nicht auf die Nachbarn
        //     verlassen ---------------------------------------------------
        //
        // Die Bedingung darueber prueft `g_vollbildVs`. Angelegt werden hier
        // aber DREI Shader. Dass die anderen beiden dabei nicht lecken,
        // haengt allein daran, dass sie immer zusammen geloescht werden -
        // eine Zusicherung an einer ganz anderen Stelle.
        //
        // Genau dieses Muster ist hier schon dreimal auseinandergelaufen
        // (`brauchtEcken`/`vbZuKlein`, die doppelte Anzeige, zwei Fassungen
        // der Figurenknochen). `tools/lint_d3dbesitz.py` hat es beim ersten
        // Lauf gemeldet.
        //
        // Zwei Zeilen, und die Aussage gilt hier statt anderswo.
        if (g_glowShrinkPs != nullptr) {
            g_glowShrinkPs->Release();
            g_glowShrinkPs = nullptr;
        }
        if (g_glowBlurPs != nullptr) {
            g_glowBlurPs->Release();
            g_glowBlurPs = nullptr;
        }
        if (g_glowCompPs != nullptr) {
            g_glowCompPs->Release();
            g_glowCompPs = nullptr;
        }
        const std::string vs = vollbildVertexShaderHlsl();
        const std::string v = glowShrinkPixelShaderHlsl();
        const std::string b = glowBlurPixelShaderHlsl();
        const std::string k = glowCompositePixelShaderHlsl();
        ID3DBlob* blob = nullptr;
        ID3DBlob* err = nullptr;
        if (SUCCEEDED(D3DCompile(vs.data(), vs.size(), "vollbild", nullptr,
                                 nullptr, "main", "vs_4_0", uebersetzFahnen(), 0, &blob,
                                 &err))) {
            d->CreateVertexShader(blob->GetBufferPointer(),
                                  blob->GetBufferSize(), nullptr,
                                  &g_vollbildVs);
            blob->Release();
        }
        if (SUCCEEDED(D3DCompile(v.data(), v.size(), "glowshrink", nullptr,
                                 nullptr, "main", "ps_4_0", uebersetzFahnen(), 0, &blob,
                                 &err))) {
            d->CreatePixelShader(blob->GetBufferPointer(),
                                 blob->GetBufferSize(), nullptr,
                                 &g_glowShrinkPs);
            blob->Release();
        }
        if (SUCCEEDED(D3DCompile(b.data(), b.size(), "glowblur", nullptr,
                                 nullptr, "main", "ps_4_0", uebersetzFahnen(), 0, &blob,
                                 &err))) {
            d->CreatePixelShader(blob->GetBufferPointer(),
                                 blob->GetBufferSize(), nullptr,
                                 &g_glowBlurPs);
            blob->Release();
        }
        if (SUCCEEDED(D3DCompile(k.data(), k.size(), "glowcomp", nullptr,
                                 nullptr, "main", "ps_4_0", uebersetzFahnen(), 0, &blob,
                                 &err))) {
            d->CreatePixelShader(blob->GetBufferPointer(),
                                 blob->GetBufferSize(), nullptr,
                                 &g_glowCompPs);
            blob->Release();
        }
        if (err != nullptr) { err->Release(); }
    }
    if (g_vollbildVs == nullptr || g_glowShrinkPs == nullptr ||
        g_glowBlurPs == nullptr || g_glowCompPs == nullptr) {
        return false;
    }
    // Kein Vertexpuffer und kein Eingabelayout - die Ecken kommen aus
    // SV_VertexID. Beides muss ABGEMELDET werden, sonst haelt Direct3D das
    // alte Layout fuer gueltig und meldet einen Fehler, den man nur im
    // Debug-Ausgabefenster sieht.
    ID3D11Buffer* keiner = nullptr;
    const UINT null1 = 0;
    c->IASetVertexBuffers(0, 1, &keiner, &null1, &null1);
    c->IASetInputLayout(nullptr);
    c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    c->VSSetShader(g_vollbildVs, nullptr, 0);
    // Ein Vollbilddreieck hat keine sinnvolle Tiefe und keine Rueckseite.
    // Ohne diese beiden Zeilen erbte es den Zustand des zuletzt
    // gezeichneten Stapels - und ein Gluehdurchgang, der gegen die Tiefe
    // der Szene testet, bleibt schwarz.
    {
        // Beim Vollbild AUCH DANN setzen, wenn der Schalter aus ist: ohne
        // Tiefenzustand erbt das Vollbilddreieck den der Szene und der
        // Gluehdurchgang bliebe schwarz. Vor rc434 stand hier die Vorgabe
        // von Direct3D, und die reichte - jetzt nicht mehr.
        PipelineState voll;
        voll.blend = Blend::Add;      // also NICHT deckend: schreibt nicht
        voll.depthWrite = false;
        voll.depthTestOff = true;
        ID3D11DepthStencilState* ds = holeTiefe(voll);
        if (ds != nullptr) { c->OMSetDepthStencilState(ds, 0); }
        if (keulenAn()) {
            ID3D11RasterizerState* rs = holeRaster(kCullNone);
            if (rs != nullptr) { c->RSSetState(rs); }
        }
    }
    ID3D11BlendState* bs = holeBlend(Blend::Opaque);
    if (bs != nullptr) {
        const float faktor[4] = {0.0F, 0.0F, 0.0F, 0.0F};
        c->OMSetBlendState(bs, faktor, 0xFFFFFFFFU);
    }
    return true;
}

void setzeGlowKonstanten(float abstand, float gewicht, bool weich) {
    ID3D11DeviceContext* c = ctx();
    if (c == nullptr || g_cbGlow == nullptr) {
        return;
    }
    D3D11_MAPPED_SUBRESOURCE m{};
    if (SUCCEEDED(c->Map(g_cbGlow, 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) {
        auto* f = static_cast<float*>(m.pData);
        f[0] = (g_glowW > 0) ? 1.0F / static_cast<float>(g_glowW) : 0.0F;
        f[1] = (g_glowH > 0) ? 1.0F / static_cast<float>(g_glowH) : 0.0F;
        f[2] = abstand;
        f[3] = gewicht;
        f[4] = weich ? 1.0F : 0.0F;
        // Die Groesse des vollen Puffers - fuer das Kastenmittel beim
        // Verkleinern (glowShrinkPixelShaderHlsl).
        f[5] = static_cast<float>(g_glowVollW);
        f[6] = static_cast<float>(g_glowVollH);
        f[7] = 0.0F;
        c->Unmap(g_cbGlow, 0);
    }
    c->PSSetConstantBuffers(3, 1, &g_cbGlow);
}

// Einen Durchgang ueber das ganze Bild.
void blitte(ID3D11ShaderResourceView* quelle, ID3D11RenderTargetView* ziel,
            int breite, int hoehe, ID3D11PixelShader* ps) {
    ID3D11DeviceContext* c = ctx();
    if (c == nullptr || ziel == nullptr) {
        return;
    }
    // Die Quelle ABMELDEN, bevor sie als Ziel gebunden wird - sonst haelt
    // Direct3D die Bindung fuer ungueltig und zeichnet still nichts.
    ID3D11ShaderResourceView* nichts[2] = {nullptr, nullptr};
    c->PSSetShaderResources(0, 2, nichts);
    c->OMSetRenderTargets(1, &ziel, nullptr);
    D3D11_VIEWPORT vp{};
    vp.Width = static_cast<float>(breite);
    vp.Height = static_cast<float>(hoehe);
    vp.MaxDepth = 1.0F;
    c->RSSetViewports(1, &vp);
    c->PSSetShader(ps, nullptr, 0);
    ID3D11ShaderResourceView* srvs[2] = {quelle, nullptr};
    c->PSSetShaderResources(0, 2, srvs);
    // Der KLEMMENDE Abtaster: siehe g_samplerRand. Mit WRAP holt sich der
    // Weichzeichner am Rand die gegenueberliegende Kante.
    c->PSSetSamplers(0, 1, (g_samplerRand != nullptr) ? &g_samplerRand
                                                      : &g_sampler);
    c->Draw(3, 0);
}

}  // namespace

bool bereiteZiel(int breite, int hoehe) {
    ID3D11Device* d = dev();
    if (d == nullptr || breite <= 0 || hoehe <= 0) {
        return false;
    }
    if (g_zielRtv != nullptr && g_zielW == breite && g_zielH == hoehe) {
        return true;
    }
    // Nur die Groesse hat sich geaendert - die Mover-Netze bleiben gueltig.
    // (Beim Kartenwechsel raeumt shutdown() auf; ein neues Netz an alter
    // Adresse faellt ueber `anderesNetz` auf.)
    if (g_zielSrv != nullptr) { g_zielSrv->Release(); g_zielSrv = nullptr; }
    if (g_zielRtv != nullptr) { g_zielRtv->Release(); g_zielRtv = nullptr; }
    if (g_zielTex != nullptr) { g_zielTex->Release(); g_zielTex = nullptr; }
    if (g_tiefeDsv != nullptr) { g_tiefeDsv->Release(); g_tiefeDsv = nullptr; }
    if (g_tiefeTex != nullptr) { g_tiefeTex->Release(); g_tiefeTex = nullptr; }
    if (g_tiefeLesen != nullptr) { g_tiefeLesen->Release(); g_tiefeLesen = nullptr; }

    D3D11_TEXTURE2D_DESC td{};
    td.Width = static_cast<UINT>(breite);
    td.Height = static_cast<UINT>(hoehe);
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    if (FAILED(d->CreateTexture2D(&td, nullptr, &g_zielTex))) {
        return false;
    }
    if (FAILED(d->CreateRenderTargetView(g_zielTex, nullptr, &g_zielRtv)) ||
        FAILED(d->CreateShaderResourceView(g_zielTex, nullptr, &g_zielSrv))) {
        return false;
    }
    D3D11_TEXTURE2D_DESC dd = td;
    dd.Format = DXGI_FORMAT_D32_FLOAT;
    dd.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    if (FAILED(d->CreateTexture2D(&dd, nullptr, &g_tiefeTex))) {
        return false;
    }
    if (FAILED(d->CreateDepthStencilView(g_tiefeTex, nullptr, &g_tiefeDsv))) {
        return false;
    }
    g_zielW = breite;
    g_zielH = hoehe;
    return true;
}

void* zielTextur() { return g_zielSrv; }

bool leseTiefe(std::vector<float>& abstand, int breite, int hoehe, float nah, float fern) {
    ID3D11Device* d = dev();
    ID3D11DeviceContext* c = ctx();
    if (d == nullptr || c == nullptr || g_tiefeTex == nullptr || breite <= 0 || hoehe <= 0) {
        return false;
    }
    D3D11_TEXTURE2D_DESC td{};
    g_tiefeTex->GetDesc(&td);
    if (static_cast<int>(td.Width) != breite || static_cast<int>(td.Height) != hoehe) {
        return false;
    }
    if (g_tiefeLesen != nullptr) {
        D3D11_TEXTURE2D_DESC ld{};
        g_tiefeLesen->GetDesc(&ld);
        if (ld.Width != td.Width || ld.Height != td.Height) {
            g_tiefeLesen->Release();
            g_tiefeLesen = nullptr;
        }
    }
    if (g_tiefeLesen == nullptr) {
        D3D11_TEXTURE2D_DESC ld = td;
        ld.Usage = D3D11_USAGE_STAGING;
        ld.BindFlags = 0;
        ld.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        ld.MiscFlags = 0;
        if (FAILED(d->CreateTexture2D(&ld, nullptr, &g_tiefeLesen))) {
            g_tiefeLesen = nullptr;
            return false;
        }
    }
    c->CopyResource(g_tiefeLesen, g_tiefeTex);
    D3D11_MAPPED_SUBRESOURCE m{};
    if (FAILED(c->Map(g_tiefeLesen, 0, D3D11_MAP_READ, 0, &m))) {
        return false;
    }
    abstand.resize(static_cast<std::size_t>(breite) * static_cast<std::size_t>(hoehe));
    // Tiefe d = a + b / z mit a = f/(f-n), b = -n f/(f-n) (baueViewProj),
    // also z = n f / (f - d (f - n)). d = 1 ist "nichts gezeichnet".
    const float fn = fern - nah;
    for (int y = 0; y < hoehe; ++y) {
        const float* zeile = reinterpret_cast<const float*>(
            static_cast<const unsigned char*>(m.pData) + static_cast<std::size_t>(y) * m.RowPitch);
        float* aus = abstand.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(breite);
        for (int x = 0; x < breite; ++x) {
            const float t = zeile[x];
            aus[x] = (t >= 0.999999F) ? 1.0e30F : nah * fern / (fern - t * fn);
        }
    }
    c->Unmap(g_tiefeLesen, 0);
    return true;
}

bool leseFarbe(std::vector<std::uint8_t>& rgba, int& breite, int& hoehe) {
    ID3D11Device* d = dev();
    ID3D11DeviceContext* c = ctx();
    if (d == nullptr || c == nullptr || g_zielTex == nullptr) {
        return false;
    }
    D3D11_TEXTURE2D_DESC td{};
    g_zielTex->GetDesc(&td);
    D3D11_TEXTURE2D_DESC ld = td;
    ld.Usage = D3D11_USAGE_STAGING;
    ld.BindFlags = 0;
    ld.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ld.MiscFlags = 0;
    ID3D11Texture2D* lesen = nullptr;
    if (FAILED(d->CreateTexture2D(&ld, nullptr, &lesen)) || lesen == nullptr) {
        return false;
    }
    c->CopyResource(lesen, g_zielTex);
    D3D11_MAPPED_SUBRESOURCE m{};
    if (FAILED(c->Map(lesen, 0, D3D11_MAP_READ, 0, &m))) {
        lesen->Release();
        return false;
    }
    breite = static_cast<int>(td.Width);
    hoehe = static_cast<int>(td.Height);
    const std::size_t zeile = static_cast<std::size_t>(breite) * 4U;
    rgba.resize(zeile * static_cast<std::size_t>(hoehe));
    for (int y = 0; y < hoehe; ++y) {
        std::memcpy(rgba.data() + static_cast<std::size_t>(y) * zeile,
                    static_cast<const unsigned char*>(m.pData) + static_cast<std::size_t>(y) * m.RowPitch, zeile);
    }
    c->Unmap(lesen, 0);
    lesen->Release();
    return true;
}

int zeichneMover(const BspMesh& mesh, const TextureSet* textures,
                 const BspGeometry* geo, const float* viewProj,
                 const float* welt, const float* kameraPos,
                 const float* vorn, const float* rechts, const float* hoch,
                 float fokus, float zeitSekunden, float helligkeit,
                 float grundlicht, std::string* fehler,
                 int* uebersprungenAus, Lage lage) {
    // Netzplatz 2: Mover teilen ihn sich. Sie werden nacheinander
    // gezeichnet, und ihre Netze aendern sich nicht - nur die Stellung, und
    // die steht in der Matrix.
    //
    // Der Netzwechsel je Mover kostet dadurch ein Neuanlegen des Puffers.
    // Das ist der Preis dafuer, nicht zwoelf Plaetze zu verwalten; Tueren
    // sind klein, und es sind ein Dutzend, nicht tausend.
    // --- Die Kameraachsen MUESSEN mit ------------------------------------
    //
    // `JeBild` ist EIN Puffer fuer alle Aufrufe eines Bildes. Wer hier
    // nullptr durchreicht, schreibt Nullen hinein - und der Gluehdurchgang
    // danach zeichnet den Himmel mit Nullachsen.
    //
    // Das Ergebnis war der Himmelsfaecher aus rc402, diesmal ueber die ganze
    // Karte gelegt: rote Strahlen aus einem Punkt. Ein Mover braucht die
    // Achsen selbst nicht, aber er darf sie nicht LOESCHEN.
    // Die Stellung VOR dem Zeichnen in die Kameramatrix rechnen. Der Shader
    // kennt dadurch nur eine Matrix, und der Stapelpuffer bleibt bei 60
    // float - siehe die Notiz in gpucam.h.
    float mvp[16];
    multipliziere(viewProj, welt, mvp);
    return zeichneKarte(mesh, textures, geo, mvp, kameraPos,
                        zeitSekunden, helligkeit, grundlicht, 2,
                        vorn, rechts, hoch, fokus, fehler, uebersprungenAus,
                        lage);
}

int zeichneGluehen(const BspMesh& mesh, const TextureSet* textures,
                   const BspGeometry* geo, const float* viewProj,
                   float zeitSekunden, int breite, int hoehe, bool weich,
                   std::string* fehler) {
    ID3D11Device* d = dev();
    ID3D11DeviceContext* c = ctx();
    if (d == nullptr || c == nullptr || g_zielRtv == nullptr ||
        breite <= 0 || hoehe <= 0) {
        if (fehler != nullptr) { *fehler = "kein Geraet oder kein Ziel"; }
        return 0;
    }

    // Nur die leuchtenden Stapel - aus der geprueften Liste gefiltert
    // (gpudraw.cpp), damit die Reihenfolge dieselbe bleibt.
    const std::vector<DrawCall> alle =
        buildDrawCalls(mesh, textures, zeitSekunden);
    const std::vector<DrawCall> gluehend = nurGluehende(alle);
    // --- Die Puffer HIER binden ---------------------------------------
    //
    // `zeichneAufrufe` bindet sie nicht selbst; das tat bisher allein
    // `zeichneKarte` (IASetVertexBuffers/IASetIndexBuffer). Der
    // Gluehdurchgang rief `zeichneAufrufe` mit den Zeichenaufrufen der
    // KARTE, waehrend noch die Puffer der zuletzt gezeichneten FIGUR
    // gebunden waren - ein Modellindexpuffer mit ein paar tausend
    // Eintraegen gegen Kartenindizes bis in die Hunderttausende.
    //
    // Genau davon spricht die Meldung der Debugschicht:
    //   "DrawIndexed: Index buffer has not enough space!"
    //
    // Die Reihenfolge im Bild ist Karte, Effekte, Mover, Figuren, Gluehen -
    // die Figuren kommen also unmittelbar davor, jedes Mal.
    if (gluehend.empty()) {
        // Kein Fehler: viele Karten haben keine leuchtenden Flaechen.
        return 0;
    }
    setzeSchritt("Gluehen: Puffer");
    // Wie bei den Figuren: nach den Movern steht in gViewProj
    // `Kamera x Moverstellung`. Der Gluehdurchgang zeichnet Kartenstapel und
    // braucht die reine Kamera.
    if (g_bildDatenDa && g_cbBild != nullptr && viewProj != nullptr) {
        std::memcpy(g_bildDaten, viewProj, 16U * sizeof(float));
        D3D11_MAPPED_SUBRESOURCE mb{};
        if (SUCCEEDED(c->Map(g_cbBild, 0, D3D11_MAP_WRITE_DISCARD, 0, &mb))) {
            std::memcpy(mb.pData, g_bildDaten, sizeof(g_bildDaten));
            c->Unmap(g_cbBild, 0);
        }
    }
    if (!bereiteGluehpuffer(breite, hoehe)) {
        if (fehler != nullptr) { *fehler = "Gluehpuffer nicht anlegbar"; }
        return 0;
    }

    // --- Durchgang 1: die leuchtenden Stapel in den kleinen Puffer --------
    //
    // Mit der TIEFE der Hauptszene, aber ohne hineinzuschreiben: eine
    // leuchtende Flaeche hinter einer Wand darf nicht leuchten. Der Rasterer
    // macht es genauso (applyGlow uebernimmt die Tiefe des ersten
    // Durchgangs).
    //
    // Der Tiefenpuffer hat die Groesse des ZIELS, der Gluehpuffer ist
    // kleiner - beide zusammen gehen deshalb nicht. Also wird in voller
    // Groesse gezeichnet und danach verkleinert, wie beim Rasterer.
    const float leer[4] = {0.0F, 0.0F, 0.0F, 1.0F};
    c->OMSetRenderTargets(1, &g_glowVollRtv, g_tiefeDsv);
    c->ClearRenderTargetView(g_glowVollRtv, leer);
    D3D11_VIEWPORT vp{};
    vp.Width = static_cast<float>(g_zielW);
    vp.Height = static_cast<float>(g_zielH);
    vp.MaxDepth = 1.0F;
    c->RSSetViewports(1, &vp);
    // Die Puffer der KARTE binden, bevor Kartenaufrufe abgesetzt werden.
    // Ohne diese drei Zeilen galt, was der letzte Zeichner gebunden hatte -
    // bei der ueblichen Reihenfolge also die Figur davor.
    {
        setzeSchritt("Gluehen: Puffer binden");
        Netzpuffer& np = g_netz[0];
        if (np.vb == nullptr || np.ib == nullptr || np.schrittweite == 0) {
            if (fehler != nullptr) {
                *fehler = "Gluehen: die Karte hat keine Puffer";
            }
            return 0;
        }
        const UINT schritt = np.schrittweite;
        const UINT versatz = 0;
        c->IASetVertexBuffers(0, 1, &np.vb, &schritt, &versatz);
        c->IASetIndexBuffer(np.ib, DXGI_FORMAT_R32_UINT, 0);
        c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    }
    setzeSchritt("Gluehen: zeichnen");
    const int n = zeichneAufrufe(gluehend, mesh, textures, geo, zeitSekunden,
                                 fehler);
    if (n == 0) {
        return 0;
    }

    // --- Durchgang 2: verkleinern und fuenfmal weichzeichnen --------------
    //
    // Fuenf Durchgaenge, Abstand waechst um 0,8 und wird gerundet,
    // Gewicht 1,13/4 - alles aus mapview.h:578. Andere Werte ergaeben ein
    // anderes Gluehen, und der Vergleich mit dem Rasterer waere wertlos.
    setzeSchritt("Gluehen: weichzeichnen");
    if (!wechsleAufVollbild()) {
        if (fehler != nullptr) { *fehler = "Vollbild-Shader fehlt"; }
        return 0;
    }
    // Erst verkleinern - mit dem Kastenmittel wie shrink() im Rasterer.
    setzeGlowKonstanten(0.0F, 1.0F, weich);
    blitte(g_glowVollSrv, g_glowRtv[0], breite, hoehe, g_glowShrinkPs);

    float abstand = 0.1F;
    int quelle = 0;
    for (int p = 0; p < 5; ++p) {
        const auto d2 = static_cast<float>(std::lround(abstand));
        setzeGlowKonstanten(d2, 1.13F * 0.25F, weich);
        blitte(g_glowSrv[quelle], g_glowRtv[1 - quelle], breite, hoehe,
               g_glowBlurPs);
        quelle = 1 - quelle;
        abstand += 0.8F;
    }

    // --- Durchgang 3: auflegen -------------------------------------------
    //
    // Liest die Szene und schreibt sie - deshalb ueber den zweiten
    // Vollpuffer, nicht am Ziel vorbei. Eine Textur gleichzeitig zu lesen
    // und zu beschreiben ist bei Direct3D nicht erlaubt, und der Treiber
    // meldet es als stille Nichtbindung: das Bild bliebe schwarz.
    setzeSchritt("Gluehen: auflegen");
    setzeGlowKonstanten(0.0F, 1.0F, weich);
    // --- Erst das Ziel setzen, DANN die Eingaenge -------------------------
    //
    // Eine Textur darf bei Direct3D nicht gleichzeitig Eingang und Ausgang
    // sein. Bindet man die Eingaenge zuerst, setzt die Laufzeit sie beim
    // OMSetRenderTargets stillschweigend auf NULL - der Durchgang liest dann
    // nichts, ohne dass irgendwo ein Fehler steht.
    //
    // Hier war es zufaellig unkritisch (Ziel und Eingang sind verschiedene
    // Texturen), aber die Reihenfolge Ziel-vor-Eingang ist die richtige und
    // haelt auch, wenn sich das einmal aendert.
    c->OMSetRenderTargets(1, &g_glowVollRtv, nullptr);
    ID3D11ShaderResourceView* zwei[2] = {g_glowSrv[quelle], g_zielSrv};
    c->PSSetShaderResources(0, 2, zwei);
    // Auch beim Auflegen klemmen - es liest dieselben Vollbildtexturen.
    c->PSSetSamplers(0, 1, (g_samplerRand != nullptr) ? &g_samplerRand
                                                      : &g_sampler);
    vp.Width = static_cast<float>(g_zielW);
    vp.Height = static_cast<float>(g_zielH);
    c->RSSetViewports(1, &vp);
    c->PSSetShader(g_glowCompPs, nullptr, 0);
    c->Draw(3, 0);
    // Abmelden VOR dem Kopieren: g_zielSrv zeigt auf g_zielTex, und das ist
    // gleich das Ziel der Kopie. Eine Textur, die noch als Eingang gebunden
    // ist, waehrend sie beschrieben wird, ist genau die Gefahrenstelle aus
    // der Direct3D-Dokumentation.
    ID3D11ShaderResourceView* nichts[3] = {nullptr, nullptr, nullptr};
    c->PSSetShaderResources(0, 3, nichts);
    c->OMSetRenderTargets(0, nullptr, nullptr);
    c->CopyResource(g_zielTex, g_glowVollTex);
    return n;
}

// --- Dynamisches Licht und Blobschatten ------------------------------------
namespace {

ID3DBlob* uebersetze(const std::string& src, const char* profil, const char* name,
                     std::string* fehler);

ID3D11VertexShader* g_lichtVs = nullptr;
ID3D11PixelShader* g_lichtPs = nullptr;
ID3D11PixelShader* g_schattenPs = nullptr;
ID3D11InputLayout* g_lichtLayout = nullptr;
ID3D11Buffer* g_cbLicht = nullptr;
bool g_lichtVersucht = false;

// Aufbau von JeLicht (gpushader.cpp, kLichtKonstanten): nur float4-Felder,
// also keine Packluecken.
constexpr int kMaxLichter = 32;   // MAX_DLIGHTS (tr_local.h)
constexpr int kMaxSchatten = 32;
constexpr std::size_t kLichtOrtAt = 4U;
constexpr std::size_t kLichtFarbeAt = kLichtOrtAt + 4U * kMaxLichter;
constexpr std::size_t kSchattenOrtAt = kLichtFarbeAt + 4U * kMaxLichter;
constexpr std::size_t kSchattenNormaleAt = kSchattenOrtAt + 4U * kMaxSchatten;
constexpr std::size_t kLichtFloats = kSchattenNormaleAt + 4U * kMaxSchatten;

// Aus code/game/surfaceflags.h - wie in bspgeo.cpp abgeschrieben.
constexpr std::uint32_t kSurfSkyFlag = 0x00002000U;
constexpr std::uint32_t kSurfNoImpact = 0x00080000U;
constexpr std::uint32_t kSurfNoMarks = 0x00100000U;
constexpr std::uint32_t kSurfNoDlight = 0x00800000U;

// Der Huellkasten je Stapel - einmal je Netz. Ohne ihn bekaeme jeder
// deckende Stapel der Karte einen zweiten Aufruf, auch wenn kein Licht in
// seiner Naehe ist.
struct StapelKasten {
    float mins[3]{};
    float maxs[3]{};
};
std::vector<StapelKasten> g_stapelKaesten;
const void* g_kastenVerts = nullptr;
const void* g_kastenIdx = nullptr;
std::size_t g_kastenBatches = 0;

const std::vector<StapelKasten>& stapelKaesten(const BspMesh& mesh) {
    if (g_kastenVerts == mesh.verts.data() && g_kastenIdx == mesh.indexes.data() &&
        g_kastenBatches == mesh.batches.size()) {
        return g_stapelKaesten;
    }
    g_stapelKaesten.assign(mesh.batches.size(), StapelKasten{});
    for (std::size_t b = 0; b < mesh.batches.size(); ++b) {
        const BspMesh::Batch& bt = mesh.batches[b];
        StapelKasten& k = g_stapelKaesten[b];
        for (int a = 0; a < 3; ++a) {
            k.mins[a] = 1.0e30F;
            k.maxs[a] = -1.0e30F;
        }
        const std::size_t ende = std::min<std::size_t>(
            static_cast<std::size_t>(bt.firstIndex) + bt.numIndexes, mesh.indexes.size());
        for (std::size_t i = bt.firstIndex; i < ende; ++i) {
            const std::uint32_t v = mesh.indexes[i];
            if (v >= mesh.verts.size()) {
                continue;
            }
            for (int a = 0; a < 3; ++a) {
                k.mins[a] = std::min(k.mins[a], mesh.verts[v].xyz[a]);
                k.maxs[a] = std::max(k.maxs[a], mesh.verts[v].xyz[a]);
            }
        }
    }
    g_kastenVerts = mesh.verts.data();
    g_kastenIdx = mesh.indexes.data();
    g_kastenBatches = mesh.batches.size();
    return g_stapelKaesten;
}

// Beruehrt der Wuerfel um `ort` mit halber Kantenlaenge `r` den Kasten?
bool kastenNahe(const StapelKasten& k, const float ort[3], float r) {
    for (int a = 0; a < 3; ++a) {
        if (ort[a] + r < k.mins[a] || ort[a] - r > k.maxs[a]) {
            return false;
        }
    }
    return true;
}

bool holeLichtShader(std::string* fehler) {
    if (g_lichtVs != nullptr && g_lichtPs != nullptr && g_schattenPs != nullptr &&
        g_lichtLayout != nullptr) {
        return true;
    }
    if (g_lichtVersucht) {
        if (fehler != nullptr) { *fehler = "Licht-Shader nicht uebersetzbar"; }
        return false;
    }
    g_lichtVersucht = true;
    ID3D11Device* d = dev();
    if (d == nullptr) {
        return false;
    }
    ID3DBlob* vs = uebersetze(lichtVertexShaderHlsl(), "vs_4_0", "licht", fehler);
    if (vs == nullptr) {
        return false;
    }
    ID3DBlob* ps = uebersetze(lichtPixelShaderHlsl(), "ps_4_0", "licht", fehler);
    ID3DBlob* ps2 = uebersetze(schattenPixelShaderHlsl(), "ps_4_0", "schatten", fehler);
    if (ps == nullptr || ps2 == nullptr) {
        vs->Release();
        if (ps != nullptr) { ps->Release(); }
        if (ps2 != nullptr) { ps2->Release(); }
        return false;
    }
    d->CreateVertexShader(vs->GetBufferPointer(), vs->GetBufferSize(), nullptr, &g_lichtVs);
    d->CreatePixelShader(ps->GetBufferPointer(), ps->GetBufferSize(), nullptr, &g_lichtPs);
    d->CreatePixelShader(ps2->GetBufferPointer(), ps2->GetBufferSize(), nullptr,
                         &g_schattenPs);
    // Dieselbe Ecke wie in holeShader (zeichneKarte, `Ecke`, 44 Byte).
    const D3D11_INPUT_ELEMENT_DESC ein[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 1, DXGI_FORMAT_R32G32_FLOAT, 0, 32, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 40, D3D11_INPUT_PER_VERTEX_DATA, 0},
    };
    d->CreateInputLayout(ein, static_cast<UINT>(std::size(ein)), vs->GetBufferPointer(),
                         vs->GetBufferSize(), &g_lichtLayout);
    vs->Release();
    ps->Release();
    ps2->Release();
    if (g_lichtVs == nullptr || g_lichtPs == nullptr || g_schattenPs == nullptr ||
        g_lichtLayout == nullptr) {
        if (fehler != nullptr) { *fehler = "Licht-Shader unvollstaendig"; }
        return false;
    }
    return true;
}

void gibLichtFrei() {
    if (g_lichtVs != nullptr) { g_lichtVs->Release(); g_lichtVs = nullptr; }
    if (g_lichtPs != nullptr) { g_lichtPs->Release(); g_lichtPs = nullptr; }
    if (g_schattenPs != nullptr) { g_schattenPs->Release(); g_schattenPs = nullptr; }
    if (g_lichtLayout != nullptr) { g_lichtLayout->Release(); g_lichtLayout = nullptr; }
    if (g_cbLicht != nullptr) { g_cbLicht->Release(); g_cbLicht = nullptr; }
    g_lichtVersucht = false;
}

}  // namespace

int zeichneLichtUndSchatten(const BspMesh& mesh, const TextureSet* textures,
                            const BspGeometry* geo, const float* viewProj,
                            float zeitSekunden,
                            const std::vector<WeltLicht>& lichter,
                            const std::vector<BlobSchatten>& schatten,
                            const TextureSet::Tex* dlichtBild,
                            const TextureSet::Tex* schattenBild,
                            std::string* fehler) {
    ID3D11Device* d = dev();
    ID3D11DeviceContext* c = ctx();
    if (d == nullptr || c == nullptr || g_zielRtv == nullptr || g_tiefeDsv == nullptr ||
        viewProj == nullptr || mesh.batches.empty()) {
        return 0;
    }
    ID3D11ShaderResourceView* dlSrv =
        lichter.empty() ? nullptr : holeTexBild(dlichtBild);
    ID3D11ShaderResourceView* scSrv =
        schatten.empty() ? nullptr : holeTexBild(schattenBild);
    if (dlSrv == nullptr && scSrv == nullptr) {
        return 0;
    }
    setzeSchritt("Licht: Shader");
    if (!holeLichtShader(fehler)) {
        return 0;
    }
    if (g_cbLicht == nullptr) {
        D3D11_BUFFER_DESC bd{};
        bd.ByteWidth = static_cast<UINT>(kLichtFloats * sizeof(float));
        bd.Usage = D3D11_USAGE_DYNAMIC;
        bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        if (FAILED(d->CreateBuffer(&bd, nullptr, &g_cbLicht))) {
            if (fehler != nullptr) { *fehler = "Lichtpuffer nicht anlegbar"; }
            return 0;
        }
    }
    Netzpuffer& np = g_netz[0];
    if (np.vb == nullptr || np.ib == nullptr || np.schrittweite == 0) {
        return 0;
    }
    // Wie beim Gluehen: nach den Movern und Figuren steht in gViewProj
    // nicht mehr die reine Kamera.
    if (g_bildDatenDa && g_cbBild != nullptr) {
        std::memcpy(g_bildDaten, viewProj, 16U * sizeof(float));
        D3D11_MAPPED_SUBRESOURCE mb{};
        if (SUCCEEDED(c->Map(g_cbBild, 0, D3D11_MAP_WRITE_DISCARD, 0, &mb))) {
            std::memcpy(mb.pData, g_bildDaten, sizeof(g_bildDaten));
            c->Unmap(g_cbBild, 0);
        }
    }
    setzeSchritt("Licht: binden");
    c->OMSetRenderTargets(1, &g_zielRtv, g_tiefeDsv);
    D3D11_VIEWPORT vp{};
    vp.Width = static_cast<float>(g_zielW);
    vp.Height = static_cast<float>(g_zielH);
    vp.MaxDepth = 1.0F;
    c->RSSetViewports(1, &vp);
    const UINT schritt = np.schrittweite;
    const UINT versatz = 0;
    c->IASetVertexBuffers(0, 1, &np.vb, &schritt, &versatz);
    c->IASetIndexBuffer(np.ib, DXGI_FORMAT_R32_UINT, 0);
    c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    c->IASetInputLayout(g_lichtLayout);
    c->VSSetShader(g_lichtVs, nullptr, 0);
    c->VSSetConstantBuffers(0, 1, &g_cbBild);
    c->PSSetConstantBuffers(4, 1, &g_cbLicht);
    ID3D11SamplerState* sampler[2] = {g_sampler, g_samplerRand};
    c->PSSetSamplers(0, 2, sampler);
    // Auf der gezeichneten Flaeche, ohne zu schreiben - GLS_DEPTHFUNC_EQUAL
    // in der Engine, LESS_EQUAL hier aus demselben Grund wie bei
    // `depthFunc equal` (holeTiefe).
    PipelineState tiefe;
    tiefe.blend = Blend::Add;
    tiefe.depthWrite = false;
    tiefe.depthTestEqual = true;
    ID3D11DepthStencilState* ds = holeTiefe(tiefe);
    if (ds != nullptr) { c->OMSetDepthStencilState(ds, 0); }
    ID3D11BlendState* mitBild = holeBlend(Blend::Add);
    ID3D11BlendState* ohneBild =
        holeBlend(Blend::Unbekannt, BlendFactor::DstColor, BlendFactor::One);
    ID3D11BlendState* abdunkeln =
        holeBlend(Blend::Unbekannt, BlendFactor::Zero, BlendFactor::SrcColor);
    const float faktor[4] = {0.0F, 0.0F, 0.0F, 0.0F};

    setzeSchritt("Licht: Zeichenliste");
    const std::vector<DrawCall> calls = buildDrawCalls(mesh, textures, zeitSekunden);
    const std::vector<StapelKasten>& kaesten = stapelKaesten(mesh);
    std::vector<int> lichtNr;
    std::vector<int> schattenNr;
    int abgesetzt = 0;
    setzeSchritt("Licht: zeichnen");
    for (const DrawCall& call : calls) {
        // Nur die Grundstufe deckender Flaechen: sort <= SS_OPAQUE in der
        // Engine. Bewegte Ecken (deformVertexes, autosprite) lagen im
        // ersten Durchgang woanders, als dieser Shader sie hinlegen wuerde.
        if (call.zusatzstufe || call.state.blend != Blend::Opaque ||
            (call.state.features & (kSky | kDeform | kAutosprite)) != 0U ||
            call.batch >= mesh.batches.size() || call.batch >= kaesten.size()) {
            continue;
        }
        const BspMesh::Batch& bt = mesh.batches[call.batch];
        std::uint32_t flaggen = 0;
        if (geo != nullptr && bt.shader >= 0 &&
            static_cast<std::size_t>(bt.shader) < geo->shaders.size()) {
            flaggen = geo->shaders[static_cast<std::size_t>(bt.shader)].surfaceFlags;
        }
        if ((flaggen & kSurfSkyFlag) != 0U) {
            continue;
        }
        const StapelKasten& k = kaesten[call.batch];
        lichtNr.clear();
        schattenNr.clear();
        if (dlSrv != nullptr && (flaggen & kSurfNoDlight) == 0U) {
            for (std::size_t i = 0; i < lichter.size() &&
                                    lichtNr.size() < static_cast<std::size_t>(kMaxLichter);
                 ++i) {
                if (lichter[i].radius > 0.0F &&
                    kastenNahe(k, lichter[i].ort, lichter[i].radius)) {
                    lichtNr.push_back(static_cast<int>(i));
                }
            }
        }
        if (scSrv != nullptr && (flaggen & (kSurfNoImpact | kSurfNoMarks)) == 0U) {
            for (std::size_t i = 0; i < schatten.size() &&
                                    schattenNr.size() < static_cast<std::size_t>(kMaxSchatten);
                 ++i) {
                // Quadrat +-radius in der Ebene, 32 darueber und 20 darunter.
                if (kastenNahe(k, schatten[i].ort, schatten[i].radius + 32.0F)) {
                    schattenNr.push_back(static_cast<int>(i));
                }
            }
        }
        if (lichtNr.empty() && schattenNr.empty()) {
            continue;
        }
        const float stapelZeit = zeitSekunden - bt.shaderTime;
        const BatchState bs2 = batchStateFor(textures, call.bild, stapelZeit);
        ID3D11ShaderResourceView* bild =
            holeBild(textures, animBildFuer(textures, call.bild, stapelZeit));
        if (bild == nullptr) {
            continue;
        }
        // Das Bild der Grundstufe nimmt die Engine nur ohne tcMod und ohne
        // tcGen environment (dStage, ProjectDlightTexture2) - sonst mischt
        // sie das Licht ohne Bild auf den Bildspeicher.
        const bool mitTextur = bs2.numTexMods == 0 && bs2.texGen != TexGen::Environment;
        const float schwelle = ((call.state.features & kAlphaTest) != 0U)
                                   ? std::max(call.state.alphaSchwelle, 1.0e-4F)
                                   : 0.0F;
        c->RSSetState(holeRaster(call.state.features));
        auto packe = [&](bool licht) {
            D3D11_MAPPED_SUBRESOURCE m{};
            if (FAILED(c->Map(g_cbLicht, 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) {
                return false;
            }
            auto* f = static_cast<float*>(m.pData);
            std::memset(f, 0, kLichtFloats * sizeof(float));
            f[0] = licht ? static_cast<float>(lichtNr.size()) : 0.0F;
            f[1] = licht ? 0.0F : static_cast<float>(schattenNr.size());
            f[2] = schwelle;
            f[3] = (licht && mitTextur) ? 1.0F : 0.0F;
            if (licht) {
                // OHNE den Helligkeitsregler. Der steht fuer
                // r_mapOverBrightBits - das Verschieben der Lightmaps beim
                // Laden (R_ColorShiftLightingBytes). Das dynamische Licht
                // kommt daran nicht vorbei: es ist dl->color * modulate,
                // und mit r_overBrightBits 0 (Vorgabe, auch in der
                // jaconfig.cfg der Mod) hebt keine Gammarampe den
                // Bildspeicher nachtraeglich an. Mit dem Faktor 2 war der
                // Lichtkreis eines Schwertes eine grelle Scheibe.
                for (std::size_t j = 0; j < lichtNr.size(); ++j) {
                    const WeltLicht& l = lichter[static_cast<std::size_t>(lichtNr[j])];
                    float* o = f + kLichtOrtAt + 4U * j;
                    float* fa = f + kLichtFarbeAt + 4U * j;
                    o[0] = l.ort[0]; o[1] = l.ort[1]; o[2] = l.ort[2]; o[3] = l.radius;
                    fa[0] = l.farbe[0]; fa[1] = l.farbe[1]; fa[2] = l.farbe[2];
                }
            } else {
                for (std::size_t j = 0; j < schattenNr.size(); ++j) {
                    const BlobSchatten& s = schatten[static_cast<std::size_t>(schattenNr[j])];
                    float* o = f + kSchattenOrtAt + 4U * j;
                    float* n = f + kSchattenNormaleAt + 4U * j;
                    o[0] = s.ort[0]; o[1] = s.ort[1]; o[2] = s.ort[2]; o[3] = s.radius;
                    n[0] = s.normale[0]; n[1] = s.normale[1]; n[2] = s.normale[2];
                    n[3] = s.alpha;
                }
            }
            c->Unmap(g_cbLicht, 0);
            return true;
        };
        // Erst das Licht (Teil des Shaders der Flaeche), dann der Schatten
        // (ein Abziehbild der Sortierstufe decal - kommt spaeter).
        if (!lichtNr.empty() && packe(true)) {
            c->OMSetBlendState(mitTextur ? mitBild : ohneBild, faktor, 0xFFFFFFFFU);
            c->PSSetShader(g_lichtPs, nullptr, 0);
            ID3D11ShaderResourceView* srvs[2] = {bild, dlSrv};
            c->PSSetShaderResources(0, 2, srvs);
            c->DrawIndexed(call.numIndexes, call.firstIndex, 0);
            ++abgesetzt;
        }
        if (!schattenNr.empty() && packe(false)) {
            c->OMSetBlendState(abdunkeln, faktor, 0xFFFFFFFFU);
            c->PSSetShader(g_schattenPs, nullptr, 0);
            ID3D11ShaderResourceView* srvs[2] = {bild, scSrv};
            c->PSSetShaderResources(0, 2, srvs);
            c->DrawIndexed(call.numIndexes, call.firstIndex, 0);
            ++abgesetzt;
        }
    }
    ID3D11ShaderResourceView* nichts[2] = {nullptr, nullptr};
    c->PSSetShaderResources(0, 2, nichts);
    return abgesetzt;
}

int zeichneFigurIn(const GlmModel& model, const ModelTextures* textures,
                   const std::vector<BoneMatrix>& knochen, const float* welt,
                   const float* viewProj, std::string* fehler,
                   const FigurLicht* licht, bool kappen, bool ersatzBild);

int zeichneFigur(const GlmModel& model, const ModelTextures* textures,
                 const std::vector<BoneMatrix>& knochen, const float* welt,
                 const float* viewProj, std::string* fehler,
                 const FigurLicht* licht, bool kappen) {
    return zeichneFigurIn(model, textures, knochen, welt, viewProj, fehler,
                          licht, kappen, false);
}

int zeichneFigurIn(const GlmModel& model, const ModelTextures* textures,
                   const std::vector<BoneMatrix>& knochen, const float* welt,
                   const float* viewProj, std::string* fehler,
                   const FigurLicht* licht, bool kappen, bool ersatzBild) {
    ID3D11Device* d = dev();
    ID3D11DeviceContext* c = ctx();
    if (d == nullptr || c == nullptr || welt == nullptr || viewProj == nullptr ||
        (g_zielRtv == nullptr && g_modellRtv == nullptr)) {
        if (fehler != nullptr) { *fehler = "kein Geraet oder kein Ziel"; }
        return 0;
    }

    // --- Der Eckenpuffer haengt am MODELL, nicht an der Figur ------------
    //
    // Zwei Sturmtruppler teilen sich ein Modell und damit einen Puffer -
    // sie unterscheiden sich nur in den Knochenmatrizen. Ein Puffer je
    // Figur waere bei 27 Figuren siebenundzwanzigmal dieselben Ecken.
    setzeSchritt("Figur: Puffer");
    FigurPuffer* fp = holeFigurPuffer(model, fehler);
    if (fp == nullptr) {
        return 0;
    }

    // --- Der Knochenpuffer haengt an der FIGUR ---------------------------
    if (g_cbFigur == nullptr) {
        D3D11_BUFFER_DESC bd{};
        // 384 float4 Knochen + eine 4x4-Matrix + drei float4 Licht.
        bd.ByteWidth = (static_cast<UINT>(kMaxKnochen) * 12U + 16U + 12U) * 4U;
        bd.Usage = D3D11_USAGE_DYNAMIC;
        bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        if (FAILED(d->CreateBuffer(&bd, nullptr, &g_cbFigur))) {
            if (fehler != nullptr) { *fehler = "Knochenpuffer nicht anlegbar"; }
            return 0;
        }
    }
    {
        D3D11_MAPPED_SUBRESOURCE m{};
        if (SUCCEEDED(c->Map(g_cbFigur, 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) {
            auto* f = static_cast<float*>(m.pData);
            packeKnochen(knochen, f);
            std::memcpy(f + static_cast<std::size_t>(kMaxKnochen) * 12U, welt,
                        16U * sizeof(float));
            float* l = f + static_cast<std::size_t>(kMaxKnochen) * 12U + 16U;
            for (int k = 0; k < 12; ++k) { l[k] = 0.0F; }
            if (licht != nullptr && licht->gitter) {
                for (int k = 0; k < 3; ++k) {
                    l[k] = licht->umgebung[k];
                    l[4 + k] = licht->gerichtet[k];
                    l[8 + k] = licht->richtung[k];
                }
                l[11] = 1.0F;
            }
            c->Unmap(g_cbFigur, 0);
        }
    }

    setzeSchritt("Figur: Shader");
    std::string sfehler;
    ShaderPaar* sp = holeFigurShader(&sfehler);
    if (sp == nullptr) {
        if (fehler != nullptr) { *fehler = sfehler; }
        return 0;
    }

    const UINT schritt = sizeof(SkinVertex);
    const UINT versatz = 0;
    c->IASetVertexBuffers(0, 1, &fp->vb, &schritt, &versatz);
    c->IASetIndexBuffer(fp->ib, DXGI_FORMAT_R32_UINT, 0);
    c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    c->IASetInputLayout(sp->layout);
    c->VSSetShader(sp->vs, nullptr, 0);
    c->PSSetShader(sp->ps, nullptr, 0);
    // --- Die REINE Kameramatrix wiederherstellen -------------------------
    //
    // Nach den Movern steht in gViewProj `Kamera x Moverstellung`. Figuren
    // bringen ihre Stellung selbst mit (gFigurWelt) und brauchen die reine
    // Kamera - sonst stehen sie um Drehpunkt und Versatz des letzten Movers
    // verschoben.
    if (g_bildDatenDa && g_cbBild != nullptr) {
        std::memcpy(g_bildDaten, viewProj, 16U * sizeof(float));
        D3D11_MAPPED_SUBRESOURCE mb{};
        if (SUCCEEDED(c->Map(g_cbBild, 0, D3D11_MAP_WRITE_DISCARD, 0, &mb))) {
            std::memcpy(mb.pData, g_bildDaten, sizeof(g_bildDaten));
            c->Unmap(g_cbBild, 0);
        }
    }
    c->VSSetConstantBuffers(0, 1, &g_cbBild);
    c->PSSetConstantBuffers(0, 1, &g_cbBild);
    c->VSSetConstantBuffers(2, 1, &g_cbFigur);
    // --- Auch b1, den Stapelpuffer ---------------------------------------
    //
    // Er wurde hier NICHT gebunden. Der Pixel-Shader liest daraus Farbe
    // (gRgb) und Alphaschwelle - und dort standen noch die Werte des
    // letzten Movers. Je nachdem, was zuletzt drin stand, wurde die Figur
    // unsichtbar.
    //
    // Genau das Bild: 378 Zeichenaufrufe fuer Figuren, 0 uebersprungen, und
    // trotzdem keine Figur zu sehen. Nicht der Aufruf war das Problem,
    // sondern der Zustand beim Zeichnen.
    //
    // Vorgabewerte hineinschreiben: weisse Farbe, keine Alphaschwelle, keine
    // Texturverschiebung. Figuren haben keine Shaderstufen wie Kartenflaechen.
    if (g_cbStapel != nullptr) {
        D3D11_MAPPED_SUBRESOURCE m2{};
        if (SUCCEEDED(c->Map(g_cbStapel, 0, D3D11_MAP_WRITE_DISCARD, 0,
                             &m2))) {
            BatchState leer;
            packeKonstanten(leer, static_cast<float*>(m2.pData), 0.0F);
            c->Unmap(g_cbStapel, 0);
        }
        c->VSSetConstantBuffers(1, 1, &g_cbStapel);
        c->PSSetConstantBuffers(1, 1, &g_cbStapel);
    }
    c->PSSetSamplers(0, 1, &g_sampler);
    // --- Tiefe und Keulen ausdruecklich setzen -------------------------
    //
    // Seit rc434 setzt `zeichneAufrufe` diese Zustaende je Stapel. Sie
    // BLEIBEN stehen. Wer nach einer gemischten Kartenflaeche zeichnet,
    // ohne sie zu setzen, erbt "keine Tiefe schreiben" - und die Figuren
    // waeren untereinander in beliebiger Reihenfolge sichtbar.
    //
    // Dieselbe Fehlerklasse wie `JeBild` in rc429: ein Zustand fuer
    // mehrere Aufrufe.
    {
        PipelineState fig;   // Vorgabe: deckend, schreibt Tiefe
        if (tiefeAn()) {
            ID3D11DepthStencilState* ds = holeTiefe(fig);
            if (ds != nullptr) { c->OMSetDepthStencilState(ds, 0); }
        }
        if (keulenAn()) {
            ID3D11RasterizerState* rs = holeRaster(kNichts);
            if (rs != nullptr) { c->RSSetState(rs); }
        }
    }
    // Figuren sind deckend. Der Rasterer zeichnet sie ebenso - wer hier
    // die Mischart des letzten Kartenstapels stehen laesst, bekommt
    // durchsichtige Figuren, und zwar je nach Kamera verschieden.
    setzeSchritt("Figur: zeichnen");
    int abgesetzt = 0;
    const PipelineState* letzter = nullptr;
    PipelineState zustand;
    for (const FigurTeil& t : fp->teile) {
        if (t.kappe && !kappen) {
            continue;
        }
        ID3D11ShaderResourceView* bild = holeFigurBild(textures, t.surface);
        if (bild == nullptr && ersatzBild) {
            bild = holeErsatzBild();
        }
        if (bild == nullptr) {
            ++g_figurFlaechenOhneBild;
            continue;
        }
        // --- Der Zustand JE FLAECHE ------------------------------------
        //
        // Hier stand einmal vor der Schleife `holeBlend(Blend::Opaque)` -
        // fuer alle Flaechen derselbe. Ein Hologramm mit
        // `blendFunc GL_ONE GL_ONE` wurde damit zu einem schwarzen Viereck
        // mit einem blassen Umriss darin; genau das stand auf dem Tisch in
        // md_am_sith, sobald das Gluehen aus war.
        //
        // `ModelTextures::bySurface` traegt die volle Shaderangabe je
        // Flaeche - Mischart, Faktoren, Keulen, Tiefe. Sie war da und wurde
        // nicht gelesen.
        //
        // Die Ableitung ist dieselbe wie bei einer Kartenflaeche
        // (`batchStateFor`, seit rc451 auch fuer ein `TextureSet::Tex`).
        // Zwei Fassungen davon waeren wieder auseinandergelaufen.
        PipelineState neu;
        if (textures != nullptr && t.surface < textures->bySurface.size()) {
            const BatchState fbs =
                batchStateFor(textures->bySurface[t.surface], 0.0F);
            neu = pipelineFor(fbs, false, false, false);
        }
        if (letzter == nullptr || !letzter->gleichWie(neu)) {
            zustand = neu;
            letzter = &zustand;
            ID3D11BlendState* bs =
                holeBlend(zustand.blend, zustand.srcFactor, zustand.dstFactor);
            if (bs != nullptr) {
                const float faktor[4] = {0.0F, 0.0F, 0.0F, 0.0F};
                c->OMSetBlendState(bs, faktor, 0xFFFFFFFFU);
            }
            if (tiefeAn()) {
                ID3D11DepthStencilState* ds = holeTiefe(zustand);
                if (ds != nullptr) { c->OMSetDepthStencilState(ds, 0); }
            }
            if (keulenAn()) {
                ID3D11RasterizerState* rs = holeRaster(zustand.features);
                if (rs != nullptr) { c->RSSetState(rs); }
            }
        }
        ID3D11ShaderResourceView* srvs[3] = {bild, nullptr, nullptr};
        c->PSSetShaderResources(0, 3, srvs);
        c->DrawIndexed(t.numIndexes, t.firstIndex, 0);
        ++abgesetzt;
    }
    return abgesetzt;
}

// --- Die Klingen der Lichtschwerter -------------------------------------
namespace {
struct KlingenEcke {
    float pos[3];
    float uv[2];
    float farbe[4];
};
ID3DBlob* uebersetze(const std::string& src, const char* profil, const char* name,
                     std::string* fehler) {
    ID3DBlob* blob = nullptr;
    ID3DBlob* err = nullptr;
    if (FAILED(D3DCompile(src.data(), src.size(), name, nullptr, nullptr, "main",
                          profil, uebersetzFahnen(), 0, &blob, &err))) {
        if (fehler != nullptr) {
            *fehler = std::string(name) + ": " +
                      ((err != nullptr) ? static_cast<const char*>(err->GetBufferPointer())
                                        : "nicht uebersetzbar");
        }
        if (err != nullptr) { err->Release(); }
        return nullptr;
    }
    if (err != nullptr) { err->Release(); }
    return blob;
}
}  // namespace

ShaderPaar* holeKlingeShader(std::string* fehler) {
    if (g_klingeShader.vs != nullptr) {
        return &g_klingeShader;
    }
    if (g_klingeShaderVersucht) {
        if (fehler != nullptr) { *fehler = "Klingen-Shader nicht uebersetzbar"; }
        return nullptr;
    }
    g_klingeShaderVersucht = true;
    ID3D11Device* d = dev();
    if (d == nullptr) {
        return nullptr;
    }
    ID3DBlob* vs = uebersetze(klingeVertexShaderHlsl(), "vs_4_0", "klinge", fehler);
    if (vs == nullptr) {
        return nullptr;
    }
    ID3DBlob* ps = uebersetze(klingePixelShaderHlsl(), "ps_4_0", "klinge", fehler);
    if (ps == nullptr) {
        vs->Release();
        return nullptr;
    }
    d->CreateVertexShader(vs->GetBufferPointer(), vs->GetBufferSize(), nullptr,
                          &g_klingeShader.vs);
    d->CreatePixelShader(ps->GetBufferPointer(), ps->GetBufferSize(), nullptr,
                         &g_klingeShader.ps);
    const D3D11_INPUT_ELEMENT_DESC ein[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 20, D3D11_INPUT_PER_VERTEX_DATA, 0},
    };
    d->CreateInputLayout(ein, static_cast<UINT>(std::size(ein)), vs->GetBufferPointer(),
                         vs->GetBufferSize(), &g_klingeShader.layout);
    vs->Release();
    ps->Release();
    if (g_klingeShader.vs == nullptr || g_klingeShader.ps == nullptr ||
        g_klingeShader.layout == nullptr) {
        if (fehler != nullptr) { *fehler = "Klingen-Shader unvollstaendig"; }
        return nullptr;
    }
    return &g_klingeShader;
}

int zeichneKlingen(const std::vector<KlingenQuad>& quads, const float* viewProj,
                   std::string* fehler) {
    ID3D11Device* d = dev();
    ID3D11DeviceContext* c = ctx();
    if (quads.empty()) {
        return 0;
    }
    if (d == nullptr || c == nullptr || viewProj == nullptr || g_zielRtv == nullptr ||
        g_tiefeDsv == nullptr || g_cbBild == nullptr) {
        if (fehler != nullptr) { *fehler = "Klingen: kein Ziel"; }
        return 0;
    }
    setzeSchritt("Klingen");
    ShaderPaar* sp = holeKlingeShader(fehler);
    if (sp == nullptr) {
        return 0;
    }
    // Sechs Ecken je Band (zwei Dreiecke: 0-1-2 und 0-2-3), die Textur QUER
    // ueber die Klinge wie im Rasterer.
    std::vector<KlingenEcke> ecken;
    ecken.reserve(quads.size() * 6U);
    const float uu[4] = {0.0F, 1.0F, 1.0F, 0.0F};
    const float vv[4] = {0.0F, 0.0F, 1.0F, 1.0F};
    const int folge[6] = {0, 1, 2, 0, 2, 3};
    for (const KlingenQuad& q : quads) {
        const bool mitBild = holeTexBild(q.tex) != nullptr;
        for (const int e : folge) {
            KlingenEcke k{};
            for (int a = 0; a < 3; ++a) { k.pos[a] = q.ecke[e][a]; }
            k.uv[0] = uu[e];
            k.uv[1] = vv[e];
            for (int a = 0; a < 3; ++a) { k.farbe[a] = q.farbe[a] / 255.0F; }
            k.farbe[3] = mitBild ? 1.0F : 0.0F;
            ecken.push_back(k);
        }
    }
    if (g_klingeVb == nullptr || g_klingeVbEcken < ecken.size()) {
        if (g_klingeVb != nullptr) { g_klingeVb->Release(); g_klingeVb = nullptr; }
        D3D11_BUFFER_DESC bd{};
        g_klingeVbEcken = std::max<std::size_t>(ecken.size() * 2U, 96U);
        bd.ByteWidth = static_cast<UINT>(g_klingeVbEcken * sizeof(KlingenEcke));
        bd.Usage = D3D11_USAGE_DYNAMIC;
        bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        if (FAILED(d->CreateBuffer(&bd, nullptr, &g_klingeVb))) {
            g_klingeVbEcken = 0;
            if (fehler != nullptr) { *fehler = "Klingen-Eckenpuffer nicht anlegbar"; }
            return 0;
        }
    }
    {
        D3D11_MAPPED_SUBRESOURCE m{};
        if (FAILED(c->Map(g_klingeVb, 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) {
            return 0;
        }
        std::memcpy(m.pData, ecken.data(), ecken.size() * sizeof(KlingenEcke));
        c->Unmap(g_klingeVb, 0);
    }
    // Die reine Kameramatrix - nach den Movern steht dort Kamera x Mover.
    std::memcpy(g_bildDaten, viewProj, 16U * sizeof(float));
    {
        D3D11_MAPPED_SUBRESOURCE mb{};
        if (SUCCEEDED(c->Map(g_cbBild, 0, D3D11_MAP_WRITE_DISCARD, 0, &mb))) {
            std::memcpy(mb.pData, g_bildDaten, sizeof(g_bildDaten));
            c->Unmap(g_cbBild, 0);
        }
    }
    c->OMSetRenderTargets(1, &g_zielRtv, g_tiefeDsv);
    D3D11_VIEWPORT vp{};
    vp.Width = static_cast<float>(g_zielW);
    vp.Height = static_cast<float>(g_zielH);
    vp.MaxDepth = 1.0F;
    c->RSSetViewports(1, &vp);
    const UINT schritt = sizeof(KlingenEcke);
    const UINT versatz = 0;
    c->IASetVertexBuffers(0, 1, &g_klingeVb, &schritt, &versatz);
    c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    c->IASetInputLayout(sp->layout);
    c->VSSetShader(sp->vs, nullptr, 0);
    c->PSSetShader(sp->ps, nullptr, 0);
    c->VSSetConstantBuffers(0, 1, &g_cbBild);
    c->PSSetSamplers(0, 1, (g_samplerRand != nullptr) ? &g_samplerRand : &g_sampler);
    // Additiv (blendFunc GL_ONE GL_ONE in shaders/sabers.shader), die Tiefe
    // TESTEN, nicht schreiben - ein Leuchten verdeckt nichts.
    ID3D11BlendState* bs = holeBlend(Blend::Add);
    if (bs != nullptr) {
        const float faktor[4] = {0.0F, 0.0F, 0.0F, 0.0F};
        c->OMSetBlendState(bs, faktor, 0xFFFFFFFFU);
    }
    PipelineState p;
    p.blend = Blend::Add;
    p.depthWrite = false;
    ID3D11DepthStencilState* ds = holeTiefe(p);
    if (ds != nullptr) { c->OMSetDepthStencilState(ds, 0); }
    ID3D11RasterizerState* rs = holeRaster(kCullNone);
    if (rs != nullptr) { c->RSSetState(rs); }
    int n = 0;
    for (std::size_t i = 0; i < quads.size(); ++i) {
        ID3D11ShaderResourceView* bild = holeTexBild(quads[i].tex);
        c->PSSetShaderResources(0, 1, &bild);
        c->Draw(6, static_cast<UINT>(i * 6U));
        ++n;
    }
    ID3D11ShaderResourceView* nichts = nullptr;
    c->PSSetShaderResources(0, 1, &nichts);
    return n;
}

// --- Das Modellfenster ---------------------------------------------------
bool bereiteModellZiel(int breite, int hoehe) {
    ID3D11Device* d = dev();
    if (d == nullptr || breite <= 0 || hoehe <= 0) {
        return false;
    }
    if (g_modellRtv != nullptr && g_modellW == breite && g_modellH == hoehe) {
        return true;
    }
    if (g_modellSrv != nullptr) { g_modellSrv->Release(); g_modellSrv = nullptr; }
    if (g_modellRtv != nullptr) { g_modellRtv->Release(); g_modellRtv = nullptr; }
    if (g_modellTex != nullptr) { g_modellTex->Release(); g_modellTex = nullptr; }
    if (g_modellDsv != nullptr) { g_modellDsv->Release(); g_modellDsv = nullptr; }
    if (g_modellTiefeTex != nullptr) { g_modellTiefeTex->Release(); g_modellTiefeTex = nullptr; }
    D3D11_TEXTURE2D_DESC td{};
    td.Width = static_cast<UINT>(breite);
    td.Height = static_cast<UINT>(hoehe);
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    if (FAILED(d->CreateTexture2D(&td, nullptr, &g_modellTex)) ||
        FAILED(d->CreateRenderTargetView(g_modellTex, nullptr, &g_modellRtv)) ||
        FAILED(d->CreateShaderResourceView(g_modellTex, nullptr, &g_modellSrv))) {
        gibModellFrei();
        return false;
    }
    D3D11_TEXTURE2D_DESC dd = td;
    dd.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    dd.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    if (FAILED(d->CreateTexture2D(&dd, nullptr, &g_modellTiefeTex)) ||
        FAILED(d->CreateDepthStencilView(g_modellTiefeTex, nullptr, &g_modellDsv))) {
        gibModellFrei();
        return false;
    }
    g_modellW = breite;
    g_modellH = hoehe;
    return true;
}

void* modellZielTextur() { return g_modellSrv; }

int zeichneModellAnsicht(const GlmModel& model, const ModelTextures* textures,
                         const std::vector<BoneMatrix>& knochen,
                         const float* viewProj, bool kappen, std::string* fehler) {
    ID3D11Device* d = dev();
    ID3D11DeviceContext* c = ctx();
    if (d == nullptr || c == nullptr || g_modellRtv == nullptr || g_modellDsv == nullptr ||
        viewProj == nullptr) {
        if (fehler != nullptr) { *fehler = "Modellfenster: kein Ziel"; }
        return 0;
    }
    setzeSchritt("Modellfenster");
    // Die Grundausstattung, falls noch keine Karte gezeichnet wurde: die
    // beiden Konstantenpuffer und der Sampler entstehen sonst erst dort.
    auto legeCb = [&](ID3D11Buffer** ziel, UINT bytes) {
        if (*ziel != nullptr) { return true; }
        D3D11_BUFFER_DESC bd{};
        bd.ByteWidth = (bytes + 15U) & ~15U;
        bd.Usage = D3D11_USAGE_DYNAMIC;
        bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        return SUCCEEDED(d->CreateBuffer(&bd, nullptr, ziel));
    };
    if (!legeCb(&g_cbBild, 40U * 4U) ||
        !legeCb(&g_cbStapel, static_cast<UINT>(kKonstFloats) * 4U)) {
        if (fehler != nullptr) { *fehler = "Konstantenpuffer nicht anlegbar"; }
        return 0;
    }
    if (g_sampler == nullptr) {
        D3D11_SAMPLER_DESC sd{};
        sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sd.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
        sd.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
        sd.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
        sd.ComparisonFunc = D3D11_COMPARISON_ALWAYS;
        sd.MaxLOD = D3D11_FLOAT32_MAX;
        d->CreateSamplerState(&sd, &g_sampler);
    }
    if (!g_bildDatenDa) {
        for (float& f : g_bildDaten) { f = 0.0F; }
        g_bildDaten[20] = 1.0F;   // gHelligkeit - fuer kRohEcke ohne Belang
        g_bildDatenDa = true;
    }
    c->OMSetRenderTargets(1, &g_modellRtv, g_modellDsv);
    // Auch das Bild loeschen, mit Alpha 1: die Mischzustaende schreiben nur
    // RGB (sonst schien bei den Hologrammen der Fensterhintergrund durch),
    // und ein nie geloeschtes Ziel bliebe mit Alpha 0 unsichtbar - genau so
    // war es beim ersten Versuch, das Modellfenster blieb leer.
    {
        const float grau[4] = {45.0F / 255.0F, 45.0F / 255.0F, 45.0F / 255.0F, 1.0F};
        c->ClearRenderTargetView(g_modellRtv, grau);
    }
    c->ClearDepthStencilView(g_modellDsv, D3D11_CLEAR_DEPTH, 1.0F, 0);
    D3D11_VIEWPORT vp{};
    vp.Width = static_cast<float>(g_modellW);
    vp.Height = static_cast<float>(g_modellH);
    vp.MaxDepth = 1.0F;
    c->RSSetViewports(1, &vp);
    // --- Der Hintergrund ---------------------------------------------------
    if (g_modellHgVs == nullptr && g_modellHgPs == nullptr && !g_modellHgVersucht) {
        g_modellHgVersucht = true;
        ID3DBlob* vs = uebersetze(vollbildVertexShaderHlsl(), "vs_4_0", "modellhg", fehler);
        ID3DBlob* ps = uebersetze(modellHintergrundPixelShaderHlsl(), "ps_4_0", "modellhg", fehler);
        if (vs != nullptr && ps != nullptr) {
            d->CreateVertexShader(vs->GetBufferPointer(), vs->GetBufferSize(), nullptr,
                                  &g_modellHgVs);
            d->CreatePixelShader(ps->GetBufferPointer(), ps->GetBufferSize(), nullptr,
                                 &g_modellHgPs);
        }
        if (vs != nullptr) { vs->Release(); }
        if (ps != nullptr) { ps->Release(); }
    }
    if (g_modellHgVs != nullptr && g_modellHgPs != nullptr) {
        c->IASetInputLayout(nullptr);
        c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        c->VSSetShader(g_modellHgVs, nullptr, 0);
        c->PSSetShader(g_modellHgPs, nullptr, 0);
        ID3D11BlendState* bs = holeBlend(Blend::Opaque);
        if (bs != nullptr) {
            const float faktor[4] = {0.0F, 0.0F, 0.0F, 0.0F};
            c->OMSetBlendState(bs, faktor, 0xFFFFFFFFU);
        }
        PipelineState p;
        p.depthWrite = false;
        p.depthTestOff = true;
        ID3D11DepthStencilState* ds = holeTiefe(p);
        if (ds != nullptr) { c->OMSetDepthStencilState(ds, 0); }
        ID3D11RasterizerState* rs = holeRaster(kCullNone);
        if (rs != nullptr) { c->RSSetState(rs); }
        c->Draw(3, 0);
    }
    // Die Tiefe ERST JETZT loeschen: ein deckender Zustand schreibt immer
    // Tiefe (holeTiefe, wie die Engine), und das Vollbilddreieck liegt bei
    // z = 0. Vorher geloescht, stand danach ueberall 0 in der Tiefe, und
    // die Figur fiel an jedem Bildpunkt durch den Tiefentest - das
    // Modellfenster blieb leer.
    c->ClearDepthStencilView(g_modellDsv, D3D11_CLEAR_DEPTH, 1.0F, 0);
    // --- Das Modell, am Ursprung ------------------------------------------
    const float einheit[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    return zeichneFigurIn(model, textures, knochen, einheit, viewProj, fehler, nullptr,
                          kappen, true);
}

int veralteteKartenBilder() { return g_veraltet; }

SpeicherStand speicherStand() {
    SpeicherStand s;
    s.bilder = static_cast<int>(g_bilder.size());
    s.lightmaps = static_cast<int>(g_lightmaps.size());
    s.moverNetze = static_cast<int>(g_moverNetze.size());
    s.figuren = static_cast<int>(g_figuren.size());
    s.figurBilder = static_cast<int>(g_figurBilder.size());
    // Belegter Grafikspeicher ueber DXGI 1.4 (ab Windows 10).
    if (ID3D11Device* d = dev()) {
        IDXGIDevice* dx = nullptr;
        if (SUCCEEDED(d->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void**>(&dx))) && dx != nullptr) {
            IDXGIAdapter* ad = nullptr;
            if (SUCCEEDED(dx->GetAdapter(&ad)) && ad != nullptr) {
                IDXGIAdapter3* ad3 = nullptr;
                if (SUCCEEDED(ad->QueryInterface(__uuidof(IDXGIAdapter3), reinterpret_cast<void**>(&ad3))) &&
                    ad3 != nullptr) {
                    DXGI_QUERY_VIDEO_MEMORY_INFO mi{};
                    if (SUCCEEDED(ad3->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &mi))) {
                        s.grafikBytes = static_cast<long long>(mi.CurrentUsage);
                    }
                    ad3->Release();
                }
                ad->Release();
            }
            dx->Release();
        }
    }
    PROCESS_MEMORY_COUNTERS_EX pm{};
    if (K32GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pm),
                                sizeof(pm)) != 0) {
        s.prozessBytes = static_cast<long long>(pm.PrivateUsage);
    }
    return s;
}

void vergissNetz(const BspMesh* netz) {
    const auto it = g_moverNetze.find(netz);
    if (it == g_moverNetze.end()) {
        return;
    }
    if (it->second.vb != nullptr) { it->second.vb->Release(); }
    if (it->second.ib != nullptr) { it->second.ib->Release(); }
    g_moverNetze.erase(it);
}

void vergissMoverNetze() {
    // Schluessel ist die Adresse des Netzes in brushMeshes. Wird das geleert
    // (Kartenwechsel, Detail, Himmel an/aus), blieben die Puffer bis zum
    // Beenden liegen - mit jeder Karte mehr Grafikspeicher.
    for (auto& [k, n] : g_moverNetze) {
        if (n.vb != nullptr) { n.vb->Release(); }
        if (n.ib != nullptr) { n.ib->Release(); }
    }
    g_moverNetze.clear();
}

void vergissKarte() {
    // Kartenwechsel (Kartenpruefung 27.09., shank: "es waren einfach alle
    // Texturen oder UV-Maps falsch, von jeder Map"): beide Speicher waren
    // nach Nummer abgelegt und wurden nur beim Beenden geleert. Die zweite
    // Karte zeichnete also mit den Bildern der ersten - Nummer 5 der neuen
    // Karte mit Bild 5 der alten, samt deren Lightmaps.
    for (auto& [k, s2] : g_bilder) { if (s2 != nullptr) { s2->Release(); } }
    g_bilder.clear();
    g_bildQuelle.clear();
    for (auto& [k, s2] : g_lightmaps) { if (s2 != nullptr) { s2->Release(); } }
    g_lightmaps.clear();
    g_lightmapQuelle.clear();
    if (g_himmel != nullptr) { g_himmel->Release(); g_himmel = nullptr; }
    g_himmelQuelle.clear();
    vergissMoverNetze();
}

void shutdown() {
    gibKlingenFrei();
    gibModellFrei();
    gibLichtFrei();
    // Dieselben Freigaben noch einmal ausgeschrieben, damit lint_d3dbesitz
    // sie sieht (gibKlingenFrei/gibModellFrei haben sie schon genullt).
    if (g_klingeVb != nullptr) { g_klingeVb->Release(); g_klingeVb = nullptr; }
    if (g_ersatzBild != nullptr) { g_ersatzBild->Release(); g_ersatzBild = nullptr; }
    if (g_modellTex != nullptr) { g_modellTex->Release(); g_modellTex = nullptr; }
    if (g_modellRtv != nullptr) { g_modellRtv->Release(); g_modellRtv = nullptr; }
    if (g_modellSrv != nullptr) { g_modellSrv->Release(); g_modellSrv = nullptr; }
    if (g_modellTiefeTex != nullptr) { g_modellTiefeTex->Release(); g_modellTiefeTex = nullptr; }
    if (g_modellDsv != nullptr) { g_modellDsv->Release(); g_modellDsv = nullptr; }
    if (g_modellHgVs != nullptr) { g_modellHgVs->Release(); g_modellHgVs = nullptr; }
    if (g_modellHgPs != nullptr) { g_modellHgPs->Release(); g_modellHgPs = nullptr; }
    for (auto& [name, p] : g_shader) {
        if (p.vs != nullptr) { p.vs->Release(); }
        if (p.ps != nullptr) { p.ps->Release(); }
        if (p.layout != nullptr) { p.layout->Release(); }
    }
    g_shader.clear();
    for (auto& [k, s] : g_blend) { if (s != nullptr) { s->Release(); } }
    g_blend.clear();
    for (auto& [k, s] : g_depth) { if (s != nullptr) { s->Release(); } }
    g_depth.clear();
    for (auto& [k, s] : g_raster) { if (s != nullptr) { s->Release(); } }
    g_raster.clear();
    if (g_cbBild != nullptr) { g_cbBild->Release(); g_cbBild = nullptr; }
    if (g_cbStapel != nullptr) { g_cbStapel->Release(); g_cbStapel = nullptr; }
    if (g_sampler != nullptr) { g_sampler->Release(); g_sampler = nullptr; }
    if (g_samplerRand != nullptr) {
        g_samplerRand->Release();
        g_samplerRand = nullptr;
    }
    vergissKarte();
    for (auto& [k, s2] : g_bilder) { if (s2 != nullptr) { s2->Release(); } }
    g_bilder.clear();
    for (auto& [k, s2] : g_lightmaps) { if (s2 != nullptr) { s2->Release(); } }
    g_lightmaps.clear();
    for (auto& [k, fp] : g_figuren) {
        if (fp.vb != nullptr) { fp.vb->Release(); }
        if (fp.ib != nullptr) { fp.ib->Release(); }
    }
    g_figuren.clear();
    for (auto& [k, s2] : g_figurBilder) { if (s2 != nullptr) { s2->Release(); } }
    g_figurBilder.clear();
    g_figurBildDaten.clear();
    if (g_cbFigur != nullptr) { g_cbFigur->Release(); g_cbFigur = nullptr; }
    if (g_figurShader.vs != nullptr) { g_figurShader.vs->Release(); }
    if (g_figurShader.ps != nullptr) { g_figurShader.ps->Release(); }
    if (g_figurShader.layout != nullptr) { g_figurShader.layout->Release(); }
    g_figurShader = ShaderPaar{};
    g_figurShaderVersucht = false;
    if (g_glowVollSrv != nullptr) { g_glowVollSrv->Release(); g_glowVollSrv = nullptr; }
    if (g_glowVollRtv != nullptr) { g_glowVollRtv->Release(); g_glowVollRtv = nullptr; }
    if (g_glowVollTex != nullptr) { g_glowVollTex->Release(); g_glowVollTex = nullptr; }
    for (int i = 0; i < 2; ++i) {
        if (g_glowSrv[i] != nullptr) { g_glowSrv[i]->Release(); g_glowSrv[i] = nullptr; }
        if (g_glowRtv[i] != nullptr) { g_glowRtv[i]->Release(); g_glowRtv[i] = nullptr; }
        if (g_glowTex[i] != nullptr) { g_glowTex[i]->Release(); g_glowTex[i] = nullptr; }
    }
    if (g_cbGlow != nullptr) { g_cbGlow->Release(); g_cbGlow = nullptr; }
    if (g_vollbildVs != nullptr) { g_vollbildVs->Release(); g_vollbildVs = nullptr; }
    if (g_glowShrinkPs != nullptr) { g_glowShrinkPs->Release(); g_glowShrinkPs = nullptr; }
    if (g_glowBlurPs != nullptr) { g_glowBlurPs->Release(); g_glowBlurPs = nullptr; }
    if (g_glowCompPs != nullptr) { g_glowCompPs->Release(); g_glowCompPs = nullptr; }
    g_glowShaderVersucht = false;
    g_glowW = 0;
    g_glowH = 0;
    g_glowVollW = 0;
    g_glowVollH = 0;
    g_bildDatenDa = false;
    // Die Abfragen gehoeren dem Geraet. Bleiben sie beim Aufraeumen stehen,
    // meldet die Debugschicht beim Beenden lebende Objekte - und beim
    // naechsten Geraet zeigten sie auf ein totes.
    gibMessungenFrei();
    if (g_himmel != nullptr) { g_himmel->Release(); g_himmel = nullptr; }
    g_himmelQuelle.clear();
    if (g_zielSrv != nullptr) { g_zielSrv->Release(); g_zielSrv = nullptr; }
    if (g_zielRtv != nullptr) { g_zielRtv->Release(); g_zielRtv = nullptr; }
    if (g_zielTex != nullptr) { g_zielTex->Release(); g_zielTex = nullptr; }
    if (g_tiefeDsv != nullptr) { g_tiefeDsv->Release(); g_tiefeDsv = nullptr; }
    if (g_tiefeTex != nullptr) { g_tiefeTex->Release(); g_tiefeTex = nullptr; }
    if (g_tiefeLesen != nullptr) { g_tiefeLesen->Release(); g_tiefeLesen = nullptr; }
    g_zielW = 0;
    g_zielH = 0;
    for (Netzpuffer& n : g_netz) {
        if (n.vb != nullptr) { n.vb->Release(); n.vb = nullptr; }
        if (n.ib != nullptr) { n.ib->Release(); n.ib = nullptr; }
        n.vbGroesse = 0;
        n.ibGroesse = 0;
    }
    for (auto& [k, n] : g_moverNetze) {
        if (n.vb != nullptr) { n.vb->Release(); }
        if (n.ib != nullptr) { n.ib->Release(); }
    }
    g_moverNetze.clear();
}

const char* abschnittName(Abschnitt a) {
    switch (a) {
        case Abschnitt::KarteDeckend:    return "Karte deckend";
        case Abschnitt::KarteGemischt:   return "Karte gemischt";
        case Abschnitt::EffekteDeckend:  return "Effekte deckend";
        case Abschnitt::EffekteGemischt: return "Effekte gemischt";
        case Abschnitt::MoverDeckend:    return "Mover deckend";
        case Abschnitt::MoverGemischt:   return "Mover gemischt";
        case Abschnitt::Figuren:         return "Figuren";
        case Abschnitt::Gluehen:         return "Gluehen";
        case Abschnitt::kAnzahl: break;
    }
    return "?";
}

// --- Die Warteschlange der Debugschicht leeren -------------------------
//
// Die Schicht laeuft seit rc425 immer mit. Sie schreibt ihre Beanstandungen
// an den Windows-Debugger - und wer das Programm normal startet, sieht
// nichts davon. Jede Meldung seit rc425 war damit unsichtbar, obwohl sie
// erzeugt wurde.
//
// `ID3D11InfoQueue` ist die Warteschlange dahinter. Sie haengt am GERAET,
// nicht am Kontext, und ist nur da, wenn die Schicht laeuft.
std::vector<std::string> debugMeldungen() {
    std::vector<std::string> aus;
    ID3D11Device* d = dev();
    if (d == nullptr) {
        return aus;
    }
    static ID3D11InfoQueue* q = nullptr;
    static ID3D11Device* fuer = nullptr;
    if (fuer != d) {
        // Beim Geraetewechsel neu holen - die alte Warteschlange gehoerte
        // dem alten Geraet.
        if (q != nullptr) { q->Release(); q = nullptr; }
        fuer = d;
        if (FAILED(d->QueryInterface(__uuidof(ID3D11InfoQueue),
                                     reinterpret_cast<void**>(&q)))) {
            q = nullptr;
        }
    }
    if (q == nullptr) {
        return aus;   // ohne Debugschicht gestartet
    }

    // --- Auf Beanstandungen anhalten -----------------------------------
    //
    // `SetBreakOnSeverity` haelt das Programm an der Stelle an, an der
    // Direct3D etwas beanstandet - statt es hinterher im Protokoll zu
    // suchen. Der Aufrufstapel im Debugger sagt dann unmittelbar, welcher
    // Zeichenaufruf es war.
    //
    // NUR auf Anforderung: ohne angehaengten Debugger ist ein Haltepunkt
    // ein Absturz. `BHED_D3D_BREAK=1` setzt ihn, und dann auch nur fuer
    // FEHLER und SCHWER - Warnungen kommen zu haeufig, um bei jeder
    // anzuhalten.
    static bool halteGesetzt = false;
    if (!halteGesetzt) {
        halteGesetzt = true;
        const char* e = std::getenv("BHED_D3D_BREAK");
        if (e != nullptr && e[0] == '1') {
            q->SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_CORRUPTION, TRUE);
            q->SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_ERROR, TRUE);
        }
    }

    // Gleiche Meldungen nur EINMAL.
    //
    // Die Schicht wiederholt dieselbe Beanstandung in jedem Bild. Bei 300
    // Bildern je Sekunde stuenden nach einer Minute achtzehntausend gleiche
    // Zeilen im Protokoll, und die eine ANDERE darunter faende niemand.
    static std::map<std::string, int> gesehen;

    const UINT64 n = q->GetNumStoredMessages();
    for (UINT64 i = 0; i < n; ++i) {
        SIZE_T laenge = 0;
        if (FAILED(q->GetMessage(i, nullptr, &laenge)) || laenge == 0) {
            continue;
        }
        std::vector<char> puffer(laenge);
        auto* m = reinterpret_cast<D3D11_MESSAGE*>(puffer.data());
        if (FAILED(q->GetMessage(i, m, &laenge))) {
            continue;
        }
        const char* stufe = "?";
        switch (m->Severity) {
            case D3D11_MESSAGE_SEVERITY_CORRUPTION: stufe = "SCHWER"; break;
            case D3D11_MESSAGE_SEVERITY_ERROR:      stufe = "FEHLER"; break;
            case D3D11_MESSAGE_SEVERITY_WARNING:    stufe = "WARNUNG"; break;
            case D3D11_MESSAGE_SEVERITY_INFO:       stufe = "Hinweis"; break;
            case D3D11_MESSAGE_SEVERITY_MESSAGE:    stufe = "Meldung"; break;
            default: break;
        }
        std::string text(m->pDescription,
                         m->pDescription + m->DescriptionByteLength);
        while (!text.empty() &&
               (text.back() == '\0' || text.back() == '\n')) {
            text.pop_back();
        }
        // --- WO ist das passiert? ------------------------------------
        //
        // Die Meldung der Debugschicht nennt den Aufruf, aber nicht den
        // Zusammenhang. "Index buffer has not enough space" sagt nichts
        // darueber, ob es die Karte, ein Mover, eine Figur oder der
        // Gluehdurchgang war - und die vier haben verschiedene Puffer.
        //
        // `setzeSchritt` steht seit rc4xx im ganzen Zeichenweg und wurde
        // bisher nur im Absturzbericht benutzt. Hier kostet er nichts und
        // beantwortet genau die Frage, die sonst eine Runde kostet.
        const char* wo = letzterSchritt();
        const std::string zeile = std::string(stufe) + ": " + text +
                                  ((wo != nullptr && wo[0] != '\0')
                                       ? ("   [waehrend: " + std::string(wo) +
                                          "]")
                                       : std::string());
        auto& zahl = gesehen[zeile];
        ++zahl;
        if (zahl == 1) {
            aus.push_back(zeile);
        } else if (zahl == 100) {
            aus.push_back("(die vorige Meldung kam hundertmal - weitere "
                          "werden nicht mehr genannt)");
        }
    }
    q->ClearStoredMessages();
    return aus;
}

std::string geraetInfo() {
    ID3D11Device* d = dev();
    if (d == nullptr) {
        return "kein Direct3D-Geraet";
    }
    std::string aus;
    // Geraet -> IDXGIDevice -> Adapter.
    IDXGIDevice* dxdev = nullptr;
    if (SUCCEEDED(d->QueryInterface(__uuidof(IDXGIDevice),
                                    reinterpret_cast<void**>(&dxdev))) &&
        dxdev != nullptr) {
        IDXGIAdapter* ad = nullptr;
        if (SUCCEEDED(dxdev->GetAdapter(&ad)) && ad != nullptr) {
            DXGI_ADAPTER_DESC beschr{};
            if (SUCCEEDED(ad->GetDesc(&beschr))) {
                for (const WCHAR* p = beschr.Description; *p != 0; ++p) {
                    // Die Namen sind reines ASCII; alles andere wird ein
                    // Fragezeichen, statt das Protokoll zu verstuemmeln.
                    aus += (*p < 128) ? static_cast<char>(*p) : '?';
                }
                aus += ", " +
                       std::to_string(beschr.DedicatedVideoMemory /
                                      (1024ULL * 1024ULL)) +
                       " MB eigener Speicher";
            }
            ad->Release();
        }
        dxdev->Release();
    }
    if (aus.empty()) {
        aus = "Adapter nicht lesbar";
    }
    const int stufe = static_cast<int>(d->GetFeatureLevel());
    char merk[48];
    std::snprintf(merk, sizeof(merk), ", Merkmalsstufe %d.%d",
                  (stufe >> 12) & 0xF, (stufe >> 8) & 0xF);
    aus += merk;
    // Laeuft die Debugschicht wirklich? Das ist die Frage, die man sonst
    // nicht beantworten kann, wenn keine Meldung kommt.
    ID3D11InfoQueue* q = nullptr;
    const bool schicht =
        SUCCEEDED(d->QueryInterface(__uuidof(ID3D11InfoQueue),
                                    reinterpret_cast<void**>(&q))) &&
        q != nullptr;
    if (q != nullptr) { q->Release(); }
    aus += schicht ? ", Debugschicht LAEUFT" : ", Debugschicht aus";
    // Die Schalter mit ausgeben. Sonst steht im Bericht ein Shader ohne
    // Debugangaben, und niemand weiss, ob der Schalter vergessen wurde
    // oder nicht wirkt.
    if (uebersetzFahnen() != 0U) {
        aus += ", Shader MIT Debugangaben (BHED_D3D_SHADERDEBUG)";
    }
    const char* halt = std::getenv("BHED_D3D_BREAK");
    if (halt != nullptr && halt[0] == '1') {
        aus += ", haelt bei FEHLER an (BHED_D3D_BREAK)";
    }
    return aus;
}

std::vector<StapelNotiz> auffaelligeStapel() {
    std::vector<StapelNotiz> aus;
    aus.reserve(g_auffaellig.size());
    for (const auto& [schluessel, n] : g_auffaellig) {
        const std::size_t trenn = schluessel.rfind("  ");
        StapelNotiz notiz;
        notiz.shader = (trenn != std::string::npos)
                           ? schluessel.substr(0, trenn)
                           : schluessel;
        notiz.was = (trenn != std::string::npos)
                        ? schluessel.substr(trenn + 2)
                        : std::string();
        notiz.anzahl = n;
        aus.push_back(notiz);
    }
    return aus;
}

std::string berichtText() {
    std::string s;
    s += "Figurenflaechen ohne Bild: " +
         std::to_string(g_figurFlaechenOhneBild) + "\n";
    s += "Geraet   : " + geraetInfo() + "\n";
    // `zustandsLage()` bringt die Beschriftung schon mit - hier stand
    // "Zustaende: " noch einmal davor, und im Bericht las man
    // "Zustaende: Zustaende: Tiefe und Keulen".
    s += std::string(zustandsLage()) + "\n";
    s += "Shader   : " + std::to_string(g_shader.size()) + " uebersetzt\n";
    for (const auto& [name, unbenutzt] : g_shader) {
        (void)unbenutzt;
        s += "           " + name + "\n";
    }
    s += "Zeiten der Grafikkarte (ms, -1 = keine Messung):\n";
    for (int i = 0; i < kAbschnitte; ++i) {
        const auto a = static_cast<Abschnitt>(i);
        char z[96];
        std::snprintf(z, sizeof(z), "           %-18s %7.3f\n",
                      abschnittName(a),
                      static_cast<double>(millisekunden(a)));
        s += z;
    }
    return s;
}

const char* zustandsLage() {
    switch (zustandsSchalter()) {
        case 0: return "Zustaende: keine (wie vor rc434)";
        case 1: return "Zustaende: nur Tiefe";
        case 2: return "Zustaende: nur Keulen";
        default: break;
    }
    return "Zustaende: Tiefe und Keulen";
}

float millisekundenBeide(Abschnitt a, Abschnitt b) {
    const float x = millisekunden(a);
    const float y = millisekunden(b);
    if (x < 0.0F && y < 0.0F) { return -1.0F; }
    return (x < 0.0F ? 0.0F : x) + (y < 0.0F ? 0.0F : y);
}

void bildBeginnt() {
    if (!messungBereit()) { return; }
    ID3D11DeviceContext* c = ctx();
    if (c == nullptr) { return; }
    // Erst abholen, DANN den neuen Satz beginnen: der abzuholende ist der
    // aelteste, und der wird gleich ueberschrieben.
    holeMessung();
    g_messJetzt = (g_messJetzt + 1) % kMessTiefe;
    MessSatz& m = g_mess[g_messJetzt];
    // Ein Satz, dessen Ergebnis noch aussteht, wird NICHT neu begonnen:
    // End auf eine Abfrage, deren Werte nie abgeholt wurden, meldet die
    // D3D-Debugschicht ("End is being invoked on a Query, where the previous
    // results have not been obtained" - im Szenenlauf 28.09., wenn Bilder
    // schneller kommen als die Grafikkarte antwortet). Dieses Bild bleibt
    // dann ohne Messwert; lieber das als ein Wartepunkt.
    if (m.abzuholen) {
        m.offen = false;
        return;
    }
    for (int i = 0; i < kAbschnitte; ++i) {
        m.vollstaendig[i] = false;
        m.begonnen[i] = false;
    }
    m.abzuholen = false;
    c->Begin(m.disjoint);
    m.offen = true;
}

void bildFertig() {
    if (!g_messGeht) { return; }
    ID3D11DeviceContext* c = ctx();
    if (c == nullptr) { return; }
    MessSatz& m = g_mess[g_messJetzt];
    if (!m.offen) { return; }
    c->End(m.disjoint);
    m.offen = false;
    m.abzuholen = true;
}

void beginneMessung(Abschnitt a) {
    if (!g_messGeht) { return; }
    const int i = static_cast<int>(a);
    if (i < 0 || i >= kAbschnitte) { return; }
    ID3D11DeviceContext* c = ctx();
    MessSatz& m = g_mess[g_messJetzt];
    // Ausserhalb der Bildklammer nicht messen - sonst stuende eine Marke
    // in einem Satz, dessen Frequenzabfrage nie laeuft, und GetData wartet
    // ewig darauf.
    if (c == nullptr || !m.offen || m.begonnen[i]) { return; }
    c->End(m.anfang[i]);
    m.begonnen[i] = true;
}

void beendeMessung(Abschnitt a) {
    if (!g_messGeht) { return; }
    const int i = static_cast<int>(a);
    if (i < 0 || i >= kAbschnitte) { return; }
    ID3D11DeviceContext* c = ctx();
    MessSatz& m = g_mess[g_messJetzt];
    if (c == nullptr || !m.offen || !m.begonnen[i]) { return; }
    c->End(m.ende[i]);
    m.vollstaendig[i] = true;
}

float millisekunden(Abschnitt a);

float millisekunden(Abschnitt a) {
    const int i = static_cast<int>(a);
    if (i < 0 || i >= kAbschnitte) { return -1.0F; }
    return g_messWert[i];
}

bool verfuegbar() {
    return dev() != nullptr && ctx() != nullptr;
}

int zeichneKarte(const BspMesh& mesh, const TextureSet* textures,
                 const BspGeometry* geo, const float* viewProj,
                 const float* kameraPos, float zeitSekunden,
                 float helligkeit, float grundlicht, int netzId,
                 const float* vorn, const float* rechts, const float* hoch,
                 float fokus, std::string* fehler, int* uebersprungenAus,
                 Lage lage) {
    // Die Marke ganz nach vorn: die Pruefschleife weiter unten laeuft ueber
    // die Stapel des Netzes, und wenn DIE abstuerzt, soll der Bericht es
    // sagen.
    // Beim letzten Absturz stand sie hinter der Schleife, und die Zeile im
    // Protokoll fehlte deshalb - was mich auf die falsche Faehrte brachte.
    setzeSchritt("Karte: Netz pruefen");
    ID3D11Device* d = dev();
    ID3D11DeviceContext* c = ctx();
    if (d == nullptr || c == nullptr || viewProj == nullptr ||
        mesh.indexes.empty() || mesh.verts.empty()) {
        if (fehler != nullptr) { *fehler = "kein Geraet oder kein Netz"; }
        return 0;
    }
    // --- Ist das Netz ueberhaupt fertig? ---------------------------------
    //
    // Beim Laden einer Mission wird es aufgebaut, waehrend die Ansicht
    // weiterlaeuft. Ein halbes Netz hat Stapel, die auf Ecken zeigen, die es
    // noch nicht gibt - und daraus wird ein Zugriff daneben.
    //
    // Der Rasterer hat das Problem nicht: er wird erst gerufen, wenn
    // `mapDirty` gesetzt ist, und das passiert nach dem Laden. Der GPU-Weg
    // steht an einer anderen Stelle im Ablauf und muss selbst nachsehen.
    for (const BspMesh::Batch& b : mesh.batches) {
        if (static_cast<std::size_t>(b.firstIndex) +
                static_cast<std::size_t>(b.numIndexes) > mesh.indexes.size()) {
            if (fehler != nullptr) { *fehler = "Netz noch unvollstaendig"; }
            return 0;
        }
    }

    // --- Die Ecken in die Form bringen, die der Shader erwartet ----------
    //
    // BspVertex hat eine andere Reihenfolge als VSIn (dort erst normal, dann
    // st). Umsortiert wird EINMAL beim Anlegen des Puffers, nicht je Bild -
    // und die Reihenfolge hier muss zum Eingabelayout in holeShader passen.
    struct Ecke {
        float pos[3];
        float normal[3];
        float uv[2];
        float uvLm[2];
        std::uint8_t farbe[4];
    };
    // Muss zum Eingabelayout passen (drei Stellen, eine Wahrheit).
    static_assert(sizeof(Ecke) == 44, "Eingabelayout erwartet 44 Byte je Ecke");

    // --- Die Groessen EINMAL lesen ---------------------------------------
    //
    // Sie standen dreimal einzeln da: fuer die Puffergroesse, fuer
    // `ecken.resize` und als Schleifengrenze. Waechst das Netz dazwischen -
    // und beim Laden einer Mission waechst es -, dann ist die Schleifengrenze
    // groesser als das Feld, und sie schreibt darueber hinaus.
    //
    // Gemeldet als:
    //
    //     ABSTURZ code 0xC0000005
    //       schreibend an Adresse 0x000000000007F804
    //       waehrend: Grafikschnittstelle starten
    //
    // Ein SCHREIBzugriff daneben, waehrend eine Mission geladen wurde und
    // der Schalter an war. Genau dieses Muster.
    //
    // Eine Kopie der Groesse loest das nicht vollstaendig - wenn der Vektor
    // umzieht, ist auch `.data()` von vorhin ungueltig. Deshalb steht unten
    // zusaetzlich eine Pruefung, dass ueberhaupt ein vollstaendiges Netz da
    // ist.
    setzeSchritt("Karte: Groessen lesen");
    const std::size_t nVerts = mesh.verts.size();
    const std::size_t nIdx = mesh.indexes.size();
    const std::size_t vbBytes = nVerts * sizeof(Ecke);
    const std::size_t ibBytes = nIdx * sizeof(std::uint32_t);

    // Neu anlegen, wenn sich die Groesse geaendert hat - also bei einem
    // Kartenwechsel. Innerhalb einer Karte aendert sich das Netz nicht.
    // netzId 2 heisst: ein Mover, Puffer nach Netz. Sonst Karte oder
    // Effektnetz.
    Netzpuffer& np = (netzId == 2) ? g_moverNetze[&mesh]
                                   : g_netz[(netzId == 1) ? 1 : 0];
    // Das Effektnetz aendert sich jedes Bild - DYNAMIC und ueberschreiben.
    // Die Karte aendert sich nur beim Kartenwechsel - IMMUTABLE.
    // Nur das Effektnetz ist dynamisch. Mover-Netze aendern sich nicht -
    // nur ihre Stellung, und die steht in der Matrix.
    const bool dyn = (netzId == 1);
    // --- Die Ecken VOR dem if -------------------------------------------
    //
    // Sie werden in beiden Zweigen gebraucht: beim Anlegen als Startinhalt,
    // beim Ueberschreiben als Quelle. Standen sie im ersten Zweig, war der
    // zweite nicht uebersetzbar - genau das hat shanks Bau gemeldet:
    //
    //     error C2065: "ecken": nichtdeklarierter Bezeichner
    //
    // Dieselbe Sorte Fehler wie das lokale `bs` in rc388. Dort hat ihn kein
    // Compiler gefunden, weil der Zeiger gueltig AUSSAH; hier bricht der Bau,
    // und das ist der bessere Fall.
    std::vector<Ecke> ecken;
    // --- Autosprite: welche Stapel? ----------------------------------------
    // Siehe baueAutosprites (gpudraw.cpp) und den Vertexshader.
    std::vector<char> istSprite(mesh.batches.size(), 0);
    std::size_t spriteZeichen = 0;
    if (textures != nullptr) {
        for (std::size_t b = 0; b < mesh.batches.size(); ++b) {
            if (batchStateFor(textures, mesh.batches[b].shader, 0.0F).autosprite) {
                istSprite[b] = 1;
                spriteZeichen += (b + 1U) * 7919U + 1U;
            }
        }
    }
    // Hat sich das Netz geaendert? Siehe Netzpuffer::quelleVerts.
    const bool anderesNetz = (np.quelleVerts != mesh.verts.data() ||
                              np.quelleIdx != mesh.indexes.data() ||
                              np.nVerts != nVerts || np.nIdx != nIdx ||
                              np.spriteZeichen != spriteZeichen);
    // --- EINE Bedingung, aus der die andere folgt ----------------------
    //
    // Hier standen zwei Ausdruecke, die dasselbe entscheiden sollten:
    //
    //   brauchtEcken = vb==0 || vbGroesse != vbBytes || dynamisch != dyn || dyn
    //   vbZuKlein    = vb==0 || vbGroesse <  vbBytes || dynamisch != dyn
    //                  || (!dyn && anderesNetz)
    //
    // Der Fall, in dem sie auseinanderlaufen: ein FESTES Netz wird gegen ein
    // anderes GLEICHER GROESSE getauscht. Alle Mover teilen sich Netzplatz 2;
    // haben zwei gleich viele Ecken, ist `vbGroesse == vbBytes`, aber
    // `anderesNetz` steht. Dann ist `brauchtEcken` falsch - niemand baut die
    // Ecken - und `vbZuKlein` wahr - der Puffer wird neu angelegt, mit einer
    // LEEREN Eckenliste.
    //
    // Genau das stand auf shanks Bild zu rc437:
    //   "GPU: Ecken fehlen - die beiden Bedingungen sind auseinandergelaufen"
    //
    // Die Meldung aus rc425 hat also getan, wozu sie da war. Die Bedingungen
    // gleichzuhalten hat sie nicht geschafft - dafuer waren es zwei.
    //
    // Jetzt ist es EINE: `vbZuKlein` wird zuerst gerechnet, `brauchtEcken`
    // folgt daraus. Ecken braucht man genau dann, wenn der Puffer neu
    // angelegt (`vbZuKlein`) oder ueberschrieben wird (`dyn`).
    const std::size_t vbPlatz = ((vbBytes + 65535U) / 65536U) * 65536U;
    const bool vbZuKlein = (np.vb == nullptr || np.vbGroesse < vbBytes ||
                            np.dynamisch != dyn ||
                            // Ein festes Netz wird beim Wechsel NEU angelegt:
                            // es ist IMMUTABLE und laesst sich nicht
                            // ueberschreiben.
                            (!dyn && anderesNetz));
    const bool brauchtEcken = (vbZuKlein || dyn);
    if (brauchtEcken) {
        setzeSchritt("Karte: Ecken umsortieren");
        // --- Zeiger und Anzahl ZUSAMMEN festhalten ------------------------
        //
        // Gemeldet: Absturz mit der Marke "Karte: Ecken umsortieren",
        // LESEND an einer gueltig aussehenden Adresse - beim Drehen an der
        // Detailstufe und beim Bewegen der Zeitleiste. Beides baut das Netz
        // neu.
        //
        // `nVerts` wurde oben gelesen, `mesh.verts[i]` hier - dazwischen kann
        // der Vektor umgezogen sein, und dann zeigt der alte Speicher ins
        // Leere. Ein Zeiger, der zu einer anderen Anzahl gehoert, ist die
        // haeufigste Form dieses Fehlers.
        //
        // Beides zusammen holen und NICHT mehr auf `mesh` zugreifen.
        const BspVertex* quelle = mesh.verts.data();
        const std::size_t anzahl = std::min(nVerts, mesh.verts.size());
        if (quelle == nullptr || anzahl == 0) {
            if (fehler != nullptr) { *fehler = "Netz waehrend des Lesens veraendert"; }
            return 0;
        }
        ecken.resize(anzahl);
        for (std::size_t i = 0; i < anzahl; ++i) {
            const BspVertex& v = quelle[i];
            Ecke& e = ecken[i];
            for (int k = 0; k < 3; ++k) {
                e.pos[k] = v.xyz[k];
                e.normal[k] = v.normal[k];
            }
            e.uv[0] = v.st[0];
            e.uv[1] = v.st[1];
            e.uvLm[0] = v.lightmap[0];
            e.uvLm[1] = v.lightmap[1];
            for (int k = 0; k < 4; ++k) { e.farbe[k] = v.colour[k]; }
        }
    }
    // Autosprite-Vierecke umbauen - Ecken UND Wicklung, beides aus einem
    // Aufruf, damit sie zusammenpassen.
    std::vector<std::uint32_t> spriteIdx;
    const bool ibNeu = (np.ib == nullptr || np.ibGroesse < ibBytes ||
                        (!dyn && anderesNetz));
    if (spriteZeichen != 0U && (brauchtEcken || ibNeu || dyn)) {
        setzeSchritt("Karte: Autosprite");
        spriteIdx = mesh.indexes;
        const std::vector<SpriteEcke> sp = baueAutosprites(mesh, istSprite, spriteIdx);
        for (const SpriteEcke& s : sp) {
            if (s.ecke >= ecken.size()) { continue; }
            Ecke& e = ecken[s.ecke];
            for (int k = 0; k < 3; ++k) { e.pos[k] = s.mitte[k]; }
            e.normal[0] = s.links;
            e.normal[1] = s.hoch;
            e.normal[2] = s.radius;
            e.uv[0] = s.st[0];
            e.uv[1] = s.st[1];
        }
    }
    const std::uint32_t* idxDaten =
        spriteIdx.empty() ? mesh.indexes.data() : spriteIdx.data();
    // --- Nicht bei jeder Groessenaenderung neu anlegen -------------------
    //
    // Das Effektnetz aendert seine Groesse in JEDEM Bild. Mit einer Pruefung
    // auf Gleichheit wurde der Puffer bei 240 Bildern je Sekunde 240-mal
    // freigegeben und neu angelegt.
    //
    // Direct3D gibt einen Puffer nicht sofort frei - es wartet, bis die
    // Grafikkarte mit ihm fertig ist. Bei diesem Tempo staut sich das, bis
    // der Speicher voll ist. Gemeldet als: "ich kann die Map sehen und die
    // Kamera drehen, dann freezed es und crashed" - erst langsam, dann tot.
    //
    // Deshalb waechst der Puffer nur und schrumpft nie: passt der Inhalt
    // hinein, bleibt er. Aufgerundet auf 64 KiB, damit auch das Wachsen
    // selten ist.
    if (vbZuKlein) {
        if (np.vb != nullptr) { np.vb->Release(); np.vb = nullptr; }
        D3D11_BUFFER_DESC bd{};
        // Beim festen Netz genau passend, beim dynamischen mit Reserve.
        bd.ByteWidth = static_cast<UINT>(dyn ? vbPlatz : vbBytes);
        bd.Usage = dyn ? D3D11_USAGE_DYNAMIC : D3D11_USAGE_IMMUTABLE;
        bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        // Nicht `: 0U` - der eine Zweig waere ein Aufzaehlungswert, der
        // andere eine Zahl. GCC beanstandet das unter -Wextra, MSVC bei /W4.
        bd.CPUAccessFlags =
            dyn ? static_cast<UINT>(D3D11_CPU_ACCESS_WRITE) : 0U;
        D3D11_SUBRESOURCE_DATA sd{};
        sd.pSysMem = ecken.data();
        // Ein IMMUTABLE-Puffer OHNE Startinhalt ist nicht erlaubt - und genau
        // das passiert, wenn `ecken` leer blieb, weil `brauchtEcken` und
        // `vbZuKlein` auseinandergelaufen sind. Lieber hier melden.
        if (!dyn && ecken.empty()) {
            if (fehler != nullptr) {
                *fehler = "Ecken fehlen - die beiden Bedingungen sind "
                          "auseinandergelaufen";
            }
            return 0;
        }
        if (FAILED(d->CreateBuffer(&bd, dyn ? nullptr : &sd, &np.vb))) {
            if (fehler != nullptr) { *fehler = "Vertexpuffer nicht anlegbar"; }
            return 0;
        }
        np.vbGroesse = dyn ? vbPlatz : vbBytes;
        // Der Name landet in jeder Meldung der Debugschicht und in
        // RenderDoc. Ohne ihn heisst der Puffer dort `<unnamed>`.
        benenne(np.vb, netzId == 0   ? "Ecken: Karte"
                       : netzId == 1 ? "Ecken: Effekte"
                                     : "Ecken: Mover");
        np.dynamisch = dyn;
    } else if (dyn) {
        // Gleiche Groesse, neuer Inhalt: ueberschreiben statt neu anlegen.
        D3D11_MAPPED_SUBRESOURCE m{};
        if (SUCCEEDED(c->Map(np.vb, 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) {
            std::memcpy(m.pData, ecken.data(), vbBytes);
            c->Unmap(np.vb, 0);
        }
    }
    setzeSchritt("Karte: Indexpuffer");
    const std::size_t ibPlatz = ((ibBytes + 65535U) / 65536U) * 65536U;
    if (ibNeu) {
        // Eine Zeile, nicht zwei: hier standen zwei Freigaben desselben
        // Zeigers hintereinander, von denen die erste wirkungslos war.
        if (np.ib != nullptr) { np.ib->Release(); np.ib = nullptr; }
        D3D11_BUFFER_DESC bd{};
        bd.ByteWidth = static_cast<UINT>(dyn ? ibPlatz : ibBytes);
        bd.Usage = dyn ? D3D11_USAGE_DYNAMIC : D3D11_USAGE_IMMUTABLE;
        bd.BindFlags = D3D11_BIND_INDEX_BUFFER;
        // Nicht `: 0U` - der eine Zweig waere ein Aufzaehlungswert, der
        // andere eine Zahl. GCC beanstandet das unter -Wextra, MSVC bei /W4.
        bd.CPUAccessFlags =
            dyn ? static_cast<UINT>(D3D11_CPU_ACCESS_WRITE) : 0U;
        D3D11_SUBRESOURCE_DATA sd{};
        sd.pSysMem = idxDaten;
        if (FAILED(d->CreateBuffer(&bd, dyn ? nullptr : &sd, &np.ib))) {
            if (fehler != nullptr) { *fehler = "Indexpuffer nicht anlegbar"; }
            return 0;
        }
        np.ibGroesse = dyn ? ibPlatz : ibBytes;
        benenne(np.ib, netzId == 0   ? "Indizes: Karte"
                       : netzId == 1 ? "Indizes: Effekte"
                                     : "Indizes: Mover");
    }
    np.quelleVerts = mesh.verts.data();
    np.quelleIdx = mesh.indexes.data();
    np.nVerts = nVerts;
    np.nIdx = nIdx;
    np.spriteZeichen = spriteZeichen;
    if (dyn) {
        // Immer schreiben - auch direkt nach dem Anlegen, weil der
        // dynamische Puffer leer erzeugt wird.
        D3D11_MAPPED_SUBRESOURCE m{};
        if (SUCCEEDED(c->Map(np.ib, 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) {
            std::memcpy(m.pData, idxDaten, ibBytes);
            c->Unmap(np.ib, 0);
        }
    }

    // --- Die beiden Konstantenpuffer -------------------------------------
    //
    // D3D11 verlangt Vielfache von 16 Byte. Die Groessen stehen deshalb
    // aufgerundet da, nicht als sizeof.
    auto legeCb = [&](ID3D11Buffer** ziel, UINT bytes) {
        if (*ziel != nullptr) { return true; }
        D3D11_BUFFER_DESC bd{};
        bd.ByteWidth = (bytes + 15U) & ~15U;
        bd.Usage = D3D11_USAGE_DYNAMIC;
        bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        return SUCCEEDED(d->CreateBuffer(&bd, nullptr, ziel));
    };
    // JeBild: float4x4 + float3 + float = 20 float.
    // 40 float - siehe die Lagentabelle beim Fuellen. Nicht 38: die
    // Ausrichtungsluecken zaehlen mit.
    if (!legeCb(&g_cbBild, 40U * 4U) ||
        !legeCb(&g_cbStapel, static_cast<UINT>(kKonstFloats) * 4U)) {
        if (fehler != nullptr) { *fehler = "Konstantenpuffer nicht anlegbar"; }
        return 0;
    }
    if (g_sampler == nullptr) {
        D3D11_SAMPLER_DESC sd{};
        sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sd.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
        sd.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
        sd.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
        sd.ComparisonFunc = D3D11_COMPARISON_ALWAYS;
        sd.MaxLOD = D3D11_FLOAT32_MAX;
        d->CreateSamplerState(&sd, &g_sampler);
    }
    if (g_samplerRand == nullptr) {
        // Derselbe Filter, aber am Rand geklemmt statt umlaufend.
        D3D11_SAMPLER_DESC sd{};
        sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sd.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
        sd.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
        sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        sd.ComparisonFunc = D3D11_COMPARISON_ALWAYS;
        sd.MaxLOD = D3D11_FLOAT32_MAX;
        d->CreateSamplerState(&sd, &g_samplerRand);
    }

    // JeBild fuellen.
    setzeSchritt("Karte: Konstanten fuellen");
    {
        D3D11_MAPPED_SUBRESOURCE m{};
        if (SUCCEEDED(c->Map(g_cbBild, 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) {
            auto* f = static_cast<float*>(m.pData);
            std::memcpy(f, viewProj, 16U * sizeof(float));
            f[16] = (kameraPos != nullptr) ? kameraPos[0] : 0.0F;
            f[17] = (kameraPos != nullptr) ? kameraPos[1] : 0.0F;
            f[18] = (kameraPos != nullptr) ? kameraPos[2] : 0.0F;
            f[19] = zeitSekunden;
            f[20] = helligkeit;
            f[21] = grundlicht;
            // Die Groessen fuer den Himmel - dieselben wie die Handrechnung
            // des Rasterers. Die Fuellwerte dazwischen sind nicht Zierde:
            // HLSL laesst ein float3 keinen Viererblock ueberschreiten.
            // --- Die Lagen sind NICHT fortlaufend ----------------------
            //
            // HLSL laesst ein float3 keine 16-Byte-Grenze ueberschreiten.
            // `gVorn` stuende bei 26 - das waere 26,27,28 ueber die Grenze
            // bei 28 hinweg -, also schiebt HLSL es auf 28. Dasselbe fuer
            // die beiden anderen Achsen.
            //
            // Ich habe fortlaufend geschrieben. Die Folge war ein Himmel aus
            // radialen Strahlen: `d = gVorn + gRechts*a + gHoch*b` bekam
            // Muell fuer die Achsen, uebrig blieb ein reiner Bildschirm-
            // faecher - genau das Muster im Bild.
            //
            //   Feld            HLSL   vorher (falsch)
            //   gHalb             22     22
            //   gFokus            24     24
            //   gVorn             28     26
            //   gRechts           32     30
            //   gHoch             36     34
            f[22] = static_cast<float>(g_zielW) * 0.5F;   // halfW
            f[23] = static_cast<float>(g_zielH) * 0.5F;   // halfH
            f[24] = fokus;
            f[25] = 0.0F;
            f[26] = 0.0F;  f[27] = 0.0F;
            // --- Die Achsen duerfen fehlen -----------------------------
            //
            // `zeichneMover` uebergibt nullptr: ein Mover braucht keinen
            // Himmel, und die Achsen werden nur dafuer gelesen.
            //
            // Hier stand der Zugriff OHNE Pruefung. Gemeldet als:
            //
            //     ABSTURZ code 0xC0000005
            //       lesend an Adresse 0x0000000000000000
            //       GPU-Weg zuletzt: Karte: Indexpuffer
            //
            // Die Marke stand auf "Indexpuffer", weil danach keine mehr
            // kommt - das Fuellen der Konstanten hatte keine eigene.
            auto achse = [&](const float* a, int k) {
                return (a != nullptr) ? a[k] : 0.0F;
            };
            f[28] = achse(vorn, 0);   f[29] = achse(vorn, 1);
            f[30] = achse(vorn, 2);   f[31] = 0.0F;
            f[32] = achse(rechts, 0); f[33] = achse(rechts, 1);
            f[34] = achse(rechts, 2); f[35] = 0.0F;
            f[36] = achse(hoch, 0);   f[37] = achse(hoch, 1);
            f[38] = achse(hoch, 2);   f[39] = 0.0F;
            // Merken, damit zeichneFigur nur die Matrix austauschen kann.
            std::memcpy(g_bildDaten, f, sizeof(g_bildDaten));
            g_bildDatenDa = true;
            c->Unmap(g_cbBild, 0);
        }
    }

    // --- Die geprüfte Liste abarbeiten ------------------------------------
    setzeSchritt("Karte: Zeichenliste bauen");
    const std::vector<DrawCall> alleCalls =
        buildDrawCalls(mesh, textures, zeitSekunden);
    const std::vector<DrawCall> calls = nurLage(alleCalls, lage);
    if (calls.empty()) {
        // Eine LEERE LAGE ist kein Fehler.
        //
        // Ein Netz ganz ohne gemischte Flaechen - die meisten Mover - hat im
        // zweiten Durchgang nichts zu tun. Nur wenn die VOLLE Liste leer ist,
        // stimmt etwas nicht; sonst stuende bei jedem Bild "die Zeichenliste
        // ist leer" im Panel, und die Meldung waere wertlos.
        //
        // Auch ein MOVER ohne zeichenbare Flaeche ist keiner: ein Brush-
        // Modell nur aus fehlenden Bildern (textures/colors/black_nomarks,
        // nodraw) hat schlicht nichts zu zeigen. Die Meldung stand sonst im
        // Panel, obwohl die Karte vollstaendig gezeichnet war
        // (Kartenpruefung 27.09.: 33 Karten, alle wegen eines Movers).
        if (alleCalls.empty() && fehler != nullptr && netzId != 2) {
            *fehler = "die Zeichenliste ist leer";
        }
        return 0;
    }

    // --- Ziel setzen und loeschen ----------------------------------------
    //
    // Ohne das Loeschen des Tiefenpuffers steht dort der Wert des vorigen
    // Bildes, und die Karte verschwindet teilweise hinter sich selbst.
    if (g_zielRtv == nullptr || g_tiefeDsv == nullptr) {
        if (fehler != nullptr) { *fehler = "kein Renderziel - bereiteZiel() fehlt"; }
        return 0;
    }
    const float leer[4] = {0.0F, 0.0F, 0.0F, 1.0F};
    c->OMSetRenderTargets(1, &g_zielRtv, g_tiefeDsv);
    // Nur das ERSTE Netz loescht. Das Effektnetz kommt danach und muss auf
    // die Tiefe der Karte treffen - sonst schweben die Teilchen vor allem,
    // genau der Fehler, der im Rasterer rc377 gekostet hat.
    // Nur der ERSTE Durchgang loescht. Im gemischten Durchgang stuenden
    // sonst Bild und Tiefe wieder auf Null - und alles Deckende, das gerade
    // gezeichnet wurde, waere weg.
    if (netzId == 0 && lage != Lage::Gemischt) {
        c->ClearRenderTargetView(g_zielRtv, leer);
        c->ClearDepthStencilView(g_tiefeDsv, D3D11_CLEAR_DEPTH, 1.0F, 0);
    }
    D3D11_VIEWPORT vp{};
    vp.Width = static_cast<float>(g_zielW);
    vp.Height = static_cast<float>(g_zielH);
    vp.MaxDepth = 1.0F;
    c->RSSetViewports(1, &vp);

    np.schrittweite = sizeof(Ecke);
    const UINT schritt = np.schrittweite;
    const UINT versatz = 0;
    c->IASetVertexBuffers(0, 1, &np.vb, &schritt, &versatz);
    c->IASetIndexBuffer(np.ib, DXGI_FORMAT_R32_UINT, 0);
    c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    c->VSSetConstantBuffers(0, 1, &g_cbBild);
    c->PSSetConstantBuffers(0, 1, &g_cbBild);
    c->PSSetSamplers(0, 1, &g_sampler);

    setzeSchritt("Karte: zeichnen");
    // --- Dieselbe Schleife wie der Gluehdurchgang -------------------------
    //
    // Hier stand eine zweite, wortgleiche Fassung. Zwei Schleifen, die
    // dasselbe binden, laufen frueher oder spaeter auseinander - beim
    // Einbau der Modellmatrix haette ich sie an einer Stelle vergessen, und
    // Mover waeren an der falschen Stelle gezeichnet worden.
    const int abgesetzt = zeichneAufrufe(calls, mesh, textures, geo,
                                         zeitSekunden, fehler);
    if (uebersprungenAus != nullptr) {
        // ADDIEREN, nicht zuweisen. Bei zwei Lagen laeuft diese Funktion
        // zweimal ueber dasselbe Netz; eine Zuweisung wuerde die Zahl des
        // deckenden Durchgangs verwerfen. Der Aufrufer setzt vorher auf null.
        *uebersprungenAus += static_cast<int>(calls.size()) - abgesetzt;
    }
    return abgesetzt;
}

}  // namespace bhed::gpu

#else   // kein Direct3D

namespace bhed::gpu {
void shutdown() {}
void vergissKarte() {}
void vergissMoverNetze() {}
void vergissNetz(const BspMesh*) {}
int veralteteKartenBilder() { return 0; }
SpeicherStand speicherStand() { return SpeicherStand{}; }
bool verfuegbar() { return false; }
int zeichneMover(const BspMesh&, const TextureSet*, const BspGeometry*,
                 const float*, const float*, const float*, const float*,
                 const float*, const float*, float, float, float, float,
                 std::string* fehler, int*, Lage) {
    if (fehler != nullptr) { *fehler = "ohne Direct3D gebaut"; }
    return 0;
}
int zeichneGluehen(const BspMesh&, const TextureSet*, const BspGeometry*,
                   const float*, float, int, int, bool,
                   std::string* fehler) {
    if (fehler != nullptr) { *fehler = "ohne Direct3D gebaut"; }
    return 0;
}
int zeichneLichtUndSchatten(const BspMesh&, const TextureSet*, const BspGeometry*,
                            const float*, float,
                            const std::vector<WeltLicht>&,
                            const std::vector<BlobSchatten>&,
                            const TextureSet::Tex*, const TextureSet::Tex*,
                            std::string* fehler) {
    if (fehler != nullptr) { *fehler = "ohne Direct3D gebaut"; }
    return 0;
}
int zeichneFigur(const GlmModel&, const ModelTextures*,
                 const std::vector<BoneMatrix>&, const float*, const float*,
                 std::string* fehler, const FigurLicht*, bool) {
    if (fehler != nullptr) { *fehler = "ohne Direct3D gebaut"; }
    return 0;
}
int zeichneKlingen(const std::vector<KlingenQuad>&, const float*, std::string* fehler) {
    if (fehler != nullptr) { *fehler = "ohne Direct3D gebaut"; }
    return 0;
}
bool bereiteModellZiel(int, int) { return false; }
void* modellZielTextur() { return nullptr; }
int zeichneModellAnsicht(const GlmModel&, const ModelTextures*, const std::vector<BoneMatrix>&,
                         const float*, bool, std::string* fehler) {
    if (fehler != nullptr) { *fehler = "ohne Direct3D gebaut"; }
    return 0;
}
bool bereiteZiel(int, int) { return false; }
void* zielTextur() { return nullptr; }
bool leseTiefe(std::vector<float>&, int, int, float, float) { return false; }
bool leseFarbe(std::vector<std::uint8_t>&, int&, int&) { return false; }
const char* abschnittName(Abschnitt) { return "?"; }
void bildBeginnt() {}
void bildFertig() {}
void beginneMessung(Abschnitt) {}
void beendeMessung(Abschnitt) {}
float millisekunden(Abschnitt) { return -1.0F; }
float millisekundenBeide(Abschnitt, Abschnitt) { return -1.0F; }
const char* zustandsLage() { return "ohne Direct3D gebaut"; }
std::vector<std::string> debugMeldungen() { return {}; }
std::string geraetInfo() { return "ohne Direct3D gebaut"; }
std::string berichtText() { return "ohne Direct3D gebaut\n"; }
std::vector<StapelNotiz> auffaelligeStapel() { return {}; }
int zeichneKarte(const BspMesh&, const TextureSet*, const BspGeometry*,
                 const float*, const float*, float, float, float, int,
                 const float*, const float*, const float*, float,
                 std::string* fehler, int*, Lage) {
    if (fehler != nullptr) { *fehler = "ohne Direct3D gebaut"; }
    return 0;
}
}  // namespace bhed::gpu

#endif

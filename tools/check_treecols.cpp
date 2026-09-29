// check_treecols.cpp - Sitzen die Spalten der Baumansicht dort, wo sie im
// Original sitzen? Braucht echtes Dear ImGui, aber KEINEN Bildschirm.
//
// Die Zahlen stammen aus einem Bildschirmfoto von BehavEd 2.0
// (MS Sans Serif 8pt, Schrifthoehe 13, Zeilenhoehe 16):
//
//   Mitte der gepunkteten Senkrechten   x = 25 (Tiefe 0), x = 44 (Tiefe 1)
//   Symbol beginnt                      x = 36            x = 55
//   Name beginnt                        x = 58            x = 77
//   Klammer beginnt                     x = 117           x = 136
//
// Geprueft wird die SameLine-Rechnerei aus drawTree: dass Symbol und Name
// bei jeder Tiefe genau iconAt und nameAt hinter der Spaltenmitte landen -
// und zwar unabhaengig davon, ob die Zeile ein Aufklappkaestchen hat. Genau
// das war vorher falsch: blattlose Zeilen bekamen keinen Platz fuer das
// Kaestchen, und ihre Symbole standen weiter links als die der Bloecke.
//
// Uebersetzen (aus dem Projektverzeichnis):
//   g++ -std=c++20 -I$IMGUI -Iinclude tools/check_treecols.cpp \
//       src/tree.cpp $IMGUI/imgui.cpp $IMGUI/imgui_draw.cpp \
//       $IMGUI/imgui_tables.cpp $IMGUI/imgui_widgets.cpp -o treecols
#include "bhed/tree.h"
#include "imgui.h"

#include <cmath>
#include <cstdio>
#include <string>

namespace {

int g_fails = 0;

void expect(const char* what, bool ok) {
    if (!ok) {
        ++g_fails;
    }
    std::printf("  %-4s %s\n", ok ? "ok" : "FEHL", what);
}

void near(const char* what, float got, float want, float tol = 0.6F) {
    const bool ok = std::fabs(got - want) <= tol;
    if (!ok) {
        ++g_fails;
    }
    std::printf("  %-4s %-46s gemessen %6.1f, erwartet %6.1f\n",
                ok ? "ok" : "FEHL", what, got, want);
}

// Eine Zeile genau so aufbauen wie drawTree.
struct Measured {
    float iconX = 0.0F;
    float nameX = 0.0F;
};

Measured row(const bhed::TreeMetrics& tm, int depth, bool hasChildren,
             float rowH) {
    Measured m;
    if (depth != 0) {
        ImGui::Indent(tm.indent * static_cast<float>(depth));
    }
    if (hasChildren) {
        (void)ImGui::InvisibleButton("##auf", ImVec2{tm.box, rowH});
    } else {
        ImGui::Dummy(ImVec2{tm.box, rowH});
    }
    ImGui::SameLine(0, tm.iconAt + std::floor(tm.box * 0.5F) - tm.box);
    ImGui::Dummy(ImVec2{ImGui::GetFontSize(), rowH});   // steht fuer das Symbol
    m.iconX = ImGui::GetItemRectMin().x;
    ImGui::SameLine(0, tm.nameAt - tm.iconAt - ImGui::GetFontSize());
    ImGui::TextUnformatted("affect");
    m.nameX = ImGui::GetItemRectMin().x;
    if (depth != 0) {
        ImGui::Unindent(tm.indent * static_cast<float>(depth));
    }
    return m;
}

}  // namespace

int main() {
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2{1280, 720};
    io.DeltaTime = 1.0F / 60.0F;
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
    io.Fonts->AddFontDefault();

    // Die eingebaute Schrift ist 13 Punkte hoch - genau die Hoehe, bei der
    // die gemessenen Werte des Originals gelten.
    ImGui::NewFrame();
    ImGui::Begin("t");
    const float fs = ImGui::GetFontSize();
    std::printf("Schrifthoehe: %.1f\n", fs);
    const bhed::TreeMetrics tm = bhed::treeMetricsFor(fs);

    expect("Schrift ist 13 hoch (sonst gelten die Zahlen nicht)",
           std::fabs(fs - 13.0F) < 0.01F);

    // Keine Abstaende zwischen den Elementen: drawTree rechnet mit
    // ausdruecklichen SameLine-Abstaenden, nicht mit denen des Stils.
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{0, 0});
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2{0, 0});

    const float base = ImGui::GetCursorScreenPos().x;
    const Measured d0blk = row(tm, 0, true, fs);
    const Measured d0leaf = row(tm, 0, false, fs);
    const Measured d1leaf = row(tm, 1, false, fs);
    const Measured d2leaf = row(tm, 2, false, fs);

    ImGui::PopStyleVar(2);
    // --- Stehen die Zeilen UNTEREINANDER? --------------------------------
    //
    // Eine Zeile beginnt mit einem Platzhalter fuer das Kaestchen, DANN
    // kommt SameLine. Laesst man den Platzhalter weg, steht das SameLine am
    // Zeilenanfang - und ImGui setzt die Zeile neben die VORIGE. Der ganze
    // Baum landet dann auf einer einzigen Zeile.
    //
    // Genau das ist beim Nachbauen der geteilten Ansicht passiert, und am
    // Bild sah man nur "irgendwas stimmt nicht". Diese Probe misst es: die
    // Oberkanten dreier Zeilen muessen um je eine Zeilenhoehe auseinander
    // liegen.
    float zeilenY[3] = {0.0F, 0.0F, 0.0F};
    ImGui::Begin("zeilen");
    for (int z = 0; z < 3; ++z) {
        ImGui::PushID(z);
        zeilenY[z] = ImGui::GetCursorScreenPos().y;
        ImGui::Dummy(ImVec2{tm.box, ImGui::GetTextLineHeight()});
        ImGui::SameLine(0, tm.iconAt + std::floor(tm.box * 0.5F) - tm.box);
        ImGui::Selectable("name");
        ImGui::PopID();
    }
    ImGui::End();

    ImGui::End();
    ImGui::Render();

    {
        const float schritt1 = zeilenY[1] - zeilenY[0];
        const float schritt2 = zeilenY[2] - zeilenY[1];
        const float soll = 13.0F + 4.0F;   // Zeilenhoehe plus ItemSpacing
        std::printf("Zeilenabstand: %.1f und %.1f (erwartet etwa %.1f)\n",
                    (double)schritt1, (double)schritt2, (double)soll);
        expect("die Zeilen stehen untereinander, nicht nebeneinander",
               schritt1 > 10.0F && schritt2 > 10.0F);
        expect("und zwar gleichmaessig",
               std::fabs(schritt1 - schritt2) < 0.51F);
    }

    // Die Spaltenmitte liegt eine halbe Kaestchenbreite hinter dem Rand.
    const float half = std::floor(tm.box * 0.5F);
    auto centre = [&](int depth) {
        return base + tm.indent * static_cast<float>(depth) + half;
    };

    std::printf("\nTiefe 0, Block (mit Kaestchen):\n");
    near("Symbol hinter der Spaltenmitte", d0blk.iconX - centre(0), tm.iconAt);
    near("Name hinter der Spaltenmitte", d0blk.nameX - centre(0), tm.nameAt);

    std::printf("\nTiefe 0, Blatt (OHNE Kaestchen) - muss gleich stehen:\n");
    near("Symbol wie beim Block", d0leaf.iconX, d0blk.iconX);
    near("Name wie beim Block", d0leaf.nameX, d0blk.nameX);

    std::printf("\nEinrueckung:\n");
    near("Tiefe 1 ist 19 weiter rechts", d1leaf.nameX - d0leaf.nameX, tm.indent);
    near("Tiefe 2 ist 38 weiter rechts", d2leaf.nameX - d0leaf.nameX,
         tm.indent * 2.0F);

    std::printf("\nGegen das Bildschirmfoto (Spaltenmitte auf x=25 gelegt):\n");
    const float shift = 25.0F - centre(0);
    near("Symbol bei x=36", d0blk.iconX + shift, 36.0F, 1.5F);
    near("Name bei x=58", d0blk.nameX + shift, 58.0F, 1.5F);
    near("Klammer bei x=117", d0blk.nameX + shift + tm.nameW, 117.0F, 1.5F);
    near("Tiefe 1: Spaltenmitte bei x=44", centre(1) + shift, 44.0F, 1.5F);
    near("Tiefe 1: Klammer bei x=136", d1leaf.nameX + shift + tm.nameW, 136.0F,
         1.5F);

    ImGui::DestroyContext();
    std::printf("\n%s (%d Fehlschlaege)\n",
                g_fails == 0 ? "alle Spaltenproben bestanden" : "FEHLGESCHLAGEN",
                g_fails);
    return g_fails == 0 ? 0 : 1;
}

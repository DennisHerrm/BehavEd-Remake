// Der Spaltenaufbau, KOPFLOS gemessen.
//
// Warum es das gibt:
//
// shank: "kannst du es nicht fuer dich nachbauen hier im chat um es zu
// ueberwachen?" - ja, und das haette sechs Runden gespart.
//
// Beim Layout konnte ich als einziges nicht messen: ich habe blind
// geaendert, shank hat gebaut, ein Bild geschickt, und ich habe gesehen,
// dass es falsch ist. Sechs Runden (rc469 bis rc474), jede zwanzig Minuten
// seiner Zeit.
//
// ImGui braucht dafuer weder Fenster noch Grafikkarte. Kontext anlegen,
// DisplaySize setzen, NewFrame/Render - und danach sagt GetWindowPos und
// GetWindowSize, wo jede Spalte steht. ECHTES ImGui mit denselben Fahnen
// und denselben Zahlen, kein Nachbau der Rechnung.
//
// Uebersetzen:
//   g++ -std=c++20 -I<imgui> tools/layoutcheck.cpp \
//       <imgui>/imgui.cpp <imgui>/imgui_draw.cpp \
//       <imgui>/imgui_tables.cpp <imgui>/imgui_widgets.cpp -o layoutcheck
//   ./layoutcheck 2496 1484
//
// Was er zeigt: ob alle Spalten auf derselben Linie enden, wie breit sie
// innen wirklich sind, und wieviel zwischen ihnen und der Statuszeile
// uebrig bleibt.
//
// Er stellt den Aufbau NACH, er ist nicht behaved. Wer eine Zahl in
// gui/app.cpp aendert, muss sie hier auch aendern - sonst misst er etwas
// anderes. Das ist der Preis dafuer, dass er ohne den ganzen Programmrumpf
// laeuft.
#include "imgui.h"
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

struct Rechteck { std::string name; float x, y, w, h; };
static std::vector<Rechteck> g_rechtecke;

static void merke(const char* name) {
    const ImVec2 p = ImGui::GetWindowPos();
    const ImVec2 s = ImGui::GetWindowSize();
    g_rechtecke.push_back({name, p.x, p.y, s.x, s.y});
}

int main(int argc, char** argv) {
    const float breite = (argc > 1) ? std::stof(argv[1]) : 2496.0f;
    const float hoehe  = (argc > 2) ? std::stof(argv[2]) : 1484.0f;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(breite, hoehe);
    io.DeltaTime = 1.0f / 60.0f;
    io.IniFilename = nullptr;
    unsigned char* px = nullptr; int fw = 0, fh = 0;
    io.Fonts->GetTexDataAsRGBA32(&px, &fw, &fh);
    io.Fonts->SetTexID((ImTextureID)(intptr_t)1);

    // Zwei Bilder: Tabellen brauchen eines, um sich einzupendeln.
    for (int bild = 0; bild < 3; ++bild) {
        g_rechtecke.clear();
        ImGui::NewFrame();
        const ImGuiViewport* vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(vp->WorkPos);
        ImGui::SetNextWindowSize(vp->WorkSize);
        ImGui::Begin("Haupt", nullptr,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                         ImGuiWindowFlags_NoMove | ImGuiWindowFlags_MenuBar);
        if (ImGui::BeginMenuBar()) { ImGui::MenuItem("File"); ImGui::EndMenuBar(); }

        // Reiterleiste, wie in behaved
        ImGui::Button("unnamed.txt");
        ImGui::SameLine(); ImGui::Button("+");

        const ImGuiStyle& st = ImGui::GetStyle();
        const float statusH = ImGui::GetTextLineHeightWithSpacing();
        const float toolH = 0.0f;
        const float fussH = toolH + statusH + st.ItemSpacing.y;
        const float headerH = ImGui::GetFrameHeightWithSpacing();
        const float frameH = -(fussH + headerH);

        // Kopfzeilen der Spalten
        ImGui::TextUnformatted("Events");

        const float linksAnteil = 0.22f;
        // Wie computeLayout: der breiteste Text bestimmt die Untergrenze.
        // Die Ueberschriften und die Kaestchen der Ankreuzfelder zaehlen mit
        // - genau das fehlte bis rc476.
        float widest = 0.0f;
        for (const char* t : {"Delete","Duplicate","Paste","REM","Find","New",
                              "Append","Save","Save all","Save as","Export",
                              "Backup","Restore","Exit","Compile!"}) {
            widest = std::max(widest, ImGui::CalcTextSize(t).x);
        }
        for (const char* t : {"File","Treeview Options","Application"}) {
            widest = std::max(widest, ImGui::CalcTextSize(t).x);
        }
        const float kaestchen =
            ImGui::GetFrameHeight() + st.ItemInnerSpacing.x;
        for (const char* t : {"Show Types","%g floats","Fold Macros"}) {
            widest = std::max(widest, ImGui::CalcTextSize(t).x + kaestchen);
        }
        const float buttonsW =
            widest + st.FramePadding.x * 2.0f + st.ScrollbarSize +
            st.ItemSpacing.x;
        g_rechtecke.push_back({"  breitester Text", 0, 0, widest, 0});
        g_rechtecke.push_back({"  daraus buttonsW", 0, 0, buttonsW, 0});
        // Mindestbreite: `table->MinColumnWidth` entsteht in BeginTable aus
        // `FramePadding.x` (imgui_tables.cpp:905). Fuer feste Spalten gilt
        // das Vierfache (Zeile 1047), fuer gedehnte das Einfache (1093).
        const float mindest = ImGui::GetFontSize() * 3.0f;
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
                            ImVec2(mindest, st.FramePadding.y));
        g_rechtecke.push_back({"  Mindestbreite gedehnt", 0, 0, mindest, 0});
        g_rechtecke.push_back({"  Mindestbreite fest", 0, 0, mindest * 4.0f, 0});
        if (ImGui::BeginTable("##spalten", 3,
                              ImGuiTableFlags_Resizable |
                                  ImGuiTableFlags_BordersInnerV |
                                  ImGuiTableFlags_NoPadOuterX |
                                  ImGuiTableFlags_NoSavedSettings,
                              ImVec2(0.0f, 0.0f))) {
            ImGui::PopStyleVar();
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, st.FramePadding);
            const float gesamtB = ImGui::GetContentRegionAvail().x;
            const float minEvents = ImGui::GetFontSize() * 14.0f;
            const float maxEvents = std::max(minEvents,
                gesamtB - buttonsW - ImGui::GetFontSize() * 12.0f);
            const float breiteEvents =
                std::clamp(gesamtB * linksAnteil, minEvents, maxEvents);
            g_rechtecke.push_back({"  Events min/soll/max", 0, minEvents,
                                   breiteEvents, 0});
            ImGui::TableSetupColumn("Events", ImGuiTableColumnFlags_WidthFixed,
                                    breiteEvents);
            ImGui::TableSetupColumn("Flow", ImGuiTableColumnFlags_WidthStretch,
                                    1.0f);
            ImGui::TableSetupColumn("Actions",
                                    ImGuiTableColumnFlags_WidthFixed,
                                    buttonsW);
            ImGui::TableNextRow();

            ImGui::TableNextColumn();
            ImGui::BeginChild("events", ImVec2(0.0f, -fussH),
                              ImGuiChildFlags_Borders);
            merke("Events");
            ImGui::EndChild();

            ImGui::TableNextColumn();
            ImGui::BeginChild("flow", ImVec2(0.0f, -fussH),
                              ImGuiChildFlags_Borders);
            merke("ScriptFlow");
            ImGui::EndChild();

            ImGui::TableNextColumn();
            ImGui::BeginChild("buttons", ImVec2(0.0f, -fussH),
                              ImGuiChildFlags_Borders);
            merke("Actions");
            // Was passt hier hinein?
            g_rechtecke.push_back({"  Actions-Innenbreite",
                                   0, 0, ImGui::GetContentRegionAvail().x, 0});
            ImGui::EndChild();
            ImGui::EndTable();
        }
        ImGui::PopStyleVar();
        // Statuszeile - wo landet sie?
        const float statusY = ImGui::GetCursorPosY();
        ImGui::TextUnformatted("Ready");
        g_rechtecke.push_back({"  Statuszeile beginnt bei y", 0,
                               ImGui::GetWindowPos().y + statusY, 0, 0});
        g_rechtecke.push_back({"  Innenbereich endet bei y", 0,
                               ImGui::GetWindowPos().y +
                                   ImGui::GetWindowHeight() -
                                   ImGui::GetStyle().WindowPadding.y, 0, 0});
        g_rechtecke.push_back({"  CellPadding.x", 0, 0,
                               ImGui::GetStyle().CellPadding.x, 0});
        g_rechtecke.push_back({"  WindowPadding.x", 0, 0,
                               ImGui::GetStyle().WindowPadding.x, 0});
        merke("Hauptfenster");
        ImGui::End();
        ImGui::Render();
    }

    std::printf("Fenster %.0f x %.0f\n\n", breite, hoehe);
    std::printf("%-22s %8s %8s %8s %8s %10s\n", "", "x", "y", "b", "h",
                "endet bei");
    for (const Rechteck& r : g_rechtecke) {
        if (r.h == 0.0f && r.w == 0.0f) {
            std::printf("%-28s %8s %8.0f\n", r.name.c_str(), "", r.y);
        } else if (r.h == 0.0f) {
            std::printf("%-28s %8s %8s %8.0f\n", r.name.c_str(), "", "", r.w);
        } else {
            std::printf("%-28s %8.0f %8.0f %8.0f %8.0f %10.0f\n",
                        r.name.c_str(), r.x, r.y, r.w, r.h, r.y + r.h);
        }
    }
    ImGui::DestroyContext();
    return 0;
}

// check_keyorder.cpp - Regressionsprobe fuer die Reihenfolge der
// Tastenbehandlung. Braucht echtes Dear ImGui, aber KEINEN Bildschirm.
//
// Anlass (rc95): Enter im Ereignis-Editor schloss den Dialog und oeffnete
// ihn im SELBEN Bild wieder. drawEditor() setzt editorOpen auf false,
// danach lief handleShortcuts(), dessen Wache genau dieses Feld abfragte -
// und mit IsKeyPressed (ohne Route) dieselbe Taste ein zweites Mal sah.
//
// Diese Probe baut den Ablauf nach und prueft: eine Eingabe oeffnet
// genau einmal, die naechste schliesst, und danach bleibt es zu.
//
// Uebersetzen (aus tools/):
//   g++ -std=c++20 -I$IMGUI check_keyorder.cpp \
//       $IMGUI/imgui.cpp $IMGUI/imgui_draw.cpp \
//       $IMGUI/imgui_tables.cpp $IMGUI/imgui_widgets.cpp -o keyorder
// Nachbau der Reihenfolge aus app.cpp: drawEditor() vor handleTreeKeys(),
// beide mit Shortcut(Enter). Kein Bildschirm noetig.
#include "imgui.h"
#include "imgui_internal.h"
#include <cstdio>
#include <cstdint>

static bool editorOpen = false;
static bool dialogAtFrameStart = false;
static int  openedCount = 0;
static int  closedCount = 0;

static void drawEditor(int frame) {
    if (!editorOpen) { return; }
    ImGui::OpenPopup("Event editor");
    if (!ImGui::BeginPopupModal("Event editor", &editorOpen, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }
    ImGui::TextUnformatted("MOVE");
    const bool enter = ImGui::Shortcut(ImGuiKey_Enter) || ImGui::Shortcut(ImGuiKey_KeypadEnter);
    if (ImGui::Button("Ok") || enter) {
        printf("  [Bild %d] Editor SCHLIESST (Shortcut im Popup = %d)\n", frame, (int)enter);
        ++closedCount;
        editorOpen = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

// Das ist die Stelle aus app.cpp:699 - IsKeyPressed statt Shortcut,
// also OHNE Route, und die Wache fragt editorOpen ab, das drawEditor
// im selben Bild schon auf false gesetzt hat.
static void handleShortcuts(int frame) {
    if (dialogAtFrameStart || editorOpen) { return; }
    if (ImGui::IsKeyPressed(ImGuiKey_Enter, false)) {
        printf("  [Bild %d] handleShortcuts OEFFNET den Editor (EditItem)\n", frame);
        ++openedCount;
        editorOpen = true;
    }
}

static void handleTreeKeys(int frame) {
    if (dialogAtFrameStart || editorOpen) { return; }
    if (ImGui::Shortcut(ImGuiKey_Enter)) {
        printf("  [Bild %d] Baum OEFFNET den Editor\n", frame);
        ++openedCount;
        editorOpen = true;
    }
}

int main() {
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2{1280, 720};
    io.DeltaTime = 1.0F / 60.0F;
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
    io.Fonts->AddFontDefault();

    for (int frame = 1; frame <= 8; ++frame) {
        // Eingabe: Bild 2 oeffnet den Editor, Bild 5 druecken wir Enter im Editor.
        if (frame == 2 || frame == 5) { io.AddKeyEvent(ImGuiKey_Enter, true); }
        if (frame == 3 || frame == 6) { io.AddKeyEvent(ImGuiKey_Enter, false); }

        ImGui::NewFrame();
        dialogAtFrameStart = editorOpen;
        printf("Bild %d: editorOpen=%d\n", frame, (int)editorOpen);
        ImGui::Begin("behaved");
        ImGui::TextUnformatted("Script Flow");
        drawEditor(frame);
        handleShortcuts(frame);
        handleTreeKeys(frame);
        ImGui::End();
        ImGui::Render();
    }
    const bool ok = (openedCount == 1 && closedCount == 1 && !editorOpen);
    printf("\ngeoeffnet: %d  geschlossen: %d  am Ende offen: %d  -> %s\n",
           openedCount, closedCount, (int)editorOpen,
           ok ? "bestanden" : "FEHLGESCHLAGEN");
    ImGui::DestroyContext();
    return ok ? 0 : 1;
}

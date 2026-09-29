// imgui_config.h - eigene Einstellungen fuer Dear ImGui
//
// Wird ueber -DIMGUI_USER_CONFIG eingebunden.
//
// Der einzige Zweck: IM_ASSERT umleiten. Eine ImGui-Zusicherung erschien
// sonst nur als Meldungskasten der Laufzeitbibliothek -
//
//     Assertion failed!  imgui.cpp Line 3674
//     GImGui != NULL && "No current context. ..."
//
// - und im Protokoll stand nichts. Jetzt landet Ausdruck, Datei und Zeile
// zuerst in der Protokolldatei; erst danach bricht das Programm wie gewohnt
// ab. Ein Absturz ohne Spur ist nicht zu untersuchen.
#pragma once

// Die Haken der ImGui-Testumgebung: jedes Element meldet Kennung,
// Beschriftung und Rechteck. Damit klickt der Selbsttest (gui/selbsttest.cpp)
// Knoepfe dort, wo sie gezeichnet wurden. ImGui ruft die Haken nur, solange
// `TestEngineHookItems` gesetzt ist - und das setzt allein der Selbsttest.
// Im normalen Betrieb kostet es eine Abfrage je Element.
#define IMGUI_ENABLE_TEST_ENGINE

namespace bhed::gui {
void logImGuiAssert(const char* expression, const char* file, int line);
}

#define IM_ASSERT(expr)                                                      \
    do {                                                                     \
        if (!(expr)) {                                                       \
            ::bhed::gui::logImGuiAssert(#expr, __FILE__, __LINE__);          \
        }                                                                    \
    } while (0)

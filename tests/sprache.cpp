// Welche Sprache gewinnt beim Start?
//
// shank arbeitet auf `Gebiet de-DE`, und in seinem rc539-Protokoll stand
// die Statuszeile trotzdem englisch:
//
//     Statuszeile: Dropped where it already was - nothing to do
//
// Die Kette beim Start:
//     setLanguage(fromSystemLocale(systemLocale()))   <- Gebietsschema
//     loadSettingsFile(...)                           <- kann ueberschreiben
//     if (findLanguage(settings.language)) setLanguage(...)
//
// `Settings::language` hatte die Vorgabe "en". Damit war "noch nichts
// gewaehlt" nicht von "Englisch gewaehlt" zu unterscheiden - und fehlte
// der Schluessel in der Einstellungsdatei, gewann die Vorgabe und machte
// die Gebietserkennung wirkungslos.
#include "bhed/i18n.h"
#include "bhed/settings.h"

#include <cstdio>
#include <string>

static int g_fehl = 0;

static void erwarte(const char* was, bool ist, bool soll) {
    if (ist == soll) { std::printf("  ok    %s\n", was); return; }
    std::printf("  FEHL  %s (erwartet %s)\n", was, soll ? "ja" : "nein");
    ++g_fehl;
}

// Genau die Kette aus main_win32.cpp und loadSettings().
static bhed::i18n::Language starte(const std::string& gebiet,
                                   const std::string& ausDerDatei) {
    bhed::i18n::setLanguage(bhed::i18n::fromSystemLocale(gebiet));
    bhed::Settings s;                    // Vorgaben
    if (!ausDerDatei.empty()) {
        s.language = ausDerDatei;        // stand so in der Datei
    }
    if (const bhed::i18n::LanguageInfo* l = bhed::i18n::findLanguage(s.language)) {
        bhed::i18n::setLanguage(l->language);
    }
    return bhed::i18n::currentLanguage();
}

int main() {
    using L = bhed::i18n::Language;

    // --- Das ist der Fall, der schiefging -----------------------------
    erwarte("de-DE ohne Eintrag in der Datei -> Deutsch",
            starte("de-DE", "") == L::German, true);
    erwarte("ja-JP ohne Eintrag -> Japanisch",
            starte("ja-JP", "") == L::Japanese, true);
    erwarte("fr-FR ohne Eintrag -> Englisch (haben wir nicht)",
            starte("fr-FR", "") == L::English, true);

    // --- Eine ausdrueckliche Wahl gewinnt weiterhin -------------------
    erwarte("de-DE, aber \"en\" gewaehlt -> Englisch",
            starte("de-DE", "en") == L::English, true);
    erwarte("en-US, aber \"de\" gewaehlt -> Deutsch",
            starte("en-US", "de") == L::German, true);

    // --- Die Vorgabe selbst -------------------------------------------
    {
        bhed::Settings s;
        erwarte("Settings::language ist ab Werk LEER", s.language.empty(), true);
        erwarte("und leer findet keine Sprache",
                bhed::i18n::findLanguage(s.language) == nullptr, true);
    }

    // --- Unsinn in der Datei darf das Gebietsschema nicht kippen ------
    erwarte("de-DE mit unbekanntem Code -> Deutsch bleibt",
            starte("de-DE", "kl-KL") == L::German, true);

    if (g_fehl != 0) {
        std::printf("Sprach-Proben: %d Fehlschlag(e)\n", g_fehl);
        return 1;
    }
    std::printf("Sprach-Proben bestanden (0 Fehlschlaege)\n");
    return 0;
}

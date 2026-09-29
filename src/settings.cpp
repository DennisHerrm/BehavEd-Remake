#include "bhed/settings.h"

#include "bhed/num.h"

#include <cstddef>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

namespace bhed {
namespace {

std::string trim(const std::string& s) {
    std::size_t a = 0;
    std::size_t b = s.size();
    auto sp = [](char c) { return c == ' ' || c == '\t' || c == '\r'; };
    while (a < b && sp(s[a])) { ++a; }
    while (b > a && sp(s[b - 1])) { --b; }
    return s.substr(a, b - a);
}

void put(std::string& o, const char* key, const std::string& value) {
    o += key;
    o += '=';
    o += value;
    o += '\n';
}

void put(std::string& o, const char* key, bool value) {
    put(o, key, value ? std::string("1") : std::string("0"));
}

bool toBool(const std::string& v) { return v == "1" || v == "true" || v == "yes"; }

}  // namespace

void Settings::noteRecent(const std::string& path) {
    if (path.empty()) {
        return;
    }
    for (std::size_t i = 0; i < recent.size(); ++i) {
        if (recent[i] == path) {
            recent.erase(recent.begin() + static_cast<std::ptrdiff_t>(i));
            break;
        }
    }
    recent.insert(recent.begin(), path);
    while (recent.size() > kMaxRecent) {
        recent.pop_back();
    }
}

std::string writeSettings(const Settings& s) {
    std::string o = "# behaved\n";
    put(o, "scriptPath", s.scriptPath);
    put(o, "ibizePath", s.ibizePath);
    put(o, "commandFile", s.commandFile);
    put(o, "sourcePath", s.sourcePath);
    put(o, "reopenLastFile", s.reopenLastFile);
    put(o, "legalExit", s.legalExit);
    put(o, "alphaSortPulldowns", s.alphaSortPulldowns);
    put(o, "alternativeIcons", s.alternativeIcons);
    put(o, "queryOnDiscard", s.queryOnDiscard);
    put(o, "dialogUsesLastDir", s.dialogUsesLastDir);
    put(o, "lastDir", s.lastDir);
    put(o, "mapPath", s.mapPath);
    put(o, "language", s.language);
    put(o, "theme", s.theme);
    put(o, "windowPlacement", s.windowPlacement);
    // Die Helferzeilen, eine Zeile je Befehl und Feld.
    for (const auto& kv : s.helfer) {
        put(o, ("helfer." + kv.first).c_str(), kv.second);
    }
    for (const auto& kv : s.lesezeichen) {
        put(o, ("lesezeichen." + kv.first).c_str(), kv.second);
    }
    put(o, "changeHistory", s.changeHistory);
    put(o, "highlightSame", s.highlightSame);
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.3f", static_cast<double>(s.uiScale));
    put(o, "uiScale", std::string(buf));
    std::snprintf(buf, sizeof(buf), "%.4f", static_cast<double>(s.splitEvents));
    put(o, "splitEvents", std::string(buf));
    std::snprintf(buf, sizeof(buf), "%.4f", static_cast<double>(s.splitButtons));
    put(o, "splitButtons", std::string(buf));
    std::snprintf(buf, sizeof(buf), "%.4f", static_cast<double>(s.splitMap));
    put(o, "splitMap", std::string(buf));
    std::snprintf(buf, sizeof(buf), "%.4f", static_cast<double>(s.splitModel));
    put(o, "splitModel", std::string(buf));
    std::snprintf(buf, sizeof(buf), "%.4f", static_cast<double>(s.splitStatus));
    put(o, "splitStatus", std::string(buf));
    std::snprintf(buf, sizeof(buf), "%.4f", static_cast<double>(s.timelineFrac));
    put(o, "timelineFrac", std::string(buf));
    std::snprintf(buf, sizeof(buf), "%.4f", static_cast<double>(s.splitFracX));
    put(o, "splitFracX", std::string(buf));
    std::snprintf(buf, sizeof(buf), "%.4f", static_cast<double>(s.splitFracY));
    put(o, "splitFracY", std::string(buf));
    put(o, "timelineFrames", s.timelineFrames);
    put(o, "mapSidebar", s.mapSidebar);
    put(o, "showTypes", s.showTypes);
    put(o, "gFloats", s.gFloats);
    put(o, "foldMacros", s.foldMacros);
    for (const std::string& r : s.recent) {
        put(o, "recent", r);
    }
    for (const std::string& g : s.gamePaths) {
        put(o, "gamePath", g);
    }
    // "key=MoveUp 4194643" - Aktionsname und ImGuiKeyChord als Zahl.
    // Warum die Zahl und nicht der Name der Taste, steht bei
    // Settings::KeyBinding.
    for (const Settings::KeyBinding& b : s.keyBindings) {
        if (b.action.empty() || b.action.find(' ') != std::string::npos) {
            continue;   // kaputte Kennung nicht schreiben
        }
        put(o, "key", b.action + " " + std::to_string(b.chord));
    }
    return o;
}

bool readSettings(const std::string& text, Settings& out) {
    out.recent.clear();
    out.gamePaths.clear();
    out.keyBindings.clear();
    std::string line;
    std::istringstream in(text);
    while (std::getline(in, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') {
            continue;
        }
        const std::size_t eq = line.find('=');
        if (eq == std::string::npos) {
            continue;
        }
        const std::string key = trim(line.substr(0, eq));
        const std::string value = trim(line.substr(eq + 1));

        if (key.rfind("breite.", 0) == 0) {
            // Gelernte Breiten des Ereignisfensters (rc564-rc567). Seit
            // rc568 wird die Breite ausgerechnet; alte Zeilen werden
            // ueberlesen und beim naechsten Speichern nicht mehr geschrieben.
        }
        else if (key == "windowPlacement") { out.windowPlacement = value; }
        else if (key.rfind("helfer.", 0) == 0) {
            out.helfer[key.substr(7)] = value;
        }
        else if (key.rfind("lesezeichen.", 0) == 0) {
            out.lesezeichen[key.substr(12)] = value;
        }
        else if (key == "changeHistory") { out.changeHistory = toBool(value); }
        else if (key == "highlightSame") { out.highlightSame = toBool(value); }
        else if (key == "scriptPath") { out.scriptPath = value; }
        else if (key == "ibizePath") { out.ibizePath = value; }
        else if (key == "commandFile") { out.commandFile = value; }
        else if (key == "sourcePath") { out.sourcePath = value; }
        else if (key == "reopenLastFile") { out.reopenLastFile = toBool(value); }
        else if (key == "legalExit") { out.legalExit = toBool(value); }
        else if (key == "alphaSortPulldowns") { out.alphaSortPulldowns = toBool(value); }
        else if (key == "alternativeIcons") { out.alternativeIcons = toBool(value); }
        else if (key == "queryOnDiscard") { out.queryOnDiscard = toBool(value); }
        else if (key == "dialogUsesLastDir") { out.dialogUsesLastDir = toBool(value); }
        else if (key == "lastDir") { out.lastDir = value; }
        else if (key == "mapPath") { out.mapPath = value; }
        else if (key == "language") { out.language = value; }
        else if (key == "theme") { out.theme = value; }
        else if (key == "showTypes") { out.showTypes = toBool(value); }
        else if (key == "gFloats") { out.gFloats = toBool(value); }
        else if (key == "foldMacros") { out.foldMacros = toBool(value); }
        else if (key == "uiScale") {
            float v = 0.0F;
            if (parseFloat(value, v)) {
                // Unsinnige Werte nicht uebernehmen: eine kaputte Datei darf
                // das Fenster nicht unbedienbar machen.
                if (v >= 0.5F && v <= 4.0F) { out.uiScale = v; }
            }
        }
        else if (key == "splitEvents") {
            float v = 0.0F;
            if (parseFloat(value, v)) {
                if (v >= 0.05F && v <= 0.90F) { out.splitEvents = v; }
            }
        }
        // --- splitMap wurde GESCHRIEBEN, aber nie gelesen ---------------
        //
        // Gemeldet: "jedes Mal wenn ich das Programm starte ist die Leiste
        // rechts wieder ausgestreckt, statt so wie ich es eingestellt habe."
        //
        // Im Protokoll stand es deutlich, man musste nur beide Zeilen
        // nebeneinanderhalten:
        //
        //     geladen:   Karte 0.550   <- der VORGABEwert (settings.h:50)
        //     gesichert: Karte 0.943
        //
        // Jede Sitzung sicherte 0.943 und lud wieder 0.550. Der Schreiber
        // kannte den Schluessel (settings.cpp:77), der Leser nicht - also
        // fiel der Wert bei jedem Start auf die Vorgabe zurueck.
        //
        // Die Grenzen wie beim Schieber selbst (gui/app.cpp: 0.12 bis 0.98).
        else if (key == "splitMap") {
            float v = 0.0F;
            if (parseFloat(value, v)) {
                // Untergrenze 0.35, nicht 0.12.
                //
                // In dieser Spalte liegt die GANZE Kartenansicht - Ebenen,
                // Bild und die Einstellungsspalte. Bei 0.12 ist davon
                // nichts zu erkennen.
                //
                // Und es steht wirklich so in shanks Datei: die
                // Rueckkopplung aus rc484 hat den Wert bis zur Untergrenze
                // heruntergeschrieben, und beim Start galt er wieder.
                // "Das ist immer, wenn ich es starte, und dann muss ich es
                // nach rechts ziehen."
                //
                // Ein gespeicherter Wert, der unbrauchbar ist, wird
                // verworfen und nicht uebernommen.
                if (v >= 0.35F && v <= 0.98F) { out.splitMap = v; }
            }
        }
        // Die Modellansicht hat ihren eigenen Wert - dieselben Grenzen,
        // denn in ihrer Spalte steckt ebenfalls die ganze Ansicht.
        else if (key == "splitModel") {
            float v = 0.0F;
            if (parseFloat(value, v)) {
                if (v >= 0.35F && v <= 0.98F) { out.splitModel = v; }
            }
        }
        else if (key == "splitStatus") {
            float v = 0.0F;
            if (parseFloat(value, v)) {
                if (v >= 0.05F && v <= 0.60F) { out.splitStatus = v; }
            }
        }
        else if (key == "timelineFrac") {
            float v = 0.0F;
            // Dieselben Grenzen wie beim Ziehen - eine Datei von Hand
            // bearbeitet, und die Leiste waere sonst weg.
            if (parseFloat(value, v) && v >= 0.08F && v <= 0.60F) {
                out.timelineFrac = v;
            }
        }
        else if (key == "splitFracX") {
            float v = 0.0F;
            if (parseFloat(value, v) && v >= 0.15F && v <= 0.85F) {
                out.splitFracX = v;
            }
        }
        else if (key == "splitFracY") {
            float v = 0.0F;
            if (parseFloat(value, v) && v >= 0.15F && v <= 0.85F) {
                out.splitFracY = v;
            }
        }
        else if (key == "timelineFrames") {
            out.timelineFrames = (value == "true" || value == "1");
        }
        else if (key == "mapSidebar") {
            out.mapSidebar = (value == "true" || value == "1");
        }
        else if (key == "splitMap") {
            float v = 0.0F;
            if (parseFloat(value, v)) {
                if (v >= 0.05F && v <= 0.90F) { out.splitMap = v; }
            }
        }
        else if (key == "splitModel") {
            float v = 0.0F;
            if (parseFloat(value, v)) {
                if (v >= 0.05F && v <= 0.90F) { out.splitModel = v; }
            }
        }
        else if (key == "splitButtons") {
            float v = 0.0F;
            if (parseFloat(value, v)) {
                if (v >= 0.03F && v <= 0.60F) { out.splitButtons = v; }
            }
        }
        else if (key == "gamePath") {
            if (out.gamePaths.size() < 8) { out.gamePaths.push_back(value); }
        }
        else if (key == "key") {
            const std::size_t sp = value.find(' ');
            if (sp != std::string::npos && sp != 0) {
                Settings::KeyBinding b;
                b.action = value.substr(0, sp);
                try {
                    b.chord = std::stoi(value.substr(sp + 1));
                } catch (...) {
                    continue;   // keine Zahl - Eintrag verwerfen, Vorgabe gilt
                }
                out.keyBindings.push_back(std::move(b));
            }
        }
        else if (key == "recent") {
            if (out.recent.size() < Settings::kMaxRecent) { out.recent.push_back(value); }
        }
        // Unbekannte Schluessel bleiben unbeachtet.
    }
    return true;
}

bool loadSettingsFile(const std::string& path, Settings& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        return false;
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    return readSettings(ss.str(), out);
}

bool saveSettingsFile(const std::string& path, const Settings& s) {
    std::ofstream f(path, std::ios::binary);
    if (!f) {
        return false;
    }
    const std::string text = writeSettings(s);
    f.write(text.data(), static_cast<std::streamsize>(text.size()));
    return static_cast<bool>(f);
}

}  // namespace bhed

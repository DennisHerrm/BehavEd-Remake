#include "bhed/diag.h"

#include <chrono>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

namespace bhed::diag {
namespace {

std::ofstream g_file;
std::vector<std::string> g_lines;
std::vector<std::string> g_stack;   // offene Schritte
int g_depth = 0;

std::int64_t nowMicros() {
    return std::chrono::duration_cast<std::chrono::microseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

const char* mark(Level level) {
    switch (level) {
        case Level::Step: return "";
        case Level::Warning: return "WARN ";
        case Level::Error: return "ERR  ";
        default: return "     ";
    }
}

}  // namespace

bool open(const std::string& path) {
    // Das vorige Protokoll beiseitelegen, nicht ueberschreiben - sonst
    // loescht der Neustartversuch nach dem Absturz gerade den Beweis.
    const std::string previous = path + ".vorher";
    std::remove(previous.c_str());
    std::rename(path.c_str(), previous.c_str());

    g_file.open(path, std::ios::binary);
    return g_file.is_open();
}

void close() {
    write(Level::Info, "beendet");
    if (g_file.is_open()) {
        g_file.close();
    }
}

namespace {
std::ofstream g_detail;
}  // namespace

namespace {
std::string g_logVerzeichnis;
}

std::string logVerzeichnis() { return g_logVerzeichnis; }

bool openDetail(const std::string& path) {
    // Aus dem Pfad das Verzeichnis merken - fuer den Bericht.
    const std::size_t schraeg = path.find_last_of("/\\");
    if (schraeg != std::string::npos) {
        g_logVerzeichnis = path.substr(0, schraeg);
    }
    // Wie beim Ablaufprotokoll: das vorige beiseitelegen, nicht
    // ueberschreiben. Wer zweimal startet, um einen Fehler nachzustellen,
    // soll den ersten Lauf nicht dabei loeschen.
    //
    // Das ZIEL muss vorher weg.
    //
    // Auf POSIX ueberschreibt rename() stillschweigend, unter Windows
    // SCHEITERT es, wenn die Zieldatei schon existiert. Ich hatte hier nur
    // umbenannt - unter Linux lief das durch, unter Windows ging ab dem
    // ZWEITEN Start das vorige Protokoll verloren. Genau der Fall, fuer den
    // es gedacht ist.
    //
    // open() daneben macht es seit jeher richtig; ich habe es beim
    // Nachbauen uebersehen. Gefunden hat es der MSVC-Lauf auf shanks
    // Rechner, nicht meiner hier.
    const std::string previous = path + ".vorher";
    std::remove(previous.c_str());
    std::rename(path.c_str(), previous.c_str());
    g_detail.open(path, std::ios::out | std::ios::trunc);
    return g_detail.is_open();
}

void closeDetail() {
    if (g_detail.is_open()) {
        g_detail << "--- Ende ---\n";
        g_detail.close();
    }
}

bool detailOn() noexcept { return g_detail.is_open(); }

void detail(const std::string& text) {
    if (!g_detail.is_open()) {
        return;
    }
    g_detail << text << '\n';
    // Wie beim Ablaufprotokoll leeren: stuerzt das Programm mitten im
    // Laden ab, ist die letzte Zeile die interessante.
    g_detail.flush();
}

void write(Level level, const std::string& text) {
    std::string line;
    line.reserve(text.size() + 16);
    line += mark(level);
    for (int i = 0; i < g_depth; ++i) {
        line += "  ";
    }
    line += text;

    g_lines.push_back(line);
    if (g_file.is_open()) {
        g_file << line << '\n';
        // Nach JEDER Zeile leeren. Ein gepuffertes Protokoll verliert genau
        // die Zeile, auf die es ankommt.
        g_file.flush();
    }
}

Step::Step(std::string name) : name_(std::move(name)), startMicros_(nowMicros()) {
    write(Level::Step, "> " + name_);
    g_stack.push_back(name_);
    ++g_depth;
}

Step::~Step() {
    --g_depth;
    if (!g_stack.empty()) {
        g_stack.pop_back();
    }
    const double ms = static_cast<double>(nowMicros() - startMicros_) / 1000.0;
    char buf[64];
    std::snprintf(buf, sizeof(buf), " (%.1f ms)", ms);
    write(Level::Step, "< " + name_ + (failure_.empty() ? "" : "  FEHLER: " + failure_) + buf);
}

void Step::fail(const std::string& reason) { failure_ = reason; }

std::string currentStep() { return g_stack.empty() ? std::string{} : g_stack.back(); }

const std::vector<std::string>& lines() { return g_lines; }

void writeHeader(const std::vector<std::pair<std::string, std::string>>& entries) {
    for (const auto& [key, value] : entries) {
        char buf[300];
        std::snprintf(buf, sizeof(buf), "%-14s %s", key.c_str(), value.c_str());
        write(Level::Info, buf);
    }
    write(Level::Info, "---");
}

void resetForTesting() {
    if (g_file.is_open()) {
        g_file.close();
    }
    g_lines.clear();
    g_stack.clear();
    g_depth = 0;
}

}  // namespace bhed::diag

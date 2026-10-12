#include "wache.h"

#include <windows.h>
#include <dbghelp.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <thread>

#include "bhed/diag.h"

namespace bhed::gui::wache {
namespace {

using MiniDumpFn = BOOL(WINAPI*)(HANDLE, DWORD, HANDLE, MINIDUMP_TYPE, PMINIDUMP_EXCEPTION_INFORMATION,
                                 PMINIDUMP_USER_STREAM_INFORMATION, PMINIDUMP_CALLBACK_INFORMATION);

std::wstring g_ordner;
HANDLE g_auftrag = nullptr;   // Ereignis: bitte schreiben
HANDLE g_fertig = nullptr;    // Ereignis: geschrieben
EXCEPTION_POINTERS* g_info = nullptr;
DWORD g_fadenId = 0;
const wchar_t* g_name = L"behaved-absturz.dmp";
std::atomic<bool> g_ergebnis{false};
std::atomic<unsigned long long> g_zaehler{0};
std::atomic<int> g_modal{0};
std::atomic<bool> g_gestartet{false};

// Schreibt das Abbild in den Ordner des Protokolls. dbghelp.dll erst hier
// laden - sie steht in jedem Windows, muss aber nicht beim Start mit.
bool schreibe(const wchar_t* name, EXCEPTION_POINTERS* info, DWORD fadenId) {
    static MiniDumpFn fn = [] {
        HMODULE dll = LoadLibraryW(L"dbghelp.dll");
        return dll != nullptr ? reinterpret_cast<MiniDumpFn>(reinterpret_cast<void*>(GetProcAddress(dll, "MiniDumpWriteDump")))
                              : nullptr;
    }();
    if (fn == nullptr || g_ordner.empty()) {
        return false;
    }
    const std::wstring pfad = g_ordner + L"\\" + name;
    HANDLE datei = CreateFileW(pfad.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (datei == INVALID_HANDLE_VALUE) {
        return false;
    }
    MINIDUMP_EXCEPTION_INFORMATION ei{};
    ei.ThreadId = fadenId;
    ei.ExceptionPointers = info;
    ei.ClientPointers = FALSE;
    // Normal plus Fadeninfo und das, worauf die Stapel zeigen - klein genug
    // zum Verschicken (wenige MB), genug fuer Aufrufkette und Variablen.
    const auto art = static_cast<MINIDUMP_TYPE>(MiniDumpNormal | MiniDumpWithThreadInfo |
                                                MiniDumpWithIndirectlyReferencedMemory);
    const BOOL ok = fn(GetCurrentProcess(), GetCurrentProcessId(), datei, art, info != nullptr ? &ei : nullptr,
                       nullptr, nullptr);
    CloseHandle(datei);
    return ok != FALSE;
}

void schreiber() {
    for (;;) {
        if (WaitForSingleObject(g_auftrag, INFINITE) != WAIT_OBJECT_0) {
            return;
        }
        g_ergebnis = schreibe(g_name, g_info, g_fadenId);
        SetEvent(g_fertig);
    }
}

void waechter(DWORD hauptFaden) {
    using namespace std::chrono;
    unsigned long long zuletzt = g_zaehler.load();
    auto seit = steady_clock::now();
    bool gemeldet = false;
    for (;;) {
        std::this_thread::sleep_for(seconds(2));
        const unsigned long long jetzt = g_zaehler.load();
        // Erst ab dem ersten Bild: das Laden beim Start darf dauern.
        if (jetzt == 0U || jetzt != zuletzt || g_modal.load() > 0) {
            zuletzt = jetzt;
            seit = steady_clock::now();
            continue;
        }
        if (!gemeldet && steady_clock::now() - seit > seconds(30)) {
            gemeldet = true;
            diag::error("HAENGER: der Hauptfaden zeichnet seit 30 s kein Bild mehr - waehrend: " +
                        diag::currentStep());
            // Abbild aller Faeden; ohne Ausnahme-Angaben, der Hauptfaden ist
            // darin enthalten.
            (void)hauptFaden;
            if (schreibe(L"behaved-haenger.dmp", nullptr, 0)) {
                diag::error("  Abbild: behaved-haenger.dmp neben dem Protokoll");
            }
        }
    }
}

}  // namespace

void starten(const std::string& ordner) {
    if (g_gestartet.exchange(true)) {
        return;
    }
    g_ordner = std::filesystem::u8path(ordner).wstring();
    g_auftrag = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    g_fertig = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (g_auftrag != nullptr && g_fertig != nullptr) {
        std::thread(schreiber).detach();
    }
    // Den Waechter nicht im Selbsttest: der haelt die Schleife bei manchen
    // Schritten absichtlich lange an (Kartenpruefung aller Archive).
    if (std::getenv("BHED_EDITORTEST") == nullptr) {
        const DWORD haupt = GetCurrentThreadId();
        std::thread([haupt] { waechter(haupt); }).detach();
    }
}

void absturz(_EXCEPTION_POINTERS* info) {
    if (g_auftrag == nullptr || g_fertig == nullptr) {
        return;
    }
    g_info = info;
    g_fadenId = GetCurrentThreadId();
    g_name = L"behaved-absturz.dmp";
    SetEvent(g_auftrag);
    if (WaitForSingleObject(g_fertig, 20000) == WAIT_OBJECT_0 && g_ergebnis) {
        diag::error("  Abbild: behaved-absturz.dmp neben dem Protokoll");
    }
}

void lebenszeichen() { g_zaehler.fetch_add(1, std::memory_order_relaxed); }
void modalBeginn() { g_modal.fetch_add(1); }
void modalEnde() { g_modal.fetch_sub(1); }

bool probeAbbild() {
    if (g_auftrag == nullptr || g_fertig == nullptr) {
        return false;
    }
    g_info = nullptr;
    g_fadenId = 0;
    g_name = L"behaved-probe.dmp";
    SetEvent(g_auftrag);
    const bool ok = WaitForSingleObject(g_fertig, 20000) == WAIT_OBJECT_0 && g_ergebnis;
    if (ok) {
        DeleteFileW((g_ordner + L"\\behaved-probe.dmp").c_str());
    }
    return ok;
}

}  // namespace bhed::gui::wache

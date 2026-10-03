// update_win32.cpp - der Auto-Updater: GitHub fragen, laden, installieren
//
// Siehe update.h. Alles Netz laeuft in einem Hintergrundfaden; die
// Oberflaeche liest nur eine Kopie des Zustands.
//
// ZUGRIFF: Das Repository (kRepo) ist oeffentlich - gefragt wird OHNE
// Anmeldung, jeder bekommt die Updates ohne GitHub-Konto. Nur wenn GitHub
// das ablehnt (403: zu viele Anfragen ohne Anmeldung, 60 je Stunde), hilft ein
// Schluessel von aussen, der NIE gespeichert wird: GH_TOKEN bzw.
// GITHUB_TOKEN, sonst "gh auth token" (GitHub CLI, falls angemeldet).
//
// INSTALLATION: Die laufende .exe laesst sich unter Windows nicht
// ueberschreiben, wohl aber umbenennen. Sie wird zu "behaved.exe.alt", die
// neue kommt an ihren Platz; beim naechsten Start wird die alte geloescht.
// Jede Datei wird erst als "<name>.neu" geschrieben und dann ersetzt - ein
// abgebrochener Download hinterlaesst keine halbe Datei.

#include "update.h"

#include <windows.h>
#include <winhttp.h>
#include <shellapi.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "app_internal.h"
#include "bhed/fassung.h"
#include "bhed/pk3.h"

namespace bhed::gui::updater {
namespace {

namespace fs = std::filesystem;

// Absichtlich nie freigegeben: der Hintergrundfaden kann beim Beenden noch
// laufen, und dann darf sein Zustand nicht schon zerstoert sein.
struct Geteilt {
    std::mutex m;
    Zustand z;
    bool fensterOffen = false;
    bool hinweisWeg = false;   // "Spaeter" - der Hinweis unten verschwindet bis zum naechsten Fund
    std::atomic<bool> laeuft{false};
};
Geteilt& g() {
    static Geteilt* x = new Geteilt();
    return *x;
}

void setze(const std::function<void(Zustand&)>& f) {
    std::lock_guard<std::mutex> l(g().m);
    f(g().z);
}

// Eine uebersetzte Meldung mit Zahl/Text darin (alle Meldungen in den vier
// Sprachen, wie der Rest der Oberflaeche).
std::string text(Str id, const std::string& a = {}, long long n = 0) {
    char z[512];
    std::snprintf(z, sizeof(z), tr(id), a.c_str(), n);
    return z;
}

std::wstring breit(const std::string& s) {
    if (s.empty()) { return {}; }
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

std::string schmal(const std::wstring& w) {
    if (w.empty()) { return {}; }
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<std::size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}

std::wstring exePfad() {
    std::wstring p(MAX_PATH, L'\0');
    for (;;) {
        const DWORD n = GetModuleFileNameW(nullptr, p.data(), static_cast<DWORD>(p.size()));
        if (n < p.size()) {
            p.resize(n);
            return p;
        }
        p.resize(p.size() * 2);
    }
}

// --- Der Schluessel fuer ein privates Repository -------------------------
std::string ausgabeVon(const std::wstring& programm, const std::wstring& argumente) {
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    HANDLE lesen = nullptr;
    HANDLE schreiben = nullptr;
    if (!CreatePipe(&lesen, &schreiben, &sa, 0)) { return {}; }
    SetHandleInformation(lesen, HANDLE_FLAG_INHERIT, 0);
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = schreiben;
    si.hStdError = schreiben;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    PROCESS_INFORMATION pi{};
    std::wstring zeile = L"\"" + programm + L"\" " + argumente;
    const BOOL ok = CreateProcessW(programm.c_str(), zeile.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr,
                                   nullptr, &si, &pi);
    CloseHandle(schreiben);
    std::string out;
    if (ok) {
        char puffer[512];
        DWORD n = 0;
        while (ReadFile(lesen, puffer, sizeof(puffer), &n, nullptr) && n > 0 && out.size() < 4096) {
            out.append(puffer, n);
        }
        WaitForSingleObject(pi.hProcess, 10000);
        DWORD code = 1;
        GetExitCodeProcess(pi.hProcess, &code);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        if (code != 0) { out.clear(); }
    }
    CloseHandle(lesen);
    while (!out.empty() && (out.back() == '\n' || out.back() == '\r' || out.back() == ' ')) { out.pop_back(); }
    return out;
}

std::string schluessel() {
    for (const char* name : {"GH_TOKEN", "GITHUB_TOKEN"}) {
        char* wert = nullptr;
        std::size_t laenge = 0;
        if (_dupenv_s(&wert, &laenge, name) == 0 && wert != nullptr) {
            std::string s = wert;
            std::free(wert);
            if (!s.empty()) { return s; }
        }
    }
    // GitHub CLI: im PATH oder am ueblichen Ort.
    wchar_t gefunden[MAX_PATH] = {};
    std::wstring gh;
    if (SearchPathW(nullptr, L"gh.exe", nullptr, MAX_PATH, gefunden, nullptr) > 0) {
        gh = gefunden;
    } else {
        for (const wchar_t* k : {L"C:\\Program Files\\GitHub CLI\\gh.exe", L"C:\\Program Files (x86)\\GitHub CLI\\gh.exe"}) {
            if (GetFileAttributesW(k) != INVALID_FILE_ATTRIBUTES) { gh = k; break; }
        }
    }
    if (gh.empty()) { return {}; }
    const std::string t = ausgabeVon(gh, L"auth token");
    // Ein Schluessel hat keine Leerzeichen; alles andere ist eine Fehlermeldung.
    return (t.find(' ') == std::string::npos && t.size() >= 20) ? t : std::string();
}

// --- HTTP ----------------------------------------------------------------
struct Antwort {
    DWORD status = 0;
    std::string body;
    std::wstring weiter;   // Location bei einer Umleitung
    std::string fehler;
};

Antwort holen(const std::wstring& url, const std::vector<std::wstring>& kopf, bool folgen,
              const std::function<void(std::uint64_t, std::uint64_t)>& fortschritt = {}) {
    Antwort a;
    URL_COMPONENTSW teile{};
    teile.dwStructSize = sizeof(teile);
    wchar_t host[256] = {};
    wchar_t pfad[4096] = {};
    teile.lpszHostName = host;
    teile.dwHostNameLength = 256;
    teile.lpszUrlPath = pfad;
    teile.dwUrlPathLength = 4096;
    wchar_t extra[4096] = {};
    teile.lpszExtraInfo = extra;
    teile.dwExtraInfoLength = 4096;
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &teile)) {
        a.fehler = text(Str::UpdErrUrl);
        return a;
    }
    const std::wstring agent = L"BehavEd-Remake/" + breit(bhed::kFassung);
    HINTERNET sitzung = WinHttpOpen(agent.c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                                    WINHTTP_NO_PROXY_BYPASS, 0);
    if (sitzung == nullptr) {
        // Vor Windows 8.1 gibt es AUTOMATIC_PROXY nicht.
        sitzung = WinHttpOpen(agent.c_str(), WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
                              WINHTTP_NO_PROXY_BYPASS, 0);
    }
    if (sitzung == nullptr) {
        a.fehler = text(Str::UpdErrHttp);
        return a;
    }
    WinHttpSetTimeouts(sitzung, 10000, 10000, 15000, 30000);
    HINTERNET verbindung = WinHttpConnect(sitzung, host, teile.nPort, 0);
    const std::wstring ziel = std::wstring(pfad) + extra;
    HINTERNET anfrage = (verbindung == nullptr)
                            ? nullptr
                            : WinHttpOpenRequest(verbindung, L"GET", ziel.c_str(), nullptr, WINHTTP_NO_REFERER,
                                                 WINHTTP_DEFAULT_ACCEPT_TYPES,
                                                 teile.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0);
    if (anfrage == nullptr) {
        a.fehler = text(Str::UpdErrConnect, schmal(host));
    } else {
        if (!folgen) {
            DWORD aus = WINHTTP_DISABLE_REDIRECTS;
            WinHttpSetOption(anfrage, WINHTTP_OPTION_DISABLE_FEATURE, &aus, sizeof(aus));
        }
        std::wstring kopfzeilen;
        for (const std::wstring& k : kopf) { kopfzeilen += k + L"\r\n"; }
        const BOOL gesendet =
            WinHttpSendRequest(anfrage, kopfzeilen.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : kopfzeilen.c_str(),
                               kopfzeilen.empty() ? 0 : static_cast<DWORD>(-1L), WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
            WinHttpReceiveResponse(anfrage, nullptr);
        if (!gesendet) {
            a.fehler = text(Str::UpdErrNoAnswer, schmal(host), static_cast<long long>(GetLastError()));
        } else {
            DWORD groesse = sizeof(a.status);
            WinHttpQueryHeaders(anfrage, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                                &a.status, &groesse, WINHTTP_NO_HEADER_INDEX);
            wchar_t ort[4096] = {};
            DWORD ortGroesse = sizeof(ort);
            if (WinHttpQueryHeaders(anfrage, WINHTTP_QUERY_LOCATION, WINHTTP_HEADER_NAME_BY_INDEX, ort, &ortGroesse,
                                    WINHTTP_NO_HEADER_INDEX)) {
                a.weiter = ort;
            }
            DWORD laengeWert = 0;
            DWORD laengeGroesse = sizeof(laengeWert);
            std::uint64_t gesamt = 0;
            if (WinHttpQueryHeaders(anfrage, WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER,
                                    WINHTTP_HEADER_NAME_BY_INDEX, &laengeWert, &laengeGroesse, WINHTTP_NO_HEADER_INDEX)) {
                gesamt = laengeWert;
            }
            for (;;) {
                DWORD verfuegbar = 0;
                if (!WinHttpQueryDataAvailable(anfrage, &verfuegbar)) {
                    a.fehler = text(Str::UpdErrAbort);
                    break;
                }
                if (verfuegbar == 0) { break; }
                const std::size_t alt = a.body.size();
                a.body.resize(alt + verfuegbar);
                DWORD gelesen = 0;
                if (!WinHttpReadData(anfrage, a.body.data() + alt, verfuegbar, &gelesen)) {
                    a.fehler = text(Str::UpdErrAbort);
                    break;
                }
                a.body.resize(alt + gelesen);
                if (fortschritt) { fortschritt(a.body.size(), gesamt); }
            }
        }
        WinHttpCloseHandle(anfrage);
    }
    if (verbindung != nullptr) { WinHttpCloseHandle(verbindung); }
    WinHttpCloseHandle(sitzung);
    return a;
}

std::vector<std::wstring> apiKopf(const std::string& token, const wchar_t* accept) {
    std::vector<std::wstring> k{std::wstring(L"Accept: ") + accept, L"X-GitHub-Api-Version: 2022-11-28"};
    if (!token.empty()) { k.push_back(L"Authorization: Bearer " + breit(token)); }
    return k;
}

// --- Die beiden Hintergrundauftraege --------------------------------------
void pruefAuftrag() {
    const std::wstring url = L"https://api.github.com/repos/" + breit(bhed::update::kRepo) + L"/releases/latest";
    // Erst ohne Anmeldung - das Release-Repository ist oeffentlich.
    Antwort a = holen(url, apiKopf({}, L"application/vnd.github+json"), true);
    bool angemeldet = false;
    if (a.fehler.empty() && (a.status == 403 || a.status == 429)) {
        // Zu viele Anfragen ohne Anmeldung: mit Schluessel, falls es einen gibt.
        const std::string token = schluessel();
        if (!token.empty()) {
            a = holen(url, apiKopf(token, L"application/vnd.github+json"), true);
            angemeldet = true;
        }
    }
    diag::detail("Update: GitHub (" + std::string(bhed::update::kRepo) + ") antwortet " + std::to_string(a.status) +
                 (angemeldet ? " (angemeldet)" : " (ohne Anmeldung)") + (a.fehler.empty() ? "" : " - " + a.fehler));
    if (!a.fehler.empty()) {
        setze([&](Zustand& z) { z.stand = Stand::Fehler; z.meldung = a.fehler; });
        return;
    }
    if (a.status == 404) {
        setze([&](Zustand& z) { z.stand = Stand::Fehler; z.meldung = tr(Str::UpdNoRelease); });
        return;
    }
    if (a.status == 401 || a.status == 403 || a.status == 429) {
        setze([&](Zustand& z) { z.stand = Stand::Fehler; z.meldung = tr(Str::UpdNoAccess); });
        return;
    }
    bhed::update::Release r;
    std::string fehler;
    if (a.status != 200 || !bhed::update::parseRelease(a.body, r, &fehler)) {
        setze([&](Zustand& z) {
            z.stand = Stand::Fehler;
            z.meldung = text(Str::UpdErrBadAnswer, fehler, static_cast<long long>(a.status));
        });
        return;
    }
    const bool neuer = bhed::update::istNeuer(r.tag, lokaleFassung());
    diag::detail("Update: neuestes Release " + r.tag + ", installiert " + lokaleFassung() + (neuer ? " -> NEUER" : " -> aktuell"));
    setze([&](Zustand& z) {
        z.release = r;
        z.stand = neuer ? Stand::Verfuegbar : Stand::Aktuell;
    });
    if (neuer) {
        std::lock_guard<std::mutex> l(g().m);
        g().hinweisWeg = false;
    }
}

void installAuftrag(bhed::update::Release r) {
    const bhed::update::Asset* asset = bhed::update::zipAsset(r);
    if (asset == nullptr) {
        setze([&](Zustand& z) { z.stand = Stand::Fehler; z.meldung = tr(Str::UpdNoZip); });
        return;
    }
    const auto fortschritt = [&](std::uint64_t n, std::uint64_t gesamt) {
        const double teil = gesamt > 0 ? static_cast<double>(n) / static_cast<double>(gesamt)
                                       : (asset->size > 0 ? static_cast<double>(n) / static_cast<double>(asset->size) : 0.0);
        setze([&](Zustand& z) { z.fortschritt = std::min(1.0, teil); });
    };
    // Oeffentlich: der normale Download-Link, ohne Anmeldung.
    Antwort a;
    if (!asset->downloadUrl.empty()) {
        a = holen(breit(asset->downloadUrl), {}, true, fortschritt);
    }
    // Sonst (oder wenn das abgelehnt wird) ueber die API mit Schluessel: sie
    // leitet auf einen signierten Speicherort um, und DORT darf der
    // Schluessel nicht mitgehen - deshalb von Hand folgen.
    if ((asset->downloadUrl.empty() || a.status == 401 || a.status == 403 || a.status == 404) && !asset->apiUrl.empty()) {
        const std::string token = schluessel();
        if (!token.empty()) {
            a = holen(breit(asset->apiUrl), apiKopf(token, L"application/octet-stream"), false);
            if (a.fehler.empty() && (a.status == 301 || a.status == 302 || a.status == 307) && !a.weiter.empty()) {
                a = holen(a.weiter, {}, true, fortschritt);
            }
        }
    }
    if (!a.fehler.empty() || a.status != 200 || a.body.size() < 22) {
        setze([&](Zustand& z) {
            z.stand = Stand::Fehler;
            z.meldung = a.fehler.empty() ? "HTTP " + std::to_string(a.status) : a.fehler;
        });
        return;
    }
    setze([&](Zustand& z) { z.fortschritt = 1.0; });
    // Das .zip ablegen und mit dem pk3-Leser oeffnen (dasselbe Format).
    std::error_code ec;
    wchar_t temp[MAX_PATH] = {};
    GetTempPathW(MAX_PATH, temp);
    const fs::path ablage = fs::path(temp) / L"behaved_update";
    fs::create_directories(ablage, ec);
    const fs::path zipDatei = ablage / breit(asset->name);
    {
        std::ofstream f(zipDatei, std::ios::binary);
        f.write(a.body.data(), static_cast<std::streamsize>(a.body.size()));
    }
    Pk3 archiv;
    std::string fehler;
    if (!readPk3Directory(schmal(zipDatei.wstring()), archiv, &fehler)) {
        setze([&](Zustand& z) { z.stand = Stand::Fehler; z.meldung = text(Str::UpdErrZip, fehler); });
        return;
    }
    std::vector<std::string> namen;
    for (const Pk3Entry& e : archiv.entries) { namen.push_back(e.name); }
    const std::string ober = bhed::update::gemeinsamerOrdner(namen);
    const fs::path exe = exePfad();
    const fs::path ordner = exe.parent_path();
    int ersetzt = 0;
    std::string problem;
    for (const Pk3Entry& e : archiv.entries) {
        const std::string rel = bhed::update::zielImOrdner(e.name, ober);
        if (rel.empty()) { continue; }
        std::string inhalt;
        if (!readPk3File(archiv, e, inhalt, &fehler)) {
            problem = e.name + ": " + fehler;
            break;
        }
        const fs::path ziel = ordner / fs::u8path(rel);
        fs::create_directories(ziel.parent_path(), ec);
        fs::path neu = ziel;
        neu += L".neu";
        {
            std::ofstream f(neu, std::ios::binary | std::ios::trunc);
            f.write(inhalt.data(), static_cast<std::streamsize>(inhalt.size()));
            if (!f) {
                problem = text(Str::UpdErrWrite, rel);
                break;
            }
        }
        const bool istExe = _wcsicmp(ziel.filename().c_str(), exe.filename().c_str()) == 0;
        if (istExe) {
            // Die laufende .exe nur umbenennen - ueberschreiben geht nicht.
            fs::path alt = exe;
            alt += L".alt";
            DeleteFileW(alt.c_str());
            if (!MoveFileExW(exe.c_str(), alt.c_str(), MOVEFILE_REPLACE_EXISTING) ||
                !MoveFileExW(neu.c_str(), exe.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
                MoveFileExW(alt.c_str(), exe.c_str(), MOVEFILE_REPLACE_EXISTING);   // zurueck
                problem = text(Str::UpdErrReplace, rel, static_cast<long long>(GetLastError()));
                break;
            }
        } else if (!MoveFileExW(neu.c_str(), ziel.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            problem = text(Str::UpdErrReplace, rel, static_cast<long long>(GetLastError()));
            break;
        }
        ++ersetzt;
    }
    diag::detail("Update: " + std::to_string(ersetzt) + " Dateien ersetzt" + (problem.empty() ? "" : " - " + problem));
    setze([&](Zustand& z) {
        z.dateien = ersetzt;
        if (!problem.empty()) {
            z.stand = Stand::Fehler;
            z.meldung = problem;
        } else {
            z.stand = Stand::Fertig;
        }
    });
}

void imHintergrund(std::function<void()> auftrag) {
    bool erwartet = false;
    if (!g().laeuft.compare_exchange_strong(erwartet, true)) {
        return;   // es laeuft schon einer
    }
    std::thread([auftrag = std::move(auftrag)] {
        auftrag();
        g().laeuft = false;
    }).detach();
}

}  // namespace

std::string lokaleFassung() {
    char* wert = nullptr;
    std::size_t laenge = 0;
    if (_dupenv_s(&wert, &laenge, "BHED_UPDATE_LOKAL") == 0 && wert != nullptr) {
        std::string s = wert;
        std::free(wert);
        if (!s.empty()) { return s; }
    }
    return bhed::kFassung;
}

Zustand zustand() {
    std::lock_guard<std::mutex> l(g().m);
    return g().z;
}

void pruefen(bool stumm) {
    setze([](Zustand& z) {
        z.stand = Stand::Pruefe;
        z.meldung.clear();
    });
    if (!stumm) {
        std::lock_guard<std::mutex> l(g().m);
        g().fensterOffen = true;
    }
    imHintergrund(pruefAuftrag);
}

void installieren() {
    const Zustand z = zustand();
    if (z.stand != Stand::Verfuegbar && z.stand != Stand::Fehler) { return; }
    if (z.release.tag.empty()) { return; }
    setze([](Zustand& x) {
        x.stand = Stand::Laedt;
        x.fortschritt = 0.0;
        x.meldung.clear();
    });
    imHintergrund([r = z.release] { installAuftrag(r); });
}

void beimStart() {
    // Reste eines frueheren Updates: die alte .exe und das geladene .zip.
    std::wstring alt = exePfad() + L".alt";
    DeleteFileW(alt.c_str());
    wchar_t temp[MAX_PATH] = {};
    GetTempPathW(MAX_PATH, temp);
    std::error_code ec;
    fs::remove_all(fs::path(temp) / L"behaved_update", ec);
    // Im Selbsttest nie von selbst ins Netz.
    char* test = nullptr;
    std::size_t laenge = 0;
    const bool selbsttest = _dupenv_s(&test, &laenge, "BHED_EDITORTEST") == 0 && test != nullptr;
    std::free(test);
    if (!selbsttest && g_app != nullptr && g_app->settings.updateCheck) {
        pruefen(true);
    }
}

// Vorher: erst confirmQuit(), und nur wenn das SOFORT true war, den neuen
// Prozess starten. Mit einem ungesicherten Reiter (oder der Frage "Exit?")
// ist es das nie - die Fragen kamen, und ihr "Nein"/"Ja" beendete das
// Programm, OHNE neu zu starten (Code-Pruefung 03.10., im Selbsttest
// "pruefung" nachgestellt). Jetzt laeuft "Neu starten" durch denselben
// Ablauf wie "Exit" und startet erst, wenn wirklich beendet wird.
namespace {
bool g_neustartVorgemerkt = false;
}

void neuStarten() {
    g_neustartVorgemerkt = true;
    g_app->wantQuit = true;
}

void neustartVergessen() { g_neustartVorgemerkt = false; }

void neustartWennVorgemerkt() {
    if (!g_neustartVorgemerkt) { return; }
    g_neustartVorgemerkt = false;
    const std::wstring exe = exePfad();
    std::wstring zeile = L"\"" + exe + L"\" --nach-update=" + std::to_wstring(GetCurrentProcessId());
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (CreateProcessW(exe.c_str(), zeile.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        diag::detail("Update: Neustart angestossen");
    } else {
        const std::string m = text(Str::UpdErrRestart, {}, static_cast<long long>(GetLastError()));
        diag::detail("Update: " + m);
        platform::showError(m, "BehavEd");
    }
}

bool warteAufVorgaenger(const std::string& argument) {
    const std::string vorsatz = "--nach-update=";
    if (argument.compare(0, vorsatz.size(), vorsatz) != 0) { return false; }
    const DWORD pid = static_cast<DWORD>(std::strtoul(argument.c_str() + vorsatz.size(), nullptr, 10));
    if (HANDLE h = OpenProcess(SYNCHRONIZE, FALSE, pid)) {
        WaitForSingleObject(h, 15000);
        CloseHandle(h);
    }
    return true;
}

void oeffneFenster() {
    std::lock_guard<std::mutex> l(g().m);
    g().fensterOffen = true;
}

bool fensterOffen() {
    std::lock_guard<std::mutex> l(g().m);
    return g().fensterOffen;
}

void zeichneStatus() {
    const Zustand z = zustand();
    bool weg = false;
    {
        std::lock_guard<std::mutex> l(g().m);
        weg = g().hinweisWeg;
    }
    if ((z.stand != Stand::Verfuegbar && z.stand != Stand::Fertig) || (weg && z.stand == Stand::Verfuegbar)) {
        return;
    }
    char text[200];
    if (z.stand == Stand::Verfuegbar) {
        std::snprintf(text, sizeof(text), tr(Str::UpdStatusAvail), z.release.tag.c_str());
    } else {
        std::snprintf(text, sizeof(text), "%s", tr(Str::UpdStatusDone));
    }
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{0, 0, 0, 0});
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4{0.45F, 0.85F, 0.45F, 1.0F});
    if (ImGui::SmallButton(text)) {
        oeffneFenster();
    }
    ImGui::PopStyleColor(2);
    ImGui::SameLine();
}

void zeichneFenster() {
    bool offen = fensterOffen();
    if (!offen) { return; }
    const Zustand z = zustand();
    ImGui::SetNextWindowSize(ImVec2{ImGui::GetFontSize() * 34.0F, 0.0F}, ImGuiCond_Appearing);
    if (ImGui::Begin((std::string(tr(Str::UpdTitle)) + "###update").c_str(), &offen,
                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize)) {
        char z1[256];
        std::snprintf(z1, sizeof(z1), tr(Str::UpdInstalled), lokaleFassung().c_str());
        ImGui::TextDisabled("%s", z1);
        switch (z.stand) {
            case Stand::Ruhe:
            case Stand::Pruefe:
                ImGui::TextUnformatted(tr(Str::UpdChecking));
                break;
            case Stand::Aktuell:
                ImGui::TextUnformatted(tr(Str::UpdCurrent));
                break;
            case Stand::Verfuegbar:
            case Stand::Laedt:
            case Stand::Fertig: {
                char z2[256];
                std::snprintf(z2, sizeof(z2), tr(Str::UpdAvailable), z.release.tag.c_str());
                ImGui::TextUnformatted(z2);
                if (!z.release.body.empty()) {
                    ImGui::TextDisabled("%s", tr(Str::UpdNotes));
                    if (ImGui::BeginChild("##notizen", ImVec2{ImGui::GetFontSize() * 32.0F, ImGui::GetFontSize() * 12.0F},
                                          ImGuiChildFlags_Borders)) {
                        ImGui::PushTextWrapPos(0.0F);
                        ImGui::TextUnformatted(z.release.body.c_str());
                        ImGui::PopTextWrapPos();
                    }
                    ImGui::EndChild();
                }
                if (z.stand == Stand::Laedt) {
                    ImGui::ProgressBar(static_cast<float>(z.fortschritt), ImVec2{-FLT_MIN, 0.0F});
                } else if (z.stand == Stand::Fertig) {
                    char z3[200];
                    std::snprintf(z3, sizeof(z3), tr(Str::UpdDone), z.dateien);
                    ImGui::TextUnformatted(z3);
                }
                break;
            }
            case Stand::Fehler: {
                char z4[600];
                std::snprintf(z4, sizeof(z4), tr(Str::UpdError), z.meldung.c_str());
                ImGui::PushTextWrapPos(ImGui::GetFontSize() * 32.0F);
                ImGui::TextUnformatted(z4);
                ImGui::PopTextWrapPos();
                break;
            }
        }
        ImGui::Separator();
        if (z.stand == Stand::Verfuegbar) {
            if (ImGui::Button(tr(Str::UpdInstall))) { installieren(); }
            ImGui::SameLine();
        } else if (z.stand == Stand::Fertig) {
            if (ImGui::Button(tr(Str::UpdRestart))) { neuStarten(); }
            ImGui::SameLine();
        } else if (z.stand == Stand::Fehler || z.stand == Stand::Aktuell) {
            if (ImGui::Button(tr(Str::UpdRetry))) { pruefen(false); }
            ImGui::SameLine();
        }
        if (!z.release.htmlUrl.empty() && ImGui::Button(tr(Str::UpdOpenPage))) {
            ShellExecuteW(nullptr, L"open", breit(z.release.htmlUrl).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        }
        if (!z.release.htmlUrl.empty()) { ImGui::SameLine(); }
        if (ImGui::Button(tr(Str::UpdLater))) {
            offen = false;
            std::lock_guard<std::mutex> l(g().m);
            g().hinweisWeg = (z.stand == Stand::Verfuegbar);
        }
    }
    ImGui::End();
    std::lock_guard<std::mutex> l(g().m);
    g().fensterOffen = offen;
}

}  // namespace bhed::gui::updater

// main_win32.cpp - Fenster, Nachrichtenschleife, Schriften, Rueckfall
//
// Die DPI-Staffelung ist aus efxed uebernommen und dort hart erarbeitet:
// jede dieser Funktionen fehlt auf irgendeiner Windows-Fassung, und ein
// direkter Aufruf laesst das Programm nicht starten statt eine Meldung zu
// zeigen. Deshalb durchweg GetProcAddress mit Rueckfall.
//
// Die Schriftbehandlung ist dagegen NEU und viel kuerzer als bei efxed:
// Dear ImGui 1.92 laedt Glyphen bei Bedarf nach. Die Zwei-Stufen-Loesung
// (erst die Zeichen der Sprachnamen, dann der volle CJK-Bereich) und der
// beruehmte Zeiger auf den Glyphbereich, der zu frueh starb (imgui#2052),
// sind damit gegenstandslos.

#include "backend.h"
#include "gpumap.h"

#include "bhed/gpustate.h"
#include "bhed/diag.h"
#include "bhed/fassung.h"
#include "bhed/edit.h"
#include "bhed/i18n.h"
#include "bhed/theme.h"

#include "imgui.h"
#include "imgui_impl_win32.h"

#include <windows.h>

#include "platform.h"

#include <cstdio>
#include <cstdlib>
#include <vector>
#include <string>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

namespace bhed::gui {
struct App;
App* createApp();
void destroyApp(App* a);
void setApp(App* a);
void draw();
bool wantsQuit();
bool confirmQuit();
void selbsttestVorBild();
std::string windowTitle();
// Schreibt Geraet, Zustaende, Zaehler, Zeiten und die Meldungen der
// Debugschicht ins Protokoll. Ohne Zutun - beim Beenden und beim Absturz.
void schreibeBericht();
void loadSettings(App* a);
void resetIconTexture();
void setDpiScale(App* a, float s);
void reopenLastIfWanted(App* a, const std::string& kommandozeile);
void defaultWindowSize(int* w, int* h);
// Lage und Zustand des Programmfensters, als "showCmd l t r b".
//
// Zwei schmale Funktionen statt `app_internal.h` hier einzubinden: dieser
// Uebersetzungsabschnitt kennt `App` absichtlich nicht, und ein ganzer
// Kopf mitsamt imgui_internal.h nur fuer zwei Zeichenketten waere zu viel.
std::string gemerkteFensterlage();
void merkeFensterlage(const std::string& wert);
bool loadModel(App* a, const std::string& dataDir, std::string* error);
const theme::Theme* currentTheme(App* a);
void setThemeById(App* a, const char* id);
float uiScale(App* a);
void setUiScale(App* a, float s);
}  // namespace bhed::gui

namespace {

UINT windowDpi(HWND hwnd);

bool g_quit = false;
int g_width = 0;
int g_height = 0;
bool g_fontsDirty = true;

LRESULT WINAPI wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    // Im Selbsttest die Fokusmeldungen NICHT an ImGui: ein kurzer
    // Fokusaussetzer (Fenster im Hintergrund, Klick woanders) setzt dort die
    // Maustasten zurueck und bricht einen simulierten Klick oder Zug ab. Der
    // Test speist Maus und Tasten selbst ein und braucht keinen Fokus.
    static const bool imSelbsttestFokus = std::getenv("BHED_EDITORTEST") != nullptr;
    if (imSelbsttestFokus && (msg == WM_SETFOCUS || msg == WM_KILLFOCUS)) {
        return ::DefWindowProcW(hwnd, msg, wParam, lParam);
    }
    if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam) != 0) {
        return 1;
    }
    switch (msg) {
        case WM_SIZE:
            if (wParam != SIZE_MINIMIZED) {
                g_width = LOWORD(lParam);
                g_height = HIWORD(lParam);
                bhed::render::resize(g_width, g_height);
            }
            return 0;
        case WM_DPICHANGED: {
            const RECT* r = reinterpret_cast<RECT*>(lParam);
            SetWindowPos(hwnd, nullptr, r->left, r->top, r->right - r->left,
                         r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
            // Kein Neubau der Schriften: seit 1.92 rastert ImGui dynamisch,
            // der neue Massstab wird im naechsten Bild uebernommen.
            return 0;
        }
        case WM_GETMINMAXINFO: {
            // Kleiner als das hat keinen Sinn: die Knopfspalte und die
            // Listen brauchen Platz, sonst ueberlappt alles.
            auto* mm = reinterpret_cast<MINMAXINFO*>(lParam);
            const float k = static_cast<float>(windowDpi(hwnd)) / 96.0F;
            mm->ptMinTrackSize.x = static_cast<LONG>(900.0F * k);
            mm->ptMinTrackSize.y = static_cast<LONG>(600.0F * k);
            return 0;
        }
        case WM_SYSCOMMAND:
            if ((wParam & 0xfff0) == SC_KEYMENU) {
                return 0;   // Alt-Menue abschalten
            }
            break;
        case WM_CLOSE:
            // Das Kreuz oben rechts. Vorher schloss es sofort - auch mit
            // ungesicherten Aenderungen. Jetzt fragt es dasselbe wie der
            // Beenden-Knopf; sagt der Anwender "Abbrechen", bleibt das
            // Fenster offen.
            if (!bhed::gui::confirmQuit()) {
                return 0;
            }
            // Lage und Zustand festhalten, SOLANGE es das Fenster noch gibt.
            //
            // Bei WM_DESTROY ist es dafuer zu spaet. Gespeichert wird die
            // Zeichenkette nur; die Datei schreibt `destroyApp` weiter unten
            // wie bisher.
            //
            // `length` MUSS gesetzt sein, sonst schlaegt der Aufruf fehl.
            {
                WINDOWPLACEMENT wp{};
                wp.length = sizeof(wp);
                if (GetWindowPlacement(hwnd, &wp) != 0) {
                    char zp[160];
                    std::snprintf(zp, sizeof(zp), "%u %ld %ld %ld %ld",
                                  wp.showCmd,
                                  static_cast<long>(wp.rcNormalPosition.left),
                                  static_cast<long>(wp.rcNormalPosition.top),
                                  static_cast<long>(wp.rcNormalPosition.right),
                                  static_cast<long>(wp.rcNormalPosition.bottom));
                    bhed::gui::merkeFensterlage(zp);
                    bhed::diag::detail(std::string("Fensterlage gesichert: \"") +
                                       zp + "\"");
                } else {
                    bhed::diag::detail(
                        "Fensterlage gesichert: GetWindowPlacement scheiterte");
                }
            }
            break;
        case WM_DESTROY:
            g_quit = true;
            PostQuitMessage(0);
            return 0;
        default:
            break;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

std::string toUtf8(const std::wstring& text) {
    if (text.empty()) {
        return {};
    }
    const int n = WideCharToMultiByte(CP_UTF8, 0, text.c_str(),
                                      static_cast<int>(text.size()), nullptr, 0,
                                      nullptr, nullptr);
    std::string out(static_cast<std::size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
                        out.data(), n, nullptr, nullptr);
    return out;
}

std::string exeDirectory() {
    wchar_t buffer[MAX_PATH]{};
    const DWORD n = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    std::wstring path(buffer, n);
    const std::size_t cut = path.find_last_of(L"\\/");
    return toUtf8(cut == std::wstring::npos ? std::wstring{} : path.substr(0, cut));
}

std::string systemLocale() {
    wchar_t name[LOCALE_NAME_MAX_LENGTH]{};
    if (GetUserDefaultLocaleName(name, LOCALE_NAME_MAX_LENGTH) > 0) {
        return toUtf8(name);
    }
    return "en";
}

// GetDpiForWindow gibt es erst seit Windows 10. Direkt aufgerufen waere es
// derselbe harte Ausfall wie bei SetProcessDpiAwarenessContext: das Programm
// laedt nicht. Der Rueckfall ueber GetDeviceCaps kennt nur den Hauptbildschirm,
// aber besser als gar nicht starten.
UINT windowDpi(HWND hwnd) {
    if (HMODULE user32 = GetModuleHandleW(L"user32.dll")) {
        using GetDpi = UINT(WINAPI*)(HWND);
        if (auto fn = reinterpret_cast<GetDpi>(
                reinterpret_cast<void*>(GetProcAddress(user32, "GetDpiForWindow")))) {
            const UINT dpi = fn(hwnd);
            if (dpi > 0) {
                return dpi;
            }
        }
    }
    const HDC dc = GetDC(nullptr);
    const int dpi = (dc != nullptr) ? GetDeviceCaps(dc, LOGPIXELSX) : 96;
    if (dc != nullptr) {
        ReleaseDC(nullptr, dc);
    }
    return dpi > 0 ? static_cast<UINT>(dpi) : 96U;
}

void announceDpiAwareness() {
    if (HMODULE user32 = GetModuleHandleW(L"user32.dll")) {
        using SetContext = BOOL(WINAPI*)(DPI_AWARENESS_CONTEXT);
        if (auto fn = reinterpret_cast<SetContext>(reinterpret_cast<void*>(
                GetProcAddress(user32, "SetProcessDpiAwarenessContext")))) {
            if (fn(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2) != 0) {
                return;                      // Windows 10 1703 und neuer
            }
        }
    }
    if (HMODULE shcore = LoadLibraryW(L"shcore.dll")) {
        using SetAwareness = HRESULT(WINAPI*)(int);
        if (auto fn = reinterpret_cast<SetAwareness>(reinterpret_cast<void*>(
                GetProcAddress(shcore, "SetProcessDpiAwareness")))) {
            fn(2);                           // Windows 8.1
            FreeLibrary(shcore);
            return;
        }
        FreeLibrary(shcore);
    }
    if (HMODULE user32 = GetModuleHandleW(L"user32.dll")) {
        using SetAware = BOOL(WINAPI*)();
        if (auto fn = reinterpret_cast<SetAware>(reinterpret_cast<void*>(
                GetProcAddress(user32, "SetProcessDPIAware")))) {
            fn();                            // Windows Vista
        }
    }
}

bool addFontIfPresent(ImGuiIO& io, const char* path, float size, bool merge) {
    if (GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES) {
        return false;
    }
    ImFontConfig config;
    config.MergeMode = merge;
    return io.Fonts->AddFontFromFileTTF(path, size, &config) != nullptr;
}

// Ab ImGui 1.92 braucht das weder Glyphbereiche noch einen Neubau bei
// Massstabsaenderungen.
//
// Bei efxed musste eine CJK-Schrift in zwei Stufen geladen werden: erst nur
// die Zeichen der Sprachnamen, damit das Sprachmenue ueberhaupt lesbar war,
// dann bei Bedarf der volle Bereich. Der Bereichsvektor musste dabei laenger
// leben als der Aufruf, weil ImGui ihn nicht kopiert - eine beliebte Falle.
// Beides faellt weg: 1.92 laedt jede Glyphe nach, wenn sie gebraucht wird.
//
// Und ganz wichtig: die Schrift wird in der GRUNDGROESSE geladen, nicht in
// der Zielgroesse. Der Massstab kommt ueber style.FontScaleDpi. Wer beides
// tut, multipliziert doppelt. Deshalb gibt es hier auch kein dpiScale mehr
// und keinen Neubau bei WM_DPICHANGED.
void buildFonts() {
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Clear();
    const float size = 16.0F;

    if (!addFontIfPresent(io, "C:\\Windows\\Fonts\\segoeui.ttf", size, false)) {
        io.Fonts->AddFontDefault();
    }
    // Eine Schrift mit CJK-Zeichen dazumischen, damit 中文 und 日本語 im
    // Sprachmenue lesbar sind - unabhaengig davon, welche Sprache gerade
    // eingestellt ist.
    const char* cjk[] = {"C:\\Windows\\Fonts\\msyh.ttc",     // Microsoft YaHei
                         "C:\\Windows\\Fonts\\meiryo.ttc",   // Meiryo
                         "C:\\Windows\\Fonts\\msgothic.ttc"};
    for (const char* path : cjk) {
        if (addFontIfPresent(io, path, size, true)) {
            break;
        }
    }
}

bool runSession(bhed::render::Backend preferred, HWND hwnd, bhed::gui::App* app) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;

    // --- Die Klammer umschliesst den START, nicht die Sitzung ------------
    //
    // `Step` misst bis zu seinem Lebensende. Er stand hier im Rumpf von
    // `runSession` und lebte damit bis zum Beenden des Programms - die
    // Nachrichtenschleife inbegriffen. Im Protokoll stand deshalb
    //
    //     < Grafikschnittstelle starten: Direct3D 11 (91002.6 ms)
    //
    // und das waren 91 Sekunden Benutzung, nicht 91 Sekunden Start.
    //
    // Dieselbe Fehlerart wie eine Zahl, die etwas anderes misst als ihr
    // Name sagt - nur hier im Protokoll statt in der Seitenleiste. Wer die
    // Ladezeit sucht, findet sonst eine Zahl, die mit ihr nichts zu tun hat.
    {
        bhed::diag::Step step(std::string("Grafikschnittstelle starten: ") +
                              bhed::render::backendName(preferred));
        if (!bhed::render::init(preferred,
                                reinterpret_cast<HWNDHandle>(hwnd))) {
            step.fail("nicht verfuegbar");
            ImGui::DestroyContext();
            return false;
        }
        ImGui_ImplWin32_Init(hwnd);
        g_fontsDirty = true;
        // Die Symboltextur gehoert der Grafikschnittstelle. Beim Wechsel von
        // Direct3D auf OpenGL ist die alte weg.
        bhed::gui::resetIconTexture();
    }
    // Was hier drunter im Protokoll steht - das Laden der Mission - gehoert
    // ohnehin nicht zum Starten der Schnittstelle. Es stand nur eingerueckt
    // darunter, weil die Klammer offen war.
    bhed::diag::info("Nachrichtenschleife laeuft");

    while (!g_quit) {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE) != 0) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
            if (msg.message == WM_QUIT) {
                g_quit = true;
            }
        }
        if (g_quit) {
            break;
        }
        if (g_fontsDirty) {
            bhed::diag::Step fontStep("Schriften bauen");
            buildFonts();
            g_fontsDirty = false;
        }
        // Der Bildschirmmassstab kann sich beim Verschieben auf einen anderen
        // Bildschirm aendern. Die Schrift wird deswegen NICHT neu gebaut -
        // ImGui rastert sie in der neuen Groesse selbst.
        bhed::gui::setDpiScale(app, static_cast<float>(windowDpi(hwnd)) / 96.0F);

        bhed::render::newFrame();
        ImGui_ImplWin32_NewFrame();
        // Nur im Selbsttest wirksam: die simulierte Maus nach der echten.
        bhed::gui::selbsttestVorBild();
        ImGui::NewFrame();
        bhed::gui::draw();
        // Hat der Beenden-Knopf gedrueckt? Er kennt kein Fenster, deshalb
        // fragt die Fensterschicht nach.
        if (bhed::gui::wantsQuit()) {
            PostMessageW(hwnd, WM_CLOSE, 0, 0);
        }

        // Den Titel nachfuehren - aber nur, wenn er sich geaendert hat.
        //
        // SetWindowTextW bei jedem Bild waere Verschwendung: es schickt
        // eine Nachricht und laesst die Taskleiste neu zeichnen.
        {
            static std::string letzterTitel;
            const std::string jetzt = bhed::gui::windowTitle();
            if (jetzt != letzterTitel) {
                letzterTitel = jetzt;
                // Nach UTF-16 wandeln - der Titel kann Umlaute aus einem
                // Dateinamen enthalten. toWide steht in platform_win32.cpp
                // und ist von hier nicht erreichbar; hier reicht der eine
                // Aufruf.
                const int n = MultiByteToWideChar(
                    CP_UTF8, 0, jetzt.c_str(),
                    static_cast<int>(jetzt.size()), nullptr, 0);
                std::wstring w(static_cast<std::size_t>(n), L'\0');
                MultiByteToWideChar(CP_UTF8, 0, jetzt.c_str(),
                                    static_cast<int>(jetzt.size()), w.data(), n);
                SetWindowTextW(hwnd, w.c_str());
            }
        }
        ImGui::Render();

        const bhed::theme::Theme* t = bhed::gui::currentTheme(app);
        const bhed::theme::Color bg = (t != nullptr) ? t->window
                                                     : bhed::theme::Color{0.2F, 0.2F, 0.2F, 1.0F};
        bhed::render::present(bg.r, bg.g, bg.b);
    }

    // Der Bericht VOR dem Aufraeumen: danach gibt es kein Geraet mehr, und
    // Adaptername, Merkmalsstufe und die Warteschlange der Debugschicht
    // haengen daran. Ohne Zutun - es gibt keinen Knopf dafuer.
    bhed::gui::schreibeBericht();

    ImGui_ImplWin32_Shutdown();
    // VOR render::shutdown(): die Puffer und Texturen des GPU-Wegs gehoeren
    // dem Geraet. Wer sie danach freigibt, gibt sie an ein Geraet zurueck,
    // das es nicht mehr gibt.
    bhed::gpu::shutdown();
    bhed::render::shutdown();
    ImGui::DestroyContext();
    return true;
}

// Wer einen Absturz aufschreibt.
//
// Ein Absturz ohne Spur ist nicht zu untersuchen; man kann nur raten. Der
// Filter schreibt Fehlercode, Adresse und den gerade laufenden Schritt und
// laesst den Absturz dann WEITERLAUFEN. Er repariert nichts und soll es auch
// nicht: ein Programm, das nach einem Speicherfehler weitermacht, richtet
// mehr Schaden an, als es verhindert.
//
// SetUnhandledExceptionFilter gibt es seit Windows XP.
LONG WINAPI logCrash(EXCEPTION_POINTERS* info) {
    if (info != nullptr && info->ExceptionRecord != nullptr) {
        char text[256];
        std::snprintf(text, sizeof(text), "ABSTURZ code 0x%08lX bei 0x%p",
                      static_cast<unsigned long>(info->ExceptionRecord->ExceptionCode),
                      info->ExceptionRecord->ExceptionAddress);
        bhed::diag::error(text);

        // Der Bericht auch beim ABSTURZ - dort braucht man ihn am meisten,
        // und dort kann niemand mehr etwas anklicken. Er kommt vor den
        // Einzelheiten, damit er im Protokoll oben steht, falls der Rest
        // nicht mehr geschrieben wird.
        bhed::gui::schreibeBericht();

        // Bei einem Speicherfehler steht in den Angaben, ob gelesen oder
        // geschrieben wurde und an welcher Adresse. Genau das braucht man.
        if (info->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION &&
            info->ExceptionRecord->NumberParameters >= 2) {
            const ULONG_PTR kind = info->ExceptionRecord->ExceptionInformation[0];
            std::snprintf(text, sizeof(text), "  %s an Adresse 0x%p",
                          kind == 0 ? "lesend" : (kind == 1 ? "schreibend" : "ausfuehrend"),
                          reinterpret_cast<void*>(
                              info->ExceptionRecord->ExceptionInformation[1]));
            bhed::diag::error(text);
        }
        // Wo war der GPU-Weg? Siehe bhed::gpu::setzeSchritt.
        const char* gs = bhed::gpu::letzterSchritt();
        if (gs != nullptr && gs[0] != '\0') {
            std::snprintf(text, sizeof(text), "  GPU-Weg zuletzt: %s", gs);
            bhed::diag::error(text);
        }
        const std::string step = bhed::diag::currentStep();
        if (!step.empty()) {
            bhed::diag::error("  waehrend: " + step);
        }
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

std::string windowsVersion() {
    // GetVersionEx luegt seit Windows 8.1 ohne Manifest. Die Fassung steht
    // aber in der Dateiversion von kernel32.dll, und die luegt nicht.
    char text[64] = "unbekannt";
    const DWORD size = GetFileVersionInfoSizeA("kernel32.dll", nullptr);
    if (size != 0) {
        std::vector<unsigned char> buffer(size);
        if (GetFileVersionInfoA("kernel32.dll", 0, size, buffer.data()) != 0) {
            VS_FIXEDFILEINFO* fixed = nullptr;
            UINT len = 0;
            if (VerQueryValueA(buffer.data(), "\\", reinterpret_cast<void**>(&fixed), &len) != 0 &&
                fixed != nullptr) {
                std::snprintf(text, sizeof(text), "%u.%u.%u",
                              HIWORD(fixed->dwProductVersionMS),
                              LOWORD(fixed->dwProductVersionMS),
                              HIWORD(fixed->dwProductVersionLS));
            }
        }
    }
    return text;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    // Skalierung anmelden, bevor irgendein Fenster entsteht. Danach ist es
    // zu spaet und alles erscheint verwaschen.
    announceDpiAwareness();

    // COM anmelden, bevor irgendein Dateidialog aufgeht.
    //
    // Der Common Item Dialog ist ein COM-Objekt: ohne CoInitializeEx gibt
    // CoCreateInstance CO_E_NOTINITIALIZED zurueck, und der Dialog geht
    // stillschweigend gar nicht auf.
    //
    // APARTMENTTHREADED, weil es ein Bedienfaden ist - so steht es in der
    // Anleitung zum Common Item Dialog. DISABLE_OLE1DDE ist die uebliche
    // Beigabe; sie schaltet einen Rueckfall aus, den niemand mehr braucht.
    const HRESULT comInit = CoInitializeEx(
        nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

    // Als Allererstes das Protokoll. Vor allem, was abstuerzen koennte.
    //
    // Es liegt NEBEN DER .EXE, nicht in %APPDATA%: wer es hinschicken soll,
    // findet es dort, ohne einen versteckten Ordner suchen zu muessen.
    //
    // Wenn das nicht geht - in "Programme" darf ein Programm nicht schreiben -
    // bleibt %APPDATA% als Rueckfall. Beides stumm scheitern zu lassen waere
    // schlecht: dann gaebe es bei einem Absturz gar nichts.
    std::string logDir = bhed::platform::executableDirectory();
    bool logging = !logDir.empty() &&
                   bhed::diag::open(logDir + "\\behaved.log");
    if (!logging) {
        logDir = bhed::platform::settingsDirectory();
        logging = !logDir.empty() &&
                  bhed::diag::open(logDir + "\\behaved.log");
    }
    // Das Detailprotokoll daneben, im selben Ordner.
    //
    // Es beantwortet die andere Frage: nicht "wo ist es steckengeblieben",
    // sondern "warum wurde diese eine Datei nicht gefunden". Dafuer
    // vermerkt es JEDEN Versuch - bei einer Karte mit hundert Texturen
    // mehrere hundert Zeilen. Deshalb eine eigene Datei; zwischen die
    // fuenfzig Zeilen des Ablaufprotokolls gemischt waere beides
    // unbrauchbar.
    if (logging) {
        (void)bhed::diag::openDetail(logDir + "\\behaved-detail.log");
    }
    SetUnhandledExceptionFilter(logCrash);

    {
        bhed::diag::Step step("Start");
        bhed::diag::writeHeader({
            {"behaved", std::string("1.0.0-") + bhed::kFassung},
            {"gebaut", std::string(__DATE__) + " " + __TIME__},
            {"Windows", windowsVersion()},
            {"DPI", std::to_string(windowDpi(nullptr))},
            {"Protokoll", logging ? logDir + "\\behaved.log" : std::string("nicht schreibbar")},
            // Damit man weiss, dass es die zweite Datei gibt.
            {"Detail", bhed::diag::detailOn()
                            ? logDir + "\\behaved-detail.log"
                            : std::string("aus")},
            {"Gebiet", systemLocale()},
        });
    }

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_CLASSDC;
    wc.lpfnWndProc = wndProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    // Das Symbol MUSS an der Fensterklasse haengen, nicht nur in der .exe.
    //
    // Die erste ICON-Anweisung in behaved.rc bestimmt, was der Explorer an
    // der Datei zeigt - deshalb sah es dort richtig aus. Die TASKLEISTE und
    // die Titelzeile nehmen ihr Symbol aber vom FENSTER, und das hatte
    // keins: ohne hIcon/hIconSm setzt Windows sein Vorgabesymbol ein.
    //
    // Die Ressourcennummer ist 1, wie in behaved.rc. Zwei Groessen, weil
    // Windows sie an verschiedenen Stellen verlangt: hIconSm fuer die
    // Titelzeile, hIcon fuer Alt+Tab und die Taskleiste. LoadImage mit der
    // passenden Groesse liefert die richtige Stufe aus der .ico, statt eine
    // grosse herunterzurechnen.
    wc.hIcon = static_cast<HICON>(
        LoadImageW(instance, MAKEINTRESOURCEW(1), IMAGE_ICON,
                   GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON),
                   LR_DEFAULTCOLOR));
    wc.hIconSm = static_cast<HICON>(
        LoadImageW(instance, MAKEINTRESOURCEW(1), IMAGE_ICON,
                   GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON),
                   LR_DEFAULTCOLOR));
    wc.lpszClassName = L"behaved";
    RegisterClassExW(&wc);

    int w = 0;
    int h = 0;
    bhed::gui::defaultWindowSize(&w, &h);
    const float dpiScale = static_cast<float>(windowDpi(nullptr)) / 96.0F;
    w = static_cast<int>(static_cast<float>(w) * dpiScale) + 40;
    h = static_cast<int>(static_cast<float>(h) * dpiScale) + 60;

    bhed::diag::info("Fenster erstellen");
    const HWND hwnd = CreateWindowW(wc.lpszClassName, L"BehavEd", WS_OVERLAPPEDWINDOW,
                                    CW_USEDEFAULT, CW_USEDEFAULT, w, h, nullptr, nullptr,
                                    instance, nullptr);
    if (hwnd == nullptr) {
        MessageBoxW(nullptr, L"Fenster konnte nicht erstellt werden.", L"BehavEd",
                    MB_ICONERROR);
        return 1;
    }

    bhed::diag::Step appStep("Programmzustand aufbauen");
    bhed::gui::App* app = bhed::gui::createApp();
    bhed::gui::setApp(app);
    // Gebietsvorgabe zuerst; die Einstellungen ueberschreiben sie gleich.
    bhed::i18n::setLanguage(bhed::i18n::fromSystemLocale(systemLocale()));
    // Einstellungen zuerst: dort kann eine eigene .bhc stehen, und die soll
    // beim Laden des Modells schon bekannt sein.
    bhed::gui::loadSettings(app);

    std::string error;
    if (!bhed::gui::loadModel(app, exeDirectory() + "\\data\\base", &error)) {
        MessageBoxA(hwnd, error.c_str(), "BehavEd", MB_ICONERROR);
        return 2;
    }
    // Sprache aus den Regionseinstellungen; der Nutzer kann sie im Menue
    // umstellen.
    // Eine Datei auf der Kommandozeile (Doppelklick, "Oeffnen mit", auf die
    // .exe gezogen, oder der "run"-Knopf einer anderen Instanz) geht vor
    // "Re-open last file" - wie im Original.
    std::string kommandozeile;
    {
        int argc = 0;
        LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        if (argv != nullptr) {
            if (argc > 1) {
                kommandozeile = toUtf8(argv[1]);
            }
            LocalFree(argv);
        }
    }
    bhed::gui::reopenLastIfWanted(app, kommandozeile);

    // Lage und Zustand des Fensters wiederherstellen.
    //
    // `WINDOWPLACEMENT` ist die richtige Stelle: laut Microsoft liefert und
    // nimmt sie `showCmd` (maximiert/minimiert/normal) UND
    // `rcNormalPosition` - die Groesse im wiederhergestellten Zustand. Wer
    // maximiert beendet, bekommt beim naechsten Start ein maximiertes
    // Fenster, und beim Wiederherstellen die alte Groesse.
    //
    // `length` MUSS gesetzt sein, sonst schlaegt der Aufruf fehl - das steht
    // ausdruecklich in der Anleitung, und zwar bei beiden Funktionen.
    //
    // Liegt das Fenster nach einem Monitorwechsel ganz ausserhalb, rueckt
    // Windows es von selbst zurecht. Darum muss ich mich nicht kuemmern.
    //
    // Minimiert stellen wir NICHT wieder her: ein Programm, das beim Start
    // sofort in der Leiste verschwindet, sieht aus wie ein Absturz.
    bool wiederhergestellt = false;
    const std::string lage = bhed::gui::gemerkteFensterlage();
    // Im Selbsttest nicht: SetWindowPlacement mit "maximiert" aktiviert das
    // Fenster und holt es nach vorn (siehe unten).
    const bool imSelbsttest = std::getenv("BHED_EDITORTEST") != nullptr;
    if (!lage.empty() && !imSelbsttest) {
        WINDOWPLACEMENT wp{};
        wp.length = sizeof(wp);
        int cmd = 0;
        int l = 0;
        int t = 0;
        int r = 0;
        int b = 0;
        if (std::sscanf(lage.c_str(),
                        "%d %d %d %d %d", &cmd, &l, &t, &r, &b) == 5 &&
            r > l && b > t) {
            wp.showCmd = (cmd == SW_SHOWMINIMIZED)
                             ? static_cast<UINT>(SW_SHOWNORMAL)
                             : static_cast<UINT>(cmd);
            wp.rcNormalPosition.left = l;
            wp.rcNormalPosition.top = t;
            wp.rcNormalPosition.right = r;
            wp.rcNormalPosition.bottom = b;
            wiederhergestellt = SetWindowPlacement(hwnd, &wp) != 0;
        }
        char zp[220];
        std::snprintf(zp, sizeof(zp),
                      "Fensterlage geladen: \"%s\" -> showCmd %d, %d/%d bis "
                      "%d/%d, gesetzt %s",
                      lage.c_str(), cmd, l, t, r, b,
                      wiederhergestellt ? "ja" : "NEIN");
        bhed::diag::detail(zp);
    } else {
        bhed::diag::detail("Fensterlage geladen: nichts gespeichert");
    }
    // --- Selbsttest im HINTERGRUND --------------------------------------
    //
    // shank, 27.09.: "Eventuell kannst du in zukunft bitte im hintergrund
    // alles testen ... so das mein browser tab nicht weg springt". Bisher
    // ging das Testfenster aktiviert nach vorn und nahm dem Browser den
    // Fokus; klickte er zurueck, verlor behaved ihn mitten im Test (ImGui
    // setzt dann die Maustasten zurueck, ein laufender Zug brach ab).
    //
    // Im Selbsttest also: gross wie der Arbeitsbereich, aber NICHT
    // aktiviert und ganz hinten. Der Test speist Maus und Tasten selbst
    // ein und braucht weder Fokus noch Sichtbarkeit; gezeichnet wird auch
    // verdeckt (nichts hier drosselt auf verdeckte Fenster).
    if (imSelbsttest) {
        RECT wa{};
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
        SetWindowPos(hwnd, HWND_BOTTOM, wa.left, wa.top, wa.right - wa.left,
                     wa.bottom - wa.top, SWP_NOACTIVATE | SWP_SHOWWINDOW);
        bhed::diag::detail("Selbsttest: Fenster im Hintergrund, nicht aktiviert");
    } else if (wiederhergestellt) {
        // SetWindowPlacement hat es schon gezeigt.
    } else {
        ShowWindow(hwnd, SW_SHOWDEFAULT);
    }
    UpdateWindow(hwnd);

    // Erst Direct3D 11, dann OpenGL. Auf Rechnern ohne Hardware-Direct3D
    // faellt der erste Versuch durch, und der zweite traegt.
#ifdef BHED_WITH_D3D11
    const bool first = runSession(bhed::render::Backend::Direct3D11, hwnd, app);
#else
    const bool first = false;
#endif
    if (!first) {
        g_quit = false;
        if (!runSession(bhed::render::Backend::OpenGL3, hwnd, app)) {
            MessageBoxW(hwnd,
                        L"Weder Direct3D 11 noch OpenGL 3.3 stehen zur Verfuegung.",
                        L"BehavEd", MB_ICONERROR);
            bhed::gui::destroyApp(app);
            DestroyWindow(hwnd);
            UnregisterClassW(wc.lpszClassName, instance);
            return 3;
        }
    }

    bhed::gui::destroyApp(app);
    DestroyWindow(hwnd);
    UnregisterClassW(wc.lpszClassName, instance);
    bhed::diag::closeDetail();
    bhed::diag::close();
    if (SUCCEEDED(comInit)) {
        CoUninitialize();
    }
    return 0;
}

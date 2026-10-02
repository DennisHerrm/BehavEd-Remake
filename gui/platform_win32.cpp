#include "platform.h"

#include "bhed/diag.h"

#include <cstdlib>

#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>

#include <commdlg.h>
#include <shlobj.h>

#include <string>
#include <vector>

namespace bhed::platform {
namespace {

std::wstring toWide(const std::string& text) {
    if (text.empty()) {
        return {};
    }
    const int n = MultiByteToWideChar(CP_UTF8, 0, text.c_str(),
                                      static_cast<int>(text.size()), nullptr, 0);
    std::wstring out(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
                        out.data(), n);
    return out;
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

// Die Dateidialoge - ueber den Common Item Dialog, nicht ueber
// GetOpenFileNameW.
//
// Microsoft sagt es deutlich: "Starting with Windows Vista, the Open and Save
// As common dialog boxes have been superseded by the Common Item Dialog. We
// recommended that you use the Common Item Dialog API instead."
//
// Drei Gruende, die hier zaehlen:
//
//   * Es gibt einen echten ORDNERDIALOG (FOS_PICKFOLDERS). Vorher musste man
//     eine .pk3 im gewuenschten Ordner auswaehlen, damit das Programm den
//     Ordner davon nehmen konnte - eine Kruecke, die im Quelltext auch so
//     kommentiert war.
//   * Der Dialog ist der, den der Benutzer aus jedem anderen Programm kennt:
//     groessenveraenderlich, mit Seitenleiste und zuletzt benutzten Orten.
//   * Der Startordner ist nur ein VORSCHLAG (SetDefaultFolder). Windows merkt
//     sich je Programm, wo man zuletzt war, und das ist meistens nuetzlicher.
//
// COM muss angemeldet sein, bevor das hier laeuft - CoInitializeEx steht in
// wWinMain. Fehlt es, gibt CoCreateInstance CO_E_NOTINITIALIZED zurueck und
// der Dialog geht stillschweigend gar nicht auf.

// Aus "Scripts (*.icarus)|*.icarus|All files (*.*)|*.*" die Paare bauen, die
// SetFileTypes erwartet. Die Zeichenketten muessen leben, solange der Dialog
// laeuft - deshalb bleiben sie im Aufrufer stehen.
struct FilterSpec {
    std::vector<std::wstring> texts;
    std::vector<COMDLG_FILTERSPEC> specs;
};

FilterSpec buildFilter(const char* filter) {
    FilterSpec out;
    if (filter == nullptr) {
        return out;
    }
    std::vector<std::string> parts;
    std::string cur;
    for (const char* p = filter; *p != '\0'; ++p) {
        if (*p == '|') {
            parts.push_back(cur);
            cur.clear();
        } else {
            cur += *p;
        }
    }
    if (!cur.empty()) {
        parts.push_back(cur);
    }
    // Paarweise: Name, Muster.
    out.texts.reserve(parts.size());
    for (const std::string& part : parts) {
        out.texts.push_back(toWide(part));
    }
    for (std::size_t i = 0; i + 1 < out.texts.size(); i += 2) {
        out.specs.push_back(COMDLG_FILTERSPEC{out.texts[i].c_str(),
                                              out.texts[i + 1].c_str()});
    }
    return out;
}

std::string runDialog(const char* title, const char* filter,
                      const std::string& startDir, const std::string& suggested,
                      bool save, bool folder) {
    IFileDialog* dialog = nullptr;
    const CLSID clsid = save ? CLSID_FileSaveDialog : CLSID_FileOpenDialog;
    const IID iid = save ? IID_IFileSaveDialog : IID_IFileOpenDialog;
    if (FAILED(CoCreateInstance(clsid, nullptr, CLSCTX_INPROC_SERVER, iid,
                                reinterpret_cast<void**>(&dialog))) ||
        dialog == nullptr) {
        return {};
    }

    DWORD options = 0;
    if (SUCCEEDED(dialog->GetOptions(&options))) {
        options |= FOS_FORCEFILESYSTEM;   // keine virtuellen Orte
        if (folder) {
            options |= FOS_PICKFOLDERS;
        } else if (!save) {
            options |= FOS_FILEMUSTEXIST | FOS_PATHMUSTEXIST;
        } else {
            options |= FOS_OVERWRITEPROMPT;
        }
        dialog->SetOptions(options);
    }

    const std::wstring wtitle = toWide(title);
    dialog->SetTitle(wtitle.c_str());

    FilterSpec spec;
    if (!folder && filter != nullptr) {
        spec = buildFilter(filter);
        if (!spec.specs.empty()) {
            dialog->SetFileTypes(static_cast<UINT>(spec.specs.size()),
                                 spec.specs.data());
        }
    }

    if (!suggested.empty()) {
        dialog->SetFileName(toWide(suggested).c_str());
    }

    // Der Startordner ist ein VORSCHLAG: SetDefaultFolder statt
    // SetFolder. Damit hat der Ort, an dem der Benutzer zuletzt war, Vorrang
    // - so verhaelt sich jedes andere Programm auch.
    if (!startDir.empty()) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(SHCreateItemFromParsingName(
                toWide(startDir).c_str(), nullptr, IID_PPV_ARGS(&item))) &&
            item != nullptr) {
            dialog->SetDefaultFolder(item);
            item->Release();
        }
    }

    std::string result;
    if (SUCCEEDED(dialog->Show(nullptr))) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dialog->GetResult(&item)) && item != nullptr) {
            PWSTR path = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) &&
                path != nullptr) {
                result = toUtf8(path);
                CoTaskMemFree(path);
            }
            item->Release();
        }
    }
    dialog->Release();
    return result;
}

}  // namespace

std::string openFileDialog(const char* title, const char* filter,
                           const std::string& startDir) {
    return runDialog(title, filter, startDir, {}, false, false);
}

std::string pickFolder(const char* title, const std::string& startDir) {
    return runDialog(title, nullptr, startDir, {}, false, true);
}

std::string saveFileDialog(const char* title, const char* filter,
                           const std::string& startDir,
                           const std::string& suggestedName) {
    return runDialog(title, filter, startDir, suggestedName, true, false);
}

Answer askSaveChanges(const std::string& question, const char* title) {
    const int r = MessageBoxW(nullptr, toWide(question).c_str(), toWide(title).c_str(),
                              MB_YESNOCANCEL | MB_ICONQUESTION);
    if (r == IDYES) {
        return Answer::Yes;
    }
    if (r == IDNO) {
        return Answer::No;
    }
    return Answer::Cancel;
}

void showError(const std::string& message, const char* title) {
    // Im Selbsttest KEIN Fenster: es hielte den Lauf an, und shank saesse
    // davor. Stattdessen ins Protokoll - der Test sieht es dort.
    if (std::getenv("BHED_EDITORTEST") != nullptr) {
        diag::detail("Selbsttest: Fehlermeldung statt Dialog: " + message);
        return;
    }
    MessageBoxW(nullptr, toWide(message).c_str(), toWide(title).c_str(), MB_ICONERROR);
}

void openWithDefaultApp(const std::string& path) {
    // "open" statt eines festen Programms: welcher Editor eingestellt ist,
    // entscheidet Windows.
    ShellExecuteW(nullptr, L"open", toWide(path).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

bool startBehavedWith(const std::string& datei) {
    std::vector<wchar_t> exe(MAX_PATH);
    for (;;) {
        const DWORD n = GetModuleFileNameW(nullptr, exe.data(),
                                           static_cast<DWORD>(exe.size()));
        if (n == 0) {
            return false;
        }
        if (n < exe.size()) {
            break;
        }
        exe.resize(exe.size() * 2);
    }
    const std::wstring arg = L"\"" + toWide(datei) + L"\"";
    const HINSTANCE r = ShellExecuteW(nullptr, L"open", exe.data(), arg.c_str(),
                                      nullptr, SW_SHOWNORMAL);
    // ShellExecute meldet Erfolg mit einem Wert ueber 32.
    return reinterpret_cast<INT_PTR>(r) > 32;
}

std::string settingsDirectory() {
    wchar_t* appdata = nullptr;
    std::string dir;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &appdata))) {
        dir = toUtf8(appdata) + "\\behaved";
        CoTaskMemFree(appdata);
        CreateDirectoryW(toWide(dir).c_str(), nullptr);
    }
    return dir;
}


void openInExplorer(const std::string& folder) {
    ShellExecuteW(nullptr, L"open", toWide(folder).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

std::string executableDirectory() {
    // GetModuleFileNameW mit wachsendem Puffer: der Pfad kann laenger als
    // MAX_PATH sein, und die Funktion sagt das nur durch Abschneiden.
    std::wstring buf(MAX_PATH, L'\0');
    for (int attempt = 0; attempt < 5; ++attempt) {
        const DWORD n = GetModuleFileNameW(nullptr, buf.data(),
                                           static_cast<DWORD>(buf.size()));
        if (n == 0) {
            return {};
        }
        if (n < buf.size()) {
            buf.resize(n);
            break;
        }
        buf.resize(buf.size() * 2);
    }
    const std::size_t slash = buf.find_last_of(L"\\/");
    if (slash == std::wstring::npos) {
        return {};
    }
    return toUtf8(buf.substr(0, slash));
}

std::vector<std::string> listDirectory(const std::string& dir) {
    std::vector<std::string> out;
    if (dir.empty()) {
        return out;
    }
    // FindFirstFileW mit Sternchen. Unterordner werden uebersprungen: es geht
    // um Dateien NEBEN dem Modell, nicht um einen Baum.
    const std::wstring pattern = toWide(dir) + L"\\*";
    WIN32_FIND_DATAW data{};
    HANDLE h = FindFirstFileW(pattern.c_str(), &data);
    if (h == INVALID_HANDLE_VALUE) {
        return out;
    }
    do {
        if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
            continue;
        }
        out.push_back(toUtf8(data.cFileName));
    } while (FindNextFileW(h, &data) != 0);
    FindClose(h);
    return out;
}


// --- RenderDoc ---------------------------------------------------------
//
// Nur die eine Funktion von Hand erklaert, statt renderdoc_app.h
// mitzuliefern. Die Fassungsnummer 1.0.0 ist die aelteste, die
// `TriggerCapture` kennt; RenderDoc gibt auch bei neueren Fassungen eine
// abwaertsvertraegliche Tabelle zurueck.
namespace {

struct RenderDocApi110 {
    void* rest1[6];
    void (*TriggerCapture)();
    // Der Rest der Tabelle wird nicht angefasst.
};

using GetApiFn = int (*)(int version, void** outAPIPointers);

RenderDocApi110* holeRenderDoc() {
    static RenderDocApi110* api = nullptr;
    static bool versucht = false;
    if (versucht) {
        return api;
    }
    versucht = true;
    // GetModuleHandle, NICHT LoadLibrary: wir wollen RenderDoc nicht
    // nachtraeglich hineinladen - das ginge ohnehin nicht -, sondern nur
    // wissen, ob es schon da ist.
    HMODULE m = GetModuleHandleA("renderdoc.dll");
    if (m == nullptr) {
        return nullptr;
    }
    auto* hole = reinterpret_cast<GetApiFn>(
        reinterpret_cast<void*>(GetProcAddress(m, "RENDERDOC_GetAPI")));
    if (hole == nullptr) {
        return nullptr;
    }
    void* p = nullptr;
    // 10100 = eRENDERDOC_API_Version_1_1_0
    if (hole(10100, &p) != 1) {
        return nullptr;
    }
    api = static_cast<RenderDocApi110*>(p);
    return api;
}

}  // namespace

bool renderDocDa() { return holeRenderDoc() != nullptr; }

void renderDocAufnehmen() {
    RenderDocApi110* api = holeRenderDoc();
    if (api != nullptr && api->TriggerCapture != nullptr) {
        api->TriggerCapture();
    }
}

}  // namespace bhed::platform

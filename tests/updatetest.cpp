// updatetest.cpp - der Auto-Updater ohne Netz (siehe update.h)
#include <cstdio>
#include <string>

#include "bhed/update.h"

namespace {

int fehler = 0;

void expect(const char* was, bool ok) {
    std::printf("  %s  %s\n", ok ? "ok  " : "FEHL", was);
    if (!ok) { ++fehler; }
}

}  // namespace

int main() {
    using namespace bhed::update;

    // --- Eine Antwort, gebaut wie die von /releases/latest ---------------
    const std::string json = R"({
  "url": "https://api.github.com/repos/DennisHerrm/BehavEd-Remake/releases/1",
  "html_url": "https://github.com/DennisHerrm/BehavEd-Remake/releases/tag/v1.0.0-rc570",
  "tag_name": "v1.0.0-rc570",
  "name": "BehavEd-Remake 1.0.0-rc570",
  "draft": false, "prerelease": false, "id": 123456789,
  "author": {"login": "DennisHerrm", "site_admin": false, "nested": {"a": [1, 2, {"b": null}]}},
  "assets": [
    {"url": "https://api.github.com/repos/DennisHerrm/BehavEd-Remake/releases/assets/11",
     "name": "notes.txt", "size": 12, "browser_download_url": "https://github.com/x/notes.txt"},
    {"url": "https://api.github.com/repos/DennisHerrm/BehavEd-Remake/releases/assets/22",
     "name": "BehavEd-Remake-rc570.ZIP", "size": 2032686,
     "browser_download_url": "https://github.com/DennisHerrm/BehavEd-Remake/releases/download/v1.0.0-rc570/BehavEd-Remake-rc570.zip"}
  ],
  "body": "Neu:\r\n- \u00c4nderung \"mit\" Zeichen \\ und \ud83e\udd16\n"
})";
    Release r;
    std::string f;
    expect("JSON: gelesen", parseRelease(json, r, &f));
    expect("JSON: tag_name", r.tag == "v1.0.0-rc570");
    expect("JSON: name und html_url",
           r.name == "BehavEd-Remake 1.0.0-rc570" && r.htmlUrl.find("/tag/v1.0.0-rc570") != std::string::npos);
    expect("JSON: zwei Dateien, Groesse", r.assets.size() == 2 && r.assets[1].size == 2032686);
    expect("JSON: Notizen mit Escapes (Umlaut, Anfuehrung, Backslash, Emoji)",
           r.body.find("\xC3\x84nderung \"mit\" Zeichen \\ und \xF0\x9F\xA4\x96") != std::string::npos);
    const Asset* z = zipAsset(r);
    expect("zipAsset: das .zip (Grossschreibung egal), nicht die .txt",
           z != nullptr && z->name == "BehavEd-Remake-rc570.ZIP" && z->apiUrl.find("/assets/22") != std::string::npos);

    Release leer;
    std::string meldung;
    expect("JSON: GitHub-Fehler {message} -> false mit Meldung",
           !parseRelease(R"({"message": "Not Found", "documentation_url": "x"})", leer, &meldung) &&
               meldung == "Not Found");
    expect("JSON: kaputt -> false", !parseRelease(R"({"tag_name": "v1", )", leer));
    expect("JSON: Liste statt Objekt -> false", !parseRelease("[1,2]", leer));

    // --- Fassungen ---------------------------------------------------------
    expect("rcNummer: v1.0.0-rc568 -> 568", rcNummer("v1.0.0-rc568") == 568);
    expect("rcNummer: rc568 -> 568", rcNummer("rc568") == 568);
    expect("rcNummer: RC12 -> 12", rcNummer("RC12") == 12);
    expect("rcNummer: ohne rc -> -1", rcNummer("v1.0.0") == -1 && rcNummer("rcx") == -1);
    expect("istNeuer: rc570 > rc568", istNeuer("v1.0.0-rc570", "rc568"));
    expect("istNeuer: gleich ist nicht neuer", !istNeuer("v1.0.0-rc568", "rc568"));
    expect("istNeuer: aelter ist nicht neuer", !istNeuer("v1.0.0-rc500", "rc568"));
    expect("istNeuer: ohne Nummer nie", !istNeuer("latest", "rc568") && !istNeuer("v1.0.0-rc570", "dev"));

    // --- Wohin die Dateien gehoeren ---------------------------------------
    const std::string ober = gemeinsamerOrdner({"BehavEd-Remake/behaved.exe", "BehavEd-Remake/data/base/anims.h"});
    expect("gemeinsamerOrdner: BehavEd-Remake/", ober == "BehavEd-Remake/");
    expect("gemeinsamerOrdner: keiner, wenn eine Datei oben liegt",
           gemeinsamerOrdner({"behaved.exe", "data/x.h"}).empty());
    expect("zielImOrdner: Ordner abgestreift",
           zielImOrdner("BehavEd-Remake/data/base/anims.h", ober) == "data/base/anims.h" &&
               zielImOrdner("BehavEd-Remake/behaved.exe", ober) == "behaved.exe");
    expect("zielImOrdner: ein Ordnereintrag wird uebersprungen", zielImOrdner("BehavEd-Remake/data/", ober).empty());
    expect("zielImOrdner: .. wird abgewiesen",
           zielImOrdner("BehavEd-Remake/../evil.exe", ober).empty() &&
               zielImOrdner("data/../../x", "").empty());
    expect("zielImOrdner: absolute Pfade werden abgewiesen",
           zielImOrdner("/abs/x", "").empty() && zielImOrdner("C:/Windows/x.dll", "").empty());
    expect("zielImOrdner: Backslashes werden zu /", zielImOrdner("data\\base\\x.h", "") == "data/base/x.h");

    std::printf(fehler == 0 ? "\nalle Updateproben bestanden\n" : "\nFEHLGESCHLAGEN (%d)\n", fehler);
    return fehler == 0 ? 0 : 1;
}

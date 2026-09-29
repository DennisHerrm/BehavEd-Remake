// diagtest.cpp - die vier Regeln des Protokolls
#include "bhed/diag.h"
#include <iterator>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace {
int fails = 0;
void expect(const char* what, bool ok) {
    std::printf("  %s  %s\n", ok ? "ok  " : "FEHL", what);
    if (!ok) { ++fails; }
}
std::string slurp(const std::string& p) {
    std::ifstream f(p, std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}
}  // namespace

namespace {

void detailProben() {
    // --- Das Detailprotokoll ------------------------------------------
    //
    // Zweite Datei neben dem Ablaufprotokoll. Sie beantwortet die andere
    // Frage: nicht "wo ist es steckengeblieben", sondern "warum wurde
    // diese eine Datei nicht gefunden".
    // Kein fester Unix-Pfad: die Proben laufen auch unter Windows. Der
    // Prueflauf schreibt neben sich selbst, das geht ueberall.
    const std::string pfad = "bhed_detail_probe.log";
    std::remove(pfad.c_str());
    std::remove((pfad + ".vorher").c_str());

    expect("vor dem Oeffnen ist es aus", !bhed::diag::detailOn());
    // Ein Aufruf ohne offene Datei darf nicht stuerzen - er kostet nichts.
    bhed::diag::detail("geht ins Leere");

    expect("es laesst sich oeffnen", bhed::diag::openDetail(pfad));
    expect("und meldet sich als offen", bhed::diag::detailOn());
    bhed::diag::detail("erste Zeile");
    bhed::diag::detail("zweite Zeile");
    bhed::diag::closeDetail();
    expect("nach dem Schliessen ist es wieder aus", !bhed::diag::detailOn());

    // Die Lesedatei MUSS wieder zu sein, bevor unten neu geoeffnet wird.
    //
    // Unter Linux laesst sich eine offene Datei umbenennen, unter Windows
    // NICHT - dort haelt der Lesezugriff sie fest, und das rename() in
    // openDetail scheitert. Genau daran ist diese Probe auf shanks Rechner
    // zweimal gescheitert, waehrend sie hier durchlief. Der Fehler lag
    // nicht im Protokoll, sondern in der Probe.
    //
    // Deshalb ein eigener Gueltigkeitsbereich: der Strom geht am Ende
    // zuverlaessig zu, auch wenn dazwischen etwas dazukommt.
    std::string inhalt;
    {
        std::ifstream f(pfad);
        inhalt.assign(std::istreambuf_iterator<char>(f),
                      std::istreambuf_iterator<char>());
    }
    expect("beide Zeilen stehen drin",
           inhalt.find("erste Zeile") != std::string::npos &&
               inhalt.find("zweite Zeile") != std::string::npos);
    expect("und der Abschluss", inhalt.find("--- Ende ---") != std::string::npos);
    expect("das Weggeschriebene fehlt",
           inhalt.find("geht ins Leere") == std::string::npos);

    // Ein zweiter Lauf legt das vorige beiseite, statt es zu loeschen.
    // Wer zweimal startet, um einen Fehler nachzustellen, soll den ersten
    // Lauf nicht dabei verlieren.
    expect("zweites Oeffnen geht", bhed::diag::openDetail(pfad));
    bhed::diag::detail("neuer Lauf");
    bhed::diag::closeDetail();
    std::string vorher;
    {
        std::ifstream alt(pfad + ".vorher");
        vorher.assign(std::istreambuf_iterator<char>(alt),
                      std::istreambuf_iterator<char>());
    }
    expect("das vorige Protokoll liegt als .vorher daneben",
           vorher.find("erste Zeile") != std::string::npos);

    // Und ein DRITTER Lauf, mit schon vorhandenem .vorher.
    //
    // Genau hier ist es unter Windows gescheitert: rename() ueberschreibt
    // dort NICHT, sondern schlaegt fehl, wenn das Ziel existiert. Auf
    // POSIX laeuft es durch, also hat mein Lauf hier nichts gemerkt - der
    // MSVC-Lauf auf shanks Rechner schon. Die Probe deckt den Fall jetzt
    // ab, weil sie den Zustand herstellt, in dem er auftritt: .vorher ist
    // schon da.
    expect("drittes Oeffnen geht auch mit vorhandenem .vorher",
           bhed::diag::openDetail(pfad));
    bhed::diag::detail("dritter Lauf");
    bhed::diag::closeDetail();
    std::string vorher2;
    {
        std::ifstream alt2(pfad + ".vorher");
        vorher2.assign(std::istreambuf_iterator<char>(alt2),
                       std::istreambuf_iterator<char>());
    }
    expect("und .vorher traegt jetzt den ZWEITEN Lauf",
           vorher2.find("neuer Lauf") != std::string::npos);
}

}  // namespace

int main() {
    // Kein "/tmp": das gibt es unter Windows nicht, und der Test schlug dort
    // mit sieben Fehlschlaegen fehl, weil sich die Datei nicht oeffnen liess.
    // std::filesystem::temp_directory_path liefert auf beiden Systemen einen
    // beschreibbaren Ort (%TEMP% bzw. /tmp).
    std::error_code ec;
    const std::filesystem::path dir = std::filesystem::temp_directory_path(ec);
    const std::string path = (dir / "bhed_diag_test.log").string();
    std::remove(path.c_str());
    std::remove((path + ".vorher").c_str());

    bhed::diag::resetForTesting();
    if (!bhed::diag::open(path)) {
        // Ohne Datei sind die folgenden Proben sinnlos - dann lieber EIN
        // klarer Fehlschlag statt sieben Folgefehlern, die den Grund
        // verdecken.
        std::printf("  FEHL  Protokoll laesst sich nicht oeffnen: %s\n", path.c_str());
        std::printf("\nFEHLGESCHLAGEN (1 Fehlschlag)\n");
        return 1;
    }
    expect("Protokoll laesst sich oeffnen", true);
    bhed::diag::writeHeader({{"behaved", "test"}, {"Windows", "0.0"}});

    {
        bhed::diag::Step outer("aeusserer Schritt");
        expect("laufender Schritt ist bekannt",
               bhed::diag::currentStep() == "aeusserer Schritt");
        {
            bhed::diag::Step inner("innerer Schritt");
            bhed::diag::info("etwas passiert");
            // Regel 1: nach jeder Zeile wird geleert. Also muss die Zeile
            // JETZT schon in der Datei stehen, mitten im offenen Schritt -
            // sonst waere sie bei einem Absturz genau hier verloren.
            expect("Zeile steht sofort in der Datei, nicht erst am Ende",
                   slurp(path).find("etwas passiert") != std::string::npos);
            expect("innerer Schritt ist der laufende",
                   bhed::diag::currentStep() == "innerer Schritt");
        }
        expect("nach dem inneren ist wieder der aeussere dran",
               bhed::diag::currentStep() == "aeusserer Schritt");
    }
    expect("nach allen Schritten ist keiner mehr offen",
           bhed::diag::currentStep().empty());

    {
        bhed::diag::Step s("Schritt mit Misserfolg");
        s.fail("Datei fehlt");
    }
    const std::string text = slurp(path);
    expect("Misserfolg steht in der Abschlusszeile",
           text.find("FEHLER: Datei fehlt") != std::string::npos);
    expect("Beginn wird vermerkt", text.find("> aeusserer Schritt") != std::string::npos);
    expect("Ende wird vermerkt", text.find("< aeusserer Schritt") != std::string::npos);

    bhed::diag::close();
    expect("Abschlusszeile vorhanden",
           slurp(path).find("beendet") != std::string::npos);

    // Regel 4: beim erneuten Oeffnen wird das vorige beiseitegelegt, nicht
    // ueberschrieben - sonst loescht der Neustart nach dem Absturz den Beweis.
    bhed::diag::resetForTesting();
    (void)bhed::diag::open(path);
    bhed::diag::info("neuer Lauf");
    bhed::diag::close();
    expect("voriges Protokoll liegt als .vorher daneben",
           slurp(path + ".vorher").find("aeusserer Schritt") != std::string::npos);
    expect("neues Protokoll enthaelt das alte nicht mehr",
           slurp(path).find("aeusserer Schritt") == std::string::npos);

    detailProben();

    std::printf("\n%s (%d Fehlschlaege)\n",
                fails != 0 ? "FEHLGESCHLAGEN" : "alle Protokollproben bestanden", fails);
    return fails != 0 ? 1 : 0;
}

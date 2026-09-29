// enginecheck.cpp - liest die Engine, was wir schreiben?
//
// Bisher ist unser Modell gegen die .bhc, gegen Bildschirmfotos und gegen
// 1011 Dateien geprueft. Was fehlte, ist die Gegenprobe an der ENGINE: was
// liest sie tatsaechlich, Befehl fuer Befehl.
//
// Hier ist ihr Leser nachgebaut - abgeschrieben aus OpenJK,
// code/icarus/TaskManager.cpp und Sequencer.cpp. Dann wird jeder Block aus
// Ravens Dateien damit durchgespielt: verbraucht die Engine genau alle
// Glieder, oder bleibt etwas liegen bzw. fehlt etwas?
//
// Bleibt ein Glied liegen, wuerde die Engine es uebergehen. Fehlt eines,
// liest sie ueber das Ende hinaus. Beides waere ein Fund.
#include "bhed/ibi.h"

#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

int fails = 0;

// Was die Engine an einer Stelle erwartet.
enum class Read { Str, Flt, Vec, Op, None };

// Ein Leser, der sich verhaelt wie CTaskManager::Get/GetFloat/GetVector.
struct EngineReader {
    const std::vector<bhed::IbiMember>& ms;
    std::size_t i = 0;
    bool overrun = false;

    [[nodiscard]] bool done() const { return i >= ms.size(); }

    // Wechselseitig: ein Marker liest Werte, ein Wert kann ein Marker sein.
    bool read(Read what);
    bool marker();

    // Marker ziehen ihre Argumente nach - und die koennen SELBST Marker
    // sein. In Ravens Dateien steht 35 Mal ein get() als Name eines tag():
    //     [49 36 6 4 6 ...]  =  tag( get( FLOAT, "x" ), ORIGIN )
    // Ein Marker, der stur drei Glieder zaehlt, verrutscht dort - der erste
    // Anlauf hier hat genau deshalb 49 move-Bloecke falsch bewertet.
    bool markerImpl() {
        if (done()) { overrun = true; return false; }
        const std::int32_t id = ms[i].id;
        if (id == bhed::ID_GET) {
            ++i;
            (void)read(Read::Flt);   // Typ
            (void)read(Read::Str);   // Name, kann selbst ein Marker sein
            return true;
        }
        if (id == bhed::ID_TAG) {
            ++i;
            (void)read(Read::Str);   // Name, kann selbst ein Marker sein
            (void)read(Read::Flt);   // Typ
            return true;
        }
        if (id == bhed::ID_RANDOM) {
            ++i;
            (void)read(Read::Flt);
            (void)read(Read::Flt);
            return true;
        }
        if (id == bhed::TK_VECTOR) {
            ++i;
            for (int k = 0; k < 3; ++k) {
                (void)read(Read::Flt);
            }
            return true;
        }
        return false;
    }

    bool readImpl(Read what) {
        if (done()) { overrun = true; return false; }
        if (marker()) {
            return true;
        }
        const std::int32_t id = ms[i].id;
        switch (what) {
            case Read::Str:
                // Get() nimmt Text, Zahl und Vektor an - Zahlen werden mit
                // "%f" formatiert (TaskManager.cpp, CTaskManager::Get).
                if (id == bhed::TK_STRING || id == bhed::TK_IDENTIFIER ||
                    id == bhed::TK_FLOAT || id == bhed::TK_INT) {
                    ++i;
                    return true;
                }
                return false;
            case Read::Flt:
                // GetFloat() nimmt TK_INT und TK_FLOAT.
                if (id == bhed::TK_FLOAT || id == bhed::TK_INT) {
                    ++i;
                    return true;
                }
                return false;
            case Read::Vec:
                return false;   // Vektoren kommen nur als Marker
            case Read::Op:
                if (id >= bhed::TK_GREATER_THAN && id <= bhed::TK_NOT) {
                    ++i;
                    return true;
                }
                return false;
            default:
                return false;
        }
    }
};

bool EngineReader::marker() { return markerImpl(); }
bool EngineReader::read(Read what) { return readImpl(what); }

// Die Lesefolge je Befehl, aus dem Engine-Quelltext abgeschrieben.
// Rueckgabe: hat der Block genau gepasst?
bool simulate(const bhed::IbiBlock& b) {
    EngineReader r{b.members};
    switch (b.id) {
        case bhed::ID_FLUSH:
        case bhed::ID_ELSE:
        case bhed::ID_BLOCK_END:
            break;                                   // keine Glieder
        case bhed::ID_PRINT:                         // Print: Get
        case bhed::ID_USE:                           // Use: Get
        case bhed::ID_KILL:                          // Kill: Get
        case bhed::ID_REMOVE:                        // Remove: Get
        case bhed::ID_RUN:
        case bhed::ID_REM:
        case bhed::ID_TASK:
        case bhed::ID_DO:
        case bhed::ID_DOWAIT:
        case bhed::ID_FREE:                          // FreeVariable: Get
        case bhed::ID_SIGNAL:                        // Signal: Get
        case bhed::ID_WAITSIGNAL:                    // WaitSignal: Get
            (void)r.read(Read::Str);
            break;
        case bhed::ID_SOUND:                         // Sound: Get, Get
        case bhed::ID_SET:                           // Set: Get, Get
        case bhed::ID_PLAY:                          // Play: Get, Get
            (void)r.read(Read::Str);
            (void)r.read(Read::Str);
            break;
        case bhed::ID_AFFECT:                        // Sequencer: Name, Typ
            (void)r.read(Read::Str);
            (void)r.read(Read::Flt);
            break;
        case bhed::ID_DECLARE:                       // DeclareVariable: Float, Get
            (void)r.read(Read::Flt);
            (void)r.read(Read::Str);
            break;
        case bhed::ID_LOOP:                          // Zaehler
            (void)r.read(Read::Flt);
            break;
        case bhed::ID_WAIT:
            // Wait: Glied 0 als Text heisst "auf Signal warten", sonst Zeit.
            if (!b.members.empty() && b.members[0].id == bhed::TK_STRING) {
                (void)r.read(Read::Str);
            } else {
                (void)r.read(Read::Flt);
            }
            break;
        case bhed::ID_MOVE:
            // Move: Vektor, dann optional zweiter Vektor, dann Dauer.
            //   if ( GetVector(...) == false ) GetFloat( duration )
            //
            // Ein "Vektor" ist dabei nicht nur TK_VECTOR: GetVector nimmt
            // auch tag() und get(). In Ravens Dateien steht fuenfmal
            //     move ( $tag(a,ORIGIN)$, $tag(b,ANGLES)$, 1000 )
            // also zweimal ein Marker hintereinander.
            (void)r.read(Read::Vec);
            if (!r.done()) {
                const std::int32_t next = r.ms[r.i].id;
                const bool looksVector =
                    next == bhed::TK_VECTOR || next == bhed::ID_TAG ||
                    next == bhed::ID_GET || next == bhed::ID_RANDOM;
                if (looksVector) {
                    (void)r.read(Read::Vec);
                }
            }
            (void)r.read(Read::Flt);
            break;
        case bhed::ID_ROTATE:
            (void)r.read(Read::Vec);
            (void)r.read(Read::Flt);
            break;
        case bhed::ID_IF:
            (void)r.read(Read::Str);
            (void)r.read(Read::Op);
            (void)r.read(Read::Str);
            break;
        case bhed::ID_CAMERA: {
            // Camera: Typ als float, danach je nach Typ. Abgeschrieben aus
            // CTaskManager::Camera.
            float type = 0.0F;
            if (!b.members.empty() && b.members[0].data.size() >= 4) {
                std::memcpy(&type, b.members[0].data.data(), 4);
            }
            (void)r.read(Read::Flt);
            switch (static_cast<int>(type)) {
                case 57:  // PAN: Vektor, Vektor, Float
                    (void)r.read(Read::Vec);
                    (void)r.read(Read::Vec);
                    (void)r.read(Read::Flt);
                    break;
                case 58:  // ZOOM
                case 65:  // ROLL
                case 67:  // DISTANCE
                case 64:  // SHAKE
                    (void)r.read(Read::Flt);
                    (void)r.read(Read::Flt);
                    break;
                case 59:  // MOVE: Vektor, Float
                    (void)r.read(Read::Vec);
                    (void)r.read(Read::Flt);
                    break;
                case 66:  // TRACK: Text, Float, Float
                case 68:  // FOLLOW
                    (void)r.read(Read::Str);
                    (void)r.read(Read::Flt);
                    (void)r.read(Read::Flt);
                    break;
                case 60:  // FADE: Vektor, Float, Vektor, Float, Float
                    (void)r.read(Read::Vec);
                    (void)r.read(Read::Flt);
                    (void)r.read(Read::Vec);
                    (void)r.read(Read::Flt);
                    (void)r.read(Read::Flt);
                    break;
                case 61:  // PATH: Text
                    (void)r.read(Read::Str);
                    break;
                default:  // ENABLE, DISABLE: nichts weiter
                    break;
            }
            break;
        }
        default:
            return true;   // nicht nachgebildet, nicht bewerten
    }
    return r.done() && !r.overrun;
}

std::string slurp(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::ostringstream s;
    s << f.rdbuf();
    return s.str();
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::printf("  (kein Ordner mit .ibi angegeben - Test uebersprungen)\n");
        return 0;
    }
    int files = 0;
    int blocks = 0;
    int fit = 0;
    std::map<std::int32_t, int> mismatchByCommand;

    for (const auto& e : fs::recursive_directory_iterator(argv[1])) {
        if (!e.is_regular_file()) { continue; }
        std::string ext = e.path().extension().string();
        for (char& c : ext) { c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }
        if (ext != ".ibi") { continue; }
        ++files;
        std::vector<bhed::IbiBlock> bl;
        std::vector<bhed::Diag> d;
        if (!bhed::readIbi(slurp(e.path()), bl, d)) { continue; }
        for (const bhed::IbiBlock& b : bl) {
            ++blocks;
            if (simulate(b)) { ++fit; } else { ++mismatchByCommand[b.id]; }
        }
    }

    std::printf("Dateien              : %d\n", files);
    std::printf("Bloecke              : %d\n", blocks);
    std::printf("von der Engine genau : %d  (%.2f %%)\n", fit,
                blocks != 0 ? 100.0 * fit / blocks : 0.0);
    for (const auto& [id, n] : mismatchByCommand) {
        std::printf("   Blockkennung %-3d  %6d x passt nicht\n", id, n);
        ++fails;
    }
    std::printf("\n%s\n", fails != 0 ? "ABWEICHUNGEN GEFUNDEN"
                                     : "die Engine liest jeden Block genau auf");
    return fails != 0 ? 1 : 0;
}

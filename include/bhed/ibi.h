// ibi.h - .ibi lesen und schreiben (Interpreted Block Instructions)
//
// Format, aus code/icarus/BlockStream.cpp der Engine abgelesen und an
// start_cin2.IBI (30 Byte) nachgeprueft:
//
//   Kopf   "IBI\0"            + float 1.57
//   Block  int32 id | int32 numMembers | uint8 flags
//   Glied  int32 id | int32 size | size Byte Daten
//
// Die Glieder liegen FLACH. Ein eingebettetes get()/random()/tag() ist kein
// Unterblock, sondern eine Kennung gefolgt von seinen Argumenten als weitere
// Glieder desselben Blocks - so liest die Engine sie auch:
//   Sequencer.cpp, ParseAffect:  type = (int)(*(float*)GetMemberData(1));
//                                name = (char*)GetMemberData(2);
#ifndef BHED_IBI_H
#define BHED_IBI_H

#include "bhed/commands.h"
#include "bhed/script.h"

#include <cstdint>
#include <string>
#include <vector>

namespace bhed {

// Token- und Befehlskennungen aus CIcarus (IcarusImplementation.h).
// Von DEvahebs IBIToken unabhaengig bestaetigt.
enum : std::int32_t {
    TK_CHAR = 3, TK_STRING = 4, TK_INT = 5, TK_FLOAT = 6, TK_IDENTIFIER = 7,
    TK_VECTOR = 14, TK_GREATER_THAN = 15, TK_LESS_THAN = 16, TK_EQUALS = 17,
    TK_NOT = 18,

    ID_AFFECT = 19, ID_SOUND = 20, ID_MOVE = 21, ID_ROTATE = 22, ID_WAIT = 23,
    ID_BLOCK_START = 24, ID_BLOCK_END = 25, ID_SET = 26, ID_LOOP = 27,
    ID_LOOPEND = 28, ID_PRINT = 29, ID_USE = 30, ID_FLUSH = 31, ID_RUN = 32,
    ID_KILL = 33, ID_REMOVE = 34, ID_CAMERA = 35, ID_GET = 36, ID_RANDOM = 37,
    ID_IF = 38, ID_ELSE = 39, ID_REM = 40, ID_TASK = 41, ID_DO = 42,
    ID_DECLARE = 43, ID_FREE = 44, ID_DOWAIT = 45, ID_SIGNAL = 46,
    ID_WAITSIGNAL = 47, ID_PLAY = 48, ID_TAG = 49, ID_EOF = 50,
};

struct IbiMember {
    std::int32_t id = TK_STRING;
    std::string data;   // Rohbytes, einschliesslich abschliessender Null bei Text
};

struct IbiBlock {
    std::int32_t id = 0;
    std::uint8_t flags = 0;
    std::vector<IbiMember> members;
};

// --- Rohstrom ---------------------------------------------------------
[[nodiscard]] bool readIbi(const std::string& bytes, std::vector<IbiBlock>& out,
                           std::vector<Diag>& diag);
[[nodiscard]] std::string writeIbi(const std::vector<IbiBlock>& blocks);

// --- Uebersetzen ------------------------------------------------------
// .icarus-Baum -> Blockfolge. db wird gebraucht, um blanke Bezeichner
// (FLUSH, CHAN_VOICE, PAN) ihrer Zahl zuzuordnen.
[[nodiscard]] bool compile(const Script& s, const CommandDb& db,
                           std::vector<IbiBlock>& out, std::vector<Diag>& diag);

// Blockfolge -> .icarus-Baum. Fuer die Gegenprobe und als Ersatz fuer
// DEvaheb.
[[nodiscard]] bool decompile(const std::vector<IbiBlock>& blocks,
                             const CommandDb& db, Script& out,
                             std::vector<Diag>& diag);

} // namespace bhed
#endif

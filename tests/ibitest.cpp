// ibitest.cpp - .ibi gegen die einzige echte Datei und gegen sich selbst
#include "bhed/ibi.h"
#include <cstddef>
#include <cstdio>
#include <fstream>
#include <filesystem>
#include <sstream>
namespace fs=std::filesystem;
static int fails=0;
static void expect(const char*w,bool ok){std::printf("  %s  %s\n",ok?"ok  ":"FEHL",w); if(!ok)++fails;}
static std::string slurp(const fs::path&p){std::ifstream f(p,std::ios::binary);std::ostringstream s;s<<f.rdbuf();return s.str();}

// --- Die echte .ibi, eingebaut ----------------------------------------
//
// Diese Probe hat von rc347 bis rc359 NICHTS geprueft. Der Grund war kein
// Fehler im Kopf, sondern eine Weiche: sie erwartete den Pfad einer echten
// .ibi als erstes Argument, CMake reicht ihn nur `if(BEHAVED_IBI)` weiter,
// und niemand hatte ihn gesetzt. Eine Probe, die schweigend uebersprungen
// wird, sieht im Prueflauf genauso aus wie eine bestandene.
//
// Diese dreissig Byte sind `md_roc/start_cin2.IBI` aus MD_Missions_Ep3 -
// dieselbe Datei, gegen die der Kopf bhed/ibi.h das Format abgeglichen hat.
// Sie stehen jetzt hier, damit die Probe IMMER laeuft.
//
//   49 42 49 00              "IBI\0"
//   c3 f5 c8 3f              float 1.57
//   1e 00 00 00              Block-Kennung 30 = ID_USE
//   01 00 00 00              ein Glied
//   00                       Flags
//   04 00 00 00              Glied-Kennung 4 = TK_STRING
//   05 00 00 00              fuenf Byte
//   63 69 6e 32 00           "cin2\0"
//
// Warum nicht selbst erzeugt? Weil dann `writeIbi() == real` gegen unsere
// EIGENE Ausgabe pruefen wuerde und damit gar nichts mehr aussagt. Diese
// Bytes stammen aus Ravens Uebersetzer; das ist der ganze Wert.
//
// Wird trotzdem ein Pfad uebergeben, gilt der. `-` heisst ausdruecklich:
// die eingebauten nehmen.
static const unsigned char kStartCin2[] = {
  0x49,0x42,0x49,0x00, 0xc3,0xf5,0xc8,0x3f,
  0x1e,0x00,0x00,0x00, 0x01,0x00,0x00,0x00, 0x00,
  0x04,0x00,0x00,0x00, 0x05,0x00,0x00,0x00,
  0x63,0x69,0x6e,0x32,0x00
};
int main(int argc,char**argv){
  if(argc<5){std::fprintf(stderr,"Aufruf: ibitest <ibi|-> <skriptdir> <bhc> <hdr> [nachtrag]\n");return 2;}
  bhed::CommandDb db; std::vector<bhed::LoadDiag> ld;
  if(!bhed::loadCommandDb(argv[3],argv[4],db,ld))return 2;
  if(argc>5&&!bhed::loadCommandDb(argv[5],argv[4],db,ld))return 2;

  // --- 1) Ravens echte Datei ---
  std::string real;
  if(std::string(argv[1])!="-"){ real=slurp(argv[1]); }
  if(real.empty()){
    real.assign(reinterpret_cast<const char*>(kStartCin2),sizeof(kStartCin2));
    std::printf("  (eingebaute start_cin2.IBI, %zu Byte)\n",real.size());
  }
  std::vector<bhed::IbiBlock> bl; std::vector<bhed::Diag> d;
  expect("echte .ibi gelesen", bhed::readIbi(real,bl,d));
  expect("genau ein Block", bl.size()==1);
  expect("Block ist ID_USE(30)", !bl.empty()&&bl[0].id==bhed::ID_USE);
  expect("ein Glied, TK_STRING", !bl.empty()&&bl[0].members.size()==1&&bl[0].members[0].id==bhed::TK_STRING);
  expect("Inhalt \"cin2\"", !bl.empty()&&!bl[0].members.empty()&&bl[0].members[0].data==std::string("cin2\0",5));
  expect("zurueckgeschrieben BYTEGLEICH", bhed::writeIbi(bl)==real);

  bhed::Script sc; std::vector<bhed::Diag> dd;
  expect("rueckuebersetzt", bhed::decompile(bl,db,sc,dd));
  const std::string src=bhed::writeScript(sc);
  expect("ergibt use ( \"cin2\" );", src.find("use ( \"cin2\" );")!=std::string::npos);

  // --- 2) alle 1510 Skripte: uebersetzen, zurueck, wieder uebersetzen ---
  int files=0,compiled=0,stable=0,nodiag=0,blocks=0;
  std::map<std::string,int> why;
  for(const auto&e:fs::recursive_directory_iterator(argv[2])){
    if(!e.is_regular_file()||e.path().extension()!=".icarus")continue;
    ++files;
    bhed::Script s; std::vector<bhed::Diag> rd;
    if(!bhed::readScript(slurp(e.path()),s,rd))continue;
    std::vector<bhed::IbiBlock> b1; std::vector<bhed::Diag> cd;
    const bool okc=bhed::compile(s,db,b1,cd);
    ++compiled; blocks+=(int)b1.size();
    if(okc)++nodiag; else for(auto&x:cd)++why[x.message.substr(0,x.message.find(':'))];
    // Rohstrom-Festpunkt
    const std::string bytes=bhed::writeIbi(b1);
    std::vector<bhed::IbiBlock> b2; std::vector<bhed::Diag> d2;
    if(bhed::readIbi(bytes,b2,d2)&&bhed::writeIbi(b2)==bytes)++stable;
  }
  std::printf("\n  Skripte             : %d\n  uebersetzt          : %d (%d Bloecke)\n"
              "  ohne Anmerkung      : %d\n  Rohstrom-Festpunkt  : %d\n",
              files,compiled,blocks,nodiag,stable);
  for(auto&[k,v]:why)std::printf("    %5d  %s\n",v,k.c_str());
    // --- Eine .ibi wird am INHALT erkannt, nicht an der Endung ----------
    //
    // Gemeldet: eine .ibi laesst sich oeffnen, aber danach ist nichts drin.
    //
    // Der Weg ueber die Platte rief nur readScript() - und eine .ibi ist
    // uebersetzter Bytecode, kein Text. Der Textleser findet darin keinen
    // Befehl und liefert ein LEERES Skript: kein Absturz, keine Meldung,
    // nur ein leerer Baum.
    //
    // Diese Probe haelt die Erkennung fest und - wichtiger - dass der
    // Textleser bei so einer Datei wirklich nichts liefert. Sonst waere
    // nicht klar, warum die Unterscheidung noetig ist.
    {
        // Die ersten Bytes einer echten .ibi: "IBI\0" und die Version.
        std::string echt = "IBI";
        echt.push_back('\0');
        echt += "\xc3\xf5\xc8\x3f";   // 1.57f

        auto istIbi = [](const std::string& roh) {
            return roh.size() > 4 && roh.compare(0, 3, "IBI") == 0;
        };

        expect("eine .ibi wird erkannt", istIbi(echt));
        expect("ein Textskript nicht",
               !istIbi("affect ( \"ani1\", FLUSH )\n{\n}\n"));
        expect("und etwas zu Kurzes auch nicht", !istIbi("IBI"));

        // Der Beleg fuer den Fehler: der TEXTLESER findet darin keinen
        // Befehl. (Seit rc582 behaelt er unlesbare Zeilen wortgetreu als
        // Kommentarknoten - Befehle bleiben es trotzdem null.)
        bhed::Script leer;
        std::vector<bhed::Diag> dl;
        (void)bhed::readScript(echt, leer, dl);
        std::size_t befehle = 0;
        for (const bhed::Node& n : leer.nodes) {
            if (n.kind == bhed::Node::Kind::Command) { ++befehle; }
        }
        std::printf("  Textleser auf einer .ibi: %zu Knoten, %zu Befehle\n",
                    leer.nodes.size(), befehle);
        expect("der Textleser findet in einer .ibi keinen Befehl",
               befehle == 0);
    }
    // --- Wie IBIze: rem, dowait, Einfeld-if, Zahlen ---------------------
    //
    // shank, 27.09.: eine mit behaved uebersetzte cin2_jedi.IBI -> im Spiel
    // "Invalid block ID". Der Byte-Vergleich aller 1510 Raven-Skripte mit
    // IBIze.exe (1508 byte-gleich, 2 kann IBIze nicht) fand vier Dinge; hier
    // steht je eins davon fest.
    {
        const char* src2 =
            "rem ( \"kommt nicht in die ibi\" );\n"
            "dowait ( \"t1\" );\n"
            "if ( $random( 0, 1 ) > 0.800000$ )\n{\n}\n"
            "camera ( /*@CAMERA_COMMANDS*/ PAN, < 1.993 -675.587 0.000 >, < 0.000 0.000 0.000 >, 0 );\n";
        bhed::Script s2; std::vector<bhed::Diag> r2;
        (void)bhed::readScript(src2, s2, r2);
        std::vector<bhed::IbiBlock> b; std::vector<bhed::Diag> c2;
        (void)bhed::compile(s2, db, b, c2);
        bool rem = false;
        for (const auto& x : b) { if (x.id == bhed::ID_REM) rem = true; }
        expect("rem kommt nicht in die .ibi (Sequencer: invalid block ID)", !rem);
        expect("dowait wird do + wait (wie IBIze)",
               b.size() >= 2 && b[0].id == bhed::ID_DO && b[1].id == bhed::ID_WAIT);
        // if: random-Marker, zwei Zahlen, Vergleich, Zahl
        bool ifDrei = false;
        for (const auto& x : b) {
            if (x.id != bhed::ID_IF) continue;
            bool vergleich = false;
            for (const auto& m : x.members) { if (m.id == bhed::TK_GREATER_THAN) vergleich = true; }
            ifDrei = vergleich && !x.members.empty() && x.members.back().id == bhed::TK_FLOAT;
        }
        expect("Einfeld-if behaelt seinen Vergleich (> 0.8)", ifDrei);
        // Zahlen wie IBIze (x87, Ziffer fuer Ziffer): 1.993 -> a0 1a ff 3f,
        // -675.587 -> 92 e5 28 c4. atof gaebe fuer -675.587 ein anderes Bit.
        const std::string bytes2 = bhed::writeIbi(b);
        expect("1.993 wie IBIze", bytes2.find(std::string("\xa0\x1a\xff\x3f", 4)) != std::string::npos);
        expect("-675.587 wie IBIze", bytes2.find(std::string("\x92\xe5\x28\xc4", 4)) != std::string::npos);
    }
  expect("jeder Rohstrom ist Festpunkt", stable==compiled);
  std::printf("\n%s (%d Fehlschlaege)\n",fails?"FEHLGESCHLAGEN":"bestanden",fails);
  return fails?1:0;
}



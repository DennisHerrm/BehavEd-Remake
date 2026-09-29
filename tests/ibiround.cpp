// ibiround.cpp - unser Uebersetzer gegen Ravens IBIZE.EXE
//
// Jede .ibi wird gelesen, nach .icarus zurueckuebersetzt, wieder eingelesen,
// neu uebersetzt und byteweise mit dem Original verglichen. Stimmt das
// Ergebnis, erzeugen wir dieselben Bytes wie Ravens Werkzeug.
//
// Gegen 1011 Dateien des Story-Modus laeuft das auf 100 %. Bis dahin waren
// es fuenf Anlaeufe, und jeder hat eine Eigenheit des Formats aufgedeckt:
//   62 %  Vektoren sind kein 12-Byte-Glied, sondern ein Marker plus drei
//         einzelne Floats; TK_INT kommt nie vor, alle Zahlen sind Floats
//   88 %  Aufzaehlungswerte stehen als float da (FLUSH -> 56.0), ausser bei
//         sound, wo der Kanal als Text steht
//   90 %  %.3f verliert Werte, die sich damit nicht wiederherstellen lassen
//   99 %  auch Vergleichsoperatoren sind Marker mit vier Byte Inhalt
//  100 %  ein Float darf nur dort zum Aufzaehlungsnamen werden, wo die
//         Signatur eine %i-Typmenge vorsieht - sonst wird aus
//         camera(ZOOM, 53.640, 0) ein camera(ZOOM, ANGLES, 0), weil 53
//         zufaellig TYPE_ANGLES ist
#include "bhed/ibi.h"
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
namespace fs=std::filesystem;
static std::string slurp(const fs::path&p){std::ifstream f(p,std::ios::binary);std::ostringstream s;s<<f.rdbuf();return s.str();}
static std::string hexAt(const std::string&a,const std::string&b){
  size_t i=0; while(i<a.size()&&i<b.size()&&a[i]==b[i])++i;
  char buf[200]; std::string ha,hb;
  for(size_t k=i;k<i+8&&k<a.size();++k){char t[4];std::snprintf(t,4,"%02x ",(unsigned char)a[k]);ha+=t;}
  for(size_t k=i;k<i+8&&k<b.size();++k){char t[4];std::snprintf(t,4,"%02x ",(unsigned char)b[k]);hb+=t;}
  std::snprintf(buf,sizeof(buf),"ab Byte %zu:  Raven %s |  unser %s",i,ha.c_str(),hb.c_str());
  return buf;
}
int main(int argc,char**argv){
  bhed::CommandDb db; std::vector<bhed::LoadDiag> ld;
  if(!bhed::loadCommandDb(argv[2],argv[3],db,ld))return 2;
  if(argc>4)(void)bhed::loadCommandDb(argv[4],argv[3],db,ld);
  int files=0,same=0,diff=0; std::map<std::string,int> why; std::vector<std::string> ex;
  for(const auto&e:fs::recursive_directory_iterator(argv[1])){
    if(!e.is_regular_file())continue;
    auto ext=e.path().extension().string();
    for(auto&c:ext)c=(char)tolower((unsigned char)c);
    if(ext!=".ibi")continue;
    ++files;
    const std::string orig=slurp(e.path());
    std::vector<bhed::IbiBlock> bl; std::vector<bhed::Diag> d;
    if(!bhed::readIbi(orig,bl,d))continue;
    bhed::Script sc; std::vector<bhed::Diag> dd;
    (void)bhed::decompile(bl,db,sc,dd);
    // ueber den Text: schreiben, wieder lesen, uebersetzen
    const std::string text=bhed::writeScript(sc);
    bhed::Script sc2; std::vector<bhed::Diag> rd;
    (void)bhed::readScript(text,sc2,rd);
    std::vector<bhed::IbiBlock> b2; std::vector<bhed::Diag> cd;
    (void)bhed::compile(sc2,db,b2,cd);
    const std::string back=bhed::writeIbi(b2);
    if(back==orig)++same; else {
      ++diff;
      if(ex.size()<6)ex.push_back(e.path().string()+"  "+hexAt(orig,back));
      for(auto&x:cd)++why[x.message.substr(0,60)];
    }
  }
  std::printf("Dateien                    : %d\n",files);
  if(files==0){ std::printf("  (kein Ordner mit .ibi angegeben - Test uebersprungen)\n"); return 0; }
  std::printf("nach Rundlauf BYTEGLEICH   : %d  (%.2f %%)\n",same,files?100.0*same/files:0.0);
  std::printf("abweichend                 : %d\n",diff);
  for(auto&[k,v]:why)std::printf("   %6d  %s\n",v,k.c_str());
  for(auto&x:ex)std::printf("   %s\n",x.c_str());
  return diff==0?0:1;
}

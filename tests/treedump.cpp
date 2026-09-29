// treedump.cpp - Baumansicht als Text, zum Nachsehen und Vergleichen.
#include "bhed/tree.h"
#include <cstdio>
#include <fstream>
#include <sstream>
int main(int argc,char**argv){
  if(argc<4){std::fprintf(stderr,"Aufruf: treedump <skript> <bhc> <headerdir> [nachtrag] [--types] [--g] [--nofold]\n");return 2;}
  bhed::CommandDb db; std::vector<bhed::LoadDiag> ld;
  if(!bhed::loadCommandDb(argv[2],argv[3],db,ld))return 2;
  bhed::TreeOptions o;
  for(int i=4;i<argc;++i){
    std::string a=argv[i];
    if(a=="--types")o.showTypes=true; else if(a=="--g")o.gFloats=true;
    else if(a=="--nofold")o.foldMacros=false;
    else (void)bhed::loadCommandDb(a,argv[3],db,ld);
  }
  std::ifstream f(argv[1],std::ios::binary); std::ostringstream ss; ss<<f.rdbuf();
  bhed::Script s; std::vector<bhed::Diag> d;
  if(!bhed::readScript(ss.str(),s,d))std::printf("(Strukturfehler)\n");
  std::vector<bhed::Row> rows; bhed::buildTree(s,db,o,rows);
  for(const auto&r:rows){
    for(int i=0;i<r.depth;++i)std::printf("  ");
    std::printf("%-14s %s%s\n",r.icon.c_str(),r.text.c_str(),
      r.childCount&&r.what==bhed::Row::What::Macro?"  [+]":"");
  }
  std::printf("\n%zu Zeilen\n",rows.size());
}

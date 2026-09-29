// Rundlauf: Datei -> Modell -> Kennungen vergeben -> Datei. Muss bytegleich
// bleiben. Das ist der Waechter dafuer, dass die Kennung NICHT geschrieben
// wird.
#include "bhed/edit.h"
#include "bhed/script.h"
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
int main(int argc,char**argv){
    int schlecht=0, geprueft=0;
    for(int i=1;i<argc;i++){
        std::ifstream f(argv[i], std::ios::binary);
        std::stringstream ss; ss<<f.rdbuf();
        const std::string vorher = ss.str();
        bhed::Script s; std::vector<bhed::Diag> d;
        (void)bhed::readScript(vorher, s, d);
        bhed::Document doc{s};
        doc.vergibKennungen();                 // <- der neue Schritt
        const std::string nachher = bhed::writeScript(doc.script());
        ++geprueft;
        if(nachher != vorher){
            printf("  ABWEICHUNG %s (%zu -> %zu Bytes)\n", argv[i],
                   vorher.size(), nachher.size());
            ++schlecht;
        } else {
            printf("  bytegleich %s (%zu Bytes)\n", argv[i], vorher.size());
        }
    }
    printf("%d von %d bytegleich\n", geprueft-schlecht, geprueft);
    return schlecht?1:0;
}

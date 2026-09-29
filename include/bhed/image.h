// Bilder laden.
//
// JKA liest drei Formate - `R_ImageLoader_Add` in `tr_image_load.cpp` meldet
// `jpg`, `png` und `tga` an. Fuer Effekttexturen ist TGA das haeufigste, weil
// es einen Alphakanal ohne Verluste traegt; JPEG kommt bei grossen, undurchsich-
// tigen Flaechen vor.
//
// Ergebnis ist immer RGBA mit acht Bit je Kanal, egal was in der Datei stand.
// Der Renderer bekommt damit genau ein Format statt fuenf Sonderfaelle.
//
// Uebernommen aus efxed, unveraendert bis auf den Namensraum.
#ifndef BHED_IMAGE_H
#define BHED_IMAGE_H

#include <cstdint>
#include <string>
#include <vector>

namespace bhed::image {

struct Image {
    int width = 0;
    int height = 0;
    std::vector<unsigned char> rgba;  // width*height*4, oben links beginnend
    bool ok = false;
    std::string error;

    bool hasAlpha = false;  // ob die Datei ueberhaupt einen Alphakanal hatte
};

// Erkennt das Format am Inhalt, nicht an der Endung.
//
// Das ist kein Uebereifer: in JKA-Mods liegen regelmaessig JPEG-Dateien mit der
// Endung `.tga`, weil jemand sie umbenannt statt umgewandelt hat. Die Engine
// stolpert darueber, ein Editor sollte es besser koennen - und wenigstens sagen,
// was wirklich drinsteht.
Image decode(const unsigned char* data, size_t size);

// Targa. Unterstuetzt die Faelle, die in JKA vorkommen: 24 und 32 Bit,
// unkomprimiert und lauflaengenkodiert, mit und ohne gedrehte Zeilenrichtung.
Image decodeTga(const unsigned char* data, size_t size);

// JPEG, Baseline (sequenziell, Huffman-kodiert).
//
// Selbst geschrieben, wie der Auspacker - das Projekt hat bis hierhin keine
// einzige Abhaengigkeit, und ein JPEG-Decoder laesst sich gegen fremde
// Referenzdaten genauso sauber pruefen wie Deflate.
//
// **Nicht unterstuetzt: progressive JPEGs.** Sie kommen in Spieldaten praktisch
// nicht vor (der Vorteil ist ein frueher Vorschau-Aufbau beim Laden ueber eine
// langsame Leitung, was fuer eine Textur sinnlos ist), brauchen aber einen
// komplett anderen Dekodierweg. Eine solche Datei wird abgewiesen und beim
// Namen genannt, statt Muell zu liefern.
Image decodeJpeg(const unsigned char* data, size_t size);

// PNG. Nutzt den vorhandenen Deflate-Auspacker.
//
// JKA-Effekte benutzen praktisch nur TGA und JPG - PNG kam erst mit OpenJK
// dazu. Es ist trotzdem drin, weil es fast nichts kostet: der Auspacker war
// schon fuer die `.pk3`-Dateien noetig, und was bleibt, sind die Zeilenfilter.
Image decodePng(const unsigned char* data, size_t size);

// Ein Bild als Targa schreiben - fuer Bildschirmfotos.
//
// Targa und nicht PNG, obwohl der Auspacker da ist: **Packen** ist etwas
// anderes als Auspacken, und einen Deflate-Packer zu schreiben, nur um ein
// Bildschirmfoto kleiner zu machen, waere unverhaeltnismaessig. Quake und seine
// Nachfolger schreiben ihre Bildschirmfotos ebenfalls als Targa.
std::vector<unsigned char> encodeTga(const unsigned char* rgba, int width,
                                     int height);

// Welches Format steckt drin? Fuer Meldungen.
enum class Format { Unknown, Tga, Png, Jpeg };
Format sniff(const unsigned char* data, size_t size);
const char* formatName(Format format);

// --- Der Schluessel fuer eine Bildtabelle -------------------------------
//
// Nachgebaut nach GenerateImageMappingName() in
// code/rd-vanilla/tr_image.cpp: klein geschrieben, ohne Endung,
// Rueckwaertsstriche zu Schraegstrichen.
//
// Die Endung MUSS weg. Dieselbe Textur steht in den Archiven mal als .jpg
// und mal als .tga, und die Suche probiert beide - ohne das Kuerzen laege
// sie zweimal in der Tabelle und wuerde zweimal entziffert.
std::string mappingName(const std::string& name);

// Ein Bild auf eine Hoechstkantenlaenge verkleinern.
//
// Gemittelt ueber den ganzen Block, nicht ein Punkt daraus - sonst
// verliert eine 1024er Textur beim Weg auf 512 drei Viertel ihrer
// Bildpunkte, und feine Muster kommen als Flimmern zurueck.
//
// Stand dreimal ausgeschrieben da: in shrink() der Modellansicht, inmitten
// von loadTextureFor() fuer die Karte, und im Vorwaermen. Drei Abschriften
// derselben Rechnung laufen auseinander - und hier waere die Folge, dass
// dasselbe Bild je nach Weg unterschiedlich aussieht.
//
// Gibt Breite, Hoehe und die Bildpunkte zurueck; das Ziel liefert der
// Aufrufer, weil TextureSet dem Bildmodul nicht bekannt ist.
void shrinkTo(const Image& im, int maxSize, int& outW, int& outH,
              std::vector<unsigned char>& outRgba);

}  // namespace bhed::image
#endif

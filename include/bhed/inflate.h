// Deflate auspacken (RFC 1951).
//
// Der Schluessel zu allem Weiteren: die Texturen, Modelle und Klaenge einer
// JKA-Installation liegen in `.pk3`-Dateien, und deren Inhalte sind fast immer
// deflate-komprimiert. Ohne Auspacker bleibt der Materialsuchlauf bei den
// Namen stehen.
//
// Selbst geschrieben statt zlib eingebunden: es sind etwa dreihundert Zeilen,
// das Format ist seit 1996 unveraendert, und es haelt die Abhaengigkeitsliste des
// Programms bei null. Der Bau soll auf einem frischen Rechner ohne
// Vorbereitung durchlaufen.
//
// Ausschliesslich lesend, und gegen boeswillige Eingaben abgesichert: eine
// `.pk3` liegt im Spielordner, und da kommt alles Moegliche her. Jede Grenze
// wird geprueft, und die erwartete Groesse ist eine harte Obergrenze - ein
// Archiv, das behauptet, eine Datei sei 3 GB gross, bekommt kein Gigabyte
// Speicher.
//
// Uebernommen aus efxed, unveraendert bis auf den Namensraum. Dort ist es
// gegen boesartige Eingaben abgesichert und im Einsatz erprobt.
#ifndef BHED_INFLATE_H
#define BHED_INFLATE_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace bhed::inflate {

struct Result {
    std::vector<unsigned char> data;
    bool ok = false;
    std::string error;
};

// Rohes Deflate ohne zlib-Kopf - so liegt es in einer Zip-Datei.
//
// expectedSize kommt aus dem Zip-Verzeichnis und dient als Obergrenze.
Result raw(const unsigned char* data, size_t size, size_t expectedSize);

}  // namespace bhed::inflate
#endif

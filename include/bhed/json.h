// json.h - ein kleiner JSON-Leser und -Schreiber
//
// Paket tour (rc601): der Leser stand bis rc600 im namenlosen Namensraum von
// src/update.cpp (GitHub-Antworten). Die Tutorials im Programm lesen ihre
// Daten (handbuch/<sprache>/touren.json) und den Suchindex des Handbuchs
// ebenfalls als JSON - also eine gemeinsame Stelle, dazu ein Schreiber.
//
// Der Leser versteht die ganze Grammatik (sonst verliert er bei
// verschachtelten Objekten den Faden), haelt aber nur Zeichenketten, Zahlen,
// Wahrheitswerte und die Struktur fest. Mehr als 64 Ebenen weist er ab.
#ifndef BHED_JSON_H
#define BHED_JSON_H

#include <string>
#include <utility>
#include <vector>

namespace bhed::json {

struct Wert {
    enum class Art { Null, Bool, Zahl, Text, Liste, Objekt } art = Art::Null;
    std::string text;
    double zahl = 0.0;   // bei Bool: 1 oder 0
    std::vector<Wert> liste;
    std::vector<std::pair<std::string, Wert>> objekt;

    [[nodiscard]] const Wert* feld(const std::string& k) const;
    [[nodiscard]] std::string textVon(const std::string& k) const;
    // rc601: Zahl bzw. Wahrheitswert eines Feldes, sonst die Vorgabe.
    [[nodiscard]] double zahlVon(const std::string& k, double vorgabe = 0.0) const;
    [[nodiscard]] bool wahrVon(const std::string& k, bool vorgabe = false) const;
    [[nodiscard]] bool wahr() const { return art == Art::Bool && zahl != 0.0; }

    // Bauen (zum Schreiben).
    static Wert alsText(std::string t);
    static Wert alsZahl(double z);
    static Wert alsBool(bool b);
    static Wert alsListe();
    static Wert alsObjekt();
    // Ein Feld eines Objekts setzen; ein vorhandenes gleichen Namens wird
    // ersetzt, nicht verdoppelt.
    Wert& setze(const std::string& k, Wert w);
};

// Genau einen JSON-Wert lesen. false bei einem Fehler oder bei Resten hinter
// dem Wert; `fehler` nennt dann die Stelle.
[[nodiscard]] bool lies(const std::string& text, Wert& out, std::string* fehler = nullptr);

// Einen Wert als JSON schreiben (kompakt, UTF-8 bleibt unveraendert).
[[nodiscard]] std::string schreibe(const Wert& w);

// Nur eine Zeichenkette, mit Anfuehrungszeichen: " und \ werden escaped,
// Steuerzeichen als \n, \t ... oder \u00XX.
[[nodiscard]] std::string zeichenkette(const std::string& s);

}  // namespace bhed::json

#endif

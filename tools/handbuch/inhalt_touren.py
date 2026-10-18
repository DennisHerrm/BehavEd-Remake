"""Die Tutorials im Programm (rc601, Paket tour): worauf jeder Schritt zeigt
und wann er erledigt ist.

Der Betreuer wuenschte Tutorials, bei denen das Programm selbst zeigt, welche
Knoepfe man druecken muss, und Schritt fuer Schritt fuehrt ("Erst ohne KI, KI
spaeter"). Text, Bild und Bildunterschrift jedes Schritts kommen aus den
Handbuchquellen (inhalt_schritte.py) - hier steht nur, was das Programm dazu
braucht. gen_handbuch.py schreibt daraus handbuch/<sprache>/touren.json; ein
unbekannter Name bricht den Bau ab, wie eine unbekannte {{Kennung}}.

Schluessel: der Bildname des Schritts (steht in inhalt_schritte.py schon an
jedem Schritt), oder (Seite, Ansicht, Schrittnummer) fuer Schritte ohne Bild
oder mit einem Bild, das mehrere Schritte teilen.

Felder (Grammatik in include/bhed/tour.h):
  ziel        worauf gezeigt wird, ";" fuer mehrere (nummerierte Rahmen)
  fertig      wann der Schritt erledigt ist, "|" = oder; "manuell" = Knopf Weiter
  voraus      "<pruefung> < <ziel>": gilt sie nicht, zeigt die Tour auf den Oeffner
  einrichten  was "Mach es" ausfuehrt, ";" fuer mehrere - es muss die
              Pruefung `fertig` erfuellen (tests/tourtest.cpp prueft das)
  sicher      "Mach es" erlaubt - nie bei Dateidialogen (Speichern,
              Uebersetzen eines unbenannten Skripts): ein Windows-Dialog haelt
              die Nachrichtenschleife an (gui/selbsttest.cpp, Kopf); nie bei
              Schritten, die man nur ansieht (schwebt)
  ueber       was "Ueberspringen" ausfuehrt (rc601, Code-Pruefung) - nur, was
              der naechste Schritt braucht. Leer: `einrichten` ohne befehl und
              wert (include/bhed/tour.h, ueberspringAktionen) - Ueberspringen
              aendert das Skript nie (vorher setzte es bei k-befehl-einfuegen
              ein streunendes `wait` ein)

Erste Fassung: die fuenf Skript-Tutorials ohne Spieldaten, Ansicht Classic.
"""


def A(ziel="", fertig="manuell", voraus="", einrichten="", sicher=False, ueber=""):
    return {"ziel": ziel, "fertig": fertig, "voraus": voraus, "einrichten": einrichten, "sicher": sicher,
            "ueberspringen": ueber}


# Die Touren dieser Fassung (Handbuchseiten) und ihre Ansichten.
TOUREN = ["k-neues-skript", "k-befehl-einfuegen", "k-werte-bearbeiten", "k-zeilen-ordnen", "k-uebersetzen"]
ANSICHTEN = ["classic"]

# Die Ereignisliste gibt es nur im Modus Events - sonst zeigt die Tour auf
# dessen Knopf in der Modusleiste.
EVENTS = "modus:0 < knopf:ViewEvents@modebar"
# Der Ereigniseditor muss offen sein - sonst zeigt die Tour auf den Baum.
EDITOR = "editor < baum"
AUSWAHL = "auswahl < baum"

NACH_BILD = {
    # --------------------------------------------------------- k-neues-skript
    # "+" oder File -> New: beides gibt ein leeres Skript (New nimmt dafuer
    # auch einen leeren Reiter, den es schon gibt - deshalb "leer"). Beide
    # Wege werden gezeigt (1, 2); die Menuekette zeigt erst "File", aufgeklappt
    # "New".
    "s_neu_plus": A("reiter:+;menue:MenuFile>FileNew", "reiter+|leer", einrichten="neuerReiter", sicher=True),
    # "Hover over it to read what it does" - oder gleich doppelklicken. Kein
    # "Mach es": Schweben kann das Programm nicht fuer dich (Code-Pruefung:
    # "Do it for me ... cannot complete the step"); Ueberspringen stellt Events.
    "s_neu_affect_liste": A("eintrag:affect", "schwebt|befehl:affect", EVENTS, "modus:0"),
    "s_neu_affect_baum": A("eintrag:affect", "befehl:affect", EVENTS, "modus:0;befehl:affect", True),
    # Erledigt, wenn affect einen Namen hat (DEFAULT ist Ravens Platzhalter) -
    # also nach Ok im Editor.
    # Ueberspringen schreibt keinen Wert, es schliesst nur den Editor (der
    # naechste Schritt braucht den Baum).
    "s_neu_editor": A("knopfN:##v@###editor#0;knopf:EditorOk@###editor", "wert:affect#0:0",
                      "editor:affect < zeile:affect#0", "wert:affect#0:0=kyle", True, ueber="editorzu"),
    "s_neu_wait_liste": A("zeile:affect#0;eintrag:wait", "kind:affect>wait", EVENTS,
                          "modus:0;waehle:affect#0;befehl:wait", True),
    "s_neu_ergebnis": A("zeile:wait#0", "manuell"),
    # Speichern fragt beim ersten Mal nach einem Namen (Dateidialog): nie "Mach es".
    "s_neu_speichern": A("knopf:FileSave", "gespeichert"),

    # --------------------------------------------------------- k-befehl-einfuegen
    # Jeder Weg (Doppelklick, Einfg, Ziehen, Rechtsklick) erfuellt dieselbe
    # Pruefung: ein Befehl mehr. So zaehlt jeder Weg, den das Handbuch nennt.
    # "Mach es" fuegt ein wait ein; Ueberspringen stellt nur Events (kein
    # streunendes wait im Skript).
    "s_ein_doppel": A("baum;eintrag:wait", "knoten+", EVENTS, "modus:0;befehl:wait", True),
    "s_ein_einfg": A("eintrag:wait;baum", "knoten+", EVENTS, "modus:0;befehl:wait", True),
    "s_ein_ziehen": A("eintrag:wait;baum", "knoten+", EVENTS, "modus:0;befehl:wait", True),
    "s_ein_kontext": A("baum", "knoten+", "", "befehl:wait", True),

    # --------------------------------------------------------- k-werte-bearbeiten
    "h_editor": A("baum", "editor", "", "editor", True),
    "s_wert_felder": A("knopfN:##v@###editor#0;knopfN:##v@###editor#1", "manuell", EDITOR),
    "s_wert_liste": A("knopfN:##combo@###editor#0;knopfN:##combo@###editor#1", "manuell", EDITOR),
    "s_wert_datei": A("knopf:EditorBrowse@###editor;knopf:EditorPlay@###editor", "manuell", EDITOR),
    "s_wert_helper": A("knopf:EditorHelper@###editor", "manuell", EDITOR),
    "s_wert_expr": A("knopf:EditorExpr@###editor", "manuell", EDITOR),
    "s_wert_ok": A("knopf:EditorOk@###editor;knopf:EditorCancel@###editor", "editorzu", "", "editorzu", True),

    # --------------------------------------------------------- k-zeilen-ordnen
    "s_auswahl": A("baum", "auswahl", "", "waehle:*", True),
    "s_verschieben": A("gruppe:ActMoveUp,ActMoveDown", "aenderung+", AUSWAHL),
    "s_clone": A("knopf:ActClone", "knoten+", AUSWAHL),
    "s_copy": A("knopf:ActCopy;knopf:ActPaste", "knoten+", AUSWAHL),
    "s_rem": A("knopf:ActRem", "rem+", AUSWAHL),

    # --------------------------------------------------------- k-uebersetzen
    # Uebersetzen sichert zuerst - ein unbenanntes Skript fragt nach dem
    # Namen (Dateidialog): nie "Mach es".
    "t1_6": A("knopf:Compile", "uebersetzt+"),
    "s_comp_fehler": A("knopf:Compile", "manuell"),
    "s_comp_meldung": A("meldungen", "fenster:messages", "", "fenster:messages", True),
    "s_comp_editor": A("knopf:Compile", "uebersetzt+"),
}

# (Seite, Ansicht, Nummer ab 1) -> Angaben; gehen den Angaben je Bild vor.
NACH_SCHRITT = {}


def angaben(seite, ansicht, nr, bild):
    """Die Angaben eines Schritts oder None."""
    return NACH_SCHRITT.get((seite, ansicht, nr)) or (NACH_BILD.get(bild) if bild else None)


# --- Was das Programm kennt (include/bhed/tour.h) - der Bau prueft dagegen ---
ZIELARTEN = {"knopf", "knopfN", "menue", "gruppe", "eintrag", "zeile", "reiter", "fenster", "baum", "meldungen"}
PRUEFUNGEN = {"reiter", "knoten", "aenderung", "rem", "uebersetzt", "gespeichert", "befehl", "leer", "kind", "wert",
              "auswahl", "editor", "editorzu", "fenster", "modus", "ansicht", "schwebt", "geklickt", "manuell"}
AKTIONEN = {"neuerReiter", "befehl", "waehle", "wert", "modus", "ansicht", "editor", "editorzu", "fenster"}


def pruefe(seite, ansicht, nr, a, kennung_bekannt):
    """Bricht den Bau bei einem unbekannten Namen ab (wie eine unbekannte
    {{Kennung}}, LIESMICH.md). Die volle Grammatik prueft tests/tourtest.cpp
    an der erzeugten Datei."""
    wo = "%s/%s Schritt %d" % (seite, ansicht, nr)

    def ziele(text, was):
        for z in [t.strip() for t in text.split(";") if t.strip()]:
            art, _, arg = z.partition(":")
            if art not in ZIELARTEN:
                raise SystemExit("%s: unbekannte Zielart in %s: %s" % (wo, was, z))
            if art in ("knopf", "menue", "gruppe"):
                namen = arg.split("@")[0]
                for k in namen.replace(">", ",").split(","):
                    if not kennung_bekannt(k.strip()):
                        raise SystemExit("%s: unbekannte Kennung {{%s}} in %s" % (wo, k.strip(), was))

    def pruefungen(text, was):
        for p in [t.strip() for t in text.split("|") if t.strip()]:
            name = p.rstrip("+").split(":")[0].strip()
            if name not in PRUEFUNGEN:
                raise SystemExit("%s: unbekannte Pruefung in %s: %s" % (wo, was, p))

    ziele(a["ziel"], "ziel")
    pruefungen(a["fertig"] or "manuell", "fertig")
    if a["voraus"]:
        p, _, o = a["voraus"].partition("<")
        pruefungen(p, "voraus")
        ziele(o, "voraus")
    for akt in [t.strip() for t in (a["einrichten"] + ";" + a.get("ueberspringen", "")).split(";") if t.strip()]:
        if akt.split(":")[0] not in AKTIONEN:
            raise SystemExit("%s: unbekannte Aktion: %s" % (wo, akt))

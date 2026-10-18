/* Handbuch BehavEd-Remake: Suche, Hell/Dunkel, Bild gross, Sprung zum Treffer.
 *
 * Die Suche liest window.HANDBUCH_INDEX (suche.js der Sprache): je Abschnitt
 * Seite, Anker, Titel und Text. Gesucht wird ohne Gross/Klein, alle Woerter
 * muessen vorkommen (Chinesisch/Japanisch: als Zeichenfolge). Ein Klick
 * springt auf die Seite an den Abschnitt und markiert dort den Begriff.
 */
(function () {
  "use strict";
  var index = window.HANDBUCH_INDEX || [];
  var texte = window.HANDBUCH_TEXTE || { keine_treffer: "Nothing found.", treffer_auf: "on" };
  var feld = document.getElementById("suchfeld");
  var liste = document.getElementById("treffer");
  var wahl = -1;

  function norm(s) {
    return (s || "").normalize("NFKC").toLowerCase();
  }
  function woerter(q) {
    return norm(q).split(/\s+/).filter(function (w) { return w.length > 0; });
  }
  function escHtml(s) {
    return s.replace(/[&<>"]/g, function (c) { return { "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;" }[c]; });
  }
  function markiere(text, ws) {
    var t = escHtml(text);
    ws.forEach(function (w) {
      var e = escHtml(w).replace(/[.*+?^${}()|[\]\\]/g, "\\$&");
      t = t.replace(new RegExp("(" + e + ")", "gi"), "<mark>$1</mark>");
    });
    return t;
  }
  function schnipsel(text, ws) {
    var n = norm(text);
    var at = n.indexOf(ws[0]);
    if (at < 0) { return text.slice(0, 140); }
    var von = Math.max(0, at - 60);
    return (von > 0 ? "…" : "") + text.slice(von, von + 170) + (von + 170 < text.length ? "…" : "");
  }
  function suche(q) {
    var ws = woerter(q);
    if (!ws.length) { return []; }
    var aus = [];
    index.forEach(function (e) {
      var titel = norm(e.s + " " + e.t);
      var alles = titel + " " + norm(e.x);
      for (var i = 0; i < ws.length; i++) { if (alles.indexOf(ws[i]) < 0) { return; } }
      var punkte = 0;
      ws.forEach(function (w) {
        if (norm(e.s).indexOf(w) >= 0) { punkte += 10; }
        if (norm(e.t).indexOf(w) >= 0) { punkte += 4; }
        punkte += Math.min(5, norm(e.x).split(w).length - 1);
      });
      aus.push({ e: e, p: punkte });
    });
    aus.sort(function (a, b) { return b.p - a.p; });
    return aus.slice(0, 40);
  }
  function zeige() {
    var q = feld.value;
    var ws = woerter(q);
    var r = suche(q);
    wahl = -1;
    if (!ws.length) { liste.hidden = true; return; }
    liste.innerHTML = r.length ? r.map(function (x) {
      var e = x.e;
      var ziel = e.p + "?q=" + encodeURIComponent(q) + (e.a ? "#" + e.a : "");
      return '<a href="' + ziel + '"><b>' + markiere(e.s, ws) + "</b><small>" + escHtml(texte.treffer_auf) + " " +
        escHtml(e.t) + '</small><span class="schnipsel">' + markiere(schnipsel(e.x, ws), ws) + "</span></a>";
    }).join("") : '<div class="leer">' + escHtml(texte.keine_treffer) + "</div>";
    liste.hidden = false;
  }
  function waehle(d) {
    var a = liste.querySelectorAll("a");
    if (!a.length) { return; }
    if (wahl >= 0) { a[wahl].classList.remove("wahl"); }
    wahl = (wahl + d + a.length) % a.length;
    a[wahl].classList.add("wahl");
    a[wahl].scrollIntoView({ block: "nearest" });
  }
  if (feld) {
    feld.addEventListener("input", zeige);
    feld.addEventListener("focus", zeige);
    feld.addEventListener("keydown", function (ev) {
      if (ev.key === "ArrowDown") { waehle(1); ev.preventDefault(); }
      else if (ev.key === "ArrowUp") { waehle(-1); ev.preventDefault(); }
      else if (ev.key === "Enter") {
        var a = liste.querySelectorAll("a");
        if (a.length) { location.href = a[Math.max(0, wahl)].getAttribute("href"); }
      } else if (ev.key === "Escape") { liste.hidden = true; feld.blur(); }
    });
    document.addEventListener("click", function (ev) {
      if (!liste.contains(ev.target) && ev.target !== feld) { liste.hidden = true; }
    });
    document.addEventListener("keydown", function (ev) {
      var imFeld = document.activeElement && /input|textarea/i.test(document.activeElement.tagName);
      if (!imFeld && (ev.key === "/" || (ev.ctrlKey && ev.key.toLowerCase() === "k"))) {
        feld.focus(); feld.select(); ev.preventDefault();
      }
    });
  }

  // Auf der Zielseite: den Begriff markieren und den ersten Treffer zeigen.
  var q = new URLSearchParams(location.search).get("q");
  if (q) {
    var ws = woerter(q);
    var art = document.querySelector("article");
    var erster = null;
    var gang = document.createTreeWalker(art, NodeFilter.SHOW_TEXT, null);
    var knoten = [];
    while (gang.nextNode()) { knoten.push(gang.currentNode); }
    knoten.forEach(function (k) {
      var t = k.nodeValue;
      var n = norm(t);
      var stellen = [];
      ws.forEach(function (w) {
        var p = n.indexOf(w);
        while (p >= 0) { stellen.push([p, p + w.length]); p = n.indexOf(w, p + w.length); }
      });
      if (!stellen.length || k.parentNode.closest("mark, script, style")) { return; }
      stellen.sort(function (a, b) { return a[0] - b[0]; });
      var frag = document.createDocumentFragment();
      var pos = 0;
      stellen.forEach(function (s) {
        if (s[0] < pos) { return; }
        frag.appendChild(document.createTextNode(t.slice(pos, s[0])));
        var m = document.createElement("mark");
        m.textContent = t.slice(s[0], s[1]);
        frag.appendChild(m);
        if (!erster) { erster = m; }
        pos = s[1];
      });
      frag.appendChild(document.createTextNode(t.slice(pos)));
      k.parentNode.replaceChild(frag, k);
    });
    var ziel = location.hash ? document.getElementById(location.hash.slice(1)) : null;
    var zeigen = ziel || erster;
    if (zeigen) {
      setTimeout(function () {
        zeigen.scrollIntoView({ block: "start" });
        if (ziel) { ziel.classList.add("blink"); }
      }, 30);
    }
    if (feld) { feld.value = q; }
  }

  // Beim Scrollen: den Abschnitt, in dem man liest, in der Seitenleiste
  // hervorheben (shank: "das Unterthema gehighlighted, damit man sieht, bei
  // welchem Abschnitt man ist").
  var abschnitte = Array.prototype.slice.call(document.querySelectorAll("article h2[id]"));
  var unterLinks = {};
  document.querySelectorAll(".gruppe ul.unter a").forEach(function (a) {
    unterLinks[a.getAttribute("href").slice(1)] = a;
  });
  function hier() {
    var aktiv = null;
    abschnitte.forEach(function (h) { if (h.getBoundingClientRect().top <= 140) { aktiv = h.id; } });
    if (!aktiv && abschnitte.length && window.scrollY > 40) { aktiv = abschnitte[0].id; }
    if (window.innerHeight + window.scrollY >= document.body.scrollHeight - 4 && abschnitte.length) {
      aktiv = abschnitte[abschnitte.length - 1].id;   // ganz unten: der letzte
    }
    Object.keys(unterLinks).forEach(function (id) { unterLinks[id].classList.toggle("hier", id === aktiv); });
  }
  window.addEventListener("scroll", hier, { passive: true });
  hier();

  // Hell / dunkel, gemerkt.
  var knopf = document.getElementById("thema");
  if (knopf) {
    knopf.addEventListener("click", function () {
      var neu = document.documentElement.dataset.theme === "light" ? "dark" : "light";
      document.documentElement.dataset.theme = neu;
      try { localStorage.setItem("bhed-thema", neu); } catch (e) { /* ohne Speicher */ }
    });
  }

  // Bewegte Bilder anhalten (man kann ein GIF sonst nicht stoppen).
  document.querySelectorAll("button.anhalten").forEach(function (k) {
    k.addEventListener("click", function (ev) {
      ev.preventDefault();
      ev.stopPropagation();
      var img = k.parentNode.querySelector("img");
      var steht = k.classList.toggle("steht");
      img.src = steht ? k.dataset.stand : k.dataset.lauf + "?" + Date.now();
      k.innerHTML = steht ? "&#9654;" : "&#10074;&#10074;";
    });
  });

  // Classic / Graph / Tracks, gemerkt (rc601).
  document.querySelectorAll(".moduswahl button").forEach(function (k) {
    k.addEventListener("click", function () {
      document.documentElement.dataset.modus = k.dataset.modus;
      try { localStorage.setItem("bhed-modus", k.dataset.modus); } catch (e) { /* ohne Speicher */ }
    });
  });

  // Bilder gross.
  var gross = document.getElementById("gross");
  document.querySelectorAll("a.zoom").forEach(function (a) {
    a.addEventListener("click", function (ev) {
      ev.preventDefault();
      gross.querySelector("img").src = a.getAttribute("href");
      gross.hidden = false;
    });
  });
  if (gross) { gross.addEventListener("click", function () { gross.hidden = true; }); }
})();

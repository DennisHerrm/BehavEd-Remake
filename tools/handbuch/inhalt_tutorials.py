"""Tutorials: die Uebersicht - das Yavin-Projekt und die kurzen Tutorials.

Bis rc599 standen hier vier Tutorials auf einer Movie-Duels-Mission. shank:
"es geht nicht nur drum, Movie-Duels-Missionen zu machen, das ist ein Tool
fuer die gesamte JKA-Community". Jetzt: eine eigene Mission auf yavin1
(inhalt_projekt.py) und viele kurze Tutorials (inhalt_kurz.py), alle Bilder
von derselben Karte.
"""
from inhalt_basis import T
import inhalt_kurz
import inhalt_projekt
import inhalt_kurz2
import inhalt_kurz3

UEBERSICHT = {
    "id": "tutorials",
    "gruppe": "tut",
    "titel": T("Tutorials", "Tutorials", "教程", "チュートリアル"),
    "kurz": T("Start with the Taspir mission - it builds a whole cutscene for Jedi Academy from nothing and touches almost every part of the program. The short tutorials below each show one thing in a few steps."),
    "bloecke": [
        ("h2", "im-programm", T("Interactive tutorials in the program", "Interaktive Tutorials im Programm", "程序内的交互式教程", "プログラム内のインタラクティブ チュートリアル")),
        ("p", T(
            "Some tutorials also run inside the program: **{{MenuHelp}} → {{TourMenu}}** (or **{{TourStartGuide}}** on the start page) opens a small window with the list. Pick one and press **{{TourStart}}**: the program darkens the screen except for the control you need, frames it in yellow and moves on by itself as soon as you have done the step - whether you used the button, the menu or the shortcut.",
            "Manche Tutorials laufen auch im Programm selbst: **{{MenuHelp}} → {{TourMenu}}** (oder **{{TourStartGuide}}** auf der Startfläche) öffnet ein kleines Fenster mit der Liste. Wähle eines und drücke **{{TourStart}}**: das Programm dunkelt alles ab außer dem Bedienelement, das du brauchst, rahmt es gelb ein und geht von selbst weiter, sobald du den Schritt erledigt hast - egal ob über den Knopf, das Menü oder die Taste.",
            "有些教程也可以直接在程序中运行:**{{MenuHelp}} → {{TourMenu}}**(或起始页面上的 **{{TourStartGuide}}**)会打开一个带列表的小窗口。选择一个并按 **{{TourStart}}**:程序会把屏幕变暗,只留下你需要操作的控件,用黄色框标出,并在你完成该步骤后自动继续——无论你用的是按钮、菜单还是快捷键。",
            "いくつかのチュートリアルはプログラムの中でも動きます。**{{MenuHelp}} → {{TourMenu}}**(またはスタート画面の **{{TourStartGuide}}**)で一覧付きの小さなウィンドウが開きます。1 つ選んで **{{TourStart}}** を押すと、必要な操作部分以外の画面が暗くなり、その部分が黄色の枠で囲まれ、手順を終えると自動で次へ進みます。ボタン、メニュー、ショートカットのどれを使っても構いません。")),
        ("img", "t_tour", T(
            "An interactive tutorial: the tutorial window and the highlighted control.",
            "Ein interaktives Tutorial: das Tutorial-Fenster und das hervorgehobene Bedienelement.",
            "交互式教程:教程窗口和高亮的控件。",
            "インタラクティブ チュートリアル:チュートリアル ウィンドウと強調表示された操作部分。"), "breit"),
        ("list", [
            T("**{{TourBack}}** goes one step back. **{{TourSkip}}** skips a step - where it can, the program prepares the next one (it never changes your script). **{{TourNext}}** moves on after a step that only shows something.",
              "**{{TourBack}}** geht einen Schritt zurück. **{{TourSkip}}** überspringt einen Schritt - wo es geht, bereitet das Programm den nächsten vor (dein Skript ändert es dabei nie). **{{TourNext}}** geht nach einem Schritt zum Ansehen weiter.",
              "**{{TourBack}}** 返回上一步。**{{TourSkip}}** 跳过一步——能做到时,程序会为下一步做好准备(绝不会改动你的脚本)。**{{TourNext}}** 用于在只展示内容的步骤后继续。",
              "**{{TourBack}}** で 1 つ前の手順に戻ります。**{{TourSkip}}** で手順を飛ばします。可能なら次の手順の準備をプログラムがします(スクリプトは決して変更しません)。**{{TourNext}}** は見るだけの手順の後に先へ進みます。"),
            T("**{{TourShowMe}}** moves a pointer to the right place; nothing is clicked. **{{TourDoIt}}** does the step for you - offered only where that is safe, never for saving (which asks for a file name).",
              "**{{TourShowMe}}** fährt einen Zeiger an die richtige Stelle; geklickt wird nichts. **{{TourDoIt}}** erledigt den Schritt für dich - nur wo das sicher ist, nie beim Speichern (das nach einem Dateinamen fragt).",
              "**{{TourShowMe}}** 把指针移到正确的位置,但不会点击。**{{TourDoIt}}** 替你完成该步骤——只在安全的地方提供,保存时绝不提供(保存需要输入文件名)。",
              "**{{TourShowMe}}** はポインターを正しい場所へ動かします。クリックはしません。**{{TourDoIt}}** は手順を代わりに実行します。安全な場合だけ表示され、保存(ファイル名を尋ねる)では表示されません。"),
            T("**{{TourGuide}}** opens the page with its pictures in your browser. The search box below the list searches this guide, offline.",
              "**{{TourGuide}}** öffnet die Seite mit ihren Bildern im Browser. Das Suchfeld unter der Liste durchsucht dieses Handbuch, ohne Netz.",
              "**{{TourGuide}}** 在浏览器中打开带图片的对应页面。列表下方的搜索框可离线搜索本手册。",
              "**{{TourGuide}}** で画像付きのページをブラウザーで開きます。一覧の下の検索欄では、このガイドをオフラインで検索できます。"),
            T("The program remembers how far you got; next time you continue where you stopped.",
              "Das Programm merkt sich, wie weit du gekommen bist; beim nächsten Mal machst du dort weiter.",
              "程序会记住你的进度;下次可以从上次停下的地方继续。",
              "どこまで進んだかをプログラムが覚えているので、次回は止めたところから続けられます。"),
            T("The first five are the script tutorials [[k-neues-skript]], [[k-befehl-einfuegen]], [[k-werte-bearbeiten]], [[k-zeilen-ordnen]] and [[k-uebersetzen]], in the {{FlowClassic}} view.",
              "Die ersten fünf sind die Skript-Tutorials [[k-neues-skript]], [[k-befehl-einfuegen]], [[k-werte-bearbeiten]], [[k-zeilen-ordnen]] und [[k-uebersetzen]], in der Ansicht {{FlowClassic}}.",
              "最先提供的五个是脚本教程 [[k-neues-skript]]、[[k-befehl-einfuegen]]、[[k-werte-bearbeiten]]、[[k-zeilen-ordnen]] 和 [[k-uebersetzen]],使用 {{FlowClassic}} 视图。",
              "最初の 5 つはスクリプトのチュートリアル [[k-neues-skript]]・[[k-befehl-einfuegen]]・[[k-werte-bearbeiten]]・[[k-zeilen-ordnen]]・[[k-uebersetzen]] で、{{FlowClassic}} ビューで動きます。"),
        ]),
        ("h2", "alle", T("All tutorials at a glance")),
        ("p", T("Pick any of them - each one stands on its own. Below the list: the big project, then every short tutorial as a card with a picture.")),
        ("index", [
            (T("The big project"), ["tut-projekt"] + ["tut-p%d" % i for i in range(1, 9)]),
            (T("Scripts"), [s["id"] for s in inhalt_kurz.SKRIPT + inhalt_kurz2.SKRIPT2 + inhalt_kurz3.VORHANDEN]),
            (T("Camera and timeline"), [s["id"] for s in inhalt_kurz.KAMERA + inhalt_kurz2.KAMERA2 + inhalt_kurz3.ZEIT + [inhalt_kurz3.SZENE[1]]]),
            (T("Figures"), [s["id"] for s in inhalt_kurz.FIGUREN + inhalt_kurz2.FIGUREN2]),
            (T("Map and entities"), [s["id"] for s in inhalt_kurz.KARTE + inhalt_kurz2.KARTE2 + [inhalt_kurz3.SZENE[0], inhalt_kurz3.SZENE[2], inhalt_kurz3.SZENE[3]]]),
            (T("Animation"), [s["id"] for s in inhalt_kurz.ANIMATION]),
            (T("Missions and models"), [s["id"] for s in inhalt_kurz.MISSION + inhalt_kurz.MODELL + inhalt_kurz2.MISSION2 + inhalt_kurz3.LOGIK + inhalt_kurz3.MODELLE + inhalt_kurz2.OBERFLAECHE]),
        ]),
        ("h2", "projekt", T("The big one: your own mission")),
        ("cards", ["tut-projekt"], "breit"),
        ("p", T("All tutorials use the base game - the map `taspir1` and its characters - so the pictures match what you see. Any other map, and any mod such as Movie Duels, works the same way. If the parts of the window are new to you, read [[fenster]] and [[karte#bewegen]] first.")),
        ("h2", "kurz", T("Short tutorials")),
        ("p", T("Each one shows one small thing in three to six steps - start with any of them.")),
        ("h3", T("Scripts")),
        ("cards", [s["id"] for s in inhalt_kurz.SKRIPT + inhalt_kurz2.SKRIPT2 + inhalt_kurz3.VORHANDEN]),
        ("h3", T("Camera and timeline")),
        ("cards", [s["id"] for s in inhalt_kurz.KAMERA + inhalt_kurz2.KAMERA2 + inhalt_kurz3.ZEIT + [inhalt_kurz3.SZENE[1]]]),
        ("h3", T("Figures")),
        ("cards", [s["id"] for s in inhalt_kurz.FIGUREN + inhalt_kurz2.FIGUREN2]),
        ("h3", T("Map and entities")),
        ("cards", [s["id"] for s in inhalt_kurz.KARTE + inhalt_kurz2.KARTE2 + [inhalt_kurz3.SZENE[0], inhalt_kurz3.SZENE[2], inhalt_kurz3.SZENE[3]]]),
        ("h3", T("Animation")),
        ("cards", [s["id"] for s in inhalt_kurz.ANIMATION]),
        ("h3", T("Missions and models")),
        ("cards", [s["id"] for s in inhalt_kurz.MISSION + inhalt_kurz.MODELL + inhalt_kurz2.MISSION2 + inhalt_kurz3.LOGIK + inhalt_kurz3.MODELLE + inhalt_kurz2.OBERFLAECHE]),
    ],
}

SEITEN = [UEBERSICHT] + inhalt_projekt.SEITEN

"""Nachschlagen: Einstellungen, Tastenkuerzel, Begriffe."""
from inhalt_basis import T

EINSTELLUNGEN = {
    "id": "einstellungen",
    "gruppe": "ref",
    "titel": T("Settings and updates", "Einstellungen und Updates", "设置与更新", "設定とアップデート"),
    "kurz": T(
        "Where the program keeps its paths and options, how to change keys, and how updates arrive.",
        "Wo das Programm seine Pfade und Optionen hält, wie man Tasten ändert und wie Updates kommen.",
        "程序在哪里保存路径和选项、如何修改快捷键,以及更新如何到来。",
        "プログラムのパスとオプションの場所、キーの変更方法、アップデートの受け取り方。"),
    "bloecke": [
        ("h2", "spielordner", T("Game folders", "Spielordner", "游戏文件夹", "ゲームフォルダー")),
        ("p", T(
            "**{{MenuEdit}} → {{PathsTitle}}** lists the folders whose `.pk3` archives the program reads, with **{{PrefsUp}}**, **{{PrefsDown}}**, **{{PrefsRemove}}** and **{{PrefsOpenFolder}}** for each. The bottom one wins over everything above it, as `fs_game` overrides `base` in the engine. A folder without archives says so - then the path is wrong.",
            "**{{MenuEdit}} → {{PathsTitle}}** nennt die Ordner, deren `.pk3`-Archive das Programm liest, je mit **{{PrefsUp}}**, **{{PrefsDown}}**, **{{PrefsRemove}}** und **{{PrefsOpenFolder}}**. Der unterste gewinnt gegen alles darüber, wie `fs_game` in der Engine `base` überschreibt. Ein Ordner ohne Archive sagt das - dann stimmt der Pfad nicht.",
            "**{{MenuEdit}} → {{PathsTitle}}** 列出程序读取其 `.pk3` 的文件夹,每个都有 **{{PrefsUp}}**、**{{PrefsDown}}**、**{{PrefsRemove}}** 和 **{{PrefsOpenFolder}}**。最下面的优先于上面所有文件夹,正如引擎中 `fs_game` 覆盖 `base`。没有压缩包的文件夹会给出提示——说明路径不对。",
            "**{{MenuEdit}} → {{PathsTitle}}** には、プログラムが `.pk3` を読むフォルダーが **{{PrefsUp}}**・**{{PrefsDown}}**・**{{PrefsRemove}}**・**{{PrefsOpenFolder}}** 付きで並びます。エンジンで `fs_game` が `base` を上書きするのと同じく、一番下が上のすべてより優先されます。アーカイブのないフォルダーはそう表示されるので、パスが間違っていると分かります。")),
        ("img", "h_spielordner", T(
            "The game folders.",
            "Die Spielordner.",
            "游戏文件夹。",
            "ゲームフォルダー。")),
        ("h2", "prefs", T("Preferences", "Einstellungen", "首选项", "設定")),
        ("p", T(
            "**{{MenuEdit}} → {{AppPrefs}}** (or **{{AppPrefs}}** in the action column, <kbd>Alt</kbd>+<kbd>P</kbd>) holds the options of the original BehavEd:",
            "**{{MenuEdit}} → {{AppPrefs}}** (oder **{{AppPrefs}}** in der Aktionsspalte, <kbd>Alt</kbd>+<kbd>P</kbd>) enthält die Optionen des Original-BehavEd:",
            "**{{MenuEdit}} → {{AppPrefs}}**(或操作栏中的 **{{AppPrefs}}**,<kbd>Alt</kbd>+<kbd>P</kbd>)包含原版 BehavEd 的选项:",
            "**{{MenuEdit}} → {{AppPrefs}}**(または操作列の **{{AppPrefs}}**、<kbd>Alt</kbd>+<kbd>P</kbd>)にはオリジナル BehavEd のオプションがあります。")),
        ("list", [
            T("**{{PrefsDirectories}}**: {{PrefsScriptPath}}, {{PrefsCommandFile}} (the `.bhc` that defines every command - **{{PrefsReload}}** reads it again), {{PrefsSourcePath}}.",
              "**{{PrefsDirectories}}**: {{PrefsScriptPath}}, {{PrefsCommandFile}} (die `.bhc`, die jeden Befehl festlegt - **{{PrefsReload}}** liest sie neu), {{PrefsSourcePath}}.",
              "**{{PrefsDirectories}}**:{{PrefsScriptPath}}、{{PrefsCommandFile}}(定义所有命令的 `.bhc`——**{{PrefsReload}}** 重新读取)、{{PrefsSourcePath}}。",
              "**{{PrefsDirectories}}**:{{PrefsScriptPath}}、{{PrefsCommandFile}}(すべてのコマンドを定義する `.bhc`。**{{PrefsReload}}** で再読み込み)、{{PrefsSourcePath}}。"),
            T("**{{PrefsReopen}}**, **{{PrefsAlphaSort}}**, **{{PrefsAltIcons}}**, **{{PrefsQuery}}**, **{{PrefsLastDir}}**.",
              "**{{PrefsReopen}}**, **{{PrefsAlphaSort}}**, **{{PrefsAltIcons}}**, **{{PrefsQuery}}**, **{{PrefsLastDir}}**.",
              "**{{PrefsReopen}}**、**{{PrefsAlphaSort}}**、**{{PrefsAltIcons}}**、**{{PrefsQuery}}**、**{{PrefsLastDir}}**。",
              "**{{PrefsReopen}}**・**{{PrefsAlphaSort}}**・**{{PrefsAltIcons}}**・**{{PrefsQuery}}**・**{{PrefsLastDir}}**。"),
            T("Language, theme and interface size are in the **{{MenuView}}** menu.",
              "Sprache, Farbgebung und Oberflächengröße stehen im Menü **{{MenuView}}**.",
              "语言、主题和界面大小位于 **{{MenuView}}** 菜单。",
              "言語・テーマ・インターフェースの大きさは **{{MenuView}}** メニューにあります。"),
        ]),
        ("h2", "tasten", T("Changing keys", "Tasten ändern", "修改快捷键", "キーの変更")),
        ("p", T(
            "**{{MenuEdit}} → {{KeysTitle}}** lists every action that has a key. Click a shortcut and press the new key; <kbd>Esc</kbd> cancels, <kbd>Backspace</kbd> clears. A key that is already taken is named. The defaults are in [[tasten]].",
            "**{{MenuEdit}} → {{KeysTitle}}** nennt jede Aktion mit Taste. Ein Kürzel anklicken und die neue Taste drücken; <kbd>Esc</kbd> bricht ab, <kbd>Rücktaste</kbd> löscht. Eine schon belegte Taste wird genannt. Die Vorgaben stehen in [[tasten]].",
            "**{{MenuEdit}} → {{KeysTitle}}** 列出所有带快捷键的操作。单击某个快捷键,再按新键;<kbd>Esc</kbd> 取消,<kbd>Backspace</kbd> 清除。已被占用的键会提示。默认值见 [[tasten]]。",
            "**{{MenuEdit}} → {{KeysTitle}}** にキーを持つすべての操作が並びます。ショートカットをクリックして新しいキーを押します。<kbd>Esc</kbd> で取り消し、<kbd>Backspace</kbd> で削除。使用済みのキーは表示されます。既定値は [[tasten]] を参照。")),
        ("h2", "updates", T("Updates and changelog", "Updates und Änderungsliste", "更新与更新日志", "アップデートと変更履歴")),
        ("p", T(
            "**{{MenuHelp}} → {{UpdMenu}}** asks GitHub for a newer version and shows what is new; **{{UpdInstall}}** replaces the program files and offers a restart. The switch next to it decides whether this happens at startup. **{{MenuHelp}} → {{ChangelogMenu}}** shows what was fixed and added, version by version, in your language - it also opens once by itself after an update.",
            "**{{MenuHelp}} → {{UpdMenu}}** fragt GitHub nach einer neueren Fassung und zeigt, was neu ist; **{{UpdInstall}}** ersetzt die Programmdateien und bietet einen Neustart an. Der Schalter daneben entscheidet, ob das beim Start geschieht. **{{MenuHelp}} → {{ChangelogMenu}}** zeigt Fassung für Fassung, was behoben und hinzugekommen ist, in deiner Sprache - nach einem Update öffnet sie sich einmal von selbst.",
            "**{{MenuHelp}} → {{UpdMenu}}** 会向 GitHub 查询新版本并显示更新内容;**{{UpdInstall}}** 会替换程序文件并提示重启。旁边的开关决定是否在启动时检查。**{{MenuHelp}} → {{ChangelogMenu}}** 按版本以你的语言显示修复和新增的内容——更新后会自动打开一次。",
            "**{{MenuHelp}} → {{UpdMenu}}** で GitHub に新しいバージョンを問い合わせ、新機能を表示します。**{{UpdInstall}}** でプログラムファイルを置き換え、再起動を案内します。隣の切り替えで起動時に確認するかを決めます。**{{MenuHelp}} → {{ChangelogMenu}}** はバージョンごとの修正と追加をあなたの言語で表示し、アップデート後に一度自動で開きます。")),
    ],
}

TASTEN = {
    "id": "tasten",
    "gruppe": "ref",
    "titel": T("Keyboard shortcuts", "Tastenkürzel", "键盘快捷键", "キーボードショートカット"),
    "kurz": T(
        "The default keys. Most of them can be changed under Edit → Keyboard shortcuts.",
        "Die vorgegebenen Tasten. Die meisten lassen sich unter Bearbeiten → Tastenkürzel ändern.",
        "默认快捷键。大多数可在 编辑 → 键盘快捷键 中修改。",
        "既定のキーです。ほとんどは 編集 → キーボードショートカット で変更できます。"),
    "bloecke": [
        ("h2", "skript", T("Script", "Skript", "脚本", "スクリプト")),
        ("keys", [
            ("Enter", T("Edit the selected command", "Gewählten Befehl bearbeiten", "编辑所选命令", "選択中のコマンドを編集")),
            ("Insert", T("Insert the command selected in the Events list", "Den in der Events-Liste gewählten Befehl einfügen", "插入在事件列表中选中的命令", "イベント一覧で選んだコマンドを挿入")),
            ("Delete", T("Delete", "Löschen", "删除", "削除")),
            ("Space", T("Duplicate", "Duplizieren", "复制一份", "複製")),
            ("Ctrl+C", T("Copy", "Kopieren", "复制", "コピー")),
            ("Ctrl+X", T("Cut (also Ctrl+T)", "Ausschneiden (auch Strg+T)", "剪切(也可 Ctrl+T)", "切り取り(Ctrl+T でも可)")),
            ("Ctrl+V", T("Paste", "Einfügen", "粘贴", "貼り付け")),
            ("Backspace", T("REM: comment out / back (Ctrl+Backspace: uncomment)", "REM: auskommentieren / zurück (Strg+Rücktaste: Kommentar entfernen)", "REM:注释/取消(Ctrl+Backspace:取消注释)", "REM:コメント化/解除(Ctrl+Backspace:コメント解除)")),
            ("Alt+Up", T("Move up (Alt+Down: down), staying in the block", "Nach oben (Alt+Unten: nach unten), im selben Block", "上移(Alt+Down:下移),保持在同一块中", "上へ移動(Alt+Down で下へ)。同じブロック内")),
            ("Ctrl+Z", T("Undo (Ctrl+Y or Ctrl+Shift+Z: redo)", "Rückgängig (Strg+Y oder Strg+Umschalt+Z: wiederholen)", "撤销(Ctrl+Y 或 Ctrl+Shift+Z:重做)", "元に戻す(Ctrl+Y または Ctrl+Shift+Z でやり直し)")),
            ("Ctrl+F", T("Find; F3 / Shift+F3 next / previous", "Suchen; F3 / Umschalt+F3 nächster / voriger", "查找;F3 / Shift+F3 下一个/上一个", "検索。F3 / Shift+F3 で次/前")),
            ("Ctrl+F2", T("Toggle bookmark; F2 / Shift+F2 next / previous", "Lesezeichen setzen; F2 / Umschalt+F2 nächstes / voriges", "切换书签;F2 / Shift+F2 下一个/上一个", "ブックマーク切替。F2 / Shift+F2 で次/前")),
            ("Num+", T("Expand item and children (Ctrl+Num+: all)", "Eintrag und Kinder aufklappen (Strg+Num+: alles)", "展开项及其子项(Ctrl+Num+:全部)", "項目と子を展開(Ctrl+Num+:すべて)")),
            ("Num-", T("Collapse item (Ctrl+Num-: all)", "Eintrag zuklappen (Strg+Num-: alles)", "折叠项(Ctrl+Num-:全部)", "項目を折りたたむ(Ctrl+Num-:すべて)")),
        ]),
        ("h2", "datei", T("Files", "Dateien", "文件", "ファイル")),
        ("keys", [
            ("F1", T("Open this guide", "Dieses Handbuch öffnen", "打开本手册", "このガイドを開く")),
            ("Ctrl+O", T("Open", "Öffnen", "打开", "開く")),
            ("Ctrl+S", T("Save", "Speichern", "保存", "保存")),
            ("Ctrl+A", T("Save as (as in the original BehavEd)", "Speichern unter (wie im Original-BehavEd)", "另存为(与原版 BehavEd 相同)", "名前を付けて保存(オリジナル BehavEd と同じ)")),
            ("Ctrl+Shift+S", T("Save all", "Alle speichern", "全部保存", "すべて保存")),
            ("Alt+B", T("Backup; Alt+R restore", "Sicherung; Alt+R wiederherstellen", "备份;Alt+R 恢复", "バックアップ。Alt+R で復元")),
            ("Alt+M", T("Recently opened files", "Zuletzt geöffnete Dateien", "最近打开的文件", "最近開いたファイル")),
            ("Alt+P", T("Prefs; Alt+X exit", "Einstellungen; Alt+X beenden", "首选项;Alt+X 退出", "設定。Alt+X で終了")),
        ]),
        ("h2", "karte", T("Map and camera", "Karte und Kamera", "地图与摄像机", "マップとカメラ")),
        ("keys", [
            (T("W A S D", "W A S D", "W A S D", "W A S D"), T("Fly (mouse over the view); Q / E down / up, Shift faster; right mouse looks around", "Fliegen (Maus über der Ansicht); Q / E runter / hoch, Umschalt schneller; rechte Maus schaut umher", "飞行(鼠标位于视图上);Q / E 下降/上升,Shift 加速;鼠标右键环顾四周", "飛行(マウスがビュー上)。Q / E で下降/上昇、Shift で高速、右ボタンで見回す")),
            ("Ctrl+1", T("Save view 1 (… 9); 1 … 9 jumps back", "Ansicht 1 merken (… 9); 1 … 9 springt zurück", "保存视角 1(…9);1…9 跳回", "視点 1 を保存(…9)。1〜9 で戻る")),
            ("Ctrl", T("While dragging an entity: rings in 15-degree steps; with a grid set it turns the grid off, without one an arrow snaps to 8 units", "Beim Ziehen einer Entity: Ringe in 15-Grad-Schritten; mit eingestelltem Raster schaltet es das Raster aus, ohne rastet ein Pfeil auf 8 Einheiten", "拖动实体时:圆环以 15 度为一步;设置了网格时关闭网格,未设置时箭头吸附到 8 单位", "エンティティのドラッグ中:リングは 15 度刻み。グリッド設定時はグリッドを解除、未設定なら矢印が 8 ユニットにスナップ")),
            ("Alt", T("Alt+drag an entity: copy it first", "Alt+Ziehen einer Entity: erst kopieren", "Alt+拖动实体:先复制", "エンティティを Alt+ドラッグ:先にコピー")),
            ("Shift", T("While dragging a flight target or a key diamond: only up and down", "Beim Ziehen eines Flugziels oder einer Schlüsselraute: nur hoch und runter", "拖动飞行目标或关键帧菱形时:仅上下", "飛行目標やキーのひし形のドラッグ中:上下のみ")),
            ("Esc", T("Cancel placing, the waypoint tool or piloting", "Platzieren, Wegpunktwerkzeug oder Steuern abbrechen", "取消放置、路径点工具或驾驶", "配置・ウェイポイントツール・操縦を取り消す")),
            ("Enter", T("While piloting: set a camera key at the playhead", "Beim Steuern: Kameraschlüssel am Zeiger setzen", "驾驶时:在播放头处设置摄像机关键帧", "操縦中:再生位置にカメラキーを設定")),
        ]),
        ("h2", "zeit", T("Timeline", "Zeitleiste", "时间轴", "タイムライン")),
        ("keys", [
            (T("Left / Right", "Links / Rechts", "左 / 右", "← / →"), T("50 ms back / forward, mouse over the timeline (Shift: 1 s)", "50 ms zurück / vor, Maus über der Zeitleiste (Umschalt: 1 s)", "后退/前进 50 毫秒,鼠标位于时间轴上(Shift:1 秒)", "50 ms 戻る/進む。マウスがタイムライン上(Shift:1 秒)")),
            (T("Home / End", "Pos1 / Ende", "Home / End", "Home / End"), T("Jump to start / end (mouse over the timeline)", "Zum Anfang / Ende (Maus über der Zeitleiste)", "跳到开头/结尾(鼠标位于时间轴上)", "先頭/末尾へ(マウスがタイムライン上)")),
            ("Alt", T("While dragging a block: move only this block", "Beim Ziehen eines Blocks: nur diesen Block verschieben", "拖动块时:只移动此块", "ブロックのドラッグ中:このブロックだけ動かす")),
        ]),
        ("h2", "fenster", T("Window", "Fenster", "窗口", "ウィンドウ")),
        ("keys", [
            (T("Ctrl + wheel", "Strg + Mausrad", "Ctrl + 滚轮", "Ctrl + ホイール"), T("Interface size (over the timeline tracks: zoom the timeline)", "Oberflächengröße (über den Spuren der Zeitleiste: die Zeitleiste zoomen)", "界面大小(在时间轴轨道上:缩放时间轴)", "インターフェースの大きさ(タイムラインのトラック上ではタイムラインのズーム)")),
        ]),
    ],
}

BEGRIFFE = {
    "id": "begriffe",
    "gruppe": "ref",
    "titel": T("Glossary", "Begriffe", "术语表", "用語集"),
    "kurz": T(
        "The words of Jedi Academy scripting in one place.",
        "Die Wörter der Jedi-Academy-Skripte an einem Ort.",
        "《绝地学院》脚本相关术语一览。",
        "『ジェダイ・アカデミー』のスクリプト用語をまとめました。"),
    "bloecke": [
        ("table", [T("Term", "Begriff", "术语", "用語"), T("Meaning", "Bedeutung", "含义", "意味")], [
            ["ICARUS", T("The scripting system of the game. Scripts are text (`.txt`) and run compiled (`.ibi`).", "Das Skriptsystem des Spiels. Skripte sind Text (`.txt`) und laufen übersetzt (`.ibi`).", "游戏的脚本系统。脚本是文本(`.txt`),以编译后的形式(`.ibi`)运行。", "ゲームのスクリプトシステム。スクリプトはテキスト(`.txt`)で、コンパイルして(`.ibi`)実行されます。")],
            ["`.ibi`", T("A compiled script - what the game actually runs. **{{Compile}}** makes it.", "Ein übersetztes Skript - das, was das Spiel wirklich ausführt. **{{Compile}}** erzeugt es.", "编译后的脚本——游戏真正运行的文件。由 **{{Compile}}** 生成。", "コンパイル済みのスクリプト。ゲームが実際に実行するもので、**{{Compile}}** で作ります。")],
            ["`.bhc`", T("The command description file: every command, its fields and their choices.", "Die Befehlsbeschreibung: jeder Befehl, seine Felder und deren Auswahl.", "命令描述文件:所有命令、其字段及可选值。", "コマンド定義ファイル。すべてのコマンドとその欄、選択肢。")],
            ["`affect`", T("A block whose commands go to one named figure or entity.", "Ein Block, dessen Befehle an eine benannte Figur oder Entity gehen.", "其中命令发送给某个指定角色或实体的块。", "中のコマンドが名前付きの 1 つのキャラクターやエンティティに送られるブロック。")],
            ["`task` / `do` / `dowait`", T("A named group of commands; `do` starts it alongside, `dowait` waits until it is done.", "Eine benannte Gruppe von Befehlen; `do` startet sie nebenher, `dowait` wartet, bis sie fertig ist.", "命名的命令组;`do` 并行启动,`dowait` 等待其完成。", "名前付きのコマンドのまとまり。`do` で並行して開始し、`dowait` で完了を待ちます。")],
            ["`wait`", T("A pause in milliseconds. In the game it ends one frame (50 ms) later than written.", "Eine Pause in Millisekunden. Im Spiel endet sie ein Bild (50 ms) später als geschrieben.", "以毫秒计的暂停。游戏中会比写入值晚一帧(50 毫秒)结束。", "ミリ秒単位の一時停止。ゲームでは書かれた値より 1 フレーム(50 ms)遅れて終わります。")],
            ["`SET_NAVGOAL`", T("Sends a figure walking to a `waypoint_navgoal`.", "Schickt eine Figur zu einem `waypoint_navgoal`.", "让角色走向某个 `waypoint_navgoal`。", "キャラクターを `waypoint_navgoal` へ歩かせます。")],
            ["`NPC_spawner`", T("The entity that makes a character appear; `NPC_type` says which one, `NPC_targetname` names it for scripts.", "Die Entity, die eine Figur erscheinen lässt; `NPC_type` sagt welche, `NPC_targetname` benennt sie für Skripte.", "使角色出现的实体;`NPC_type` 指定角色种类,`NPC_targetname` 为脚本提供名字。", "キャラクターを出現させるエンティティ。`NPC_type` で種類、`NPC_targetname` でスクリプト用の名前を指定します。")],
            ["`ref_tag`", T("A named mark in the map - for camera positions and look targets.", "Eine benannte Marke in der Karte - für Kamerapositionen und Blickziele.", "地图中的命名标记——用于摄像机位置和注视目标。", "マップ上の名前付きマーク。カメラ位置や視線目標に使います。")],
            ["`target_scriptrunner`", T("Runs a script when it is used (by a trigger, another entity or a script).", "Startet ein Skript, wenn es benutzt wird (von einem Auslöser, einer anderen Entity oder einem Skript).", "被使用时(由触发器、其他实体或脚本)运行脚本。", "使われたとき(トリガー、別のエンティティ、スクリプトから)スクリプトを実行します。")],
            ["`func_static`", T("A brush model of the map that scripts can move - ships, doors, platforms.", "Ein Brush-Modell der Karte, das Skripte bewegen können - Schiffe, Türen, Plattformen.", "脚本可以移动的地图笔刷模型——飞船、门、平台。", "スクリプトで動かせるマップのブラシモデル。船・扉・プラットフォーム。")],
            ["ROFF (`.rof`)", T("A recorded movement: one small step of position and angle per game frame. See [[animation#roff]].", "Eine aufgezeichnete Bewegung: je Spielbild ein kleiner Schritt in Ort und Winkel. Siehe [[animation#roff]].", "记录下来的运动:每个游戏帧一小步位置和角度变化。参见 [[animation#roff]]。", "記録された動き。ゲームの 1 フレームごとの位置と角度の小さな 1 歩です。[[animation#roff]] を参照。")],
            ["`.pk3`", T("A zip archive the game reads its files from. A mission is packed into one.", "Ein Zip-Archiv, aus dem das Spiel seine Dateien liest. Eine Mission wird in eines gepackt.", "游戏从中读取文件的 zip 压缩包。任务会打包成一个 .pk3。", "ゲームがファイルを読み込む zip アーカイブ。ミッションは 1 つにまとめられます。")],
            ["`.ent`", T("The entities of a map in a separate file; Movie Duels keeps the map and puts its own entities next to it.", "Die Entities einer Karte in einer eigenen Datei; Movie Duels lässt die Karte und legt eigene Entities daneben.", "单独文件中的地图实体;Movie Duels 保留原地图,并在旁边放置自己的实体。", "マップのエンティティを別ファイルにしたもの。Movie Duels はマップをそのままにして、独自のエンティティを隣に置きます。")],
            ["Movie Duels", T("A large Jedi Academy mod with cutscene missions - the main use of this program.", "Ein großer Jedi-Academy-Mod mit Zwischensequenz-Missionen - der Hauptzweck dieses Programms.", "一个大型《绝地学院》模组,包含带过场动画的任务——本程序的主要用途。", "カットシーン付きミッションを含む大型の『ジェダイ・アカデミー』MOD。このプログラムの主な用途です。")],
        ]),
    ],
}

SEITEN = [EINSTELLUNGEN, TASTEN, BEGRIFFE]

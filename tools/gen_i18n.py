# -*- coding: utf-8 -*-
"""
Erzeugt include/bhed/i18n.h und src/i18n.cpp aus der Tabelle unten.

Der Aufbau ist von efxed uebernommen: ein Aufzaehlungswert je Text, eine
Tabelle mit vier Spalten, und ein static_assert, das beide vergleicht.
Geraten sie aus dem Tritt, faellt es beim Uebersetzen auf und nicht als
leerer Knopf im laufenden Programm.

Hauptsprache ist Englisch - das ist die Sprache der JKA-Modding-Gemeinde,
der Dateiformate und der Engine-Meldungen. Die Knopfbeschriftungen des
Originals bleiben im Englischen WORTGLEICH ("Ok", "Sa&ve As", "Pythonise !"),
damit der Nachbau erkennbar bleibt.
"""

# (Kennung, English, Deutsch, 中文, 日本語)
S = [
    ("AppTitle", "BehavEd", "BehavEd", "BehavEd", "BehavEd"),

    # --- Gruppen des Hauptfensters (Beschriftungen aus der Ressource) ---
    ("GroupEvents", "Events", "Ereignisse", "事件", "イベント"),
    ("GroupScriptFlow", "Script Flow", "Skriptablauf", "脚本流程", "スクリプトの流れ"),
    ("GroupActions", "Actions", "Aktionen", "操作", "操作"),
    ("GroupFile", "File", "Datei", "文件", "ファイル"),
    ("GroupApplication", "Application", "Programm", "程序", "アプリケーション"),
    ("GroupTreeview", "Treeview Options", "Baumansicht", "树视图选项", "ツリー表示の設定"),
    # GroupStatus ist weg: die Statusliste hat keine Ueberschrift mehr,
    # weil sie kein eigenes Feld mehr ist (siehe drawStatus in gui/app.cpp).

    # --- Knopfgruppe Actions ---
    ("ActDelete", "Delete", "Löschen", "删除", "削除"),
    ("ActClone", "Duplicate", "Klonen", "克隆", "複製"),
    ("ActCopy", "Copy", "Kopieren", "复制", "コピー"),
    ("ActCut", "Cut", "Ausschneiden", "剪切", "切り取り"),
    ("ActPaste", "Paste", "Einfügen", "粘贴", "貼り付け"),
    ("ActRem", "REM", "Auskommentieren", "注释掉", "コメント化"),
    ("ActFind", "Find", "Suchen", "查找", "検索"),

    # --- Knopfgruppe File ---
    ("FileNew", "New", "Neu", "新建", "新規"),
    ("FileOpen", "Open", "Öffnen", "打开", "開く"),
    ("FileMru", "MRU", "Zuletzt", "最近", "最近使用"),
    ("FileAppend", "Append", "Anhängen", "追加", "追加読み込み"),
    ("FileSave", "Save", "Speichern", "保存", "保存"),
    ("FileSaveAs", "Save as", "Speichern unter", "另存为", "名前を付けて保存"),
    ("FileExport", "Export", "Exportieren", "导出", "エクスポート"),
    ("FileBackup", "Backup", "Sicherung", "备份", "バックアップ"),
    ("FileRestore", "Restore", "Wiederherstellen", "还原", "復元"),

    # --- Programm ---
    ("AppPrefs", "Prefs", "Einstellungen", "首选项", "設定"),
    ("AppAbout", "About", "Über", "关于", "情報"),
    ("AppExit", "Exit", "Beenden", "退出", "終了"),

    # --- Baumansicht ---
    ("TreeShowTypes", "Show Types", "Typen zeigen", "显示类型", "型を表示"),
    ("TreeGFloats", "%g floats", "%g Kommazahlen", "%g 浮点数", "%g 浮動小数"),
    ("TreeExpand", "+", "+", "+", "+"),
    ("TreeCollapse", "-", "-", "-", "-"),
    ("TreeFoldMacros", "Fold Macros", "Makros falten", "折叠宏", "マクロを畳む"),

    # --- Übersetzen ---
    ("Compile", "Compile!", "Übersetzen!", "编译！", "コンパイル！"),
    ("CompileDone", "Compiled", "Übersetzt", "已编译", "コンパイル済み"),
    ("CompileFailedShort", "Failed", "Fehler", "失败", "失敗"),
    ("CompileOk", "Compiled: %d blocks, %d bytes",
     "Übersetzt: %d Blöcke, %d Byte", "已编译：%d 个块，%d 字节",
     "コンパイル完了: %d ブロック, %d バイト"),
    ("CompileFailed", "Compile failed", "Übersetzen fehlgeschlagen", "编译失败",
     "コンパイルに失敗"),

    # --- Event-Editor ---
    ("EditorTitle", "Event editor", "Ereignis-Editor", "事件编辑器", "イベントエディタ"),
    ("EditorOk", "Ok", "Ok", "确定", "OK"),
    ("EditorCancel", "Cancel", "Abbrechen", "取消", "キャンセル"),
    ("EditorReEvaluate", "Re-Evaluate", "Neu auswerten", "重新求值", "再評価"),
    ("EditorExpr", "Expr!", "Ausdruck!", "表达式！", "式！"),
    ("EditorHelper", "Helper", "Helfer", "助手", "ヘルパー"),
    ("EditorRevert", "Revert", "Zurücknehmen", "还原", "戻す"),
    ("MenuBookmarks", "Bookmarks", "Lesezeichen", "书签", "ブックマーク"),
    ("ActBookmarkToggle", "Toggle bookmark", "Lesezeichen setzen/entfernen", "切换书签", "ブックマークの切り替え"),
    ("ActBookmarkNext", "Next bookmark", "Nächstes Lesezeichen", "下一个书签", "次のブックマーク"),
    ("ActBookmarkPrev", "Previous bookmark", "Voriges Lesezeichen", "上一个书签", "前のブックマーク"),
    ("ActBookmarkClear", "Clear all bookmarks", "Alle Lesezeichen entfernen", "清除所有书签", "すべてのブックマークを解除"),
    ("MsgNoBookmarks", "No bookmarks set", "Keine Lesezeichen gesetzt", "没有设置书签", "ブックマークがありません"),
    ("ViewChangeHistory", "Change history (margin)", "Änderungsrand", "更改历史（边栏）", "変更履歴（余白）"),
    ("ViewHighlightSame", "Highlight identical commands", "Gleiche Befehle hervorheben",
     "高亮相同的命令", "同じコマンドを強調"),
    ("ViewHighlightSameHint",
     "Faintly marks every command that looks exactly like the selected one - same name, same values. Handy for cameras that are used more than once.",
     "Markiert schwach jeden Befehl, der genau so aussieht wie der ausgewählte - gleicher Name, gleiche Werte. Praktisch bei Kameras, die mehrfach vorkommen.",
     "淡色标记与所选命令完全相同的所有命令。", "選択したコマンドと全く同じコマンドを薄く表示します。"),
    ("ActFindSimilar", "Find similar", "Ähnliche suchen", "查找相似", "類似を検索"),
    ("FindSimilarAny", "Every \"%s\"", "Jeder \"%s\"", "所有 \"%s\"", "すべての \"%s\""),
    ("HintGutter", "Click: toggle bookmark (Ctrl+F2). Orange: changed, not saved - green: changed and saved - blue: back to original",
     "Klick: Lesezeichen setzen/entfernen (Strg+F2). Orange: geändert, nicht gespeichert - grün: geändert und gespeichert - blau: wieder wie beim Öffnen",
     "单击：切换书签 (Ctrl+F2)。橙色：已更改未保存 - 绿色：已更改并保存 - 蓝色：恢复为原始",
     "クリック：ブックマークの切り替え (Ctrl+F2)。オレンジ：変更・未保存 - 緑：変更・保存済み - 青：元に戻った"),
    ("EditorBrowse", "...", "...", "...", "..."),
    ("EditorPlay", "Do", "Abspielen", "播放", "再生"),
    ("EditorGet", "Get", "Get", "Get", "Get"),
    ("EditorTag", "Tag", "Tag", "Tag", "Tag"),
    ("EditorRnd", "Rnd", "Rnd", "Rnd", "Rnd"),
    ("EditorRange", ".... range ....", ".... Bereich ....", "…… 范围 ……",
     "…… 範囲 ……"),
    ("EditorNoHelp", "no help comment available", "keine Hilfe vorhanden",
     "无可用帮助", "ヘルプはありません"),
    ("EditorWas", "..... was", "..... war", "……原为", "……元は"),

    # --- Menü (im Nachbau, das Original hat keins) ---
    ("MenuFile", "File", "Datei", "文件", "ファイル"),
    ("MenuEdit", "Edit", "Bearbeiten", "编辑", "編集"),
    ("MenuView", "View", "Ansicht", "视图", "表示"),
    ("MenuHelp", "Help", "Hilfe", "帮助", "ヘルプ"),
    ("HelpAbout", "About BehavEd...", "Ueber BehavEd...", "关于 BehavEd...", "BehavEd について..."),
    ("EditUndo", "Undo", "Rückgängig", "撤销", "元に戻す"),
    ("EditRedo", "Redo", "Wiederholen", "やり直し", "やり直し"),
    ("ViewTheme", "Theme", "Farbgebung", "主题", "テーマ"),
    ("ViewDumpActors", "Log figures at playhead",
     "Figuren an der Zeitmarke protokollieren",
     "记录时间标记处的角色", "再生位置のキャラクターを記録"),
    ("MapCamAtPlayhead", "at the playhead", "an der Zeitmarke",
     "在时间标记处", "再生位置"),
    ("ViewFrameCost", "Log time per frame", "Zeit je Bild protokollieren",
     "记录每帧耗时", "1フレームあたりの時間を記録"),
    ("ViewLookRotation", "Head and torso follow the look target",
     "Kopf und Oberkoerper folgen dem Blickziel",
     "头部和上身跟随注视目标", "頭と上体が注視対象を追う"),
    ("ViewLookRotationHint",
     "Off shows the animation alone - the pose without any added turning. "
     "If the figure looks right with this off, the added turning is at fault.",
     "Aus zeigt allein die Animation - die Pose ohne jede zusaetzliche "
     "Drehung. Sieht die Figur so richtig aus, liegt es an der Drehung.",
     "关闭后仅显示动画本身，即未附加任何转动的姿势。若此时角色显示正确，则问题出在附加转动上。",
     "オフにするとアニメーションのみ、つまり追加の回転がない姿勢を表示します。"
     "これで正しく見えるなら、原因は追加の回転です。"),
    ("ViewLanguage", "Language", "Sprache", "语言", "言語"),
    ("ViewDpiScale", "Interface Scale", "Bedienoberfläche skalieren",
     "界面缩放", "UI の拡大率"),

    # --- Farbgebungen ---
    ("ThemeBehavEdClassic", "BehavEd Classic", "BehavEd klassisch",
     "BehavEd 经典", "BehavEd クラシック"),
    ("ThemeWindows", "Windows", "Windows", "Windows", "Windows"),
    ("ThemeDark", "Dark", "Dunkel", "深色", "ダーク"),
    ("ThemeMidnight", "Midnight", "Mitternacht", "午夜", "ミッドナイト"),
    ("ThemeLight", "Light", "Hell", "浅色", "ライト"),
    ("ThemeHighContrast", "High Contrast", "Hoher Kontrast", "高对比度",
     "ハイコントラスト"),

    # --- Meldungen ---
    ("MsgNoScript", "No script loaded", "Kein Skript geladen", "未加载脚本",
     "スクリプトが読み込まれていません"),
    ("MsgModelLoaded", "Command model: %d signatures, %d typesets, %d macros",
     "Befehlsmodell: %d Signaturen, %d Typmengen, %d Makros",
     "命令模型：%d 个签名，%d 个类型集，%d 个宏",
     "コマンドモデル: %d シグネチャ, %d 型集合, %d マクロ"),
    ("MsgIssues", "%d errors, %d notes", "%d Fehler, %d Anmerkungen",
     "%d 个错误，%d 条提示", "%d 件のエラー, %d 件の注記"),
    ("MsgUnknownValue", "unknown", "unbekannt", "未知", "不明"),
    ("MsgUnknownValueHint",
     "Not in the type set. Allowed - mods bring their own values - but far "
     "more often a typo.",
     "Steht nicht in der Typmenge. Erlaubt - Mods bringen eigene Werte mit - "
     "aber weit öfter ein Tippfehler.",
     "不在类型集中。允许（模组会自带值），但更多时候是拼写错误。",
     "型セットにありません。許容されます（MOD は独自の値を持ちます）が、"
     "多くはタイプミスです。"),
    ("MsgUnsavedChanges", "Current script has unsaved changes.\nSave this file first?",
     "Das aktuelle Skript hat ungespeicherte Änderungen.\nZuerst speichern?",
     "当前脚本有未保存的更改。\n先保存此文件吗？",
     "現在のスクリプトに未保存の変更があります。\n先に保存しますか？"),
    ("MsgLoaded", "Loaded, %d lines", "Geladen, %d Zeilen", "已加载，%d 行",
     "読み込み完了、%d 行"),
    ("MsgSaved", "Saved", "Gespeichert", "已保存", "保存しました"),
    ("MsgRecent", "Recent files", "Zuletzt geöffnet", "最近的文件",
     "最近使ったファイル"),
    ("MsgNoRecent", "(none)", "(keine)", "（无）", "（なし）"),

    ("FindNamed", "Item called", "Element mit Namen", "名称为", "名前が"),
    ("FindContains", "Item containing", "Element enthält", "包含", "次を含む"),
    ("FindWhole", "Whole-string match only", "Nur ganze Zeichenkette",
     "仅完全匹配", "完全一致のみ"),

    ("ViewScaleHint", "Ctrl + mouse wheel also works",
     "Geht auch mit Strg + Mausrad", "也可使用 Ctrl + 鼠标滚轮",
     "Ctrl + マウスホイールでも可"),

    # --- Einstellungen, Beschriftungen aus Dialog 131 ---
    ("PrefsTitle", "Preferences", "Einstellungen", "首选项", "設定"),
    ("PrefsDirectories", "Directories", "Verzeichnisse", "目录", "フォルダー"),
    ("PrefsScriptPath", "Script Path", "Skriptpfad", "脚本路径", "スクリプトのパス"),
    ("PrefsIbizePath", "Location of IBIZE.EXE", "Ort von IBIZE.EXE",
     "IBIZE.EXE 的位置", "IBIZE.EXE の場所"),
    ("PrefsCommandFile", "Command Description File", "Befehlsbeschreibung",
     "命令描述文件", "コマンド定義ファイル"),
    ("PrefsSourcePath", "Source Files Path", "Quelldateien",
     "源文件路径", "ソースファイルのパス"),
    ("PrefsBrowse", "Browse...", "Durchsuchen...", "浏览…", "参照…"),
    ("PrefsReopen", "Re-open last file at startup",
     "Letzte Datei beim Start öffnen", "启动时重新打开上次的文件",
     "起動時に前回のファイルを開く"),
    ("PrefsAlphaSort", "Alpha-sort edit pulldowns",
     "Klapplisten alphabetisch", "下拉列表按字母排序",
     "プルダウンを五十音順に"),
    ("PrefsAltIcons", "Use Alternative Coloured Icons",
     "Zweiter Farbsatz für Symbole", "使用备用配色图标",
     "アイコンの別配色を使う"),
    ("PrefsQuery", "Yes/No query on Open/New/Exit",
     "Nachfragen bei Öffnen/Neu/Beenden", "打开/新建/退出时询问",
     "開く・新規・終了時に確認"),
    ("PrefsLastDir", "File-Open dialog defaults to last dir",
     "Dateidialog beim zuletzt benutzten Ordner öffnen",
     "文件对话框默认使用上次的目录",
     "ファイルダイアログは前回のフォルダーを開く"),
    ("PrefsReload", "Reload command model", "Befehlsmodell neu laden",
     "重新加载命令模型", "コマンドモデルを再読み込み"),
    ("PrefsNote", "The command description file is read on startup and when reloading.",
     "Die Befehlsbeschreibung wird beim Start und beim Neuladen gelesen.",
     "命令描述文件在启动时和重新加载时读取。",
     "コマンド定義ファイルは起動時と再読み込み時に読まれます。"),

    ("MapLoad", "Load map...", "Karte laden...", "加载地图…", "マップを読み込む…"),
    ("MapClose", "Close map", "Karte schließen", "关闭地图", "マップを閉じる"),
    ("MapLoaded", "Map: %d entities, %d target names",
     "Karte: %d Entities, %d Zielnamen", "地图：%d 个实体，%d 个目标名",
     "マップ: %d エンティティ, %d ターゲット名"),
    ("MapPick", "From map", "Aus der Karte", "取自地图", "マップから"),

    # --- .pk3 ---
    ("Pk3Browse", "Browse .pk3...", "In .pk3 blättern...", "浏览 .pk3…",
     ".pk3 を参照…"),
    ("Pk3AddPath", "Add game folder...", "Spielordner hinzufügen...",
     "添加游戏目录…", "ゲームフォルダーを追加…"),
    ("Pk3Title", "Files in .pk3", "Dateien in .pk3", ".pk3 中的文件",
     ".pk3 内のファイル"),
    ("Pk3Archive", "Archive", "Archiv", "归档", "アーカイブ"),
    ("Pk3ArchiveInfo", "%d files", "%d Dateien", "%d 个文件", "%d ファイル"),
    ("Pk3AllArchives", "All archives", "Alle Archive", "所有归档",
     "すべてのアーカイブ"),
    ("Pk3Models", "Models", "Modelle", "模型", "モデル"),
    ("Pk3Filter", "Filter", "Filter", "筛选", "絞り込み"),
    ("Pk3Scripts", "Scripts", "Skripte", "脚本", "スクリプト"),
    ("Pk3Maps", "Maps", "Karten", "地图", "マップ"),
    ("Pk3Found", "%d archives, %d files", "%d Archive, %d Dateien",
     "%d 个归档，%d 个文件", "%d 個のアーカイブ, %d ファイル"),
    ("Pk3None", "No game folder added yet", "Noch kein Spielordner hinzugefügt",
     "尚未添加游戏目录", "ゲームフォルダーが未追加"),
    ("Pk3FromArchive", "from %s", "aus %s", "来自 %s", "%s から"),

    ("SoundPlayed", "Playing: %s", "Spielt: %s", "正在播放：%s", "再生中: %s"),
    ("SoundNotFound", "Sound not found: %s", "Klang nicht gefunden: %s",
     "未找到声音：%s", "サウンドが見つかりません: %s"),
    ("SoundNoDevice", "No sound device", "Kein Tongerät", "无音频设备",
     "サウンドデバイスがありません"),

    ("PrefsGamePaths", "Game folders (.pk3)", "Spielordner (.pk3)",
     "游戏目录（.pk3）", "ゲームフォルダー（.pk3）"),
    ("PrefsGamePathsNote",
     "The BOTTOM one wins: it overrides all above it, just as fs_game overrides base in the engine.",
     "Der UNTERSTE gewinnt: er überdeckt alle darüber, genau wie fs_game in der Engine base überdeckt.",
     "按顺序读取；后面的目录覆盖前面的，与引擎一致。",
     "順に読み込み、後のフォルダーが前を上書きします（エンジンと同じ）。"),
    ("PrefsRemove", "Remove", "Entfernen", "移除", "削除"),
    ("PrefsWins", "wins", "gewinnt", "优先", "優先"),
    ("PrefsEmptyPath", "no archives found - is the path right?",
     "keine Archive gefunden - stimmt der Pfad?", "未找到档案——路径正确吗？",
     "アーカイブが見つかりません — パスは正しいですか？"),
    ("PrefsOpenFolder", "Open", "Öffnen", "打开", "開く"),
    ("PrefsUp", "Up", "Hoch", "上移", "上へ"),
    ("PrefsDown", "Down", "Runter", "下移", "下へ"),
    # MapTextures, MapPvs*, Cull*, FxHalfRes, FxScale, MapScale und MapOnGpu
    # sind mit dem Software-Rasterer entfallen (rc568): sie schalteten nur
    # ihn.
    # FindHits ist weg: die Trefferzahl stand frueher in der Statuszeile.
    # Jetzt steht sie IM Suchfenster ("3 von 12"), weil das Fenster offen
    # bleibt und man dort hinsieht - nicht unten am Bildrand.
    # Die Pruefmeldungen. Der Kern schreibt sie auf Deutsch - er kennt keine
    # Uebersetzung, und fuer die Werkzeuge auf der Befehlszeile ist das
    # richtig. Die Oberflaeche setzt hier die eigene Sprache ein; die beiden
    # %s sind arg1 und arg2 aus dem Issue.
    ("VmsgV002", "annotation /*@%s*/ does not match %s",
     "Annotation /*@%s*/ passt nicht zu %s",
     "注解 /*@%s*/ 与 %s 不匹配", "注釈 /*@%s*/ は %s と一致しません"),
    ("VmsgV004", "%s is not in %s (kept unchanged)",
     "%s steht nicht in %s (bleibt unverändert erhalten)",
     "%s 不在 %s 中（保持不变）", "%s は %s にありません（そのまま保持）"),
    ("VmsgV006", "text field without quotes: %s",
     "Textfeld ohne Anführungszeichen: %s",
     "文本字段缺少引号：%s", "引用符のないテキスト項目：%s"),
    ("VmsgV007", "comparison expects = < > !, found: %s",
     "Vergleich erwartet = < > !, gefunden: %s",
     "比较应为 = < > !，实际：%s", "比較は = < > ! を想定、実際：%s"),
    ("VmsgV010", "command %s is not in the .bhc",
     "Befehl %s steht nicht in der .bhc",
     "命令 %s 不在 .bhc 中", "コマンド %s は .bhc にありません"),
    ("MsgCopied", "%d command(s) copied", "%d Befehl(e) kopiert",
     "已复制 %d 条命令", "%d 件のコマンドをコピーしました"),
    # Warum eine Bewegung nichts getan hat. Vorher stand der Grund nur im
    # Protokoll - das sieht der Benutzer beim Arbeiten nicht.
    ("MoveNoEnd", "Already at the end - nothing to do",
     "Steht schon an letzter Stelle - nichts zu tun",
     "已在末尾 - 无需操作", "すでに末尾です - 何もしません"),
    ("MoveNoTop", "Already at the top - nothing to do",
     "Steht schon an erster Stelle - nichts zu tun",
     "已在开头 - 无需操作", "すでに先頭です - 何もしません"),
    ("MoveNoSame", "Dropped where it already was - nothing to do",
     "Dort abgelegt, wo er schon stand - nichts zu tun",
     "放回原位 - 无需操作", "元の位置に置かれました - 何もしません"),
    ("MoveNoSelf", "A block cannot go into itself",
     "Ein Block kann nicht in sich selbst",
     "块不能放入自身", "ブロックは自分自身の中には入れられません"),
    ("MoveNoBlock", "Only affect, task, if, else, loop and do can hold commands",
     "Nur affect, task, if, else, loop und do koennen Befehle aufnehmen",
     "只有 affect、task、if、else、loop 和 do 可以容纳命令",
     "コマンドを収められるのは affect、task、if、else、loop、do だけです"),

    # --- In rc537 wiederhergestellt ------------------------------------
    #
    # Diese vierundzwanzig standen NUR in den erzeugten Dateien
    # include/bhed/i18n.h und src/i18n.cpp, nicht hier. Beide tragen im
    # Kopf "Erzeugt von tools/gen_i18n.py - nicht von Hand aendern", und
    # trotzdem waren sie von Hand eingetragen.
    #
    # Beim naechsten Lauf des Erzeugers waren sie weg, und der Bau unter
    # MSVC brach mit vierundzwanzig C2065 ab. Auf der Linux-Seite faellt
    # das nicht auf: gui/app.cpp braucht ImGui und wird dort gar nicht
    # uebersetzt. tools/lint_i18n_erzeugt.py prueft es jetzt.
    #
    # DebugDump hatte im Japanischen ausserdem ein verirrtes Byte 0x8f
    # mitten in "書く" - ein Tippfehler, wie er beim Eintragen von Hand
    # entsteht und beim Erzeugen nicht.
    ("MacroRowHint", "Part of a macro - these commands sit beside it, not inside it",
     "Teil eines Makros - diese Befehle stehen daneben, nicht darin",
     "宏的一部分", "マクロの一部"),
    ("VmsgV001", "command is not in the .bhc: %s",
     "Befehl steht nicht in der .bhc: %s",
     "命令不在 .bhc 中: %s", "%s は .bhc にない"),
    ("VmsgV005", "annotation /*@ ... */ not understood: %s",
     "Annotation /*@ ... */ nicht verstanden: %s",
     "注释 /*@ ... */ 无法识别: %s", "注釈 /*@ ... */ 不明: %s"),
    ("VmsgV008", "field expects a vector, found: %s",
     "Feld erwartet Vektor, gefunden: %s",
     "字段需要向量，实际: %s", "ベクトルを想定、実際: %s"),
    ("VmsgV009", "comparison expects = < > !, found: %s",
     "Vergleich erwartet = < > !, gefunden: %s",
     "比较应为 = < > !，实际: %s", "比較 = < > ! 、実際: %s"),
    ("VmsgV011", "text field without quotes: %s",
     "Textfeld ohne Anfuehrungszeichen: %s",
     "文本字段缺引号: %s", "引用符なし: %s"),
    ("MenuDebug", "Debug",
     "Debug",
     "调试", "デバッグ"),
    ("DebugLayout", "Show layout numbers",
     "Aufteilung zeigen",
     "显示布局数值", "レイアウト数値を表示"),
    ("DebugLayoutTitle", "Layout",
     "Aufteilung",
     "布局", "レイアウト"),
    ("DebugDump", "Write everything to the log",
     "Alles ins Protokoll schreiben",
     "写入日志", "すべてをログに書く"),
    ("DebugDumpHint", "Writes the current layout numbers to behaved-detail.log.",
     "Schreibt die aktuellen Aufteilungszahlen nach behaved-detail.log.",
     "将当前布局数值写入日志。", "現在の数値をログに書きます。"),
    ("DebugReset", "Restore default layout",
     "Aufteilung zuruecksetzen",
     "恢复默认布局", "レイアウトを戻す"),
    ("DebugResetDone", "Layout restored to defaults",
     "Aufteilung auf Vorgaben zurueckgesetzt",
     "布局已恢复", "レイアウトを戻しました"),
    ("DebugResetHint", "Sets all widths and split positions back to their defaults.",
     "Setzt alle Breiten und Teilerstellungen auf die Vorgaben zurueck.",
     "将所有宽度恢复为默认值。", "すべての幅を既定値に戻します。"),
    ("DebugCapture", "Capture a frame (RenderDoc)",
     "Bild aufnehmen (RenderDoc)",
     "抓取一帧 (RenderDoc)", "フレームを取得 (RenderDoc)"),
    ("DebugCaptureHint", "Captures the next frame. RenderDoc writes the file itself.",
     "Nimmt das naechste Bild auf. RenderDoc schreibt die Datei selbst.",
     "抓取下一帧。", "次のフレームを取得します。"),
    ("DebugNoRenderDoc", "Start behaved FROM RenderDoc - it cannot attach afterwards.",
     "behaved AUS RenderDoc heraus starten - nachtraeglich geht es nicht.",
     "请从 RenderDoc 启动。", "RenderDoc から起動してください。"),
    ("MapNeedsD3d",
     "The 3D view needs Direct3D 11. It is not available here (OpenGL fallback or a build without D3D) - the software renderer has been removed.",
     "Die 3D-Ansicht braucht Direct3D 11. Es steht hier nicht zur Verfügung (OpenGL-Rückfall oder ein Bau ohne D3D) - der Software-Zeichner ist entfernt.",
     "3D 视图需要 Direct3D 11，此处不可用。",
     "3D 表示には Direct3D 11 が必要です。ここでは使用できません。"),
    ("MapGlow", "Glow",
     "Glühen",
     "发光", "グロー"),
    ("MapGlowHint", "Second pass over the stages marked `glow` in the shader, blurred and laid over the picture. This is what JKA does with r_DynamicGlow; without it lightsabers, consoles, lava and holograms have hard edges instead of blooming.",
     "Zweiter Durchgang über die Stufen mit `glow` im Shader, weichgezeichnet und über das Bild gelegt. Genau das macht JKA mit r_DynamicGlow; ohne es haben Lichtsäber, Konsolen, Lava und Hologramme harte Kanten statt zu blühen.",
     "对着著色器中标记 `glow` 的阶段进行第二次渲染。", "シェーダーの `glow` 段階を二回目に描画します。"),
    ("MapShadows", "Shadows",
     "Schatten",
     "阴影", "影"),
    ("MapShadowsHint", "Character shadows like the game's cg_shadows: Blob (1) is the round spot under every character, the game's default. Stencil (2) casts the character's real outline as a shadow volume and darkens everything inside it, the character too. Planar (3) presses the character flat and black onto the floor.",
     "Figurenschatten wie cg_shadows im Spiel: Rund (1) ist der Fleck unter jeder Figur, die Vorgabe des Spiels. Volumen (2) wirft den echten Umriss als Schattenvolumen und dunkelt alles darin ab, auch die Figur selbst. Projiziert (3) drückt die Figur flach und schwarz auf den Boden.",
     "与游戏 cg_shadows 相同的角色阴影：圆形(1)为默认；体积(2)投射真实轮廓；投影(3)把角色压平成黑色投到地面。",
     "ゲームの cg_shadows と同じキャラクターの影：丸(1)が既定、ボリューム(2)は実際の輪郭、投影(3)は床に黒く平らに映します。"),
    ("MapFog", "Fog", "Nebel", "雾", "霧"),
    ("MapFogHint", "The map's fog volumes and global fog, as the game draws them with r_drawfog 1: walls, floors and characters inside the fog fade into its color with distance.",
     "Die Nebelvolumen und der globale Nebel der Karte, wie das Spiel sie mit r_drawfog 1 zeichnet: Wände, Boden und Figuren im Nebel verschwinden mit der Entfernung in seiner Farbe.",
     "地图的雾体积和全局雾，与游戏中 r_drawfog 1 相同。", "マップの霧ボリュームとグローバル霧（ゲームの r_drawfog 1 と同じ）。"),
    ("MapShadowOff", "Off", "Aus", "关", "オフ"),
    ("MapShadowBlob", "Blob (1)", "Rund (1)", "圆形 (1)", "丸 (1)"),
    ("MapShadowStencil", "Stencil (2)", "Volumen (2)", "体积 (2)", "ボリューム (2)"),
    ("MapShadowPlanar", "Planar (3)", "Projiziert (3)", "投影 (3)", "投影 (3)"),
    ("MapDynLights", "Dynamic light",
     "Dynamisches Licht",
     "动态光", "動的ライト"),
    ("MapDynLightsHint", "Lightsabers and effect lights light up walls, floors and characters, as in the game with r_dynamiclight 1.",
     "Lichtschwerter und Effektlichter hellen Wände, Boden und Figuren auf, wie im Spiel mit r_dynamiclight 1.",
     "光剑和特效光照亮墙壁、地面和角色，与游戏中 r_dynamiclight 1 相同。", "ライトセーバーやエフェクトの光が壁・床・キャラクターを照らします（ゲームの r_dynamiclight 1 と同じ）。"),
    ("ModelSurfaceDump", "Click to list every surface in the detail log",
     "Klicken: alle Flächen ins Detailprotokoll",
     "点击：列出所有面", "クリック：全サーフェス"),
    ("AnswerYes", "Yes", "Ja", "是", "はい"),
    ("AnswerNo", "No", "Nein", "否", "いいえ"),
    # Zusatzfragen bei "Yes/No query on Open/New/Exit" (Original: "Open%s?",
    # "New?", "Exit?")
    # Start und Speichern - Wortlaut des Originals
    ("AskAutoLoad", "Auto-load most recent file: \"%s\"?",
     "Zuletzt bearbeitete Datei laden: \"%s\"?",
     "自动加载最近的文件：\"%s\"？", "最近のファイルを自動で開きますか: \"%s\""),
    ("AskCrashReopen",
     "It appears you didn't exit BehavEd legally last time (crash?)\n"
     "Should I still try and open your last file \"%s\"?",
     "BehavEd wurde beim letzten Mal nicht sauber beendet (Absturz?).\n"
     "Soll die zuletzt bearbeitete Datei \"%s\" trotzdem geöffnet werden?",
     "上次似乎没有正常退出 BehavEd（崩溃？）\n仍要尝试打开上次的文件 \"%s\" 吗？",
     "前回 BehavEd が正常に終了しなかったようです（クラッシュ？）\n"
     "それでも前回のファイル \"%s\" を開きますか？"),
    ("AskUnprotect",
     "The file \"%s\" is write-protected.\n"
     "Do you want me to un-writeprotect it so you can save over it?\n"
     "('No' will abort the save)",
     "Die Datei \"%s\" ist schreibgeschützt.\n"
     "Schreibschutz aufheben, damit sie überschrieben werden kann?\n"
     "(\"Nein\" bricht das Speichern ab)",
     "文件 \"%s\" 是只读的。\n要取消只读以便覆盖保存吗？\n（选\"否\"将取消保存）",
     "ファイル \"%s\" は書き込み禁止です。\n上書きできるよう書き込み禁止を解除しますか？\n"
     "（\"いいえ\" で保存を中止）"),
    # Tastenbelegung (Einstellungen): Numpad +/-, Strg+T
    ("ActExpandNode", "Expand item and all its children",
     "Eintrag samt Unterpunkten aufklappen", "展开项目及其所有子项",
     "項目とすべての子項目を展開"),
    ("ActCollapseNode", "Collapse item", "Eintrag zuklappen", "折叠项目",
     "項目を折りたたむ"),
    ("ActExpandAll", "Expand all", "Alles aufklappen", "全部展开", "すべて展開"),
    ("ActCollapseAll", "Collapse all", "Alles zuklappen", "全部折叠",
     "すべて折りたたむ"),
    ("ActCutAlt", "Cut (second key)", "Ausschneiden (zweite Taste)",
     "剪切（第二个键）", "切り取り（2 番目のキー）"),
    ("AskNew", "New?", "Neu?", "新建？", "新規作成しますか？"),
    ("AskExit", "Exit?", "Beenden?", "退出？", "終了しますか？"),
    # Copy ohne Auswahl, Paste fremden Texts (Original 04e090, 04e118)
    ("AskCopyAll", "No line selected, Copy entire script?",
     "Keine Zeile ausgewählt. Ganzes Skript kopieren?",
     "未选择任何行，要复制整个脚本吗？",
     "行が選択されていません。スクリプト全体をコピーしますか？"),
    ("AskPasteForeign",
     "You seem to be pasting something that wasn't copied from BehavEd\nProceed?",
     "Das scheint nicht aus BehavEd kopiert worden zu sein.\nTrotzdem einfügen?",
     "您粘贴的内容似乎不是从 BehavEd 复制的。\n继续吗？",
     "BehavEd からコピーされたものではないようです。\n続行しますか？"),
    # Titel des Dateidialogs hinter "..." (Original 051228)
    ("BrowseTitle", "Browse to file", "Datei suchen", "浏览文件", "ファイルを参照"),
    ("GizmoMoved",
     "Key moved by %.0f %.0f %.0f - preview only, not written to the script yet",
     "Schlüssel um %.0f %.0f %.0f verschoben - nur Vorschau, noch nicht ins Skript geschrieben",
     "关键点移动 %.0f %.0f %.0f（仅预览，尚未写入脚本）",
     "キーを %.0f %.0f %.0f 移動（プレビューのみ、スクリプト未反映）"),
    ("GizmoSelect", "Select", "Auswählen", "选择", "選択"),
    ("GizmoMove", "Move", "Verschieben", "移动", "移動"),
    ("GizmoRotate", "Rotate", "Drehen", "旋转", "回転"),
    ("GizmoSpaceWorld", "World", "Welt", "世界", "ワールド"),
    ("GizmoSpaceLocal", "Local", "Lokal", "本地", "ローカル"),
    ("TlUnitFrames", "Frames", "Bilder", "帧", "フレーム"),
    ("TlUnitSeconds", "Seconds", "Sekunden", "秒", "秒"),
    ("TlUnitHint", "Switch between frames and seconds", "Zwischen Bildern und Sekunden umschalten", "在帧与秒之间切换", "フレームと秒を切替"),
    ("TlFrame", "frame %d of %d", "Bild %d von %d", "第 %d 帧，共 %d", "%d / %d フレーム"),
    ("TlSpanSeconds", "%.2f s to %.2f s", "%.2f s bis %.2f s", "%.2f 秒 至 %.2f 秒", "%.2f 秒～%.2f 秒"),
    ("TlSpanFrames", "frame %d to %d", "Bild %d bis %d", "第 %d 帧至 %d 帧", "%d～%d フレーム"),
    ("TlClickHint", "Click to jump to this line", "Anklicken springt zu dieser Zeile", "点击跳转到该行", "クリックでその行へ"),
    ("TlActorHint", "Click to show where this figure walks", "Anklicken zeigt, wo diese Figur entlanggeht", "点击查看该角色的路径", "この人物の移動経路を表示"),
    ("SplitNeedsTabs", "Open another script first - a pane needs its own", "Erst ein weiteres Skript oeffnen - jedes Feld braucht ein eigenes", "请先打开另一个脚本", "先に別のスクリプトを開いてください"),
    ("TlZoomHint", "Click or drag on the ruler to set the time. Wheel over the ruler zooms, Ctrl+wheel over the tracks, middle button drags",
     "Klicken oder Ziehen im Lineal setzt die Zeit. Rad ueber dem Lineal vergroessert, Strg+Rad ueber den Spuren, mittlere Taste schiebt",
     "在标尺上点击或拖动设置时间。滚轮缩放，中键拖动", "ルーラーをクリック/ドラッグで時間を設定。ホイールで拡大、中ボタンで移動"),
    ("TlOverlap", "Overlaps the next %s (starts at %.2f s). In the game that one starts from where this one BEGAN - the camera jumps back.",
     "Laeuft in den naechsten %s hinein (beginnt bei %.2f s). Im Spiel faengt der dort an, wo dieser BEGANN - die Kamera springt zurueck.",
     "与下一个 %s 重叠（开始于 %.2f 秒）。游戏中下一个会从这个的起点开始 - 镜头会跳回。",
     "次の %s（%.2f 秒開始）と重なります。ゲームではこの開始位置から始まり、カメラが戻ります。"),
    ("TlIdleTracks", "Idle tracks (%d)", "Ruhige Spuren (%d)", "空闲轨道 (%d)", "動きのないトラック (%d)"),
    ("TlIdleTracksHint", "Tracks where nothing happens after the start - hidden to save space. Tick to show them.",
     "Spuren, in denen nach dem Anfang nichts mehr passiert - ausgeblendet, um Platz zu sparen. Anhaken zeigt sie.",
     "开始后没有任何动作的轨道 - 为节省空间而隐藏。勾选以显示。", "開始後に何も起きないトラック - 省スペースのため非表示。チェックで表示。"),
    ("TlFit", "All", "Alles", "全部", "全体"),
    ("TlZoomFactor", "%.0fx", "%.0f-fach", "%.0f 倍", "%.0f 倍"),
    ("TlZoomMenu", "Zoom", "Zoom", "缩放", "ズーム"),
    ("TlZoomToPlay", "Centre on the playhead", "Auf den Zeiger stellen", "居中于播放头", "再生位置を中央へ"),
    ("TlDragDur", "Duration %.2f s", "Dauer %.2f s", "时长 %.2f 秒", "長さ %.2f 秒"),
    ("TlSpeedHint", "Playback speed. Sound only plays at 1x (it cannot be slowed down with the picture)", "Abspieltempo. Ton nur bei 1x (er ließe sich nicht mit dem Bild verlangsamen)", "播放速度。仅在 1x 时播放声音", "再生速度。音声は 1x のときのみ"),
    ("TlLoop", "Loop", "Schleife", "循环", "ループ"),
    ("TlFromScript", "From another script: %s", "Aus einem anderen Skript: %s", "来自另一个脚本：%s", "別のスクリプトから: %s"),
    ("TlLoopHint", "Start again from the beginning at the end", "Am Ende wieder von vorn", "结束后从头开始", "最後まで来たら最初から"),
    ("TlMoveDelta", "Move %+.2f s", "Verschieben %+.2f s", "移动 %+.2f 秒", "移動 %+.2f 秒"),
    ("TlMoveRipple", "Everything after moves along (hold Alt: only this block)", "Alles danach rückt mit (Alt: nur dieser Block)", "之后的内容一起移动（按住 Alt：仅此块）", "後ろもまとめて移動（Alt: このブロックのみ）"),
    ("TlMoveSlide", "Only this block, the rest stays", "Nur dieser Block, der Rest bleibt stehen", "仅此块，其余不动", "このブロックのみ、他は動かない"),
    ("TlMoveNone", "Cannot move further: the wait before is used up", "Weiter geht es nicht: das wait davor ist aufgebraucht", "无法继续移动：前面的等待已用完", "これ以上動かせません: 前の wait を使い切りました"),
    ("GizmoApply", "Write to script", "Ins Skript schreiben",
     "写入脚本", "スクリプトに書き込む"),
    ("GizmoWrote", "written to the script - Ctrl+Z undoes it",
     "ins Skript geschrieben - Strg+Z nimmt es zurück",
     "已写入脚本，Ctrl+Z 可撤销", "スクリプトに書き込みました（Ctrl+Z で取り消し）"),
    ("GizmoWroteShort", "written to the script", "ins Skript geschrieben", "已写入脚本", "スクリプトに書き込み済み"),
    ("GizmoNoMove", "this key has no MOVE to write to",
     "dieser Schlüssel hat kein MOVE zum Schreiben",
     "该关键点没有可写入的 MOVE", "このキーには書き込む MOVE がありません"),
    ("GizmoTurned",
     "Key turned by %.0f %.0f %.0f - preview only, not written to the script yet",
     "Schlüssel um %.0f %.0f %.0f gedreht - nur Vorschau, noch nicht ins Skript geschrieben",
     "关键点旋转 %.0f %.0f %.0f（仅预览）",
     "キーを %.0f %.0f %.0f 回転（プレビューのみ）"),
    # GizmoRotateSoon ist weg: der Eintrag "Drehen" stand abgeblendet da,
    # mit dem Hinweis, dass er mit dem Rueckschreiben kommt. Er ist da.
    ("GizmoReset", "Reset", "Zurücksetzen", "重置", "リセット"),
    # Vier Befehle haben in der .bhc KEINE Beschreibung: camera, task, do
    # und play. Das ist eine Luecke in Ravens Datei, kein Fehler bei uns -
    # das Original zeigt dort ebenso nichts. Weil aber jeder andere Befehl
    # eine hat, sieht es nach einem Fehler aus, und man erfaehrt gerade bei
    # camera nichts ueber den wichtigsten Befehl im Skript.
    #
    # Also ein Ersatz von uns. Er tritt NUR ein, wo die .bhc nichts sagt -
    # was dort steht, hat immer Vorrang.
    ("DescCamera",
     "control the cutscene camera - the sub-command follows as the first argument",
     "die Kamera der Zwischensequenz steuern - der Unterbefehl steht als erstes Argument",
     "控制过场动画摄像机——子命令为第一个参数",
     "カットシーンのカメラを操作します（サブコマンドが第1引数）"),
    ("DescTask",
     "declare a named task; do() starts it, dowait() waits for it to finish",
     "eine benannte Aufgabe erklären; do() startet sie, dowait() wartet auf ihr Ende",
     "声明一个命名任务；do() 启动，dowait() 等待其完成",
     "名前付きタスクを宣言します。do() で開始、dowait() で完了待ち"),
    ("DescDo",
     "start a task declared with task() - does not wait for it",
     "eine mit task() erklärte Aufgabe starten - wartet nicht auf sie",
     "启动由 task() 声明的任务（不等待）",
     "task() で宣言したタスクを開始します（待機しません）"),
    ("DescPlay",
     "play a ROFF animation on the entity being affected",
     "eine ROFF-Bewegung auf der betroffenen Entity abspielen",
     "在受影响的实体上播放 ROFF 动画",
     "対象エンティティで ROFF アニメーションを再生します"),
    ("ActSaveAll", "Save all", "Alle sichern", "全部保存", "すべて保存"),
    ("HintSaveAll",
     "Save every tab that has unsaved changes. Tabs opened from an archive "
     "have no path to write back to and are skipped.",
     "Jeden Reiter mit ungesicherten Änderungen speichern. Reiter aus einem "
     "Archiv haben keinen Pfad zum Zurückschreiben und werden übersprungen.",
     "保存所有有未保存更改的标签页。", "未保存の変更があるタブをすべて保存します。"),
    ("MsgAlreadyOpen", "%s is already open - switched to its tab",
     "%s ist schon offen - zu seinem Reiter gewechselt",
     "%s 已打开 - 已切换到其标签页", "%s は既に開いています - そのタブに切り替えました"),
    ("MsgSavedAll", "%d saved, %d without a path",
     "%d gesichert, %d ohne Pfad", "已保存 %d 个，%d 个无路径",
     "%d 件を保存、%d 件はパスなし"),
    ("SplitOnlyScript",
     "Only in the script view - the map and model views need the room.",
     "Nur in der Skriptansicht - Karte und Modell brauchen den Platz.",
     "仅在脚本视图中可用。", "スクリプト表示でのみ利用できます。"),
    # SplitDetail und SplitDetailHint standen hier, wurden aber nirgends
    # mehr benutzt - lint_i18n.py hat sie gemeldet ("haeufig die Spur
    # einer Bedienung, die verlorengegangen ist"). Sie fehlten auch in
    # den eingecheckten i18n-Dateien; die Doppelklick-Bedienung, zu der
    # sie gehoerten, gibt es nicht mehr.
    ("SplitTitle", "Compare", "Vergleich", "对比", "比較"),
    ("SplitToggle", "Split view", "Geteilte Ansicht", "分屏", "分割表示"),
    ("SplitHint",
     "Show a second script beside the first - for comparing and for copying "
     "commands across. The right half is read-only; to edit it, switch to "
     "its tab.",
     "Ein zweites Skript daneben zeigen - zum Vergleichen und zum "
     "Übertragen von Befehlen. Die rechte Hälfte ist nur zum Lesen; wer "
     "dort ändern will, wechselt auf ihren Reiter.",
     "在旁边显示第二个脚本，用于比较和复制命令。",
     "2 つ目のスクリプトを横に表示します（比較とコピー用）。"),
    ("FindNextBtn", "Find next", "Weiter", "查找下一个", "次を検索"),
    ("FindPrevBtn", "Find previous", "Zurück", "查找上一个", "前を検索"),
    ("FindClose", "Close", "Schließen", "关闭", "閉じる"),
    ("FindAtOf", "%d of %d", "%d von %d", "第 %d / %d 个", "%d / %d 件"),
    ("FindReplaceWith", "Replace with", "Ersetzen durch", "替换为", "置換後"),
    ("FindReplaceBtn", "Replace", "Ersetzen", "替换", "置換"),
    ("FindReplaceAll", "Replace All", "Alle ersetzen", "全部替换", "すべて置換"),
    ("FindReplaceAllDocs", "Replace All in All Opened Documents",
     "Alle ersetzen in allen offenen Dokumenten", "在所有打开的文档中全部替换",
     "開いているすべての文書で置換"),
    ("FindInSelection", "In selection", "In Auswahl", "在选区内", "選択範囲内"),
    ("FindReplacedN", "%d replaced", "%d ersetzt", "已替换 %d 处", "%d 件置換"),
    ("FindReplacedDocs", "%d replaced in %d documents", "%d ersetzt in %d Dokumenten",
     "已在 %d 个文档中替换 %d 处", "%d 件置換 (%d 文書)"),
    ("FindReplaceHint", "Replaces what \"Item containing\" finds - each field on its own. "
     "Numbers and vectors are written like BehavEd (1299.000). One undo step.",
     "Ersetzt, was \"Item containing\" findet - jedes Feld f\u00fcr sich. Zahlen und "
     "Vektoren schreibt es wie BehavEd (1299.000). Ein R\u00fcckg\u00e4ngig-Schritt.",
     "替换“Item containing”找到的内容——每个字段单独处理。一次撤销。",
     "「Item containing」で見つかった内容を置換します。元に戻すは 1 回。"),
    ("FindNone", "Search string not found", "Suchtext nicht gefunden",
     "未找到搜索字符串", "検索文字列が見つかりません"),
    ("PathsTitle", "Game folders", "Spielordner", "游戏目录", "ゲームフォルダー"),
    ("MissionSharesMap", "same map:", "gleiche Karte:", "相同地图：", "同じマップ:"),
    ("InterplayTitle", "Scripts together", "Skripte im Zusammenspiel", "脚本协作", "スクリプト連携"),
    ("InterplaySignals", "Signals", "Signale", "信号", "シグナル"),
    ("InterplayCamera", "Camera", "Kamera", "摄像机", "カメラ"),
    ("InterplayNoScripts", "No scripts open", "Keine Skripte offen", "未打开脚本", "スクリプトなし"),
    ("InterplayOrphan", "nobody waits for it", "niemand wartet darauf", "无人等待", "待機者なし"),
    ("InterplayStuck", "nobody sends it", "niemand sendet es", "无人发送", "送信者なし"),
    ("InterplayCameraWarn", "more than one script drives the camera", "mehr als ein Skript treibt die Kamera", "多个脚本控制摄像机", "複数のスクリプトがカメラを操作"),
    ("PrefsArchives", "%d archives, %d files", "%d Archive, %d Dateien",
     "%d 个归档，%d 个文件", "%d 個のアーカイブ, %d ファイル"),

    # Namen der Bearbeitungsschritte, fuer "Rueckgaengig: Löschen"
    ("StepInsert", "insert", "Einfügen", "插入", "挿入"),
    ("StepDelete", "delete", "Löschen", "删除", "削除"),
    ("StepClone", "clone", "Klonen", "复制", "複製"),
    ("StepMove", "move", "Verschieben", "移动", "移動"),
    ("StepComment", "comment out", "Auskommentieren", "注释掉", "コメント化"),
    ("StepUncomment", "uncomment", "Entkommentieren", "取消注释", "コメント解除"),
    ("StepEdit", "edit", "Ändern", "编辑", "編集"),
    ("StepPaste", "paste", "Einfügen", "粘贴", "貼り付け"),
    ("UndoOf", "Undo: %s", "Rückgängig: %s", "撤销：%s", "元に戻す: %s"),
    ("ChangeAdded", "inserted", "eingefügt", "插入", "挿入"),
    ("ChangeRemoved", "deleted", "gelöscht", "删除", "削除"),
    ("ChangeEdited", "changed", "geändert", "修改", "変更"),
    ("ChangeMoved", "moved", "verschoben", "移动", "移動"),
    ("UpdTitle", "Update", "Update", "更新", "アップデート"),
    ("UpdMenu", "Check for updates...", "Nach Updates suchen...", "检查更新...", "アップデートを確認..."),
    ("UpdAutoCheck", "Check for updates at startup", "Beim Start nach Updates suchen", "启动时检查更新",
     "起動時にアップデートを確認"),
    ("UpdInstalled", "Installed: %s", "Installiert: %s", "已安装：%s", "インストール済み: %s"),
    ("UpdChecking", "Checking GitHub for a newer version...", "Frage GitHub nach einer neueren Fassung...",
     "正在 GitHub 上检查新版本...", "GitHub で新しいバージョンを確認中..."),
    ("UpdCurrent", "You have the newest version.", "Du hast die neueste Fassung.", "已是最新版本。", "最新バージョンです。"),
    ("UpdAvailable", "New version available: %s", "Neue Fassung verfügbar: %s", "有新版本：%s", "新しいバージョン: %s"),
    ("UpdNotes", "What's new:", "Was ist neu:", "更新内容：", "新機能:"),
    ("UpdInstall", "Download and install", "Herunterladen und installieren", "下载并安装", "ダウンロードしてインストール"),
    ("UpdDone", "Installed (%d files). Restart to use the new version.",
     "Installiert (%d Dateien). Neu starten, um die neue Fassung zu benutzen.",
     "已安装（%d 个文件）。重新启动以使用新版本。", "インストール完了（%d ファイル）。再起動で新バージョンになります。"),
    ("UpdRestart", "Restart now", "Jetzt neu starten", "立即重新启动", "今すぐ再起動"),
    ("UpdRetry", "Check again", "Erneut prüfen", "再次检查", "再確認"),
    ("UpdOpenPage", "Open release page", "Release-Seite öffnen", "打开发布页面", "リリースページを開く"),
    ("UpdLater", "Later", "Später", "稍后", "後で"),
    ("UpdError", "Update failed: %s", "Update fehlgeschlagen: %s", "更新失败：%s", "アップデート失敗: %s"),
    ("UpdNoAccess",
     "GitHub refuses the request right now (too many requests without sign-in). Please try again later.",
     "GitHub lehnt die Anfrage gerade ab (zu viele Anfragen ohne Anmeldung). Bitte später erneut versuchen.",
     "GitHub 暂时拒绝请求（未登录请求过多）。请稍后再试。",
     "GitHub が現在リクエストを拒否しています（未ログインのリクエストが多すぎます）。後でもう一度お試しください。"),
    ("UpdNoRelease", "No release found on GitHub.", "Auf GitHub ist kein Release zu finden.", "GitHub 上没有发布版本。",
     "GitHub にリリースがありません。"),
    ("UpdNoZip", "The release has no .zip file.", "Das Release enthält keine .zip-Datei.", "该发布没有 .zip 文件。",
     "リリースに .zip ファイルがありません。"),
    ("UpdStatusAvail", "Update %s available - click", "Update %s verfügbar - klicken", "有更新 %s - 点击", "アップデート %s あり - クリック"),
    ("UpdStatusDone", "Update installed - click to restart", "Update installiert - klicken zum Neustart",
     "更新已安装 - 点击重新启动", "アップデート完了 - クリックで再起動"),
    ("UndoHistoryStatus", "Undo history (%d): %s", "Rückgängig-Liste (%d): %s", "撤销历史 (%d)：%s",
     "元に戻す履歴 (%d): %s"),
    # Fehler des Updaters: %s ist ein Name/Text, %lld eine Fehlernummer.
    ("UpdErrUrl", "Address not readable%s", "Adresse nicht lesbar%s", "地址无法读取%s", "アドレスを読めません%s"),
    ("UpdErrHttp", "WinHTTP is not available%s", "WinHTTP ist nicht verfügbar%s", "WinHTTP 不可用%s", "WinHTTP が使えません%s"),
    ("UpdErrConnect", "Cannot connect to %s", "Keine Verbindung zu %s", "无法连接到 %s", "%s に接続できません"),
    ("UpdErrNoAnswer", "No answer from %s (error %lld)", "Keine Antwort von %s (Fehler %lld)",
     "%s 无响应（错误 %lld）", "%s から応答がありません（エラー %lld）"),
    ("UpdErrAbort", "Download interrupted%s", "Download abgebrochen%s", "下载中断%s", "ダウンロードが中断されました%s"),
    ("UpdErrBadAnswer", "Unexpected answer from GitHub: %s (HTTP %lld)", "Unerwartete Antwort von GitHub: %s (HTTP %lld)",
     "GitHub 返回意外响应：%s（HTTP %lld）", "GitHub から予期しない応答: %s（HTTP %lld）"),
    ("UpdErrZip", "The downloaded .zip cannot be read: %s", "Das geladene .zip ist nicht lesbar: %s",
     "下载的 .zip 无法读取：%s", "ダウンロードした .zip を読めません: %s"),
    ("UpdErrWrite", "%s cannot be written", "%s lässt sich nicht schreiben", "%s 无法写入", "%s を書き込めません"),
    ("UpdErrReplace", "%s cannot be replaced (error %lld)", "%s lässt sich nicht ersetzen (Fehler %lld)",
     "%s 无法替换（错误 %lld）", "%s を置き換えられません（エラー %lld）"),
    ("UpdErrRestart", "Restart failed%s (error %lld)", "Neustart fehlgeschlagen%s (Fehler %lld)",
     "重新启动失败%s（错误 %lld）", "再起動に失敗しました%s（エラー %lld）"),
    ("UndoUpToHere", "Undo up to here", "Bis hierher rückgängig", "撤销到此处", "ここまで元に戻す"),
    ("UndoUpToHereHint", "Undoes the selected step and all newer ones above it",
     "Nimmt den markierten Schritt und alle neueren darüber zurück",
     "撤销所选步骤及其上方所有较新的步骤", "選択した手順とその上の新しい手順をすべて元に戻します"),
    ("RedoUpToHere", "Redo up to here", "Bis hierher wiederholen", "重做到此处", "ここまでやり直す"),
    ("RedoUpToHereHint", "Redoes the selected step and all steps above it",
     "Wiederholt den markierten Schritt und alle darüber",
     "重做所选步骤及其上方所有步骤", "選択した手順とその上の手順をすべてやり直します"),
    ("UndoOnlyThis", "Undo only this step", "Nur diesen Schritt rückgängig", "仅撤销此步骤", "この手順だけ元に戻す"),
    ("UndoOnlyThisHint",
     "Undoes only the clicked step - everything done after it stays",
     "Nimmt nur den angeklickten Schritt zurück - alles, was danach kam, bleibt",
     "仅撤销所点击的步骤 - 之后的操作保持不变", "クリックした手順だけを元に戻します - それ以降の操作はそのまま"),
    ("StepSelectiveUndo", "undo of one step", "einzelner Schritt zurückgenommen", "撤销单个步骤", "単一手順の取り消し"),
    ("FileBackupFolder", "Open backup folder", "Sicherungsordner öffnen", "打开备份文件夹", "バックアップフォルダーを開く"),
    ("FileBackupFolderHint",
     "Every save and compile keeps a copy: backup1.txt (newest) to backup10.txt, next to behaved.exe",
     "Jedes Speichern und Kompilieren legt eine Kopie ab: backup1.txt (neueste) bis backup10.txt, neben behaved.exe",
     "每次保存和编译都会保留副本：backup1.txt（最新）到 backup10.txt，位于 behaved.exe 旁",
     "保存とコンパイルのたびにコピーを残します: backup1.txt（最新）〜 backup10.txt、behaved.exe の隣"),
    ("UndoSteps", "Undo: %d steps", "Rückgängig: %d Schritte", "撤销：%d 步", "元に戻す: %d 手順"),
    ("RedoSteps", "Redo: %d steps", "Wiederholen: %d Schritte", "重做：%d 步", "やり直し: %d 手順"),
    ("UndoListTitle", "Undo history - newest at the top", "Rückgängig-Liste - neuester oben",
     "撤销历史 - 最新在上", "元に戻す履歴 - 新しい順"),
    ("RedoListTitle", "Redo history - next at the top", "Wiederholen-Liste - nächster oben",
     "重做历史 - 下一步在上", "やり直し履歴 - 次が上"),
    ("UndoListEmpty", "Nothing to undo", "Nichts rückgängig zu machen", "没有可撤销的操作", "元に戻す操作はありません"),
    ("UndoListHint", "Click: show the undo history (all steps)", "Klick: Rückgängig-Liste zeigen (alle Schritte)",
     "单击：显示撤销历史（所有步骤）", "クリック：元に戻す履歴を表示（全手順）"),
    ("UndoListMenu", "Undo history...", "Rückgängig-Liste...", "撤销历史...", "元に戻す履歴..."),
    ("RedoOf", "Redo: %s", "Wiederholen: %s", "重做：%s", "やり直し: %s"),

    ("MoveUp", "Move up", "Nach oben", "上移", "上へ"),
    ("MoveDown", "Move down", "Nach unten", "下移", "下へ"),
    ("AboutTitle", "About BehavEd", "Über BehavEd", "关于 BehavEd",
     "BehavEd について"),
    ("AboutOriginal",
     "Reconstruction of BehavEd 2.0 by Raven Software (1999), written by "
     "Josh Weier and Ste Cork.",
     "Nachbau von BehavEd 2.0 von Raven Software (1999), geschrieben von "
     "Josh Weier und Ste Cork.",
     "Raven Software 的 BehavEd 2.0（1999）的重建版，原作者 Josh Weier 与 Ste Cork。",
     "Raven Software の BehavEd 2.0（1999）の再構築版。原作者は Josh Weier と Ste Cork。"),
    ("AboutVerified", "Verified against", "Geprüft gegen", "验证依据", "検証対象"),
    ("AboutResources", "Resources of BehavEd.exe: dialogs, accelerators, bitmaps",
     "Ressourcen von BehavEd.exe: Dialoge, Tastentabelle, Bitmaps",
     "BehavEd.exe 的资源：对话框、快捷键表、位图",
     "BehavEd.exe のリソース: ダイアログ、アクセラレータ、ビットマップ"),
    ("AboutIbi", "1011 .ibi from IBIZE.EXE, byte for byte",
     "1011 .ibi von IBIZE.EXE, byteweise", "来自 IBIZE.EXE 的 1011 个 .ibi，逐字节",
     "IBIZE.EXE の .ibi 1011 個、バイト単位"),
    ("AboutIcarus", "1510 .icarus from Raven, byte for byte",
     "1510 .icarus von Raven, byteweise", "来自 Raven 的 1510 个 .icarus，逐字节",
     "Raven の .icarus 1510 個、バイト単位"),
    ("AboutModel", "%d signatures, %d type sets, %d macros",
     "%d Signaturen, %d Typmengen, %d Makros", "%d 个签名，%d 个类型集，%d 个宏",
     "%d シグネチャ, %d 型集合, %d マクロ"),
    ("AboutClose", "Close", "Schließen", "关闭", "閉じる"),
    # Der Hinweis sagt jetzt BEIDES. Das Feld konnte den getippten Text
    # schon immer uebernehmen - nur stand da "Tippen zum Filtern", und
    # niemand kam auf die Idee, dass Enter einen eigenen Wert einträgt.
    ("FilterHint", "Filter, or type your own value",
     "Filtern oder eigenen Wert eintippen", "筛选，或输入自定义值",
     "絞り込み、または独自の値を入力"),
    # Steht als anklickbare Zeile ganz oben, sobald der getippte Text nicht
    # genau einem Eintrag entspricht. %s ist der getippte Text.
    ("UseTyped", "Use \"%s\"", "\"%s\" übernehmen", "使用“%s”",
     "「%s」を使う"),
    # Diagnose, keine Einstellung: die Grundstellung der .gla legt die
    # Blickrichtung auf +/-Y, die Engine auf +X. Ein Blick auf den Bildschirm
    # entscheidet, welcher Zuschlag stimmt.
    ("ActorYaw", "Actor facing +%d°", "Blickrichtung +%d°", "角色朝向 +%d°",
     "キャラの向き +%d°"),
    ("MapEffects", "Effects", "Effekte", "特效", "エフェクト"),
    ("StatusReady", "Ready", "Bereit", "就绪", "準備完了"),
    # Einzahl/Mehrzahl unterscheiden wir nicht - die Zahl steht davor.
    ("StatusErrors", "errors", "Fehler", "个错误", "件のエラー"),
    ("StatusWarnings", "notes", "Anmerkungen", "条提示", "件の注記"),
    ("ActRevertOriginal", "Revert to original", "Auf Original zuruecksetzen",
     "恢复为原始", "元に戻す（開いた時の状態）"),
    ("ActRevertOriginalHint",
     "Restore the selected command(s) as they were when the file was opened - only this command, not the rest",
     "Den oder die gewaehlten Befehle so wiederherstellen, wie sie beim Oeffnen der Datei waren - nur diese, nicht den Rest",
     "将所选命令恢复为打开文件时的状态 - 仅此命令，不影响其他",
     "選択したコマンドをファイルを開いた時の状態に戻す - このコマンドのみ"),
    ("RevertPreviewLabel", "Original:", "Original:", "原始：", "元の状態："),
    ("RevertPreviewNew", "New command - there is no original", "Neuer Befehl - es gibt kein Original",
     "新命令 - 没有原始版本", "新しいコマンド - 元の状態はありません"),
    ("RevertPreviewMore", "... and %d more", "... und %d weitere", "... 以及另外 %d 个", "... ほか %d 件"),
    ("MsgRevertedN", "Reverted %d command(s) to the original", "%d Befehl(e) auf das Original zurueckgesetzt",
     "已将 %d 条命令恢复为原始", "%d 個のコマンドを元に戻しました"),
    ("StatusLastCompile", "Compiled: %s", "Kompiliert: %s",
     "已编译: %s", "コンパイル: %s"),
    ("StatusLastCompileHint", "When the .ibi of this script was last written",
     "Wann die .ibi dieses Skripts zuletzt geschrieben wurde",
     "此脚本的 .ibi 最后写入的时间", "このスクリプトの .ibi が最後に書き込まれた日時"),
    # Meldungen des Uebersetzers (Diag::code), wie die Pruefcodes V001...
    ("CmsgC001", "expression cannot be compiled: %s", "Ausdruck nicht uebersetzbar: %s",
     "表达式无法编译: %s", "式をコンパイルできません: %s"),
    ("CmsgC002", "expression only partly compiled: %s", "Ausdruck nicht vollstaendig uebersetzt: %s",
     "表达式仅部分编译: %s", "式の一部のみコンパイルされました: %s"),
    ("CmsgC003", "no .ibi equivalent for %s", "kein .ibi-Gegenstueck fuer %s",
     "%s 没有 .ibi 对应项", "%s に対応する .ibi がありません"),
    ("CmsgC004", "ICARUS has no \"%s\" comparison - use = < > or ! (! means \"not equal\")",
     "ICARUS kennt keinen Vergleich \"%s\" - erlaubt sind = < > und ! (! heisst \"ungleich\")",
     "ICARUS 没有 \"%s\" 比较 - 请使用 = < > 或 !（! 表示“不等于”）",
     "ICARUS に \"%s\" の比較はありません - = < > または ! を使用（! は「等しくない」）"),
    ("CmsgC005", "line not understood, not compiled: %s - fix or delete it",
     "Zeile nicht verstanden, nicht uebersetzt: %s - korrigieren oder loeschen",
     "无法识别的行，未编译: %s - 请修正或删除", "解釈できない行（コンパイルされません）: %s - 修正または削除してください"),
    ("CmsgC006", "the block of %s has no closing } in the file - everything after it was read into the block. Check where it should end; saving writes the }",
     "der Block von %s hat in der Datei keine schliessende } - alles danach wurde in den Block gelesen. Pruefen, wo er enden soll; Speichern schreibt die }",
     "%s 的块在文件中没有结尾的 } - 其后的所有内容都被读入了该块。请检查它应在何处结束；保存时会写入 }",
     "%s のブロックにはファイル内で閉じ括弧 } がありません - 以降はすべてブロック内として読み込まれました。終わる位置を確認してください。保存すると } が書き込まれます"),
    # Meldungen des Lesers beim Oeffnen (Diag::code L001...). %s = Datei,
    # %d = Zeile, %s = Meldung.
    ("ReadNoteLine", "%s, line %d: %s", "%s, Zeile %d: %s", "%s，第 %d 行: %s", "%s、%d 行目: %s"),
    ("LmsgL001", "line not understood (no opening parenthesis), kept as text and not compiled: %s",
     "Zeile nicht verstanden (keine oeffnende Klammer), als Text behalten und nicht uebersetzt: %s",
     "无法识别的行（缺少左括号），已作为文本保留且不会编译: %s",
     "解釈できない行（開き括弧がありません）。テキストとして残し、コンパイルしません: %s"),
    ("LmsgL002", "closing parenthesis missing, kept as text and not compiled: %s",
     "schliessende Klammer fehlt, als Text behalten und nicht uebersetzt: %s",
     "缺少右括号，已作为文本保留且不会编译: %s",
     "閉じ括弧がありません。テキストとして残し、コンパイルしません: %s"),
    ("LmsgL003", "%s has no { after it - read without a block",
     "nach %s steht keine { - ohne Block gelesen",
     "%s 后面没有 { - 已按无块读取", "%s の後に { がありません - ブロックなしとして読み込みました"),
    ("LmsgL004", "the block of %s is not closed - everything up to the end of the file was read into it",
     "der Block von %s ist nicht geschlossen - alles bis zum Dateiende wurde hineingelesen",
     "%s 的块没有闭合 - 直到文件末尾的所有内容都被读入其中",
     "%s のブロックが閉じられていません - ファイルの終わりまでがすべてブロック内として読み込まれました"),
    ("LmsgL005", "unexpected }, kept as text: %s", "unerwartete }, als Text behalten: %s",
     "意外的 }，已作为文本保留: %s", "予期しない } です。テキストとして残しました: %s"),
    ("LmsgL006", "unexpected text after the closing parenthesis, ignored: %s",
     "unerwarteter Text hinter der schliessenden Klammer, ignoriert: %s",
     "右括号后有意外文本，已忽略: %s", "閉じ括弧の後に予期しないテキストがあります。無視しました: %s"),
    # Changelog-Fenster
    ("ChangelogMenu", "Changelog...", "Changelog...", "更新日志...", "更新履歴..."),
    ("ChangelogTitle", "Changelog", "Changelog", "更新日志", "更新履歴"),
    ("ChangelogIntro", "What was fixed and added, version by version. Your version: %s",
     "Was behoben und hinzugekommen ist, Fassung fuer Fassung. Deine Fassung: %s",
     "按版本列出的修复和新增内容。你的版本: %s", "バージョンごとの修正と追加内容。お使いのバージョン: %s"),
    ("ChangelogInstalled", "installed", "installiert", "已安装", "インストール済み"),
    ("ChangelogClose", "Close", "Schliessen", "关闭", "閉じる"),
    # --- Zielspiel (Singleplayer / Multiplayer) und MP-Regeln ---
    ("ViewTarget", "Target game", "Zielspiel", "目标游戏", "対象ゲーム"),
    ("TargetSp", "Single player (Movie Duels / JKA)", "Singleplayer (Movie Duels / JKA)",
     "单人游戏（Movie Duels / JKA）", "シングルプレイ（Movie Duels / JKA）"),
    ("TargetMp", "Multiplayer (MP rules)", "Multiplayer (MP-Regeln)", "多人游戏（MP 规则）", "マルチプレイ（MP ルール）"),
    ("TargetMpHint",
     "Notes also show what doesn't work, or works differently, on a multiplayer server (checked against the MP game code).",
     "Die Anmerkungen zeigen zusaetzlich, was auf einem Multiplayer-Server nicht oder anders geht (geprueft am MP-Spielcode).",
     "提示中还会显示在多人服务器上无法使用或行为不同的内容（已对照 MP 游戏代码核实）。",
     "マルチプレイサーバーで動かないもの、動作が異なるものも注記に表示します（MP のゲームコードで確認済み）。"),
    ("StatusMp", "MP", "MP", "MP", "MP"),
    ("StatusMpHint", "Multiplayer rules are on (View > Target game).",
     "Multiplayer-Regeln sind an (Ansicht > Zielspiel).", "多人游戏规则已开启（视图 > 目标游戏）。",
     "マルチプレイのルールが有効です（表示 > 対象ゲーム）。"),
    ("VmsgV012", "%s is not a known field (typing error?)", "%s ist kein bekanntes Feld (Tippfehler?)",
     "%s 不是已知字段（拼写错误？）", "%s は既知のフィールドではありません（入力ミス？）"),
    ("VmsgV012b", "%s is not a known field - did you mean %s?", "%s ist kein bekanntes Feld - meintest du %s?",
     "%s 不是已知字段 - 你是指 %s 吗？", "%s は既知のフィールドではありません - %s のことですか？"),
    ("MmsgM001", "camera does nothing in multiplayer (%s) - stage the scene with movers and ROFFs instead",
     "camera tut im Multiplayer nichts (%s) - die Szene stattdessen mit Movern und ROFFs stellen",
     "camera 在多人游戏中不起作用（%s）- 请改用移动体和 ROFF 来布置场景",
     "camera はマルチプレイでは何もしません（%s）- 代わりにムーバーと ROFF で演出してください"),
    ("MmsgM002", "%s is unknown in multiplayer - the server prints \"variable or field not found\" and does nothing",
     "%s kennt der Multiplayer nicht - der Server meldet \"variable or field not found\" und tut nichts",
     "多人游戏不认识 %s - 服务器会显示 \"variable or field not found\" 且不执行任何操作",
     "%s はマルチプレイでは不明です - サーバーは \"variable or field not found\" と表示し、何もしません"),
    ("MmsgM003", "%s does nothing in multiplayer (it only warns with developer 2)",
     "%s tut im Multiplayer nichts (warnt nur mit developer 2)",
     "%s 在多人游戏中不起作用（仅在 developer 2 时警告）",
     "%s はマルチプレイでは何もしません（developer 2 のときだけ警告）"),
    ("MmsgM004", "%s is accepted in multiplayer but does nothing", "%s wird im Multiplayer angenommen, tut aber nichts",
     "%s 在多人游戏中会被接受，但不起作用", "%s はマルチプレイで受け付けられますが、何もしません"),
    ("MmsgM005", "%s only works on NPCs in multiplayer", "%s wirkt im Multiplayer nur auf NPCs",
     "%s 在多人游戏中只对 NPC 有效", "%s はマルチプレイでは NPC にしか効きません"),
    ("MmsgM006", "%s only works on players and NPCs in multiplayer", "%s wirkt im Multiplayer nur auf Spieler und NPCs",
     "%s 在多人游戏中只对玩家和 NPC 有效", "%s はマルチプレイではプレイヤーと NPC にしか効きません"),
    ("MmsgM007", "print shows nothing in multiplayer unless the text starts with ! (centerprint to everyone): \"!%s\"",
     "print zeigt im Multiplayer nichts, ausser der Text beginnt mit ! (Mitteltext fuer alle): \"!%s\"",
     "在多人游戏中，print 只有在文本以 ! 开头时才会显示（向所有人居中显示）: \"!%s\"",
     "マルチプレイでは、テキストが ! で始まらないと print は何も表示しません（全員に中央表示）: \"!%s\""),
    ("MmsgM008", "%s has no effect on players or NPCs in multiplayer (it works on other entities)",
     "%s wirkt im Multiplayer nicht auf Spieler oder NPCs (auf anderen Entities schon)",
     "%s 在多人游戏中对玩家和 NPC 无效（对其他实体有效）",
     "%s はマルチプレイではプレイヤーと NPC に効きません（他のエンティティには効きます）"),
    ("MmsgM009", "%s crashes the multiplayer server unless the script runs on a player or NPC",
     "%s bringt den Multiplayer-Server zum Absturz, wenn das Skript nicht auf einem Spieler oder NPC laeuft",
     "如果脚本不是在玩家或 NPC 上运行，%s 会使多人服务器崩溃",
     "スクリプトがプレイヤーまたは NPC 上で動いていない場合、%s はマルチプレイサーバーをクラッシュさせます"),
    ("MmsgM010", "%s is reversed in multiplayer: \"true\" turns the saber off, \"false\" turns it on",
     "%s ist im Multiplayer umgekehrt: \"true\" schaltet das Schwert aus, \"false\" an",
     "%s 在多人游戏中是反的：\"true\" 关闭光剑，\"false\" 打开光剑",
     "%s はマルチプレイでは逆です：\"true\" でセーバーがオフ、\"false\" でオンになります"),
    ("MmsgM011", "%s sets the force power %s in multiplayer (engine bug), not the saber value",
     "%s setzt im Multiplayer die Macht %s (Fehler im Spiel), nicht den Schwertwert",
     "%s 在多人游戏中设置的是原力 %s（引擎错误），而不是光剑数值",
     "%s はマルチプレイではセーバーの値ではなくフォース %s を設定します（エンジンのバグ）"),
    ("MmsgM012", "%s changes the speed of the whole server", "%s aendert das Tempo des ganzen Servers",
     "%s 会改变整个服务器的速度", "%s はサーバー全体の速度を変えます"),
    ("MmsgM013", "%s only sticks on NPCs in multiplayer - players are reset every frame",
     "%s haelt im Multiplayer nur bei NPCs - Spieler werden jedes Bild zurueckgesetzt",
     "%s 在多人游戏中只对 NPC 持续有效 - 玩家每帧都会被重置",
     "%s はマルチプレイでは NPC にしか残りません - プレイヤーは毎フレームリセットされます"),
    ("MmsgM014", "%s variables declared - multiplayer holds at most 32 at a time",
     "%s Variablen deklariert - der Multiplayer haelt hoechstens 32 gleichzeitig",
     "声明了 %s 个变量 - 多人游戏同时最多只能保存 32 个",
     "%s 個の変数を宣言しています - マルチプレイで同時に保持できるのは最大 32 個です"),
    ("MmsgM015", "$get( %s, %s ) never returns a value in multiplayer - an if always takes else, other commands are skipped",
     "$get( %s, %s ) liefert im Multiplayer nie einen Wert - ein if nimmt immer else, andere Befehle fallen aus",
     "$get( %s, %s ) 在多人游戏中永远不会返回值 - if 总是走 else 分支，其他命令会被跳过",
     "$get( %s, %s ) はマルチプレイでは値を返しません - if は常に else に進み、他のコマンドはスキップされます"),
    ("MmsgM016", "%s only works on OpenJK-based servers - in the original multiplayer game it does nothing",
     "%s wirkt nur auf OpenJK-basierten Servern - im originalen Multiplayer tut es nichts",
     "%s 只在基于 OpenJK 的服务器上有效 - 在原版多人游戏中不起作用",
     "%s は OpenJK ベースのサーバーでのみ動作します - オリジナルのマルチプレイでは何もしません"),
    ("MmsgM017", "dowait ( \"%s\" ) never returns in multiplayer: the task contains %s, which multiplayer never reports as finished",
     "dowait ( \"%s\" ) kehrt im Multiplayer nie zurueck: der Task enthaelt %s, das der Multiplayer nie als fertig meldet",
     "dowait ( \"%s\" ) 在多人游戏中永远不会返回：该任务包含 %s，而多人游戏从不报告它已完成",
     "dowait ( \"%s\" ) はマルチプレイでは戻りません：タスクに %s が含まれており、マルチプレイはその完了を報告しません"),
    ("StatusClickHint", "Click to see the messages",
     "Zum Nachlesen anklicken", "点击查看消息", "クリックしてメッセージを表示"),
    ("MessagesTitle", "Messages", "Meldungen", "消息", "メッセージ"),
    ("MessagesNone", "Nothing to report.", "Nichts zu melden.", "无消息。",
     "報告はありません。"),
    ("MapEffectsHint",
     "Particle effects from the map's fx_runner entities. Opaque quads - "
     "the software renderer cannot blend additively, so fire and smoke show "
     "as coloured shapes, not as glow.",
     "Partikeleffekte aus den fx_runner-Entities der Karte. Deckende "
     "Vierecke - der Softwarezeichner kann nicht additiv mischen, also "
     "erscheinen Feuer und Rauch als farbige Flächen, nicht als Leuchten.",
     "来自地图 fx_runner 实体的粒子特效。不透明四边形——软件渲染器无法叠加混合。",
     "マップの fx_runner エンティティによるパーティクル。不透明な四角形です"
     "——ソフトウェア描画は加算合成ができません。"),
    # Tastenbelegung im Einstellungsfenster.
    ("KeysTitle", "Keyboard shortcuts", "Tastenkürzel", "键盘快捷键",
     "キーボードショートカット"),
    ("KeysHint", "Click a shortcut, then press the new key. Esc cancels, "
                 "Backspace clears.",
     "Auf ein Kürzel klicken, dann die neue Taste drücken. Esc bricht ab, "
     "Rücktaste löscht.",
     "点击快捷键，然后按下新按键。Esc 取消，退格键清除。",
     "ショートカットをクリックし、新しいキーを押します。Esc で中止、"
     "Backspace で解除。"),
    ("KeysPress", "press a key...", "Taste drücken ...", "请按键…",
     "キーを押してください…"),
    ("KeysNone", "(none)", "(keine)", "（无）", "（なし）"),
    ("KeysAction", "Action", "Aktion", "操作", "操作"),
    ("KeysShortcut", "Shortcut", "Kürzel", "快捷键", "ショートカット"),
    ("KeysReset", "Reset all to defaults", "Alle auf Vorgabe zurücksetzen",
     "全部恢复默认", "すべて既定に戻す"),
    ("KeysClash", "already used by %s", "schon von %s belegt", "已被 %s 占用",
     "%s が使用中"),
    # Die Aktionsnamen. Bewusst nah am Original-Knopfstreifen.
    ("ActUnrem", "Uncomment", "Kommentar aufheben", "取消注释", "コメント解除"),
    ("ActFindNext", "Find next", "Weitersuchen", "查找下一个", "次を検索"),
    ("ActFindPrev", "Find previous", "Rückwärts suchen", "查找上一个", "前を検索"),
    ("ActMoveUp", "Move up", "Nach oben", "上移", "上へ"),
    ("ActMoveDown", "Move down", "Nach unten", "下移", "下へ"),
    ("ActUndo", "Undo", "Rückgängig", "撤销", "元に戻す"),
    ("ActRedo", "Redo", "Wiederherstellen", "やり直し", "やり直し"),
    ("ActEdit2", "Edit item", "Eintrag bearbeiten", "编辑条目", "項目を編集"),
    ("ActInsert", "Insert item", "Eintrag einfügen", "插入条目", "項目を挿入"),
    ("ActOpen", "Open", "Öffnen", "打开", "開く"),
    ("ActSave", "Save", "Speichern", "保存", "保存"),
    ("ActSaveAs", "Save As", "Speichern unter", "另存为", "名前を付けて保存"),
    ("ActBackup", "Backup", "Sicherung", "备份", "バックアップ"),
    ("ActRestore", "Restore", "Wiederherstellen aus Sicherung",
     "从备份恢复", "バックアップから復元"),
    # Wortgleich zum Original: die Statuszeile meldet dort "( Exported )".
    ("ExportDone", "( Exported )", "( Exportiert )", "（已导出）", "（書き出し済み）"),

    ("BackupDone", "Backup written: %s", "Sicherung geschrieben: %s",
     "已写入备份：%s", "バックアップを書き出しました: %s"),
    # Wortlaut des Originals
    ("BackupNoFile", "You can't backup until you've saved at least once",
     "Sichern geht erst, wenn das Skript einmal gespeichert wurde",
     "至少保存一次后才能备份", "一度保存するまでバックアップできません"),
    ("RestoreNone", "No Backup file available to restore from",
     "Keine Sicherung zum Wiederherstellen vorhanden",
     "没有可用于还原的备份文件", "復元できるバックアップファイルがありません"),
    ("RestoreDone", "Restored from backup", "Aus der Sicherung geholt",
     "已从备份还原", "バックアップから復元しました"),
    ("RestoreAsk", "This RESTORE will overwrite your current script, proceed?",
     "Das Wiederherstellen überschreibt das aktuelle Skript. Fortfahren?",
     "还原将覆盖当前脚本，继续吗？",
     "復元すると現在のスクリプトが上書きされます。続行しますか？"),
    ("TreeExpandAll", "+", "+", "+", "+"),
    ("TreeCollapseAll", "-", "-", "-", "-"),

    ("ViewEvents", "Events", "Ereignisse", "事件", "イベント"),
    ("ViewMap", "Map", "Karte", "地图", "マップ"),
    ("MapInsertCamera", "Insert camera here", "Kamera hier einfügen",
     "在此插入摄像机", "ここにカメラを挿入"),
    ("MapFly",
     "Middle mouse: pan | Alt+middle: orbit | Wheel: zoom | "
     "Right mouse + WASD: fly | Shift: faster",
     "Mittlere Maustaste: schieben | Alt+Mitte: umkreisen | Rad: zoomen | "
     "Rechte Maustaste + WASD: fliegen | Umschalt: schneller",
     "中键：平移 | Alt+中键：环绕 | 滚轮：缩放 | 右键+WASD：飞行 | Shift：加速",
     "中ボタン: 平行移動 | Alt+中: 旋回 | ホイール: 拡大縮小 | "
     "右ボタン+WASD: 移動 | Shift: 加速"),
    ("ModeEventsHint", "The command list, for building scripts",
     "Die Befehlsliste, zum Bauen von Skripten", "命令列表，用于构建脚本",
     "スクリプトを組み立てるためのコマンド一覧"),
    ("ModeMapHint", "The map in 3D, for placing cameras",
     "Die Karte in 3D, zum Setzen von Kameras", "3D 地图，用于放置摄像机",
     "カメラを配置するための 3D マップ"),
    ("MapPos", "%d %d %d", "%d %d %d", "%d %d %d", "%d %d %d"),
    ("MapTexFound", "%d of %d textures found", "%d von %d Texturen gefunden",
     "找到 %d / %d 个纹理", "%d / %d のテクスチャが見つかりました"),
    ("MapTexNone", "No textures - add a game folder first",
     "Keine Texturen - erst einen Spielordner hinzufügen",
     "无纹理 — 请先添加游戏目录", "テクスチャなし — 先にゲームフォルダーを追加"),
    ("MapThroughCam", "Through camera", "Durch die Kamera", "通过摄像机",
     "カメラ視点"),
    ("MapThroughCamHint",
     "Show what the selected camera command sees, with its zoom and the "
     "cinematic bars",
     "Zeigen, was der gewählte camera-Befehl sieht - mit seinem Zoom und den "
     "Kinoblenden",
     "显示所选摄像机命令的视野，包含其缩放和电影黑边",
     "選択した camera コマンドの視野を、ズームとレターボックス付きで表示"),
    ("MapCamNone", "Select a camera command in the script",
     "Einen camera-Befehl im Skript auswählen", "请在脚本中选择摄像机命令",
     "スクリプトで camera コマンドを選択してください"),
    ("MapCamInfo", "%s  |  zoom %.0f°", "%s  |  Zoom %.0f°", "%s ｜ 缩放 %.0f°",
     "%s ｜ ズーム %.0f°"),
    ("TlPlay", "Play", "Abspielen", "播放", "再生"),
    ("TlStop", "Stop", "Anhalten", "停止", "停止"),
    ("TlRewind", "To start", "An den Anfang", "回到开头", "先頭へ"),
    ("TlTime", "%.2f s of %.2f s", "%.2f s von %.2f s", "%.2f 秒 / %.2f 秒",
     "%.2f 秒 / %.2f 秒"),
    ("TlNone", "No camera commands in the script",
     "Keine camera-Befehle im Skript", "脚本中没有摄像机命令",
     "スクリプトに camera コマンドがありません"),
    ("TlAudio", "Sound", "Klang", "声音", "サウンド"),
    ("TlAudioHint",
     "Play the sound files while the timeline runs - that is what you match "
     "the cuts against",
     "Die Klangdateien beim Ablaufen mitspielen - danach richtet man die "
     "Schnitte aus",
     "时间轴运行时播放声音文件 — 剪辑就是照着它对的",
     "タイムライン再生中に音声も鳴らします。カット合わせの基準です"),
    ("TlScript", "Script", "Skript", "脚本", "スクリプト"),
    ("TlTracks", "Character rows", "Figurenzeilen", "角色行", "キャラクター行"),
    ("TlTracksHint",
     "One row per affect target - shows when each character speaks and acts",
     "Eine Zeile je affect-Ziel - zeigt, wann welche Figur spricht und handelt",
     "每个 affect 目标一行 — 显示各角色何时说话和行动",
     "affect の対象ごとに 1 行。各キャラクターが話し動くタイミングを表示"),
    ("HintDelete", "Remove the selected commands",
     "Die gewählten Befehle entfernen", "删除所选命令", "選択したコマンドを削除"),
    ("HintClone", "Insert a copy right below",
     "Eine Kopie direkt darunter einfügen", "在下方插入副本",
     "すぐ下にコピーを挿入"),
    ("HintCopy", "Copy to the clipboard", "In die Zwischenablage kopieren",
     "复制到剪贴板", "クリップボードにコピー"),
    ("HintCut", "Copy and remove", "Kopieren und entfernen", "复制并删除",
     "コピーして削除"),
    ("HintPaste", "Insert what was copied, below the selection",
     "Das Kopierte unter der Auswahl einfügen", "在所选项下方粘贴",
     "選択の下に貼り付け"),
    ("HintRem", "Turn the line into a comment, or back",
     "Die Zeile zu einem Kommentar machen - oder zurück",
     "将该行变为注释，或还原", "行をコメントに、または元に戻す"),
    ("HintFind", "Search the script", "Im Skript suchen", "搜索脚本",
     "スクリプトを検索"),
    ("HintMoveUp", "Move up one line, staying in the same block",
     "Eine Zeile nach oben - im selben Block",
     "上移一行，保持在同一块内", "同じブロック内で 1 行上へ"),
    ("HintMoveDown", "Move down one line, staying in the same block",
     "Eine Zeile nach unten - im selben Block",
     "下移一行，保持在同一块内", "同じブロック内で 1 行下へ"),
    ("HintUndo", "Undo the last change", "Die letzte Änderung zurücknehmen",
     "撤销上一次更改", "直前の変更を取り消す"),
    ("HintRedo", "Redo what was undone", "Das Zurückgenommene wiederholen",
     "重做已撤销的操作", "取り消した操作をやり直す"),
    ("HintNew", "Start an empty script", "Ein leeres Skript beginnen",
     "新建空脚本", "空のスクリプトを開始"),
    ("HintOpen", "Open a .txt or .ibi", "Eine .txt oder .ibi öffnen",
     "打开 .txt 或 .ibi", ".txt または .ibi を開く"),
    ("HintMru", "Recently opened files", "Zuletzt geöffnete Dateien",
     "最近打开的文件", "最近開いたファイル"),
    ("HintAppend", "Add another script to the end of this one",
     "Ein weiteres Skript hinten anhängen", "将另一个脚本追加到末尾",
     "別のスクリプトを末尾に追加"),
    ("HintSave", "Save to the same file", "In dieselbe Datei speichern",
     "保存到同一文件", "同じファイルに保存"),
    ("HintSaveAs", "Save under a new name", "Unter neuem Namen speichern",
     "另存为", "名前を付けて保存"),
    ("HintExport", "Write the script as plain text",
     "Das Skript als reinen Text schreiben", "导出为纯文本",
     "プレーンテキストとして書き出す"),
    ("HintBackup", "Keep a copy of the current state",
     "Eine Kopie des jetzigen Standes ablegen", "备份当前状态",
     "現在の状態をバックアップ"),
    ("HintRestore", "Go back to the last backup",
     "Zur letzten Sicherung zurückgehen", "恢复到上次备份",
     "最後のバックアップに戻す"),
    ("HintCompile", "Turn the script into an .ibi the game can run",
     "Das Skript in eine .ibi übersetzen, die das Spiel ausführt",
     "将脚本编译为游戏可运行的 .ibi",
     "ゲームが実行できる .ibi にコンパイル"),
    ("HintPrefs", "Settings: language, theme, game folders",
     "Einstellungen: Sprache, Farben, Spielordner",
     "设置：语言、主题、游戏目录",
     "設定: 言語、テーマ、ゲームフォルダー"),
    ("MsgNoMatch", "no match", "kein Treffer", "无匹配", "該当なし"),
    ("ViewScaleNow", "currently %d %%", "zurzeit %d %%", "当前 %d %%", "現在 %d %%"),
    ("EntityLayers", "Entities", "Entities", "实体", "エンティティ"),
    ("EntityLayersHint",
     "Eye toggles visibility. Alt-click an eye to show only that group.",
     "Das Auge schaltet sichtbar und unsichtbar. Alt-Klick zeigt nur diese "
     "Gruppe.",
     "眼睛图标切换可见性。Alt+点击仅显示该组。",
     "目のアイコンで表示を切り替えます。Alt クリックでそのグループのみ表示。"),
    ("EntityShowAll", "Show all", "Alle zeigen", "全部显示", "すべて表示"),
    ("EntitySearch", "Search name or class...", "Name oder Klasse suchen ...", "搜索名称或类...", "名前またはクラスを検索..."),
    ("EntityRowHint", "Double-click: fly there. Right-click: insert a command for it.", "Doppelklick: hinfliegen. Rechtsklick: Befehl dafür einfügen.", "双击：飞过去。右键：为其插入命令。", "ダブルクリック: 移動。右クリック: コマンドを挿入。"),
    ("EntityKatTags", "Camera marks (ref_tag)", "Kameramarken (ref_tag)", "摄像机标记 (ref_tag)", "カメラマーク (ref_tag)"),
    ("EntityKatNav", "Walk targets (navgoal)", "Laufziele (navgoal)", "行走目标 (navgoal)", "移動先 (navgoal)"),
    ("EntityKatLook", "Look targets", "Blickziele", "注视目标", "注視先"),
    ("EntityKatNpc", "Characters (NPC_*)", "Figuren (NPC_*)", "角色 (NPC_*)", "キャラクター (NPC_*)"),
    ("EntityKatRunner", "Script runners", "Skriptstarter", "脚本启动器", "スクリプト起動"),
    ("EntityKatFx", "Effects (fx_runner)", "Effekte (fx_runner)", "特效 (fx_runner)", "エフェクト (fx_runner)"),
    ("EntityKatSound", "Sound and music", "Klang und Musik", "声音和音乐", "サウンドと音楽"),
    ("EntityKatMover", "Movable (func_*)", "Bewegliches (func_*)", "可移动 (func_*)", "可動 (func_*)"),
    ("EntityKatBreak", "Breakables", "Zerbrechliches", "可破坏物", "破壊可能"),
    ("EntityFlyTo", "Fly there", "Hinfliegen", "飞过去", "そこへ移動"),
    ("EntityCopyName", "Copy name", "Namen kopieren", "复制名称", "名前をコピー"),
    ("EntityCamToTag", "Camera to this mark (MOVE + PAN)", "Kamera auf diese Marke (MOVE + PAN)", "摄像机到此标记 (MOVE + PAN)", "カメラをこのマークへ (MOVE + PAN)"),
    ("EntityAsNavgoal", "Insert as walk target (SET_NAVGOAL)", "Als Laufziel einfügen (SET_NAVGOAL)", "作为行走目标插入 (SET_NAVGOAL)", "移動先として挿入 (SET_NAVGOAL)"),
    ("EntityAsLook", "Insert as look target (SET_LOOK_TARGET)", "Als Blickziel einfügen (SET_LOOK_TARGET)", "作为注视目标插入 (SET_LOOK_TARGET)", "注視先として挿入 (SET_LOOK_TARGET)"),
    ("EntityAsWatch", "Turn towards it (SET_WATCHTARGET)", "Dorthin drehen (SET_WATCHTARGET)", "转向它 (SET_WATCHTARGET)", "そちらを向く (SET_WATCHTARGET)"),
    ("EntityAffect", "Insert affect block", "affect-Block einfügen", "插入 affect 块", "affect ブロックを挿入"),
    ("EntityUse", "Insert use", "use einfügen", "插入 use", "use を挿入"),
    ("EntityCount", "%d of %d", "%d von %d", "%d / %d", "%d / %d"),
    # --- Missionsbau (gui/app_mission.cpp, 11.10.2026) ---
    ("MissionNew", "New mission...", "Neue Mission...", "新建任务...", "新規ミッション..."),
    ("MissionNewHint", "Start a mission on any map: a player start, a script runner, the intro script and a mission folder are created for you.",
     "Eine Mission auf einer beliebigen Karte anfangen: Spielerstart, Scriptrunner, Intro-Skript und ein Missionsordner werden angelegt.",
     "在任意地图上开始一个任务:自动创建玩家出生点、脚本运行器、开场脚本和任务文件夹。",
     "任意のマップでミッションを開始します。プレイヤー開始位置、スクリプトランナー、イントロスクリプト、ミッションフォルダーが作成されます。"),
    ("MissionSave", "Save mission...", "Mission speichern...", "保存任务...", "ミッションを保存..."),
    ("MissionSaveHint", "Save all scripts, write the .ent, compile the scripts and pack the .pk3 for the game.",
     "Alle Skripte speichern, die .ent schreiben, die Skripte übersetzen und die .pk3 für das Spiel packen.",
     "保存所有脚本,写入 .ent,编译脚本并为游戏打包 .pk3。",
     "すべてのスクリプトを保存し、.ent を書き出し、スクリプトをコンパイルしてゲーム用の .pk3 をまとめます。"),
    ("MissionNewTitle", "New mission", "Neue Mission", "新建任务", "新規ミッション"),
    ("MissionMap", "Map", "Karte", "地图", "マップ"),
    ("MissionMapSearch", "Search maps...", "Karten suchen...", "搜索地图...", "マップを検索..."),
    ("MissionMapCurrent", "Current map (%s)", "Aktuelle Karte (%s)", "当前地图 (%s)", "現在のマップ (%s)"),
    ("MissionName", "Mission name", "Missionsname", "任务名称", "ミッション名"),
    ("MissionNameHint", "Letters, digits and _ only. The game loads it as maps/<name>.bsp (devmap <name>).",
     "Nur Buchstaben, Ziffern und _. Das Spiel lädt sie als maps/<name>.bsp (devmap <name>).",
     "只能使用字母、数字和 _。游戏以 maps/<名称>.bsp 加载 (devmap <名称>)。",
     "英数字と _ のみ。ゲームは maps/<名前>.bsp として読み込みます (devmap <名前>)。"),
    ("MissionFolder", "Script folder", "Skriptordner", "脚本文件夹", "スクリプトフォルダー"),
    ("MissionProject", "Mission folder: %s", "Missionsordner: %s", "任务文件夹: %s", "ミッションフォルダー: %s"),
    ("MissionCreate", "Create", "Anlegen", "创建", "作成"),
    ("MissionCancel", "Cancel", "Abbrechen", "取消", "キャンセル"),
    ("MissionExists", "A mission folder with this name already exists:\n%s\nChoose another name.",
     "Einen Missionsordner mit diesem Namen gibt es schon:\n%s\nBitte einen anderen Namen wählen.",
     "已存在同名的任务文件夹:\n%s\n请换一个名称。",
     "この名前のミッションフォルダーは既にあります:\n%s\n別の名前を選んでください。"),
    ("MissionBadName", "Use only letters, digits and _ (at most 48).", "Nur Buchstaben, Ziffern und _ (höchstens 48).",
     "只能使用字母、数字和 _ (最多 48 个)。", "英数字と _ のみ (最大 48 文字)。"),
    ("MapViewSettings", "View settings", "Ansicht einstellen", "视图设置", "表示設定"),
    ("MapViewSettingsHint", "Brightness, ambient light, detail, shadows and how characters face.",
     "Helligkeit, Umgebungslicht, Detail, Schatten und die Ausrichtung der Figuren.",
     "亮度、环境光、细节、阴影以及角色朝向。", "明るさ、環境光、詳細度、影、キャラクターの向き。"),
    ("MapStats", "Statistics", "Statistik", "统计", "統計"),
    ("MapStatsHint", "Frame rate, textures, triangles and graphics card values - for finding problems.",
     "Bildrate, Texturen, Dreiecke und Werte der Grafikkarte - für die Fehlersuche.",
     "帧率、纹理、三角形和显卡数据 - 用于排查问题。", "フレームレート、テクスチャ、三角形、グラフィック関連の値 - 問題の調査用。"),
    ("MapMore", "Maps & files", "Karten & Dateien", "地图和文件", "マップとファイル"),
    ("MapMoreHint", "Open a map from disk or from a .pk3, or load an .ent file.",
     "Eine Karte von der Platte oder aus einer .pk3 öffnen, oder eine .ent-Datei laden.",
     "从磁盘或 .pk3 打开地图,或加载 .ent 文件。", "ディスクや .pk3 からマップを開く、または .ent ファイルを読み込みます。"),
    ("MissionUnsavedHint", "There are unsaved changes - Save mission... writes them.",
     "Es gibt ungespeicherte Änderungen - Save mission... schreibt sie.",
     "有未保存的更改 - Save mission... 会写入它们。", "未保存の変更があります - Save mission... で保存します。"),
    ("MissionNoneShort", "No mission", "Keine Mission", "没有任务", "ミッションなし"),
    ("EntSecGeneral", "General", "Allgemein", "常规", "一般"),
    ("EntSecFields", "Fields", "Felder", "字段", "フィールド"),
    ("EntSearchFields", "Filter fields...", "Felder filtern...", "筛选字段...", "フィールドを絞り込む..."),
    ("EntFloor", "Floor", "Boden", "地面", "床"),
    ("EntGizmoAxis", "Drag to move along this axis (Ctrl: 8-unit grid).",
     "Ziehen verschiebt entlang dieser Achse (Strg: 8er-Raster).",
     "拖动可沿此轴移动 (Ctrl: 8 单位网格)。", "ドラッグでこの軸に沿って移動 (Ctrl: 8 単位グリッド)。"),
    ("EntGizmoRing", "Drag to turn the facing (Ctrl: 15 degree steps).",
     "Ziehen dreht die Blickrichtung (Strg: 15-Grad-Schritte).",
     "拖动可旋转朝向 (Ctrl: 每步 15 度)。", "ドラッグで向きを回転 (Ctrl: 15 度刻み)。"),
    ("StartTitle", "Build a mission", "Eine Mission bauen", "制作任务", "ミッションを作る"),
    ("StartSub", "Pick how you want to begin. Everything you need is in this tab: entities, scripts, the preview and Save mission.",
     "Wähle, wie du anfangen willst. Alles Nötige steht in diesem Tab: Entities, Skripte, die Vorschau und Save mission.",
     "选择开始方式。所需的一切都在此标签页中:实体、脚本、预览和 Save mission。",
     "始め方を選んでください。必要なものはすべてこのタブにあります: エンティティ、スクリプト、プレビュー、Save mission。"),
    ("StartCardNew", "Create a new mission", "Neue Mission erstellen", "创建新任务", "新しいミッションを作成"),
    ("StartCardNewSub", "Any map and a name - player start, intro and mission folder are made for you.",
     "Eine Karte und ein Name - Spielerstart, Intro und Missionsordner entstehen von selbst.",
     "任选地图并命名 - 自动创建玩家出生点、开场和任务文件夹。",
     "マップと名前を選ぶだけ - プレイヤー開始位置、イントロ、ミッションフォルダーが作られます。"),
    ("StartCardLoad", "Open a mission (.pk3)", "Mission öffnen (.pk3)", "打开任务 (.pk3)", "ミッションを開く (.pk3)"),
    ("StartCardLoadSub", "Change an existing Movie Duels mission or one you packed.",
     "Eine vorhandene Movie-Duels-Mission oder eine eigene gepackte ändern.",
     "修改现有的 Movie Duels 任务或你打包的任务。", "既存の Movie Duels ミッションや自分でまとめたものを編集します。"),
    ("StartCardMap", "Just open a map", "Nur eine Karte öffnen", "只打开地图", "マップだけ開く"),
    ("StartCardMapSub", "Look around and set up cameras - without a mission.",
     "Umsehen und Kameras einrichten - ohne Mission.", "四处查看并设置摄像机 - 不需要任务。", "見回してカメラを設定 - ミッションなしで。"),
    ("StartRecent", "Recent missions", "Zuletzt bearbeitet", "最近的任务", "最近のミッション"),
    ("StartNoRecent", "No missions yet - the missions you create appear here.",
     "Noch keine Missionen - deine Missionen erscheinen hier.", "还没有任务 - 你创建的任务会显示在这里。",
     "まだミッションがありません - 作成したミッションはここに表示されます。"),
    ("MissionOpenFolder", "Open mission folder...", "Missionsordner öffnen...", "打开任务文件夹...", "ミッションフォルダーを開く..."),
    ("MissionOpenFolderHint", "Continue a mission from its folder - also one that was never packed into a .pk3.",
     "Eine Mission aus ihrem Ordner weiterbearbeiten - auch eine, die noch nie gepackt wurde.",
     "从任务文件夹继续编辑 - 包括从未打包成 .pk3 的任务。",
     "フォルダーからミッションの作業を続けます - まだ .pk3 にまとめていないものも。"),
    ("MissionOpened", "Mission %s opened.", "Mission %s geöffnet.", "已打开任务 %s。", "ミッション %s を開きました。"),
    ("ToastExplorer", "Show in Explorer", "Im Explorer zeigen", "在资源管理器中显示", "エクスプローラーで表示"),
    ("ToastClose", "Close", "Schließen", "关闭", "閉じる"),
    ("GraphCount", "%d nodes, %d links", "%d Knoten, %d Verbindungen", "%d 个节点, %d 条连接", "ノード %d, つながり %d"),
    ("GraphCountCut", "%d nodes, %d links (shortened)", "%d Knoten, %d Verbindungen (gekürzt)", "%d 个节点, %d 条连接 (已截断)",
     "ノード %d, つながり %d (省略あり)"),
    ("GraphLinkIn", "<- %s  %s", "<- %s  %s", "<- %s  %s", "<- %s  %s"),
    ("GraphLinkOut", "-> %s  %s", "-> %s  %s", "-> %s  %s", "-> %s  %s"),
    ("TracksOf", "/ %s", "/ %s", "/ %s", "/ %s"),
    ("TracksBlockTip", "%s\n%s - %s", "%s\n%s - %s", "%s\n%s - %s", "%s\n%s - %s"),
    ("FlowClassic", "Classic", "Classic", "经典", "クラシック"),
    ("FlowGraph", "Graph", "Graph", "图表", "グラフ"),
    ("FlowTracks", "Tracks", "Spuren", "轨道", "トラック"),
    ("FlowViewHint", "Three views of the same script and mission - switch any time, nothing is lost: Classic (the list), Graph (what triggers what), Tracks (the script over time).",
     "Drei Ansichten desselben Skripts und derselben Mission - jederzeit umschaltbar, nichts geht verloren: Classic (die Liste), Graph (was löst was aus), Spuren (das Skript über die Zeit).",
     "同一脚本和任务的三种视图 - 随时切换,不会丢失任何内容:经典(列表)、图表(谁触发谁)、轨道(随时间的脚本)。",
     "同じスクリプトとミッションの 3 つの表示 - いつでも切り替え可能、何も失われません: クラシック(リスト)、グラフ(何が何を起動するか)、トラック(時間軸のスクリプト)。"),
    ("FlowFollowsHint", "In a split view, Graph and Tracks follow the script you are working on.",
     "In der geteilten Ansicht folgen Graph und Spuren dem Skript, an dem du gerade arbeitest.",
     "在分屏视图中,图表和轨道会跟随你正在编辑的脚本。",
     "分割表示では、グラフとトラックは作業中のスクリプトに追従します。"),
    ("GraphFit", "Fit", "Einpassen", "适应", "全体表示"),
    ("GraphRelayout", "Arrange", "Anordnen", "排列", "整列"),
    ("GraphHint", "Double-click opens a script or shows an entity in the map - drag from the dot on the right of a node to another node to connect them - wheel zooms, middle or right mouse moves the view.",
     "Doppelklick öffnet ein Skript oder zeigt eine Entity in der Karte - vom Punkt rechts an einem Knoten zu einem anderen ziehen verbindet sie - Mausrad zoomt, mittlere oder rechte Maustaste verschiebt.",
     "双击打开脚本或在地图中显示实体 - 从节点右侧的圆点拖到另一个节点即可连接 - 滚轮缩放,鼠标中键或右键平移。",
     "ダブルクリックでスクリプトを開く/エンティティをマップで表示 - ノード右の点から別のノードへドラッグで接続 - ホイールでズーム、中/右ボタンで移動。"),
    ("GraphEmpty", "Load a mission or a map to see how it fits together.", "Eine Mission oder Karte laden, dann sieht man hier, wie alles zusammenhängt.",
     "加载任务或地图即可在此看到各部分如何关联。", "ミッションかマップを読み込むと、ここでつながりが見えます。"),
    ("GraphPickHint", "Click a node to see its details and connections.", "Einen Knoten anklicken zeigt seine Angaben und Verbindungen.",
     "单击节点可查看其详情和连接。", "ノードをクリックすると詳細とつながりを表示します。"),
    ("GraphScript", "Script", "Skript", "脚本", "スクリプト"),
    ("GraphShowInMap", "Show in map", "In der Karte zeigen", "在地图中显示", "マップで表示"),
    ("GraphLinks", "Connections", "Verbindungen", "连接", "つながり"),
    ("GraphConnectAs", "Connect as", "Verbinden als", "连接为", "接続の種類"),
    ("GraphConnected", "%s -> %s connected (%s)", "%s -> %s verbunden (%s)", "%s -> %s 已连接 (%s)", "%s -> %s を接続しました (%s)"),
    ("TracksCamera", "Camera", "Kamera", "摄像机", "カメラ"),
    ("TracksDialog", "Dialogue", "Dialog", "对白", "セリフ"),
    ("TracksScript", "Script", "Skript", "脚本", "スクリプト"),
    ("TracksShot", "Shot %d", "Einstellung %d", "镜头 %d", "ショット %d"),
    ("TracksMoving", "moving", "Fahrt", "移动", "移動"),
    ("TracksEmpty", "This script has nothing on the timeline yet.", "Dieses Skript hat noch nichts auf der Zeitachse.",
     "此脚本在时间轴上还没有内容。", "このスクリプトにはまだタイムライン上の内容がありません。"),
    ("TracksHint", "Click a block to select its line - drag its right edge to change the duration - drag in the ruler to move through time.",
     "Einen Block anklicken wählt seine Zeile - die rechte Kante ziehen ändert die Dauer - im Lineal ziehen bewegt durch die Zeit.",
     "单击块可选中对应行 - 拖动右边缘可修改时长 - 在标尺上拖动可移动时间。",
     "ブロックをクリックで行を選択 - 右端をドラッグで長さを変更 - ルーラーをドラッグで時間を移動。"),
    ("TracksPlay", "Play", "Abspielen", "播放", "再生"),
    ("TracksPause", "Pause", "Pause", "暂停", "一時停止"),
    ("TracksZoom", "Zoom %.1fx", "Zoom %.1fx", "缩放 %.1fx", "ズーム %.1fx"),
    ("MissionNameTaken", "The game already has %s - choose another mission name.",
     "%s gibt es im Spiel schon - bitte einen anderen Missionsnamen wählen.",
     "游戏中已有 %s - 请换一个任务名称。", "ゲームに %s が既にあります - 別のミッション名を選んでください。"),
    ("MissionNoMap", "Choose a map first.", "Bitte zuerst eine Karte wählen.", "请先选择地图。", "先にマップを選んでください。"),
    ("MissionCreated", "Mission %s created - the intro script runs when the player spawns.",
     "Mission %s angelegt - das Intro-Skript läuft, sobald der Spieler erscheint.",
     "已创建任务 %s - 玩家出生时运行开场脚本。",
     "ミッション %s を作成しました - プレイヤーが出現するとイントロスクリプトが動きます。"),
    ("MissionSaveTitle", "Save mission", "Mission speichern", "保存任务", "ミッションを保存"),
    ("MissionTarget", "Game archive (.pk3)", "Spielarchiv (.pk3)", "游戏压缩包 (.pk3)", "ゲームアーカイブ (.pk3)"),
    ("MissionChoose", "Choose...", "Wählen...", "选择...", "選択..."),
    ("MissionFiles", "%d files, %s", "%d Dateien, %s", "%d 个文件, %s", "%d ファイル, %s"),
    ("MissionSaveNow", "Save", "Speichern", "保存", "保存"),
    ("MissionSaved", "Mission saved: %s", "Mission gespeichert: %s", "任务已保存: %s", "ミッションを保存しました: %s"),
    ("MissionSaveFailed", "Could not save the mission: %s", "Mission nicht gespeichert: %s", "无法保存任务: %s",
     "ミッションを保存できませんでした: %s"),
    ("MissionKeine", "No mission yet - use New mission... or Load mission... above.",
     "Noch keine Mission - oben mit New mission... oder Load mission... anfangen.",
     "尚无任务 - 请使用上方的 New mission... 或 Load mission...。",
     "ミッションがありません - 上の New mission... か Load mission... から始めてください。"),
    ("MissionHasErrors", "Fix the errors above first.", "Erst die Fehler oben beheben.", "请先修正上面的错误。",
     "先に上のエラーを直してください。"),
    ("MissionS001", "Script %s is used by an entity but does not exist yet (use Open next to the field).",
     "Skript %s wird von einer Entity benutzt, gibt es aber noch nicht (Open neben dem Feld).",
     "实体使用了脚本 %s,但它还不存在 (使用字段旁的 Open)。",
     "スクリプト %s はエンティティが使っていますが、まだありません (フィールド横の Open)。"),
    ("MissionS002", "Script does not compile - %s", "Skript lässt sich nicht übersetzen - %s", "脚本无法编译 - %s",
     "スクリプトをコンパイルできません - %s"),
    ("MissionS003", "No player start (info_player_start) - the mission cannot start.",
     "Kein Spielerstart (info_player_start) - die Mission kann nicht beginnen.",
     "没有玩家出生点 (info_player_start) - 任务无法开始。",
     "プレイヤー開始位置 (info_player_start) がありません - ミッションを開始できません。"),
    ("MissionS004", "%s had no ENDMARKER - added (without it Movie Duels crashes at start).",
     "%s hatte kein ENDMARKER - ergänzt (ohne stürzt Movie Duels beim Start ab).",
     "%s 没有 ENDMARKER - 已补上 (没有它 Movie Duels 启动时会崩溃)。",
     "%s に ENDMARKER がありませんでした - 追加しました (ないと Movie Duels が起動時に落ちます)。"),
    ("MissionS005", "%s targets something that does not exist.", "%s löst etwas aus, das es nicht gibt.",
     "%s 指向不存在的目标。", "%s は存在しないものをターゲットにしています。"),
    ("MissionS006", "Invalid mission name: %s", "Ungültiger Missionsname: %s", "无效的任务名称: %s", "無効なミッション名: %s"),
    ("MissionUnsavedQuit", "The entities of %s have unsaved changes.\nQuit anyway and discard them?\n(No: back, Save mission... opens.)",
     "Die Entities von %s haben ungespeicherte Änderungen.\nTrotzdem beenden und sie verwerfen?\n(Nein: zurück, Save mission... öffnet sich.)",
     "%s 的实体有未保存的更改。\n仍要退出并放弃这些更改吗?\n(否: 返回并打开 Save mission...。)",
     "%s のエンティティに未保存の変更があります。\n破棄して終了しますか?\n(いいえ: 戻って Save mission... を開きます。)"),
    ("MissionUnsavedReload", "The entities of %s have unsaved changes.\nDiscard them and load the mission again?",
     "Die Entities von %s haben ungespeicherte Änderungen.\nVerwerfen und die Mission neu laden?",
     "%s 的实体有未保存的更改。\n放弃并重新加载任务吗?",
     "%s のエンティティに未保存の変更があります。\n破棄してミッションを読み込み直しますか?"),
    ("EntNew", "+ Entity", "+ Entity", "+ 实体", "+ エンティティ"),
    ("EntNewHint", "Place a new entity under the crosshair. Or right-click a spot in the view: New entity here.",
     "Eine neue Entity unter das Fadenkreuz setzen. Oder kurzer Rechtsklick in die Ansicht: New entity here.",
     "在准星下放置新实体。也可以在视图中右键单击某处: New entity here。",
     "照準の下に新しいエンティティを置きます。ビューを右クリックして New entity here でも置けます。"),
    ("EntNewHere", "New entity here", "Neue Entity hier", "在此新建实体", "ここに新規エンティティ"),
    ("EntAllClasses", "All classes...", "Alle Klassen...", "所有类...", "すべてのクラス..."),
    ("EntKindNpc", "Character (NPC_spawner)", "Figur (NPC_spawner)", "角色 (NPC_spawner)", "キャラクター (NPC_spawner)"),
    ("EntKindNav", "Walk target (waypoint_navgoal)", "Laufziel (waypoint_navgoal)", "行走目标 (waypoint_navgoal)",
     "移動先 (waypoint_navgoal)"),
    ("EntKindTag", "Camera / look mark (ref_tag)", "Kamera-/Blickmarke (ref_tag)", "摄像机/注视标记 (ref_tag)",
     "カメラ/注視マーク (ref_tag)"),
    ("EntKindPos", "Look target (target_position)", "Blickziel (target_position)", "注视目标 (target_position)",
     "注視先 (target_position)"),
    ("EntKindRunner", "Script runner (target_scriptrunner)", "Skriptstarter (target_scriptrunner)",
     "脚本运行器 (target_scriptrunner)", "スクリプトランナー (target_scriptrunner)"),
    ("EntKindFx", "Effect (fx_runner)", "Effekt (fx_runner)", "特效 (fx_runner)", "エフェクト (fx_runner)"),
    ("EntKindSpeaker", "Sound (target_speaker)", "Klang (target_speaker)", "声音 (target_speaker)", "サウンド (target_speaker)"),
    ("EntKindMusic", "Music (target_play_music)", "Musik (target_play_music)", "音乐 (target_play_music)",
     "音楽 (target_play_music)"),
    ("EntKindLevel", "Level change (target_level_change)", "Kartenwechsel (target_level_change)",
     "切换关卡 (target_level_change)", "レベル切り替え (target_level_change)"),
    ("EntKindStart", "Player start (info_player_start)", "Spielerstart (info_player_start)",
     "玩家出生点 (info_player_start)", "プレイヤー開始位置 (info_player_start)"),
    ("EntPickHint", "Click an entity in the list or the view to edit it. New ones: + Entity, or right-click in the view.",
     "Eine Entity in der Liste oder Ansicht anklicken, um sie zu bearbeiten. Neue: + Entity oder Rechtsklick in die Ansicht.",
     "在列表或视图中单击实体即可编辑。新建: + 实体,或在视图中右键单击。",
     "リストかビューでエンティティをクリックすると編集できます。新規: + エンティティ、またはビューを右クリック。"),
    ("EntUnknownClass", "Unknown class - no description in the game code.", "Unbekannte Klasse - keine Beschreibung im Spielcode.",
     "未知类 - 游戏代码中没有说明。", "不明なクラス - ゲームコードに説明がありません。"),
    ("EntBrushClass", "Needs a brush model of the map (*1, *2 ...) - can be edited, not placed new.",
     "Braucht ein Brush-Modell der Karte (*1, *2 ...) - bearbeiten ja, neu setzen nicht.",
     "需要地图中的笔刷模型 (*1, *2 ...) - 可以编辑,不能新建。",
     "マップのブラシモデル (*1, *2 ...) が必要です - 編集はできますが新規配置はできません。"),
    ("EntUnsavedMark", "(changed)", "(geändert)", "(已更改)", "(変更あり)"),
    ("EntName", "Name", "Name", "名称", "名前"),
    ("EntNameHint", "Scripts and other entities address this entity by its name.",
     "Unter diesem Namen sprechen Skripte und andere Entities diese Entity an.",
     "脚本和其他实体通过此名称引用该实体。", "スクリプトや他のエンティティはこの名前でこのエンティティを指します。"),
    ("EntOrigin", "Position", "Position", "位置", "位置"),
    ("EntAngle", "Facing (degrees)", "Blickrichtung (Grad)", "朝向 (度)", "向き (度)"),
    ("EntDropFloor", "Onto floor", "Auf den Boden", "放到地面", "床に置く"),
    ("EntDropFloorHint", "Put it on the floor below - measured in the map, like DROPTOFLOOR in the game.",
     "Auf den Boden darunter stellen - in der Karte gemessen, wie DROPTOFLOOR im Spiel.",
     "放到下方的地面上 - 按地图测量,类似游戏中的 DROPTOFLOOR。",
     "下の床に置きます - マップで測定、ゲームの DROPTOFLOOR と同じ。"),
    ("EntAboveFloor", "Feet %.0f above the floor", "Füße %.0f über dem Boden", "脚离地面 %.0f", "足元は床の %.0f 上"),
    ("EntBelowFloor", "Feet %.0f INSIDE the floor - may be invisible in the game",
     "Füße %.0f IM Boden - im Spiel womöglich unsichtbar", "脚陷入地面 %.0f - 游戏中可能看不见",
     "足元が床に %.0f めり込んでいます - ゲームで見えない可能性"),
    ("EntNoFloor", "No floor below", "Kein Boden darunter", "下方没有地面", "下に床がありません"),
    ("EntFlags", "Spawnflags", "Spawnflags", "生成标志", "スポーンフラグ"),
    ("EntOtherBits", "Other bits set: %d", "Weitere Bits gesetzt: %d", "其他已设置的位: %d", "その他のビット: %d"),
    ("EntRaw", "All keys (raw)", "Alle Schlüssel (roh)", "全部键 (原始)", "すべてのキー (生)"),
    ("EntRawHint", "Show every key as plain text - like turning SmartEdit off in Hammer.",
     "Jeden Schlüssel als reinen Text zeigen - wie SmartEdit aus in Hammer.",
     "将每个键显示为纯文本 - 类似 Hammer 中关闭 SmartEdit。",
     "すべてのキーをテキストで表示 - Hammer の SmartEdit オフと同じ。"),
    ("EntKeyName", "new key", "neuer Schlüssel", "新键", "新しいキー"),
    ("EntRemoveKey", "x", "x", "x", "x"),
    ("EntRemoveKeyHint", "Remove this key", "Diesen Schlüssel entfernen", "删除此键", "このキーを削除"),
    ("EntAddKey", "+ Key", "+ Schlüssel", "+ 键", "+ キー"),
    ("EntOpenScript", "Open", "Öffnen", "打开", "開く"),
    ("EntOpenScriptHint", "Open this script in a tab - it is created in the mission folder if it does not exist yet.",
     "Dieses Skript in einem Reiter öffnen - gibt es es noch nicht, wird es im Missionsordner angelegt.",
     "在标签页中打开此脚本 - 若不存在则在任务文件夹中创建。",
     "このスクリプトをタブで開きます - まだなければミッションフォルダーに作成します。"),
    ("EntPickNpc", "...", "...", "...", "..."),
    ("EntNpcSearch", "Search NPC types...", "NPC-Typen suchen...", "搜索 NPC 类型...", "NPC タイプを検索..."),
    ("EntNpcTypeMissing", "No NPC_type - nothing will spawn.", "Kein NPC_type - es erscheint niemand.",
     "没有 NPC_type - 不会生成任何角色。", "NPC_type がありません - 何も出現しません。"),
    ("EntDuplicate", "Duplicate", "Duplizieren", "复制", "複製"),
    ("EntDelete", "Delete", "Löschen", "删除", "削除"),
    ("EntDragHint", "In the view: drag the entity over the floor, the coloured arrows move it along one axis, the yellow ring turns it.",
     "In der Ansicht: die Entity über den Boden ziehen, die farbigen Pfeile verschieben entlang einer Achse, der gelbe Ring dreht sie.",
     "在视图中:拖动实体沿地面移动,彩色箭头沿单轴移动,黄色圆环旋转。",
     "ビューで: エンティティをドラッグして床の上を移動、色付きの矢印で軸に沿って移動、黄色のリングで回転。"),
    ("EntMoved", "Entity moved", "Entity verschoben", "实体已移动", "エンティティを移動しました"),
    ("ActorsFound", "%d of %d figures have a model",
     "%d von %d Figuren haben ein Modell", "%d / %d 角色有模型",
     "%d / %d のキャラクターにモデルあり"),
    ("ActorsNoAffect", "The script has no affect blocks - no figures to show",
     "Das Skript hat keine affect-Blöcke - es gibt keine Figuren zu zeigen",
     "脚本没有 affect 块 — 没有可显示的角色",
     "スクリプトに affect ブロックがありません。表示するキャラクターは"
     "ありません"),
    ("ActorsNoNpcFiles",
     "No .npc files in the game folders - without them NPC_type cannot be "
     "resolved to a model",
     "Keine .npc-Dateien in den Spielordnern - ohne sie lässt sich NPC_type "
     "keinem Modell zuordnen",
     "游戏目录中没有 .npc 文件 — 无法将 NPC_type 解析为模型",
     "ゲームフォルダーに .npc がありません。NPC_type をモデルに対応"
     "づけられません"),
    ("ActorsNoType", "%d figures have no NPC_type in the script",
     "%d Figuren haben im Skript kein NPC_type",
     "%d 个角色在脚本中没有 NPC_type",
     "%d 体のキャラクターにスクリプト内で NPC_type がありません"),
    ("ActorsNoNpcEntry",
     "%d NPC types are not in the %d entries of the .npc files",
     "%d NPC-Typen stehen nicht in den %d Einträgen der .npc-Dateien",
     "%d 个 NPC 类型不在 .npc 文件的 %d 个条目中",
     "%d 個の NPC タイプが .npc の %d 項目に見つかりません"),
    ("ActorsNoGlm", "%d models are not in any archive - is the mod folder "
     "listed?",
     "%d Modelle liegen in keinem Archiv - ist der Mod-Ordner eingetragen?",
     "%d 个模型不在任何归档中 — 是否已添加 MOD 目录？",
     "%d 個のモデルがどのアーカイブにもありません。MOD フォルダーは"
     "登録済みですか？"),
    ("ActorsBadGlm", "%d models could not be read",
     "%d Modelle ließen sich nicht lesen", "%d 个模型无法读取",
     "%d 個のモデルを読み込めませんでした"),
    ("PickedEntity", "%s (%s)", "%s (%s)", "%s（%s）", "%s（%s）"),
    ("PickedUses", "used %d times in the script", "%d mal im Skript benutzt",
     "在脚本中使用 %d 次", "スクリプトで %d 回使用"),
    ("PickedUnused", "not used in this script",
     "in diesem Skript nicht benutzt", "此脚本中未使用",
     "このスクリプトでは未使用"),
    ("PickHint",
     "Click an entity in the view to see where the script uses it",
     "Eine Entity im Bild anklicken, um zu sehen, wo das Skript sie benutzt",
     "点击视图中的实体以查看脚本在何处使用它",
     "ビュー内のエンティティをクリックすると、スクリプトでの使用箇所が分かります"),
    ("LoadMission", "Load mission...", "Mission laden...", "加载任务…",
     "ミッションを読み込む…"),
    ("LoadMissionHint",
     "Pick a .pk3 and everything is set up: map, entities, script, figures",
     "Eine .pk3 wählen - Karte, Entities, Skript und Figuren werden "
     "eingerichtet",
     "选择一个 .pk3，地图、实体、脚本和角色将一并设置",
     ".pk3 を選ぶと、マップ・エンティティ・スクリプト・キャラクターが"
     "まとめて設定されます"),
    ("MissionPick", "Missions in this archive", "Missionen in diesem Archiv",
     "此归档中的任务", "このアーカイブのミッション"),
    ("MissionNone", "No mission found - this archive has no map with scripts",
     "Keine Mission gefunden - dieses Archiv hat keine Karte mit Skripten",
     "未找到任务 — 此归档没有带脚本的地图",
     "ミッションが見つかりません。スクリプト付きのマップがありません"),
    ("MissionDuel", "no scripts", "keine Skripte", "无脚本", "スクリプトなし"),
    ("MissionLoaded", "%s: map, %d entities, %d scripts",
     "%s: Karte, %d Entities, %d Skripte", "%s：地图，%d 个实体，%d 个脚本",
     "%s: マップ, %d エンティティ, %d スクリプト"),
    ("MapScripts", "Scripts", "Skripte", "脚本", "スクリプト"),
    ("MapScriptsHint",
     "The scripts this map's entities point to - open one to load the cutscene",
     "Die Skripte, auf die die Entities dieser Karte zeigen - eines öffnen "
     "lädt die Zwischensequenz",
     "此地图实体指向的脚本 — 打开其一即可加载过场动画",
     "このマップのエンティティが参照するスクリプト。開くとカットシーンを"
     "読み込みます"),
    ("MapScriptsNone", "No scripts referenced by this map",
     "Diese Karte verweist auf keine Skripte", "此地图未引用脚本",
     "このマップはスクリプトを参照していません"),
    ("MapScriptMissing", "not found", "nicht gefunden", "未找到", "見つかりません"),
    ("MapActors", "Figures", "Figuren", "角色", "キャラクター"),
    ("MapActorsHint",
     "Show the script's characters in the map, moved by the timeline",
     "Die Figuren des Skripts in der Karte zeigen, bewegt von der Zeitleiste",
     "在地图中显示脚本的角色，由时间轴驱动",
     "スクリプトのキャラクターをマップに表示し、タイムラインで動かします"),
    ("ActorsNoModels",
     "No models found - add a game folder so the NPC types can be resolved",
     "Keine Modelle gefunden - einen Spielordner hinzufügen, damit die "
     "NPC-Typen aufgelöst werden können",
     "未找到模型 — 请添加游戏目录以解析 NPC 类型",
     "モデルが見つかりません。NPC タイプを解決するにはゲームフォルダーを"
     "追加してください"),
    ("TlFollow", "Follow", "Mitfahren", "跟随", "追従"),
    ("TlFollowHint",
     "Move the view along the camera path while playing",
     "Die Ansicht beim Abspielen der Kamerabahn folgen lassen",
     "播放时让视图跟随摄像机路径",
     "再生中、ビューをカメラパスに追従させます"),
    ("MapSky", "Sky", "Himmel", "天空", "空"),
    ("MapSkyHint",
     "Sky surfaces enclose the map like a box - hiding them lets you look in "
     "from outside",
     "Himmelsflächen umschließen die Karte wie einen Kasten - ausgeblendet "
     "sieht man von außen hinein",
     "天空面像盒子一样包围地图 — 隐藏后可从外部看进去",
     "空の面はマップを箱のように囲みます。隠すと外から中が見えます"),
    ("MapEntities", "Entities", "Entities", "实体", "エンティティ"),
    ("MapNames", "Names", "Namen", "名称", "名前"),
    ("MapNamesHint", "Show the names of entities and characters in the view", "Namen von Entities und Figuren in der Ansicht zeigen", "在视图中显示实体和角色的名称", "ビューにエンティティとキャラクターの名前を表示"),
    ("MapEntitiesHint",
     "Navgoals, look targets, script runners and NPC spawns from the map",
     "Wegpunkte, Blickziele, Skriptstarter und NPC-Punkte aus der Karte",
     "地图中的导航点、注视目标、脚本触发器和 NPC 出生点",
     "マップのナビゴール、注視ターゲット、スクリプト起動、NPC 出現点"),
    ("ViewModel", "Model", "Modell", "模型", "モデル"),
    ("ModeModelHint", "A Ghoul2 model, to look at figures",
     "Ein Ghoul2-Modell, um Figuren anzusehen", "Ghoul2 模型，用于查看角色",
     "Ghoul2 モデル。キャラクターを確認します"),
    ("ModelNone", "Load a model (File → Browse .pk3 → Models)",
     "Modell laden (Datei → In .pk3 blättern → Modelle)",
     "加载模型（文件 → 浏览 .pk3 → 模型）",
     "モデルを読み込む（ファイル → .pk3 を参照 → モデル）"),
    ("ModelInfo", "%d surfaces, %d triangles, %d bones",
     "%d Flächen, %d Dreiecke, %d Knochen", "%d 个面，%d 个三角形，%d 根骨骼",
     "%d サーフェス, %d 三角形, %d ボーン"),
    ("ModelSkeleton", "Skeleton: %s", "Skelett: %s", "骨架：%s", "スケルトン: %s"),
    ("OpenGlm", "Open .glm...", ".glm öffnen...", "打开 .glm…", ".glm を開く…"),
    ("OpenGlmPk3", "From .pk3...", "Aus .pk3...", "从 .pk3…", ".pk3 から…"),
    ("OpenSkin", "Open .skin...", ".skin öffnen...", "打开 .skin…",
     ".skin を開く…"),
    ("OpenMapFile", "Open map...", "Karte öffnen...", "打开地图…",
     "マップを開く…"),
    ("OpenMapPk3", "From .pk3...", "Aus .pk3...", "从 .pk3…", ".pk3 から…"),
    ("OpenEnt", "Open .ent...", ".ent öffnen...", "打开 .ent…", ".ent を開く…"),
    ("OpenEntHint",
     "Load entities from a separate .ent file - it replaces the ones in the map",
     "Entities aus einer eigenen .ent-Datei laden - sie ersetzen die der Karte",
     "从单独的 .ent 文件加载实体 — 将替换地图中的实体",
     "別の .ent ファイルからエンティティを読み込み、マップのものを置き換えます"),
    ("EntLoaded", "%d entities from the .ent file",
     "%d Entities aus der .ent-Datei", "来自 .ent 文件的 %d 个实体",
     ".ent ファイルから %d エンティティ"),
    ("ModelLoadGla", "Load .gla...", ".gla laden...", "加载 .gla…",
     ".gla を読み込む…"),
    ("ModelPlay", "Play", "Abspielen", "播放", "再生"),
    ("ModelStop", "Stop", "Anhalten", "停止", "停止"),
    ("ModelLoop", "Loop", "Wiederholen", "循环", "ループ"),
    ("ModelLoopHint",
     "Off follows the .cfg: walking loops, dying does not",
     "Aus folgt der .cfg: Gehen wiederholt sich, Sterben nicht",
     "关闭时遵循 .cfg：行走循环，死亡不循环",
     "オフのときは .cfg に従います。歩行はループし、死亡はしません"),
    ("ViewMenuModel", "Model", "Modell", "模型", "モデル"),
    ("ModelSeconds", "%.2f s of %.2f s", "%.2f s von %.2f s",
     "%.2f 秒 / %.2f 秒", "%.2f 秒 / %.2f 秒"),
    ("ModelSpeed", "Speed", "Tempo", "速度", "速度"),
    ("ModelSkin", "Skin", "Haut", "皮肤", "スキン"),
    ("ModelSkinHint",
     "A model often ships several skins - they decide which surfaces show",
     "Ein Modell bringt oft mehrere Häute mit - sie entscheiden, welche "
     "Flächen sichtbar sind",
     "模型通常自带多个皮肤 — 它们决定显示哪些面",
     "モデルには複数のスキンが同梱されていることが多く、どの面を表示するかを"
     "決めます"),
    ("ModelAnimCopied", "Animation name copied: %s",
     "Name der Animation kopiert: %s",
     "已复制动画名称：%s", "アニメーション名をコピーしました: %s"),
    ("ModelSurfaces", "%d of %d surfaces", "%d von %d Flächen",
     "%d / %d 个面", "%d / %d サーフェス"),
    ("ModelTextures", "Textures", "Texturen", "纹理", "テクスチャ"),
    ("ModelTexFound", "%d of %d textures", "%d von %d Texturen",
     "%d / %d 个纹理", "%d / %d のテクスチャ"),
    ("ModelFrame", "Frame %d of %d", "Bild %d von %d", "第 %d / %d 帧",
     "フレーム %d / %d"),
    ("ModelAnimCount", "%d animations, %d frames",
     "%d Animationen, %d Bilder", "%d 个动画，%d 帧",
     "%d アニメーション, %d フレーム"),
    ("ModelNoAnim",
     "No animation - that needs the .gla; the model is shown in its rest pose",
     "Keine Animation - dafür braucht es die .gla; das Modell steht in "
     "seiner Ruhelage",
     "无动画 — 需要 .gla 文件；模型显示为静止姿势",
     "アニメーションなし — .gla が必要です。モデルは静止姿勢で表示されます"),
    ("ModelCaps", "Show caps", "Kappen zeigen", "显示封盖", "キャップを表示"),
    ("ModelCapsHint",
     "Surfaces ending in _off are switched off by the default skin - they "
     "cap the holes where limbs attach",
     "Flächen mit _off sind in der Vorgabehaut abgeschaltet - sie decken die "
     "Löcher ab, an denen Gliedmaßen ansetzen",
     "以 _off 结尾的面在默认皮肤中被关闭 — 它们封住肢体连接处的孔洞",
     "_off で終わる面は既定スキンでは無効です。手足の接合部の穴を塞ぎます"),
    ("MapBrightness", "Brightness", "Helligkeit", "亮度", "明るさ"),
    ("MapMinLight", "Ambient", "Grundlicht", "环境光", "環境光"),
    ("MapMinLightHint",
     "Lifts the darkest areas so you can navigate - 0 is exactly like the game",
     "Hebt die dunkelsten Stellen an, damit man sich zurechtfindet - 0 ist "
     "genau wie im Spiel",
     "提升最暗区域以便导航 — 0 与游戏完全一致",
     "最も暗い場所を明るくします。0 はゲームと同じです"),
    ("MapFps", "%.0f fps", "%.0f Bilder/s", "%.0f 帧/秒", "%.0f fps"),
    ("MapFrameMs", "%.2f ms per frame", "%.2f ms je Bild", "每帧 %.2f 毫秒",
     "1 フレーム %.2f ms"),
    ("MapDetail", "Detail", "Feinheit", "细节", "細かさ"),
    ("MapTriangles", "%d triangles, %d draw calls",
     "%d Dreiecke, %d Zeichenaufrufe", "%d 个三角形，%d 次绘制调用",
     "%d 三角形, %d 描画呼び出し"),

    ("ActEdit", "Edit...", "Bearbeiten...", "编辑…", "編集…"),
    ("MsgMacros", "Macros", "Makros", "宏", "マクロ"),

    ("EditorReEvalHint",
     "Reset the fields below to the defaults of the current selection",
     "Die Felder darunter auf die Vorgabewerte der aktuellen Auswahl zurücksetzen",
     "将下方字段重置为当前选择的默认值",
     "下の項目を現在の選択の既定値に戻します"),

    ("InsertInto", "Insert into block", "In den Block einfügen",
     "插入到块中", "ブロックに挿入"),
    ("InsertAfter", "Insert after", "Dahinter einfügen", "在其后插入",
     "後ろに挿入"),
    ("MsgSelected", "%d selected", "%d ausgewählt", "已选择 %d 个", "%d 件を選択"),

]

# --- Formatplatzhalter pruefen -------------------------------------------
#
# Die Meldungen werden mit snprintf zusammengesetzt, und die Formatzeichen-
# kette kommt aus DIESER Tabelle. Der Uebersetzer kann sie deshalb nicht
# pruefen. Schreibt jemand in einer Sprache %d, wo die anderen %s haben, ist
# das undefiniertes Verhalten - in der Regel ein Absturz, und nur in dieser
# einen Sprache. Also hier pruefen, wo es billig ist.
import re
import sys

_SPEC = re.compile(r'%[-+ #0]*[0-9]*(?:\.[0-9]+)?[hlLqjzt]*([diouxXeEfgGaAcspn%])')

problems = []
for row in S:
    name = row[0]
    kinds = [tuple(m.group(1) for m in _SPEC.finditer(t) if m.group(1) != '%')
             for t in row[1:]]
    if len(set(kinds)) != 1:
        problems.append((name, kinds))

names = [r[0] for r in S]
if len(set(names)) != len(names):
    dup = [n for n in names if names.count(n) > 1]
    problems.append(("doppelte Kennung", sorted(set(dup))))

if problems:
    for p in problems:
        print("FEHLER:", p, file=sys.stderr)
    sys.exit(1)


def esc(s):
    """
    Nicht-ASCII als ausdrueckliche UTF-8-Bytes schreiben.

    Weder Umlaute noch CJK duerfen roh im Quelltext stehen: MSVC wandelt
    Zeichenliterale ohne /utf-8 in die ANSI-Codepage um, und dann sind die
    chinesischen Texte Muell - und zwar nur beim Uebersetzen mit MSVC, was
    man hier nie merken wuerde. \\uXXXX hilft nicht, das unterliegt
    derselben Umwandlung. Rohe Bytes unterliegen ihr nicht.
    """
    out = []
    prev_hex = False
    for b in s.encode('utf-8'):
        if b == 0x5C:                       # Rueckstrich
            out.append('\\\\'); prev_hex = False
        elif b == 0x22:                     # Anfuehrungszeichen
            out.append('\\"'); prev_hex = False
        elif 0x20 <= b < 0x7F:
            # Nach einer Hex-Folge muss die Zeichenkette getrennt werden,
            # sonst frisst \xNN das naechste Hex-Zeichen mit auf.
            if prev_hex and chr(b) in '0123456789abcdefABCDEF':
                out.append('" "')
            out.append(chr(b)); prev_hex = False
        else:
            out.append('\\x%02x' % b); prev_hex = True
    return ''.join(out)


h = '''// Oberflaechentexte in vier Sprachen.
//
// Erzeugt von tools/gen_i18n.py - nicht von Hand aendern, sondern dort.
// Ein Aufzaehlungswert je Text, eine Tabelle mit vier Spalten, und ein
// static_assert, das Tabellenlaenge und Aufzaehlung vergleicht.
//
// Hauptsprache ist Englisch: die Sprache der JKA-Modding-Gemeinde, der
// Dateiformate und der Engine-Meldungen.
#ifndef BHED_I18N_H
#define BHED_I18N_H

#include <string>
#include <vector>

namespace bhed::i18n {

enum class Language {
    English,
    German,
    ChineseSimplified,
    Japanese,
};

enum class Str {
'''
for n in names:
    h += "    %s,\n" % n
h += '''    Count,
};

// Der aktuelle Text.
const char* tr(Str id);

Language currentLanguage();
void setLanguage(Language language);

struct LanguageInfo {
    Language language;
    const char* code;        // fuer die Einstellungsdatei: en, de, zh-Hans, ja
    const char* nativeName;  // im Menue, in der eigenen Schrift
    bool needsCjkFont;       // ob eine zusaetzliche Schrift noetig ist
};
const std::vector<LanguageInfo>& languages();
const LanguageInfo* findLanguage(const std::string& code);

// Sprache aus den Windows-Regionseinstellungen ableiten. Getrennt gehalten,
// damit die Zuordnung ohne Windows pruefbar bleibt.
Language fromSystemLocale(const std::string& locale);

// Nur fuer Tests: ein Text in einer bestimmten Sprache.
const char* trIn(Language language, Str id);

}  // namespace bhed::i18n
#endif
'''
# newline='\n': sonst schreibt Python unter Windows CRLF, und die Datei
# weicht vom Paketstand (LF) ab - lint_i18n_erzeugt schlug unter Windows an.
open('include/bhed/i18n.h', 'w', encoding='utf-8', newline='\n').write(h)

c = '''// Erzeugt von tools/gen_i18n.py - nicht von Hand aendern.
#include "bhed/i18n.h"

#include <cstddef>

namespace bhed::i18n {
namespace {

struct Entry {
    const char* en;
    const char* de;
    const char* zh;
    const char* ja;
};

// Reihenfolge muss der Aufzaehlung Str entsprechen.
const Entry kTable[] = {
'''
for (n, en, de, zh, ja) in S:
    c += '    {"%s", "%s", "%s", "%s"},  // %s\n' % (esc(en), esc(de), esc(zh), esc(ja), n)
c += '''};

static_assert(sizeof(kTable) / sizeof(kTable[0]) == static_cast<std::size_t>(Str::Count),
              "Die Texttabelle passt nicht zur Aufzaehlung Str. "
              "Beide werden von tools/gen_i18n.py erzeugt - dort aendern.");

Language g_language = Language::English;

}  // namespace

const char* trIn(Language language, Str id) {
    const std::size_t index = static_cast<std::size_t>(id);
    if (index >= static_cast<std::size_t>(Str::Count)) {
        return "";
    }
    const Entry& e = kTable[index];
    switch (language) {
        case Language::German: return e.de;
        case Language::ChineseSimplified: return e.zh;
        case Language::Japanese: return e.ja;
        default: return e.en;
    }
}

const char* tr(Str id) { return trIn(g_language, id); }

Language currentLanguage() { return g_language; }
void setLanguage(Language language) { g_language = language; }

const std::vector<LanguageInfo>& languages() {
    // ImGui 1.92 laedt Glyphen bei Bedarf nach; Glyphbereiche muessen nicht
    // mehr vorgegeben werden. needsCjkFont sagt nur noch, ob ueberhaupt eine
    // Schrift mit CJK-Zeichen gebraucht wird.
    static const std::vector<LanguageInfo> kLanguages = {
        {Language::English, "en", "English", false},
        {Language::German, "de", "Deutsch", false},
        {Language::ChineseSimplified, "zh-Hans", "\\u4e2d\\u6587", true},
        {Language::Japanese, "ja", "\\u65e5\\u672c\\u8a9e", true},
    };
    return kLanguages;
}

const LanguageInfo* findLanguage(const std::string& code) {
    for (const auto& l : languages()) {
        if (code == l.code) {
            return &l;
        }
    }
    return nullptr;
}

Language fromSystemLocale(const std::string& locale) {
    // Nur das Praefix vergleichen: "de-DE", "de-AT" und "de" sind alle
    // Deutsch. Chinesisch braucht mehr Sorgfalt - zh-Hant (Taiwan, Hongkong)
    // ist nicht dasselbe wie zh-Hans, und wir haben nur Vereinfachtes. Fuer
    // zh-Hant bleibt Englisch die bessere Wahl als falsche Zeichen.
    auto startsWith = [&](const char* prefix) {
        std::size_t n = 0;
        while (prefix[n] != 0) {
            ++n;
        }
        return locale.size() >= n && locale.compare(0, n, prefix) == 0;
    };

    if (startsWith("de")) { return Language::German; }
    if (startsWith("ja")) { return Language::Japanese; }
    if (startsWith("zh-Hant") || startsWith("zh-TW") || startsWith("zh-HK") ||
        startsWith("zh-MO")) {
        return Language::English;
    }
    if (startsWith("zh")) { return Language::ChineseSimplified; }
    return Language::English;
}

}  // namespace bhed::i18n
'''
open('src/i18n.cpp', 'w', encoding='utf-8', newline='\n').write(c)
print("i18n erzeugt: %d Texte x 4 Sprachen = %d Zeichenketten" % (len(S), len(S) * 4))

"""Missionen und Modellansicht."""
from inhalt_basis import T

MISSIONEN = {
    "id": "missionen",
    "gruppe": "mission",
    "titel": T("Missions", "Missionen", "任务", "ミッション"),
    "kurz": T(
        "A mission is a map with its entities and all its scripts. Open one whole, build your own, and pack it into a .pk3 that the game loads.",
        "Eine Mission ist eine Karte mit ihren Entities und all ihren Skripten. Eine ganz öffnen, eine eigene bauen und sie in ein .pk3 packen, das das Spiel lädt.",
        "任务就是一张地图及其实体和全部脚本。可以完整打开任务、创建自己的任务,并打包成游戏可加载的 .pk3。",
        "ミッションとは、マップとそのエンティティ、すべてのスクリプトのまとまりです。まるごと開き、自分で作り、ゲームが読み込める .pk3 にまとめられます。"),
    "bloecke": [
        ("h2", "laden", T("Opening a mission", "Eine Mission öffnen", "打开任务", "ミッションを開く")),
        ("p", T(
            "**{{LoadMission}}** (menu {{MenuFile}} or the map toolbar) picks a `.pk3` and lists **all missions of that game** - the maps that have scripts, from every archive of the game folders, exactly as the game finds them (for a name, the archive that wins counts). It does not matter which archive of the game you pick: `assets1.pk3` of plain Jedi Academy lists `t1_sour`, `kor1`, `yavin1` … just like `assets0.pk3`. The missions of the chosen archive stand at the top; the others follow once they have been read (the maps are read in the background - the line above the list shows how far). The filter field narrows the list by name. Pick one and everything is set up: the map, its `.ent` with the mission's figures, and **every script of the mission in its own tab**. All tabs share the map and the camera.",
            "**{{LoadMission}}** (Menü {{MenuFile}} oder die Kartenleiste) wählt ein `.pk3` und nennt **alle Missionen dieses Spiels** - die Karten mit Skripten aus allen Archiven der Spielordner, genau so, wie das Spiel sie findet (je Name zählt das Archiv, das gewinnt). Welches Archiv des Spiels man wählt, ist gleich: `assets1.pk3` des reinen Jedi Academy nennt `t1_sour`, `kor1`, `yavin1` … genau wie `assets0.pk3`. Die Missionen des gewählten Archivs stehen oben, die übrigen folgen, sobald sie gelesen sind (die Karten werden im Hintergrund gelesen - die Zeile über der Liste zeigt, wie weit). Das Filterfeld schränkt die Liste nach dem Namen ein. Eine wählen, und alles ist eingerichtet: die Karte, ihre `.ent` mit den Figuren der Mission und **jedes Skript der Mission in seinem eigenen Reiter**. Alle Reiter teilen sich Karte und Kamera.",
            "**{{LoadMission}}**({{MenuFile}} 菜单或地图工具栏)选择一个 `.pk3`,并列出**该游戏的全部任务**——游戏目录中所有压缩包里带脚本的地图,与游戏查找它们的方式完全相同(同名文件以胜出的压缩包为准)。选择该游戏的哪个压缩包都无所谓:原版 Jedi Academy 的 `assets1.pk3` 与 `assets0.pk3` 一样会列出 `t1_sour`、`kor1`、`yavin1`……所选压缩包中的任务排在最上方,其余任务读取完毕后列在下面(地图在后台读取——列表上方的一行显示进度)。筛选框可按名称缩小列表。选择一个,一切就绪:地图、包含任务角色的 `.ent`,以及**任务的每个脚本各占一个标签页**。所有标签页共享地图和摄像机。",
            "**{{LoadMission}}**({{MenuFile}} メニューまたはマップのツールバー)で `.pk3` を選ぶと、**そのゲームのすべてのミッション**が一覧表示されます。ゲームフォルダーのすべてのアーカイブにある、スクリプト付きのマップで、ゲームが見つけるのとまったく同じように探します(同じ名前なら優先されるアーカイブのものが使われます)。ゲームのどのアーカイブを選んでもかまいません。素の Jedi Academy の `assets1.pk3` でも、`assets0.pk3` と同じく `t1_sour`、`kor1`、`yavin1` … が表示されます。選んだアーカイブのミッションが上に並び、残りは読み込みが終わると下に続きます(マップはバックグラウンドで読み込まれ、一覧の上の行に進み具合が表示されます)。フィルター欄で名前による絞り込みができます。選ぶとすべてが準備されます。マップ、ミッションのキャラクターを含む `.ent`、そして**ミッションのすべてのスクリプトがそれぞれのタブで**開きます。タブはすべてマップとカメラを共有します。")),
        ("img", "h_mission_wahl", T(
            "The missions of the game, here narrowed with the filter field - those of the chosen archive first, the other archives of the game below.",
            "Die Missionen des Spiels, hier mit dem Filterfeld eingeschränkt - zuerst die des gewählten Archivs, darunter die aus den übrigen Archiven des Spiels.",
            "该游戏的任务(此处已用筛选框缩小)——所选压缩包的任务在前,游戏其他压缩包的任务在下方。",
            "ゲームのミッション(ここではフィルター欄で絞り込んでいます)。選んだアーカイブのものが先頭に、ゲームのほかのアーカイブのものがその下に並びます。")),
        ("p", T(
            "The tabs open **in the order in which the game starts the scripts**. For this, BehavEd-Remake plays the map from the moment it loads, like the engine: the spawn scripts of the entities, the spawn script of `worldspawn`, `trigger_always`, NPC spawners without a `targetname`, the targets of the player start when the player enters the game - and everything that follows from them through `run`, `use`, relays, counters, spawners and `kill` with a death script. The tab of the first script is in front. Scripts that only the player sets off (a trigger he walks into, a fight) come last, in the order of the entities.",
            "Die Reiter öffnen sich **in der Reihenfolge, in der das Spiel die Skripte startet**. Dazu spielt BehavEd-Remake die Karte vom Laden an wie die Engine: die spawnscripts der Entities, das spawnscript von `worldspawn`, `trigger_always`, NPC-Spawner ohne `targetname`, die Ziele der Startposition, wenn der Spieler ins Spiel kommt - und alles, was daraus über `run`, `use`, Relais, Zähler, Spawner und `kill` mit deathscript folgt. Vorn steht der Reiter des ersten Skripts. Skripte, die erst der Spieler auslöst (ein Trigger, in den er läuft, ein Kampf), stehen hinten, in der Reihenfolge der Entities.",
            "标签页按**游戏启动脚本的顺序**打开。为此 BehavEd-Remake 像引擎一样从地图加载的那一刻起运行地图:实体的 spawnscript、`worldspawn` 的 spawnscript、`trigger_always`、没有 `targetname` 的 NPC 生成器、玩家进入游戏时出生点的目标——以及由此经 `run`、`use`、中继、计数器、生成器和带 deathscript 的 `kill` 引发的一切。第一个脚本的标签页在最前。只有玩家才会触发的脚本(走进的触发器、战斗)排在最后,按实体的顺序。",
            "タブは**ゲームがスクリプトを開始する順番**で開きます。そのために BehavEd-Remake はエンジンと同じようにマップを読み込みの時点から動かします。エンティティの spawnscript、`worldspawn` の spawnscript、`trigger_always`、`targetname` のない NPC スポナー、プレイヤーがゲームに入ったときのスタート位置のターゲット、そしてそこから `run`、`use`、リレー、カウンター、スポナー、deathscript 付きの `kill` を通じて起きるすべてです。最初のスクリプトのタブが前面になります。プレイヤーが引き起こすまで始まらないスクリプト(踏み込むトリガー、戦闘)は、エンティティの順で最後に並びます。")),
        ("p", T(
            "Which game folders apply is decided by the archive: if its folder is one of your game folders ({{MenuEdit}} → {{PathsTitle}}), those are used. If it belongs to another installation - its `base`, or a mod folder next to a `base` - BehavEd-Remake uses that installation's folders **for this session only**, in the engine's order (`base`, then the mod), and does not mix in your configured folders. A `.pk3` in any other folder is added behind your game folders, also only for this session. Nothing is saved: the game folders window shows the folders of the session with a button back to your own ({{PathsSessionBack}}).",
            "Welche Spielordner gelten, entscheidet das Archiv: liegt es in einem deiner Spielordner ({{MenuEdit}} → {{PathsTitle}}), gelten diese. Gehört es zu einer anderen Installation - deren `base` oder ein Mod-Ordner neben einem `base` -, nutzt BehavEd-Remake **nur für diese Sitzung** die Ordner dieser Installation in der Reihenfolge der Engine (`base`, dann die Mod) und mischt deine eingerichteten Ordner nicht dazu. Ein `.pk3` in irgendeinem anderen Ordner kommt hinter deine Spielordner, ebenfalls nur für diese Sitzung. Gespeichert wird nichts: das Fenster der Spielordner zeigt die Ordner der Sitzung mit einem Knopf zurück zu deinen eigenen ({{PathsSessionBack}}).",
            "使用哪些游戏目录由压缩包决定:如果它位于你的某个游戏目录中({{MenuEdit}} → {{PathsTitle}}),就使用这些目录。如果它属于另一个安装——该安装的 `base`,或 `base` 旁边的模组目录——BehavEd-Remake **仅在本次会话中**按引擎的顺序(先 `base`,再模组)使用该安装的目录,不会混入你配置的目录。其他任意目录中的 `.pk3` 会排在你的游戏目录之后,同样仅限本次会话。不会保存任何内容:游戏目录窗口会显示本次会话的目录,并提供返回你自己目录的按钮({{PathsSessionBack}})。",
            "どのゲームフォルダーを使うかはアーカイブで決まります。設定したゲームフォルダー({{MenuEdit}} → {{PathsTitle}})のどれかにあれば、それらが使われます。別のインストールのもの(その `base`、または `base` の隣の Mod フォルダー)なら、BehavEd-Remake は**このセッションに限り**、エンジンの順番(`base`、次に Mod)でそのインストールのフォルダーを使い、設定済みのフォルダーは混ぜません。それ以外のフォルダーにある `.pk3` は、同じくこのセッションに限り、ゲームフォルダーの後ろに加わります。何も保存されません。ゲームフォルダーのウィンドウにはセッションのフォルダーと、自分のフォルダーに戻すボタン({{PathsSessionBack}})が表示されます。")),
        ("h2", "zuletzt", T("Recent missions", "Zuletzt geöffnete Missionen", "最近的任务", "最近のミッション")),
        ("p", T(
            "**{{MenuFile}} → {{RecentMissions}}** and the start page list the missions you opened last - from an archive or from a mission folder. One click opens the whole project again: map, entities and all scripts. An entry whose file is gone is removed from the list.",
            "**{{MenuFile}} → {{RecentMissions}}** und die Startfläche nennen die zuletzt geöffneten Missionen - aus einem Archiv oder aus einem Missionsordner. Ein Klick öffnet das ganze Projekt wieder: Karte, Entities und alle Skripte. Ein Eintrag, dessen Datei fehlt, fällt aus der Liste.",
            "**{{MenuFile}} → {{RecentMissions}}** 和起始页面列出最近打开的任务——无论来自压缩包还是任务文件夹。单击即可重新打开整个项目:地图、实体和所有脚本。文件已不存在的条目会从列表中移除。",
            "**{{MenuFile}} → {{RecentMissions}}** とスタート画面に、最近開いたミッション(アーカイブまたはミッションフォルダー)が表示されます。クリック 1 つでプロジェクト全体(マップ、エンティティ、すべてのスクリプト)が開き直されます。ファイルがなくなった項目は一覧から外れます。")),
        ("img", "h_menue_missionen", T(
            "{{MenuFile}} → {{RecentMissions}}.",
            "{{MenuFile}} → {{RecentMissions}}.",
            "{{MenuFile}} → {{RecentMissions}}。",
            "{{MenuFile}} → {{RecentMissions}}。")),
        ("h2", "neu", T("A new mission", "Eine neue Mission", "新建任务", "新しいミッション")),
        ("p", T(
            "**{{MissionNew}}** starts a mission on any map. Choose the map, give the mission a name (letters, digits and `_`; the game loads it as `maps/<name>.bsp`, so `devmap <name>`), and a script folder. BehavEd-Remake creates a player start, a script runner, the intro script and a mission folder under `Documents\\behaved\\missions`. The intro runs when the player spawns.",
            "**{{MissionNew}}** beginnt eine Mission auf einer beliebigen Karte. Wähle die Karte, gib der Mission einen Namen (Buchstaben, Ziffern und `_`; das Spiel lädt sie als `maps/<Name>.bsp`, also `devmap <Name>`) und einen Skriptordner. BehavEd-Remake legt einen Spielerstart, einen Skriptstarter, das Intro-Skript und einen Missionsordner unter `Dokumente\\behaved\\missions` an. Das Intro läuft, wenn der Spieler erscheint.",
            "**{{MissionNew}}** 可在任意地图上开始一个任务。选择地图,为任务命名(字母、数字和 `_`;游戏以 `maps/<名称>.bsp` 加载,即 `devmap <名称>`),并指定脚本文件夹。BehavEd-Remake 会创建玩家出生点、脚本启动器、开场脚本,以及位于 `文档\\behaved\\missions` 下的任务文件夹。玩家出生时开场脚本开始运行。",
            "**{{MissionNew}}** で任意のマップにミッションを始めます。マップを選び、ミッション名(英数字と `_`。ゲームは `maps/<名前>.bsp` として読み込むので `devmap <名前>`)とスクリプトフォルダーを指定します。BehavEd-Remake はプレイヤースタート、スクリプトランナー、イントロスクリプト、`ドキュメント\\behaved\\missions` 内のミッションフォルダーを作ります。イントロはプレイヤーの出現時に実行されます。")),
        ("img", "h_mission_neu", T(
            "The {{MissionNewTitle}} dialog.",
            "Der Dialog {{MissionNewTitle}}.",
            "{{MissionNewTitle}} 对话框。",
            "{{MissionNewTitle}} ダイアログ。")),
        ("p", T(
            "A mission that was never packed lives in its folder; **{{MissionOpenFolder}}** continues it after a restart.",
            "Eine Mission, die nie gepackt wurde, lebt in ihrem Ordner; **{{MissionOpenFolder}}** setzt sie nach einem Neustart fort.",
            "从未打包的任务保存在其文件夹中;重启后可用 **{{MissionOpenFolder}}** 继续。",
            "一度もパックしていないミッションはフォルダーにあり、再起動後は **{{MissionOpenFolder}}** で続けられます。")),
        ("h2", "speichern", T("Saving a mission", "Eine Mission speichern", "保存任务", "ミッションの保存")),
        ("p", T(
            "**{{MissionSave}}** shows what will go into the archive - every file with its size - and checks the mission first: a script that an entity uses but that does not exist, a script that does not compile, no player start, a target that points nowhere, a `.str` without `ENDMARKER`. Errors (red) must be fixed, warnings (orange) may stay. **{{MissionSaveNow}}** saves all scripts, writes the `.ent`, compiles the scripts, adds your own ROFF files and packs the `.pk3` into the game folder.",
            "**{{MissionSave}}** zeigt, was ins Archiv kommt - jede Datei mit ihrer Größe - und prüft die Mission vorher: ein Skript, das eine Entity benutzt, das es aber nicht gibt, ein Skript, das sich nicht übersetzen lässt, kein Spielerstart, ein Ziel, das ins Leere zeigt, eine `.str` ohne `ENDMARKER`. Fehler (rot) müssen behoben werden, Warnungen (orange) dürfen bleiben. **{{MissionSaveNow}}** speichert alle Skripte, schreibt die `.ent`, übersetzt die Skripte, nimmt deine eigenen ROFF-Dateien mit und packt das `.pk3` in den Spielordner.",
            "**{{MissionSave}}** 会显示将放入压缩包的内容——每个文件及其大小——并先检查任务:实体使用了但不存在的脚本、无法编译的脚本、缺少玩家出生点、指向不存在目标的引用、缺少 `ENDMARKER` 的 `.str`。错误(红色)必须修正,警告(橙色)可以保留。**{{MissionSaveNow}}** 保存所有脚本、写入 `.ent`、编译脚本、加入你自己的 ROFF 文件,并把 `.pk3` 打包到游戏文件夹。",
            "**{{MissionSave}}** はアーカイブに入るもの(各ファイルとサイズ)を表示し、先にミッションをチェックします。エンティティが使うのに存在しないスクリプト、コンパイルできないスクリプト、プレイヤースタートがない、存在しない対象を指すターゲット、`ENDMARKER` のない `.str`。エラー(赤)は直す必要があり、警告(オレンジ)は残してもかまいません。**{{MissionSaveNow}}** はすべてのスクリプトを保存し、`.ent` を書き、スクリプトをコンパイルし、自作の ROFF ファイルを加え、`.pk3` をゲームフォルダーに作ります。")),
        ("fluss", T("How a mission starts in the game"),
         [T("The player spawns"), T("info_player_start"), T("target_scriptrunner"), T("The intro script"), T("affect blocks")],
         [[], [T("its key target fires the runner")], [T("its key usescript names the script")],
          [T("camera ENABLE ... DISABLE")], [T("figures walk, look, speak"), T("their spawnscripts run when they appear")]],
         T("A new mission sets this up for you - the Graph view shows the same chain.")),
        ("img", "h_mission_speichern", T(
            "The {{MissionSaveTitle}} dialog with its checks.",
            "Der Dialog {{MissionSaveTitle}} mit seinen Prüfungen.",
            "带检查结果的 {{MissionSaveTitle}} 对话框。",
            "チェック結果付きの {{MissionSaveTitle}} ダイアログ。")),
        ("legende", [
            (T("**{{MissionTarget}}**"), T("Where the `.pk3` is written; **{{MissionChoose}}** picks another place.")),
            (T("**{{MissionSaveNow}}** / **{{MissionCancel}}**"), T("Saves, compiles and packs - or closes the window. Above them: every file of the package and the checks.")),
        ]),
        ("note", T(
            "The archive goes into the game folder that wins - the bottom one under **{{MenuEdit}} → {{PathsTitle}}** - as `zz_<name>.pk3`, so it wins over older files. Start the game and type `devmap <name>` in the console to play it.",
            "Das Archiv landet im Spielordner, der gewinnt - dem untersten unter **{{MenuEdit}} → {{PathsTitle}}** - als `zz_<Name>.pk3`, damit es gegen ältere Dateien gewinnt. Starte das Spiel und tippe in der Konsole `devmap <Name>`.")),
    ],
}

MODELL = {
    "id": "modell",
    "gruppe": "mission",
    "titel": T("Model view and characters", "Modellansicht und Figuren", "模型视图与角色", "モデルビューとキャラクター"),
    "kurz": T(
        "Look at any character of the game with its skins and animations, and put an animation into the script with a double-click.",
        "Jede Figur des Spiels mit ihren Skins und Animationen ansehen und eine Animation per Doppelklick ins Skript setzen.",
        "查看游戏中任意角色及其皮肤和动画,双击即可把动画写入脚本。",
        "ゲームのどのキャラクターでも、スキンとアニメーション付きで表示し、ダブルクリックでアニメーションをスクリプトに入れられます。"),
    "bloecke": [
        ("img", "h_modell", T(
            "Model mode: the animation list on the left, the model in the middle, the playback bar below.",
            "Modell-Modus: links die Animationsliste, in der Mitte das Modell, unten die Abspielleiste.",
            "模型模式:左侧是动画列表,中间是模型,下方是播放栏。",
            "モデルモード。左にアニメーション一覧、中央にモデル、下に再生バー。"), "breit"),
        ("p", T(
            "Switch to **{{ViewModel}}** in the mode bar. **{{NpcBrowser}}** lists every character of the game (from the `.npc` files) - click one to load its model and skin as the game shows it. **{{OpenGlm}}** and **{{OpenGlmPk3}}** open any model. Left mouse orbits, middle mouse pans, the wheel zooms; **{{ModelTurntable}}**, **{{ModelBones}}** and **{{ModelResetView}}** sit in the toolbar.",
            "Schalte in der Modusleiste auf **{{ViewModel}}**. **{{NpcBrowser}}** nennt jede Figur des Spiels (aus den `.npc`-Dateien) - eine anklicken lädt ihr Modell und ihren Skin, wie das Spiel sie zeigt. **{{OpenGlm}}** und **{{OpenGlmPk3}}** öffnen ein beliebiges Modell. Linke Maus umkreist, mittlere verschiebt, das Rad zoomt; **{{ModelTurntable}}**, **{{ModelBones}}** und **{{ModelResetView}}** stehen in der Leiste.",
            "在模式栏切换到 **{{ViewModel}}**。**{{NpcBrowser}}** 列出游戏中的所有角色(来自 `.npc` 文件)——单击即可按游戏中的样子加载其模型和皮肤。**{{OpenGlm}}** 和 **{{OpenGlmPk3}}** 可打开任意模型。鼠标左键环绕,中键平移,滚轮缩放;**{{ModelTurntable}}**、**{{ModelBones}}** 和 **{{ModelResetView}}** 位于工具栏中。",
            "モードバーで **{{ViewModel}}** に切り替えます。**{{NpcBrowser}}** はゲームの全キャラクター(`.npc` ファイル)を一覧表示し、クリックするとゲームと同じモデルとスキンを読み込みます。**{{OpenGlm}}** と **{{OpenGlmPk3}}** で任意のモデルを開けます。左ボタンで周回、中ボタンでパン、ホイールでズーム。**{{ModelTurntable}}**・**{{ModelBones}}**・**{{ModelResetView}}** はツールバーにあります。")),
        ("p", T("The **{{ModelAnimations}}** list on the left shows every animation of the skeleton with its length; filter by name or by BOTH / TORSO / LEGS / FACE. Click plays it; double-click (or **{{ModelAnimToScript}}**) puts `set ( SET_ANIM_BOTH, \"…\" )` after the selected line of the open script - `SET_ANIM_UPPER` for TORSO and `SET_ANIM_LOWER` for LEGS animations.", "Die Liste **{{ModelAnimations}}** links zeigt jede Animation des Skeletts mit ihrer Länge; filtern nach Name oder nach BOTH / TORSO / LEGS / FACE. Klick spielt sie; Doppelklick (oder **{{ModelAnimToScript}}**) setzt `set ( SET_ANIM_BOTH, \"…\" )` hinter die gewählte Zeile des offenen Skripts - `SET_ANIM_UPPER` bei TORSO- und `SET_ANIM_LOWER` bei LEGS-Animationen.", "左侧的 **{{ModelAnimations}}** 列表显示骨骼的每个动画及其长度;可按名称或 BOTH / TORSO / LEGS / FACE 筛选。单击播放;双击(或 **{{ModelAnimToScript}}**)会在当前脚本所选行之后写入 `set ( SET_ANIM_BOTH, \"…\" )`——TORSO 动画使用 `SET_ANIM_UPPER`,LEGS 动画使用 `SET_ANIM_LOWER`。", "左の **{{ModelAnimations}}** 一覧は骨格のすべてのアニメーションと長さを表示し、名前や BOTH / TORSO / LEGS / FACE で絞り込めます。クリックで再生、ダブルクリック(または **{{ModelAnimToScript}}**)で開いているスクリプトの選択行の後ろに `set ( SET_ANIM_BOTH, \"…\" )` を入れます。TORSO のアニメーションは `SET_ANIM_UPPER`、LEGS は `SET_ANIM_LOWER` になります。")),
    ],
}

SEITEN = [MISSIONEN, MODELL]

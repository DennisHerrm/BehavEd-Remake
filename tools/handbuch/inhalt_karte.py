"""Karte und Szene: Karte, Entities, Wegpunkte (dazu inhalt_szene)."""
from inhalt_basis import T
import inhalt_szene

KARTE = {
    "id": "karte",
    "gruppe": "karte",
    "titel": T("The map", "Die Karte", "地图", "マップ"),
    "kurz": T(
        "The map in 3D, drawn like the game draws it - with lights, glow, fog, effects and the figures of the script.",
        "Die Karte in 3D, gezeichnet wie im Spiel - mit Licht, Glühen, Nebel, Effekten und den Figuren des Skripts.",
        "以游戏的方式绘制的三维地图——包含光照、辉光、雾、特效以及脚本中的角色。",
        "ゲームと同じように描かれる 3D マップ。ライト、グロー、霧、エフェクト、スクリプトのキャラクター付き。"),
    "bloecke": [
        ("img", "h_karte", T(
            "The map view with its toolbar on top and the placement tiles on the left.",
            "Die Kartenansicht mit ihrer Leiste oben und den Platzier-Kacheln links.",
            "地图视图:顶部为工具栏,左侧为放置图块。",
            "マップビュー。上にツールバー、左に配置タイル。"), "breit"),
        ("h2", "laden", T("Opening a map", "Eine Karte öffnen", "打开地图", "マップを開く")),
        ("p", T(
            "Switch to **{{ViewMap}}** in the mode bar. A mission brings its map along ([[missionen]]). Without a mission, **{{MapMore}}** opens a map from disk or from a `.pk3`, or loads a separate `.ent` file. **Scripts (N)** lists the scripts the map's entities point to - pick one to load its cutscene.",
            "Schalte in der Modusleiste auf **{{ViewMap}}**. Eine Mission bringt ihre Karte mit ([[missionen]]). Ohne Mission öffnet **{{MapMore}}** eine Karte von der Platte oder aus einem `.pk3` oder lädt eine eigene `.ent`-Datei. **Scripts (N)** nennt die Skripte, auf die die Entities der Karte zeigen - eines wählen lädt seine Zwischensequenz.",
            "在模式栏切换到 **{{ViewMap}}**。任务会自带地图([[missionen]])。没有任务时,**{{MapMore}}** 可从磁盘或 `.pk3` 打开地图,或加载单独的 `.ent` 文件。**Scripts (N)** 列出地图实体所引用的脚本——选择一个即可加载其过场动画。",
            "モードバーで **{{ViewMap}}** に切り替えます。ミッションはマップも一緒に開きます([[missionen]])。ミッションなしなら **{{MapMore}}** でディスクや `.pk3` からマップを開くか、別の `.ent` ファイルを読み込みます。**Scripts (N)** はマップのエンティティが参照するスクリプトの一覧で、選ぶとそのカットシーンを読み込みます。")),
        ("h2", "bewegen", T("Moving around", "Sich bewegen", "视角操作", "視点の操作")),
        ("keys", [
            (T("W A S D (mouse over the view)", "W A S D (Maus über der Ansicht)", "W A S D(鼠标位于视图上)", "W A S D(マウスがビュー上)"), T("Fly; <kbd>Q</kbd> / <kbd>E</kbd> down and up, <kbd>Shift</kbd> faster", "Fliegen; <kbd>Q</kbd> / <kbd>E</kbd> runter und hoch, <kbd>Umschalt</kbd> schneller", "飞行;<kbd>Q</kbd> / <kbd>E</kbd> 下降和上升,<kbd>Shift</kbd> 加速", "飛行。<kbd>Q</kbd> / <kbd>E</kbd> で下降・上昇、<kbd>Shift</kbd> で高速")),
            (T("Right mouse drag", "Rechte Maus ziehen", "按住右键拖动", "右ドラッグ"), T("Look around", "Umsehen", "环顾四周", "見回す")),
            (T("Middle mouse drag", "Mittlere Maus ziehen", "按住中键拖动", "中ドラッグ"), T("Pan", "Verschieben", "平移", "パン")),
            (T("Alt + middle mouse", "Alt + mittlere Maus", "Alt + 鼠标中键", "Alt + 中ボタン"), T("Orbit", "Umkreisen", "环绕", "周回")),
            (T("Wheel", "Mausrad", "滚轮", "ホイール"), T("Zoom", "Zoomen", "缩放", "ズーム")),
            (T("Double-click an entity in the list", "Doppelklick auf eine Entity in der Liste", "在列表中双击实体", "一覧でエンティティをダブルクリック"), T("Fly there", "Hinfliegen", "飞到该处", "そこへ移動")),
            (T("Ctrl + 1 ... 9", "Strg + 1 ... 9", "Ctrl + 1 ... 9", "Ctrl + 1 ... 9"), T("Save the view; <kbd>1</kbd> ... <kbd>9</kbd> jumps back (kept per map)", "Ansicht merken; <kbd>1</kbd> ... <kbd>9</kbd> springt zurück (je Karte)", "保存视角;<kbd>1</kbd> ... <kbd>9</kbd> 跳回(按地图保存)", "視点を保存。<kbd>1</kbd>〜<kbd>9</kbd> で戻る(マップごと)")),
        ]),
        ("h2", "anzeige", T("What is shown", "Was gezeigt wird", "显示内容", "表示内容")),
        ("p", T(
            "The buttons on the right of the toolbar switch parts of the picture on and off: entities, names, the figures of the script, effects, glow, dynamic light, fog and sky. **{{MapTopView}}** looks straight down like a map and cuts away everything above the cut height - drag the height box to look at another floor. **{{MapViewSettings}}** (gear) holds brightness, ambient light, detail, character shadows, **{{MapScriptFocus}}**, which dims every entity the active script does not use, and **{{GameScreen}}**: the game's aspect ratio and cg_fovAspectAdjust, which decide how wide every camera sees ([[kamera#vorschau]]).",
            "Die Knöpfe rechts in der Leiste schalten Teile des Bildes an und aus: Entities, Namen, die Figuren des Skripts, Effekte, Glühen, dynamisches Licht, Nebel und Himmel. **{{MapTopView}}** schaut wie eine Landkarte senkrecht hinunter und schneidet alles über der Schnitthöhe weg - die Höhe ziehen zeigt ein anderes Stockwerk. **{{MapViewSettings}}** (Zahnrad) enthält Helligkeit, Umgebungslicht, Detail, Figurenschatten, **{{MapScriptFocus}}**, das alle Entities abblendet, die das aktive Skript nicht benutzt, und **{{GameScreen}}**: Seitenverhältnis und cg_fovAspectAdjust des Spiels, die bestimmen, wie weit jede Kamera sieht ([[kamera#vorschau]]).",
            "工具栏右侧的按钮用于开关画面中的各部分:实体、名称、脚本角色、特效、辉光、动态光、雾和天空。**{{MapTopView}}** 像地图一样垂直向下看,并剖去剖切高度以上的一切——拖动高度框可查看其他楼层。**{{MapViewSettings}}**(齿轮)包含亮度、环境光、细节、角色阴影、**{{MapScriptFocus}}**(淡化当前脚本未使用的所有实体)以及 **{{GameScreen}}**:游戏的宽高比和 cg_fovAspectAdjust,它们决定每台摄像机能看多宽([[kamera#vorschau]])。",
            "ツールバー右のボタンで画面の要素を切り替えます。エンティティ、名前、スクリプトのキャラクター、エフェクト、グロー、動的ライト、霧、空。**{{MapTopView}}** は地図のように真上から見下ろし、切断高さより上を取り除きます。高さの欄をドラッグすると別の階を見られます。**{{MapViewSettings}}**(歯車)には明るさ、環境光、詳細度、キャラクターの影、アクティブなスクリプトが使わないエンティティを暗くする **{{MapScriptFocus}}**、そして **{{GameScreen}}**(各カメラがどれだけ広く映すかを決める、ゲームのアスペクト比と cg_fovAspectAdjust)があります([[kamera#vorschau]])。")),
        ("img", "h_ansicht_einst", T("{{MapViewSettings}}.", "{{MapViewSettings}}.", "{{MapViewSettings}}。", "{{MapViewSettings}}。")),
        ("note", T(
            "The picture is meant to look like the game: shaders, lightmaps, glow (like `r_DynamicGlow` - on the map, on brush entities and models, on the figures' glowing parts such as droid eyes, on effects like fire and sparks and around lightsaber blades), fog (`r_drawfog`), dynamic lights, lens flares, weather and saber trails, sparks and burn marks ([[figuren#schwerter]]). Weapons, medpacks, shields and ammo lie where the game drops them (items with a `targetname` appear only when used), and floor charging stations, security panels and wall cameras stand with the models the game gives them. Figures show every stage of their shader - shine, chrome (also on the weapon or saber hilt in their hand, turning with the figure as in the game), tinted parts such as the battle droids' paint, the black visors of the clone troopers, blinking and animated stages. **{{MapStats}}** shows frame rate, textures and graphics card values when something looks wrong.",
            "Das Bild soll aussehen wie im Spiel: Shader, Lightmaps, Glühen (wie `r_DynamicGlow` - auf der Karte, auf Brush-Entities und Modellen, an leuchtenden Teilen der Figuren wie Droidenaugen, an Effekten wie Feuer und Funken und um Lichtschwertklingen), Nebel (`r_drawfog`), dynamische Lichter, Lens-Flares, Wetter und Klingenspuren, Funken und Brandspuren ([[figuren#schwerter]]). Waffen, Medipacks, Schilde und Munition liegen dort, wo das Spiel sie fallen lässt (Gegenstände mit `targetname` erscheinen erst, wenn sie benutzt werden), und Ladestationen, Sicherheitspaneele und Wandkameras stehen mit den Modellen, die das Spiel ihnen gibt. Figuren zeigen alle Stufen ihres Shaders - Glanz, Chrom (auch an Waffe oder Schwertgriff in der Hand, der wie im Spiel mit der Figur dreht), getönte Teile wie die Farbe der Kampfdroiden, die schwarzen Visiere der Klonkrieger, blinkende und animierte Stufen. **{{MapStats}}** zeigt Bildrate, Texturen und Werte der Grafikkarte, wenn etwas falsch aussieht.",
            "画面力求与游戏一致:着色器、光照贴图、辉光(如 `r_DynamicGlow`——地图、笔刷实体和模型上,角色的发光部位如机器人的眼睛,火焰和火花等特效,以及光剑剑刃周围)、雾(`r_drawfog`)、动态光、镜头光晕、天气以及光剑轨迹、火花和灼痕([[figuren#schwerter]])。武器、医疗包、护盾和弹药位于游戏让它们落下的位置(带 `targetname` 的物品要在被使用后才出现),充能站、安全面板和墙上摄像机使用游戏赋予它们的模型。角色会显示其着色器的所有阶段——光泽、镀铬(也包括手中的武器或光剑剑柄,与游戏中一样随角色转动)、着色部分(例如战斗机器人的涂装)、克隆人士兵的黑色面罩以及闪烁和动画阶段。画面异常时,**{{MapStats}}** 会显示帧率、纹理和显卡参数。",
            "画面はゲームと同じに見えるように作られています。シェーダー、ライトマップ、グロー(`r_DynamicGlow` 相当。マップ、ブラシエンティティやモデル、ドロイドの目などキャラクターの発光部分、炎や火花などのエフェクト、ライトセーバーの刃の周り)、霧(`r_drawfog`)、動的ライト、レンズフレア、天候、セイバーの軌跡・火花・焼け跡([[figuren#schwerter]])。武器、メディパック、シールド、弾薬はゲームが落とす位置に置かれ(`targetname` のあるアイテムは使われたときに現れます)、充電ステーション、セキュリティパネル、壁のカメラはゲームが与えるモデルで表示されます。キャラクターはシェーダーのすべてのステージを表示します(光沢、クローム(手に持った武器やセーバーの柄にも。ゲームと同じくキャラクターと一緒に回ります)、バトル・ドロイドの塗装のような色付きの部分、クローン・トルーパーの黒いバイザー、点滅やアニメーションのステージ)。おかしく見えるときは **{{MapStats}}** でフレームレート、テクスチャ、グラフィックカードの値を確認できます。")),
    ],
}

ENTITIES = {
    "id": "entities",
    "gruppe": "karte",
    "titel": T("Entities", "Entities", "实体", "エンティティ"),
    "kurz": T(
        "Everything in a map that a script can talk to - figures, walk targets, camera marks, triggers, ships - is an entity. Select it, change it, move it, place new ones.",
        "Alles in einer Karte, das ein Skript ansprechen kann - Figuren, Laufziele, Kameramarken, Auslöser, Schiffe -, ist eine Entity. Wählen, ändern, verschieben, neue setzen.",
        "地图中脚本能够操作的一切——角色、行走目标、摄像机标记、触发器、飞船——都是实体。可以选择、修改、移动,也可以放置新的实体。",
        "スクリプトが扱えるマップ内のもの(キャラクター、歩行目標、カメラマーク、トリガー、船)はすべてエンティティです。選択・変更・移動し、新しく配置できます。"),
    "bloecke": [
        ("img", "h_entity", T(
            "A selected ship: the list on the left, the inspector below it, the gizmo in the view.",
            "Ein gewähltes Schiff: links die Liste, darunter der Inspektor, in der Ansicht das Gizmo.",
            "选中的飞船:左侧是列表,其下是检查器,视图中是操纵器。",
            "選択した船。左に一覧、その下にインスペクター、ビューにギズモ。"), "breit"),
        ("h2", "liste", T("The entity list", "Die Entity-Liste", "实体列表", "エンティティ一覧")),
        ("p", T(
            "The left column sorts the entities into groups - camera marks, walk targets, look targets, characters, script runners, effects, sound, movables, breakables, NPC waypoints. The eye hides a group in the view (<kbd>Alt</kbd>+click: show only this one), the search box filters by name or class. Click selects, double-click flies there, right-click inserts a command for it: walk target, look target, affect block, camera to this mark.",
            "Die linke Spalte ordnet die Entities in Gruppen - Kameramarken, Laufziele, Blickziele, Figuren, Skriptstarter, Effekte, Klang, Bewegliches, Zerbrechliches, NPC-Wegpunkte. Das Auge blendet eine Gruppe in der Ansicht aus (<kbd>Alt</kbd>+Klick: nur diese zeigen), das Suchfeld filtert nach Name oder Klasse. Klick wählt, Doppelklick fliegt hin, Rechtsklick fügt einen Befehl für sie ein: Laufziel, Blickziel, affect-Block, Kamera zu dieser Marke.",
            "左栏把实体分组——摄像机标记、行走目标、注视目标、角色、脚本启动器、特效、声音、可移动物体、可破坏物体、NPC 路径点。眼睛图标可在视图中隐藏某组(<kbd>Alt</kbd>+单击:只显示该组),搜索框按名称或类别筛选。单击选择,双击飞过去,右键为其插入命令:行走目标、注视目标、affect 块、摄像机移到此标记。",
            "左の列はエンティティをグループに分けます。カメラマーク、歩行目標、視線目標、キャラクター、スクリプトランナー、エフェクト、サウンド、可動物、破壊可能物、NPC ウェイポイント。目のアイコンでビューから隠し(<kbd>Alt</kbd>+クリックでそのグループだけ表示)、検索欄で名前やクラスを絞り込みます。クリックで選択、ダブルクリックでそこへ移動、右クリックでそれ向けのコマンド(歩行目標、視線目標、affect ブロック、カメラをこのマークへ)を挿入します。")),
        ("h2", "waehlen", T("Selecting in the view", "In der Ansicht wählen", "在视图中选择", "ビューで選択")),
        ("p", T(
            "Click a marker, a figure or a model to select it; <kbd>Ctrl</kbd>/<kbd>Shift</kbd>+click adds more; a box dragged over empty space selects everything inside. Hovering shows a box and the name. The transport bar under the map says how often the selected entity is used in the script, with buttons that jump to those lines.",
            "Eine Marke, eine Figur oder ein Modell anklicken wählt es; <kbd>Strg</kbd>/<kbd>Umschalt</kbd>+Klick nimmt weitere dazu; ein über leere Fläche gezogener Rahmen wählt alles darin. Darüberfahren zeigt einen Kasten und den Namen. Die Leiste unter der Karte sagt, wie oft die gewählte Entity im Skript vorkommt, mit Knöpfen, die zu diesen Zeilen springen.",
            "单击标记、角色或模型即可选择;<kbd>Ctrl</kbd>/<kbd>Shift</kbd>+单击追加;在空白处拖出框可选择框内所有对象。悬停时显示边框和名称。地图下方的控制栏会显示所选实体在脚本中被使用的次数,并提供跳转到这些行的按钮。",
            "マーカー、キャラクター、モデルをクリックして選択。<kbd>Ctrl</kbd>/<kbd>Shift</kbd>+クリックで追加、何もない所をドラッグした枠で中のものをすべて選択します。マウスを乗せると枠と名前が表示されます。マップ下のバーには、選択中のエンティティがスクリプトで何回使われているかと、その行へ移動するボタンが表示されます。")),
        ("h2", "gizmo", T("Move and Rotate", "Verschieben und Drehen", "移动与旋转", "移動と回転")),
        ("p", T("A small bar in the corner of the view switches the gizmo between **{{GizmoMove}}** and **{{GizmoRotate}}**. In Move you drag the entity over the floor or along one of the coloured arrows; the grid set in the toolbar snaps, <kbd>Ctrl</kbd> turns it off while you drag - without a grid, <kbd>Ctrl</kbd> snaps an arrow drag to 8 units. In Rotate you get rings (<kbd>Ctrl</kbd>: 15-degree steps): blue turns left and right; ships and models also get green (pitch) and red (roll); figures only have the blue ring, because the game only knows `angle` for them. <kbd>Alt</kbd>+drag copies first. Several selected entities move and turn as a group.", "Eine kleine Leiste in der Ecke der Ansicht schaltet das Gizmo zwischen **{{GizmoMove}}** und **{{GizmoRotate}}**. Im Verschieben ziehst du die Entity über den Boden oder an einem der farbigen Pfeile; das in der Leiste eingestellte Raster rastet, <kbd>Strg</kbd> schaltet es beim Ziehen aus - ohne Raster rastet <kbd>Strg</kbd> einen Pfeilzug auf 8 Einheiten. Im Drehen gibt es Ringe (<kbd>Strg</kbd>: 15-Grad-Schritte): blau dreht links und rechts; Schiffe und Modelle haben dazu grün (nicken) und rot (rollen); Figuren nur den blauen, weil das Spiel für sie nur `angle` kennt. <kbd>Alt</kbd>+Ziehen kopiert zuerst. Mehrere gewählte Entities ziehen und drehen als Gruppe.", "视图角落的小工具栏在 **{{GizmoMove}}** 和 **{{GizmoRotate}}** 之间切换操纵器。移动模式下,可在地面上拖动实体或沿彩色箭头拖动;工具栏中设置的网格会吸附,拖动时按 <kbd>Ctrl</kbd> 可暂时关闭——未设置网格时,<kbd>Ctrl</kbd> 让箭头拖动吸附到 8 单位。旋转模式下显示圆环(<kbd>Ctrl</kbd>:15 度一步):蓝色左右旋转;飞船和模型还有绿色(俯仰)和红色(翻滚);角色只有蓝色圆环,因为游戏对角色只认 `angle`。<kbd>Alt</kbd>+拖动会先复制。多个选中的实体作为整体移动和旋转。", "ビューの隅の小さなバーでギズモを **{{GizmoMove}}** と **{{GizmoRotate}}** に切り替えます。移動では床の上をドラッグするか、色付きの矢印に沿ってドラッグします。ツールバーで設定したグリッドにスナップし、ドラッグ中に <kbd>Ctrl</kbd> を押すと解除されます。グリッドがないときは <kbd>Ctrl</kbd> で矢印のドラッグが 8 ユニットにスナップします。回転ではリングが表示されます(<kbd>Ctrl</kbd>:15 度刻み)。青は左右の回転、船とモデルには緑(ピッチ)と赤(ロール)もあります。キャラクターは青だけです。ゲームはキャラクターに `angle` しか使わないからです。<kbd>Alt</kbd>+ドラッグで先にコピーします。複数選択したエンティティはまとめて動き、回転します。")),
        ("imgs", [
            ("h_gizmo_verschieben", T("{{GizmoMove}}: arrows only.", "{{GizmoMove}}: nur Pfeile.", "{{GizmoMove}}:只有箭头。", "{{GizmoMove}}:矢印のみ。")),
            ("h_gizmo_drehen", T("{{GizmoRotate}}: three rings on a ship.", "{{GizmoRotate}}: drei Ringe an einem Schiff.", "{{GizmoRotate}}:飞船上的三个圆环。", "{{GizmoRotate}}:船の 3 つのリング。")),
        ]),
        ("note", T(
            "When the timeline stands in the middle of a movement, the gizmo does not change the start - it sets a new point at that moment. See [[figuren#griffe]] and [[animation#schluessel]].",
            "Steht die Zeitleiste mitten in einer Bewegung, ändert das Gizmo nicht den Anfang - es setzt an diesem Augenblick einen neuen Punkt. Siehe [[figuren#griffe]] und [[animation#schluessel]].",
            "当时间轴位于某段运动中间时,操纵器不会修改起点,而是在该时刻设置新的路径点。参见 [[figuren#griffe]] 和 [[animation#schluessel]]。",
            "タイムラインが動きの途中にあるとき、ギズモは始点を変えず、その瞬間に新しい点を設定します。[[figuren#griffe]] と [[animation#schluessel]] を参照。")),
        ("h2", "inspektor", T("The inspector", "Der Inspektor", "检查器", "インスペクター")),
        ("p", T(
            "Under the list the inspector shows the selected entity: its class with a description from the game code, **{{EntName}}**, **{{EntOrigin}}**, **{{EntFloor}}** (how high the feet stand above the floor - red when inside it) and **{{EntAngle}}**, the spawnflags as checkboxes and all other keys as fields. Script fields have **{{EntOpenScript}}**, `NPC_type` has a search over every NPC of the game; when the class sets the type itself (`NPC_Droid_R2D2` → `r2d2`), the empty field shows it in grey, and a note says when the game chooses at random or ignores the key. The icon bar flies there, puts it **{{EntDropFloor}}**, duplicates, deletes, animates ([[animation]]) and shows **{{EntRaw}}**. Entity changes have their own undo while the mouse is over the map or the list.",
            "Unter der Liste zeigt der Inspektor die gewählte Entity: ihre Klasse mit einer Beschreibung aus dem Spielcode, **{{EntName}}**, **{{EntOrigin}}**, **{{EntFloor}}** (wie hoch die Füße über dem Boden stehen - rot, wenn darin) und **{{EntAngle}}**, die Spawnflags als Kästchen und alle übrigen Keys als Felder. Skriptfelder haben **{{EntOpenScript}}**, `NPC_type` hat eine Suche über alle NPCs des Spiels; setzt die Klasse den Typ selbst (`NPC_Droid_R2D2` → `r2d2`), zeigt das leere Feld ihn grau, und ein Hinweis sagt, wenn das Spiel würfelt oder den Key nicht beachtet. Die Symbolleiste fliegt hin, setzt sie **{{EntDropFloor}}**, dupliziert, löscht, animiert ([[animation]]) und zeigt **{{EntRaw}}**. Änderungen an Entities haben ihr eigenes Rückgängig, solange die Maus über der Karte oder der Liste ist.",
            "列表下方的检查器显示所选实体:来自游戏代码的类别说明、**{{EntName}}**、**{{EntOrigin}}**、**{{EntFloor}}**(脚离地面的高度——陷入地面时显示红色)和 **{{EntAngle}}**,生成标志以复选框显示,其余所有键以字段显示。脚本字段带有 **{{EntOpenScript}}**,`NPC_type` 可搜索游戏中的所有 NPC;若类别自行设定类型(`NPC_Droid_R2D2` → `r2d2`),空字段会以灰色显示该类型,当游戏随机选择或忽略该键时会有提示。图标栏可以飞过去、**{{EntDropFloor}}**、复制、删除、制作动画([[animation]])以及显示 **{{EntRaw}}**。鼠标位于地图或列表上时,实体的修改有独立的撤销记录。",
            "一覧の下のインスペクターは選択中のエンティティを表示します。ゲームコード由来の説明付きのクラス、**{{EntName}}**、**{{EntOrigin}}**、**{{EntFloor}}**(足が床からどれだけ上にあるか。めり込むと赤)、**{{EntAngle}}**、チェックボックスのスポーンフラグ、その他すべてのキーの欄。スクリプト欄には **{{EntOpenScript}}**、`NPC_type` にはゲームの全 NPC の検索があります。クラスが自分でタイプを決める場合(`NPC_Droid_R2D2` → `r2d2`)、空の欄にそれが灰色で表示され、ゲームがランダムに選ぶときやキーを無視するときは注記が出ます。アイコンバーでそこへ移動、**{{EntDropFloor}}**、複製、削除、アニメーション([[animation]])、**{{EntRaw}}** を使えます。マウスがマップか一覧の上にある間、エンティティの変更は専用の元に戻す履歴を使います。")),
        ("h2", "setzen", T("Placing new entities", "Neue Entities setzen", "放置新实体", "新しいエンティティの配置")),
        ("p", T(
            "The tiles on the left edge of the view place the common kinds: character, walk target, camera/look mark, look target, script runner, effect, sound, NPC waypoint, player start. Click a tile, then click in the view - or drag the tile into the view; <kbd>Shift</kbd> places several. **{{EntNew}}** above the list and a right-click in the view (**{{EntNewHere}}**) offer every class. The **Grid** button snaps to 8, 16, 32 or 64 units.",
            "Die Kacheln am linken Rand der Ansicht setzen die üblichen Arten: Figur, Laufziel, Kamera-/Blickmarke, Blickziel, Skriptstarter, Effekt, Klang, NPC-Wegpunkt, Spielerstart. Kachel anklicken, dann in die Ansicht klicken - oder die Kachel in die Ansicht ziehen; <kbd>Umschalt</kbd> setzt mehrere. **{{EntNew}}** über der Liste und ein Rechtsklick in der Ansicht (**{{EntNewHere}}**) bieten jede Klasse an. Der Knopf **Raster** rastet auf 8, 16, 32 oder 64 Einheiten.",
            "视图左边缘的图块用于放置常见类型:角色、行走目标、摄像机/注视标记、注视目标、脚本启动器、特效、声音、NPC 路径点、玩家出生点。单击图块,再在视图中单击——或把图块拖进视图;按住 <kbd>Shift</kbd> 可连续放置。列表上方的 **{{EntNew}}** 和视图中的右键菜单(**{{EntNewHere}}**)提供所有类别。**网格**按钮可吸附到 8、16、32 或 64 单位。",
            "ビュー左端のタイルでよく使う種類を配置します。キャラクター、歩行目標、カメラ/視線マーク、視線目標、スクリプトランナー、エフェクト、サウンド、NPC ウェイポイント、プレイヤースタート。タイルをクリックしてからビューをクリックするか、タイルをビューへドラッグします。<kbd>Shift</kbd> で連続配置。一覧の上の **{{EntNew}}** とビューの右クリック(**{{EntNewHere}}**)ではすべてのクラスを選べます。**グリッド**ボタンで 8・16・32・64 ユニットにスナップします。")),
    ],
}

WEGPUNKTE = {
    "id": "wegpunkte",
    "gruppe": "karte",
    "titel": T("NPC waypoints", "NPC-Wegpunkte", "NPC 路径点", "NPC ウェイポイント"),
    "kurz": T(
        "The network NPCs walk on - to far walk targets in cutscenes, and when they chase or flee. Place it by hand or let the program build it, then check it.",
        "Das Netz, auf dem NPCs laufen - zu fernen Laufzielen in Zwischensequenzen und wenn sie verfolgen oder fliehen. Von Hand setzen oder vom Programm bauen lassen, dann prüfen.",
        "NPC 行走所用的路网——在过场中前往远处的行走目标,以及追击或逃跑时。可以手动放置,也可以让程序自动生成,然后进行检查。",
        "NPC が歩くネットワーク。カットシーンで遠い目標へ歩くときや、追いかけたり逃げたりするときに使います。手で置くか自動で作り、確認します。"),
    "bloecke": [
        ("img", "h_wegpunkte", T(
            "The {{WegTitel}} window and the network on the map.",
            "Das Fenster {{WegTitel}} und das Netz auf der Karte.",
            "{{WegTitel}} 窗口及地图上的路网。",
            "{{WegTitel}} ウィンドウとマップ上のネットワーク。"), "breit"),
        ("legende", [
            (T("**{{WegZeigen}}**"), T("Shows the network on the map.")),
            (T("**{{WegSetzen}}** / **{{WegVerbinden}}**"), T("Place points by clicking the floor / connect two points.")),
            (T("**{{WegErzeugen}}** / **{{WegEntfernen}}**"), T("Spread a network over the walkable floor / remove what was created.")),
            (T("**{{WegPruefen}}**"), T("Finds what the game would reject; click a problem to fly there.")),
        ]),
        ("p", T(
            "Open it with **{{WegTitel}}** in the map toolbar. **{{WegZeigen}}** draws the paths: green set by hand (`target`), blue automatic (nav goals and combat points), orange jumps (`targetJump`).",
            "Geöffnet wird es mit **{{WegTitel}}** in der Kartenleiste. **{{WegZeigen}}** zeichnet die Wege: grün von Hand gesetzt (`target`), blau automatisch (Navgoals und Kampfpunkte), orange Sprünge (`targetJump`).",
            "通过地图工具栏中的 **{{WegTitel}}** 打开。**{{WegZeigen}}** 绘制路径:绿色为手动设置(`target`),蓝色为自动(导航点和战斗点),橙色为跳跃(`targetJump`)。",
            "マップのツールバーの **{{WegTitel}}** で開きます。**{{WegZeigen}}** で経路を描きます。緑は手動(`target`)、青は自動(ナビゴールと戦闘ポイント)、オレンジはジャンプ(`targetJump`)。")),
        ("list", [
            T("**{{WegSetzen}}**: click the floor - each new point is connected to the one before. <kbd>Shift</kbd>+click starts a new path, <kbd>Esc</kbd> ends.",
              "**{{WegSetzen}}**: auf den Boden klicken - jeder neue Punkt wird mit dem vorigen verbunden. <kbd>Umschalt</kbd>+Klick beginnt einen neuen Weg, <kbd>Esc</kbd> beendet.",
              "**{{WegSetzen}}**:单击地面——每个新点都会与上一个相连。<kbd>Shift</kbd>+单击开始新路径,<kbd>Esc</kbd> 结束。",
              "**{{WegSetzen}}**:床をクリックすると、新しい点が直前の点とつながります。<kbd>Shift</kbd>+クリックで新しい経路、<kbd>Esc</kbd> で終了。"),
            T("**{{WegVerbinden}}**: click two points to connect them (<kbd>Ctrl</kbd>: a jump); clicking two connected points disconnects them. A point has at most four connections.",
              "**{{WegVerbinden}}**: zwei Punkte anklicken verbindet sie (<kbd>Strg</kbd>: ein Sprung); zwei verbundene anklicken trennt sie. Ein Punkt hat höchstens vier Verbindungen.",
              "**{{WegVerbinden}}**:单击两个点将其连接(<kbd>Ctrl</kbd>:跳跃连接);单击两个已连接的点会断开。每个点最多四个连接。",
              "**{{WegVerbinden}}**:2 点をクリックで接続(<kbd>Ctrl</kbd>:ジャンプ)、接続済みの 2 点をクリックで切断。1 点の接続は最大 4 本です。"),
            T("**{{WegErzeugen}}** spreads waypoints over the walkable floor (**{{WegAbstand}}**: the original levels use about 230) and connects them - one undo step. **{{WegEntfernen}}** removes only those, hand-placed ones stay.",
              "**{{WegErzeugen}}** verteilt Wegpunkte über den begehbaren Boden (**{{WegAbstand}}**: die Originalkarten nutzen etwa 230) und verbindet sie - ein Rückgängig-Schritt. **{{WegEntfernen}}** entfernt nur diese, von Hand gesetzte bleiben.",
              "**{{WegErzeugen}}** 在可行走的地面上分布路径点(**{{WegAbstand}}**:原版关卡约 230)并连接——可一步撤销。**{{WegEntfernen}}** 只删除这些点,手动放置的保留。",
              "**{{WegErzeugen}}** は歩ける床にウェイポイントを配置して接続します(**{{WegAbstand}}**:オリジナルのレベルは約 230)。1 回で元に戻せます。**{{WegEntfernen}}** は自動で作ったものだけを削除し、手動のものは残ります。"),
            T("**{{WegPruefen}}** finds what the game would reject or NPCs cannot use; click a problem to fly there.",
              "**{{WegPruefen}}** findet, was das Spiel ablehnen würde oder NPCs nicht nutzen können; ein Klick auf ein Problem fliegt hin.",
              "**{{WegPruefen}}** 找出游戏会拒绝或 NPC 无法使用的部分;单击某个问题即可飞过去。",
              "**{{WegPruefen}}** はゲームが拒否するものや NPC が使えないものを見つけます。問題をクリックするとそこへ移動します。"),
        ]),
        ("note", T(
            "The cutscene preview uses this network for walks exactly like the game: far targets need a path here, otherwise the figure stands still ([[figuren#wieimspiel]]). A network needs at least four waypoints. Doors on the network count while they are open; a closed door counts only if it opens by itself for NPCs.",
            "Die Szenenvorschau nutzt dieses Netz für Läufe genau wie das Spiel: ferne Ziele brauchen hier einen Weg, sonst bleibt die Figur stehen ([[figuren#wieimspiel]]). Für ein Netz braucht es mindestens vier Wegpunkte. Türen im Netz zählen, solange sie offen sind; eine geschlossene nur, wenn sie für NPCs von selbst aufgeht.",
            "过场预览会像游戏一样使用此网络来行走:远处的目标需要在这里有路径,否则角色会站着不动([[figuren#wieimspiel]])。一个网络至少需要四个路径点。网络上的门在打开时才算数;关闭的门只有在会为 NPC 自动打开时才算。",
            "カットシーンのプレビューは、ゲームとまったく同じようにこのネットワークを歩行に使います。遠い目標にはここに経路が必要で、なければキャラクターは立ち止まります([[figuren#wieimspiel]])。ネットワークには最低 4 つのウェイポイントが必要です。ネットワーク上のドアは開いている間だけ通れます。閉じたドアは、NPC のために自動で開く場合だけ通れます。")),
    ],
}

SEITEN = [KARTE, ENTITIES] + inhalt_szene.SEITEN + [WEGPUNKTE]

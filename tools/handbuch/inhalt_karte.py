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
            "The picture is meant to look like the game: shaders, lightmaps, glow (like `r_DynamicGlow` - on the map, on brush entities and models, on the figures' glowing parts such as droid eyes, on effects like fire and sparks and around lightsaber blades), fog (`r_drawfog`), dynamic lights, lens flares, grass, bushes and rain splashes on the ground (`surfaceSprites` - blade for blade where the game puts them, swaying and fading out with distance), weather (under Movie Duels also the lava of fx_lava) and saber trails, sparks and burn marks ([[figuren#schwerter]]). Weapons, medpacks, shields and ammo lie where the game drops them (items with a `targetname` appear only when used), and floor charging stations, security panels, wall cameras, turrets, emplaced guns, E-Webs, sentry guns and the ion cannon stand with the models the game gives them (without a player they do not aim or fire; a sentry gun unfolds one second after the map starts or after it is switched on and then slowly sweeps from side to side like the game's sentry without a target, and the ion cannon fires on its own - or after a use - with its muzzle flash and recoil, at twice its model size like in the game); laser trip mines hang on their wall and, when switched on, draw their beam and hum (with the warning sound when they arm); a script or trigger switches them on or off, the beam stops at doors and other moving parts where they are at that moment, and a figure walking into the beam sets the mine off - explosion, then the mine is gone (the figure itself plays on as scripted), and like in the game the mine uses its targets, so doors, walls or a counter behind it switch, and scripts behind it run (the usescript of a counter, a script runner) - with one count for scripts and movers. Figures show every stage of their shader - shine, chrome (also on the weapon or saber hilt in their hand, turning with the figure as in the game), tinted parts such as the battle droids' paint, the black visors of the clone troopers, blinking and animated stages, moving texture stages (tcMod) such as the shimmer of the Emperor's hologram, and surfaces their shader moves with `deformVertexes` wave, bulge or move, such as the smoke around the shadow Yoda (md_yoda_shadow). Effects are drawn with every stage of their shader too - an explosion blends its two animated stages as in the game, and every stage, the first one included, takes the particle's colour and fading only where its shader asks for it (`rgbGen vertex`, `alphaGen vertex`); otherwise it shows its image as it is, like the game. Beams of fx_target_beam fire towards their target like in the game, also when a script or trigger switches them on or off (a one-shot beam fires once per use); each shot aims at where the target is at that moment - a beam on a moving train follows it -, stops at doors, moving parts and figures in its way, and a beam switched back on waits out its pause (`wait`) like the game. **{{MapStats}}** shows frame rate, textures and graphics card values when something looks wrong.",
            "Das Bild soll aussehen wie im Spiel: Shader, Lightmaps, Glühen (wie `r_DynamicGlow` - auf der Karte, auf Brush-Entities und Modellen, an leuchtenden Teilen der Figuren wie Droidenaugen, an Effekten wie Feuer und Funken und um Lichtschwertklingen), Nebel (`r_drawfog`), dynamische Lichter, Lens-Flares, Gras, Büsche und Regenspritzer auf dem Boden (`surfaceSprites` - Halm für Halm dort, wo das Spiel sie hinsetzt, wiegend und mit der Entfernung ausblendend), Wetter (unter Movie Duels auch die Lava von fx_lava) und Klingenspuren, Funken und Brandspuren ([[figuren#schwerter]]). Waffen, Medipacks, Schilde und Munition liegen dort, wo das Spiel sie fallen lässt (Gegenstände mit `targetname` erscheinen erst, wenn sie benutzt werden), und Ladestationen, Sicherheitspaneele, Wandkameras, Geschütze, Geschützstellungen, E-Webs, Wachgeschütze und die Ionenkanone stehen mit den Modellen, die das Spiel ihnen gibt (ohne Spieler zielen und feuern sie nicht; ein Wachgeschütz klappt eine Sekunde nach dem Kartenstart oder dem Einschalten auf und schwenkt danach langsam hin und her wie im Spiel ohne Ziel, und die Ionenkanone feuert von selbst - oder nach einem use - mit Mündungsfeuer und Rückstoß, wie im Spiel in doppelter Modellgröße); Laserfallen hängen an ihrer Wand und ziehen, wenn sie an sind, ihren Strahl und summen (beim Scharfwerden mit dem Warnklang); ein Skript oder Auslöser schaltet sie ein oder aus, der Strahl endet an Türen und anderen beweglichen Teilen dort, wo sie gerade sind, und läuft eine Figur in den Strahl, geht die Falle hoch - Explosion, danach ist die Falle fort (die Figur selbst spielt weiter wie im Skript), und wie im Spiel benutzt die Falle ihre Ziele: Türen, Wände oder ein Zähler dahinter schalten, und Skripte dahinter laufen (das usescript eines Zählers, ein Script-Runner) - mit einem Zählerstand für Skripte und Mover. Figuren zeigen alle Stufen ihres Shaders - Glanz, Chrom (auch an Waffe oder Schwertgriff in der Hand, der wie im Spiel mit der Figur dreht), getönte Teile wie die Farbe der Kampfdroiden, die schwarzen Visiere der Klonkrieger, blinkende und animierte Stufen, bewegte Texturstufen (tcMod) wie das Flimmern des Imperator-Hologramms und Flächen, die ihr Shader mit `deformVertexes` wave, bulge oder move bewegt, etwa der Rauch um den Schatten-Yoda (md_yoda_shadow). Auch Effekte zeigen alle Stufen ihres Shaders - eine Explosion überblendet wie im Spiel ihre zwei animierten Stufen, und jede Stufe, auch die erste, nimmt Farbe und Ausblenden des Teilchens nur, wo ihr Shader es verlangt (`rgbGen vertex`, `alphaGen vertex`); sonst zeigt sie wie im Spiel ihr Bild, wie es ist. Strahlen von fx_target_beam feuern wie im Spiel auf ihr Ziel, auch wenn ein Skript oder Auslöser sie ein- oder ausschaltet (ein Einzelschuss-Strahl feuert je Benutzung einmal); jeder Schuss zielt dorthin, wo das Ziel gerade ist - ein Strahl auf einen fahrenden Zug folgt ihm -, endet an Türen, beweglichen Teilen und Figuren im Weg, und ein wieder eingeschalteter Strahl wartet wie im Spiel seine Pause (`wait`) ab. **{{MapStats}}** zeigt Bildrate, Texturen und Werte der Grafikkarte, wenn etwas falsch aussieht.",
            "画面力求与游戏一致:着色器、光照贴图、辉光(如 `r_DynamicGlow`——地图、笔刷实体和模型上,角色的发光部位如机器人的眼睛,火焰和火花等特效,以及光剑剑刃周围)、雾(`r_drawfog`)、动态光、镜头光晕、地面上的草、灌木和雨滴溅花(`surfaceSprites`——每一株都在游戏放置的位置,随风摆动并随距离淡出)、天气(在 Movie Duels 下也包括 fx_lava 的熔岩)以及光剑轨迹、火花和灼痕([[figuren#schwerter]])。武器、医疗包、护盾和弹药位于游戏让它们落下的位置(带 `targetname` 的物品要在被使用后才出现),充能站、安全面板、墙上摄像机、炮塔、固定炮位、E-Web、哨戒炮和离子炮使用游戏赋予它们的模型(没有玩家时它们不会瞄准或开火;哨戒炮在地图开始或被打开一秒后展开,随后像游戏中没有目标时一样缓慢地左右转动,离子炮会自行开火——或在 use 之后——带有炮口火光和后坐动作,并像游戏中一样以两倍模型尺寸显示);激光绊雷挂在墙上,开启时会射出光束并发出嗡鸣(启动时伴有警告声);脚本或触发器可以将其打开或关闭,光束会停在门和其他移动部件当时所在的位置,角色走进光束时绊雷会引爆——爆炸之后绊雷消失(角色本身仍按脚本继续),并且与游戏中一样,绊雷会触发它的目标,其后的门、墙或计数器随之切换,其后的脚本(计数器的 usescript、脚本运行器)也会运行——脚本和移动部件共用同一个计数。角色会显示其着色器的所有阶段——光泽、镀铬(也包括手中的武器或光剑剑柄,与游戏中一样随角色转动)、着色部分(例如战斗机器人的涂装)、克隆人士兵的黑色面罩以及闪烁和动画阶段、移动的纹理阶段(tcMod),例如皇帝全息影像的闪烁,以及其着色器用 `deformVertexes` wave、bulge 或 move 移动的表面,例如影子尤达(md_yoda_shadow)周围的烟雾。特效同样以其着色器的所有阶段绘制——爆炸会像游戏中一样交叉淡化它的两个动画阶段;每个阶段(包括第一个)只有在其着色器要求时(`rgbGen vertex`、`alphaGen vertex`)才采用粒子的颜色和淡出,否则会像游戏中一样按原样显示其图像。fx_target_beam 的光束会像游戏中一样射向其目标,脚本或触发器将其打开或关闭时也是如此(单发光束每次使用发射一次);每一次射击都瞄准目标当时所在的位置——射向行驶中列车的光束会跟随列车——会停在路径上的门、移动部件和角色处,重新打开的光束会像游戏中一样等待其间隔(`wait`)结束。画面异常时,**{{MapStats}}** 会显示帧率、纹理和显卡参数。",
            "画面はゲームと同じに見えるように作られています。シェーダー、ライトマップ、グロー(`r_DynamicGlow` 相当。マップ、ブラシエンティティやモデル、ドロイドの目などキャラクターの発光部分、炎や火花などのエフェクト、ライトセーバーの刃の周り)、霧(`r_drawfog`)、動的ライト、レンズフレア、地面の草・茂み・雨の跳ね(`surfaceSprites`。ゲームと同じ場所に一本ずつ、風に揺れ、距離で薄れる)、天候(Movie Duels では fx_lava の溶岩も)、セイバーの軌跡・火花・焼け跡([[figuren#schwerter]])。武器、メディパック、シールド、弾薬はゲームが落とす位置に置かれ(`targetname` のあるアイテムは使われたときに現れます)、充電ステーション、セキュリティパネル、壁のカメラ、タレット、据え置き砲、E-Web、セントリーガン、イオン砲はゲームが与えるモデルで表示されます(プレイヤーがいないので照準も射撃もしません。セントリーガンはマップ開始またはオンにされてから 1 秒後に展開し、そのあとゲームで標的がないときと同じくゆっくり左右に旋回し、イオン砲は自動で、または use の後に、発射炎と反動つきで発射します。ゲームと同じくモデルは 2 倍の大きさです)。レーザートリップマインは壁に付き、オンのときはビームを出してハム音を鳴らします(起動時には警告音)。スクリプトやトリガーでオン・オフでき、ビームはドアなどの動く部分のその時点の位置で止まり、キャラクターがビームに入るとマインが爆発して消えます(キャラクター自身はスクリプトどおりに動き続けます)。ゲームと同じく、マインはそのターゲットを使うので、その先のドアや壁、カウンターが切り替わり、その先のスクリプト(カウンターの usescript やスクリプトランナー)も実行されます。カウントはスクリプトと可動部で共通です。キャラクターはシェーダーのすべてのステージを表示します(光沢、クローム(手に持った武器やセーバーの柄にも。ゲームと同じくキャラクターと一緒に回ります)、バトル・ドロイドの塗装のような色付きの部分、クローン・トルーパーの黒いバイザー、点滅やアニメーションのステージ、皇帝のホログラムのゆらめきのように動くテクスチャステージ(tcMod)、シャドウ・ヨーダ(md_yoda_shadow)の周りの煙のようにシェーダーが `deformVertexes` の wave・bulge・move で動かす面)。エフェクトもシェーダーのすべてのステージで描かれ、爆発はゲームと同じく 2 つのアニメーションステージをクロスフェードします。最初のステージを含む各ステージは、シェーダーが求める場合(`rgbGen vertex`、`alphaGen vertex`)だけパーティクルの色とフェードを使い、それ以外はゲームと同じく画像をそのまま表示します。fx_target_beam のビームはゲームと同じくターゲットに向けて発射されます。スクリプトやトリガーでオン・オフしたときも同様です(単発のビームは使用ごとに 1 回発射)。各発射はその時点のターゲットの位置を狙い(走行中の列車へのビームは列車を追います)、途中のドア、動く部分、キャラクターで止まります。再びオンにされたビームはゲームと同じく間隔(`wait`)が過ぎるまで待ちます。おかしく見えるときは **{{MapStats}}** でフレームレート、テクスチャ、グラフィックカードの値を確認できます。")),
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

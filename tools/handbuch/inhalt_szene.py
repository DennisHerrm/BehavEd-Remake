"""Karte und Szene, zweiter Teil: Kamera, Zeitleiste, Figuren, Animation."""
from inhalt_basis import T

KAMERA = {
    "id": "kamera",
    "gruppe": "karte",
    "titel": T("Camera", "Kamera", "摄像机", "カメラ"),
    "kurz": T(
        "Every camera MOVE and PAN of the script is a shot you can see, grab and fly - the script is rewritten as you drag.",
        "Jedes camera MOVE und PAN des Skripts ist eine Einstellung, die du siehst, greifst und fliegst - das Skript wird beim Ziehen neu geschrieben.",
        "脚本中的每个 camera MOVE 和 PAN 都是一个可以看到、抓取和驾驶的镜头——拖动时脚本会随之改写。",
        "スクリプトの camera MOVE と PAN はすべて、見て・つかんで・飛ばせるショットです。ドラッグするとスクリプトが書き換わります。"),
    "bloecke": [
        ("img", "h_kamera", T(
            "A selected camera: its path on the map, the gizmo, the toolbar on top and the list of shots on the left.",
            "Eine gewählte Kamera: ihre Bahn auf der Karte, das Gizmo, die Leiste oben und links die Liste der Einstellungen.",
            "选中的摄像机:地图上的路径、操纵器、顶部工具栏以及左侧的镜头列表。",
            "選択したカメラ。マップ上のパス、ギズモ、上部のツールバー、左側のショット一覧。"), "breit"),
        ("h2", "einstellungen", T("Shots", "Einstellungen", "镜头", "ショット")),
        ("p", T(
            "In ICARUS a shot is `camera ( MOVE, <position>, <time> )` followed by `camera ( PAN, <angles>, <time> )`. BehavEd-Remake reads them in order and draws each as a camera symbol on the map. Click the camera symbol to select the camera; the list **{{CamShots}}** on the left shows every shot with its time and travel time (`moves 6.0 s`). Click a line to jump there.",
            "In ICARUS ist eine Einstellung `camera ( MOVE, <Ort>, <Zeit> )` gefolgt von `camera ( PAN, <Winkel>, <Zeit> )`. BehavEd-Remake liest sie der Reihe nach und zeichnet jede als Kamerasymbol auf die Karte. Ein Klick auf das Symbol wählt die Kamera; die Liste **{{CamShots}}** links zeigt jede Einstellung mit ihrer Zeit und Fahrzeit (`fährt 6,0 s`). Ein Klick auf eine Zeile springt dorthin.",
            "在 ICARUS 中,一个镜头就是 `camera ( MOVE, <位置>, <时间> )` 后接 `camera ( PAN, <角度>, <时间> )`。BehavEd-Remake 按顺序读取它们,并在地图上把每个镜头画成摄像机图标。单击图标选择摄像机;左侧的 **{{CamShots}}** 列表显示每个镜头的时间和移动时间(`移动 6.0 秒`)。单击某行即可跳转。",
            "ICARUS ではショットは `camera ( MOVE, <位置>, <時間> )` と、それに続く `camera ( PAN, <角度>, <時間> )` です。BehavEd-Remake はそれを順に読み、マップ上にカメラ記号として描きます。記号をクリックするとカメラが選択され、左の **{{CamShots}}** 一覧に各ショットの時刻と移動時間(`移動 6.0 秒`)が表示されます。行をクリックするとそこへ移動します。")),
        ("list", [
            T("**{{CamPrevShot}} / {{CamNextShot}}** step through the shots; **{{CamFlyToShot}}** puts your view exactly where the selected shot stands and looks.", "**{{CamPrevShot}} / {{CamNextShot}}** blättern durch die Einstellungen; **{{CamFlyToShot}}** stellt deine Ansicht genau dorthin, wo die gewählte Einstellung steht und hinschaut.", "**{{CamPrevShot}} / {{CamNextShot}}** 在镜头间切换;**{{CamFlyToShot}}** 把你的视角精确移到所选镜头的位置和朝向。", "**{{CamPrevShot}} / {{CamNextShot}}** でショットを順に切り替え、**{{CamFlyToShot}}** で視点を選択中のショットの位置と向きにぴったり合わせます。"),
            T("**{{CamNewShot}}** adds a MOVE and a PAN at your current view, after the selected shot. **{{CamSetToView}}** moves the selected shot to where you are looking.",
              "**{{CamNewShot}}** fügt an deiner jetzigen Ansicht ein MOVE und ein PAN hinter der gewählten Einstellung ein. **{{CamSetToView}}** setzt die gewählte Einstellung dorthin, wohin du gerade schaust.",
              "**{{CamNewShot}}** 在当前视角、所选镜头之后添加 MOVE 和 PAN。**{{CamSetToView}}** 把所选镜头移到你当前注视的位置。",
              "**{{CamNewShot}}** は現在の視点に MOVE と PAN を、選択中のショットの後ろに追加します。**{{CamSetToView}}** は選択中のショットを今見ている位置に移します。"),
            T("**{{CamDeleteShot}}** removes it. Under **{{CamSelectedShot}}** you type exact values: {{CamPosition}}, {{CamAngles}} and {{CamDuration}} (0 = a cut).",
              "**{{CamDeleteShot}}** entfernt sie. Unter **{{CamSelectedShot}}** tippst du genaue Werte: {{CamPosition}}, {{CamAngles}} und {{CamDuration}} (0 = Schnitt).",
              "**{{CamDeleteShot}}** 删除镜头。在 **{{CamSelectedShot}}** 下可输入精确数值:{{CamPosition}}、{{CamAngles}} 和 {{CamDuration}}(0 = 硬切)。",
              "**{{CamDeleteShot}}** で削除します。**{{CamSelectedShot}}** では {{CamPosition}}・{{CamAngles}}・{{CamDuration}}(0 = カット)を正確に入力できます。"),
        ]),
        ("h2", "gizmo", T("Moving and turning a shot", "Eine Einstellung verschieben und drehen", "移动和旋转镜头", "ショットの移動と回転")),
        ("p", T(
            "The toolbar above the view switches the gizmo: **{{GizmoMove}}** shows three arrows and a box in the middle - drag an arrow to move along one axis, the box to move in the view plane. **{{GizmoRotate}}** shows three rings for pitch, yaw and roll. **{{GizmoSpaceWorld}} / {{GizmoSpaceLocal}}** decides whether the arrows follow the map or the camera. Letting go writes the new values into the script - one undo step.",
            "Die Leiste über der Ansicht schaltet das Gizmo um: **{{GizmoMove}}** zeigt drei Pfeile und ein Kästchen in der Mitte - einen Pfeil ziehen verschiebt entlang einer Achse, das Kästchen in der Bildebene. **{{GizmoRotate}}** zeigt drei Ringe für Nicken, Gieren und Rollen. **{{GizmoSpaceWorld}} / {{GizmoSpaceLocal}}** entscheidet, ob die Pfeile der Karte oder der Kamera folgen. Beim Loslassen stehen die neuen Werte im Skript - ein Rückgängig-Schritt.",
            "视图上方的工具栏用于切换操纵器:**{{GizmoMove}}** 显示三个箭头和中间的方块——拖动箭头沿某一轴移动,拖动方块在视平面内移动。**{{GizmoRotate}}** 显示俯仰、偏航和翻滚三个圆环。**{{GizmoSpaceWorld}} / {{GizmoSpaceLocal}}** 决定箭头跟随地图还是摄像机。松开鼠标时新数值写入脚本——可一步撤销。",
            "ビュー上部のツールバーでギズモを切り替えます。**{{GizmoMove}}** は 3 本の矢印と中央の箱を表示し、矢印のドラッグで 1 軸に沿って、箱のドラッグで画面の平面内を移動します。**{{GizmoRotate}}** はピッチ・ヨー・ロールの 3 つのリングを表示します。**{{GizmoSpaceWorld}} / {{GizmoSpaceLocal}}** で矢印がマップとカメラのどちらに従うかを決めます。離すと新しい値がスクリプトに書かれます(1 回の元に戻す)。")),
        ("imgs", [
            ("h_kamera_verschieben", T("{{GizmoMove}}: arrows and the centre box.", "{{GizmoMove}}: Pfeile und das Kästchen in der Mitte.", "{{GizmoMove}}:箭头和中间的方块。", "{{GizmoMove}}:矢印と中央の箱。")),
            ("h_kamera_drehen", T("{{GizmoRotate}}: three rings.", "{{GizmoRotate}}: drei Ringe.", "{{GizmoRotate}}:三个圆环。", "{{GizmoRotate}}:3 つのリング。")),
        ]),
        ("p", T("If the timeline stands **between** two keys when you grab the gizmo, a new key is created at that moment instead of changing the start - like the auto-key of animation programs. While piloting, <kbd>Enter</kbd> sets such a key too ([[kamera#steuern]]).", "Steht die Zeitleiste beim Anfassen **zwischen** zwei Schlüsseln, entsteht an dieser Stelle ein neuer Schlüssel, statt den Anfang zu ändern - wie der Auto-Key in Animationsprogrammen. Beim Steuern setzt <kbd>Enter</kbd> ebenso einen Schlüssel ([[kamera#steuern]]).", "如果抓取操纵器时时间轴位于两个关键帧**之间**,会在该时刻新建关键帧,而不是修改起点——就像动画软件的自动关键帧。驾驶时按 <kbd>Enter</kbd> 也会设置这样的关键帧([[kamera#steuern]])。", "ギズモをつかんだときタイムラインが 2 つのキーの**間**にあれば、始点を変えるのではなく、その時刻に新しいキーが作られます。アニメーションソフトのオートキーと同じです。操縦中は <kbd>Enter</kbd> でも同じようにキーを設定できます([[kamera#steuern]])。")),
        ("h2", "bahnen", T("Camera paths", "Kamerabahnen", "摄像机路径", "カメラパス")),
        ("p", T(
            "The selected camera draws its path: already flown parts in yellow, the rest in grey. With many shots that gets crowded - **{{CamPathActive}}** shows only the move into the current shot and on to the next, **{{CamPathAll}}** shows every move. Both buttons sit in the camera panel.",
            "Die gewählte Kamera zeichnet ihre Bahn: schon geflogene Stücke gelb, der Rest grau. Bei vielen Einstellungen wird das unübersichtlich - **{{CamPathActive}}** zeigt nur die Fahrt in die jetzige Einstellung und weiter zur nächsten, **{{CamPathAll}}** zeigt jede Fahrt. Beide Knöpfe stehen im Kamerafeld.",
            "选中的摄像机会绘制其路径:已飞过的部分为黄色,其余为灰色。镜头很多时会显得杂乱——**{{CamPathActive}}** 只显示进入当前镜头和通往下一个镜头的移动,**{{CamPathAll}}** 显示所有移动。两个按钮都在摄像机面板中。",
            "選択したカメラはパスを描きます。通過済みの部分は黄色、残りは灰色です。ショットが多いと見づらくなるので、**{{CamPathActive}}** は現在のショットへの移動と次への移動だけを、**{{CamPathAll}}** はすべての移動を表示します。どちらもカメラパネルにあります。")),
        ("h2", "steuern", T("Fly a shot yourself (Pilot)", "Eine Einstellung selbst fliegen (Steuern)", "亲自驾驶镜头", "ショットを自分で飛ばす(操縦)")),
        ("p", T(
            "**{{CamPilot}}** puts you into the shot: hold the right mouse button to look around, <kbd>W</kbd> <kbd>A</kbd> <kbd>S</kbd> <kbd>D</kbd> to move, <kbd>Q</kbd> <kbd>E</kbd> for down and up. **{{CamPilotApply}}** writes the view into the script, <kbd>Esc</kbd> cancels. While piloting, move the timeline and press <kbd>Enter</kbd> (**{{CamPilotKey}}**) to set key after key along the way.",
            "**{{CamPilot}}** setzt dich in die Einstellung: rechte Maustaste halten zum Umsehen, <kbd>W</kbd> <kbd>A</kbd> <kbd>S</kbd> <kbd>D</kbd> zum Bewegen, <kbd>Q</kbd> <kbd>E</kbd> für runter und hoch. **{{CamPilotApply}}** schreibt die Ansicht ins Skript, <kbd>Esc</kbd> bricht ab. Während du steuerst, die Zeitleiste weiterstellen und <kbd>Enter</kbd> drücken (**{{CamPilotKey}}**) setzt Schlüssel um Schlüssel entlang des Wegs.",
            "**{{CamPilot}}** 让你进入镜头:按住鼠标右键环顾四周,<kbd>W</kbd> <kbd>A</kbd> <kbd>S</kbd> <kbd>D</kbd> 移动,<kbd>Q</kbd> <kbd>E</kbd> 下降和上升。**{{CamPilotApply}}** 把视角写入脚本,<kbd>Esc</kbd> 取消。驾驶时移动时间轴并按 <kbd>Enter</kbd>(**{{CamPilotKey}}**),即可沿途逐个设置关键帧。",
            "**{{CamPilot}}** でショットの中に入ります。右ボタンを押したまま見回し、<kbd>W</kbd> <kbd>A</kbd> <kbd>S</kbd> <kbd>D</kbd> で移動、<kbd>Q</kbd> <kbd>E</kbd> で下降・上昇します。**{{CamPilotApply}}** で視点をスクリプトに書き、<kbd>Esc</kbd> で取り消します。操縦中にタイムラインを進めて <kbd>Enter</kbd>(**{{CamPilotKey}}**)を押すと、道に沿ってキーを次々に設定できます。")),
        ("h2", "vorschau", T("Preview and looking through", "Vorschau und Durchschauen", "预览与透过镜头观看", "プレビューとカメラ視点")),
        ("p", T("**{{CamPreview}}** (in the bar of the selected camera, top left in the view) shows a small picture of what the camera sees, bottom right in the view; it follows the gizmo while you drag. Click the picture to look through the camera full size; while you look through it, playing the timeline shows the cutscene as the game will. **{{MapThroughCam}}** in the same bar does this directly.", "**{{CamPreview}}** (in der Leiste der gewählten Kamera, oben links in der Ansicht) zeigt unten rechts ein kleines Bild aus Sicht der Kamera; es folgt dem Gizmo beim Ziehen. Ein Klick aufs Bild schaut groß durch die Kamera; dabei zeigt das Abspielen der Zeitleiste die Zwischensequenz so, wie das Spiel sie zeigen wird. **{{MapThroughCam}}** in derselben Leiste tut das direkt.", "**{{CamPreview}}**(位于所选摄像机的工具栏中,视图左上角)在视图右下角显示摄像机看到的小画面;拖动时会跟随操纵器。单击画面即可全屏透过摄像机观看;此时播放时间轴,就能看到与游戏中一致的过场动画。同一工具栏中的 **{{MapThroughCam}}** 可直接切换到摄像机视角。", "**{{CamPreview}}**(選択したカメラのバー、ビュー左上)は、カメラから見た小さな画像をビュー右下に表示し、ドラッグ中もギズモに追従します。画像をクリックするとカメラ視点で大きく表示され、そのままタイムラインを再生すると、ゲームと同じようにカットシーンが見られます。同じバーの **{{MapThroughCam}}** でも直接カメラ視点にできます。")),
        ("anim", "a_kamera", T(
            "Looking through the camera while the timeline plays.",
            "Durch die Kamera schauen, während die Zeitleiste läuft.",
            "播放时间轴时透过摄像机观看。",
            "タイムライン再生中にカメラ視点で見る。")),
        ("note", T(
            "Some shots stand on a `ref_tag` or look at one. Then the panel says so, and moving the shot moves that ref_tag in the map - the script itself only names it.",
            "Manche Einstellungen stehen auf einem `ref_tag` oder schauen auf einen. Dann sagt es das Feld, und das Verschieben der Einstellung verschiebt diesen ref_tag in der Karte - das Skript nennt ihn nur.",
            "有些镜头位于某个 `ref_tag` 上或注视某个 `ref_tag`。此时面板会提示,移动镜头就会移动地图中的该 ref_tag——脚本只引用它的名字。",
            "`ref_tag` の上に置かれたショットや、それを注視するショットもあります。その場合はパネルに表示され、ショットを動かすとマップ上の ref_tag が動きます。スクリプトは名前を参照しているだけです。")),
    ],
}

ZEITLEISTE = {
    "id": "zeitleiste",
    "gruppe": "karte",
    "titel": T("Timeline", "Zeitleiste", "时间轴", "タイムライン"),
    "kurz": T(
        "The timeline plays the scene and shows when every command happens. Drag a block and the script changes with it.",
        "Die Zeitleiste spielt die Szene ab und zeigt, wann jeder Befehl geschieht. Einen Block ziehen ändert das Skript mit.",
        "时间轴播放场景并显示每个命令何时发生。拖动一个块,脚本也会随之改变。",
        "タイムラインはシーンを再生し、各コマンドがいつ起こるかを示します。ブロックをドラッグするとスクリプトも変わります。"),
    "bloecke": [
        ("img", "h_zeitleiste", T(
            "The timeline: camera rows on top, one row per figure, sub-rows by kind, the playhead in orange.",
            "Die Zeitleiste: oben die Kamerazeilen, je Figur eine Zeile, Unterzeilen nach Art, der Zeiger in Orange.",
            "时间轴:上方是摄像机行,每个角色一行,按类型分为子行,橙色为播放头。",
            "タイムライン。上にカメラの行、キャラクターごとに 1 行、種類別のサブ行、オレンジ色が再生位置。"), "breit"),
        ("h2", "abspielen", T("Playing", "Abspielen", "播放", "再生")),
        ("p", T(
            "The play button (▶) under the map starts and stops. Click or drag on the ruler to set the time. The speed field slows the scene down (sound only plays at 1x), **{{TlLoop}}** repeats it. The wheel over the ruler zooms, <kbd>Ctrl</kbd>+wheel over the tracks too; **{{TlFit}}** shows everything, the middle mouse button drags. Frames or seconds - the unit button switches.",
            "Der Abspielknopf (▶) unter der Karte startet und hält an. Auf das Lineal klicken oder ziehen stellt die Zeit. Das Tempofeld verlangsamt die Szene (Ton nur bei 1x), **{{TlLoop}}** wiederholt sie. Das Mausrad über dem Lineal zoomt, <kbd>Strg</kbd>+Rad über den Spuren ebenso; **{{TlFit}}** zeigt alles, die mittlere Maustaste verschiebt. Bilder oder Sekunden - der Einheitenknopf schaltet um.",
            "地图下方的播放按钮(▶)用于开始和停止播放。在标尺上单击或拖动可设置时间。速度框可放慢场景(声音仅在 1x 时播放),**{{TlLoop}}** 循环播放。在标尺上滚动滚轮可缩放,在轨道上按 <kbd>Ctrl</kbd>+滚轮也可缩放;**{{TlFit}}** 显示全部,鼠标中键拖动视图。单位按钮可在帧与秒之间切换。",
            "マップ下の再生ボタン(▶)で再生・停止します。ルーラーをクリックまたはドラッグして時刻を設定します。速度欄でシーンを遅くでき(音は 1x のときのみ)、**{{TlLoop}}** で繰り返します。ルーラー上のホイールでズーム、トラック上では <kbd>Ctrl</kbd>+ホイール。**{{TlFit}}** で全体表示、中ボタンでスクロール。単位ボタンでフレームと秒を切り替えます。")),
        ("h2", "zeilen", T("Rows and categories", "Zeilen und Arten", "行与类别", "行と種類")),
        ("p", T(
            "The **{{TlKamera}}** row holds the camera commands (move, pan, zoom, fade). Below, every figure or mover that the script touches has its own row. Click the arrow at its name to open it: the commands are sorted into **{{TlKatBewegung}}**, **{{TlKatAnimation}}**, **{{TlKatBlick}}**, **{{TlKatTon}}**, **{{TlKatZustand}}** and **{{TlKatAblauf}}**. Rows where nothing happens after the start are hidden; tick **{{TlIdleTracks}}** to show them.",
            "Die Zeile **{{TlKamera}}** enthält die Kamerabefehle (Fahrt, Schwenk, Zoom, Blende). Darunter hat jede Figur und jeder Mover, den das Skript anfasst, eine eigene Zeile. Ein Klick auf den Pfeil am Namen klappt sie auf: die Befehle sind sortiert nach **{{TlKatBewegung}}**, **{{TlKatAnimation}}**, **{{TlKatBlick}}**, **{{TlKatTon}}**, **{{TlKatZustand}}** und **{{TlKatAblauf}}**. Zeilen, in denen nach dem Start nichts geschieht, sind ausgeblendet; **{{TlIdleTracks}}** zeigt sie.",
            "**{{TlKamera}}** 行包含摄像机命令(移动、平移、缩放、淡入淡出)。下方,脚本涉及的每个角色或移动物体各占一行。单击名称旁的箭头展开:命令按 **{{TlKatBewegung}}**、**{{TlKatAnimation}}**、**{{TlKatBlick}}**、**{{TlKatTon}}**、**{{TlKatZustand}}** 和 **{{TlKatAblauf}}** 分类。开始后没有任何事件的行会被隐藏;勾选 **{{TlIdleTracks}}** 即可显示。",
            "**{{TlKamera}}** 行にはカメラのコマンド(移動・パン・ズーム・フェード)が入ります。その下に、スクリプトが扱うキャラクターや可動物ごとの行があります。名前の矢印をクリックすると開き、コマンドが **{{TlKatBewegung}}**・**{{TlKatAnimation}}**・**{{TlKatBlick}}**・**{{TlKatTon}}**・**{{TlKatZustand}}**・**{{TlKatAblauf}}** に分類されます。開始後に何も起こらない行は隠れています。**{{TlIdleTracks}}** で表示できます。")),
        ("p", T(
            "The `wait` commands are hidden by default - they still place everything in time, and moving a block adjusts them. **{{TlWaits}}** shows them as blocks of their own.",
            "Die `wait`-Befehle sind zunächst ausgeblendet - sie setzen trotzdem alles in die Zeit, und das Verschieben eines Blocks passt sie an. **{{TlWaits}}** zeigt sie als eigene Blöcke.",
            "`wait` 命令默认隐藏——它们仍然决定一切的时间位置,移动块时会自动调整它们。**{{TlWaits}}** 将它们显示为独立的块。",
            "`wait` コマンドは最初は非表示です。それでもすべてを時間に配置しており、ブロックを動かすと自動で調整されます。**{{TlWaits}}** で独立したブロックとして表示します。")),
        ("h2", "ziehen", T("Changing time by dragging", "Zeit durch Ziehen ändern", "拖动以修改时间", "ドラッグで時間を変える")),
        ("p", T(
            "Drag a block left or right and the `wait` before it changes - the command happens earlier or later, everything after it moves along. A camera block can be dragged at its end to change its duration. Walks and flights appear as bands at the bottom of a figure's row; drag them in time the same way, right-click a walk to switch between walking and running.",
            "Einen Block nach links oder rechts ziehen ändert das `wait` davor - der Befehl kommt früher oder später, alles danach rückt mit. Einen Kamerablock an seinem Ende ziehen ändert seine Dauer. Laufen und Fliegen erscheinen als Bänder am unteren Rand der Zeile einer Figur; sie lassen sich genauso in der Zeit ziehen, ein Rechtsklick auf einen Lauf schaltet zwischen Gehen und Rennen.",
            "向左或向右拖动块会改变它前面的 `wait`——命令提前或推迟发生,其后的一切随之移动。拖动摄像机块的末端可改变其时长。行走和飞行显示为角色行底部的条带;同样可以在时间上拖动,右键单击行走条带可在步行和奔跑之间切换。",
            "ブロックを左右にドラッグすると、その前の `wait` が変わり、コマンドが早まったり遅れたりして、その後ろのすべてが一緒に動きます。カメラのブロックは端をドラッグすると長さが変わります。歩行と飛行はキャラクターの行の下端に帯として表示され、同じように時間方向にドラッグでき、歩行を右クリックすると歩く/走るを切り替えられます。")),
        ("p", T(
            "Clicking a block selects its line in the script, and the map jumps to that moment - timeline, script and map always show the same thing.",
            "Ein Klick auf einen Block wählt seine Zeile im Skript, und die Karte springt zu diesem Augenblick - Zeitleiste, Skript und Karte zeigen immer dasselbe.",
            "单击块会在脚本中选中对应行,地图也会跳到该时刻——时间轴、脚本和地图始终显示同一内容。",
            "ブロックをクリックするとスクリプトの該当行が選択され、マップもその瞬間へ移動します。タイムライン・スクリプト・マップは常に同じものを示します。")),
        ("note", T(
            "Times come from a re-creation of the game's ICARUS engine, including its quirks: a `wait` ends one game frame (50 ms) later than written, and a `dowait` waits for the task it names. When a time cannot be known (a `waitsignal`), the timeline says so instead of guessing.",
            "Die Zeiten kommen aus einem Nachbau der ICARUS-Maschine des Spiels, samt ihrer Eigenheiten: ein `wait` endet ein Spielbild (50 ms) später als geschrieben, und ein `dowait` wartet auf die genannte Aufgabe. Lässt sich eine Zeit nicht wissen (ein `waitsignal`), sagt die Zeitleiste das, statt zu raten.",
            "时间来自对游戏 ICARUS 引擎的重现,包括其特性:`wait` 比写入值晚一个游戏帧(50 毫秒)结束,`dowait` 会等待它指定的任务。无法确定时间时(例如 `waitsignal`),时间轴会明确提示,而不是猜测。",
            "時間はゲームの ICARUS エンジンの再現から得られ、その癖も含みます。`wait` は書かれた値より 1 ゲームフレーム(50 ms)遅れて終わり、`dowait` は指定したタスクを待ちます。時間が分からない場合(`waitsignal`)は、推測せずにそう表示します。")),
    ],
}

FIGUREN = {
    "id": "figuren",
    "gruppe": "karte",
    "titel": T("Figures and paths", "Figuren und Wege", "角色与路径", "キャラクターと道"),
    "kurz": T(
        "Figures walk to their navgoals and ships fly their moves - you see the whole path and can change it on the map.",
        "Figuren laufen zu ihren Navgoals und Schiffe fliegen ihre moves - du siehst den ganzen Weg und kannst ihn auf der Karte ändern.",
        "角色走向它们的导航点,飞船按 move 飞行——你能看到完整路径,并直接在地图上修改。",
        "キャラクターはナビゴールへ歩き、船は move で飛びます。道全体が見え、マップ上で変更できます。"),
    "bloecke": [
        ("img", "h_figur_bahn", T(
            "A walking figure: the path ahead in grey, the part already walked in yellow, the handles at start and end.",
            "Eine laufende Figur: der Weg voraus in Grau, das schon gelaufene Stück in Gelb, die Griffe an Anfang und Ende.",
            "行走中的角色:前方路径为灰色,已走过的部分为黄色,起点和终点有控制柄。",
            "歩いているキャラクター。前方の道は灰色、歩き終えた部分は黄色、始点と終点にハンドル。"), "breit"),
        ("h2", "folgen", T("Selecting figures and ships", "Figuren und Schiffe wählen", "选择角色和飞船", "キャラクターと船の選択")),
        ("p", T(
            "Click a figure or a ship in the view to select its entity - the spawner of the figure, the brush entity of the ship. Marker and gizmo follow the model while it moves, and an outline shows what is selected. The arrow at the figure points where the model looks.",
            "Eine Figur oder ein Schiff in der Ansicht anklicken wählt ihre Entity - den Spawner der Figur, die Brush-Entity des Schiffs. Marke und Gizmo folgen dem Modell, während es sich bewegt, und ein Umriss zeigt, was gewählt ist. Der Pfeil an der Figur zeigt, wohin das Modell schaut.",
            "在视图中单击角色或飞船即可选中其实体——角色的生成器、飞船的笔刷实体。模型移动时标记和操纵器会跟随,轮廓线显示选中的对象。角色身上的箭头指示模型的朝向。",
            "ビューでキャラクターや船をクリックすると、そのエンティティ(キャラクターのスポナー、船のブラシエンティティ)が選択されます。モデルが動いてもマーカーとギズモが追従し、輪郭線で選択中のものが分かります。キャラクターの矢印はモデルの向きを示します。")),
        ("h2", "griffe", T("Start, end and new points", "Anfang, Ende und neue Punkte", "起点、终点与新路径点", "始点・終点・新しい点")),
        ("list", [
            T("**Drag the end handle** to move the walk target (the `waypoint_navgoal`) or the end of a flight.",
              "**Den Endgriff ziehen** verschiebt das Laufziel (den `waypoint_navgoal`) oder das Ende eines Flugs.",
              "**拖动终点控制柄**可移动行走目标(`waypoint_navgoal`)或飞行终点。",
              "**終点のハンドルをドラッグ**すると、歩行の目標(`waypoint_navgoal`)や飛行の終点が動きます。"),
            T("**Drag the start handle** to move only the start, not the whole line.",
              "**Den Startgriff ziehen** verschiebt nur den Anfang, nicht die ganze Linie.",
              "**拖动起点控制柄**只移动起点,而不是整条线。",
              "**始点のハンドルをドラッグ**すると、線全体ではなく始点だけが動きます。"),
            T("**Grab the figure in the middle of its walk** (timeline between start and arrival) and drag: a new point is created at that moment - a new navgoal for figures, a split `move` for ships.",
              "**Die Figur mitten im Lauf greifen** (Zeitleiste zwischen Start und Ankunft) und ziehen: an diesem Augenblick entsteht ein neuer Punkt - ein neuer Navgoal bei Figuren, ein geteiltes `move` bei Schiffen.",
              "**在行走途中抓取角色**(时间轴位于出发与到达之间)并拖动:会在该时刻创建新的路径点——角色为新的导航点,飞船为拆分后的 `move`。",
              "**歩いている途中のキャラクターをつかんで**(タイムラインが出発と到着の間)ドラッグすると、その瞬間に新しい点が作られます。キャラクターは新しいナビゴール、船は分割された `move` になります。"),
            T("**Rotate** works the same way: in the middle of a flight the rings create a new point with the new angles; for a figure they write `set ( SET_ANGLES )` at that time - while walking it applies when the figure arrives, because the game turns walking figures toward their direction.",
              "**Drehen** geht genauso: mitten in einem Flug legen die Ringe einen neuen Punkt mit den neuen Winkeln an; bei einer Figur schreiben sie `set ( SET_ANGLES )` zu dieser Zeit - im Laufen gilt das beim Ankommen, weil das Spiel laufende Figuren in ihre Richtung dreht.",
              "**旋转**同理:在飞行途中,圆环会以新角度创建新的路径点;对于角色,会在该时刻写入 `set ( SET_ANGLES )`——行走中则在到达时生效,因为游戏会让行走的角色面向行进方向。",
              "**回転**も同様です。飛行の途中ではリングが新しい角度の点を作り、キャラクターではその時刻に `set ( SET_ANGLES )` を書きます。歩行中は到着時に適用されます。ゲームは歩くキャラクターを進行方向に向けるからです。"),
        ]),
        ("h2", "gangart", T("Walk or run, shared targets", "Gehen oder rennen, geteilte Ziele", "步行或奔跑、共享目标", "歩く/走る、共有目標")),
        ("p", T(
            "Right-click the end handle of a walk (or its band in the timeline) for its menu:",
            "Ein Rechtsklick auf den Endgriff eines Laufs (oder sein Band in der Zeitleiste) öffnet sein Menü:",
            "右键单击行走路径的终点控制柄(或时间轴中的条带)打开菜单:",
            "歩行の終点ハンドル(またはタイムラインの帯)を右クリックするとメニューが開きます。")),
        ("list", [
            T("**{{BahnGehen}}** / **{{BahnRennen}}** - writes `SET_WALKING` or `SET_RUNNING` before the navgoal.",
              "**{{BahnGehen}}** / **{{BahnRennen}}** - schreibt `SET_WALKING` bzw. `SET_RUNNING` vor den Navgoal.",
              "**{{BahnGehen}}** / **{{BahnRennen}}**——在导航点前写入 `SET_WALKING` 或 `SET_RUNNING`。",
              "**{{BahnGehen}}** / **{{BahnRennen}}** - ナビゴールの前に `SET_WALKING` または `SET_RUNNING` を書きます。"),
            T("**{{BahnAnsehen}}** - the figure keeps turning toward another figure while it walks (`SET_WATCHTARGET`).",
              "**{{BahnAnsehen}}** - die Figur dreht sich beim Laufen weiter zu einer anderen Figur (`SET_WATCHTARGET`).",
              "**{{BahnAnsehen}}**——角色行走时持续转向另一个角色(`SET_WATCHTARGET`)。",
              "**{{BahnAnsehen}}** - 歩きながら別のキャラクターの方を向き続けます(`SET_WATCHTARGET`)。"),
            T("**{{BahnRueckwaerts}}** - a look mark behind the start makes it walk backwards; **{{BahnBlickFrei}}** ends that.",
              "**{{BahnRueckwaerts}}** - eine Blickmarke hinter dem Start lässt sie rückwärts laufen; **{{BahnBlickFrei}}** beendet das.",
              "**{{BahnRueckwaerts}}**——在起点后方放置注视标记使其倒退行走;**{{BahnBlickFrei}}** 取消。",
              "**{{BahnRueckwaerts}}** - 始点の後ろに視線マークを置いて後ろ向きに歩かせます。**{{BahnBlickFrei}}** で解除します。"),
        ]),
        ("p", T(
            "When several figures share one navgoal, moving it would move all of them. BehavEd-Remake notices and gives this walk its own copy of the target first - the status line tells you which one it created.",
            "Teilen sich mehrere Figuren einen Navgoal, würde das Verschieben alle verschieben. BehavEd-Remake merkt das und gibt diesem Lauf zuerst eine eigene Kopie des Ziels - die Statuszeile sagt, welche es angelegt hat.",
            "多个角色共用一个导航点时,移动它会影响所有角色。BehavEd-Remake 会察觉这一点,先为这段行走复制一个独立目标——状态栏会告诉你新建了哪个。",
            "複数のキャラクターが 1 つのナビゴールを共有している場合、動かすと全員が動いてしまいます。BehavEd-Remake はそれを検知し、先にこの歩行専用の目標のコピーを作ります。どれを作ったかはステータス行に表示されます。")),
        ("note", T(
            "Walking backwards is detected in existing scripts too: when a figure moves against the way it faces, the scene plays `BOTH_WALKBACK1` or `BOTH_RUNBACK1` like the game.",
            "Rückwärtslaufen wird auch in vorhandenen Skripten erkannt: bewegt sich eine Figur entgegen ihrer Blickrichtung, spielt die Szene wie das Spiel `BOTH_WALKBACK1` oder `BOTH_RUNBACK1`.",
            "已有脚本中的倒退行走也会被识别:角色朝与面向相反的方向移动时,场景会像游戏一样播放 `BOTH_WALKBACK1` 或 `BOTH_RUNBACK1`。",
            "既存のスクリプトの後ろ歩きも認識されます。キャラクターが向きと逆に動くと、ゲームと同じく `BOTH_WALKBACK1` または `BOTH_RUNBACK1` が再生されます。")),
    ],
}

ANIMATION = {
    "id": "animation",
    "gruppe": "karte",
    "titel": T("Animating movers and ROFF files", "Mover animieren und ROFF-Dateien", "移动物体动画与 ROFF 文件", "可動物のアニメーションと ROFF ファイル"),
    "kurz": T(
        "Ships, doors and platforms move by keys over time. Store them as move commands in the script, or as a ROFF file - and edit the ROFF files of the game.",
        "Schiffe, Türen und Plattformen bewegen sich über Schlüssel in der Zeit. Speichere sie als move-Befehle im Skript oder als ROFF-Datei - und bearbeite die ROFF-Dateien des Spiels.",
        "飞船、门和平台通过随时间变化的关键帧运动。可以将其保存为脚本中的 move 命令或 ROFF 文件,还能编辑游戏自带的 ROFF 文件。",
        "船・扉・プラットフォームは時間に沿ったキーで動きます。スクリプトの move コマンドとして、または ROFF ファイルとして保存でき、ゲームの ROFF ファイルも編集できます。"),
    "bloecke": [
        ("img", "h_animation", T(
            "The {{AnimTitle}} window in ROFF mode and the path of the ship on the map.",
            "Das Fenster {{AnimTitle}} im ROFF-Modus und die Bahn des Schiffs auf der Karte.",
            "ROFF 模式下的 {{AnimTitle}} 窗口以及地图上飞船的路径。",
            "ROFF モードの {{AnimTitle}} ウィンドウと、マップ上の船のパス。"), "breit"),
        ("h2", "start", T("Opening the window", "Das Fenster öffnen", "打开窗口", "ウィンドウを開く")),
        ("p", T(
            "Select a movable entity - `func_*` brush entities such as `func_static` ships and `func_door`, `misc_model_breakable`, `fx_runner` - and press **{{EntAnimate}}** in the inspector. If it has no `script_targetname` yet, it gets one, because a script can only move what has a name.",
            "Wähle eine bewegliche Entity - `func_*`-Brush-Entities wie `func_static`-Schiffe und `func_door`, `misc_model_breakable`, `fx_runner` - und drücke **{{EntAnimate}}** im Inspektor. Hat sie noch keinen `script_targetname`, bekommt sie einen, denn ein Skript kann nur bewegen, was einen Namen hat.",
            "选择一个可移动实体——例如 `func_static` 飞船、`func_door` 等 `func_*` 笔刷实体,以及 `misc_model_breakable`、`fx_runner`——然后在检查器中按 **{{EntAnimate}}**。如果它还没有 `script_targetname`,会自动获得一个,因为脚本只能移动有名字的对象。",
            "動かせるエンティティ(`func_static` の船や `func_door` などの `func_*` ブラシエンティティ、`misc_model_breakable`、`fx_runner`)を選び、インスペクターの **{{EntAnimate}}** を押します。`script_targetname` がなければ付けられます。スクリプトは名前のあるものしか動かせないからです。")),
        ("h2", "schluessel", T("Keys", "Schlüssel", "关键帧", "キー")),
        ("steps", [
            T("Put the timeline where the movement starts and press **{{AnimAddKey}}** - the first key stores where the entity stands.",
              "Stell die Zeitleiste dorthin, wo die Bewegung beginnt, und drücke **{{AnimAddKey}}** - der erste Schlüssel merkt sich, wo die Entity steht.",
              "把时间轴移到运动开始处,按 **{{AnimAddKey}}**——第一个关键帧记录实体当前的位置。",
              "動きの始まりにタイムラインを合わせ、**{{AnimAddKey}}** を押します。最初のキーはエンティティの現在位置を記録します。"),
            T("Move on in time and press it again. Then drag the new diamond in the view (<kbd>Shift</kbd>: only up and down), or type values into the table: time, position, direction.",
              "Geh in der Zeit weiter und drück ihn noch einmal. Dann die neue Raute in der Ansicht ziehen (<kbd>Umschalt</kbd>: nur hoch und runter) oder Werte in die Tabelle tippen: Zeit, Ort, Richtung.",
              "在时间上前进后再按一次。然后在视图中拖动新的菱形(<kbd>Shift</kbd>:仅上下),或在表格中输入数值:时间、位置、方向。",
              "時間を進めてもう一度押します。ビューで新しいひし形をドラッグするか(<kbd>Shift</kbd>:上下のみ)、表に時間・位置・向きを入力します。"),
            T("Or simply use the gizmo: while the window shows this entity and the timeline stands at or after the first key, moving or rotating it with the gizmo sets a key at the playhead (or changes the one that is there).",
              "Oder einfach das Gizmo: solange das Fenster diese Entity zeigt und die Zeitleiste am ersten Schlüssel oder danach steht, setzt Verschieben oder Drehen mit dem Gizmo einen Schlüssel am Zeiger (oder ändert den, der dort steht).",
              "或者直接使用操纵器:只要窗口显示该实体且时间轴位于第一个关键帧或其后,用操纵器移动或旋转就会在播放头处设置关键帧(或修改已有的关键帧)。",
              "あるいはギズモを使います。ウィンドウがこのエンティティを表示していて、タイムラインが最初のキー以降にあれば、ギズモで移動・回転すると再生位置にキーが設定されます(既にあればそれを変更します)。"),
            T("**{{AnimDeleteKey}}** and **{{AnimGotoKey}}** work on the selected row; **{{AnimRemove}}** takes the whole animation out of the script.",
              "**{{AnimDeleteKey}}** und **{{AnimGotoKey}}** wirken auf die gewählte Zeile; **{{AnimRemove}}** nimmt die ganze Animation aus dem Skript.",
              "**{{AnimDeleteKey}}** 和 **{{AnimGotoKey}}** 作用于所选行;**{{AnimRemove}}** 从脚本中移除整个动画。",
              "**{{AnimDeleteKey}}** と **{{AnimGotoKey}}** は選択中の行に作用し、**{{AnimRemove}}** はアニメーション全体をスクリプトから削除します。"),
        ]),
        ("h2", "arten", T("Script (move) or ROFF file", "Skript (move) oder ROFF-Datei", "脚本(move)还是 ROFF 文件", "スクリプト(move)か ROFF ファイルか")),
        ("table", ["", T("{{AnimModeMove}}", "{{AnimModeMove}}", "{{AnimModeMove}}", "{{AnimModeMove}}"), T("{{AnimModeRoff}}", "{{AnimModeRoff}}", "{{AnimModeRoff}}", "{{AnimModeRoff}}")], [
            [T("Stored as", "Gespeichert als", "保存为", "保存形式"),
             T("an `affect` block with `move` and `wait` at the start of the script", "ein `affect`-Block mit `move` und `wait` am Anfang des Skripts", "脚本开头带 `move` 和 `wait` 的 `affect` 块", "スクリプト先頭の `move` と `wait` を含む `affect` ブロック"),
             T("`scripts/<name>.rof`, played by `play ( \"PLAY_ROFF\", ... )`", "`scripts/<Name>.rof`, abgespielt mit `play ( \"PLAY_ROFF\", ... )`", "`scripts/<名称>.rof`,由 `play ( \"PLAY_ROFF\", ... )` 播放", "`scripts/<名前>.rof`(`play ( \"PLAY_ROFF\", ... )` で再生)")],
            [T("Between keys", "Zwischen den Schlüsseln", "关键帧之间", "キーの間"),
             T("each move starts fast and slows down at the end, like the game's movers (or constant with {{AnimLinear}})", "jede Fahrt beginnt schnell und läuft langsam aus wie die Mover des Spiels (oder gleichmäßig mit {{AnimLinear}})", "每段移动开始快、结尾减速,与游戏的移动物体相同(或用 {{AnimLinear}} 匀速)", "各移動はゲームの可動物と同じく速く始まり最後に減速します({{AnimLinear}} で等速)"),
             T("one smooth curve through all keys ({{AnimRoffSmooth}}) or straight lines", "ein weicher Bogen durch alle Schlüssel ({{AnimRoffSmooth}}) oder gerade Stücke", "穿过所有关键帧的平滑曲线({{AnimRoffSmooth}})或直线段", "すべてのキーを通るなめらかな曲線({{AnimRoffSmooth}})または直線")],
            [T("Good for", "Gut für", "适合", "向いている用途"),
             T("doors, lifts, a ship crossing the screen", "Türen, Aufzüge, ein Schiff, das durchs Bild fliegt", "门、升降机、穿过画面的飞船", "扉、エレベーター、画面を横切る船"),
             T("long free flights with many turns, effects at exact frames", "lange freie Flüge mit vielen Kurven, Effekte auf genaue Bilder", "多转弯的长距离自由飞行、精确到帧的特效", "多くの曲がりを含む長い自由飛行、正確なフレームでのエフェクト")],
        ]),
        ("p", T(
            "Switch with **{{AnimModeMove}}** / **{{AnimModeRoff}}** at the top of the window - the keys are converted, nothing is lost. In ROFF mode the field **{{AnimRoffName}}** names the file (for example `md_am/roffs/sta2`); below it you see how many frames and seconds it has.",
            "Umschalten mit **{{AnimModeMove}}** / **{{AnimModeRoff}}** oben im Fenster - die Schlüssel werden umgewandelt, nichts geht verloren. Im ROFF-Modus nennt das Feld **{{AnimRoffName}}** die Datei (zum Beispiel `md_am/roffs/sta2`); darunter steht, wie viele Bilder und Sekunden sie hat.",
            "用窗口顶部的 **{{AnimModeMove}}** / **{{AnimModeRoff}}** 切换——关键帧会被转换,不会丢失。ROFF 模式下 **{{AnimRoffName}}** 字段指定文件名(例如 `md_am/roffs/sta2`);下方显示其帧数和秒数。",
            "ウィンドウ上部の **{{AnimModeMove}}** / **{{AnimModeRoff}}** で切り替えます。キーは変換され、失われません。ROFF モードでは **{{AnimRoffName}}** 欄がファイル名(例 `md_am/roffs/sta2`)を指定し、その下にフレーム数と秒数が表示されます。")),
        ("h2", "roff", T("How ROFF files work", "Wie ROFF-Dateien funktionieren", "ROFF 文件的原理", "ROFF ファイルの仕組み")),
        ("p", T("A ROFF file (`.rof`, \"Rotation and Origin File Format\") stores one small step per game frame: how far the entity moves and turns in the next 50 ms. The game adds these steps to wherever the entity stands when `PLAY_ROFF` runs - so a ROFF path is **relative**: move the entity and the whole path moves with it. All 108 files of the game and Movie Duels are version 2, all but one with 50 ms per frame - and that is what BehavEd-Remake writes.", "Eine ROFF-Datei (`.rof`, \"Rotation and Origin File Format\") speichert je Spielbild einen kleinen Schritt: wie weit sich die Entity in den nächsten 50 ms bewegt und dreht. Das Spiel addiert diese Schritte zu dem Ort, an dem die Entity steht, wenn `PLAY_ROFF` läuft - eine ROFF-Bahn ist also **relativ**: verschiebst du die Entity, wandert die ganze Bahn mit. Alle 108 Dateien des Spiels und von Movie Duels sind Fassung 2, alle bis auf eine mit 50 ms je Bild - und so schreibt BehavEd-Remake sie auch.", "ROFF 文件(`.rof`,“Rotation and Origin File Format”)为每个游戏帧保存一小步:实体在接下来 50 毫秒内移动和旋转多少。游戏在 `PLAY_ROFF` 执行时,把这些步长累加到实体当时所在的位置——因此 ROFF 路径是**相对的**:移动实体,整条路径也随之移动。游戏和 Movie Duels 中的全部 108 个文件都是版本 2,除一个外均为每帧 50 毫秒——BehavEd-Remake 也按此格式写入。", "ROFF ファイル(`.rof`、\"Rotation and Origin File Format\")は、ゲームの 1 フレームごとに小さな 1 歩、つまり次の 50 ms でエンティティがどれだけ動き回転するかを保存します。ゲームは `PLAY_ROFF` が実行された時点のエンティティの位置にこれを足していくので、ROFF のパスは**相対的**です。エンティティを動かせばパス全体も一緒に動きます。ゲームと Movie Duels の 108 ファイルはすべてバージョン 2 で、1 つを除き 1 フレーム 50 ms です。BehavEd-Remake もその形式で書き出します。")),
        ("p", T(
            "The keys you edit are kept as a comment line in the script right before the `play`; the `.rof` is built from them. That is why undo, copy and paste work on ROFF paths like on any command. When you **save the script**, the file is written to `scripts/<name>.rof` in the same scripts folder; **{{MissionSave}}** packs it into the mission's `.pk3`.",
            "Die Schlüssel, die du bearbeitest, stehen als Kommentarzeile im Skript direkt vor dem `play`; die `.rof` wird aus ihnen gebaut. Deshalb wirken Rückgängig, Kopieren und Einfügen auf ROFF-Bahnen wie auf jeden Befehl. Beim **Speichern des Skripts** wird die Datei als `scripts/<Name>.rof` in denselben scripts-Ordner geschrieben; **{{MissionSave}}** packt sie ins `.pk3` der Mission.",
            "你编辑的关键帧作为注释行保存在脚本中 `play` 的正前方;`.rof` 由它们生成。因此撤销、复制和粘贴对 ROFF 路径与普通命令一样有效。**保存脚本**时,文件写入同一 scripts 文件夹下的 `scripts/<名称>.rof`;**{{MissionSave}}** 会把它打包进任务的 `.pk3`。",
            "編集するキーは、スクリプト内の `play` の直前にコメント行として保持され、`.rof` はそこから作られます。そのため元に戻す・コピー・貼り付けは ROFF パスにも通常のコマンドと同じように効きます。**スクリプトを保存**すると、ファイルは同じ scripts フォルダーの `scripts/<名前>.rof` に書き出され、**{{MissionSave}}** でミッションの `.pk3` に入ります。")),
        ("h2", "bearbeiten", T("Editing a ROFF file of the game", "Eine ROFF-Datei des Spiels bearbeiten", "编辑游戏中的 ROFF 文件", "ゲームの ROFF ファイルを編集する")),
        ("p", T("When a script plays a finished ROFF on the entity, the window lists it under **{{AnimRoffPlays}}**. **{{AnimRoffEdit}}** turns it into keys - either straight pieces or one smooth curve, whichever needs fewer keys - within half a unit of the original. The game's file is never overwritten: you edit a copy (`…_2`), and the `play` command is pointed at it.", "Spielt ein Skript eine fertige ROFF auf der Entity ab, nennt das Fenster sie unter **{{AnimRoffPlays}}**. **{{AnimRoffEdit}}** zerlegt sie in Schlüssel - entweder gerade Stücke oder einen weichen Bogen, je nachdem, was mit weniger Schlüsseln auskommt -, bis auf eine halbe Einheit genau. Die Datei des Spiels wird nie überschrieben: du bearbeitest eine Kopie (`…_2`), und der `play`-Befehl zeigt auf sie.", "当脚本在该实体上播放现成的 ROFF 时,窗口会在 **{{AnimRoffPlays}}** 下列出。**{{AnimRoffEdit}}** 把它分解为关键帧——直线段或一条平滑曲线,取所需关键帧较少的一种——与原文件误差在半个单位以内。游戏文件永远不会被覆盖:你编辑的是副本(`…_2`),`play` 命令会指向该副本。", "スクリプトがこのエンティティで既製の ROFF を再生している場合、ウィンドウの **{{AnimRoffPlays}}** に表示されます。**{{AnimRoffEdit}}** でそれをキーに分解します。直線か 1 本のなめらかな曲線のうち、キーが少なくて済む方を選び、元との差は 0.5 ユニット以内です。ゲームのファイルは上書きされず、コピー(`…_2`)を編集し、`play` コマンドはそのコピーを指すようになります。")),
        ("anim", "a_roff", T(
            "A ship flying its ROFF path.",
            "Ein Schiff fliegt seine ROFF-Bahn.",
            "飞船沿 ROFF 路径飞行。",
            "ROFF のパスを飛ぶ船。")),
        ("note", T(
            "In the timeline a ROFF flight is a band under **{{TlKatBewegung}}**; drag it to start the flight earlier or later.",
            "In der Zeitleiste ist ein ROFF-Flug ein Band unter **{{TlKatBewegung}}**; ziehen startet den Flug früher oder später.",
            "在时间轴中,ROFF 飞行显示为 **{{TlKatBewegung}}** 下的条带;拖动可让飞行提前或推迟开始。",
            "タイムラインでは ROFF の飛行は **{{TlKatBewegung}}** の帯になり、ドラッグすると飛行の開始を早めたり遅らせたりできます。")),
    ],
}

SEITEN = [KAMERA, ZEITLEISTE, FIGUREN, ANIMATION]

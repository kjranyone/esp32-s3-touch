# 画像生成指示書 (GPT Imagen 用) — 美少女マスコット「ルミ」

放置系インクリメンタル『ルミの水晶庭園』(詳細: `docs/GAME_DESIGN.md`) のための
立ち絵・表情差分を **GPT Imagen (GPT Image)** で生成するための手順書。

生成したファイルはリポジトリの `assets_src/` フォルダに保存してください(取り込みと RGB565 変換はこちらで行います)。

---

## 1. ゲーム概要(アセットが必要な理由)

- **放置系**: 庭園の施設が自動でクリスタルを生産し続け、プレイヤーは眺めて育てる
- プレイヤーの操作は「施設/アップグレード購入」「昇華」「ルミを撫でる」だけ(タップ連打要素なし)
- マスコット少女 **「ルミ」(クリスタルの精霊)** が画面中央に立ち、状況で表情が切り替わる

| 状況 | 表現 | 対応アセット |
|---|---|---|
| 待機(常時) | 微笑み、たまに瞬き | `01_idle`, `02_blink` |
| マイルストーン達成 / 施設・アップグレード購入 | 喜んで両手を挙げる | `03_happy` |
| 頭をなでなで(スワイプ・生産バフ) | 照れて目を閉じる | `04_pet` |
| 電源を切っていた期間からの復帰 | あくび(眠そう)→ 報告 | `05_sleepy` |
| 離席中(スクリーンセーバ) | 丸まって寝ている | `06_sleep` |
| 不意のタップ(ごく稀な演出) | 驚き | `07_surprised` |
| (UI 用)背景・通貨アイコン | 画面の土台 | `90_bg`, `91_crystal` |

---

## 2. キャラクター設定(全アセット共通の"アンカー")

一貫性を保つため、キャラの識別点は **シンプルで数を絞る**。以下を全プロンプトに毎回含めること。

> **ルミ (Lumi)** — クリスタルの精霊の少女
> - 髪:シルバーホワイトのツインテール、毛先だけ水色
> - 瞳:水色(ライトアクア)
> - 服装:白いワンピースに淡い水色のサスペンダー、大きな袖
> - アクセサリ:頭の横に星形の水色クリップ(左右1個ずつ)
> - 年齢感:10代前半くらいのあどけなさ、身長は低め(妖精っぽい可愛らしさ)
> - 小物:胸元に小さな水晶のペンダント

---

## 3. 共通スタイル指定(全アセットで固定)

```
anime style, clean thick lineart, soft cel shading, pastel color palette,
friendly and cute mascot design, high contrast readable colors,
suitable for a small 480x480 LCD screen, minimal fine details,
no text, no watermark, no signature
```

画面・形式の要件:

| 項目 | 指定 |
|---|---|
| サイズ | **正方形 1024×1024** で生成(デバイス側で縮小します) |
| 背景 | **透過 PNG**。透過がうまくいかない場合は背景を **完全なマゼンタ単色 (#FF00FF)** に(影を背景に落とさない) |
| 構図 | 立ち絵は全身、キャラがフレームの高さの約 75〜80% を占め、**やや下寄せ**。上 15〜20% は余白(HUD 表示用) |
| ファイル名 | 下記アセット一覧の名前のまま |

---

## 4. 生成手順(この順番が重要)

### Step 1: キャラクターシート `00_chara_sheet.png` を作る
まずキャラの"定義"を作る。以降の全アセットは **この画像を参照画像として添付** して「同じキャラで」と指示する。

**プロンプト例:**
```
Character design sheet of ONE anime girl mascot named Lumi, a little crystal spirit fairy.
She has: silver-white twin-tails with light aqua tips, aqua eyes, a white dress with pale
aqua suspenders and big sleeves, star-shaped aqua hair clips on both sides of her head,
a small crystal pendant on her chest. Cute petite girl, early-teens look.
Full body, standing, front view, neutral gentle smile, arms relaxed at her sides.
Clean thick lineart, soft cel shading, pastel palette. Centered, full body visible,
occupying about 80% of the frame height, slightly low in frame with headroom above.
Plain transparent background. No text, no watermark.
```

### Step 2: メイン立ち絵 `01_idle.png`
`00_chara_sheet.png` を **画像参照として添付** し:

```
Using the attached character as the exact same girl (same hair, eyes, outfit, hair clips,
pendant), draw her full body standing portrait: gentle warm smile, eyes open, arms relaxed
at her sides, looking slightly toward the viewer. Same pose as the reference.
Anime style, clean thick lineart, soft cel shading, pastel palette, transparent background,
no text. Square image, character occupying about 80% of the height, slightly low in frame.
```

### Step 3: 表情差分(以降はすべて `01_idle.png` を参照添付して生成)
ポイント: **ポーズは変えない**(差分は表情と腕だけ)。全画面をまるごと差し替えるので、
体の位置がずれるとアニメーションがガタつきます。

**`02_blink.png` — 瞬き(必須)**
```
Same exact girl, same pose, same framing and same composition as the attached image.
Only change: her eyes are fully closed (blink frame). Everything else identical.
Transparent background, no text.
```

**`03_happy.png` — 喜び(収入ボーナス)**
```
Same exact girl and same framing as the attached image, full body.
Change: joyful expression, big open smile, eyes sparkling, both arms raised up cheering,
slight bounce pose. Keep her feet position and body size identical.
Transparent background, no text.
```

**`04_pet.png` — なでなで反応**
```
Same exact girl and same framing as the attached image, full body.
Change: blushing cheeks, eyes closed, shy happy smile, both hands together near her chest,
slightly leaning her head down as if being petted. Keep composition identical.
Transparent background, no text.
```

**`05_sleepy.png` — 眠い(放置復帰)**
```
Same exact girl and same framing as the attached image, full body.
Change: sleepy face, half-closed droopy eyes, small yawn with one hand near her mouth,
slightly slouched posture. Keep composition identical. Transparent background, no text.
```

**`06_sleep.png` — スリープ画面**
```
Same exact girl as the attached image.
Change: she is sitting on the ground curled up, sleeping peacefully with closed eyes,
hugging her knees, tiny "Z z z" floating is NOT allowed — no text at all.
Soft relaxed pose, full body visible, same art style. Transparent background, no text.
```

**`07_surprised.png` — 驚き(不意のタップ)**
```
Same exact girl and same framing as the attached image, full body.
Change: surprised expression, wide eyes, small "o" mouth, hands slightly raised near her
chest, leaning back a little. Keep feet position identical.
Transparent background, no text.
```

### Step 4: UI 素材

**`90_bg.png` — 画面背景 480×480 用**
```
Simple soft background illustration for a square LCD screen, magical crystal workshop theme:
very light pastel blue-violet gradient with faint silhouettes of crystals and shelves,
extremely low contrast and low detail (it must stay behind a character and UI text),
no characters, no text, no strong shapes in the center area.
```
※ これは透過ではなく不透明で OK。

**`91_crystal.png` — 通貨アイコン**
```
A single cute crystal gem icon, light aqua color with soft glow, small star sparkles,
anime game UI icon style, thick clean outline, centered in the square frame,
occupying about 60% of the frame, transparent background, no text.
```

### (任意 / v2 用)追加差分
`08_proud.png`(大型アップグレード購入時の得意顔)、`09_wave.png`(起動時の挨拶)、
瞬きの中間フレーム(`02_blink_half`:まぶた半分)を作るとアニメがさらに滑らかになります。

---

## 5. チェックリスト(納品前に自己確認)

- [ ] 全アセットで **ツインテールの形状・水色の毛先・星クリップ左右2個・水晶ペンダント** が一致している
- [ ] 立ち絵系(`01`〜`07`)は **体の位置・大きさ・足の位置がほぼ同じ**(表情と腕だけ変わる)
- [ ] 背景が透過 or 完全マゼンタ単色(被り物・グラデーション影がない)
- [ ] テキスト・署名・透かしが一切無い
- [ ] ファイル名が指示どおり(`01_idle.png` など、2桁番号 + 英小文字)

---

## 6. 取り込み後の技術メモ(こちらで作業)

- 1024×1024 → 480×480 に縮小し、LVGL 用に RGB565 変換(C 配列は Flash、実行時は PSRAM に展開)
- `01_idle` ⇄ `02_blink` を LVGL タイマーで 2 枚スワップ(瞬き)、`04_pet`/`03_happy` 等はイベントで差し替え
- 表情差分はフルフレーム交換方式なので、差分間の構図一致が品質の最重要ポイント

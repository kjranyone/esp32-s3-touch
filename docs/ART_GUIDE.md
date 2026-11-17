# 画像生成指示書 (GPT Imagen 用) v3 — ベルトスクロール用ちびキャラ

『ルミの水晶庭園』v3(ベルトスクロールアクション)用のスプライトを作る手順。
スタイルは **アニメ調ちびキャラ(2頭身)**。GPT でフレーム整合を取るのが最難点なので、
「参照画像添付 + 正面固定 + ポーズ数を絞る」戦略を取る。

生成したファイルは `assets_src/` に保存する(縮小・RGB565 化はこちらで実施)。

---

## 1. 必要アセット一覧

### ルミ (プレイヤー) — 計 8 枚
| ファイル | 内容 |
|---|---|
| `lumi_ref.png` | キャラ定義(全フレームの参照元) |
| `lumi_idle.png` | 待機(構え) |
| `lumi_walk.png` | 歩き(脚を動かした 1 枚。2 フレーム歩行は idle と交互で代用) |
| `lumi_atk1.png` | 攻撃 1 段目(片手パンチ/素手) |
| `lumi_atk2.png` | 攻撃 2 段目(逆手) |
| `lumi_atk3.png` | 攻撃 3 段目(両手で強打・大ポーズ) |
| `lumi_hurt.png` | 被弾(痛がる極端ポーズ) |
| `lumi_down.png` | 転倒(寝ている。復帰はリバース再生) |

### 敵 — 各 2 枚 (idle + attack)。被弾は白フラッシュで代用、転倒は回転で代用
| ファイル | 内容 |
|---|---|
| `slime_idle.png` / `slime_atk.png` | スライム(緑の体当たり雑魚) |
| `bat_idle.png` / `bat_atk.png` | コウモリ(素早いヒットアウェイ) |
| `golem_idle.png` / `golem_atk.png` | ゴーレム(大振りタンク) |
| `boss_idle.png` / `boss_atk.png` | ボス(庭を荒らす巨大な影) |

### 背景 — 2 枚
| ファイル | 内容 |
|---|---|
| `bg_far.png` | 遠景(水晶の森のシルエット。低コントラスト)横長 |
| `bg_ground.png` | 歩行帯(庭園の地面。タイル可能な横長ストリップ) |

---

## 2. キャラ設定アンカー (全フレーム共通)

> **ルミ (Lumi)** — クリスタルの精霊の少女、**2頭身ちび**、戦闘服
> - 銀髪ツインテール(毛先水色)、水色の瞳、星形の髪クリップ左右
> - 白×水色の戦闘ワンピ、胸に水晶ペンダント、素手
> - **横向き(右向き)サイドビュー**で描く(ベルトスクロールは横視点)

## 3. 共通スタイル (全アセット固定)

```
anime chibi style, 2-heads-tall proportion, clean thick lineart, soft cel shading,
pastel colors, side view facing right, full body centered,
character occupying about 60% of the frame height, plain transparent background,
no text, no watermark
```

- サイズ: 1024×1024 正方形 → デバイス側で高さ ~96px に縮小
- 透過 PNG(不可なら完全マゼンタ #FF00FF)
- **背景に影を落とさない**(影はデバイス側で楕円描画)

## 4. 生成手順

### Step 1: `lumi_ref.png`
```
Anime chibi character design of Lumi, a crystal spirit girl, 2-heads-tall.
Silver-white twin-tails with aqua tips, aqua eyes, star-shaped hair clips,
white and pale-aqua battle dress, small crystal pendant, bare hands, ready stance.
Side view facing right. Clean thick lineart, soft cel shading, pastel palette.
Centered, full body, transparent background, no text.
```

### Step 2: 各フレーム (`lumi_ref.png` を必ず参照添付)
共通の冒頭文:
```
Exact same chibi girl as the reference image (same hair, outfit, colors, proportions,
same side view facing right, same scale and same position in frame).
Only change the pose: <ポーズ>
Transparent background, no text.
```
ポーズ指定:
- idle: `standing ready pose, fists slightly raised, relaxed`
- walk: `mid-walk, one leg forward, arms swinging`
- atk1: `throwing a quick straight punch with the front arm, slight lean forward`
- atk2: `punching with the other arm, body twisting`
- atk3: `big powerful double-fisted smash overhead, exaggerated dynamic pose`
- hurt: `flinching, leaning back, eyes closed, arms flailing (exaggerated pain pose)`
- down: `lying on the ground on her back, knocked down, dizzy`

### Step 3: 敵 (ルミ参照は不要。敵内での一貫性のみ)
```
Anime chibi enemy design for a cute beat-em-up game, side view facing LEFT,
2-heads-tall, clean thick lineart, soft cel shading, transparent background, no text.
<敵ごとの説明>
```
- slime: `a round green slime monster with big cute eyes, small fangs`
- bat: `a small speedy purple bat with tiny wings`
- golem: `a heavy stone golem with moss and glowing blue crystal veins, big fists`
- boss: `a large shadowy plant-monster with red eyes entangled in dead crystal vines, twice as big as a normal enemy`

attack フレームは idle を参照添付して `same creature, lunging attack pose` で生成。

### Step 4: 背景
- `bg_ground.png`:
```
Seamless horizontal tileable ground strip for a 2D side-scroller set in a magical
crystal garden, soft pastel grass with faint glowing crystal fragments,
low contrast (must not compete with characters), anime painted style, no text.
```
- `bg_far.png`:
```
Distant background layer for a 2D side-scroller: silhouettes of crystal trees and
a pale aqua-violet sky, very low contrast, soft gradients, anime painted style,
no characters, no text.
```

## 5. 納品チェックリスト

- [ ] ルミ全 8 フレームで **髪型・服・色・2頭身比率・右向き** が一致
- [ ] 攻撃 3 フレームは **拳の位置がコマ送りで連続**に見える(体の位置はほぼ同じ)
- [ ] 敵は idle/attack で **大きさ・配色一致**、向きは左
- [ ] 透過 or 完全マゼンタ、影なし
- [ ] `lumi_ref.png` と他フレームを並べて **黒塗りでも判別可能**(シルエット判定)

## 6. 技術メモ

- 高さ ~96px に縮小して RGB565 変換。攻撃フレームは必要に応じ 1.2 倍表示(誇張)
- 被弾 = 白フラッシュ(色反転)、転倒 = down フレーム + 復帰は逆再生でコスト削減
- 敵は左右反転で向きを切り替える

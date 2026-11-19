# 全キャラクターのアニメーション素材

## 制作仕様

内蔵 image_gen 使用。既存ルミPNGを共通の画風参照として、7キャラの6列×6行・36コマの透過アトラスを制作。
原画は `assets_src/{lumi,mira,goro,slime,bat,golem,boss}_atlas.png`。
最終プロンプト全文は `assets_src/ATLAS_PROMPTS.md`。
銀髪・水色のルミ、杖を持つMIRA、石の守護者GORO、スライム、コウモリ、敵ゴーレム、影の植物ボス。

## コマ割り (番号は1始まり)

| 動作 | ルミ | その他 |
|---|---|---|
| 待機ループ | 1–4 | 1–4 |
| 歩行/飛行ループ | 5–12 | 5–12 |
| 攻撃1 | 13–18 | 13–20 |
| 攻撃2 | 19–24 | — |
| 攻撃3 | 25–30 | — |
| 被弾 | 31–33 | 21–24 |
| 転倒/撃破 | 34–36 | 25–28 |
| 大技 | 攻撃3を使用 | 29–36 |

中割を含む手描き連番。フレームのクロスフェードで動きを代用せず、予備動作・振り抜き・戻りのポーズを再生する。
桜井スキルの構え→打撃→フォロースルーを維持し、打撃フレームは既存の当たり判定とヒットストップに同期。
歩行は1コマ5ゲームフレーム。待機は9ゲームフレーム。攻撃は既存の予備動作/硬直時間へ連番を割り当てる。

## 変換・確認

`python tools/build_atlases.py` (Pillow)。各セルを等分し、キャラ内で共通の矩形・縮尺を使用する。
RGBA原画を残し、256色へ量子化したRGB565＋8bitアルファを行ごとのスパンへ変換。
実機では不透明な区間をまとめてコピーし、半透明の縁だけ背景と合成する。
ほぼ不透明なアルファ(240以上)は255、ほぼ透明(8以下)は0へ丸める。原画のアルファは変更しない。
毎フレームのメモリ確保は不要。

`assets_src/previews/` に各キャラのコマ一覧、歩行GIF、攻撃GIFを出力。
`assets_src/atlas_manifest.json` にフレーム数・寸法・保存量を記録する。
実装は `src/actor_sprites.inc`。旧図形キャラクター描画を全種置換し、敵の撃破連番も再生する。

生成画像は完全な機械的作画ではないため、髪や装飾、体積の小さな揺れは実機確認後の調整対象。
背景・UI・効果音はこのキャラクター制作工程の対象外。

## 実機検証 (v3.2 Sakurai Overhaul)

- **ビルド・書き込み**: `pio run -t upload --upload-port COM6` 成功 (Flash `3,484,546 / 6,553,600 B` = 53.2%, Static RAM `30,696 / 327,680 B` = 9.4%)。
- **ポーズ & VFX/背景統合**:
  - `assets_src/previews/*_contact.png` を実測照合し全 7 キャラの `0..35` フレーム割り当てを修正、打撃決定フレーム保持状態 `A_STRIKE` と勝利状態 `A_VICTORY` を追加。オートバトルの爽快感・連続性を優先して全体のヒットストップ (時間停止) は外し、`A_STRIKE` 決めポーズ・49 種セル VFX・スクワッシュ＆ストレッチ・被弾振動・ノックバック・黒縁取り最前面ダメージ数字 (`layer = 2`) で打撃感を表現。
  - `tools/build_vfx_atlas.py` により 49 種の 4x SS セルアニメ VFX (`src/generated/fx_combat_atlas.h`)、`tools/build_scenery_and_ui.py` により 3 層パララックス背景と 5 種カットイン (`src/generated/scenery_assets.h`) を追加し、`DIR_BOSS_INTRO` / 味方必殺カットイン / `DIR_WAVE_CLEAR` / `DIR_BOSS_VICTORY` を実装。
- **最適化 & 実測テレメトリ** (`docs/evidence/sakurai-overhaul-runtime.txt`):
  - 旧スプライト初版の 24fps (render 13.7ms) から、32 行内部 SRAM 15×11 ダーティタイル合成器・11 スロットポーズキャッシュ (`actor_pose_buf`)・VFX/背景/プロップ RAM 常駐・モノトニック行カーソル `blit_span_band_cursor`・`TightBounds`・ゾーンロックカメラ・差分 HUD (ミニマップは毎フレーム更新) を導入。
  - 実機計測: ミニマップ毎フレーム更新版 Wave 2 (**fps 62**, `render 7.93ms` / `battle 6.91ms` / `hud 1.02ms` / `total 9.71ms`; 激戦 Wave 3 では `fps 41`, `total 22.0ms`)、`int_max 102,388 B`、`psram_free 3,133,544 B`。
- **実機スクリーンショット検証**:
  - [shot_battle.png](file:///C:/lib/github/kjranyone/esp32-s3-touch/docs/evidence/shot_battle.png) (通常戦 / VFX・カットイン合成)
  - [shot_boss_cutin.png](file:///C:/lib/github/kjranyone/esp32-s3-touch/docs/evidence/shot_boss_cutin.png) (`WARNING!!` + `cutin_boss` ボス登場演出)
  - [shot_ultimate.png](file:///C:/lib/github/kjranyone/esp32-s3-touch/docs/evidence/shot_ultimate.png) (味方必殺カットイン + 速度線分離)
  - [shot_boss_victory.png](file:///C:/lib/github/kjranyone/esp32-s3-touch/docs/evidence/shot_boss_victory.png) (`GARDEN PURIFIED!!` + `cutin_lumi_win` + 宝石/紙吹雪)
- 画像データ総量: キャラスプライト `2,397,622 B` + VFX `~237 KB` + 背景/カットイン。起動中に RAM キャッシュとタイト境界テーブルを構築済み。

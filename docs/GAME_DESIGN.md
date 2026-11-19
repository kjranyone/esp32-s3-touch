# ゲームデザイン — 『ルミの水晶庭園』(v4.0: 全50ウェーブ・ローグライト)

**ジャンル**: 全50ウェーブ完結型ローグライト・オートバトラー (ESP32-4848S040C_I_Y_3 / 480×480 スマートタッチスクリーン)
**コンセプト**: 初期は弱い **LUMI ★1 単独** から出発し、倒した敵が落とすクリスタルで **ルートボックス (`LOOTBOX: PICK 1`)** を開け、**36種チャーム** と **全64人 (`N/R/SR/SSR`) の仲間スカウト** だけでパーティを構築。床ハザードや呪い弾でHPを削ってくる敵の波と、**HP 50% / 30% / 10% で固有アクションを発動する 6 種のボス** を突破し、**Wave 50 クリアのエンディング** を目指す。クリア率 **約 1% (100回に1回)** の高難易度バランス。

> v3.x までの「無限ウェーブ + 放置収入 + スキルツリー」から全面刷新。延々と続く放置ゲームではなく、毎ラン 1 人から組み上げて 50 ウェーブ完走を目指す有限ローグライトへ移行した。

---

## 1. コアループ & 敗北 / 完走ルール

1. **弱いソロスタート**: 各ランは `#00 LUMI ★1` (`HP=92, ATK=6.5, NOVICE`) 単独・チャーム 0・放置収入 0 で Wave 1 から開始する。
2. **戦って稼ぐ**: 放置収入 (`idle_x10`) は撤廃。クリスタルは敵撃破ドロップとウェーブ突破報酬のみから獲得する (`add_drops`)。
3. **ルートボックス (3択ドラフト)**: 所持クリスタルが `box_cost()` に達する、またはボス撃破の無料ボックス (`free_boxes`) を得たら右下の `BOX` ボタンから開封。**チャーム** と **Scout New Talent** が混在する 3 枚から 1 枚を選ぶ。
4. **部分回復の緊張感**: 通常ウェーブ突破時の HP 回復は **12%** (`wave_heal_pct=12`)、ボス撃破時は **45%** (チャーム `CAMP TEA` / `SOLAR HERB` で上乗せ可能)。敵の削りダメージが次のウェーブへ重く響く。
5. **決着の明確化**:
   - **全滅 (`DIR_GAME_OVER`)**: 全員戦闘不能 (※SSR `PHOENIX ASH` 所持時は 1 ラン 1 度だけ HP50% で全体蘇生) で **敗北・ラン終了**。画面タップで能力引き継ぎなしの Wave 1 (`start_new_run()`) へ戻る。
   - **Wave 50 踏破 (`DIR_ENDING`)**: 最終ボス `ECLIPSE GOD CALAMITY` を撃破すると完全浄化エンディング (`CRYSTAL GARDEN SAVED!!` + Hall of Fame + `SC_CUTIN_LUMI_WIN`) へ到達する。

---

## 2. 成長要素: チャーム 36 種 & キャラクター 64 人

### ルートボックス & 3人パーティ編成 (`ALLY_N = 3`)

- **価格スケール**: `raw = 35 + 24*n + (n^1.46)*8.5` (`MERCHANT DICE` で最大 -50% まで割引)。ボス撃破時 (`Wave < 50`) は無料ボックスを 1 個獲得。
- **スカウト挙動**:
  - 空きスロットあり → **HIRE HERO** (最大 3 人まで出撃増員)
  - 編成中と同キャラを引く → **RANK UP (`*1 -> *2 ...`, 最大 `*9`)** (基礎能力 +28%/Rank + 体力回復)
  - 3/3 満員で別キャラを引く → **SWAP SLOT UI (`PARTY FULL - REPLACE SLOT`)** で入れ替え先を選択可能 (`[X]` でキャンセルして 3 択へ戻ることも可)
- **レアリティ排出重み (`roll_rarity`)**:
  `luck = wave * 0.25 + scout_luck * 0.45` に応じ、`N / R / SR / SSR` の出現率が後半ほど上昇する。

### キャラクター 64 人 (`src/roguelite_data.h`)

| レアリティ | 人数 | 代表キャラ | 特徴 |
|---|---|---|---|
| **COMMON `[N]`** | 26 | `LUMI`, `PIP`, `RENO`, `TINA`, `KAI`, `BERN`, `NORA` 他 | 初期主力〜序盤の土台 |
| **RARE `[R]`** | 20 | `MIRA`, `ELENA`, `DANTE`, `CEDRIC`, `IGNIS`, `FREYA` 他 | 役割特化・優秀なTrait |
| **EPIC `[SR]`** | 12 | `GORO`, `KAGURA`, `ALISTAR`, `SELENE`, `CLARICE`, `ARTEMIS` 他 | 高基礎値 + 強力な単体/支援性能 |
| **LEGEND `[SSR]`** | 6 | `SERAPHIA`, `VALKYRIA`, `CHRONOS`, `SOLARIS`, `CELESTIA`, `ASTRAEA` | 切り札級の基礎ステ & Trait |

- **8 種の戦闘スタイル (`CombatStyle`)**: `COMBO_BLADE` (3段+打上), `FLURRY_DART` (高速ダッシュ+会心+8%), `MAGIC_BOLT` (追尾弾+回避後退), `SPREAD_VOLLEY` (3方向対空扇射), `SNIPER_LANCE` (長射程貫通槍), `HEAVY_SLAM` (重AoE+被ダメ-18%), `PALADIN_WARD` (3段毎小回復＆状態短縮), `ALCH_MEND` (遠距離攻撃+最弱味方回復/状態異常解除)。
- **12 種のパッシブ Trait (`TraitId`)**: `BERSERK`, `VAMP`, `CRIT`, `WARDEN`, `HASTE`, `EXEC`, `THORNS`, `SCAVENGER`, `FROST`, `RAGE`, `BARRIER`, `NONE`。
- **6 種の必殺技 (`UltKind`)**: `CRYSTAL_RAIN`, `AQUA_STORM`, `GRAND_SLAM`, `SANCTUARY` (全体32%回復+全浄化+無敵), `STAR_BARRAGE` (10連追尾星弾), `OVERDRIVE` (広範囲斬撃+全体MP+35 & ヘイスト)。
- **Flash 肥大化ゼロの色替え描画**: ベース 3 アトラス (`LUMI / MIRA / GORO`) に対し、輪郭線・白ハイライト・暖色肌を保護した動的 RLE パレットリマップ (`remap_span_palette` in `src/actor_sprites.inc`) で全 64 人＋ボス群を描き分ける。

### パッシブチャーム 36 種 (`N:12 / R:12 / SR:8 / SSR:4`)

31 種の `CharmEff` により攻撃力・HP・会心・速度・Vamp・秒間リジェネ・**床/毒/火傷ダメ軽減 (`ANTIDOTE VIAL`, `GUARD CLOAK`)**・連鎖電撃 (`CHAIN SPARK`)・追撃弾 (`TWIN ECHO`)・必殺異常浄化 (`PURE RIBBON`)・ボス特効 (`GIANT BANE`)・ハイリスク高火力 (`GLASS PRISM`: ATK+42% / HP-16%)・**1 ラン 1 回全滅蘇生 (`PHOENIX ASH`)** などのシナジーを組む。

### メタアンロック (排出プール拡張のみ)

敗北時の基礎ステータス持ち越しは **一切なし**。最高到達ウェーブ (`best_wave`)・挑戦回数 (`run_number`)・クリア実績に応じた 9 段階 (`unlock_tier 0..8`) で、ルートボックスの排出プールが段階的に拡張される（初期 **24人 / 19チャーム** → 最大 **64人 / 36チャーム**）ほか、図鑑 (`CODEX: X/64`) を埋めていく楽しみを提供する。

---

## 3. 嫌がらせ敵 & ボス 50% / 30% / 10% アクション

### HP を執拗に削る雑魚敵バリエーション

| 敵種別 | サブ種別・行動 | プレイヤーへの嫌がらせ |
|---|---|---|
| **Slime (`E_SLIME`)** | `sub=0` Toxic (`A`/`P`), `sub=1` Leech (`R`/MP減), `sub=2` Lava Bomber (`wave>=3`, Wave 2 からは死亡爆発) | 死亡時や着弾時に **毒水溜まり・燃焼クレーター (`FX_HAZARD`)** を残留させ持続ダメージ。Leech は味方の MP を 14 奪う。 |
| **Bat (`E_BAT`)** | `sub=0` Curse Sniper, `sub=1` Blood Diver | 低 HP の後衛を優先ターゲット (`60%`)。Sniper は **スロウ付与の呪い針**, Diver は急降下吸血 (`HP 8%回復`) + ヒットアンドアウェイ。 |
| **Golem (`E_GOLEM`)** | `sub=0` Magma Mortar, `sub=1` Thorn Guard | Mortar は **赤予告リング付きの迫撃砲 (`spawn_telegraph_ring`)** を後衛へ投下して燃焼床化。Guard は物理攻撃を 20% 反射する棘鎧。 |

### ボス 6 種 (`Waves 5, 10, ..., 50`) と 3 段階しきい値アクション

| Wave | ボス名 | 50% HP アクション | 30% HP アクション | 10% HP アクション |
|---|---|---|---|---|
| 5, 30 | `DREAD THORN TITAN` | `THORN GARDEN` (毒床展開) | `BRIAR SWARM` (護衛召喚) | `OVERGROWTH` (全画面棘衝撃) |
| 10, 35 | `VENOM QUEEN MIASMA` | `ACID FLOOD` (強毒散布) | `BROOD CALL` (増援召喚) | `MELTDOWN MIST` (猛毒波状) |
| 15, 40 | `IRONCLAD COLOSSUS` (1.33×巨神骨格) | `MAGMA MORTAR` (多連迫撃砲) | `AEGIS QUAKES` (棘鎧+地震) | `CATACLYSM` (焦土轟爆) |
| 20, 45 | `NOCTURNE ARCHFIEND` | `SHADOW VOLLEY` (ワープ弾幕) | `BLOOD ECLIPSE` (吸血+増援) | `ABYSS TEMPEST` (全方位呪弾) |
| 25 | `FROSTBANNER SOVEREIGN` | `PERMAFROST` (氷結針+氷床) | `GLACIAL GUARD` (極寒反撃鎧) | `ABSOLUTE ZERO` (凍結嵐) |
| **50** | **`ECLIPSE GOD CALAMITY`** | **`VOID RIFT`** | **`ECLIPSE NOVA`** | **`LAST JUDGMENT`** |

- **一撃スキップ防止 & 視覚フィードバック**: `hit_enemy()` 側で次しきい値直前までクランプするため、50% → 30% → 10% を飛ばさず必ず各アクションを発動する。発動時は 34F のスーパーアーマー、衝撃波演出、`50%/30%/10% PHASE` バナーを表示し、上部ボス HP バーにも **50% / 30% / 10% の金色/白ノッチ** を描画する。

---

## 4. 「100回に1回クリア (~1%)」のゲームバランス根拠

- `src/roguelite_data.h` を直接パースして戦闘・ルートボックス・状態異常・ボス 50/30/10% アクションを再現するモンテカルロ検証スクリプト `tools/sim_roguelite_balance.py` で **計 10,000 ラン** を実測 (`docs/evidence/roguelite-balance-sim.txt`)：
  - **新規セーブ 50キャンペーン×100ラン (計 5,000 ラン)**: **クリア率 `0.88%`**、平均到達 `Wave 20.04`、敗退分布 `[W1-4: 34, W5-9: 583, W10-19: 2679, W20-29: 1043, W30-39: 463, W40-49: 154]`
  - **全プール解禁後 5,000 ラン**: **クリア率 `1.20%`**、平均到達 `Wave 20.33`、敗退分布 `[W1-4: 44, W5-9: 537, W10-19: 2696, W20-29: 1010, W30-39: 468, W40-49: 185]`
- **序盤安全弁**: 初期ソロ LUMI が Wave 1 で即死しないよう、`Wave 1-2` は Bomber Slime を封印し与ダメ補正 (`intro_soft = 0.65 / 0.82`) を適用。実機テレメトリでも初期 Solo LUMI が Wave 1 (`80/92 HP`)→ Wave 3 (`156 crystals`) へ安定到達することを確認済み。

---

## 5. 操作・HUD・手応え継承仕様 (480×480)

- **上部 HUD (`y=0..55`)**: リーダー HP/名前、`RUN# / CHARM数`、クリスタル残高、目標行 (`BOX COST` / `BOX READY!` / `FREE BOX Xn` / `DRAFT PICK OPEN!`)、`WAVE x/50` or `BOSS x/50`、`POOL x/64` (なでなで中は `PET m:ss`)。
- **戦闘領域 (`y=56..407`)**: 3 層パララックス水晶庭園背景。敵/味方の足元にはハザード (毒/燃焼/氷) や予告リングを描画。縦スワイプで「なでなで回復 (+14% HP + 攻撃バフ)」。
- **ミニマップ (`y=408..424`)**: **毎フレーム描画 (`map_dirty = true`)** により味方・敵・ハザード位置とカメラ枠を遅延ゼロで更新。
- **下部パーティ + BOX ボタン (`y=425..479`)**: 3 枠カード (`名前/Rank/Shield青バー/HP/状態異常タグ Poison|Burn|Slow/ULTゲージ`、空枠は `EMPTY / SCOUT BOX`) と右下 `BOX` ボタン (開封可能時は金色ハイライト)。
- **継承された手応え原則**:
  - **全体ヒットストップは使用しない**: オートバトルのテンポ維持のため時間停止を入れず、被弾側微振動・ノックバック・白黒交互フラッシュ・スクワッシュ＆ストレッチ・明暗両対応ヒットマークで手応えを表現。
  - **ダメージ・回復・ドロップ数字は最前面 (`layer = 2`)** + 黒アウトライン (`draw_text_outline_band`) で視認性を担保。

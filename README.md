# ルミの水晶庭園 (Crystal Garden)

ESP32-S3 スマートスクリーン (JingCai ESP32-4848S040C_I_Y_3, 4.0" 480×480 タッチ) 上で動く、
**見て楽しむ放置系オートバトラー**。クリスタルの精霊少女「ルミ」と味方ユニットが、
AI で自動戦闘する様を観戦し、貯まったクリスタルでスキルツリーを伸ばしていくゲーム。

![現在の状態](docs/evidence/bootlog.txt) ※スクリーンショットは `shot` コマンドで取得可

## クイックスタート

### Web エミュレータ (GitHub Pages / ローカル)

実機と同一の C++ ゲームエンジン (`src/beat.cpp`, `src/garden_game.cpp`, `src/game_params.cpp`) を
Emscripten (WebAssembly) でブラウザ実行できます。

- **公開 URL (GitHub Pages)**: `https://kjranyone.github.io/esp32-s3-touch/`
  (`main` ブランチへの push で `.github/workflows/deploy-web.yml` が自動ビルド＆デプロイします)
- **ローカルビルド＆確認**:
  ```powershell
  powershell -ExecutionPolicy Bypass -File web/build.ps1   # ローカル em++ または Docker (emscripten/emsdk) でビルド
  python -m http.server 8080 --directory web/dist          # http://localhost:8080/ で確認
  ```

### 実機ビルド (PlatformIO)

```powershell
pio run              # ビルド
pio run -t upload    # デバイスへ書き込み (COM5 / 460800)
pio device monitor   # シリアルモニタ (115200)
```

- デバイス: JingCai ESP32-4848S040C_I_Y_3 互換ボード(CH340 で接続)
- 初回は toolchain のダウンロードが走ります(数分)

## ドキュメント

| ドキュメント | 内容 |
|---|---|
| **docs/HANDOVER.md** | **まずこれ**: 引き継ぎ(現状・アーキテクチャ・開発手順・残課題) |
| docs/GAME_DESIGN.md | ゲームデザイン v3(放置系オートバトル設計 + 桜井スキル適用ノート) |
| docs/DEVICE_SPEC.md | デバイス実測スペック・ピン配置・純正 FW バックアップ |
| docs/ART_GUIDE.md | キャラ画像生成指示書(GPT Imagen 用) |
| docs/ANIMATION_ATLASES.md | 全7種の連番スプライト・再変換・プレビュー |

## 開発のポイント(要約)

- **描画は LVGL を使わない**: フレームバッファ (PSRAM) をゲームが単独所有し、
  毎フレーム `esp_cache_msync` → `switchFrameBufferTo` で表示。(理由は HANDOVER §5)
- **調整はシリアルから**: `dmg_pct 150` 等で実行中にパラメータ変更(#046)
- **デバッグ**: `dump`(座標)/ `shot`(スクショ)/ `give`(通貨付与)
- ゲームデザインの意思決定は「桜井政博のゲーム作るには」の原則スキルを参照
  (`external/sakurai-skill` — 未導入なら clone して docs/GAME_DESIGN.md §12 のように適用)

## ライセンス・クレジット

- **Created by Kojiro Tanaka** — X: [@mu_sette](https://x.com/mu_sette) / Web: [https://oto-nari.com](https://oto-nari.com)
- ボード定義: ESP32_Display_Panel (Apache-2.0, © Espressif Systems) を使用
- 「桜井政博のゲーム作るには」原則は有志による非公式 Agent Skill の要約(要出典は元動画)
- キャラクター「ルミ」および本ゲームデザインはオリジナル

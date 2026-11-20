# esp32-s3-touch — Modular 3D ESP32-S3 Hardware Workbench & Firmware Simulator

実機 ESP32-S3 にそのまま焼ける **本物の C++ ファームウェア** を、物理準拠の 3D ボード・ディスプレイ・外付けペリフェラル構成の上で検証・共有できるポータブル・ハードウェアサンドボックス。

- **ライブデモ (GitHub Pages)**: https://kjranyone.github.io/esp32-s3-touch/
- **単一タグ埋め込み例 (`<esp32-workbench>`)**: https://kjranyone.github.io/esp32-s3-touch/embed.html

## 主な特徴

- **実機 ⇔ Web WASM 100% 同一 C++ ファームウェア (`src/hal/hw_hal.h`, `src/hal/hw_app.h`)**
  - 同一ソースを PlatformIO（実機 ESP32-S3）と Emscripten（Web WASM カートリッジ）の双方へビルド。
- **マルチボード ＆ 着脱式 3D ペリフェラル (`web/sim/`)**
  - **ボード**:
    - `ESP32-4848S040C_I_Y_3`: 物理準拠 2048px 背面 PCB、ST7701 4.0" 480×480 RGB565、GT911 タッチ、NS4168 I2S / `J3` 3ch リレー（背面 `R21-23` ⇔ `R25-27` 0Ω 抵抗の排他切替を 3D 再現）、クリック可能な `SW1 BOOT` / `SW2 RST`。
    - `ESP32-S3-DevKitC-1-N16R8`: 400 ホール・ブレッドボード実装、44 ピン正規シルク印刷、オンボード `GPIO48` RGB LED、着脱式 `0.96" 128×64 SSD1306` 2色 I2C OLED。
  - **ペリフェラル**:
    - `3ch Relay + Loads` (`GPIO 1/2/40`): Songle 青色リレー 3ch ＋ エジソンランプ / 冷却ファン / 回転ビーコン＋電磁クリック音。
    - `WS2812B 16-LED Ring` (`GPIO 48` RMT): 16 灯 5050 SMD NeoPixel リング＋床面リアルタイム照り返し。
    - `EC11 Rotary Encoder + BTN` (`ENC0 / GPIO 47`): 3D 空間でドラッグ／ホイール回転＆クリック押下可能。
- **空間 IR 音響エンジン (`SpatialAudio.js`)**: スピーカー物理座標とカメラ角度に連動する筐体 IR → 回折フィルタ → 3D HRTF → ステレオ室内 IR (`STUDIO / HALL / DRY`)。
- **ポータブル構成共有 ＆ ローカル WASM 書き込み**:
  - 左上ピルからボード・ペリフェラル・ジャンパを自在に組み替え、**Share URL** / **JSON** で構成共有。
  - ローカルビルドした `.js` + `.wasm`（または `.json` リグ定義）をウィンドウへドラッグ＆ドロップして即座に書き込み実行可能。
  - `<esp32-workbench app="oled_pocket" view="iso" intro="0"></esp32-workbench>` で外部サイトへ 1 タグ埋め込み可能。

## 組み込みファームウェア・カートリッジ (`src/apps/`)

| カートリッジ | PlatformIO 環境 | デフォルトボード / 画面 | 内容 |
|---|---|---|---|
| **Crystal Garden** | `BOARD_JINGCAI_ESP32_4848S040C_I_Y_3` | `ESP32-4848S040C` (480×480) | 全 50 ウェーブ・ローグライト オートバトラー（LVGL 不使用 32 行 SRAM バンド合成） |
| **I/O Workbench** | `io_workbench` | `ESP32-4848S040C` + 3ch Relay + WS2812B + EC11 | リレー負荷・16灯リング・EC11 ノブ・`R25-27` 排他切替の統合制御盤 |
| **OLED Pocket Synth** | `oled_pocket` | `ESP32-S3-DevKitC-1` + 0.96" OLED (128×64) | リアルタイム DSP シンセ＋オシロスコープ＋8 ステップアルペジエータ |
| **HW Diagnostics** | `hw_diag` | `ESP32-4848S040C` (480×480) | GT911 タッチ軌跡・RGB565・GPIO・オーディオ診断ツール |

## ビルド ＆ 開発手順

### Web シミュレータ一括ビルド (Emscripten / Docker)

```powershell
powershell -ExecutionPolicy Bypass -File web/build.ps1   # 全カートリッジを WASM 化し web/dist へ出力
python -m http.server 8080 --directory web/dist          # http://localhost:8080/ で確認
```

### 実機ビルド ＆ 書き込み (PlatformIO)

```powershell
pio run                                  # デフォルト (Crystal Garden)
pio run -e io_workbench                  # I/O Workbench
pio run -e oled_pocket                   # OLED Pocket Synth (DevKitC-1)
pio run -e hw_diag                       # HW Diagnostics
pio run -t upload --upload-port COM6     # 実機へ書き込み
```

## ドキュメント

| ドキュメント | 内容 |
|---|---|
| `docs/DEVICE_SPEC.md` | ESP32-4848S040C 実測スペック・ピン配置・排他制約 |
| `docs/UX_GUIDELINE.md` | 4.0" 480×480 タッチ画面の視認性・操作性ガイドライン |
| `docs/HANDOVER.md` | Crystal Garden アーキテクチャ・開発履歴 |
| `docs/GAME_DESIGN.md` | Crystal Garden ゲームデザインノート |

## ライセンス・クレジット

- **Created by Kojiro Tanaka** — X: [@mu_sette](https://x.com/mu_sette) / Web: [https://oto-nari.com](https://oto-nari.com)
- ボード定義: ESP32_Display_Panel (Apache-2.0, © Espressif Systems)

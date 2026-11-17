# デバイス解析レポート (DEVICE SPEC)

- 解析日: 2026-11-17
- 接続方法: USB Serial **CH340 → COM5** (VID 1A86 / PID 7523, wch.cn 製ドライバ CH341SER_A64)
- 解析手法: esptool (chip_id / flash_id / read_flash) + 起動ログ取得 + フラッシュ(app0 / NVS)文字列解析

> 注意: この PC に繋がっていた別の USB 機器 `VID_328F&PID_0121` は eMeet SmartCam C960(ウェブカメラ)であり、本ガジェットとは無関係。

---

## 1. 確定済みスペック(実測値)

| 項目 | 実測値 | 根拠 |
|---|---|---|
| SoC | **ESP32-S3 (QFN56) rev v0.2** | esptool `chip_id` |
| 無線 | WiFi + BLE | esptool Features |
| PSRAM | **内蔵 8MB (octal, AP_3v3)** → モジュールは **ESP32-S3R8 系** (ESP32-S3-WROOM-1-N16R8 相当) | esptool Features |
| Flash | **16MB, quad (4データライン)**, JEDEC ID `0x68 0x4018` (容量コード 0x18 = 128Mbit = 16MB。メーカー 0x68 は Boya Micro 系の可能性) | esptool `flash_id` |
| 起動モード | Flash **DIO / 80MHz** (clock div:1), SPIWP:0xee | 起動ログ |
| 水晶 | 40MHz | esptool |
| MAC | `ac:27:6e:a4:2e:44` | esptool |
| USB-Serial | CH340 (COM5)。**921600 baud はデータ破損するため 460800 までが安全** | 実測 |
| 画面 | **4.0 インチ 正方形 480×480** (RGB565 パラレル + ST7701 / 静電容量式 GT911) | ユーザー確認 + FW 文字列解析 |

### ボード同定: **Shenzhen Jingcai Intelligent「ESP32-4848S040C_I_Y_3」(確定)**

以下の証拠により確定:

1. ハード構成が完全一致: 4.0" 480×480 / ST7701(3-wire SPI + RGB565)/ GT911(I2C)/ ESP32-S3 + 8MB octal PSRAM + 16MB Flash
2. NVS に保存されていた WiFi SSID **「JINGCAI-1」** はベンダー名(Shenzhen **Jingcai** Intelligent)と一致
3. Espressif 公式ライブラリ **ESP32_Display_Panel v1.0.4 が本ボードを内蔵サポート**(`BOARD_JINGCAI_ESP32_4848S040C_I_Y_3`)しており、ピン定義が本デバイスの構成と一致
4. 純正 FW も ESP32_Display_Panel v0.x 系で構築されている(後述)

- ベンダー製品ページ: https://www.displaysmodule.com/sale-41828962- (SKU: ESP32-4848S040C_I_Y_3)
- ベンダー資料一式: http://pan.jczn1688.com/directlink/1/ESP32%20module/4.0inch_ESP32-4848S040.zip

---

## 2. フラッシュパーティション(実測)

```
# Name,   Type, SubType, Offset,   Size, Flags
nvs,      data, nvs,     0x9000,   20K,
otadata,  data, ota,     0xe000,   8K,
app0,     app,  factory, 0x10000,  8064K,
coredump, data, coredump,0x7f0000, 64K,
```

- OTA スロット無し / SPIFFS 無しのシンプル構成
- 0x800000〜0xFFFFFF(後半 8MB)はパーティション未定義(未使用領域)
- 純正 FW の app0 は約 4.7MB 使用

---

## 2.5 確定ピン配置 (ESP32_Display_Panel v1.0.4 ボード定義より)

| 機能 | GPIO | 備考 |
|---|---|---|
| LCD ST7701 初期化 SPI (3-wire) CS / SCK / SDA | 39 / 48 / 47 | パネル初期コマンド送信用 |
| RGB HSYNC / VSYNC / DE / PCLK | 16 / 17 / 18 / 21 | PCLK 26MHz |
| RGB データ B0〜B3 | 4, 5, 6, 7 | RGB565 16bit |
| RGB データ B4 / G0 / G1 / G2 | 15 / 8 / 20 / 3 | |
| RGB データ G3 / G4 / G5 | 46 / 9 / 10 | |
| RGB データ R0〜R4 | 11, 12, 13, 14, 0 | |
| RGB タイミング | HPW10 / HBP10 / HFP20 / VPW10 / VBP10 / VFP10 | |
| タッチ GT911 I2C SCL / SDA | 45 / 19 | 400kHz, アドレス 0x5D(または 0x14) |
| バックライト | 38 | PWM (LEDC 1kHz), High = ON |
| LCD リセット / タッチ リセット・INT | 未接続 (-1) | |

---

## 3. 純正ファームウェアの解析

### ビルド環境(文字列解析による)
| 項目 | 値 |
|---|---|
| ビルドシステム | **PlatformIO** (Linux root でビルド, `.platformio/packages/framework-arduinoespressif32@src-87caf4f9…`) |
| フレームワーク | **arduino-esp32 (IDF v5.1.1 ベース → core 3.0.x)** |
| 表示ライブラリ | **ESP32_Display_Panel v0.x 系** (`ESP_PanelBus_RGB`, `ESP_PanelHost`, `lib/ESP32_Display_Panel/src/lcd_touch/GT911.cpp`) |
| LCD ドライバ | `esp_lcd_new_panel_st7701` / `panel_st7701_send_init_cmds` (esp_lcd 形式の ST7701 ドライバ) |
| タッチドライバ | GT911 (`esp_lcd_touch_new_i2c_gt911`) |
| UI | **LVGL 8.3.7** + 独自 BSP (`src/bsp_lvgl_port.cpp`) |

### アプリの挙動(起動ログ + 埋め込み JSON)
- 起動時に **UI 定義 JSON(17 要素)** をパースするイベント駆動 UI
- 機能: WiFi スキャン/接続 UI、**open-meteo** 天気予報(gooding-api 地理コード検索)、淘宝(Taobao)時刻 API による時計、**UI からの GPIO 操作(GPIO 1 / 2 / 40 をトグル)**
- 中国ベンダー製の汎用スマートスクリーン FW と考えられる

### NVS の内容(注意点)
- WiFi 資格情報が `nvs.net80211` / `sta.apinfo` に保存されている(SSID: `JINGCAI-1` ほか)
- **平文に近い形で資格情報が残る**ため、再配布・解析ログ公開時は消去推奨
  (消去方法: `esptool.py --port COM5 erase_region 0x9000 0x5000`)

---

## 4. 開発環境(このリポジトリの雛形)

Espressif 公式の ESP32_Display_Panel PlatformIO 例(`lvgl_v8_port`)をベースに、本ボード向けに調整済み。

| 項目 | 設定 |
|---|---|
| Platform | pioarduino `53.03.11` + arduino-esp32 **3.1.1** (S3 の RGB LCD 画面揺れ対策済み高速 libs) |
| ボード定義 | ESP32_Display_Panel v1.0.4 内蔵 `BOARD_JINGCAI_ESP32_4848S040C_I_Y_3` |
| UI | LVGL **8.4.0** (`src/lv_conf.h`, `src/lvgl_v8_port.cpp` は公式例から取得) |
| 書き込み | COM5 / 460800 baud(CH340 の実測に合わせる) |
| パーティション | `default_16MB.csv`(OTA x2 + SPIFFS) |

### 使い方
```powershell
pio run                 # ビルド
pio run -t upload       # 書き込み (COM5)
pio device monitor      # シリアルモニタ (115200)
```
初回書き込み後、画面に「Hello World!」と LVGL/ライブラリのバージョンが表示されれば環境 OK。

### 未確定事項(TODO)
| 項目 | 確認方法 |
|---|---|
| 実機での表示・タッチ動作 | `pio run -t upload` 後の実機確認 |
| スピーカー / SD カードスロット等の追加 HW | 実機目視 + ベンダー資料 (pan.jczn1688.com) |

### バックアップ
- 純正ファーム完全バックアップ: `backup/app0.bin` (8,257,536 bytes, 検証ハッシュ OK)
  - 復元: `esptool.py --port COM5 --baud 460800 write_flash 0x10000 backup/app0.bin`
- `backup/nvs.bin` (0x9000 20K), `backup/partition.bin` (0x8000 4K)
- `docs/evidence/bootlog.txt` — 起動ログ

### 既知の注意点
- このボードの CH340 は 921600 baud でデータ破損が発生 → `upload_speed = 460800` を採用
- esptool での接続直後は RTS 経由で自動リセットされる

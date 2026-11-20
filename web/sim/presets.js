/**
 * presets.js — Declarative Hardware Workbench & Firmware Cartridge Registry
 *
 * Each preset pairs a physical board manifest, mounted 3D peripherals,
 * initial solder-jumper configuration, and a real ESP32-S3 C++ WASM cartridge.
 */

export const CARTRIDGES = {
  crystal_garden: {
    id: 'crystal_garden',
    name: 'Crystal Garden',
    tag: '4848S040C · Game',
    board: 'esp32_4848s040c',
    peripherals: [],
    isoScale: 1.0,
    script: 'apps/crystal_garden.js',
    exportName: 'createCrystalGarden',
    jumpers: { 0: 0 }, // R21-23 populated (NS4168 I2S Audio)
    uartPlaceholder: 'UART 115200 (boss / ult / box / wave 50 / ?)',
  },
  io_workbench: {
    id: 'io_workbench',
    name: 'I/O Workbench',
    tag: 'Relay · LED · EC11',
    board: 'esp32_4848s040c',
    peripherals: ['relay_load_3ch', 'ws2812_ring', 'rotary_encoder'],
    isoScale: 1.26,
    script: 'apps/io_workbench.js',
    exportName: 'createIoWorkbench',
    jumpers: { 0: 1 }, // R25-27 populated (3ch Relay Header J3 active!)
    uartPlaceholder: 'UART 115200 (relay 3 1 / all 1 / led 1 / enc 3 / ?)',
  },
  oled_pocket: {
    id: 'oled_pocket',
    name: 'OLED Pocket Synth',
    tag: 'DevKitC · 0.96" OLED',
    board: 'esp32s3_devkitc',
    peripherals: [
      {
        type: 'ws2812_ring',
        opts: { position: [-7.5, 0, -0.3], rotationY: 0.30, wireTarget: [-4.6, 0.86, 0.45] },
      },
      {
        type: 'rotary_encoder',
        opts: { position: [7.1, 0, 1.8], rotationY: -0.26, wireTarget: [4.6, 0.86, 0.30] },
      },
    ],
    isoScale: 1.22,
    script: 'apps/oled_pocket.js',
    exportName: 'createOledPocket',
    jumpers: { 0: 0 }, // Audio active on piezo/I2S speaker
    uartPlaceholder: 'UART 115200 (bpm 140 / wave 2 / play 1 / note 64 / ?)',
  },
  hw_diag: {
    id: 'hw_diag',
    name: 'HW Diagnostics',
    tag: '4848S040C · Bench',
    board: 'esp32_4848s040c',
    peripherals: [],
    isoScale: 1.0,
    script: 'apps/hw_diag.js',
    exportName: 'createHwDiag',
    jumpers: { 0: 0 },
    uartPlaceholder: 'UART 115200 (gpio 1 1 / led 1 / tone / status / ?)',
  },
};

export const PRESET_LIST = [
  CARTRIDGES.crystal_garden,
  CARTRIDGES.io_workbench,
  CARTRIDGES.oled_pocket,
  CARTRIDGES.hw_diag,
];

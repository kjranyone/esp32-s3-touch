/**
 * Workbench.js — Portable, Composable ESP32-S3 3D Hardware Sandbox Controller
 *
 * Orchestrates StudioScene, VirtualBus, CartridgeHost, SpatialAudioEngine,
 * Board modules, and Pluggable 3D Peripherals. Supports runtime board swapping,
 * live peripheral toggling, JSON/URL rig serialization, and local WASM flashing.
 */
import { StudioScene } from './core/StudioScene.js';
import { VirtualBus, JUMPER_AUDIO_RELAY } from './core/VirtualBus.js';
import { CartridgeHost } from './core/CartridgeHost.js';
import { SpatialAudioEngine } from './core/SpatialAudio.js';
import { Esp32_4848S040C } from './boards/Esp32_4848S040C.js';
import { Esp32S3_DevKitC } from './boards/Esp32S3_DevKitC.js';
import { RelayLoad3ch } from './peripherals/RelayLoad3ch.js';
import { Ws2812Ring } from './peripherals/Ws2812Ring.js';
import { RotaryEncoder } from './peripherals/RotaryEncoder.js';
import { CARTRIDGES, PRESET_LIST } from './presets.js';

export const BOARDS = [
  { id: 'esp32_4848s040c', name: 'ESP32-4848S040C', tag: '4.0" 480×480' },
  { id: 'esp32s3_devkitc', name: 'ESP32-S3-DevKitC', tag: '0.96" OLED' },
];

export const PERIPHERAL_CATALOG = [
  { id: 'relay_load_3ch', name: '3ch Relay + Loads', tag: 'IO1/2/40' },
  { id: 'ws2812_ring',    name: 'WS2812B 16-LED',    tag: 'IO48 RMT' },
  { id: 'rotary_encoder', name: 'EC11 Encoder + BTN', tag: 'ENC0/IO47' },
];

const BOARD_FACTORIES = {
  esp32_4848s040c: () => new Esp32_4848S040C(),
  esp32s3_devkitc: () => new Esp32S3_DevKitC(),
};

const PERIPHERAL_FACTORIES = {
  relay_load_3ch: (board, opts) => new RelayLoad3ch(board, opts),
  ws2812_ring:    (board, opts) => new Ws2812Ring(board, opts),
  rotary_encoder: (board, opts) => new RotaryEncoder(board, opts),
};

// Board-tailored default placement & Dupont routing for any custom peripheral combination
const BOARD_PERIPH_OPTS = {
  esp32_4848s040c: {
    relay_load_3ch: { position: [-7.1, 0, -0.8], rotationY: 0.24 },
    ws2812_ring:    { position: [6.4, 0, -0.4], rotationY: -0.26, wireTarget: [2.1, 0.24, -1.32] },
    rotary_encoder: { position: [5.5, 0, 3.1], rotationY: -0.32, wireTarget: [2.4, 0.22, -1.28] },
  },
  esp32s3_devkitc: {
    relay_load_3ch: { position: [-8.6, 0, -2.5], rotationY: 0.28 },
    ws2812_ring:    { position: [-7.5, 0, 1.9], rotationY: 0.30, wireTarget: [-4.6, 0.86, 0.45] },
    rotary_encoder: { position: [7.1, 0, 1.8], rotationY: -0.26, wireTarget: [4.6, 0.86, 0.30] },
  },
};

export class Workbench {
  constructor(viewportEl, callbacks = {}) {
    this.callbacks = callbacks;
    this.bus = new VirtualBus();
    this.host = new CartridgeHost();
    this.audio = new SpatialAudioEngine();
    this.board = null;
    this.peripherals = [];
    this.activePeripheralIds = [];
    this.currentCartridge = null;
    this.speedMult = 1;
    this._running = false;
    this._frameIdx = 0;

    this.scene = new StudioScene(viewportEl, {
      onLcdPointer: (type, x, y) => {
        this.audio.ensureStarted();
        if (type === 'down') this.host.touchDown(x, y);
        else if (type === 'move') this.host.touchMove(x, y);
        else if (type === 'up') this.host.touchUp(x, y);
      },
      onPartClick: (meta) => this._handlePartClick(meta),
      onPartDrag: (meta, delta) => this._handlePartDrag(meta, delta),
      onViewChange: (name) => this.callbacks.onViewChange?.(name),
    });

    this.bus.onChange((evType, detail) => {
      if (evType === 'jumper') {
        this.callbacks.onJumperChange?.(detail);
        this.callbacks.onRigChange?.(this.getRigConfig());
      }
    });

    const unlock = () => this.audio.ensureStarted();
    window.addEventListener('pointerdown', unlock, { capture: true, passive: true });
    window.addEventListener('keydown', unlock, { capture: true, passive: true });
    window.addEventListener('pagehide', () => this.host.save());
  }

  _handlePartDrag(meta, delta) {
    if (!meta || !delta) return;
    this.audio.ensureStarted();
    if (meta.kind === 'rotary') {
      meta.peripheral?.onStep(delta);
      this.bus.stepEncoder(meta.encoderId || 0, delta);
      this.audio.playMechanicalClick(delta > 0 ? 2350 : 2050, 0.12);
    }
  }

  _handlePartClick(meta) {
    if (!meta) return;
    this.audio.ensureStarted();

    if (meta.action === 'reset_btn') {
      this.audio.playMechanicalClick(2100, 0.18);
      window.onEmulatorSerial?.('\n[hw] RST pressed -> hardware reset\n');
      this.host.reboot();
      this.bus.attachHost(this.host);
      this._blit(true);
      this.callbacks.onSfxSync?.();
    } else if (meta.action === 'boot_btn') {
      this.audio.playMechanicalClick(1900, 0.15);
      this.bus.setGpioInput(0, true);
      window.onEmulatorSerial?.('[hw] BOOT (GPIO0) pressed\n');
      setTimeout(() => this.bus.setGpioInput(0, false), 120);
    } else if (meta.action === 'toggle_jumper') {
      this.toggleJumper(meta.jumperId);
    } else if (meta.action === 'encoder_click') {
      meta.peripheral?.onPress();
      this.audio.playMechanicalClick(1750, 0.16);
      const id = meta.encoderId || 0;
      this.bus.setEncoderButton(id, true);
      setTimeout(() => this.bus.setEncoderButton(id, false), 110);
    } else if (meta.action === 'tactile_btn') {
      meta.peripheral?.onBtnPress();
      this.audio.playMechanicalClick(1950, 0.16);
      const pin = meta.pin ?? 47;
      this.bus.setGpioInput(pin, true);
      setTimeout(() => this.bus.setGpioInput(pin, false), 110);
    }
  }

  toggleJumper(jumperId = JUMPER_AUDIO_RELAY) {
    const next = this.bus.getJumper(jumperId) ? 0 : 1;
    this.bus.setJumper(jumperId, next);
    this.audio.playMechanicalClick(next ? 1350 : 1650, 0.16);
    const label = next ? 'R25-27 (3ch Relay Header J3)' : 'R21-23 (NS4168 I2S Speaker)';
    window.onEmulatorSerial?.(`[hw] Jumper switched -> ${label}\n`);
    return next;
  }

  mountBoard(boardId) {
    const targetId = BOARD_FACTORIES[boardId] ? boardId : 'esp32_4848s040c';
    if (this.board && this.board.id === targetId) return false;
    this.board = BOARD_FACTORIES[targetId]();
    this.scene.mountBoard(this.board);
    if (this.host.ready && this.host.info) {
      this.board.setDisplaySize(this.host.info.width, this.host.info.height);
      this._blit(true);
    }
    return true;
  }

  mountPeripherals(peripheralSpecs = [], isoScale = null) {
    const boardId = this.board?.id || 'esp32_4848s040c';
    const defaults = BOARD_PERIPH_OPTS[boardId] || BOARD_PERIPH_OPTS.esp32_4848s040c;
    const built = [];
    const ids = [];

    for (const spec of peripheralSpecs) {
      const pid = typeof spec === 'string' ? spec : spec?.type;
      if (!pid || !PERIPHERAL_FACTORIES[pid] || ids.includes(pid)) continue;
      const customOpts = typeof spec === 'string' ? undefined : spec?.opts;
      const opts = customOpts || defaults[pid];
      built.push(PERIPHERAL_FACTORIES[pid](this.board, opts));
      ids.push(pid);
    }

    this.peripherals = built;
    this.activePeripheralIds = ids;
    const scale = isoScale !== null ? isoScale : (ids.length > 0 ? 1.25 : 1.0);
    this.scene.mountPeripherals(built, scale);
  }

  /** Runtime board swap while keeping current firmware & re-wiring peripherals */
  setBoard(boardId) {
    const changed = this.mountBoard(boardId);
    if (!changed) return;
    this.mountPeripherals([...this.activePeripheralIds]);
    if (this._running && !this.scene.welcomeIntro) {
      this.scene.setView(this.scene.currentViewName, false);
    }
    window.onEmulatorSerial?.(`[rig] Board swapped -> ${this.board.name}\n`);
    this.callbacks.onRigChange?.(this.getRigConfig());
  }

  /** Runtime peripheral snap ON/OFF */
  togglePeripheral(periphId) {
    if (!PERIPHERAL_FACTORIES[periphId]) return;
    const next = this.activePeripheralIds.includes(periphId)
      ? this.activePeripheralIds.filter((id) => id !== periphId)
      : [...this.activePeripheralIds, periphId];
    this.mountPeripherals(next);
    if (this._running && !this.scene.welcomeIntro) {
      this.scene.setView(this.scene.currentViewName, false);
    }
    this.audio.playMechanicalClick(next.includes(periphId) ? 1820 : 1280, 0.14);
    window.onEmulatorSerial?.(`[rig] Peripherals -> ${next.length ? next.join(', ') : 'none'}\n`);
    this.callbacks.onRigChange?.(this.getRigConfig());
  }

  /** Export current declarative workbench manifest */
  getRigConfig() {
    return {
      version: 1,
      board: this.board?.id || 'esp32_4848s040c',
      cartridge: this.currentCartridge?.id || 'crystal_garden',
      peripherals: [...this.activePeripheralIds],
      jumpers: { [JUMPER_AUDIO_RELAY]: this.bus.getJumper(JUMPER_AUDIO_RELAY) },
    };
  }

  /** Apply a full declarative rig manifest (from URL, JSON import, or preset) */
  async applyRigConfig(cfg, opts = {}) {
    const cartId = (cfg && cfg.cartridge && CARTRIDGES[cfg.cartridge]) ? cfg.cartridge : 'crystal_garden';
    const preset = CARTRIDGES[cartId];
    const boardId = (cfg && cfg.board && BOARD_FACTORIES[cfg.board]) ? cfg.board : preset.board;
    const periphs = Array.isArray(cfg?.peripherals) ? cfg.peripherals : (preset.peripherals || []);
    const jumpers = cfg?.jumpers || preset.jumpers || { 0: 0 };

    return this.loadCartridge(cartId, {
      ...opts,
      boardOverride: boardId,
      peripheralsOverride: periphs,
      jumpersOverride: jumpers,
    });
  }

  /** Build a portable shareable URL reproducing this exact board + peripherals + cartridge */
  buildShareUrl(baseHref = window.location.href) {
    const cfg = this.getRigConfig();
    const u = new URL(baseHref);
    u.searchParams.set('app', cfg.cartridge);
    u.searchParams.set('board', cfg.board);
    u.searchParams.set('periph', cfg.peripherals.join(','));
    u.searchParams.set('j0', String(cfg.jumpers[0] || 0));
    u.searchParams.set('intro', '0');
    return u.toString();
  }

  static parseRigFromParams(searchParams) {
    const app = searchParams.get('app') || 'crystal_garden';
    const preset = CARTRIDGES[app] || CARTRIDGES.crystal_garden;
    const board = searchParams.get('board') || preset.board;
    let peripherals = preset.peripherals || [];
    if (searchParams.has('periph')) {
      const raw = searchParams.get('periph').trim();
      peripherals = raw ? raw.split(',').map((s) => s.trim()).filter(Boolean) : [];
    }
    const jumpers = { ...(preset.jumpers || { 0: 0 }) };
    if (searchParams.has('j0')) {
      jumpers[0] = Number(searchParams.get('j0')) ? 1 : 0;
    }
    return { version: 1, board, cartridge: preset.id, peripherals, jumpers };
  }

  async loadCartridge(cartridgeId, {
    initCmd = null,
    skipFrames = 0,
    boardOverride = null,
    peripheralsOverride = null,
    jumpersOverride = null,
  } = {}) {
    const cart = CARTRIDGES[cartridgeId] || CARTRIDGES.crystal_garden;
    this.currentCartridge = cart;

    const boardId = boardOverride || cart.board;
    this.mountBoard(boardId);

    const periphs = peripheralsOverride !== null ? peripheralsOverride : (cart.peripherals || []);
    const isoScale = peripheralsOverride !== null ? null : (cart.isoScale || 1.0);
    this.mountPeripherals(periphs, isoScale);

    const jumpers = jumpersOverride || cart.jumpers;
    if (jumpers) {
      for (const [k, v] of Object.entries(jumpers)) {
        this.bus.setJumper(Number(k), v);
      }
    }

    const info = await this.host.load(cart);
    this.board.setDisplaySize(info.width, info.height);
    this.bus.attachHost(this.host);
    this.audio.attach(this.host, this.bus);

    if (initCmd) {
      initCmd.split(';').forEach((c) => { if (c.trim()) this.host.sendUart(c.trim()); });
      this.host.step(1 / 60);
      this.host.render();
    }
    const sf = Math.min(600, Math.max(0, skipFrames | 0));
    for (let i = 0; i < sf; i++) this.host.step(1 / 60);
    if (sf > 0) this.host.render();

    this.bus.syncFromFirmware();
    this._blit(true);

    if (this._running && !this.scene.welcomeIntro) {
      this.scene.setView(this.scene.currentViewName, false);
    }

    this.callbacks.onCartridgeLoaded?.(cart, info);
    this.callbacks.onRigChange?.(this.getRigConfig());
    this.callbacks.onSfxSync?.();
    return info;
  }

  /** Flash locally dropped Emscripten (.js + .wasm) firmware directly onto the active 3D board */
  async flashLocalFiles(jsFile, wasmFile) {
    window.onEmulatorSerial?.(`\n[flash] Flashing local cartridge: ${jsFile.name} + ${wasmFile.name}...\n`);
    const info = await this.host.loadFromFiles(jsFile, wasmFile);
    this.currentCartridge = {
      id: info.id || 'custom_wasm',
      name: info.name || jsFile.name.replace(/\.js$/i, ''),
      tag: `${info.width}×${info.height} · Local`,
      board: this.board?.id || info.defaultBoard || 'esp32_4848s040c',
      uartPlaceholder: 'UART 115200 (help / ?)',
    };
    if (info.defaultBoard && !this.board) {
      this.mountBoard(info.defaultBoard);
    }
    this.board.setDisplaySize(info.width, info.height);
    this.bus.attachHost(this.host);
    this.audio.attach(this.host, this.bus);
    this.bus.syncFromFirmware();
    this._blit(true);
    this.audio.playMechanicalClick(2200, 0.18);
    window.onEmulatorSerial?.(`[flash] Booted '${info.name}' (${info.width}x${info.height}) OK!\n`);
    this.callbacks.onCartridgeLoaded?.(this.currentCartridge, info);
    this.callbacks.onRigChange?.(this.getRigConfig());
    this.callbacks.onSfxSync?.();
    return info;
  }

  _blit(sampleGlow) {
    if (!this.board || !this.host.ready) return;
    const bytes = this.host.getRgbaBytes();
    const glow = this.board.blitRgba(bytes, sampleGlow);
    if (glow) this.scene.setGlowRgb(glow);
  }

  start({ startView = null, skipIntro = false } = {}) {
    if (startView) {
      this.scene.setView(startView, true);
    } else if (!skipIntro) {
      this.scene.startWelcomeIntro();
    } else {
      this.scene.setView('iso', true);
    }

    if (this._running) return;
    this._running = true;

    const STEP_MS = 1000 / 60;
    let lastT = performance.now();
    let accum = 0;

    const tick = (now) => {
      requestAnimationFrame(tick);
      const dtMs = Math.min(100, now - lastT);
      const dtSec = dtMs * 0.001;
      lastT = now;

      if (this.host.ready) {
        accum += dtMs * this.speedMult;
        let steps = 0;
        const maxSteps = this.speedMult >= 2 ? 4 : 2;
        while (accum >= STEP_MS && steps < maxSteps) {
          this.host.step(1 / 60);
          accum -= STEP_MS;
          steps++;
        }
        if (steps > 0) {
          this.host.render();
          this.bus.syncFromFirmware();
          this._blit((this._frameIdx++ % 10) === 0);
          if ((this._frameIdx % 15) === 0) this.callbacks.onSfxSync?.();
        }
      }

      this.board?.update(now, this.bus);
      for (const p of this.peripherals) {
        p.update(dtSec, this.bus, this.audio);
      }
      this.scene.renderFrame(now);
      this.audio.updateSpatial(this.scene.camera, this.board);
    };
    requestAnimationFrame(tick);
  }

  setView(name, immediate = false) { this.scene.setView(name, immediate); }

  toggleSpeed() {
    this.speedMult = this.speedMult === 1 ? 2 : 1;
    return this.speedMult;
  }

  toggleMute() {
    this.audio.ensureStarted();
    if (!this.host.ready) return true;
    const nextMuted = !this.host.audioIsMuted();
    this.host.audioSetMuted(nextMuted);
    this.callbacks.onSfxSync?.();
    return nextMuted;
  }

  cycleIrMode() {
    this.audio.ensureStarted();
    return this.audio.cycleIrMode();
  }

  sendUart(line) { this.host.sendUart(line); }

  static get presets() { return PRESET_LIST; }
  static get boards() { return BOARDS; }
  static get peripheralCatalog() { return PERIPHERAL_CATALOG; }
}

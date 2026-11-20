/**
 * CartridgeHost.js — Universal Emscripten WASM Firmware Cartridge Loader
 *
 * Loads any C++ firmware compiled against src/hal/wasm/wasm_host.cpp
 * (Sandbox ABI v1: _hw_*) from either a URL preset or locally dropped
 * Emscripten .js + .wasm files, with clean hot-swapping on the live 3D board.
 */

const loadedScripts = new Map();

function loadScriptOnce(url) {
  if (loadedScripts.has(url)) return loadedScripts.get(url);
  const p = new Promise((resolve, reject) => {
    const s = document.createElement('script');
    s.src = url;
    s.async = true;
    s.onload = () => resolve();
    s.onerror = () => reject(new Error(`Failed to load cartridge script: ${url}`));
    document.head.appendChild(s);
  });
  loadedScripts.set(url, p);
  return p;
}

export class CartridgeHost {
  constructor() {
    this.mod = null;
    this.api = null;
    this.info = null;
    this.ready = false;
    this._ptrs = null;
    this._blobUrls = [];
  }

  _revokeBlobs() {
    for (const u of this._blobUrls) {
      try { URL.revokeObjectURL(u); } catch (_) {}
    }
    this._blobUrls = [];
  }

  async _bindModule(mod) {
    const api = {
      init:          mod.cwrap('hw_init', null, []),
      step:          mod.cwrap('hw_step', null, ['number']),
      render:        mod.cwrap('hw_render', null, []),
      save:          mod.cwrap('hw_save', null, []),
      infoId:        mod.cwrap('hw_info_id', 'string', []),
      infoName:      mod.cwrap('hw_info_name', 'string', []),
      infoBoard:     mod.cwrap('hw_info_board', 'string', []),
      fbWidth:       mod.cwrap('hw_fb_width', 'number', []),
      fbHeight:      mod.cwrap('hw_fb_height', 'number', []),
      fbIsMono:      mod.cwrap('hw_fb_is_mono', 'number', []),
      fbRgbaPtr:     mod.cwrap('hw_fb_rgba_ptr', 'number', []),
      touchDown:     mod.cwrap('hw_touch_down', null, ['number', 'number']),
      touchMove:     mod.cwrap('hw_touch_move', null, ['number', 'number']),
      touchUp:       mod.cwrap('hw_touch_up', null, ['number', 'number']),
      uartRx:        mod.cwrap('hw_uart_rx', null, ['string']),
      audioSetRate:  mod.cwrap('hw_audio_set_rate', null, ['number']),
      audioRender:   mod.cwrap('hw_audio_render', 'number', ['number']),
      audioSetMuted: mod.cwrap('hw_audio_set_muted', null, ['number']),
      audioIsMuted:  mod.cwrap('hw_audio_is_muted', 'number', []),
      gpioOutPtr:    mod.cwrap('hw_gpio_out_ptr', 'number', []),
      gpioDirPtr:    mod.cwrap('hw_gpio_dir_ptr', 'number', []),
      gpioSetIn:     mod.cwrap('hw_gpio_set_in', null, ['number', 'number']),
      pwmPtr:        mod.cwrap('hw_pwm_ptr', 'number', []),
      adcSet:        mod.cwrap('hw_adc_set', null, ['number', 'number']),
      encoderStep:   mod.cwrap('hw_encoder_step', null, ['number', 'number']),
      encoderBtn:    mod.cwrap('hw_encoder_btn', null, ['number', 'number']),
      ledPtr:        mod.cwrap('hw_led_ptr', 'number', []),
      ledPin:        mod.cwrap('hw_led_pin', 'number', []),
      ledCount:      mod.cwrap('hw_led_count', 'number', []),
      ledVersion:    mod.cwrap('hw_led_version', 'number', []),
      jumperSet:     mod.cwrap('hw_jumper_set', null, ['number', 'number']),
    };

    this.mod = mod;
    this.api = api;
    this.info = {
      id:           api.infoId(),
      name:         api.infoName(),
      defaultBoard: api.infoBoard(),
      width:        api.fbWidth(),
      height:       api.fbHeight(),
      mono:         api.fbIsMono() !== 0,
    };

    api.init();
    this._ptrs = {
      gpioOut: api.gpioOutPtr(),
      gpioDir: api.gpioDirPtr(),
      pwm:     api.pwmPtr(),
      led:     api.ledPtr(),
    };
    this.ready = true;
    return this.info;
  }

  _teardown() {
    if (this.ready && this.api) {
      try { this.api.save(); } catch (_) {}
    }
    this.ready = false;
    this.mod = null;
    this.api = null;
    this._ptrs = null;
    this._revokeBlobs();
  }

  async load(cartridge) {
    this._teardown();

    await loadScriptOnce(cartridge.script);
    const factory = window[cartridge.exportName];
    if (typeof factory !== 'function') {
      throw new Error(`Cartridge factory '${cartridge.exportName}' not found after loading ${cartridge.script}`);
    }

    const baseDir = cartridge.script.substring(0, cartridge.script.lastIndexOf('/') + 1);
    const mod = await factory({
      locateFile: (file) => baseDir + file,
    });
    return this._bindModule(mod);
  }

  /** Flash a locally compiled Emscripten (.js + .wasm) firmware cartridge directly in browser */
  async loadFromFiles(jsFile, wasmFile) {
    this._teardown();
    const jsText = await jsFile.text();
    const wasmUrl = URL.createObjectURL(wasmFile);
    this._blobUrls.push(wasmUrl);

    // Detect Emscripten MODULARIZE export name (e.g. `var createMyApp = (() => ...`)
    const m = jsText.match(/(?:var|let|const)\s+([A-Za-z_$][\w$]*)\s*=\s*\(\(\)\s*=>/);
    const exportName = m ? m[1] : 'Module';
    const factory = new Function(`${jsText}\nreturn typeof ${exportName} === 'function' ? ${exportName} : null;`)();
    if (typeof factory !== 'function') {
      throw new Error(`Could not locate Emscripten factory function in ${jsFile.name}`);
    }

    const mod = await factory({
      locateFile: (file) => (file.endsWith('.wasm') ? wasmUrl : file),
    });
    return this._bindModule(mod);
  }

  reboot() {
    if (!this.ready || !this.api) return;
    try { this.api.save(); } catch (_) {}
    this.api.init();
    this._ptrs = {
      gpioOut: this.api.gpioOutPtr(),
      gpioDir: this.api.gpioDirPtr(),
      pwm:     this.api.pwmPtr(),
      led:     this.api.ledPtr(),
    };
  }

  step(dtSec) { if (this.ready) this.api.step(dtSec); }
  render()    { if (this.ready) this.api.render(); }
  save()      { if (this.ready) this.api.save(); }

  getRgbaBytes() {
    if (!this.ready) return null;
    const ptr = this.api.fbRgbaPtr();
    return this.mod.HEAPU8.subarray(ptr, ptr + this.info.width * this.info.height * 4);
  }

  touchDown(x, y) { if (this.ready) this.api.touchDown(x, y); }
  touchMove(x, y) { if (this.ready) this.api.touchMove(x, y); }
  touchUp(x, y)   { if (this.ready) this.api.touchUp(x, y); }
  sendUart(line)  { if (this.ready) this.api.uartRx(line); }

  audioSetRate(hz)    { if (this.ready) this.api.audioSetRate(hz); }
  audioSetMuted(m)    { if (this.ready) this.api.audioSetMuted(m ? 1 : 0); }
  audioIsMuted()      { return this.ready ? (this.api.audioIsMuted() !== 0) : true; }
  audioRender(frames) {
    if (!this.ready) return null;
    const ptr = this.api.audioRender(frames);
    return this.mod.HEAPF32.subarray(ptr >> 2, (ptr >> 2) + frames);
  }

  getGpioOutView() { return this.ready ? this.mod.HEAPU8.subarray(this._ptrs.gpioOut, this._ptrs.gpioOut + 49) : null; }
  getGpioDirView() { return this.ready ? this.mod.HEAPU8.subarray(this._ptrs.gpioDir, this._ptrs.gpioDir + 49) : null; }
  getPwmView()     { return this.ready ? this.mod.HEAPF32.subarray(this._ptrs.pwm >> 2, (this._ptrs.pwm >> 2) + 49) : null; }
  getLedRgbView()  { return this.ready ? this.mod.HEAPU8.subarray(this._ptrs.led, this._ptrs.led + 64 * 3) : null; }
  getLedPin()      { return this.ready ? this.api.ledPin() : -1; }
  getLedCount()    { return this.ready ? this.api.ledCount() : 0; }
  getLedVersion()  { return this.ready ? this.api.ledVersion() : 0; }

  setGpioIn(pin, high)     { if (this.ready) this.api.gpioSetIn(pin, high); }
  setAdc(pin, val)         { if (this.ready) this.api.adcSet(pin, val); }
  stepEncoder(id, delta)   { if (this.ready) this.api.encoderStep(id, delta); }
  setEncoderBtn(id, down)  { if (this.ready) this.api.encoderBtn(id, down); }
  setJumper(id, state)     { if (this.ready) this.api.jumperSet(id, state); }
}

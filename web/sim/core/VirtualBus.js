/**
 * VirtualBus.js — Shared Electrical Signal Bus between WASM Firmware & 3D Hardware
 *
 * Mirrors real ESP32-S3 GPIO (0..48), PWM, ADC, Rotary Encoders, WS2812B RMT LEDs,
 * and physical solder-bridge strapping jumpers (e.g., R21-23 I2S vs R25-27 Relay).
 */

export const MAX_GPIO = 49;
export const MAX_ENCODERS = 4;
export const MAX_NEOPIXELS = 64;

export const JUMPER_AUDIO_RELAY = 0; // 0 = R21-23 (NS4168 I2S), 1 = R25-27 (3ch Relay)

export class VirtualBus {
  constructor() {
    this.gpioDir = new Uint8Array(MAX_GPIO);
    this.gpioOut = new Uint8Array(MAX_GPIO);
    this.gpioPrevOut = new Uint8Array(MAX_GPIO);
    this.gpioIn = new Uint8Array(MAX_GPIO);
    this.pwmDuty = new Float32Array(MAX_GPIO);
    this.adcVal = new Uint16Array(MAX_GPIO);
    this.jumpers = new Int32Array(8);

    this.ledPin = -1;
    this.ledCount = 0;
    this.ledVersion = 0;
    this.ledRgb = new Uint8Array(MAX_NEOPIXELS * 3);

    this._listeners = new Set();
    this._host = null;
  }

  attachHost(host) {
    this._host = host;
    this.resetSignals();
    // Push current jumper states down into newly booted firmware
    for (let i = 0; i < this.jumpers.length; i++) {
      this._host?.setJumper(i, this.jumpers[i]);
    }
  }

  resetSignals() {
    this.gpioDir.fill(0);
    this.gpioOut.fill(0);
    this.gpioPrevOut.fill(0);
    this.gpioIn.fill(0);
    this.pwmDuty.fill(0);
    this.ledPin = -1;
    this.ledCount = 0;
    this.ledRgb.fill(0);
    this.ledVersion = 0;
  }

  onChange(fn) {
    this._listeners.add(fn);
    return () => this._listeners.delete(fn);
  }

  _emit(type, detail) {
    for (const fn of this._listeners) fn(type, detail);
  }

  /** Pull latest GPIO / PWM / WS2812B state from WASM shared memory */
  syncFromFirmware() {
    const h = this._host;
    if (!h || !h.ready) return;

    const dirView = h.getGpioDirView();
    const outView = h.getGpioOutView();
    const pwmView = h.getPwmView();

    if (dirView) this.gpioDir.set(dirView);
    if (pwmView) this.pwmDuty.set(pwmView);

    if (outView) {
      let changed = false;
      for (let pin = 0; pin < MAX_GPIO; pin++) {
        const v = outView[pin];
        if (v !== this.gpioOut[pin]) {
          this.gpioPrevOut[pin] = this.gpioOut[pin];
          this.gpioOut[pin] = v;
          changed = true;
          this._emit('gpio', { pin, value: v, prev: this.gpioPrevOut[pin] });
        }
      }
      if (changed) this._emit('bus', null);
    }

    const ver = h.getLedVersion();
    if (ver !== this.ledVersion) {
      this.ledVersion = ver;
      this.ledPin = h.getLedPin();
      this.ledCount = Math.min(MAX_NEOPIXELS, h.getLedCount());
      const rgb = h.getLedRgbView();
      if (rgb && this.ledCount > 0) {
        this.ledRgb.set(rgb.subarray(0, this.ledCount * 3));
      }
      this._emit('neopixel', { pin: this.ledPin, count: this.ledCount, rgb: this.ledRgb });
    }
  }

  /** Inject external 3D hardware input into firmware */
  setGpioInput(pin, high) {
    if (pin < 0 || pin >= MAX_GPIO) return;
    this.gpioIn[pin] = high ? 1 : 0;
    this._host?.setGpioIn(pin, high ? 1 : 0);
  }

  setAdcInput(pin, value0to4095) {
    if (pin < 0 || pin >= MAX_GPIO) return;
    const v = Math.max(0, Math.min(4095, value0to4095 | 0));
    this.adcVal[pin] = v;
    this._host?.setAdc(pin, v);
  }

  stepEncoder(id, delta) {
    if (id < 0 || id >= MAX_ENCODERS) return;
    this._host?.stepEncoder(id, delta | 0);
  }

  setEncoderButton(id, down) {
    if (id < 0 || id >= MAX_ENCODERS) return;
    this._host?.setEncoderBtn(id, down ? 1 : 0);
  }

  setJumper(id, state) {
    if (id < 0 || id >= this.jumpers.length) return;
    const s = state | 0;
    if (this.jumpers[id] === s) return;
    this.jumpers[id] = s;
    this._host?.setJumper(id, s);
    this._emit('jumper', { id, state: s });
  }

  getJumper(id) {
    return (id >= 0 && id < this.jumpers.length) ? this.jumpers[id] : 0;
  }

  /** True if NS4168 I2S audio path is physically connected (R21-23 populated) */
  isI2sAudioRouted() {
    return this.getJumper(JUMPER_AUDIO_RELAY) === 0;
  }

  /** True if 3ch Relay header J3 receives GPIO 1/2/40 (R25-27 populated) */
  isRelayRouted() {
    return this.getJumper(JUMPER_AUDIO_RELAY) === 1;
  }
}

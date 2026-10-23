import { serial as usbSerial, SerialPort as UsbSerialPort } from 'web-serial-polyfill';

import { DONGLE_USB_FILTER } from '../config';

// Required by the API; the C3's native USB ignores it.
const BAUD_RATE = 115200;

// Android Chrome reaches the dongle through WebUSB (via web-serial-polyfill,
// which speaks CDC-ACM). Desktop OSes claim CDC devices with their own driver,
// so there the native Web Serial API is the way in. `?serial=native|webusb`
// forces one.
function chooseBackend() {
  const forced = new URLSearchParams(window.location.search).get('serial');
  const hasNative = 'serial' in navigator;
  const hasUsb = 'usb' in navigator;
  if (forced === 'native' && hasNative) return 'native';
  if (forced === 'webusb' && hasUsb) return 'webusb';
  if (/Android/i.test(navigator.userAgent) && hasUsb) return 'webusb';
  if (hasNative) return 'native';
  if (hasUsb) return 'webusb';
  return null;
}

function isDongle(info) {
  return info.usbVendorId === DONGLE_USB_FILTER.usbVendorId && info.usbProductId === DONGLE_USB_FILTER.usbProductId;
}

// A line-based serial link to the dongle. Events: { type: 'connection',
// connected } and { type: 'line', line }.
export class WebSerialLink {
  constructor() {
    this.backend = window.isSecureContext ? chooseBackend() : null;
    this.unsupportedReason = !window.isSecureContext ? 'insecure' : this.backend ? null : 'no-api';
    this.listeners = new Set();
    this.port = null;
    this.reader = null;
    this.opening = false;
    this.running = false;
    this.writes = Promise.resolve();
    this.closing = Promise.resolve();
    this.encoder = new TextEncoder();
  }

  get supported() {
    return this.backend !== null;
  }

  get serialApi() {
    return this.backend === 'native' ? navigator.serial : usbSerial;
  }

  // Where connect/disconnect events for the dongle are fired.
  get eventTarget() {
    return this.backend === 'native' ? navigator.serial : navigator.usb;
  }

  start() {
    if (!this.supported) return;
    this.running = true;
    this.eventTarget.addEventListener('connect', this.handleAttached);
    this.eventTarget.addEventListener('disconnect', this.handleDetached);
    this.openKnownPort();
  }

  stop() {
    this.running = false;
    if (!this.supported) return;
    this.eventTarget.removeEventListener('connect', this.handleAttached);
    this.eventTarget.removeEventListener('disconnect', this.handleDetached);
    this.dropPort();
  }

  subscribe(listener) {
    this.listeners.add(listener);
    return () => this.listeners.delete(listener);
  }

  // Must run from a tap: shows the browser's device picker.
  async connect() {
    const port = await this.serialApi.requestPort({ filters: [DONGLE_USB_FILTER] });
    await this.openPort(port);
  }

  writeLine(line) {
    const write = async () => {
      const port = this.port;
      if (!port?.writable) throw new Error('Dongle not connected');
      const writer = port.writable.getWriter();
      try {
        await writer.write(this.encoder.encode(`${line}\n`));
      } finally {
        writer.releaseLock();
      }
    };
    // One write at a time: the stream allows only one writer.
    const result = this.writes.then(write, write);
    this.writes = result.catch(() => {});
    return result;
  }

  // A dongle the user already allowed opens without asking again.
  async openKnownPort() {
    try {
      const ports = await this.serialApi.getPorts();
      const port = ports.find((p) => isDongle(p.getInfo()));
      if (port) await this.openPort(port);
    } catch {
      // Nothing to reopen; the user taps Connect.
    }
  }

  handleAttached = (event) => {
    if (this.port) return;
    try {
      const port = this.backend === 'native' ? event.target : new UsbSerialPort(event.device);
      if (isDongle(port.getInfo())) this.openPort(port).catch(() => {});
    } catch {
      // Not a serial device.
    }
  };

  handleDetached = (event) => {
    const info =
      this.backend === 'native'
        ? event.target.getInfo()
        : { usbVendorId: event.device.vendorId, usbProductId: event.device.productId };
    if (this.port && isDongle(info)) this.dropPort();
  };

  async openPort(port) {
    if (this.port || this.opening || !this.running) return;
    this.opening = true;
    try {
      await this.closing; // A port being closed can't be reopened yet.
      await port.open({ baudRate: BAUD_RATE });
      // Both lines idle. On the C3's USB Serial/JTAG, RTS high holds the chip
      // in reset and DTR high pulls the boot pin.
      await port.setSignals({ dataTerminalReady: false, requestToSend: false }).catch(() => {});
      this.port = port;
      this.emit({ type: 'connection', connected: true });
      this.readLoop(port);
    } finally {
      this.opening = false;
    }
  }

  async readLoop(port) {
    const decoder = new TextDecoder();
    let buffer = '';
    while (this.port === port && port.readable) {
      const reader = port.readable.getReader();
      this.reader = reader;
      try {
        for (;;) {
          const { value, done } = await reader.read();
          if (done) break;
          buffer += decoder.decode(value, { stream: true });
          let end;
          while ((end = buffer.indexOf('\n')) >= 0) {
            const line = buffer.slice(0, end);
            buffer = buffer.slice(end + 1);
            this.emit({ type: 'line', line });
          }
        }
      } catch {
        break; // Unplugged or the port failed.
      } finally {
        reader.releaseLock();
        this.reader = null;
      }
    }
    if (this.port === port) this.dropPort();
  }

  dropPort() {
    const port = this.port;
    if (!port) return;
    this.port = null;
    this.emit({ type: 'connection', connected: false });
    const reader = this.reader;
    this.closing = Promise.resolve(reader?.cancel())
      .catch(() => {})
      .then(() => port.close())
      .catch(() => {});
  }

  emit(event) {
    this.listeners.forEach((listener) => listener(event));
  }
}

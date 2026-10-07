// flash-core.js - every decision the web flasher makes, with no DOM, so
// node --test can cover it (pedal/tests/flash-core.test.mjs). flash.js is
// the page layer on top.
//
// The pedal runs from the STM32H750's 128 KB of internal flash at
// 0x08000000, no bootloader, so a flash is one image written to one
// address. The saved settings live on the QSPI chip, which this never
// touches.

export const FLASH_ADDRESS = 0x08000000;
export const MAX_IMAGE = 131072;

// libDaisy's linker script sets _estack to 0x20020000, the top of DTCM.
const SP_MIN = 0x20000001;
const SP_MAX = 0x20020000;
const RV_MIN = FLASH_ADDRESS;
const RV_MAX = FLASH_ADDRESS + MAX_IMAGE - 1;

export class FlashError extends Error {
  constructor(code, message) {
    super(message);
    this.name = 'FlashError';
    this.code = code;
  }
}

const hex32 = (n) => '0x' + (n >>> 0).toString(16).padStart(8, '0');

export function checkImage(bytes) {
  if (bytes.byteLength === 0) {
    throw new FlashError('empty', 'The firmware file is empty.');
  }
  if (bytes.byteLength > MAX_IMAGE) {
    throw new FlashError('too-big',
      `The firmware is ${bytes.byteLength} bytes, more than the ` +
      `${MAX_IMAGE} bytes of flash the pedal has.`);
  }
  if (bytes.byteLength < 8) {
    throw new FlashError('bad-vectors',
      'The firmware file is too short to be a pedal image.');
  }
  const v = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  const sp = v.getUint32(0, true);
  const rv = v.getUint32(4, true);
  const spOk = sp >= SP_MIN && sp <= SP_MAX;
  const rvOk = (rv & 1) === 1 && rv >= RV_MIN && rv <= RV_MAX;
  if (!spOk || !rvOk) {
    throw new FlashError('bad-vectors',
      `This file is not a pedal image (stack ${hex32(sp)}, ` +
      `reset ${hex32(rv)}).`);
  }
}

async function sha256Hex(bytes) {
  const d = await crypto.subtle.digest('SHA-256', bytes);
  return Array.from(new Uint8Array(d),
    (b) => b.toString(16).padStart(2, '0')).join('');
}

function parseVersion(text) {
  let v;
  try { v = JSON.parse(text); } catch { v = null; }
  const ok = v && typeof v === 'object' && !Array.isArray(v) &&
    typeof v.commit === 'string' && /^[0-9a-f]{40}$/.test(v.commit) &&
    typeof v.date === 'string' &&
    Number.isInteger(v.size) && v.size >= 0 &&
    typeof v.sha256 === 'string' && /^[0-9a-f]{64}$/.test(v.sha256);
  if (!ok) {
    throw new FlashError('bad-release',
      'The published version.json is damaged, so the page will not ' +
      'flash anything.');
  }
  return { commit: v.commit, date: v.date, size: v.size, sha256: v.sha256 };
}

// Fetches url and reads the body with `read` ('text' or 'arrayBuffer'). The
// read is inside the try: a connection that drops mid-download is a missing
// release, not a USB problem.
async function get(fetchFn, url, opts, read) {
  let body = null;
  try {
    const res = await fetchFn(url, opts);
    if (res && res.ok) body = await res[read]();
  } catch { body = null; }
  if (body === null) {
    throw new FlashError('no-release',
      'No published firmware was found on this site.');
  }
  return body;
}

export async function loadRelease(fetchFn = globalThis.fetch,
                                  base = 'firmware/latest/') {
  // version.json is never taken from a cache: an old one beside a new
  // binary would fail the hash check for no reason. The binary is keyed on
  // its commit, so a cached copy can only ever be the right one.
  const version = parseVersion(await get(
    fetchFn, base + 'version.json', { cache: 'no-store' }, 'text'));
  const image = new Uint8Array(await get(fetchFn,
    `${base}dbscreamz_pedal.bin?v=${version.commit}`, undefined, 'arrayBuffer'));
  if (image.byteLength !== version.size) {
    throw new FlashError('size-mismatch',
      `The download is ${image.byteLength} bytes but the release says ` +
      `${version.size}. Reload the page and try again.`);
  }
  if (await sha256Hex(image) !== version.sha256) {
    throw new FlashError('hash-mismatch',
      'The download does not match the published checksum. Reload the ' +
      'page and try again.');
  }
  checkImage(image);
  return { version, image };
}

// --- the bootloader --------------------------------------------------------
//
// The STM32 system bootloader is in ROM [read-only memory]: it cannot be
// overwritten, so BOOT + RESET always gets back to it.

const STM_VID = 0x0483;
const DFU_PID = 0xdf11;
const FACTORY_PID = 0x4009;   // the Seed's factory USB image, not DFU
const DT_DFU_FUNCTIONAL = 0x21;
const FALLBACK_TRANSFER = 1024;

const hex16 = (n) => n.toString(16).padStart(4, '0');

async function readTransferSize(probe, dfu) {
  try {
    const raw = await probe.readConfigurationDescriptor(0);
    const conf = dfu.parseConfigurationDescriptor(raw);
    const fd = conf.descriptors.find(
      (d) => d.bDescriptorType === DT_DFU_FUNCTIONAL);
    if (fd && fd.wTransferSize > 0) return fd.wTransferSize;
  } catch {
    // fall through: a smaller chunk than the device allows is still safe
  }
  return FALLBACK_TRANSFER;
}

export async function findDfuInterface(usbDevice, { dfu, dfuse }) {
  const { vendorId, productId } = usbDevice;
  if (vendorId === STM_VID && productId === FACTORY_PID) {
    throw new FlashError('not-dfu',
      'That is the pedal, but it is not in DFU [Device Firmware Upgrade] ' +
      'mode yet. Follow the steps above to enter it, then connect again.');
  }
  if (vendorId !== STM_VID || productId !== DFU_PID) {
    throw new FlashError('wrong-device',
      `That device (${hex16(vendorId)}:${hex16(productId)}) is not the ` +
      'pedal in DFU mode. Pick the one called "DFU in FS Mode".');
  }
  const interfaces = dfu.findDeviceDfuInterfaces(usbDevice);
  if (interfaces.length === 0) {
    throw new FlashError('no-flash-interface',
      'The device in DFU mode does not offer internal flash.');
  }
  const probe = new dfu.Device(usbDevice, interfaces[0]);
  await probe.open();
  if (interfaces.some((s) => !s.name)) {
    // Chrome leaves interfaceName null on some platforms. Read the names
    // from the device's string descriptors instead, as the webdfu demo
    // does.
    const names = await probe.readInterfaceNames();
    for (const s of interfaces) {
      s.name = names[s.configuration.configurationValue]
        ?.[s.interface.interfaceNumber]
        ?.[s.alternate.alternateSetting] ?? null;
    }
  }
  const transferSize = await readTransferSize(probe, dfu);
  for (const s of interfaces) {
    if (!s.name) continue;
    let map;
    try { map = dfuse.parseMemoryDescriptor(s.name); } catch { continue; }
    const seg = map.segments.find(
      (g) => g.start <= FLASH_ADDRESS && FLASH_ADDRESS < g.end);
    if (seg && seg.writable && seg.erasable &&
        seg.end - FLASH_ADDRESS >= MAX_IMAGE) {
      const device = new dfuse.Device(usbDevice, s);
      device.startAddress = FLASH_ADDRESS;
      device.transferSize = transferSize;
      return device;
    }
  }
  throw new FlashError('no-flash-interface',
    'The device in DFU mode does not offer internal flash at 0x08000000.');
}

const DOING = {
  connect: 'connecting',
  erase: 'erasing',
  write: 'writing',
  leave: 'restarting the pedal',
};

export async function flash(device, image, onProgress = () => {}) {
  let phase = 'connect';
  let written = 0;
  device.logDebug = () => {};
  device.logWarning = () => {};
  device.logError = () => {};
  device.logInfo = (msg) => {
    if (msg.startsWith('Erasing')) phase = 'erase';
    else if (msg.startsWith('Copying')) phase = 'write';
    else if (msg.startsWith('Manifesting')) phase = 'leave';
  };
  device.logProgress = (done, total) => {
    if (phase === 'write') written = done;
    onProgress({ phase, done, total });
  };
  try {
    await device.open();
    await device.abortToIdle();
    const data = image.buffer.slice(
      image.byteOffset, image.byteOffset + image.byteLength);
    await device.do_download(device.transferSize ?? FALLBACK_TRANSFER, data);
  } catch (e) {
    // The bootloader often resets into the new firmware before it answers
    // the last request, the same thing dfu-util 0.9 reports as "Error 74"
    // after "Download done.". Every byte is in flash by then, so that one
    // case is success. An error at any earlier point is a real failure.
    const landed = phase === 'leave' && written === image.byteLength;
    if (!landed) {
      const err = new FlashError('flash-failed',
        `Flashing stopped while ${DOING[phase]}: ${e?.message ?? e}`);
      err.phase = phase;
      throw err;
    }
  }
  try { await device.close(); } catch { /* the pedal has already left */ }
  onProgress({ phase: 'done', done: image.byteLength, total: image.byteLength });
}

// --- the page ---------------------------------------------------------------

const CONTROLS = {
  loading:     { connect: false, flash: false },
  unsupported: { connect: false, flash: false },
  'no-release': { connect: false, flash: false },
  ready:       { connect: true,  flash: false },
  connected:   { connect: true,  flash: true },
  flashing:    { connect: false, flash: false },
  done:        { connect: true,  flash: false },
};

export function controlsFor(state) {
  const c = CONTROLS[state];
  if (!c) throw new Error(`unknown flash page state: ${state}`);
  return { ...c };
}

const HELP = {
  'not-dfu': ['enter-dfu'],
  'flash-failed': ['recover'],
  'bad-release': ['reload'],
  'size-mismatch': ['reload'],
  'hash-mismatch': ['reload'],
};

export function explain(err) {
  if (err instanceof FlashError) {
    return { message: err.message, help: HELP[err.code] ?? [] };
  }
  const name = err?.name ?? '';
  const message = err?.message ?? String(err);
  if (name === 'NotFoundError' && /no device selected/i.test(message)) {
    return null;
  }
  if (name === 'SecurityError' || name === 'NetworkError') {
    return {
      message: 'The browser was not allowed to open the pedal.',
      help: ['linux', 'windows'],
    };
  }
  return { message: `Something went wrong: ${message}`, help: ['linux', 'windows'] };
}

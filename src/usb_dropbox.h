#pragma once

#include <Arduino.h>

// A second USB mass-storage LUN, presented beside the read-only Field Notes
// drive (which stays LUN 0, unchanged): a real, host-writable FAT volume named
// DROP BOX whose only job is to let a computer drag DuckyScript and BadUSB
// files onto the badge instead of pasting them into the web portal. Files
// dropped into its DUCKY/ and BADUSB/ folders are imported into the same
// LittleFS /payloads store the portal writes (usbHidSavePayload /
// usbBadUSBSavePayload), so an imported script then appears -- rendered -- on
// the read-only drive and in the TUI, exactly as a portal-saved one does.
//
// The volume is backed by the `ffat` flash partition, served to the host as raw
// 512-byte blocks. The host owns all FAT bookkeeping; the badge only READS the
// files back (never writes that FAT beyond a one-time seed), so there is no
// dual-writer hazard. A second USBMSC instance adds a LUN, not a USB interface,
// so the CDC+HID+MSC composite descriptor -- and the S3's endpoint budget -- is
// unchanged.

struct UsbDropboxState {
  bool available;       // the writable LUN is registered and enumerating
  bool mediaPresent;    // a formatted volume is mounted and offered to the host
  uint32_t capacityBytes;
  uint16_t importedDucky;   // DuckyScripts imported since boot
  uint16_t importedBadUSB;  // BadUSB scripts imported since boot
};

// Claim the writable LUN before USB.begin(). No-op unless the Drive profile is
// active, SM_USB_DROPBOX is built in, and the ffat partition exists. Does not
// touch flash or present a medium yet -- only reserves the LUN.
void usbDropboxConfigure(bool enabled);

// Format-on-first-boot, seed the folders, import anything already present, then
// present the medium to the host. Call once during setup(), after the USB
// controller has begun (mirrors the Field Notes drive, which also comes up with
// no medium and gains one later).
void usbDropboxBegin();

// Poll from loop(). Runs a pending import -- triggered by the host ejecting the
// volume or by it going idle a couple of seconds after the last host write --
// on the Arduino task, never inside the USB callback. Returns true when it
// imported at least one new script this call, so the caller can refresh the
// read-only drive's snapshot.
bool usbDropboxService();

// True for a short window after the host reads or writes the volume, for the
// same storage-activity indication the Field Notes drive drives.
bool usbDropboxActive();

UsbDropboxState getUsbDropboxState();

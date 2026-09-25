#pragma once

#include <Arduino.h>

// A second USB mass-storage LUN, presented beside the read-only Santa Muerte
// drive (which stays LUN 0, unchanged): a real, host-writable FAT volume named
// Ofrenda whose only job is to let a computer drag DuckyScript and BadUSB
// files onto the badge instead of pasting them into the web portal. Files
// dropped into its matching DuckyScript/ and BadUSB/ folders are moved into the same
// LittleFS /payloads store the portal writes (usbHidSavePayload /
// usbBadUSBSavePayload), so an imported script then appears -- rendered -- on
// the read-only drive and in the TUI, exactly as a portal-saved one does.
//
// The volume is backed by the `ffat` flash partition, served to the host as raw
// blocks the size of one wear-levelling sector -- the size FATFS formatted the
// volume with. A host told a block size that disagrees with the BPB on the
// volume refuses to mount it. The host owns all FAT bookkeeping; the badge
// only READS the files back (never writes that FAT beyond a one-time seed), so
// there is no dual-writer hazard. A second USBMSC instance adds a LUN, not a
// USB interface, so the CDC+HID+MSC composite descriptor -- and the S3's
// endpoint budget -- is unchanged.

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

// Format-on-first-boot, seed the claimed folders, move anything dropped into
// them, then
// present the medium to the host. Call once during setup(), before USB.begin(),
// so both LUNs are ready on the host's first probe.
void usbDropboxBegin();

// Poll from loop(). Runs a pending move after the host ejects/releases the
// volume, on the Arduino task and never inside the USB callback. Returns true when it
// moved at least one new script this call, so the caller can refresh the
// read-only drive's snapshot.
bool usbDropboxService();

// Cleans up a legacy or failed Ofrenda source whose derived payload name is
// `name` before the portal deletes that payload. A normal move has already
// removed the source. Cleanup is skipped while the host owns the medium so the
// badge never mutates FAT behind a live host cache.
bool usbDropboxDeletePayloadSource(bool badusb, const String &name, String &error);

// True during a host read/write, its short afterglow, or a local import/delete
// handoff while the LUN is temporarily unavailable. main.cpp routes all of
// these through the existing Field Notes busy-drive animation.
bool usbDropboxActive();

UsbDropboxState getUsbDropboxState();

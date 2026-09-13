#include "usb_network.h"
#include "usb_console.h"

// The normal production environment used to be Arduino 2, whose TinyUSB
// archive did not contain the NCM class.  Keeping the fallback lets that older
// environment still compile while the Arduino 3 environment below owns NCM.
#if !ARDUINO_USB_MODE && defined(SM_USB_NCM) && SM_USB_NCM

#include <esp_err.h>
#include <esp_mac.h>
#include <esp_private/wifi.h>
#include <esp_wifi.h>

extern "C" {
#include <esp32-hal-tinyusb.h>
#include <tusb.h>
#include <device/usbd.h>
#include <class/net/net_device.h>
#include <device/usbd_pvt.h>
}

namespace {

constexpr uint16_t MAX_ETHERNET_FRAME = 1514;
constexpr uint8_t WIFI_TO_USB_QUEUE_SIZE = 3;

struct QueuedFrame {
  uint16_t length;
  uint8_t bytes[MAX_ETHERNET_FRAME];
};

QueuedFrame wifiToUsb[WIFI_TO_USB_QUEUE_SIZE] = {};
portMUX_TYPE queueMux = portMUX_INITIALIZER_UNLOCKED;
uint8_t queueHead = 0;
uint8_t queueTail = 0;
uint8_t queueCount = 0;

bool bridgeEnabled = false;
bool stationConnected = false;
bool reportedLinkUp = false;
uint32_t sentFrames = 0;
uint32_t receivedFrames = 0;
uint32_t droppedFrames = 0;
char ncmMacString[13] = {};
bool descriptorRegistered = false;
bool transmitDeferred = false;

void setReportedLinkState() {
  const bool linkUp = bridgeEnabled && stationConnected;
  if (linkUp == reportedLinkUp) return;
  reportedLinkUp = linkUp;
  tud_network_link_state(0, linkUp);
}

void clearQueue() {
  portENTER_CRITICAL(&queueMux);
  queueHead = 0;
  queueTail = 0;
  queueCount = 0;
  portEXIT_CRITICAL(&queueMux);
}

// TinyUSB's NCM buffers belong to its device task. Calling tud_network_xmit()
// from the Arduino loop can produce an NTB while the USB task is assembling
// another one; Linux then accepts the interface but discards every frame. Keep
// our Wi-Fi callback short and hand the actual transfer to TinyUSB, matching
// Espressif's tinyusb_net implementation.
void sendQueuedFrame(void *) {
  QueuedFrame *frame = nullptr;
  portENTER_CRITICAL(&queueMux);
  if (queueCount) frame = &wifiToUsb[queueTail];
  portEXIT_CRITICAL(&queueMux);

  if (frame && tud_network_can_xmit(frame->length)) {
    tud_network_xmit(frame, frame->length);
    portENTER_CRITICAL(&queueMux);
    if (queueCount && &wifiToUsb[queueTail] == frame) {
      queueTail = (queueTail + 1) % WIFI_TO_USB_QUEUE_SIZE;
      --queueCount;
    }
    transmitDeferred = false;
    portEXIT_CRITICAL(&queueMux);
    return;
  }

  portENTER_CRITICAL(&queueMux);
  transmitDeferred = false;
  portEXIT_CRITICAL(&queueMux);
}

esp_err_t forwardWifiPacket(void *buffer, uint16_t length, void *wifiBuffer) {
  bool queued = false;
  if (buffer && length > 0 && length <= MAX_ETHERNET_FRAME && bridgeEnabled &&
      stationConnected) {
    portENTER_CRITICAL(&queueMux);
    if (queueCount < WIFI_TO_USB_QUEUE_SIZE) {
      QueuedFrame &frame = wifiToUsb[queueHead];
      frame.length = length;
      memcpy(frame.bytes, buffer, length);
      queueHead = (queueHead + 1) % WIFI_TO_USB_QUEUE_SIZE;
      ++queueCount;
      ++receivedFrames;
      queued = true;
    }
    portEXIT_CRITICAL(&queueMux);
  }
  if (!queued) ++droppedFrames;
  if (wifiBuffer) esp_wifi_internal_free_rx_buffer(wifiBuffer);
  return ESP_OK;
}

uint16_t loadNcmDescriptor(uint8_t *destination, uint8_t *interfaceNumber) {
  const uint8_t descriptionString =
      tinyusb_add_string_descriptor("Santa Muerte USB Wi-Fi");
  const uint8_t macString = tinyusb_add_string_descriptor(ncmMacString);
  const uint8_t bulkEndpoint = tinyusb_get_free_duplex_endpoint();
  const uint8_t notificationEndpoint = tinyusb_get_free_in_endpoint();
  if (!descriptionString || !macString || !bulkEndpoint || !notificationEndpoint)
    return 0;

  const uint8_t descriptor[TUD_CDC_NCM_DESC_LEN] = {
      TUD_CDC_NCM_DESCRIPTOR(*interfaceNumber, descriptionString, macString,
                             static_cast<uint8_t>(0x80 | notificationEndpoint), 8, bulkEndpoint,
                             static_cast<uint8_t>(0x80 | bulkEndpoint), 64, MAX_ETHERNET_FRAME, 50,
                             static_cast<uint8_t>(NCM_NETWORK_CAPS_ETH_FILTER |
                                                  NCM_NETWORK_CAPS_NTB_INPUT_SIZE))};
  memcpy(destination, descriptor, sizeof(descriptor));
  *interfaceNumber += 2;
  return sizeof(descriptor);
}

}  // namespace

extern "C" bool tud_network_default_link_state_cb(void) { return false; }

extern "C" void tud_network_init_cb(void) {}

extern "C" bool tud_network_recv_cb(const uint8_t *source, uint16_t size) {
  if (!bridgeEnabled || !stationConnected || !source || size == 0 ||
      size > MAX_ETHERNET_FRAME) {
    ++droppedFrames;
    return true;
  }
  if (esp_wifi_internal_tx(WIFI_IF_STA, const_cast<uint8_t *>(source), size) !=
      ESP_OK) {
    ++droppedFrames;
  }
  else {
    ++sentFrames;
  }
  // Required by TinyUSB after consuming the packet; without this, only the
  // first DHCP/ARP request can reach the Wi-Fi side of the bridge.
  tud_network_recv_renew();
  return true;
}

extern "C" uint16_t tud_network_xmit_cb(uint8_t *destination, void *reference,
                                          uint16_t length) {
  if (!destination || !reference || length == 0 || length > MAX_ETHERNET_FRAME)
    return 0;
  // `reference` is our queued record, not the first Ethernet byte. Copying
  // from the record itself prefixes every frame with its uint16_t length,
  // which makes a host correctly enumerate NCM yet discard every packet.
  const auto *frame = static_cast<const QueuedFrame *>(reference);
  if (frame->length != length) return 0;
  memcpy(destination, frame->bytes, length);
  return length;
}

void usbNetworkConfigure(bool enabled) {
  if (!enabled || descriptorRegistered) return;
  uint8_t mac[6] = {0x02, 0x53, 0x4D, 0x00, 0x00, 0x01};
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
  // The CDC Ethernet descriptor calls for exactly 12 hexadecimal digits.
  snprintf(ncmMacString, sizeof(ncmMacString), "%02X%02X%02X%02X%02X%02X",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  descriptorRegistered =
      tinyusb_enable_interface(USB_INTERFACE_CUSTOM, TUD_CDC_NCM_DESC_LEN,
                               loadNcmDescriptor) == ESP_OK;
}

void usbNetworkBegin() {
  Serial.printf("[USB] NCM adapter %s // MAC %s\r\n",
                descriptorRegistered ? "ready" : "unavailable", ncmMacString);
}

void usbNetworkService() {
  if (!bridgeEnabled || !stationConnected || !reportedLinkUp) return;

  portENTER_CRITICAL(&queueMux);
  const bool shouldDefer = queueCount && !transmitDeferred;
  if (shouldDefer) transmitDeferred = true;
  portEXIT_CRITICAL(&queueMux);
  if (shouldDefer) usbd_defer_func(sendQueuedFrame, nullptr, false);
}

void usbNetworkSetStationConnected(bool connected) {
  stationConnected = connected;
  setReportedLinkState();
}

bool usbNetworkSetEnabled(bool enabled, String &error) {
  error = String();
  if (enabled == bridgeEnabled) return true;
  if (enabled && !descriptorRegistered) {
    error = "USB Wi-Fi is unavailable in this firmware.";
    return false;
  }
  if (enabled && !stationConnected) {
    error = "Connect saved Wi-Fi before starting USB Wi-Fi.";
    return false;
  }

  if (enabled) {
    const esp_err_t result = esp_wifi_internal_reg_rxcb(WIFI_IF_STA, forwardWifiPacket);
    if (result != ESP_OK) {
      error = String("Could not start Wi-Fi bridge (") + esp_err_to_name(result) + ").";
      return false;
    }
    bridgeEnabled = true;
  } else {
    esp_wifi_internal_reg_rxcb(WIFI_IF_STA, nullptr);
    bridgeEnabled = false;
    clearQueue();
  }
  setReportedLinkState();
  return true;
}

UsbNetworkState getUsbNetworkState() {
  return {descriptorRegistered, bridgeEnabled, stationConnected,
          bridgeEnabled && stationConnected, sentFrames, receivedFrames,
          droppedFrames};
}

#else

void usbNetworkConfigure(bool) {}
void usbNetworkBegin() {}
void usbNetworkService() {}
void usbNetworkSetStationConnected(bool) {}
bool usbNetworkSetEnabled(bool, String &error) {
  error = "USB Wi-Fi requires the current Arduino 3 firmware.";
  return false;
}
UsbNetworkState getUsbNetworkState() { return {false, false, false, false, 0, 0, 0}; }

#endif

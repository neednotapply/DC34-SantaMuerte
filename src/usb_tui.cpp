#include "usb_tui.h"

#include <LittleFS.h>
#include <esp_system.h>
#include <stdarg.h>

#include "badge_led.h"
#include "badge_settings.h"
#include "badge_wifi.h"
#include "board.h"
#include "nfc.h"

namespace {

constexpr uint8_t LOG_CAPACITY = 28;
constexpr uint8_t LOG_LINE_LENGTH = 96;
constexpr uint32_t REVEAL_MS = 10000;

enum class Screen : uint8_t { DASHBOARD, NETWORK, LED, NFC, OFFERINGS, SYSTEM, LOGS, HELP };
enum class Prompt : uint8_t { NONE, AP_PASSWORD, HOME_SSID, HOME_PASSWORD, LED_HEX, LED_BRIGHTNESS, LED_SPEED, NFC_TEXT, NFC_URL, EMU_TEXT, EMU_URL, OFFERING };
enum class Confirm : uint8_t { NONE, SWITCH_AP, SWITCH_HOME, SAVE_AP, TOGGLE_AP_HIDDEN, SAVE_HOME, NFC_WRITE_TEXT, NFC_WRITE_URL, EMU_TEXT, EMU_URL, NFC_WIFI, CLEAR_BOARD, REBOOT, POST_OFFERING };

struct LogLine { char module[12]; char text[LOG_LINE_LENGTH]; uint32_t at; };
LogLine logs[LOG_CAPACITY] = {};
uint8_t logHead = 0;
uint8_t logCount = 0;

Screen screen = Screen::DASHBOARD;
bool ansi = true;
bool english = false;
bool rawStream = false;
bool needsRedraw = true;
bool revealSecrets = false;
uint32_t revealUntil = 0;
Prompt prompt = Prompt::NONE;
Confirm confirm = Confirm::NONE;
String input;
String stagedA;
String stagedB;
String notice;
uint8_t escapeState = 0;
uint32_t escapeAt = 0;
uint8_t dashboardSelection = 0;
// A USB terminal can attach halfway through the first dashboard paint. Give
// it two deliberate chances to see a complete screen, then remain entirely
// event-driven so serial monitors do not scroll forever.
uint8_t welcomeRefreshesRemaining = 2;
uint32_t welcomeRefreshAt = 0;

const char *tr(const char *spanish, const char *englishText) {
  return english ? englishText : spanish;
}

const char *screenName(Screen value) {
  switch (value) {
    case Screen::DASHBOARD: return tr("ALTAR // DASHBOARD", "ALTAR // DASHBOARD");
    case Screen::NETWORK: return tr("REDES // NETWORK", "NETWORK");
    case Screen::LED: return tr("VELAS // LED STUDIO", "CANDLES // LED STUDIO");
    case Screen::NFC: return tr("NFC // STUDIO", "NFC // STUDIO");
    case Screen::OFFERINGS: return tr("LAS OFRENDAS", "OFFERINGS");
    case Screen::SYSTEM: return tr("SISTEMA // SYSTEM", "SYSTEM");
    case Screen::LOGS: return tr("DIAGNOSTICOS // LOGS", "DIAGNOSTICS // LOGS");
    case Screen::HELP: return tr("AYUDA // HELP", "HELP");
  }
  return "SANTA MUERTE";
}

void color(const char *code) { if (ansi) Serial.print(code); }
void resetColor() { if (ansi) Serial.print("\x1b[0m"); }
void rowStart() { Serial.write('\r'); }
void tuiLine(const char *value) { rowStart(); Serial.println(value); }
void tuiLine(const String &value) { rowStart(); Serial.println(value); }
// HardwareSerial::printf emits a bare LF. That is acceptable in a log viewer,
// but `screen` treats it as "down one row, same column." Prefixing each TUI
// row with CR makes every formatted line unambiguously start at column zero.
void tuiPrintf(const char *format, ...) {
  char buffer[768];
  va_list arguments;
  va_start(arguments, format);
  const int length = vsnprintf(buffer, sizeof(buffer), format, arguments);
  va_end(arguments);
  Serial.write('\r');
  if (length > 0) Serial.write(reinterpret_cast<const uint8_t *>(buffer),
                               min(length, static_cast<int>(sizeof(buffer) - 1)));
}
void clearScreen() { if (ansi) Serial.print("\x1b[2J\x1b[H"); else Serial.print("\r\n\r\n------------------------------------------------------------\r\n"); }
void line() { rowStart(); color("\x1b[38;5;137m"); Serial.println("------------------------------------------------------------"); resetColor(); }
void title(const char *value) { rowStart(); color("\x1b[1;38;5;179m"); Serial.println(value); resetColor(); }
void muted(const String &value) { rowStart(); color("\x1b[38;5;245m"); Serial.println(value); resetColor(); }
void good(const String &value) { rowStart(); color("\x1b[38;5;150m"); Serial.println(value); resetColor(); }
void warn(const String &value) { rowStart(); color("\x1b[38;5;215m"); Serial.println(value); resetColor(); }
void danger(const String &value) { rowStart(); color("\x1b[1;38;5;203m"); Serial.println(value); resetColor(); }

String mask(const char *value) {
  if (!value || !value[0]) return tr("(sin guardar)", "(not saved)");
  String result;
  for (size_t i = 0; value[i] && i < 63; ++i) result += '*';
  return result;
}

String maskedInput() {
  String result;
  for (size_t i = 0; i < input.length(); ++i) result += '*';
  return result;
}

String clipped(const String &value, size_t limit) {
  if (value.length() <= limit) return value;
  return value.substring(0, limit - 2) + "..";
}

String localizedNfcMessage(const String &message) {
  if (!english) return message;
  if (message == "Ofrenda NFC encendida. Cada tag que se lea se va a las ofrendas.") {
    return "NFC Offering on. Every read tag joins the offerings.";
  }
  if (message == "Ofrenda NFC apagada.") return "NFC Offering off.";
  if (message == "Listo para leer") return "Ready to read";
  if (message == "Emulación activa") return "Emulation active";
  if (message == "Emulación parada") return "Emulation stopped";
  return message;
}

void appendLog(const char *module, const String &message) {
  LogLine &line = logs[logHead];
  strlcpy(line.module, module ? module : "TUI", sizeof(line.module));
  strlcpy(line.text, message.c_str(), sizeof(line.text));
  line.at = millis();
  logHead = (logHead + 1) % LOG_CAPACITY;
  if (logCount < LOG_CAPACITY) ++logCount;
}

void setNotice(const String &message, bool error = false) {
  notice = message;
  appendLog(error ? "ERROR" : "TUI", message);
  needsRedraw = true;
}

void header() {
  clearScreen();
  // Twin votives mirror the web altar. Each row begins at column zero because
  // basic serial terminals do not treat LF as a carriage return.
  rowStart();
  color("\x1b[1;38;5;214m"); Serial.print("   .");
  color("\x1b[1;38;5;179m"); Serial.print("                 S A N T A   M U E R T E");
  color("\x1b[1;38;5;214m"); Serial.println("                 .");
  rowStart();
  color("\x1b[38;5;203m"); Serial.print("  /~\\");
  color("\x1b[38;5;88m"); Serial.print("            // USB ALTAR // 115200 //");
  color("\x1b[38;5;203m"); Serial.println("            /~\\");
  rowStart();
  color("\x1b[38;5;179m"); Serial.print("  |#|");
  resetColor(); Serial.print("                                     ");
  color("\x1b[38;5;179m"); Serial.println("|#|");
  rowStart();
  color("\x1b[1;38;5;137m"); Serial.print(" _|#|_");
  resetColor(); Serial.print("                                   ");
  color("\x1b[1;38;5;137m"); Serial.println("_|#|_");
  resetColor();
  line();
  title(screenName(screen));
}

void footer() {
  line();
  muted(tr("1 Inicio  2 Red  3 LED  4 NFC  5 Ofrendas  6 Sistema",
           "1 Home   2 Network  3 LED  4 NFC  5 Offerings  6 System"));
  muted(tr("L English  ? Ayuda  p ANSI/texto  r Logs  q/ESC Inicio",
           "L Español  ? Help  p ANSI/plain  r Logs  q/ESC Home"));
  if (notice.length()) { rowStart(); color("\x1b[38;5;215m"); Serial.print("> "); Serial.println(notice); resetColor(); }
}

void renderDashboard() {
  const WifiTuiState wifi = getWifiTuiState();
  const LedTuiState led = getLedTuiState();
  const NfcTuiState nfc = getNfcTuiState();
  tuiPrintf("%-8s %s\n", tr("RED", "NETWORK"), wifi.accessPointActive ? "Santa Muerte AP" : tr("Wi-Fi de casa", "home Wi-Fi"));
  if (wifi.accessPointActive) tuiPrintf("SSID     %s  //  10.69.4.20\n", wifi.accessPointSsid.c_str());
  else tuiPrintf("%-8s %s // %s // %s.local\n", tr("CASA", "HOME"), wifi.homeSsid.c_str(), wifi.localIp.c_str(), wifi.hostname.c_str());
  tuiPrintf("LED      %s  RGB %u,%u,%u  B:%u V:%u\n", led.pattern, led.red, led.green, led.blue, led.brightness, led.speed);
  tuiPrintf("NFC      %s%s\n", nfc.readerReady ? tr("listo", "ready") : tr("PN532 fuera", "PN532 offline"), nfc.emulating ? tr(" / emulando", " / emulating") : nfc.captureEnabled ? tr(" / ofrendando", " / capturing") : "");
  muted(clipped(localizedNfcMessage(nfc.message), 64));
  tuiPrintf("%-8s %u / %u %s // %u %s\n", tr("OFRENDAS", "OFFERINGS"), boardStoredCount(), boardCapacity(), tr("textos", "texts"), boardImageCapacity(), tr("dibujos", "drawings"));
  tuiPrintf("%-8s %u KB %s // %u KB // %lus\n", tr("MEMORIA", "MEMORY"), ESP.getFreeHeap() / 1024, tr("libres", "free"), ESP.getMaxAllocHeap() / 1024, millis() / 1000);
  Serial.println();
  const char *menu[] = {"Dashboard", tr("Red", "Network"), "LED Studio", "NFC Studio", tr("Ofrendas", "Offerings"), tr("Sistema", "System")};
  for (uint8_t i = 0; i < 6; ++i) {
    tuiPrintf("%c %u %s\n", dashboardSelection == i ? '>' : ' ', i + 1, menu[i]);
  }
  muted(tr("Flechas o j/k eligen; Enter abre. L cambia idioma.",
           "Arrows or j/k select; Enter opens. L changes language."));
}

void renderNetwork() {
  const WifiTuiState wifi = getWifiTuiState();
  tuiPrintf("%s: %s\n\n", tr("MODO ACTIVO", "ACTIVE MODE"), wifi.accessPointActive ? "SANTA MUERTE AP" : tr("WI-FI DE CASA", "HOME WI-FI"));
  tuiPrintf("AP   %s // %s %s // %s\n", wifi.accessPointSsid.c_str(), tr("clave", "key"), revealSecrets ? getBadgeWifiPassword() : mask(getBadgeWifiPassword()).c_str(), wifi.hidden ? tr("oculta", "hidden") : tr("visible", "visible"));
  tuiPrintf("%s %s // %s %s // %s\n\n", tr("CASA", "HOME"), wifi.homeConfigured ? wifi.homeSsid.c_str() : tr("sin guardar", "not saved"), tr("clave", "key"), revealSecrets ? getPersistentStationWifiPassword() : mask(getPersistentStationWifiPassword()).c_str(), wifi.homeConnected ? wifi.localIp.c_str() : tr("sin conectar", "offline"));
  tuiLine(tr("1 Cambiar a Santa Muerte AP", "1 Switch to Santa Muerte AP"));
  tuiLine(tr("2 Cambiar a Wi-Fi de casa", "2 Switch to home Wi-Fi"));
  tuiLine(tr("3 Cambiar clave del AP", "3 Change AP password"));
  tuiLine(tr("4 Guardar Wi-Fi de casa y cambiar", "4 Save and switch home Wi-Fi"));
  tuiLine(tr("5 Alternar SSID AP visible/oculto", "5 Toggle AP SSID visible/hidden"));
  tuiLine(tr("v Revelar claves por 10 segundos", "v Reveal passwords for 10 seconds"));
}

void renderLed() {
  const LedTuiState led = getLedTuiState();
  tuiPrintf("%s: %s // #%02X%02X%02X // %s %u // %s %u\n\n", tr("ACTUAL", "CURRENT"), led.pattern, led.red, led.green, led.blue, tr("brillo", "brightness"), led.brightness, tr("velocidad", "speed"), led.speed);
  tuiLine(tr("1 Sólido", "1 Solid"));
  tuiLine(tr("2 Arcoíris", "2 Rainbow"));
  tuiLine(tr("3 Carrera", "3 Chase"));
  tuiLine(tr("4 Pulso", "4 Pulse"));
  tuiLine(tr("5 Destello", "5 Twinkle"));
  tuiLine(tr("6 Teatro", "6 Theater"));
  tuiLine("7 Aurora");
  tuiLine(tr("8 Apagado", "8 Off"));
  tuiLine("9 Ofrenda");
  tuiLine("a Corona");
  tuiLine("b Aureola");
  tuiLine("c Encuentro");
  tuiLine(tr("d Manos", "d Hands"));
  tuiLine(tr("e Escáner", "e Scanner"));
  tuiLine("f Plasma");
  tuiLine(tr("g Deriva", "g Drift"));
  tuiLine(tr("h Color hexadecimal", "h Hex colour"));
  tuiLine(tr("i Brillo", "i Brightness"));
  tuiLine(tr("j Velocidad", "j Speed"));
  tuiLine(tr("k Identificar LEDs", "k Identify LEDs"));
}

void renderNfc() {
  const NfcTuiState nfc = getNfcTuiState();
  tuiPrintf("%s: %s // %s\n", tr("LECTOR", "READER"), nfc.readerReady ? tr("listo", "ready") : tr("PN532 fuera", "PN532 offline"), clipped(nfc.message, 48).c_str());
  tuiPrintf("%s: %s\n", tr("ESTADO", "STATE"), clipped(nfc.status, 58).c_str());
  tuiPrintf("%s: %lu // TAGS: %lu\n\n", tr("CAPTURAS", "CAPTURES"), static_cast<unsigned long>(nfc.captureCount), static_cast<unsigned long>(nfc.tagScans));
  tuiLine(tr("1 Leer tag", "1 Read tag"));
  tuiLine(tr("2 Escribir texto", "2 Write text"));
  tuiLine(tr("3 Escribir URL", "3 Write URL"));
  tuiLine(tr("4 Alternar NFC Offering", "4 Toggle NFC Offering"));
  tuiLine(tr("5 Emular texto", "5 Emulate text"));
  tuiLine(tr("6 Emular URL", "6 Emulate URL"));
  tuiLine(tr("7 Emular Wi-Fi del badge", "7 Emulate badge Wi-Fi"));
  tuiLine(tr("8 Parar emulación", "8 Stop emulation"));
}

void renderOfferings() {
  tuiPrintf("%u / %u %s // %s %u %s\n\n", boardStoredCount(), boardCapacity(), tr("textos guardados", "stored texts"), tr("ring", "ring"), boardImageCapacity(), tr("dibujos", "drawings"));
  uint32_t cursor = 0;
  BoardPost post;
  uint8_t shown = 0;
  while (shown < 7 && readNextBoardPost(cursor, post)) {
    tuiPrintf("%lu  %s%s\n", static_cast<unsigned long>(post.id), post.text.length() ? post.text.c_str() : "[dibujo sin texto]", post.hasImage ? "  [imagen]" : "");
    ++shown;
  }
  if (!shown) muted(tr("El pasillo está vacío.", "The hall is empty."));
  tuiLine(tr("1 Nueva ofrenda de texto", "1 New text offering"));
  tuiLine(tr("2 Borrar todas las ofrendas", "2 Clear all offerings"));
}

void renderSystem() {
  const WifiTuiState wifi = getWifiTuiState();
  tuiPrintf("USB 115200 // %s %s // %s %s.local\n", tr("modo", "mode"), wifi.accessPointActive ? "AP" : tr("casa", "home"), tr("hostname", "hostname"), wifi.hostname.c_str());
  tuiPrintf("LittleFS %u / %u KB %s\n", static_cast<unsigned>(LittleFS.usedBytes() / 1024), static_cast<unsigned>(LittleFS.totalBytes() / 1024), tr("usados", "used"));
  tuiPrintf("Heap %u KB // %s %u KB\n\n", ESP.getFreeHeap() / 1024, tr("bloque mayor", "largest block"), ESP.getMaxAllocHeap() / 1024);
  tuiLine(tr("1 Diagnósticos capturados", "1 Captured diagnostics"));
  tuiLine(tr("2 Flujo de logs crudos", "2 Raw log stream"));
  tuiLine(tr("3 Mostrar credenciales", "3 Show credentials"));
  tuiLine(tr("4 Reiniciar badge", "4 Reboot badge"));
  tuiLine(tr("5 Cambiar idioma (English)", "5 Switch language (Español)"));
}

void renderLogs() {
  tuiLine(tr("Últimos eventos del TUI (r inicia/termina flujo crudo):", "Latest TUI events (r starts/stops raw log stream):"));
  for (uint8_t i = 0; i < logCount; ++i) {
    const uint8_t index = (logHead + LOG_CAPACITY - logCount + i) % LOG_CAPACITY;
    const LogLine &entry = logs[index];
    tuiPrintf("%6lus %-10s %s\n", static_cast<unsigned long>(entry.at / 1000), entry.module, entry.text);
  }
}

void renderHelp() {
  tuiLine(tr("FLECHAS / j k: elegir secciones en el Dashboard. ENTER abre.", "ARROWS / j k: select on Dashboard. ENTER opens."));
  tuiLine(tr("ESC: volver.  1-6: secciones. p: ANSI/texto. r: logs crudos.", "ESC: back. 1-6: sections. p: ANSI/plain. r: raw logs."));
  tuiLine(tr("Desconectar, NFC, borrar y reiniciar piden y/n.", "Network, NFC, erase, and reboot require y/n."));
  tuiLine(tr("USB admite texto; dibujos/fotos y flasheo son del portal.", "USB supports text; drawings/photos and flashing stay on the web."));
}

void renderPrompt() {
  line();
  if (confirm != Confirm::NONE) { warn("¿Confirmar? [y/n]"); return; }
  if (prompt != Prompt::NONE) {
    const bool secret = prompt == Prompt::AP_PASSWORD || prompt == Prompt::HOME_PASSWORD;
    rowStart();
    Serial.print(tr("Ingrese valor (Enter guarda, Esc cancela): ", "Enter value (Enter saves, Esc cancels): "));
    if (secret) Serial.println(maskedInput()); else Serial.println(input);
  }
}

void render() {
  if (rawStream) return;
  header();
  switch (screen) {
    case Screen::DASHBOARD: renderDashboard(); break;
    case Screen::NETWORK: renderNetwork(); break;
    case Screen::LED: renderLed(); break;
    case Screen::NFC: renderNfc(); break;
    case Screen::OFFERINGS: renderOfferings(); break;
    case Screen::SYSTEM: renderSystem(); break;
    case Screen::LOGS: renderLogs(); break;
    case Screen::HELP: renderHelp(); break;
  }
  renderPrompt();
  footer();
  needsRedraw = false;
  if (welcomeRefreshesRemaining > 0) welcomeRefreshAt = millis() + 1400;
}

void beginPrompt(Prompt next, const String &message) { prompt = next; input = String(); notice = message; needsRedraw = true; }
void beginConfirm(Confirm next, const String &message) { confirm = next; notice = message; needsRedraw = true; }

bool parseHex(const String &value, int &r, int &g, int &b) {
  String text = value; if (text.startsWith("#")) text.remove(0, 1);
  if (text.length() != 6) return false;
  char *end = nullptr; const long number = strtol(text.c_str(), &end, 16);
  if (!end || *end) return false;
  r = (number >> 16) & 0xFF; g = (number >> 8) & 0xFF; b = number & 0xFF; return true;
}

void applyConfirm() {
  String error;
  bool ok = false;
  switch (confirm) {
    case Confirm::SWITCH_AP: ok = setBadgeAccessPointEnabled(true, error); break;
    case Confirm::SWITCH_HOME: ok = setBadgeAccessPointEnabled(false, error); break;
    case Confirm::SAVE_AP: ok = setBadgeAccessPointSettings(stagedA, getPersistentWifiHidden(), error); break;
    case Confirm::TOGGLE_AP_HIDDEN:
      ok = setBadgeAccessPointSettings(getBadgeWifiPassword(), !getPersistentWifiHidden(), error);
      break;
    case Confirm::SAVE_HOME: ok = setBadgeHomeWifiSettings(stagedA, stagedB, error); break;
    case Confirm::NFC_WRITE_TEXT: ok = queueNfcWrite("text", stagedA); break;
    case Confirm::NFC_WRITE_URL: ok = queueNfcWrite("url", stagedA); break;
    case Confirm::EMU_TEXT: ok = startNfcTagEmulation("text", stagedA); break;
    case Confirm::EMU_URL: ok = startNfcTagEmulation("url", stagedA); break;
    case Confirm::NFC_WIFI: {
      if (!isBadgeAccessPointActive()) {
        error = "Activa primero Santa Muerte AP; Wi-Fi de casa no se comparte por NFC.";
        break;
      }
      uint8_t mac[6] = {}; ok = startNfcWifiOnboarding(getBadgeWifiSsid(), getBadgeWifiPassword(), getBadgeWifiApMac(mac) ? mac : nullptr); break;
    }
    case Confirm::CLEAR_BOARD: ok = clearBoard(); break;
    case Confirm::POST_OFFERING: ok = addBoardPost(stagedA, 0, nullptr, 0, error); break;
    case Confirm::REBOOT: Serial.println("[TUI] Reiniciando..."); delay(80); ESP.restart(); return;
    default: break;
  }
  setNotice(ok ? "Hecho." : error.length() ? error : "No se pudo completar la acción.", !ok);
  confirm = Confirm::NONE;
}

void completePrompt() {
  if (prompt == Prompt::AP_PASSWORD) { stagedA = input; beginConfirm(Confirm::SAVE_AP, "Guardar nueva clave del AP"); }
  else if (prompt == Prompt::HOME_SSID) { stagedA = input; beginPrompt(Prompt::HOME_PASSWORD, "Clave de Wi-Fi de casa"); }
  else if (prompt == Prompt::HOME_PASSWORD) { stagedB = input; beginConfirm(Confirm::SAVE_HOME, "Guardar y cambiar a Wi-Fi de casa"); }
  else if (prompt == Prompt::LED_HEX) { int r, g, b; if (parseHex(input, r, g, b) && setLedTuiState("", r, g, b, -1, -1)) setNotice("Color actualizado."); else setNotice("Usa #RRGGBB.", true); prompt = Prompt::NONE; }
  else if (prompt == Prompt::LED_BRIGHTNESS) { const int value = input.toInt(); if (value >= 0 && value <= 255 && setLedTuiState("", -1, -1, -1, value, -1)) setNotice("Brillo actualizado."); else setNotice("Brillo: 0 a 255.", true); prompt = Prompt::NONE; }
  else if (prompt == Prompt::LED_SPEED) { const int value = input.toInt(); if (value >= 1 && value <= 100 && setLedTuiState("", -1, -1, -1, -1, value)) setNotice("Velocidad actualizada."); else setNotice("Velocidad: 1 a 100.", true); prompt = Prompt::NONE; }
  else if (prompt == Prompt::NFC_TEXT) { stagedA = input; prompt = Prompt::NONE; beginConfirm(Confirm::NFC_WRITE_TEXT, "Escribir texto en el próximo tag"); }
  else if (prompt == Prompt::NFC_URL) { stagedA = input; prompt = Prompt::NONE; beginConfirm(Confirm::NFC_WRITE_URL, "Escribir URL en el próximo tag"); }
  else if (prompt == Prompt::EMU_TEXT) { stagedA = input; prompt = Prompt::NONE; beginConfirm(Confirm::EMU_TEXT, "Emular tag de texto"); }
  else if (prompt == Prompt::EMU_URL) { stagedA = input; prompt = Prompt::NONE; beginConfirm(Confirm::EMU_URL, "Emular tag de URL"); }
  else if (prompt == Prompt::OFFERING) { stagedA = input; prompt = Prompt::NONE; beginConfirm(Confirm::POST_OFFERING, "Publicar esta ofrenda de texto"); }
  needsRedraw = true;
}

void routeGlobal(char key) {
  if (key == '?') { screen = Screen::HELP; needsRedraw = true; return; }
  if (key == 'p' || key == 'P') { ansi = !ansi; needsRedraw = true; return; }
  if (key == 'l' || key == 'L') {
    String error;
    const bool next = !english;
    if (setPersistentEnglishLanguage(next, error)) {
      english = next;
      setNotice(english ? "Language: English" : "Idioma: Español");
    } else {
      setNotice(error, true);
    }
    return;
  }
  if (key == 'r' || key == 'R') { rawStream = !rawStream; if (!rawStream) needsRedraw = true; else Serial.println("\n[RAW LOGS] Pulsa cualquier tecla para volver al TUI.\n"); return; }
  // A capital A is deliberately available everywhere as a recovery shortcut.
  // Lowercase a remains an LED pattern shortcut in LED Studio.
  if (key == 'A') {
    beginConfirm(isBadgeAccessPointActive() ? Confirm::SWITCH_HOME : Confirm::SWITCH_AP,
                 isBadgeAccessPointActive() ? "Cambiar a Wi-Fi de casa (apaga el AP)" : "Cambiar a Santa Muerte AP (corta Wi-Fi de casa)");
  }
}

void handleScreenKey(char key) {
  if (key == 27 || key == 'q' || key == 'Q') { screen = Screen::DASHBOARD; needsRedraw = true; return; }
  if (screen == Screen::NETWORK) {
    const WifiTuiState wifi = getWifiTuiState();
    if (key == '1') beginConfirm(Confirm::SWITCH_AP, "Cambiar a Santa Muerte AP (corta Wi-Fi de casa)");
    else if (key == '2') beginConfirm(Confirm::SWITCH_HOME, "Cambiar a Wi-Fi de casa (apaga el AP)");
    else if (key == '3') beginPrompt(Prompt::AP_PASSWORD, "Nueva clave WPA2 del AP");
    else if (key == '4') beginPrompt(Prompt::HOME_SSID, "SSID de Wi-Fi de casa");
    else if (key == '5') beginConfirm(Confirm::TOGGLE_AP_HIDDEN, wifi.hidden ? "Hacer visible el SSID Santa Muerte" : "Ocultar el SSID Santa Muerte");
    else if (key == 'v' || key == 'V') { revealSecrets = true; revealUntil = millis() + REVEAL_MS; setNotice("Claves visibles durante 10 segundos."); }
  } else if (screen == Screen::LED) {
    const char *patterns[] = {"solid","rainbow","chase","pulse","twinkle","theater","aurora","off","ofrenda","corona","aureola","encuentro","manos","escaner","plasma","deriva"};
    int index = -1;
    if (key >= '1' && key <= '9') index = key - '1';
    else if (key >= 'a' && key <= 'g') index = 9 + key - 'a';
    if (index >= 0 && index < 16) setNotice(setLedTuiState(patterns[index], -1, -1, -1, -1, -1) ? "Patrón actualizado." : "Patrón inválido.", index < 0);
    else if (key == 'h') beginPrompt(Prompt::LED_HEX, "Color hexadecimal");
    else if (key == 'i') beginPrompt(Prompt::LED_BRIGHTNESS, "Brillo 0-255");
    else if (key == 'j') beginPrompt(Prompt::LED_SPEED, "Velocidad 1-100");
    else if (key == 'k') { const LedTuiState led = getLedTuiState(); setLedTuiIdentifyFrame((led.identifyFrame + 1) % 4); setNotice("Marco de identificación cambiado."); }
  } else if (screen == Screen::NFC) {
    if (key == '1') setNotice(queueNfcRead() ? "Lectura en cola. Acerca un tag." : "No se pudo iniciar lectura.", false);
    else if (key == '2') beginPrompt(Prompt::NFC_TEXT, "Texto para escribir");
    else if (key == '3') beginPrompt(Prompt::NFC_URL, "URL para escribir");
    else if (key == '4') { const bool next = !isNfcCaptureEnabled(); setNotice(setNfcCaptureEnabled(next) ? next ? "NFC Offering encendida." : "NFC Offering apagada." : "No se pudo cambiar NFC Offering.", false); }
    else if (key == '5') beginPrompt(Prompt::EMU_TEXT, "Texto para emular");
    else if (key == '6') beginPrompt(Prompt::EMU_URL, "URL para emular");
    else if (key == '7') beginConfirm(Confirm::NFC_WIFI, "Emular Wi-Fi del badge");
    else if (key == '8') setNotice(stopNfcTagEmulation() ? "Parando emulación." : "No se pudo parar.", false);
  } else if (screen == Screen::OFFERINGS) {
    if (key == '1') beginPrompt(Prompt::OFFERING, "Texto de la ofrenda");
    else if (key == '2') beginConfirm(Confirm::CLEAR_BOARD, "Borrar todas las ofrendas");
  } else if (screen == Screen::SYSTEM) {
    if (key == '1') { screen = Screen::LOGS; needsRedraw = true; }
    else if (key == '2') { rawStream = true; Serial.println("\n[RAW LOGS] Pulsa cualquier tecla para volver al TUI.\n"); }
    else if (key == '3') { revealSecrets = true; revealUntil = millis() + REVEAL_MS; screen = Screen::NETWORK; setNotice("Claves visibles durante 10 segundos."); }
    else if (key == '4') beginConfirm(Confirm::REBOOT, "Reiniciar el badge");
    else if (key == '5') routeGlobal('l');
  }
  needsRedraw = true;
}

void handleKey(char key) {
  if (rawStream) { rawStream = false; needsRedraw = true; return; }
  if (escapeState == 1) { escapeState = key == '[' ? 2 : 0; return; }
  if (escapeState == 2) {
    if ((key == 'A' || key == 'B') && screen == Screen::DASHBOARD) {
      dashboardSelection = key == 'A' ? (dashboardSelection + 5) % 6 : (dashboardSelection + 1) % 6;
      needsRedraw = true;
    }
    escapeState = 0;
    return;
  }
  if (key == 27) { escapeState = 1; escapeAt = millis(); return; }
  if (confirm != Confirm::NONE) { if (key == 'y' || key == 'Y') applyConfirm(); else if (key == 'n' || key == 'N') { confirm = Confirm::NONE; setNotice("Cancelado."); } return; }
  if (prompt != Prompt::NONE) {
    if (key == '\r' || key == '\n') { if (input.length()) completePrompt(); else setNotice("Escribe un valor.", true); return; }
    if (key == 8 || key == 127) { if (input.length()) input.remove(input.length() - 1); needsRedraw = true; return; }
    if (key >= 32 && key <= 126 && input.length() < 700) { input += key; needsRedraw = true; }
    return;
  }
  if (screen == Screen::DASHBOARD && key >= '1' && key <= '6') {
    screen = static_cast<Screen>(key - '1');
    needsRedraw = true;
    return;
  }
  if (screen == Screen::DASHBOARD && (key == 'j' || key == 'k')) {
    dashboardSelection = key == 'j' ? (dashboardSelection + 1) % 6 : (dashboardSelection + 5) % 6;
    needsRedraw = true;
    return;
  }
  if (screen == Screen::DASHBOARD && (key == '\r' || key == '\n')) {
    screen = static_cast<Screen>(dashboardSelection);
    needsRedraw = true;
    return;
  }
  routeGlobal(key);
  handleScreenKey(key);
}

}  // namespace

void usbTuiBegin() {
  english = getPersistentEnglishLanguage();
  welcomeRefreshesRemaining = 2;
  welcomeRefreshAt = 0;
  appendLog("TUI", "USB Altar listo");
  needsRedraw = true;
}

void usbTuiLog(const char *module, const String &message) {
  appendLog(module, message);
  if (rawStream) tuiPrintf("[%s] %s\n", module ? module : "LOG", message.c_str());
}

void usbTuiService() {
  while (Serial.available()) handleKey(static_cast<char>(Serial.read()));
  if (escapeState == 1 && millis() - escapeAt > 60) {
    escapeState = 0;
    screen = Screen::DASHBOARD;
    needsRedraw = true;
  }
  if (revealSecrets && static_cast<int32_t>(millis() - revealUntil) >= 0) { revealSecrets = false; needsRedraw = true; }
  if (!rawStream && welcomeRefreshesRemaining > 0 && welcomeRefreshAt != 0 &&
      static_cast<int32_t>(millis() - welcomeRefreshAt) >= 0) {
    --welcomeRefreshesRemaining;
    needsRedraw = true;
  }
  // A serial monitor is often only a byte viewer rather than a terminal
  // emulator. Redraw only after input/state changes, never on a timer.
  if (!rawStream && needsRedraw) render();
}

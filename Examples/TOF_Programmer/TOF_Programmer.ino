/*
  SoBotz TOF Distance Sensor - Arduino programmer / passthrough
  -------------------------------------------------------------
  Turns an Arduino Nano / Uno (ATmega328P) or any ESP32 board into a programming
  adapter for the TOF distance sensor, for use with the SoBotz web tool
  (interface = "Arduino programmer").

  The board powers the sensor from GPIO pin(s), so it can power-cycle it and
  hold SIG low during boot - the sensor is forced into serial mode automatically,
  no manual shorting of SIG to GND. SIG is driven truly open-drain, so there is
  no push-pull TX fighting the sensor like with a USB-UART adapter.

  Powering from a GPIO pin: one pin is enough. The sensor draws ~25 mA, so the pin
  voltage sags a little (on a 3.3 V ESP32 it stays around 3 V, above the sensor's
  2.8 V minimum), but it is only powered like this while you configure it. If the
  sensor keeps restarting, add a second pin to the power list and tie it to VIN too.

  Pull-up: a resistor from SIG to the sensor's VIN pin. 2k-10k works in most
  cases (4.7k is a good default); use the lower end for fast baud rates or long wires.

  Arduino Nano / Uno wiring (all on one header, side by side)
    D8  ---------------- SIG  (white)   fixed: SingleWireSerial input-capture pin
    D9  ---------------- VIN  (red)     the sensor is powered from this pin
    GND ---------------- GND  (black)
    pull-up resistor between D8 and D9
    D10, D3 and D4 switch together with D9 (D3/D4 keep older wiring with VIN on D3 working).
    Requires the "SingleWireSerial" library (Arduino Library Manager).

  ESP32 wiring (defaults below are for the ESP32-C3 SuperMini)
    GPIO3 -------------- SIG  (white)
    GPIO4 -------------- VIN  (red)     the sensor is powered from this pin
    GND ---------------- GND  (black)
    pull-up resistor between GPIO3 and GPIO4 (neighbouring pins)
    Any ESP32 works (ESP32, S2, S3, C3, C6...): any output-capable GPIOs can be used,
    just avoid strapping pins (C3: GPIO2/8/9, ESP32: 0/2/5/12/15), flash pins and the
    USB pins. Change ESP_SIG_PIN / ESP_PWR_PINS below. No extra library needed.
    Enable "USB CDC On Boot" for boards that use the chip's built-in USB.

  USB protocol: 115200 baud, lines end with '\n'
    !I          -> !SoBotz-TOF-PROG <version>
    !S          -> power-cycle with SIG held low (sensor: serial mode, 9600, ID 00)
                   replies !OK <status>, or !E <reason> if the sensor does not answer
    !N          -> normal power-cycle (sensor boots in its saved mode)  -> !OK
    !B<baud>    -> set the sensor-side baud rate, e.g. !B115200        -> !OK
    !P0 / !P1   -> sensor power off / on                               -> !OK
    !M          -> measure the SIG pin: !M <level> <high_us> <low_us>
                   (high_us / low_us are 0 if the line is not toggling)
    anything else is sent to the sensor as-is; the sensor's reply line is
    returned, or !T if the sensor did not answer.
*/

#define FW_VERSION 5
#define USB_BAUD   115200
#define POWER_OFF_MS   400    // time the sensor stays unpowered
#define BOOT_MS        500    // SIG held low this long after power-up (as in the repo example)

// ---------------------------------------------------------------- ESP32 ----
#if defined(ARDUINO_ARCH_ESP32)
#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_rom_gpio.h"
#include "soc/uart_periph.h"

#define ESP_SIG_PIN 3                          // SIG
const uint8_t ESP_PWR_PINS[] = {4};             // VIN (add more pins here to share the current)
#define SIG_PIN    ESP_SIG_PIN
#define SIG_UART   UART_NUM_1
#define PWR_PINS   ESP_PWR_PINS

long sensorBaud = 9600;
bool sigActive = false;

void powerSet(bool on) {
  for (uint8_t p : PWR_PINS) digitalWrite(p, on);
}
void powerInit() {
  for (uint8_t p : PWR_PINS) {
    pinMode(p, OUTPUT);
    gpio_set_drive_capability((gpio_num_t)p, GPIO_DRIVE_CAP_3);   // strongest drive
  }
}
void sigRelease() { pinMode(SIG_PIN, INPUT); }
void sigHoldLow() { pinMode(SIG_PIN, OUTPUT); digitalWrite(SIG_PIN, LOW); }

// UART1 TX and RX are both routed to the one SIG pin, which is set open-drain.
void sigStart() {
  if (sigActive) return;
  uart_config_t cfg = {};
  cfg.baud_rate  = sensorBaud;
  cfg.data_bits  = UART_DATA_8_BITS;
  cfg.parity     = UART_PARITY_DISABLE;
  cfg.stop_bits  = UART_STOP_BITS_1;
  cfg.flow_ctrl  = UART_HW_FLOWCTRL_DISABLE;
  cfg.source_clk = UART_SCLK_DEFAULT;
  uart_driver_install(SIG_UART, 256, 0, 0, NULL, 0);
  uart_param_config(SIG_UART, &cfg);
  uart_set_pin(SIG_UART, SIG_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
  gpio_set_pull_mode((gpio_num_t)SIG_PIN, GPIO_FLOATING);
  gpio_set_direction((gpio_num_t)SIG_PIN, GPIO_MODE_INPUT_OUTPUT_OD);
  esp_rom_gpio_connect_in_signal(SIG_PIN, UART_PERIPH_SIGNAL(SIG_UART, SOC_UART_RX_PIN_IDX), false);
  sigActive = true;
}
void sigStop() {
  if (!sigActive) return;
  uart_driver_delete(SIG_UART);
  gpio_reset_pin((gpio_num_t)SIG_PIN);
  sigRelease();
  sigActive = false;
}

// Read one '\n'-terminated line from SIG. Returns its length, or -1 on timeout.
int sigReadLine(char* out, uint8_t maxLen, uint32_t firstMs, uint32_t totalMs) {
  uint8_t n = 0;
  bool gotAny = false;
  uint32_t t0 = millis();
  while (millis() - t0 < totalMs) {
    uint8_t c;
    if (uart_read_bytes(SIG_UART, &c, 1, pdMS_TO_TICKS(2)) == 1) {
      gotAny = true;
      if (c == '\n') { out[n] = 0; return n; }
      if (c != '\r' && n < maxLen - 1) out[n++] = (char)c;
      continue;
    }
    if (!gotAny && millis() - t0 > firstMs) break;
  }
  out[n] = 0;
  return -1;
}

// The UART hears its own transmission on the shared wire: skip that echo line.
int sensorTransact(const char* cmd, char* out, uint8_t maxLen) {
  sigStart();
  uart_flush_input(SIG_UART);
  uart_write_bytes(SIG_UART, cmd, strlen(cmd));
  uart_write_bytes(SIG_UART, "\n", 1);
  uart_wait_tx_done(SIG_UART, pdMS_TO_TICKS(200));
  int len = sigReadLine(out, maxLen, 300, 500);
  if (len >= 0 && strcmp(out, cmd) == 0) len = sigReadLine(out, maxLen, 300, 500);
  return len;
}

// ---------------------------------------------------------- AVR (Uno/Nano) ----
#else
#include <SingleWireSerial.h>

#define SIG_PIN    8          // fixed by SingleWireSerial (ICP1)
const uint8_t PWR_PINS[] = {9, 10, 3, 4};   // all switched together; VIN on D9

#define REPLY_TICKS    3000   // x100 us: wait for the first reply character
#define TOTAL_TICKS    5000   // x100 us: hard limit for one exchange (never hang)

SingleWireSerial sig(false);
long sensorBaud = 9600;
bool sigActive = false;

void powerSet(bool on) {
  for (uint8_t p : PWR_PINS) digitalWrite(p, on);
}
void powerInit() {
  for (uint8_t p : PWR_PINS) pinMode(p, OUTPUT);
}

// SIG as input, no internal pull-up. The port bit must stay 0: the library
// "sends" by toggling the pin direction (open-drain).
void sigRelease() {
  pinMode(SIG_PIN, INPUT);
  digitalWrite(SIG_PIN, LOW);
}

void sigHoldLow() {
  digitalWrite(SIG_PIN, LOW);
  pinMode(SIG_PIN, OUTPUT);
}

void sigStart() {
  if (sigActive) return;
  sigRelease();
  sig.begin(sensorBaud);
  sigActive = true;
}

void sigStop() {
  if (!sigActive) return;
  sig.end();
  sigActive = false;
}

// Send one command line to the sensor and read its reply line.
// Returns the reply length, or -1 if no complete line ('\n'-terminated) came back.
// Stray bytes (e.g. a Digital-mode output toggling) never count as a reply.
int sensorTransact(const char* cmd, char* out, uint8_t maxLen) {
  sigStart();
  Serial.flush();                         // no USB TX interrupts during the exchange
  while (sig.read() >= 0) {}

  // Above 125 kbaud the millis() interrupt is long enough to corrupt a received
  // bit, so switch it off for the exchange and time out by counting instead.
  bool fast = sensorBaud > 125000L;
  if (fast) TIMSK0 &= ~_BV(TOIE0);

  sig.print(cmd);
  sig.write('\n');

  uint8_t n = 0;
  uint16_t ticks = 0;
  int result = -1;
  bool gotAny = false;
  while (ticks < TOTAL_TICKS) {
    int c = sig.read();
    if (c >= 0) {
      gotAny = true;
      if (c == '\n') { result = n; break; }
      if (c != '\r' && n < maxLen - 1) out[n++] = (char)c;
      continue;
    }
    delayMicroseconds(100);
    ticks++;
    if (!gotAny && ticks > REPLY_TICKS) break;               // no reply at all
  }

  if (fast) TIMSK0 |= _BV(TOIE0);
  out[n] = 0;
  return result;
}
#endif

// ------------------------------------------------------------- common ----
char line[32];
uint8_t lineLen = 0;
bool lineOverflow = false;

// A valid register read reply is 1-4 hex digits.
bool isHexReply(const char* s, int len) {
  if (len < 1 || len > 4) return false;
  for (int i = 0; i < len; i++) if (!isxdigit(s[i])) return false;
  return true;
}

// Power-cycle with SIG held low: the sensor comes up in serial mode, 9600, ID 00.
// Prints !OK <status reply> or !E <reason>.
void forceSerialMode() {
  char reply[8];
  sigStop();
  powerSet(false);
  sigRelease();
  delay(POWER_OFF_MS);

  // With the sensor off and the pull-up going to the power pin (now LOW), SIG must read LOW.
  // HIGH means the sensor or its pull-up is still powered from somewhere else,
  // so this power-cycle cannot reach it.
  if (digitalRead(SIG_PIN)) {
    powerSet(true);
    Serial.println(F("!E SIG is HIGH while power is off: sensor VIN and the pull-up must both go to the power pin, not 5V/3.3V"));
    return;
  }

  sigHoldLow();
  delay(20);
  powerSet(true);
  delay(BOOT_MS);
  sigRelease();
  delay(20);
  sensorBaud = 9600;
  for (uint8_t i = 0; i < 6; i++) {
    int len = sensorTransact("R0000", reply, sizeof(reply));
    if (isHexReply(reply, len)) {
      Serial.print(F("!OK "));
      Serial.println(reply);
      return;
    }
    delay(50);
  }
  if (!digitalRead(SIG_PIN))
    Serial.println(F("!E SIG stays LOW after boot: pull-up missing, or sensor in Digital mode outputting 0 - serial mode was not entered"));
  else
    Serial.println(F("!E sensor powered up but did not answer in serial mode (9600, ID 00)"));
}

// Normal power-cycle: the sensor boots in whatever mode is saved in its memory.
void normalBoot() {
  sigStop();
  powerSet(false);
  sigRelease();
  delay(POWER_OFF_MS);
  powerSet(true);
  delay(BOOT_MS);
}

void measureSig() {
  sigStop();
  sigRelease();
  unsigned long highUs = pulseIn(SIG_PIN, HIGH, 60000UL);
  unsigned long lowUs = highUs ? pulseIn(SIG_PIN, LOW, 60000UL) : 0;
  Serial.print(F("!M "));
  Serial.print(digitalRead(SIG_PIN));
  Serial.print(' ');
  Serial.print(highUs);
  Serial.print(' ');
  Serial.println(lowUs);
}

void handleLocal(const char* cmd) {      // cmd = text after the '!'
  switch (cmd[0]) {
    case 'I':
      Serial.print(F("!SoBotz-TOF-PROG "));
      Serial.println(FW_VERSION);
      return;
    case 'S':
      forceSerialMode();
      return;
    case 'N':
      normalBoot();
      Serial.println(F("!OK"));
      return;
    case 'B': {
      long b = atol(cmd + 1);
      if (b < 1200 || b > 250000L) { Serial.println(F("!E bad baud rate")); return; }
      sigStop();
      sensorBaud = b;
      Serial.println(F("!OK"));
      return;
    }
    case 'P':
      if (cmd[1] == '0') { sigStop(); powerSet(false); sigRelease(); }
      else powerSet(true);
      Serial.println(F("!OK"));
      return;
    case 'M':
      measureSig();
      return;
  }
  Serial.println(F("!E unknown command"));
}

void handleLine() {
  if (line[0] == '!') { handleLocal(line + 1); return; }
  char reply[24];
  if (sensorTransact(line, reply, sizeof(reply)) < 0) Serial.println(F("!T"));
  else Serial.println(reply);
}

void setup() {
  sigRelease();
  powerInit();
  powerSet(true);                         // sensor starts in its saved mode
  Serial.begin(USB_BAUD);
  Serial.println(F("!READY"));
}

void loop() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\r') continue;
    if (c == '\n') {
      if (lineOverflow) Serial.println(F("!E line too long"));
      else if (lineLen > 0) { line[lineLen] = 0; handleLine(); }
      lineLen = 0;
      lineOverflow = false;
    } else if (lineLen < sizeof(line) - 1) {
      line[lineLen++] = c;
    } else {
      lineOverflow = true;
    }
  }
}

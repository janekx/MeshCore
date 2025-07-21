#include <Arduino.h>
#include "PromicroBoard.h"
#include "SimpleHardwareTimer.h"

#include <bluefruit.h>
#include <Wire.h>

static BLEDfu bledfu;

void PromicroBoard::begin() {    
    // for future use, sub-classes SHOULD call this from their begin()
    startup_reason = BD_STARTUP_NORMAL;
    btn_prev_state = HIGH;
  
    pinMode(PIN_VBAT_READ, INPUT);

    #ifdef BUTTON_PIN
      pinMode(BUTTON_PIN, INPUT_PULLUP);
    #endif
    sd_power_mode_set(NRF_POWER_MODE_LOWPWR);


    #if defined(PIN_BOARD_SDA) && defined(PIN_BOARD_SCL)
      Wire.setPins(PIN_BOARD_SDA, PIN_BOARD_SCL);
    #endif
    
    Wire.begin();
    SimpleHardwareTimer::init();

    pinMode(SX126X_POWER_EN, OUTPUT);
    digitalWrite(SX126X_POWER_EN, HIGH);
    delay(10);   // give sx1262 some time to power up
}

static void connect_callback(uint16_t conn_handle) {
    (void)conn_handle;
    MESH_DEBUG_PRINTLN("BLE client connected");
}

static void disconnect_callback(uint16_t conn_handle, uint8_t reason) {
    (void)conn_handle;
    (void)reason;
    MESH_DEBUG_PRINTLN("BLE client disconnected");
}

void PromicroBoard::loop() {
  static uint32_t last_sleep_check = 0;
  uint32_t now = millis();
  
  // Light sleep check every second
  if (now - last_sleep_check > 1000) {
    #ifdef PIN_BUZZER
    if (!rtttl::isPlaying()) {
      enterLightSleep(500);
      wakeFromSleep();
    }
    #else
    enterLightSleep(500);
    wakeFromSleep();
    #endif
    
    last_sleep_check = now;
  }
}

void PromicroBoard::enterLightSleep(uint32_t timeout_ms) {
  // Skip sleep if buzzer is active
#ifdef PIN_BUZZER
  if (rtttl::isPlaying()) {
    return;
  }
#endif

  // Configure button wake-up
#ifdef BUTTON_PIN
  nrf_gpio_cfg_sense_input(
    digitalPinToInterrupt(BUTTON_PIN),
    NRF_GPIO_PIN_PULLDOWN,
    NRF_GPIO_PIN_SENSE_HIGH
  );
#endif

  if (timeout_ms == 0) {
    sd_app_evt_wait();
    return;
  }

  // Controlled sleep loop with timeout handling
  uint32_t sleep_start = millis();
  
  while ((millis() - sleep_start) < timeout_ms) {
    // Check for immediate wake conditions
#ifdef BUTTON_PIN
    if (digitalRead(BUTTON_PIN) == HIGH) {
      return;
    }
#endif
    
#ifdef PIN_BUZZER
    if (rtttl::isPlaying()) {
      return;
    }
#endif
    
    uint32_t elapsed = millis() - sleep_start;
    if (elapsed >= timeout_ms) {
      break;
    }
    
    sd_app_evt_wait();
    
#ifdef PIN_BUZZER
    if (rtttl::isPlaying()) {
      return;
    }
#endif
    
    if ((millis() - sleep_start) >= timeout_ms) {
      break;
    }
  }
}

void PromicroBoard::wakeFromSleep() {
  // Clean up GPIO configuration
#ifdef BUTTON_PIN
  nrf_gpio_cfg_input(digitalPinToInterrupt(BUTTON_PIN), NRF_GPIO_PIN_NOPULL);
#endif
}


bool PromicroBoard::startOTAUpdate(const char* id, char reply[]) {
  // Config the peripheral connection with maximum bandwidth
  // more SRAM required by SoftDevice
  // Note: All config***() function must be called before begin()
  Bluefruit.configPrphBandwidth(BANDWIDTH_MAX);
  Bluefruit.configPrphConn(92, BLE_GAP_EVENT_LENGTH_MIN, 16, 16);

  Bluefruit.begin(1, 0);
  // Set max power. Accepted values are: -40, -30, -20, -16, -12, -8, -4, 0, 4
  Bluefruit.setTxPower(4);
  // Set the BLE device name
  Bluefruit.setName("ProMicro_OTA");

  Bluefruit.Periph.setConnectCallback(connect_callback);
  Bluefruit.Periph.setDisconnectCallback(disconnect_callback);

  // To be consistent OTA DFU should be added first if it exists
  bledfu.begin();

  // Set up and start advertising
  // Advertising packet
  Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
  Bluefruit.Advertising.addTxPower();
  Bluefruit.Advertising.addName();

  /* Start Advertising
    - Enable auto advertising if disconnected
    - Interval:  fast mode = 20 ms, slow mode = 152.5 ms
    - Timeout for fast mode is 30 seconds
    - Start(timeout) with timeout = 0 will advertise forever (until connected)

    For recommended advertising interval
    https://developer.apple.com/library/content/qa/qa1931/_index.html
  */
  Bluefruit.Advertising.restartOnDisconnect(true);
  Bluefruit.Advertising.setInterval(32, 244); // in unit of 0.625 ms
  Bluefruit.Advertising.setFastTimeout(30);   // number of seconds in fast mode
  Bluefruit.Advertising.start(0);             // 0 = Don't stop advertising after n seconds

  strcpy(reply, "OK - started");
  return true;
}

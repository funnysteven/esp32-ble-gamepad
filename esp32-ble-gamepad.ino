/* THIS FILE BASICLY COPIED A EXAMPLE FILE FROM https://github.com/lemmingDev/ESP32-BLE-Gamepad
 * This code programs a number of pins on an ESP32 as buttons on a BLE gamepad
 *
 * It uses arrays to cut down on code
 *
 * Uses the Bounce2 library to debounce all buttons
 *
 * Uses the rose/fell states of the Bounce instance to track button states
 *
 * Before using, adjust the numOfButtons, buttonPins and physicalButtons to suit your senario
 *
 */

#define BOUNCE_WITH_PROMPT_DETECTION  // Make button state changes available immediately

#include <Arduino.h>
#include <Bounce2.h>     // https://github.com/thomasfredericks/Bounce2
#include <BleGamepad.h>  // https://github.com/lemmingDev/ESP32-BLE-Gamepad
#include <Keypad.h>

#define LOGI(format, ...) console_printf("%s[INFO]: " format "\n", "GAMEPAD", ##__VA_ARGS__)

#define BOOT_MODE_JOYSTICK 0
#define BOOT_MODE_DPAD 1
//press UP key during boot, to enter dpad mode
#define BOOT_MODE_PIN 6

#define numOfButtons 8

#define AXES_MIN 0
#define AXES_MAX 32767
#define AXES_CENTER 16383

Bounce debouncers[numOfButtons];
BleGamepad bleGamepad;
int bootmode = BOOT_MODE_JOYSTICK;
int16_t currX = 0;
int16_t currY = 0;
signed char currDPAD = 0;
static bool sendReport;
long lastReportTime;
long lastblink = 0;
bool firstConnected;
bool ledon = false;

//https://modernroboticsinc.com/fusion_docs/img/usbGamepad/gamepad_xbox.png
//6-up 3-down 10-left 2-right 7-A(0) 4-LT(5) 5-B(1) 8-Y(3)
byte buttonPins[numOfButtons] = { 6, 10, 3, 2, 5, 4, 7, 8 };
byte physicalButtons[numOfButtons] = { 0, 0, 0, 0, 2, 5, 1, 4 };
byte buttonStatus[numOfButtons] = { 0, 0, 0, 0, 0, 0, 0, 0 };
byte lastButtonStatus[numOfButtons] = { 1, 0, 0, 0, 0, 0, 0, 0 };

// 3*4 matrix
#define ROWS 4
#define COLS 3
char keys[ROWS][COLS] = {
  { 'h', 'b', 's' },  // home(8)   back(6)   select
  { 'S', 'd', 'i' },  // start(7)  vol_dec   vol_inc
  { 43, 47, 48 },     // 2-X(3)    4-LB(7)   5-RB(8)
  { 46, 49, 'm' }     // R-RT(6)      9-T(9)    M-mute
};
byte rowPins[ROWS] = { 9, 0, 1, 12 };  //connect to the row pinouts of the keypad
byte colPins[COLS] = { 20, 21, 13 };   //connect to the column pinouts of the keypad

Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);
char key;

void setup() {
  lastReportTime = millis();
  lastblink = millis();
  LOGI("BOOTING...");
  LOGI("lastReportTime=%d", lastReportTime);
  boot_check_mode();

  LOGI("INIT pins...");
  for (byte currentPinIndex = 0; currentPinIndex < numOfButtons; currentPinIndex++) {
    pinMode(buttonPins[currentPinIndex], INPUT_PULLUP);
    debouncers[currentPinIndex] = Bounce();
    debouncers[currentPinIndex].attach(buttonPins[currentPinIndex]);  // After setting up the button, setup the Bounce instance :
    debouncers[currentPinIndex].interval(5);
  }
  LOGI("INIT pins done");

  BleGamepadConfiguration bleGamepadConfig;

  LOGI("INIT MODE...");
  if (isJoystickMode()) {
    bleGamepadConfig.setControllerType(CONTROLLER_TYPE_JOYSTICK);  // CONTROLLER_TYPE_JOYSTICK, CONTROLLER_TYPE_GAMEPAD (DEFAULT), CONTROLLER_TYPE_MULTI_AXIS
  } else {
    bleGamepadConfig.setControllerType(CONTROLLER_TYPE_GAMEPAD);  // CONTROLLER_TYPE_JOYSTICK, CONTROLLER_TYPE_GAMEPAD (DEFAULT), CONTROLLER_TYPE_MULTI_AXIS
  }
  LOGI("INIT MODE done");
  bleGamepadConfig.setHatSwitchCount(1);
  bleGamepadConfig.setIncludeZAxis(false);
  bleGamepadConfig.setIncludeRxAxis(false);
  bleGamepadConfig.setIncludeRyAxis(false);
  bleGamepadConfig.setIncludeRzAxis(false);
  bleGamepadConfig.setIncludeSlider1(false);
  bleGamepadConfig.setIncludeSlider2(false);
  bleGamepadConfig.setIncludeRudder(false);
  bleGamepadConfig.setIncludeThrottle(false);
  bleGamepadConfig.setIncludeAccelerator(false);
  bleGamepadConfig.setIncludeBrake(false);
  bleGamepadConfig.setIncludeSteering(false);
  //bleGamepadConfig.setVid(0x045E);
  //bleGamepadConfig.setPid(0x02FD);//  Xbox One S Controller [Bluetooth]
  //bleGamepadConfig.setPid(0x028E);  // Xbox 360  Controller
  //bleGamepadConfig.setPid(0x02d1);  // Xbox one  Controller

  //bleGamepadConfig.setAxesMin(AXES_MAX);                         // 0 --> int16_t - 16 bit signed integer - Can be in decimal or hexadecimal
  //bleGamepadConfig.setAxesMax(AXES_MIN);                          // 32767 --> int16_t - 16 bit signed integer - Can be in decimal or hexadecimal

  LOGI("INIT SpecialButtons...");
  bleGamepadConfig.setWhichSpecialButtons(true, true, true, true, true, true, true, true);
  bleGamepadConfig.setButtonCount(9);
  LOGI("INIT SpecialButtons done");

  bleGamepadConfig.setAutoReport(false);
  LOGI("ble begin...");
  bleGamepad.begin(&bleGamepadConfig);

  LOGI("WAITING FOR CONNECTION...");
  // changing bleGamepadConfig after the begin function has no effect, unless you call the begin function again
}



void loop() {
  if (bleGamepad.isConnected()) {
    if (firstConnected) {
      firstConnected = false;
      pinMode(LED_BUILTIN, INPUT_PULLUP);
      LOGI("CONNECTED.");
      bleGamepad.setZ(AXES_CENTER);
    }
    // scan and debounce
    for (byte i = 0; i < numOfButtons; i++) {
      debouncers[i].update();
      if (debouncers[i].fell()) {
        buttonStatus[i] = 1;
      } else if (debouncers[i].rose()) {
        buttonStatus[i] = 0;
      }
    }
    sendReport = false;
    for (byte i = 0; i < numOfButtons; i++) {
      // key changed
      if (lastButtonStatus[i] != buttonStatus[i]) {
        sendReport = true;
        // copy value
        lastButtonStatus[i] = buttonStatus[i];
        if (i > 3) {
          // buttons: X,Y, A,B
          if (buttonStatus[i] == 1) {
            bleGamepad.press(physicalButtons[i]);
          } else {
            bleGamepad.release(physicalButtons[i]);
          }
        }
      }
    }

    if (sendReport) {
      if (isJoystickMode()) {
        // axes:
        if (buttonStatus[0] == 0 && buttonStatus[1] == 0) {
          currY = AXES_CENTER;
        }
        if (buttonStatus[2] == 0 && buttonStatus[3] == 0) {
          currX = AXES_CENTER;
        }
        if (buttonStatus[0] == 1 && buttonStatus[1] == 0) {
          currY = AXES_MIN;
        }
        if (buttonStatus[0] == 0 && buttonStatus[1] == 1) {
          currY = AXES_MAX;
        }
        if (buttonStatus[2] == 1 && buttonStatus[3] == 0) {
          currX = AXES_MIN;
        }
        if (buttonStatus[2] == 0 && buttonStatus[3] == 1) {
          currX = AXES_MAX;
        }
        bleGamepad.setLeftThumb(currX, currY);
      } else {
        // axes:
        if (buttonStatus[0] == 0 && buttonStatus[1] == 0 && buttonStatus[2] == 0 && buttonStatus[3] == 0) {
          currDPAD = HAT_CENTERED;
        }
        if (buttonStatus[0] == 1 && buttonStatus[1] == 0) {
          currDPAD = HAT_UP;
        }
        if (buttonStatus[0] == 0 && buttonStatus[1] == 1) {
          currDPAD = HAT_DOWN;
        }
        if (buttonStatus[2] == 1 && buttonStatus[3] == 0) {
          currDPAD = HAT_LEFT;
        }
        if (buttonStatus[2] == 0 && buttonStatus[3] == 1) {
          currDPAD = HAT_RIGHT;
        }
        if (buttonStatus[0] == 1 && buttonStatus[2] == 1) {
          currDPAD = HAT_UP_LEFT;
        }
        if (buttonStatus[0] == 1 && buttonStatus[3] == 1) {
          currDPAD = HAT_UP_RIGHT;
        }
        if (buttonStatus[1] == 1 && buttonStatus[2] == 1) {
          currDPAD = HAT_DOWN_LEFT;
        }
        if (buttonStatus[1] == 1 && buttonStatus[3] == 1) {
          currDPAD = HAT_DOWN_RIGHT;
        }
        bleGamepad.setHat1(currDPAD);
      }
    }

    if (keypad.getKeys()) {
      for (int i = 0; i < LIST_MAX; i++)  // Scan the whole key list.
      {
        if (keypad.key[i].stateChanged) {
          sendReport = true;
          key = keypad.key[i].kchar;
          switch (keypad.key[i].kstate) {
            case PRESSED:
              if (key < 60) {
                bleGamepad.press(key - 40);
              } else if (key == 'h') {
                bleGamepad.pressHome();
              } else if (key == 's') {
                bleGamepad.pressSelect();
              } else if (key == 'S') {
                bleGamepad.pressStart();
              } else if (key == 'b') {
                bleGamepad.pressBack();
              } else if (key == 'd') {
                bleGamepad.pressVolumeDec();
              } else if (key == 'i') {
                bleGamepad.pressVolumeInc();
              } else if (key == 'm') {
                bleGamepad.pressVolumeMute();
              }
              break;

            case RELEASED:
              if (key < 60) {
                bleGamepad.release(key - 40);
              } else if (key == 'h') {
                bleGamepad.releaseHome();
              } else if (key == 's') {
                bleGamepad.releaseSelect();
              } else if (key == 'S') {
                bleGamepad.releaseStart();
              } else if (key == 'b') {
                bleGamepad.releaseBack();
              } else if (key == 'd') {
                bleGamepad.releaseVolumeDec();
              } else if (key == 'i') {
                bleGamepad.releaseVolumeInc();
              } else if (key == 'm') {
                bleGamepad.releaseVolumeMute();
              }
              break;
          }
        }
      }
    }

    if (sendReport) {
      lastReportTime = millis();
      bleGamepad.sendReport();
    }
  } else {
    delay(20);
    blink();
    firstConnected = true;
  }
  checkToSleep();
  //**/
  //delay(5);  // (Un)comment to remove/add delay between loops
}

void checkToSleep() {
  if ((millis() - lastReportTime) > 1000 * 60 * 5) {
    LOGI("idleTime=%d", millis() - lastReportTime);
    LOGI("SLEEPING...");
    lastReportTime = millis();
    esp_deep_sleep_start();
  }
}

void blink() {

  if (millis() - lastblink >= 1000) {
    pinMode(LED_BUILTIN, OUTPUT);
    lastblink = millis();
    ledon = !ledon;
    digitalWrite(LED_BUILTIN, ledon);
  }
}

void boot_check_mode() {
  pinMode(BOOT_MODE_PIN, INPUT_PULLUP);
  int buttonState = digitalRead(BOOT_MODE_PIN);
  if (buttonState == LOW) {
    bootmode = BOOT_MODE_DPAD;
    LOGI("BOOT_MODE_DPAD");
  } else {
    LOGI("BOOT_MODE_JOYSTICK");
  }
}

bool isJoystickMode() {
  return bootmode == BOOT_MODE_JOYSTICK;
}

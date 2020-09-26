/* 
 *                    Joywheel firmware for Teensy4
 * 
 * To compile this file you need Arduino 1.8.1 or later with the Teensyduino package installed
 * in the 'tools' menu, select "Teensy 4" and "USB type Keyboard + mouse + joystick"
 */

#include "USBHost_t36.h"
#include <EEPROM.h>
#include <Bounce2.h>


/* === CONSTS === */
#define TRUE 1
#define FALSE 0
#define BUTTON_PRESSED 0
#define BUTTON_RELEASED 1

#define USE_Y_MOUSE

#define SERIAL_DBG Serial5
//#define USE_EEPROM
#define EEPROM_BASE_ADDRESS 0

// settings
#ifdef USE_Y_MOUSE
#define RESOLUTION (500 + (resMultiplyer * 2))
#else
#define RESOLUTION (3 + (resMultiplyer / 100))
#endif
#define POLL_FREQ 1

#define KEYPRESS_LENGTH 5

#define DEBUG_RATE 1

// number of different key mappings
#define NB_MAPS 2

#define NB_BUTTONS 3

#define DEFAULT_MAP_SETTING 0
#define DEFAULT_RES_SETTING 1
#define DEFAULT_KEYLEN_SETTING 2

// indexes for each setting value
enum SETTING_LIST
{
	SETTING_NONE = 0x00,
	SETTING_RES = 0x01,
	SETTING_KEYLEN = 0x02,
	SETTING_MAP = 0x04,
};


/* === PINS DEFINITION === */

#define ledDoNotUse 13
// PS2
#define MDATA 10
#define MCLK  9

#define BUTTON_A_PIN 23
#define BUTTON_B_PIN 19
#define BUTTON_C_PIN 17

#define LIGHT_A_PIN 4
#define LIGHT_B_PIN 6
#define LIGHT_C_PIN 8

#define SWITCH1_PIN 2
#define SWITCH2_PIN 3  // unplugged
#define SWITCH3_PIN 5  // unplugged

#define POT_PIN 15

// GLOBALS FROM SPINBOTAGE 1
int settingArray[8] = {0, 0, 0, 0, 0 ,0 ,0 ,0};

// TODO check left&right arrows
uint16_t keymapLeft[NB_MAPS] = {'d', KEY_LEFT};
uint16_t keymapRight[NB_MAPS] = {'f', KEY_RIGHT};
uint16_t keymapButton[NB_BUTTONS][NB_MAPS] = {{'y', KEY_UP}, {'n', KEY_DOWN}, {'a', KEY_ENTER}};
int currentMap = 0;
int previousSwitches = 0;


char buttonPins[NB_BUTTONS] = {BUTTON_A_PIN, BUTTON_B_PIN, BUTTON_C_PIN};

int encoder0Pos = 0;

// resolution multiplayer for the encoder, controlled by a switch
int resMultiplyer = 1;
// multiply the length of a key press. controlled by a switch
int keyLengthMultiplyer = 2;

// the last "rotational" pressed key
char keyCode;
// used to record the duration of the current "rotary" key press
int keyJustPressed;
// target duration, key will be released after that
int keyPressDuration;
// we have a separate variable for each button
int buttonJustPressed[NB_BUTTONS] = {0, 0, 0};


USBHost myusb;
USBHIDParser hid1(myusb);
MouseController mouse1(myusb);
Bounce debouncers[NB_BUTTONS];

int nb_read = 0;
int sum_x = 0;
int sum_y = 0;



// mapping between button pins and led pins
int buttonLed(int index)
{
	switch(index) {
	case BUTTON_A_PIN:
		return LIGHT_A_PIN;
	case BUTTON_B_PIN:
		return LIGHT_B_PIN;
	case BUTTON_C_PIN:
		return LIGHT_C_PIN;
	default:
		SERIAL_DBG.print("Unknown button ");SERIAL_DBG.println("index");
		// in case of error, we switch the default led
		return ledDoNotUse;
	};
}


void setup()
{	
	// Attach the debouncer to a pin with INPUT_PULLUP mode
	// Use a debounce interval of 25 milliseconds
	debouncers[0].attach(BUTTON_A_PIN,INPUT_PULLUP); 
	debouncers[0].interval(25);
	debouncers[1].attach(BUTTON_B_PIN,INPUT_PULLUP); 
	debouncers[1].interval(25); 
	debouncers[2].attach(BUTTON_C_PIN,INPUT_PULLUP); 
	debouncers[2].interval(25); 
	
	pinMode (SWITCH1_PIN, INPUT);
	pinMode (SWITCH2_PIN, INPUT);
	pinMode (SWITCH3_PIN, INPUT);

	pinMode (LIGHT_A_PIN, OUTPUT);
	pinMode (LIGHT_B_PIN, OUTPUT);
	pinMode (LIGHT_C_PIN, OUTPUT);

	digitalWrite(LIGHT_A_PIN, LOW);
	digitalWrite(LIGHT_B_PIN, LOW);
	digitalWrite(LIGHT_C_PIN, LOW);
	
#ifdef USE_EEPROM
	int val;
	SERIAL_DBG.println("EEPROM");
	for(int i = 0 ; i < 8 ; i++) {
		EEPROM.get( EEPROM_BASE_ADDRESS + i * sizeof(int), val );
		settingArray[i] = val&0x7;
		SERIAL_DBG.println(val);
	}
#else
	settingArray[SETTING_RES] = DEFAULT_RES_SETTING;
	settingArray[SETTING_KEYLEN] = DEFAULT_KEYLEN_SETTING;
	settingArray[SETTING_MAP] = DEFAULT_MAP_SETTING;
#endif
  SERIAL_DBG.begin(115200);
  while (!SERIAL_DBG) ; // wait for Arduino Serial Monitor
  SERIAL_DBG.println("\n\nUSB Enumeration start");
  myusb.begin();

  myusb.Task();
  const uint8_t *psz = ((USBDriver *)(&mouse1))->manufacturer();

  // we wait until the mouse is detected
  while ((psz && *psz) == false) {
      psz = ((USBDriver *)(&mouse1))->manufacturer();
  };


  psz = ((USBDriver *)(&mouse1))->manufacturer();
  if (psz && *psz) SERIAL_DBG.printf("  manufacturer: %s\n", psz);
  psz = ((USBDriver *)(&mouse1))->product();
  if (psz && *psz) SERIAL_DBG.printf("  product: %s\n", psz);
  psz = ((USBDriver *)(&mouse1))->serialNumber();
  if (psz && *psz) SERIAL_DBG.printf("  Serial: %s\n", psz);

}


void loop()
{
  myusb.Task();
  
  resMultiplyer = analogRead(POT_PIN);

#ifdef DEBUG_MODE // mode that only print mouse coords
  if (mouse1.available()) {
	  int val_x = mouse1.getMouseX();
	  int val_y = mouse1.getMouseY();

	  sum_x += val_x;
	  sum_y += val_y;
	  mouse1.mouseDataClear();
	  nb_read++;

	  if (nb_read%DEBUG_RATE == 0) {
		  SERIAL_DBG.print("AVG mouseX = ");
		  SERIAL_DBG.print(sum_x / DEBUG_RATE);
		  SERIAL_DBG.print(",  mouseY = ");
		  SERIAL_DBG.print(sum_y / DEBUG_RATE);
          SERIAL_DBG.print("\tencoder0Pos = ");
          SERIAL_DBG.print(encoder0Pos);
		  SERIAL_DBG.println();
		  sum_x = 0;
		  sum_y = 0;
	  }
  }
#else // regular mode that send HID events
  if (mouse1.available()) {

#ifdef USE_Y_MOUSE
      int val_y = mouse1.getMouseY();
	  if (abs(val_y) < 10)
		  val_y = 0;
	  encoder0Pos += val_y;
#else
      int val_x = mouse1.getMouseX();
	  encoder0Pos += val_x;
#endif

	  if (encoder0Pos <= -RESOLUTION) {
		  encoder0Pos += RESOLUTION;
		  // should we test for keyJustPressed in the condition? Here we reset it and make a longer keypress
		  keyJustPressed = 1;
		  keyCode = keymapLeft[currentMap];
		  //Keyboard.press(keyCode);
          usb_keyboard_press_keycode(keyCode);
		  Serial.println("LEFT");
	  }
	  if (encoder0Pos >= RESOLUTION) {
		  encoder0Pos -= RESOLUTION;
		  keyJustPressed = 1;
		  keyCode = keymapRight[currentMap];
		  //Keyboard.press(keyCode);
          usb_keyboard_press_keycode(keyCode);
		  Serial.println("RIGHT");
	  }

	  delay(20);  /* twiddle */
	  if (keyJustPressed == 1)
		  //Keyboard.release(keyCode);
          usb_keyboard_release_keycode(keyCode);
  }
#endif

  for (int btn = 0 ; btn < NB_BUTTONS ; btn++) {

	  // Update the Bounce instance
	  debouncers[btn].update(); 
	  
	  // we release the key when the user releases the button
	  if (debouncers[btn].rose()) {
		  buttonJustPressed[btn] = 0;
		  digitalWrite(buttonLed(buttonPins[btn]), LOW);
          usb_keyboard_release_keycode(keymapButton[btn][currentMap]);
	  }

	  if (debouncers[btn].fell()) {
		  buttonJustPressed[btn] = 1;
		  digitalWrite(buttonLed(buttonPins[btn]), HIGH);
		  usb_keyboard_press_keycode(keymapButton[btn][currentMap]);
	  }
  }

}



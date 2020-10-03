/*
 *                    Joywheel firmware for Teensy4
 *
 * To compile this file you need Arduino 1.8.1 or later with the Teensyduino package installed
 * in the 'tools' menu, select "Teensy 4" and "USB type Keyboard + mouse + joystick"
 */

#include "USBHost_t36.h"
#include <Bounce2.h>


/* === CONSTS === */
#define TRUE 1
#define FALSE 0
#define BUTTON_PRESSED 0
#define BUTTON_RELEASED 1

//#define USE_Y_MOUSE

#define SERIAL_DBG Serial5

// settings
#define POLL_FREQ 1

#define KEYPRESS_LENGTH 20

#define DEBUG_RATE 1

// number of different key mappings
#define NB_MAPS 4

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

enum HidType {
    HID_KEYBOARD,
    HID_JOYSTICK
};

typedef enum {
    BUTTON,
    X_AXIS,
    Y_AXIS,
    Z_AXIS,
    NA_EVENT
} JoyEvent;

typedef struct _HidEvent{
    HidType type;
    JoyEvent event;
    uint16_t value;
} HidEvent;

/* === PINS DEFINITION === */

#define ledDoNotUse 13

#define BUTTON_A_PIN 23
#define BUTTON_B_PIN 19
#define BUTTON_C_PIN 17

#define LIGHT_A_PIN 4
#define LIGHT_B_PIN 6
#define LIGHT_C_PIN 8

#define SWITCH1_PIN 2
#define SWITCH2_PIN 3
#define SWITCH3_PIN 5

#define POT_PIN 15

HidEvent keymapLeft[NB_MAPS] = {{HID_KEYBOARD, NA_EVENT, 'd'}, {HID_JOYSTICK, X_AXIS, 1023},
 {HID_KEYBOARD, NA_EVENT, KEY_LEFT}, {HID_KEYBOARD, NA_EVENT, KEY_UP}};
HidEvent keymapRight[NB_MAPS] = {{HID_KEYBOARD, NA_EVENT,'f'}, {HID_JOYSTICK, X_AXIS, 0},
 {HID_KEYBOARD, NA_EVENT, KEY_RIGHT}, {HID_KEYBOARD, NA_EVENT, KEY_DOWN}};
HidEvent keymapButton[NB_BUTTONS][NB_MAPS] = {\
    {{HID_KEYBOARD, NA_EVENT,'y'}, {HID_JOYSTICK, BUTTON, 1}, {HID_KEYBOARD, NA_EVENT, KEY_UP}, {HID_KEYBOARD, NA_EVENT, KEY_LEFT}},
    {{HID_KEYBOARD, NA_EVENT,'n'}, {HID_JOYSTICK, Y_AXIS, 0}, {HID_KEYBOARD, NA_EVENT, KEY_DOWN}, {HID_KEYBOARD, NA_EVENT, KEY_RIGHT}},
    {{HID_KEYBOARD, NA_EVENT,'a'}, {HID_JOYSTICK, Y_AXIS, 1023},{HID_KEYBOARD, NA_EVENT, KEY_ENTER}, {HID_KEYBOARD, NA_EVENT, KEY_ENTER}} };
int currentMap = 0;

char buttonPins[NB_BUTTONS] = {BUTTON_A_PIN, BUTTON_B_PIN, BUTTON_C_PIN};

int encoder0Pos = 0;

// resolution multiplayer for the encoder, controlled by a potentiometer
int resMultiplyer = 1;

// the last "rotational" pressed key
HidEvent keyCode;
// used to record the duration of the current "rotary" key press
int keyJustPressed;
// we have a separate variable for each button
int buttonJustPressed[NB_BUTTONS] = {0, 0, 0};


USBHost myusb;
USBHIDParser hid1(myusb);
MouseController mouse1(myusb);
Bounce debouncers[NB_BUTTONS];

int nb_read = 0;
int sum_x = 0;
int sum_y = 0;


void sendHidEvent(HidEvent event)
{
    switch(event.type) {
    case HID_KEYBOARD:
        usb_keyboard_press_keycode(event.value);
        break;
    case HID_JOYSTICK:
    {
        switch(event.event) {
        case X_AXIS:
            SERIAL_DBG.print("X AXIS ");
            SERIAL_DBG.println(event.value);
            Joystick.X(event.value);
            break;
        case Y_AXIS:
            Joystick.Y(event.value);
            break;
        case Z_AXIS:
            Joystick.Z(event.value);
            break;
        case BUTTON:
            SERIAL_DBG.println("BUTTON");
            if (event.value <= 32)
                Joystick.button(event.value, 1);
            break;
        default: //unsupported case
            SERIAL_DBG.println("unsupported event.event");
        };
        break;
    }
    default: //unsupported case
        SERIAL_DBG.println("unsupported event.type");
    };
}

void releaseHidEvent(HidEvent event)
{
    switch(event.type) {
    case HID_KEYBOARD:
        usb_keyboard_release_keycode(event.value);
        break;
    case HID_JOYSTICK:
    {
        switch(event.event) {
        case X_AXIS:
            SERIAL_DBG.println("X AXIS CENTER");
            Joystick.X(512);
            break;
        case Y_AXIS:
            Joystick.Y(512);
            break;
        case Z_AXIS:
            Joystick.Z(512);
            break;
        case BUTTON:
            SERIAL_DBG.println("BUTTON RELEASE");
            if (event.value <= 32)
                Joystick.button(event.value, 0);
            break;
        default:
            //unsupported case
            SERIAL_DBG.println("unsupported event.event");
        }
        break;
    };
    default:
        // unsupported case
        SERIAL_DBG.println("unsupported event.type");
    };
}

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

    pinMode (SWITCH1_PIN, INPUT_PULLDOWN);
    pinMode (SWITCH2_PIN, INPUT_PULLDOWN);
    pinMode (SWITCH3_PIN, INPUT_PULLDOWN);

    pinMode (LIGHT_A_PIN, OUTPUT);
    pinMode (LIGHT_B_PIN, OUTPUT);
    pinMode (LIGHT_C_PIN, OUTPUT);

    digitalWrite(LIGHT_A_PIN, LOW);
    digitalWrite(LIGHT_B_PIN, LOW);
    digitalWrite(LIGHT_C_PIN, LOW);

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

    // settings for the Joystick HID
    Joystick.useManualSend(false);
    Joystick.X(512);
    Joystick.Y(512);
    Joystick.Z(512);
    Joystick.Zrotate(512);
    Joystick.sliderLeft(512);
    Joystick.sliderRight(512);
    Joystick.slider(512);
    Joystick.button(1, 0);
}


void loop()
{
    myusb.Task();

    resMultiplyer = analogRead(POT_PIN);

#ifdef USE_Y_MOUSE
    int resolution = (100 + (resMultiplyer * 4));
#else
    int resolution = (3 + (resMultiplyer / 100));
#endif

    int temp_switches = digitalRead(SWITCH1_PIN) + digitalRead(SWITCH2_PIN) * 2
        + digitalRead(SWITCH3_PIN) * 4;

    temp_switches = min(temp_switches, NB_MAPS);
    if (currentMap != temp_switches) {
        currentMap = temp_switches;
        SERIAL_DBG.print("new switches val = ");
        SERIAL_DBG.println(currentMap);
    }

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
            SERIAL_DBG.println();
            sum_x = 0;
            sum_y = 0;
        }
    }
#else // regular mode that send HID events
    if (mouse1.available()) {

#ifdef USE_Y_MOUSE
        int val_x = mouse1.getMouseX();
        int val_y = mouse1.getMouseY();

        if (abs(val_y) < 10)
            val_y = 0;
        encoder0Pos += val_y;
        // handle saturation in heuristic way :
        // if mouse_y reaches its maximum, we take the abs of the x value,
        // no more than 16 to avoid glitches, and abs() because it is not reliable
        // and we multiply it by the mouse_y value, so we don't care about the sign.
        // and we divide it by 16, so that y saturation AND x = 16 means double the Y value
        if (abs(val_y) == 127)
            encoder0Pos += val_y * max(abs(val_x), 16) / 16;
#else
        int val_x = mouse1.getMouseX();
        int val_y = mouse1.getMouseY();

        // we only take X into account if Y is high, otherwise we consider it is garbage
        // and we only take X into account if its sign is coherent with Y sign
        if ( (val_y > 100 && val_x < 0) || (val_y < -100 && val_x > 0) )
            encoder0Pos += val_x;
#endif
        mouse1.mouseDataClear();

        if (encoder0Pos <= -resolution) {
            encoder0Pos += resolution;
            // should we test for keyJustPressed in the condition? Here we reset it and make a longer keypress
            keyJustPressed = 1;
            keyCode = keymapLeft[currentMap];
            sendHidEvent(keyCode);
            Serial.println("LEFT");
        }
        if (encoder0Pos >= resolution) {
            encoder0Pos -= resolution;
            keyJustPressed = 1;
            keyCode = keymapRight[currentMap];
            sendHidEvent(keyCode);
            Serial.println("RIGHT");
        }

        delay(KEYPRESS_LENGTH);
        if (keyJustPressed == 1) {
            releaseHidEvent(keyCode);
            keyJustPressed = 0;
        }
    }
#endif

    for (int btn = 0 ; btn < NB_BUTTONS ; btn++) {
        // Update the Bounce instance
        debouncers[btn].update();

        // we release the key when the user releases the button
        if (debouncers[btn].rose()) {
            buttonJustPressed[btn] = 0;
            digitalWrite(buttonLed(buttonPins[btn]), LOW);
            //usb_keyboard_release_keycode(keymapButton[btn][currentMap]);
            releaseHidEvent(keymapButton[btn][currentMap]);
        }

        if (debouncers[btn].fell()) {
            buttonJustPressed[btn] = 1;
            digitalWrite(buttonLed(buttonPins[btn]), HIGH);
            //usb_keyboard_press_keycode(keymapButton[btn][currentMap]);
            sendHidEvent(keymapButton[btn][currentMap]);
        }
    }
}

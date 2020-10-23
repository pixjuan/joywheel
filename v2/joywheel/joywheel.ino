/*
 *                    Joywheel firmware for Teensy4
 *
 * To compile this file you need Arduino 1.8.1 or later with the Teensyduino package installed
 * in the 'tools' menu, select "Teensy 4" and "USB type Keyboard + mouse + joystick"
 */

#include "USBHost_t36.h"
#include <Bounce2.h>


// DEBUG mode just output to serial ports without sending actual HID events.
//#define DEBUG_MODE

// FAKE serial mode doesn't send any serial data, useful to avoid slow down.
//#define FAKE_SERIAL

// enable REPEAT_WHEEL_EVENTS if you want continous wheel movements to generate multiple HID events.
//#define REPEAT_WHEEL_EVENTS

#ifdef FAKE_SERIAL
typedef struct {
    void print(const char* s) {};
    void print(int i) {};
    void println(const char* s) {};
    void println(int i) {};
    void printf( char const* format, ...) {};
} _FAKE_SERIAL ;
_FAKE_SERIAL SERIAL_DBG;
#else
#define SERIAL_DBG Serial5
#endif

// how long a key is pressed when the wheel is turned
#define KEYPRESS_LENGTH 200
#define DEBUG_RATE 1

// number of different key mappings
#define NB_MAPS 4

// number of buttons and pedals on the Joystick
#define NB_BUTTONS 4

// how long to wait between 2 HID events repetition
#define COOLDOWN_TIME 1

enum HidType {
    HID_KEYBOARD,
    HID_JOYSTICK,
    HID_NONE
};

enum EventRepetition {
    NO_EVENT = 0,
    EVENT_SENT = 1,
    EVENT_REPEAT = 2, // event needs to be repeated
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

typedef struct _KeyMap{
    HidEvent left;
    HidEvent right;
    HidEvent buttons[NB_BUTTONS];
} KeyMap;


const HidEvent NO_KEY = { HID_NONE, NA_EVENT, 0};

/* === PINS DEFINITION === */

#define ledDoNotUse 13

#define BUTTON_A_PIN 23
#define BUTTON_B_PIN 19
#define BUTTON_C_PIN 17
#define PEDAL_PIN 0

#define LIGHT_A_PIN 4
#define LIGHT_B_PIN 6
#define LIGHT_C_PIN 8

#define SWITCH1_PIN 2
#define SWITCH2_PIN 3
#define SWITCH3_PIN 5

#define POT_PIN 15

const KeyMap maps[NB_MAPS] = {
    { //Sabotage map
        .left = {HID_KEYBOARD, NA_EVENT, 'd'},
        .right = {HID_KEYBOARD, NA_EVENT, 'f'},
        .buttons= {{HID_KEYBOARD, NA_EVENT,'y'},
                   {HID_KEYBOARD, NA_EVENT,'n'},
                   {HID_KEYBOARD, NA_EVENT,'a'},
                   {HID_KEYBOARD, NA_EVENT,'z'}},
    },
    { //SuperStardust map
        .left = {HID_JOYSTICK, X_AXIS, 0},
        .right = {HID_JOYSTICK, X_AXIS, 1023},
        .buttons= {{HID_JOYSTICK, BUTTON, 1},
                   {HID_JOYSTICK, Y_AXIS, 0},
               {HID_JOYSTICK, Y_AXIS, 1023},
                   {HID_JOYSTICK, Y_AXIS, 0}},
    },
    { // generic keyboard map #1
        .left = {HID_KEYBOARD, NA_EVENT, KEY_LEFT},
        .right = {HID_KEYBOARD, NA_EVENT, KEY_RIGHT},
        .buttons= {{HID_KEYBOARD, NA_EVENT, KEY_UP},
                   {HID_KEYBOARD, NA_EVENT, KEY_DOWN},
                   {HID_KEYBOARD, NA_EVENT, KEY_ENTER},
                   {HID_KEYBOARD, NA_EVENT, KEY_SPACE}},
    },
    { // generic keyboard map #2
        .left = {HID_KEYBOARD, NA_EVENT, KEY_UP},
        .right = {HID_KEYBOARD, NA_EVENT, KEY_DOWN},
        .buttons= {{HID_KEYBOARD, NA_EVENT, KEY_LEFT},
                   {HID_KEYBOARD, NA_EVENT, KEY_RIGHT},
                   {HID_KEYBOARD, NA_EVENT, KEY_ENTER},
                   {HID_KEYBOARD, NA_EVENT, KEY_SPACE}},
    }
};


int currentMap = 0;
int encoder0Pos = 0;

// if buttons have led, put the right pin for each button in this array
char buttonPins[NB_BUTTONS] = {BUTTON_A_PIN, BUTTON_B_PIN, BUTTON_C_PIN, ledDoNotUse};

// resolution multiplyer for the encoder, controlled by a potentiometer
int resMultiplyer = 1;

// the last "rotational" pressed key
HidEvent keyCode;
// used to record the duration of the current "rotary" key press
int keyJustPressed;
// we have a separate variable for each button
int buttonJustPressed[NB_BUTTONS] = {0, 0, 0, 0};

// value to keep track of the duration for each key press
int currentTime;
int lastWheelEvent;

USBHost myusb;
USBHIDParser hid1(myusb);
MouseController mouse1(myusb);
Bounce debouncers[NB_BUTTONS];


#ifdef DEBUG_MODE
int nb_read = 0;
int sum_x = 0;
int sum_y = 0;
#endif

void sendHidEvent(HidEvent event)
{
#ifdef DEBUG_MODE
    return;
#endif
    switch(event.type) {
    case HID_KEYBOARD:
        usb_keyboard_press_keycode(event.value);
        break;
    case HID_JOYSTICK:
    {
        switch(event.event) {
        case X_AXIS:
            SERIAL_DBG.printf("X AXIS %d\r\n",event.value);
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
        // the pedal doesn't have any light for example, we just switch the Teensy led instead
        return ledDoNotUse;
    };
}




void repeatWheelEvent(HidEvent event)
{
    if (event.type != HID_NONE) SERIAL_DBG.println("repeatWheelEvent");
    switch(keyJustPressed){
        case NO_EVENT:
            if (event.type != HID_NONE) {
                // send Hid event
                keyJustPressed = EVENT_SENT;
                keyCode = event;
                lastWheelEvent = millis();
                sendHidEvent(keyCode);
            }
            //if event is 0 we don't care
            break;
        case EVENT_SENT:
            if (event.type == HID_NONE) {
                //check timer to release key
                currentTime = millis();
                if ((currentTime - lastWheelEvent) > KEYPRESS_LENGTH) {
                    releaseHidEvent(keyCode);
                    keyJustPressed = NO_EVENT;
                }
            }  else  {
#if REPEAT_WHEEL_EVENTS
                // TODO check for the switch indicating "repeat" or "extend" behaviour
                if (event.event == keyCode.event && event.value == keyCode.value) {
                    releaseHidEvent(keyCode);
                    keyJustPressed = EVENT_REPEAT;
                    lastWheelEvent = millis();
                }
#else // continuous keypress/HID event
                if (event.event == keyCode.event && event.value == keyCode.value) {
                    // if the same key is we update 
                    lastWheelEvent = millis();
                }
#endif
                else {
                    // unlikely situation when the wheel was quickly moved the other side
                    // we release the current keycode press and press the new one
                    // we don't change the current state and stay in EVENT_SENT
                    SERIAL_DBG.println("unlikely: opposite wheel event");
                    releaseHidEvent(keyCode);
                    keyCode = event;
                    sendHidEvent(keyCode);
                }

            }
            break;
        case EVENT_REPEAT:
            if (event.type == HID_NONE) {
                currentTime = millis();
                if ((currentTime - lastWheelEvent) > COOLDOWN_TIME) {
                    sendHidEvent(keyCode);
                    lastWheelEvent = millis();
                    keyJustPressed = EVENT_SENT;
                }
                    
            } else {
                SERIAL_DBG.println("This shouldn't happen.");
            }
            break;
        default:
            SERIAL_DBG.printf("unkown state : %d\r\n", keyJustPressed);
            };
}

#ifdef DEBUG_MODE
void printMouse(int x, int y, int encoder)
{
    sum_x += x;
    sum_y += y;
    nb_read++;

    if (nb_read%DEBUG_RATE == 0) {
        SERIAL_DBG.printf("AVG mouseX = %d \tmouseY = %d \tencoder = %d\r\n",
                          sum_x / DEBUG_RATE, sum_y / DEBUG_RATE, encoder);
        sum_x = 0;
        sum_y = 0;
    }
}
#endif

// USE_Y_MOUSE
int MouseToEncoder1()
{
    int resolution = (100 + (resMultiplyer * 4));
    int val_x = mouse1.getMouseX();
    int val_y = mouse1.getMouseY();

    // we ignore small moves
    if (abs(val_y) < 10)
        val_y = 0;
    
    encoder0Pos += val_y;
    // handle saturation in heuristic way :
    // if mouse_y reaches its maximum, we take the abs of the x value,
    // no more than 16 to avoid glitches, and abs() because it is not reliable
    // and we multiply it by the mouse_y value, so we don't care about the sign.
    // and we divide it by 16, so that y saturation AND x = 16 means double the Y value
    if (abs(val_y) > 120)
        encoder0Pos += val_y * max(abs(val_x), 16) / 16;

#ifdef DEBUG_MODE
    printMouse(val_x, val_y, encoder0Pos);
#endif
    
    if (encoder0Pos <= -resolution) {
        encoder0Pos += resolution;
        return 1;
    }
    else if (encoder0Pos >=  resolution) {
        encoder0Pos -= resolution;
        return -1;
    }
    else {
        return 0;
    }
}

int MouseToEncoder2()
{
    int resolution = (3 + (resMultiplyer / 100));
    int val_x = mouse1.getMouseX();
    int val_y = mouse1.getMouseY();

    // we only take X into account if Y is high, otherwise we consider it is garbage
    // and we only take X into account if its sign is coherent with Y sign
    if ( (val_y > 100 && val_x < 0) || (val_y < -100 && val_x > 0) )
        encoder0Pos += val_x;

#ifdef DEBUG_MODE
    printMouse(val_x, val_y, encoder0Pos);
#endif
    
    if (encoder0Pos <= -resolution) {
        encoder0Pos += resolution;   
        return -1;
    }
    else if (encoder0Pos >=  resolution) {
        encoder0Pos -= resolution;
        return 1;
    }
    else
        return 0;
}

void setup()
{
    // Attach the debouncer to a pin with INPUT_PULLUP mode
    // Use a debounce interval of 25 milliseconds
    debouncers[0].attach(BUTTON_A_PIN, INPUT_PULLUP);
    debouncers[1].attach(BUTTON_B_PIN, INPUT_PULLUP);

    debouncers[2].attach(BUTTON_C_PIN, INPUT_PULLUP);
    debouncers[3].attach(PEDAL_PIN, INPUT_PULLUP);
    for (int i = 0; i < NB_BUTTONS ; i++) {debouncers[i].interval(25);}
    
    pinMode (SWITCH1_PIN, INPUT_PULLDOWN);
    pinMode (SWITCH2_PIN, INPUT_PULLDOWN);
    pinMode (SWITCH3_PIN, INPUT_PULLDOWN);

    pinMode (LIGHT_A_PIN, OUTPUT);
    pinMode (LIGHT_B_PIN, OUTPUT);
    pinMode (LIGHT_C_PIN, OUTPUT);

    digitalWrite(LIGHT_A_PIN, LOW);
    digitalWrite(LIGHT_B_PIN, LOW);
    digitalWrite(LIGHT_C_PIN, LOW);

#ifndef FAKE_SERIAL
    SERIAL_DBG.begin(115200);
    while (!SERIAL_DBG) ; // wait for Arduino Serial Monitor
    SERIAL_DBG.println("\r\n\nUSB Enumeration start");
#endif
    myusb.begin();

    myusb.Task();
    const uint8_t *psz = ((USBDriver *)(&mouse1))->manufacturer();

    // we wait until the mouse is detected
    while ((psz && *psz) == false) {
        psz = ((USBDriver *)(&mouse1))->manufacturer();
    };

    psz = ((USBDriver *)(&mouse1))->manufacturer();
    if (psz && *psz) SERIAL_DBG.printf("  manufacturer: %s\r\n", psz);
    psz = ((USBDriver *)(&mouse1))->product();
    if (psz && *psz) SERIAL_DBG.printf("  product: %s\r\n", psz);
    psz = ((USBDriver *)(&mouse1))->serialNumber();
    if (psz && *psz) SERIAL_DBG.printf("  Serial: %s\r\n", psz);

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

    lastWheelEvent = currentTime = millis();
}


void loop()
{
    myusb.Task();

    int val = analogRead(POT_PIN);
    if ( abs(val - resMultiplyer) > 10 ) {
        resMultiplyer = val;
        SERIAL_DBG.printf("resMultiplyer = %d\r\n", resMultiplyer);
    }
    
    int temp_switches = digitalRead(SWITCH1_PIN) + digitalRead(SWITCH2_PIN) * 2
        + digitalRead(SWITCH3_PIN) * 4;

    temp_switches = min(temp_switches, NB_MAPS);
    if (currentMap != temp_switches) {
        currentMap = temp_switches;
        SERIAL_DBG.printf("new switches val = %d\r\n", currentMap);
    }

    if (mouse1.available()) {
        int move = MouseToEncoder1();
        //int move = MouseToEncoder2();
        
        mouse1.mouseDataClear();

        // after reading the mouse sensor, we get the current time
        currentTime = millis();
        
        if (move == -1) {
            SERIAL_DBG.print(encoder0Pos);

            if (keyJustPressed == NO_EVENT) {
                SERIAL_DBG.println(" LEFT");
            }
            repeatWheelEvent(maps[currentMap].left);
        }
        if (move == 1) {
            SERIAL_DBG.print(encoder0Pos);
            
            if (keyJustPressed == NO_EVENT) {
                SERIAL_DBG.println(" RIGHT");
            }
            repeatWheelEvent(maps[currentMap].right);
        }

        // repeatWheelEvent will release the key/joystick if needed
        repeatWheelEvent(NO_KEY);
    } else {
        // even if there is no mouse activity, we need to release the event after KEYPRESS_LENGTH ms
        repeatWheelEvent(NO_KEY);
    }


    for (int btn = 0 ; btn < NB_BUTTONS ; btn++) {
        // Update the Bounce instance
        debouncers[btn].update();

        // we release the key when the user releases the button
        if (debouncers[btn].rose()) {
            buttonJustPressed[btn] = 0;
            digitalWrite(buttonLed(buttonPins[btn]), LOW);
            releaseHidEvent(maps[currentMap].buttons[btn]);
        }

        if (debouncers[btn].fell()) {
            buttonJustPressed[btn] = 1;
            digitalWrite(buttonLed(buttonPins[btn]), HIGH);
            sendHidEvent(maps[currentMap].buttons[btn]);
        }
    }
}

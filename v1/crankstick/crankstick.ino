#include <Keyboard.h>
#include <EEPROM.h>

/*
 * To compile this file you need Arduino 1.8.1 or later
 * in the 'tools' menu, select "Teensy 3.2/3.1" and "USB type Keyboard + mouse + joystick"
 */


/* === Consts === */
#define TRUE 1
#define FALSE 0
#define BUTTON_PRESSED 0
#define BUTTON_RELEASED 1

/* === Features === */
#define ENABLE_SERIAL3
#define USE_EEPROM
#define EEPROM_BASE_ADDRESS 0

/* === Settings === */
#define RESOLUTION (3 * resMultiplyer)
#define POLL_FREQ 1
#define KEYPRESS_LENGTH 50
#define NB_MAPS 4 // number of different key mappings
#define NB_BUTTONS 4
#define DEFAULT_MAP_SETTING 0
#define DEFAULT_RES_SETTING 1
#define DEFAULT_KEYLEN_SETTING 2
#define SERIAL_DBG Serial3

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
    HID_JOYSTICK,
    HID_NONE
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



/* === Pins definition === */

//  we use serial port 3 so the serial pins are :  TX=7  RX=8

#define ledDoNotUse 13
#define MDATA 10  // PS2
#define MCLK  9   // PS2

#define BUTTON_A_PIN 14
#define BUTTON_B_PIN 15
#define BUTTON_C_PIN 16
#define PEDAL_PIN    20

#define LIGHT_A_PIN 0
#define LIGHT_B_PIN 1
#define LIGHT_C_PIN 2

#define SWITCH1_PIN 17
#define SWITCH2_PIN 18
#define SWITCH3_PIN 19

#define POT1_PIN 9
#define POT2_PIN 8

/* === Globals === */

const HidEvent NO_KEY = { HID_NONE, NA_EVENT, 0};

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

unsigned char currentMap = 0;
int previousSwitches = 0;

char buttonPins[NB_BUTTONS] = {BUTTON_A_PIN, BUTTON_B_PIN, BUTTON_C_PIN, PEDAL_PIN};

int encoder0Pos = 0;

// resolution multiplayer for the encoder (potentiometer-controlled)
int resMultiplyer = 2;
// multiply the length of a key press (potentiometer-controlled)
int keyLengthMultiplyer = 4;

// the last "rotational" pressed key
HidEvent keyCode;
// used to record the duration of the current "rotary" key press
int keyJustPressed;
// target duration, key will be released after that
int keyPressDuration;
// we have a separate variable for each button
int buttonJustPressed[NB_BUTTONS] = {0, 0, 0};

int lastWheelEvent;

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


/* === PS2 mouse functions from Arduino playground === */
void gohi(int pin)
{
    pinMode(pin, INPUT);
    digitalWrite(pin, HIGH);
}

void golo(int pin)
{
    pinMode(pin, OUTPUT);
    digitalWrite(pin, LOW);
}


char mouse_write(char data, char ack)
{
    char i;
    char parity = 1;

    /* put pins in output mode */
    gohi(MDATA);
    gohi(MCLK);
    delayMicroseconds(300);
    golo(MCLK);
    delayMicroseconds(300);
    golo(MDATA);
    delayMicroseconds(10);
    /* start bit */
    gohi(MCLK);
    /* wait for mouse to take control of clock); */
    while (digitalRead(MCLK) == HIGH)
        ;
    /* clock is low, and we are clear to send data */
    for (i=0; i < 8; i++) {
        if (data & 0x01) {
            gohi(MDATA);
        }
        else {
            golo(MDATA);
        }
        /* wait for clock cycle */
        while (digitalRead(MCLK) == LOW)
            ;
        while (digitalRead(MCLK) == HIGH)
            ;
        parity = parity ^ (data & 0x01);
        data = data >> 1;
    }
    /* parity */
    if (parity) {
        gohi(MDATA);
    }
    else {
        golo(MDATA);
    }
    while (digitalRead(MCLK) == LOW)
        ;
    while (digitalRead(MCLK) == HIGH)
        ;
    /* stop bit */
    gohi(MDATA);
    delayMicroseconds(50);
    while (digitalRead(MCLK) == HIGH)
        ;
    /* wait for mouse to switch modes */
    while ((digitalRead(MCLK) == LOW) || (digitalRead(MDATA) == LOW))
        ;
    /* put a hold on the incoming data. */
    golo(MCLK);
    //  Serial.print("done.\n");

    return ack?mouse_read():0;
}

/*
 * Get a byte of data from the mouse
 */
char mouse_read(void)
{
    char data = 0x00;
    int i;
    char bit = 0x01;

    //  Serial.print("reading byte from mouse\n");
    /* start the clock */
    gohi(MCLK);
    gohi(MDATA);
    delayMicroseconds(50);
    while (digitalRead(MCLK) == HIGH)
        ;
    delayMicroseconds(5);  /* not sure why */
    while (digitalRead(MCLK) == LOW) /* eat start bit */
        ;
    for (i=0; i < 8; i++) {
        while (digitalRead(MCLK) == HIGH) ;
        if (digitalRead(MDATA) == HIGH) {
            data = data | bit;
        }
        while (digitalRead(MCLK) == LOW) ;
        bit = bit << 1;
    }
    /* eat parity bit, which we ignore */
    while (digitalRead(MCLK) == HIGH) ;
    while (digitalRead(MCLK) == LOW) ;
    /* eat stop bit */
    while (digitalRead(MCLK) == HIGH) ;
    while (digitalRead(MCLK) == LOW) ;
    /* put a hold on the incoming data. */
    golo(MCLK);
    return data;
}

void mouse_init()
{
    gohi(MCLK);
    gohi(MDATA);
    //  Serial.print("Sending reset to mouse\n");
    mouse_write(0xff, TRUE);
    //  Serial.print("Read ack byte1\n");
    mouse_read();  /* blank */
    mouse_read();  /* blank */
    //  Serial.print("Setting sample rate 200\n");
    mouse_write(0xf3, TRUE);  /* Set rate command */
    mouse_write(0xC8, TRUE);  /* Set rate command */
    //  Serial.print("Setting sample rate 100\n");
    mouse_write(0xf3, TRUE);  /* Set rate command */
    mouse_write(0x64, TRUE);  /* Set rate command */
    //  Serial.print("Setting sample rate 80\n");
    mouse_write(0xf3, TRUE);  /* Set rate command */
    mouse_write(0x50, TRUE);  /* Set rate command */
    //  Serial.print("Read device type\n");
    mouse_write(0xf2, TRUE);  /* Set rate command */
    mouse_read();  /* mouse id, if this value is 0x00 mouse is standard, if it is 0x03 mouse is Intellimouse */
    //  Serial.print("Setting wheel\n");
    mouse_write(0xe8, TRUE);  /* Set wheel resolution */
    mouse_write(0x03, TRUE);  /* 8 counts per mm */
    mouse_write(0xe6, TRUE);  /* scaling 1:1 */
    mouse_write(0xf3, TRUE);  /* Set sample rate */
    mouse_write(0x28, TRUE);  /* Set sample rate */
    mouse_write(0xf4, TRUE);  /* Enable device */

    //  Serial.print("Sending remote mode code\n");
    mouse_write(0xf0, TRUE);  /* remote mode */
    //  Serial.print("Read ack byte2\n");
    delayMicroseconds(100);
}


/* === Internal functions === */


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
            SERIAL_DBG.printf("unsupported event.event %d\r\n", event.event);
        };
        break;
    }
    default: //unsupported case
        SERIAL_DBG.printf("unsupported event.type %d\r\n",event.type);
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


void processPS2Mouse()
{
    char mz;

    /* get a reading from the mouse */
    mouse_write(0xeb, TRUE);  /* give me data! */
    mz = mouse_read(); // we read status but discard it
    mz = mouse_read(); // we read x position but discard it
    mz = mouse_read(); // we read y position but discard it
    mz = mouse_read(); // we read mouse wheel position and use it

    // TODO  :can be simplified
    if ((int)mz > 0 && (int)mz < 128)
        encoder0Pos += (int)mz;
    if ((int)mz >= 128 && (int)mz < 256)
        encoder0Pos -= (256 - (int)mz);

    if (encoder0Pos <= -RESOLUTION) {
        encoder0Pos += RESOLUTION;
        // should we test for keyJustPressed in the condition?
        // Here we reset it and make a longer keypress
        keyJustPressed = 1;
        keyCode = maps[currentMap].left;
        lastWheelEvent = millis();
        sendHidEvent(keyCode);
    }
    if (encoder0Pos >= RESOLUTION) {
        encoder0Pos -= RESOLUTION;
        keyJustPressed = 1;
        keyCode = maps[currentMap].right;
        lastWheelEvent = millis();
        sendHidEvent(keyCode);
    }

    //delay(20);  /* twiddle */
    int current = millis();
    if (keyJustPressed == 1 && ((current - lastWheelEvent) > keyPressDuration )){
        releaseHidEvent(keyCode);
        keyJustPressed = 0;
    }
}

/* read all 3 switches  return (a + b * 2 + c * 4) */
int readSwitchesVal()
{
    return (digitalRead(SWITCH1_PIN) +
            digitalRead(SWITCH2_PIN) * 2 +
            digitalRead(SWITCH3_PIN) * 4);
}

void setButtonsLights(int value)
{
    digitalWrite(LIGHT_A_PIN, (value&1)?HIGH:LOW);
    digitalWrite(LIGHT_B_PIN, (value&2)?HIGH:LOW);
    digitalWrite(LIGHT_C_PIN, (value&4)?HIGH:LOW);
}

void setup() {
    pinMode (BUTTON_A_PIN, INPUT_PULLUP);
    pinMode (BUTTON_B_PIN, INPUT_PULLUP);
    pinMode (BUTTON_C_PIN, INPUT_PULLUP);
    pinMode (PEDAL_PIN, INPUT_PULLUP);

    pinMode (SWITCH1_PIN, INPUT);
    pinMode (SWITCH2_PIN, INPUT);
    pinMode (SWITCH3_PIN, INPUT);

    pinMode (LIGHT_A_PIN, OUTPUT);
    pinMode (LIGHT_B_PIN, OUTPUT);
    pinMode (LIGHT_C_PIN, OUTPUT);

    digitalWrite(LIGHT_A_PIN, LOW);
    digitalWrite(LIGHT_B_PIN, LOW);
    digitalWrite(LIGHT_C_PIN, LOW);

    pinMode (POT1_PIN, INPUT);
    pinMode (POT2_PIN, INPUT);

#ifdef ENABLE_SERIAL3
    SERIAL_DBG.begin(115200);        //  TX:7  RX:8
    SERIAL_DBG.println("----------\n|SERIAL 3|\n----------");
#endif

    // PS2 mouse init
    mouse_init();
#ifdef ENABLE_SERIAL3
    SERIAL_DBG.println("mouse_init done");
#endif

    Keyboard.begin();
    keyJustPressed = 0;
    keyCode = NO_KEY;
    lastWheelEvent = millis();
}

void loop() {
    processPS2Mouse();

    /* those values are read in the main loop because they can change at any time */
    int tempSwitches = readSwitchesVal();

    tempSwitches = min(tempSwitches, NB_MAPS);
    if (currentMap != tempSwitches) {
        currentMap = tempSwitches;
        SERIAL_DBG.printf("new switches val = %d\r\n", currentMap);
    }

    int val = 1 + (analogRead(POT1_PIN) >> 6);
    if ( val != resMultiplyer ) {
        resMultiplyer = val;
        SERIAL_DBG.printf("resMultiplyer = %d\r\n", resMultiplyer);
    }
    val = 1 + (analogRead(POT2_PIN) >> 6);
    if ( val != keyLengthMultiplyer ) {
        keyLengthMultiplyer = val;
        SERIAL_DBG.printf("keyLengthMultiplyer = %d\r\n", keyLengthMultiplyer);
    }
    keyPressDuration = KEYPRESS_LENGTH * keyLengthMultiplyer;

    for (int btn = 0 ; btn < NB_BUTTONS ; btn++) {
        // we release the key when the user releases the button
        if (digitalRead(buttonPins[btn]) == BUTTON_RELEASED && buttonJustPressed[btn] == 1) {
            buttonJustPressed[btn] = 0;
            digitalWrite(buttonLed(buttonPins[btn]), LOW);
            releaseHidEvent(maps[currentMap].buttons[btn]);
        }

        if (digitalRead(buttonPins[btn]) == BUTTON_PRESSED && buttonJustPressed[btn] == 0) {
            buttonJustPressed[btn] = 1;
            digitalWrite(buttonLed(buttonPins[btn]), HIGH);
            sendHidEvent(maps[currentMap].buttons[btn]);
        }
    }

}

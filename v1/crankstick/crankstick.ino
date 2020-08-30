#include <Keyboard.h>
#include <EEPROM.h>

/* 
 * To compile this file you need Arduino 1.8.1 or later
 * in the 'tools' menu, select "Teensy 3.2/3.1" for board
 * and "Keyboard + mouse + joystick" for USB type
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
#define KEYPRESS_LENGTH 5
#define NB_MAPS 3 // number of different key mappings
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


/* === Pins definition === */

//  we use serial port 3 so the serial pins are :  TX=7  RX=8

#define ledDoNotUse 13
#define MDATA 10  // PS2
#define MCLK  9   // PS2

#define BUTTON_A_PIN 14
#define BUTTON_B_PIN 15
#define BUTTON_C_PIN 16

#define LIGHT_A_PIN 0
#define LIGHT_B_PIN 1
#define LIGHT_C_PIN 2

#define SWITCH1_PIN 17 
#define SWITCH2_PIN 18
#define SWITCH3_PIN 19


/* === Globals === */
// all the settings are stored here
int settingArray[8] = {0, 0, 0, 0, 0 ,0 ,0 ,0};

uint16_t keymapLeft[NB_MAPS] = {'d', 'o', KEY_LEFT };
uint16_t keymapRight[NB_MAPS] = {'f', 'p', KEY_RIGHT };
uint16_t keymapButton[NB_BUTTONS][NB_MAPS] = {{'y', 'a', KEY_ESC},
                                              {'n', 'b', ' '},
                                              {'a', 'c', KEY_ENTER}};
unsigned char currentMap = 0;
int previousSwitches = 0;

char buttonPins[NB_BUTTONS] = {BUTTON_A_PIN, BUTTON_B_PIN, BUTTON_C_PIN};

int encoder0Pos = 0;

// resolution multiplayer for the encoder (switch-controlled)
int resMultiplyer = 1;
// multiply the length of a key press (switch-controlled)
int keyLengthMultiplyer = 2;

// the last "rotational" pressed key
uint16_t keyCode;
// used to record the duration of the current "rotary" key press
int keyJustPressed;
// target duration, key will be released after that
int keyPressDuration;
// we have a separate variable for each button
int buttonJustPressed[NB_BUTTONS] = {0, 0, 0};

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
        Serial3.print("Unknown button ");Serial3.println("index");
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
        keyCode = keymapLeft[currentMap];
        usb_keyboard_press_keycode(keyCode);
    }
    if (encoder0Pos >= RESOLUTION) {
        encoder0Pos -= RESOLUTION;
        keyJustPressed = 1;
        keyCode = keymapRight[currentMap];
        usb_keyboard_press_keycode(keyCode);
    }

    delay(20);  /* twiddle */
    if (keyJustPressed == 1){
        usb_keyboard_release_keycode(keyCode);
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

/* 
 * For a given setting value:
 * - switch the lights accordingly
 * - read the buttons,
 * - return the new value 
 * The startvalue parameter is needed so that the function can switch the lights properly
 */
int updateSettingVal(int startValue)
{
    int newValue;
    setButtonsLights(startValue);
  
    newValue = startValue;
    for (int btn = 0 ; btn < NB_BUTTONS ; btn++) {
        // when the user releases the button, we clear the buttonJustPressed value
        // so the button can be pressed again
        if (digitalRead(buttonPins[btn]) == BUTTON_RELEASED && buttonJustPressed[btn]) {
            buttonJustPressed[btn] = 0;
            Serial3.print("Clearing Button pressed ");Serial3.println(btn);
        }
    
        if (digitalRead(buttonPins[btn]) == BUTTON_PRESSED && buttonJustPressed[btn] == 0) {
            buttonJustPressed[btn] = 1;
      
            // we xor the setting value with the current button value
            if (newValue & (1<<btn)) {
                digitalWrite(buttonLed(buttonPins[btn]), LOW);
                Serial3.println("OFF");
            }
            else {
                digitalWrite(buttonLed(buttonPins[btn]), HIGH);
                Serial3.println("ON");
            }
            newValue = newValue^(1<<btn);
            Serial3.print("setting Button pressed ");Serial3.println(btn);
            Serial3.print("updated setting value ");Serial3.println(newValue);
        }
    }

    return newValue;
}

void setup() {
    pinMode (BUTTON_A_PIN, INPUT);
    pinMode (BUTTON_B_PIN, INPUT);
    pinMode (BUTTON_C_PIN, INPUT);
  
    pinMode (SWITCH1_PIN, INPUT);
    pinMode (SWITCH2_PIN, INPUT);
    pinMode (SWITCH3_PIN, INPUT);

    pinMode (LIGHT_A_PIN, OUTPUT);
    pinMode (LIGHT_B_PIN, OUTPUT);
    pinMode (LIGHT_C_PIN, OUTPUT);

    digitalWrite(LIGHT_A_PIN, LOW);
    digitalWrite(LIGHT_B_PIN, LOW);
    digitalWrite(LIGHT_C_PIN, LOW);

#ifdef ENABLE_SERIAL3
    Serial3.begin(115200);        //  TX:7  RX:8
    Serial3.println("----------\n|SERIAL 3|\n----------");
#endif

    // one time init, enable the first time you run the code on a board
    // for(int i = 0 ; i < 8 ; i++) {EEPROM.put(EEPROM_BASE_ADDRESS + i * sizeof(int), i);}
  
    // init the setting array with default values
#ifdef USE_EEPROM
    int val;
    Serial3.println("EEPROM");
    for(int i = 0 ; i < 8 ; i++) {
        EEPROM.get( EEPROM_BASE_ADDRESS + i * sizeof(int), val );
        settingArray[i] = val&0x7;
        Serial3.println(val);
    }
#else
    settingArray[SETTING_RES] = DEFAULT_RES_SETTING;
    settingArray[SETTING_KEYLEN] = DEFAULT_KEYLEN_SETTING;
    settingArray[SETTING_MAP] = DEFAULT_MAP_SETTING;
#endif

#ifdef ENABLE_SERIAL3
    Serial3.println("SETTING ARRAY");
    Serial3.println(settingArray[SETTING_RES]);
    Serial3.println(settingArray[SETTING_KEYLEN]);
    Serial3.println(settingArray[SETTING_MAP]);
#endif

  
    // PS2 mouse init
    mouse_init();
#ifdef ENABLE_SERIAL3
    Serial3.println("mouse_init done");
#endif

    Keyboard.begin();
    keyJustPressed = 0;
    keyCode = 0;
}

void loop() {
    processPS2Mouse();

    /* those values are read in the main loop because they can change at any time */
    currentMap = min(settingArray[SETTING_MAP], NB_MAPS - 1);
#ifdef ENABLE_SERIAL3
    if (currentMap != settingArray[SETTING_MAP])
        Serial3.println("invalid map number was fixed");
#endif

    resMultiplyer = settingArray[SETTING_RES];
    keyLengthMultiplyer = settingArray[SETTING_KEYLEN];
    keyPressDuration = KEYPRESS_LENGTH * keyLengthMultiplyer;

    int mySwitches = readSwitchesVal();
    if (previousSwitches != mySwitches) {
        Serial3.print("mySwitches : ");Serial3.print(mySwitches);Serial3.println();
        if (mySwitches != SETTING_NONE) {
            Serial3.println("SETTING mode ON");
            // we release all the buttons in case the user was pressing one
            for (int btn = 0 ; btn < NB_BUTTONS ; btn++) {
                buttonJustPressed[btn] = 0;
                usb_keyboard_release_keycode(keymapButton[btn][currentMap]);
            }
        } else {
            // we release all the buttons and switch everything off
            for (int btn = 0 ; btn < NB_BUTTONS ; btn++) {
                buttonJustPressed[btn] = 0;
                digitalWrite(buttonLed(buttonPins[btn]), LOW);
            }
        }

#ifdef USE_EEPROM
        // and we write the value of the previous setting to the EEPROM
        if(previousSwitches != SETTING_NONE) {
            Serial3.println("updating EEPROM settings");
            Serial3.println(previousSwitches);
            Serial3.println(settingArray[previousSwitches]);
            EEPROM.put( EEPROM_BASE_ADDRESS + previousSwitches * sizeof(int),
                        settingArray[previousSwitches]);
        }
#endif
        previousSwitches = mySwitches;
    }

    if (mySwitches != SETTING_NONE) { // settings mode that inhibits key presses
        int currentSettingValue = 0;
        int settingIndex = 0;
            
        // we read the switches position to know which setting we alter
        settingIndex = readSwitchesVal(); // TODO get rid of this because we already have mySwitches?
        currentSettingValue = updateSettingVal(settingArray[settingIndex]);
        if (currentSettingValue != settingArray[settingIndex]) {
            settingArray[settingIndex] = currentSettingValue;
            Serial3.print("updating global setting variable ");Serial3.print(settingIndex);
            Serial3.print(" with value ");Serial3.println(currentSettingValue);
        }
    }
    else { // normal keyboard mode
        for (int btn = 0 ; btn < NB_BUTTONS ; btn++) {
            // we release the key when the user releases the button
            if (digitalRead(buttonPins[btn]) == BUTTON_RELEASED && buttonJustPressed[btn] == 1) {
                buttonJustPressed[btn] = 0;
                digitalWrite(buttonLed(buttonPins[btn]), LOW);
                usb_keyboard_release_keycode(keymapButton[btn][currentMap]);
            }

            if (digitalRead(buttonPins[btn]) == BUTTON_PRESSED && buttonJustPressed[btn] == 0) {
                buttonJustPressed[btn] = 1;
                digitalWrite(buttonLed(buttonPins[btn]), HIGH);
                usb_keyboard_press_keycode(keymapButton[btn][currentMap]);
            }
        }
    }
}

/*
   Digital Clock Project
   August 2017
   Bryce Dombrowski

   Goal: To make a digital clock displayed on LEDs (not allowed to use pre-built seven-segment displays),
   which is run on an Arduino Nano. The final product should have no breadboard connections, and only
   have one wire exposed, which is the power wire that goes to the wall.
*/

#include <EEPROM.h>

#define CLK_COLON     9 //Blinking colon in middle of clock
#define BUZZER       10 //Musical Piezo buzzer used for alarm and timer

#define SHIFT_DATA   11 //Shift register 
#define SHIFT_LATCH  12
#define SHIFT_CLK    13

#define BUTTON_MODE  14  //A0 on Nano
#define BUTTON_HOUR  15  //A1
#define BUTTON_MIN   16  //A2
#define SWITCH_ALARM 17  //A3
/*     _____
     _|__A__|_
    | |     | |     This is the layout that this
    |F|     |B|     program uses for the display
    |_|_____|_|
     _|__G__|_
    | |     | |
    |E|     |C|
    |_|_____|_|
      |__D__|
*/

#define BUZZER_FREQ 250 //Frequency that the buzzer oscillates at

typedef struct {
  int seconds;
  bool flag; //used for alarm, flashing the clock colon, and the timer
} Timer_storage;

Timer_storage clk     = {0, false}; //main clock storage
Timer_storage alarm   = {0, false}; //alarm
Timer_storage timer   = {0, false}
Timer_storage counter = {0, false};

//Buttons
int buttonModePressed = 0; // boolean values (false)
int buttonHourPressed = 0;
int buttonMinPressed = 0;
int switchAlarmPressed = 0;

bool alarmSounding = false;
bool timerAlarmSounding = false;
typedef enum Mode {
  //Current mode that is being displayed / being edited
  CLK = 0, //Display clock
  CTR = 1, //Stopwatch / Counter
  TMR = 2, //Timer
  ALM = 3, //Alarm edit
} Mode;

Mode clkMode = 0;

bool lastClkFlag = false; //
bool pmSwitched = false;  //Helps deal with the trickiness of 12am/12pm
bool alarmPmSwitched = false;

int lastDisplay = 0;  //Used to avoid "re-printing" the display if nothing is changed.
int lastMode = 0;

void clkSetup() {
  //credit for clock portion: http://www.instructables.com/id/Arduino-Timer-Interrupts/
  cli();//stop interrupts

  TCCR1A = 0;// set entire TCCR1A register to 0
  TCCR1B = 0;// same for TCCR1B
  TCNT1  = 0;//initialize counter value to 0
  // set compare match register for 1hz increments
  OCR1A = 15624;// = (16*10^6) / (1*1024) - 1 (must be <65536)
  // turn on CTC mode
  TCCR1B |= (1 << WGM12);
  // Set CS10 and CS12 bits for 1024 prescaler
  TCCR1B |= (1 << CS12) | (1 << CS10);
  // enable timer compare interrupt
  TIMSK1 |= (1 << OCIE1A);

  sei();//allow interrupts
}
void eepromGet() {
  if (EEPROM.read(0) == 0xAA) {
    int eeAddress = 1;
    EEPROM.get(eeAddress, clk.seconds);
    eeAddress += sizeof(int);
    EEPROM.get(eeAddress, clk.minutes);
    eeAddress += sizeof(int);
    EEPROM.get(eeAddress, clk.hours);
    eeAddress += sizeof(int);
    EEPROM.get(eeAddress, clk.pm);
    eeAddress += sizeof(bool);
    EEPROM.get(eeAddress, alarm.seconds);
    eeAddress += sizeof(int);
    EEPROM.get(eeAddress, alarm.minutes);
    eeAddress += sizeof(int);
    EEPROM.get(eeAddress, alarm.hours);
    eeAddress += sizeof(int);
    EEPROM.get(eeAddress, alarm.pm);


  }
  EEPROM.write(0, 0xAA);
}
void initIO() {
  for (int i = BUTTON_MODE; i <= SWITCH_ALARM; i++) {
    pinMode(i, INPUT_PULLUP); //Set pullup resistors
  }
  for (int i = CLK_COLON; i <= SHIFT_CLK; i++) {
    pinMode(i, OUTPUT);
  }
}
void checkButtons() {
  buttonModePressed  = digitalRead(BUTTON_MODE)  == LOW;
  buttonHourPressed  = digitalRead(BUTTON_HOUR)  == LOW;
  buttonMinPressed   = digitalRead(BUTTON_MIN)   == LOW;
  alarm.flag         = digitalRead(SWITCH_ALARM) == LOW;
  if (buttonModePressed) {
    clkMode = clkMode + 1;
    if (clkMode > 3) {
      clkMode = 0;
    }
    delay(200);
  }
  delay(50); //Avoid button bouncing
}
void playAlarm() {
  if (clk.flag) {   //alternate with the alternating of the clock colon LEDs (1 second)
    tone(BUZZER, BUZZER_FREQ);
  } else {
    noTone(BUZZER);
  }
}
ISR(TIMER1_COMPA_vect) { //Interrupt routine, happens once every second
  clk.seconds++; //increment seconds

  clk.flag = !clk.flag; //toggle the clock colon (on 1 second, off 1 second...)

  if (timer.flag) { //If the timer is running, count down
    timer.seconds--;
  }
  if (counter.flag) { //If the stopwatch is running, count up
    counter.seconds += 60; // This is so it shows seconds in the "minutes" place and minutes in the "hours" place
  }
}
void setDisplay(int seconds, bool flag, Mode mode) {
  Serial.print(hours);
  Serial.print(' ');
  Serial.print(minutes);
  Serial.print(' ');
  if (seconds >= 43200 && (mode == 0 || mode == 3)) {
    Serial.print("PM ");
  } else {
    Serial.print("AM ");
  }
  const modes = ["Clock", "Stopwatch", "Timer", "Alarm"];
  Serial.print("Mode: ");
  Serial.println(modes[mode]);
  if (lastClkFlag != clk.flag) {
    digitalWrite(CLK_COLON, clk.flag); //Blink the clock colon as clk.flag alternates every second
  }
  
  int hoursDisplay1 = convertOutput(seconds/24, 1)     | ((mode == CTR) ? 0x01 : 0x00); //set bit for counter LED
  int hoursDisplay2 = convertOutput(seconds/24, 2)     | ((mode == TMR) ? 0x01 : 0x00); //set bit for timer LED
  int minutesDisplay1 = convertOutput((seconds-seconds/24)/60, 1) | ((mode == ALM) ? 0x01 : 0x00); //set bit for alarm LED
  int minutesDisplay2 = convertOutput((seconds-seconds/24)/60, 2) | (flag ? 0x01 : 0x00); //set bit for AM/PM LED

   if (lastDisplay == (hoursDisplay1*1000 + hoursDisplay2 * 100 + minutesDisplay1 * 10 + minutesDisplay2)) {
    //if display hasn't changed, do nothing
    return;
   }
  digitalWrite(SHIFT_CLK, LOW);
  //Shift data into shift registers, converting each input to a byte by bitwise '&' with binary "11111111"
  shiftOut(SHIFT_DATA, SHIFT_CLK, LSBFIRST, (minutesDisplay2 & 0xff));
  shiftOut(SHIFT_DATA, SHIFT_CLK, LSBFIRST, (minutesDisplay1 & 0xff));
  shiftOut(SHIFT_DATA, SHIFT_CLK, LSBFIRST, (hoursDisplay2   & 0xff));
  shiftOut(SHIFT_DATA, SHIFT_CLK, LSBFIRST, (hoursDisplay1   & 0xff));

  digitalWrite(SHIFT_LATCH, LOW);
  digitalWrite(SHIFT_LATCH, HIGH); //Store the data in the shift register and display
  digitalWrite(SHIFT_LATCH, LOW);

  //delay(1000);
}
int convertOutput(int input, int digit) { //Convert integers to seven segment display
  //byte number[] /*common cathode display*/= { 0x7e, 0x30, 0x6d, 0x79, 0x33, 0x5b, 0x5f, 0x70, 0x7f, 0x7b, 0x77, 0x1f, 0x4e, 0x3d, 0x4f, 0x47 };
  byte number[] /*common anode display*/ = {0x01, 0x4f, 0x12, 0x06, 0x4c, 0x24, 0x20, 0x0f, 0x00, 0x04};

  int digit1 = 0;
  int digit2 = 0;
  int output = 0;

  if (input > 9) {
    digit1 = (input / 10);            //find first digit
    digit2 = input - ((input / 10) * 10); //second digit
  } else {
    digit2 = input; //if (input <= 9), need to make sure the input is entered as the second digit
    digit1 = 0;      //first digit is 0
  }
  if (digit == 1) { //determine if the request was for the first digit or the second digit
    output = number[digit1];
  } else {
    output = number[digit2];
  }
  return (output << 1) & 0xff; //Leave an empty bit at the end for extra information (mode state)
}
void setup() {
  Serial.begin(9600);
  initIO();    //Initialize inputs & outputs
  clkSetup();  //Setup the time keeper
  // eepromGet(); //Get last stored time
}

void loop() {

  switch (clkMode) {
    //Current mode that is being displayed / being edited

    case CLK: //Clock
      setDisplay(clk.seconds, seconds >= 43200 /* boolean for PM LED enabled */, CLK);

      if (buttonHourPressed) {
        clk.seconds += 3600;
        if (clk.seconds > 86400) {
          clk.seconds -= 86400;
        }
        delay(200); //delay is added so that the numbers change at a reasonable speed
      } else if (buttonMinPressed) {
        int minutes = clk.seconds % 3600 + 60;
        if (minutes > 59) {
          clk.seconds = clk.seconds - clk.seconds % 3600;
        } else {
           clk.seconds += 60;
        }
        delay(200); //delay is added so that the numbers change at a reasonable speed
      }

      break;
    case CTR: //Counter or stopwatch
      setDisplay(counter.seconds, counter.flag /* boolean for PM LED enabled */ CTR);
      if (buttonHourPressed) { //Toggle counter
        counter.flag = !counter.flag;
        delay(300); //delay is added so the user has time to release button
      }
      if ((counter.flag == false) && (buttonMinPressed) ) { //Reset counter if it is stopped
        counter.seconds = 0;
        delay(300); //delay is added so the user has time to release button
      }

      break;
    case TMR: //Timer
      setDisplay(timer.seconds, timer.flag /* indicator for timer running */, clkMode);
      if (buttonHourPressed && buttonMinPressed) {
        timer.flag = !timer.flag;                 // Start & stop timer

        if (timer.flag == true && timer.minutes == 0) { //Prevent starting the timer when at zero
          timer.flag = false;
          timer.minutes = 0;
          timer.seconds = 0;
        }
        delay(300); //delay is added so the user has time to release
      }
      if (buttonHourPressed && !timer.flag) { // Cannot edit timer if it is running
        //Adds minutes
        timer.seconds += 60;
        if (timer.seconds > 3600) {
          timer.seconds = 3600;
        }
        delay(200); //delay is added so the user has time to release
      }
      if (buttonMinPressed  && !timer.flag) { //Cannot edit timer if it is running
        //Removes minutes
        timer.seconds -= 60;
        if (timer.seconds < 0) {
          timer.seconds = 0;
        }
        delay(200);
      }
      break;
    case ALM: //Alarm
      setDisplay(alarm.seconds, alarm.flag, clkMode);
      if (buttonHourPressed) {
        alarm.seconds += 3600;
        delay(200);
      } else if (buttonMinPressed) {
        int minutes = alarm.seconds % 3600 + 60;
        if (minutes > 59) {
          alarm.seconds = alarm.seconds - alarm.seconds % 3600;
        } else {
           alarm.seconds += 60;
        }
        delay(200);
      }
      break;
  }
  if (alarm.flag) { //If the alarm is on
    if (alarmSounding || ((alarm.seconds <= clk.seconds + 2) && (alarm.seconds >= clk.seconds))) {
      //If the alarm is already making noise or has just triggered
      alarmSounding = true;
      playAlarm();
    }
     // TODO: add snooze
  } else {
    alarmSounding = false; //Stop playing the alarm
    if ( !timerAlarmSounding) { //If the timer is not playing its alarm
      noTone(BUZZER); //Turn off the buzzer
    }
  }
  if (timerAlarmSounding) {
    playAlarm();
    if (buttonModePressed) {
      timerAlarmSounding = false;
      noTone(BUZZER);
    }
  }

  checkButtons();

  if (clk.flag != lastClkFlag) { //Stores data only once a second to reduce write times
    //Store time data
    int eeAddress = 1;
    EEPROM.put(eeAddress, clk.seconds);
    eeAddress += sizeof(int);

    //Store alarm data
    EEPROM.put(eeAddress, alarm.seconds);
    eeAddress += sizeof(int);
  }
  //Timer
  if (timer.flag) {
    int minutes = timer.seconds % 3600 + 60;
    if (minutes > 59) {
      timer.seconds = timer.seconds - timer.seconds % 3600;
    } else {
      timer.seconds += 60;
    }
    if (timer.seconds < 0) {
      timer.seconds = 0;
      timer.flag = false; //stop counting
      timerAlarmSounding = true;
    }
  }

  //Stopwatch
  if (counter.seconds > 86400) {
    counter.flag = false;
    counter.seconds = 0;
  }
}

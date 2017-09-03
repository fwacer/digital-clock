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
  int hours;
  int minutes;
  int seconds;
  bool pm; // AM or PM
  bool flag; //used for alarm, flashing the clock colon, and the timer
} Timer_storage;

Timer_storage clk     = {12, 0, 0, false, false}; //main clock storage
Timer_storage alarm   = {12, 0, 0, false, false}; //alarm
Timer_storage timer   = { 0, 0, 0, false, false};;
Timer_storage counter = { 0, 0, 0, false, false};;

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

int lastHour = 0;  //Used to avoid "re-printing" the display if nothing is changed.
int lastMinute = 0;
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

  if (clk.seconds > 59) {
    clk.minutes++;
    clk.seconds = 0; //Reset seconds
  }
  if (clk.minutes > 59) {
    clk.hours++;
    clk.minutes = 0; //Reset minutes
  }

  if (clk.hours > 12) {
    clk.hours = 1; // Reset hours to 1
    pmSwitched = false;
  }
  if (clk.hours > 11 && (pmSwitched == false)) {
    clk.pm = !clk.pm; // Switch between AM & PM
    pmSwitched = true;
  }

  clk.flag = !clk.flag; //toggle the clock colon (on 1 second, off 1 second...)

  if (timer.flag) { //If the timer is running, count down
    timer.seconds--;
  }
  if (counter.flag) { //If the stopwatch is running, count up
    counter.seconds++;
  }
}
void setDisplay(int hours, int minutes, bool pm, Mode mode) {
  Serial.print(hours);
  Serial.print(' ');
  Serial.print(minutes);
  Serial.print(' ');
  if (pm) {
    Serial.print("PM ");
  } else {
    Serial.print("AM ");
  }
  String modeType;
  switch (mode) {
    case 0:
      modeType = "Clock";
      break;
    case 1:
      modeType = "Stopwatch";
      break;
    case 2:
      modeType = "Timer";
      break;
    case 3:
      modeType = "Alarm";
      break;
    default:
      modeType = "Error";
      break;
  }
  Serial.print("Mode: ");
  Serial.println(modeType);
  if (lastClkFlag != clk.flag) {
    digitalWrite(CLK_COLON, clk.flag); //Blink the clock colon as clk.flag alternates every second
  }
  if ((hours == lastHour) && (lastMinute = minutes) && (mode == lastMode)) {
    //if display hasn't changed, do nothing
    return;
  }

  int hoursDisplay1 = convertOutput(hours, 1)     | ((mode == 1) ? 0x01 : 0x00); //set bit for counter LED
  int hoursDisplay2 = convertOutput(hours, 2)     | ((mode == 2) ? 0x01 : 0x00); //set bit for timer LED
  int minutesDisplay1 = convertOutput(minutes, 1) | ((mode == 3) ? 0x01 : 0x00); //set bit for alarm LED
  int minutesDisplay2 = convertOutput(minutes, 2) | ((pm)        ? 0x01 : 0x00); //set bit for AM/PM LED

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
  eepromGet(); //Get last stored time
}

void loop() {

  switch (clkMode) {
    //Current mode that is being displayed / being edited

    case CLK: //Clock
      setDisplay(clk.hours, clk.minutes, clk.pm, clkMode);

      if (buttonHourPressed) {
        clk.hours++;
        if (clk.hours > 12) {
          clk.hours = 1;
          pmSwitched = false;
        }
        if (clk.hours > 11 && (pmSwitched == false)) {
          pmSwitched = true;
          clk.pm = !clk.pm;
        }
        delay(200); //delay is added so that the numbers change at a reasonable speed
      } else if (buttonMinPressed) {
        clk.minutes++;
        if (clk.minutes > 59) {
          clk.minutes = 0;
        }
        delay(200); //delay is added so that the numbers change at a reasonable speed
      }

      break;
    case CTR: //Counter or stopwatch
      setDisplay(counter.minutes, counter.seconds, false /*Doesn't use the PM flag*/, clkMode);
      if (buttonHourPressed) { //Toggle counter
        counter.flag = !counter.flag;
        delay(300); //delay is added so the user has time to release button
      }
      if ((counter.flag == false) && (buttonMinPressed) ) { //Reset counter if it is stopped
        counter.hours = 0;
        counter.minutes = 0;
        counter.seconds = 0;
        delay(300); //delay is added so the user has time to release button
      }

      break;
    case TMR: //Timer
      setDisplay(timer.minutes, timer.seconds, timer.flag /*indicator for timer running*/, clkMode);
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
        timer.minutes++;
        if (timer.minutes > 60) {
          timer.minutes = 0;
        }
        delay(200); //delay is added so the user has time to release
      }
      if (buttonMinPressed  && !timer.flag) { //Cannot edit timer if it is running
        //Removes minutes
        timer.minutes--;
        if (timer.minutes < 0) {
          timer.minutes = 0;
        }
        delay(200);
      }
      break;
    case ALM: //Alarm
      setDisplay(alarm.hours, alarm.minutes, alarm.pm, clkMode);
      if (buttonHourPressed) {
        alarm.hours++;
        if (alarm.hours > 12) {
          alarm.hours = 1;
          alarmPmSwitched = false;
        }
        if (alarm.hours > 11 && (alarmPmSwitched == false)) {
          alarmPmSwitched = true;
          alarm.pm = !alarm.pm;
        }
        delay(200);
      } else if (buttonMinPressed) {
        alarm.minutes++;
        if (alarm.minutes > 59) {
          alarm.minutes = 0;
        }
        delay(200);
      }
      break;
  }
  if (alarm.flag) { //If the alarm is on
    if (alarmSounding || ((clk.hours == alarm.hours) && (clk.minutes == alarm.minutes))) {
      //If the alarm is already making noise or has just triggered
      alarmSounding = true;
      playAlarm();
    }
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
    EEPROM.put(eeAddress, clk.minutes);
    eeAddress += sizeof(int);
    EEPROM.put(eeAddress, clk.hours);
    eeAddress += sizeof(int);
    EEPROM.put(eeAddress, clk.pm);
    eeAddress += sizeof(bool);

    //Store alarm data
    EEPROM.put(eeAddress, alarm.seconds);
    eeAddress += sizeof(int);
    EEPROM.put(eeAddress, alarm.minutes);
    eeAddress += sizeof(int);
    EEPROM.put(eeAddress, alarm.hours);
    eeAddress += sizeof(int);
    EEPROM.put(eeAddress, alarm.pm);
  }
  //Timer
  if (timer.flag) {
    if (timer.seconds < 0) {
      timer.minutes--;
      if (timer.minutes < 0) {
        timer.hours--;
        if (timer.hours < 0) {
          timer.hours = 0;
          timer.minutes = 0;
          timer.seconds = 0;
          timer.flag = false; //stop counting
          timerAlarmSounding = true;
        } else {
          timer.minutes = 59;
        }
      } else {
        timer.seconds = 59;
      }
    }
  }

  //Counter / Stopwatch
  if (counter.seconds > 59) {
    counter.minutes++;
    counter.seconds = 0; //Reset seconds
  }
  if (counter.minutes > 59) {
    counter.hours++;
    counter.minutes = 0; //Reset minutes
  }
  if (counter.hours > 99) {
    counter.flag = false;
    counter.seconds = 0;
    counter.minutes = 0;
    counter.hours = 0;
  }
}

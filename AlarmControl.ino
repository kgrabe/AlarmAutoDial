/*
  Send SMS messages for an Alarm panel
  KarlGrabe.com

  2018.01.23 Initial
  2018.01.25 Add GSM
  2018.01.31 Rename constants, add functions for gsm send and serialPrintln
  2018.02.02 Add test SMS button
  2018.02.04 WatchDog timer. Conditional compile for serial
  2018.02.04 Conditional compiles added,
  2018.02.05 SMS counter, value appended to texts
  2018.02.05 Receive call
  2018.02.09 Inverted Alarm Ringing and PA pins, INPUT_PULLUP on inputs
  2018.02.09 Version 1.0 Installed
  2018.02.15 V1.1 inverted inputs, txt on startup
  2018.02.15 V1.2 Debug mode, work without gsm shield


  ***** TODO:
    debug mode, print messages to screen- work without shield
    multiple numbers for texts
    echo received texts
    echo calling number
    generic method to send text on pin change including:
      mains power outage, send text
      fire alarm - send text
      text on tank water level too low or no pressure
    drop pin input variables if needed to save space
    config.h file for global settings, pin allocations
    contactsConfig.h - SMS contact numbers and who to send for what,
      add first contact to end so alarm message repeated.
    Doorbell - text for press
    add fast flash of status led suring post SMS send delay - POST_SMS_SENT_DELAY
    check sending text in receiveVoiceCall, remove sendSMS if not working

*/

#include "alarmGlobals.h"         // timer values etc
#include "alarmStrings.h"         // all text stings
#include "phoneNumbers.h"         // personal phone numbers to call/text
#include <GSM.h>
#include <avr/wdt.h>

#define GSM_LIVE                  // Send SMS messages if defined (debugging)
#define PRINT_SERIAL_ENABLED      // print to serial if defined (debugging)

// Assign Output LED Pins:
#define ALARM_ARMED_LED       8   // alarm armed tell tale (proto LED1)
#define ALARM_RINGING_LED     9   // Ringing tell tale LED (proto LED2)
#define ALARM_PER_ATTACK_LED  10  // PA tell tale LED (proto center red LED)
#define ALARM_RUNNING_LED     13  // Slow flash running, fast flash rebooting

// Assign Input Pins connected to alarm panel Digi outputs
#define ALARM_ARMED_PIN       4   // true = armed
#define ALARM_RINGING_PIN     5   // true = ringing
#define ALARM_PER_ATTACK_PIN  6   // true = PA pressed


// Test sending a text
#define TEST_GSM_SMS          12  // test button pin, send test sms

// AC Power supply monitor
#define AC_POWER_PIN          11  // ***** TO BE COMPLETED

// Pins used by GSM library: Serial comms: 2,3; Power on: 7


// initialize the library instance
GSM gsmAccess;    // GSM Modem, Arduino's GSM Shield 2
GSM_SMS sms;      // send/(receive) texts
GSMVoiceCall vcs; // (call) receive voice calls

// *************** debugging ***************
const int noChars = PHONE_NUMBER_MAX_DIGITS;
const boolean GSM_ACTIVE = true;

// Alarm panel status & intial setting, later copied from pins connected to alarm panel
boolean alarmArmed          = false;        // current state
boolean alarmArmedOld       = alarmArmed;   // old state, send text if different to curren state

boolean alarmRinging        = false;        // ditto
boolean alarmRingingOld     = alarmRinging;

boolean alarmPerAttack      = false;        // ditto
boolean alarmPerAttackOld   = alarmPerAttack;

// global variables
unsigned long previousMillis  = 0;      // for loop interval timer
unsigned short sentSMScounter = 0;      // SMS sent counter, indicates if reboot occurred

/*

          SETUP

*/

void setup() {
  configurePorts(); // configure digital pins inputs/outputs
  flashLED (ALARM_RUNNING_LED, 100, 40); // we're booting/re-booting, 100ms, 40times
  initSerialPort();  // if PRINT_SERIAL init port to PC
  startGSMshield (); // start GSM
  watchdogSetup(); // set up & start watchdog timer
  sendSMS (CONTACT_1, "Alarm Monitor Running");
}


/*

          MAIN LOOP

*/

void loop() {
  wdt_reset();   // reset watchdog timer
  // Check at what interval to run loop code
  unsigned long currentMillis = millis();
  if (currentMillis - previousMillis >= LOOP_INTERVAL) {
    previousMillis = currentMillis; // reset interval time
    wdt_reset();   // reset watchdog timer
    copyAlarmDigiOutputsPins();   // copy Alarm panel DigiOutpus to variables
    /*
        if (alarmRingingMsgToSend) {
          sendText (contact1, "** RINGING ** Alarm");
          sendText (contact2,"** RINGING ** Alarm")
          sendText (contact3,"** RINGING ** Alarm")
          alarmRingingMsgToSend = false;
          }




    */


    // check if alarm armed input has changed, send SMS if so
    checkSendText (alarmArmed, alarmArmedOld, "Alarm Set", "Alarm Unset");
    alarmArmedOld = alarmArmed;

    // check if alarm ringing input has changed, send SMS if so
    checkSendText (alarmRinging, alarmRingingOld, "** RINGING ** Alarm", "Stopped Ringing");
    alarmRingingOld = alarmRinging;

    // check if alarm PA input has changed, send SMS if so
    checkSendText (alarmPerAttack, alarmPerAttackOld, "** PERSONAL ATTACK **", "PA cancelled");
    alarmPerAttackOld = alarmPerAttack;

    // show we're running, flip ALARM_RUNNING_LED
    digitalWrite (ALARM_RUNNING_LED, !digitalRead(ALARM_RUNNING_LED));

    if (!digitalRead(TEST_GSM_SMS)) {     // ** add force test let on
      //serialPrintln ("test button pressed");
      sendSMS (CONTACT_1, "Alarm SMS test");
    }
    wdt_reset();   // reset watchdog timer
    receiveVoiceCall ();    // answer if someone calling
    wdt_reset();   // reset watchdog timer
  } // end timed interval inside main loop

  // code here outside timed loop.
}   // end of MAIN LOOP


// read and Copy Alarm Digi Outputs Pins to variables
void copyAlarmDigiOutputsPins () {
  alarmArmed      = digitalRead (ALARM_ARMED_PIN);
  alarmRinging    = digitalRead (ALARM_RINGING_PIN);
  alarmPerAttack  = digitalRead (ALARM_PER_ATTACK_PIN);


  // update the telltale LEDs to reflect current alarm digi outputs
  digitalWrite(ALARM_ARMED_LED, alarmArmed);
  digitalWrite(ALARM_RINGING_LED, alarmRinging);
  digitalWrite(ALARM_PER_ATTACK_LED, alarmPerAttack);
}

/*
   checkSendText
   Use after calling copyAlarmDigiOutputsPins ()
   Check if an alarm input has changed by comparing to it's state tothe previus
   state and if so send one of two SMS texts depending on current pin state


   @param currentState  Current state of pin, as read by copyAlarmDigiOutputsPins
   @param OldState      Previous state of pin, to be compared with current state
   @param trueText      Text to send if pin changed to true
   @param falseText     ext to send if pin changed to false
   @return              --

*/

void checkSendText (boolean currentState, boolean OldState, String trueText, String falseText) {
  if (currentState != OldState) {     // check for change, print if so
    String statusText = trueText;
    if (!currentState)
      statusText = falseText;
    //serialPrintln ("SENDING text...");
    //serialPrintln (statusText);
    sendSMS (CONTACT_1, statusText);
    //sendSMS (karlNumber, "second text"); // ***** replace with 2nd num
    //serialPrintln ("Sent!\n------------\n\n");
  }

}

// send the message
void sendSMS (char remoteNumber[noChars], String message) {
  digitalWrite (ALARM_RUNNING_LED, true); // was flahsing, force on during send
  message += String (" (") + String (++sentSMScounter) + String (")");

  if (GSM_ACTIVE)       // actually send message
  {
    wdt_reset();   // reset watchdog timer
    sms.beginSMS(remoteNumber);
    sms.print(message);
    sms.endSMS();
    wdt_reset();   // reset watchdog timer
    delay (POST_SMS_SENT_DELAY); // DELAY ***** ad fast flash of status led
    wdt_reset();   // reset watchdog timer
  }

  else        // debugging, only send to serial port
  {
    serialPrintln ("* GSM Off *, phone number message: ");
    message = remoteNumber + String (", ") + message;

    serialPrintln (message);
  }



}

// configure i/o ports
void configurePorts () {

  // connected to alarm panel oupputs
  pinMode(ALARM_ARMED_PIN, INPUT_PULLUP);
  pinMode(ALARM_RINGING_PIN, INPUT_PULLUP);
  pinMode(ALARM_PER_ATTACK_PIN, INPUT_PULLUP);

  // Test Button - send test SMS when briefly pressed
  pinMode(TEST_GSM_SMS, INPUT_PULLUP);

  // AC power pin
  pinMode(AC_POWER_PIN, INPUT_PULLUP);

  // TellTale LEDS
  pinMode(ALARM_ARMED_LED, OUTPUT);
  pinMode(ALARM_RINGING_LED, OUTPUT);
  pinMode(ALARM_PER_ATTACK_LED, OUTPUT);
  pinMode(ALARM_RUNNING_LED, OUTPUT);

}


void serialPrintln (String theMessage) {
#ifdef PRINT_SERIAL_ENABLED
  Serial.println (theMessage);
#endif
}

void serialPrint (String theMessage) {
#ifdef PRINT_SERIAL_ENABLED
  Serial.print (theMessage);
#endif
}


void initSerialPort () {
#ifdef PRINT_SERIAL_ENABLED
  {
    Serial.begin(SERIAL_BAUD); // initialize serial comms
    while (!Serial);  // wait for serial port to connect.
    serialPrintln ("Serial port open...");
  }
#endif
}

// flash whichLED LED! howManyFlashes times
void flashLED (int whichLED, int periodMS, int howManyFlashes) {
  for (int i = 0; i <= howManyFlashes; i++) {
    digitalWrite(whichLED, !digitalRead(whichLED));
    delay (periodMS);
  }
}

/*
   Start GSM shield

  set PIN_NUMBER to "" for no SIM pin

*/
void startGSMshield () {

  if (GSM_ACTIVE)
  {
    serialPrintln ("Starting GSM");
    boolean notConnected = true; // connection state
    while (notConnected) {
      if (gsmAccess.begin(SIM_PIN_NUMBER) == GSM_READY) {
        notConnected = false;
      } else {
        serialPrintln ("Not connected");
        delay(1000);
      }
    }
    serialPrintln ("GSM active");
  }
  else {
    serialPrintln ("GSM not acrive");
  }

}

/*
   Watchdog timer

*/
void watchdogSetup(void) {
  wdt_enable(WATCHDOG_TIMER_INTERVAL);  // Reset after this interval
}

// Watchdog triggered, final code to run before reset
ISR(WDT_vect)
{
  digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN)); // was off, flick on WDT reset
}


// Receive call
void receiveVoiceCall ()

{
  if (!GSM_ACTIVE)      // exit if GMS not active
    return;
  // Array to hold the number for the incoming call
  char numtel[PHONE_NUMBER_MAX_DIGITS];

  // Check the status of the voice call
  switch (vcs.getvoiceCallStatus()) {
    case IDLE_CALL: // Nothing is happening

      break;

    case RECEIVINGCALL: // Yes! Someone is calling us
      serialPrintln(INCOMING_CALL_MSG);


      // Retrieve the calling number
      vcs.retrieveCallingNumber(numtel, PHONE_NUMBER_MAX_DIGITS);

      // Print the calling number
      //serialPrint("Number:");
      //serialPrintln(numtel);

      // Answer the call, establish the call
      vcs.answerCall();
      break;

    case TALKING:  // call established
      // serialPrintln("listening 2 seconds");
      wdt_reset();   // reset watchdog timer
      delay (INCOMING_CALL_DURATION); // hangup after this time
      wdt_reset();   // reset watchdog timer
      vcs.hangCall();
      // serialPrintln("Hanging up and waiting for the next call.");
      wdt_reset();   // reset watchdog timer
      delay (6000);
      wdt_reset();
      sendSMS (CONTACT_1, "got call");   // *** remove this if not working
      break;
  }
}


# AlarmAutoDialler
House alarm panel outputs connected to Arduino which sends SMS when triggered.
An Arduino GSM Shield 2 is used to send SMS texts.

The alarm panel's outputs have the following open collector outputs:
  Alarm Set
  Panic Attack
  Alarm Activated (bell ringing)
  
Any change to any of the alarm outputs initiates an SMS being sent to the number in the file phoneNumbers.h

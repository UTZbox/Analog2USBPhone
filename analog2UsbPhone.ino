/* -----------------------------------------------------------------------
 * Programmer:   Mike Utz
 * Date:         October 19, 2023
 * Platform:     Teensy 3.x or 4.0
 * USB Type:     “Audio + Keyboard + Serial“
 * Clock:        72 MHz or 96 MHz for 3.2 / any higher for 4.0
 * Add.Hardware  Tennsy Audio Board Rev. C or D
 * Description:  Turnes an old analog Phone to a USB-HeadSet for TEAMS and ZOOM
 *               Originaly made for a PTT Model 70 Phone of Switzerland
 *               Act as ClassCompliant v1 Audio Interface to use it with ZOOM/TEMAS or Headset
 *               The Dial-Wheel generates Numbers which will be sent as Keystrokes
 *               It rings the Bell when Audio is sent to the Device while the the Handset is hung up.
 *               It sends Keyboard shortcuts to Mute pick up and hangup the Calls for TEAMS and ZOOM.
 *               The whole Functionality is the same as it was back in the past (Line Tone, Ringer etc.)
 *               
 */

 
#include <Audio.h>
#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <SerialFlash.h>

AudioInputUSB            usb1; 
AudioSynthWaveformSine   sine1;
AudioInputI2S            i2s1;
AudioMixer4              mixer1;
AudioAnalyzePeak         rms1;
AudioAnalyzePeak         rms2;
AudioMixer4              mixer2;
AudioOutputI2S           i2s2;
AudioOutputUSB           usb2;
AudioConnection          patchCord1(usb1, 0, mixer1, 0);
AudioConnection          patchCord2(usb1, 0, rms1, 0);
AudioConnection          patchCord3(usb1, 1, mixer2, 0);
AudioConnection          patchCord4(usb1, 1, rms2, 0);
AudioConnection          patchCord5(sine1, 0, mixer1, 2);
AudioConnection          patchCord6(sine1, 0, mixer2, 2);
AudioConnection          patchCord7(i2s1, 0, mixer1, 1);
AudioConnection          patchCord8(i2s1, 0, usb2, 0);
AudioConnection          patchCord9(i2s1, 1, mixer2, 1);
AudioConnection          patchCord10(i2s1, 1, usb2, 1);
AudioConnection          patchCord11(mixer1, 0, i2s2, 0);
AudioConnection          patchCord12(mixer2, 0, i2s2, 1);
AudioControlSGTL5000     sgtl5000_1;


//Pin Assignments
const byte hookNC       = 0;
const byte dialRdy1     = 1;
const byte dialRdy2     = 2;
const byte pulse        = 3;
const byte mode         = 4;
const byte runLed       = 13;
const byte ringerL      = 14;
const byte ringerR      = 15;

//Array to store Keystrokes for the Dialed Number
const int DIALNRKEY[10] = {KEY_0, KEY_1, KEY_2, KEY_3, KEY_4, KEY_5, KEY_6, KEY_7, KEY_8, KEY_9};

//Variables
int keyMode                    = 0;   // 0 = KeyStrokes for TEAMS / 1 for ZOOM
int microGain                  = 15;  // 0 - 63 number in decibels
uint8_t audioLvlTrsh           = 1;   // Trehshold Level for Trigger audioInAct
volatile int sineLevel         = 0;   // 0 - 1 = mute - max
volatile int dialedNr          = 0;   // Stores the dialed Number
volatile int callState         = 0;   // Stores the State of Idle, Incoming Call, Call Active
volatile bool hookState        = 0;   // 1 = Receiver picked up
volatile bool audioInAct       = 0;   // 1 = There is an Audio Signal present from USB
volatile bool pulseState       = 0;   // Stores the Input State of the Dialing Input
volatile bool lastPulseState   = 0;   // Stores the Last Inpute State of the Dialing Input
volatile bool muteState        = 0;   // Stores the Input State of the Mute Button
volatile bool lastMuteState    = 0;   // Stores the Last Inpute State of the Mute Button
volatile bool dialing          = 0;   // 1 = Dialing Wheel is activated
volatile bool stopDialTone     = 0;   // 1 = The Reciver was picked up and a Number has been dialed
const long ringInterval        = 125; // Switch Interval for ringer
unsigned long previousMillis   = 0;   // Stores the State of the previous millisecond for ringer Timer

//Timers
elapsedMillis fps;
elapsedMillis debugPrintTimer;


void setup() {
  // Configuring the Audio Function and setup the SGTL500 Chip
  AudioMemory(12);
  sgtl5000_1.enable();
  sgtl5000_1.volume(0.6);
  sgtl5000_1.inputSelect(AUDIO_INPUT_MIC);
  sgtl5000_1.micGain(microGain); // Value in dB

  sine1.amplitude(sineLevel);
  sine1.frequency(440);
  sine1.phase(0);

  mixer1.gain(0, 1);    // USB Input Level Left
  mixer1.gain(1, 0.2);  // Line Tone Level Left
  mixer1.gain(2, 0.3);  // Mic Bleed Level Left
  mixer2.gain(0, 1);    // USB Input Level Right
  mixer2.gain(1, 0.2);  // Line Tone Level Right
  mixer2.gain(2, 0.3);  // Mic Bleed Level Right
  
  // Pin Mode Settings
  pinMode (hookNC, INPUT_PULLUP); 
  pinMode (dialRdy1, INPUT_PULLUP);
  pinMode (dialRdy2, INPUT_PULLUP);
  pinMode (pulse, INPUT_PULLUP);
  pinMode (mode, INPUT_PULLUP);
  pinMode (runLed, OUTPUT);
  pinMode (ringerL, OUTPUT);
  pinMode (ringerR, OUTPUT);

  // Turn on Onboard LED on Pin 13 to show the Device is running
  digitalWrite(runLed, HIGH);

  // Start Serial Print
  Serial.begin(9600);
}

void loop() {
  // read the PC's volume setting and set it to the Audio Processor
  float vol = usb1.volume();
  sgtl5000_1.volume(map(vol,0,1,0,0.8));

  // check if an audio signal coming in (is present) from USB
  if (fps > 48) {
    if (rms1.available() && rms2.available()) {
      fps = 0;
      uint8_t rms1Level = rms1.read() * 100;
      uint8_t rms2Level = rms1.read() * 100;
      if (rms1Level >= audioLvlTrsh && rms2Level >= audioLvlTrsh){
        audioInAct = 1;
      }
      else {audioInAct = 0;
      }
    }
  }

  // check if the hook is picked up
  if (digitalRead(hookNC) == LOW){
    hookState  = 1;
  }
  else { hookState = 0;
  }
  
  // check if the dialing wheel is active
  if (digitalRead(dialRdy1) == LOW && digitalRead(dialRdy2) == LOW){
    dialing  = 1;
  }
  else { dialing = 0;
  }

  // check which mode is selected
  if (digitalRead(mode) == LOW) {
    keyMode = 1;
  }
  else { keyMode = 0;
  }

  //check if the Dialer is turned  a bit while in a call to Mute the HeadPhone
  muteState = digitalRead(dialRdy1);
  if (muteState == LOW && lastMuteState == 1 && callState == 2){
    if (keyMode == 1){
      muteTeams();
    }
    if (keyMode == 0){
      muteZoom();
    }
  }
  lastMuteState = muteState;


  // Activate Dial Tone if the Receiver is picked up and no Audio is coming in from USB
  if ((hookState == 1 && dialing == 1) || (hookState == 1 && audioInAct == 1)){
    stopDialTone = 1;
  }
  if (hookState == 0 && stopDialTone == 1){
    stopDialTone = 0;
  } 
  if (hookState == 1 && audioInAct == 0 && stopDialTone == 0){
    sineLevel = 1;
  }
  else { sineLevel = 0;
  }
  sine1.amplitude(sineLevel);
  
  //Ring the Bells if the Reciver is placed on the Hook an Audio is coming in from USB
  if (hookState == 0 && audioInAct == 1){
    callState = 1;
    ringing();
  } else {
    digitalWrite(ringerL, LOW);
    digitalWrite(ringerR, LOW);
  }


  // Set Call State to idle (missed Call)
  if (callState == 1 && audioInAct == 0){
    callState = 0; 
  }
  
  // Set Call State to picked up
  if (callState == 1 && hookState == 1){
    if (keyMode == 1){
      acceptCallTeams();
    }
    if (keyMode == 0){
      acceptCallZoom();
    }
    callState = 2;
  }
  
  // Set Call Sate to end Call
  if (callState == 2 && hookState == 0){
    if (keyMode == 1){
      endCallTeams();
    }
    if (keyMode == 0){
      endCallZoom();
    }
    callState = 0;
  }
  
  // Read the dialed Number and send it out as Keyboard Command
  pulseState = digitalRead(pulse);
  if (pulseState == HIGH && lastPulseState == 0){
    dialedNr++;
  }
  lastPulseState = pulseState;

  if (dialing == 0 && dialedNr > 0){
    if (dialedNr == 10){
      dialedNr = 0;
    }
  Keyboard.press (DIALNRKEY[dialedNr]);
  delay(10);
  Keyboard.release(DIALNRKEY[dialedNr]);    
  dialedNr = 0;
  }

  // Debug
  if (debugPrintTimer > 500){
    Serial.print("sineLevel      ");
    Serial.println(sineLevel);
    Serial.print("dialedNr       ");
    Serial.println(dialedNr);
    Serial.print("callState      ");
    Serial.println(callState);
    Serial.print("hookState      ");
    Serial.println(hookState);
    Serial.print("audioInAct     ");
    Serial.println(audioInAct);
    Serial.print("pulseState     ");
    Serial.println(pulseState);
    Serial.print("lastPulseState ");
    Serial.println(lastPulseState);
    Serial.print("dialing        ");
    Serial.println(dialing);
    Serial.print("stopDialTone   ");
    Serial.println(stopDialTone);
    Serial.print("keyMode        ");
    Serial.println(keyMode);
    Serial.print("audio cpu:     ");
    Serial.println(AudioProcessorUsageMax());
    debugPrintTimer = 0;
    } //
  
}

// Functions
void ringing(){
  unsigned long currentMillis = millis();
  if (currentMillis - previousMillis >= ringInterval) {
    previousMillis = currentMillis;
    if (digitalRead(ringerL) == LOW) {
      digitalWrite(ringerL, HIGH);
      digitalWrite(ringerR, LOW);
    } else {
      digitalWrite(ringerL, LOW);
      digitalWrite(ringerR, HIGH);
      }
    }  
}


void muteTeams(){
  Keyboard.press(MODIFIERKEY_GUI);
  delay(5);
  Keyboard.press(MODIFIERKEY_SHIFT);
  delay(5);
  Keyboard.press(KEY_M);
  delay(5);
  Keyboard.release(KEY_M);
  delay(5);
  Keyboard.release(MODIFIERKEY_SHIFT);
  delay(5);
  Keyboard.release(MODIFIERKEY_GUI);
}


void muteZoom() {
  Keyboard.press(MODIFIERKEY_CTRL);
  delay(5);
  Keyboard.press(MODIFIERKEY_SHIFT);
  delay(5);
  Keyboard.press(KEY_M);
  delay(5);
  Keyboard.release(KEY_M);
  delay(5);
  Keyboard.release(MODIFIERKEY_SHIFT);
  delay(5);
  Keyboard.release(MODIFIERKEY_CTRL);    
}

void acceptCallTeams(){
  Keyboard.press(MODIFIERKEY_GUI);
  delay(5);
  Keyboard.press(MODIFIERKEY_SHIFT);
  delay(5);
  Keyboard.press(KEY_S);
  delay(5);
  Keyboard.release(KEY_S);
  delay(5);
  Keyboard.release(MODIFIERKEY_SHIFT);
  delay(5);
  Keyboard.release(MODIFIERKEY_GUI);
}


void acceptCallZoom() {
  Keyboard.press(MODIFIERKEY_CTRL);
  delay(5);
  Keyboard.press(MODIFIERKEY_SHIFT);
  delay(5);
  Keyboard.press(KEY_A);
  delay(5);
  Keyboard.release(KEY_A);
  delay(5);
  Keyboard.release(MODIFIERKEY_SHIFT);
  delay(5);
  Keyboard.release(MODIFIERKEY_CTRL);    
}

void endCallTeams(){
  Keyboard.press(MODIFIERKEY_GUI);
  delay(5);
  Keyboard.press(MODIFIERKEY_SHIFT);
  delay(5);
  Keyboard.press(KEY_H);
  delay(5);
  Keyboard.release(KEY_H);
  delay(5);
  Keyboard.release(MODIFIERKEY_SHIFT);
  delay(5);
  Keyboard.release(MODIFIERKEY_GUI);
}


void endCallZoom() {
  Keyboard.press(MODIFIERKEY_CTRL);
  delay(5);
  Keyboard.press(MODIFIERKEY_SHIFT);
  delay(5);
  Keyboard.press(KEY_E);
  delay(5);
  Keyboard.release(KEY_E);
  delay(5);
  Keyboard.release(MODIFIERKEY_SHIFT);
  delay(5);
  Keyboard.release(MODIFIERKEY_CTRL);    
}

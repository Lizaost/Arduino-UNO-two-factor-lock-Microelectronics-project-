#include <Servo.h>

#include <Wire.h>
#include <Keypad.h>

#include <deprecated.h>
#include <MFRC522.h>
#include <MFRC522Extended.h>
#include <require_cpp11.h>


int ledPin = A0;

int buzzerPin = 3;
int buzzerVolume = 0.05 * 255;  //10% of maximum
int buzzerOnTime = 300;  //in ms
int buzzerOffTime = 300;  //in ms
int buzzerCycles = 8;
int buzzerCyclesCounter = 0;
int buzzerActiveFlag = 0;
int buzzerStatusFlag = 0;
int buzzerStartTime = 0;
bool buzzerCyclesCounterLock = false;

int passwordLength = 5;
char inputData[5];
const char PASSWORD[5] = "1234";
byte inputCount = 0, masterCount = 0;
char keyPressed;

const byte ROWS = 4;
const byte COLS = 3;

char hexaKeys[ROWS][COLS] = {
  {'1', '2', '3'},
  {'4', '5', '6'},
  {'7', '8', '9'},
  {'*', '0', '#'}
};

byte rowPins[ROWS] = {8, 7, 6, 5};
byte colPins[COLS] = {4, 2, A3};

Keypad customKeypad = Keypad(makeKeymap(hexaKeys), rowPins, colPins, ROWS, COLS);


// RFID

int SS_PIN =  10;
int RST_PIN =  9;
MFRC522 mfrc522(SS_PIN, RST_PIN);

#define authorizedRFIDCardsNumber 1
char authorizedRFIDCards[authorizedRFIDCardsNumber][12] = {"D6 FF F9 48"};

//SERVO (lock control)

Servo servo;
int servoPin = A5;
int servoLockAngle = 0;
int servoOpenAngle = 90;


// TIMERS (AND DELAYS)

// 1 RFID tag in 1 second
bool RFIDLock = false;
float RFIDStartTime = 0;
float RFIDDelay = 1 * 1000;

// Password must be entered in 10 seconds after placeing correct RFID tag
bool passwordInputAllowed = false;
float passwordInputStartTime = 0;
float passwordInputPeriod = 10 * 1000;

// Automatically close the lock after 10 seconds
bool lockAutoCloseActive = false;
float lockAutoCloseStartTime = 0;
float lockAutoCloseDelay = 10 * 1000;


// Block lock after 3 sequential failed attepts
bool securityBlockActive = false;  //block for some time after series of failed attempts
float securityBlockPeriod = 20 * 1000;
float securityBlockStartTime = 0;

int failedAttemptsSequence = 0;
int failedAttemptsToBlock = 3;


// Turn led off for a short time when a key is pressed or rfid tag is read
bool keyPressedFlag = false;
bool keyPressedSignalFlag = false;
bool RFIDReadFlag = false;
bool RFIDReadSignalFlag = false;
int inputSignalLength = 700;
float inputSignalStart = 0;

// Indicate sequrity lock using led (1 second on, 1 second off)

// Indicate then the lock will be closed soon
int lockIsClosingSignalOnTime = 250;
int lockIsClosingSignalOffTime = 250;
float lockIsClosingSignalStartTime = 0;
int lockIsClosingSignalRemainedTime = 5*1000;  //start when 5 second remained before the lock will be closed


// FLAGS and COUNTERS

bool correctRFIDTag = false;
bool correctPassword = false;
bool attemptOver = false;
bool lockOpenedFlag = false;  // true after correct opening of the lock, prevent lock from openeng several times without closing
bool lockOpenStatus = false;  // status of lock





void setup() {
  //Serial.begin(9600);
  pinMode(ledPin, OUTPUT);

  SPI.begin();
  mfrc522.PCD_Init();

  servo.attach(servoPin);                                  
  servo.write(servoLockAngle);
}



void loop() {
  // IF LOCK IS NOT BLOCKED

  // RFID TAG
  if (mfrc522.PICC_IsNewCardPresent() and mfrc522.PICC_ReadCardSerial()) {
    if (checkRFIDtag() and securityBlockActive and !RFIDLock) {
      //Unlock security blocked lock using authorized rfid card
      securityBlockActive = false;
      RFIDStartTime = millis();
      RFIDLock = true;
      RFIDReadFlag = true;
      //Serial.println("The lock was unlocked by using an authorized RFID tag");
    }
    if (!securityBlockActive) {
      if (!RFIDLock and !attemptOver and !lockOpenStatus and !passwordInputAllowed) {
        RFIDReadFlag = true;
        RFIDStartTime = millis();
        RFIDLock = true;
        if (checkRFIDtag()) {
          //Serial.println("CORRECT RFID TAG");
          correctRFIDTag = true;
          attemptOver = false;
          passwordInputStartTime = millis();
          passwordInputAllowed = true;
          //Serial.println("Password input is now allowed for " + String(int(passwordInputPeriod / 1000)) + " seconds");
        } else {
          //Serial.println("INCORRECT RFID TAG");
          correctRFIDTag = false;
          attemptOver = true;
        }
      }
    } else {  //IF THE LOCK IS BLOCKED
      float blockTimeRemained = (securityBlockPeriod - (millis() - securityBlockStartTime)) / 1000;
      //Serial.println("Lock is blocked for security reasons for " + String(blockTimeRemained) + " more seconds");
    }
  }

  // Unlock rfid reading after RFIDDelay ms
  if (RFIDLock and millis() - RFIDStartTime > RFIDDelay) {
    RFIDLock = false;
  }

  // PASSWORD INPUT
  // IF LOCK IS NOT BLOCKED

  keyPressed = customKeypad.getKey();
  if (keyPressed) {
    keyPressedFlag = true;
    // IF KEY IS NOT BLOCKED
    if (!securityBlockActive) {
      if (passwordInputAllowed) {
        //Serial.println(keyPressed);
        //inputData[inputCount] = keyPressed;
        //inputCount++;
        inputSymbol(keyPressed);
      } else {
        //Serial.println("Password input is blocked until the correct RFID tag is placed on the lock");
      }
    } else {  //IF THE LOCK IS BLOCKED
      float blockTimeRemained = (securityBlockPeriod - (millis() - securityBlockStartTime)) / 1000;
      //Serial.println("Lock is blocked for sequrity reasons for " + String(blockTimeRemained) + " more seconds");
    }
  }



  // CHECK PASSWORD
  if (isPasswordInputted()) {
    if (passwordInputAllowed) {
      if (checkPassword()) {
        //Serial.println("Correct password");
        correctPassword = true;
      } else {
        //Serial.println("Wrong password");
        correctPassword = false;
      }
      //passwordInputStartTime = millis();
      attemptOver = true;
    } else {
      clearData();
      correctPassword = false;
    }
  }

  if (passwordInputAllowed and millis() - passwordInputStartTime > passwordInputPeriod) {
    passwordInputAllowed = false;
    //Serial.println("Password input is now blocked");
  }

  // If input attempt is over
  if (attemptOver) {
    if (correctRFIDTag and correctPassword) {
      lockOpenStatus = true;
      correctSignal();
      failedAttemptsSequence = 0;
    } else {
      lockOpenStatus = false;
      wrongSignal();
      failedAttemptsSequence++;
    }
    correctRFIDTag = false;
    correctPassword = false;
    attemptOver = false;
    passwordInputAllowed = false;
    //Serial.println("Failed attempts sequence = " + String(failedAttemptsSequence));
  }

  if (!lockOpenedFlag and lockOpenStatus) {
    openLock();
    lockAutoCloseActive = true;
    lockAutoCloseStartTime = millis();
    //Serial.println("Lock will be closed automatically after " + String(int(lockAutoCloseDelay / 1000)) + " seconds");
  }

  if (lockAutoCloseActive and millis() - lockAutoCloseStartTime > lockAutoCloseDelay) {
    lockAutoCloseActive = false;
    //Serial.println("The lock was automatically closed");
    closeLock();
  }


  //BLOCK PROCESSING END

  // LOCK SEQURITY BLOCK
  if (failedAttemptsSequence >= failedAttemptsToBlock) {
    securityBlockActive = true;
    failedAttemptsSequence = 0;
    securityBlockStartTime = millis();
    //Serial.println(securityBlockPeriod);
    //Serial.println("Too many failed attempts. The lock is blocked for " + String(securityBlockPeriod / 1000) + " seconds.");
  }

  if (securityBlockActive and millis() - securityBlockStartTime > securityBlockPeriod) {
    securityBlockActive = false;
    //Serial.println("The lock is unlocked");
  }


  // DAEMONS (update indicators and buzzer)
  audioSignalDaemon(1);
  lockIndicatorDaemon();
  ledDaemon();
  //TODO Signal thet the lock will be closed after 10 seconds (blink the led)
}


void openLock() {
  //Serial.println("LOCK IS OPEN");
  lockOpenedFlag = true;
  servo.write(servoOpenAngle); 
}

void closeLock() {
  //Serial.println("LOCK IS CLOSED");
  lockOpenedFlag = false;
  lockOpenStatus = false;
  servo.write(servoLockAngle); 
}

void inputSymbol(char symbol) {
  //Serial.println(symbol);
  inputData[inputCount] = symbol;
  inputCount++;
}

bool isPasswordInputted() {
  if (inputCount == passwordLength - 1) {
    return true;
  } else {
    return false;
  }
}

bool checkPassword() {
  bool isCorrectPassword = false;
  if (!strcmp(inputData, PASSWORD)) {
    isCorrectPassword = true;
  }
  clearData();
  return isCorrectPassword;
}


// Return true if an authorized rfid tag is placed,
// otherwise return false
bool checkRFIDtag() {
  //Serial.print("UID tag :");
  String content = "";
  byte letter;
  for (byte i = 0; i < mfrc522.uid.size; i++)
  {
    //Serial.print(mfrc522.uid.uidByte[i] < 0x10 ? " 0" : " ");
    //Serial.print(mfrc522.uid.uidByte[i], HEX);
    content.concat(String(mfrc522.uid.uidByte[i] < 0x10 ? " 0" : " "));
    content.concat(String(mfrc522.uid.uidByte[i], HEX));
  }
  //Serial.println();
  content.toUpperCase();

  for (int i = 0; i < authorizedRFIDCardsNumber; i++) {
    if (content.substring(1) == authorizedRFIDCards[i])
    {
      return true;
    } else {
      return false;
    }
  }
}

void correctSignal() {
  //Serial.println("Correct signal");
}

void wrongSignal() {
  //Serial.println("Wrong signal");
  buzzerActiveFlag = 1;
  buzzerStartTime = millis();
}

// DAEMONS (indicators controllers)

void lockIndicatorDaemon() {
  // The LED is active when the lock is closed
  if (lockOpenStatus) {
    digitalWrite(ledPin, LOW);
  } else {
    digitalWrite(ledPin, HIGH);
  }
}

// activate wrong signal on buzzer
void audioSignalDaemon(int mode) {
  if (buzzerActiveFlag == 1) {
    // Cycle = on + off
    // MODE 0 - _|-|_|-|_|-|_
    if (mode == 0) {
      int timeActive = millis() - buzzerStartTime;
      int cycleTime = timeActive % (buzzerOnTime + buzzerOffTime);
      if (cycleTime < buzzerOnTime) { //if buzzer is on
        if (buzzerCyclesCounter % 2 == 0) {
          analogWrite(buzzerPin, buzzerVolume);
        } else {
          analogWrite(buzzerPin, buzzerVolume * 2);
        }
        buzzerCyclesCounterLock = false;  // unlock cycles count
        buzzerStatusFlag = 1;
      } else {
        analogWrite(buzzerPin, LOW);
        if (!buzzerCyclesCounterLock) {
          buzzerCyclesCounter++;         // count this cycle
        }
        buzzerCyclesCounterLock = true;  // and lock cycles count until next cycle start
        buzzerStatusFlag = 0;
      }
      if (buzzerCyclesCounter == buzzerCycles) {
        buzzerActiveFlag = 0;
        buzzerCyclesCounter = 0;
      }
    }

    // MODE 1 - sinusoidal
    if (mode == 1) {
      int timeActive = millis() - buzzerStartTime;
      float period = (buzzerOnTime + buzzerOffTime) * 1.0;
      int cycleTime = timeActive % int(period);

      float t = 3.1415 * (cycleTime / period);
      int v = buzzerVolume * (1 + 2*sin(t));
      
      analogWrite(buzzerPin, v);
      if ((cycleTime/period) > 0.8) {
        if (!buzzerCyclesCounterLock) {
          buzzerCyclesCounter++;         // count this cycle
        }
        buzzerCyclesCounterLock = true;  // and lock cycles count until next cycle start
      }
      if ((cycleTime/period) < 0.2){
        buzzerCyclesCounterLock = false;
      }
      if (buzzerCyclesCounter == buzzerCycles) {
        buzzerActiveFlag = 0;
        buzzerCyclesCounter = 0;
        analogWrite(buzzerPin, LOW);
      }
    }
  }
}

void ledDaemon(){
  // turn led off for a short time when a key is pressed
  if (keyPressedFlag){
    digitalWrite(ledPin, LOW);
    keyPressedSignalFlag = true;
    keyPressedFlag = false;
    inputSignalStart = millis();
  }
  if (RFIDReadFlag){
    digitalWrite(ledPin, LOW);
    RFIDReadSignalFlag = true;
    RFIDReadFlag = false;
    inputSignalStart = millis();
  }
  if (keyPressedSignalFlag or RFIDReadSignalFlag){
    if (millis() - inputSignalStart > inputSignalLength){
      digitalWrite(ledPin, HIGH);
      keyPressedSignalFlag = false;
      RFIDReadSignalFlag = false;
    }
  }

  // 1s on, 1s off to indicate security lock
  if (securityBlockActive){
    int slActiveTime = millis() - securityBlockStartTime;
    if (slActiveTime % 2000 < 1000){  //2000ms - 2s - one cycle
      digitalWrite(ledPin, HIGH);
    } else {
      digitalWrite(ledPin, LOW);
    }
  }

  // blink fast when 5 seconds left before lock will be closed
  if (lockAutoCloseActive) {
    int timeBeforeLock = lockAutoCloseDelay - (millis() - lockAutoCloseStartTime);
    int period = lockIsClosingSignalOnTime + lockIsClosingSignalOffTime;
    if (timeBeforeLock < lockIsClosingSignalRemainedTime){
    //Serial.println(timeBeforeLock);
    //Serial.println(">>" + (lockIsClosingSignalRemainedTime - timeBeforeLock) % period);
      if ((lockIsClosingSignalRemainedTime - timeBeforeLock) % period <lockIsClosingSignalOnTime){
        digitalWrite(ledPin, HIGH);
      } else {
        digitalWrite(ledPin, LOW);
      }
    }
  }
}



// Clear input data array and reset input counter
void clearData() {
  while (inputCount != 0) {
    inputData[inputCount--] = 0;
  }
  return;
}

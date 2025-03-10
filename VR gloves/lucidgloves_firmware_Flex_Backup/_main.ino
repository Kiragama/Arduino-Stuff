#include <mutex>
#define ALWAYS_CALIBRATING CALIBRATION_LOOPS == -1

#define CALIB_OVERRIDE false
#if USING_CALIB_PIN && COMMUNICATION == COMM_SERIAL && PIN_CALIB == 0 && !CALIB_OVERRIDE
  #error "You can't set your calibration pin to 0 over usb. You can calibrate with the BOOT button when using bluetooth only. Set CalibOverride to true to override this."
#endif

bool calibrate = false;
bool calibButton = false;
int* fingerPos = (int[]){0,0,0,0,0,0,0,0,0,0};

ICommunication* comm;

#if ESP32_DUAL_CORE_SET //DUEL CORE ONLY
//std::mutex fingerPosMutex;
ordered_lock* fingerPosLock = new ordered_lock();
TaskHandle_t Task1;
int threadLoops = 1;
int totalLocks = 0;
int lastMicros = 0;
int fullLoopTime = 0;
int fullLoopTotal = 0;

void getInputs(void* parameter){ //This task is passed to another core, hense infinite loop. Gets finger pos
    for(;;){ //infinite loop
      fullLoopTime = micros() - lastMicros;
      fullLoopTotal += fullLoopTime;
      lastMicros = micros();
      {
        fingerPosLock->lock();
        totalLocks++;
        getFingerPositions(calibrate, calibButton); //Save finger positions in thread

        fingerPosLock->unlock();
      }
      threadLoops++;
      if (threadLoops%100 == 0){
        vTaskDelay(1); //keep watchdog fed
      }
      delayMicroseconds(1);
    }           
}
#endif//DUEL CORE ONLY

int loops = 0;
void setup() {
  #ifndef ESP32S3
  pinMode(32, INPUT_PULLUP);
  #endif
  pinMode(DEBUG_LED, OUTPUT); //was OUTPUT, mofified because calibration
  #ifdef NEOPIXEL //tempted
  neopixelWrite(DEBUG_LED,RGB_BRIGHTNESS,0,0); // Red 
  #else   
  digitalWrite(DEBUG_LED, HIGH); 
  #endif 
  #if COMMUNICATION == COMM_SERIAL
    comm = new SerialCommunication();
  #elif COMMUNICATION == COMM_BTSERIAL
    comm = new BTSerialCommunication();
  #elif COMMUNICATION == COMM_BLESERIAL
    comm = new BLESerialCommunication();
  #endif  
  comm->start();

  setupInputs(); //See input.ino

  #if USING_FORCE_FEEDBACK
    setupServoHaptics();  
  #endif
  
  #if ESP32_DUAL_CORE_SET
    xTaskCreatePinnedToCore(
      getInputs, /* Function to implement the task */
      "Get_Inputs", /* Name of the task */
      10000,  /* Stack size in words */
      NULL,  /* Task input parameter */
      tskIDLE_PRIORITY,  /* Priority of the task */
      &Task1,  /* Task handle. */
      0); /* Core where the task should run */
  #endif

 
}


int lastMainMicros = micros();
int mainMicros = 0;
int mainMicrosTotal = 0;
int mainloops = 1;

int target = 0;
bool latch = false;

void loop() {
  mainloops++;
  mainMicros = micros() - lastMainMicros;
  mainMicrosTotal += mainMicros;
  lastMainMicros = micros();

  if (!digitalRead(27)){ //27 does not exist on ESP32-S3-Wroom-1. Does exist on the Lucids as 2A27 (used in MUX) ... Seems to always be called
    if (!latch){
       target++;
       target %= 5; //The % (modulus operator) finds the remainder when target is divided by 5.

       latch = true;
    }
  }
  else
    latch = false; //This is a latch and allows for target to increase every other loop. Possibly be a reset latch when button released
  
  if (comm->isOpen()){
    #if USING_CALIB_PIN
    calibButton = getButton(PIN_CALIB) != INVERT_CALIB;
    //Serial.println(getButton(PIN_CALIB));
    if (calibButton)
      loops = 0;
    #else
    calibButton = false;
    #endif


    //bool calibrate = false;
    if (loops < CALIBRATION_LOOPS || ALWAYS_CALIBRATING){ //if loops is less then calibration loops OR always calibrating is true. This is the calibrate every x loops or always
      calibrate = true;
      loops++;
    }
    else{
      calibrate = false;
    }

    #if !ESP32_DUAL_CORE_SET //System operation if single core. Should not run
      getFingerPositions(calibrate, calibButton);
    #endif
    bool joyButton = getButton(PIN_JOY_BTN) != INVERT_JOY;

    #if TRIGGER_GESTURE
    bool triggerButton = triggerGesture(fingerPos);
    #else
    bool triggerButton = getButton(PIN_TRIG_BTN) != INVERT_TRIGGER;
    #endif

    bool aButton = getButton(PIN_A_BTN) != INVERT_A;
    bool bButton = getButton(PIN_B_BTN) != INVERT_B;

    #if GRAB_GESTURE
    bool grabButton = grabGesture(fingerPos);
    #else
    bool grabButton = getButton(PIN_GRAB_BTN) != INVERT_GRAB;
    #endif

    #if PINCH_GESTURE
    bool pinchButton = pinchGesture(fingerPos);
    #else
    bool pinchButton = getButton(PIN_PNCH_BTN) != INVERT_PINCH;
    #endif

    int fingerPosCopy[10];
    int mutexTimeDone;
    bool menuButton = getButton(PIN_MENU_BTN) != INVERT_MENU;
    {
      #if ESP32_DUAL_CORE_SET
      int mutexTime = micros();
      //const std::lock_guard<std::mutex> lock(fingerPosMutex);
      fingerPosLock->lock();
      mutexTimeDone = micros()-mutexTime;
      #endif
      //memcpy(fingerPosCopy, fingerPos, sizeof(fingerPos));
      for (int i = 0; i < 10; i++){
        fingerPosCopy[i] = fingerPos[i];
      }
      #if ESP32_DUAL_CORE_SET
      fingerPosLock->unlock();
      #endif
      
    }

    comm->output(encode(fingerPosCopy, getJoyX(), getJoyY(), joyButton, triggerButton, aButton, bButton, grabButton, pinchButton, calibButton, menuButton));
    #if USING_FORCE_FEEDBACK
      char received[100];
      if (comm->readData(received)){
        int hapticLimits[5];
        //This check is a temporary hack to fix an issue with haptics on v0.5 of the driver, will make it more snobby code later
        if(String(received).length() >= 5) {
           decodeData(received, hapticLimits);
           writeServoHaptics(hapticLimits); 
        }
      }
    #endif
    delay(LOOP_TIME);
  }
}

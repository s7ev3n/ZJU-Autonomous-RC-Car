/* ---------------------------------------------------------------------------
 # Licensing Information: You are free to use or extend these projects for
 # education or reserach purposes provided that (1) you retain this notice
 # and (2) you provide clear attribution to UC Berkeley, including a link
 # to http://barc-project.com
 #
 # Attibution Information: The barc project ROS code-base was developed
 # at UC Berkeley in the Model Predictive Control (MPC) lab by Jon Gonzales
 # (jon.gonzales@berkeley.edu)  Development of the web-server app Dator was
 # based on an open source project by Bruce Wootton, with contributions from
 # Kiet Lam (kiet.lam@berkeley.edu). The RC Input code was based on sample code
 # from http://rcarduino.blogspot.com/2012/04/how-to-read-multiple-rc-channels-draft.html
 # --------------------------------------------------------------------------- */


/* ---------------------------------------------------------------------------
 WARNING:
 * Be sure to have all ultrasound sensors plugged in, otherwise the pins may get stuck in
 some float voltage
 # --------------------------------------------------------------------------- */

// include libraries
//#define USE_USBCON
#include <ros.h>
#include <smartcar/Encoder.h>
#include <smartcar/ECU_PWM.h>
#include <Servo.h>
//#include "Maxbotix.h"
#include <EnableInterrupt.h>


class Car {
public:
  void initEncoders();
  void initRCInput();
  void initActuators();
  void armActuators();
  void writeToActuators(const smartcar::ECU_PWM& ecu);
  // Used for copying variables shared with interrupts to avoid read/write
  // conflicts later
  void readAndCopyInputs();
  // Getters
  uint16_t getRCThrottle();
  uint16_t getRCSteering();
  int getEncoderFL();
  int getEncoderFR();
  int getEncoderBL();
  int getEncoderBR();
  // Interrupt service routines
  void incFR();
  void incFL();
  void incBR();
  void incBL();
  void calcThrottle();
  void calcSteering();
private:
  // Pin assignments
  const int ENC_FL_PIN = 2;
  const int ENC_FR_PIN = 3;
  const int ENC_BL_PIN = 5;
  const int ENC_BR_PIN = 6;
  const int THROTTLE_PIN = 7;
  const int STEERING_PIN = 8;
  const int MOTOR_PIN = 10;
  const int SERVO_PIN= 11;

  // Car properties
  // unclear what this is for
  const int noAction = 0;

  // Motor limits
  const int MOTOR_MAX = 1900;
  const int MOTOR_MIN = 1100;
  const int MOTOR_NEUTRAL = 1500;
  // Optional: smaller values for testing safety

  // Steering limits
  const int D_THETA_MAX = 400;
  const int THETA_CENTER = 1500;
  const int THETA_MAX = THETA_CENTER + D_THETA_MAX;
  const int THETA_MIN = THETA_CENTER - D_THETA_MAX;

  // Interfaces to motor and steering actuators
  Servo motor;
  Servo steering;

  // Utility variables to handle RC and encoder inputs
  volatile uint8_t updateFlagsShared;
  uint8_t updateFlags;
  const int THROTTLE_FLAG = 1;
  const int STEERING_FLAG = 2;
  const int FL_FLAG = 3;
  const int FR_FLAG = 4;
  const int BL_FLAG = 5;
  const int BR_FLAG = 6;

  uint16_t throttleStart;
  uint16_t steeringStart;
  volatile uint16_t throttleInShared;
  volatile uint16_t steeringInShared;
  uint16_t throttleIn = 1500;
  uint16_t steeringIn = 1500;

  // Number of encoder counts on tires
  // count tick on {FL, FR, BL, BR}
  // F = front, B = back, L = left, R = right
  volatile int FL_count_shared = 0;
  volatile int FR_count_shared = 0;
  volatile int BL_count_shared = 0;
  volatile int BR_count_shared = 0;
  int FL_count = 0;
  int FR_count = 0;
  int BL_count = 0;
  int BR_count = 0;

  // Utility functions
  uint8_t microseconds2PWM(uint16_t microseconds);
  float saturateMotor(int x);
  float saturateServo(int x);
};

// Initialize an instance of the Car class as car
Car car;

// Callback Functions
// These are really sad solutions to the fact that using class member functions
// as callbacks is complicated in C++ and I haven't figured it out. If you can
// figure it out, please atone for my sins.
void ecuCallback(const smartcar::ECU_PWM& ecu) {
  car.writeToActuators(ecu);
}
void incFLCallback() {
  car.incFL();
}
void incFRCallback() {
  car.incFR();
}
void incBLCallback() {
  car.incBL();
}
void incBRCallback() {
  car.incBR();
}
void calcSteeringCallback() {
  car.calcSteering();
}
void calcThrottleCallback() {
  car.calcThrottle();
}

// Variables for time step
volatile unsigned long dt;
volatile unsigned long t0;

// Global message variables
// Encoder, RC Inputs, Electronic Control Unit, Ultrasound
smartcar::ECU_PWM ecu;
smartcar::ECU_PWM rc_inputs;
smartcar::Encoder encoder;


ros::NodeHandle nh;

ros::Publisher pub_encoder("encoder", &encoder);
ros::Publisher pub_rc_inputs("rc_inputs", &rc_inputs);
ros::Subscriber<smartcar::ECU_PWM> sub_ecu("ecu_pwm", ecuCallback);


// Set up ultrasound sensors
/*
Maxbotix us_fr(14, Maxbotix::PW, Maxbotix::LV); // front
 Maxbotix us_bk(15, Maxbotix::PW, Maxbotix::LV); // back
 Maxbotix us_rt(16, Maxbotix::PW, Maxbotix::LV); // right
 Maxbotix us_lt(17, Maxbotix::PW, Maxbotix::LV); // left
 */

/**************************************************************************
 * ARDUINO INITIALIZATION
 **************************************************************************/
void setup()
{
  // Set up encoders, rc input, and actuators
  car.initEncoders();
  car.initRCInput();
  car.initActuators();

  // Start ROS node
  nh.getHardware()->setBaud(57600);
  nh.initNode();

  // Publish and subscribe to topics
  nh.advertise(pub_encoder);
  nh.advertise(pub_rc_inputs);
  nh.subscribe(sub_ecu);

  // Arming ESC, 1 sec delay for arming and ROS
  car.armActuators();
  t0 = millis();

}


/**************************************************************************
 * ARDUINO MAIN lOOP
 **************************************************************************/
void loop() {
  // compute time elapsed (in ms)
  dt = millis() - t0;

  if (dt > 50) {
    car.readAndCopyInputs();

    // TODO make encoder and rc_inputs private properties on car? Then
    // car.publishEncoder(); and car.publishRCInputs();
    encoder.FL = car.getEncoderFL();
    encoder.FR = car.getEncoderFR();
    encoder.BL = car.getEncoderBL();
    encoder.BR = car.getEncoderBR();
    pub_encoder.publish(&encoder);

    rc_inputs.motor = car.getRCThrottle();
    rc_inputs.servo = car.getRCSteering();
    pub_rc_inputs.publish(&rc_inputs);

    // publish ultra-sound measurement
    /*
    ultrasound.front = us_fr.getRange();
     ultrasound.back = us_bk.getRange();
     ultrasound.right = us_rt.getRange();
     ultrasound.left = us_lt.getRange();
     pub_ultrasound.publish(&ultrasound);
     */
    t0 = millis();
  }

  nh.spinOnce();
}

/**************************************************************************
 * CAR CLASS IMPLEMENTATION
 **************************************************************************/
float Car::saturateMotor(int x) {
  if (x  == noAction ){ 
    return MOTOR_NEUTRAL; 
  }

  if (x  >  MOTOR_MAX) {
    x = MOTOR_MAX;
  } 
  else if (x < MOTOR_MIN) {
    x = MOTOR_MIN;
  }
  return x;
}

float Car::saturateServo(int x) {
  if (x  == noAction ) {
    return THETA_CENTER;
  }

  if (x  >  THETA_MAX) {
    x = THETA_MAX;
  }
  else if (x < THETA_MIN) {
    x = THETA_MIN;
  }
  return x;
}

void Car::initEncoders() {
  pinMode(ENC_FR_PIN, INPUT_PULLUP);
  pinMode(ENC_FL_PIN, INPUT_PULLUP);
  pinMode(ENC_BR_PIN, INPUT_PULLUP);
  pinMode(ENC_BL_PIN, INPUT_PULLUP);
  enableInterrupt(ENC_FR_PIN, incFRCallback, CHANGE);
  enableInterrupt(ENC_FL_PIN, incFLCallback, CHANGE);
  enableInterrupt(ENC_BR_PIN, incBRCallback, CHANGE);
  enableInterrupt(ENC_BL_PIN, incBLCallback, CHANGE);
}

void Car::initRCInput() {
  pinMode(THROTTLE_PIN, INPUT_PULLUP);
  pinMode(STEERING_PIN, INPUT_PULLUP);
  enableInterrupt(THROTTLE_PIN, calcThrottleCallback, CHANGE);
  enableInterrupt(STEERING_PIN, calcSteeringCallback, CHANGE);
}

void Car::initActuators() {
  motor.attach(MOTOR_PIN);
  steering.attach(SERVO_PIN);
}

void Car::armActuators() {
  motor.writeMicroseconds(MOTOR_NEUTRAL);
  steering.writeMicroseconds(THETA_CENTER);
  delay(1000);
}

void Car::writeToActuators(const smartcar::ECU_PWM& ecu) {

  int motorCMD = ecu.motor;
  int servoCMD = ecu.servo;
  motor.writeMicroseconds(motorCMD);
  steering.writeMicroseconds(servoCMD);

}

uint8_t Car::microseconds2PWM(uint16_t microseconds) {
  // Scales RC pulses from 1000 - 2000 microseconds to 0 - 180 PWM angles
  uint16_t pwm = (microseconds - 1000.0)/1000.0*180;
  return static_cast<uint8_t>(pwm);
}

void Car::calcThrottle() {
  if(digitalRead(THROTTLE_PIN) == HIGH) {
    // rising edge of the signal pulse, start timing
    throttleStart = micros();
  } 
  else {
    // falling edge, calculate duration of throttle pulse
    throttleInShared = (uint16_t)(micros() - throttleStart);
    // set the throttle flag to indicate that a new signal has been received
    updateFlagsShared |= THROTTLE_FLAG;
  }
}

void Car::calcSteering() {
  if(digitalRead(STEERING_PIN) == HIGH) {
    steeringStart = micros();
  } 
  else {
    steeringInShared = (uint16_t)(micros() - steeringStart);
    updateFlagsShared |= STEERING_FLAG;
  }
}

void Car::incFL() {
  FL_count_shared++;
  updateFlagsShared |= FL_FLAG;
}

void Car::incFR() {
  FR_count_shared++;
  updateFlagsShared |= FR_FLAG;
}

void Car::incBL() {
  BL_count_shared++;
  updateFlagsShared |= BL_FLAG;
}

void Car::incBR() {
  BR_count_shared++;
  updateFlagsShared |= BR_FLAG;
}

void Car::readAndCopyInputs() {
  // check shared update flags to see if any channels have a new signal
  if (updateFlagsShared) {
    // Turn off interrupts, make local copies of variables set by interrupts,
    // then turn interrupts back on. Without doing this, an interrupt could
    // update a shared multibyte variable while the loop is in the middle of
    // reading it
    noInterrupts();
    // make local copies
    updateFlags = updateFlagsShared;
    if(updateFlags & THROTTLE_FLAG) {
      throttleIn = throttleInShared;
    }
    if(updateFlags & STEERING_FLAG) {
      steeringIn = steeringInShared;
    }
    if(updateFlags & FL_FLAG) {
      FL_count = FL_count_shared;
    }
    if(updateFlags & FR_FLAG) {
      FR_count = FR_count_shared;
    }
    if(updateFlags & BL_FLAG) {
      BL_count = BL_count_shared;
    }
    if(updateFlags & BR_FLAG) {
      BR_count = BR_count_shared;
    }
    // clear shared update flags and turn interrupts back on
    updateFlagsShared = 0;
    interrupts();
  }
}

uint16_t Car::getRCThrottle() {
  return throttleIn;
//return microseconds2PWM(throttleIn);
}
uint16_t Car::getRCSteering() {
  return steeringIn;
//return microseconds2PWM(steeringIn);
}

int Car::getEncoderFL() {
  return FL_count;
}
int Car::getEncoderFR() {
  return FR_count;
}
int Car::getEncoderBL() {
  return BL_count;
}
int Car::getEncoderBR() {
  return BR_count;
}


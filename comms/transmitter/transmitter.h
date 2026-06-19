#ifndef TRANSMITTER_H
#define TRANSMITTER_H



typedef enum Instruction_ {
  CHANGE_SPEED, 
  TURN_LEFT_FLIPPER,
  TURN_RIGHT_FLIPPER,
  TURN_TOP_FLIPPER,
  CALIBRATE_ANGLES
} Instruction;



typedef struct ControllerData_ {
  double leftJoystick;
  double rightJoystick;

  bool dPadUp;
  bool dPadDown;




} ControllerData;

#endif

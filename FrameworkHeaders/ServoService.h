
#ifndef SERVO_SERVICE_H
#define SERVO_SERVICE_H

#include <stdint.h>
#include <stdbool.h>
#include "ES_Configure.h"
#include "ES_Events.h"

// Initialize the Servo Service
bool InitServoService(uint8_t Priority);

// Post events to the Servo Service
bool PostServoService(ES_Event_t ThisEvent);

// Run the Servo Service
ES_Event_t RunServoService(ES_Event_t ThisEvent);

#endif

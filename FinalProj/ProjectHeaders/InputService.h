#ifndef INPUT_SERVICE_H
#define INPUT_SERVICE_H

#include <stdint.h>
#include <stdbool.h>
#include "ES_Framework.h"

// Framework service functions
bool InitInputService(uint8_t Priority);
bool PostInputService(ES_Event_t ThisEvent);
ES_Event_t RunInputService(ES_Event_t ThisEvent);

// Latest sensor readings
uint16_t QueryPotValue(void);
uint16_t QueryIRValue(void);

// Button states
bool QueryStartButton(void);
bool QueryCarrotButton(void);

#endif

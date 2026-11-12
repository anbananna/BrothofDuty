#ifndef GAME_SERVICE_H
#define GAME_SERVICE_H

#include <stdint.h>
#include <stdbool.h>

#include "ES_Framework.h"

// Public service functions
bool InitGameService(uint8_t Priority);
bool PostGameService(ES_Event_t ThisEvent);
ES_Event_t RunGameService(ES_Event_t ThisEvent);

// Game state query
uint8_t QueryGameProgress(void);

#endif

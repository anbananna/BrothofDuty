#ifndef MESSAGE_SERVICE_H
#define MESSAGE_SERVICE_H

#include <stdint.h>
#include <stdbool.h>
#include "ES_Framework.h"

bool InitMessageService(uint8_t Priority);
bool PostMessageService(ES_Event_t ThisEvent);
ES_Event_t RunMessageService(ES_Event_t ThisEvent);

#endif // MESSAGE_SERVICE_H

#ifndef LED_SERVICE_H
#define LED_SERVICE_H

#include <stdint.h>
#include <stdbool.h>
#include "ES_Framework.h"

bool InitLEDService(uint8_t Priority);
bool PostLEDService(ES_Event_t ThisEvent);
bool PostLEDDisplayNumber(uint8_t Number);
bool PostLEDCountdownStep(uint8_t Remaining);
bool PostLEDDisplayGo(void);
bool PostLEDClearDisplay(void);
bool PostLEDShowCongrats(bool Visible);
ES_Event_t RunLEDService(ES_Event_t ThisEvent);

#endif // LED_SERVICE_H
#ifndef LED_SERVICE_H
#define LED_SERVICE_H

#include <stdint.h>
#include <stdbool.h>
#include "ES_Framework.h"

bool InitLEDService(uint8_t Priority);
bool PostLEDService(ES_Event_t ThisEvent);
bool PostLEDDisplayNumber(uint8_t Number);
// Shows Number with a progress bar on the bottom 2 rows (4 columns per task, 0-8)
bool PostLEDDisplayTimer(uint8_t Number, uint8_t TasksComplete);
bool PostLEDDisplayTimerWithVisibility(
    uint8_t Number,
    uint8_t TasksComplete,
    bool TimerVisible
);
bool PostLEDCountdownStep(uint8_t Remaining);
bool PostLEDDisplayGo(void);
bool PostLEDDisplayLoss(void);
bool PostLEDClearDisplay(void);
bool PostLEDShowCongrats(bool Visible);
ES_Event_t RunLEDService(ES_Event_t ThisEvent);

#endif // LED_SERVICE_H
/****************************************************************************
 Module
     GameService.c

 Description
     Main game state machine for Karl's Kitchen.
****************************************************************************/

// Module header
#include "GameService.h"

// Framework
#include "ES_Configure.h"
#include "ES_Framework.h"
#include "ES_DeferRecall.h"

// Hardware / other services
#include "LEDService.h"
#include "ES_Port.h"
#include "dbprintf.h"
#include "InputService.h"
#include "ServoService.h"
#include "NeoPixelService.h"

// C libraries
#include <xc.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>


/*******************************
   Module Defines
 *******************************/
#define MAX_GAME_PROGRESS       8
#define GAME_DURATION           60000
#define GAME_TIMER_TICK         1000U
#define INACTIVITY_DURATION     20000
#define INTERACTION_DURATION    2000
#define COUNTDOWN_DURATION      1000    // ms per countdown step
#define COUNTDOWN_SECONDS       3

// Carrot task
#define CARROT_PRESSES_MIN      2U
#define CARROT_PRESSES_MAX      5U
#define CARROT_BLINK_DURATION   500U
#define CARROT_SERVO_OPEN_DURATION 500U
#define CARROT_SERVO_CLOSE_DURATION 500U
#define CARROT_LED              LATAbits.LATA3
#define CARROT_LED_TRIS         TRISAbits.TRISA3
#define LED_ON                  1
#define LED_OFF                 0

#define RESTART_DISPLAY_OFF_DURATION 2000U
#define RESTART_SCROLL_INTERVAL     250U
#define RESTART_SCROLL_PADDING      8U

#define WELCOME_DISPLAY_OFF_DURATION 2000U
#define WELCOME_SCROLL_INTERVAL     250U
#define WELCOME_SCROLL_PADDING      8U

// Start button LED (the switch itself is on RB3)
#define START_LED               LATBbits.LATB10
#define START_LED_TRIS          TRISBbits.TRISB10

// Stir pot task target ranges across the 10-bit ADC's usable range
#define SOUP_STIR_SPEED         80      // percent, -100..100
#define SOUP_SHAKE_DURATION     200    // ms to shake the soup
// Shake speed scales with distance from the target: just outside the window
// shakes at MIN, the farthest possible pot position shakes at MAX
#define SOUP_SHAKE_MIN_SPEED    20      // percent
#define SOUP_SHAKE_MAX_SPEED    SOUP_STIR_SPEED

#define HEAT_RISE_TARGET        150     // counts above ambient at the right height
#define HEAT_TOLERANCE_COUNTS   20

#define HEAT_TARGET_VARIANT_COUNT 3U
#define HEAT_TARGET_STEP_COUNTS   40U
#define SENSOR_PRINT_PERIOD     500     // ms between IR readouts during the task
#define HEAT_SERIAL_READOUT     1       // set 0 to stop printing IR values

static const uint16_t HeatTargetRiseOptions[HEAT_TARGET_VARIANT_COUNT] =
{
    HEAT_RISE_TARGET - HEAT_TARGET_STEP_COUNTS,
    HEAT_RISE_TARGET,
    HEAT_RISE_TARGET + HEAT_TARGET_STEP_COUNTS
};

// Pause between finishing one task and starting the next
#define TASK_GAP_DURATION       500

#define WIN_CELEBRATION_DURATION 5000U
#define WIN_ANIMATION_STEP       100U
#define WIN_TOGGLE_INTERVAL_STEPS 5U
#define WIN_ANIMATION_TOTAL_STEPS \
    (WIN_CELEBRATION_DURATION / WIN_ANIMATION_STEP)
#define WIN_RAINBOW_PHASE_STEP   8U
#define WIN_CELEBRATION_TIMER    GAME_SERVICE_TIMER

#define DISPENSE_TIMEOUT         5000U


/*******************************
   State Definitions
 *******************************/
typedef enum
{
    GAME_INITIALIZING,
    GAME_WELCOME,
    GAME_COUNTDOWN,
    GAME_SELECT_TASK,
    GAME_CUT_CARROT,
    GAME_STIR_POT,
    GAME_HEAT_COIL,
    GAME_WINNING,
    GAME_RESTARTING,
    GAME_LOSING,
    GAME_INACTIVE,
    GAME_TEST
} GameState_t;


/*******************************
   Task Definitions
 *******************************/
typedef enum
{
    TASK_CUT_CARROT = 0,
    TASK_STIR_POT,
    TASK_HEAT_COIL,
    TASK_COUNT
} GameTask_t;

typedef enum
{
    STIR_TARGET_LOW = 0,
    STIR_TARGET_MID,
    STIR_TARGET_HIGH,
    STIR_TARGET_COUNT
} StirTarget_t;

typedef struct
{
    uint16_t Low;
    uint16_t High;
} StirTargetRange_t;

static const StirTargetRange_t StirTargetRanges[STIR_TARGET_COUNT] =
{
    { 225U, 287U },
    { 481U, 543U },
    { 737U, 799U }
};


/*******************************
   Module Variables
 *******************************/
static uint8_t MyPriority;

static GameState_t CurrentState = GAME_INITIALIZING;
static GameTask_t CurrentTask;

static uint8_t GameProgress = 0;
static uint8_t GameTime = GAME_DURATION / 1000;
static uint8_t CountdownRemaining = 0;
static bool CountdownShowingGo = false;
static bool GameTimerActive = false;

static GameTask_t LastTask = TASK_COUNT;

static uint8_t TargetButtonPresses = 0;
static uint8_t DetectedButtonPresses = 0;
static uint8_t CarrotBlinksRemaining = 0;
static bool CarrotBlinkOn = false;
static bool CarrotInputEnabled = false;
static bool CarrotServoOpening = false;
static bool CarrotServoClosing = false;

static const char RestartingMessage[] = "restarting....";
static const char WelcomeMessage[] = "Welcome to broth of duty!";
static uint8_t RestartScrollIndex = 0;
static uint8_t WelcomeScrollIndex = 0;
static bool LossDisplayed = false;

static uint16_t DetectedPotValue = 0;
static StirTarget_t CurrentStirTarget;
static StirTarget_t LastStirTarget = STIR_TARGET_COUNT;
static bool SoupStirring = false;
static bool SoupShaking = false;        // pot out of range: servo shakes
static bool SoupShakeForward = true;    // current shake direction
static bool CoilHeating = false;
static uint16_t HeatBaseline = 0;       // ambient IR reading at task start

static uint16_t TargetHandValue = 0;
static uint16_t DetectedHandValue = 0;

static uint16_t TestCarrotPressCount = 0;
static uint16_t TestPotValue = 0;
static uint16_t TestIRValue = 0;
static uint16_t TestBeamBreak = 0;

// Dispense servo phases
typedef enum
{
    DISPENSE_IDLE,          // servo off
    DISPENSE_PRIMING,       // init spin until the beam is established
    DISPENSE_WAIT_BREAK,    // spin until a treat breaks the beam
    DISPENSE_WAIT_RESTORE   // then until the beam is restored
} DispensePhase_t;

static DispensePhase_t DispensePhase = DISPENSE_IDLE;

static bool WinCelebrationActive = false;
static bool WinCarrotOpen = false;
static bool WinCongratsVisible = false;
static uint8_t WinAnimationSteps = 0;
static uint8_t WinRainbowPhase = 0;

/*******************************
   Private Function Prototypes
 *******************************/
static void InitHWPins(void);
static void StartNewGame(void);
static void AssignNewTask(void);
static void UpdateGameProgress(void);
static void CompleteTask(void);
static void EnterWelcomeMode(void);
static void EnterCarrotTask(void);
static void StartCarrotBlinkSequence(void);
static void EnterStirTask(void);
static void UpdateSoupStirring(uint16_t PotReading);
static void StartSoupShake(void);
static void StopSoupShake(void);
static int16_t ShakeSpeedForPot(uint16_t PotReading);
static void SetSoupSpeed(int16_t Speed);
static void PrimeDispenser(void);
static void StartDispense(void);
static void UpdateDispense(ES_Event_t ThisEvent);
static void StopDispense(void);
static void EnterHeatTask(void);
static uint16_t HeatWindowLow(void);
static uint16_t HeatWindowHigh(void);
static void UpdateCoilHeating(uint16_t IRReading);
static void PostServoCommand(ES_EventType_t Command);
static void StartWinCelebration(void);
static void UpdateWinCelebration(void);
static void UpdateGameTimer(void);
static void StopGameTimers(void);

/*******************************
   Initialization
 *******************************/
bool InitGameService(uint8_t Priority)
{
    ES_Event_t ThisEvent;

    MyPriority = Priority;

    // Initialize game variables
    GameProgress = 0;
    GameTime = GAME_DURATION / 1000U;

    // Initialize hardware pins
    InitHWPins();

    // Start in welcome state
    CurrentState = GAME_INITIALIZING;

    ThisEvent.EventType = ES_INIT;
    ThisEvent.EventParam = 0;

    return ES_PostToService(MyPriority, ThisEvent);
}

/*******************************
   Post Function
 *******************************/
bool PostGameService(ES_Event_t ThisEvent)
{
    return ES_PostToService(MyPriority, ThisEvent);
}

/*******************************
   Run Function
 *******************************/
ES_Event_t RunGameService(ES_Event_t ThisEvent)
{
    ES_Event_t ReturnEvent;

    //DB_printf("GameService: State=%d, Event=%d, Param=%u\r\n",CurrentState,ThisEvent.EventType,ThisEvent.EventParam);

    ReturnEvent.EventType = ES_NO_EVENT;
    ReturnEvent.EventParam = 0;

    if ((ThisEvent.EventType == ES_TIMEOUT) &&
        (ThisEvent.EventParam == GAME_TIMER) &&
        GameTimerActive)
    {
        UpdateGameTimer();
    }

    UpdateDispense(ThisEvent);

    switch (CurrentState)
    {
        case GAME_INITIALIZING:
            if (ThisEvent.EventType == ES_INIT)
            {
                PrimeDispenser();
                EnterWelcomeMode();
                CurrentState = GAME_WELCOME;
                DB_printf("GAME INITIALIZED\r\n");
            }
            break;

        case GAME_WELCOME:
            // DB_printf("In GAME WELCOME\r\n");
            switch (ThisEvent.EventType)
            {
                case ES_TIMEOUT:
                    // Scroll the welcome message on repeat until start is pressed
                    if (ThisEvent.EventParam == GAME_SERVICE_TIMER)
                    {
                        const uint8_t MessageLength =
                            (uint8_t)(sizeof(WelcomeMessage) - 1U);
                        const uint8_t TotalCharacters =
                            (uint8_t)(MessageLength + WELCOME_SCROLL_PADDING);

                        if (WelcomeScrollIndex < TotalCharacters)
                        {
                            ES_Event_t LEDCharEvent;
                            LEDCharEvent.EventType = ES_LED_ADD_CHAR;
                            LEDCharEvent.EventParam =
                                (WelcomeScrollIndex < MessageLength) ?
                                (uint8_t)WelcomeMessage[WelcomeScrollIndex] : ' ';

                            if (PostLEDService(LEDCharEvent))
                            {
                                WelcomeScrollIndex++;
                            }
                            else
                            {
                                DB_printf("ERROR: Could not post welcome message character\r\n");
                            }
                            ES_Timer_InitTimer(GAME_SERVICE_TIMER,
                                               WELCOME_SCROLL_INTERVAL);
                        }
                        else
                        {
                            // Message fully scrolled off: pause, then repeat
                            WelcomeScrollIndex = 0;
                            ES_Timer_InitTimer(GAME_SERVICE_TIMER,
                                               WELCOME_DISPLAY_OFF_DURATION);
                        }
                    }
                    break;
                case ES_INPUT_START_PRESSED:
                    DB_printf("GAME STARTED\r\n");
                    START_LED = LED_OFF;
                    StartNewGame();

                    // Begin countdown
                    CountdownRemaining = COUNTDOWN_SECONDS;
                    CountdownShowingGo = false;
                    DB_printf("COUNTDOWN: %u\r\n", CountdownRemaining);
                    if (!PostLEDCountdownStep(CountdownRemaining))
                    {
                        DB_printf("ERROR: Could not display game countdown\r\n");
                    }
                    ES_Timer_InitTimer(SEQUENCE_TIMER, COUNTDOWN_DURATION);
                    CurrentState = GAME_COUNTDOWN;
                    break;

                case ES_NEW_KEY:
                {
                    // Preserve the original key before any deferral modifications
                    unsigned char Key = (unsigned char)ThisEvent.EventParam;
                    DB_printf("ES_NEW_KEY received with -> %c <- in Service 0\r\n",
                            Key);

                    if (Key == 't')
                    {
                        CurrentState = GAME_TEST;

                        TestCarrotPressCount = 0;

                        TestPotValue = QueryPotValue();
                        TestIRValue = QueryIRValue();
                        TestBeamBreak = QueryBeamBroken();

                        DB_printf("\r\n");
                        DB_printf("============================\r\n");
                        DB_printf("       INPUT TEST MODE      \r\n");
                        DB_printf("============================\r\n");

                        DB_printf("Initial POT: %u\r\n", TestPotValue);
                        DB_printf("Initial IR: %u\r\n", TestIRValue);
                        DB_printf("Beam Break: %u\r\n", TestBeamBreak);

                        DB_printf("NeoPixels: r/g/b/w = dim color, o = off\r\n");
                        DB_printf("Press X to exit test mode\r\n");
                    }
                    break;
                }

                default:
                    break;
            }
            break;

        case GAME_COUNTDOWN:
            if ((ThisEvent.EventType == ES_TIMEOUT) &&
                (ThisEvent.EventParam == SEQUENCE_TIMER))
            {
                if (CountdownShowingGo)
                {
                    CountdownShowingGo = false;
                    CurrentState = GAME_SELECT_TASK;

                    GameTime = GAME_DURATION / 1000U;
                    GameTimerActive = true;
                    DB_printf("GAME TIMER STARTED: %u\r\n", GameTime);
                    if (!PostLEDDisplayTimer(GameTime, GameProgress))
                    {
                        DB_printf("ERROR: Could not display game timer\r\n");
                    }
                    ES_Timer_InitTimer(GAME_TIMER, GAME_TIMER_TICK);

                    ES_Event_t NextEvent;
                    NextEvent.EventType = ES_ASSIGN_TASK;
                    NextEvent.EventParam = 0;
                    if (!PostGameService(NextEvent))
                    {
                        DB_printf("ERROR: Could not post first task assignment\r\n");
                    }
                }
                else if (CountdownRemaining > 1U)
                {
                    CountdownRemaining--;
                    DB_printf("COUNTDOWN: %u\r\n", CountdownRemaining);
                    if (!PostLEDCountdownStep(CountdownRemaining))
                    {
                        DB_printf("ERROR: Could not display game countdown\r\n");
                    }
                    ES_Timer_InitTimer(SEQUENCE_TIMER, COUNTDOWN_DURATION);
                }
                else
                {
                    CountdownRemaining = 0;
                    CountdownShowingGo = true;
                    DB_printf("G O !\r\n");
                    if (!PostLEDDisplayGo())
                    {
                        DB_printf("ERROR: Could not display GO\r\n");
                    }
                    ES_Timer_InitTimer(SEQUENCE_TIMER, COUNTDOWN_DURATION);
                }
            }
            break;

        case GAME_SELECT_TASK:
            DB_printf("GAME SELECT TASK\r\n");
            // Pick a task on request, or when the pause after a task ends
            if ((ThisEvent.EventType == ES_ASSIGN_TASK) ||
                ((ThisEvent.EventType == ES_TIMEOUT) &&
                 (ThisEvent.EventParam == SEQUENCE_TIMER)))
            {
                AssignNewTask();
                switch (CurrentTask)
                {
                    case TASK_CUT_CARROT:
                        EnterCarrotTask();
                        CurrentState = GAME_CUT_CARROT;
                        break;
                    case TASK_STIR_POT:
                        EnterStirTask();
                        CurrentState = GAME_STIR_POT;
                        break;
                    case TASK_HEAT_COIL:
                        EnterHeatTask();
                        CurrentState = GAME_HEAT_COIL;
                        break;
                }
            }
            break;

        case GAME_CUT_CARROT:
            switch (ThisEvent.EventType)
            {
                case ES_INPUT_CARROT_PRESSED:
                    if (CarrotInputEnabled)
                    {
                        // Finish after the required presses and an interaction interval without a press.
                        ES_Timer_InitTimer(INTERACTION_TIMER, INTERACTION_DURATION);

                        if (DetectedButtonPresses <= TargetButtonPresses)
                        {
                            DetectedButtonPresses++;
                        }
                        DB_printf("CARROT CHOP %u / %u\r\n",
                                  DetectedButtonPresses,
                                  TargetButtonPresses);
                    }
                    break;

                case ES_TIMEOUT:
                    if ((ThisEvent.EventParam == SEQUENCE_TIMER) &&
                        CarrotServoOpening)
                    {
                        CarrotServoOpening = false;
                        CarrotServoClosing = true;
                        PostServoCommand(ES_SERVO_CARROT_CLOSE);
                        ES_Timer_InitTimer(SEQUENCE_TIMER,
                                           CARROT_SERVO_CLOSE_DURATION);
                    }
                    else if ((ThisEvent.EventParam == SEQUENCE_TIMER) &&
                             CarrotServoClosing)
                    {
                        CarrotServoClosing = false;
                        StartCarrotBlinkSequence();
                    }
                    else if ((ThisEvent.EventParam == SEQUENCE_TIMER) &&
                        !CarrotInputEnabled)
                    {
                        if (CarrotBlinkOn)
                        {
                            CARROT_LED = LED_OFF;
                            CarrotBlinkOn = false;
                            CarrotBlinksRemaining--;

                            if (CarrotBlinksRemaining == 0U)
                            {
                                CarrotInputEnabled = true;
                                DB_printf("BLINKING COMPLETE. Press the carrot button %u times.\r\n",
                                          TargetButtonPresses);
                            }
                            else
                            {
                                ES_Timer_InitTimer(SEQUENCE_TIMER, CARROT_BLINK_DURATION);
                            }
                        }
                        else
                        {
                            CARROT_LED = LED_ON;
                            CarrotBlinkOn = true;
                            ES_Timer_InitTimer(SEQUENCE_TIMER, CARROT_BLINK_DURATION);
                        }
                    }
                    else if ((ThisEvent.EventParam == INTERACTION_TIMER) &&
                             CarrotInputEnabled)
                    {
                        if (DetectedButtonPresses == TargetButtonPresses)
                        {
                            ES_Event_t DoneEvent;
                            DoneEvent.EventType = ES_CARROT_TASK_COMPLETE;
                            DoneEvent.EventParam = 0;
                            if (!PostGameService(DoneEvent))
                            {
                                DB_printf("ERROR: Could not post carrot task completion\r\n");
                            }
                        }
                        else if (DetectedButtonPresses > TargetButtonPresses)
                        {
                            DB_printf("TOO MANY PRESSES. Try the carrot task again.\r\n");
                            DetectedButtonPresses = 0;
                            StartCarrotBlinkSequence();
                        }
                    }
                    break;

                case ES_CARROT_TASK_COMPLETE:
                    if (CarrotInputEnabled &&
                        (DetectedButtonPresses == TargetButtonPresses))
                    {
                        DB_printf("CARROT CUT!\r\n");
                        CARROT_LED = LED_OFF;
                        PostServoCommand(ES_SERVO_CARROT_OPEN);
                        ES_Timer_StopTimer(INTERACTION_TIMER);
                        CompleteTask();
                    }
                    break;

                default:
                    break;
            }
            break;

        case GAME_STIR_POT:
            switch (ThisEvent.EventType)
            {
                case ES_INPUT_POT_CHANGED:
                    UpdateSoupStirring(ThisEvent.EventParam);
                    break;

                case ES_TIMEOUT:
                    // Pot stayed in the window for the full hold time
                    if ((ThisEvent.EventParam == INTERACTION_TIMER) && SoupStirring)
                    {
                        ES_Event_t DoneEvent;
                        DoneEvent.EventType = ES_STIR_TASK_COMPLETE;
                        DoneEvent.EventParam = 0;
                        PostGameService(DoneEvent);
                    }
                    // Pot out of range, shake
                    else if ((ThisEvent.EventParam == SOUP_SHAKE_TIMER) && SoupShaking)
                    {
                        int16_t ShakeSpeed = ShakeSpeedForPot(QueryPotValue());
                        SoupShakeForward = !SoupShakeForward;
                        SetSoupSpeed(SoupShakeForward ? ShakeSpeed : -ShakeSpeed);
                        ES_Timer_InitTimer(SOUP_SHAKE_TIMER, SOUP_SHAKE_DURATION);
                    }
                    break;

                case ES_STIR_TASK_COMPLETE:
                    DB_printf("SOUP STIRRED!\r\n");
                    ES_Timer_StopTimer(INTERACTION_TIMER);
                    StopSoupShake();
                    // Make sure the soup servo is off when leaving the task
                    if (SoupStirring)
                    {
                        SoupStirring = false;
                        PostServoCommand(ES_SERVO_SOUP_STOP);
                    }
                    CompleteTask();
                    break;

                default:
                    break;
            }
            break;

        case GAME_HEAT_COIL:
            switch (ThisEvent.EventType)
            {
                case ES_INPUT_IR_CHANGED:
                    UpdateCoilHeating(ThisEvent.EventParam);
                    break;

                case ES_TIMEOUT:
                    if (ThisEvent.EventParam == SENSOR_PRINT_TIMER)
                    {
                        // Calibration readout
                        uint16_t IR = QueryIRValue();
                        // Rise above ambient is what the window checks
                        int16_t Rise = (int16_t)IR - (int16_t)HeatBaseline;
                        ES_Timer_InitTimer(SENSOR_PRINT_TIMER, SENSOR_PRINT_PERIOD);
                    }
                    else if ((ThisEvent.EventParam == INTERACTION_TIMER) && CoilHeating)
                    {
                        // Hand stayed in the window for the full hold time
                        ES_Event_t DoneEvent;
                        DoneEvent.EventType = ES_HEAT_TASK_COMPLETE;
                        DoneEvent.EventParam = 0;
                        PostGameService(DoneEvent);
                    }
                    break;

                case ES_HEAT_TASK_COMPLETE:
                    DB_printf("COIL HEATED!\r\n");
                    ES_Timer_StopTimer(INTERACTION_TIMER);
                    ES_Timer_StopTimer(SENSOR_PRINT_TIMER);
                    if (CoilHeating)
                    {
                        CoilHeating = false;
                    }
                    // Strip off when leaving the task
                    SetAllNeoPixels(0,0,0);
                    UpdateNeoPixels();
                    CompleteTask();
                    break;

                default:
                    break;
            }
            break;

        case GAME_WINNING:
            switch (ThisEvent.EventType)
            {
                case ES_GAME_WON:
                    StartDispense();
                    if (!WinCelebrationActive) {
                        StartWinCelebration();
                    }
                    break;

                case ES_TIMEOUT:
                    if ((ThisEvent.EventParam == WIN_CELEBRATION_TIMER) &&
                        WinCelebrationActive)
                    {
                        UpdateWinCelebration();
                    }
                    break;

                default:
                    break;
            }
            break;

        case GAME_RESTARTING:
            if ((ThisEvent.EventType == ES_TIMEOUT) &&
                (ThisEvent.EventParam == GAME_SERVICE_TIMER))
            {
                const uint8_t MessageLength =
                    (uint8_t)(sizeof(RestartingMessage) - 1U);
                const uint8_t TotalCharacters =
                    (uint8_t)(MessageLength + RESTART_SCROLL_PADDING);

                if (RestartScrollIndex < TotalCharacters)
                {
                    ES_Event_t LEDCharEvent;
                    LEDCharEvent.EventType = ES_LED_ADD_CHAR;
                    LEDCharEvent.EventParam =
                        (RestartScrollIndex < MessageLength) ?
                        (uint8_t)RestartingMessage[RestartScrollIndex] : ' ';

                    if (PostLEDService(LEDCharEvent))
                    {
                        RestartScrollIndex++;
                    }
                    else
                    {
                        DB_printf("ERROR: Could not post restarting message character\r\n");
                    }
                    ES_Timer_InitTimer(GAME_SERVICE_TIMER,
                                       RESTART_SCROLL_INTERVAL);
                }
                else
                {
                    ES_Event_t InitEvent;
                    CurrentState = GAME_INITIALIZING;
                    InitEvent.EventType = ES_INIT;
                    InitEvent.EventParam = 0;
                    if (!PostGameService(InitEvent))
                    {
                        DB_printf("ERROR: Could not restart game service initialization\r\n");
                    }
                }
            }
            break;

        case GAME_LOSING:
            if ((ThisEvent.EventType == ES_TIMEOUT) &&
                (ThisEvent.EventParam == GAME_SERVICE_TIMER))
            {
                if (!LossDisplayed)
                {
                    // show LOSS
                    LossDisplayed = true;
                    DB_printf("L O S S\r\n");
                    if (!PostLEDDisplayLoss())
                    {
                        DB_printf("ERROR: Could not display LOSS\r\n");
                    }
                    ES_Timer_InitTimer(GAME_SERVICE_TIMER, RESTART_DISPLAY_OFF_DURATION);
                }
                else
                {
                    // loss finished, restarting
                    LossDisplayed = false;
                    if (!PostLEDClearDisplay())
                    {
                        DB_printf("ERROR: Could not clear LED display for welcome\r\n");
                    }
                    DB_printf("RESTARTING\r\n");
                    RestartScrollIndex = 0;
                    CurrentState = GAME_RESTARTING;
                    ES_Timer_InitTimer(GAME_SERVICE_TIMER, RESTART_SCROLL_INTERVAL);
                }
            }
            break;

        case GAME_INACTIVE:
            // Inactivity timeout logic will go here
            DB_printf("NO INPUT\r\n");
            break;

        case GAME_TEST:
            switch (ThisEvent.EventType)
            {
                case ES_INPUT_START_PRESSED:
                    DB_printf("\r\n[BUTTON TEST]");
                    DB_printf(" START BUTTON PRESSED - RB3\r\n");
                    break;

                case ES_INPUT_START_RELEASED:
                    DB_printf("[BUTTON TEST]");
                    DB_printf(" START BUTTON RELEASED - RB3\r\n");
                    break;

                case ES_INPUT_CARROT_PRESSED:
                    TestCarrotPressCount++;
                    DB_printf("\r\n[BUTTON TEST]");
                    DB_printf(" CARROT BUTTON PRESSED - RB2\r\n");
                    DB_printf("Total presses: %u\r\n", TestCarrotPressCount);
                    break;

                case ES_INPUT_CARROT_RELEASED:
                    DB_printf("[BUTTON TEST]");
                    DB_printf(" CARROT BUTTON RELEASED - RB2\r\n");
                    break;

                case ES_INPUT_POT_CHANGED:
                    TestPotValue = ThisEvent.EventParam;
                    DB_printf("\r\n[POTENTIOMETER TEST]\r\n");
                    DB_printf("Raw ADC: %u / 1023\r\n", TestPotValue);
                    DB_printf("Position: %u%%\r\n", (uint16_t)((TestPotValue * 100U) / 1023U));
                    break;

                case ES_INPUT_IR_CHANGED:
                    TestIRValue = ThisEvent.EventParam;
                    DB_printf("\r\n[IR SENSOR TEST]\r\n");
                    DB_printf("Raw ADC: %u / 1023\r\n", TestIRValue);
                    DB_printf("Relative reading: %u%%\r\n", (uint16_t)((TestIRValue * 100U) / 1023U));
                    break;

                case ES_INPUT_BEAM_BROKEN:
                    DB_printf("\r\n[BEAM BREAK TEST]");
                    DB_printf(" BEAM BROKEN - RB4\r\n");
                    break;

                case ES_INPUT_BEAM_RESTORED:
                    DB_printf("[BEAM BREAK TEST]");
                    DB_printf(" BEAM RESTORED - RB4\r\n");
                    break;

                case ES_NEW_KEY:
                    switch ((char)ThisEvent.EventParam)
                    {
                        case 'x':
                            DB_printf("\r\nEXITING INPUT TEST MODE\r\n");
                            ClearNeoPixels();
                            UpdateNeoPixels();
                            EnterWelcomeMode();
                            CurrentState = GAME_WELCOME;
                            break;

                        default:
                            break;
                    }
                    break;

                default:
                    break;
            }

            break;

        default:
            break;
    }

    return ReturnEvent;
}


/*******************************
   Private Functions
 *******************************/

static void InitHWPins(void)
{
    // Carrot button LED on RA3 (digital output, no analog function)
    CARROT_LED_TRIS = 0;
    CARROT_LED = LED_OFF;

    // Start button LED on RB10 (digital output, no analog function)
    START_LED_TRIS = 0;
    START_LED = LED_OFF;

    // NeoPixel strip on RB11, blanked at startup
    InitNeoPixelService();
}


// Called on every entry to the carrot task
static void EnterCarrotTask(void)
{
    TargetButtonPresses = (uint8_t)(CARROT_PRESSES_MIN +
        (rand() % (CARROT_PRESSES_MAX - CARROT_PRESSES_MIN + 1U)));
    DetectedButtonPresses = 0;
    CarrotInputEnabled = false;
    CarrotServoOpening = true;
    CarrotServoClosing = false;
    PostServoCommand(ES_SERVO_CARROT_OPEN);
    ES_Timer_InitTimer(SEQUENCE_TIMER, CARROT_SERVO_OPEN_DURATION);
    DB_printf("CUT CARROT! Wait for %u blinks.\r\n", TargetButtonPresses);
}


static void StartCarrotBlinkSequence(void)
{
    CarrotBlinksRemaining = TargetButtonPresses;
    CarrotBlinkOn = true;
    CarrotInputEnabled = false;
    CARROT_LED = LED_ON;
    ES_Timer_InitTimer(SEQUENCE_TIMER, CARROT_BLINK_DURATION);
}


// Called on every entry to the stir pot task
static void EnterStirTask(void)
{
    StirTarget_t EligibleTargets[STIR_TARGET_COUNT];
    uint8_t EligibleCount = 0;
    uint8_t TargetIndex;
    uint16_t PotReading = QueryPotValue();

    // Exclude the previous target and any range the knob is already in.
    for (TargetIndex = 0; TargetIndex < STIR_TARGET_COUNT; TargetIndex++)
    {
        bool PotInRange =
            (PotReading >= StirTargetRanges[TargetIndex].Low) &&
            (PotReading <= StirTargetRanges[TargetIndex].High);

        if ((TargetIndex != LastStirTarget) && !PotInRange)
        {
            EligibleTargets[EligibleCount++] = (StirTarget_t)TargetIndex;
        }
    }

    CurrentStirTarget = EligibleTargets[rand() % EligibleCount];

    LastStirTarget = CurrentStirTarget;
    DB_printf("STIR POT! Set knob to ADC range %u-%u\r\n",
              (unsigned int)StirTargetRanges[CurrentStirTarget].Low,
              (unsigned int)StirTargetRanges[CurrentStirTarget].High);

    // Start from a known state: not stirring or shaking
    SoupStirring = false;
    SoupShaking = false;

    // Pot events only arrive when the pot moves, so check where it is now.
    // It may already be in the window when the task starts.
    UpdateSoupStirring(PotReading);
}


// Soup servo stirs while the pot is inside the window and shakes back and
// forth while it is outside
static void UpdateSoupStirring(uint16_t PotReading)
{
    StirTargetRange_t Target = StirTargetRanges[CurrentStirTarget];
    bool InWindow = (PotReading >= Target.Low) &&
                    (PotReading <= Target.High);

    if (InWindow && !SoupStirring) // in the range
    {
        StopSoupShake();
        SoupStirring = true;
        DB_printf("POT IN RANGE (%u) - STIRRING\r\n", PotReading);
        SetSoupSpeed(SOUP_STIR_SPEED);

        // Task completes if the pot stays in range for the interaction interval.
        ES_Timer_InitTimer(INTERACTION_TIMER, INTERACTION_DURATION);
    }
    else if (!InWindow && !SoupShaking) // out of range (or just started)
    {
        if (SoupStirring)
        {
            SoupStirring = false;
            // Left the window before the hold time: start over next time
            ES_Timer_StopTimer(INTERACTION_TIMER);
        }
        DB_printf("POT OUT OF RANGE (%u) - SHAKING\r\n", PotReading);
        StartSoupShake();
    }
}


// Begin shaking: alternate direction every SOUP_SHAKE_DURATION
static void StartSoupShake(void)
{
    SoupShaking = true;
    SoupShakeForward = true;
    SetSoupSpeed(ShakeSpeedForPot(QueryPotValue()));
    ES_Timer_InitTimer(SOUP_SHAKE_TIMER, SOUP_SHAKE_DURATION);
}


static int16_t ShakeSpeedForPot(uint16_t PotReading)
{
    int32_t Target = (StirTargetRanges[CurrentStirTarget].High - StirTargetRanges[CurrentStirTarget].Low) / 2;

    int32_t Distance = 0;
    if(PotReading > StirTargetRanges[CurrentStirTarget].High)
    {
        Distance = (int32_t)((PotReading - StirTargetRanges[CurrentStirTarget].High));
    }
    else if(PotReading < StirTargetRanges[CurrentStirTarget].Low)
    {
        Distance = (int32_t)((StirTargetRanges[CurrentStirTarget].Low - PotReading));
    }

    int32_t Speed = SOUP_SHAKE_MAX_SPEED * (1023 - Distance) / 1023;

    return (int16_t)Speed;
}


// Stop shaking (does not change the servo speed; caller sets what's next)
static void StopSoupShake(void)
{
    SoupShaking = false;
    ES_Timer_StopTimer(SOUP_SHAKE_TIMER);
}


static void SetSoupSpeed(int16_t Speed)
{
    ES_Event_t ServoEvent;
    ServoEvent.EventType = ES_SERVO_SOUP_SPEED;
    ServoEvent.EventParam = (uint16_t)Speed;
    if (!PostServoService(ServoEvent))
    {
        DB_printf("ERROR: Could not post soup servo speed\r\n");
    }
}

// spin until the beam is established
static void PrimeDispenser(void)
{
    if (DispensePhase != DISPENSE_IDLE)
    {
        return;     // already running
    }

    if (!QueryBeamBroken())
    {
        DB_printf("DISPENSER READY (beam established)\r\n");
        return;
    }

    DispensePhase = DISPENSE_PRIMING;
    DB_printf("PRIMING DISPENSER until beam is established\r\n");
    PostServoCommand(ES_SERVO_DISPENSE_START);
    ES_Timer_InitTimer(DISPENSE_TIMER, DISPENSE_TIMEOUT);
}

// spin until the beam is broken and then restored on win
static void StartDispense(void)
{
    // If the beam is already broken, wait for it to restore
    DispensePhase = QueryBeamBroken() ? DISPENSE_WAIT_RESTORE
                                      : DISPENSE_WAIT_BREAK;
    DB_printf("DISPENSING\r\n");
    PostServoCommand(ES_SERVO_DISPENSE_START);
    ES_Timer_InitTimer(DISPENSE_TIMER, DISPENSE_TIMEOUT);
}

static void UpdateDispense(ES_Event_t ThisEvent)
{
    if (DispensePhase == DISPENSE_IDLE)
    {
        return;
    }

    if ((ThisEvent.EventType == ES_TIMEOUT) &&
        (ThisEvent.EventParam == DISPENSE_TIMER))
    {
        DB_printf("DISPENSE TIMEOUT (phase %u)\r\n",
                  (unsigned int)DispensePhase);
        StopDispense();
        return;
    }

    switch (DispensePhase)
    {
        case DISPENSE_WAIT_BREAK:
            if (ThisEvent.EventType == ES_INPUT_BEAM_BROKEN)
            {
                DB_printf("TREAT DETECTED\r\n");
                DispensePhase = DISPENSE_WAIT_RESTORE;
            }
            break;

        case DISPENSE_PRIMING:
        case DISPENSE_WAIT_RESTORE:
            if (ThisEvent.EventType == ES_INPUT_BEAM_RESTORED)
            {
                StopDispense();
            }
            break;

        default:
            break;
    }
}

static void StopDispense(void)
{
    DispensePhase = DISPENSE_IDLE;
    ES_Timer_StopTimer(DISPENSE_TIMER);
    PostServoCommand(ES_SERVO_DISPENSE_STOP);
    DB_printf("DISPENSE COMPLETE\r\n");
}


// Heat window bounds, relative to the ambient baseline
static uint16_t HeatWindowLow(void)
{
    return TargetHandValue - HEAT_TOLERANCE_COUNTS;
}

static uint16_t HeatWindowHigh(void)
{
    return TargetHandValue + HEAT_TOLERANCE_COUNTS;
}


// Called on every entry to the heat coil task
static void EnterHeatTask(void)
{
    uint16_t TargetRise;

    // Ambient baseline, assumes no hand over the sensor right now
    HeatBaseline = QueryIRValue();
    TargetRise = HeatTargetRiseOptions[rand() % HEAT_TARGET_VARIANT_COUNT];
    TargetHandValue = HeatBaseline + TargetRise;

    DB_printf("HEAT COIL! Baseline %u, target %u, hold hand over sensor: %u-%u\r\n",
              (unsigned int)HeatBaseline,
              (unsigned int)TargetHandValue,
              (unsigned int)HeatWindowLow(),
              (unsigned int)HeatWindowHigh());

    CoilHeating = false;
    SetAllNeoPixels(0,0,0);
    UpdateNeoPixels();

#if HEAT_SERIAL_READOUT
    ES_Timer_InitTimer(SENSOR_PRINT_TIMER, SENSOR_PRINT_PERIOD);
#endif

    // IR events only arrive on change, so check the current reading now
    UpdateCoilHeating(QueryIRValue());
}


// NeoPixels glow while the hand is inside the window, off outside it
static void UpdateCoilHeating(uint16_t IRReading)
{
    DetectedHandValue = IRReading;
    bool InWindow = (DetectedHandValue >= HeatWindowLow()) &&
                    (DetectedHandValue <= HeatWindowHigh());

    if (InWindow && !CoilHeating)
    {
        CoilHeating = true;
        DB_printf("HAND IN RANGE (%u) - HEATING\r\n", IRReading);
        SetAllNeoPixels(255, 0, 0);
        UpdateNeoPixels();

        // Task completes if the hand stays in range for the interaction interval.
        ES_Timer_InitTimer(INTERACTION_TIMER, INTERACTION_DURATION);
    }
    else if (!InWindow && CoilHeating)
    {
        CoilHeating = false;
        DB_printf("HAND OUT OF RANGE (%u) - COOLING\r\n", IRReading);
              
        // Left the window before the hold time: start over next time
        ES_Timer_StopTimer(INTERACTION_TIMER);
    }
    else {
        if(IRReading < HeatWindowLow())
        {
            int16_t Distance = HeatWindowLow() - IRReading;
            SetAllNeoPixels(200, 0 + Distance, 0);
            UpdateNeoPixels();

        }
        else if(IRReading > HeatWindowHigh())
        {
            int16_t Distance = IRReading - HeatWindowHigh();
            if(Distance > 200)
            {
                Distance = 200;
            }
            SetAllNeoPixels(200 - Distance, 0, 0 + Distance);
            UpdateNeoPixels();
        }
    }
}

static void PostServoCommand(ES_EventType_t Command)
{
    ES_Event_t ServoEvent;
    ServoEvent.EventType = Command;
    ServoEvent.EventParam = 0;
    if (!PostServoService(ServoEvent))
    {
        DB_printf("ERROR: Could not post servo command %u\r\n",
                  (unsigned int)Command);
    }
}

static void StartWinCelebration(void)
{
    WinCelebrationActive = true;
    WinCarrotOpen = true;
    WinCongratsVisible = true;
    WinAnimationSteps = 0;
    WinRainbowPhase = 0;

    DB_printf("YOU'RE A WINNER!\r\n");

    if (!PostLEDShowCongrats(true))
    {
        DB_printf("ERROR: Could not start LED win message\r\n");
    }

    PostServoCommand(ES_SERVO_CARROT_OPEN);
    SetSoupSpeed(SOUP_STIR_SPEED);
    SetNeoPixelRainbow(WinRainbowPhase);
    UpdateNeoPixels();


    ES_Timer_InitTimer(WIN_CELEBRATION_TIMER, WIN_ANIMATION_STEP);
}


static void UpdateWinCelebration(void)
{
    WinAnimationSteps++;
    WinRainbowPhase = (uint8_t)(WinRainbowPhase + WIN_RAINBOW_PHASE_STEP);
    SetNeoPixelRainbow(WinRainbowPhase);
    UpdateNeoPixels();

    if (WinAnimationSteps >= WIN_ANIMATION_TOTAL_STEPS)
    {
        WinCelebrationActive = false;
        if (!PostLEDShowCongrats(false))
        {
            DB_printf("ERROR: Could not clear LED win message\r\n");
        }
        PostServoCommand(ES_SERVO_CARROT_CLOSE);
        PostServoCommand(ES_SERVO_SOUP_STOP);
        ClearNeoPixels();
        UpdateNeoPixels();

        RestartScrollIndex = 0;
        CurrentState = GAME_RESTARTING;
        ES_Timer_InitTimer(GAME_SERVICE_TIMER, RESTART_DISPLAY_OFF_DURATION);
        return;
    }

    if ((WinAnimationSteps % WIN_TOGGLE_INTERVAL_STEPS) == 0U)
    {
        WinCongratsVisible = !WinCongratsVisible;
        if (!PostLEDShowCongrats(WinCongratsVisible))
        {
            DB_printf("ERROR: Could not update LED win message\r\n");
        }

        WinCarrotOpen = !WinCarrotOpen;
        PostServoCommand(WinCarrotOpen ?
                         ES_SERVO_CARROT_OPEN : ES_SERVO_CARROT_CLOSE);
    }

    ES_Timer_InitTimer(WIN_CELEBRATION_TIMER, WIN_ANIMATION_STEP);
}


static void StartNewGame(void)
{
    GameProgress = 0;
    GameTime = GAME_DURATION / 1000U;
    GameTimerActive = false;
    CountdownShowingGo = false;
    StopGameTimers();
    LastTask = TASK_COUNT;
    LastStirTarget = STIR_TARGET_COUNT;

    DetectedButtonPresses = 0;

    DB_printf("NEW GAME\r\n");

    // The game timer starts when the GO display finishes.
}

static void UpdateGameTimer(void)
{
    if (GameTime > 0U)
    {
        GameTime--;
    }

    DB_printf("GAME TIME: %u\r\n", GameTime);
    if (!PostLEDDisplayTimer(GameTime, GameProgress))
    {
        DB_printf("ERROR: Could not update game timer display\r\n");
    }

    if (GameTime == 0U)
    {
        GameTimerActive = false;
        ClearNeoPixels();
        StopGameTimers();
        CurrentState = GAME_LOSING;
        LossDisplayed = false;
        DB_printf("TIME UP\r\n");
        bool SoupServoWasActive = SoupStirring || SoupShaking;
        SoupStirring = false;
        StopSoupShake();
        if (SoupServoWasActive)
        {
            PostServoCommand(ES_SERVO_SOUP_STOP);
        }
        ES_Timer_InitTimer(GAME_SERVICE_TIMER, COUNTDOWN_DURATION);
    }
    else
    {
        ES_Timer_InitTimer(GAME_TIMER, GAME_TIMER_TICK);
    }
}


static void AssignNewTask(void)
{
    do
    {
        CurrentTask = (GameTask_t)(rand() % TASK_COUNT);
    } while (CurrentTask == LastTask);

    LastTask = CurrentTask;
    DB_printf("NEW TASK: %d\r\n", CurrentTask);
}


// Score the finished task, then move on to the next one after a short pause
static void CompleteTask(void)
{
    UpdateGameProgress();

    if (CurrentState != GAME_WINNING)
    {
        CurrentState = GAME_SELECT_TASK;
        ES_Timer_InitTimer(SEQUENCE_TIMER, TASK_GAP_DURATION);

        if (!PostLEDDisplayTimer(GameTime, GameProgress))
        {
            DB_printf("ERROR: Could not update progress bar\r\n");
        }
    }
}


static void UpdateGameProgress(void)
{
    if (GameProgress < MAX_GAME_PROGRESS)
    {
        GameProgress++;
    }

    DB_printf("GAME PROGRESS: %d / %d\r\n",
              GameProgress,
              MAX_GAME_PROGRESS);

    if (GameProgress >= MAX_GAME_PROGRESS)
    {
        CurrentState = GAME_WINNING;
        GameTimerActive = false;
        StopGameTimers();

        ES_Event_t WinEvent;

        WinEvent.EventType = ES_GAME_WON;
        WinEvent.EventParam = GameProgress;

        if (!PostGameService(WinEvent))
        {
            DB_printf("ERROR: Could not post game-won event\r\n");
        }
    }
}


static void EnterWelcomeMode(void)
{
    GameProgress = 0;
    GameTime = GAME_DURATION / 1000U;
    GameTimerActive = false;
    CountdownShowingGo = false;
    StopGameTimers();

    DB_printf("BROTH OF DUTY\r\n");

    START_LED = LED_ON;

    ClearNeoPixels();
    UpdateNeoPixels();

    if (!PostLEDClearDisplay())
    {
        DB_printf("ERROR: Could not clear LED display for welcome\r\n");
    }

    // Start scrolling the welcome message (handled in GAME_WELCOME)
    WelcomeScrollIndex = 0;
    ES_Timer_InitTimer(GAME_SERVICE_TIMER, WELCOME_SCROLL_INTERVAL);
}

static void StopGameTimers(void)
{
    ES_Timer_StopTimer(GAME_TIMER);
    ES_Timer_StopTimer(INTERACTION_TIMER);
    ES_Timer_StopTimer(SEQUENCE_TIMER);
    ES_Timer_StopTimer(SENSOR_PRINT_TIMER);
    ES_Timer_StopTimer(SOUP_SHAKE_TIMER);
    ES_Timer_StopTimer(GAME_SERVICE_TIMER);
}


/*******************************
   Public Query Function
 *******************************/

uint8_t QueryGameProgress(void)
{
    return GameProgress;
}

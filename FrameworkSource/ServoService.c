#include <xc.h>
#include <stdint.h>
#include <sys/attribs.h>

#include "ServoService.h"

/*******************************************************************************
 * CONFIGURATION
 ******************************************************************************/
/* Assumes:
 *   PBCLK = 40 MHz
 *   Timer2 prescaler = 1:16
 *   Timer2 frequency = 2.5 MHz
 *   Timer tick = 0.4 us
 */
#ifndef SERVO_PBCLK_HZ
#define SERVO_PBCLK_HZ 40000000UL
#endif

#define SERVO_TIMER_PRESCALE       16UL

#define SERVO_FRAME_RATE_HZ        50UL
#define SERVO_FRAME_US             20000UL

#define SERVO_MIN_PULSE_US         500U
#define SERVO_MAX_PULSE_US         2500U

#define SERVO_DEFAULT_PULSE_US     1500U

// Timer2 ticks per servo frame.
#define SERVO_FRAME_TICKS ((SERVO_PBCLK_HZ / SERVO_TIMER_PRESCALE) / SERVO_FRAME_RATE_HZ)

// Servo output pin masks.
#define DISPENSE_SERVO_MASK        (1U << 5)  // RB5
#define CARROT_SERVO_MASK          (1U << 4)  // RA4
#define SOUP_SERVO_MASK            (1U << 9)  // RB9

/*******************************************************************************
 * COMPILE-TIME CHECK
 ******************************************************************************/
#if (SERVO_FRAME_TICKS > 65535UL)
#error "Servo frame exceeds Timer2 16-bit capacity. Check PBCLK or prescaler."
#endif

/*******************************************************************************
 * PRIVATE VARIABLES
 ******************************************************************************/
// Pulse widths expressed in Timer2 ticks. Volatile because these values can be updated by the main program and read by the Timer2 interrupt.
static volatile uint16_t ServoPulseTicks[SERVO_COUNT];

// Number of ticks elapsed within the current 20 ms frame.
static volatile uint16_t ElapsedTicks = 0;

// Number of ticks scheduled for the current Timer2 interval.
static volatile uint16_t ScheduledTicks = 0;

/*******************************************************************************
 * PRIVATE FUNCTION PROTOTYPES
 ******************************************************************************/
static uint16_t MicrosecondsToTicks(uint16_t microseconds);

static void SetServoPinHigh(ServoID_t servo);
static void SetServoPinLow(ServoID_t servo);

static void SetAllServoPinsHigh(void);
static void SetAllServoPinsLow(void);

static void ScheduleTimer(uint16_t ticks);
static void StartServoFrame(void);

/*******************************************************************************
 * MICROSECONDS TO TIMER TICKS
 ******************************************************************************/
static uint16_t MicrosecondsToTicks(uint16_t microseconds)
{
    uint64_t numerator;
    uint64_t denominator;
    uint64_t result;

    numerator = (uint64_t)microseconds * (uint64_t)SERVO_PBCLK_HZ;
    denominator = (uint64_t)SERVO_TIMER_PRESCALE * 1000000ULL;
    result = (numerator + (denominator / 2ULL)) / denominator;

    if (result < 1ULL) {
        result = 1ULL;
    }

    return (uint16_t)result;
}

/*******************************************************************************
 * SERVO GPIO CONTROL
 ******************************************************************************/
static void SetServoPinHigh(ServoID_t servo)
{
    switch (servo) {

        case SERVO_DISPENSE:
            LATBSET = DISPENSE_SERVO_MASK;
            break;

        case SERVO_CARROT:
            LATASET = CARROT_SERVO_MASK;
            break;

        case SERVO_SOUP:
            LATBSET = SOUP_SERVO_MASK;
            break;

        default:
            break;
    }
}

static void SetServoPinLow(ServoID_t servo)
{
    switch (servo) {
        case SERVO_DISPENSE:
            LATBCLR = DISPENSE_SERVO_MASK;
            break;

        case SERVO_CARROT:
            LATACLR = CARROT_SERVO_MASK;
            break;

        case SERVO_SOUP:
            LATBCLR = SOUP_SERVO_MASK;
            break;

        default:
            break;
    }
}

static void SetAllServoPinsHigh(void)
{
    LATBSET = DISPENSE_SERVO_MASK | SOUP_SERVO_MASK;
    LATASET = CARROT_SERVO_MASK;
}

static void SetAllServoPinsLow(void)
{
    LATBCLR = DISPENSE_SERVO_MASK | SOUP_SERVO_MASK;
    LATACLR = CARROT_SERVO_MASK;
}

/*******************************************************************************
 * TIMER SCHEDULING
 ******************************************************************************/
// Schedule the next Timer2 interrupt after the requested number of ticks. Timer2 is reset for each interval. This keeps the timer scheduling simple, although interrupt latency introduces a small amount of timing error.
static void ScheduleTimer(uint16_t ticks)
{
    if (ticks == 0U) {
        ticks = 1U;
    }

    ScheduledTicks = ticks;

    TMR2 = 0;
    PR2 = (uint16_t)(ticks - 1U);
}

/*******************************************************************************
 * START A NEW SERVO FRAME
 ******************************************************************************/
static void StartServoFrame(void)
{
    uint16_t shortestPulse;
    uint8_t i;

    // Reset elapsed time for the new 20 ms frame.
    ElapsedTicks = 0U;

    // Raise all three servo signals simultaneously.
    SetAllServoPinsHigh();

    // Find the earliest servo falling edge.
    shortestPulse = ServoPulseTicks[0];

    for (i = 1U; i < SERVO_COUNT; i++) {

        if (ServoPulseTicks[i] < shortestPulse) {
            shortestPulse = ServoPulseTicks[i];
        }
    }

    // Schedule the first falling edge.
    ScheduleTimer(shortestPulse);
}

/*******************************************************************************
 * TIMER2 INTERRUPT SERVICE ROUTINE
 ******************************************************************************/
void __ISR(_TIMER_2_VECTOR, IPL4SOFT) Timer2ISR(void)
{
    uint8_t i;
    uint16_t nextPulse;
    uint16_t remainingTicks;

    // Clear Timer2 interrupt flag.
    IFS0CLR = _IFS0_T2IF_MASK;

    // Account for the interval that just completed.
    ElapsedTicks += ScheduledTicks;

    // If the 20 ms frame has ended, begin the next frame.
    if (ElapsedTicks >= SERVO_FRAME_TICKS) {
        StartServoFrame();
        return;
    }

    // Lower every servo output whose requested pulse width has elapsed.
    for (i = 0U; i < SERVO_COUNT; i++) {
        if (ServoPulseTicks[i] <= ElapsedTicks) {
            SetServoPinLow((ServoID_t)i);
        }
    }

    // Find the next servo falling edge. Only pulse widths later than the current elapsed time are considered.
    nextPulse = SERVO_FRAME_TICKS;

    for (i = 0U; i < SERVO_COUNT; i++) {
        if ((ServoPulseTicks[i] > ElapsedTicks) && (ServoPulseTicks[i] < nextPulse)) {
            nextPulse = ServoPulseTicks[i];
        }
    }

    // If another servo falling edge remains, schedule it.
    if (nextPulse < SERVO_FRAME_TICKS) {
        ScheduleTimer((uint16_t)(nextPulse - ElapsedTicks));
    } else {
        remainingTicks = (uint16_t)(SERVO_FRAME_TICKS - ElapsedTicks);
        ScheduleTimer(remainingTicks);
    }
}

/*******************************************************************************
 * INITIALIZATION
 ******************************************************************************/

void InitServoService(void)
{
    uint8_t i;

    // Configure servo pins as outputs. 0 = output
    TRISBCLR = DISPENSE_SERVO_MASK | SOUP_SERVO_MASK;
    TRISACLR = CARROT_SERVO_MASK;

    SetAllServoPinsLow();

    for (i = 0U; i < SERVO_COUNT; i++) {
        ServoPulseTicks[i] = MicrosecondsToTicks(SERVO_DEFAULT_PULSE_US);
    }

    ElapsedTicks = 0U;
    ScheduledTicks = 0U;

    T2CON = 0;
    TMR2 = 0;

    /*
     * Timer2 prescaler = 1:16.
     *
     * PIC32 timer prescaler encoding:
     *   0 = 1:1
     *   1 = 1:2
     *   2 = 1:4
     *   3 = 1:8
     *   4 = 1:16
     */
    T2CONbits.TCKPS = 4;
    T2CONbits.TCS = 0;
    IPC2bits.T2IP = 4;
    IPC2bits.T2IS = 0;

    // Clear any pending interrupt.
    IFS0CLR = _IFS0_T2IF_MASK;

    // Enable Timer2 interrupt.
    IEC0SET = _IEC0_T2IE_MASK;

    // Start the first servo frame.
    StartServoFrame();

    T2CONbits.ON = 1;
}

/*******************************************************************************
 * PUBLIC SERVO CONTROL FUNCTIONS
 ******************************************************************************/
void Servo_SetPulseUs(ServoID_t servo, uint16_t pulseUs)
{
    if (servo >= SERVO_COUNT) {
        return;
    }

    // Clamp pulse width to a safe generic servo range.
    if (pulseUs < SERVO_MIN_PULSE_US) {
        pulseUs = SERVO_MIN_PULSE_US;
    }

    if (pulseUs > SERVO_MAX_PULSE_US) {
        pulseUs = SERVO_MAX_PULSE_US;
    }

    // Update the pulse width. The Timer2 ISR reads this value when starting each new frame.
    ServoPulseTicks[servo] = MicrosecondsToTicks(pulseUs);
}

// Set a positional servo angle from 0 to 180 degrees.
void Servo_SetAngle(ServoID_t servo, uint8_t angle)
{
    uint16_t pulseUs;

    if (servo >= SERVO_COUNT) {
        return;
    }

    if (angle > 180U) {
        angle = 180U;
    }

    pulseUs = (uint16_t)(1000U + (((uint16_t)angle * 1000U) / 180U));
    Servo_SetPulseUs(servo, pulseUs);
}

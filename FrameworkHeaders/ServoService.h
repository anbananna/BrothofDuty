#ifndef SERVO_SERVICE_H
#define SERVO_SERVICE_H

#include <stdint.h>

typedef enum {
    SERVO_DISPENSE = 0,
    SERVO_CARROT,
    SERVO_SOUP,
    SERVO_COUNT
} ServoID_t;

/*
 * Initialize the servo GPIO pins and Timer2.
 * Call once during system initialization.
 */
void InitServoService(void);

/*
 * Set a servo's pulse width directly in microseconds.
 *
 * Typical positional servo:
 *   1000 us = one end
 *   1500 us = center
 *   2000 us = other end
 *
 * Typical continuous rotation servo:
 *   1000 us = full speed in one direction
 *   1500 us = stop (may require calibration)
 *   2000 us = full speed in the other direction
 */
void Servo_SetPulseUs(ServoID_t servo, uint16_t pulseUs);

/*
 * Set a positional servo to an angle from 0 to 180 degrees.
 * Maps 0-180 degrees to 1000-2000 microseconds.
 */
void Servo_SetAngle(ServoID_t servo, uint8_t angle);

#endif

#include <Arduino.h>
#include "Fusion2F2CP.h"

/*
 * Example: tune Fusion2 to 5880 MHz once, directly from firmware code.
 *
 * Add the call to the main loop or to an application task. No serial command
 * is involved. The example waits until pairing and secure session setup have
 * completed before sending the request.
 */
void serviceTuneExample()
{
    static bool commandSent = false;
    static uint32_t retryAtMs = 0;

    if (commandSent || !Fusion2F2CP_IsConnected())
        return;

    if (static_cast<int32_t>(millis() - retryAtMs) < 0)
        return;

    if (Fusion2F2CP_Tune(5880, true)) {
        commandSent = true;
        Serial.println("Code API: Fusion2 tuned to 5880 MHz.");
    } else {
        retryAtMs = millis() + 2000u;
        Serial.println("Code API: tune failed; retry scheduled.");
    }
}

/* Call this from the project's existing loop():
 *
 *     serviceTuneExample();
 */

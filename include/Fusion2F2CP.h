#pragma once

#include <stdint.h>

/*
 * Minimal application-facing F2CP API.
 *
 * These functions allow firmware code to control Fusion2 directly without
 * passing text through the serial command parser.
 *
 * Call command functions from normal task/loop context only. Do not call them
 * from an interrupt service routine or directly from an ESP-NOW callback.
 */

/* True when a Fusion2 peer record exists in local NVS. */
bool Fusion2F2CP_IsPaired();

/* True when the encrypted F2CP session is currently ready for commands. */
bool Fusion2F2CP_IsConnected();

/*
 * Tune Fusion2 to an exact frequency in MHz.
 *
 * mhz  : requested frequency, normally within the Fusion2 5.8 GHz range.
 * save : true stores the frequency in Fusion2; false applies it temporarily.
 *
 * Returns true when Fusion2 replies with the expected acknowledgement.
 */
bool Fusion2F2CP_Tune(uint16_t mhz, bool save = true);

/*
  Customized version for Miyoo-Mini handheld.
  Only tested under Miyoo-Mini stock OS (original firmware) with Parasyte compatible layer.

  Copyright (C) 1997-2022 Sam Lantinga <slouken@libsdl.org>
  Copyright (C) 2022-2022 Steward Fu <steward.fu@gmail.com>


  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you must not
     claim that you wrote the original software. If you use this software
     in a product, an acknowledgment in the product documentation would be
     appreciated but is not required.
  2. Altered source versions must be plainly marked as such, and must not be
     misrepresented as being the original software.
  3. This notice may not be removed or altered from any source distribution.
*/
#include "../../SDL_internal.h"

#if defined(SDL_JOYSTICK_MMIYOO)

#include "SDL_events.h"
#include "SDL_joystick.h"
#include "SDL_gamecontroller.h"
#include "../SDL_sysjoystick.h"
#include "../SDL_joystick_c.h"
#include "../../video/mmiyoo/SDL_event_mmiyoo.h"

extern MMIYOO_EventInfo MMiyooEventInfo;

/* Button order matches MYKEY_UP..MYKEY_START (0..13) in SDL_event_mmiyoo.h,
 * which is also the bit layout of MMiyooEventInfo.keypad.bitmaps. Keeping the
 * joystick button index identical to the MYKEY_* value lets JoystickUpdate()
 * turn the bitmap straight into button indices without a translation table.
 */
#define MMIYOO_JOYSTICK_NBUTTONS (MYKEY_START + 1)

static uint32_t last_bitmaps = 0;

static SDL_JoystickGUID MMIYOO_JoystickGetDeviceGUID(int device_index);

static int MMIYOO_JoystickInit(void)
{
    SDL_JoystickGUID guid = MMIYOO_JoystickGetDeviceGUID(0);
    char guid_str[33];
    char mapping[256];

    SDL_JoystickGetGUIDString(guid, guid_str, sizeof(guid_str));
    SDL_snprintf(mapping, sizeof(mapping),
        "%s,MMiyoo,"
        "a:b%d,b:b%d,x:b%d,y:b%d,back:b%d,start:b%d,"
        "leftshoulder:b%d,rightshoulder:b%d,lefttrigger:b%d,righttrigger:b%d,"
        "dpup:b%d,dpdown:b%d,dpleft:b%d,dpright:b%d,",
        /* Physical A/B and X/Y are swapped here on purpose: the Miyoo's
         * face buttons are labeled Nintendo-style (A right, B bottom, X top,
         * Y left) but SDL_CONTROLLER_BUTTON_A/B/X/Y follow Xbox-style
         * position semantics (A bottom, B right, X left, Y top) - hosts
         * expect logical buttons by position, not by the physical label. */
        guid_str,
        MYKEY_B, MYKEY_A, MYKEY_Y, MYKEY_X, MYKEY_SELECT, MYKEY_START,
        MYKEY_L1, MYKEY_R1, MYKEY_L2, MYKEY_R2,
        MYKEY_UP, MYKEY_DOWN, MYKEY_LEFT, MYKEY_RIGHT);
    SDL_GameControllerAddMapping(mapping);

    last_bitmaps = 0;
    return 1;
}

static int MMIYOO_JoystickGetCount(void)
{
    return 1;
}

static void MMIYOO_JoystickDetect(void)
{
}

static const char* MMIYOO_JoystickGetDeviceName(int device_index)
{
    return "MMiyoo Joystick";
}

static int MMIYOO_JoystickGetDevicePlayerIndex(int device_index)
{
    return -1;
}

static void MMIYOO_JoystickSetDevicePlayerIndex(int device_index, int player_index)
{
}

static SDL_JoystickGUID MMIYOO_JoystickGetDeviceGUID(int device_index)
{
    /* Fixed GUID: the device is built into the console, not hot-pluggable,
     * so a stable, self-picked identifier is fine (no vendor/product IDs
     * to report). MMIYOO_JoystickInit() derives the gamepad mapping string
     * from this same value via SDL_JoystickGetGUIDString(), so the two
     * always agree without hand-encoding a matching hex string here. */
    SDL_JoystickGUID guid;
    SDL_zero(guid);
    guid.data[0] = 'M';
    guid.data[1] = 'M';
    guid.data[2] = 'i';
    guid.data[3] = 'y';
    guid.data[4] = 'o';
    guid.data[5] = 'o';
    return guid;
}

static SDL_JoystickID MMIYOO_JoystickGetDeviceInstanceID(int device_index)
{
    return device_index;
}

static int MMIYOO_JoystickOpen(SDL_Joystick *joystick, int device_index)
{
    joystick->nbuttons = MMIYOO_JOYSTICK_NBUTTONS;
    joystick->naxes = 0;
    joystick->nhats = 0;
    last_bitmaps = 0;
    return 0;
}

static int MMIYOO_JoystickRumble(SDL_Joystick *joystick, Uint16 low_frequency_rumble, Uint16 high_frequency_rumble)
{
    return SDL_Unsupported();
}

static int MMIYOO_JoystickRumbleTriggers(SDL_Joystick *joystick, Uint16 left_rumble, Uint16 right_rumble)
{
    return SDL_Unsupported();
}

static Uint32 MMIYOO_JoystickGetCapabilities(SDL_Joystick *joystick)
{
    return 0;
}

static int MMIYOO_JoystickSetLED(SDL_Joystick *joystick, Uint8 red, Uint8 green, Uint8 blue)
{
    return SDL_Unsupported();
}

static int MMIYOO_JoystickSendEffect(SDL_Joystick *joystick, const void *data, int size)
{
    return SDL_Unsupported();
}

static int MMIYOO_JoystickSetSensorsEnabled(SDL_Joystick *joystick, SDL_bool enabled)
{
    return SDL_Unsupported();
}

static void MMIYOO_JoystickUpdate(SDL_Joystick *joystick)
{
    uint32_t cur = MMiyooEventInfo.keypad.bitmaps;
    uint32_t changed = cur ^ last_bitmaps;
    int i;

    if (!changed)
        return;

    for (i = 0; i < MMIYOO_JOYSTICK_NBUTTONS; i++) {
        if (changed & (1u << i)) {
            SDL_PrivateJoystickButton(joystick, (Uint8)i,
                (cur & (1u << i)) ? SDL_PRESSED : SDL_RELEASED);
        }
    }

    last_bitmaps = cur;
}

static void MMIYOO_JoystickClose(SDL_Joystick *joystick)
{
}

static void MMIYOO_JoystickQuit(void)
{
}

static SDL_bool MMIYOO_JoystickGetGamepadMapping(int device_index, SDL_GamepadMapping *out)
{
    return SDL_FALSE;
}

SDL_JoystickDriver SDL_MMIYOO_JoystickDriver = {
    MMIYOO_JoystickInit,
    MMIYOO_JoystickGetCount,
    MMIYOO_JoystickDetect,
    MMIYOO_JoystickGetDeviceName,
    MMIYOO_JoystickGetDevicePlayerIndex,
    MMIYOO_JoystickSetDevicePlayerIndex,
    MMIYOO_JoystickGetDeviceGUID,
    MMIYOO_JoystickGetDeviceInstanceID,
    MMIYOO_JoystickOpen,
    MMIYOO_JoystickRumble,
    MMIYOO_JoystickRumbleTriggers,
    MMIYOO_JoystickGetCapabilities,
    MMIYOO_JoystickSetLED,
    MMIYOO_JoystickSendEffect,
    MMIYOO_JoystickSetSensorsEnabled,
    MMIYOO_JoystickUpdate,
    MMIYOO_JoystickClose,
    MMIYOO_JoystickQuit,
    MMIYOO_JoystickGetGamepadMapping
};

#endif

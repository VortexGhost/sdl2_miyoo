/*
  Customized version for Miyoo-Mini handheld.
  Only tested under Miyoo-Mini stock OS (original firmware) with Parasyte compatible layer.

  Copyright (C) 1997-2022 Sam Lantinga <slouken@libsdl.org>
  Copyright (C) 2022-2022 Steward Fu <steward.fu@gmail.com>

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute itqpte
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

#if SDL_VIDEO_DRIVER_MMIYOO

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <linux/input.h>
#include "../../events/SDL_events_c.h"
#include "../../core/linux/SDL_evdev.h"
#include "../../thread/SDL_systhread.h"
#include <signal.h>
#include <time.h>

#include "SDL_video_mmiyoo.h"
#include "SDL_event_mmiyoo.h"
#include "../../cfg/SDL_moonlightcfg_mmiyoo.h"

MMIYOO_EventInfo MMiyooEventInfo = {0};

extern MOON moon;
extern MMIYOO_VideoInfo MMiyooVideoInfo;

static int running = 0;
static int event_fd = -1;
static int is_stock_system = 0;
static SDL_sem *event_sem = NULL;
static SDL_Thread *thread = NULL;
static uint32_t pre_keypad_bitmaps = 0;

int volume_inc(void);
int volume_dec(void);

int specialKey = 0;
int sendEscape = 0;
int hotkeyEnable = 1;
int r2Hold = 0;

uint32_t hotkey = 0;

// move to struct eventually
int scaleFactor = 2;
static float acceleration = 2.0;
float accelerationRate = 2.5;
float maxAcceleration = 10.0;

void sendSpecial() {
    if (specialKey) {
        SDL_SendKeyboardKey(SDL_PRESSED, SDL_SCANCODE_LCTRL);
        SDL_SendKeyboardKey(SDL_PRESSED, SDL_SCANCODE_LSHIFT);
        SDL_SendKeyboardKey(SDL_PRESSED, SDL_SCANCODE_LALT);
        SDL_SendKeyboardKey(SDL_PRESSED, specialKey);
        
        SDL_SendKeyboardKey(SDL_RELEASED, specialKey);

        SDL_SendKeyboardKey(SDL_RELEASED, SDL_SCANCODE_LCTRL);
        SDL_SendKeyboardKey(SDL_RELEASED, SDL_SCANCODE_LSHIFT);
        SDL_SendKeyboardKey(SDL_RELEASED, SDL_SCANCODE_LALT);
        
        specialKey = 0;
    }
}

void modeSwitchKeypad() {
    hotkeyEnable = 1;
    MMiyooEventInfo.mode = MMIYOO_KEYPAD_MODE;
    MMiyooEventInfo.keypad.bitmaps &= ~(1 << MYKEY_START);
    printf("Mode switched to KEYPAD\n");
}

void modeSwitchMouse() {
    MMiyooEventInfo.mode = MMIYOO_MOUSE_MODE;
    hotkey = 0;
    hotkeyEnable = 0;
    printf("Mode switched to MOUSE\n");
}

void handleR2Event(struct input_event ev) { // R2 for speeding up the mouse
    static int copy = 0;
    static int isCopied = 0;
    
    if(ev.value == 1) {
        r2Hold = 1;
        if (!isCopied && moon.customkey.R2 != 0) {
            copy = moon.customkey.R2;
            moon.customkey.R2 = 0;
            isCopied = 1;
        }
    } else if(ev.value == 0) {
        r2Hold = 0;
        if (isCopied) {
            moon.customkey.R2 = copy;
            isCopied = 0; 
        }
    }
}

void updateMousePosition(int xDirection, int yDirection) {
    static clock_t last_mouse_update_time = 0;
    clock_t current_time = clock();
    float move = 1.0;
    int xIncrement = 0, yIncrement = 0;

    if (last_mouse_update_time != 0) {
        move = ((float)current_time - last_mouse_update_time) / 8000;
    }
    
    if (move <= 0.0) {
        move = 1.0;
    }

    xIncrement = (int)(MOVE_INCREMENT * move * scaleFactor * acceleration);
    yIncrement = (int)(MOVE_INCREMENT * move * scaleFactor * acceleration);

    if (r2Hold == 1) { // when r2 is held, speed up the mouse
        xIncrement *= 2;
        yIncrement *= 2;
    }

    acceleration *= accelerationRate;
    if (acceleration > maxAcceleration) {
        acceleration = maxAcceleration;
    }

    if (xDirection < 0) {
        MMiyooEventInfo.mouse.x -= xIncrement;
    } else if (xDirection > 0) {
        MMiyooEventInfo.mouse.x += xIncrement;
    }

    if (yDirection < 0) {
        MMiyooEventInfo.mouse.y -= yIncrement;
    } else if (yDirection > 0) {
        MMiyooEventInfo.mouse.y += yIncrement;
    }

    if (MMiyooEventInfo.mouse.y < MMiyooEventInfo.mouse.miny) {
        MMiyooEventInfo.mouse.y = MMiyooEventInfo.mouse.miny;
    }
    if (MMiyooEventInfo.mouse.y > MMiyooEventInfo.mouse.maxy) {
        MMiyooEventInfo.mouse.y = MMiyooEventInfo.mouse.maxy;
    }
    if (MMiyooEventInfo.mouse.x < MMiyooEventInfo.mouse.minx) {
        MMiyooEventInfo.mouse.x = MMiyooEventInfo.mouse.minx;
    }
    if (MMiyooEventInfo.mouse.x >= MMiyooEventInfo.mouse.maxx) {
        MMiyooEventInfo.mouse.x = MMiyooEventInfo.mouse.maxx;
    }
        
        // printf("Updated X-coordinate: %d, Y-coordinate: %d\n", MMiyooEventInfo.mouse.x, MMiyooEventInfo.mouse.y);

    last_mouse_update_time = current_time;
}

void flushEvents(void) {
    SDL_FlushEvents(SDL_FIRSTEVENT, SDL_LASTEVENT);
}

int EventUpdate(void *data);

void MMIYOO_EventInit(void)
{
    DIR *dir = NULL;

    pre_keypad_bitmaps = 0;
    memset(&MMiyooEventInfo, 0, sizeof(MMiyooEventInfo));
    MMiyooEventInfo.mouse.minx = 0;
    MMiyooEventInfo.mouse.miny = 0;
    MMiyooEventInfo.mouse.maxx = 640;
    MMiyooEventInfo.mouse.maxy = 480;
    MMiyooEventInfo.mouse.x = 0;
    MMiyooEventInfo.mouse.y = 0;
    MMiyooEventInfo.mode = MMIYOO_KEYPAD_MODE;

#if defined(MMIYOO)
    event_fd = open("/dev/input/event0", O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if(event_fd < 0){
        printf("failed to open /dev/input/event0\n");
    }
#endif

    if((event_sem =  SDL_CreateSemaphore(1)) == NULL) {
        SDL_SetError("Can't create input semaphore");
        return;
    }

    running = 1;
    if((thread = SDL_CreateThreadInternal(EventUpdate, "MMIYOOInputThread", 4096, NULL)) == NULL) {
        SDL_SetError("Can't create input thread");
        return;
    }


    dir = opendir("/mnt/SDCARD/.tmp_update");
    if (dir) {
        closedir(dir);
    }
    else {
        is_stock_system = 1;
        printf("Run on stock system\n");
    }
}

void MMIYOO_EventDeinit(void)
{
    running = 0;
    SDL_WaitThread(thread, NULL);
    SDL_DestroySemaphore(event_sem);
    if(event_fd > 0) {
        close(event_fd);
        event_fd = -1;
    }
}

int EventUpdate(void *data)
{

    struct input_event ev = {0};
    uint32_t bit = 0;

    while (running) {
        SDL_SemWait(event_sem);
        if (event_fd > 0) {
            if (read(event_fd, &ev, sizeof(struct input_event))) {
                if ((ev.type == EV_KEY) && (ev.value != 2)) {
                    //printf("%s, code:%d\n", __func__, ev.code);

                    switch (ev.code) {
                        case 18:  bit = (1 << MYKEY_L1);      break;
                        case 15:  bit = (1 << MYKEY_L2);      break;
                        case 20:  bit = (1 << MYKEY_R1);      break;     
                        case 14:
                            bit = (1 << MYKEY_R2);
                            if (MMiyooEventInfo.mode == MMIYOO_MOUSE_MODE) {
                                handleR2Event(ev);
                            }
                            break;
                        case 103: bit = (1 << MYKEY_UP);      break;
                        case 108: bit = (1 << MYKEY_DOWN);    break;
                        case 105: bit = (1 << MYKEY_LEFT);    break;
                        case 106: bit = (1 << MYKEY_RIGHT);   break;
                        case 57:  bit = (1 << MYKEY_A);       break;
                        case 29:  bit = (1 << MYKEY_B);       break;
                        case 42:  bit = (1 << MYKEY_X);       break;
                        case 56:  bit = (1 << MYKEY_Y);       break;
                        case 28:  bit = (1 << MYKEY_START);   break;
                        case 97:  bit = (1 << MYKEY_SELECT);  break;
                        case 1:   bit = (1 << MYKEY_MENU);    break;
                        case 116: bit = (1 << MYKEY_POWER);   break;
                        case 115: bit = (1 << MYKEY_VOLUP);
                    }
                    
                    if(bit){
                        if(ev.value){
                            MMiyooEventInfo.keypad.bitmaps|= bit;
                        }
                        else{
                            MMiyooEventInfo.keypad.bitmaps&= ~bit;
                        }
                    }
                                                            
                    if (hotkeyEnable) {
                        hotkey = MMiyooEventInfo.keypad.bitmaps & (1 << MYKEY_SELECT);
                        // printf("Hotkey is enabled.\n");
                        // printf("Current hotkey state: %d\n", hotkey);
                    } else {
                        if (MMiyooEventInfo.keypad.bitmaps & (1 << MYKEY_START)) {
                            modeSwitchKeypad();
                            MMiyooEventInfo.keypad.bitmaps &= ~(1 << MYKEY_START);
                        }
                    }

                    // Special key handling - this needs improving but for now it's fine
                    
                    if (hotkey && (MMiyooEventInfo.keypad.bitmaps & (1 << MYKEY_L1))) { // Screen 1
                        specialKey = SDL_SCANCODE_F1;
                        MMiyooEventInfo.keypad.bitmaps &= ~(1 << MYKEY_L1);
                    } else if (hotkey && (MMiyooEventInfo.keypad.bitmaps & (1 << MYKEY_L2))) { // Screen 2
                        specialKey = SDL_SCANCODE_F2;
                        MMiyooEventInfo.keypad.bitmaps &= ~(1 << MYKEY_L2);
                    } else if (hotkey && (MMiyooEventInfo.keypad.bitmaps & (1 << MYKEY_R1))) { // Screen 3
                        specialKey = SDL_SCANCODE_F3;
                        MMiyooEventInfo.keypad.bitmaps &= ~(1 << MYKEY_R1);
                    } else if (hotkey && (MMiyooEventInfo.keypad.bitmaps & (1 << MYKEY_R2))) { // Screen 4
                        specialKey = SDL_SCANCODE_F4;
                        MMiyooEventInfo.keypad.bitmaps &= ~(1 << MYKEY_R2);
                    } else if (hotkey && (MMiyooEventInfo.keypad.bitmaps & (1 << MYKEY_MENU))) { // Quits
                        specialKey = SDL_SCANCODE_Q; 
                        MMiyooEventInfo.keypad.bitmaps &= ~(1 << MYKEY_MENU);
                        hotkey = 0;
                        running = 0;
                    } else if (hotkey && (MMiyooEventInfo.keypad.bitmaps & (1 << MYKEY_Y))) { // Geforce Experience
                        specialKey = SDL_SCANCODE_Z; 
                        MMiyooEventInfo.keypad.bitmaps &= ~(1 << MYKEY_Y);
                    } else if (hotkey && (MMiyooEventInfo.keypad.bitmaps & (1 << MYKEY_A))) {
                        modeSwitchMouse();
                        MMiyooEventInfo.keypad.bitmaps &= ~(1 << MYKEY_A);
                    }
                }
            }
        }
        SDL_SemPost(event_sem);
        usleep(1000000 / 120);
    }
    return 0;
}

void MMIYOO_PumpEvents(_THIS)
{
    const SDL_Scancode code[]={
        moon.customkey.UpDpad, moon.customkey.DownDpad, moon.customkey.LeftDpad, moon.customkey.RightDpad, // Up/Down/Left/Right
        moon.customkey.A, moon.customkey.B, moon.customkey.X, moon.customkey.Y, // A/B/X/Y
        moon.customkey.L1, moon.customkey.R1, moon.customkey.L2, moon.customkey.R2,    // L1/R1/L2/R2
        moon.customkey.Select,                           // Select 
        moon.customkey.Start, moon.customkey.Menu, SDLK_HOME, 
        SDLK_0, SDLK_1, SDLK_2, SDLK_3,
        SDLK_HOME, SDLK_BACKSPACE 
    };
    
    int updated = 0;
    int xDirection, yDirection;
    
    SDL_SemWait(event_sem);

    if (specialKey) { // handles moonlight key combos (CTRL+ALT+SHIFT+_)
        sendSpecial();
    }

    if (!hotkeyEnable) {
        SDL_SendKeyboardKey(SDL_RELEASED, moon.customkey.Select);
    }

    if (MMiyooEventInfo.mode == MMIYOO_KEYPAD_MODE) {
        if (pre_keypad_bitmaps != MMiyooEventInfo.keypad.bitmaps) {
            int cc = 0;
            uint32_t v0 = pre_keypad_bitmaps;
            uint32_t v1 = MMiyooEventInfo.keypad.bitmaps;

            /* MYKEY_UP..MYKEY_START (0..MYKEY_START) are now reported as real
             * SDL joystick buttons by the mmiyoo joystick driver (which reads
             * this same MMiyooEventInfo.keypad.bitmaps). Sending them as
             * synthetic keyboard keys too would make every press arrive at
             * the host twice - once as a key, once as a gamepad button - so
             * skip those bits here and only keep the keyboard path for
             * everything above MYKEY_START (Menu, and whatever else uses
             * this table). Bits still have to be shifted for every entry to
             * keep cc aligned with the bitmap position. */
            for (cc=0; cc<=MYKEY_LAST_BITS; cc++) {
                if (cc > MYKEY_START && (v0 & 1) != (v1 & 1)) {
                    SDL_SendKeyboardKey((v1 & 1) ? SDL_PRESSED : SDL_RELEASED, SDL_GetScancodeFromKey(code[cc]));
                }
                v0>>= 1;
                v1>>= 1;
            }

            pre_keypad_bitmaps = MMiyooEventInfo.keypad.bitmaps;
        }
    } else {
        if (pre_keypad_bitmaps != MMiyooEventInfo.keypad.bitmaps) {
            uint32_t cc = 0;
            uint32_t v0 = pre_keypad_bitmaps;
            uint32_t v1 = MMiyooEventInfo.keypad.bitmaps;
                       
            if ((v0 & (1 << MYKEY_A)) != (v1 & (1 << MYKEY_A))) {
                SDL_SendMouseButton(MMiyooVideoInfo.window, 0, (v1 & (1 << MYKEY_A)) ? SDL_PRESSED : SDL_RELEASED, SDL_BUTTON_LEFT);
            }
            
            if ((v0 & (1 << MYKEY_B)) != (v1 & (1 << MYKEY_B))) {
                SDL_SendMouseButton(MMiyooVideoInfo.window, 0, (v1 & (1 << MYKEY_B)) ? SDL_PRESSED : SDL_RELEASED, SDL_BUTTON_RIGHT);
            }
            
            for (cc=0; cc<=MYKEY_LAST_BITS; cc++) {
                if ((cc == MYKEY_SELECT) || (cc == MYKEY_R2)) {
                    if ((v0 & 1) != (v1 & 1)) {
                        SDL_SendKeyboardKey((v1 & 1) ? SDL_PRESSED : SDL_RELEASED, SDL_GetScancodeFromKey(code[cc]));
                    }
                }
                v0>>= 1;
                v1>>= 1;
            }
        }
        
        xDirection = 0;
        yDirection = 0;

        if (MMiyooEventInfo.keypad.bitmaps & (1 << MYKEY_UP)) {
            yDirection -= 1;
        }
        if (MMiyooEventInfo.keypad.bitmaps & (1 << MYKEY_DOWN)) {
            yDirection += 1;
        }
        if (MMiyooEventInfo.keypad.bitmaps & (1 << MYKEY_LEFT)) {
            xDirection -= 1;
        }
        if (MMiyooEventInfo.keypad.bitmaps & (1 << MYKEY_RIGHT)) {
            xDirection += 1;
        }

        if (xDirection != 0 || yDirection != 0) {
            updated = 1;
            updateMousePosition(xDirection, yDirection);
        }
        
        if (updated) {
            SDL_SendMouseMotion(MMiyooVideoInfo.window, 0, 0, MMiyooEventInfo.mouse.x, MMiyooEventInfo.mouse.y);
        } else {
            acceleration = 2.0;
        }
        
        pre_keypad_bitmaps = MMiyooEventInfo.keypad.bitmaps;
    }
    SDL_SemPost(event_sem);
}

#endif


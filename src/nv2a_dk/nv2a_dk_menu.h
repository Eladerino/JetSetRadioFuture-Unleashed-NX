/* Deko3D's lightweight controller menu. */
#ifndef NV2A_DK_MENU_H
#define NV2A_DK_MENU_H

#include <stdint.h>

enum {
    DK_MENU_UP       = 1u << 0,
    DK_MENU_DOWN     = 1u << 1,
    DK_MENU_LEFT     = 1u << 2,
    DK_MENU_RIGHT    = 1u << 3,
    DK_MENU_SELECT   = 1u << 4,
    DK_MENU_CANCEL   = 1u << 5,
    DK_MENU_TAB_LEFT = 1u << 6,
    DK_MENU_TAB_RIGHT= 1u << 7,
};

int  xbox_MenuIsOpen(void);
void xbox_MenuSetOpen(int on);
void nv2a_dk_menu_input(uint32_t buttons);

#endif

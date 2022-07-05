#ifndef VIDEO_SCREEN_ASM_H
#define VIDEO_SCREEN_ASM_H

#include <stdint.h>

extern void asm_screen_save(void);
extern void asm_screen_restore(void);
extern void asm_screen_set_vram(const void* pScreen);
extern void asm_screen_set_falcon_palette(const uint32_t* pPalette);
extern void asm_screen_set_ste_palette(const uint16_t* pPalette);

#endif

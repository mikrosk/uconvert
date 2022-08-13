#ifndef VIDEO_SCREEN_ASM_H
#define VIDEO_SCREEN_ASM_H

#include <stdint.h>

extern void asm_screen_ste_save(void);
extern void asm_screen_tt_save(void);
extern void asm_screen_falcon_save(void);

extern void asm_screen_ste_restore(void);
extern void asm_screen_tt_restore(void);
extern void asm_screen_falcon_restore(void);

extern void asm_screen_set_ste_palette(const uint16_t* pPalette);
extern void asm_screen_set_tt_palette(const uint16_t* pPalette);
extern void asm_screen_set_falcon_palette(const uint32_t* pPalette);

extern void asm_screen_set_vram(const void* pScreen);

#endif

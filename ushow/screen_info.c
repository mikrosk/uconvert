#include "screen_info.h"

#include <mint/cookie.h>
#include <mint/falcon.h>
#include <stdbool.h>

ScreenInfo get_screen_info(const BitmapInfo* bitmap_info, const VdoValue vdo_val)
{
    ScreenInfo screen_info = {};
    screen_info.rez  = screen_info.old_rez  = -1;
    screen_info.mode = screen_info.old_mode = -1;

    if (vdo_val == VdoValueST || vdo_val == VdoValueSTE) {
        screen_info.old_rez = Getrez();
        bool mono_monitor = screen_info.old_rez == 2;

        switch (bitmap_info->bpp) {
        case 1:
            if (mono_monitor && bitmap_info->bpc <= 0) {
                // packed chunks (-1) = planar (0) if 1bpp
                screen_info.width  = 640;
                screen_info.height = 400;
                screen_info.bpp    = 1;
                screen_info.rez    = RezValueSTHigh;
            }
            break;
        case 2:
            if (!mono_monitor && bitmap_info->bpc == 0) {
                // planar only
                screen_info.width  = 640;
                screen_info.height = 200;
                screen_info.bpp    = 2;
                screen_info.rez    = RezValueSTMid;
            }
            break;
        case 4:
            if (!mono_monitor && (bitmap_info->bpc == 0 || bitmap_info->bpc == 1)) {
                screen_info.width  = 320;
                screen_info.height = 200;
                screen_info.bpp    = 4;
                screen_info.rez    = RezValueSTLow;
            }
            break;
        }
    } else if (vdo_val == VdoValueTT) {
        screen_info.old_rez = Getrez();
        bool mono_monitor = screen_info.old_rez == RezValueTTHigh;

        switch (bitmap_info->bpp) {
        case 1:
            if (bitmap_info->bpc <= 0) {
                // packed chunks (-1) = planar (0) if 1bpp
                screen_info.width  = mono_monitor ? 1280 : 640;
                screen_info.height = mono_monitor ? 960  : 400;
                screen_info.bpp    = 1;
                screen_info.rez    = mono_monitor ? RezValueTTHigh : RezValueSTHigh;
            }
            break;
        case 2:
            if (!mono_monitor && bitmap_info->bpc == 0) {
                // planar only
                screen_info.width  = 640;
                screen_info.height = 200;
                screen_info.bpp    = 2;
                screen_info.rez    = RezValueSTMid;
            }
            break;
        case 4:
            if (!mono_monitor && (bitmap_info->bpc == 0 || bitmap_info->bpc == 1)) {
                screen_info.width  = bitmap_info->palette_type == PaletteTypeSTE ? 320 : 640;
                screen_info.height = bitmap_info->palette_type == PaletteTypeSTE ? 200 : 480;
                screen_info.bpp    = 4;
                screen_info.rez    = bitmap_info->palette_type == PaletteTypeSTE ? RezValueSTLow : RezValueTTMid;
            }
            break;
        case 8:
            if (!mono_monitor && (bitmap_info->bpc == 0 || bitmap_info->bpc == 1)) {
                screen_info.width  = 320;
                screen_info.height = 480;
                screen_info.bpp    = 8;
                screen_info.rez    = RezValueTTLow;
            }
            break;
        }
    } else if (vdo_val == VdoValueFalcon) {
        screen_info.old_mode = VsetMode(VM_INQUIRE);
        bool supervidel = Getcookie(C_SupV, NULL) == C_FOUND && VgetMonitor() == MON_VGA;
        bool mono_monitor = VgetMonitor() == MON_MONO;

        switch (bitmap_info->bpp) {
        case 1:
            if (bitmap_info->bpc <= 0) {
                // packed chunks (-1) = planar (0) if 1bpp
                screen_info.width  = mono_monitor || bitmap_info->palette_type == PaletteTypeSTE ? 640 : 320;
                screen_info.height = mono_monitor || bitmap_info->palette_type == PaletteTypeSTE ? 400 : (VgetMonitor() != MON_VGA ? 200 : 240);
                screen_info.bpp    = 1;  // duochrome
                screen_info.mode   = BPS1;
            }
            break;
        case 2:
            if (!mono_monitor && bitmap_info->bpc == 0) {
                // planar only
                screen_info.width  = bitmap_info->palette_type == PaletteTypeSTE ? 640 : 320;
                screen_info.height = bitmap_info->palette_type == PaletteTypeSTE || VgetMonitor() != MON_VGA ? 200 : 240;
                screen_info.bpp    = 2;
                screen_info.mode   = BPS2;
            }
            break;
        case 4:
            if (!mono_monitor && (bitmap_info->bpc == 0 || bitmap_info->bpc == 1)) {
                screen_info.width  = 320;
                screen_info.height = bitmap_info->palette_type == PaletteTypeSTE || VgetMonitor() != MON_VGA ? 200 : 240;
                screen_info.bpp    = 4;
                screen_info.mode   = BPS4;
            }
            break;
        case 8:
            if (!mono_monitor && (bitmap_info->bpc == 0 || bitmap_info->bpc == 1)) {
                screen_info.width  = 320;
                screen_info.height = VgetMonitor() != MON_VGA ? 200 : 240;
                screen_info.bpp    = 8;
                screen_info.mode   = bitmap_info->bpc == 1 && supervidel ? BPS8C : BPS8;
            }
            break;
        case 16:
            if (!mono_monitor && bitmap_info->bpc == 2) {
                screen_info.width  = 320;
                screen_info.height = VgetMonitor() != MON_VGA ? 200 : 240;
                screen_info.bpp    = 16;
                screen_info.mode   = BPS16;
            }
            break;
        case 32:
            if (supervidel && bitmap_info->bpc == 4) {
                screen_info.width  = 320;
                screen_info.height = 240;
                screen_info.bpp    = 32;
                screen_info.mode   = SVEXT | BPS32;
            }
            break;
        }

        if (screen_info.mode != -1) {
            bool changed = false;
            screen_info.mode |= (screen_info.old_mode & VGA);
            screen_info.mode |= (screen_info.old_mode & PAL);

            // on Falcon we always create multiplies of 320x240 (VGA) / 320x200 (RGB/TV)
            if (bitmap_info->palette_type != PaletteTypeSTE && (bitmap_info->width > screen_info.width || bitmap_info->height > screen_info.height)) {
                // applies to pure Falcon resolutions only
                if (!mono_monitor && VgetMonitor() != MON_VGA && !supervidel) {
                    changed = true;
                    // try overscan first
                    if (bitmap_info->width <= screen_info.width*1.2 && bitmap_info->height <= screen_info.height*1.2) {
                        screen_info.mode   |= COL40 | OVERSCAN;
                        screen_info.width  *= 1.2;
                        screen_info.height *= 1.2;
                    } else if (bitmap_info->width <= screen_info.width*2 && bitmap_info->height <= screen_info.height*2) {
                        screen_info.mode   |= COL80 | VERTFLAG;
                        screen_info.width  *= 2;
                        screen_info.height *= 2;
                    } else {
                        screen_info.mode   |= COL80 | VERTFLAG | OVERSCAN;
                        screen_info.width  *= 1.2 * 2;
                        screen_info.height *= 1.2 * 2;
                    }
                } else if (VgetMonitor() == MON_VGA) {
                    // try non-SuperVidel options first
                    if (bitmap_info->bpp <= 8 && ((bitmap_info->width <= screen_info.width*2 && bitmap_info->height <= screen_info.height*2) || !supervidel)) {
                        changed = true;
                        screen_info.mode   |= COL80;
                        screen_info.width  *= 2;
                        screen_info.height *= 2;
                    } else if (supervidel) {
                        changed = true;
                        // at this point we are going full SuperVidel (640x480@16bpp works without SVEXT, too but why bother)
                        screen_info.mode |= COL80 | SVEXT;

                        if (bitmap_info->width <= 640 && bitmap_info->height <= 480) {
                            screen_info.mode  |= SVEXT_BASERES(0);
                            screen_info.width  = 640;
                            screen_info.height = 480;
                        } else if (bitmap_info->width <= 800 && bitmap_info->height <= 600) {
                            screen_info.mode  |= SVEXT_BASERES(1);
                            screen_info.width  = 800;
                            screen_info.height = 600;
                        } else if (bitmap_info->width <= 1024 && bitmap_info->height <= 768) {
                            screen_info.mode  |= SVEXT_BASERES(2);
                            screen_info.width  = 1024;
                            screen_info.height = 768;
                        } else if (bitmap_info->width <= 1280 && bitmap_info->height <= 720) {
                            screen_info.mode  |= SVEXT_BASERES(0) | OVERSCAN;
                            screen_info.width  = 1280;
                            screen_info.height = 720;
                        } else if (bitmap_info->width <= 1280 && bitmap_info->height <= 1024) {
                            screen_info.mode  |= SVEXT_BASERES(3);
                            screen_info.width  = 1280;
                            screen_info.height = 1024;
                        } else if (bitmap_info->width <= 1680 && bitmap_info->height <= 1050) {
                            screen_info.mode  |= SVEXT_BASERES(1) | OVERSCAN;
                            screen_info.width  = 1680;
                            screen_info.height = 1050;
                        } else if (bitmap_info->width <= 1600 && bitmap_info->height <= 1200) {
                            screen_info.mode  |= SVEXT_BASERES(4);
                            screen_info.width  = 1600;
                            screen_info.height = 1200;
                        } else {
                            screen_info.mode  |= SVEXT_BASERES(2) | OVERSCAN;
                            screen_info.width  = 1920;
                            screen_info.height = 1080;
                        }
                    }
                }
            }

            if (!changed) {
                // ST-compatible modes or no change at all
                if (screen_info.width == 640)
                    screen_info.mode |= COL80;
                else if (screen_info.width == 320)
                    screen_info.mode |= COL40;

                if (bitmap_info->palette_type == PaletteTypeSTE)
                    screen_info.mode |= STMODES;

                if (((screen_info.mode & VGA) && (screen_info.height == 200 || screen_info.height == 240))
                        || (!(screen_info.mode & VGA) && (screen_info.height == 400 || screen_info.height == 480)))
                    screen_info.mode |= VERTFLAG;
            }
        }
    }

    return screen_info;
}

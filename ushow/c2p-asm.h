/*
 * ushow: Atari ST/STE/TT/Falcon-specific bitmap viewer
 *
 * Copyright (c) 2022 Miro Kropacek <miro.kropacek@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 */

#ifndef C2P_ASM_H
#define C2P_ASM_H

extern void c2p1x1_4_falcon(const char* pChunky, const char* pChunkyEnd, char* pScreen);
extern void c2p1x1_6_falcon(const char* pChunky, const char* pChunkyEnd, char* pScreen);
extern void c2p1x1_8_falcon(const char* pChunky, const char* pChunkyEnd, char* pScreen);

#endif

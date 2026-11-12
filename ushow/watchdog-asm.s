; ushow: Atari ST/STE/TT/Falcon-specific bitmap viewer
;
; Copyright (c) 2022 Miro Kropacek <miro.kropacek@gmail.com>
;
; This program is free software: you can redistribute it and/or modify
; it under the terms of the GNU General Public License as published by
; the Free Software Foundation, either version 3 of the License, or
; (at your option) any later version.
;
; This program is distributed in the hope that it will be useful,
; but WITHOUT ANY WARRANTY; without even the implied warranty of
; MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
; GNU General Public License for more details.
;
; You should have received a copy of the GNU General Public License
; along with this program.  If not, see <https://www.gnu.org/licenses/>.

; The watchdog of the unattended test build: MFP Timer A at 48 Hz counts
; down and resets the machine (cold) when asm_watchdog_kick() has not been
; called for WD_TICKS ticks. A VBL handler that keeps the CPU at interrupt
; level 7 for longer than a frame keeps the program from kicking it.

		xdef	asm_watchdog_start
		xdef	asm_watchdog_stop
		xdef	asm_watchdog_kick

MFP_IERA	equ	$fffffa07
MFP_ISRA	equ	$fffffa0f
MFP_IMRA	equ	$fffffa13
MFP_TACR	equ	$fffffa19
MFP_TADR	equ	$fffffa1f
VEC_TIMERA	equ	$134
TIMERA_BIT	equ	5				; in IERA, ISRA, IMRA
WD_TICKS	equ	150				; ~3 s

; ------------------------------------------------------
		section	text
; ------------------------------------------------------

; extern void asm_watchdog_start(void);
;
asm_watchdog_start:
		move.w	sr,-(sp)
		ori.w	#$0700,sr

		move.l	VEC_TIMERA.w,old_timera
		move.b	MFP_IERA.w,old_iera
		move.b	MFP_IMRA.w,old_imra
		move.b	MFP_TACR.w,old_tacr
		move.b	MFP_TADR.w,old_tadr

		moveq	#0,d0
		move.b	d0,MFP_TACR.w			; stopped
		move.w	#WD_TICKS,wd_count
		move.l	#watchdog,VEC_TIMERA.w
		move.b	d0,MFP_TADR.w			; 256
		move.b	#7,MFP_TACR.w			; 2457600 Hz / 200 / 256 = 48 Hz
		bset	#TIMERA_BIT,MFP_IERA.w
		bset	#TIMERA_BIT,MFP_IMRA.w

		move.w	(sp)+,sr
		rts

; extern void asm_watchdog_stop(void);
;
asm_watchdog_stop:
		move.w	sr,-(sp)
		ori.w	#$0700,sr

		moveq	#0,d0
		move.b	d0,MFP_TACR.w			; stopped
		bclr	#TIMERA_BIT,MFP_IERA.w		; (clears a pending request, too)
		bclr	#TIMERA_BIT,MFP_IMRA.w

		move.l	old_timera,VEC_TIMERA.w
		move.b	old_tadr,MFP_TADR.w
		move.b	old_tacr,MFP_TACR.w

		btst	#TIMERA_BIT,old_imra		; Timer A's bits only, the
		beq.s	.imra				; others are not ours
		bset	#TIMERA_BIT,MFP_IMRA.w
.imra:		btst	#TIMERA_BIT,old_iera
		beq.s	.iera
		bset	#TIMERA_BIT,MFP_IERA.w
.iera:
		move.w	(sp)+,sr
		rts

; extern void asm_watchdog_kick(void);
;
asm_watchdog_kick:
		move.w	#WD_TICKS,wd_count
		rts

; ======================================================

watchdog:	subq.w	#1,wd_count
		beq.s	cold_reset
		move.b	#~(1<<TIMERA_BIT),MFP_ISRA.w	; end of interrupt
		rte

cold_reset:	move.w	#$2700,sr
		moveq	#0,d0
		move.l	d0,$420.w			; memvalid
		move.l	d0,$43a.w			; memval2
		move.l	d0,$51a.w			; memval3
		reset
		movea.l	$4.w,a0
		jmp	(a0)

; ------------------------------------------------------
		section	bss
; ------------------------------------------------------

		align	2

old_timera:	ds.l	1
wd_count:	ds.w	1
old_iera:	ds.b	1
old_imra:	ds.b	1
old_tacr:	ds.b	1
old_tadr:	ds.b	1

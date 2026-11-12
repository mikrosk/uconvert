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

; A bitmap with palette records per scanline on the Falcon: VGA 640x480,
; 8 bitplanes, 8 palette entries rewritten in the horizontal blank after
; every scanline.
;
; The VBL handler writes the 256 colours of scanline 0 and then one record
; per scanline: 8 colours ($RRGG00BB) and the address of the palette
; register of the first one; record r is written in the blank after
; scanline r. It stays at interrupt level 7 from the vertical blank to the
; last scanline, then goes on to the VBL handler it has replaced.

		xdef	asm_linepal_start
		xdef	asm_linepal_stop

LINES		equ	480
REC_SIZE	equ	9*4				; 8 colours, the palette register
HHC_AT		equ	123				; the HHC value from which a record is
							; written (no writes in the picture
							; from 118 to 128)

V_HHC		equ	$ffff8280
V_VFC		equ	$ffff82a0
V_VDB		equ	$ffff82a8
PALETTE		equ	$ffff9800
VEC_VBL		equ	$70

; ------------------------------------------------------
		section	text
; ------------------------------------------------------

; extern void asm_linepal_start(const uint32_t* pPalette, const uint32_t* pRecords);
;
asm_linepal_start:
		move.l	(4,sp),palette
		move.l	(8,sp),records

		move.w	sr,-(sp)
		ori.w	#$0700,sr
		move.l	VEC_VBL.w,old_vbl
		move.l	#vbl,VEC_VBL.w
		move.w	(sp)+,sr
		rts

; extern void asm_linepal_stop(void);
;
asm_linepal_stop:
		move.w	sr,-(sp)
		ori.w	#$0700,sr
		move.l	old_vbl,VEC_VBL.w
		move.w	(sp)+,sr
		rts

; ======================================================

; The VBL: d1 = the HHC value, d2-d7/a2/a3 = the 8 colours, a4 = their
; palette register. Before the first record it goes through the loop once
; with a record that writes into RAM, so that the loop's code is in the
; instruction cache when the first record is due.
		cnop	0,4
vbl:		movem.l	d0-d7/a0-a6,-(sp)
		move.w	#$2700,sr
		movea.l	palette,a5			; the palette of scanline 0
		lea	PALETTE.w,a4
		move.w	#256-1,d0
.pre:		move.l	(a5)+,(a4)+
		dbra	d0,.pre
		moveq	#0,d0
		move.l	d0,-(sp)			; (the end)
		move.l	records,-(sp)			; (the records, after the pass into RAM)

		lea	V_HHC+1.w,a0
		lea	V_VFC.w,a1
		moveq	#HHC_AT,d1
		lea	prime,a5			; once through the loop with RAM as the
		lea	REC_SIZE(a5),a6			; target, still in the vertical blank
.line:		movem.l	(a5)+,d2-d7/a2-a4		; the record: 8 colours, the address
.blank:		move.b	(a0),d0				; the blank after the scanline on the
		cmp.b	d1,d0				; beam: HHC at the value or past it
		blo.s	.blank
		btst	#0,1(a1)			; in a second half line (VFC odd)
		beq.s	.blank
		move.b	(a0),d0				; (HHC a second time)
		cmp.b	d1,d0
		blo.s	.blank
		movem.l	d2-d6,(a4)			; 8 longs, nothing in between that
		movem.l	d7/a2-a3,20(a4)			; reads memory
.away:		move.b	(a0),d0				; HHC has started over (two reads):
		cmp.b	#100,d0				; out of this blank
		bhs.s	.away
		move.b	(a0),d0
		cmp.b	#100,d0
		bhs.s	.away
		cmpa.l	a6,a5
		blo.s	.line
		move.l	(sp)+,d0			; after the pass into RAM: the records
		beq.s	.done
		movea.l	d0,a5
		lea	LINES*REC_SIZE(a5),a6
		move.w	V_VDB.w,d2
		and.w	#$03ff,d2
		addq.w	#1,d2				; VFC = VDB + 1: the half line with the
.top:		move.w	(a1),d0				; left edge of scanline 0, from HHC 96
		cmp.w	(a1),d0				; on (two reads that agree)
		bne.s	.top
		cmp.w	d2,d0
		bne.s	.top
		bra.s	.line

.done:		movem.l	(sp)+,d0-d7/a0-a6
		move.w	#$2400,sr			; the level the VBL came in on
		move.l	old_vbl,-(sp)			; on to the replaced VBL handler
		rts
vbl_end:
		if	(vbl_end-vbl)>250
		fail	"the VBL handler does not fit into the instruction cache"
		endif

; ------------------------------------------------------
		section	data
; ------------------------------------------------------

		align	2

prime:		dcb.l	8,0				; a record that writes into RAM
		dc.l	scratch

; ------------------------------------------------------
		section	bss
; ------------------------------------------------------

		align	2

palette:	ds.l	1				; 256 colours
records:	ds.l	1				; LINES records
old_vbl:	ds.l	1
scratch:	ds.l	8

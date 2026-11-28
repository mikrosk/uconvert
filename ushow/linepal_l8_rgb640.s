; linepal_l8_rgb640: per-line palette blocks, LP_RGB=1 LP_W=640 LP_DBL=0.
; Flat copy of linepal.s (IFs, macros resolved); the entry lp_entry is renamed linepal_l8_rgb640.
; Assemble: vasmm68k_mot -m68030 -quiet -Felf -opt-allbra

LP_RGB			equ	1			; command line
LP_W			equ	640			; command line
LP_DBL			equ	0			; command line
HLPL			equ	2			; = 2+2*LP_DBL half-lines per screen line
PASSES			equ	400			; polls per line, shared by its waits
SYNC_PASSES		equ	50000			; polls for the frame sync (> 1 frame)
SYNC_HL			equ	56			; frame sync: half-lines before VDB
NOSYNC_MAX		equ	8			; failed frame syncs in a row -> -1
HL_PAL			equ	0			; palette write position (HL,VAL)
ENTRY_R			equ	$a0
SYNC_R			equ	$a0
BLKVFC_R		equ	$a0
VAL_W			equ	416			; = VAL_PAL
VBL_SR			equ	$2700
FRAME_SR		equ	$2700
PRECACHE_SR		equ	$2700			; palette restore and dry line

		section	text

		xdef	linepal_l8_rgb640

;------------------------------------------------------------------------------
; entry: arguments, system state, set-up
;------------------------------------------------------------------------------

linepal_l8_rgb640:	movem.l	d2-d7/a2-a6,-(sp)		; 11 longs
		lea	48(sp),a0			; first argument
		move.l	(a0)+,lp_recs
		move.l	(a0)+,lp_pal
		move.l	(a0)+,d0			; entries
		subq.w	#1,d0
		and.w	#$ff,d0
		move.w	d0,lp_npal			; entries-1 (dbra count)
		move.l	(a0)+,d1			; y0
		move.l	(a0)+,d2			; h
		moveq	#-1,d7
		subq.l	#1,d2				; h-1
		bmi	lp_ret
		move.w	d2,lp_hm1
; 68030 only (the CACR values below): _CPU cookie must be 30, else -1
		move.l	$5a0.w,d0			; _p_cookies
		beq	lp_ret
		move.l	d0,a0
		move.w	#256-1,d3			; entries searched at most (d1 = y0)
.ck:		move.l	(a0)+,d0
		beq	lp_ret				; end of the jar
		cmp.l	#'_CPU',d0
		beq.s	.cpu
		addq.l	#4,a0
		dbra	d3,.ck
		bra	lp_ret
.cpu:		cmp.l	#30,(a0)
		bne	lp_ret
		move.l	sp,lp_sp
		move.w	sr,lp_sr
		move.w	#$2700,sr
		lea	$ffff8200.w,a6
; VFC targets: first write, frame sync
		mulu.w	#HLPL,d1			; y0 * half-lines per line
		add.w	$a8(a6),d1			; + VDB
		add.w	#HL_PAL,d1
		move.w	d1,lp_first
		move.w	$a8(a6),d1
		sub.w	#SYNC_HL,d1
		move.w	d1,lp_sync
		clr.b	lp_nosync
		clr.b	lp_skip
; system state
		movec	cacr,d0
		move.l	d0,lp_cacr
		lea	$fffffa00.w,a0
		move.b	$07(a0),lp_iera
		move.b	$09(a0),lp_ierb
		move.b	$13(a0),lp_imra
		move.b	$15(a0),lp_imrb
		clr.b	$07(a0)				; IERA (also clears the pending bits)
		clr.b	$09(a0)				; IERB
		moveq	#$12,d0				; IKBD: mouse reporting off
		bsr	lp_ikbd
; timed block -> 16-byte aligned buffer (16 I-cache sets of 16 bytes:
; a block of up to 256 bytes there never evicts itself)
		move.l	#lp_blkbuf+15,d0
		and.w	#$fff0,d0
		move.l	d0,lp_blk
		move.l	d0,a1
		lea	blk_start,a0
		move.w	#(blk_end-blk_start)/2-1,d0
.copy:		move.w	(a0)+,(a1)+
		dbra	d0,.copy
		move.l	#$0808,d0			; clear I- and D-cache
		movec	d0,cacr

;------------------------------------------------------------------------------
; Frame loop. The untimed part runs from the end of the image to the next
; frame's frame sync, SYNC_HL half-lines before VDB (vertical blank): there
; the full palette is restored, one dry line through the block fills the
; I-cache (writing record 0, the full palette's first entries, in the
; blank), the block freezes the cache as it leaves, then the image runs.
;------------------------------------------------------------------------------

lp_frame:	move.w	#VBL_SR,sr
		moveq	#1,d0				; I-cache on, unfrozen, no burst;
		movec	d0,cacr				; D-cache off
; keyboard: at most 4 bytes per frame; packets (header $F6..$FF) skipped
		moveq	#4-1,d1
.kb:		btst	#0,$fffffc00.w			; byte received?
		beq.s	.kbdone
		moveq	#0,d0
		move.b	$fffffc02.w,d0
		tst.b	lp_skip
		beq.s	.kbnew
		subq.b	#1,lp_skip			; inside a packet
		bra.s	.kbnext
.kbnew:		cmp.b	#$f6,d0
		blo.s	.kbkey
		sub.b	#$f6,d0
		move.b	.pktlen(pc,d0.w),lp_skip
		bra.s	.kbnext
.kbkey:		tst.b	d0
		bmi.s	.kbnext				; break code
		beq.s	.kbnext
		move.l	d0,d7				; make code
		bra	lp_exit
.kbnext:	dbra	d1,.kb
		bra.s	.kbdone
.pktlen:	dc.b	7,5,2,2,2,2,6,2,1,1		; bytes after headers $F6..$FF
.kbdone:

; frame sync: VFC below lp_sync (the next frame has begun), then VFC >=
; lp_sync, two reads in a row each; ranges, so an interrupt or a slow read
; cannot make it miss the frame
		move.w	lp_sync,d6
		move.w	#SYNC_PASSES-1,d3
.syn1:		cmp.w	SYNC_R(a6),d6			; VFC < lp_sync
		dbhi	d3,.syn1
		bls.s	.synclost
		cmp.w	SYNC_R(a6),d6
		dbhi	d3,.syn1
		bls.s	.synclost
.syn2:		cmp.w	SYNC_R(a6),d6			; VFC >= lp_sync
		dbls	d3,.syn2
		bhi.s	.synclost
		cmp.w	SYNC_R(a6),d6
		dbls	d3,.syn2
		bls.s	.synced
.synclost:	addq.b	#1,lp_nosync
		moveq	#-1,d7
		cmp.b	#NOSYNC_MAX,lp_nosync
		bhs	lp_exit
		bsr	lp_dropped
		bra	lp_frame

.synced:	clr.b	lp_nosync
		move.w	#PRECACHE_SR,sr
		bsr	lp_setpal			; full palette
; dry line: one pass through the block (VFC_WAIT 1: its parity waits start
; from the current half-line; VFC_WAIT 0: target 2 half-lines on)
		st	lp_dry
		move.l	lp_recs,a5
		movem.l	(a5)+,d1/d4-d5/d7/a0-a3		; next record: colours, register
		movea.w	(a5)+,a4			; $98xx -> $FFFF98xx
		move.w	$a0(a6),d6
		addq.w	#2,d6
		moveq	#0,d3				; one line
		pea	lp_done(pc)			; the block returns there with rts
		move.l	lp_blk,-(sp)			; jump into the block without a register
		rts					; -> lp_done/lp_miss -> lp_drydone

lp_drydone:	moveq	#3,d0				; freeze (the block freezes it as it
		movec	d0,cacr				; leaves, but the dry line may have missed)
		sf	lp_dry
		move.w	#FRAME_SR,sr
		move.l	lp_recs,a5
		movem.l	(a5)+,d1/d4-d5/d7/a0-a3		; next record: colours, register
		movea.w	(a5)+,a4			; $98xx -> $FFFF98xx
; enter the block at a defined point, VFC >= d6 in two reads in a row:
; VFC_WAIT 1 on the half-line of the previous line's write, where the parity
; waits start; VFC_WAIT 0 one VFC step before the first target
		move.w	lp_first,d6
		subq.w	#1,d6
		move.w	#SYNC_PASSES-1,d3
.entry:		cmp.w	ENTRY_R(a6),d6
		dbls	d3,.entry
		bhi.s	.enter
		cmp.w	ENTRY_R(a6),d6
		dbls	d3,.entry
.enter:
		move.w	lp_first,d6			; VFC target of the first write
		moveq	#0,d3
		move.w	lp_hm1,d3
		swap	d3				; lines left - 1, pass counter 0
		pea	lp_done(pc)			; the block returns there with rts
		move.l	lp_blk,-(sp)			; jump into the block without a register
		rts

lp_done:	tst.b	lp_dry
		bne	lp_drydone
		bra	lp_frame

lp_miss:	addq.l	#4,sp				; drop lp_done (pushed for the rts)
		tst.b	lp_dry
		bne	lp_drydone
		bsr.s	lp_dropped
		bra	lp_frame

lp_dropped:
		rts

;------------------------------------------------------------------------------
; Timed block: one image line per pass. Copied to lp_blkbuf (16-byte
; aligned), so it must stay position independent and within 256 bytes.
; a6 = $FFFF8200, a5 = next record, d3.hi = lines left - 1, d3.lo = pass
; counter, PALREGS = colours of this line, a4 = their first register;
; VFC_WAIT 0: d6 = VFC target of this line's write.
;------------------------------------------------------------------------------

blk_start:	bra.s	.line
.miss:		jmp	(lp_miss).l
.line:		move.w	#PASSES-1,d3
; VFC >= target in two reads in a row (one read across a step that carries
; over several bits can return a value too high)
.vfc:		cmp.w	BLKVFC_R(a6),d6
		dbls	d3,.vfc
		bhi.s	.miss				; (d3 = -1: never loop again)
		cmp.w	BLKVFC_R(a6),d6
		dbls	d3,.vfc				; 2nd read low: count, retry
		bhi.s	.miss
.hhc:		cmpi.w	#VAL_W,$80(a6)			; HHC >= val, two reads in a row
		dbhs	d3,.hhc
		blo.s	.miss				; (d3 = -1: never loop again)
		cmpi.w	#VAL_W,$80(a6)
		dbhs	d3,.hhc				; 2nd read low: count, retry
		blo.s	.miss
		movem.l	d1/d4-d5/d7/a0-a3,(a4)
		movem.l	(a5)+,d1/d4-d5/d7/a0-a3		; next record: colours, register
		movea.w	(a5)+,a4			; $98xx -> $FFFF98xx
		addq.w	#HLPL,d6
		sub.l	#$10000,d3
		bpl.w	.line
		moveq	#3,d0				; freeze the I-cache before leaving
		movec	d0,cacr
		rts					; -> lp_done (pushed by the frame code)
blk_end:

					; (assemble-time check: (blk_end-blk_start)>256)

;------------------------------------------------------------------------------
; helpers
;------------------------------------------------------------------------------

; full palette: lp_npal+1 longs from lp_pal
lp_setpal:	move.l	lp_pal,a0
		lea	$ffff9800.w,a1
		move.w	lp_npal,d0
.p:		move.l	(a0)+,(a1)+
		dbra	d0,.p
		rts

; send d0.b to the IKBD (ACIA transmit register empty: at most 2000 polls)
lp_ikbd:	move.w	#2000-1,d1
.w:		btst	#1,$fffffc00.w
		dbne	d1,.w
		beq.s	.no
		move.b	d0,$fffffc02.w
.no:		rts

;------------------------------------------------------------------------------
; exit with d7: restore everything, return d7
;------------------------------------------------------------------------------

lp_exit:	move.w	#$2700,sr
		move.l	#$0808,d0			; unfreeze, clear both caches
		movec	d0,cacr
		move.l	lp_cacr,d0
		movec	d0,cacr
		moveq	#$08,d0				; IKBD: relative mouse
		bsr	lp_ikbd
		move.w	#64-1,d1			; drop bytes received meanwhile
.flush:		btst	#0,$fffffc00.w
		beq.s	.flushed
		move.b	$fffffc02.w,d0
		dbra	d1,.flush
.flushed:	lea	$fffffa00.w,a0
		move.b	lp_imra,$13(a0)
		move.b	lp_imrb,$15(a0)
		move.b	lp_iera,$07(a0)
		move.b	lp_ierb,$09(a0)
		bsr	lp_setpal
		move.l	lp_sp,sp
		move.w	lp_sr,sr
lp_ret:		move.l	d7,d0
		movem.l	(sp)+,d2-d7/a2-a6
		rts

		section	bss

lp_recs:	ds.l	1				; records
lp_pal:		ds.l	1				; full palette
lp_sp:		ds.l	1				; stack pointer after the entry
lp_cacr:	ds.l	1
lp_blk:		ds.l	1				; timed block, 16-byte aligned copy
lp_npal:	ds.w	1				; entries-1
lp_hm1:		ds.w	1				; h-1
lp_first:	ds.w	1				; VFC target of the first write
lp_sync:	ds.w	1				; VFC value of the frame sync
lp_sr:		ds.w	1
lp_iera:	ds.b	1
lp_ierb:	ds.b	1
lp_imra:	ds.b	1
lp_imrb:	ds.b	1
lp_nosync:	ds.b	1				; failed frame syncs in a row
lp_skip:	ds.b	1				; IKBD packet bytes still to skip
lp_dry:		ds.b	1				; nonzero while the dry line runs
		even
lp_blkbuf:	ds.b	256+16

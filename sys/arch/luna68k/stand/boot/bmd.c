/*	$NetBSD: bmd.c,v 1.10 2024/07/05 19:28:36 andvar Exp $	*/

/*
 * Copyright (c) 1992 OMRON Corporation.
 *
 * This code is derived from software contributed to Berkeley by
 * OMRON Corporation.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)bmd.c	8.2 (Berkeley) 8/15/93
 */
/*
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * OMRON Corporation.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)bmd.c	8.2 (Berkeley) 8/15/93
 */
/*

 * bmd.c --- Bitmap-Display raw-level driver routines
 *
 *	by A.Fujita, SEP-09-1992
 */


#include <sys/param.h>
#include <luna68k/stand/boot/samachdep.h>
#include <machine/board.h>

/*
 *  RFCNT register
 */

union bmd_rfcnt {
	struct {
		int16_t	rfc_hcnt;
		int16_t	rfc_vcnt;
	} p;
	uint32_t u;
};

#define isprint(c)	((c) >= 0x20 && (c) < 0x7f)

/*
 *  Width & Height
 */

#define BMAP_OFFSET	8

#define PB_WIDTH	2048			/* Plane Width   (Bit) */
#define PB_HEIGHT	1024			/* Plane Height  (Bit) */
#define PL_WIDTH	64			/* Plane Width  (long) */
#define PS_WIDTH	128			/* Plane Width  (long) */
#define P_WIDTH		256			/* Plane Width  (Byte) */

#define SB_WIDTH	1280			/* Screen Width  (Bit) */
#define SB_HEIGHT	1024			/* Screen Height (Bit) */
#define SL_WIDTH	40			/* Screen Width (Long) */
#define S_WIDTH		160			/* Screen Width (Byte) */

#define FB_WIDTH	12			/* Font Width    (Bit) */
#define FB_HEIGHT	20			/* Font Height   (Bit) */


#define NEXT_LINE(addr)			(addr +  (PL_WIDTH * FB_HEIGHT))
#define SKIP_NEXT_LINE(addr)		(addr += (PL_WIDTH - SL_WIDTH))


static void	bmd_draw_char(uint8_t *, uint8_t *, int, int, int);
static void	bmd_reverse_char(uint8_t *, uint8_t *, int, int);
static void	bmd_erase_char(uint8_t *, uint8_t *, int, int);
static void	bmd_erase_screen(volatile uint32_t *);
static void	bmd_scroll_screen(volatile uint32_t *, volatile uint32_t *,
		    int, int, int, int);


struct bmd_linec {
	struct bmd_linec *bl_next;
	struct bmd_linec *bl_prev;
	int	bl_col;
	int	bl_end;
	uint8_t	bl_line[128];
};

struct bmd_softc {
	int	bc_stat;
	uint8_t *bc_raddr;
	uint8_t *bc_waddr;
	int	bc_xmin;
	int	bc_xmax;
	int	bc_ymin;
	int	bc_ymax;
	int	bc_col;
	int	bc_row;
	struct bmd_linec *bc_bl;
	char	bc_escseq[8];
	char   *bc_esc;
	void  (*bc_escape)(int);
};

#define STAT_NORMAL	0x0000
#define STAT_ESCAPE	0x0001
#define STAT_INSERT	0x0100

static struct	bmd_softc bmd_softc;
static struct	bmd_linec bmd_linec[52];

static void	bmd_escape(int);
static void	bmd_escape_0(int);
#if 0
static void	bmd_escape_1(int);
#endif


/*
 * Escape-Sequence
 */

static void
bmd_escape(int c)
{
	struct bmd_softc *bp = &bmd_softc;

	switch (c) {

	case '[':
		bp->bc_escape = bmd_escape_0;
		break;

	default:
		bp->bc_stat &= ~STAT_ESCAPE;
		bp->bc_esc = &bp->bc_escseq[0];
		bp->bc_escape = bmd_escape;
		break;
	}
}

static void
bmd_escape_0(int c)
{
	struct bmd_softc *bp = &bmd_softc;
	struct bmd_linec *bq = bp->bc_bl;

	switch (c) {

	case 'A':
		if (bp->bc_row > bp->bc_ymin) {
			bp->bc_row--;
		}
		break;

	case 'C':
		if (bq->bl_col < bp->bc_xmax - 1) {
			bq->bl_col++;
		}
		break;

	case 'K':
		if (bq->bl_col < bp->bc_xmax) {
			int col;
			for (col = bq->bl_col; col < bp->bc_xmax; col++)
				bmd_erase_char(bp->bc_raddr,
					       bp->bc_waddr,
					       col, bp->bc_row);
		}
		bq->bl_end = bq->bl_col;
		break;

	case 'H':
		bq->bl_col = bq->bl_end = bp->bc_xmin;
		bp->bc_row = bp->bc_ymin;
		break;

	default:
#if 0
		*bp->bc_esc++ = c;
		bp->bc_escape = bmd_escape_1;
		return;
#endif
		break;
	}

	bp->bc_stat &= ~STAT_ESCAPE;
	bp->bc_esc = &bp->bc_escseq[0];
	bp->bc_escape = bmd_escape;
}

#if 0
static void
bmd_escape_1(int c)
{
	struct bmd_softc *bp = &bmd_softc;
	struct bmd_linec *bq = bp->bc_bl;
	int col = 0, row = 0;
	char *p;

	switch (c) {

	case 'J':
		bp->bc_stat &= ~STAT_ESCAPE;
		bp->bc_esc = &bp->bc_escseq[0];
		bp->bc_escape = bmd_escape;
		break;

	case 'H':
		for (p = &bp->bc_escseq[0]; *p != ';'; p++)
			row = (row * 10) + (*p - 0x30);
		p++;
		for (p = &bp->bc_escseq[0]; p != bp->bc_esc; p++)
			col = (col * 10) + (*p - 0x30);

		bq->bl_col = col + bp->bc_xmin;
		bp->bc_row = row + bp->bc_ymin;

		bp->bc_stat &= ~STAT_ESCAPE;
		bp->bc_esc = &bp->bc_escseq[0];
		bp->bc_escape = bmd_escape;
		break;

	default:
		*bp->bc_esc++ = c;
		break;
	}
}
#endif

/*
 * Entry Routine
 */

void
bmdinit(void)
{
	volatile uint32_t *bmd_rfcnt = (uint32_t *)BMAP_RFCNT;
	volatile uint32_t *bmd_bmsel = (uint32_t *)BMAP_BMSEL;
	struct bmd_softc *bp = &bmd_softc;
	struct bmd_linec *bq;
	int i;
	union bmd_rfcnt rfcnt;

	/*
	 *  adjust plane position
	 */

	/* plane-0 hardware address */
	bp->bc_raddr = (uint8_t *)(BMAP_BMAP0 + BMAP_OFFSET);
	/* common bitmap hardware address */
	bp->bc_waddr = (uint8_t *)(BMAP_BMP   + BMAP_OFFSET);

	rfcnt.p.rfc_hcnt = 7;			/* shift left   16 dot */
	rfcnt.p.rfc_vcnt = -27;			/* shift down    1 dot */
	*bmd_rfcnt = rfcnt.u;

	bp->bc_stat  = STAT_NORMAL;

	bp->bc_xmin  = 8;
	bp->bc_xmax  = 96;
	bp->bc_ymin  = 2;
	bp->bc_ymax  = 48;

	bp->bc_row = bp->bc_ymin;

	for (i = bp->bc_ymin; i < bp->bc_ymax; i++) {
		bmd_linec[i].bl_next = &bmd_linec[i + 1];
		bmd_linec[i].bl_prev = &bmd_linec[i - 1];
	}
	bmd_linec[bp->bc_ymax - 1].bl_next = &bmd_linec[bp->bc_ymin];
	bmd_linec[bp->bc_ymin].bl_prev = &bmd_linec[bp->bc_ymax - 1];

	bq = bp->bc_bl = &bmd_linec[bp->bc_ymin];
	bq->bl_col = bq->bl_end = bp->bc_xmin;

	bp->bc_col = bp->bc_xmin;

	bp->bc_esc = &bp->bc_escseq[0];
	bp->bc_escape = bmd_escape;

	*bmd_bmsel = 0xff;				/* all planes */
	bmd_erase_screen((uint32_t *)bp->bc_waddr);	/* clear screen */
	*bmd_bmsel = 0x01;				/* 1 plane */

	/* turn on cursor */
	bmd_reverse_char(bp->bc_raddr,
			 bp->bc_waddr,
			 bq->bl_col, bp->bc_row);
}

void
bmdadjust(int16_t hcnt, int16_t vcnt)
{
	volatile uint32_t *bmd_rfcnt = (uint32_t *)BMAP_RFCNT;
	union bmd_rfcnt rfcnt;

	printf("bmdadjust: hcnt = %d, vcnt = %d\n", hcnt, vcnt);

	rfcnt.p.rfc_hcnt = hcnt;		/* shift left   16 dot */
	rfcnt.p.rfc_vcnt = vcnt;		/* shift down    1 dot */

	*bmd_rfcnt = rfcnt.u;
}

int
bmdputc(int c)
{
	struct bmd_softc *bp = &bmd_softc;
	struct bmd_linec *bq = bp->bc_bl;
	int i;

	c &= 0x7F;

	/* turn off cursor */
	bmd_reverse_char(bp->bc_raddr,
			 bp->bc_waddr,
			 bq->bl_col, bp->bc_row);

	/* do escape-sequence */
	if (bp->bc_stat & STAT_ESCAPE) {
		*bp->bc_esc++ = c;
		(*bp->bc_escape)(c);
		goto done;
	}

	if (isprint(c)) {
		bmd_draw_char(bp->bc_raddr, bp->bc_waddr,
			      bq->bl_col, bp->bc_row, c);
		bq->bl_col++;
		bq->bl_end++;
		if (bq->bl_col >= bp->bc_xmax) {
			bq->bl_col = bq->bl_end = bp->bc_xmin;
			bp->bc_row++;
			if (bp->bc_row >= bp->bc_ymax) {
				bmd_scroll_screen((uint32_t *)bp->bc_raddr,
						  (uint32_t *)bp->bc_waddr,
						  bp->bc_xmin, bp->bc_xmax,
						  bp->bc_ymin, bp->bc_ymax);

				bp->bc_row = bp->bc_ymax - 1;
			}
		}
	} else {
		switch (c) {
		case 0x08:				/* BS */
			if (bq->bl_col > bp->bc_xmin) {
				bq->bl_col--;
			}
			break;

		case 0x09:				/* HT */
		case 0x0B:				/* VT */
			i = ((bq->bl_col / 8) + 1) * 8;
			if (i < bp->bc_xmax) {
				bq->bl_col = bq->bl_end = i;
			}
			break;

		case 0x0A:				/* NL */
			bp->bc_row++;
			if (bp->bc_row >= bp->bc_ymax) {
				bmd_scroll_screen((uint32_t *)bp->bc_raddr,
						  (uint32_t *)bp->bc_waddr,
						  bp->bc_xmin, bp->bc_xmax,
						  bp->bc_ymin, bp->bc_ymax);

				bp->bc_row = bp->bc_ymax - 1;
			}
			break;

		case 0x0D:				/* CR */
			bq->bl_col = bp->bc_xmin;
			break;

		case 0x1B:				/* ESC */
			bp->bc_stat |= STAT_ESCAPE;
			*bp->bc_esc++ = 0x1b;
			break;

		case 0x7F:				/* DEL */
			if (bq->bl_col > bp->bc_xmin) {
				bq->bl_col--;
				bmd_erase_char(bp->bc_raddr,
					       bp->bc_waddr,
					       bq->bl_col, bp->bc_row);
			}
			break;

		default:
			break;
		}
	}

 done:
	/* turn on  cursor */
	bmd_reverse_char(bp->bc_raddr,
			 bp->bc_waddr,
			 bq->bl_col, bp->bc_row);

	return c;
}

void
bmdclear(void)
{
	struct bmd_softc *bp = &bmd_softc;
	struct bmd_linec *bq = bp->bc_bl;

	/* clear screen */
	bmd_erase_screen((uint32_t *)bp->bc_waddr);

	bq->bl_col = bq->bl_end = bp->bc_xmin;
	bp->bc_row = bp->bc_ymin;

	/* turn on cursor */
	bmd_reverse_char(bp->bc_raddr,
			 bp->bc_waddr,
			 bq->bl_col, bp->bc_row);
}


/*
 *  character operation routines
 */

static void
bmd_draw_char(uint8_t *raddr, uint8_t *waddr, int col, int row, int c)
{
	volatile uint16_t *p, *q;
	volatile uint32_t *lp, *lq;
	const uint16_t *fp;
	int i;

	fp = &bmdfont[c][0];

	switch (col % 4) {

	case 0:
		p = (uint16_t *)(raddr + ((row * FB_HEIGHT) << 8)
		    + ((col / 4) * 6));
		q = (uint16_t *)(waddr + ((row * FB_HEIGHT) << 8)
		    + ((col / 4) * 6));
		for (i = 0; i < FB_HEIGHT; i++) {
			*q = (*p & 0x000F) | (*fp & 0xFFF0);
			p += 128;
			q += 128;
			fp++;
		}
		break;

	case 1:
		lp = (uint32_t *)(raddr + ((row * FB_HEIGHT) << 8)
		    + ((col / 4) * 6));
		lq = (uint32_t *)(waddr + ((row * FB_HEIGHT) << 8)
		    + ((col / 4) * 6));
		for (i = 0; i < FB_HEIGHT; i++) {
			*lq = (*lp & 0xFFF000FF) |
			    ((uint32_t)(*fp & 0xFFF0) << 4);
			lp += 64;
			lq += 64;
			fp++;
		}
		break;

	case 2:
		lp = (uint32_t *)(raddr + ((row * FB_HEIGHT) << 8)
		    + ((col / 4) * 6) + 2);
		lq = (uint32_t *)(waddr + ((row * FB_HEIGHT) << 8)
		    + ((col / 4) * 6) + 2);
		for (i = 0; i < FB_HEIGHT; i++) {
			*lq = (*lp & 0xFF000FFF) |
			    ((uint32_t)(*fp & 0xFFF0) << 8);
			lp += 64;
			lq += 64;
			fp++;
		}
		break;

	case 3:
		p = (uint16_t *)(raddr + ((row * FB_HEIGHT) << 8)
		    + ((col / 4) * 6) + 4);
		q = (uint16_t *)(waddr + ((row * FB_HEIGHT) << 8)
		    + ((col / 4) * 6) + 4);
		for (i = 0; i < FB_HEIGHT; i++) {
			*q = (*p & 0xF000) | ((*fp & 0xFFF0) >> 4);
			p += 128;
			q += 128;
			fp++;
		}
		break;

	default:
		break;
	}
}

static void
bmd_reverse_char(uint8_t *raddr, uint8_t *waddr, int col, int row)
{
	volatile uint16_t *p, *q;
	volatile uint32_t *lp, *lq;
	int i;

	switch (col % 4) {

	case 0:
		p = (uint16_t *)(raddr + ((row * FB_HEIGHT) << 8)
		    + ((col / 4) * 6));
		q = (uint16_t *)(waddr + ((row * FB_HEIGHT) << 8)
		    + ((col / 4) * 6));
		for (i = 0; i < FB_HEIGHT; i++) {
			*q = (*p & 0x000F) | (~(*p) & 0xFFF0);
			p += 128;
			q += 128;
		}
		break;

	case 1:
		lp = (uint32_t *)(raddr + ((row * FB_HEIGHT) << 8)
		    + ((col / 4) * 6));
		lq = (uint32_t *)(waddr + ((row * FB_HEIGHT) << 8)
		    + ((col / 4) * 6));
		for (i = 0; i < FB_HEIGHT; i++) {
			*lq = (*lp & 0xFFF000FF) | (~(*lp) & 0x000FFF00);
			lp += 64;
			lq += 64;
		}
		break;

	case 2:
		lp = (uint32_t *)(raddr + ((row * FB_HEIGHT) << 8)
		    + ((col / 4) * 6) + 2);
		lq = (uint32_t *)(waddr + ((row * FB_HEIGHT) << 8)
		    + ((col / 4) * 6) + 2);
		for (i = 0; i < FB_HEIGHT; i++) {
			*lq = (*lp & 0xFF000FFF) | (~(*lp) & 0x00FFF000);
			lp += 64;
			lq += 64;
		}
		break;

	case 3:
		p = (uint16_t *)(raddr + ((row * FB_HEIGHT) << 8)
		    + ((col / 4) * 6) + 4);
		q = (uint16_t *)(waddr + ((row * FB_HEIGHT) << 8)
		    + ((col / 4) * 6) + 4);
		for (i = 0; i < FB_HEIGHT; i++) {
			*q = (*p & 0xF000) | (~(*p) & 0x0FFF);
			p += 128;
			q += 128;
		}
		break;

	default:
		break;
	}
}

static void
bmd_erase_char(uint8_t *raddr, uint8_t *waddr, int col, int row)
{

	bmd_draw_char(raddr, waddr, col, row, 0);
}


/*
 * screen operation routines
 */

static void
bmd_erase_screen(volatile uint32_t *lp)
{
	int i, j;

	for (i = 0; i < SB_HEIGHT; i++) {
		for (j = 0; j < SL_WIDTH; j++)
			*lp++ = 0;
		SKIP_NEXT_LINE(lp);
	}
}

static void
bmd_scroll_screen(volatile uint32_t *lp, volatile uint32_t *lq,
    int xmin, int xmax, int ymin, int ymax)
{
	int i, j;

	lp += ((PL_WIDTH * FB_HEIGHT) * (ymin + 1));
	lq += ((PL_WIDTH * FB_HEIGHT) *  ymin);

	for (i = 0; i < ((ymax - ymin -1) * FB_HEIGHT); i++) {
		for (j = 0; j < SL_WIDTH; j++) {
			*lq++ = *lp++;
		}
		lp += (PL_WIDTH - SL_WIDTH);
		lq += (PL_WIDTH - SL_WIDTH);
	}

	for (i = 0; i < FB_HEIGHT; i++) {
		for (j = 0; j < SL_WIDTH; j++) {
			*lq++ = 0;
		}
		lq += (PL_WIDTH - SL_WIDTH);
	}
}

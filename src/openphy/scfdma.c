/*
 * LTE Single Carrier Frequency Division Multiple Access (SC-FDMA)
 *
 * Copyright (C) 2015 Ettus Research LLC
 * Author Tom Tsou <tom.tsou@ettus.com>
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
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "ofdm.h"
#include "log.h"
#include "slot.h"
#include "lte/ref.h"
#include "subframe.h"
#include "prach.h"
#include "lte/interpolate.h"
#include "lte/fft.h"
#include "sigproc/sigvec_internal.h"

#ifndef M_PI
#define M_PI	3.14159265358979323846
#endif

#define INTERP_TAPS		32

/*
 * Map center resource block that spans the Nyquist edge
 *
 * This occurs on 3 MHz and 5 MHz channel allocations. Address the split block
 * by reallocating the center block to a contiguous vector.
 */
static int lte_sym_rb_map_special(struct lte_sym *sym, int rb)
{
	int len, rb0, rb1;
	int rbs = sym->slot->subframe->rbs;

	if ((rbs == 15) && (rb == 7)) {
		rb0 = LTE_N15_RB7;
		rb1 = LTE_N15_RB7_1;
	} else if ((rbs == 25) && (rb == 12)) {
		rb0 = LTE_N25_RB12;
		rb1 = LTE_N25_RB12_1;
	} else if ((rbs == 75) && (rb == 37)) {
		rb0 = LTE_N75_RB37;
		rb1 = LTE_N75_RB37_1;
	} else {
		LOG_DSP_ARG("Resource block mapping error ", rb);
		return -1;
	}

	if (!sym->rb[rb])
		sym->rb[rb] = cxvec_alloc_simple(LTE_RB_LEN);

	len = LTE_RB_LEN / 2;
	cxvec_cp(sym->rb[rb], sym->fd, 0, rb0, len);
	cxvec_cp(sym->rb[rb], sym->fd, len, rb1, len);

	return 0;
}

/*
 * FIXME: Map symbols to resource blocks
 *
 * We don't do this at initial allocation because the frequency domain symbols 
 * are not available until after performing the FFT.
 */
static void lte_sym_rb_map(struct lte_sym *sym)
{
	int pos;
	int rbs = sym->slot->subframe->rbs;

	for (int i = 0; i < rbs; i++) {
		if (((rbs == 15) && (i == 7)) ||
		    ((rbs == 25) && (i == 12)) ||
		    ((rbs == 75) && (i == 37))) {
			lte_sym_rb_map_special(sym, i);
			continue;
		}

		pos = lte_rb_pos(rbs, i);
		sym->rb[i] = cxvec_subvec(sym->fd, pos,  0, 0, LTE_RB_LEN);
	}
}

static int lte_sym_chan_rb_map_special(struct lte_ref *ref, int rb, int p)
{
	int len = LTE_RB_LEN / 2;
	int rbs = ref->sym->slot->subframe->rbs;
	int rb0, rb1;

	if ((rbs == 15) && (rb == 7)) {
		rb0 = LTE_N15_RB7;
		rb1 = LTE_N15_RB7_1;
	} else if ((rbs == 25) && (rb == 12)) {
		rb0 = LTE_N25_RB12;
		rb1 = LTE_N25_RB12_1;
	} else if ((rbs == 75) && (rb == 37)) {
		rb0 = LTE_N75_RB37;
		rb1 = LTE_N75_RB37_1;
	} else {
		LOG_DSP_ARG("Resource block mapping error ", rb);
		return -1;
	}

	if (!ref->rb[p][rb])
		ref->rb[p][rb] = cxvec_alloc_simple(LTE_RB_LEN);

	cxvec_cp(ref->rb[p][rb], ref->chan[p], 0, rb0, len);
	cxvec_cp(ref->rb[p][rb], ref->chan[p], len, rb1, len);

	return 0;
}

/*
 * Handle center resource block channel for antenna 'p'
 *
 * This occurs on 3 MHz and 5 MHz channel allocations that span the Nyquist
 * boundary. Address the split block by reallocating the center block to a
 * contiguous vector.
 */
static int lte_sym_chan_rb_map(struct lte_ref *ref, int p)
{
	int pos;
	int rbs = ref->sym->slot->subframe->rbs;

	/* We have an extra magnitude channel */
	if (p >= 2 + 1) {
		LOG_DSP_ARG("Invalid antenna configuration ", p - 1);
		return -1;
	}

	for (int i = 0; i < rbs; i++) {
		if (((rbs == 15) && (i == 7)) ||
		    ((rbs == 25) && (i == 12)) ||
		    ((rbs == 75) && (i == 37))) {
			lte_sym_chan_rb_map_special(ref, i, p);
			continue;
		}

		pos = lte_rb_pos(rbs, i);
		ref->rb[p][i] = cxvec_subvec(ref->chan[p],
					     pos, 0, 0, LTE_RB_LEN);
	}

	return 0;
}

/* Initialize a LTE symbol struct  */
static int lte_sym_init(struct lte_slot *slot, int l)
{
	int rbs = slot->rbs;
	int sym_len = lte_sym_len(rbs);
	int td_pos = lte_sym_pos(rbs, l);
	int fd_pos = l * sym_len;

	struct lte_sym *sym = &slot->syms[l];

	sym->l = l;
	sym->slot = slot;
	sym->td = cxvec_subvec(slot->td, td_pos, 0, 0, sym_len);
	sym->fd = cxvec_subvec(slot->fd, fd_pos, 0, 0, sym_len);
	sym->rb = (struct cxvec **) calloc(rbs, sizeof(struct cxvec *));
	lte_sym_rb_map(sym);

	/* All symbols use the same reference symbol */
	sym->ref = &slot->subframe->slot[0].refs[0];

	return 0;
}

/*
 * Release LTE symbol resources. The slot allocated object is not released.
 *
 * States:
 * 	1. Initialized
 * 	2. FFT converted
 * 	3. Channel estimated applied
 * 	4. FIXME
 */
static void lte_sym_free(struct lte_sym *sym)
{
	int rbs = sym->slot->rbs;

	cxvec_free(sym->td);
	cxvec_free(sym->fd);

	for (int i = 0; i < rbs; i++)
		cxvec_free(sym->rb[i]);
	free(sym->rb);
}

/*
 * Allocate an LTE slot object
 *
 * Each slot containing 7 symbols - 2 of which contain reference symbols.
 */
static int lte_slot_init(struct lte_subframe *subframe, int ns)
{
	int start, rbs = subframe->rbs;
	int slot_len = lte_slot_len(rbs);
	int sym_len = lte_sym_len(rbs);
	struct lte_slot *slot;

	if (ns % 2) {
		start = slot_len;
		slot = &subframe->slot[1];
	} else {
		start = 0;
		slot = &subframe->slot[0];
	}

	slot->rbs = rbs;
	slot->subframe = subframe;
	slot->td = cxvec_subvec(subframe->samples, start, 0, 0, slot_len);
	slot->fd = cxvec_alloc(sym_len * 7, 0, 0, NULL, CXVEC_FLG_FFT_ALIGN);

	/* Initialize 7 symbols and 2 reference symbols */
	for (int l = 0; l < 7; l++) {
		lte_sym_init(slot, l);

		if ((l == 0) || (l == 4))
			lte_ref_init(slot, l);
	}

	return 0;
}

/*
 * Release an LTE slot object
 *
 * Do not release the sample vector or interpolator as those are always
 * allocated by the parent subframe.
 */
static void lte_slot_free(struct lte_slot *slot)
{
	lte_ref_free(&slot->refs[0]);
	lte_ref_free(&slot->refs[1]);

	for (int l = 0; l < 7; l++)
		lte_sym_free(&slot->syms[l]);

	cxvec_free(slot->td);
	cxvec_free(slot->fd);
}

static struct fft_hdl *create_ul_fft(int rbs)
{
	int ilen, olen, rev = 1;
	struct cxvec *in = NULL, *out = NULL;
	int slen = lte_sym_len(rbs);
	int clen = lte_cp_len(rbs);

	ilen = clen + slen;
	olen = slen;

	in = cxvec_alloc(7 * ilen, 0 , 0, NULL, CXVEC_FLG_FFT_ALIGN);
	out = cxvec_alloc(7 * olen, 0 , 0, NULL, CXVEC_FLG_FFT_ALIGN);

	return init_fft(rev, slen, 7, ilen, olen, 1, 1, in, out, 0);
}

struct lte_subframe *lte_ul_subframe_alloc(int rbs, int cell_id, int ant)
{
	struct lte_subframe *subframe;
	int subframe_len = lte_subframe_len(rbs);
	int flags = CXVEC_FLG_FFT_ALIGN;

	if (subframe_len < 0)
		return NULL;

	subframe = (struct lte_subframe *)
		   calloc(1, sizeof(struct lte_subframe));
	subframe->rbs = rbs;
	subframe->assigned = 0;
	subframe->samples = cxvec_alloc(subframe_len, 0, 0, NULL, flags);
	subframe->num_dci = 0;
	subframe->cell_id = cell_id;
	subframe->ant = ant;

	lte_slot_init(subframe, maps0, 0);
	lte_slot_init(subframe, maps1, 1);

	subframe->fft = create_ul_fft(rbs);
	if (!subframe->fft) {
		LOG_DSP_ERR("Internal FFT failure");
		return NULL;
	}

	/* Bit reservevation table */
	subframe->reserve = (int *) calloc(rbs * 12, sizeof(int));

	return subframe;
}

void lte_subframe_free(struct lte_subframe *subframe)
{
	if (!subframe)
		return;

	lte_slot_free(&subframe->slot[0]);
	lte_slot_free(&subframe->slot[1]);
	cxvec_free(subframe->samples);

	free_interp(subframe->interp);
	fft_free_hdl(subframe->fft);

	free(subframe->reserve);
	free(subframe);
}

static void ref_reset(struct lte_ref *ref)
{
	cxvec_reset(ref->refs[0]);
	cxvec_reset(ref->refs[1]);
}

static void slot_reset(struct lte_slot *slot,
		       struct lte_ref_map **maps)
{
	ref_reset(&slot->refs[0]);
	ref_reset(&slot->refs[1]);

	assign_ref_maps(slot, maps);
}

int lte_subframe_reset(struct lte_subframe *subframe,
		       struct lte_ref_map **map0, struct lte_ref_map **map1)
{
	/* Set new values */
	subframe->assigned = 0;
	subframe->num_dci = 0;

	slot_reset(&subframe->slot[0], map0);
	slot_reset(&subframe->slot[1], map1);

	memset(subframe->reserve, 0, subframe->rbs * 12 * sizeof(int));

	return 0;
}

/*
 * Run the FFT
 *
 * Compute frequency domain symbols for all 7 time domain symbols. For resource
 * block combinations with split center resource blocks, re-map the center block
 * to be consistent with converted samples.
 */
static int lte_slot_convert(struct lte_subframe *subframe, int ns)
{
	int edge_rb;
	struct lte_slot *slot = &subframe->slot[ns];

	cxvec_fft(subframe->fft, slot->syms[0].td, slot->fd);

	switch (slot->rbs) {
	case 15:
		edge_rb = 7;
		break;
	case 25:
		edge_rb = 12;
		break;
	case 75:
		edge_rb = 37;
		break;
	default:
		edge_rb = 0;
	}

	if (edge_rb) {
		for (int l = 0; l < 7; l++)
			lte_sym_rb_map_special(&slot->syms[l], edge_rb);
	}

	return 0;
}

static int lte_subframe_convert_refs(struct lte_subframe *subframe)
{
	int i, p, edge_rb;
	struct lte_ref *ref0 = &subframe->slot[0].refs[0];

	for (i = 0; i < 2; i++) {
		lte_slot_convert(subframe, i);
		lte_slot_chan_recov(&subframe->slot[i]);
	}

	avg_pilots(subframe);
	p = 0;
	cxvec_interp(subframe->interp, ref0->refs[p], ref0->chan[p]);

	if (subframe->ant == 2) {
		p = 1;
		cxvec_interp(subframe->interp, ref0->refs[p], ref0->chan[p]);
	}

	lte_combine_chan(ref0, subframe->ant);

	switch (subframe->rbs) {
	case 15:
		edge_rb = 7;
		break;
	case 25:
		edge_rb = 12;
		break;
	case 75:
		edge_rb = 37;
		break;
	default:
		edge_rb = 0;
	}

	if (edge_rb) {
		lte_sym_chan_rb_map_special(ref0, edge_rb, 0);
		lte_sym_chan_rb_map_special(ref0, edge_rb, 1);
		lte_sym_chan_rb_map_special(ref0, edge_rb, 2);
	}

	subframe->assigned = 1;

	return 0;
}

int lte_subframe_convert(struct lte_subframe *subframe)
{
	if (subframe->assigned)
		return 0;

	return lte_subframe_convert_refs(subframe);
}

static void gen_ul_scram_seq(int8_t *seq, int len, int ns,
			     int N_id_cell, unsigned rnti)
{
	unsigned c_init;

	c_init = rnti * (1 << 14) + ns / 2 * (1 << 9) + N_id_cell;

	lte_ul_gen_scrambler(c_init, seq, len);
}


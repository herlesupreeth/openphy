/*
 * LTE Physical Downlink Shared Channel (PDSCH)
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

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

#include "sigproc.h"
#include "subframe.h"
#include "pdsch.h"
#include "ofdm.h"
#include "qam.h"
#include "dci.h"
#include "pdsch_tbs.h"
#include "pdsch_vrb.h"
#include "pdsch_riv.h"
#include "pdsch_block.h"
#include "scramble.h"
#include "precode.h"
#include "slot.h"
#include "sigvec_internal.h"
#include "log.h"

#define LTE_RB_LEN		12

struct pdsch_slot {
	int num;
	int cfi;
	struct lte_slot *slot;
	struct lte_sym *syms[7];
};

struct pdsch_sym_blk {
	struct cxvec *vec;
	int idx;
	int len;
};

static struct pdsch_sym_blk *pdsch_sym_blk_alloc(int ant, int n_vrb, int cfi)
{
	int len;
	struct pdsch_sym_blk *sym_blk;

	if ((ant < 0) || (ant > 2) || (n_vrb < 0) || (cfi < 0))
		return NULL;

	if (ant == 2)
		len = n_vrb * (12 * (14 - cfi) - 12);
	else
		len = n_vrb * (12 * (14 - cfi) - 6);

	sym_blk = malloc(sizeof *sym_blk);
	sym_blk->idx = 0;
	sym_blk->len = len;
	sym_blk->vec = cxvec_alloc_simple(len);

	return sym_blk;
}

static void pdsch_sym_blk_free(struct pdsch_sym_blk *sym_blk)
{
	if (!sym_blk)
		return;

	cxvec_free(sym_blk->vec);
	free(sym_blk);
}

static struct pdsch_slot *pdsch_slot_alloc(struct lte_slot *slot,
					   int num, int cfi)
{
	struct pdsch_slot *pdsch;

	pdsch = malloc(sizeof *pdsch);
	pdsch->num = num;
	pdsch->cfi = !num ? cfi : 0;
	pdsch->slot = slot;

	for (int i = 0; i < 7; i++)
		pdsch->syms[i] = &slot->syms[i];

	return pdsch;
}

static void pdsch_slot_free(struct pdsch_slot *pdsch)
{
	free(pdsch);
}

enum pdsch_res_type {
	PDSCH_NORM,
	PDSCH_REF,
	PDSCH_SPECIAL,
};

static int chk_reserved(struct pdsch_slot *pdsch, int sc)
{
	int *pos = pdsch->slot->subframe->ref_indices;

	if ((sc == pos[0]) || (sc == pos[1]) ||
	    (sc == pos[2]) || (sc == pos[3]))
		return 1;

	return 0;
}

static int reserved_rb(int rbs, int rb, int slot, int l, int subframe)
{
	int start, end;

	if (!subframe) {
		if ((!slot && (l < 5)) || (slot && (l > 3)))
			return 0;
	} else if (subframe != 5) {
		return 0;
	} else if (subframe == 5) {
		if (!slot && (l < 5))
			return 0;
		if (slot)
			return 0;
	}

	switch (rbs) {
	case 6:
		start = 0;
		end = 5;
		break;
	case 15:
		start = 4;
		end = 9;
		break;
	case 25:
		if (rb == 9)
			return 2;
		if (rb == 15)
			return 3;

		start = 10;
		end = 14;
		break;
	case 50:
		start = 22;
		end = 27;
		break;
	case 75:
		if (rb == 34)
			return 2;
		if (rb == 40)
			return 3;

		start = 35;
		end = 39;
		break;
	case 100:
		start = 47;
		end = 52;
		break;
	default:
		LOG_PDSCH_ERR("PDSCH: Unsupported RB combination");
		return -1;
	}

	if ((rb >= start) && (rb <= end))
		return 1;

	return 0;
}

static int pdsch_extract_basic_tx1(struct pdsch_slot **pdsch, int chans,
				   struct lte_riv *riv,
				   struct pdsch_sym_blk *sym_blk,
				   int l, int subframe, int start)
{
	int rbs = pdsch[0]->slot->rbs;
	struct lte_sym *sym[chans];

	for (int i = 0; i < chans; i++)
		sym[i] = pdsch[i]->syms[l];

	int i = start;
	int n = 0, end = 12;

	if (reserved_rb(rbs, i, pdsch[0]->num, l, subframe) == 1) {
//		printf("PDSCH: Skipping RB %i, %i\n", i, l);
		return 0;
	}
	if (reserved_rb(rbs, i, pdsch[0]->num, l, subframe) == 2) {
		n = 0;
		end = 6;
	}
	if (reserved_rb(rbs, i, pdsch[0]->num, l, subframe) == 3) {
		n = 6;
		end = 12;
	}

	for (n = n / 2; n < end / 2; n++) {
//		printf("l %i, rb %i, sc %i, idx %i\n",
//		       l, i, 2 * n, sym_blk->idx);
		if (chans == 2) {
			lte_unprecode_1x2(sym[0], sym[1], i,
					  2 * n, 2 * n + 1,
					  sym_blk->vec, sym_blk->idx);
		} else {
			lte_unprecode_1x1(sym[0], i,
					  2 * n, 2 * n + 1,
					  sym_blk->vec, sym_blk->idx);
		}
		sym_blk->idx += 2;
	}

	return 0;
}

static int pdsch_extract_basic_tx2(struct pdsch_slot **pdsch,
				   int chans, struct lte_riv *riv,
				   struct pdsch_sym_blk *sym_blk,
				   int l, int subframe, int start)
{
	int i, rbs = pdsch[0]->slot->rbs;
	struct lte_sym *sym[chans];

	for (i = 0; i < chans; i++)
		sym[i] = pdsch[i]->syms[l];

	i = start;

	if (reserved_rb(rbs, i, pdsch[0]->num, l, subframe)) {
//		printf("PDSCH: Skipping RB %i, %i\n", i, l);
		return 0;
	}

	for (int n = 0; n < LTE_RB_LEN / 2; n++) {
//		printf("l %i, rb %i, sc %i, idx %i\n",
//			l, i, 2 * n, sym_blk->idx);
		if (chans == 2) {
			lte_unprecode_2x2(sym[0], sym[1], i,
					  2 * n, 2 * n + 1,
					  sym_blk->vec, sym_blk->idx);
		} else {
			lte_unprecode_2x1(sym[0], i,
					  2 * n, 2 * n + 1,
					  sym_blk->vec, sym_blk->idx);
		}

		sym_blk->idx += 2;
	}

	return 0;
}

static int pdsch_extract_ref_tx1(struct pdsch_slot **pdsch, int chans,
				 struct lte_riv *riv,
				 struct pdsch_sym_blk *sym_blk,
				 int l, int subframe, int start)
{
	int i, sc0, sc1;
	int rbs = pdsch[0]->slot->rbs;
	struct lte_sym *sym[chans];

	for (i = 0; i < chans; i++)
		sym[i] = pdsch[i]->syms[l];

	i = start;

	if (reserved_rb(rbs, i, pdsch[0]->num, l, subframe))
		return 0;

	for (int n = 0; n < LTE_RB_LEN;) {
		if (lte_chk_ref(pdsch[0]->slot->subframe, pdsch[0]->num, l, n, 1)) {
			sc0 = n + 1;
			sc1 = n + 2;
			n += 3;
		} else if (lte_chk_ref(pdsch[0]->slot->subframe, pdsch[0]->num, l, n + 1, 1)) {
			sc0 = n + 0;
			sc1 = n + 2;
			n += 3;
		} else {
			sc0 = n + 0;
			sc1 = n + 1;
			n += 2;
		}

		if (chans == 2)
			lte_unprecode_1x2(sym[0], sym[1], i, sc0, sc1,
					  sym_blk->vec, sym_blk->idx);
		else
			lte_unprecode_1x1(sym[0], i, sc0, sc1,
					  sym_blk->vec, sym_blk->idx);

		sym_blk->idx += 2;
	}

	return 0;
}

static int pdsch_extract_ref_tx2(struct pdsch_slot **pdsch, int chans,
				 struct lte_riv *riv,
				 struct pdsch_sym_blk *sym_blk,
				 int l, int subframe, int start)
{
	int i, sc0, sc1, sc2, sc3;
	int rbs = pdsch[0]->slot->rbs;
	struct lte_sym *sym[chans];

	for (i = 0; i < chans; i++)
		sym[i] = pdsch[i]->syms[l];

	i = start;

	if (reserved_rb(rbs, i, pdsch[0]->num, l, subframe))
		return 0;

	for (int n = 0; n < LTE_RB_LEN; n += 6) {
		if (chk_reserved(pdsch[0], n)) {
			sc0 = n + 1;
			sc1 = n + 2;
			sc2 = n + 4;
			sc3 = n + 5;
		} else if (chk_reserved(pdsch[0], n + 1)) {
			sc0 = n + 0;
			sc1 = n + 2;
			sc2 = n + 3;
			sc3 = n + 5;
		} else if (chk_reserved(pdsch[0], n + 2)) {
			sc0 = n + 0;
			sc1 = n + 1;
			sc2 = n + 3;
			sc3 = n + 4;
		} else {
			LOG_PDSCH_ERR("PBCH: Reference map fault");
			return -1;
		}

		if (chans == 2) {
			lte_unprecode_2x2(sym[0], sym[1], i, sc0, sc1,
					  sym_blk->vec, sym_blk->idx);
			sym_blk->idx += 2;
			lte_unprecode_2x2(sym[0], sym[1], i, sc2, sc3,
					  sym_blk->vec, sym_blk->idx);
			sym_blk->idx += 2;
		} else {
			lte_unprecode_2x1(sym[0], i, sc0, sc1,
					  sym_blk->vec, sym_blk->idx);
			sym_blk->idx += 2;
			lte_unprecode_2x1(sym[0], i, sc2, sc3,
					  sym_blk->vec, sym_blk->idx);
			sym_blk->idx += 2;
		}
	}

	return 0;
}

static int pdsch_extract_rb(struct pdsch_slot **slot, int chans, int ant,
			    int l, int subframe, struct lte_riv *riv,
			    struct pdsch_sym_blk *sym_blk, int rb)
{
	if (!l || (l == 4)) {
		if (ant == 2)
			pdsch_extract_ref_tx2(slot, chans, riv,
					      sym_blk, l, subframe, rb);
		if (ant == 1)
			pdsch_extract_ref_tx1(slot, chans, riv,
					      sym_blk, l, subframe, rb);
	} else {
		if (ant == 2)
			pdsch_extract_basic_tx2(slot, chans,
						riv, sym_blk, l, subframe, rb);
		if (ant == 1)
			pdsch_extract_basic_tx1(slot, chans,
						riv, sym_blk, l, subframe, rb);
	}

	return 0;
}

static void gen_scram_seq(int8_t *seq, int len, int sf,
			  int n_id_cell, unsigned rnti)
{
	unsigned c_init, q = 0;

	c_init = rnti * (1 << 14) + q * (1 << 13) +
		 (2 * sf / 2) * (1 << 9) + n_id_cell;

	lte_pbch_gen_scrambler(c_init, seq, len);
}

static void pdsch_log_blk_info0(int tbs, int G)
{
	char sbuf[80];

	snprintf(sbuf, 80, "PDSCH : Transport block size A=%i, Physical bits G=%i",
		 tbs, G);
	LOG_DATA(sbuf);
}

static void pdsch_log_blk_info1(int rnti, int mod, int rv)
{
	char sbuf[80];

	snprintf(sbuf, 80, "PDSCH : RNTI %i, Modulation %i, Redundancy version %i",
		 rnti, mod, rv);
	LOG_DATA(sbuf);
}

static int pdsch_get_rv(struct lte_dci *dci, struct lte_time *ltime)
{
	switch (dci->type) {
	case LTE_DCI_FORMAT1:
		return lte_dci_get_val(dci, LTE_DCI_FORMAT1_RV);
	case LTE_DCI_FORMAT1A:
		return lte_dci_get_val(dci, LTE_DCI_FORMAT1A_RV);
	case LTE_DCI_FORMAT1B:
		return lte_dci_get_val(dci, LTE_DCI_FORMAT1B_RV);
	case LTE_DCI_FORMAT1C:
		/* Redundancy version for SIB1 and a guess for other SIBs */
		if (dci->rnti == 65535) {
			int k = (ltime->frame / 2) % 4;
			return (int) ceilf(3.0f / 2.0f * (float) k) % 4;
		}
		return 0;
	case LTE_DCI_FORMAT1D:
		return lte_dci_get_val(dci, LTE_DCI_FORMAT1D_RV);
	case LTE_DCI_FORMAT2:
		return lte_dci_get_val(dci, LTE_DCI_FORMAT2_RV);
	case LTE_DCI_FORMAT2A:
		return lte_dci_get_val(dci, LTE_DCI_FORMAT2A_RV_1);
	}

	return -1;
}

#define SI_RNTI		0xffff

int lte_wireshark_send(uint8_t *data, int len,
		       int subframe, int si_rnti, int rnti);

static int pdsch_decode_blk(struct pdsch_sym_blk *pblk, int n_id_cell,
			    struct lte_dci *dci, int vrb,
			    struct lte_pdsch_blk *tblk, struct lte_time *ltime)
{
	int i, G, rv;
	signed char *f;
	uint8_t *a;

	int mod = lte_tbs_get_mod_order(dci);
	if (mod < 0)
		return -1;

	/* Transport block size */
	int tbs = lte_tbs_get(dci, vrb, dci->rnti);
	if (tbs < 0) {
		LOG_PDSCH_ERR("Transport block size determination failed");
		return -1;
	}

	/* Redundancy version */
	rv = pdsch_get_rv(dci, ltime);
	if (rv < 0) {
		LOG_PDSCH_ERR("Invalid redundancy version");
		exit(-1);
	}

	/* Overall number of physical bits */
	G = mod * pblk->idx;

	pdsch_log_blk_info0(tbs, G);
	pdsch_log_blk_info1(dci->rnti, mod, rv);

	/* Initialize the transport block handler */
	if (lte_pdsch_blk_init(tblk, tbs, G, 1, mod) < 0) {
		LOG_PDSCH_ERR("Transport block initialization failed");
		return -1;
	}

	f = lte_pdsch_blk_fbuf(tblk, G);

	switch (mod) {
	case 2:
		lte_qpsk_decode2(pblk->vec, f, G);
		break;
	case 4:
		lte_qam16_decode(pblk->vec, f, G);
		break;
	case 6:
		lte_qam64_decode(pblk->vec, f, G);
		break;
	case 8:
		lte_qam256_decode(pblk->vec, f, G);
		break;
	default:
		LOG_PDSCH_ERR("Invalid modulation format");
		return -1;
	}

	/* Descramble */
	int8_t seq[G];
	gen_scram_seq(seq, G, ltime->subframe, n_id_cell, dci->rnti);
	lte_scramble2(f, seq, G);

	/* Decode the transport block */
	if (!lte_pdsch_blk_decode(tblk, rv))
		goto wireshark;

	/* If we fail on DCI Format 1C, try all redundancy versions */
	if (dci->type == LTE_DCI_FORMAT1C) {
		for (i = 0; i < 4; i++) {
			if (i == rv)
				continue;

			pdsch_log_blk_info1(dci->rnti, mod, i);
			if (!lte_pdsch_blk_decode(tblk, i))
				goto wireshark;
		}
	}

	return 0;

wireshark:
	a = lte_pdsch_blk_abuf(tblk, tbs);
	if (!a)
		return -1;

	int si = dci->rnti == SI_RNTI;

	lte_wireshark_send(a, tbs / 8, ltime->subframe,
			   si, dci->rnti);
	return 1;
}

static int pdsch_extract_symbols(struct pdsch_slot **slot,
				 int rx_ants, int tx_ants, int sf,
				 struct lte_riv *riv,
				 struct pdsch_sym_blk *sym_blk)
{
	int i, l, cfi = 0;
	int *prbs_indices;

	if (riv->n_vrb >= 110) {
		LOG_PDSCH_ERR("Invalid RIV number");
		return -1;
	}

	if (!slot[0]->num) {
		prbs_indices = riv->prbs0;
		cfi = slot[0]->cfi;
	} else {
		prbs_indices = riv->prbs1;
	}

	/* Extract symbols slot 0 */
	for (l = cfi; l < 7; l++) {
		for (i = 0; i < riv->n_vrb; i++) {
			pdsch_extract_rb(slot, rx_ants, tx_ants, l, sf,
					 riv, sym_blk, prbs_indices[i]);
		}
	}

	return 0;
}

/* Decode one slot of sample data using specified reference signal map */
int lte_decode_pdsch(struct lte_subframe **subframe,
		     int chans,
		     struct lte_pdsch_blk *tblk,
		     int cfi, int dci_index,
		     struct lte_time *ltime)
{
	int i, rc, sf = ltime->subframe;
	int tx_ants = subframe[0]->tx_ants;
	struct pdsch_slot *slot0[chans];
	struct pdsch_slot *slot1[chans];
	struct pdsch_sym_blk *sym_blk;

	struct lte_riv riv = {
		.offset = 0,
		.step = 0,
		.n_vrb = 0
	};

	if (dci_index >= subframe[0]->num_dci) {
		LOG_PDSCH_ERR("No DCI available");
		return -1;
	}

	if (!subframe[0]->assigned) {
		LOG_PDSCH_ERR("Subframe not assigned");
		return -1;
	}

	rc = lte_decode_riv(subframe[0]->rbs,
			    &subframe[0]->dci[dci_index], &riv);
	if (rc < 0) {
		LOG_PDSCH_ERR("Failed to recover RIV");
		return -1;
	}

	if (subframe[0]->rbs <= 10)
		cfi++;

	for (int i = 0; i < chans; i++) {
		slot0[i] = pdsch_slot_alloc(&subframe[i]->slot[0], 0, cfi);
		slot1[i] = pdsch_slot_alloc(&subframe[i]->slot[1], 1, 0);

		if (!slot0[i] || !slot1[i]) {
			LOG_PDSCH_ERR("PDSCH: Failed to allocate slot");
			return -1;
		}
	}


	sym_blk = pdsch_sym_blk_alloc(tx_ants, riv.n_vrb, cfi);

	pdsch_extract_symbols(slot0, chans, tx_ants, sf, &riv, sym_blk);
	pdsch_extract_symbols(slot1, chans, tx_ants, sf, &riv, sym_blk);

	rc = pdsch_decode_blk(sym_blk, subframe[0]->cell_id,
			      &subframe[0]->dci[dci_index],
			      riv.n_vrb, tblk, ltime);
	if (rc < 0)
		goto release;
release:
	for (i = 0; i < chans; i++) {
		pdsch_slot_free(slot0[i]);
		pdsch_slot_free(slot1[i]);
	}

	pdsch_sym_blk_free(sym_blk);

	return rc;
}

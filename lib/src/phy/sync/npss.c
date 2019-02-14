/**
 *
 * \section COPYRIGHT
 *
 * Copyright 2013-2015 Software Radio Systems Limited
 *
 * \section LICENSE
 *
 * This file is part of the srsLTE library.
 *
 * srsLTE is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as
 * published by the Free Software Foundation, either version 3 of
 * the License, or (at your option) any later version.
 *
 * srsLTE is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * A copy of the GNU Affero General Public License can be found in
 * the LICENSE file in the top-level directory of this distribution
 * and at http://www.gnu.org/licenses/.
 *
 */


#include <strings.h>
#include <string.h>
#include <stdlib.h>
#include <complex.h>
#include <math.h>

#include "srslte/phy/sync/npss.h"
#include "srslte/phy/utils/debug.h"


int srslte_npss_init_N_id_2(cf_t *npss_signal_freq, cf_t *npss_signal_time,
                                 uint32_t N_id_2, uint32_t fft_size, int cfo_i) {
  srslte_dft_plan_t plan;
  cf_t npss_signal_pad[2048];
  int ret = SRSLTE_ERROR_INVALID_INPUTS;
  
  if (srslte_N_id_2_isvalid(N_id_2)    && 
      fft_size                  <= 2048) 
  {
    
    srslte_npss_generate(npss_signal_freq, N_id_2);

    bzero(npss_signal_pad, fft_size * sizeof(cf_t));
    bzero(npss_signal_time, fft_size * sizeof(cf_t));
    memcpy(&npss_signal_pad[(fft_size-SRSLTE_NPSS_LEN)/2+cfo_i], npss_signal_freq, SRSLTE_NPSS_LEN * sizeof(cf_t));

    /* Convert signal into the time domain */    
    if (srslte_dft_plan(&plan, fft_size, SRSLTE_DFT_BACKWARD, SRSLTE_DFT_COMPLEX)) {
      return SRSLTE_ERROR;
    }
    
    srslte_dft_plan_set_mirror(&plan, true);
    srslte_dft_plan_set_dc(&plan, true);
    srslte_dft_plan_set_norm(&plan, true);
    srslte_dft_run_c(&plan, npss_signal_pad, npss_signal_time);

    srslte_vec_conj_cc(npss_signal_time, npss_signal_time, fft_size);
    srslte_vec_sc_prod_cfc(npss_signal_time, 1.0/SRSLTE_NPSS_LEN, npss_signal_time, fft_size);

    srslte_dft_plan_free(&plan);

    ret = SRSLTE_SUCCESS;
  }
  return ret;
}

/* Initializes the NPSS synchronization object with fft_size=128
 */
int srslte_npss_init(srslte_npss_t *q, uint32_t frame_size) {
  return srslte_npss_init_fft(q, frame_size, 128);
}

int srslte_npss_init_fft(srslte_npss_t *q, uint32_t frame_size, uint32_t fft_size) {
  return srslte_npss_init_fft_offset(q, frame_size, fft_size, 0);
}

int srslte_npss_init_fft_offset(srslte_npss_t *q, uint32_t frame_size, uint32_t fft_size, int offset) {
  return srslte_npss_init_fft_offset_decim(q, frame_size, fft_size,  offset,  1);
}

/* Initializes the NPSS synchronization object. 
 * 
 * It correlates a signal of frame_size samples with the PSS sequence in the frequency 
 * domain. The PSS sequence is transformed using fft_size samples. 
 */
int srslte_npss_init_fft_offset_decim(srslte_npss_t *q,
                                           uint32_t max_frame_size, uint32_t max_fft_size,
                                           int offset, int decimate)
{

  int ret = SRSLTE_ERROR_INVALID_INPUTS;
  if (q != NULL) {
  
    ret = SRSLTE_ERROR; 
    
    uint32_t N_id_2; 
    uint32_t buffer_size;
    bzero(q, sizeof(srslte_npss_t));
    
    q->N_id_2 = 10;  
    q->ema_alpha = 0.2;

    q->max_fft_size  = max_fft_size;
    q->max_frame_size = max_frame_size;

    q->decimate = decimate;
    uint32_t fft_size = max_fft_size/q->decimate;
    uint32_t frame_size = max_frame_size/q->decimate;
    
    q->fft_size = fft_size;
    q->frame_size = frame_size;

    buffer_size = fft_size + frame_size + 1;

    q->filter_npss_enable = false;
    q->chest_on_filter   = false;

    if(q->decimate > 1) {
        int filter_order = 3;
        srslte_filt_decim_cc_init(&q->filter,q->decimate,filter_order);
        q->filter.filter_output = srslte_vec_malloc((buffer_size) * sizeof(cf_t));
        q->filter.downsampled_input = srslte_vec_malloc((buffer_size + filter_order) * sizeof(cf_t));
        printf("decimation for the  NPSS search is %d \n",q->decimate);
    }
      
    if (srslte_dft_plan(&q->dftp_input, fft_size, SRSLTE_DFT_FORWARD, SRSLTE_DFT_COMPLEX)) {
      fprintf(stderr, "Error creating DFT plan \n");
      goto clean_and_exit;
    }
    srslte_dft_plan_set_mirror(&q->dftp_input, true);
    srslte_dft_plan_set_dc(&q->dftp_input, true);
    srslte_dft_plan_set_norm(&q->dftp_input, false);

    if (srslte_dft_plan(&q->idftp_input, fft_size, SRSLTE_DFT_BACKWARD, SRSLTE_DFT_COMPLEX)) {
      fprintf(stderr, "Error creating DFT plan \n");
      goto clean_and_exit;
    }
    srslte_dft_plan_set_mirror(&q->idftp_input, true);
    srslte_dft_plan_set_dc(&q->idftp_input, true);
    srslte_dft_plan_set_norm(&q->idftp_input, false);

    bzero(q->tmp_fft2, sizeof(cf_t)*SRSLTE_SYMBOL_SZ_MAX);

    q->tmp_input = srslte_vec_malloc((buffer_size + frame_size*(q->decimate - 1)) * sizeof(cf_t));
    if (!q->tmp_input) {
      fprintf(stderr, "Error allocating memory\n");
      goto clean_and_exit;
    }

    bzero(&q->tmp_input[q->frame_size], q->fft_size * sizeof(cf_t));

    q->conv_output = srslte_vec_malloc(buffer_size * sizeof(cf_t));
    if (!q->conv_output) {
      fprintf(stderr, "Error allocating memory\n");
      goto clean_and_exit;
    }
    bzero(q->conv_output, sizeof(cf_t) * buffer_size);
    q->conv_output_avg = srslte_vec_malloc(buffer_size * sizeof(float));
    if (!q->conv_output_avg) {
      fprintf(stderr, "Error allocating memory\n");
      goto clean_and_exit;
    }
    bzero(q->conv_output_avg, sizeof(float) * buffer_size);
#ifdef SRSLTE_PSS_ACCUMULATE_ABS
    q->conv_output_abs = srslte_vec_malloc(buffer_size * sizeof(float));
    if (!q->conv_output_abs) {
      fprintf(stderr, "Error allocating memory\n");
      goto clean_and_exit;
    }
    bzero(q->conv_output_abs, sizeof(float) * buffer_size);
#endif

    for (N_id_2=0;N_id_2<3;N_id_2++) {
      q->pss_signal_time[N_id_2] = srslte_vec_malloc(buffer_size * sizeof(cf_t));
      if (!q->pss_signal_time[N_id_2]) {
        fprintf(stderr, "Error allocating memory\n");
        goto clean_and_exit;
      }
      /* The NPSS is translated into the time domain for each N_id_2  */
      if (srslte_npss_init_N_id_2(q->npss_signal_freq[N_id_2], q->npss_signal_time[N_id_2], N_id_2, fft_size, offset)) {
        fprintf(stderr, "Error initiating NPSS detector for N_id_2=%d fft_size=%d\n", N_id_2, fft_size);
        goto clean_and_exit;
      }
      bzero(&q->npss_signal_time[N_id_2][q->fft_size], q->frame_size * sizeof(cf_t));
    }
    #ifdef CONVOLUTION_FFT


    if (srslte_conv_fft_cc_init(&q->conv_fft, frame_size, fft_size)) {
      fprintf(stderr, "Error initiating convolution FFT\n");
      goto clean_and_exit;
    }
    for(N_id_2=0; N_id_2<3; N_id_2++) {
      q->npss_signal_freq_full[N_id_2] = srslte_vec_malloc(buffer_size * sizeof(cf_t));
      srslte_dft_run_c(&q->conv_fft.filter_plan, q->npss_signal_time[N_id_2], q->npss_signal_freq_full[N_id_2]);
    }

    #endif

    srslte_npss_reset(q);

    ret = SRSLTE_SUCCESS;
  }

clean_and_exit:
  if (ret == SRSLTE_ERROR) {
    srslte_npss_free(q);
  }
  return ret;

}


/* Initializes the PSS synchronization object.
 *
 * It correlates a signal of frame_size samples with the PSS sequence in the frequency
 * domain. The PSS sequence is transformed using fft_size samples.
 */
int srslte_npss_resize(srslte_npss_t *q, uint32_t frame_size, uint32_t fft_size, int offset) {


  int ret = SRSLTE_ERROR_INVALID_INPUTS;
  if (q != NULL) {

    ret = SRSLTE_ERROR;

    if (fft_size > q->max_fft_size || frame_size > q->max_frame_size) {
      fprintf(stderr, "Error in npss_config(): fft_size and frame_size must be lower than initialized\n");
      return SRSLTE_ERROR;
    }

    uint32_t N_id_2;
    uint32_t buffer_size;

    q->N_id_2 = 10;
    q->ema_alpha = 0.2;

    fft_size = fft_size/q->decimate;
    frame_size = frame_size/q->decimate;

    q->fft_size = fft_size;
    q->frame_size = frame_size;

    buffer_size = fft_size + frame_size + 1;

    if (srslte_dft_replan(&q->dftp_input, fft_size)) {
      fprintf(stderr, "Error creating DFT plan \n");
      return SRSLTE_ERROR;
    }

    if (srslte_dft_replan(&q->idftp_input, fft_size)) {
      fprintf(stderr, "Error creating DFT plan \n");
      return SRSLTE_ERROR;
    }

    bzero(q->tmp_fft2, sizeof(cf_t)*SRSLTE_SYMBOL_SZ_MAX);

    bzero(&q->tmp_input[q->frame_size], q->fft_size * sizeof(cf_t));
    bzero(q->conv_output, sizeof(cf_t) * buffer_size);
    bzero(q->conv_output_avg, sizeof(float) * buffer_size);

#ifdef SRSLTE_PSS_ACCUMULATE_ABS
    bzero(q->conv_output_abs, sizeof(float) * buffer_size);
#endif

    // Generate PSS sequences for this FFT size
    for (N_id_2=0;N_id_2<3;N_id_2++) {
      if (srslte_npss_init_N_id_2(q->npss_signal_freq[N_id_2], q->npss_signal_time[N_id_2], N_id_2, fft_size, offset)) {
        fprintf(stderr, "Error initiating NPSS detector for N_id_2=%d fft_size=%d\n", N_id_2, fft_size);
        return SRSLTE_ERROR;
      }
      bzero(&q->npss_signal_time[N_id_2][q->fft_size], q->frame_size * sizeof(cf_t));
    }
#ifdef CONVOLUTION_FFT

    if (srslte_conv_fft_cc_replan(&q->conv_fft, frame_size, fft_size)) {
      fprintf(stderr, "Error initiating convolution FFT\n");
      return SRSLTE_ERROR;
    }
    for(int i =0; i< 3; i++) {
      srslte_dft_run_c(&q->conv_fft.filter_plan, q->npss_signal_time[i], q->npss_signal_freq_full[i]);
    }

#endif

    srslte_npss_reset(q);

    ret = SRSLTE_SUCCESS;
  }
  return ret;

}

void srslte_npss_free(srslte_npss_t *q) {
  uint32_t i;

  if (q) {
    for (i=0;i<3;i++) {
      if (q->npss_signal_time[i]) {
        free(q->npss_signal_time[i]);
      }
      if(q->npss_signal_freq_full[i]){
        free(q->npss_signal_freq_full[i]);
      }
    }
  #ifdef CONVOLUTION_FFT
    srslte_conv_fft_cc_free(&q->conv_fft);

  #endif
    if (q->tmp_input) {
      free(q->tmp_input);
    }
    if (q->conv_output) {
      free(q->conv_output);
    }
    if (q->conv_output_abs) {
      free(q->conv_output_abs);
    }
    if (q->conv_output_avg) {
      free(q->conv_output_avg);
    }

    srslte_dft_plan_free(&q->dftp_input);
    srslte_dft_plan_free(&q->idftp_input);

    if(q->decimate > 1)
    {
        srslte_filt_decim_cc_free(&q->filter);
        free(q->filter.filter_output);
        free(q->filter.downsampled_input);
    }


    bzero(q, sizeof(srslte_pss_t));
  }
}

void srslte_npss_reset(srslte_pss_t *q) {
  uint32_t buffer_size = q->fft_size + q->frame_size + 1;
  bzero(q->conv_output_avg, sizeof(float) * buffer_size);
}

/**
 * This function calculates the Zadoff-Chu sequence.
 * @param signal Output array.
 */
int srslte_npss_generate(cf_t *signal, uint32_t N_id_2) {
  int i;
  float arg;
  const float root_value[] = { 25.0, 29.0, 34.0 };
  const int sync_value[] = { 1, 1, 1, 1, -1, -1, 1, 1, 1, 1, -1, 1};
  int root_idx;

  int sign = -1;

  if (N_id_2 > 2) {
    fprintf(stderr, "Invalid N_id_2 %d\n", N_id_2);
    return -1;
  }

  root_idx = N_id_2;

//TODO
// Cyclic Prefix S(l), n = 0 ~ 10
  for (i = 0; i < SRSLTE_NPSS_LEN;  i++) {
    arg = (float) sync_value[i] * (float) sign * M_PI * root_value[root_idx]
        * ((float) i * ((float) i + 1.0)) / 11.0;
    __real__ signal[i] = cosf(arg);
    __imag__ signal[i] = sinf(arg);
  }
  return 0;
}

/** 36.211 10.3 section 6.11.1.2
 */
void srslte_npss_put_slot(cf_t *npss_signal, cf_t *slot, uint32_t nof_prb, srslte_cp_t cp) {
  int k;
  k = (SRSLTE_CP_NSYMB(cp) - 1) * nof_prb * SRSLTE_NRE + nof_prb * SRSLTE_NRE / 2 - 31;
  memset(&slot[k - 5], 0, 5 * sizeof(cf_t));
  memcpy(&slot[k], npss_signal, SRSLTE_NPSS_LEN * sizeof(cf_t));
  memset(&slot[k + SRSLTE_NPSS_LEN], 0, 5 * sizeof(cf_t));
}

void srslte_npss_get_slot(cf_t *slot, cf_t *pss_signal, uint32_t nof_prb, srslte_cp_t cp) {
  int k;
  k = (SRSLTE_CP_NSYMB(cp) - 1) * nof_prb * SRSLTE_NRE + nof_prb * SRSLTE_NRE / 2 - 31;
  memcpy(npss_signal, &slot[k], SRSLTE_NPSS_LEN * sizeof(cf_t));
}


/** Sets the current N_id_2 value. Returns -1 on error, 0 otherwise
 */
int srslte_npss_set_N_id_2(srslte_npss_t *q, uint32_t N_id_2) {
  if (!srslte_N_id_2_isvalid((N_id_2))) {
    fprintf(stderr, "Invalid N_id_2 %d\n", N_id_2);
    return -1;
  } else {
    q->N_id_2 = N_id_2;
    return 0;
  }
}

/* Sets the weight factor alpha for the exponential moving average of the PSS correlation output
 */
void srslte_npss_set_ema_alpha(srslte_npss_t *q, float alpha) {
  q->ema_alpha = alpha;
}

float compute_peak_sidelobe(srslte_pss_t *q, uint32_t corr_peak_pos, uint32_t conv_output_len)
{
  // Find end of peak lobe to the right
  int pl_ub = corr_peak_pos+1;
  while(q->conv_output_avg[pl_ub+1] <= q->conv_output_avg[pl_ub] && pl_ub < conv_output_len) {
    pl_ub ++;
  }
  // Find end of peak lobe to the left
  int pl_lb;
  if (corr_peak_pos > 2) {
    pl_lb = corr_peak_pos-1;
    while(q->conv_output_avg[pl_lb-1] <= q->conv_output_avg[pl_lb] && pl_lb > 1) {
      pl_lb --;
    }
  } else {
    pl_lb = 0;
  }

  int sl_distance_right = conv_output_len-1-pl_ub;
  if (sl_distance_right < 0) {
    sl_distance_right = 0;
  }
  int sl_distance_left = pl_lb;

  int sl_right = pl_ub+srslte_vec_max_fi(&q->conv_output_avg[pl_ub], sl_distance_right);
  int sl_left = srslte_vec_max_fi(q->conv_output_avg, sl_distance_left);
  float side_lobe_value = SRSLTE_MAX(q->conv_output_avg[sl_right], q->conv_output_avg[sl_left]);

  return q->conv_output_avg[corr_peak_pos]/side_lobe_value;
}

/** Performs time-domain PSS correlation.
 * Returns the index of the PSS correlation peak in a subframe.
 * The frame starts at corr_peak_pos-subframe_size/2.
 * The value of the correlation is stored in corr_peak_value.
 *
 * Input buffer must be subframe_size long.
 */
int srslte_npss_find_npss(srslte_npss_t *q, const cf_t *input, float *corr_peak_value)
{
  int ret = SRSLTE_ERROR_INVALID_INPUTS;

  if (q                 != NULL  &&
      input             != NULL)
  {

    uint32_t corr_peak_pos;
    uint32_t conv_output_len;

    if (!srslte_N_id_2_isvalid(q->N_id_2)) {
      fprintf(stderr, "Error finding PSS peak, Must set N_id_2 first\n");
      return SRSLTE_ERROR;
    }

    /* Correlate input with NPSS sequence
     *
     * We do not reverse time-domain NPSS signal because it's conjugate is symmetric.
     * The conjugate operation on npss_signal_time has been done in srslte_pss_init_N_id_2
     * This is why we can use FFT-based convolution
     */
    if (q->frame_size >= q->fft_size) {
    #ifdef CONVOLUTION_FFT
      memcpy(q->tmp_input, input, (q->frame_size * q->decimate) * sizeof(cf_t));
      if(q->decimate > 1) {
        srslte_filt_decim_cc_execute(&(q->filter), q->tmp_input, q->filter.downsampled_input, q->filter.filter_output , (q->frame_size * q->decimate));
        conv_output_len = srslte_conv_fft_cc_run_opt(&q->conv_fft, q->filter.filter_output,q->pss_signal_freq_full[q->N_id_2], q->conv_output);
      } else {
        conv_output_len = srslte_conv_fft_cc_run_opt(&q->conv_fft, q->tmp_input, q->pss_signal_freq_full[q->N_id_2], q->conv_output);
      }

    #else
      conv_output_len = srslte_conv_cc(input, q->pss_signal_time[q->N_id_2], q->conv_output, q->frame_size, q->fft_size);
    #endif
    } else {
      for (int i=0;i<q->frame_size;i++) {
        q->conv_output[i] = srslte_vec_dot_prod_ccc(q->pss_signal_time[q->N_id_2], &input[i], q->fft_size);
      }
      conv_output_len = q->frame_size;
    }

    // Compute modulus square
    srslte_vec_abs_square_cf(q->conv_output, q->conv_output_abs, conv_output_len-1);

    // If enabled, average the absolute value from previous calls
    if (q->ema_alpha < 1.0 && q->ema_alpha > 0.0) {
      srslte_vec_sc_prod_fff(q->conv_output_abs, q->ema_alpha, q->conv_output_abs, conv_output_len-1);
      srslte_vec_sc_prod_fff(q->conv_output_avg, 1-q->ema_alpha, q->conv_output_avg, conv_output_len-1);

      srslte_vec_sum_fff(q->conv_output_abs, q->conv_output_avg, q->conv_output_avg, conv_output_len-1);
    } else {
      memcpy(q->conv_output_avg, q->conv_output_abs, sizeof(float)*(conv_output_len-1));
    }

    /* Find maximum of the absolute value of the correlation */
    corr_peak_pos = srslte_vec_max_fi(q->conv_output_avg, conv_output_len-1);

    // save absolute value
    q->peak_value = q->conv_output_avg[corr_peak_pos];

#ifdef SRSLTE_PSS_RETURN_PSR
    if (corr_peak_value) {
      *corr_peak_value = compute_peak_sidelobe(q, corr_peak_pos, conv_output_len);
    }
#else
    if (corr_peak_value) {
      *corr_peak_value = q->conv_output_avg[corr_peak_pos];
    }
#endif

    if(q->decimate >1) {
      int decimation_correction = (q->filter.num_taps - 2);
      corr_peak_pos = corr_peak_pos - decimation_correction;
      corr_peak_pos = corr_peak_pos*q->decimate;
    }

    if (q->frame_size >= q->fft_size) {
      ret = (int) corr_peak_pos;
    } else {
      ret = (int) corr_peak_pos + q->fft_size;
    }
  }
  return ret;
}

/* Computes frequency-domain channel estimation of the PSS symbol
 * input signal is in the time-domain.
 * ce is the returned frequency-domain channel estimates.
 */
int srslte_npss_chest(srslte_npss_t *q, const cf_t *input, cf_t ce[SRSLTE_NPSS_LEN]) {
  int ret = SRSLTE_ERROR_INVALID_INPUTS;

  if (q                 != NULL  &&
      input             != NULL)
  {

    if (!srslte_N_id_2_isvalid(q->N_id_2)) {
      fprintf(stderr, "Error finding PSS peak, Must set N_id_2 first\n");
      return SRSLTE_ERROR;
    }

    /* Transform to frequency-domain */
    srslte_dft_run_c(&q->dftp_input, input, q->tmp_fft);

    /* Compute channel estimate taking the NPSS sequence as reference */
    srslte_vec_prod_conj_ccc(&q->tmp_fft[(q->fft_size-SRSLTE_NPSS_LEN)/2], q->npss_signal_freq[q->N_id_2], ce, SRSLTE_NPSS_LEN);

    ret = SRSLTE_SUCCESS;
  }
  return ret;
}

/* input points to beginning of last OFDM symbol of slot 0 of subframe 0 or 5
 * It must be called after calling srslte_pss_cfo_compute() with filter enabled
 */
void srslte_pss_sic(srslte_pss_t *q, cf_t *input) {
  if (q->chest_on_filter) {

    bzero(q->tmp_fft, sizeof(cf_t)*q->fft_size);

    // Pass transmitted PSS sequence through the channel
    srslte_vec_prod_ccc(q->pss_signal_freq[q->N_id_2], q->tmp_ce, &q->tmp_fft[(q->fft_size-SRSLTE_PSS_LEN)/2], SRSLTE_PSS_LEN);

    // Get time-domain version of the received PSS
    srslte_dft_run_c(&q->idftp_input, q->tmp_fft, q->tmp_fft2);

    // Substract received PSS from this N_id_2 from the input signal
    srslte_vec_sc_prod_cfc(q->tmp_fft2, 1.0/q->fft_size, q->tmp_fft2, q->fft_size);
    srslte_vec_sub_ccc(input, q->tmp_fft2, input, q->fft_size);

  } else {
    fprintf(stderr, "Error calling srslte_pss_sic(): need to enable channel estimation on filtering\n");
  }
}

// Frequency-domain filtering of the central 64 sub-carriers
void srslte_pss_filter(srslte_pss_t *q, const cf_t *input, cf_t *output)
{
  srslte_dft_run_c(&q->dftp_input, input, q->tmp_fft);

  memcpy(&q->tmp_fft2[q->fft_size/2-SRSLTE_PSS_LEN/2],
         &q->tmp_fft[q->fft_size/2-SRSLTE_PSS_LEN/2],
         sizeof(cf_t)*SRSLTE_PSS_LEN);

  if (q->chest_on_filter) {
    srslte_vec_prod_conj_ccc(&q->tmp_fft[(q->fft_size-SRSLTE_PSS_LEN)/2], q->pss_signal_freq[q->N_id_2], q->tmp_ce, SRSLTE_PSS_LEN);
  }

  srslte_dft_run_c(&q->idftp_input, q->tmp_fft2, output);
}

/* Returns the CFO estimation given a PSS received sequence
 *
 * Source: An Efﬁcient CFO Estimation Algorithm for the Downlink of 3GPP-LTE
 *       Feng Wang and Yu Zhu
 */
float srslte_pss_cfo_compute(srslte_pss_t* q, const cf_t *pss_recv) {
  cf_t y0, y1;

  const cf_t *pss_ptr = pss_recv;

  if (q->filter_pss_enable) {
    srslte_pss_filter(q, pss_recv, q->tmp_fft);
    pss_ptr = (const cf_t*) q->tmp_fft;
  }

  y0 = srslte_vec_dot_prod_ccc(q->pss_signal_time[q->N_id_2], pss_ptr, q->fft_size/2);
  y1 = srslte_vec_dot_prod_ccc(&q->pss_signal_time[q->N_id_2][q->fft_size/2], &pss_ptr[q->fft_size/2], q->fft_size/2);
  return carg(conjf(y0) * y1)/M_PI;
}

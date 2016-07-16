/* ----------------------------------------------------------------- */
/*           The HMM-Based Speech Synthesis Engine "hts_engine API"  */
/*           developed by HTS Working Group                          */
/*           http://hts-engine.sourceforge.net/                      */
/* ----------------------------------------------------------------- */
/*                                                                   */
/*  Copyright (c) 2001-2011  Nagoya Institute of Technology          */
/*                           Department of Computer Science          */
/*                                                                   */
/*                2001-2008  Tokyo Institute of Technology           */
/*                           Interdisciplinary Graduate School of    */
/*                           Science and Engineering                 */
/*                                                                   */
/* All rights reserved.                                              */
/*                                                                   */
/* Redistribution and use in source and binary forms, with or        */
/* without modification, are permitted provided that the following   */
/* conditions are met:                                               */
/*                                                                   */
/* - Redistributions of source code must retain the above copyright  */
/*   notice, this list of conditions and the following disclaimer.   */
/* - Redistributions in binary form must reproduce the above         */
/*   copyright notice, this list of conditions and the following     */
/*   disclaimer in the documentation and/or other materials provided */
/*   with the distribution.                                          */
/* - Neither the name of the HTS working group nor the names of its  */
/*   contributors may be used to endorse or promote products derived */
/*   from this software without specific prior written permission.   */
/*                                                                   */
/* THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND            */
/* CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,       */
/* INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF          */
/* MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE          */
/* DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS */
/* BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,          */
/* EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED   */
/* TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,     */
/* DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON */
/* ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,   */
/* OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY    */
/* OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE           */
/* POSSIBILITY OF SUCH DAMAGE.                                       */
/* ----------------------------------------------------------------- */

#ifndef HTS106_PSTREAM_C
#define HTS106_PSTREAM_C

#ifdef __cplusplus
#define HTS106_PSTREAM_C_START extern "C" {
#define HTS106_PSTREAM_C_END   }
#else
#define HTS106_PSTREAM_C_START
#define HTS106_PSTREAM_C_END
#endif                          /* __CPLUSPLUS */

HTS106_PSTREAM_C_START;

#include <math.h>               /* for sqrt() */

/* hts_engine libraries */
#include "HTS106_hidden.h"

/* HTS106_finv: calculate 1.0/variance function */
static double HTS106_finv(const double x)
{
   if (x >= INFTY2)
      return 0.0;
   if (x <= -INFTY2)
      return 0.0;
   if (x <= INVINF2 && x >= 0)
      return INFTY;
   if (x >= -INVINF2 && x < 0)
      return -INFTY;

   return (1.0 / x);
}

/* HTS106_PStream_calc_wuw_and_wum: calcurate W'U^{-1}W and W'U^{-1}M */
static void HTS106_PStream_calc_wuw_and_wum(HTS106_PStream * pst, const int m)
{
   int t, i, j, k;
   double wu;

   for (t = 0; t < pst->length; t++) {
      /* initialize */
      pst->sm.wum[t] = 0.0;
      for (i = 0; i < pst->width; i++)
         pst->sm.wuw[t][i] = 0.0;

      /* calc WUW & WUM */
      for (i = 0; i < pst->win_size; i++)
         for (j = pst->win_l_width[i]; j <= pst->win_r_width[i]; j++)
            if ((t + j >= 0) && (t + j < pst->length)
                && (pst->win_coefficient[i][-j] != 0.0)) {
               wu = pst->win_coefficient[i][-j] * pst->sm.ivar[t + j][i * pst->static_length + m];
               pst->sm.wum[t] += wu * pst->sm.mean[t + j][i * pst->static_length + m];
               for (k = 0; (k < pst->width) && (t + k < pst->length); k++)
                  if ((k - j <= pst->win_r_width[i])
                      && (pst->win_coefficient[i][k - j] != 0.0))
                     pst->sm.wuw[t][k] += wu * pst->win_coefficient[i][k - j];
            }
   }
}


/* HTS106_PStream_ldl_factorization: Factorize W'*U^{-1}*W to L*D*L' (L: lower triangular, D: diagonal) */
static void HTS106_PStream_ldl_factorization(HTS106_PStream * pst)
{
   int t, i, j;

   for (t = 0; t < pst->length; t++) {
      for (i = 1; (i < pst->width) && (t >= i); i++)
         pst->sm.wuw[t][0] -= pst->sm.wuw[t - i][i] * pst->sm.wuw[t - i][i] * pst->sm.wuw[t - i][0];

      for (i = 1; i < pst->width; i++) {
         for (j = 1; (i + j < pst->width) && (t >= j); j++)
            pst->sm.wuw[t][i] -= pst->sm.wuw[t - j][j] * pst->sm.wuw[t - j][i + j] * pst->sm.wuw[t - j][0];
         pst->sm.wuw[t][i] /= pst->sm.wuw[t][0];
      }
   }
}

/* HTS106_PStream_forward_substitution: forward subtitution for mlpg */
static void HTS106_PStream_forward_substitution(HTS106_PStream * pst)
{
   int t, i;

   for (t = 0; t < pst->length; t++) {
      pst->sm.g[t] = pst->sm.wum[t];
      for (i = 1; (i < pst->width) && (t >= i); i++)
         pst->sm.g[t] -= pst->sm.wuw[t - i][i] * pst->sm.g[t - i];
   }
}

/* HTS106_PStream_backward_substitution: backward subtitution for mlpg */
static void HTS106_PStream_backward_substitution(HTS106_PStream * pst, const int m)
{
   int t, i;

   for (t = pst->length - 1; t >= 0; t--) {
      pst->par[t][m] = pst->sm.g[t] / pst->sm.wuw[t][0];
      for (i = 1; (i < pst->width) && (t + i < pst->length); i++)
         pst->par[t][m] -= pst->sm.wuw[t][i] * pst->par[t + i][m];
   }
}

/* HTS106_PStream_calc_gv: subfunction for mlpg using GV */
static void HTS106_PStream_calc_gv(HTS106_PStream * pst, const int m, double *mean, double *vari)
{
   int t;

   *mean = 0.0;
   for (t = 0; t < pst->length; t++)
      if (pst->gv_switch[t])
         *mean += pst->par[t][m];
   *mean /= pst->gv_length;
   *vari = 0.0;
   for (t = 0; t < pst->length; t++)
      if (pst->gv_switch[t])
         *vari += (pst->par[t][m] - *mean) * (pst->par[t][m] - *mean);
   *vari /= pst->gv_length;
}

/* HTS106_PStream_conv_gv: subfunction for mlpg using GV */
static void HTS106_PStream_conv_gv(HTS106_PStream * pst, const int m)
{
   int t;
   double ratio;
   double mean;
   double vari;

   HTS106_PStream_calc_gv(pst, m, &mean, &vari);
   ratio = sqrt(pst->gv_mean[m] / vari);
   for (t = 0; t < pst->length; t++)
      if (pst->gv_switch[t])
         pst->par[t][m] = ratio * (pst->par[t][m] - mean) + mean;
}

/* HTS106_PStream_calc_derivative: subfunction for mlpg using GV */
static double HTS106_PStream_calc_derivative(HTS106_PStream * pst, const int m)
{
   int t, i;
   double mean;
   double vari;
   double dv;
   double h;
   double gvobj;
   double hmmobj;
   const double w = 1.0 / (pst->win_size * pst->length);

   HTS106_PStream_calc_gv(pst, m, &mean, &vari);
   gvobj = -0.5 * W2 * vari * pst->gv_vari[m] * (vari - 2.0 * pst->gv_mean[m]);
   dv = -2.0 * pst->gv_vari[m] * (vari - pst->gv_mean[m]) / pst->length;

   for (t = 0; t < pst->length; t++) {
      pst->sm.g[t] = pst->sm.wuw[t][0] * pst->par[t][m];
      for (i = 1; i < pst->width; i++) {
         if (t + i < pst->length)
            pst->sm.g[t] += pst->sm.wuw[t][i] * pst->par[t + i][m];
         if (t + 1 > i)
            pst->sm.g[t] += pst->sm.wuw[t - i][i] * pst->par[t - i][m];
      }
   }

   for (t = 0, hmmobj = 0.0; t < pst->length; t++) {
      hmmobj += W1 * w * pst->par[t][m] * (pst->sm.wum[t] - 0.5 * pst->sm.g[t]);
      h = -W1 * w * pst->sm.wuw[t][1 - 1] - W2 * 2.0 / (pst->length * pst->length) * ((pst->length - 1) * pst->gv_vari[m] * (vari - pst->gv_mean[m]) + 2.0 * pst->gv_vari[m] * (pst->par[t][m] - mean) * (pst->par[t][m] - mean));
      if (pst->gv_switch[t])
         pst->sm.g[t] = 1.0 / h * (W1 * w * (-pst->sm.g[t] + pst->sm.wum[t]) + W2 * dv * (pst->par[t][m] - mean));
      else
         pst->sm.g[t] = 1.0 / h * (W1 * w * (-pst->sm.g[t] + pst->sm.wum[t]));
   }

   return (-(hmmobj + gvobj));
}

/* HTS106_PStream_gv_parmgen: function for mlpg using GV */
static void HTS106_PStream_gv_parmgen(HTS106_PStream * pst, const int m)
{
   int t, i;
   double step = STEPINIT;
   double prev = -LZERO;
   double obj;

   if (pst->gv_length == 0)
      return;

   HTS106_PStream_conv_gv(pst, m);
   if (GV_MAX_ITERATION > 0) {
      HTS106_PStream_calc_wuw_and_wum(pst, m);
      for (i = 1; i <= GV_MAX_ITERATION; i++) {
         obj = HTS106_PStream_calc_derivative(pst, m);
         if (obj > prev)
            step *= STEPDEC;
         if (obj < prev)
            step *= STEPINC;
         for (t = 0; t < pst->length; t++)
            pst->par[t][m] += step * pst->sm.g[t];
         prev = obj;
      }
   }
}

/* HTS106_PStream_mlpg: generate sequence of speech parameter vector maximizing its output probability for given pdf sequence */
static void HTS106_PStream_mlpg(HTS106_PStream * pst)
{
   int m;

   if (pst->length == 0)
      return;

   for (m = 0; m < pst->static_length; m++) {
      HTS106_PStream_calc_wuw_and_wum(pst, m);
      HTS106_PStream_ldl_factorization(pst);       /* LDL factorization */
      HTS106_PStream_forward_substitution(pst);    /* forward substitution   */
      HTS106_PStream_backward_substitution(pst, m);        /* backward substitution  */
      if (pst->gv_length > 0)
         HTS106_PStream_gv_parmgen(pst, m);
   }
}

/* HTS106_PStreamSet_initialize: initialize parameter stream set */
void HTS106_PStreamSet_initialize(HTS106_PStreamSet * pss)
{
   pss->pstream = NULL;
   pss->nstream = 0;
   pss->total_frame = 0;
}

/* HTS106_PStreamSet_create: parameter generation using GV weight */
HTS106_Boolean HTS106_PStreamSet_create(HTS106_PStreamSet * pss, HTS106_SStreamSet * sss, double *msd_threshold, double *gv_weight)
{
   int i, j, k, l, m;
   int frame, msd_frame, state;

   HTS106_PStream *pst;
   HTS106_Boolean not_bound;

   if (pss->nstream) {
      HTS106_error(1, "HTS106_PstreamSet_create: HTS106_PStreamSet should be clear.\n");
      return FALSE;
   }

   /* initialize */
   pss->nstream = HTS106_SStreamSet_get_nstream(sss);
   pss->pstream = (HTS106_PStream *) HTS106_calloc(pss->nstream, sizeof(HTS106_PStream));
   pss->total_frame = HTS106_SStreamSet_get_total_frame(sss);

   /* create */
   for (i = 0; i < pss->nstream; i++) {
      pst = &pss->pstream[i];
      if (HTS106_SStreamSet_is_msd(sss, i)) {      /* for MSD */
         pst->length = 0;
         for (state = 0; state < HTS106_SStreamSet_get_total_state(sss); state++)
            if (HTS106_SStreamSet_get_msd(sss, i, state) > msd_threshold[i])
               pst->length += HTS106_SStreamSet_get_duration(sss, state);
         pst->msd_flag = (HTS106_Boolean *) HTS106_calloc(pss->total_frame, sizeof(HTS106_Boolean));
         for (state = 0, frame = 0; state < HTS106_SStreamSet_get_total_state(sss); state++)
            if (HTS106_SStreamSet_get_msd(sss, i, state) > msd_threshold[i])
               for (j = 0; j < HTS106_SStreamSet_get_duration(sss, state); j++) {
                  pst->msd_flag[frame] = TRUE;
                  frame++;
            } else
               for (j = 0; j < HTS106_SStreamSet_get_duration(sss, state); j++) {
                  pst->msd_flag[frame] = FALSE;
                  frame++;
               }
      } else {                  /* for non MSD */
         pst->length = pss->total_frame;
         pst->msd_flag = NULL;
      }
      pst->vector_length = HTS106_SStreamSet_get_vector_length(sss, i);
      pst->width = HTS106_SStreamSet_get_window_max_width(sss, i) * 2 + 1; /* band width of R */
      pst->win_size = HTS106_SStreamSet_get_window_size(sss, i);
      pst->static_length = pst->vector_length / pst->win_size;
      pst->sm.mean = HTS106_alloc_matrix(pst->length, pst->vector_length);
      pst->sm.ivar = HTS106_alloc_matrix(pst->length, pst->vector_length);
      pst->sm.wum = (double *) HTS106_calloc(pst->length, sizeof(double));
      pst->sm.wuw = HTS106_alloc_matrix(pst->length, pst->width);
      pst->sm.g = (double *) HTS106_calloc(pst->length, sizeof(double));
      pst->par = HTS106_alloc_matrix(pst->length, pst->static_length);
      /* copy dynamic window */
      pst->win_l_width = (int *) HTS106_calloc(pst->win_size, sizeof(int));
      pst->win_r_width = (int *) HTS106_calloc(pst->win_size, sizeof(int));
      pst->win_coefficient = (double **) HTS106_calloc(pst->win_size, sizeof(double));
      for (j = 0; j < pst->win_size; j++) {
         pst->win_l_width[j] = HTS106_SStreamSet_get_window_left_width(sss, i, j);
         pst->win_r_width[j] = HTS106_SStreamSet_get_window_right_width(sss, i, j);
         if (pst->win_l_width[j] + pst->win_r_width[j] == 0)
            pst->win_coefficient[j] = (double *)
                HTS106_calloc(-2 * pst->win_l_width[j] + 1, sizeof(double));
         else
            pst->win_coefficient[j] = (double *)
                HTS106_calloc(-2 * pst->win_l_width[j], sizeof(double));
         pst->win_coefficient[j] -= pst->win_l_width[j];
         for (k = pst->win_l_width[j]; k <= pst->win_r_width[j]; k++)
            pst->win_coefficient[j][k] = HTS106_SStreamSet_get_window_coefficient(sss, i, j, k);
      }
      /* copy GV */
      if (HTS106_SStreamSet_use_gv(sss, i)) {
         pst->gv_mean = (double *) HTS106_calloc(pst->static_length, sizeof(double));
         pst->gv_vari = (double *) HTS106_calloc(pst->static_length, sizeof(double));
         for (j = 0; j < pst->static_length; j++) {
            pst->gv_mean[j] = HTS106_SStreamSet_get_gv_mean(sss, i, j) * gv_weight[i];
            pst->gv_vari[j] = HTS106_SStreamSet_get_gv_vari(sss, i, j);
         }
         pst->gv_switch = (HTS106_Boolean *) HTS106_calloc(pst->length, sizeof(HTS106_Boolean));
         if (HTS106_SStreamSet_is_msd(sss, i)) {   /* for MSD */
            for (state = 0, frame = 0, msd_frame = 0; state < HTS106_SStreamSet_get_total_state(sss); state++)
               for (j = 0; j < HTS106_SStreamSet_get_duration(sss, state); j++, frame++)
                  if (pst->msd_flag[frame])
                     pst->gv_switch[msd_frame++] = HTS106_SStreamSet_get_gv_switch(sss, i, state);
         } else {               /* for non MSD */
            for (state = 0, frame = 0; state < HTS106_SStreamSet_get_total_state(sss); state++)
               for (j = 0; j < HTS106_SStreamSet_get_duration(sss, state); j++)
                  pst->gv_switch[frame++] = HTS106_SStreamSet_get_gv_switch(sss, i, state);
         }
         for (j = 0, pst->gv_length = 0; j < pst->length; j++)
            if (pst->gv_switch[j])
               pst->gv_length++;
      } else {
         pst->gv_switch = NULL;
         pst->gv_length = 0;
         pst->gv_mean = NULL;
         pst->gv_vari = NULL;
      }
      /* copy pdfs */
      if (HTS106_SStreamSet_is_msd(sss, i)) {      /* for MSD */
         for (state = 0, frame = 0, msd_frame = 0; state < HTS106_SStreamSet_get_total_state(sss); state++)
            for (j = 0; j < HTS106_SStreamSet_get_duration(sss, state); j++) {
               if (pst->msd_flag[frame]) {
                  /* check current frame is MSD boundary or not */
                  for (k = 0; k < pst->win_size; k++) {
                     not_bound = TRUE;
                     for (l = pst->win_l_width[k]; l <= pst->win_r_width[k]; l++)
                        if (frame + l < 0 || pss->total_frame <= frame + l || !pst->msd_flag[frame + l]) {
                           not_bound = FALSE;
                           break;
                        }
                     for (l = 0; l < pst->static_length; l++) {
                        m = pst->static_length * k + l;
                        pst->sm.mean[msd_frame][m] = HTS106_SStreamSet_get_mean(sss, i, state, m);
                        if (not_bound || k == 0)
                           pst->sm.ivar[msd_frame][m] = HTS106_finv(HTS106_SStreamSet_get_vari(sss, i, state, m));
                        else
                           pst->sm.ivar[msd_frame][m] = 0.0;
                     }
                  }
                  msd_frame++;
               }
               frame++;
            }
      } else {                  /* for non MSD */
         for (state = 0, frame = 0; state < HTS106_SStreamSet_get_total_state(sss); state++) {
            for (j = 0; j < HTS106_SStreamSet_get_duration(sss, state); j++) {
               for (k = 0; k < pst->win_size; k++) {
                  not_bound = TRUE;
                  for (l = pst->win_l_width[k]; l <= pst->win_r_width[k]; l++)
                     if (frame + l < 0 || pss->total_frame <= frame + l) {
                        not_bound = FALSE;
                        break;
                     }
                  for (l = 0; l < pst->static_length; l++) {
                     m = pst->static_length * k + l;
                     pst->sm.mean[frame][m] = HTS106_SStreamSet_get_mean(sss, i, state, m);
                     if (not_bound || k == 0)
                        pst->sm.ivar[frame][m] = HTS106_finv(HTS106_SStreamSet_get_vari(sss, i, state, m));
                     else
                        pst->sm.ivar[frame][m] = 0.0;
                  }
               }
               frame++;
            }
         }
      }
      /* parameter generation */
      HTS106_PStream_mlpg(pst);
   }

   return TRUE;
}

/* HTS106_PStreamSet_get_nstream: get number of stream */
int HTS106_PStreamSet_get_nstream(HTS106_PStreamSet * pss)
{
   return pss->nstream;
}

/* HTS106_PStreamSet_get_static_length: get static features length */
int HTS106_PStreamSet_get_static_length(HTS106_PStreamSet * pss, int stream_index)
{
   return pss->pstream[stream_index].static_length;
}

/* HTS106_PStreamSet_get_total_frame: get total number of frame */
int HTS106_PStreamSet_get_total_frame(HTS106_PStreamSet * pss)
{
   return pss->total_frame;
}

/* HTS106_PStreamSet_get_parameter: get parameter */
double HTS106_PStreamSet_get_parameter(HTS106_PStreamSet * pss, int stream_index, int frame_index, int vector_index)
{
   return pss->pstream[stream_index].par[frame_index][vector_index];
}

/* HTS106_PStreamSet_get_parameter_vector: get parameter vector*/
double *HTS106_PStreamSet_get_parameter_vector(HTS106_PStreamSet * pss, int stream_index, int frame_index)
{
   return pss->pstream[stream_index].par[frame_index];
}

/* HTS106_PStreamSet_get_msd_flag: get generated MSD flag per frame */
HTS106_Boolean HTS106_PStreamSet_get_msd_flag(HTS106_PStreamSet * pss, int stream_index, int frame_index)
{
   return pss->pstream[stream_index].msd_flag[frame_index];
}

/* HTS106_PStreamSet_is_msd: get MSD flag */
HTS106_Boolean HTS106_PStreamSet_is_msd(HTS106_PStreamSet * pss, int stream_index)
{
   return pss->pstream[stream_index].msd_flag ? TRUE : FALSE;
}

/* HTS106_PStreamSet_clear: free parameter stream set */
void HTS106_PStreamSet_clear(HTS106_PStreamSet * pss)
{
   int i, j;
   HTS106_PStream *pstream;

   if (pss->pstream) {
      for (i = 0; i < pss->nstream; i++) {
         pstream = &pss->pstream[i];
         HTS106_free(pstream->sm.wum);
         HTS106_free(pstream->sm.g);
         HTS106_free_matrix(pstream->sm.wuw, pstream->length);
         HTS106_free_matrix(pstream->sm.ivar, pstream->length);
         HTS106_free_matrix(pstream->sm.mean, pstream->length);
         HTS106_free_matrix(pstream->par, pstream->length);
         if (pstream->msd_flag)
            HTS106_free(pstream->msd_flag);
         for (j = pstream->win_size - 1; j >= 0; j--) {
            pstream->win_coefficient[j] += pstream->win_l_width[j];
            HTS106_free(pstream->win_coefficient[j]);
         }
         if (pstream->gv_mean)
            HTS106_free(pstream->gv_mean);
         if (pstream->gv_vari)
            HTS106_free(pstream->gv_vari);
         HTS106_free(pstream->win_coefficient);
         HTS106_free(pstream->win_l_width);
         HTS106_free(pstream->win_r_width);
         if (pstream->gv_switch)
            HTS106_free(pstream->gv_switch);
      }
      HTS106_free(pss->pstream);
   }
   HTS106_PStreamSet_initialize(pss);
}

HTS106_PSTREAM_C_END;

#endif                          /* !HTS106_PSTREAM_C */
/*
 * H.265 video codec.
 * Copyright (c) 2013-2014 struktur AG, Dirk Farin <farin@struktur.de>
 *
 * This file is part of libde265.
 *
 * libde265 is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation, either version 3 of
 * the License, or (at your option) any later version.
 *
 * libde265 is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with libde265.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef DE265_IMAGE_H
#define DE265_IMAGE_H

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <assert.h>
#include <stdint.h>
#include <string.h>
#ifdef HAVE_STDBOOL_H
#include <stdbool.h>
#endif
#include "libde265/de265.h"
#include "libde265/sps.h"
#include "libde265/pps.h"
#include "libde265/motion.h"
#include "libde265/threads.h"
#include "libde265/slice.h"
#include "libde265/nal.h"


enum PictureState {
  UnusedForReference,
  UsedForShortTermReference,
  UsedForLongTermReference
};


/* TODO:
   At INTEGRITY_DERIVED_FROM_FAULTY_REFERENCE images, we can check the SEI hash, whether
   the output image is correct despite the faulty reference, and set the state back to correct.
*/
#define INTEGRITY_CORRECT 0
#define INTEGRITY_UNAVAILABLE_REFERENCE 1
#define INTEGRITY_NOT_DECODED 2
#define INTEGRITY_DECODING_ERRORS 3
#define INTEGRITY_DERIVED_FROM_FAULTY_REFERENCE 4

#define SEI_HASH_UNCHECKED 0
#define SEI_HASH_CORRECT   1
#define SEI_HASH_INCORRECT 2

#define TU_FLAG_NONZERO_COEFF  (1<<7)
#define TU_FLAG_SPLIT_TRANSFORM_MASK  0x1F

#define DEBLOCK_FLAG_VERTI (1<<4)
#define DEBLOCK_FLAG_HORIZ (1<<5)
#define DEBLOCK_PB_EDGE_VERTI (1<<6)
#define DEBLOCK_PB_EDGE_HORIZ (1<<7)
#define DEBLOCK_BS_MASK     0x03


#define CTB_PROGRESS_NONE      0
#define CTB_PROGRESS_PREFILTER 1
#define CTB_PROGRESS_FILTERED  2

#define PIXEL2CB(x) (x >> img->Log2MinCbSizeY)
#define CB_IDX(x0,y0) (PIXEL2CB(x0) + PIXEL2CB(y0)*img->PicWidthInMinCbsY)
#define SET_CB_BLK(x,y,log2BlkWidth,  Field,value)                      \
  int cbX = PIXEL2CB(x);                                                \
  int cbY = PIXEL2CB(y);                                                \
  int width = 1 << (log2BlkWidth - img->Log2MinCbSizeY);                \
  for (int cby=cbY;cby<cbY+width;cby++)                                 \
    for (int cbx=cbX;cbx<cbX+width;cbx++)                               \
      {                                                                 \
        img->cb_info[ cbx + cby*img->PicWidthInMinCbsY ].Field = value; \
      }

typedef struct {
  uint16_t SliceAddrRS;
  uint16_t SliceHeaderIndex; // index into array to slice header for this CTB

  sao_info saoInfo;

  uint16_t thread_context_id; // which thread-context is used to decode this CTB
} CTB_info;


typedef struct {
  uint8_t log2CbSize : 3;   // [0;6] (1<<log2CbSize) = 64
  uint8_t PartMode : 3;     // (enum PartMode)  [0;7] set only in top-left of CB
                            // TODO: could be removed if prediction-block-boundaries would be
                            // set during decoding
  uint8_t ctDepth : 2;      // [0:3]? (0:64, 1:32, 2:16, 3:8)
  uint8_t PredMode : 2;     // (enum PredMode)  [0;2] must be saved for past images
  uint8_t pcm_flag : 1;     //
  uint8_t cu_transquant_bypass : 1;

  int8_t  QP_Y;

  // uint8_t pcm_flag;  // TODO
} CB_ref_info;


typedef struct {
  PredVectorInfo mvi; // TODO: this can be done in 16x16 grid
} PB_ref_info;


/*
typedef struct {
  //uint16_t cbf_cb;   // bitfield (1<<depth)
  //uint16_t cbf_cr;   // bitfield (1<<depth)
  //uint16_t cbf_luma; // bitfield (1<<depth)

  //uint8_t IntraPredMode;  // NOTE: can be thread-local // (enum IntraPredMode)
  //uint8_t IntraPredModeC; // NOTE: can be thread-local // (enum IntraPredMode)

  //uint8_t split_transform_flag;  // NOTE: can be local if deblocking flags set during decoding
  //uint8_t transform_skip_flag;   // NOTE: can be in local context    // read bit (1<<cIdx)
  //uint8_t flags;                 // NOTE: can be removed if deblocking flags set during decoding (nonzero coefficients)
} TU_log_info;
*/


typedef struct de265_image {
  uint8_t* y;   // pointer to pixel at (0,0), which is inside the optional image borders
  uint8_t* cb;
  uint8_t* cr;

  uint8_t* y_mem;  // usually, you don't use these, but the pointers above
  uint8_t* cb_mem;
  uint8_t* cr_mem;

  enum de265_chroma chroma_format;

  int width, height;  // size in luma pixels
  int chroma_width, chroma_height;
  int stride, chroma_stride;

  int border;


  // --- conformance cropping window ---

  uint8_t* y_confwin;
  uint8_t* cb_confwin;
  uint8_t* cr_confwin;

  int width_confwin, height_confwin;
  int chroma_width_confwin, chroma_height_confwin;


  // --- decoding info ---

  // If PicOutputFlag==false && PicState==UnusedForReference, image buffer is free.

  int  picture_order_cnt_lsb;
  int  PicOrderCntVal;
  bool PicOutputFlag;
  enum PictureState PicState;


  seq_parameter_set* sps;  // the SPS used for decoding this image
  pic_parameter_set* pps;  // the PPS used for decoding this image


  CTB_info* ctb_info; // in raster scan
  int ctb_info_size;
  int Log2CtbSizeY;
  int PicWidthInCtbsY;

  CB_ref_info* cb_info;
  int cb_info_size;
  int Log2MinCbSizeY;
  int PicWidthInMinCbsY;

  PB_ref_info* pb_info;
  int pb_info_size;
  int pb_info_stride;
  int Log2MinPUSize;
  int PicWidthInMinPUs;

  uint8_t* intraPredMode; // sps->PicWidthInMinPUs * sps->PicHeightInMinPUs
  int intraPredModeSize;

  uint8_t* tu_info;
  int tu_info_size;
  int Log2MinTrafoSize;
  int PicWidthInTbsY;

  uint8_t* deblk_info;
  int deblk_info_size;
  int deblk_width;
  int deblk_height;

  // TODO CHECK: should this move to slice header? Can this be different for each slice in the image?

  // --- meta information ---

  de265_PTS pts;
  void*     user_data;

  uint8_t integrity; /* Whether an error occured while the image was decoded.
                        When generated, this is initialized to INTEGRITY_CORRECT,
                        and changed on decoding errors.
                      */
  uint8_t sei_hash_check_result;

  nal_header nal_header;

  // --- multi core ---

  de265_progress_lock* ctb_progress; // ctb_info_size

  ALIGNED_8(de265_sync_int tasks_pending); // number of tasks pending to complete decoding
  de265_mutex mutex;
  de265_cond  finished_cond;

} de265_image;


void de265_init_image (de265_image* img); // (optional) init variables, do not alloc image
de265_error de265_alloc_image(de265_image* img, int w,int h, enum de265_chroma c,
                              const seq_parameter_set* sps);
void de265_free_image (de265_image* img);

void de265_fill_image(de265_image* img, int y,int u,int v);
void de265_copy_image(de265_image* dest, const de265_image* src);

LIBDE265_INLINE static void get_image_plane(const de265_image* img, int cIdx, uint8_t** image, int* stride)
{
  switch (cIdx) {
  case 0: *image = img->y;  if (stride) *stride = img->stride; break;
  case 1: *image = img->cb; if (stride) *stride = img->chroma_stride; break;
  case 2: *image = img->cr; if (stride) *stride = img->chroma_stride; break;
  default: *image = NULL; if (stride) *stride = 0; break;
  }
}
void set_conformance_window(de265_image* img, int left,int right,int top,int bottom);


void increase_pending_tasks(de265_image* img, int n);
void decrease_pending_tasks(de265_image* img, int n);
void wait_for_completion(de265_image* img);  // block until image is decoded by background threads


/* Clear all CTB/CB/PB decoding data of this image.
   All CTB's processing states are set to 'unprocessed'.
 */
void img_clear_decoding_data(de265_image*);



LIBDE265_INLINE static void set_pred_mode(de265_image* img, int x,int y, int log2BlkWidth, enum PredMode mode)
{
  SET_CB_BLK(x,y,log2BlkWidth, PredMode, mode);
}
LIBDE265_INLINE static enum PredMode get_pred_mode(const de265_image* img, int x,int y)
{
  int cbX = PIXEL2CB(x);
  int cbY = PIXEL2CB(y);

  return (enum PredMode)img->cb_info[ cbX + cbY*img->PicWidthInMinCbsY ].PredMode;
}

LIBDE265_INLINE static uint8_t get_cu_skip_flag(const de265_image* img, int x,int y)
{
  return get_pred_mode(img,x,y)==MODE_SKIP;
}

LIBDE265_INLINE static void set_pcm_flag(de265_image* img, int x,int y, int log2BlkWidth)
{
  SET_CB_BLK(x,y,log2BlkWidth, pcm_flag, 1);
}
LIBDE265_INLINE static int get_pcm_flag(const de265_image* img, int x,int y)
{
  int cbX = PIXEL2CB(x);
  int cbY = PIXEL2CB(y);

  return img->cb_info[ cbX + cbY*img->PicWidthInMinCbsY ].pcm_flag;
}

LIBDE265_INLINE static void set_cu_transquant_bypass(const de265_image* img, int x,int y, int log2BlkWidth)
{
  SET_CB_BLK(x,y,log2BlkWidth, cu_transquant_bypass, 1);
}
LIBDE265_INLINE static int  get_cu_transquant_bypass(const de265_image* img, int x,int y)
{
  int cbX = PIXEL2CB(x);
  int cbY = PIXEL2CB(y);

  return img->cb_info[ cbX + cbY*img->PicWidthInMinCbsY ].cu_transquant_bypass;
}

LIBDE265_INLINE static void set_log2CbSize(de265_image* img, int x0, int y0, int log2CbSize)
{
  int cbX = PIXEL2CB(x0);
  int cbY = PIXEL2CB(y0);

  img->cb_info[ cbX + cbY*img->PicWidthInMinCbsY ].log2CbSize = log2CbSize;

  // assume that remaining cb_info blocks are initialized to zero
}
LIBDE265_INLINE static int  get_log2CbSize(const de265_image* img, int x0, int y0)
{
  int cbX = PIXEL2CB(x0);
  int cbY = PIXEL2CB(y0);

  return (enum PredMode)img->cb_info[ cbX + cbY*img->PicWidthInMinCbsY ].log2CbSize;
}
// coordinates in CB units
LIBDE265_INLINE static int  get_log2CbSize_cbUnits(const de265_image* img, int xCb, int yCb)
{
  return (enum PredMode)img->cb_info[ xCb + yCb*img->PicWidthInMinCbsY ].log2CbSize;
}

LIBDE265_INLINE static void          set_PartMode(      de265_image* img, int x,int y, enum PartMode mode)
{
  img->cb_info[ CB_IDX(x,y) ].PartMode = mode;
}
LIBDE265_INLINE static enum PartMode get_PartMode(const de265_image* img, int x,int y)
{
  return (enum PartMode)img->cb_info[ CB_IDX(x,y) ].PartMode;
}


LIBDE265_INLINE static void set_ctDepth(de265_image* img, int x,int y, int log2BlkWidth, int depth)
{
  SET_CB_BLK(x,y,log2BlkWidth, ctDepth, depth);
}
LIBDE265_INLINE static int get_ctDepth(const de265_image* img, int x,int y)
{
  return img->cb_info[ CB_IDX(x,y) ].ctDepth;
}

LIBDE265_INLINE static void set_QPY(de265_image* img, int x,int y, int log2BlkWidth, int QP_Y)
{
  assert(x>=0 && x<img->sps->pic_width_in_luma_samples);
  assert(y>=0 && y<img->sps->pic_height_in_luma_samples);

  SET_CB_BLK (x, y, log2BlkWidth, QP_Y, QP_Y);
}
LIBDE265_INLINE static int  get_QPY(const de265_image* img, int x0,int y0)
{
  return img->cb_info[CB_IDX(x0,y0)].QP_Y;
}

#define PIXEL2TU(x) (x >> img->Log2MinTrafoSize)
#define TU_IDX(x0,y0) (PIXEL2TU(x0) + PIXEL2TU(y0)*img->PicWidthInTbsY)

#define OR_TU_BLK(x,y,log2BlkWidth,  value)                             \
  int tuX = PIXEL2TU(x);                                                \
  int tuY = PIXEL2TU(y);                                                \
  int width = 1 << (log2BlkWidth - img->Log2MinTrafoSize);              \
  for (int tuy=tuY;tuy<tuY+width;tuy++)                                 \
    for (int tux=tuX;tux<tuX+width;tux++)                               \
      {                                                                 \
        img->tu_info[ tux + tuy*img->PicWidthInTbsY ] |= value;         \
      }

LIBDE265_INLINE static void set_split_transform_flag(de265_image* img, int x0,int y0,int trafoDepth)
{
  img->tu_info[TU_IDX(x0,y0)] |= (1<<trafoDepth);
}
LIBDE265_INLINE static int  get_split_transform_flag(const de265_image* img, int x0,int y0,int trafoDepth)
{
  int idx = TU_IDX(x0,y0);
  return (img->tu_info[idx] & (1<<trafoDepth));
}

LIBDE265_INLINE static void set_nonzero_coefficient(de265_image* img, int x,int y, int log2TrafoSize)
{
  OR_TU_BLK(x,y,log2TrafoSize, TU_FLAG_NONZERO_COEFF);
}

LIBDE265_INLINE static int  get_nonzero_coefficient(const de265_image* img, int x,int y)
{
  return img->tu_info[TU_IDX(x,y)] & TU_FLAG_NONZERO_COEFF;
}

LIBDE265_INLINE static enum IntraPredMode get_IntraPredMode(const de265_image* img, int x,int y)
{
  int PUidx = (x>>img->Log2MinPUSize) + (y>>img->Log2MinPUSize) * img->PicWidthInMinPUs;

  return (enum IntraPredMode) img->intraPredMode[PUidx];
}


LIBDE265_INLINE static void    set_deblk_flags(de265_image* img, int x0,int y0, uint8_t flags)
{
  const int xd = x0/4;
  const int yd = y0/4;

  if (xd<img->deblk_width && yd<img->deblk_height) {
    img->deblk_info[xd + yd*img->deblk_width] |= flags;
  }
}
LIBDE265_INLINE static uint8_t get_deblk_flags(const de265_image* img, int x0,int y0)
{
  const int xd = x0/4;
  const int yd = y0/4;
  assert (xd<img->deblk_width && yd<img->deblk_height);

  return img->deblk_info[xd + yd*img->deblk_width];
}

LIBDE265_INLINE static void    set_deblk_bS(de265_image* img, int x0,int y0, uint8_t bS)
{
  uint8_t* data = &img->deblk_info[x0/4 + y0/4*img->deblk_width];
  *data &= ~DEBLOCK_BS_MASK;
  *data |= bS;
}
LIBDE265_INLINE static uint8_t get_deblk_bS(const de265_image* img, int x0,int y0)
{
  return img->deblk_info[x0/4 + y0/4*img->deblk_width] & DEBLOCK_BS_MASK;
}


// address of first CTB in slice
LIBDE265_INLINE static void set_SliceAddrRS(de265_image* img, int ctbX, int ctbY, int SliceAddrRS)
{
  assert(ctbX + ctbY*img->PicWidthInCtbsY < img->ctb_info_size);
  img->ctb_info[ctbX + ctbY*img->PicWidthInCtbsY].SliceAddrRS = SliceAddrRS;
}
LIBDE265_INLINE static int  get_SliceAddrRS(const de265_image* img, int ctbX, int ctbY)
{
  return img->ctb_info[ctbX + ctbY*img->PicWidthInCtbsY].SliceAddrRS;
}
LIBDE265_INLINE static int  get_SliceAddrRS_atCtbRS(const de265_image* img, int ctbRS)
{
  return img->ctb_info[ctbRS].SliceAddrRS;
}


LIBDE265_INLINE static void set_SliceHeaderIndex(de265_image* img, int x, int y, int SliceHeaderIndex)
{
  int ctbX = x >> img->Log2CtbSizeY;
  int ctbY = y >> img->Log2CtbSizeY;
  img->ctb_info[ctbX + ctbY*img->PicWidthInCtbsY].SliceHeaderIndex = SliceHeaderIndex;
}
LIBDE265_INLINE static int  get_SliceHeaderIndex(const de265_image* img, int x, int y)
{
  int ctbX = x >> img->Log2CtbSizeY;
  int ctbY = y >> img->Log2CtbSizeY;
  return img->ctb_info[ctbX + ctbY*img->PicWidthInCtbsY].SliceHeaderIndex;
}

LIBDE265_INLINE static void set_sao_info(de265_image* img, int ctbX,int ctbY,const sao_info* saoinfo)
{
  assert(ctbX + ctbY*img->PicWidthInCtbsY < img->ctb_info_size);
  memcpy(&img->ctb_info[ctbX + ctbY*img->PicWidthInCtbsY].saoInfo,
         saoinfo,
         sizeof(sao_info));
}
LIBDE265_INLINE static const sao_info* get_sao_info(const de265_image* img, int ctbX,int ctbY)
{
  assert(ctbX + ctbY*img->PicWidthInCtbsY < img->ctb_info_size);
  return &img->ctb_info[ctbX + ctbY*img->PicWidthInCtbsY].saoInfo;
}


// --- value logging ---

#endif

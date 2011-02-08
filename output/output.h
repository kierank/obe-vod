/*****************************************************************************
 * output.h: x264 file output modules
 *****************************************************************************
 * Copyright (C) 2003-2011 x264 project
 *
 * Authors: Laurent Aimar <fenrir@via.ecp.fr>
 *          Loren Merritt <lorenm@u.washington.edu>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02111, USA.
 *
 * This program is also available under a commercial proprietary license.
 * For more information, contact us at licensing@x264.com.
 *****************************************************************************/

#ifndef X264_OUTPUT_H
#define X264_OUTPUT_H

#include "x264cli.h"

#define MAX_EXTRA 20

typedef struct
{
    char *filename;
    int bitrate;
    int pid;
    char lang[4];

    int frame_size;

    FILE *fp;
    int increment;
    int64_t next_audio_pts;
    int num_audio_frames;

    int aac_written_first;
    uint8_t aac_buffer[7];
    int aac_sample_rate;
    int aac_channel_config;
} ts_extra_opt_t;

typedef struct
{
    int use_dts_compress;

    /* ts options */
    int i_ts_muxrate;
    int i_ts_type;
    int b_ts_cbr;
    int i_ts_video_pid;
    int i_ts_pmt_pid;
    int i_ts_pcr_pid;

    int num_extra_streams;
    ts_extra_opt_t extra_streams[MAX_EXTRA];

    int b_ts_dvb_au;
} cli_output_opt_t;

typedef struct
{
    int (*open_file)( char *psz_filename, hnd_t *p_handle, cli_output_opt_t *opt );
    int (*set_param)( hnd_t handle, x264_param_t *p_param );
    int (*write_headers)( hnd_t handle, x264_nal_t *p_nal );
    int (*write_frame)( hnd_t handle, uint8_t *p_nal, int i_size, x264_picture_t *p_picture, int i_ref_idc );
    int (*close_file)( hnd_t handle, int64_t largest_pts, int64_t second_largest_pts );
} cli_output_t;

extern const cli_output_t raw_output;
extern const cli_output_t mkv_output;
extern const cli_output_t mp4_output;
extern const cli_output_t flv_output;
#ifdef HAVE_LIBMPEGTS
extern const cli_output_t ts_output;
#endif

#endif

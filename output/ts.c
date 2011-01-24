/*****************************************************************************
 * ts.c :
 *****************************************************************************
 * Copyright (C) 2010 Kieran Kunhya
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
 *****************************************************************************/

#include "output.h"
#include <libmpegts.h>

#define MIN_PID 0x30
#define MAX_PID 0x1fff

#define AC3_NUM_SAMPLES 1536
#define MP2_NUM_SAMPLES 1152
#define AAC_NUM_SAMPLES 1024

typedef struct ts_hnd_t
{
    ts_writer_t *w;

    ts_stream_t *streams;
    cli_output_opt_t opt;

    FILE *fp;

    int write_pic_struct;
    int open_gop;
    int first;
} ts_hnd_t;

static int open_file( char *psz_filename, hnd_t *p_handle, cli_output_opt_t *opt )
{
    ts_hnd_t *p_ts = calloc( 1, sizeof(*p_ts) );
    *p_handle = NULL;
    if( !p_ts )
        return -1;

    p_ts->w = ts_create_writer();
    if( !p_ts->w )
    {
        free( p_ts );
        return -1;
    }

    memcpy( &p_ts->opt, opt, sizeof(cli_output_opt_t) );
    *p_handle = p_ts;

    p_ts->fp = fopen( psz_filename, "wb+" );

    if( !p_ts->fp )
    {
        fprintf( stderr, "[ts] Cannot Open Transport Stream file \n" );
        free( p_ts );
        return -1;
    }

    return 0;
}

static int set_param( hnd_t handle, x264_param_t *p_param )
{
    ts_hnd_t *p_ts = handle;

    if( !p_ts->opt.i_ts_muxrate )
    {
        fprintf( stderr, "[ts] TS muxing requires a muxing rate\n" );
        return -1;
    }

    if( p_ts->opt.i_ts_pmt_pid && ( p_ts->opt.i_ts_pmt_pid <= MIN_PID || p_ts->opt.i_ts_pmt_pid >= MAX_PID ) )
    {
        fprintf( stderr, "[ts] PMT PID is invalid\n" );
        p_ts->opt.i_ts_pmt_pid = 0;
    }

    if( p_ts->opt.i_ts_video_pid && ( p_ts->opt.i_ts_video_pid <= MIN_PID || p_ts->opt.i_ts_video_pid >= MAX_PID ) )
    {
        fprintf( stderr, "[ts] Video PID is invalid\n" );
        p_ts->opt.i_ts_video_pid = 0;
    }

    if( p_ts->opt.i_ts_pcr_pid && ( p_ts->opt.i_ts_pcr_pid <= MIN_PID || p_ts->opt.i_ts_pcr_pid >= MAX_PID ) )
    {
        fprintf( stderr, "[ts] PCR PID is invalid\n" );
        p_ts->opt.i_ts_pcr_pid = 0;
    }

    ts_main_t params;
    memset( &params, 0, sizeof(ts_main_t) );

    // TODO Check for collisions

    int cur_pid = MIN_PID+1;
    int program_num = 1; // 0 is NIT

    int ts_id, num_programs;
    ts_id = num_programs = 1;

    // TODO Override video PID and change if invalid per TS Spec

    ts_program_t *program = calloc( 1, sizeof(ts_program_t) );
    if( !program )
        return -1;

    ts_stream_t *streams = calloc( 1 + p_ts->opt.num_extra_streams, sizeof(ts_stream_t) );
    if( !streams )
        return -1;

    // One program with one stream

    streams[0].pid = p_ts->opt.i_ts_video_pid ? p_ts->opt.i_ts_video_pid : cur_pid++;
    streams[0].stream_format = LIBMPEGTS_VIDEO_AVC;
    streams[0].stream_id = LIBMPEGTS_STREAM_ID_MPEGVIDEO;
    streams[0].dvb_au = p_ts->opt.b_ts_dvb_au && !p_param->b_vfr_input;

    if( streams[0].dvb_au )
    {
        if( p_param->i_timebase_num == 1001 && p_param->i_timebase_den == 24000 )
            streams[0].dvb_au_frame_rate = LIBMPEGTS_DVB_AU_23_976_FPS;
	else if( p_param->i_timebase_num == 1 && p_param->i_timebase_den == 24 )
            streams[0].dvb_au_frame_rate = LIBMPEGTS_DVB_AU_24_FPS;
	else if( p_param->i_timebase_num == 1 && p_param->i_timebase_den == 25 )
            streams[0].dvb_au_frame_rate = LIBMPEGTS_DVB_AU_25_FPS;
	else if( p_param->i_timebase_num == 1001 && p_param->i_timebase_den == 30000 )
            streams[0].dvb_au_frame_rate = LIBMPEGTS_DVB_AU_29_97_FPS;
	else if( p_param->i_timebase_num == 1 && p_param->i_timebase_den == 30 )
            streams[0].dvb_au_frame_rate = LIBMPEGTS_DVB_AU_30_FPS;
	else if( p_param->i_timebase_num == 1 && p_param->i_timebase_den == 50 )
            streams[0].dvb_au_frame_rate = LIBMPEGTS_DVB_AU_50_FPS;
	else if( p_param->i_timebase_num == 1001 && p_param->i_timebase_den == 60000 )
            streams[0].dvb_au_frame_rate = LIBMPEGTS_DVB_AU_59_94_FPS;
	else if( p_param->i_timebase_num == 1 && p_param->i_timebase_den == 60 )
            streams[0].dvb_au_frame_rate = LIBMPEGTS_DVB_AU_60_FPS;
        else
        {
            fprintf( stderr, "DVB AU_Information does not support chosen framerate\n" );
            streams[0].dvb_au = 0;
        }
    }

    p_ts->write_pic_struct = p_param->b_pic_struct;
    p_ts->open_gop = !!p_param->i_open_gop;
    p_ts->streams = streams;

    program->streams = streams;
    program->num_streams = 1 + p_ts->opt.num_extra_streams;
    program->program_num = program_num;
    program->pmt_pid = p_ts->opt.i_ts_pmt_pid ? p_ts->opt.i_ts_pmt_pid : cur_pid++;
    program->pcr_pid = p_ts->opt.i_ts_pcr_pid ? p_ts->opt.i_ts_pcr_pid : streams[0].pid;

    // from sps.c
    int b_qpprime_y_zero_transform_bypass, i_profile_idc;

    b_qpprime_y_zero_transform_bypass = p_param->rc.i_rc_method == X264_RC_CQP && p_param->rc.i_qp_constant == 0;
    if( b_qpprime_y_zero_transform_bypass )
        i_profile_idc = AVC_HIGH_444_PRED;
    else if( BIT_DEPTH > 8 )
        i_profile_idc = AVC_HIGH_10;
    else if( p_param->analyse.b_transform_8x8 || p_param->i_cqm_preset != X264_CQM_FLAT )
        i_profile_idc = AVC_HIGH;
    else if( p_param->b_cabac || p_param->i_bframe > 0 || p_param->b_interlaced || p_param->analyse.i_weighted_pred > 0 )
        i_profile_idc = AVC_MAIN;
    else
        i_profile_idc = AVC_BASELINE;

    #define BR_SHIFT  6
    #define CPB_SHIFT 4

    int bitrate = 1000*p_param->rc.i_vbv_max_bitrate;
    int bufsize = 1000*p_param->rc.i_vbv_buffer_size;

    // normalize HRD size and rate to the value / scale notation
    int hrd_bit_rate_scale = x264_clip3( x264_ctz( bitrate ) - BR_SHIFT, 0, 15 );
    int hrd_bit_rate_value = bitrate >> ( hrd_bit_rate_scale + BR_SHIFT );
    int hrd_bit_rate = hrd_bit_rate_value << ( hrd_bit_rate_scale + BR_SHIFT );
    int cpb_size_scale = x264_clip3( x264_ctz( bufsize ) - CPB_SHIFT, 0, 15 );
    int cpb_size_value = bufsize >> ( cpb_size_scale + CPB_SHIFT );
    int cpb_size = cpb_size_value << ( cpb_size_scale + CPB_SHIFT );

    #undef CPB_SHIFT
    #undef BR_SHIFT

    streams[0].max_frame_size = (int)((((double)cpb_size / hrd_bit_rate)*90000)+0.5);

    for( int i = 0; i < p_ts->opt.num_extra_streams; i++ )
    {
        p_ts->opt.extra_streams[i].fp = fopen( p_ts->opt.extra_streams[i].filename, "rb" );
        if( !p_ts->opt.extra_streams[i].fp )
        {
            fprintf( stderr, "[ts] Cannot Open file %s\n", p_ts->opt.extra_streams[i].filename );
            return -1;
        }

        if( strlen( p_ts->opt.extra_streams[i].lang ) )
        {
            streams[1+i].write_lang_code = 1;
            strcpy( streams[1+i].lang_code, p_ts->opt.extra_streams[i].lang );
        }

        streams[1+i].pid = p_ts->opt.extra_streams[i].pid ? p_ts->opt.extra_streams[i].pid : cur_pid++;

        // FIXME this assumes 48000Hz

        int file_len = strlen( p_ts->opt.extra_streams[i].filename );

        if( !strcasecmp( p_ts->opt.extra_streams[i].filename + file_len - 3, "ac3" ) )
        {
            streams[1+i].stream_format = LIBMPEGTS_AUDIO_AC3;
            streams[1+i].stream_id = LIBMPEGTS_STREAM_ID_PRIVATE_1;
            streams[1+i].max_frame_size = (int)(((double)AC3_NUM_SAMPLES * p_ts->opt.extra_streams[i].bitrate / 384000) + 0.5);
            p_ts->opt.extra_streams[i].increment = (int)(((double)AC3_NUM_SAMPLES * 90000LL / 48000) + 0.5);
        }
        else if( !strcasecmp( p_ts->opt.extra_streams[i].filename + file_len - 3, "mp2" ) )
        {
            streams[1+i].stream_format = LIBMPEGTS_AUDIO_MPEG2;
            streams[1+i].stream_id = LIBMPEGTS_STREAM_ID_MPEGAUDIO;
            streams[1+i].max_frame_size = (int)(((double)MP2_NUM_SAMPLES * p_ts->opt.extra_streams[i].bitrate / 384000) + 0.5);
            p_ts->opt.extra_streams[i].increment = (int)(((double)MP2_NUM_SAMPLES * 90000LL / 48000) + 0.5);
        }
        // TODO aac adts
#if 0
        else if( !strcasecmp( p_ts->opt.extra_streams[i].filename + file_len - 4, "latm" ) )
        {
            streams[1+i].stream_format = LIBMPEGTS_AUDIO_LATM;
            streams[1+i].stream_id = LIBMPEGTS_STREAM_ID_MPEGAUDIO;
            streams[1+i].max_frame_size = (int)(((double)AAC_NUM_SAMPLES * p_ts->opt.extra_streams[i].bitrate / 384000) + 0.5);
            p_ts->opt.extra_streams[i].increment = (int)(((double)AAC_NUM_SAMPLES * 90000LL / 48000) + 0.5);
        }
#endif
    }

    params.programs = program;
    params.num_programs = 1;
    params.ts_id = ts_id;
    params.muxrate =  p_ts->opt.i_ts_muxrate;
    params.cbr = p_ts->opt.b_ts_cbr;
    params.ts_type = p_ts->opt.i_ts_type;

    program[0].cablelabs_is_3d = params.ts_type == TS_TYPE_CABLELABS && 
                                 ( p_param->i_frame_packing == 3 || p_param->i_frame_packing == 4 );
    
    if( ts_setup_transport_stream( p_ts->w, &params ) < 0 )
        return -1;

    if( ts_setup_mpegvideo_stream( p_ts->w, streams[0].pid, p_param->i_level_idc, i_profile_idc, hrd_bit_rate, cpb_size, 0 ) < 0 )
        return -1;

    free( program );

    return 0;
}

static int write_frame( hnd_t handle, uint8_t *p_nalu, int i_size, x264_picture_t *p_picture, int i_ref_idc )
{
    ts_hnd_t *p_ts = handle;
    int64_t video_pts = (int64_t)((p_picture->hrd_timing.dpb_output_time * 90000LL) + 0.5);

    uint8_t *output = NULL;
    int len = 0;

    int total_audio_frames = 0;
    for( int i = 0; i < p_ts->opt.num_extra_streams; i++ )
    {
        int64_t audio_pts;
        p_ts->opt.extra_streams[i].num_audio_frames = 0;

        if( !p_ts->first )
        {
            audio_pts = p_ts->opt.extra_streams[i].next_audio_pts = video_pts;
            p_ts->opt.extra_streams[i].num_audio_frames++;
            audio_pts += p_ts->opt.extra_streams[i].increment;
        }
        else
        {
            audio_pts = p_ts->opt.extra_streams[i].next_audio_pts;
            while( audio_pts < video_pts )
            {
                p_ts->opt.extra_streams[i].num_audio_frames++;
                audio_pts += p_ts->opt.extra_streams[i].increment;
            }
        }
        total_audio_frames += p_ts->opt.extra_streams[i].num_audio_frames;
    }

    p_ts->first = 1;

    int frame_idx = 1;
    ts_frame_t *frame = calloc( 1, (total_audio_frames + 1) * sizeof(ts_frame_t) );
    if( !frame )
    {
        fprintf( stderr, "Malloc Failed" );
        return -1;
    }

    for( int i = 0; i < p_ts->opt.num_extra_streams; i++ )
    {
        for( int j = 0; j < p_ts->opt.extra_streams[i].num_audio_frames; j++ )
        {
            ts_stream_t *cur_stream = &p_ts->streams[1+i];

            if( cur_stream->stream_format == LIBMPEGTS_AUDIO_LATM )
            {
                /* Read the length bytes into a temporary buffer */
                uint8_t length_bytes[2];
                fread( length_bytes, 1, 2, p_ts->opt.extra_streams[i].fp );
                int length = ((length_bytes[0] & 0x1f) << 5) | length_bytes[1];
                frame[frame_idx].data = malloc( length + 2 );
                memcpy( frame[frame_idx].data, length_bytes, 2 );
                fread( frame[frame_idx].data+2, 1, length, p_ts->opt.extra_streams[i].fp );
            }
            else
            {
                frame[frame_idx].data = malloc( cur_stream->max_frame_size );
                fread( frame[frame_idx].data, 1, cur_stream->max_frame_size, p_ts->opt.extra_streams[i].fp );
            }

            frame[frame_idx].size = cur_stream->max_frame_size;
            frame[frame_idx].pid = cur_stream->pid;
            frame[frame_idx].dts = p_ts->opt.extra_streams[i].next_audio_pts;
            frame[frame_idx].pts = p_ts->opt.extra_streams[i].next_audio_pts;
            p_ts->opt.extra_streams[i].next_audio_pts += p_ts->opt.extra_streams[i].increment;
            frame_idx++;
        }
    }

    frame[0].data = p_nalu;
    frame[0].size = i_size;
    frame[0].pid = p_ts->streams[0].pid;
    frame[0].dts = (int64_t)((p_picture->hrd_timing.cpb_removal_time * 90000LL) + 0.5);
    frame[0].pts = video_pts;
    frame[0].random_access = p_picture->b_keyframe;
    frame[0].priority = IS_X264_TYPE_I( p_picture->i_type );

    if( p_ts->streams[0].dvb_au )
    {
        frame[0].ref_pic_idc = i_ref_idc;

        if( p_picture->i_type == X264_TYPE_IDR || ( p_picture->i_type == X264_TYPE_KEYFRAME && !p_ts->open_gop ) )
            frame[0].frame_type = LIBMPEGTS_CODING_TYPE_SLICE_IDR;
	else if( p_picture->i_type == X264_TYPE_I || ( p_picture->i_type == X264_TYPE_KEYFRAME && p_ts->open_gop ) )
            frame[0].frame_type = LIBMPEGTS_CODING_TYPE_SLICE_I;
        else if( p_picture->i_type == X264_TYPE_P )
            frame[0].frame_type = LIBMPEGTS_CODING_TYPE_SLICE_P;
        else
            frame[0].frame_type = LIBMPEGTS_CODING_TYPE_SLICE_B;

        frame[0].write_pulldown_info = p_ts->write_pic_struct;
        if( frame[0].write_pulldown_info )
            frame[0].pic_struct = p_picture->i_pic_struct;
    }

    ts_write_frames( p_ts->w, frame, 1 + total_audio_frames, &output, &len );

    if( len )
        fwrite( output, 1, len, p_ts->fp );

    for( int i = 0; i < total_audio_frames; i++ )
        free( frame[1+i].data );

    free( frame );

    return i_size;
}

static int close_file( hnd_t handle, int64_t largest_pts, int64_t second_largest_pts )
{
    ts_hnd_t *p_ts = handle;

    if( p_ts->first )
    {
        uint8_t *output = NULL;
        int len = 0;

        ts_write_frames( p_ts->w, NULL, 0, &output, &len );

        if( len )
            fwrite( output, 1, len, p_ts->fp );
    }

    if( ts_close_writer( p_ts->w ) < 0 )
        return -1;

    fclose( p_ts->fp );

    if( p_ts->streams )
        free( p_ts->streams );
    free( p_ts );

    return 0;
}

const cli_output_t ts_output = { open_file, set_param, NULL, write_frame, close_file };

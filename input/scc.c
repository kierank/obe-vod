/*****************************************************************************
 * scc.c: scc captions input functions
 *****************************************************************************
 * Copyright (C) 2010-2011 Open Broadcast Encoder
 *
 * Authors: Kieran Kunhya <kieran@ob-encoder.com>
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

#include "input.h"

#define SCC_HEADER "Scenarist_SCC V1.0"

static void write_bytes( bs_t *s, uint8_t *bytes, int length )
{
    bs_flush( s );
    uint8_t *p_start = s->p_start;

    memcpy( s->p, bytes, length );
    s->p += length;

    bs_init( s, s->p, s->p_end - s->p );
    s->p_start = p_start;
}

static void write_invalid( bs_t *s )
{
    bs_write1( s, 0 );     // cc_valid
    bs_write( s, 2, 2 );   // cc_type
    bs_write( s, 8, 0 );   // c_data_1
    bs_write( s, 8, 0 );   // c_data_2
}

static void write_null( bs_t *s )
{
    bs_write1( s, 1 );     // cc_valid
    bs_write( s, 2, 1 );   // cc_type
    bs_write( s, 8, 128 ); // c_data_1
    bs_write( s, 8, 128 ); // c_data_2
}

static void write_payload( bs_t *s, cli_opt_t *opt )
{
    char cc_data1[2];
    char cc_data2[2];
    char spacer[1];
    char tc_separator[1];
    int ret;
    uint8_t out1, out2;

    bs_write1( s, 1 );     // cc_valid
    bs_write( s, 2, 0 );   // cc_type

    if( !memcmp( &opt->timecode, &opt->scc_opts[0].timecode, sizeof(cli_timecode_t) ) )
    {
        char *end;
        out1 = out2 = 128;
        ret = fscanf( opt->scc_opts[0].scc_file, "%2s", cc_data1 );
        ret = fscanf( opt->scc_opts[0].scc_file, "%2s", cc_data2 );

        if( ret > 0 )
        {
            cc_data1[2] = cc_data2[2] = 0;
            out1 = strtol( cc_data1, &end, 16 );
            if( end == cc_data1 || *end != '\0' )
                out1 = 128;
            out2 = strtol( cc_data2, &end, 16 );
            if( end == cc_data2 || *end != '\0' )
                out2 = 128;
        }

        bs_write( s, 8, out1 );  // c_data_1
        bs_write( s, 8, out2 );  // c_data_2

        ret = fread( spacer, 1, 1, opt->scc_opts[0].scc_file );

        if( ret < 1 )
            opt->scc_opts[0].timecode.frame = -1; /* will never match */
        else if( spacer[0] == ' ' )
             increase_tc( opt, &opt->scc_opts[0].timecode );
        else if( spacer[0] == '\r' || spacer[0] == '\n' )
        {
            ret = fscanf( opt->scc_opts[0].scc_file, "%2u:%2u:%2u%[;:]%2u\t", &opt->scc_opts[0].timecode.hour,
                  &opt->scc_opts[0].timecode.min, &opt->scc_opts[0].timecode.sec, tc_separator, &opt->scc_opts[0].timecode.frame );
        }
    }
    else
    {
        bs_write( s, 8, 128 ); // c_data_1
        bs_write( s, 8, 128 ); // c_data_2
    }
}

int open_scc( cli_opt_t *opt, scc_opt_t *scc )
{
    char temp[19];
    char tc_separator[1];
    int ret;

    ret = fread( temp, 18, 1, scc->scc_file );
    if( ret < 1 )
        goto fail;

    temp[18] = 0;

    if( strcmp( temp, SCC_HEADER ) )
    {
        x264_cli_log( "caption", X264_LOG_ERROR, "could not find scc header\n" );
        return -1;
    }

    ret = fscanf( scc->scc_file, "%2u:%2u:%2u%[;:]%2u\t", &scc->timecode.hour, &scc->timecode.min, &scc->timecode.sec, tc_separator, &scc->timecode.frame );
    if( ret < 0 )
    {
        x264_cli_log( "caption", X264_LOG_ERROR, "could not find timecode in scc file\n" );
        return -1;
    }

    if( scc->timecode.hour != opt->timecode.hour )
        x264_cli_log( "caption", X264_LOG_WARNING, "scc timecode hour does not match stream timecode hour\n" );

    opt->drop_frame = tc_separator[0] == ';';

    return 0;

fail:
        fprintf( stderr, "[caption] could not read scc file\n" );
        return -1;
}

int write_cc( cli_opt_t *opt, x264_sei_t *sei, int odd )
{
    bs_t q, r;
    uint8_t temp[1000];
    uint8_t temp2[1000];
    int country_code = 0xb5;
    int provider_code = 0x31;
    char *user_identifier = "GA94";
    int data_type_code = 0x03;

    sei->num_payloads = 1;
    sei->sei_free = free;

    sei->payloads = calloc( 1, sizeof(*sei->payloads ) );
    if( !sei->payloads )
    {
        fprintf( stderr, "Malloc failed\n" );
        return -1;
    }

    bs_init( &r, temp, 1000 );

    bs_write( &r, 8, country_code );   // itu_t_t35_country_code
    bs_write( &r, 16, provider_code ); // itu_t_t35_provider_code

    if( !opt->echostar_captions )
    {
        for( int i = 0; i < 4; i++ )
            bs_write( &r, 8, user_identifier[i] ); // user_identifier
    }

    bs_write( &r, 8, data_type_code ); // user_data_type_code

    bs_init( &q, temp2, 1000 );

    // user_data_type_structure (echostar)
    // cc_data
    bs_write1( &q, 1 );     // reserved
    bs_write1( &q, 1 );     // process_cc_data_flag
    bs_write1( &q, 0 );     // zero_bit / additional_data_flag
    bs_write( &q, 5, opt->timecode_ctx->cc_count ); // cc_count
    bs_write( &q, 8, 0xff ); // reserved

    for( int i = 0; i < opt->timecode_ctx->cc_count; i++ )
    {
        bs_write1( &q, 1 );     // one_bit
        bs_write( &q, 4, 0xf ); // reserved

        if( i > 1 || (opt->timecode_ctx->frame_doubling && i == 1) ) /* nothing to write so maintain a constant bitrate */
            write_invalid( &q );
        /* Each field is written on a separate frame in frame doubling mode */
        else if( opt->timecode_ctx->frame_doubling )
        {
            if( odd != opt->scc_opts[0].sff )
                write_null( &q );
            else
                write_payload( &q, opt );
        }
        /* For some reason certain applications write second field first */
        else if( (i == 1) != opt->scc_opts[0].sff )
            write_null( &q );
        else
            write_payload( &q, opt );
    }

    bs_write( &q, 8, 0xff ); // marker_bits
    bs_flush( &q );

    if( opt->echostar_captions )
    {
        // ATSC1_data
        bs_write( &r, 8, bs_pos( &q ) >> 3 ); // user_data_code_length
    }

    write_bytes( &r, temp2, bs_pos( &q ) >> 3 );

    bs_flush( &r );

    sei->payloads[0].payload_type = 4; // registered itu_t_35
    sei->payloads[0].payload_size = bs_pos( &r ) >> 3;

    sei->payloads[0].payload = malloc( sei->payloads[0].payload_size );
    if( !sei->payloads[0].payload )
    {
        free( sei->payloads );
        fprintf( stderr, "Malloc failed\n" );
        return -1;
    }

    memcpy( sei->payloads[0].payload, temp, sei->payloads[0].payload_size );

    return 0;
}

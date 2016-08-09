/**********************************************************************************************/
/* Copyright 2016-2016 Twitch Interactive, Inc. or its affiliates. All Rights Reserved.       */
/*                                                                                            */
/* Licensed under the Apache License, Version 2.0 (the "License"). You may not use this file  */
/* except in compliance with the License. A copy of the License is located at                 */
/*                                                                                            */
/*     http://aws.amazon.com/apache2.0/                                                       */
/*                                                                                            */
/* or in the "license" file accompanying this file. This file is distributed on an "AS IS"    */
/* BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the    */
/* License for the specific language governing permissions and limitations under the License. */
/**********************************************************************************************/

#include "flv.h"
#include <stdlib.h>
#include <string.h>

void flvtag_init (flvtag_t* tag)
{
    memset (tag,0,sizeof (flvtag_t));
}

void flvtag_swap (flvtag_t* tag1, flvtag_t* tag2)
{
    flvtag_t* tag3;
    (*tag3) = (*tag1);
    (*tag1) = (*tag2);
    (*tag2) = (*tag3);
}

int flvtag_reserve (flvtag_t* tag, uint32_t size)
{
    if (size > tag->aloc) {
        tag->data = realloc (tag->data,size);
        tag->aloc = size;
    }

    return 1;
}

FILE* flv_open_read (const char* flv)
{
    if (0 == flv || 0 == strcmp ("-",flv)) {
        return stdin;
    }

    return fopen (flv,"rb");
}

FILE* flv_open_write (const char* flv)
{
    if (0 == flv || 0 == strcmp ("-",flv)) {
        return stdout;
    }

    return fopen (flv,"wb");
}

FILE* flv_close (FILE* flv)
{
    fclose (flv);
    return 0;
}

#define FLV_HEADER_SIZE 13
#define FLV_FOOTER_SIZE 4
int flv_read_header (FILE* flv, int* has_audio, int* has_video)
{
    uint8_t h[FLV_HEADER_SIZE];

    if (FLV_HEADER_SIZE != fread (&h[0],1,FLV_HEADER_SIZE,flv)) {
        return 0;
    }

    if ('F' != h[0] || 'L' != h[1] || 'V' != h[2]) {
        return 0;
    }

    (*has_audio) = h[4]&0x04;
    (*has_video) = h[4]&0x01;
    return 1;
}

int flv_write_header (FILE* flv, int has_audio, int has_video)
{
    uint8_t h[FLV_HEADER_SIZE] = {'F', 'L', 'V', 1, (has_audio?0x04:0x00) | (has_video?0x01:0x00), 0, 0, 0, 9, 0, 0, 0, 0 };
    return FLV_HEADER_SIZE == fwrite (&h[0],1,FLV_HEADER_SIZE,flv);
}

#define FLV_TAG_HEADER_SIZE 11
#define FLV_TAG_FOOTER_SIZE 4
int flv_read_tag (FILE* flv, flvtag_t* tag)
{
    uint32_t size;
    uint8_t h[FLV_TAG_HEADER_SIZE];

    if (FLV_TAG_HEADER_SIZE != fread (&h[0],1,FLV_TAG_HEADER_SIZE,flv)) {
        return 0;
    }

    size = ( (h[1]<<16) | (h[2]<<8) |h[3]);
    flvtag_reserve (tag, FLV_TAG_HEADER_SIZE + size + FLV_TAG_FOOTER_SIZE);
    // copy header to buffer
    memcpy (tag->data,&h[0],FLV_TAG_HEADER_SIZE);

    if (size+FLV_TAG_FOOTER_SIZE != fread (&tag->data[FLV_TAG_HEADER_SIZE],1,size+FLV_TAG_FOOTER_SIZE,flv)) {
        return 0;
    }

    return 1;
}

int flv_write_tag (FILE* flv, flvtag_t* tag)
{
    size_t size = flvtag_size (tag)+FLV_TAG_HEADER_SIZE+FLV_TAG_FOOTER_SIZE;
    return size == fwrite (tag->data,1,size,flv);
}
////////////////////////////////////////////////////////////////////////////////
size_t flvtag_header_size (flvtag_t* tag)
{
    switch (flvtag_type (tag)) {
    case flvtag_type_audio:
        return FLV_TAG_HEADER_SIZE + (flvtag_soundformat_aac != flvtag_soundformat (tag) ? 1 : 2);

    case flvtag_type_video:
        // CommandFrame does not have a compositionTime
        return FLV_TAG_HEADER_SIZE + (flvtag_codecid_avc != flvtag_codecid (tag) ? 1 : (flvtag_frametype_commandframe != flvtag_frametype (tag) ? 5 : 2));

    default:
        return FLV_TAG_HEADER_SIZE;
    }
}

size_t flvtag_payload_size (flvtag_t* tag)
{
    return FLV_TAG_HEADER_SIZE + flvtag_size (tag) - flvtag_header_size (tag);
}

uint8_t* flvtag_payload_data (flvtag_t* tag)
{
    size_t payload_offset = flvtag_header_size (tag);
    return &tag->data[payload_offset];
}
////////////////////////////////////////////////////////////////////////////////
int flvtag_updatesize (flvtag_t* tag, uint32_t size)
{
    tag->data[1] = size>>16; // DataSize
    tag->data[2] = size>>8; // DataSize
    tag->data[3] = size>>0; // DataSize
    size += 11;
    tag->data[size+0] = size>>24; // PrevTagSize
    tag->data[size+1] = size>>16; // PrevTagSize
    tag->data[size+2] = size>>8; // PrevTagSize
    tag->data[size+3] = size>>0; // PrevTagSize
    return 1;
}

#define FLVTAG_AVC_PREALOC 2048
int flvtag_initavc (flvtag_t* tag, uint32_t dts, int32_t cts, flvtag_frametype_t type)
{
    flvtag_init (tag);
    flvtag_reserve (tag,FLV_TAG_HEADER_SIZE+5+FLV_TAG_FOOTER_SIZE+FLVTAG_AVC_PREALOC);
    tag->data[0] = 0x09; // video
    tag->data[4] = dts>>16;
    tag->data[5] = dts>>8;
    tag->data[6] = dts>>0;
    tag->data[7] = dts>>24;
    tag->data[8] = 0; // StreamID
    tag->data[9] = 0; // StreamID
    tag->data[10] = 0; // StreamID
    // VideoTagHeader
    tag->data[11] = ( (type<<4) %0xF0) |0x07; // CodecId
    tag->data[12] = 0x01; // AVC NALU
    tag->data[13] = cts>>16;
    tag->data[14] = cts>>8;
    tag->data[15] = cts>>0;
    flvtag_updatesize (tag,5);
    return 1;
}

#define LENGTH_SIZE 4

int flvtag_avcwritenal (flvtag_t* tag, uint8_t* data, size_t size)
{
    uint32_t flvsize = flvtag_size (tag);
    flvtag_reserve (tag,FLV_TAG_HEADER_SIZE+flvsize+LENGTH_SIZE+size+FLV_TAG_FOOTER_SIZE);
    uint8_t* payload = tag->data + FLV_TAG_HEADER_SIZE + flvsize;
    payload[0] = size>>24; // nalu size
    payload[1] = size>>16;
    payload[2] = size>>8;
    payload[3] = size>>0;
    memcpy (&payload[LENGTH_SIZE],data,size);
    flvtag_updatesize (tag,flvsize+LENGTH_SIZE+size);

    return 1;
}

int flvtag_addcaption (flvtag_t* tag, const utf8_char_t* text)
{
    if (flvtag_avcpackettype_nalu != flvtag_avcpackettype (tag)) {
        return 0;
    }

    sei_t sei;
    size_t sei_size;
    caption_frame_t frame;
    caption_frame_init (&frame);
    caption_frame_from_text (&frame, text);
    sei_from_caption_frame (&sei, &frame);
    uint8_t* sei_data = malloc (sei_render_size (&sei));

    // rewrite tag
    flvtag_t new_tag;
    flvtag_initavc (&new_tag, flvtag_dts (tag), flvtag_cts (tag), flvtag_frametype (tag));
    uint8_t* data = flvtag_payload_data (tag);
    ssize_t  size = flvtag_payload_size (tag);

    while (0<size) {
        uint8_t*  nalu_data = &data[LENGTH_SIZE];
        uint8_t   nalu_type = nalu_data[0]&0x1F;
        uint32_t  nalu_size = (data[0]<<24) | (data[1]<<16) | (data[2]<<8) | data[3];
        data += LENGTH_SIZE + nalu_size;
        size -= LENGTH_SIZE + nalu_size;

        // we want to write after AUD if present
        if (0 < sei_size && 9 != nalu_type) {
            flvtag_avcwritenal (&new_tag,sei_data,sei_size);
            free (sei_data); sei_data = 0; sei_size = 0;
        }

        flvtag_avcwritenal (&new_tag,nalu_data,nalu_size);
    }

    // On the off chance we have an empty frame,
    // We still wish to append the sei
    if (sei_size) {
        flvtag_avcwritenal (&new_tag,sei_data,sei_size);
        free (sei_data); sei_data = 0; sei_size = 0;
    }

    free (tag->data);
    tag->data = new_tag.data;
    tag->aloc = new_tag.aloc;
    return 1;
}
/**
 * @file
 * JPEG 2000 decoder using Kakadu
 */

#include "libavutil/common.h"
#include "libavutil/opt.h"

#include "avcodec.h"
#include "codec_internal.h"

#include <kduc.h>


typedef struct LibKduContext {
    AVClass *class;
} LibKduContext;

static av_cold int libkdu_decode_init(AVCodecContext *avctx)
{
    LibKduContext *ctx = avctx->priv_data;

    // TODO set decoding parameters

    return 0;
}

static int libkdu_decode_frame(AVCodecContext *avctx, void *data, int *got_frame, AVPacket *avpkt)
{
    LibKduContext *ctx = avctx->priv_data;
    int ret;

    // TODO decode frame stripes

done:
    // TODO clean
    return ret;
}

#define OFFSET(x) offsetof(LibKduContext, x)
#define VD AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_DECODING_PARAM

static const AVOption options[] = {
    { NULL },
};

static const AVClass kakadu_decoder_class = {
    .class_name = "libkdudec",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

const FFCodec ff_libkdu_decoder = {
    .p.name         = "libkdudec",
    .p.long_name    = NULL_IF_CONFIG_SMALL("Kakadu JPEG 2000 Decoder"),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_JPEG2000,
    .priv_data_size = sizeof(LibKduContext),
    .init           = libkdu_decode_init,
    .decode         = libkdu_decode_frame,
    .p.capabilities = AV_CODEC_CAP_DELAY | AV_CODEC_CAP_DR1,
    .p.priv_class   = &kakadu_decoder_class,
    .p.wrapper_name = "libkdu",
};

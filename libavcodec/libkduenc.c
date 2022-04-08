/**
 * @file
 * JPEG 2000 encoder using Kakadu
 */

#include "libavutil/common.h"
#include "libavutil/opt.h"
#include "avcodec.h"
#include "codec_internal.h"
#include <kduc.h>

typedef struct LibKduContext {
    AVClass *avclass;
} LibKduContext;

static av_cold int libkdu_encode_init(AVCodecContext *avctx)
{
    LibKduContext *ctx = avctx->priv_data;

    // TODO set encoding parameters

    return 0;
}

static int libkdu_encode_frame(AVCodecContext *avctx, AVPacket *pkt,
                                    const AVFrame *frame, int *got_packet)
{
    LibKduContext *ctx = avctx->priv_data;
    int ret;

    // TODO encode frame stripes

done:
    // TODO clean
    return ret;
}

#define OFFSET(x) offsetof(LibKduContext, x)
#define VE AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_ENCODING_PARAM
static const AVOption options[] = {
    { NULL },
};

static const AVClass kakadu_encoder_class = {
    .class_name = "libkduenc",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

const FFCodec ff_libkdu_encoder = {
    .p.name         = "libkduenc",
    .p.long_name    = NULL_IF_CONFIG_SMALL("Kakadu JPEG 2000 Encoder"),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_JPEG2000,
    .priv_data_size = sizeof(LibKduContext),
    .init           = libkdu_encode_init,
    .encode2        = libkdu_encode_frame,
    .p.capabilities = AV_CODEC_CAP_FRAME_THREADS,
    .p.pix_fmts     = (const enum AVPixelFormat[]) {
        AV_PIX_FMT_RGB24,
    },
    .p.priv_class   = &kakadu_encoder_class,
    .p.wrapper_name = "libkdu",
};

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
    uint8_t *buf = avpkt->data;
    int buf_size = avpkt->size;
    AVFrame *picture = data;
    LibKduContext *ctx = avctx->priv_data;

    int width, height, num_comps, ret;
    unsigned char *pixels;
    int* stripe_heights;
    int pull_strip_should_stop = 0;

    kdu_compressed_source *source;
    kdu_codestream *code_stream;
    kdu_stripe_decompressor *decompressor;

    if((ret = kdu_compressed_source_buffered_new(buf, buf_size, &source))) {
        goto done;
    }

    if((ret = kdu_codestream_create_from_source(source, &code_stream))) {
        goto done;
    }

    kdu_codestream_get_size(code_stream, 0, &height, &width);

    num_comps = kdu_codestream_get_num_components(code_stream);

    if((ret = kdu_stripe_decompressor_new(&decompressor))) {
        goto done;
    }

    pixels = av_malloc(width * height * num_comps);
    stripe_heights = av_malloc(num_comps * sizeof(int));

    for (int i = 0; i < num_comps; ++i) {
        stripe_heights[i] = height;
    }

    while(!pull_strip_should_stop) {
        pull_strip_should_stop = kdu_stripe_decompressor_pull_stripe(decompressor, pixels, stripe_heights);
    }

    ret = kdu_stripe_decompressor_finish(decompressor);

done:
    av_free(pixels);
    av_free(stripe_heights);
    kdu_stripe_decompressor_delete(decompressor);
    kdu_codestream_delete(code_stream);
    kdu_compressed_source_buffered_delete(source);
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

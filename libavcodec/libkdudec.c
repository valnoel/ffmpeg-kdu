/**
 * @file
 * JPEG 2000 decoder using Kakadu
 */

#include "libavutil/common.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"

#include "avcodec.h"
#include "codec_internal.h"
#include "internal.h"

#include <kduc.h>


typedef struct LibKduContext {
    AVClass *class;
    int fastest;
    int precise;
    int reduce;
    kdu_stripe_decompressor_options decompressor_opts;
} LibKduContext;

static enum AVPixelFormat guess_pixel_format(int nb_components, int component_bit_depth, int is_chroma_sub_sampled) {
    switch (nb_components) {
        case 1:
            switch (component_bit_depth) {
                case 8: return AV_PIX_FMT_GRAY8;
                case 16: return AV_PIX_FMT_GRAY16;
                default: return AV_PIX_FMT_NONE;
            }
        case 2:
            switch (component_bit_depth) {
                case 8: return AV_PIX_FMT_YA8;
                case 16: return AV_PIX_FMT_YA16;
                default: return AV_PIX_FMT_NONE;
            }
        case 3:
            if (is_chroma_sub_sampled) {
                // TODO YUV
            } else {
                switch (component_bit_depth) {
                    case 8: return AV_PIX_FMT_RGB24;
                    case 16: return AV_PIX_FMT_RGB48;
                    default: return AV_PIX_FMT_NONE;
                }
            }
            break;
        case 4:
            if (is_chroma_sub_sampled) {
                // TODO YUVA
            } else {
                switch (component_bit_depth) {
                    case 8: return AV_PIX_FMT_RGBA;
                    case 16: return AV_PIX_FMT_RGBA64;
                    default: return AV_PIX_FMT_NONE;
                }
            }
            break;
        default: break;
    }
    return AV_PIX_FMT_NONE;
}

static av_cold int libkdu_decode_init(AVCodecContext *avctx)
{
     LibKduContext *ctx = avctx->priv_data;

    kdu_stripe_decompressor_options_init(&ctx->decompressor_opts);

    ctx->decompressor_opts.want_fastest = ctx->fastest;
    ctx->decompressor_opts.force_precise = ctx->precise;
    ctx->decompressor_opts.reduce = ctx->reduce;

    return 0;
}

static int libkdu_decode_frame(AVCodecContext *avctx, AVFrame *frame, int *got_frame, AVPacket *avpkt)
{
    uint8_t *buf = avpkt->data;
    int buf_size = avpkt->size;
    LibKduContext *ctx = avctx->priv_data;

    int width, height;
    int nb_components, component_bit_depth, component_byte_depth;
    int ret;

    int stripe_widths[KDU_MAX_COMPONENT_COUNT];
    int stripe_heights[KDU_MAX_COMPONENT_COUNT];
    int stripe_precisions[KDU_MAX_COMPONENT_COUNT];
    int stripe_row_gaps[KDU_MAX_COMPONENT_COUNT];
    int stripe_signed[KDU_MAX_COMPONENT_COUNT];

    int stop = 0;
    int is_chroma_sub_sampled = 0;

    kdu_compressed_source *source;
    kdu_codestream *code_stream;
    kdu_stripe_decompressor *decompressor;

    if(!avpkt->data) {
        *got_frame = 0;
        return 0;
    }

    // Declare the source buffer
    if ((ret = kdu_compressed_source_buffered_new(buf, buf_size, &source))) {
        goto done;
    }

    // Create a code stream from the source buffer
    if ((ret = kdu_codestream_create_from_source(source, &code_stream))) {
        goto done;
    }

    // Apply input levels restrictions
    kdu_codestream_discard_levels(code_stream, ctx->decompressor_opts.reduce);

    // Retrieve the source pixel components attributes
    nb_components = kdu_codestream_get_num_components(code_stream);
    component_bit_depth = kdu_codestream_get_depth(code_stream, 0);


    for (int i = 0; i < nb_components; ++i) {
        kdu_codestream_get_size(code_stream, i, &stripe_heights[i], &stripe_widths[i]);
        stripe_precisions[i] = kdu_codestream_get_depth(code_stream, i);
        stripe_signed[i] = kdu_codestream_get_signed(code_stream, i);

        if (component_bit_depth != stripe_precisions[i]) {
            av_log(avctx, AV_LOG_ERROR, "Pixel components must have the same bit-depth");
            ret = AVERROR_INVALIDDATA;
            goto done;
        }
    }

    // Set the output frame width and height
    width = stripe_widths[0];
    height = stripe_heights[0];

    if ((ret = ff_set_dimensions(avctx, width, height)) < 0) {
        goto done;
    }

    // Check whether chroma is sub-sampled:
    for (int i = 1; i < nb_components; ++i) {
        if (stripe_widths[i] < width || stripe_heights[i] < height) {
            is_chroma_sub_sampled = 1;
            break;
        }
    }

    // guess pixel format
    avctx->pix_fmt = guess_pixel_format(nb_components, component_bit_depth, is_chroma_sub_sampled);

    // Initialize the decompressor
    if (ret = kdu_stripe_decompressor_new(&decompressor)) {
        goto done;
    }

    // Initialize the output picture buffer
    if ((ret = ff_get_buffer(avctx, frame, 0)) < 0) {
        goto done;
    }


    component_byte_depth = component_bit_depth / 8;
    for (int i = 0; i < nb_components; ++i) {
        stripe_row_gaps[i] = frame->linesize[0] / component_byte_depth;
    }

    // Start decoding the stripes
    kdu_stripe_decompressor_start(decompressor, code_stream, &ctx->decompressor_opts);

    switch (component_bit_depth) {
        case 8:
            while (!stop) {
                stop = kdu_stripe_decompressor_pull_stripe(decompressor, frame->data[0], stripe_heights, NULL, NULL, stripe_row_gaps, stripe_precisions, NULL);
            }
            break;
        case 16:
            while (!stop) {
                stop = kdu_stripe_decompressor_pull_stripe_16(decompressor, (int16_t*) frame->data[0], stripe_heights, NULL, NULL, stripe_row_gaps,
                                                              stripe_precisions, (const bool*) stripe_signed, NULL);
            }
            break;
        default:avpriv_report_missing_feature(avctx, "Pixel component bit-depth %d", component_bit_depth);
            ret = AVERROR_PATCHWELCOME;
            goto done;
    }

    // End decoding the stripes
    if((ret = kdu_stripe_decompressor_finish(decompressor))) {
        goto done;
    }

    *got_frame = 1;

    frame->pict_type = AV_PICTURE_TYPE_I;
    frame->key_frame = 1;

    ret = buf_size;

done:
    // Clean and return
    kdu_stripe_decompressor_delete(decompressor);
    kdu_codestream_delete(code_stream);
    kdu_compressed_source_buffered_delete(source);
    return ret;
}

#define OFFSET(x) offsetof(LibKduContext, x)
#define VD AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_DECODING_PARAM

static const AVOption options[] = {
    { "fastest", "Use of 16-bit data processing as often as possible.", OFFSET(fastest), AV_OPT_TYPE_BOOL, {.i64 = 0}, 0, 1, .flags = VD },
    { "precise", "Forces the use of 32-bit representations", OFFSET(precise), AV_OPT_TYPE_BOOL, {.i64 = 0}, 0, 1, .flags = VD },
    { "reduce", "Number of highest resolution levels to be discarded", OFFSET(reduce), AV_OPT_TYPE_INT, {.i64 = 0}, 0, INT16_MAX, .flags = VD },
    { NULL },
};

static const AVClass kakadu_decoder_class = {
    .class_name = "libkdu",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

const FFCodec ff_libkdu_decoder = {
    .p.name         = "libkdu",
    .p.long_name    = NULL_IF_CONFIG_SMALL("Kakadu JPEG 2000 Decoder"),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_JPEG2000,
    .priv_data_size = sizeof(LibKduContext),
    .init           = libkdu_decode_init,
    FF_CODEC_DECODE_CB(libkdu_decode_frame),
    .p.capabilities = AV_CODEC_CAP_DELAY | AV_CODEC_CAP_DR1,
    .p.priv_class   = &kakadu_decoder_class,
    .p.wrapper_name = "libkdu",
};

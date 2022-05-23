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

static enum AVPixelFormat guess_pixel_format(AVCodecContext* avctx,
                                             int nb_components,
                                             int component_bit_depth,
                                             const int* sampling_x,
                                             const int* sampling_y) {
    switch (nb_components) {
        case 1:
            switch (component_bit_depth) {
                case 8: return AV_PIX_FMT_GRAY8;
                case 10: return AV_PIX_FMT_GRAY10;
                case 12: return AV_PIX_FMT_GRAY12;
                case 14: return AV_PIX_FMT_GRAY14;
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
            if (sampling_x[1] != sampling_x[2] || sampling_y[1] != sampling_y[2]) {
                av_log(avctx, AV_LOG_ERROR, "Chroma components must have the same sampling ratio");
                return AV_PIX_FMT_NONE;
            }

            if (sampling_x[1] > 1 || sampling_y[1] > 1) {
                // Sub-sampling case
                switch (sampling_x[1]) {
                    case 1:
                        switch (sampling_y[1]) {
                            case 2:
                                switch (component_bit_depth) {
                                    case 8: return AV_PIX_FMT_YUV440P;
                                    default: break;
                                }
                            default: break;
                        }
                    case 2:
                        switch (sampling_y[1]) {
                            case 1:
                                switch (component_bit_depth) {
                                    case 8: return AV_PIX_FMT_YUV422P;
                                    case 9: return AV_PIX_FMT_YUV422P9;
                                    case 10: return AV_PIX_FMT_YUV422P10;
                                    case 12: return AV_PIX_FMT_YUV422P12;
                                    case 14: return AV_PIX_FMT_YUV422P14;
                                    case 16: return AV_PIX_FMT_YUV422P16;
                                    default: break;
                                }
                            case 2:
                                switch (component_bit_depth) {
                                    case 8: return AV_PIX_FMT_YUV420P;
                                    case 9: return AV_PIX_FMT_YUV420P9;
                                    case 10: return AV_PIX_FMT_YUV420P10;
                                    case 12: return AV_PIX_FMT_YUV420P12;
                                    case 14: return AV_PIX_FMT_YUV420P14;
                                    case 16: return AV_PIX_FMT_YUV420P16;
                                    default: break;
                                }
                            default: break;
                        }
                    case 4:
                        switch (sampling_y[1]) {
                            case 1:
                                switch (component_bit_depth) {
                                    case 8: return AV_PIX_FMT_YUV411P;
                                    default: break;
                                }
                            case 2:
                                switch (component_bit_depth) {
                                    case 8: return AV_PIX_FMT_YUV410P;
                                    default: break;
                                }
                            default: break;
                        }
                    default: break;
                }
            } else {
                switch (component_bit_depth) {
                    case 8: return AV_PIX_FMT_RGB24;
                    case 9: return AV_PIX_FMT_GBRP9;
                    case 10: return AV_PIX_FMT_GBRP10;
                    case 12: return AV_PIX_FMT_GBRP12;
                    case 14: return AV_PIX_FMT_GBRP14;
                    case 16: return AV_PIX_FMT_RGB48;
                    default: break;
                }
            }
            break;
        case 4:
            if (sampling_x[1] != sampling_x[2] || sampling_y[1] != sampling_y[2]) {
                av_log(avctx, AV_LOG_ERROR, "Chroma components must have the same sampling ratio");
                return AV_PIX_FMT_NONE;
            }

            if (sampling_x[1] > 1 || sampling_y[1] > 1) {
                // Sub-sampling case
                switch (sampling_x[1]) {
                    case 2:
                        switch (sampling_y[1]) {
                            case 1:
                                switch (component_bit_depth) {
                                    case 8: return AV_PIX_FMT_YUVA422P;
                                    case 10: return AV_PIX_FMT_YUVA422P10;
                                    case 16: return AV_PIX_FMT_YUVA422P16;
                                    default: break;
                                }
                            case 2:
                                switch (component_bit_depth) {
                                    case 8: return AV_PIX_FMT_YUVA420P;
                                    case 10: return AV_PIX_FMT_YUVA420P10;
                                    case 16: return AV_PIX_FMT_YUVA420P16;
                                    default: break;
                                }
                            default: break;
                        }
                    default: break;
                }
            } else {
                switch (component_bit_depth) {
                    case 8: return AV_PIX_FMT_RGBA;
                    case 16: return AV_PIX_FMT_RGBA64;
                    default: break;
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

    int nb_components, component_bit_depth, component_byte_depth;
    int planes;
    int ret;

    int stripe_widths[KDU_MAX_COMPONENT_COUNT];
    int stripe_heights[KDU_MAX_COMPONENT_COUNT];
    int stripe_precisions[KDU_MAX_COMPONENT_COUNT];
    int stripe_row_gaps[KDU_MAX_COMPONENT_COUNT];
    int stripe_signed[KDU_MAX_COMPONENT_COUNT];

    int component_sampling_x[KDU_MAX_COMPONENT_COUNT];
    int component_sampling_y[KDU_MAX_COMPONENT_COUNT];

    int stop = 0;

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
    if ((ret = ff_set_dimensions(avctx, stripe_widths[0], stripe_heights[0])) < 0) {
        goto done;
    }

    // Get component sub-sampling ratios:
    for (int i = 0; i < nb_components; ++i) {
        kdu_codestream_get_subsampling(code_stream, i, &component_sampling_x[i], &component_sampling_y[i]);
    }

    // guess pixel format
    if (avctx->pix_fmt == AV_PIX_FMT_NONE) {
        avctx->pix_fmt = guess_pixel_format(avctx, nb_components, component_bit_depth, component_sampling_x, component_sampling_y);
    }
    if (avctx->pix_fmt == AV_PIX_FMT_NONE) {
        av_log(avctx, AV_LOG_ERROR, "Could not to identify the input pixel format");
        goto done;
    }

    // Initialize the decompressor
    if (ret = kdu_stripe_decompressor_new(&decompressor)) {
        goto done;
    }

    // Initialize the output picture buffer
    if ((ret = ff_get_buffer(avctx, frame, 0)) < 0) {
        goto done;
    }


    planes = av_pix_fmt_count_planes(avctx->pix_fmt);

    if (planes > 1) {
        component_byte_depth = component_bit_depth / 8;
        for (int i = 0; i < nb_components; ++i) {
            stripe_row_gaps[i] = frame->linesize[0] / component_byte_depth;
        }
    }

    // Start decoding the stripes
    kdu_stripe_decompressor_start(decompressor, code_stream, &ctx->decompressor_opts);

    switch (component_bit_depth) {
        case 8:
            if (planes > 1) {
                while (!stop) {
                    stop = kdu_stripe_decompressor_pull_stripe_planar(decompressor, frame->data, stripe_heights, NULL, NULL, stripe_precisions, NULL);
                }
            } else {
                while (!stop) {
                    stop = kdu_stripe_decompressor_pull_stripe(decompressor, frame->data[0], stripe_heights, NULL, NULL, stripe_row_gaps, stripe_precisions,
                                                               NULL);
                }
            }
            break;
        case 9:
        case 10:
        case 12:
        case 14:
        case 16:
            if (planes > 1) {
                while (!stop) {
                    stop = kdu_stripe_decompressor_pull_stripe_planar_16(decompressor, (int16_t**) frame->data, stripe_heights, NULL, NULL, stripe_precisions,
                                                                         (const bool*) stripe_signed, NULL);
                }
            } else {
                while (!stop) {
                    stop = kdu_stripe_decompressor_pull_stripe_16(decompressor, (int16_t*) frame->data[0], stripe_heights, NULL, NULL, stripe_row_gaps,
                                                                  stripe_precisions, (const bool*) stripe_signed, NULL);
                }
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

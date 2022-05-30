/**
 * @file
 * JPEG 2000 encoder using Kakadu
 */

#include "libavutil/common.h"
#include "libavutil/imgutils.h"
#include "libavutil/opt.h"
#include "libavutil/avstring.h"

#include "avcodec.h"
#include "codec_internal.h"

#include <kduc.h>

#define KAKADU_MAX_GENERIC_PARAMS 16

typedef struct LibKduContext {
    AVClass *avclass;
    const char* kdu_generic_params[KAKADU_MAX_GENERIC_PARAMS];
    kdu_stripe_compressor_options encoder_opts;
    char *kdu_params;
    char *rate;
    char *slope;
    float tolerance;
    int fastest;
    int precise;
} LibKduContext;

LibKduContext* msg_ctx = NULL;

static void libkdu_error_handler(const char* msg) {
    av_log(msg_ctx, AV_LOG_ERROR, "%s", msg);
}

static void libkdu_warning_handler(const char* msg) {
    av_log(msg_ctx, AV_LOG_WARNING, "%s", msg);
}

static void libkdu_info_handler(const char* msg) {
    av_log(msg_ctx, AV_LOG_INFO, "%s", msg);
}

static void libkdu_debug_handler(const char* msg) {
    av_log(msg_ctx, AV_LOG_DEBUG, "%s", msg);
}

static void libkdu_get_component_dimensions(AVCodecContext *avctx, const int component_index, int* height, int* width) {
    const AVPixFmtDescriptor* pix_fmt_desc = av_pix_fmt_desc_get(avctx->pix_fmt);

    if (height) {
        *height = (component_index == 0)? avctx->height : avctx->height / (1 << pix_fmt_desc->log2_chroma_h);
    }

    if (width) {
        *width = (component_index == 0)? avctx->width : avctx->width / (1 << pix_fmt_desc->log2_chroma_w);
    }
}

static int libkdu_do_encode_frame(AVCodecContext *avctx, const AVFrame *frame, const AVPixFmtDescriptor *pix_fmt_desc, kdu_stripe_compressor *encoder, kdu_codestream *code_stream, const int planes)
{
    LibKduContext* ctx = avctx->priv_data;

    int stripe_heights[KDU_MAX_COMPONENT_COUNT];
    int stripe_precisions[KDU_MAX_COMPONENT_COUNT];
    int stripe_row_gaps[KDU_MAX_COMPONENT_COUNT];
    int stripe_signed[KDU_MAX_COMPONENT_COUNT];

    int stop;

    int component_bit_depth = pix_fmt_desc->comp[0].depth;
    int component_byte_depth = ceil((double) component_bit_depth / 8);

    for (int i = 0; i < pix_fmt_desc->nb_components; ++i) {
        libkdu_get_component_dimensions(avctx, i, &stripe_heights[i], NULL);

        stripe_precisions[i] = pix_fmt_desc->comp[i].depth;
        switch (component_byte_depth) {
            case 1:
                stripe_row_gaps[i] = frame->linesize[pix_fmt_desc->comp[i].plane];
                break;
            case 2:
                stripe_row_gaps[i] = frame->linesize[pix_fmt_desc->comp[i].plane] >> 1;
                break;
            default:
                avpriv_report_missing_feature(avctx, "Pixel component bit-depth %d", component_bit_depth);
                return AVERROR_PATCHWELCOME;
        }
        stripe_signed[i] = 0;

    }

    kdu_stripe_compressor_start(encoder, code_stream, &ctx->encoder_opts);

    stop = 0;
    switch (component_bit_depth) {
        case 8:
            if (planes > 1) {
                while (!stop) {
                    stop = kdu_stripe_compressor_push_stripe_planar(encoder, (uint8_t**) frame->data, stripe_heights, NULL, stripe_row_gaps, stripe_precisions);
                }
            } else {
                while (!stop) {
                    stop = kdu_stripe_compressor_push_stripe(encoder, frame->data[0], stripe_heights, NULL, NULL, stripe_row_gaps, stripe_precisions);
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
                    stop = kdu_stripe_compressor_push_stripe_planar_16(encoder, (int16_t**) frame->data, stripe_heights, NULL, stripe_row_gaps, stripe_precisions,
                                                                       (const bool*) stripe_signed);
                }
            } else {
                while (!stop) {
                    stop = kdu_stripe_compressor_push_stripe_16(encoder, (int16_t*) frame->data[0], stripe_heights, NULL, NULL, stripe_row_gaps,
                                                                stripe_precisions, (const bool*) stripe_signed);
                }
            }
            break;
        default:
            av_log(avctx, AV_LOG_ERROR, "Unsupported %s pixel format", av_get_pix_fmt_name(avctx->pix_fmt));
            return AVERROR_INVALIDDATA;
    }


    return kdu_stripe_compressor_finish(encoder);
}

static void parse_generic_parameters(LibKduContext *ctx)
{
    char* kdu_param;
    char* save_ptr;
    const char* delims = " ";

    if (ctx->kdu_params) {
        kdu_param = av_strtok(ctx->kdu_params, delims, &save_ptr);
        for(int i = 0; kdu_param != NULL; i++) {
            ctx->kdu_generic_params[i] = av_strdup(kdu_param);
            kdu_param = av_strtok(NULL, delims, &save_ptr);
        }
    }
}

static int parse_rate_parameter(LibKduContext *ctx)
{
    char* item;
    char* save_ptr;
    const char* delims = ",";
    float ratio;

    if (ctx->rate) {
        item = av_strtok(ctx->rate, delims, &save_ptr);
        for(int i = 0; item != NULL; i++) {
            if (i == 0 && strcmp(item, "-") == 0) {
                ctx->encoder_opts.rate[i] = -1.0f;
                ctx->encoder_opts.rate_count++;
            } else {
                ratio = atof(item);
                if (ratio <= 0.0) {
                    av_log(ctx, AV_LOG_ERROR, "Rate parameters must be strictly positive real numbers");
                    return 1;
                }
                ctx->encoder_opts.rate[i] = ratio;
                ctx->encoder_opts.rate_count++;
            }
            item = av_strtok(NULL, delims, &save_ptr);
        }
    }

    return 0;
}

static int parse_slope_parameter(LibKduContext *ctx)
{
    char* item;
    char* save_ptr;
    const char* delims = ",";
    int slope;

    if (ctx->slope) {
        item = av_strtok(ctx->slope, delims, &save_ptr);
        for (int i = 0; item != NULL; i++) {
            slope = atoi(item);
            if((slope < 0) || (slope > UINT16_MAX)) {
                av_log(ctx, AV_LOG_ERROR, "Distortion-length slope values must be in the range 0 to 65535");
                return 1;
            }
            ctx->encoder_opts.slope[i] = slope;
            ctx->encoder_opts.slope_count++;
            item = av_strtok(NULL, delims, &save_ptr);
        }
    }

    return 0;
}

static av_cold int libkdu_encode_init(AVCodecContext *avctx)
{
    LibKduContext *ctx = avctx->priv_data;
    msg_ctx = ctx;

    kdu_register_error_handler(&libkdu_error_handler);
    kdu_register_warning_handler(&libkdu_warning_handler);
    kdu_register_info_handler(&libkdu_info_handler);
    kdu_register_debug_handler(&libkdu_debug_handler);

    parse_generic_parameters(ctx);

    kdu_stripe_compressor_options_init(&ctx->encoder_opts);

    if(parse_rate_parameter(ctx) || parse_slope_parameter(ctx)) {
        return 1;
    }

    ctx->encoder_opts.force_precise = ctx->precise;
    ctx->encoder_opts.want_fastest = ctx->fastest;
    ctx->encoder_opts.tolerance = ctx->tolerance / 100;

    return 0;
}

static int libkdu_encode_frame(AVCodecContext *avctx, AVPacket *pkt, const AVFrame *frame, int *got_packet)
{
    LibKduContext* ctx = avctx->priv_data;
    const AVPixFmtDescriptor *pix_fmt_desc;

    kdu_codestream *code_stream;
    kdu_siz_params *siz_params;
    mem_compressed_target *target;
    kdu_stripe_compressor *encoder;

    uint8_t* buffer;
    uint8_t* pkt_data;
    int buf_sz;
    int component_bit_depth, component_height, component_width;
    int ret;

    int planes = av_pix_fmt_count_planes(avctx->pix_fmt);

    pix_fmt_desc = av_pix_fmt_desc_get(avctx->pix_fmt);
    component_bit_depth = pix_fmt_desc->comp[0].depth;

    for (int i = 1; i < pix_fmt_desc->nb_components; ++i) {
        if (component_bit_depth != pix_fmt_desc->comp[i].depth) {
            av_log(avctx, AV_LOG_ERROR, "Pixel components must have the same bit-depth");
            return AVERROR_INVALIDDATA;
        }
    }

    if ((ret = kdu_siz_params_new(&siz_params))) {
        goto done;
    }

    kdu_siz_params_set_num_components(siz_params, pix_fmt_desc->nb_components);

    for (int i = 0; i < pix_fmt_desc->nb_components; ++i) {
        libkdu_get_component_dimensions(avctx, i, &component_height, &component_width);

        kdu_siz_params_set_precision(siz_params, i, component_bit_depth);
        kdu_siz_params_set_size(siz_params, i, component_height, component_width);
        kdu_siz_params_set_signed(siz_params, i, 0);
    }

    // Allocate output buffer and code stream
    if((ret = kdu_compressed_target_mem_new(&target))) {
        goto done;
    }

    if ((ret = kdu_codestream_create_from_target(target, siz_params, &code_stream))) {
        goto done;
    };

    for (int i = 0; i < KAKADU_MAX_GENERIC_PARAMS; ++i) {
        if (ctx->kdu_generic_params[i] != NULL) {
            if ((ret = kdu_codestream_parse_params(code_stream, ctx->kdu_generic_params[i]))) {
                goto done;
            }
        }
    }

    // Create encoder
    if ((ret = kdu_stripe_compressor_new(&encoder))) {
        goto done;
    }

    // Encode frame
    if((ret = libkdu_do_encode_frame(avctx, frame, pix_fmt_desc, encoder, code_stream, planes))) {
        goto done;
    }

    // Retrieve encoded data
    kdu_compressed_target_bytes(target, &buffer, &buf_sz);

    pkt_data = av_malloc(buf_sz);
    if (!pkt_data) {
        ret = AVERROR(ENOMEM);
        goto done;
    }
    memcpy(pkt_data, buffer, buf_sz);
    if ((ret = av_packet_from_data(pkt, pkt_data, buf_sz)))
        goto done;

    *got_packet = 1;

done:
    kdu_stripe_compressor_delete(encoder);
    kdu_codestream_delete(code_stream);
    kdu_compressed_target_mem_delete(target);
    kdu_siz_params_delete(siz_params);
    return ret;
}

#define OFFSET(x) offsetof(LibKduContext, x)
#define VE AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_ENCODING_PARAM
static const AVOption options[] = {
    { "rate",       "Compressor bit-rates: -|<bits/pel>,<bits/pel>,...",   OFFSET(rate),       AV_OPT_TYPE_STRING, {.str = NULL}, .flags = VE },
    { "slope",      "Distortion-length slope thresholds",                  OFFSET(slope),      AV_OPT_TYPE_STRING, {.str = NULL}, .flags = VE },
    { "fastest",    "Use of 16-bit data processing as often as possible.", OFFSET(fastest),    AV_OPT_TYPE_BOOL,   {.i64 = 0},    0,  1, .flags = VE },
    { "precise",    "Forces the use of 32-bit representations",            OFFSET(precise),    AV_OPT_TYPE_BOOL,   {.i64 = 0},    0,  1, .flags = VE },
    { "tolerance",  "Percent tolerance on layer sizes given using rate",   OFFSET(tolerance),  AV_OPT_TYPE_FLOAT,  {.dbl = 2.0},  0,  50, .flags = VE },
    { "kdu_params", "KDU generic arguments",                               OFFSET(kdu_params), AV_OPT_TYPE_STRING, {.str = NULL}, .flags = VE },
    { NULL },
};

static const AVClass kakadu_encoder_class = {
    .class_name = "libkdu",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

const FFCodec ff_libkdu_encoder = {
    .p.name         = "libkdu",
    .p.long_name    = NULL_IF_CONFIG_SMALL("Kakadu JPEG 2000 Encoder"),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_JPEG2000,
    .priv_data_size = sizeof(LibKduContext),
    .init           = libkdu_encode_init,
    FF_CODEC_ENCODE_CB(libkdu_encode_frame),
    .p.capabilities = AV_CODEC_CAP_FRAME_THREADS,
    .p.pix_fmts     = (const enum AVPixelFormat[]) {
        AV_PIX_FMT_RGB24, AV_PIX_FMT_RGBA, AV_PIX_FMT_RGB48, AV_PIX_FMT_RGBA64,
        AV_PIX_FMT_GBR24P, AV_PIX_FMT_GBRP9, AV_PIX_FMT_GBRP10, AV_PIX_FMT_GBRP12, AV_PIX_FMT_GBRP14, AV_PIX_FMT_GBRP16,
        AV_PIX_FMT_GRAY8, AV_PIX_FMT_YA8, AV_PIX_FMT_GRAY16, AV_PIX_FMT_YA16,
        AV_PIX_FMT_GRAY10, AV_PIX_FMT_GRAY12, AV_PIX_FMT_GRAY14,
        AV_PIX_FMT_YUV420P, AV_PIX_FMT_YUV422P, AV_PIX_FMT_YUVA420P,
        AV_PIX_FMT_YUV440P, AV_PIX_FMT_YUV444P, AV_PIX_FMT_YUVA422P,
        AV_PIX_FMT_YUV411P, AV_PIX_FMT_YUV410P, AV_PIX_FMT_YUVA444P,
        AV_PIX_FMT_YUV420P9, AV_PIX_FMT_YUV422P9, AV_PIX_FMT_YUV444P9,
        AV_PIX_FMT_YUVA420P9, AV_PIX_FMT_YUVA422P9, AV_PIX_FMT_YUVA444P9,
        AV_PIX_FMT_YUV420P10, AV_PIX_FMT_YUV422P10, AV_PIX_FMT_YUV444P10,
        AV_PIX_FMT_YUVA420P10, AV_PIX_FMT_YUVA422P10, AV_PIX_FMT_YUVA444P10,
        AV_PIX_FMT_YUV420P12, AV_PIX_FMT_YUV422P12, AV_PIX_FMT_YUV444P12,
        AV_PIX_FMT_YUV420P14, AV_PIX_FMT_YUV422P14, AV_PIX_FMT_YUV444P14,
        AV_PIX_FMT_YUV420P16, AV_PIX_FMT_YUV422P16, AV_PIX_FMT_YUV444P16,
        AV_PIX_FMT_YUVA420P16, AV_PIX_FMT_YUVA422P16, AV_PIX_FMT_YUVA444P16,
        AV_PIX_FMT_XYZ12, AV_PIX_FMT_NONE
    },
    .p.priv_class   = &kakadu_encoder_class,
    .p.wrapper_name = "libkdu",
};

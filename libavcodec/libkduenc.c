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
    char *kdu_params;
    const char* kdu_generic_params[KAKADU_MAX_GENERIC_PARAMS];
} LibKduContext;

static inline void libkdu_copy_from_packed_8(uint8_t *data, const AVFrame *frame, int nb_components)
{
    uint8_t *img_ptr;
    int x, y, c;
    int index = 0;

    for (y = 0; y < frame->height; y++) {
        img_ptr = frame->data[0] + y * frame->linesize[0];
        for (x = 0; x < frame->width; x++) {
            for (c = 0; c < nb_components; c++) {
                data[index++] = *img_ptr++;
            }
        }
    }
}

static av_cold int libkdu_encode_init(AVCodecContext *avctx)
{
    LibKduContext *ctx = avctx->priv_data;
    char* kdu_param;
    char* save_ptr;
    const char* delims = " ";

    if (ctx->kdu_params) {
        kdu_param = av_strtok(ctx->kdu_params, delims, NULL);
        for(int i = 0; kdu_param != NULL; i++) {
            ctx->kdu_generic_params[i] = av_strdup(kdu_param);
            kdu_param = av_strtok(NULL, delims, &save_ptr);
        }
    }

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
    kdu_stripe_compressor_options encoder_opts;

    uint8_t* data;
    uint8_t* buffer;
    uint8_t* pkt_data;
    int buf_sz;
    int nb_pixels;
    int* stripe_heights;

    int stop;
    int ret;

    if ((ret = kdu_siz_params_new(&siz_params))) {
        goto done;
    }

    pix_fmt_desc = av_pix_fmt_desc_get(avctx->pix_fmt);

    // Initialize input data buffer
    nb_pixels = frame->width * frame->height * pix_fmt_desc->nb_components;
    data = av_malloc(nb_pixels);
    libkdu_copy_from_packed_8(data, frame, pix_fmt_desc->nb_components);

    kdu_siz_params_set_num_components(siz_params, pix_fmt_desc->nb_components);
    kdu_siz_params_set_precision(siz_params, 0, av_get_bits_per_pixel(pix_fmt_desc));
    kdu_siz_params_set_size(siz_params, 0, avctx->height, avctx->width);
    kdu_siz_params_set_signed(siz_params, 0, 0);

    // Allocate output buffer and code stream
    if((ret = kdu_compressed_target_mem_new(&target))) {
        goto done;
    }

    if ((ret = kdu_codestream_create_from_target(target, siz_params, &code_stream))) {
        goto done;
    };

    for (int i = 0; i < KAKADU_MAX_GENERIC_PARAMS; ++i) {
        if (ctx->kdu_generic_params[i] != NULL) {
            if((ret = kdu_codestream_parse_params(code_stream, ctx->kdu_generic_params[i]))) {
                goto done;
            }
        }
    }

    // Create encoder
    if ((ret = kdu_stripe_compressor_new(&encoder))) {
        goto done;
    }

    stripe_heights = av_malloc(pix_fmt_desc->nb_components * sizeof(int));
    for (int i = 0; i < pix_fmt_desc->nb_components; ++i) {
        stripe_heights[i] = avctx->height;
    }

    kdu_stripe_compressor_options_init(&encoder_opts);

    kdu_stripe_compressor_start(encoder, code_stream, &encoder_opts);

    stop = 0;
    while (!stop) {
        stop = kdu_stripe_compressor_push_stripe(encoder, data, stripe_heights);
    }

    if ((ret = kdu_stripe_compressor_finish(encoder))) {
        goto done;
    }
    
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
    av_free(stripe_heights);
    av_free(data);
    kdu_stripe_compressor_delete(encoder);
    kdu_codestream_delete(code_stream);
    kdu_compressed_target_mem_delete(target);
    kdu_siz_params_delete(siz_params);
    return ret;
}

#define OFFSET(x) offsetof(LibKduContext, x)
#define VE AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_ENCODING_PARAM
static const AVOption options[] = {
    { "kdu_params", "KDU generic arguments", OFFSET(kdu_params), AV_OPT_TYPE_STRING, { .str = NULL }, .flags = VE },
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
    .encode2        = libkdu_encode_frame,
    .p.capabilities = AV_CODEC_CAP_FRAME_THREADS,
    .p.pix_fmts     = (const enum AVPixelFormat[]) {
        AV_PIX_FMT_RGB24, AV_PIX_FMT_NONE
    },
    .p.priv_class   = &kakadu_encoder_class,
    .p.wrapper_name = "libkdu",
};

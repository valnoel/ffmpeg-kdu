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
} LibKduContext;

static inline int libkdu_are_pixel_components_packed(enum AVPixelFormat pix_fmt)
{
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(pix_fmt);
    int i, component_plane;

    if (pix_fmt == AV_PIX_FMT_GRAY16) {
        printf("Pixel GRAY16!\n");
        return 0;
    }

    component_plane = desc->comp[0].plane;
    printf("Component 0: component_plane=%d\n", component_plane);
    for (i = 1; i < desc->nb_components; i++) {
        printf("Component %d: component_plane=%d\n", i, desc->comp[i].plane);
        if (component_plane != desc->comp[i].plane)
            return 0;
    }
    return 1;
}

static inline int libkdu_get_bytes_per_pixel(enum AVPixelFormat pix_fmt)
{
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(pix_fmt);
    return desc->comp[0].step;
}

static inline void libkdu_copy_to_packed_8(AVFrame *picture, const uint8_t *data, int nb_components)
{
    uint8_t *img_ptr;
    int x, y, c;
    int index = 0;

    for (y = 0; y < picture->height; y++) {
        img_ptr = picture->data[0] + y * picture->linesize[0];
        for (x = 0; x < picture->width; x++) {
            for (c = 0; c < nb_components; c++) {
                *img_ptr++ = data[index++];
            }
        }
    }
}

static av_cold int libkdu_decode_init(AVCodecContext *avctx)
{
    // LibKduContext *ctx = avctx->priv_data;

    // TODO set decoding parameters

    return 0;
}

static int libkdu_decode_frame(AVCodecContext *avctx, void *data, int *got_frame, AVPacket *avpkt)
{
    uint8_t *buf = avpkt->data;
    int buf_size = avpkt->size;
    // LibKduContext *ctx = avctx->priv_data;
    AVFrame *picture = data;
    uint8_t* buffer;

    int width, height, num_comps, nb_pixels, bytes_per_pixel, ret;
    int *stripe_heights;
    int pull_strip_should_stop, are_components_packed = 0;

    kdu_compressed_source *source;
    kdu_codestream *code_stream;
    kdu_stripe_decompressor *decompressor;
    kdu_stripe_decompressor_options decompressor_opts;

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

    // Retrieve the frame width and height from the source
    kdu_codestream_get_size(code_stream, 0, &height, &width);

    // Retrieve the number of components of the frame from the source
    num_comps = kdu_codestream_get_num_components(code_stream);

    // Set the output frame width and height
    if ((ret = ff_set_dimensions(avctx, width, height)) < 0) {
        goto done;
    }

    // FIXME hard-coded pixfmt
    avctx->pix_fmt = AV_PIX_FMT_RGB24;

    // Initialize the decompressor
    if (ret = kdu_stripe_decompressor_new(&decompressor)) {
        goto done;
    }

    kdu_stripe_decompressor_options_init(&decompressor_opts);

    // Initialize the output picture buffer
    if ((ret = ff_get_buffer(avctx, picture, 0)) < 0) {
        goto done;
    }

    // Initialize output picture buffer
    nb_pixels = width * height * num_comps;
    buffer = av_malloc(nb_pixels);

    // Initialize the output picture component stripes
    stripe_heights = av_malloc(num_comps * sizeof(int));
    for (int i = 0; i < num_comps; ++i) {
        stripe_heights[i] = height;
    }

    // Start decoding the stripes
    kdu_stripe_decompressor_start(decompressor, code_stream, &decompressor_opts);
    while (!pull_strip_should_stop) {
        pull_strip_should_stop = kdu_stripe_decompressor_pull_stripe(decompressor, buffer, stripe_heights);
    }

    // Get number of bytes per pixel
    bytes_per_pixel = libkdu_get_bytes_per_pixel(avctx->pix_fmt);

    // Are components interlaced (packed) or planar?
    are_components_packed   = libkdu_are_pixel_components_packed(avctx->pix_fmt);

    switch (bytes_per_pixel) {
        case 1:
            if (are_components_packed) {
                libkdu_copy_to_packed_8(picture, buffer, num_comps);
            } else {
                av_log(avctx, AV_LOG_ERROR, "Copy to 8 unimplemented!");
                ret = 0;
                goto done;
            }
            break;
        case 2:
            if (are_components_packed) {
                libkdu_copy_to_packed_8(picture, buffer, num_comps);
            } else {
                av_log(avctx, AV_LOG_ERROR, "Copy to 16 unimplemented!");
                ret = 0;
                goto done;
            }
            break;
        case 3:
        case 4:
            if (are_components_packed) {
                libkdu_copy_to_packed_8(picture, buffer, num_comps);
            }
            break;
        case 6:
        case 8:
            if (are_components_packed) {
                av_log(avctx, AV_LOG_ERROR, "Copy to packed 16 unimplemented!");
                ret = 0;
                goto done;
            }
            break;
        default:
            avpriv_report_missing_feature(avctx, "Pixel size %d", bytes_per_pixel);
            ret = AVERROR_PATCHWELCOME;
            goto done;
    }

    // End decoding the stripes
    if((ret = kdu_stripe_decompressor_finish(decompressor))) {
        goto done;
    }

    *got_frame = 1;

    picture->pict_type = AV_PICTURE_TYPE_I;
    picture->key_frame = 1;

    ret = buf_size;

done:
    // Clean and return
    av_free(buffer);
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
    .decode         = libkdu_decode_frame,
    .p.capabilities = AV_CODEC_CAP_DELAY | AV_CODEC_CAP_DR1,
    .p.priv_class   = &kakadu_decoder_class,
    .p.wrapper_name = "libkdu",
};

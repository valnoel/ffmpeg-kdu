#!/bin/sh

KDU_TEST_DIR=$1
BUILD_DIR=$2/test-output

FFMPEG_CMD="./ffmpeg -y -hide_banner -loglevel error"

mkdir -p ${BUILD_DIR}

out_name=000001_kdu_usage_examples_kdu_compress_a.j2c
${FFMPEG_CMD} -i ${KDU_TEST_DIR}/source/meridian_216p.tif -c:v libkdu -rate 1 ${BUILD_DIR}/${out_name}
cmp ${KDU_TEST_DIR}/compressed/${out_name} ${BUILD_DIR}/${out_name}

out_name=000001_kdu_usage_examples_kdu_compress_a.tif
${FFMPEG_CMD} -c:v libkdu -i ${KDU_TEST_DIR}/compressed/000001_kdu_usage_examples_kdu_compress_a.j2c ${BUILD_DIR}/${out_name}
tiffcmp -t ${KDU_TEST_DIR}/decompressed/${out_name} ${BUILD_DIR}/${out_name}

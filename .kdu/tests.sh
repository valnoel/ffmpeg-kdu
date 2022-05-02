#!/bin/sh

KDU_TEST_DIR=$1
BUILD_DIR=$2/test-output

FFMPEG_CMD="./ffmpeg -hide_banner -loglevel error"

mkdir -p ${BUILD_DIR}

outfile=${BUILD_DIR}/000001_kdu_usage_examples_kdu_compress_a.j2c
${FFMPEG_CMD} -i ${KDU_TEST_DIR}/source/meridian_216p.tif -c:v libkdu -kdu_params "Qfactor=85" ${outfile}
cmp ${KDU_TEST_DIR}/compressed/000001_kdu_usage_examples_kdu_compress_a.j2c ${outfile}

outfile=${BUILD_DIR}/000001_kdu_usage_examples_kdu_compress_a.tif
${FFMPEG_CMD} -c:v libkdu -i ${KDU_TEST_DIR}/compressed/000001_kdu_usage_examples_kdu_compress_a.j2c ${outfile}
tiffcmp -t ${KDU_TEST_DIR}/decompressed/000001_kdu_usage_examples_kdu_compress_a.tif ${outfile}

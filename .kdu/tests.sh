#!/bin/sh

# tests.sh <path to source image dir>
# Source images can be retrieved at s3://ffmpeg-kdu/tests/

TEST_IMG_DIR=$1
BUILD_DIR=${2:-build}/test-output

FFMPEG_CMD="./ffmpeg -y -hide_banner -loglevel error"
KDU_COMPRESS_CMD="kdu_compress -quiet -num_threads 0"

tiff_test () {
  test_name=$1
  source_name=$2

  echo "--- ${test_name} ---"

  source_path=${TEST_IMG_DIR}/${source_name}
  in_path=${BUILD_DIR}/in.${source_name}
  j2c_lossless_path=${BUILD_DIR}/rev.${source_name}.j2c
  j2c_lossy_path=${BUILD_DIR}/irrev.${source_name}.j2c
  lossless_path=${BUILD_DIR}/rev.${source_name}
  lossy_path=${BUILD_DIR}/irrev.${source_name}
  ${FFMPEG_CMD} -i ${source_path} ${in_path}

  ${FFMPEG_CMD} -i ${in_path} -c:v libkdu -kdu_params "Creversible=yes" ${j2c_lossless_path}
  ${FFMPEG_CMD} -c:v libkdu -i ${j2c_lossless_path} ${lossless_path}
  cmp ${in_path} ${lossless_path}

  ${FFMPEG_CMD} -i ${in_path} -c:v libkdu -rate 1 ${j2c_lossy_path}
  ${FFMPEG_CMD} -c:v libkdu -i ${j2c_lossy_path} ${lossy_path}
  ./ffmpeg -hide_banner -i ${lossy_path} -i ${lossless_path}  -lavfi psnr -f null - 2>&1 | grep Parsed_psnr_0
}

yuv_test () {
  test_name=$1
  source_name=$2
  rez=$3
  fmt=$4

  echo "--- ${test_name} ---"

  source_path=${TEST_IMG_DIR}/${source_name}
  in_path=${BUILD_DIR}/in.${source_name}
  j2c_lossless_path=${BUILD_DIR}/rev.${source_name}.j2c
  j2c_lossy_path=${BUILD_DIR}/irrev.${source_name}.j2c
  lossless_path=${BUILD_DIR}/rev.${source_name}
  lossy_path=${BUILD_DIR}/irrev.${source_name}

  yuv_params="-s $2 -pix_fmt $3"

  ${FFMPEG_CMD} ${yuv_params} -i ${in_path} -c:v libkdu -kdu_params "Creversible=yes" ${j2c_lossless_path}
  ${FFMPEG_CMD} -c:v libkdu -i ${j2c_lossless_path} ${yuv_params} ${lossless_path}
  cmp ${in_path} ${lossless_path}

  ${FFMPEG_CMD} ${yuv_params} -i ${in_path} -c:v libkdu -rate 1 ${j2c_lossy_path}
  ${FFMPEG_CMD} -c:v libkdu -i ${j2c_lossy_path} ${yuv_params} ${lossy_path}
  ./ffmpeg -hide_banner ${yuv_params} -i ${lossy_path} -i ${lossless_path}  -lavfi psnr -f null - 2>&1 | grep Parsed_psnr_0
}

mkdir -p ${BUILD_DIR}

tiff_test "RGB48" "meridian_216p.tif"
tiff_test "RGB24" "meridian_216p.8.tif"
yuv_test "yuv420p" "ParkJoy_1920x1080p_50_8b_420.000499.yuv" "1920x1080" "yuv420p"
yuv_test "yuv422p" "ParkJoy_1920x1080p_50_8b_422.000499.yuv" "1920x1080" "yuv422p"
yuv_test "yuv420p12le" "ParkJoy_1920x1080p_50_12b_420.000499.yuv" "1920x1080" "yuv420p12le"
yuv_test "yuv422p12le" "ParkJoy_1920x1080p_50_12b_422.000499.yuv" "1920x1080" "yuv422p12le"
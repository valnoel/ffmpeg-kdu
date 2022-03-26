# FFMPEG KDU

## Overview

FFmpeg-KDU allows FFmpeg to use the [Kakadu SDK](https://kakadusoftware.com/) to
process JPEG 2000 codestreams.

FFmpeg-KDU is a single patchset intended to be applied to the `HEAD` of
the `master` branch of the [FFmpeg repo](https://github.com/FFmpeg/FFmpeg),
which is considered `upstream`.

## Usage

### Relationship with Kakadu demo apps

The patchset mimics the `kdu_compress` and `kdu_expand` CLI applications
provided with the Kakadu SDK.

For example:

`kdu_compress -i image.png Cmodes=HT Creversible=yes -o image.j2c`

becomes

`ffmpeg -i image.png -c:v libkdu -kdu_params "Cmodes=HT Creversible=yes" image.j2c`

### Encoding

```sh
ffmpeg -i <input file> -c:v libkdu [kdu_option_1, kdu_option_2, ...] [-kdu_params "param_1 param_2 ..."] <output file>

  kdu_option:
    -kdu_rate -|<bits/pel>,<bits/pel>,...
    -kdu_slope <layer slope>,<layer slope>,...
    -kdu_precise -- forces the use of 32-bit representations.
    -kdu_double_buffering <stripe height>

  kdu_params:
    (see kdu_params.h)
```

### Decoding

```sh
  ffmpeg -c:v libkdu [kdu_option_1, kdu_option_2, ...] [-kdu_params "param_1 param_2 ..."] -i <input file> <output file>

  kdu_option:
    -kdu_reduce <discard levels>
    -precise -- forces the use of 32-bit representations.
    -kdu_double_buffering <stripe height>
  
  kdu_params:
    (see kdu_params.h)
```

## Directory layout

The layout is identical to `usptream` with the addition of two directories:

* `.github` for CI
* `.kdu` for files specific to FFmpeg-KDU

These two directories are not included in the patchset and not required to add
support for the Kakadu SDK to FFmpeg.

## Prerequisites

In addition to the usual FFmpeg prerequisites, the Kakadu SDK must be installed.
Below is a sample script for Ubuntu:

```sh
BUILD_DIR=~/tmp/kdu # <--- replace with a temporary location
KDU_SDK=v8_2_1-01908E # <--- replace with the version of the Kakadu SDK you are using
KDU_SDK_ZIP=~/downloads/$KDU_SDK.zip # <--- replace with the path to the Kakadu SDK zip
mkdir -p $BUILD_DIR
cd $BUILD_DIR
unzip $KDU_SDK_ZIP
cd $BUILD_DIR/$KDU_SDK
mv srclib_ht srclib_ht_noopt
cp -r altlib_ht_opt srclib_ht
cd $BUILD_DIR/$KDU_SDK/make
make CXXFLAGS=-DFBC_ENABLED -f Makefile-Linux-x86-64-gcc all_but_jni
cd $BUILD_DIR/$KDU_SDK
sudo cp bin/Linux-x86-64-gcc/* /usr/local/bin/
sudo cp lib/Linux-x86-64-gcc/* /usr/local/lib/
sudo ldconfig
sudo mkdir -p /usr/local/include/kakadu
sudo cp -r managed/all_includes/* /usr/local/include/kakadu
```

## Git workflow

The `integration` branch contains the complete history of the patchset.
Modifications to the patchset are developed on branches created from
`integration` branch. `upstream` is regularly merged onto `integration` branch.

The patchset is the difference between the `integration` branch and `upstream`,
minus the `.kdu` and `.github` directories.

The `master` branch is `upsteam` with the patchset applied.

## License

The patchset is licensed under the [GNU Lesser General Public License version
2.1](https://opensource.org/licenses/LGPL-2.1).

The [Kakadu SDK](https://kakadusoftware.com/) is *NOT* free software. Please
contact [Kakadu Software](https://kakadusoftware.com/) for more information.
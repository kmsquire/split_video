split_video
===========

Split and recode an mp4 video into evenly sized chunks.

Usage:

    ./split_video [--gop-size 30] [--chunk-size 120] [--skip 123]
                  [--length 1200] input_file output_template

where

    --gop-size   is the size of a group of pictures
    --chunk-size is the size of a chunk in frames
    --skip       are the number of frames to skip at the
                 beginning of the input file
    --length     are the number of frames to encode

Example:

    ./split_video --gop-size 25 --chunk-size 100 myfile.mp4 chunks/%05d.mp4

will split a video into chunks of size 100, with I-frames every 25 frames.

Notes
=====
ffmpeg itself has been adding functionality for chunking video in recent versions.

For an input file at 25 fps, the following command almost produces 120 frame chunks:

    ffmpeg -i input.mp4 -f segment -segment_time 4.8 \
        -segment_format_options movflags=+faststart \
        -force_key_frames '"expr:eq(mod(n,30),0)"' \
        -vcodec h264 -pix_fmt yuv420p \
        -crf 18 \
        -flags +global_header \
        chunks/%05d.mp4

However, I found that occasionally, 119 or 121 frame chunks would be produced, and
for my needs, I wanted all chunks to be exactly 120 frames.  So I wrote this tool.

Caveats
=======
1. audio information is not preserved
2. only tested on mp4 files, and makes some mp4 specific assumptions
3. assumes fixed frame rate encoding
4. only outputs I and P frames (no B frames)

The code could also use some cleanup, but it works for my use case.

Future Work
===========
I have no plans to extend this right now, but I'll gladly accept pull requests
which update the code.  Possible extensions

* include audio
* generalize to other video codecs, variable rate encoding
* for mp4, output B frames

Sources
=======
Based on [ffmpeg sample code](https://www.ffmpeg.org/doxygen/2.7/examples.html).
BSD-style license (see License.txt).


split_video
===========

Split a video into even sized chunks.

Usage:

    ./split_video [--gop-size 30] [--chunk-size 120] input_file output_template

where

    --gop-size   is the size of a group of pictures
    --chunk-size is the size of a chunk in frames

Example:

    ./split_video --gop-size 25 --chunk-size 100 myfile.mp4 chunks/%05d.mp4

will split a video into chunks of size 100, with I-frames every 25 frames.

Note that audio information is not preserved.


Partially based on [ffmpeg sample code](https://www.ffmpeg.org/doxygen/2.7/examples.html).


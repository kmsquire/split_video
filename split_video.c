/*
 * Copyright (c) 2001 Fabrice Bellard
 * Copyright (c) 2015 Kevin Squire
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

/**
 * @file
 * libavcodec API use example.
 *
 * @example decoding_encoding.c
 * Note that libavcodec only handles codecs (mpeg, mpeg4, etc...),
 * not file formats (avi, vob, mp4, mov, mkv, mxf, flv, mpegts, mpegps, etc...). See library 'libavformat' for the
 * format handling
 */

#include <math.h>
#include <stdlib.h>
#include <getopt.h>

#include <libavutil/opt.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/common.h>
#include <libavutil/imgutils.h>
#include <libavutil/mathematics.h>
#include <libavutil/samplefmt.h>
#include <libavutil/avutil.h>

#include <libavutil/avassert.h>
#include <libavutil/channel_layout.h>
#include <libavutil/timestamp.h>


#define MAX_FILENAME_LEN 256


typedef struct {
    AVFormatContext *formatCtx;
    int videoStream;
    AVCodec *codec;
    AVCodecContext *codecCtx;
    int numBytes;
    uint8_t *inbuf;
    AVFrame *frame;
    AVPacket avpkt;
    int frame_count;
} DecoderContext;


int get_video_stream(AVFormatContext *formatCtx)
{
    unsigned int i, videoStream;

    // Find the first video stream
    videoStream=-1;
    for(i=0; i < formatCtx->nb_streams; i++)
        if(formatCtx->streams[i]->codec->codec_type==AVMEDIA_TYPE_VIDEO) {
            videoStream=i;
            break;
        }

    return videoStream;
}


static DecoderContext *init_decoder(const char *filename)
{
    DecoderContext *dc = (DecoderContext *)calloc(1, sizeof(DecoderContext));
    AVCodecContext *codecCtx;

    // Open the stream
    if(avformat_open_input(&(dc->formatCtx), filename, NULL, NULL) != 0) {
        fprintf(stderr, "Couldn't open file");
        exit(1);
    }

    // Retrieve stream information
    if(avformat_find_stream_info(dc->formatCtx, NULL) < 0) {
        fprintf(stderr, "Couldn't find stream information");
        exit(1);
    }

    // Dump information about file onto standard error
    av_dump_format(dc->formatCtx, 0, filename, 0);


    // Get video Stream
    dc->videoStream = get_video_stream(dc->formatCtx);
    if (dc->videoStream == -1) {
        fprintf(stderr, "Couldn't find video stream");
        exit(1);
    }

    codecCtx = dc->formatCtx->streams[dc->videoStream]->codec;

    /* find the decoder */
    dc->codec = avcodec_find_decoder(codecCtx->codec_id);
    if (!dc->codec) {
        fprintf(stderr, "Codec not found\n");
        exit(1);
    }

    /* Allocate codec context */
    dc->codecCtx = avcodec_alloc_context3(dc->codec);
    if (!dc->codecCtx) {
        fprintf(stderr, "Could not allocate video codec context\n");
        exit(1);
    }

    if(avcodec_copy_context(dc->codecCtx, codecCtx) != 0) {
        fprintf(stderr, "Couldn't copy codec context");
        exit(1); // Error copying codec context
    }

    if(dc->codec->capabilities & CODEC_CAP_TRUNCATED)
        dc->codecCtx->flags |= CODEC_FLAG_TRUNCATED; /* we do not send complete frames */

    /* For some codecs, such as msmpeg4 and mpeg4, width and height
       MUST be initialized there because this information is not
       available in the bitstream. */

    /* open it */
    if (avcodec_open2(dc->codecCtx, dc->codec, NULL) < 0) {
        fprintf(stderr, "Could not open codec\n");
        exit(1);
    }

    dc->frame = av_frame_alloc();
    if (!dc->frame) {
        fprintf(stderr, "Could not allocate video frame\n");
        exit(1);
    }

    // Allocate input buffer
    dc->numBytes = avpicture_get_size(dc->codecCtx->pix_fmt,
                                      dc->codecCtx->width,
                                      dc->codecCtx->height);

    dc->inbuf = calloc(1, dc->numBytes + FF_INPUT_BUFFER_PADDING_SIZE);

    if (!dc->inbuf) {
        fprintf(stderr, "Could not allocate buffer");
        exit(1);
    }

    memset(dc->inbuf + dc->numBytes, 0, FF_INPUT_BUFFER_PADDING_SIZE);

    dc->frame_count = 0;

    return dc;
}

AVFrame *read_frame(DecoderContext *dc)
{
    int ret, got_frame;

    got_frame = 0;
    while (av_read_frame(dc->formatCtx, &(dc->avpkt)) == 0) {
        if (dc->avpkt.stream_index == dc->videoStream) {
            ret = avcodec_decode_video2(dc->codecCtx, dc->frame, &got_frame, &(dc->avpkt));
            if (ret < 0) {
                fprintf(stderr, "unable to decode video frame...\n");
                exit(1);
            }
        }

        av_free_packet(&(dc->avpkt));

        if (got_frame)
            break;
    }

    fflush(stderr);
    if (!got_frame)
        return NULL;

    return dc->frame;
}

static void close_decoder(DecoderContext *dc)
{
    av_frame_free(&(dc->frame));
    avcodec_close(dc->codecCtx);
    av_freep(&(dc->codecCtx));
}

typedef struct OutputStream {
    AVStream *st;
    /* pts of the next frame that will be generated */
    int64_t next_pts;
    int samples_count;
    AVFrame *frame;
    AVFrame *tmp_frame;
    float t, tincr, tincr2;
} OutputStream;


/* Add an output stream. */
static void add_stream(OutputStream *ost, AVFormatContext *oc,
                       AVCodec **codec,
                       enum AVCodecID codec_id, int gop_size,
                       int width, int height,
                       AVRational framerate,
                       enum AVPixelFormat pix_fmt)
{
    AVCodecContext *c;
    int i;
    /* find the encoder */
    *codec = avcodec_find_encoder(codec_id);
    if (!(*codec)) {
        fprintf(stderr, "Could not find encoder for '%s'\n",
                avcodec_get_name(codec_id));
        exit(1);
    }
    ost->st = avformat_new_stream(oc, *codec);
    if (!ost->st) {
        fprintf(stderr, "Could not allocate stream\n");
        exit(1);
    }
    ost->st->id = oc->nb_streams-1;
    c = ost->st->codec;
    switch ((*codec)->type) {
    case AVMEDIA_TYPE_AUDIO:
        c->sample_fmt  = (*codec)->sample_fmts ?
            (*codec)->sample_fmts[0] : AV_SAMPLE_FMT_FLTP;
        c->bit_rate    = 64000;
        c->sample_rate = 44100;
        if ((*codec)->supported_samplerates) {
            c->sample_rate = (*codec)->supported_samplerates[0];
            for (i = 0; (*codec)->supported_samplerates[i]; i++) {
                if ((*codec)->supported_samplerates[i] == 44100)
                    c->sample_rate = 44100;
            }
        }
        c->channels        = av_get_channel_layout_nb_channels(c->channel_layout);
        c->channel_layout = AV_CH_LAYOUT_STEREO;
        if ((*codec)->channel_layouts) {
            c->channel_layout = (*codec)->channel_layouts[0];
            for (i = 0; (*codec)->channel_layouts[i]; i++) {
                if ((*codec)->channel_layouts[i] == AV_CH_LAYOUT_STEREO)
                    c->channel_layout = AV_CH_LAYOUT_STEREO;
            }
        }
        c->channels        = av_get_channel_layout_nb_channels(c->channel_layout);
        ost->st->time_base = (AVRational){ 1, c->sample_rate };
        break;
    case AVMEDIA_TYPE_VIDEO:
        c->codec_id = codec_id;
        c->bit_rate = 400000;
        /* Resolution must be a multiple of two. */
        c->width    = width;
        c->height   = height;
        /* timebase: This is the fundamental unit of time (in seconds) in terms
         * of which frame timestamps are represented. For fixed-fps content,
         * timebase should be 1/framerate and timestamp increments should be
         * identical to 1. */
        ost->st->time_base = (AVRational){ framerate.den, framerate.num };
        c->time_base       = ost->st->time_base;
        c->gop_size      = gop_size;
        c->pix_fmt       = pix_fmt;

        if (c->codec_id == AV_CODEC_ID_H264)
            av_opt_set(c->priv_data, "preset", "slow", 0);

        break;
    default:
        break;
    }
    /* Some formats want stream headers to be separate. */
    if (oc->oformat->flags & AVFMT_GLOBALHEADER)
        c->flags |= CODEC_FLAG_GLOBAL_HEADER;
}

/**************************************************************/
/* video output */
static AVFrame *alloc_picture(enum AVPixelFormat pix_fmt, int width, int height)
{
    AVFrame *picture;
    int ret;
    picture = av_frame_alloc();
    if (!picture)
        return NULL;
    picture->format = pix_fmt;
    picture->width  = width;
    picture->height = height;
    /* allocate the buffers for the frame data */
    ret = av_frame_get_buffer(picture, 32);
    if (ret < 0) {
        fprintf(stderr, "Could not allocate frame data.\n");
        exit(1);
    }
    return picture;
}

static void open_video(AVFormatContext *oc, AVCodec *codec, OutputStream *ost, AVDictionary *opt_arg)
{
    int ret;
    AVCodecContext *c = ost->st->codec;
    AVDictionary *opt = NULL;
    av_dict_copy(&opt, opt_arg, 0);
    /* open the codec */
    ret = avcodec_open2(c, codec, &opt);
    av_dict_free(&opt);
    if (ret < 0) {
        fprintf(stderr, "Could not open video codec: %s\n", av_err2str(ret));
        exit(1);
    }
    /* allocate and init a re-usable frame */
    ost->frame = alloc_picture(c->pix_fmt, c->width, c->height);
    if (!ost->frame) {
        fprintf(stderr, "Could not allocate video frame\n");
        exit(1);
    }
    /* If the output format is not YUV420P, then a temporary YUV420P
     * picture is needed too. It is then converted to the required
     * output format. */
    ost->tmp_frame = NULL;
    if (c->pix_fmt != AV_PIX_FMT_YUV420P) {
        ost->tmp_frame = alloc_picture(AV_PIX_FMT_YUV420P, c->width, c->height);
        if (!ost->tmp_frame) {
            fprintf(stderr, "Could not allocate temporary picture\n");
            exit(1);
        }
    }
}

typedef struct {
    OutputStream video_st;
    OutputStream audio_st;
    int have_audio;
    int encode_audio;
    AVFormatContext *oc;
    AVOutputFormat *fmt;
    AVCodec *videoCodec;
    AVCodecContext *videoCodecCtx;
    AVPacket pkt;
    uint8_t endcode[4];
    int frame_count;
    int got_output;
} EncoderContext;


static EncoderContext *init_encoder(const char *filename, int gop_size, int width, int height,
                                    AVRational framerate, enum AVPixelFormat pix_fmt, AVDictionary *_opt) {

    EncoderContext *ec = (EncoderContext *)calloc(1, sizeof(EncoderContext));
    int ret;
    AVDictionary *opt = NULL;
    av_dict_copy(&opt, _opt, 0);

    /* Allocate output context */
    avformat_alloc_output_context2(&(ec->oc), NULL, NULL, filename);
    if (!(ec->oc)) {
        fprintf(stderr, "Could not deduce output format from file extension: using mp4.\n");
        avformat_alloc_output_context2(&(ec->oc), NULL, "mp4", filename);

        if (!(ec->oc)) {
            fprintf(stderr, "Could not allocate output format context\n");
            exit(1);
        }
    }

    ec->fmt = ec->oc->oformat;

    /* Add the video stream using the default format codecs
     * and initialize the codecs. */
    if (ec->fmt->video_codec != AV_CODEC_ID_NONE) {
        add_stream(&(ec->video_st), ec->oc, &(ec->videoCodec), ec->fmt->video_codec, gop_size, width, height, framerate, pix_fmt);
        open_video(ec->oc, ec->videoCodec, &(ec->video_st), opt);
    }

    //av_dump_format(ec->oc, 0, filename, 1);

    /* open the output file, if needed */
   if (!(ec->fmt->flags & AVFMT_NOFILE)) {
        ret = avio_open(&(ec->oc->pb), filename, AVIO_FLAG_WRITE);
        if (ret < 0) {
            fprintf(stderr, "Could not open '%s': %s\n", filename,
                    av_err2str(ret));
            exit(1);
        }
    }

    /* Write the stream header, if any. */
    ret = avformat_write_header(ec->oc, &opt);
    if (ret < 0) {
        fprintf(stderr, "Error occurred when opening output file: %s\n",
                av_err2str(ret));
        exit(1);
    }

    ec->endcode[0] = 0;
    ec->endcode[1] = 0;
    ec->endcode[2] = 1;
    ec->endcode[3] = 0xb7;

    av_init_packet(&(ec->pkt));
    ec->pkt.data = NULL;    // packet data will be allocated by the encoder
    ec->pkt.size = 0;

    ec->frame_count = 0;
    ec->got_output = 0;

    return ec;
}


static int write_frame(AVFormatContext *fmt_ctx, const AVRational *time_base, AVStream *st, AVPacket *pkt)
{
    /* rescale output packet timestamp values from codec to stream timebase */
    av_packet_rescale_ts(pkt, *time_base, st->time_base);
    pkt->stream_index = st->index;
    /* Write the compressed frame to the media file. */
    /* log_packet(fmt_ctx, pkt); */
    return av_interleaved_write_frame(fmt_ctx, pkt);
}

/*
 * encode one video frame and send it to the muxer
 * return 1 when encoding is finished, 0 otherwise
 */
static int write_video_frame(EncoderContext *ec, AVFrame *frame)
{
    OutputStream *ost = &(ec->video_st);
    AVFormatContext *oc = ec->oc;
    int ret;
    AVCodecContext *c;
    c = ost->st->codec;
    if (oc->oformat->flags & AVFMT_RAWPICTURE) {
        /* a hack to avoid data copy with some raw video muxers */
        av_init_packet(&(ec->pkt));
        if (!frame)
            return 1;
        ec->pkt.flags        |= AV_PKT_FLAG_KEY;
        ec->pkt.stream_index  = ost->st->index;
        ec->pkt.data          = (uint8_t *)frame;
        ec->pkt.size          = sizeof(AVPicture);
        ec->pkt.pts = ec->pkt.dts = frame->pts;
        av_packet_rescale_ts(&(ec->pkt), c->time_base, ost->st->time_base);
        ret = av_interleaved_write_frame(oc, &(ec->pkt));
    } else {
        /* encode the image */
        ret = avcodec_encode_video2(c, &(ec->pkt), frame, &(ec->got_output));
        if (ret < 0) {
            fprintf(stderr, "Error encoding video frame: %s\n", av_err2str(ret));
            exit(1);
        }
        if (ec->got_output) {
            ret = write_frame(oc, &c->time_base, ost->st, &(ec->pkt));
        } else {
            ret = 0;
        }
    }
    if (ret < 0) {
        fprintf(stderr, "Error while writing video frame: %s\n", av_err2str(ret));
        exit(1);
    }
    return (frame || ec->got_output) ? 0 : 1;
}


static void flush_frames(EncoderContext *ec)
{
    int ret;
    OutputStream *ost = &(ec->video_st);
    AVFormatContext *oc = ec->oc;
    AVCodecContext *c;
    c = ost->st->codec;

    /* get the delayed frames */
    while (1) {
        ret = avcodec_encode_video2(c, &(ec->pkt), NULL, &(ec->got_output));
        if (ret < 0) {
            fprintf(stderr, "Error encoding frame\n");
            exit(1);
        }

        if (ec->got_output) {
            write_frame(oc, &c->time_base, ost->st, &(ec->pkt));
        }
        else
            break;
    }
}

static void close_stream(AVFormatContext *oc, OutputStream *ost)
{
    avcodec_close(ost->st->codec);
    av_frame_free(&ost->frame);
    av_frame_free(&ost->tmp_frame);
}

static void close_encoder(EncoderContext *ec)
{
    flush_frames(ec);

    av_write_trailer(ec->oc);

    close_stream(ec->oc, &(ec->video_st));
    avio_closep(&(ec->oc->pb));
}

static void set_pict_type(AVFrame *frame, int gop_size, int frame_count) {
    if (frame_count % gop_size == 0)
        frame->pict_type = AV_PICTURE_TYPE_I;
    else
        frame->pict_type = AV_PICTURE_TYPE_P;

}

static void split_video(const char *infilename,
                        const char *outfmt,
                        int gop_size,
                        int chunk_size,
                        int skip,
                        long long length,
                        AVDictionary *_opt)
{
    DecoderContext *dc;
    EncoderContext *ec;

    AVFrame *frame;
    int width, height;
    long long frame_count = 0, out_frame_num = 0;
    int chunk_count = 0;
    char outfilename[MAX_FILENAME_LEN];
    AVDictionary *opt = NULL;
    AVRational framerate;
    enum AVPixelFormat pix_fmt;

    av_dict_copy(&opt, _opt, 0);

    // Initialize the decoder
    dc = init_decoder(infilename);

    // Extract parms needed by encoder
    width = dc->codecCtx->width;
    height = dc->codecCtx->height;
    framerate = dc->codecCtx->framerate;
    pix_fmt = dc->codecCtx->pix_fmt;

    // Skip input frames

    if (skip > 0)
        fprintf(stderr, "Skipping %d frames\n", skip);

    while (skip > 0) {
        // TODO: I'd rather not decode the frames, but this will take some work to
        //       refactor
        if (!read_frame(dc)) {
            fprintf(stderr, "No more frames available, skip = %d\n", skip);
            exit(0);
        }
        --skip;
    }

    // Initialize output
    fprintf(stderr, "\rWriting chunk %05d", chunk_count);
    fflush(stderr);

    snprintf(outfilename, MAX_FILENAME_LEN, outfmt, chunk_count++);
    ec = init_encoder(outfilename, gop_size, width, height, framerate, pix_fmt, opt);

    while (length <= 0 || frame_count < length) {
        frame = read_frame(dc);
        if (!frame)
            break;

        if (out_frame_num == chunk_size) {
            close_encoder(ec);

            fprintf(stderr, "\rWriting chunk %05d", chunk_count);
            fflush(stderr);

            snprintf(outfilename, MAX_FILENAME_LEN, outfmt, chunk_count++);
            ec = init_encoder(outfilename, gop_size, width, height, framerate, pix_fmt, opt);
            out_frame_num = 0;
        }

        set_pict_type(frame, gop_size, out_frame_num);
        frame->pts = out_frame_num++;
        frame_count++;

        write_video_frame(ec, frame);
    }

    close_encoder(ec);
    close_decoder(dc);

    fprintf(stderr, "\nRead %lld frames\n", frame_count);
    fprintf(stderr, "Wrote %d chunks of %d frames each (last chunk: %lld frames)\n", chunk_count, chunk_size, out_frame_num);
    fprintf(stderr, "  for a total of %lld frames\n", (chunk_count-1) * chunk_size + out_frame_num);
}

void print_help(const char * prog_name) {
    printf("\n"
           "    Split a video into even sized chunks.\n"
           "\n"
           "    Usage:\n"
           "\n"
           "        %s [--gop-size 30] [--chunk-size 120] [--skip 123]\n"
           "                  [--length 1200] input_file output_template\n"
           "\n"
           "    where\n"
           "\n"
           "        --gop-size   is the size of a group of pictures\n"
           "        --chunk-size is the size of a chunk in frames\n"
           "        --skip       are the number of frames to skip at the\n"
           "                     beginning of the input file\n"
           "        --length     are the number of frames to encode\n"
           "\n"
           "    Example:\n"
           "\n"
           "        %s --gop-size 25 --chunk-size 100 myfile.mp4 chunks/%%05d.mp4\n"
           "\n"
           "    will split a video into chunks of size 100, with I-frames every 25 frames.\n"
           "\n"
           "    Note that audio information is not preserved.\n\n",
           prog_name, prog_name);
}

int main(int argc, char **argv)
{
    const char *input_file;
    const char *output_template;
    AVDictionary *opt = NULL;
    int gop_size = 30;
    int chunk_size = 120;
    int skip = 0;
    long long length = -1;
    int c;
    static int help = 0;
    char *end;

    while(1)
    {
      static struct option long_options[] =
        {
          /* These options set a flag. */
          {"gop-size",  required_argument, 0, 'g'},
          {"chunk-size",  required_argument, 0, 'c'},
          {"skip", required_argument, 0, 's'},
          {"length", required_argument, 0, 'n'},
          {"help", no_argument, &help, 'h'},
          {0, 0, 0, 0}
        };
      /* getopt_long stores the option index here. */
      int option_index = 0;

      c = getopt_long (argc, argv, "g:c:s:n:h",
                       long_options, &option_index);

      /* Detect the end of the options. */
      if (c == -1)
        break;

      switch (c)
        {
        case 'g':
            gop_size = (int)strtoul(optarg, &end, 10);
            break;

        case 'c':
            chunk_size = (int)strtoul(optarg, &end, 10);
            break;

        case 's':
            skip = (int)strtoul(optarg, &end, 10);
            break;

        case 'n':
            length = strtoul(optarg, &end, 10);
            break;

        case 'h':
            print_help(argv[0]);
            exit(0);
            break;

        case '?':
            break;

        default:
            abort();
        }
    }

    if (chunk_size % gop_size != 0) {
        fprintf(stderr, "chunk size (%d) must be a multiple of gop size (%d)",
                chunk_size, gop_size);
        return 1;
    }

    if (argc - optind != 2) {
        print_help(argv[0]);
        return 1;
    }

    input_file = argv[optind];
    output_template = argv[optind+1];

    printf("GOP size: %d\n", gop_size);
    printf("Chunk size: %d\n", chunk_size);

    av_dict_set(&opt, "crf", "18", 0);
    av_dict_set(&opt, "movflags", "faststart", 0);

    /* register all the codecs */
    av_register_all();
    avcodec_register_all();
    av_log_set_level(AV_LOG_WARNING);

    split_video(input_file, output_template, gop_size, chunk_size, skip, length, opt);

    return 0;
}

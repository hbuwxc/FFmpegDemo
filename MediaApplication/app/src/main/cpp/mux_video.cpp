//
// Created by wxc on 2020-01-16.
//
#include <jni.h>
#include <string>
#include <android/log.h>
#include <unistd.h>

extern "C" {
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libavfilter/buffersink.h"
#include "libavutil/opt.h"
#include "libavfilter/buffersrc.h"
}


#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "MEDIA_CORE", __VA_ARGS__)

static AVFormatContext *mp4FormatContext;
static AVFormatContext *mp3FormatContext;
static AVFormatContext *outputFormatContext;
static AVCodecContext *mp4Context;
static AVCodecContext *mp3Context;
static int mp4Index = -1;
static int mp3Index = -1;

int open_input_file1(const char *video, const char *audio);
int open_output_file3(const char *video);

extern "C" JNIEXPORT void JNICALL
Java_com_watts_myapplication_FFmpegNativeUtils_muxVideo(JNIEnv *env, jclass clazz,
        jstring video_path, jstring mp3_path, jstring output_path){
    int ret = -1;
    int lastVideoDts = -1;
    AVPacket videoPacket;
    AVPacket audioPacket;

    const char *videoPath = env->GetStringUTFChars(video_path, 0);
    const char *mp3Path = env->GetStringUTFChars(mp3_path, 0);
    const char *outputPath = env->GetStringUTFChars(output_path, 0);

    // -1 : ALL 0： 视频  1：音频
    int next_ptk_type = -1;

    if (open_input_file1(videoPath, mp3Path) < 0){
        LOGE("FILE OPEN FAILED");
    }

    if ((open_output_file3(outputPath)) < 0){
        LOGE("open output file failed,");
        return;
    }

    while (1){
        if (next_ptk_type  == -1){
            LOGE("read video and audio ");
            if ((ret = av_read_frame(mp4FormatContext, &videoPacket)) < 0) {
                LOGE("read video frame fail = %s", av_err2str(ret));
                break;
            }
            if ((ret = av_read_frame(mp3FormatContext, &audioPacket)) < 0) {
                LOGE("read audio frame fail = %s", av_err2str(ret));
                break;
            }
        } else if (next_ptk_type == 0){
            LOGE("read video ");
            if ((ret = av_read_frame(mp4FormatContext, &videoPacket)) < 0) {
                LOGE("read video frame fail = %s", av_err2str(ret));
                break;
            }
        } else if (next_ptk_type == 1){
            LOGE("read audio ");
            if ((ret = av_read_frame(mp3FormatContext, &audioPacket)) < 0) {
                LOGE("read video frame fail = %s", av_err2str(ret));
                break;
            }
        }

        if (lastVideoDts >= videoPacket.dts){
            LOGE("ERROR VIDEO FRAME last = %f, new = %f",lastVideoDts * av_q2d(mp4FormatContext->streams[mp4Index]->time_base),videoPacket.dts * av_q2d(mp4FormatContext->streams[mp4Index]->time_base));
            continue;
        }
        LOGE("video packaet pts = %f, audio packet pts = %f",videoPacket.dts * av_q2d(mp4FormatContext->streams[mp4Index]->time_base), audioPacket.dts * av_q2d(mp3FormatContext->streams[mp3Index]->time_base));
        if (av_compare_ts(videoPacket.dts, mp4FormatContext->streams[mp4Index]->time_base, audioPacket.dts, mp3FormatContext->streams[mp3Index]->time_base) < 0){
            // video
            if (videoPacket.flags & AV_PKT_FLAG_KEY){
                LOGE("is key frame %d", videoPacket.flags);
            } else {
                LOGE("is not key frame %d", videoPacket.flags);
            }
            videoPacket.stream_index = 0;
            av_packet_rescale_ts(&videoPacket,
                                 mp4FormatContext->streams[mp4Index]->time_base,
                                 outputFormatContext->streams[0]->time_base);
            LOGE("____________VIDEO_PTS  = %f", videoPacket.pts * av_q2d(outputFormatContext->streams[0]->time_base));
            lastVideoDts = videoPacket.dts;
            ret = av_interleaved_write_frame(outputFormatContext, &videoPacket);
            if(ret < 0){
                LOGE("write video fail ,reason = %s", av_err2str(ret));
                return;
            } else {
                LOGE("write video sec");
            }
            next_ptk_type = 0;
        } else {
            //audio
            audioPacket.stream_index = 1;
            av_packet_rescale_ts(&audioPacket,
                                 mp3FormatContext->streams[mp3Index]->time_base,
                                 outputFormatContext->streams[1]->time_base);

            ret = av_interleaved_write_frame(outputFormatContext, &audioPacket);
            if(ret < 0){
                LOGE("write audio fail ,reason = %s", av_err2str(ret));
                return;
            } else {
                LOGE("write audio sec");
            }
            next_ptk_type = 1;
        }
    }
    av_write_trailer(outputFormatContext);
    LOGE("FINISH MUX VIDEO");
}

int open_input_file1(const char *mp4File, const char *mp3File)
{
    int ret;
    AVCodec *video_dec;
    AVCodec *audio_dec;

    if ((ret = avformat_open_input(&mp4FormatContext, mp4File, NULL, NULL)) < 0) {
        LOGE("Cannot open input file\n");
        return ret;
    }

    if ((ret = avformat_find_stream_info(mp4FormatContext, NULL)) < 0) {
        LOGE("Cannot find stream information\n");
        return ret;
    }

    /* select the video stream */
    ret = av_find_best_stream(mp4FormatContext, AVMEDIA_TYPE_VIDEO, -1, -1, &video_dec, 0);
    if (ret < 0) {
        LOGE("Cannot find a video stream in the input file\n");
        return ret;
    }
    mp4Index = ret;

    /* create decoding context */
    mp4Context = avcodec_alloc_context3(video_dec);
    if (!mp4Context)
        return AVERROR(ENOMEM);
    avcodec_parameters_to_context(mp4Context, mp4FormatContext->streams[mp4Index]->codecpar);
    mp4Context->framerate = av_guess_frame_rate(mp4FormatContext, mp4FormatContext->streams[mp4Index], NULL);

    /* init the video decoder */
    if ((ret = avcodec_open2(mp4Context, video_dec, NULL)) < 0) {
        LOGE("Cannot open video decoder\n");
        return ret;
    }
    av_dump_format(mp4FormatContext, 0, mp4File, 0);

    /* open audio file */
    if ((ret = avformat_open_input(&mp3FormatContext, mp3File, NULL, NULL)) < 0) {
        LOGE("Cannot open input file\n");
        return ret;
    }

    if ((ret = avformat_find_stream_info(mp3FormatContext, NULL)) < 0) {
        LOGE("Cannot find stream information\n");
        return ret;
    }

    /* select the video stream */
    ret = av_find_best_stream(mp3FormatContext, AVMEDIA_TYPE_AUDIO, -1, -1, &audio_dec, 0);
    if (ret < 0) {
        LOGE("Cannot find a video stream in the input file\n");
        return ret;
    }
    mp3Index = ret;

    /* create decoding context */
    mp3Context = avcodec_alloc_context3(audio_dec);
    if (!mp3Context)
        return AVERROR(ENOMEM);
    avcodec_parameters_to_context(mp3Context, mp3FormatContext->streams[mp3Index]->codecpar);
    mp3Context->framerate = av_guess_frame_rate(mp3FormatContext, mp3FormatContext->streams[mp3Index], NULL);

    /* init the video decoder */
    if ((ret = avcodec_open2(mp3Context, audio_dec, NULL)) < 0) {
        LOGE("Cannot open video decoder\n");
        return ret;
    }
    av_dump_format(mp3FormatContext, 0, mp3File, 0);
    LOGE("FINISH OPEN INPUT FILE");
    return 0;
}

int open_output_file3(const char *filename){
    AVStream *out_stream;
    AVStream *in_stream;
    AVCodecContext *enc_ctx;
    AVCodec *encoder;
    int ret;
    unsigned int i;

    outputFormatContext = NULL;
    avformat_alloc_output_context2(&outputFormatContext, NULL, NULL, filename);
    if (!outputFormatContext) {
        LOGE("Could not create output context\n");
        return AVERROR_UNKNOWN;
    }

    for (i = 0; i < 2; i++) {
        // 0 : video 1 : audio
        out_stream = avformat_new_stream(outputFormatContext, NULL);
        if (!out_stream) {
            LOGE("Failed allocating output stream\n");
            return AVERROR_UNKNOWN;
        }
        if (i==0){
            LOGE("NEW VIDEO -- %s", mp4Context->codec->long_name);
            in_stream = mp4FormatContext->streams[mp4Index];
        } else {
            LOGE("NEW AUDIO -- %s", mp3Context->codec->long_name);
            in_stream = mp3FormatContext->streams[mp3Index];
        }

        if (mp4Context->codec_type == AVMEDIA_TYPE_VIDEO
            || mp4Context->codec_type == AVMEDIA_TYPE_AUDIO) {
            /* in this example, we choose transcoding to same codec */
            if ( i == 0){
                encoder = avcodec_find_encoder(mp4Context->codec_id);
            } else {
                encoder = avcodec_find_encoder(mp3Context->codec_id);
            }
            if (!encoder) {
                LOGE("Necessary encoder not found\n");
                return AVERROR_INVALIDDATA;
            }
            enc_ctx = avcodec_alloc_context3(encoder);
            if (!enc_ctx) {
                LOGE("Failed to allocate the encoder context\n");
                return AVERROR(ENOMEM);
            }

            /* In this example, we transcode to same properties (picture size,
             * sample rate etc.). These properties can be changed for output
             * streams easily using filters */
            if (i == 0) {
                enc_ctx->height = mp4Context->height;
                enc_ctx->width = mp4Context->width;
                if (mp4Context->sample_aspect_ratio.num == 0){
                    LOGE("can't get sample aspect ratio ,use default");
                    enc_ctx->sample_aspect_ratio = av_make_q(1, 1);
                } else {
                    enc_ctx->sample_aspect_ratio = mp4Context->sample_aspect_ratio;
                }
                /* take first format from list of supported formats */
                if (encoder->pix_fmts)
                    enc_ctx->pix_fmt = encoder->pix_fmts[0];
                else
                    enc_ctx->pix_fmt = mp4Context->pix_fmt;
                /* video time_base can be set to whatever is handy and supported by encoder */
                /* frames per second */
                enc_ctx->time_base = av_inv_q(mp4Context->framerate);
                if (enc_ctx->codec_id == AV_CODEC_ID_MPEG4 && (enc_ctx->time_base.den > 65535 || enc_ctx->time_base.num > 65536)){
                    // the maximum admitted value for the timebase denominator is 65535
                    LOGE("time_base error, time_base-den:%d,time_base:%d,", enc_ctx->time_base.den, enc_ctx->time_base.num);
                    double f = enc_ctx->time_base.den / (double)enc_ctx->time_base.num;
//                    enc_ctx->time_base = av_make_q(65535/f ,65535);`
                    // with a permernant value , not support on upper or #time_increment_bits 15 is invalid in relation to the current bitstream, this is likely caused by a missing VOL header
                    enc_ctx->time_base = av_make_q(2504, 65535);
                    LOGE("New frame rate---time_base-den:%d,time_base:%d,", enc_ctx->time_base.den, enc_ctx->time_base.num);
                }
//                enc_ctx->framerate = dec_ctx->framerate;
//                enc_ctx->bit_rate = dec_ctx->bit_rate;
            } else {
                enc_ctx->sample_rate = mp3Context->sample_rate;
                enc_ctx->channel_layout = mp3Context->channel_layout;
                enc_ctx->channels = av_get_channel_layout_nb_channels(enc_ctx->channel_layout);
                /* take first format from list of supported formats */
                enc_ctx->sample_fmt = encoder->sample_fmts[0];
                enc_ctx->time_base = (AVRational){1, enc_ctx->sample_rate};
            }

            if (outputFormatContext->oformat->flags & AVFMT_GLOBALHEADER)
                enc_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

            /* Third parameter can be used to pass settings to encoder */
            ret = avcodec_open2(enc_ctx, encoder, NULL);
            if (ret < 0) {
                LOGE("Cannot open video encoder for codec #%s, error = %s", encoder->long_name, av_err2str(ret));
                return ret;
            }
            ret = avcodec_parameters_from_context(out_stream->codecpar, enc_ctx);
            if (ret < 0) {
                LOGE("Failed to copy encoder parameters to output stream #%u\n", i);
                return ret;
            }

            out_stream->time_base = enc_ctx->time_base;
        } else if (mp4Context->codec_type == AVMEDIA_TYPE_UNKNOWN) {
            LOGE("Elementary stream #%d is of unknown type, cannot proceed\n", i);
            return AVERROR_INVALIDDATA;
        } else {
            /* if this stream must be remuxed */
            ret = avcodec_parameters_copy(out_stream->codecpar, in_stream->codecpar);
            if (ret < 0) {
                LOGE("Copying parameters for stream #%u failed\n", i);
                return ret;
            }
            out_stream->time_base = in_stream->time_base;
        }

    }
    av_dump_format(outputFormatContext, 0, filename, 1);

    if (!(outputFormatContext->oformat->flags & AVFMT_NOFILE)) {
        ret = avio_open(&outputFormatContext->pb, filename, AVIO_FLAG_WRITE);
        if (ret < 0) {
            LOGE("Could not open output file '%s'", filename);
            return ret;
        }
    }

    /* init muxer, write output file header */
    ret = avformat_write_header(outputFormatContext, NULL);
    if (ret < 0) {
        LOGE("Error occurred when opening output file\n");
        return ret;
    }
    LOGE("FINISH OPEN OUTPUT FILE");
    return 0;
}
//
// Created by wxc on 2019-12-26.
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

static AVFormatContext *ifmt_ctx1;
static AVFormatContext *ifmt_ctx2;
static AVFormatContext *ofmt_ctx;
static AVCodecContext *decCtx1;
static AVCodecContext *encCtx;
static AVCodecContext *decCtx2;

AVFilterContext *buffersink_ctx_merge;
AVFilterContext *buffersrc_ctx_merge;
AVFilterGraph *filter_graph_merge;

static int video_stream_index1 = -1;
static int video_stream_index2 = -1;


int open_input_file(const char *video1, const char *video2);
int open_output_file2(const char *video);
int init_filters_merge(const char *descr);
void encode_write_frame(AVFrame *inputFrame, unsigned int stream_index, int *got_frame, int64_t baseTs, AVRational fromTB);

extern "C" JNIEXPORT void JNICALL
Java_com_watts_myapplication_FFmpegNativeUtils_mergeVideo(JNIEnv *env, jclass clazz,
        jstring video_path1, jstring video_path2,
        jstring output_video, jstring filter) {
    int ret = -1;
    AVPacket packet;
    AVFrame *frame;
    AVFrame *filt_frame;
    int stream_index;
    int64_t endPts = 0;
    int64_t endDts = 0;

    const char *video1 = env->GetStringUTFChars(video_path1, 0);
    const char *video2 = env->GetStringUTFChars(video_path2, 0);
    const char *outputVideo = env->GetStringUTFChars(output_video, 0);
    const char *filterGraph = env->GetStringUTFChars(filter, 0);

    if (open_input_file(video1, video2) < 0){
        LOGE("FILE OPEN FAILED");
    }

    if ((open_output_file2(outputVideo)) < 0){
        LOGE("open output file failed,");
        return;
    }

    if ((init_filters_merge(filterGraph)) < 0){
        LOGE("init filters failed,");
        return;
    }
    /* read all packets */
    while (1) {
        if ((ret = av_read_frame(ifmt_ctx1, &packet)) < 0)
            break;
        LOGE("read pkt, pts = %3" PRId64"", packet.pts);
        stream_index = packet.stream_index;
        if (packet.stream_index == video_stream_index1) {
            ret = avcodec_send_packet(decCtx1, &packet);
            if (ret < 0) {
                LOGE("Error while sending a packet to the decoder\n");
                break;
            }

            while (ret >= 0) {
                frame = av_frame_alloc();
                ret = avcodec_receive_frame(decCtx1, frame);
                LOGE("GOT FRAME ,pts = %3" PRId64" - %d/%d" ,frame->pts, decCtx1->time_base.den, decCtx1->time_base.num);
                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                    break;
                } else if (ret < 0) {
                    LOGE("Error while receiving a frame from the decoder\n");
                    goto end;
                }
                frame->pts = frame->best_effort_timestamp;
                /* push the decoded frame into the filtergraph */
                if (av_buffersrc_add_frame_flags(buffersrc_ctx_merge, frame, AV_BUFFERSRC_FLAG_KEEP_REF) < 0) {
                    LOGE("Error while feeding the filtergraph\n");
                    break;
                }

                /* pull filtered frames from the filtergraph */
                while (1) {
                    filt_frame = av_frame_alloc();
                    ret = av_buffersink_get_frame(buffersink_ctx_merge, filt_frame);
                    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
                        break;
                    if (ret < 0)
                        goto end;
                    filt_frame->pict_type = AV_PICTURE_TYPE_NONE;
                    encode_write_frame(filt_frame, stream_index, NULL, 0, ifmt_ctx1->streams[video_stream_index1]->time_base);
                    av_frame_unref(filt_frame);
                }
                endPts = frame->pts;
                endDts = packet.dts;
                av_frame_unref(frame);
            }
        }
        av_packet_unref(&packet);
    }
    LOGE("FINISH FILE 1 , START FILE 2 ----------------------------------------------");
    // read second video
    while (1) {
        if ((ret = av_read_frame(ifmt_ctx2, &packet)) < 0)
            break;
        //跨过一个timebase , 避免冲突失败
        LOGE("1111 read pkt, pts = %3" PRId64", ts = %f", packet.pts, packet.pts * av_q2d(ifmt_ctx2->streams[video_stream_index2]->time_base));
        packet.pts = endPts + av_rescale_q(packet.pts, ifmt_ctx2->streams[video_stream_index2]->time_base, ifmt_ctx1->streams[video_stream_index1]->time_base) + 1;
        LOGE("2222 read pkt, pts = %3" PRId64", ts = %f", packet.pts, packet.pts * av_q2d(ifmt_ctx2->streams[video_stream_index2]->time_base));
//        packet.dts = endDts + packet.dts;
        stream_index = packet.stream_index;
        if (packet.stream_index == video_stream_index2) {
            ret = avcodec_send_packet(decCtx2, &packet);
            if (ret < 0) {
                LOGE("Error while sending a packet to the decoder\n");
                break;
            }

            while (ret >= 0) {
                frame = av_frame_alloc();
                ret = avcodec_receive_frame(decCtx2, frame);
                LOGE("GOT FRAME ,pts = %3" PRId64" - %d/%d" ,frame->pts, decCtx2->time_base.den, decCtx2->time_base.num);
                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                    break;
                } else if (ret < 0) {
                    LOGE("Error while receiving a frame from the decoder\n");
                    goto end;
                }
                frame->pts = frame->best_effort_timestamp;
                /* push the decoded frame into the filtergraph */
                if (av_buffersrc_add_frame_flags(buffersrc_ctx_merge, frame, AV_BUFFERSRC_FLAG_KEEP_REF) < 0) {
                    LOGE("Error while feeding the filtergraph\n");
                    break;
                }

                /* pull filtered frames from the filtergraph */
                while (1) {
                    filt_frame = av_frame_alloc();
                    ret = av_buffersink_get_frame(buffersink_ctx_merge, filt_frame);
                    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
                        break;
                    if (ret < 0)
                        goto end;
                    filt_frame->pict_type = AV_PICTURE_TYPE_NONE;
                    encode_write_frame(filt_frame, stream_index, NULL, endPts, ifmt_ctx1->streams[video_stream_index1]->time_base);
                    av_frame_unref(filt_frame);
                }
                av_frame_unref(frame);
            }
        }
        av_packet_unref(&packet);
    }
    LOGE("FINISH 2 FILE ");

    av_write_trailer(ofmt_ctx);

    end:
    avfilter_graph_free(&filter_graph_merge);
    avcodec_free_context(&decCtx1);
    avformat_close_input(&ifmt_ctx1);
    avcodec_free_context(&decCtx2);
    avformat_close_input(&ifmt_ctx2);
    av_frame_free(&frame);
    av_frame_free(&filt_frame);

    if (ret < 0 && ret != AVERROR_EOF) {
        LOGE("Error occurred: %s\n", av_err2str(ret));
        return;
    }
    LOGE("SUCCESS ALL");
}

int open_input_file(const char *filename1, const char *filename2)
{
    int ret;
    AVCodec *dec;

    if ((ret = avformat_open_input(&ifmt_ctx1, filename1, NULL, NULL)) < 0) {
        LOGE("Cannot open input file\n");
        return ret;
    }

    if ((ret = avformat_find_stream_info(ifmt_ctx1, NULL)) < 0) {
        LOGE("Cannot find stream information\n");
        return ret;
    }

    /* select the video stream */
    ret = av_find_best_stream(ifmt_ctx1, AVMEDIA_TYPE_VIDEO, -1, -1, &dec, 0);
    if (ret < 0) {
        LOGE("Cannot find a video stream in the input file\n");
        return ret;
    }
    video_stream_index1 = ret;

    /* create decoding context */
    decCtx1 = avcodec_alloc_context3(dec);
    if (!decCtx1)
        return AVERROR(ENOMEM);
    avcodec_parameters_to_context(decCtx1, ifmt_ctx1->streams[video_stream_index1]->codecpar);
    decCtx1->framerate = av_guess_frame_rate(ifmt_ctx1, ifmt_ctx1->streams[video_stream_index1], NULL);

    /* init the video decoder */
    if ((ret = avcodec_open2(decCtx1, dec, NULL)) < 0) {
        LOGE("Cannot open video decoder\n");
        return ret;
    }
    av_dump_format(ifmt_ctx1, 0, filename1, 0);

    if ((ret = avformat_open_input(&ifmt_ctx2, filename2, NULL, NULL)) < 0) {
        LOGE("Cannot open input file\n");
        return ret;
    }

    if ((ret = avformat_find_stream_info(ifmt_ctx2, NULL)) < 0) {
        LOGE("Cannot find stream information\n");
        return ret;
    }

    /* select the video stream */
    ret = av_find_best_stream(ifmt_ctx2, AVMEDIA_TYPE_VIDEO, -1, -1, &dec, 0);
    if (ret < 0) {
        LOGE("Cannot find a video stream in the input file\n");
        return ret;
    }
    video_stream_index2 = ret;

    /* create decoding context */
    decCtx2 = avcodec_alloc_context3(dec);
    if (!decCtx2)
        return AVERROR(ENOMEM);
    avcodec_parameters_to_context(decCtx2, ifmt_ctx2->streams[video_stream_index2]->codecpar);
    decCtx2->framerate = av_guess_frame_rate(ifmt_ctx2, ifmt_ctx2->streams[video_stream_index2], NULL);

    /* init the video decoder */
    if ((ret = avcodec_open2(decCtx2, dec, NULL)) < 0) {
        LOGE("Cannot open video decoder\n");
        return ret;
    }
    av_dump_format(ifmt_ctx2, 0, filename2, 0);
    LOGE("FINISH OPEN INPUT FILE");
    return 0;
}

int open_output_file2(const char *filename){
    AVStream *out_stream;
    AVStream *in_stream;
    AVCodecContext *enc_ctx;
    AVCodec *encoder;
    int ret;
    unsigned int i;

    ofmt_ctx = NULL;
    avformat_alloc_output_context2(&ofmt_ctx, NULL, NULL, filename);
    if (!ofmt_ctx) {
        LOGE("Could not create output context\n");
        return AVERROR_UNKNOWN;
    }

    for (i = 0; i < ifmt_ctx1->nb_streams; i++) {
        out_stream = avformat_new_stream(ofmt_ctx, NULL);
        if (!out_stream) {
            LOGE("Failed allocating output stream\n");
            return AVERROR_UNKNOWN;
        }

        in_stream = ifmt_ctx1->streams[i];

        if (decCtx1->codec_type == AVMEDIA_TYPE_VIDEO
            || decCtx1->codec_type == AVMEDIA_TYPE_AUDIO) {
            /* in this example, we choose transcoding to same codec */
            encoder = avcodec_find_encoder(AV_CODEC_ID_MPEG4);
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
            if (decCtx1->codec_type == AVMEDIA_TYPE_VIDEO) {
                enc_ctx->height = decCtx1->height;
                enc_ctx->width = decCtx1->width;
                if (decCtx1->sample_aspect_ratio.num == 0){
                    LOGE("can't get sample aspect ratio ,use default");
                    enc_ctx->sample_aspect_ratio = av_make_q(1, 1);
                } else {
                    enc_ctx->sample_aspect_ratio = decCtx1->sample_aspect_ratio;
                }
                /* take first format from list of supported formats */
                if (encoder->pix_fmts)
                    enc_ctx->pix_fmt = encoder->pix_fmts[0];
                else
                    enc_ctx->pix_fmt = decCtx1->pix_fmt;
                /* video time_base can be set to whatever is handy and supported by encoder */
                /* frames per second */
                enc_ctx->time_base = av_inv_q(decCtx1->framerate);
                if (enc_ctx->codec_id == AV_CODEC_ID_MPEG4 && (enc_ctx->time_base.den > 65535 || enc_ctx->time_base.num > 65536)){
                    // the maximum admitted value for the timebase denominator is 65535
                    LOGE("time_base error, time_base-den:%d,time_base:%d,", enc_ctx->time_base.den, enc_ctx->time_base.num);
                    double f = enc_ctx->time_base.den / (double)enc_ctx->time_base.num;
                    enc_ctx->time_base = av_make_q(65535/f ,65535);
                    LOGE("New frame rate---time_base-den:%d,time_base:%d,", enc_ctx->time_base.den, enc_ctx->time_base.num);
                }
//                enc_ctx->framerate = dec_ctx->framerate;
//                enc_ctx->bit_rate = dec_ctx->bit_rate;
            } else {
                enc_ctx->sample_rate = decCtx1->sample_rate;
                enc_ctx->channel_layout = decCtx1->channel_layout;
                enc_ctx->channels = av_get_channel_layout_nb_channels(enc_ctx->channel_layout);
                /* take first format from list of supported formats */
                enc_ctx->sample_fmt = encoder->sample_fmts[0];
                enc_ctx->time_base = (AVRational){1, enc_ctx->sample_rate};
            }

            if (ofmt_ctx->oformat->flags & AVFMT_GLOBALHEADER)
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
            encCtx = enc_ctx;
        } else if (decCtx1->codec_type == AVMEDIA_TYPE_UNKNOWN) {
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
    av_dump_format(ofmt_ctx, 0, filename, 1);

    if (!(ofmt_ctx->oformat->flags & AVFMT_NOFILE)) {
        ret = avio_open(&ofmt_ctx->pb, filename, AVIO_FLAG_WRITE);
        if (ret < 0) {
            LOGE("Could not open output file '%s'", filename);
            return ret;
        }
    }

    /* init muxer, write output file header */
    ret = avformat_write_header(ofmt_ctx, NULL);
    if (ret < 0) {
        LOGE("Error occurred when opening output file\n");
        return ret;
    }
    LOGE("FINISH OPEN OUTPUT FILE");
    return 0;
}

//use one filter, If for different video, we may use different filter, This is just for Test.
//In some case , Wo may don't need decode/encode video for merging.
int init_filters_merge(const char *filters_descr)
{
    char args[512];
    int ret = 0;
    const AVFilter *buffersrc  = avfilter_get_by_name("buffer");
    const AVFilter *buffersink = avfilter_get_by_name("buffersink");
    AVFilterInOut *outputs = avfilter_inout_alloc();
    AVFilterInOut *inputs  = avfilter_inout_alloc();
    AVRational time_base = ifmt_ctx1->streams[video_stream_index1]->time_base;
    enum AVPixelFormat pix_fmts[] = { AV_PIX_FMT_GRAY8, AV_PIX_FMT_NONE };

    filter_graph_merge = avfilter_graph_alloc();
    if (!outputs || !inputs || !filter_graph_merge) {
        ret = AVERROR(ENOMEM);
        goto end;
    }

    /* buffer video source: the decoded frames from the decoder will be inserted here. */
    snprintf(args, sizeof(args),
             "video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=%d/%d",
             decCtx1->width, decCtx1->height, decCtx1->pix_fmt,
             time_base.num, time_base.den,
             decCtx1->sample_aspect_ratio.num, decCtx1->sample_aspect_ratio.den);

    ret = avfilter_graph_create_filter(&buffersrc_ctx_merge, buffersrc, "in",
                                       args, NULL, filter_graph_merge);
    if (ret < 0) {
        LOGE("Cannot create buffer source\n");
        goto end;
    }

    /* buffer video sink: to terminate the filter chain. */
    ret = avfilter_graph_create_filter(&buffersink_ctx_merge, buffersink, "out",
                                       NULL, NULL, filter_graph_merge);
    if (ret < 0) {
        LOGE("Cannot create buffer sink\n");
        goto end;
    }

//    ret = av_opt_set_int_list(buffersink_ctx_merge, "pix_fmts", pix_fmts,
//                              AV_PIX_FMT_NONE, AV_OPT_SEARCH_CHILDREN);
    ret = av_opt_set_bin(buffersink_ctx_merge, "pix_fmts",
                         (uint8_t*)&encCtx->pix_fmt, sizeof(encCtx->pix_fmt),
                         AV_OPT_SEARCH_CHILDREN);

    if (ret < 0) {
        LOGE("Cannot set output pixel format\n");
        goto end;
    }

    /*
     * Set the endpoints for the filter graph. The filter_graph_merge will
     * be linked to the graph described by filters_descr.
     */

    /*
     * The buffer source output must be connected to the input pad of
     * the first filter described by filters_descr; since the first
     * filter input label is not specified, it is set to "in" by
     * default.
     */
    outputs->name       = av_strdup("in");
    outputs->filter_ctx = buffersrc_ctx_merge;
    outputs->pad_idx    = 0;
    outputs->next       = NULL;

    /*
     * The buffer sink input must be connected to the output pad of
     * the last filter described by filters_descr; since the last
     * filter output label is not specified, it is set to "out" by
     * default.
     */
    inputs->name       = av_strdup("out");
    inputs->filter_ctx = buffersink_ctx_merge;
    inputs->pad_idx    = 0;
    inputs->next       = NULL;

    if ((ret = avfilter_graph_parse_ptr(filter_graph_merge, filters_descr,
                                        &inputs, &outputs, NULL)) < 0)
        goto end;

    if ((ret = avfilter_graph_config(filter_graph_merge, NULL)) < 0)
        goto end;

    end:
    avfilter_inout_free(&inputs);
    avfilter_inout_free(&outputs);

    LOGE("FINISH INIT FILTER, result = %d",ret);
    return ret;
}

void encode_write_frame(AVFrame *inputFrame, unsigned int stream_index, int *got_frame, int64_t baseTs, AVRational fromTB){
    int ret;
    AVPacket *pkt;


    pkt = av_packet_alloc();
    if (!pkt) {
        LOGE("PKT is null return");
        return;
    }
    ret = avcodec_send_frame(encCtx, inputFrame);
    if (ret < 0){
        LOGE("encode frame fail --- error = %s", av_err2str(ret));
    }
    while (ret >= 0) {
        ret = avcodec_receive_packet(encCtx, pkt);
        LOGE("get PACKET");
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            LOGE("Error during encoding %s",av_err2str(ret));
            return;
        } else if (ret < 0) {
            LOGE("Error during encoding %s",av_err2str(ret));
        }

        (*pkt).stream_index = stream_index;
        av_packet_rescale_ts(pkt,
                             fromTB,
                             ofmt_ctx->streams[stream_index]->time_base);
        LOGE("START WRITE PACKET,  pts = %3" PRId64" ，pkt.pts seconds = %f================================================", pkt->pts, pkt->pts * av_q2d(ofmt_ctx->streams[stream_index]->time_base));
        ret = av_interleaved_write_frame(ofmt_ctx, pkt);
        if(ret < 0){
            LOGE("write pkt fail\n");
        }
        av_packet_unref(pkt);
    }

    av_packet_free(&pkt);
}
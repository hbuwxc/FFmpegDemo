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

static AVFormatContext *ifmt_ctx;
static AVFormatContext *ofmt_ctx;
static AVCodecContext *decCtx;
static AVCodecContext *encCtx;
AVFilterContext *buffersink_ctx;
AVFilterContext *buffersrc_ctx;
AVFilterGraph *filter_graph;
static int video_stream_index = -1;
static bool enableCut = true;
int64_t pts_start = -1;
int64_t dts_start = -1;

int open_input_file(const char *path);

int open_output_file(const char *path);

int init_filters(const char *descr);

void encode_write_frame(AVFrame *inputFrame, unsigned int stream_index, int *got_frame);

void encode(AVCodecContext *enc_ctx, AVFrame *frame, AVPacket *pkt,
                   FILE *outfile)
;

extern "C" JNIEXPORT jstring JNICALL
Java_com_watts_myapplication_MainActivity_stringFromJNI(
        JNIEnv* env,
        jobject /* this */) {
    std::string hello = "Hello from C++";
    LOGE("this is ndk log.---%s",avcodec_configuration());


    return env->NewStringUTF(hello.c_str());
}

extern "C"
JNIEXPORT void JNICALL
Java_com_watts_myapplication_FFmpegNativeUtils_filterVideo(JNIEnv *env, jclass clazz,
                                                           jstring video_path, jstring filter_path, jstring filter) {
    int ret;
    AVPacket packet;
    AVFrame *frame;
    AVFrame *filt_frame;
    int stream_index;

    const char *filePath = env->GetStringUTFChars(video_path, 0);
    const char *filterPath = env->GetStringUTFChars(filter_path, 0);
    const char *filterGraph = env->GetStringUTFChars(filter, 0);
    LOGE("get file path = %s", filePath);

//    if (!frame || !filt_frame) {
//        LOGE("Could not allocate frame");
//        return;
//    }

    if ((ret = open_input_file(filePath)) < 0){
        LOGE("open file failed,");
        return;
    }

    if ((ret = open_output_file(filterPath)) < 0){
        LOGE("open output file failed,");
        return;
    }

    if ((ret = init_filters(filterGraph)) < 0){
        LOGE("init filters failed,");
        return;
    }

    if(enableCut) {
        ret = av_seek_frame(ifmt_ctx, -1, 5 * AV_TIME_BASE, AVSEEK_FLAG_ANY);
        if (ret < 0) {
            LOGE("Error seek: %s", av_err2str(ret));
            return;
        }
    }
    /* read all packets */
    while (1) {
        if ((ret = av_read_frame(ifmt_ctx, &packet)) < 0)
            break;
        if (pts_start == -1){
            pts_start = packet.pts;
        }
        if (dts_start == -1){
            dts_start = packet.dts;
        }
        if (enableCut){
            /* copy packet */
            packet.pts = packet.pts - pts_start;
            packet.dts = packet.dts - dts_start;
            if (packet.pts < 0) {
                packet.pts = 0;
            }
            if (packet.dts < 0) {
                packet.dts = 0;
            }
//            packet.pos = -1;
        }
        LOGE("read pkt, pts = %3" PRId64"", packet.pts);
        stream_index = packet.stream_index;
        if (packet.stream_index == video_stream_index) {
//            LOGE("first scale from %d/%d to %d/%d",ifmt_ctx->streams[stream_index]->time_base.num,ifmt_ctx->streams[stream_index]->time_base.den,decCtx->time_base.num,decCtx->time_base.den);
//            av_packet_rescale_ts(&packet,
//                                 ifmt_ctx->streams[stream_index]->time_base,
//                                 decCtx->time_base);
            ret = avcodec_send_packet(decCtx, &packet);
            if (ret < 0) {
                LOGE("Error while sending a packet to the decoder\n");
                break;
            }

            while (ret >= 0) {
                frame = av_frame_alloc();
                ret = avcodec_receive_frame(decCtx, frame);
                LOGE("GOT FRAME ,pts = %3" PRId64" - %d/%d" ,frame->pts, decCtx->time_base.den, decCtx->time_base.num);
                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                    break;
                } else if (ret < 0) {
                    LOGE("Error while receiving a frame from the decoder\n");
                    goto end;
                }
                LOGE("START FRAME filter_graph, pts = %3" PRId64", beset effort ts = %3" PRId64"", frame->pts, frame->best_effort_timestamp);
                frame->pts = frame->best_effort_timestamp;
                /* push the decoded frame into the filtergraph */
                if (av_buffersrc_add_frame_flags(buffersrc_ctx, frame, AV_BUFFERSRC_FLAG_KEEP_REF) < 0) {
                    LOGE("Error while feeding the filtergraph\n");
                    break;
                }

                /* pull filtered frames from the filtergraph */
                while (1) {
                    filt_frame = av_frame_alloc();
                    ret = av_buffersink_get_frame(buffersink_ctx, filt_frame);
                    LOGE("RECEIVE FRAME filter_graph ,ret = %d，%d,%d", ret, AVERROR(EAGAIN), AVERROR_EOF);
                    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
                        break;
                    if (ret < 0)
                        goto end;
                    filt_frame->pict_type = AV_PICTURE_TYPE_NONE;
                    encode_write_frame(filt_frame, stream_index, NULL);
                    av_frame_unref(filt_frame);
                }
                av_frame_unref(frame);
            }
        }
        av_packet_unref(&packet);
    }
    av_write_trailer(ofmt_ctx);

    end:
    avfilter_graph_free(&filter_graph);
    avcodec_free_context(&decCtx);
    avformat_close_input(&ifmt_ctx);
    av_frame_free(&frame);
    av_frame_free(&filt_frame);

    if (ret < 0 && ret != AVERROR_EOF) {
        LOGE("Error occurred: %s\n", av_err2str(ret));
        return;
    }
    LOGE("SUCCESS ALL");
}


int open_input_file(const char *filename)
{
    int ret;
    AVCodec *dec;

    if ((ret = avformat_open_input(&ifmt_ctx, filename, NULL, NULL)) < 0) {
        LOGE("Cannot open input file\n");
        return ret;
    }

    if ((ret = avformat_find_stream_info(ifmt_ctx, NULL)) < 0) {
        LOGE("Cannot find stream information\n");
        return ret;
    }

    /* select the video stream */
    ret = av_find_best_stream(ifmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, &dec, 0);
    if (ret < 0) {
        LOGE("Cannot find a video stream in the input file\n");
        return ret;
    }
    video_stream_index = ret;

    /* create decoding context */
    decCtx = avcodec_alloc_context3(dec);
    if (!decCtx)
        return AVERROR(ENOMEM);
    avcodec_parameters_to_context(decCtx, ifmt_ctx->streams[video_stream_index]->codecpar);
    decCtx->framerate = av_guess_frame_rate(ifmt_ctx, ifmt_ctx->streams[video_stream_index], NULL);

    /* init the video decoder */
    if ((ret = avcodec_open2(decCtx, dec, NULL)) < 0) {
        LOGE("Cannot open video decoder\n");
        return ret;
    }
    av_dump_format(ifmt_ctx, 0, filename, 0);
    LOGE("FINISH OPEN INPUT FILE");
    return 0;
}


int open_output_file(const char *filename)
{
    AVStream *out_stream;
    AVStream *in_stream;
    AVCodecContext *dec_ctx, *enc_ctx;
    AVCodec *encoder;
    int ret;
    unsigned int i;

    ofmt_ctx = NULL;
    avformat_alloc_output_context2(&ofmt_ctx, NULL, NULL, filename);
    if (!ofmt_ctx) {
        LOGE("Could not create output context\n -- %d",ifmt_ctx->nb_streams);
        return AVERROR_UNKNOWN;
    }

    for (i = 0; i < ifmt_ctx->nb_streams; i++) {
        out_stream = avformat_new_stream(ofmt_ctx, NULL);
        if (!out_stream) {
            LOGE("Failed allocating output stream\n");
            return AVERROR_UNKNOWN;
        }

        in_stream = ifmt_ctx->streams[i];
        dec_ctx = decCtx;

        if (dec_ctx->codec_type == AVMEDIA_TYPE_VIDEO
            || dec_ctx->codec_type == AVMEDIA_TYPE_AUDIO) {
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
            if (dec_ctx->codec_type == AVMEDIA_TYPE_VIDEO) {
                enc_ctx->height = dec_ctx->height;
                enc_ctx->width = dec_ctx->width;
                enc_ctx->sample_aspect_ratio = dec_ctx->sample_aspect_ratio;
                /* take first format from list of supported formats */
                if (encoder->pix_fmts)
                    enc_ctx->pix_fmt = encoder->pix_fmts[0];
                else
                    enc_ctx->pix_fmt = dec_ctx->pix_fmt;
                /* video time_base can be set to whatever is handy and supported by encoder */
                /* frames per second */
                enc_ctx->time_base = av_inv_q(dec_ctx->framerate);
//                enc_ctx->framerate = dec_ctx->framerate;
//                enc_ctx->bit_rate = dec_ctx->bit_rate;
            } else {
                enc_ctx->sample_rate = dec_ctx->sample_rate;
                enc_ctx->channel_layout = dec_ctx->channel_layout;
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
                LOGE("Cannot open video encoder for stream #%u\n, error = %s", i, av_err2str(ret));
                return ret;
            }
            ret = avcodec_parameters_from_context(out_stream->codecpar, enc_ctx);
            if (ret < 0) {
                LOGE("Failed to copy encoder parameters to output stream #%u\n", i);
                return ret;
            }

            out_stream->time_base = enc_ctx->time_base;
            encCtx = enc_ctx;
        } else if (dec_ctx->codec_type == AVMEDIA_TYPE_UNKNOWN) {
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

int init_filters(const char *filters_descr)
{
    char args[512];
    int ret = 0;
    const AVFilter *buffersrc  = avfilter_get_by_name("buffer");
    const AVFilter *buffersink = avfilter_get_by_name("buffersink");
    AVFilterInOut *outputs = avfilter_inout_alloc();
    AVFilterInOut *inputs  = avfilter_inout_alloc();
    AVRational time_base = ifmt_ctx->streams[video_stream_index]->time_base;
    enum AVPixelFormat pix_fmts[] = { AV_PIX_FMT_GRAY8, AV_PIX_FMT_NONE };

    filter_graph = avfilter_graph_alloc();
    if (!outputs || !inputs || !filter_graph) {
        ret = AVERROR(ENOMEM);
        goto end;
    }

    /* buffer video source: the decoded frames from the decoder will be inserted here. */
    snprintf(args, sizeof(args),
             "video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=%d/%d",
             decCtx->width, decCtx->height, decCtx->pix_fmt,
             time_base.num, time_base.den,
             decCtx->sample_aspect_ratio.num, decCtx->sample_aspect_ratio.den);

    ret = avfilter_graph_create_filter(&buffersrc_ctx, buffersrc, "in",
                                       args, NULL, filter_graph);
    if (ret < 0) {
        LOGE("Cannot create buffer source\n");
        goto end;
    }

    /* buffer video sink: to terminate the filter chain. */
    ret = avfilter_graph_create_filter(&buffersink_ctx, buffersink, "out",
                                       NULL, NULL, filter_graph);
    if (ret < 0) {
        LOGE("Cannot create buffer sink\n");
        goto end;
    }

//    ret = av_opt_set_int_list(buffersink_ctx, "pix_fmts", pix_fmts,
//                              AV_PIX_FMT_NONE, AV_OPT_SEARCH_CHILDREN);
    ret = av_opt_set_bin(buffersink_ctx, "pix_fmts",
                         (uint8_t*)&encCtx->pix_fmt, sizeof(encCtx->pix_fmt),
                         AV_OPT_SEARCH_CHILDREN);

    if (ret < 0) {
        LOGE("Cannot set output pixel format\n");
        goto end;
    }

    /*
     * Set the endpoints for the filter graph. The filter_graph will
     * be linked to the graph described by filters_descr.
     */

    /*
     * The buffer source output must be connected to the input pad of
     * the first filter described by filters_descr; since the first
     * filter input label is not specified, it is set to "in" by
     * default.
     */
    outputs->name       = av_strdup("in");
    outputs->filter_ctx = buffersrc_ctx;
    outputs->pad_idx    = 0;
    outputs->next       = NULL;

    /*
     * The buffer sink input must be connected to the output pad of
     * the last filter described by filters_descr; since the last
     * filter output label is not specified, it is set to "out" by
     * default.
     */
    inputs->name       = av_strdup("out");
    inputs->filter_ctx = buffersink_ctx;
    inputs->pad_idx    = 0;
    inputs->next       = NULL;

    if ((ret = avfilter_graph_parse_ptr(filter_graph, filters_descr,
                                        &inputs, &outputs, NULL)) < 0)
        goto end;

    if ((ret = avfilter_graph_config(filter_graph, NULL)) < 0)
        goto end;

    end:
    avfilter_inout_free(&inputs);
    avfilter_inout_free(&outputs);

    LOGE("FINISH INIT FILTER, result = %d",ret);
    return ret;
}

void encode_write_frame(AVFrame *inputFrame, unsigned int stream_index, int *got_frame){
    int ret;
    AVPacket *pkt;


    pkt = av_packet_alloc();
    if (!pkt)
        return;
    LOGE("START FRAME ENCODE pts = %3" PRId64" - %d/%d",inputFrame->pts, decCtx->time_base.den, decCtx->time_base.num);
    ret = avcodec_send_frame(encCtx, inputFrame);
    while (ret >= 0) {
        ret = avcodec_receive_packet(encCtx, pkt);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            LOGE("Error during encoding %s",av_err2str(ret));
            return;
        } else if (ret < 0) {
            LOGE("Error during encoding %s",av_err2str(ret));
        }

        (*pkt).stream_index = stream_index;
        //TODO 时间scale 有问题，后续研究。 理论上应该做时间转换, AVFilter处理后， pkt 的pts 和 dts 会使用AvCodecContext的timebase，需rescale为AVStream的。
//        LOGE("---------------");
//        LOGE("START WRITE PACKAGE pts = %3" PRId64" - %d/%d",pkt->pts, encCtx->time_base.den, encCtx->time_base.num);
//        av_packet_rescale_ts(pkt,
//                             encCtx->time_base,
//                             ofmt_ctx->streams[stream_index]->time_base);
//        LOGE("START WRITE PACKAGE pts = %3" PRId64" - %d/%d",pkt->pts, ofmt_ctx->streams[stream_index]->time_base.den, ofmt_ctx->streams[stream_index]->time_base.num);
        ret = av_interleaved_write_frame(ofmt_ctx, pkt);
        if(ret < 0){
            LOGE("write pkt fail\n");
        }
        av_packet_unref(pkt);
    }

    av_packet_free(&pkt);
}
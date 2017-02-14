//
// Created by righere on 16-12-11.
//

#include "stdio.h"
#include "stdbool.h"

#ifdef __ANDROID__

#include <jni.h>
#include <android/log.h>
//使用NDK的log
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR,"ERROR: ", __VA_ARGS__)
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,"INFO: ", __VA_ARGS__)
#else
#define LOGE(format, ...) printf("ERROR: " format "\n",###__VA_ARGS__)
#define LOGI(format, ...) printf("INFO: " format "\n",###__VA_ARGS__)
#endif



#include <libavformat/avformat.h>
#include <src/thread/stdcpp/SDL_sysmutex_c.h>
#include <SDL_mutex.h>
#include <jni.h>
#include <SDL_thread.h>
#include <SDL_video.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
#include <libavutil/avstring.h>
#include "SDL.h"

extern "C"

#define BUFFER_FMT_YUV 0
#define VIDEO_FRAME_QUEUE_SIZE 1
#define SDL_AUDIO_BUFFER_SIZE 1024
#define MAX_AUDIO_FRAME_SIZE 192000

#define MAX_AUDIOQ_SIZE (5 * 16 * 1024)

#define FF_ALLOC_EVENT   (SDL_USEREVENT)
#define FF_REFRESH_EVENT (SDL_USEREVENT + 1)
#define FF_QUIT_EVENT (SDL_USEREVENT + 2)

typedef struct PacketQueue{
    AVPacketList *front_pktL,*rear_pktL;
    int nb_packets;    //number of packets
    int size;
    SDL_mutex *mutex;
    SDL_cond *cond;
}PacketQueue;

typedef struct VideoFrame{
    AVFrame *pFrame;
    int width,height;
    bool allocated;
}VideoFrame;

typedef struct VideoPara{
    //FFmpeg Parameters
    AVFormatContext *pFormatCtx;
    AVCodecContext  *pCodecCtx;
    int             i, streamIdx;

    //video

    PacketQueue     videoQ;

    AVStream        *video_st;
    AVFrame         *pFrame_out;
    //frame
    VideoFrame videoFrame[VIDEO_FRAME_QUEUE_SIZE];
    int frameQ_size,frameQ_idx_front,frameQ_idx_rear;

    //audio
    PacketQueue     audioq;

    AVPacket        audio_pkt;
    AVStream        *audio_st;
    AVCodecContext  *audio_ctx;
    uint8_t         audio_buf[(MAX_AUDIO_FRAME_SIZE * 3) / 2];
    unsigned int    audio_buf_size;
    unsigned int    audio_buf_index;
    AVFrame         audio_frame;
    uint8_t         *audio_pkt_data;
    int             audio_pkt_size;

    //size buffer
    uint8_t *out_buffer;

    struct SwsContext *img_convert_ctx;

    //Thread


    SDL_mutex *sdlMutex;
    SDL_cond *sdlCond;

    SDL_Thread *decodeThread;
    SDL_Thread *displayThread;

    bool quit;

//SDL Parameters
    SDL_Window     *sdlWindow;
    SDL_Texture    *sdlTexture;
    SDL_Rect sdlRect;
    SDL_Renderer   *renderer;
    SDL_Thread  *sdl_thread;
    SDL_Event       event;

    //获取文件名
    char mediaUri[1024];
}VideoPara;

VideoPara *videoPara_global;

static Uint32 sdl_refresh_timer_cb(Uint32 interval, void *opaque){
    SDL_Event event;
    event.type = FF_REFRESH_EVENT;
    event.user.data1 = opaque;
    SDL_PushEvent(&event);
    return 0;
}

static void schedule_refresh(VideoPara *videoPara, Uint32 delay) {
    SDL_AddTimer(delay, sdl_refresh_timer_cb, videoPara);
}

void packet_queue_init(PacketQueue *q){
    memset(q, 0, sizeof(PacketQueue));

    q->mutex = SDL_CreateMutex();
    q->cond = SDL_CreateCond();
}

int packet_queue_put(PacketQueue *pktQ, AVPacket *packet){
    AVPacketList *pktList_input;
    AVPacket *packet_input = av_packet_alloc();
    if(av_packet_ref(packet_input, packet) < 0){

        LOGE("packetQ_put: input packet is error!");
        return  -1;
    }

    pktList_input = av_malloc(sizeof(AVPacketList));

    pktList_input->pkt = *packet_input;
    pktList_input->next = NULL;

    SDL_LockMutex(pktQ->mutex);

    //向列队中传入新来的pktList,存到队尾
    if(!pktQ->rear_pktL){
        pktQ->front_pktL = pktList_input;
    }
    else{
        pktQ->rear_pktL->next = pktList_input;
    }
    pktQ->rear_pktL = pktList_input;

    pktQ->nb_packets++;
    pktQ->size += pktList_input->pkt.size;

    SDL_CondSignal(pktQ->cond);

    SDL_UnlockMutex(pktQ->mutex);

    return 0;
}

int packet_queue_get(PacketQueue *pktQ,AVPacket *packet, bool block){
    AVPacketList *pktL;

    int flag;

    SDL_LockMutex(pktQ->mutex);

    while(true){
        if(videoPara_global->quit){
            return -1;
            LOGE("PacketQueue: quit");
        }

        pktL = pktQ->front_pktL;
        if (pktL){
            pktQ->front_pktL = pktL->next;
            pktQ->nb_packets --;
            pktQ->size -= pktL->pkt.size;

            *packet = pktL->pkt;

            av_free(pktL);
            flag = 1;
            break;
        }
        else if(!block){
            flag = 0;
            break;
        }
        else{
            SDL_CondWait(pktQ->cond,pktQ->mutex);
        }
    }

    SDL_UnlockMutex(pktQ->mutex);

    return flag;
}

int audio_decode_frame(VideoPara *is) {
    int data_size = 0;
    AVPacket *audioPkt = av_packet_alloc();
    AVFrame *audioFrame = av_frame_alloc();

    for(;;) {
        while(is->audio_pkt_size > 0) {
            int send_audio_packet = avcodec_send_packet(is->audio_ctx,audioPkt);

            if(send_audio_packet != 0 && send_audio_packet != AVERROR_EOF && send_audio_packet != AVERROR(EAGAIN))
            return -1;

            int receive_audio_frame = avcodec_receive_frame(is->audio_ctx,audioFrame);

            if(receive_audio_frame != 0 && receive_audio_frame != AVERROR_EOF && receive_audio_frame != AVERROR(EAGAIN))
                return -1;

            if(receive_audio_frame < 0) {
                /* if error, skip frame */
                is->audio_pkt_size = 0;
                break;
            }
            if (receive_audio_frame == 0)
            {
                data_size =
                        av_samples_get_buffer_size
                                (
                                        NULL,
                                        is->audio_st->codecpar->channels,
                                        is->audio_frame.nb_samples,
                                        is->audio_ctx->sample_fmt,
                                        1
                                );
                memcpy(is->audio_buf, is->audio_frame.data[0], data_size);
            }
            is->audio_pkt_data += len1;
            is->audio_pkt_size -= len1;
            if(data_size <= 0) {
                /* No data yet, get more frames */
                continue;
            }
            /* We have data, return it and come back for more later */
            return data_size;
        }
        if(pkt->data)
            av_free_packet(pkt);

        if(is->quit) {
            return -1;
        }
        /* next packet */
        if(packet_queue_get(&is->audioq, pkt, 1) < 0) {
            return -1;
        }
        is->audio_pkt_data = pkt->data;
        is->audio_pkt_size = pkt->size;
    }
}

void audio_callback(void *userdata, Uint8 *stream, int len) {

    VideoState *is = (VideoState *)userdata;
    int len1, audio_size;

    while(len > 0) {
        if(is->audio_buf_index >= is->audio_buf_size) {
            /* We have already sent all our data; get more */
            audio_size = audio_decode_frame(is);
            if(audio_size < 0) {
                /* If error, output silence */
                is->audio_buf_size = 1024;
                memset(is->audio_buf, 0, is->audio_buf_size);
            } else {
                is->audio_buf_size = audio_size;
            }
            is->audio_buf_index = 0;
        }
        len1 = is->audio_buf_size - is->audio_buf_index;
        if(len1 > len)
            len1 = len;
        memcpy(stream, (uint8_t *)is->audio_buf + is->audio_buf_index, len1);
        len -= len1;
        stream += len1;
        is->audio_buf_index += len1;
    }
}

void Videodisplay(VideoPara *input, SDL_Renderer *sdlRenderer, SDL_Texture *sdlTexture){

    //默认全屏显示，就不需要
    SDL_Rect sdlRect;

    VideoFrame *vf;
    
    AVFrame *pFrame, *pFrame_out;

    sdlRect = input->sdlRect;
    vf = &input->videoFrame[input->frameQ_idx_front];

    pFrame = vf->pFrame;
    pFrame_out = input->pFrame_out;

    AVCodecContext *pCodecCtx = input->pCodecCtx;

    int getFrameCode = avcodec_receive_frame(pCodecCtx, pFrame);
//                LOGI("%d", getFrameCode);
    // Did we get a video frame?
    if (getFrameCode == 0) {
        LOGI("%s", "Got Video Frame Succeed");

        //scale Frame
        sws_scale(input->img_convert_ctx, (const uint8_t *const *) pFrame->data,
                  pFrame->linesize, 0, pFrame->height,
                  pFrame_out->data, pFrame_out->linesize);
#if BUFFER_FMT_YUV

        SDL_UpdateYUVTexture(sdlTexture, NULL, pFrame_out->data[0], pFrame_out->linesize[0],
                                         pFrame_out->data[1],pFrame_out->linesize[1],
                                         pFrame_out->data[2],pFrame_out->linesize[2]);
#else
        SDL_UpdateTexture(sdlTexture,NULL,pFrame_out->data[0],pCodecCtx->width*4);
#endif

        SDL_RenderClear(sdlRenderer);
        SDL_RenderCopy(sdlRenderer, sdlTexture, NULL, NULL);
        SDL_RenderPresent(sdlRenderer);

        //Delay 40ms
        SDL_Delay(40);
    }
    else if (getFrameCode == AVERROR(EAGAIN)) {
        LOGE("%s", "Frame is not available right now,please try another input");
    }
    else if (getFrameCode == AVERROR_EOF) {
        LOGE("%s", "the decoder has been fully flushed");
    }
    else if (getFrameCode == AVERROR(EINVAL)) {
        LOGE("%s", "codec not opened, or it is an encoder");
    }
    else {
        LOGI("%s", "legitimate decoding errors");
    }
}

void Video_fresh_timer(void *usrData, SDL_Renderer *sdlRenderer, SDL_Texture *sdlTexture){
    VideoPara *input = (VideoPara *)usrData;

    if(input->avStream){
        if(input->frameQ_size == 0){
            schedule_refresh(input,1);
        }
        else{
            schedule_refresh(input, 30);

            Videodisplay(input, sdlRenderer, sdlTexture);

            if(++input->frameQ_idx_front == VIDEO_FRAME_QUEUE_SIZE){
                input->frameQ_idx_front = 0;
            }

            SDL_LockMutex(input->sdlMutex);
            input->frameQ_size --;
            SDL_CondSignal(input->sdlCond);
            SDL_UnlockMutex(input->sdlMutex);
        }
    }
    else{
        schedule_refresh(input,100);
    }
}

void alloc_frame(void *usrData,SDL_Renderer *sdlRenderer){
    VideoPara *input = (VideoPara *)usrData;

    VideoFrame *vf;

    vf = &input->videoFrame[input->frameQ_idx_rear];
    if (vf->pFrame){
        av_frame_free(&vf->pFrame);
    }

    vf->width = input->videoFrame->pFrame->width;
    vf->height = input->videoFrame->pFrame->height;

    AVCodecContext *pCodecCtx = input->pCodecCtx;

    AVFrame* pFrame_out= av_frame_alloc();
    if( pFrame_out == NULL )
        return;
#if BUFFER_FMT_YUV
    //output frame for SDL
    enum AVPixelFormat pixel_fmt = AV_PIX_FMT_YUV420P;

#else
    //output RGBFrame
    enum AVPixelFormat pixel_fmt = AV_PIX_FMT_RGBA;
#endif
    uint8_t* out_buffer= av_malloc((size_t) av_image_get_buffer_size(pixel_fmt,pCodecCtx->width,pCodecCtx->height,1));
    av_image_fill_arrays(pFrame_out->data,pFrame_out->linesize, out_buffer,
                         pixel_fmt,pCodecCtx->width,pCodecCtx->height,1);


    vf->pFrame = pFrame_out;

    //SDL_CreateTexture(renderer, SDL_PIXELFORMAT_IYUV, SDL_TEXTUREACCESS_STREAMING, vp->width, vp->height);

    SDL_LockMutex(input->sdlMutex);
    vf->allocated = 1;
    SDL_CondSignal(input->sdlCond);
    SDL_UnlockMutex(input->sdlMutex);

}

int queue_frame(VideoPara *input, AVFrame *pFrame){
    VideoFrame *vf;

    /* wait until we have space for a new pic */
    SDL_LockMutex(input->sdlMutex);
    while(input->frameQ_size >= VIDEO_FRAME_QUEUE_SIZE &&
          !input->quit) {
        SDL_CondWait(input->sdlCond, input->sdlMutex);
    }
    SDL_UnlockMutex(input->sdlMutex);

    if(input->quit)
        return -1;

    // windex is set to 0 initially
    vf = &input->videoFrame[input->frameQ_idx_rear];

    /* allocate or resize the buffer! */
    if(!vf->pFrame ||
       vf->width != input->avStream->codecpar->width ||
       vf->height != input->avStream->codecpar->height) {
        SDL_Event event;

        vf->allocated = false;
        /* we have to do it in the main thread */
        event.type = FF_ALLOC_EVENT;
        event.user.data1 = input;
        SDL_PushEvent(&event);

        /* wait until we have a picture allocated */
        SDL_LockMutex(input->sdlMutex);
        while(!vf->allocated && !input->quit) {
            SDL_CondWait(input->sdlCond, input->sdlMutex);
        }
        SDL_UnlockMutex(input->sdlMutex);
        if(input->quit) {
            return -1;
        }
    }

    /* We have a place to put our picture on the queue */

    if(vf->pFrame) {

        // Convert the image into YUV format that SDL uses
        sws_scale
                (
                     input->img_convert_ctx,
                     (uint8_t const * const *)pFrame->data,
                     pFrame->linesize,
                     0,
                     input->avStream->codecpar->height,
                     vf->pFrame->data,
                     vf->pFrame->linesize
                );
        /* now we inform our display thread that we have a pic ready */
        if(++input->frameQ_idx_rear == VIDEO_FRAME_QUEUE_SIZE) {
            input->frameQ_idx_rear = 0;
        }
        SDL_LockMutex(input->sdlMutex);
        input->frameQ_size++;
        SDL_UnlockMutex(input->sdlMutex);
    }
    return 0;
}

int video_thread(void *arg) {
    VideoPara *is = (VideoPara *)arg;
    AVPacket pkt1, *packet = &pkt1;
    int frameFinished;
    AVFrame *pFrame;

    pFrame = av_frame_alloc();

    for(;;) {
        if(pktQ_get(&is->videoQ, packet, 1) < 0) {
            // means we quit getting packets
            break;
        }
        // Decode video frame
       int get_pkt =  avcodec_send_packet(pCodecCtx,packet);

        avcodec_receive_frame(is->pCodecCtx, pFrame);

        // Did we get a video frame?
        if(get_pkt == 0) {
            if(queue_frame(is, pFrame) < 0) {
                break;
            }
        }
        av_packet_unref(packet);
    }
    av_frame_free(&pFrame);
    return 0;
}

int stream_component_open(VideoPara *is, int stream_index) {

    AVFormatContext *pFormatCtx = is->pFormatCtx;
    AVCodecParameters *pCodecParameters = NULL;
    AVCodecContext *codecCtx = NULL;
    AVCodec *codec = NULL;
    AVDictionary *optionsDict = NULL;
    SDL_AudioSpec wanted_spec, spec;

    if(stream_index < 0 || stream_index >= pFormatCtx->nb_streams) {
        return -1;
    }

    // Get a pointer to the codec context for the video stream
    pCodecParameters = pFormatCtx->streams[stream_index]->codecpar;

    // Find the decoder for the video stream
    codec=avcodec_find_decoder(pCodecParameters->codec_id);

    //alloc a codecContext
   codecCtx = avcodec_alloc_context3(codec);


    //transform
    if(avcodec_parameters_to_context(codecCtx,pCodecParameters) < 0){
        LOGE("copy the codec parameters to context fail!");
    }

    if(codecCtx->codec_type == AVMEDIA_TYPE_AUDIO) {
        // Set audio settings from codec info
        wanted_spec.freq = codecCtx->sample_rate;
        wanted_spec.format = AUDIO_S16SYS;
        wanted_spec.channels = (Uint8) codecCtx->channels;
        wanted_spec.silence = 0;
        wanted_spec.samples = SDL_AUDIO_BUFFER_SIZE;
        wanted_spec.callback = audio_callback;
        wanted_spec.userdata = is;

        if(SDL_OpenAudio(&wanted_spec, &spec) < 0) {
            fprintf(stderr, "SDL_OpenAudio: %s\n", SDL_GetError());
            return -1;
        }
    }
    codec = avcodec_find_decoder(codecCtx->codec_id);
    if(!codec || (avcodec_open2(codecCtx, codec, &optionsDict) < 0)) {
        fprintf(stderr, "Unsupported codec!\n");
        return -1;
    }

    switch(codecCtx->codec_type) {
        case AVMEDIA_TYPE_AUDIO:
            is->audioStream = stream_index;
            is->audio_st = pFormatCtx->streams[stream_index];
            is->audio_buf_size = 0;
            is->audio_buf_index = 0;
            memset(&is->audio_pkt, 0, sizeof(is->audio_pkt));
            packet_queue_init(&is->audioq);
            SDL_PauseAudio(0);
            break;
        case AVMEDIA_TYPE_VIDEO:
            is->videoStream = stream_index;
            is->video_st = pFormatCtx->streams[stream_index];

            packet_queue_init(&is->videoq);
            is->video_tid = SDL_CreateThread(video_thread, "video_thread",is);
            is->sws_ctx = sws_getContext
                            (
                                    is->video_st->codec->width,
                                    is->video_st->codec->height,
                                    is->video_st->codec->pix_fmt,
                                    is->video_st->codec->width,
                                    is->video_st->codec->height,
                                    PIX_FMT_YUV420P,
                                    SWS_BILINEAR,
                                    NULL,
                                    NULL,
                                    NULL
                            );
            break;
        default:
            break;
    }
    return 0;
}

int decode_interrupt_cb(void *opaque) {
    return (global_video_state && global_video_state->quit);
}

int decode_thread(void *    ) {

    VideoState *is = (VideoState *)arg;
    AVFormatContext *pFormatCtx = NULL;
    AVPacket pkt1, *packet = &pkt1;

    int video_index = -1;
    int audio_index = -1;
    int i;

    AVDictionary *io_dict = NULL;
    AVIOInterruptCB callback;

    is->videoStream=-1;
    is->audioStream=-1;

    global_video_state = is;
    // will interrupt blocking functions if we quit!
    callback.callback = decode_interrupt_cb;
    callback.opaque = is;
    if (avio_open2(&is->io_context, is->filename, 0, &callback, &io_dict))
    {
        fprintf(stderr, "Unable to open I/O for %s\n", is->filename);
        return -1;
    }

    // Open video file
    if(avformat_open_input(&pFormatCtx, is->filename, NULL, NULL)!=0)
        return -1; // Couldn't open file

    is->pFormatCtx = pFormatCtx;

    // Retrieve stream information
    if(avformat_find_stream_info(pFormatCtx, NULL)<0)
        return -1; // Couldn't find stream information

    // Dump information about file onto standard error
    av_dump_format(pFormatCtx, 0, is->filename, 0);

    // Find the first video stream

    for(i=0; i<pFormatCtx->nb_streams; i++) {
        if(pFormatCtx->streams[i]->codecpar->codec_type==AVMEDIA_TYPE_VIDEO &&
           video_index < 0) {
            video_index=i;
        }
        if(pFormatCtx->streams[i]->codecpar->codec_type==AVMEDIA_TYPE_AUDIO &&
           audio_index < 0) {
            audio_index=i;
        }
    }
    if(audio_index >= 0) {
        stream_component_open(is, audio_index);
    }
    if(video_index >= 0) {
        stream_component_open(is, video_index);
    }

    if(is->videoStream < 0 || is->audioStream < 0) {
        fprintf(stderr, "%s: could not open codecs\n", is->filename);
        goto fail;
    }

    // main decode loop

    for(;;) {
        if(is->quit) {
            break;
        }
        // seek stuff goes here
        if(is->audioq.size > MAX_AUDIOQ_SIZE ||
           is->videoq.size > MAX_VIDEOQ_SIZE) {
            SDL_Delay(10);
            continue;
        }
        if(av_read_frame(is->pFormatCtx, packet) < 0) {
            if(is->pFormatCtx->pb->error == 0) {
                SDL_Delay(100); /* no error; wait for user input */
                continue;
            } else {
                break;
            }
        }
        // Is this a packet from the video stream?
        if(packet->stream_index == is->videoStream) {
            packet_queue_put(&is->videoq, packet);
        } else if(packet->stream_index == is->audioStream) {
            packet_queue_put(&is->audioq, packet);
        } else {
            av_packet_unref(packet);
        }
    }
    /* all done - wait for it */
    while(!is->quit) {
        SDL_Delay(100);
    }

    fail:
    if(1){
        SDL_Event event;
        event.type = FF_QUIT_EVENT;
        event.user.data1 = is;
        SDL_PushEvent(&event);
    }
    return 0;
}

int main(int argc, char *argv[]) {

    SDL_Event       event;

    VideoPara     *is;

    struct SDL_Window     *pScreen;
    struct SDL_Renderer   *pRenderer;


    is = av_mallocz(sizeof(VideoPara));

    if(argc < 2) {
        fprintf(stderr, "Usage: test <file>\n");
        exit(1);
    }
    // Register all formats and codecs
    av_register_all();

    if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER)) {
        fprintf(stderr, "Could not initialize SDL - %s\n", SDL_GetError());
        exit(1);
    }


    pScreen = SDL_CreateWindow("audio & video", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 1920,1080, SDL_WINDOW_OPENGL|SDL_WINDOW_FULLSCREEN);

    if(!pScreen) {
        fprintf(stderr, "SDL: could not set video mode - exiting\n");
        exit(1);
    }
    //SDL_Window *windows = pScreen;

    //pScreen->windowed;
    pRenderer = SDL_CreateRenderer(pScreen, -1, 0);

    SDL_Texture* texture = SDL_CreateTexture(pRenderer,
                                             SDL_PIXELFORMAT_RGBA32,SDL_TEXTUREACCESS_STREAMING,
                                             pCodecCtx->width, pCodecCtx->height);


    av_strlcpy(is->mediaUri, argv[1], 1024);

    is->sdlMutex = SDL_CreateMutex();
    is->sdlCond = SDL_CreateCond();

    schedule_refresh(is, 40);

    is->parse_tid = SDL_CreateThread(decode_thread,"parse_thread", is);
    if(!is->parse_tid) {
        av_free(is);
        return -1;
    }
    for(;;) {

        SDL_WaitEvent(&event);
        switch(event.type) {
            case FF_QUIT_EVENT:
            case SDL_QUIT:
                is->quit = 1;
                /*
                 * If the video has finished playing, then both the picture and
                 * audio queues are waiting for more data.  Make them stop
                 * waiting and terminate normally.
                 */
                SDL_CondSignal(is->audioq.cond);
                SDL_CondSignal(is->videoq.cond);
                SDL_Quit();
                return 0;
                break;
            case FF_ALLOC_EVENT:
                alloc_picture(event.user.data1,pRenderer);
                break;
            case FF_REFRESH_EVENT:
                video_refresh_timer(event.user.data1,pRenderer,texture);
                break;
            default:
                break;
        }
    }

    SDL_DestroyTexture(texture);

    return 0;

}
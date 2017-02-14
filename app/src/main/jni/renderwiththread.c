//
// Created by righere on 16-12-13.
//

#include "stdio.h"
#include <stdbool.h>

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


#include "SDL.h"

#include "libavcodec/avcodec.h"
#include "libavfilter/avfilter.h"
#include "libavformat/avformat.h"
#include <libavutil/time.h>
#include <libswresample/swresample.h>
#include <libavutil/imgutils.h>
#include "libswscale/swscale.h"


//读取媒体文件，并获取ffmpeg相关参数
typedef struct AVFrameList {
    AVFrame *frame;
    struct AVFrameList *next;
} AVFrameList;

static const int capacity = 30;

typedef struct FrameQueue {

    AVFrameList *front_frameL, *rear_frameL;

    uint32_t nb_frames;

    SDL_mutex *mutex;
    SDL_cond *cond;

//    bool enQueue_frame(const AVFrame* frame);
//    bool deQueue_frame(AVFrame **frame);
}FrameQueue;

typedef struct PacketQueue {
    AVPacketList *front_pktL,*rear_pktL;

    Uint32    nb_packets;
    Uint32    size;
    SDL_mutex *mutex;
    SDL_cond  *cond;

//    bool enQueue_pkt(PacketQueue *packetQueue, AVPacket *packet);
//    bool deQueue_pkt(PacketQueue *pktQueue,AVPacket *packet, bool block);
}PacketQueue;

#define SCREEN_W 1920
#define SCREEN_H 1080
typedef struct VideoState{
    AVCodecParameters *codecPara_video;
    AVCodecContext *codecCtx_video;
    AVCodec *codec_video;
    AVStream *stream;
    int stream_index;

    enum AVPixelFormat videoFormat;

    struct SwsContext *video_convert_ctx;

    PacketQueue pktq_video;
    FrameQueue frameq_video;
    AVFrame *pFrame;
    AVFrame *pFrame_out;

    //sync video frame
    double frame_timer;
    double frame_last_pts;
    double frame_last_delay;

    double video_clock;

    SDL_Window *sdlWindow;
    SDL_Renderer *sdlRenderer;
    SDL_Texture *sdlTexture;
    SDL_Rect sdlRect;

}VideoState;

#define AUDIO_MAX_BUFFER_SIZE 192000;
#define SDL_AUDIO_BUFFER_SIZE 1024

#define MAX_AUDIOQ_SIZE (5 * 16 * 1024)
#define MAX_VIDEOQ_SIZE (5 * 256 * 1024)

typedef struct AudioState{
    AVCodecParameters *codecPara_audio;
    AVCodecContext *codecCtx_audio;
    AVCodec *codec_audio;
    AVStream *stream;
    int stream_index;
    enum AVSampleFormat audioFormat;

    struct SwrContext *audio_convert_ctx;

    uint8_t *audio_buff;
    uint32_t audio_buff_size;
    uint32_t audio_buff_index;
    double audio_clock;

    SDL_AudioSpec *sdlAudioSpec;

    PacketQueue pktq_audio;
}AudioState;

typedef struct MediaState{
    AudioState *audio;
    VideoState *video;
    AVFormatContext *formatCtx_Media;

    char* filename;

    bool quit;

}MediaState;

//open input URI
bool openInput(MediaState *mediaState){

    //init media parameters
    AudioState *audio = av_mallocz(sizeof(AudioState));
    VideoState *video = av_mallocz(sizeof(VideoState));

    mediaState->formatCtx_Media = avformat_alloc_context();
    //open format
    if(avformat_open_input(&mediaState->formatCtx_Media,mediaState->filename,NULL,NULL) < 0){
        LOGE("this media format is not support!\n");
        return false;
    }
    //find streaminfo
    if(avformat_find_stream_info(mediaState->formatCtx_Media,NULL) < 0){
        LOGE("Cannot find stream information!\n");
        return false;
    }

    //dump format
    av_dump_format(mediaState->formatCtx_Media, -1, mediaState->filename, 0);
    video->stream_index = -1;
    audio->stream_index = -1;
    //index the stream
    for (int i = 0; i < mediaState->formatCtx_Media->nb_streams; i++) {
        if (mediaState->formatCtx_Media->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO){
            audio->stream_index = i;
        }
        if (mediaState->formatCtx_Media->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO){
            video->stream_index = i;
        }
    }

    if (audio->stream_index < 0 || video->stream_index < 0)
        return false;

    // Fill audio state
    audio->codecPara_audio = mediaState->formatCtx_Media->streams[audio->stream_index]->codecpar;
    audio->codec_audio = avcodec_find_decoder(audio->codecPara_audio->codec_id);
    if (!audio->codec_audio){
        LOGE("Cannot find audio codec!\n");
        return false;
    }
    audio->stream = mediaState->formatCtx_Media->streams[audio->stream_index];
    audio->codecCtx_audio = avcodec_alloc_context3(audio->codec_audio);
    if (avcodec_parameters_to_context(audio->codecCtx_audio, audio->codecPara_audio) != 0){
        LOGE("Cannot transform audio codec_parameters to codec_context!\n");
        return false;
    }

    int open_audioCodec_info = avcodec_open2(audio->codecCtx_audio, audio->codec_audio, NULL);
    if(open_audioCodec_info < 0){
        LOGE("Unable to open audio codec!\n");
    };
    //get audio parameters:codecCtx,codec
mediaState->audio = audio;
    //fill video state
    video->codecPara_video = mediaState->formatCtx_Media->streams[video->stream_index]->codecpar;
    video->codec_video = avcodec_find_decoder(video->codecPara_video->codec_id);
    if(!video->codec_video){
        LOGE("Cannot find video codec!");
        return false;
    }
    video->stream = mediaState->formatCtx_Media->streams[video->stream_index];
    video->codecCtx_video = avcodec_alloc_context3(video->codec_video);
    if(avcodec_parameters_to_context(video->codecCtx_video,video->codecPara_video) != 0){
        LOGE("Cannot transform video codec_parameters to codec_context!\n");
        return false;
    }

    int open_videoCodec_info = avcodec_open2(video->codecCtx_video, video->codec_video, NULL);
    if(open_videoCodec_info < 0){
        LOGE("Unable to open video codec!\n");
    };

    video->frame_timer = (av_gettime()) / 1000000.0;
    video->frame_last_delay = 40e-3;
    //video parameters: codecCtx,codec
mediaState->video = video;
    return true;
}

//init packetQueue
void PacketQueue_init(PacketQueue *q){
    q->nb_packets = 0;
    q->size = 0;
    q->mutex = SDL_CreateMutex();
    q->cond = SDL_CreateCond();
}
//put packets into pktQueue
bool enQueue_pkt(PacketQueue *packetQueue, AVPacket *packet){
    AVPacket *pkt = av_packet_alloc();
    if (av_packet_ref(pkt, packet) < 0)
        return false;

    AVPacketList *pktList = av_malloc(sizeof(AVPacketList));

    pktList->pkt = *pkt;
    pktList->next = NULL;

    SDL_LockMutex(packetQueue->mutex);

    if(!packetQueue->rear_pktL)
        packetQueue->front_pktL = pktList;
    else
        packetQueue->rear_pktL->next = pktList;

    packetQueue->rear_pktL = pktList;

    packetQueue->size += pkt->size;
    packetQueue->nb_packets ++;

    SDL_CondSignal(packetQueue->cond);
    SDL_UnlockMutex(packetQueue->mutex);
    return true;
}

MediaState *mediaState_global;

//get the packet from the queue
bool deQueue_pkt(PacketQueue *pktQueue,AVPacket *packet, bool block){
    bool ret;
    AVPacketList *pktL;

    SDL_LockMutex(pktQueue->mutex);

    while(true){
        if(mediaState_global->quit){
            LOGE("PacketQueue: quit");
            ret = false;
            break;
        }

        //get the front ptk_list in packetQueue
        pktL = pktQueue->front_pktL;

        if (pktL){
            //front pktList reference to the next of pktL
            pktQueue->front_pktL = pktL->next;

            //is it the last ptkList in queue?
            if(!pktQueue->front_pktL){
                pktQueue->rear_pktL = NULL;
            }
            pktQueue->nb_packets --;
            pktQueue->size -= pktL->pkt.size;
            //get the packet
            //av_packet_ref(packet,&pktL->pkt);
            *packet = pktL->pkt;
            av_free(pktL);
            ret = true;
            break;
        }
        else if(!block){
            ret = false;
            break;
        }
        else{
            SDL_CondWait(pktQueue->cond,pktQueue->mutex);
        }
    }
    SDL_UnlockMutex(pktQueue->mutex);
    return ret;
}

//decode  to packet and queue the packets
int decode_thread(void *data){
    MediaState *mediaState = (MediaState*)data;
    AVPacket *packet = av_packet_alloc();

    while(true){
        // seek stuff goes here
        if(mediaState->audio->pktq_audio.size > MAX_AUDIOQ_SIZE ||
           mediaState->video->pktq_video.size > MAX_VIDEOQ_SIZE) {
            SDL_Delay(10);
            continue;
        }
        int ret = av_read_frame(mediaState->formatCtx_Media,packet);
        LOGI("read frame: %d",ret);
        if(ret < 0){
            if(ret == AVERROR_EOF)
                break;
            if(mediaState->formatCtx_Media->pb->error == 0){
                SDL_Delay(100);
                continue;
            }
            else
                break;
        }
        //queue video packet
        if(packet->stream_index == mediaState->video->stream_index){
            enQueue_pkt(&mediaState->video->pktq_video,packet);
            av_packet_unref(packet);
        }//queue audio packet
        else if(packet->stream_index == mediaState->audio->stream_index){
                enQueue_pkt(&mediaState->audio->pktq_audio,packet);
                av_packet_unref(packet);
        }

        else{
            av_packet_unref(packet);
        }

    }
    av_packet_free(&packet);
    return 0;
}

void FrameQueue_init(FrameQueue *q){
    q->nb_frames = 0;
    q->mutex = SDL_CreateMutex();
    q->cond = SDL_CreateCond();
}

//get the frame from the framequeue
bool deQueue_frame(FrameQueue *frameQueue, AVFrame **frame, bool block){

    bool ret = true;
    AVFrameList *frameList;
    SDL_LockMutex(frameQueue->mutex);
    while (true){

        if(mediaState_global->quit){
            LOGE("PacketQueue: quit");
            ret = false;
            break;
        }

        frameList = frameQueue->front_frameL;
        if (frameList){
            frameQueue->front_frameL = frameList->next;
            if (!frameQueue->front_frameL){
                frameQueue->rear_frameL = NULL;
            }

            frameQueue->nb_frames --;
            av_frame_ref(*frame,frameList->frame);
            ret = true;
            break;
        }
        else if(!block){
            ret = false;
            break;
        }
        else{
            SDL_CondWait(frameQueue->cond,frameQueue->mutex);
        }
    }
    SDL_UnlockMutex(frameQueue->mutex);

    return ret;
}

//put frame in FrameQueue
bool enQueue_frame(FrameQueue *frameQueue,const AVFrame* frame){
    AVFrame* pFrame = av_frame_alloc();

    if(av_frame_ref(pFrame,frame) < 0)
        return false;

    AVFrameList *pFrameList = av_malloc(sizeof(AVFrameList));

    pFrameList->frame = pFrame;
    pFrameList->next = NULL;

    SDL_LockMutex(frameQueue->mutex);

    if(!frameQueue->rear_frameL){
        frameQueue->front_frameL = pFrameList;
    }
    else{
        frameQueue->rear_frameL->next = pFrameList;
    }

    frameQueue->rear_frameL = pFrameList;

    frameQueue->nb_frames ++;

    SDL_CondSignal(frameQueue->cond);
    SDL_UnlockMutex(frameQueue->mutex);

    return true;
}

//video display process
static const double SYNC_THRESHOLD = 0.01;
static const double NOSYNC_THRESHOLD = 10.0;

#define FF_REFRESH_EVENT (SDL_USEREVENT)
#define FF_QUIT_EVENT (SDL_USEREVENT + 1)

//get audio clock
double get_audio_clock(AudioState *audioState){
    int hw_buf_size = audioState->audio_buff_size - audioState->audio_buff_index;
    int bytes_per_sec = audioState->stream->codecpar->sample_rate * audioState->codecCtx_audio->channels * 2;

    double pts = audioState->audio_clock - (double)hw_buf_size/ bytes_per_sec;

    return pts;
}

//decode audio frame from audio packet
int decode_audioFrame(void *usrdata){
    AudioState *audioState = (AudioState *) usrdata;
    AVFrame *audioFrame = av_frame_alloc();
    AVPacket *audioPkt = av_packet_alloc();
    int data_size;
    double clock;
    if(mediaState_global->quit)
        return -1;
    if(!deQueue_pkt(&audioState->pktq_audio,audioPkt,true)){
        return  -1;
    }
    if(audioPkt->pts != AV_NOPTS_VALUE){
        audioState->audio_clock = av_q2d(audioState->stream->time_base);
    }
    int send_audio_packet = avcodec_send_packet(audioState->codecCtx_audio,audioPkt);

    if(send_audio_packet != 0 && send_audio_packet != AVERROR_EOF && send_audio_packet != AVERROR(EAGAIN))
        return -1;

    int receive_audio_frame = avcodec_receive_frame(audioState->codecCtx_audio,audioFrame);

    if(receive_audio_frame != 0 && receive_audio_frame != AVERROR_EOF && receive_audio_frame != AVERROR(EAGAIN))
        return -1;

    //fix audioFrame channels
    if(audioFrame->channels > 0 && audioFrame->channel_layout == 0){
        audioFrame->channel_layout = (uint64_t) av_get_default_channel_layout(audioFrame->channels);
    }
    else if(audioFrame->channels == 0 && audioFrame->channel_layout > 0){
        audioFrame->channels = av_get_channel_layout_nb_channels(audioFrame->channel_layout);
    }

//   uint64_t nb_samples = av_rescale(swr_get_delay(audioState->audio_convert_ctx, audioFrame->sample_rate)
//                                    + audioFrame->nb_samples,audioFrame->sample_rate,Rounding(1));
    //After audio convert Context and swr_init context
    //number of samples output per channel
    int nb = swr_convert(audioState->audio_convert_ctx, &audioState->audio_buff, 192000,
                         (const uint8_t **) audioFrame->data, audioFrame->nb_samples);

    data_size = audioFrame->channels*nb*av_get_bytes_per_sample(audioState->audioFormat);

    audioState->audio_clock += (double)data_size/(2*audioState->stream->codecpar->channels*audioState->stream->codecpar->sample_rate);

    av_frame_free(&audioFrame);
    av_packet_free(&audioPkt);

    return  data_size;
}

void  audio_call_back(void *usrdata, Uint8 *stream, int len){
    AudioState *audioState = (AudioState *)usrdata;
    SDL_memset(stream, 0, (size_t) len);

    int audia_size = 0;
    int len1 = 0;
    while (len > 0){
        if(audioState->audio_buff_index>=audioState->audio_buff_size){
            audia_size = decode_audioFrame(audioState);
            if(audia_size < 0){
                audioState->audio_buff_size = 0;
                memset(audioState->audio_buff, 0, audioState->audio_buff_size);
            }
            else{
                audioState->audio_buff_size = (uint32_t) audia_size;
            }

            audioState->audio_buff_index = 0;
        }
        //buff data
        len1 = audioState->audio_buff_size - audioState->audio_buff_index;
        if(len1 > len){
            len1 = len;
        }
        SDL_MixAudio(stream,audioState->audio_buff+audioState->audio_buff_index, (Uint32) len, SDL_MIX_MAXVOLUME);

        len -= len1;
        stream += len1;
        audioState->audio_buff_index += len1;
    }
}

bool audio_play(AudioState *audioState){

    SDL_AudioSpec desired;

    desired.userdata = audioState;
    desired.callback = audio_call_back;
    desired.channels = (Uint8) audioState->codecCtx_audio->channels;
    desired.format = audioState->audioFormat;
    desired.freq = audioState->codecCtx_audio->sample_rate;
    desired.silence = 0;
    desired.samples = SDL_AUDIO_BUFFER_SIZE;


    if(SDL_OpenAudio(&desired,audioState->sdlAudioSpec) < 0){
        return false;
    }
    SDL_PauseAudio(0);
    return true;

}

uint32_t sdl_refresh_timer_cb(uint32_t interval, void *opaque) {
    SDL_Event event;
    event.type = FF_REFRESH_EVENT;
    event.user.data1 = opaque;
    SDL_PushEvent(&event);
    return 0; /* 0 means stop timer */
}

void schedule_refresh(VideoState *videoState, int delay) {
    SDL_AddTimer((Uint32) delay, sdl_refresh_timer_cb, videoState);
}

//deQueue frame and display frame
void video_refresh_timer(void *userdata) {
    MediaState *media = (MediaState*)userdata;
    VideoState *video = media->video;
    AudioState *audio = media->audio;
    AVFrame *frame;
    frame = av_frame_alloc();
    if (video->stream_index >= 0)
    {
        if (video->pktq_video.nb_packets == 0)
            schedule_refresh(video, 1);
        else
        {
            deQueue_frame(&video->frameq_video,&frame,1);

            double current_pts = video->pFrame->opaque;
            double delay = current_pts - video->frame_last_pts;
            if (delay <= 0 || delay >= 1.0)
                delay = video->frame_last_delay;

            video->frame_last_delay = delay;
            video->frame_last_pts = current_pts;

            double ref_clock = get_audio_clock(audio);

            double diff = current_pts - ref_clock;// diff < 0 => video slow,diff > 0 => video quick

            double threshold = (delay > SYNC_THRESHOLD) ? delay : SYNC_THRESHOLD;

            if (fabs(diff) < NOSYNC_THRESHOLD)
            {
                if (diff <= -threshold)
                    delay = 0;
                else if (diff >= threshold)
                    delay *= 2;
            }
            video->frame_timer += delay;
            double actual_delay = video->frame_timer - (double)(av_gettime()) / 1000000.0;
            if (actual_delay <= 0.010)
                actual_delay = 0.010;

            schedule_refresh(video, (int)(actual_delay * 1000 + 0.5));

            sws_scale(media->video->video_convert_ctx, (uint8_t const * const *)video->pFrame->data, video->pFrame->linesize, 0,
                      video->codecCtx_video->height, video->pFrame_out->data, video->pFrame_out->linesize);

            // Display the image to screen
            SDL_UpdateTexture(video->sdlTexture, NULL, video->pFrame_out->data[0], video->codecCtx_video->width*4);
            SDL_RenderClear(video->sdlRenderer);
            SDL_RenderCopy(video->sdlRenderer, video->sdlTexture, NULL, NULL);
            SDL_RenderPresent(video->sdlRenderer);
            av_frame_unref(video->pFrame);
        }
    }
    else
    {
        schedule_refresh(video, 100);
    }
}
//sync video frame
double synchronize(VideoState *videoState, AVFrame *avFrame, double pts){
    double frame_delay;

    if (pts != 0)
        videoState->video_clock = pts; // Get pts,then set video clock to it
    else
        pts = videoState->video_clock; // Don't get pts,set it to video clock

    frame_delay = av_q2d(videoState->stream->time_base);
    frame_delay += avFrame->repeat_pict * (frame_delay * 0.5);

    videoState->video_clock += frame_delay;

    return pts;
}

//decode video packet and enQueue frame
int decode_videoFrame(void *data){
    VideoState *videoState = (VideoState*)data;
    double pts;
    AVFrame *pFrame = av_frame_alloc();
    AVPacket *packet = av_packet_alloc();

    while(true){
        //get packet from queue
        if(!deQueue_pkt(&videoState->pktq_video,packet,true)){
            break;
        }

        int send_video_packet = avcodec_send_packet(videoState->codecCtx_video,packet);
        //get packet success,three valid packet type
        if (send_video_packet != 0 && send_video_packet != AVERROR_EOF && send_video_packet != AVERROR(EAGAIN))
            continue;

        int getFrame = avcodec_receive_frame(videoState->codecCtx_video, pFrame);
        //get Frame success, three valid frame type
        if(getFrame != 0 && getFrame != AVERROR_EOF && getFrame != AVERROR(EAGAIN))
            continue;

        //sync video frame time
        pts = av_frame_get_best_effort_timestamp(pFrame);
        if (pts == AV_NOPTS_VALUE)
            pts = 0;
        pts *=av_q2d(videoState->stream->time_base);

        pFrame->opaque = synchronize(videoState,pFrame,pts);

//        pFrame->opaque = &pts;

        //avoid too fast
        if(videoState->frameq_video.nb_frames >= capacity)
            SDL_Delay(1000);

        enQueue_frame(&videoState->frameq_video, pFrame);

        av_frame_unref(pFrame);
        av_packet_unref(packet);
    }
    av_free(&pFrame);
    av_free(&packet);
    return 0;
}


void video_play(VideoState *videoState){

    VideoState *is = videoState;

    int width = SCREEN_W;
    int height = SCREEN_H;

    //设置window属性
    is->sdlWindow = SDL_CreateWindow("video",SDL_WINDOWPOS_UNDEFINED,SDL_WINDOWPOS_UNDEFINED,
                                             width, height,SDL_WINDOW_RESIZABLE|SDL_WINDOW_FULLSCREEN|SDL_WINDOW_OPENGL);
    if(!is->sdlWindow) {
        LOGE("SDL: could not set video mode - exiting\n");
        exit(1);
    }
    //create renderer and texture
    is->sdlRenderer = SDL_CreateRenderer(is->sdlWindow,-1,0);
    is->sdlTexture = SDL_CreateTexture(is->sdlRenderer,SDL_PIXELFORMAT_BGRA32,SDL_TEXTUREACCESS_STREAMING,width,height);

    //set display rectangle
    SDL_Rect sdlRect;
    sdlRect.x = 0;
    sdlRect.y = 0;
    sdlRect.w = SCREEN_W;
    sdlRect.h = SCREEN_H;
    is->sdlRect = sdlRect;

    is->pFrame = av_frame_alloc();
    is->pFrame_out = av_frame_alloc();

    //set display frame
    is->videoFormat = AV_PIX_FMT_RGBA;
    is->pFrame_out->width = width;
    is->pFrame_out->height = height;

    //init buffer size
    uint8_t* out_buffer = av_malloc((size_t) av_image_get_buffer_size(is->videoFormat,is->pFrame_out->width,is->pFrame_out->height,1));

    av_image_fill_arrays(is->pFrame_out->data,is->pFrame_out->linesize, out_buffer,
                         is->videoFormat,is->pFrame_out->width,is->pFrame_out->height,1);


    //transform size and format of CodecCtx
    is->video_convert_ctx = sws_getContext(videoState->codecCtx_video->width, videoState->codecCtx_video->height, videoState->codecCtx_video->pix_fmt,
                                     width, height, is->videoFormat,
                                     SWS_BICUBIC, NULL,
                                           NULL, NULL);


    FrameQueue_init(&is->frameq_video);
    //start thread to decode video and queue the frames
    SDL_CreateThread(decode_videoFrame,"decode video frame",&is);

    //control fps 40
    schedule_refresh(is,40);
}



int main(int argc, char* argv[]){

    MediaState *mediaState;
    //init mediastate
    mediaState = av_mallocz(sizeof(MediaState));

    if(argc < 2){
        LOGE("no mediaUri input!");
        exit(1);
    }
    mediaState->filename = argv[1];

    av_register_all();//register all the decoder and encoder
    //初始化SDL
    if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER)) {
        LOGE("SDL_Init failed %s" ,SDL_GetError());
        exit(1);
    }

    //get media parameters and create the thread of decoding packets
    if(openInput(mediaState)){
        PacketQueue_init(&mediaState->video->pktq_video);
        PacketQueue_init(&mediaState->audio->pktq_audio);
        SDL_CreateThread(decode_thread,"decode to packets",mediaState);
    }

    video_play(mediaState->video);
    audio_play(mediaState->audio);


    SDL_Event event;
    while (true) // SDL event loop
    {
        SDL_WaitEvent(&event);
        switch (event.type)
        {
            case FF_QUIT_EVENT:
            case SDL_QUIT:
                mediaState->quit = true;
                SDL_Quit();
                return 0;

            case FF_REFRESH_EVENT:
                video_refresh_timer(&mediaState);
                break;

            default:
                break;
        }
    }
}
//
// Created by righere on 16-11-22.
//
#include "stdio.h"

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



#include <src/render/SDL_sysrender.h>

#include "SDL.h"
#include "SDL_log.h"
#include "SDL_thread.h"

#include "libavcodec/avcodec.h"
#include "libavfilter/avfilter.h"
#include "libavformat/avformat.h"
#include "libswscale/swscale.h"


//屏幕参数
struct ScreenParam{
    int h,w;
    int rgb;
};

int play(char *fileUri)

{
    AVFormatContext *pFormatCtx;
    int             i, streamIdx;
    AVCodecContext  *pCodecCtx;
    AVCodec         *pCodec;
    AVFrame         *pFrame;
    AVPacket        packet;
    int             frameFinished;
    float           aspect_ratio;

    static struct SwsContext *img_convert_ctx;

//创建一个sdl_window,使用SDL会弹出窗体
    SDL_Window     *screen;

//显示YUV数据
    SDL_Texture    *bmp;

//确定texture的显示位置
    const SDL_Rect *rect = NULL;

//用于将texture渲染到window
    SDL_Renderer   *renderer;

//SDL屏幕的响应事件
    SDL_Event       event;

    //获取文件名

    const char* mediaUri = fileUri;


    av_register_all();//注册所有支持的文件格式以及编解码器

    //分配一个AVFormatContext
    pFormatCtx = avformat_alloc_context();

    //判断文件流是否能打开
    if(avformat_open_input(&pFormatCtx, mediaUri, NULL, NULL) != 0){
        LOGE("Couldn't open input stream! \n");
        return -1;
    }
    //判断能够找到文件流信息
    if(avformat_find_stream_info(pFormatCtx, NULL) < 0){
        LOGE("couldn't find open stream information !\n");
        return -1;
    }

    //打印文件信息
    av_dump_format(pFormatCtx, -1, mediaUri, 0);


    streamIdx=-1;
    for(i=0; i<pFormatCtx->nb_streams; i++)
        //新版本ffmpeg将AVCodecContext *codec替换成*codecpar
        if(pFormatCtx->streams[i]->codecpar->codec_type==AVMEDIA_TYPE_VIDEO) {
            streamIdx=i;
            break;
        }
        if(streamIdx==-1){
            LOGE("Couldn't find a video stream !\n");
            return -1;
        }

    // Get a pointer to the codec context for the video stream
    pCodecCtx= (AVCodecContext*)pFormatCtx->streams[streamIdx]->codecpar;

    //pCodecCtx = avcodec_alloc_context3(pCodec);

    // Find the decoder for the video stream
    pCodec=avcodec_find_decoder(pCodecCtx->codec_id);

    if(pCodec==NULL) {
        LOGE("Unable to find codec!\n");
        return -1; // Codec not found
    }
    //打开codec
    if(avcodec_open2(pCodecCtx, pCodec, NULL) < 0){
        LOGE("Unable to open codec!\n");
        return -1;
    };

    //分配frame
    pFrame = av_frame_alloc();

    if(pFrame == NULL){
        LOGE("Unable to allocate an AVFrame!\n");
        return -1;
    }





    /**
 *  \brief A rectangle, with the origin at the upper left.
 *
 *  \sa SDL_RectEmpty
 *  \sa SDL_RectEquals
 *  \sa SDL_HasIntersection
 *  \sa SDL_IntersectRect
 *  \sa SDL_UnionRect
 *  \sa SDL_EnclosePoints
 *     typedef struct SDL_Rect
 *      {
 *          int x, y;
 *          int w, h;
 *      } SDL_Rect;
 */
//可以自定义rect，默认值：
//    rect.x = 0;
//    rect.y = 0;
//    rect.w = pCodecCtx->width;
//    rect.h = pCodecCtx->height;
    //初始化SDL
    if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER)) {
        LOGE("SDL_Init failed %s" ,SDL_GetError());
        exit(1);
    }

    //设置window属性
    screen = SDL_CreateWindow("Convexd_window",SDL_WINDOWPOS_UNDEFINED,SDL_WINDOWPOS_UNDEFINED,
                              pCodecCtx->width, pCodecCtx->height,SDL_WINDOW_RESIZABLE);

    if(!screen) {
        LOGE("SDL: could not set video mode - exiting\n");
        exit(1);
    }

    /**
 *  \brief Create a 2D rendering context for a window.
 *
 *  \param window The window where rendering is displayed.
 *  \param index    The index of the rendering driver to initialize, or -1 to
 *                  initialize the first one supporting the requested flags.
 *  \param flags    ::SDL_RendererFlags.
 *
 *  \return A valid rendering context or NULL if there was an error.
 *
 *  \sa SDL_CreateSoftwareRenderer()
 *  \sa SDL_GetRendererInfo()
 *  \sa SDL_DestroyRenderer()
 */
    //create renderer and set parameter
    renderer = SDL_CreateRenderer(screen,-1,0);

    // Allocate a place to put our RGB image on that screen
    bmp = SDL_CreateTexture(renderer,
                            SDL_PIXELFORMAT_RGBA32,SDL_TEXTUREACCESS_STREAMING,
                            pCodecCtx->width, pCodecCtx->height);


    // Read frames
    while(av_read_frame(pFormatCtx, &packet)>=0) {
        // Is this a packet from the video stream?
        if(packet.stream_index==streamIdx) {
            //decoder allocate frame to pFrame
            avcodec_receive_frame(pCodecCtx, pFrame);
            // Did we get a video frame?
            if(pFrame) {

                //默认全屏
                SDL_LockTexture(bmp, rect, bmp->pixels, (int*)bmp->pitch);

                /**
                 *
                 * */
                //            * truct SDL_Texture
                //              {
                //                const void *magic;
                //                Uint32 format;              /**< The pixel format of the texture */
                //                int access;                 /**< SDL_TextureAccess */
                //                int w;                      /**< The width of the texture */
                //                int h;                      /**< The height of the texture */
                //                int modMode;                /**< The texture modulation mode */
                //                SDL_BlendMode blendMode;    /**< The texture blend mode */
                //                Uint8 r, g, b, a;           /**< Texture modulation values */
                //
                //                SDL_Renderer *renderer;
                //
                //                /* Support for formats not supported directly by the renderer */
                //                SDL_Texture *native;
                //                SDL_SW_YUVTexture *yuv;
                //                void *pixels;
                //                int pitch;
                //                SDL_Rect locked_rect;
                //
                //                void *driverdata;           /**< Driver specific texture representation */
                //
                //                SDL_Texture *prev;
                //                SDL_Texture *next;
                //            };


                /*uint8_t *data[AV_NUM_DATA_POINTERS]：解码后原始数据（对视频来说是YUV，RGB，对音频来说是PCM）
                int linesize[AV_NUM_DATA_POINTERS]：data中“一行”数据的大小。注意：未必等于图像的宽，一般大于图像的宽。
                int width, height：视频帧宽和高（1920x1080,1280x720...）
                int nb_samples：音频的一个AVFrame中可能包含多个音频帧，在此标记包含了几个
                int format：解码后原始数据类型（YUV420，YUV422，RGB24...）
                int key_frame：是否是关键帧
                enum AVPictureType pict_type：帧类型（I,B,P...）
                AVRational sample_aspect_ratio：宽高比（16:9，4:3...）
                int64_t pts：显示时间戳
                int coded_picture_number：编码帧序号
                int display_picture_number：显示帧序号
                int8_t *qscale_table：QP表
                uint8_t *mbskip_table：跳过宏块表
                int16_t (*motion_val[2])[2]：运动矢量表
                uint32_t *mb_type：宏块类型表
                short *dct_coeff：DCT系数，这个没有提取过
                int8_t *ref_index[2]：运动估计参考帧列表（貌似H.264这种比较新的标准才会涉及到多参考帧）
                int interlaced_frame：是否是隔行扫描
                uint8_t motion_subsample_log2：一个宏块中的运动矢量采样个数，取log的*/

//                avFrame->data[0] = bmp->Y;
//                avFrame->data[1] = bmp->U;
//                avFrame->data[2] = bmp->V;
//
//                avFrame->linesize[0] = bmp->pitch;
//                avFrame->linesize[1] = bmp->pitch;
//                avFrame->linesize[2] = bmp->pitch;

                AVFrame *avFrame = av_frame_alloc();
                /**
                * Allocate and return an SwsContext. You need it to perform
                * scaling/conversion operations using sws_scale().
                *
                * @param srcW the width of the source image
                * @param srcH the height of the source image
                * @param srcFormat the source image format
                * @param dstW the width of the destination image
                * @param dstH the height of the destination image
                * @param dstFormat the destination image format
                * @param flags specify which algorithm and options to use for rescaling
                * @param param extra parameters to tune the used scaler
                *              For SWS_BICUBIC param[0] and [1] tune the shape of the basis
                *              function, param[0] tunes f(1) and param[1] f´(1)
                *              For SWS_GAUSS param[0] tunes the exponent and thus cutoff
                *              frequency
                *              For SWS_LANCZOS param[0] tunes the width of the window function
                * @return a pointer to an allocated context, or NULL in case of error
                * @note this function is to be removed after a saner alternative is
                *       written
                */
                //transform size and format of CodecCtx,转换成RGBA32的CodecCtx

                img_convert_ctx = sws_getContext(pCodecCtx->width, pCodecCtx->height, pCodecCtx->pix_fmt,
                                                 pCodecCtx->width, pCodecCtx->height, AV_PIX_FMT_RGBA,
                                                 SWS_BICUBIC, NULL,
                                                 NULL, NULL);

                sws_scale(img_convert_ctx, (const uint8_t *const *) pFrame->data, pFrame->linesize, 0, pCodecCtx->height, avFrame->data, avFrame->linesize);


                SDL_UnlockTexture(bmp);

                SDL_UpdateTexture(bmp,rect,bmp->pixels,bmp->pitch);

            }
        }
        // Free the packet that was allocated by av_read_frame
        av_packet_unref(&packet);
        SDL_PollEvent(&event);
        switch(event.type) {
            case SDL_QUIT:
                SDL_Quit();
                exit(0);
                break;
            default:
                break;
        }

    }
    // Free the YUV frame
    av_frame_free((AVFrame **) pFrame);

    // Close the codec
    avcodec_close(pCodecCtx);

    // Close the video file
    avformat_close_input((AVFormatContext **) pFormatCtx);

    return 0;
}

int main(int argc, char *argv[]){

    play(argv[1]);
    return 0;
}



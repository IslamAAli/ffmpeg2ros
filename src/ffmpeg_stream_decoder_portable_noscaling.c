// simple IP video stream decoder based on ffmpeg
// main idea is to keep all ffmpeg related code inside this .c file and only include "ffmpeg_stream_decoder_rev*.h" 
// in main application code because mixing C, C++ and ffmpeg don't work well
// multi-threaded version
// resizes input video to target resolution and 24-bit RGB pixel format so it can be directly used as texture in 3D rendering
// July 24 / 2024 - added option to grab frames in native resolution without resizing
// 
// include ffmpeg headers

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavformat/avio.h>
#include <libavutil/dict.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h> 

// keep all multi-threading related stuff in #ifdef/#endif blocks specific to Windows
// _WIN32 macro is defined for both x86 and x64 target in Visual Studio

//#define USE_WINDOWS_THREADING	  //Windows
#define USE_PTHREADS			  //instead of pthreads which is portable across Windows & Linux


#ifdef _WIN32
 #include <windows.h>
#endif
#ifdef USE_PTHREADS
 #define HAVE_STRUCT_TIMESPEC   //so timespec struct is not redefined in 'pthread.h'
 #include <pthread.h>
 #ifdef _WIN32
  //#pragma comment(lib,"pthreadVC2.lib") //32-bit  Win7 C:\OtherLibs\pthreads
 #pragma comment(lib,"pthread.lib")  //64-bit      Win7 C:\OtherLibs\pthreads\x64
 #endif
#endif


// include declarations for our functions 
//#include "ffmpeg_stream_decoder_portable.h"	//fixed rescaled image for OpenGL purposes only version
#include "ffmpeg_stream_decoder_portable_noscaling/ffmpeg_stream_decoder_portable_noscaling.h"	//merging V's July 24 addition of native resolution

// add ffmpeg libraries to linker -can only do in Windows, in Linux must do on command line gcc (or in Makefile)
#ifdef _WIN32
 #pragma comment(lib,"avcodec.lib")
 #pragma comment(lib,"avutil.lib")
 #pragma comment(lib,"avformat.lib")
 #pragma comment(lib,"swscale.lib")
#endif

// our stream decoder library can work with MAX_STREAMS simultaneously
#define MAX_STREAMS 32

// StreamContext structure holds all ffmpeg stuff needed to receive and decode IP video stream
// open() / close() functions will operate on integer 'handles' instead of pointers to this structures
struct StreamContext
	{
	int is_open;
	char URI[1024];

	uint8_t* framebuf;
	int target_width;
	int target_height;

	int is_closing;
	int status;

#ifdef USE_WINDOWS_THREADING
	CRITICAL_SECTION cs_lock_frame;
	HANDLE thread_handle;
#endif
#ifdef USE_PTHREADS
	pthread_mutex_t cs_lock_frame;
	pthread_t thread_handle;
#endif
};

static struct StreamContext stream[MAX_STREAMS];
static int num_open_streams = 0;

// internal function declarations
#ifdef USE_WINDOWS_THREADING
 UINT mt_ffmpeg_stream_decoder_start_thread(LPVOID param);
#endif
#ifdef USE_PTHREADS
 void* mt_ffmpeg_stream_decoder_start_thread(void* thread_argument);
#endif

void mt_ffmpeg_stream_decoder_thread(int handle);
int mt_ffmpeg_stream_decoder_interrupt_callback(void *p);
AVFrame* mt_ffmpeg_stream_decoder_init_frame_rgb(int width, int height);

// must call this function before any other mt_ffmpeg_stream* function!
void mt_ffmpeg_stream_decoder_init()
	{
    avformat_network_init();
//	av_log_set_level(AV_LOG_DEBUG);
	memset(stream, 0, sizeof(stream));
	}

// call this function to release resources at the end of main application
// it will also close all opened streams
void mt_ffmpeg_stream_decoder_done()
	{
	int i;
	if(num_open_streams > 0)
		{
		for(i=0; i < MAX_STREAMS; i++)
			mt_ffmpeg_stream_decoder_close(i);
		}
	}

// opens IP stream by URI
// returns stream handle or (-1) on error
// set width and height to 0 to grab frames in native resolution
// it's a non-blocking function that will create separate thread and do all processing there
// returning valid stream handle doesn't mean that IP stream is actually opened!
// should be called only from main application thread!
int mt_ffmpeg_stream_decoder_open(const char* uri, int width, int height)
	{
	int handle;
#ifdef USE_WINDOWS_THREADING
	DWORD thread_id;
#endif

	if(num_open_streams < MAX_STREAMS)
		{
		// find free handle
		for(handle=0; handle < MAX_STREAMS; handle++)
			{
			if(!stream[handle].is_open)
				break;
			}
		}
	else
		{
		// run out of handles
		handle = -1;
		}

	// set stream to open
	strcpy(stream[handle].URI, uri);
	stream[handle].status = FFMPEG_STREAM_STATUS_CONNECTING;
	stream[handle].target_width = width;
	stream[handle].target_height = height;

	if(width > 0 && height > 0)
		stream[handle].framebuf = malloc(width * height * 3);

#ifdef USE_WINDOWS_THREADING
	InitializeCriticalSection(&(stream[handle].cs_lock_frame));
#endif
#ifdef USE_PTHREADS
	pthread_mutex_init(&stream[handle].cs_lock_frame, NULL); //if   
#endif

	stream[handle].is_open = 1;

	// create and start working thread
#ifdef USE_WINDOWS_THREADING
	stream[handle].thread_handle = CreateThread(NULL, 0,
		(LPTHREAD_START_ROUTINE)(mt_ffmpeg_stream_decoder_start_thread), (LPVOID)(UINT_PTR)handle, 0, &thread_id);
#endif
#ifdef USE_PTHREADS
	int rc=pthread_create(&stream[handle].thread_handle, NULL, mt_ffmpeg_stream_decoder_start_thread, (void*)handle);
#endif
	return handle;
	}

// close stream, end worker thread, free resources
// should be called only from main application thread!
void mt_ffmpeg_stream_decoder_close(int handle)
	{
	if(stream[handle].is_open)
		{
		// signal worker thread to close
		stream[handle].is_closing = 1;

		// wait for thread to end gracefully for 3 seconds, otherwise kill it
#ifdef USE_WINDOWS_THREADING
		if(WaitForSingleObject(stream[handle].thread_handle, 3000) != WAIT_OBJECT_0)
			TerminateThread(stream[handle].thread_handle, 0);
		// cleanup
		CloseHandle(stream[handle].thread_handle);
#endif
#ifdef USE_PTHREADS
		pthread_join(stream[handle].thread_handle, NULL);
#endif

		if(stream[handle].framebuf != 0)
			{
			free(stream[handle].framebuf);
			stream[handle].framebuf = 0;
			}

		stream[handle].is_closing = 0;

#ifdef USE_WINDOWS_THREADING
		DeleteCriticalSection(&(stream[handle].cs_lock_frame));
#endif
#ifdef USE_PTHREADS
		pthread_mutex_destroy(&stream[handle].cs_lock_frame);
#endif

		stream[handle].is_open = 0;
		num_open_streams--;
		}
	}

// get stream status
// should be called only from main application thread!
int mt_ffmpeg_stream_decoder_get_status(int handle)
	{
	int status = FFMPEG_STREAM_STATUS_ERROR;

	if(stream[handle].is_open)
		{
#ifdef USE_WINDOWS_THREADING
		// guard access with critical section
		EnterCriticalSection(&(stream[handle].cs_lock_frame));
#endif
#ifdef USE_PTHREADS
		pthread_mutex_lock(&(stream[handle].cs_lock_frame));
#endif

		status = stream[handle].status;

#ifdef USE_WINDOWS_THREADING
		LeaveCriticalSection(&(stream[handle].cs_lock_frame));
#endif
#ifdef USE_PTHREADS
		pthread_mutex_unlock(&(stream[handle].cs_lock_frame));
#endif
		}

	return status;
	}

// get frame width
// should be called only from main application thread!
int mt_ffmpeg_stream_decoder_get_frame_width(int handle)
	{
	int width = 0;

	if(stream[handle].is_open)
		{
#ifdef USE_WINDOWS_THREADING
		// guard access with critical section
		EnterCriticalSection(&(stream[handle].cs_lock_frame));
#endif
#ifdef USE_PTHREADS
		pthread_mutex_lock(&(stream[handle].cs_lock_frame));
#endif

		width = stream[handle].target_width;

#ifdef USE_WINDOWS_THREADING
		LeaveCriticalSection(&(stream[handle].cs_lock_frame));
#endif
#ifdef USE_PTHREADS
		pthread_mutex_unlock(&(stream[handle].cs_lock_frame));
#endif
		}

	return width;
	}

// get frame height
// should be called only from main application thread!
int mt_ffmpeg_stream_decoder_get_frame_height(int handle)
	{
	int height = 0;

	if(stream[handle].is_open)
		{
#ifdef USE_WINDOWS_THREADING
		// guard access with critical section
		EnterCriticalSection(&(stream[handle].cs_lock_frame));
#endif
#ifdef USE_PTHREADS
		pthread_mutex_lock(&(stream[handle].cs_lock_frame));
#endif

		height = stream[handle].target_height;

#ifdef USE_WINDOWS_THREADING
		LeaveCriticalSection(&(stream[handle].cs_lock_frame));
#endif
#ifdef USE_PTHREADS
		pthread_mutex_unlock(&(stream[handle].cs_lock_frame));
#endif
		}

	return height;
	}



// helper function that starts decoder thread - Windows variant
#ifdef USE_WINDOWS_THREADING
 UINT mt_ffmpeg_stream_decoder_start_thread(LPVOID param)
	{
	mt_ffmpeg_stream_decoder_thread((int)(UINT_PTR)param);
	return 0;
	}
#endif

#ifdef USE_PTHREADS
 void* mt_ffmpeg_stream_decoder_start_thread(void* thread_argument)
	{
	mt_ffmpeg_stream_decoder_thread((int)thread_argument);
	return 0;
	}
#endif


// this callback function will be periodically called by ffmpeg blocking functions
// return 1 if associated stream should be closed and calling ffmpeg function would immediately
// terminate with error 
// return 0 to continue normally
int mt_ffmpeg_stream_decoder_interrupt_callback(void *p)
	{
	struct StreamContext* param = (struct StreamContext*)p;
	return param->is_closing;
	}

// function that actually connects to stream, receives and decodes frames - it will run inside separate thread
// will try to keep it as platform-independent as possible
void mt_ffmpeg_stream_decoder_thread(int handle)
	{
	const AVCodec* codec = 0;
	AVFormatContext* format_ctx = 0;
	AVCodecContext* codec_ctx = 0;
    uint8_t* picture_buffer = 0;
    AVFrame* picture = 0;
	AVFrame* picture_rgb = 0;
	struct SwsContext* conversion_ctx = 0;
	AVPacket* packet = 0;
	int video_stream_index = -1;
	int opened_ok = 0;
	unsigned int i;

	// try to open stream and start decoding
	// break from for(ever) loop on errors, sort of poor man's exception handling

	for(;;)
		{
		// try to open stream and start decoding

		format_ctx = avformat_alloc_context();

		// assign interrupt callback that will be periodically called by ffmpeg blocking functions
		// to check if stream should be closed immediately

		format_ctx->interrupt_callback.callback = mt_ffmpeg_stream_decoder_interrupt_callback;
		format_ctx->interrupt_callback.opaque = &(stream[handle]);

		// connect to URI

		if(avformat_open_input(&format_ctx, stream[handle].URI, NULL, NULL) < 0)
			break;

		// get info on all elementary streams

		if(avformat_find_stream_info(format_ctx, NULL) < 0)
			break;

		// find video elementary stream

		for(i = 0; i < format_ctx->nb_streams; i++) 
			{
			if(format_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
				video_stream_index = i;
			}

		if(video_stream_index == -1)
			break;

		// send PLAY command for protocols that need that, like RTSP

//		if(av_read_play(format_ctx) < 0)
//			break;

		// find suitable decoder for video 

		codec = avcodec_find_decoder(format_ctx->streams[video_stream_index]->codecpar->codec_id);

		if(codec == 0)
			break;

		// initialize decoder

		codec_ctx = avcodec_alloc_context3(codec);
		avcodec_parameters_to_context(codec_ctx, format_ctx->streams[video_stream_index]->codecpar);

		if(avcodec_open2(codec_ctx, codec, NULL) < 0)
			break;

		// allocate picture buffer

		picture = av_frame_alloc();

		// allocate packet of input data

		packet = av_packet_alloc();

		if(stream[handle].target_width > 0 && stream[handle].target_height > 0)
			{
			// allocate RGB picture buffer if we know frame dimensions beforehand
			picture_rgb = mt_ffmpeg_stream_decoder_init_frame_rgb(stream[handle].target_width, stream[handle].target_height);
			}

		// all done
		opened_ok = 1;
		stream[handle].status = FFMPEG_STREAM_STATUS_OK;

		break;
		}

	// stream opened, receive data and decode frames

	if(opened_ok && !stream[handle].is_closing)
		{
		// grabbing frames now

		while(!stream[handle].is_closing)
			{
			// try to read next frame or block until it is received

			if(av_read_frame(format_ctx, packet) >= 0)
				{
				// discard frames from other elementary streams (audio)

				if(packet->stream_index == video_stream_index)
					{
					// send raw packet to decoder

					if(avcodec_send_packet(codec_ctx, packet) == 0)
						{
						// decoding complete?

						if(avcodec_receive_frame(codec_ctx, picture) == 0)
							{
							// convert decoded YUV frame to RGB
							if(conversion_ctx == 0)
								{
								if(picture_rgb == 0)
									{
									// allocate YUV frame buffer
									stream[handle].target_width = picture->width;
									stream[handle].target_height = picture->height;
									stream[handle].framebuf = malloc(picture->width * picture->height * 3);

									// allocate RGB frame
									picture_rgb = mt_ffmpeg_stream_decoder_init_frame_rgb(stream[handle].target_width, stream[handle].target_height);
									}
								// initialize YUV to RGB conversion context

								conversion_ctx = sws_getContext(picture->width,
																picture->height,
																picture->format,
																picture_rgb->width,
																picture_rgb->height,
																picture_rgb->format,
																SWS_FAST_BILINEAR | SWS_FULL_CHR_H_INT | SWS_ACCURATE_RND,
																NULL,
																NULL,
																NULL);
								}

							sws_scale(conversion_ctx, picture->data, picture->linesize, 0, picture->height, picture_rgb->data, picture_rgb->linesize);

							//picture_rgb = avFrameConvertPixelFormat(picture, AV_PIX_FMT_RGB24, stream[handle].target_width, stream[handle].target_height);

							// guard access to framebuf with critical section, 
							// so main thread will not interfere while we are copying data
#ifdef USE_WINDOWS_THREADING
							EnterCriticalSection(&(stream[handle].cs_lock_frame));
#endif
#ifdef USE_PTHREADS
							pthread_mutex_lock(&(stream[handle].cs_lock_frame));
#endif
							// copy converted RGB frame to buffer
							memcpy(stream[handle].framebuf, picture_rgb->data[0], stream[handle].target_width * stream[handle].target_height * 3);

							// signal new frame available
							stream[handle].status = FFMPEG_STREAM_STATUS_NEW_FRAME;
#ifdef USE_WINDOWS_THREADING
							LeaveCriticalSection(&(stream[handle].cs_lock_frame));
#endif
#ifdef USE_PTHREADS
							pthread_mutex_unlock(&(stream[handle].cs_lock_frame));
#endif
						}
						}
					}

				// discard packet

				av_packet_unref(packet);
				}
			else
				break;
			}
		}

	// either we encountered some error or stream was closed by calling mt_ffmpeg_stream_decoder_close() from other thread

	stream[handle].status = FFMPEG_STREAM_STATUS_ERROR;

	// cleanup

	if(packet != 0)
		av_packet_free(&packet);

	if(picture_rgb != 0)
		{
		av_freep(&picture_rgb->data[0]);

		av_frame_free(&picture_rgb);
		}

	if(conversion_ctx != 0)
		sws_freeContext(conversion_ctx);

	if(picture != 0)
		av_frame_free(&picture);

	if(picture_buffer != 0)
		av_free(picture_buffer);

	if(codec_ctx != 0)
		avcodec_free_context(&codec_ctx);

	if(format_ctx != 0)
		avformat_free_context(format_ctx);
	}

// grab next frame, should be called only if mt_ffmpeg_stream_decoder_get_status() returned FFMPEG_STREAM_STATUS_NEW_FRAME
// should be called from main application thread!

void mt_ffmpeg_stream_decoder_grab_frame(int handle, unsigned char* framebuf)
	{
	// guard access to framebuf with critical section
	// ensure that working thread will not interfere while we are copying data
#ifdef USE_WINDOWS_THREADING
	EnterCriticalSection(&(stream[handle].cs_lock_frame));
#endif
#ifdef USE_PTHREADS
	pthread_mutex_lock(&(stream[handle].cs_lock_frame));
#endif

	// copy data
	memcpy(framebuf, stream[handle].framebuf, stream[handle].target_width * stream[handle].target_height * 3);
	stream[handle].status = FFMPEG_STREAM_STATUS_OK;

#ifdef USE_WINDOWS_THREADING
	LeaveCriticalSection(&(stream[handle].cs_lock_frame));
#endif
#ifdef USE_PTHREADS
	pthread_mutex_unlock(&(stream[handle].cs_lock_frame));
#endif
	}




// initialize frame with given width and height

AVFrame* mt_ffmpeg_stream_decoder_init_frame_rgb(int width, int height)
	{
	AVFrame* picture = av_frame_alloc();

	if(av_image_alloc(picture->data, picture->linesize, width, height, AV_PIX_FMT_RGB24, 1) < 0)
		{
		av_frame_free(&picture);
		return NULL;
		}

	picture->width = width;
	picture->height = height;
	picture->format = AV_PIX_FMT_RGB24;

	return picture;
	}

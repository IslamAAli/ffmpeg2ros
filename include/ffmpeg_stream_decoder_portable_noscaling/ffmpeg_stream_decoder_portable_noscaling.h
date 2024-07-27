#ifndef FFMPEG_STREAM_DECODER_H
#define FFMPEG_STREAM_DECODER_H

void mt_ffmpeg_stream_decoder_init();
void mt_ffmpeg_stream_decoder_done();

int mt_ffmpeg_stream_decoder_open(const char* uri, int width, int height);
void mt_ffmpeg_stream_decoder_close(int handle);

// status codes

#define FFMPEG_STREAM_STATUS_CONNECTING 0
#define FFMPEG_STREAM_STATUS_ERROR 1
#define FFMPEG_STREAM_STATUS_OK 2
#define FFMPEG_STREAM_STATUS_NEW_FRAME 3

int mt_ffmpeg_stream_decoder_get_status(int handle);

int mt_ffmpeg_stream_decoder_get_frame_width(int handle);
int mt_ffmpeg_stream_decoder_get_frame_height(int handle);

void mt_ffmpeg_stream_decoder_grab_frame(int handle, unsigned char* framebuf);

#endif // FFMPEG_STREAM_DECODER_H

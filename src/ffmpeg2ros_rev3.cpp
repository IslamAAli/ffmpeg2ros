//ffmpeg2ros_rev3.cpp -July 26/2024 -enable half scale
//ffmpeg2ros_rev2.cpp -July 26/2024 -enable greyscale
//ffmpeg2ros_rev1.cpp -July 26/2024 -bring in ROS code from 'test_pattern_640_480_RGB_rev3.cpp'
//									ros@ros-ThinkPad-T460s:~/cvrg_ws$ ls src/test_pattern_gen/src/test_pattern_640_480_RGB_rev3.cpp
//												-only outputs RGB and no half size
/*
ros@ros-ThinkPad-T460s:~$ rosmsg info sensor_msgs/Image
std_msgs/Header header
  uint32 seq
  time stamp
  string frame_id
uint32 height
uint32 width
string encoding
uint8 is_bigendian
uint32 step
uint8[] data
*/

//might have useful info:  /opt/ros/noetic/include/sensor_msgs/image_encodings.h

//-files from ROS laptop -first got working just non-ROS c program
//			ros@ros-ThinkPad-T460s:~/network_programming/ffmpeg/frame_grabber_ffmpeg_2024_portable_noscaling/
//frame_grabber_ffmpeg_2024_veaceslav_rev1_made_portable.c -July 24/2024 -add pthread changes as per 'frame_grabber_ffmpeg_2024_portable' project
//frame_grabber_ffmpeg_2024_veaceslav_rev1.c -July 24/2024 -Veaceslav's upgrade to use native resolution
//frame_grabber_ffmpeg_2024_veaceslav_rev0.c -July 22/2024 -use Veaceslav's ffmpeg made for the Oculus Rift project by itself in simple program
//frame_grabber_ffmpeg_2024_veaceslav_rev1.c -July 24/2024 -grab frames in native resolution


#define LINUX_VERSION
//#define WINDOWS_VERSION
//#define RASPI_VERSION

//#include <stdio.h>
//#include <stdlib.h>
//#include <string.h>
//#include <math.h>

#include "ros/ros.h"
#include "std_msgs/String.h"
#include "sensor_msgs/Image.h"

char write_ppm(char* file_name, char* comment, unsigned char* image, int width, int height);

/*
//CPP might need extern "C"
#ifdef WINDOWS_VERSION
 //extern "C" {
    //#include "ffmpeg_stream_decoder_portable.h"	//fixed rescaled image for OpenGL purposes only version
    #include "ffmpeg_stream_decoder_portable_1.h"	//merging V's July 24 addition of native resolution
 //          }
#endif
*/

#ifdef LINUX_VERSION
 #define USE_PTHREADS
#endif


//#include "ffmpeg_stream_decoder_portable_noscaling.h"		//include .c instead to make it build
extern "C" {
			  #include "ffmpeg_stream_decoder_portable_noscaling.c"
			  }

int main(int argc, char **argv)
{
   int i,j;
   unsigned char *rgb_image=NULL;
	unsigned char *grey_image=NULL;
   int width=0,height=0;
	int width_out=0,height_out=0;
   char rgb_greybar=1;
   char full_halfbar=1;
   //
   char rtsp_stream_address[512];
   int rtsp_stream_handle;
   int grab_num=0;

	//conventional (not ROS) command line params
	//for(int i=0;i<argc;i++) printf("argv[%d]=<%s>\n",i,argv[i]);
	for(int i=1;i<argc;i++)
		{
		if((strcmp(argv[i],"grey")==0)||(strcmp(argv[1],"GREY")==0))	
			{
			rgb_greybar=0;
			printf("command line arg GREY detected\n");
			}
		if((strcmp(argv[i],"half")==0)||(strcmp(argv[1],"HALF")==0))	
			{
			full_halfbar=0;
			printf("command line arg HALF detected\n");
			}
		}

   //strcpy(rtsp_stream_address, "rtsp://192.168.0.164:554/live/av0");
   //strcpy(rtsp_stream_address,"rtsp://10.0.0.67:554/user=admin_password=ssafd4F_channel=0_stream=1.sdp?real_stream");
   //strcpy(rtsp_stream_address,"rtsp://10.0.0.204:554/user=admin_password=ssafd4F_channel=0_stream=0.sdp?real_stream");
   strcpy(rtsp_stream_address, "rtsp://192.168.1.11:8554/inhand");

   mt_ffmpeg_stream_decoder_init();
   rtsp_stream_handle=mt_ffmpeg_stream_decoder_open(rtsp_stream_address,0,0);

	//start ROS node and advertise topic
	ros::init(argc, argv, "ffmpeg2ros");
	ROS_INFO(" 'ffmpeg2ros' node receives an IP video stream, such as an RTSP:// feed");
	ROS_INFO("      and outputs to '/ffmpeg2ros/rgb' topic");
	ROS_INFO("      or to '/ffmpeg2ros/grey' topic if \"grey\" command line arg given");
	ROS_INFO("Press CTRL-Z to stop program, then 'kill -9 %X' to end it (where X is where it's listed in 'jobs')");
	ROS_INFO("----");

	ros::NodeHandle n;
	//advertise available topic  -5 means hold max buffer of 5 images if subscriber is slow
	ros::Publisher img_pub;

	if((rgb_greybar==1)&&(full_halfbar==1))
		printf("no command line arg detected\n");
	if(rgb_greybar) 	
		{
		img_pub = n.advertise<sensor_msgs::Image>("/ffmpeg2ros/rgb",5);
		if(full_halfbar) 	
			printf(" advertising full size RGB image topic (video) /ffmpeg2ros/rgb\n");
		else	
			printf("command line arg GREY detected\n"
			 		 " advertising half size RGB image topic (video) /ffmpeg2ros/rgb\n");
		}
	else 					
		{
		img_pub = n.advertise<sensor_msgs::Image>("/ffmpeg2ros/grey",5);
		if(full_halfbar) 	
			printf(" advertising full size greyscale image topic (video) /ffmpeg2ros/grey\n");
		else	
			printf(" advertising half size greyscale image topic (video) /ffmpeg2ros/grey\n");
		}


while(1)
   {
   if(mt_ffmpeg_stream_decoder_get_status(rtsp_stream_handle) == FFMPEG_STREAM_STATUS_NEW_FRAME)
      {
       // received new video frame
       if(rgb_image == NULL)
           {
           // now we know image resolution and can allocate buffer for the frame
           width = mt_ffmpeg_stream_decoder_get_frame_width(rtsp_stream_handle);
           height = mt_ffmpeg_stream_decoder_get_frame_height(rtsp_stream_handle);
           printf("Received first frame, w,h=%d,%d\n",width,height);
			  if(full_halfbar)	 {width_out=width; 	height_out=height;}
			  else					 {width_out=width/2; height_out=height/2;}
           printf("Will publish topic at w,h=%d,%d\n",width_out,height_out);

           rgb_image=(unsigned char*)malloc(width*height*3);
           if(rgb_image==NULL) {printf("Prob mallocing rgb_image\n");exit(1);}
           grey_image=(unsigned char*)malloc(width*height);
           if(grey_image==NULL) {printf("Prob mallocing grey_image\n");exit(1);}
           }

      mt_ffmpeg_stream_decoder_grab_frame(rtsp_stream_handle, rgb_image);
      /*
      char output_filename[256];
      sprintf(output_filename,"ffmpeg2024_%d.ppm",grab_num);
      write_ppm(output_filename,"frame_grabber_ffmpeg_2024.exe",rgb_image,width,height);
      printf("Wrote out <%s>\n",output_filename);
      */
      grab_num++;
      
      //if we actually have a frame and have allocated memory
      if(rgb_image!=NULL)
      	{
			//potential down-sample
			if(full_halfbar==0) 	
				{
				//width_out=width/2; 
				//height_out=height/2;
				for(int y=0;y<height_out;y++)
					{
					for(int x=0;x<width_out;x++)
						{
						unsigned char r0=rgb_image[((y*2+0)*width+x*2+0)*3+0], g0=rgb_image[((y*2+0)*width+x*2+0)*3+1];
						unsigned char b0=rgb_image[((y*2+0)*width+x*2+0)*3+2];
						unsigned char r1=rgb_image[((y*2+0)*width+x*2+1)*3+0], g1=rgb_image[((y*2+0)*width+x*2+1)*3+1];
						unsigned char b1=rgb_image[((y*2+0)*width+x*2+1)*3+2];
						unsigned char r2=rgb_image[((y*2+1)*width+x*2+0)*3+0], g2=rgb_image[((y*2+1)*width+x*2+0)*3+1];
						unsigned char b2=rgb_image[((y*2+1)*width+x*2+0)*3+2];
						unsigned char r3=rgb_image[((y*2+1)*width+x*2+1)*3+0], g3=rgb_image[((y*2+1)*width+x*2+1)*3+1];
						unsigned char b3=rgb_image[((y*2+1)*width+x*2+1)*3+2];
						//average 4 pixels
						rgb_image[(y*width_out+x)*3+0]=(unsigned char)( ( (int)r0+(int)r1+(int)r2+(int)r3 )/4 );  
						rgb_image[(y*width_out+x)*3+1]=(unsigned char)( ( (int)g0+(int)g1+(int)g2+(int)g3 )/4 );  
						rgb_image[(y*width_out+x)*3+2]=(unsigned char)( ( (int)b0+(int)b1+(int)b2+(int)b3 )/4 ); 
						}
					}
				}
	 
		 	//potentially convert to greyscale
			if(rgb_greybar==0)
				{
				for(int p=0;p<width_out*height_out;p++)
					{
					unsigned char red=rgb_image[p*3+0];
					unsigned char grn=rgb_image[p*3+1];
					unsigned char blu=rgb_image[p*3+2];
					grey_image[p]=(unsigned char)( ((int)red+(int)grn+(int)blu)/3 );
					}
				}
		  
		   //copy to ROS image and publish
			sensor_msgs::Image img_msg;

			// Copy the header and other relevant fields
			//img_msg.header = image_msg->header;
			img_msg.height = height_out;
			img_msg.width =  width_out;
			img_msg.is_bigendian = 0;
			int dataSize;
			if(rgb_greybar) 
				{
				img_msg.encoding = "rgb8";
				img_msg.step = width_out*3;
				dataSize = width_out*height_out*3; 
				img_msg.data.assign(rgb_image, rgb_image + dataSize);
				}
			else				 
				{
				img_msg.encoding = "mono8";	//mono8 for greyscale -see /opt/ros/noetic/include/sensor_msgs/image_encodings.h
				img_msg.step = width_out;
				dataSize = width_out*height_out; 
				img_msg.data.assign(grey_image, grey_image + dataSize);
				}
			// Publish the modified image
			img_pub.publish(img_msg);
      	}//if(rgb_image!=NULL)	 //if we actually have a frame and have allocated memory
      	
     	}//if(mt_ffmpeg_stream_decoder_get_status(...
	ros::spinOnce();
   }//while(1)

   //stop camera
   mt_ffmpeg_stream_decoder_done();

	ros::shutdown();
   if(rgb_image) free(rgb_image);
   if(grey_image) free(grey_image);

   return 0;
}





//retval 0=ok, -1=couldn't write
char write_ppm(char *file_name, char *comment, unsigned char *image,int width,int height)
{
FILE *out;
int i,j;

out=(FILE*)fopen(file_name,"wb");
if(out==NULL)
   {printf("PGM_FUNCTIONS.C error: Couldn't open %s for writing\n",file_name);return -1;}  //exit(1);}
fprintf(out,"P6\n#%s\n",comment);
fprintf(out,"%d %d\n255\n",width,height);
for(i=0;i<width*height*3;i++)
    {
    j=(int)(*(image+i));
    fputc(j,out);
    }
fclose(out);
return 0;
}

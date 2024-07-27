cmake_minimum_required(VERSION 3.0.2)
project(ffmpeg2ros)

find_package(catkin REQUIRED COMPONENTS
  roscpp
  std_msgs
)

find_package(Boost REQUIRED)

add_compile_options(-fpermissive)	

catkin_package(
  INCLUDE_DIRS include
  LIBRARIES ffmpeg_stream_decoder_portable_noscaling
  CATKIN_DEPENDS roscpp std_msgs
  DEPENDS Boost
)

find_package(PkgConfig REQUIRED)
pkg_check_modules(AVCODEC REQUIRED libavcodec)
pkg_check_modules(AVFORMAT REQUIRED libavformat)
pkg_check_modules(AVUTIL REQUIRED libavutil)
pkg_check_modules(SWSCALE REQUIRED libswscale)

include_directories(
  include
  /usr/include/x86_64-linux-gnu
  ${catkin_INCLUDE_DIRS}
  ${Boost_INCLUDE_DIRS}
  ${AVCODEC_INCLUDE_DIRS}
  ${AVFORMAT_INCLUDE_DIRS}
  ${AVUTIL_INCLUDE_DIRS}
  ${SWSCALE_INCLUDE_DIRS}
)

# Specify additional directories for library search
link_directories(
  /usr/lib/x86_64-linux-gnu
)

add_library(ffmpeg_stream_decoder_portable_noscaling
  src/ffmpeg_stream_decoder_portable_noscaling.c
)


target_link_libraries(ffmpeg_stream_decoder_portable_noscaling
  ${catkin_LIBRARIES}
  ${AVCODEC_LIBRARIES}
  ${AVFORMAT_LIBRARIES}
  ${AVUTIL_LIBRARIES}
  ${SWSCALE_LIBRARIES}
  pthread
)

add_executable(ffmpeg2ros src/ffmpeg2ros_rev3.cpp)

target_link_libraries(ffmpeg2ros
  ffmpeg_stream_decoder_portable_noscaling
  avcodec avformat avutil swscale -pthread
  ${catkin_LIBRARIES}
)

install(TARGETS ffmpeg2ros
  RUNTIME DESTINATION ${CATKIN_PACKAGE_BIN_DESTINATION}
)


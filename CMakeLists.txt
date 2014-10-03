cmake_minimum_required(VERSION 2.8)
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${CMAKE_CURRENT_SOURCE_DIR}/cmake)
project(gst-loop2rtmp)
add_executable(gst-loop2rtmp app.c)

# Compiler Options - we need to add more
# set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -std=c++0x -Wall -Wextra -Werror")

find_package(GSTREAMER REQUIRED)
find_package(GLIB2 REQUIRED)
find_package(GOBJECT REQUIRED)


include_directories(
    ${GSTREAMER_INCLUDE_DIRS}
    ${GLIB2_INCLUDE_DIRS}
    ${GOBJECT_INCLUDE_DIR}
    )

target_link_libraries(gst-loop2rtmp
    ${GSTREAMER_LIBRARIES}
    ${GLIB2_LIBRARIES}
    ${GOBJECT_LIBRARIES}
    )

install(TARGETS gst-loop2rtmp DESTINATION bin)
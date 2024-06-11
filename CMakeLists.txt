CMAKE_MINIMUM_REQUIRED(VERSION 3.9)

project(charliefax)

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    message("On a 64-bit system. Using -m32")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -m32")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -m32")
ENDIF()

set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -s")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -s")

find_program(686_GCC "i686-linux-gnu-gcc")
IF(686_GCC)
    message("Using i686-linux-gnu-gcc")
    set(CMAKE_C_COMPILER i686-linux-gnu-gcc)
ENDIF()

include(FetchContent)

set(slmodem-patch git apply ${CMAKE_CURRENT_SOURCE_DIR}/pkg-sl-modem.patch)

FetchContent_Declare(
    pkg-sl-modem
    GIT_REPOSITORY https://github.com/leggewie/pkg-sl-modem.git
    GIT_TAG master
    PATCH_COMMAND ${slmodem-patch}
    UPDATE_DISCONNECTED 1
)
FetchContent_MakeAvailable(pkg-sl-modem)

add_library(modem SHARED
    ${pkg-sl-modem_SOURCE_DIR}/modem/modem.c
    ${pkg-sl-modem_SOURCE_DIR}/modem/modem_datafile.c
    ${pkg-sl-modem_SOURCE_DIR}/modem/modem_at.c
    ${pkg-sl-modem_SOURCE_DIR}/modem/modem_timer.c
    ${pkg-sl-modem_SOURCE_DIR}/modem/modem_pack.c
    ${pkg-sl-modem_SOURCE_DIR}/modem/modem_ec.c
    ${pkg-sl-modem_SOURCE_DIR}/modem/modem_comp.c
    ${pkg-sl-modem_SOURCE_DIR}/modem/modem_param.c
    ${pkg-sl-modem_SOURCE_DIR}/modem/modem_debug.c
    ${pkg-sl-modem_SOURCE_DIR}/modem/homolog_data.c
    ${pkg-sl-modem_SOURCE_DIR}/modem/modem_cmdline.c
    ${pkg-sl-modem_SOURCE_DIR}/modem/dp_sinus.c
    ${pkg-sl-modem_SOURCE_DIR}/modem/dp_dummy.c
    ${pkg-sl-modem_SOURCE_DIR}/modem/sysdep_common.c
    ${pkg-sl-modem_SOURCE_DIR}/modem/dsplibs.o
)

target_include_directories(modem PUBLIC ${pkg-sl-modem_SOURCE_DIR}/modem)

add_library(image-converter STATIC src/image-converter.c)
target_link_libraries(image-converter tiff png)

add_executable(charliefax src/charliefax.c)
target_include_directories(charliefax PUBLIC ${pkg-sl-modem_SOURCE_DIR}/modem)
target_link_libraries(charliefax PRIVATE modem image-converter pq uuid)


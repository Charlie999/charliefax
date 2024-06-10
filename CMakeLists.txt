CMAKE_MINIMUM_REQUIRED(VERSION 3.9)

project(charliefax)

set(CMAKE_C_COMPILER i686-linux-gnu-gcc)

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

add_executable(charliefax src/charliefax.c)
target_include_directories(charliefax PUBLIC ${pkg-sl-modem_SOURCE_DIR}/modem)
target_link_libraries(charliefax PRIVATE modem pq uuid)
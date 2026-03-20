# SPDX-License-Identifier: Apache-2.0

# set(MY_OBJDUMP $ENV{HOME}/zephyr-sdk-0.17.4/arm-zephyr-eabi/bin/arm-zephyr-eabi-objdump)

# set_property(GLOBAL APPEND PROPERTY extra_post_build_byproducts
#  COMMAND ${MY_OBJDUMP} -S --disassemble ${CMAKE_BINARY_DIR}/app/libapp.a > ${CMAKE_BINARY_DIR}/zephyr/libapp.asm
# )

# west build -p always -b same54_gw2 app/gateway --sysbuild -d build-hello-signed -- -DSB_CONFIG_BOOTLOADER_MCUBOOT=y
# west flash -d build-hello-signed


cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(app_gateway)


FILE(GLOB app_sources src/*.c src/mecrisp-src/atsame54-ra/mecrisp-stellaris-atsame54p20.s)
option(INCLUDE_HTML_CONTENT "Include the HTML content" ON)
target_sources(app PRIVATE ${app_sources})


include(${ZEPHYR_BASE}/samples/net/common/common.cmake)
target_include_directories(app PRIVATE src/mecrisp-src/atsame54-ra/ src/mecrisp-src/common/)
# zephyr_linker_sources(mecrisp.ld)

target_link_libraries(app PRIVATE zephyr_interface zephyr)

zephyr_linker_sources(SECTIONS sections-rom.ld)

set(gen_dir ${ZEPHYR_BINARY_DIR}/include/generated/)

foreach(web_resource
  bview_bcr.html
  favicon.ico
  fernb_bcr.html
  upload_script.html
  fw_update.html
    )
  generate_inc_file_for_target(
    app
    src/static_web_resources/${web_resource}
    ${gen_dir}/${web_resource}.gz.inc
    --gzip
  )
  generate_inc_file_for_target(
    app
    src/static_web_resources/${web_resource}
    ${gen_dir}/${web_resource}.inc
  )
endforeach()

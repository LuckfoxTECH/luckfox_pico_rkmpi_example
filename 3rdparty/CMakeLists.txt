cmake_minimum_required(VERSION 3.15)

project(rknn_model_zoo_3rdparty_libs)

set(TARGET_SOC "rv1106")
set (TARGET_LIB_ARCH armhf_uclibc)

# rknn runtime
set(RKNN_PATH ${CMAKE_CURRENT_SOURCE_DIR}/rknpu2)
set(LIBRKNNRT ${RKNN_PATH}/${CMAKE_SYSTEM_NAME}/armhf-uclibc/librknnmrt.so)
set(LIBRKNNRT_INCLUDES ${RKNN_PATH}/include PARENT_SCOPE)
install(PROGRAMS ${LIBRKNNRT} DESTINATION lib)
set(LIBRKNNRT ${LIBRKNNRT} PARENT_SCOPE)

# rga
set(RGA_PATH ${CMAKE_CURRENT_SOURCE_DIR}/librga)
set(LIBRGA ${RGA_PATH}/${CMAKE_SYSTEM_NAME}/${TARGET_LIB_ARCH}/librga.a PARENT_SCOPE)
set(LIBRGA_INCLUDES ${RGA_PATH}/include PARENT_SCOPE)
install(PROGRAMS ${RGA_PATH}/${CMAKE_SYSTEM_NAME}/${TARGET_LIB_ARCH}/librga.so DESTINATION lib)



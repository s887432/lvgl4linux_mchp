cmake_minimum_required(VERSION 3.10)
project(lvgl_app C)

# 1. 強制設定為 ARM 交叉編譯 (確保編譯出來的格式正確)
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)
set(CROSS_COMPILE_PATH "/home/a20666/WorkArea/buildroot-2026.04/9x75_curiosity/buildroot-mchp/output/host/bin/arm-buildroot-linux-gnueabi")
set(CMAKE_C_COMPILER "${CROSS_COMPILE_PATH}-gcc")
set(CMAKE_CXX_COMPILER "${CROSS_COMPILE_PATH}-g++")

# 2. 設定專案目錄路徑
set(LIBPLANES_PATH "/home/a20666/WorkArea/buildroot-2026.04/9x75_curiosity/buildroot-mchp/output/build/libplanes-v2.0.1")

# 3. 搜尋系統函式庫
find_package(PkgConfig REQUIRED)
# 設定 PKG_CONFIG_PATH 指向你的 Buildroot SDK sysroot
# 這會強迫 pkg-config 使用 SDK 內的路徑，而不是你的 PC 路徑
set(ENV{PKG_CONFIG_PATH} "/home/a20666/WorkArea/buildroot-2026.04/9x75_curiosity/buildroot-mchp/output/host/lib/pkgconfig:/home/miles/sam9x75/25.10/buildroot-at91/output/host/usr/lib/pkgconfig")
set(ENV{PKG_CONFIG_SYSROOT_DIR} "/home/a20666/WorkArea/buildroot-2026.04/9x75_curiosity/buildroot-mchp/host/arm-buildroot-linux-gnueabi/sysroot")
pkg_check_modules(DRM REQUIRED libdrm)
pkg_check_modules(CAIRO REQUIRED cairo)
file(GLOB_RECURSE LVGL_SOURCES "src/lvgl/src/*.c")
add_library(lvgl STATIC ${LVGL_SOURCES})
# 4. 設定所有標頭檔目錄
include_directories(
    src
    include
    src/lvgl
    src/lvgl/src
    src/lvgl/demos
    ${LIBPLANES_PATH}/include
    ${DRM_INCLUDE_DIRS}
    ${CAIRO_INCLUDE_DIRS}
)

# 5. 定義共用原始碼 (排除掉 main 檔案)
# 這裡使用 file 蒐集所有檔案，然後用 list REMOVE_ITEM 把 main 檔剔除
file(GLOB_RECURSE ALL_SOURCES "src/*.c")

# 2. 定義 app 目錄
add_executable(app src/app.c ${CORE_SOURCES})
target_link_libraries(app PRIVATE ${DRM_LIBRARIES} ${CAIRO_LIBRARIES} lvgl pthread m input udev ${LIBPLANES_PATH}/src/libplanes.so)

# 3. 定義 app1 目錄
add_executable(app1 src/app1.c ${CORE_SOURCES})
target_link_libraries(app1 PRIVATE ${DRM_LIBRARIES} ${CAIRO_LIBRARIES} lvgl pthread m input udev ${LIBPLANES_PATH}/src/libplanes.so)

# 4. 定義 app2 目錄
add_executable(app2 src/app2.c ${CORE_SOURCES})
target_link_libraries(app2 PRIVATE ${DRM_LIBRARIES} ${CAIRO_LIBRARIES} lvgl pthread m input udev ${LIBPLANES_PATH}/src/libplanes.so)
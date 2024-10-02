cmake_minimum_required(VERSION 3.5)

# Project and source files
project(Cfx)

set(CMAKE_CXX_STANDARD 17)  # Use C++17 standard
set(CMAKE_CXX_FLAGS "-Wall -O3")  # Set common compiler flags
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "build")
# Source files
set(SOURCE_FILES
  utils/userinput/user_input.cpp
  utils/bmp/writer/bmp_writer.cpp
  utils/bmp/reader/bmp_reader.cpp
  utils/json/json.cpp
  func/img_creation/create_img.cpp
  func/file_creation/create_file.cpp
  utils/logger/logger.cpp
  main.cpp
)

# Target executable name
set(TARGET_NAME cfx)

# Platform specific configurations
if(WIN32)  # Windows platform
  set(CMAKE_CXX_COMPILER x86_64-w64-mingw32-g++)
  set(TARGET_SUFFIX ".exe")
  set(CMAKE_EXE_LINKER_FLAGS "-static-libgcc -static-libstdc++")
elseif(ANDROID)  # Android platform
  find_package(NDK REQUIRED)
  set(CMAKE_CXX_COMPILER ${NDK_TOOLCHAIN_PREFIX}-clang++)
  set(TARGET_SUFFIX ".android")
  set(CMAKE_EXE_LINKER_FLAGS "-static-libgcc -static-libstdc++")
else()  
endif()

# Target executable
add_executable(${TARGET_NAME} ${SOURCE_FILES})

# Clean target
add_custom_target(custom_clean COMMAND rm -rf ${TARGET_NAME} ${TARGET_NAME}${TARGET_SUFFIX} *.o *.win.o *.android.o)

# Phony targets

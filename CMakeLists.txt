cmake_minimum_required(VERSION 3.20)
project(McGuffin CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# libraries come from vcpkg.json (vcpkg manifest mode)
find_package(Boost CONFIG REQUIRED COMPONENTS asio beast json)
find_package(OpenSSL REQUIRED)
find_path(PSL_INCLUDE_DIR libpsl.h REQUIRED)
find_library(PSL_LIBRARY NAMES psl REQUIRED)

add_executable(McGuffin main.cpp crawler.cpp dnsResolver.cpp extractor.cpp fetch.cpp urlStandard.cpp)
target_include_directories(McGuffin PRIVATE ${PSL_INCLUDE_DIR})
target_link_libraries(McGuffin PRIVATE Boost::asio Boost::beast Boost::json OpenSSL::SSL OpenSSL::Crypto ${PSL_LIBRARY})

# windows: target windows 10 and link the socket and crypto system libraries
if(WIN32)
    target_compile_definitions(McGuffin PRIVATE _WIN32_WINNT=0x0A00)
    target_link_libraries(McGuffin PRIVATE ws2_32 mswsock crypt32 bcrypt)
endif()

# mingw: beast makes very large object files; use the big object format
if(MINGW)
    target_compile_options(McGuffin PRIVATE -Wa,-mbig-obj)
endif()
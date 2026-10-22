cmake_minimum_required(VERSION 3.20)
project(McGuffin)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# windows 10+ for asio
add_compile_definitions(_WIN32_WINNT=0x0A00)

include_directories(${CMAKE_SOURCE_DIR}/third_party/include)
link_directories(${CMAKE_SOURCE_DIR}/third_party/lib)

add_executable(McGuffin main.cpp
        read.h
        url.h
        dnsResolver.cpp
        dnsResolver.h
        crawler.cpp
        crawler.h
        urlStandard.cpp
        urlStandard.h
        fetch.cpp
        fetch.h
        extractor.cpp
        extractor.h)

target_link_libraries(McGuffin PRIVATE
        boost_json-gcc15-mt-x64-1_92
        boost_container-gcc15-mt-x64-1_92
        psl
        ssl
        crypto
        ws2_32
        mswsock
        crypt32
        bcrypt
)
cmake_minimum_required(VERSION 3.10)
project(McGuffin)

set(CMAKE_CXX_STANDARD 23)

include_directories(${CMAKE_SOURCE_DIR}/third_party)

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
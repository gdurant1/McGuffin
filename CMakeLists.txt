cmake_minimum_required(VERSION 4.3)
project(McGuffin)

set(CMAKE_CXX_STANDARD 23)

add_executable(McGuffin main.cpp
        read.h
        url.h
        dnsResolver.cpp
        dnsResolver.h
        crawler.cpp
        crawler.h)

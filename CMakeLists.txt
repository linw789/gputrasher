# Copyright (c) 2022 Advanced Micro Devices, Inc. All rights reserved. #

cmake_minimum_required(VERSION 3.15)

project(GpuTrasher)

add_executable(gputrasher WIN32)

target_sources(gputrasher
    PRIVATE
        src/hello-triangle.cpp
)

target_link_libraries(gputrasher
    PRIVATE
        # These should already be in the default search directory.
        d3d12.lib
        DXGI.lib
)

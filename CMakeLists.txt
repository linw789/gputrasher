# Copyright (c) 2022 Advanced Micro Devices, Inc. All rights reserved. #

devdriver_executable(gpuTrasher WIN32)

install(TARGETS gpuTrasher DESTINATION bin)

target_sources(gpuTrasher
    PRIVATE
        main.cpp
)

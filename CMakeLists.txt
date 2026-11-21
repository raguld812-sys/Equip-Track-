cmake_minimum_required(VERSION 3.20)
project(LabEquipmentTracker LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "Lab Equipment Tracker uses the Windows desktop API and must be built on Windows.")
endif()

add_executable(LabEquipmentTracker WIN32 main.cpp)
target_compile_features(LabEquipmentTracker PRIVATE cxx_std_17)
target_link_libraries(LabEquipmentTracker PRIVATE comctl32)

if(MSVC)
    target_compile_options(LabEquipmentTracker PRIVATE /W4 /utf-8)
elseif(MINGW)
    target_link_options(LabEquipmentTracker PRIVATE -municode -static)
endif()

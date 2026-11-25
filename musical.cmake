add_library(ia2a_musical STATIC
    src/analysis/Features.cpp
    src/calibration/Calibrator.cpp)
target_link_libraries(ia2a_musical PUBLIC ia2a_dsp)
target_include_directories(ia2a_musical PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")

if(BUILD_TESTING)
    add_executable(FeaturesTest tests/analysis/FeaturesTest.cpp)
    add_executable(CalibratorTest tests/calibration/CalibratorTest.cpp)
    foreach(musical_test IN ITEMS FeaturesTest CalibratorTest)
        target_include_directories(${musical_test} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
        target_link_libraries(${musical_test} PRIVATE ia2a_musical)
        add_test(NAME ${musical_test} COMMAND ${musical_test})
    endforeach()
endif()

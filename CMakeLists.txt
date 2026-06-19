cmake_minimum_required(VERSION 3.21)
project(BlackHoleSim)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Dependencies
find_package(GLEW REQUIRED)
find_package(glfw3 CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(OpenGL REQUIRED)

# Common dependencies
set(DEPS glfw GLEW::GLEW glm::glm OpenGL::GL)

# 2D Lensing executable
add_executable(BlackHole2D 2D_lensing.cpp)
target_link_libraries(BlackHole2D PRIVATE ${DEPS})
target_include_directories(BlackHole2D PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# 3D Black Hole executable
# On Windows, embed a manifest declaring Per-Monitor-V2 DPI awareness so a
# wallpaper window covers the real pixel resolution instead of a virtualized box.
set(BH3D_SOURCES black_hole.cpp)
if(WIN32)
    enable_language(RC)
    list(APPEND BH3D_SOURCES app.rc)
endif()
add_executable(BlackHole3D ${BH3D_SOURCES})
target_link_libraries(BlackHole3D PRIVATE ${DEPS})
if(WIN32)
    # DirectComposition wallpaper path (behind-icons on Win11 24H2)
    target_link_libraries(BlackHole3D PRIVATE d3d11 dcomp dxgi uuid gdi32)
endif()
target_include_directories(BlackHole3D PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Shader files (copy to output dir)
file(GLOB SHADERS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.vert"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.frag"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.comp"
)

foreach(shader ${SHADERS})
    add_custom_command(TARGET BlackHole3D POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${shader}
        $<TARGET_FILE_DIR:BlackHole3D>
    )
endforeach()


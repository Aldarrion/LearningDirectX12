cmake_minimum_required( VERSION 3.16.1 ) # Latest version of CMake when this file was created.

# Use solution folders to organize projects
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# Make sure DLL and EXE targets go to the same directory.
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib) # Output directory for static lib (.LIB)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin) # Output directory for shared lib (.DLL)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin) # Output directory for executables (.EXE)

project( LearningDirectX12 LANGUAGES CXX )

# Enable multithreaded builds
if( MSVC )
    add_compile_options(/MP)
endif()

set( ASSIMP_BUILD_ASSIMP_TOOLS OFF CACHE BOOL "Build Assimp Tools" FORCE )
set( ASSIMP_BUILD_SAMPLES OFF CACHE BOOL "Build Assimp Samples" FORCE )
set( ASSIMP_BUILD_TESTS OFF CACHE BOOL "Build Assimp Tests" FORCE )

add_subdirectory( assimp )

set_target_properties( assimp IrrXML uninstall UpdateAssimpLibsDebugSymbolsAndDLLs zlib zlibstatic 
    PROPERTIES
        FOLDER assimp 
)

add_subdirectory( DirectXTex )
add_subdirectory( DX12Lib )
add_subdirectory( Tutorial1 )
add_subdirectory( Tutorial2 )
add_subdirectory( Tutorial3 )
add_subdirectory( Tutorial4 )

# Set the startup project.
set_directory_properties( PROPERTIES 
    VS_STARTUP_PROJECT Tutorial4
)

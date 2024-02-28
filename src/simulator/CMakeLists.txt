########################################
###      DRAMSys::simulator          ###
########################################

project(DRAMSys_Simulator)

# Gather all source and header files
file(GLOB_RECURSE SOURCE_FILES CONFIGURE_DEPENDS *.cpp)
file(GLOB_RECURSE HEADER_FILES CONFIGURE_DEPENDS *.h;*.hpp)

# Exclude main.cpp from the source files list
list(FILTER SOURCE_FILES EXCLUDE REGEX "main.cpp")

# Create a shared library instead of a static library
add_library(${PROJECT_NAME} SHARED ${SOURCE_FILES} ${HEADER_FILES})

# Specify include directories
target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# Link libraries
target_link_libraries(${PROJECT_NAME}
    PUBLIC
        Threads::Threads
        DRAMSys::libdramsys
)

# Source group for better organization in IDEs
build_source_group()

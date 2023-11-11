# vim: syntax=cmake
# ----------------------------------------------------------------------------
# setup mpp codec config first
# ----------------------------------------------------------------------------
include(codecs.cmake)
include(vproc.cmake)

# ----------------------------------------------------------------------------
# add include directory
# ----------------------------------------------------------------------------
include_directories(inc)
include_directories(common)
include_directories(base/inc)
include_directories(codec/inc)
include_directories(hal/inc)
include_directories(hal/common)
include_directories(vproc/inc)

# ----------------------------------------------------------------------------
# add mpp base component
# ----------------------------------------------------------------------------
add_subdirectory(base)

# ----------------------------------------------------------------------------
# add codec parsers
# ----------------------------------------------------------------------------
add_subdirectory(codec)

# ----------------------------------------------------------------------------
# add video processor
# ----------------------------------------------------------------------------
add_subdirectory(vproc)

# ----------------------------------------------------------------------------
# add register generation hal
# ----------------------------------------------------------------------------
add_subdirectory(hal)

# ----------------------------------------------------------------------------
# add mpp implement
# ----------------------------------------------------------------------------
set (MPP_SRC
    mpp_info.cpp
    mpp.cpp
    mpp_impl.cpp
    mpi.cpp
    )

add_library(${MPP_STATIC} STATIC ${MPP_SRC})
set_target_properties(${MPP_STATIC} PROPERTIES FOLDER "mpp")
set_target_properties(${MPP_STATIC} PROPERTIES CLEAN_DIRECT_OUTPUT 1)
target_link_libraries(${MPP_STATIC} mpp_codec mpp_hal mpp_vproc ${ASAN_LIB}
                      ${BEGIN_WHOLE_ARCHIVE} mpp_base ${END_WHOLE_ARCHIVE})

install(TARGETS ${MPP_STATIC} LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}")

add_library(cad_core STATIC Geometry.cpp Entity.cpp Document.cpp SpatialIndex.cpp Editing.cpp)
target_include_directories(cad_core PUBLIC "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(cad_core PUBLIC Qt6::Core Qt6::Gui)

add_library(cad_core STATIC Geometry.cpp Entity.cpp TextLayout.cpp Document.cpp DimensionLayout.cpp SpatialIndex.cpp Editing.cpp Measure.cpp ContourOperations.cpp)
target_include_directories(cad_core PUBLIC "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(cad_core PUBLIC Qt6::Core Qt6::Gui)

add_library(cad_io STATIC DxfAdapter.cpp DwgAdapter.cpp)
target_link_libraries(cad_io PUBLIC cad_core Qt6::Core)

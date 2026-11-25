add_library(cad_engineering STATIC Analysis.cpp Panelization.cpp FpcMaterialPlanner.cpp)
target_link_libraries(cad_engineering PUBLIC cad_core)

add_library(cad_ui STATIC CadCanvas.cpp MainWindow.cpp)
target_link_libraries(cad_ui PUBLIC cad_core cad_io cad_engineering Qt6::Widgets Qt6::Concurrent Qt6::PrintSupport)
add_executable(CADWorkbench WIN32 main.cpp)
target_link_libraries(CADWorkbench PRIVATE cad_ui)

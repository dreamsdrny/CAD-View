set(CAD_BRAND_NAME "CAD")
set(CAD_BRAND_ICON_LABEL "Qiao")
set(CAD_BRAND_ACCENT "#a78bfa")
set(CAD_BRAND_ACCENT_LIGHT "#ddd6fe")
set(CAD_BRAND_ACCENT_SURFACE "#35264d")

set(brand_generated_dir "${CMAKE_BINARY_DIR}/generated/branding")
file(MAKE_DIRECTORY "${brand_generated_dir}")
configure_file(BrandConfig.h.in "${brand_generated_dir}/BrandConfig.h" @ONLY)

add_executable(cad_icon_generator "${PROJECT_SOURCE_DIR}/tools/branding/IconGenerator.cpp")
target_include_directories(cad_icon_generator PRIVATE "${brand_generated_dir}")
target_link_libraries(cad_icon_generator PRIVATE Qt6::Gui)

set(CAD_ICON_FILE "${brand_generated_dir}/qiao.ico")
set(CAD_ICON_PNG "${brand_generated_dir}/qiao.png")
add_custom_command(
    OUTPUT "${CAD_ICON_FILE}" "${CAD_ICON_PNG}"
    COMMAND ${CMAKE_COMMAND} -E env "QT_QPA_PLATFORM=offscreen"
            $<TARGET_FILE:cad_icon_generator> "${brand_generated_dir}"
    DEPENDS cad_icon_generator
    COMMENT "Generating purple Qiao Windows and Qt icons"
    VERBATIM
)
add_custom_target(cad_brand_assets DEPENDS "${CAD_ICON_FILE}" "${CAD_ICON_PNG}")

configure_file("${PROJECT_SOURCE_DIR}/resources/branding.qrc.in"
               "${brand_generated_dir}/branding.qrc" @ONLY)
add_library(cad_branding STATIC AppBrand.cpp "${brand_generated_dir}/branding.qrc")
add_dependencies(cad_branding cad_brand_assets)
target_include_directories(cad_branding PUBLIC "${PROJECT_SOURCE_DIR}/src" PRIVATE "${brand_generated_dir}")
target_link_libraries(cad_branding PUBLIC Qt6::Gui Qt6::Widgets)

if(WIN32)
    enable_language(RC)
    configure_file("${PROJECT_SOURCE_DIR}/resources/windows/CAD.rc.in"
                   "${brand_generated_dir}/CAD.rc" @ONLY)
    set(CAD_WINDOWS_RESOURCE "${brand_generated_dir}/CAD.rc" PARENT_SCOPE)
    set_source_files_properties("${brand_generated_dir}/CAD.rc" PROPERTIES OBJECT_DEPENDS "${CAD_ICON_FILE}")
endif()

add_library(pm3rrg_rdv4_whereami STATIC whereami/whereami.c)

target_compile_definitions(pm3rrg_rdv4_whereami PRIVATE WAI_PM3_TUNED)
target_include_directories(pm3rrg_rdv4_whereami INTERFACE whereami)
target_compile_options(pm3rrg_rdv4_whereami PRIVATE -Wall -O3)
set_property(TARGET pm3rrg_rdv4_whereami PROPERTY POSITION_INDEPENDENT_CODE ON)

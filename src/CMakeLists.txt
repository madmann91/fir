add_library(libfir
    node.c
    module.c
    dbg_info.c)
target_include_directories(libfir PUBLIC ../include)
set_target_properties(libfir PROPERTIES PREFIX "")

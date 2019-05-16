add_library (patch_symbolic STATIC patch-symbolic.c)

add_executable(symbolic main.c)

target_link_libraries(symbolic patch_symbolic)

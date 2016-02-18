set(content "cmake_minimum_required(VERSION 3.4)\n\nproject(buildsystem2)\n\nset(var1 123)\n\nset(var2 345)\n\nadd_executable(main main.cpp)\n\nadd_executable(m_other main.cpp)\n\nadd_library(foo foo.cpp)\n\nfunction(f1)\nendfunction()\n\ntarget_\nset(var3 345)\n\n\nadd_subdirectory(subdir)\n")

do_build(buildsystem2)

do_test(buildsystem2 bs2tests/tc_targets.json)

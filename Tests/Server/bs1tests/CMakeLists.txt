set(content [==[cmake_minimum_required(VERSION 3.4)\n\nproject(buildsystem1)\n\nset(var1 123)\n\n\nset(var2 345)\n\nset(var4 987)\nset(var5 987)\n]==])

do_build(buildsystem1)

do_test(buildsystem1 bs1tests/tc_protocol0_1.json)

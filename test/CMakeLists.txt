# Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
# SPDX-License-Identifier: BSD-3-Clause-Clear
# Differential tests: the same scenarios run against plugin_loader and, when
# PLUGIN_LOADER_COMPARE_UPSTREAM=ON, against the real class_loader.
#
#   scenarios_<flavor>          every scenario except linked_directly
#   scenarios_linked_<flavor>   same binary, but linked directly against libtest_a
#                               (a "non-pure" plugin library); runs linked_directly
#
# Run test/run_diff.sh <build-dir> to execute and compare them.

# ours       plugin_loader, plugin libraries marked -z nodelete: the same situation as
#            upstream, whose plugin libraries contain STB_GNU_UNIQUE symbols and so are
#            never really unmapped. Compared against upstream.
# deletable  plugin_loader with ordinary libraries (dlclose really unmaps them).
# upstream   ROS class_loader.
set(TEST_FLAVORS ours deletable)
if(PLUGIN_LOADER_COMPARE_UPSTREAM)
  # Only this test needs ROS (to find class_loader): source /opt/ros/<distro>/setup.bash.
  find_package(class_loader REQUIRED)
  list(APPEND TEST_FLAVORS upstream)
endif()

foreach(flavor ${TEST_FLAVORS})
  set(out_dir ${CMAKE_CURRENT_BINARY_DIR}/${flavor})

  if(flavor STREQUAL "upstream")
    set(impl_lib class_loader::class_loader)
    set(impl_def USE_UPSTREAM)
  else()
    set(impl_lib plugin_loader)
    set(impl_def "")
  endif()

  foreach(lib a b c)
    string(TOUPPER ${lib} lib_upper)
    set(target test_${lib}_${flavor})
    add_library(${target} SHARED plugins/test_plugins.cpp)
    target_compile_definitions(${target} PRIVATE TEST_LIB_${lib_upper} ${impl_def})
    target_include_directories(${target} PRIVATE include)
    target_link_libraries(${target} PRIVATE ${impl_lib})
    set_target_properties(${target} PROPERTIES
      OUTPUT_NAME test_${lib}
      LIBRARY_OUTPUT_DIRECTORY ${out_dir})
    if(flavor STREQUAL "ours")
      target_link_options(${target} PRIVATE LINKER:-z,nodelete)
    endif()
  endforeach()

  foreach(variant plain linked)
    if(variant STREQUAL "plain")
      set(exe scenarios_${flavor})
    else()
      set(exe scenarios_linked_${flavor})
    endif()
    add_executable(${exe} scenarios.cpp)
    target_compile_definitions(${exe} PRIVATE ${impl_def})
    target_include_directories(${exe} PRIVATE include)
    target_link_libraries(${exe} PRIVATE ${impl_lib} Threads::Threads)
    if(variant STREQUAL "linked")
      # --no-as-needed: nothing in the executable references libtest_a, but it must
      # be loaded at startup so that its static initializers run outside any
      # ClassLoader (a "non-pure" plugin library).
      target_link_options(${exe} PRIVATE LINKER:--no-as-needed)
      target_link_libraries(${exe} PRIVATE test_a_${flavor})
    endif()
    set_target_properties(${exe} PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${out_dir})
  endforeach()
endforeach()

# PluginRegistry: test backends + registry_test. Run: <build-dir>/test/registry_test <build-dir>/test/backends
set(backend_dir ${CMAKE_CURRENT_BINARY_DIR}/backends)
set(test_backend_targets "")
# test_backend(<class> <probe: 0 ok | 1 unsupported | 2 fault> <node> [extra definitions...])
function(test_backend cls probe node)
  set(target test_backend_${cls})
  add_library(${target} SHARED backends/test_backend.cpp)
  target_compile_definitions(${target} PRIVATE TEST_CLASS=${cls} TEST_PROBE=${probe}
    "TEST_NODE=\"${node}\"" ${ARGN})
  target_include_directories(${target} PRIVATE backends)
  target_link_libraries(${target} PRIVATE plugin_discovery::plugin_discovery)
  set_target_properties(${target} PROPERTIES LIBRARY_OUTPUT_DIRECTORY ${backend_dir})
  set(test_backend_targets ${test_backend_targets} ${target} PARENT_SCOPE)
endfunction()
test_backend(Alpha 0 /dev/a)
test_backend(Beta 0 /dev/shared)
test_backend(Gamma 1 "")
test_backend(Delta 2 "")
test_backend(Cust 0 /dev/shared)
test_backend(BetaCust 0 /dev/betacust)
test_backend(Old 0 "" TEST_IFACE_MINOR=0)
test_backend(AbiX 0 "" "PLUGIN_DISCOVERY_ABI_TAG=\"fake-abi\"")

add_executable(registry_test registry_test.cpp)
target_include_directories(registry_test PRIVATE backends)
target_link_libraries(registry_test PRIVATE plugin_discovery::plugin_discovery)
set_target_properties(registry_test PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
add_dependencies(registry_test ${test_backend_targets})

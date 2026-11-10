cmake_minimum_required(VERSION 3.21)  # PROJECT_IS_TOP_LEVEL needs 3.21
project(LockFreeQueue VERSION 1.0.0 LANGUAGES CXX)

include(GNUInstallDirs)

option(LOCKFREEQUEUE_BUILD_TESTS     "Build tests"            ${PROJECT_IS_TOP_LEVEL})
option(LOCKFREEQUEUE_BUILD_EXAMPLES  "Build examples"         ${PROJECT_IS_TOP_LEVEL})
option(LOCKFREEQUEUE_INSTALL         "Generate install rules" ${PROJECT_IS_TOP_LEVEL})
option(LOCKFREEQUEUE_SANITIZE_THREAD "Build tests with ThreadSanitizer" OFF)

if(PROJECT_IS_TOP_LEVEL AND NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

add_library(LockFreeQueue INTERFACE)
add_library(LockFreeQueue::LockFreeQueue ALIAS LockFreeQueue)
target_compile_features(LockFreeQueue INTERFACE cxx_std_23)
target_include_directories(LockFreeQueue INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)

find_package(Threads REQUIRED)

function(lockfreequeue_configure target)
  target_link_libraries(${target} PRIVATE LockFreeQueue::LockFreeQueue Threads::Threads)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4)
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
  endif()
endfunction()

if(LOCKFREEQUEUE_BUILD_TESTS)
  enable_testing()
  foreach(family IN ITEMS spsc spmc mpsc mpmc)
    add_executable(test_${family} tests/test_${family}.cpp)
    lockfreequeue_configure(test_${family})
    if(LOCKFREEQUEUE_SANITIZE_THREAD AND NOT MSVC)
      target_compile_options(test_${family} PRIVATE -fsanitize=thread -g -O1)
      target_link_options(test_${family} PRIVATE -fsanitize=thread)
    endif()
    add_test(NAME test_${family} COMMAND test_${family})
  endforeach()
endif()

if(LOCKFREEQUEUE_BUILD_EXAMPLES)
  foreach(example IN ITEMS mpsc_basic spsc_basic mpmc_basic)
    add_executable(example_${example} examples/${example}.cpp)
    lockfreequeue_configure(example_${example})
  endforeach()
endif()

if(LOCKFREEQUEUE_INSTALL)
  include(CMakePackageConfigHelpers)
  set(configDir ${CMAKE_INSTALL_LIBDIR}/cmake/LockFreeQueue)

  install(TARGETS LockFreeQueue EXPORT LockFreeQueueTargets)
  install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
  install(EXPORT LockFreeQueueTargets
          NAMESPACE LockFreeQueue::
          DESTINATION ${configDir})

  # The package config is a single line, so it is generated here instead of living in a cmake/ folder.
  file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/LockFreeQueueConfig.cmake
       "include(\"\${CMAKE_CURRENT_LIST_DIR}/LockFreeQueueTargets.cmake\")\n")
  write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/LockFreeQueueConfigVersion.cmake
    COMPATIBILITY SameMajorVersion ARCH_INDEPENDENT)
  install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/LockFreeQueueConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/LockFreeQueueConfigVersion.cmake
    DESTINATION ${configDir})
endif()

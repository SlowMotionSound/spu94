# src/standalone/CMakeLists.txt -- v1.7 testbed-only WAV loader
# Phase 21 / Task 1: this directory now holds only WavLoader.{cpp,h}, used
# exclusively by the Standalone build of spu94_plugin. The plugin target in
# src/plugin/CMakeLists.txt pulls WavLoader.cpp directly via a conditional
# target_sources() call gated on the Standalone format being present.
#
# This file is intentionally empty of targets -- it exists so the directory
# is a first-class CMake subdirectory and future testbed-only sources land
# here without restructuring the build.

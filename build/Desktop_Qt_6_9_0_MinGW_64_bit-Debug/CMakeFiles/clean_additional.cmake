# Additional clean files
cmake_minimum_required(VERSION 3.16)

if("${CONFIG}" STREQUAL "" OR "${CONFIG}" STREQUAL "Debug")
  file(REMOVE_RECURSE
  "CMakeFiles\\sea_autogen.dir\\AutogenUsed.txt"
  "CMakeFiles\\sea_autogen.dir\\ParseCache.txt"
  "sea_autogen"
  )
endif()

#!/bin/bash
rm -rf 3rd-party benchmarks docs doxygen image tfprof
sed -i -E 's/TF_BUILD_TESTS\(.*\)ON/TF_BUILD_TESTS\1OFF/g' CMakeLists.txt
sed -i -E 's/TF_BUILD_EXAMPLES\(.*\)ON/TF_BUILD_EXAMPLES\1OFF/g' CMakeLists.txt

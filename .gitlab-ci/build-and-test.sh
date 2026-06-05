#!/bin/bash
#
# This script is called from various CI jobs to build e-d-s, install
# it, and run the tests.  The script can be wrapped with various
# CFLAGS or other scripts, as a reusable build-and-test step.

set -eux -o pipefail

export SOURCE=$PWD
export PREFIX=$HOME/_prefix
mkdir $PREFIX

cmake -G "Ninja"                                \
      -DCMAKE_BUILD_TYPE=Release                \
      -DCMAKE_INSTALL_PREFIX=$PREFIX            \
      -DENABLE_GOA=ON                           \
      -DENABLE_GTK=ON                           \
      -DENABLE_GTK4=ON                          \
      -DENABLE_EXAMPLES=ON                      \
      -DENABLE_TESTS=ON                         \
      -DWITH_LIBDB=OFF                          \
      -DWITH_PHONENUMBER=ON                     \
      -DENABLE_INTROSPECTION=OFF                \
      -DENABLE_GI_DOCGEN=OFF                    \
      -DENABLE_GTK_DOC=OFF                      \
      -DENABLE_VALA_BINDINGS=OFF                \
      -DENABLE_INSTALLED_TESTS=OFF              \
      -DWITH_PRIVATE_DOCS=OFF                   \
      ..

ninja
ninja install
ctest --force-new-ctest-process --output-on-failure

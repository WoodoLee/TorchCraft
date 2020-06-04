# Copyright (c) 2017-present, Facebook, Inc.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

IF(WITH_CPIDLIB)
  # Building Placer  - enviroments and training...etc
  ADD_SUBDIRECTORY(building-placer)
  # compiling micrio battle env.
  ADD_SUBDIRECTORY(micro)
  # compile defiler env. 
  ADD_SUBDIRECTORY(defiler-micro)
ENDIF(WITH_CPIDLIB)

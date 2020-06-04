-- Copyright (c) 2015-present, Facebook, Inc.
-- All rights reserved.
-- 
-- This source code is licensed under the BSD-style license found in the
-- LICENSE file in the root directory of this source tree. An additional grant
-- of patent rights can be found in the PATENTS file in the same directory.

package = "TorchCraft"
version = "1.4-0"
source = {
    url = "git://github.com/torchcraft/torchcraft",
    tag = "v1.4-0",
}
description = {
    summary = "Connects Torch to StarCraft through BWAPI",
    detailed = [[
        Connects Torch to StarCraft through BWAPI,
        allows to receive StarCraft state and send it command.
    ]],
    homepage = "http://github.com/TorchCraft/TorchCraft",
    license = "BSD 3-clause"
}
dependencies = {
    "penlight",
    "torch >= 7.0",
}
build = {
    type = "command",
    build_command = [[
    cmake -E make_directory build && cd build && cmake .. -DCMAKE_BUILD_TYPE=Release -DLUALIB=$(LUALIB) -DLUA_INCDIR="$(LUA_INCDIR)" -DLUA_LIBDIR="$(LUA_LIBDIR)" -DLUADIR="$(LUADIR)" -DLIBDIR="$(LIBDIR)" -DCMAKE_INSTALL_PREFIX="$(PREFIX)" && $(MAKE) -j8
    ]],
    install_command = "cd build && $(MAKE) install",
    install = {
      lua = { 
        ["torchcraft._env"] = "lua/_env.lua",
        ["torchcraft.init"] = "lua/init.lua",
        ["torchcraft.replayer"] = "lua/replayer.lua",
        ["torchcraft.utils"] = "lua/utils.lua"
      }
    }
}


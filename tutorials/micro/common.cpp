/**
 * Copyright (c) 2017-present, Facebook, Inc.
 * All rights reserved.
 */

#include "common.h"
#include "flags.h"

namespace microbattles
{
  //TUPLE : CURRENT UNIT STATUS - health, numbers
  std::tuple<float, float, float, float> getUnitCountsHealth(cherrypi::State* state) 
  {
    //checking the sllies and enemies states
    auto allies = state->unitsInfo().myUnits();
    auto enemies = state->unitsInfo().enemyUnits();

    //tuple 0 : allies numbers
    float allyCount = allies.size();
    //tuple 1 : enemies numbers
    float enemyCount = enemies.size();

    //tuple 2 : ally's HP 0: inintializtion.
    float allyHp = 0;
    //tuple 3 : enemy's HP 0 : initialization.
    float enemyHp = 0;

    //check & iterating  - summing the health and shield.
    for (auto& ally : allies) 
    {
      allyHp += ally->unit.health + ally->unit.shield; // Include shield in HP
    }

    //check & iterating  - summing the health and shield(in case of protoss).
    for (auto& enemy : enemies) 
    {
      enemyHp += enemy->unit.health + enemy->unit.shield;
    }

    //returning the tuples
    return std::make_tuple(allyCount, enemyCount, allyHp, enemyHp);
  }

  //DOUBLE : UNIT MOVEMENTS
  double getMovementRadius(cherrypi::Unit* u)
  {
    //need to understands FLAG
    // cal pixel by pixel
    return u->topSpeed * FLAGS_frame_skip * 3 +
      std::max(u->unit.pixel_size_x, u->unit.pixel_size_y) / 2. /
      cherrypi::tc::BW::XYPixelsPerWalktile;
  }
  
  //to cuda  
  at::Device defaultDevice() {
    return FLAGS_gpu ? torch::Device("cuda") : torch::Device("cpu");
  }
} // namespace microbattles

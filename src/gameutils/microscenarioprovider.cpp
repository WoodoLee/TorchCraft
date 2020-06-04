/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "microscenarioprovider.h"

#include "buildtype.h"
#include "microplayer.h"
#include "modules/once.h"

namespace cherrypi {

namespace {

// We don't want to reuse the same BWAPI instances too much, because the
// internal structures might overflow (dead units are not freed, for example)
//
// The BWAPI ID limit is 10,000 -- this smaller number gives us some buffer
// against eg. Scarabs, other units that get produced during the game
int constexpr kMaxUnits = 9000;

} // namespace

// It's possible to run this from not the rootdir of the repository,
// in which case you can set the mapPathPrefix to where the maps should be
// found. This is just the path to your cherrypi directory
void MicroScenarioProvider::setMapPathPrefix(const std::string& prefix) {
  mapPathPrefix_ = prefix;
}

std::unique_ptr<Reward> MicroScenarioProvider::getReward() const {
  return scenarioNow_.reward();
}

void MicroScenarioProvider::endScenario() {
  VLOG(3) << "endScenario()";

  if (!player1_ || !player2_) {
    return;
  }

  VLOG(4) << "Game ended on frame " << player1_->state()->currentFrame();

  microPlayer1().onGameEnd();
  microPlayer2().onGameEnd();

  if (launchedWithReplay()) {
    player1_->queueCmds({{tc::BW::Command::Quit}});
    player2_->queueCmds({{tc::BW::Command::Quit}});
    // Send commands, and wait for game to finish properly
    while (!player1_->state()->gameEnded()) {
      player1_->step();
      if (!player2_->state()->gameEnded()) {
        player2_->step();
      }
    }
  } else if (!needNewGame_) {
    killAllUnits();
  }

  player1_.reset();
  player2_.reset();
}

void MicroScenarioProvider::step(
    std::shared_ptr<BasePlayer> player1,
    std::shared_ptr<BasePlayer> player2,
    int times) {
  if (player1->state()->gameEnded() || player2->state()->gameEnded()) {
    throw std::runtime_error("OpenBW process might be dead.");
  }
  for (int i = 0; i < times; ++i) {
    VLOG(6) << "Stepping players.";
    player1->step();
    player2->step();
  }
}

void MicroScenarioProvider::stepUntilCommandsReflected(
    std::shared_ptr<BasePlayer> player1,
    std::shared_ptr<BasePlayer> player2) {
  auto frameNow = [&]() { return player1->state()->currentFrame(); };
  int frameBefore;
  int frameGoal = frameNow() + 1 + player1->state()->latencyFrames();
  do {
    frameBefore = frameNow();
    step(player1, player2, 1);
  } while (frameNow() != frameBefore && frameNow() <= frameGoal &&
           !player1->state()->gameEnded());
}

void MicroScenarioProvider::endGame() {
  VLOG(3) << "endGame()";

  ++resetCount_;
  endScenario();
  unitsThisGame_ = 0;
  game_.reset();
}

void MicroScenarioProvider::killAllUnits() {
  VLOG(3) << "killAllUnits()";

  if (!player1_ || !player2_) {
    return;
  }
  if (player1_->state()->gameEnded()) {
    return;
  }

  auto killPlayerUnits = [&](auto& player) {
    std::vector<tc::Client::Command> killCommands;
    auto killUnits = [&](auto& units) {
      for (const auto& unit : units) {
        killCommands.emplace_back(
            tc::BW::Command::CommandOpenbw,
            tc::BW::OpenBWCommandType::KillUnit,
            unit->id);
      }
    };
    auto& unitsInfo = player->state()->unitsInfo();
    killUnits(unitsInfo.myUnits());
    killUnits(unitsInfo.neutralUnits());
    player->queueCmds(std::move(killCommands));
    step(player1_, player2_, 1);
  };

  killPlayerUnits(player1_);
  killPlayerUnits(player2_);
  stepUntilCommandsReflected(player1_, player2_);
}

void MicroScenarioProvider::createNewPlayers() {
  VLOG(3) << "createNewPlayers()";

  endScenario();
  player1_ = std::make_shared<MicroPlayer>(client1_);
  player2_ = std::make_shared<MicroPlayer>(client2_);
  player1_->state()->setMapHack(true);
  player2_->state()->setMapHack(true);
  std::vector<tc::Client::Command> commands;
  commands.emplace_back(tc::BW::Command::SetSpeed, 0);
  commands.emplace_back(tc::BW::Command::SetGui, gui_);
  commands.emplace_back(tc::BW::Command::SetCombineFrames, combineFrames_);
  commands.emplace_back(tc::BW::Command::SetFrameskip, 1);
  commands.emplace_back(tc::BW::Command::SetBlocking, true);
  commands.emplace_back(tc::BW::Command::MapHack);
  player1_->queueCmds(commands);
  player2_->queueCmds(commands);
}

void MicroScenarioProvider::createNewGame() {
  VLOG(3) << "createNewGame() on " << mapNow();

  endGame();

  lastMap_ = mapNow();
  // Any race is fine for scenarios.
  game_ = std::make_shared<GameMultiPlayer>(
      mapPathPrefix_ + lastMap_,
      tc::BW::Race::Terran,
      tc::BW::Race::Terran,
      scenarioNow_.gameType,
      replay_,
      gui_);
  client1_ = game_->makeClient1();
  client2_ = game_->makeClient2();
}

void MicroScenarioProvider::setupScenario() {
  VLOG(3) << "setupScenario() #" << scenarioCount_;
  ++scenarioCount_;
  auto info = setupScenario(player1_, player2_, scenarioNow_);
  unitsThisGame_ += info.unitsCount;
  unitsTotal_ += info.unitsCount;
  if (info.hasAnyZergBuilding) {
    needNewGame_ = true;
  }
  unitsSeenTotal_ += player1_->state()->unitsInfo().allUnitsEver().size();
}

MicroScenarioProvider::SetupScenarioResult MicroScenarioProvider::setupScenario(
    std::shared_ptr<BasePlayer> player1,
    std::shared_ptr<BasePlayer> player2,
    FixedScenario const& scenario) {
  SetupScenarioResult result;
  bool queuedCommands = false;
  auto queueCommands =
      [&](const std::vector<torchcraft::Client::Command>& commands) {
        player1->queueCmds(std::move(commands));
        queuedCommands = queuedCommands || !commands.empty();
      };
  auto sendCommands = [&]() {
    if (queuedCommands) {
      VLOG(5) << "Sending commands";
      step(player1, player2, 1);
      queuedCommands = false;
    }
  };

  auto getPlayerId = [&](int playerIndex) {
    return (playerIndex == 0 ? player1 : player2)->state()->playerId();
  };

  // Add techs and upgrades first.
  //
  // We need to assign the state of *every* upgrade and tech, even ones not
  // mentioned in the scenario description, because the level could have been
  // raised in a previous episode.
  for (int playerIndex = 0; playerIndex < int(scenario.players.size());
       ++playerIndex) {
    VLOG(4) << "Setting techs for Player " << playerIndex;
    for (auto* techType : buildtypes::allTechTypes) {
      int techId = techType->tech;
      int hasTech = false;

      // Everyone always has these techs at the start of the game
      if (techType != buildtypes::Defensive_Matrix &&
          techType != buildtypes::Healing &&
          techType != buildtypes::Scanner_Sweep &&
          techType != buildtypes::Dark_Swarm &&
          techType != buildtypes::Archon_Warp &&
          techType != buildtypes::Dark_Archon_Meld &&
          techType != buildtypes::Feedback &&
          techType != buildtypes::Infestation &&
          techType != buildtypes::Parasite) {
        for (auto& techScenario : scenario.players[playerIndex].techs) {
          if (techScenario == techId) {
            VLOG(4) << "Adding tech for player " << playerIndex << ": "
                    << techId;
            hasTech = true;
            break;
          }
        }
      }

      VLOG(7) << "Setting tech status for " << techType->name << " to "
              << hasTech;
      queueCommands({torchcraft::Client::Command(
          torchcraft::BW::Command::CommandOpenbw,
          torchcraft::BW::OpenBWCommandType::SetPlayerResearched,
          getPlayerId(playerIndex),
          techId,
          hasTech)});
    }
    sendCommands();

    VLOG(4) << "Setting upgrades for Player " << playerIndex;
    for (auto* upgradeType : buildtypes::allUpgradeTypes) {
      if (upgradeType->level > 1) {
        continue;
      }

      int upgradeId = upgradeType->upgrade;
      int level = 0;

      for (auto& upgradeScenario : scenario.players[playerIndex].upgrades) {
        if (upgradeScenario.upgradeType == upgradeId) {
          level = upgradeScenario.level;
          VLOG(4) << "Adding upgrade for player " << playerIndex << ": "
                  << upgradeId << " #" << level;
          break;
        }
      }

      queueCommands({torchcraft::Client::Command(
          torchcraft::BW::Command::CommandOpenbw,
          torchcraft::BW::OpenBWCommandType::SetPlayerUpgradeLevel,
          getPlayerId(playerIndex),
          upgradeId,
          level)});
    }
    sendCommands();
  }

  // Next, we spawn units.
  //
  // Spawning units is tricky. There are a few considerations:
  // * There's a maximum (About 128) commands that can be processed each frame.
  // * We don't want one player's units to be around for too long before the
  //   other player's (to minimize the extra time to react/attack).
  // * Building placement can be blocked by other units, so they need to
  //   spawn first.
  // * If an add-on is spawned without its building, it will enter as neutral,
  //   which causes issues (IIRC UnitsInfo can't handle the change of owner).
  //
  // So, here are the distinct tiers in which we'll spawn units:
  // * Non-combat buildings (because units can block buildings)
  //   * Player 0 non-addon buildings (because addons spawn neutral otherwise)
  //   * Player 1 non-addon buildings
  //   * Addon buildings
  // * Combat buildings (Last, to minimize frames they could spend attacking)
  // * Player 0 non-workers
  // * Player 1 non-workers
  // * Player 0 workers
  // * Player 1 workers

  auto queueUnits = [&](const std::vector<SpawnPosition>& units,
                        std::shared_ptr<BasePlayer> player) {
    result.unitsCount += units.size();
    queueCommands(OnceModule::makeSpawnCommands(
        units, player->state(), player->state()->playerId()));
  };
  auto extractUnits = [](std::vector<SpawnPosition>& units,
                         std::function<bool(const SpawnPosition&)> predicate) {
    VLOG(7) << "Extracting from " << units.size() << " units";
    std::vector<SpawnPosition> output;
    unsigned i = 0;
    while (i < units.size()) {
      if (predicate(units[i])) {
        output.push_back(units[i]);
        std::iter_swap(units.begin() + i, units.end() - 1);
        units.pop_back();
      } else {
        ++i;
      }
    }
    VLOG(7) << units.size() << " units left after extraction. Took "
            << output.size() << " units.";
    return output;
  };
  // Predicates for spawning units in tiers
  auto producesCreep = [](const SpawnPosition& unit) {
    auto* type = getUnitBuildType(unit.type);
    return type->producesCreep;
  };
  auto isNonCombatNonAddonBuilding = [](const SpawnPosition& unit) {
    auto* type = getUnitBuildType(unit.type);
    return type->isBuilding && !type->isAddon && !type->hasAirWeapon &&
        !type->hasGroundWeapon && type != buildtypes::Terran_Bunker &&
        type != buildtypes::Protoss_Shield_Battery;
  };
  auto isAddon = [](const SpawnPosition& unit) {
    auto* type = getUnitBuildType(unit.type);
    return type->isAddon;
  };
  auto isCombatBuilding = [](const SpawnPosition& unit) {
    auto* type = getUnitBuildType(unit.type);
    return type->isBuilding && !type->isAddon;
  };
  auto isNonWorker = [](const SpawnPosition& unit) {
    auto* type = getUnitBuildType(unit.type);
    return type != buildtypes::Terran_SCV &&
        type != buildtypes::Protoss_Probe && type != buildtypes::Zerg_Drone;
  };
  auto isAnything = [](const SpawnPosition& unit) { return true; };

  std::vector<SpawnPosition> units0(scenario.players[0].units);
  std::vector<SpawnPosition> units1(scenario.players[1].units);
  std::vector<std::tuple<int, std::vector<SpawnPosition>>> tiers;

  // Semi-hack: OpenBW chokes when destroying a bunch of creep-producing
  // buildings at the same time. Let's not spawn them until we can fix that.
  auto zergBuildings0 = extractUnits(units0, producesCreep);
  auto zergBuildings1 = extractUnits(units1, producesCreep);
  result.hasAnyZergBuilding =
      !zergBuildings0.empty() || !zergBuildings1.empty();
  // Add-ons still aren't getting assigned to buildings properly.
  extractUnits(units0, isAddon);
  extractUnits(units1, isAddon);
  tiers.emplace_back(
      std::make_tuple(0, extractUnits(units0, isNonCombatNonAddonBuilding)));
  tiers.emplace_back(
      std::make_tuple(1, extractUnits(units1, isNonCombatNonAddonBuilding)));
  tiers.emplace_back(std::make_tuple(0, extractUnits(units0, isAddon)));
  tiers.emplace_back(std::make_tuple(1, extractUnits(units1, isAddon)));
  tiers.emplace_back(0, zergBuildings0);
  tiers.emplace_back(1, zergBuildings1);
  tiers.emplace_back(
      std::make_tuple(0, extractUnits(units0, isCombatBuilding)));
  tiers.emplace_back(
      std::make_tuple(1, extractUnits(units1, isCombatBuilding)));
  tiers.emplace_back(std::make_tuple(0, extractUnits(units0, isNonWorker)));
  tiers.emplace_back(std::make_tuple(1, extractUnits(units1, isNonWorker)));
  tiers.emplace_back(std::make_tuple(0, extractUnits(units0, isAnything)));
  tiers.emplace_back(std::make_tuple(1, extractUnits(units1, isAnything)));

  for (auto i = 0u; i < tiers.size(); ++i) {
    auto& tier = tiers[i];
    int playerIndex = std::get<0>(tier);
    auto& units = std::get<1>(tier);
    VLOG(4) << "Spawning " << units.size() << " units for player "
            << playerIndex << " in Tier " << i;
    queueUnits(units, playerIndex == 0 ? player1 : player2);
    sendCommands();
  }

  // Lastly, add any scenario-specific functions
  for (auto& stepFunction : scenario.stepFunctions) {
    VLOG(4) << "Running a step function";
    player1->addModule(std::make_shared<LambdaModule>(std::move(stepFunction)));
  }

  stepUntilCommandsReflected(player1, player2);
  return result;
}

std::pair<std::shared_ptr<BasePlayer>, std::shared_ptr<BasePlayer>>
MicroScenarioProvider::startNewScenario(
    const std::function<void(BasePlayer*)>& setup1,
    const std::function<void(BasePlayer*)>& setup2) {
  VLOG(3) << "startNewScenario()";

  VLOG(4) << "Total units spawned: " << unitsThisGame_ << "/" << unitsTotal_;
  VLOG(4) << "Total units seen all time: " << unitsSeenTotal_;

  scenarioNow_ = getFixedScenario();
  bool mapChanged = lastMap_ != mapNow();
  bool exceededUnitLimit = unitsThisGame_ > kMaxUnits;
  if (mapChanged) {
    VLOG(4) << "Map changed from " << lastMap_ << " to " << mapNow();
  }
  if (exceededUnitLimit) {
    VLOG(4) << "Exceeded unit limit";
  }
  needNewGame_ =
      launchedWithReplay() || !game_ || mapChanged || exceededUnitLimit;

  if (needNewGame_) {
    endGame();
  } else {
    endScenario();
  }

  if (needNewGame_) {
    createNewGame();
  }
  createNewPlayers();
  setup1(player1_.get());
  setup2(player2_.get());
  setupScenario();
  microPlayer1().onGameStart();
  microPlayer2().onGameStart();
  return {player1_, player2_};
}

} // namespace cherrypi

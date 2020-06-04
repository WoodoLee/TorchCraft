/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "gamevsbot.h"

#include "cherrypi.h"
#include "openbwprocess.h"
#include "playscript.h"

#include <common/fsutils.h>
#include <common/rand.h>
#include <common/str.h>

#include <fmt/format.h>
#include <glog/logging.h>

namespace fsutils = common::fsutils;

namespace {
int constexpr kBotPlayTimeoutMs = 120000;
} // namespace

namespace cherrypi {

GameVsBotInOpenBW::GameVsBotInOpenBW(
    std::string const& map,
    tc::BW::Race myRace,
    std::string const& enemyBot,
    GameType gameType,
    std::string const& replayPath,
    bool forceGui) {
#ifdef WITHOUT_POSIX
  throw std::runtime_error("Not available for windows");
#else
  proc1_ = std::make_shared<OpenBwProcess>(std::vector<EnvVar>{
      {"OPENBW_ENABLE_UI", forceGui ? "1" : "0", forceGui},
      {"OPENBW_LAN_MODE", "FILE", true},
      {"OPENBW_FILE_READ", pipes_.pipe1, true},
      {"OPENBW_FILE_WRITE", pipes_.pipe2, true},
      {"BWAPI_CONFIG_AUTO_MENU__AUTO_MENU", "LAN", true},
      {"BWAPI_CONFIG_AUTO_MENU__GAME_TYPE", gameTypeName(gameType), true},
      {"BWAPI_CONFIG_AUTO_MENU__MAP", map, true},
      {"BWAPI_CONFIG_AUTO_MENU__RACE", myRace._to_string(), true},
      {"BWAPI_CONFIG_AUTO_MENU__SAVE_REPLAY", replayPath, true},
  });
  proc2_ = std::make_shared<OpenBwProcess>(
      enemyBot,
      std::vector<EnvVar>{
          {"OPENBW_ENABLE_UI", "0", true},
          {"OPENBW_LAN_MODE", "FILE", true},
          {"OPENBW_FILE_READ", pipes_.pipe2, true},
          {"OPENBW_FILE_WRITE", pipes_.pipe1, true},
          {"BWAPI_CONFIG_AUTO_MENU__AUTO_MENU", "LAN", true},
          {"BWAPI_CONFIG_AUTO_MENU__GAME_TYPE", gameTypeName(gameType), true},
          {"BWAPI_CONFIG_AUTO_MENU__MAP", map, true},
      });
#endif // WITHOUT_POSIX
}

std::shared_ptr<tc::Client> GameVsBotInOpenBW::makeClient(
    tc::Client::Options opts) {
  return makeTorchCraftClient(proc1_, std::move(opts), kBotPlayTimeoutMs);
}

GameVsBotInWine::GameVsBotInWine(
    std::vector<std::string> maps,
    std::string enemyBot,
    std::string outputPath,
    std::vector<EnvVar> vars)
    : enemyBot_(enemyBot) {
  vars.insert(vars.begin(), {"OPPONENT", enemyBot, true});

  auto playId = common::randId(32);
  path_ = outputPath + "/" + playId;
  vars.insert(vars.begin(), {"OUTPUT", outputPath, true});
  vars.insert(vars.begin(), {"PLAYID", playId, true});

  // Sanity check: maps should be absolute paths and not contain duplicates --
  // otherwise, PlayScript will not be happy.
  std::unordered_set<std::string> mapSet;
  for (auto it = maps.begin(); it != maps.end(); ++it) {
    if (it->empty() || (*it)[0] != '/') {
      throw std::runtime_error(
          "Absolute map paths required, but found '" + *it + "'");
    }
    if (mapSet.find(*it) != mapSet.end()) {
      LOG(WARNING) << "Removing duplicate map '" << *it << "' from map pool";
      it = maps.erase(it);
    } else {
      mapSet.insert(*it);
    }
  }

  vars.insert(vars.begin(), {"MAPS", common::joinVector(maps, ','), true});
  proc_ = std::make_shared<PlayScript>(vars);
}

GameVsBotInWine::GameVsBotInWine(
    std::string map,
    std::string enemyBot,
    std::string outputPath,
    std::vector<EnvVar> vars)
    : GameVsBotInWine(
          std::vector<std::string>{std::move(map)},
          std::move(enemyBot),
          std::move(outputPath),
          std::move(vars)) {}

GameVsBotInWine::~GameVsBotInWine() {
  if (autoDelete_ && !path_.empty()) {
#ifdef WITHOUT_POSIX
    LOG(WARNING) << "Can't remove directory '" << path_
                 << "' on non-posix systems";
#else
    fsutils::rmrf(path_);
#endif
  }
}

void GameVsBotInWine::setAutoDelete(bool autoDelete) {
  autoDelete_ = autoDelete;
}

std::shared_ptr<tc::Client> GameVsBotInWine::makeClient(
    tc::Client::Options opts) {
  numGamesStarted_++;
  return makeTorchCraftClient(proc_, std::move(opts), kBotPlayTimeoutMs);
}

} // namespace cherrypi

/**
 * Copyright (c) 2015-present, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 */

#pragma once

#include <cstdio>
#include <fstream>
#include <iostream>
#include <vector>

#include <torchcraft/frame.h>
#include <torchcraft/refcount.h>
#include <torchcraft/state.h>

namespace torchcraft {
namespace replayer {

struct Map {
  uint32_t height, width;
  std::vector<uint8_t> data;
};

class Replayer : public RefCounted {
 private:
  std::vector<Frame*> frames;
  std::unordered_map<int32_t, int32_t> numUnits;
  Map map;
  // If keyframe = 0, every frame is a frame.
  // Otherwise, every keyframe is a frame, and all others are diffs.
  // Only affects saving/loading (replays are still large in memory)
  uint32_t keyframe;

 public:
  ~Replayer() {
    for (auto f : frames) {
      if (f)
        f->decref();
    }
  }

  Frame* getFrame(size_t i) {
    if (i >= frames.size())
      return nullptr;
    return frames[i];
  }
  void push(Frame* f) {
    auto new_frame = new Frame(f);
    frames.push_back(new_frame);
  }
  void setKeyFrame(int32_t x) {
    keyframe = x < 0 ? frames.size() + 1 : (uint32_t)x;
  }
  uint32_t getKeyFrame() {
    return keyframe;
  }
  size_t size() const {
    return frames.size();
  }
  int32_t mapHeight() const {
    return map.height;
  }
  int32_t mapWidth() const {
    return map.width;
  }

  void setNumUnits() {
    for (const auto f : frames) {
      for (auto u : f->units) {
        auto s = u.second.size();
        auto i = u.first;
        if (numUnits.count(i) == 0) {
          numUnits[i] = s;
        } else if (s > static_cast<size_t>(numUnits[i])) {
          numUnits[i] = s;
        }
      }
    }
  }

  int32_t getNumUnits(const int32_t& key) const {
    if (numUnits.find(key) == numUnits.end())
      return -1;
    return numUnits.at(key);
  }

  void setMapFromState(torchcraft::State const* state);

  void setMap(
      int32_t h,
      int32_t w,
      std::vector<uint8_t> const& walkability,
      std::vector<uint8_t> const& ground_height,
      std::vector<uint8_t> const& buildability,
      std::vector<int> const& start_loc_x,
      std::vector<int> const& start_loc_y);

  void setMap(
      int32_t h,
      int32_t w,
      uint8_t const* const walkability,
      uint8_t const* const ground_height,
      uint8_t const* const buildability,
      std::vector<int> const& start_loc_x,
      std::vector<int> const& start_loc_y);

  void setRawMap(uint32_t h, uint32_t w, uint8_t const* d) {
    // free existing map if needed
    map.data.resize(h * w);
    map.data.assign(d, d + h * w);
    map.height = h;
    map.width = w;
  }

  const std::vector<uint8_t>& getRawMap() {
    return map.data;
  }

  std::pair<int32_t, int32_t> getMap(
      std::vector<uint8_t>& walkability,
      std::vector<uint8_t>& ground_height,
      std::vector<uint8_t>& buildability,
      std::vector<int>& start_loc_x,
      std::vector<int>& start_loc_y) const;

  friend std::ostream& operator<<(std::ostream& out, const Replayer& o);
  friend std::istream& operator>>(std::istream& in, Replayer& o);

  void load(const std::string& path);
  void save(const std::string& path, bool compressed = false);
};

} // namespace replayer
} // namespace torchcraft

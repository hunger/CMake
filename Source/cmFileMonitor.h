/*============================================================================
  CMake - Cross Platform Makefile Generator
  Copyright 2016 Tobias Hunger <tobias.hunger@qt.io>

  Distributed under the OSI-approved BSD License (the "License");
  see accompanying file Copyright.txt for details.

  This software is distributed WITHOUT ANY WARRANTY; without even the
  implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
  See the License for more information.
============================================================================*/

#pragma once

#include <functional>
#include <string>
#include <vector>

#include "cm_uv.h"

namespace {
class cmRootWatcher;
} // namespace

class cmFileMonitor
{
public:
  cmFileMonitor(uv_loop_t* l);
  ~cmFileMonitor();

  using Callback = std::function<void(const std::string&, int, int)>;
  void MonitorPaths(const std::vector<std::string>& paths, Callback cb);
  void StopMonitoring();

  std::vector<std::string> WatchedFiles() const;
  std::vector<std::string> WatchedDirectories() const;

private:
  cmRootWatcher* Root;
};

/*============================================================================
  CMake - Cross Platform Makefile Generator
  Copyright 2015 Stephen Kelly <steveire@gmail.com>
  Copyright 2016 Tobias Hunger <tobias.hunger@qt.io>

  Distributed under the OSI-approved BSD License (the "License");
  see accompanying file Copyright.txt for details.

  This software is distributed WITHOUT ANY WARRANTY; without even the
  implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
  See the License for more information.
============================================================================*/

#pragma once

#include "cmListFileCache.h"
#include "cmState.h"

#if defined(CMAKE_BUILD_WITH_CMAKE)
#include "cm_jsoncpp_value.h"
#include <uv.h>
#endif

#include <string>
#include <vector>

class cmServerProtocol;
class cmServerRequest;
class cmServerResponse;

class cmServer
{
public:
  cmServer(bool supportExperimental);
  ~cmServer();

  bool Serve();

  // for callbacks:
  void PopOne();
  void handleData(std::string const& data);

private:
  void RegisterProtocol(cmServerProtocol* protocol);

  static void reportProgress(const char* msg, float progress, void* data);
  static void reportMessage(const char* msg, const char* title, bool& cancel,
                            void* data);

  // Handle requests:
  cmServerResponse SetProtocolVersion(const cmServerRequest& request);

  // Write responses:
  void WriteProgress(const cmServerRequest& request, int min, int current,
                     int max, const std::string& message);
  void WriteMessage(const cmServerRequest& request, const std::string& message,
                    const std::string& title);
  void WriteResponse(const cmServerResponse& response);
  void WriteParseError(const std::string& message);

  void WriteJsonObject(Json::Value const& jsonValue);

  static cmServerProtocol* FindMatchingProtocol(
    const std::vector<cmServerProtocol*>& protocols, int major, int minor);

  const bool SupportExperimental;

  cmServerProtocol* m_Protocol = nullptr;
  std::vector<cmServerProtocol*> m_SupportedProtocols;
  std::vector<std::string> m_Queue;

  std::string m_DataBuffer;
  std::string m_JsonData;

  uv_loop_t* m_Loop;
  uv_pipe_t m_StdinPipe;
  uv_pipe_t m_StdoutPipe;
  uv_tty_t m_StdinTty;
  uv_tty_t m_StdoutTty;

  bool m_Writing;

  friend class cmServerRequest;
};

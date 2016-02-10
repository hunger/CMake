/*============================================================================
  CMake - Cross Platform Makefile Generator
  Copyright 2015 Stephen Kelly <steveire@gmail.com>

  Distributed under the OSI-approved BSD License (the "License");
  see accompanying file Copyright.txt for details.

  This software is distributed WITHOUT ANY WARRANTY; without even the
  implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
  See the License for more information.
============================================================================*/

#pragma once

#include "cmState.h"
#include "cmListFileCache.h"

#if defined(CMAKE_BUILD_WITH_CMAKE)
# include "cm_jsoncpp_value.h"
# include <uv.h>
#endif

class cmServerProtocol;
class cmServerRequest;
class cmServerResponse;

class cmMetadataServer
{
public:
  enum ServerState {
    Uninitialized,
    Started,
    Initializing,
    ProcessingRequests
  };

  cmMetadataServer();
  ~cmMetadataServer();

  void ServeMetadata();

  // for callbacks:
  void PopOne();
  void handleData(std::string const& data);

private:
  void RegisterProtocol(cmServerProtocol *protocol);
  cmServerResponse SetProtocolVersion(const cmServerRequest& request);
  void WriteProgress(const std::string& progress); // TODO: Remove
  void WriteProgress(const cmServerRequest& request,
                     int min, int current, int max, const std::string &message);
  void WriteResponse(const cmServerResponse &response);
  void WriteParseError(const std::string &message);

  void WriteJsonObject(Json::Value const& jsonValue);

  static cmServerProtocol* FindMatchingProtocol(const std::vector<cmServerProtocol*>& protocols,
                                                int major, int minor);

  cmServerProtocol* Protocol;
  std::vector<cmServerProtocol*> SupportedProtocols;
  std::vector<std::string> mQueue;
  
  std::string mDataBuffer;
  std::string mJsonData;

  uv_loop_t *mLoop;
  uv_pipe_t mStdin_pipe;
  uv_pipe_t mStdout_pipe;

  bool Writing;

  friend class cmServerRequest;
};

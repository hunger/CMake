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

#include "cmListFileCache.h"
#include "cmake.h"

#if defined(CMAKE_BUILD_WITH_CMAKE)
#include "cm_jsoncpp_writer.h"
#endif

#include <string>

class cmake;
class cmFileMonitor;
class cmServer;

class DifferentialFileContent;
class cmServerRequest;

class cmServerResponse
{
public:
  explicit cmServerResponse(const cmServerRequest& request);

  void SetData(const Json::Value& data);
  void SetError(const std::string& message);

  bool IsComplete() const;
  bool IsError() const;
  std::string ErrorMessage() const;
  Json::Value Data() const;

  const std::string Type;
  const std::string Cookie;

private:
  enum PayLoad
  {
    UNKNOWN,
    ERROR,
    DATA
  };
  PayLoad m_Payload = UNKNOWN;
  std::string m_ErrorMessage;
  Json::Value m_Data;
  const void* m_DebugInfo;
};

class cmServerRequest
{
public:
  cmServerResponse Reply(const Json::Value& data) const;
  cmServerResponse ReportError(const std::string& message) const;

  const std::string Type;
  const std::string Cookie;
  const Json::Value Data;

private:
  cmServerRequest(cmServer* server, const std::string& t, const std::string& c,
                  const Json::Value& d);

  void ReportProgress(int min, int current, int max,
                      const std::string& message) const;
  void ReportMessage(const std::string& message,
                     const std::string& title) const;

  cmServer* m_Server;

  friend class cmServer;
};

class cmServerProtocol
{
public:
  virtual ~cmServerProtocol();

  virtual std::pair<int, int> ProtocolVersion() const = 0;
  virtual bool IsExperimental() const = 0;
  virtual const cmServerResponse Process(const cmServerRequest& request) = 0;

  bool Activate(cmServer* server, const cmServerRequest& request,
                std::string* errorMessage);

  cmFileMonitor* FileMonitor() const;
  void SendSignal(const std::string& name, const Json::Value& data) const;

protected:
  cmake* CMakeInstance() const;
  // Implement protocol specific activation tasks here. Called from Activate().
  virtual bool DoActivate(const cmServerRequest& request,
                          std::string* errorMessage);

private:
  std::unique_ptr<cmake> m_CMakeInstance;
  cmServer* m_Server = nullptr; // not owned!

  friend class cmServer;
};

class cmServerProtocol1_0 : public cmServerProtocol
{
public:
  std::pair<int, int> ProtocolVersion() const override;
  bool IsExperimental() const override;
  const cmServerResponse Process(const cmServerRequest& request) override;
  virtual ~cmServerProtocol1_0() = default;

private:
  bool DoActivate(const cmServerRequest& request,
                  std::string* errorMessage) override;

  void HandleCMakeFileChanges(const std::string& path, int event, int status);

  // Handle requests:
  cmServerResponse ProcessCache(const cmServerRequest& request);
  cmServerResponse ProcessCMakeInputs(const cmServerRequest& request);
  cmServerResponse ProcessCodeModel(const cmServerRequest& request);
  cmServerResponse ProcessCompute(const cmServerRequest& request);
  cmServerResponse ProcessConfigure(const cmServerRequest& request);
  cmServerResponse ProcessGlobalSettings(const cmServerRequest& request);
  cmServerResponse ProcessSetGlobalSettings(const cmServerRequest& request);
  cmServerResponse ProcessFileSystemWatchers(const cmServerRequest& request);

  enum State
  {
    INACTIVE,
    ACTIVE,
    CONFIGURED,
    COMPUTED
  };
  State m_State = INACTIVE;

  bool m_isDirty = false;
};

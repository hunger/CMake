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

#include "cmake.h"

#if defined(CMAKE_BUILD_WITH_CMAKE)
#include "cm_jsoncpp_writer.h"
#endif

#include <string>

class cmake;
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
  PayLoad mPayload = UNKNOWN;
  std::string m_ErrorMessage;
  Json::Value m_Data;
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

  cmServer* m_Server;

  friend class cmServer;
};

class cmServerProtocol
{
public:
  virtual ~cmServerProtocol();

  virtual std::pair<int, int> ProtocolVersion() const = 0;
  virtual const cmServerResponse Process(const cmServerRequest& request) = 0;

  virtual void Activate();

protected:
  cmake* CMakeInstance() const;
  void reset();

  // Implement protocol specific activation tasks here. Called from Activate().
  virtual void DoActivate();
  virtual void DoReset();

private:
  std::unique_ptr<cmake> m_CMakeInstance;

  friend class cmServer;
};

class cmServerProtocol0_1 : public cmServerProtocol
{
public:
  std::pair<int, int> ProtocolVersion() const override;
  const cmServerResponse Process(const cmServerRequest& request) override;
  virtual ~cmServerProtocol0_1() = default;

private:
  void DoActivate() override;
  void DoReset() override;

  // Handle requests:
  cmServerResponse ProcessConfigure(const cmServerRequest& request);
  cmServerResponse ProcessGenerate(const cmServerRequest& request);
  cmServerResponse ProcessGlobalSettings(const cmServerRequest& request);
  cmServerResponse ProcessProject(const cmServerRequest& request);
  cmServerResponse ProcessReset(const cmServerRequest& request);
  cmServerResponse ProcessSetGlobalSettings(const cmServerRequest& request);

  enum State
  {
    INACTIVE,
    ACTIVE,
    CONFIGURED,
    GENERATED
  };
  State m_State = INACTIVE;
};

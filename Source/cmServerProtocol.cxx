/*============================================================================
  CMake - Cross Platform Makefile Generator
  Copyright 2016 Tobias Hunger <tobias.hunger@qt.io>

  Distributed under the OSI-approved BSD License (the "License");
  see accompanying file Copyright.txt for details.

  This software is distributed WITHOUT ANY WARRANTY; without even the
  implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
  See the License for more information.
============================================================================*/

#include "cmServerProtocol.h"

#include "cmExternalMakefileProjectGenerator.h"
#include "cmGlobalGenerator.h"
#include "cmServer.h"
#include "cmVersionMacros.h"
#include "cmake.h"

#if defined(CMAKE_BUILD_WITH_CMAKE)
#include "cm_jsoncpp_reader.h"
#include "cm_jsoncpp_value.h"
#endif

namespace {
// Vocabulary:

char GLOBAL_SETTINGS_TYPE[] = "globalSettings";
char SET_GLOBAL_SETTINGS_TYPE[] = "setGlobalSettings";

char BUILD_DIRECTORY_KEY[] = "buildDirectory";
char CHECK_SYSTEM_VARS_KEY[] = "checkSystemVars";
char COOKIE_KEY[] = "cookie";
char CURRENT_GENERATOR_KEY[] = "currentGenerator";
char DEBUG_OUTPUT_KEY[] = "debugOutput";
char GENERATORS_KEY[] = "generators";
char MAJOR_KEY[] = "major";
char MINOR_KEY[] = "minor";
char PATCH_LEVEL_KEY[] = "patchLevel";
char STRING_KEY[] = "string";
char SOURCE_DIRECTORY_KEY[] = "sourceDirectory";
char TRACE_EXPAND_KEY[] = "traceExpand";
char TRACE_KEY[] = "trace";
char TYPE_KEY[] = "type";
char VERSION_KEY[] = "version";
char WARN_UNINITIALIZED_KEY[] = "warnUninitialized";
char WARN_UNUSED_CLI_KEY[] = "warnUnusedCli";
char WARN_UNUSED_KEY[] = "warnUnused";

} // namespace

cmServerRequest::cmServerRequest(cmServer* server, const std::string& t,
                                 const std::string& c, const Json::Value& d)
  : Type(t)
  , Cookie(c)
  , Data(d)
  , m_Server(server)
{
}

void cmServerRequest::ReportProgress(int min, int current, int max,
                                     const std::string& message) const
{
  m_Server->WriteProgress(*this, min, current, max, message);
}

cmServerResponse cmServerRequest::Reply(const Json::Value& data) const
{
  cmServerResponse response(*this);
  response.SetData(data);
  return response;
}

cmServerResponse cmServerRequest::ReportError(const std::string& message) const
{
  cmServerResponse response(*this);
  response.SetError(message);
  return response;
}

cmServerResponse::cmServerResponse(const cmServerRequest& request)
  : Type(request.Type)
  , Cookie(request.Cookie)
{
}

void cmServerResponse::SetData(const Json::Value& data)
{
  assert(mPayload == UNKNOWN);
  if (!data[COOKIE_KEY].isNull() || !data[TYPE_KEY].isNull()) {
    SetError("Response contains cookie or type field.");
    return;
  }
  mPayload = DATA;
  m_Data = data;
}

void cmServerResponse::SetError(const std::string& message)
{
  assert(mPayload == UNKNOWN);
  mPayload = ERROR;
  m_ErrorMessage = message;
}

bool cmServerResponse::IsComplete() const
{
  return mPayload != UNKNOWN;
}

bool cmServerResponse::IsError() const
{
  assert(mPayload != UNKNOWN);
  return mPayload == ERROR;
}

std::string cmServerResponse::ErrorMessage() const
{
  if (mPayload == ERROR)
    return m_ErrorMessage;
  else
    return std::string();
}

Json::Value cmServerResponse::Data() const
{
  assert(mPayload != UNKNOWN);
  return m_Data;
}

cmServerProtocol::~cmServerProtocol()
{
}

void cmServerProtocol::Activate()
{
  m_CMakeInstance = std::make_unique<cmake>();
  DoActivate();
}

cmake* cmServerProtocol::CMakeInstance() const
{
  return m_CMakeInstance.get();
}

void cmServerProtocol::DoActivate()
{
}

std::pair<int, int> cmServerProtocol0_1::ProtocolVersion() const
{
  return std::make_pair(0, 1);
}

void cmServerProtocol0_1::DoActivate()
{
  m_State = ACTIVE;
}

const cmServerResponse cmServerProtocol0_1::Process(
  const cmServerRequest& request)
{
  assert(m_State >= ACTIVE);
  if (request.Type == GLOBAL_SETTINGS_TYPE)
    return ProcessGlobalSettings(request);
  if (request.Type == SET_GLOBAL_SETTINGS_TYPE)
    return ProcessSetGlobalSettings(request);

  return request.ReportError("Unknown command!");
}

bool cmServerProtocol0_1::IsExperimental() const
{
  return true;
}

cmServerResponse cmServerProtocol0_1::ProcessGlobalSettings(
  const cmServerRequest& request)
{
  cmake* cm = CMakeInstance();
  Json::Value obj = Json::objectValue;

  // Version information:
  Json::Value version = Json::objectValue;
  version[STRING_KEY] = CMake_VERSION;
  version[MAJOR_KEY] = CMake_VERSION_MAJOR;
  version[MINOR_KEY] = CMake_VERSION_MINOR;
  version[PATCH_LEVEL_KEY] = CMake_VERSION_PATCH;

  obj[VERSION_KEY] = version;

  // Generators:
  Json::Value generators = Json::arrayValue;
  std::vector<cmake::GeneratorInfo> generatorInfoList;
  cm->GetRegisteredGenerators(generatorInfoList);

  for (auto i : generatorInfoList) {
    generators.append(i.name);
  }
  obj[GENERATORS_KEY] = generators;

  std::string fullGeneratorName;
  cmGlobalGenerator* currentGenerator = cm->GetGlobalGenerator();
  if (currentGenerator) {
    const std::string extraGeneratorName =
      currentGenerator->GetExtraGeneratorName();
    const std::string generatorName = currentGenerator->GetName();
    fullGeneratorName =
      cmExternalMakefileProjectGenerator::CreateFullGeneratorName(
        generatorName, extraGeneratorName);
  }
  obj[CURRENT_GENERATOR_KEY] = fullGeneratorName;

  obj[DEBUG_OUTPUT_KEY] = cm->GetDebugOutput();
  obj[TRACE_KEY] = cm->GetTrace();
  obj[TRACE_EXPAND_KEY] = cm->GetTraceExpand();
  obj[WARN_UNINITIALIZED_KEY] = cm->GetWarnUninitialized();
  obj[WARN_UNUSED_KEY] = cm->GetWarnUnused();
  obj[WARN_UNUSED_CLI_KEY] = cm->GetWarnUnusedCli();
  obj[CHECK_SYSTEM_VARS_KEY] = cm->GetCheckSystemVars();

  obj[SOURCE_DIRECTORY_KEY] = cm->GetHomeDirectory();
  obj[BUILD_DIRECTORY_KEY] = cm->GetHomeOutputDirectory();

  return request.Reply(obj);
}

static bool setString(const cmServerRequest& request, const std::string& key,
                      std::function<bool(std::string)> setter)
{
  if (request.Data[key].isNull())
    return true;
  return setter(request.Data[key].asString());
}

static void setBool(const cmServerRequest& request, const std::string& key,
                    std::function<void(bool)> setter)
{
  if (request.Data[key].isNull())
    return;
  setter(request.Data[key].asBool());
}

cmServerResponse cmServerProtocol0_1::ProcessSetGlobalSettings(
  const cmServerRequest& request)
{
  const std::vector<std::string> stringValues = { BUILD_DIRECTORY_KEY,
                                                  CURRENT_GENERATOR_KEY,
                                                  SOURCE_DIRECTORY_KEY };
  const std::vector<std::string> boolValues = {
    DEBUG_OUTPUT_KEY,       TRACE_KEY,       TRACE_EXPAND_KEY,
    WARN_UNINITIALIZED_KEY, WARN_UNUSED_KEY, WARN_UNUSED_CLI_KEY,
    CHECK_SYSTEM_VARS_KEY
  };

  for (auto i : stringValues) {
    if (!request.Data[i].isNull() && !request.Data[i].isString()) {
      return request.ReportError("\"" + i + "\" must be unset or a string.");
    }
  }
  for (auto i : boolValues) {
    if (!request.Data[i].isNull() && !request.Data[i].isBool()) {
      return request.ReportError("\"" + i +
                                 "\" must be unset or a bool value.");
    }
  }

  cmake* cm = CMakeInstance();
  if (!setString(request, CURRENT_GENERATOR_KEY, [cm](const std::string& v) {
        cmGlobalGenerator* generator = cm->CreateGlobalGenerator(v);
        if (!generator) {
          return false;
        }
        cm->SetGlobalGenerator(generator);
        return true;
      })) {
    return request.ReportError("Requested generator was not found.");
  }

  setString(request, SOURCE_DIRECTORY_KEY, [cm](const std::string& v) {
    cm->SetHomeDirectory(v);
    return true;
  });
  setString(request, BUILD_DIRECTORY_KEY, [cm](const std::string& v) {
    cm->SetHomeOutputDirectory(v);
    return true;
  });

  setBool(request, DEBUG_OUTPUT_KEY,
          [cm](bool e) { cm->SetDebugOutputOn(e); });
  setBool(request, TRACE_KEY, [cm](bool e) { cm->SetTrace(e); });
  setBool(request, TRACE_EXPAND_KEY, [cm](bool e) { cm->SetTraceExpand(e); });
  setBool(request, WARN_UNINITIALIZED_KEY,
          [cm](bool e) { cm->SetWarnUninitialized(e); });
  setBool(request, WARN_UNUSED_KEY, [cm](bool e) { cm->SetWarnUnused(e); });
  setBool(request, WARN_UNUSED_CLI_KEY,
          [cm](bool e) { cm->SetWarnUnusedCli(e); });
  setBool(request, CHECK_SYSTEM_VARS_KEY,
          [cm](bool e) { cm->SetCheckSystemVars(e); });

  return request.Reply(Json::Value());
}

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
#include "cmake.h"

#include "cmServerDictionary.h"

#if defined(CMAKE_BUILD_WITH_CMAKE)
#include "cm_jsoncpp_reader.h"
#include "cm_jsoncpp_value.h"
#endif

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
  this->m_Server->WriteProgress(*this, min, current, max, message);
}

void cmServerRequest::ReportMessage(const std::string& message,
                                    const std::string& title) const
{
  m_Server->WriteMessage(*this, message, title);
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
  assert(this->m_Payload == UNKNOWN);
  if (!data[COOKIE_KEY].isNull() || !data[TYPE_KEY].isNull()) {
    this->SetError("Response contains cookie or type field.");
    return;
  }
  this->m_Payload = DATA;
  this->m_Data = data;
}

void cmServerResponse::SetError(const std::string& message)
{
  assert(this->m_Payload == UNKNOWN);
  this->m_Payload = ERROR;
  this->m_ErrorMessage = message;
}

bool cmServerResponse::IsComplete() const
{
  return this->m_Payload != UNKNOWN;
}

bool cmServerResponse::IsError() const
{
  assert(this->m_Payload != UNKNOWN);
  return this->m_Payload == ERROR;
}

std::string cmServerResponse::ErrorMessage() const
{
  if (this->m_Payload == ERROR)
    return this->m_ErrorMessage;
  else
    return std::string();
}

Json::Value cmServerResponse::Data() const
{
  assert(this->m_Payload != UNKNOWN);
  return this->m_Data;
}

cmServerProtocol::~cmServerProtocol() = default;

bool cmServerProtocol::Activate(cmServer* server,
                                const cmServerRequest& request,
                                std::string* errorMessage)
{
  assert(server);
  this->m_Server = server;
  this->m_CMakeInstance = std::make_unique<cmake>();
  const bool result = this->DoActivate(request, errorMessage);
  if (!result)
    this->m_CMakeInstance = CM_NULLPTR;
  return result;
}

void cmServerProtocol::SendSignal(const std::string& name,
                                  const Json::Value& data) const
{
  if (this->m_Server)
    this->m_Server->WriteSignal(name, data);
}

cmake* cmServerProtocol::CMakeInstance() const
{
  return this->m_CMakeInstance.get();
}

bool cmServerProtocol::DoActivate(const cmServerRequest& /*request*/,
                                  std::string* /*errorMessage*/)
{
  return true;
}

std::pair<int, int> cmServerProtocol1_0::ProtocolVersion() const
{
  return std::make_pair(1, 0);
}

bool cmServerProtocol1_0::DoActivate(const cmServerRequest& request,
                                     std::string* errorMessage)
{
  std::string sourceDirectory = request.Data[SOURCE_DIRECTORY_KEY].asString();
  const std::string buildDirectory =
    request.Data[BUILD_DIRECTORY_KEY].asString();
  std::string generator = request.Data[GENERATOR_KEY].asString();
  std::string extraGenerator = request.Data[EXTRA_GENERATOR_KEY].asString();

  if (buildDirectory.empty()) {
    if (errorMessage)
      *errorMessage =
        std::string("\"") + BUILD_DIRECTORY_KEY + "\" is missing.";
    return false;
  }
  cmake* cm = CMakeInstance();
  if (cmSystemTools::PathExists(buildDirectory)) {
    if (!cmSystemTools::FileIsDirectory(buildDirectory)) {
      if (errorMessage)
        *errorMessage = std::string("\"") + BUILD_DIRECTORY_KEY +
          "\" exists but is not a directory.";
      return false;
    }

    const std::string cachePath = cmake::FindCacheFile(buildDirectory);
    if (cm->LoadCache(cachePath)) {
      cmState* state = cm->GetState();

      // Check generator:
      const std::string cachedGenerator =
        std::string(state->GetCacheEntryValue("CMAKE_GENERATOR"));
      if (cachedGenerator.empty() && generator.empty()) {
        if (errorMessage)
          *errorMessage =
            std::string("\"") + GENERATOR_KEY + "\" is required but unset.";
        return false;
      }
      if (generator.empty()) {
        generator = cachedGenerator;
      }
      if (generator != cachedGenerator) {
        if (errorMessage)
          *errorMessage = std::string("\"") + GENERATOR_KEY +
            "\" set but incompatible with configured generator.";
        return false;
      }

      // check extra generator:
      const std::string cachedExtraGenerator =
        std::string(state->GetCacheEntryValue("CMAKE_EXTRA_GENERATOR"));
      if (!cachedExtraGenerator.empty() && !extraGenerator.empty() &&
          cachedExtraGenerator != extraGenerator) {
        if (errorMessage)
          *errorMessage = std::string("\"") + EXTRA_GENERATOR_KEY +
            "\" is set but incompatible with configured extra generator.";
        return false;
      }
      if (extraGenerator.empty()) {
        extraGenerator = cachedExtraGenerator;
      }

      // check sourcedir:
      const std::string cachedSourceDirectory =
        std::string(state->GetCacheEntryValue("CMAKE_HOME_DIRECTORY"));
      if (!cachedSourceDirectory.empty() && !sourceDirectory.empty() &&
          cachedSourceDirectory != sourceDirectory) {
        if (errorMessage)
          *errorMessage = std::string("\"") + SOURCE_DIRECTORY_KEY +
            "\" is set but incompatible with configured source directory.";
        return false;
      }
      if (sourceDirectory.empty()) {
        sourceDirectory = cachedSourceDirectory;
      }
    }
  }

  if (sourceDirectory.empty()) {
    if (errorMessage)
      *errorMessage =
        std::string("\"") + SOURCE_DIRECTORY_KEY + "\" is unset but required.";
    return false;
  }
  if (!cmSystemTools::FileIsDirectory(sourceDirectory)) {
    if (errorMessage)
      *errorMessage =
        std::string("\"") + SOURCE_DIRECTORY_KEY + "\" is not a directory.";
    return false;
  }
  if (generator.empty()) {
    if (errorMessage)
      *errorMessage =
        std::string("\"") + GENERATOR_KEY + "\" is unset but required.";
    return false;
  }

  const std::string fullGeneratorName =
    cmExternalMakefileProjectGenerator::CreateFullGeneratorName(
      generator, extraGenerator);

  cmGlobalGenerator* gg = cm->CreateGlobalGenerator(fullGeneratorName);
  if (!gg) {
    if (errorMessage)
      *errorMessage = "Could not set up the requested combination of "
                      "\"" +
        GENERATOR_KEY + "\" and \"" + EXTRA_GENERATOR_KEY + "\"";
    return false;
  }

  cm->SetGlobalGenerator(gg);
  cm->SetHomeDirectory(sourceDirectory);
  cm->SetHomeOutputDirectory(buildDirectory);

  this->m_State = ACTIVE;
  return true;
}

const cmServerResponse cmServerProtocol1_0::Process(
  const cmServerRequest& request)
{
  assert(this->m_State >= ACTIVE);

  if (request.Type == CONFIGURE_TYPE)
    return this->ProcessConfigure(request);
  if (request.Type == GLOBAL_SETTINGS_TYPE)
    return this->ProcessGlobalSettings(request);
  if (request.Type == SET_GLOBAL_SETTINGS_TYPE)
    return this->ProcessSetGlobalSettings(request);

  return request.ReportError("Unknown command!");
}

bool cmServerProtocol1_0::IsExperimental() const
{
  return true;
}

cmServerResponse cmServerProtocol1_0::ProcessConfigure(
  const cmServerRequest& request)
{
  if (this->m_State == INACTIVE) {
    return request.ReportError("This instance is inactive.");
  }

  // Make sure the types of cacheArguments matches (if given):
  std::vector<std::string> cacheArgs;
  bool cacheArgumentsError = false;
  const Json::Value passedArgs = request.Data[CACHE_ARGUMENTS_KEY];
  if (!passedArgs.isNull()) {
    if (passedArgs.isString()) {
      cacheArgs.push_back(passedArgs.asString());
    } else if (passedArgs.isArray()) {
      for (auto i = passedArgs.begin(); i != passedArgs.end(); ++i) {
        if (!i->isString()) {
          cacheArgumentsError = true;
          break;
        }
        cacheArgs.push_back(i->asString());
      }
    } else {
      cacheArgumentsError = true;
    }
  }
  if (cacheArgumentsError) {
    request.ReportError(
      "cacheArguments must be unset, a string or an array of strings.");
  }

  cmake* cm = this->CMakeInstance();
  std::string sourceDir = cm->GetHomeDirectory();
  const std::string buildDir = cm->GetHomeOutputDirectory();

  if (buildDir.empty()) {
    return request.ReportError(
      "No build directory set via setGlobalSettings.");
  }

  if (cm->LoadCache(buildDir)) {
    // build directory has been set up before
    const char* cachedSourceDir =
      cm->GetState()->GetInitializedCacheValue("CMAKE_HOME_DIRECTORY");
    if (!cachedSourceDir) {
      return request.ReportError("No CMAKE_HOME_DIRECTORY found in cache.");
    }
    if (sourceDir.empty()) {
      sourceDir = std::string(cachedSourceDir);
      cm->SetHomeDirectory(sourceDir);
    }

    const char* cachedGenerator =
      cm->GetState()->GetInitializedCacheValue("CMAKE_GENERATOR");
    if (cachedGenerator) {
      cmGlobalGenerator* gen = cm->GetGlobalGenerator();
      if (gen && gen->GetName() != cachedGenerator) {
        return request.ReportError("Configured generator does not match with "
                                   "CMAKE_GENERATOR found in cache.");
      }
    }
  } else {
    // build directory has not been set up before
    if (sourceDir.empty()) {
      return request.ReportError("No sourceDirectory set via "
                                 "setGlobalSettings and no cache found in "
                                 "buildDirectory.");
    }
  }

  if (cm->AddCMakePaths() != 1) {
    return request.ReportError("Failed to set CMake paths.");
  }

  if (!cm->SetCacheArgs(cacheArgs)) {
    return request.ReportError("cacheArguments could not be set.");
  }

  int ret = cm->Configure();
  if (ret < 0) {
    return request.ReportError("Configuration failed.");
  } else {
    m_State = CONFIGURED;
    return request.Reply(Json::Value());
  }
}

cmServerResponse cmServerProtocol1_0::ProcessGlobalSettings(
  const cmServerRequest& request)
{
  cmake* cm = this->CMakeInstance();
  Json::Value obj = Json::objectValue;

  // Capabilities information:
  obj[CAPABILITIES_KEY] = cm->ReportCapabilitiesJson(true);

  obj[DEBUG_OUTPUT_KEY] = cm->GetDebugOutput();
  obj[TRACE_KEY] = cm->GetTrace();
  obj[TRACE_EXPAND_KEY] = cm->GetTraceExpand();
  obj[WARN_UNINITIALIZED_KEY] = cm->GetWarnUninitialized();
  obj[WARN_UNUSED_KEY] = cm->GetWarnUnused();
  obj[WARN_UNUSED_CLI_KEY] = cm->GetWarnUnusedCli();
  obj[CHECK_SYSTEM_VARS_KEY] = cm->GetCheckSystemVars();

  obj[SOURCE_DIRECTORY_KEY] = cm->GetHomeDirectory();
  obj[BUILD_DIRECTORY_KEY] = cm->GetHomeOutputDirectory();

  // Currently used generator:
  cmGlobalGenerator* gen = cm->GetGlobalGenerator();
  obj[GENERATOR_KEY] = gen ? gen->GetName() : std::string();
  obj[EXTRA_GENERATOR_KEY] =
    gen ? gen->GetExtraGeneratorName() : std::string();

  return request.Reply(obj);
}

static void setBool(const cmServerRequest& request, const std::string& key,
                    std::function<void(bool)> setter)
{
  if (request.Data[key].isNull())
    return;
  setter(request.Data[key].asBool());
}

cmServerResponse cmServerProtocol1_0::ProcessSetGlobalSettings(
  const cmServerRequest& request)
{
  const std::vector<std::string> boolValues = {
    DEBUG_OUTPUT_KEY,       TRACE_KEY,       TRACE_EXPAND_KEY,
    WARN_UNINITIALIZED_KEY, WARN_UNUSED_KEY, WARN_UNUSED_CLI_KEY,
    CHECK_SYSTEM_VARS_KEY
  };
  for (auto i : boolValues) {
    if (!request.Data[i].isNull() && !request.Data[i].isBool()) {
      return request.ReportError("\"" + i +
                                 "\" must be unset or a bool value.");
    }
  }

  cmake* cm = this->CMakeInstance();

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

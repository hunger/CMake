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
#include "cmLocalGenerator.h"
#include "cmMakefile.h"
#include "cmServer.h"
#include "cmSourceFile.h"
#include "cmSystemTools.h"
#include "cmVersionMacros.h"
#include "cmake.h"

#if defined(CMAKE_BUILD_WITH_CMAKE)
#include "cm_jsoncpp_reader.h"
#include "cm_jsoncpp_value.h"
#endif

#include <algorithm>
#include <string>
#include <vector>

namespace {
// Vocabulary:

char CODE_MODEL_TYPE[] = "codemodel";
char COMPUTE_TYPE[] = "compute";
char CONFIGURE_TYPE[] = "configure";
char GLOBAL_SETTINGS_TYPE[] = "globalSettings";
char SET_GLOBAL_SETTINGS_TYPE[] = "setGlobalSettings";

char ARTIFACTS_KEY[] = "artifacts";
char BUILD_DIRECTORY_KEY[] = "buildDirectory";
char CACHE_ARGUMENTS_KEY[] = "cacheArguments";
char CHECK_SYSTEM_VARS_KEY[] = "checkSystemVars";
char COMPILE_FLAGS_KEY[] = "compileFlags";
char CONFIGURATIONS_KEY[] = "configurations";
char COOKIE_KEY[] = "cookie";
char CURRENT_GENERATOR_KEY[] = "currentGenerator";
char DEBUG_OUTPUT_KEY[] = "debugOutput";
char DEFINES_KEY[] = "defines";
char FILE_GROUPS_KEY[] = "fileGroups";
char FRAMEWORK_PATH_KEY[] = "frameworkPath";
char FULL_NAME_KEY[] = "fullName";
char GENERATORS_KEY[] = "generators";
char INCLUDE_PATH_KEY[] = "includePath";
char IS_GENERATED_KEY[] = "isGenerated";
char IS_SYSTEM_KEY[] = "isSystem";
char LANGUAGE_KEY[] = "lanugage";
char LINKER_LANGUAGE_KEY[] = "linkerLanguage";
char LINK_FLAGS_KEY[] = "linkFlags";
char LINK_LANGUAGE_FLAGS_KEY[] = "linkLanguageFlags";
char LINK_LIBRARIES_KEY[] = "linkLibraries";
char LINK_PATH_KEY[] = "linkPath";
char MAJOR_KEY[] = "major";
char MINOR_KEY[] = "minor";
char NAME_KEY[] = "name";
char PATCH_LEVEL_KEY[] = "patchLevel";
char PATH_KEY[] = "path";
char PROJECTS_KEY[] = "projects";
char SOURCE_DIRECTORY_KEY[] = "sourceDirectory";
char SOURCES_KEY[] = "sources";
char STRING_KEY[] = "string";
char SYSROOT_KEY[] = "sysroot";
char TARGETS_KEY[] = "targets";
char TRACE_EXPAND_KEY[] = "traceExpand";
char TRACE_KEY[] = "trace";
char TYPE_KEY[] = "type";
char VERSION_KEY[] = "version";
char WARN_UNINITIALIZED_KEY[] = "warnUninitialized";
char WARN_UNUSED_CLI_KEY[] = "warnUnusedCli";
char WARN_UNUSED_KEY[] = "warnUnused";

static std::vector<std::string> getConfigurations(
  const cmake* cm)
{
  std::vector<std::string> configurations;
  auto makefiles = cm->GetGlobalGenerator()->GetMakefiles();
  if (makefiles.empty()) {
    return configurations;
  }

  makefiles[0]->GetConfigurations(configurations);
  if (configurations.empty())
    configurations.push_back("");
  return configurations;
}

static bool hasString(const Json::Value& v, const std::string& s)
{
  return !v.isNull() &&
    std::find_if(v.begin(), v.end(), [s](const Json::Value& i) {
      return i.asString() == s;
    }) == v.end();
}

template <class T>
static Json::Value fromStringList(const T& in)
{
  Json::Value result = Json::arrayValue;
  for (const std::string& i : in) {
    result.append(i);
  }
  return result;
}

static std::vector<std::string> toStringList(const Json::Value& in)
{
  std::vector<std::string> result;
  for (const auto& it : in) {
    result.push_back(it.asString());
  }
  return result;
}

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

void cmServerProtocol::Activate()
{
  this->m_CMakeInstance = std::make_unique<cmake>();
  this->DoActivate();
}

cmake* cmServerProtocol::CMakeInstance() const
{
  return this->m_CMakeInstance.get();
}

void cmServerProtocol::DoActivate()
{
}

std::pair<int, int> cmServerProtocol1_0::ProtocolVersion() const
{
  return std::make_pair(1, 0);
}

void cmServerProtocol1_0::DoActivate()
{
  this->m_State = ACTIVE;
}

const cmServerResponse cmServerProtocol1_0::Process(
  const cmServerRequest& request)
{
  assert(this->m_State >= ACTIVE);

  if (request.Type == CODE_MODEL_TYPE)
    return this->ProcessCodeModel(request);
  if (request.Type == COMPUTE_TYPE)
    return this->ProcessCompute(request);
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

class LanguageData
{
public:
  bool operator==(const LanguageData& other) const;

  void SetDefines(const std::set<std::string>& defines);

  bool IsGenerated = false;
  std::string Language;
  std::string Flags;
  std::vector<std::string> Defines;
  std::vector<std::pair<std::string, bool> > IncludePathList;
};

bool LanguageData::operator==(const LanguageData& other) const
{
  return Language == other.Language && Defines == other.Defines &&
    Flags == other.Flags && IncludePathList == other.IncludePathList &&
    IsGenerated == other.IsGenerated;
}

void LanguageData::SetDefines(const std::set<std::__cxx11::string>& defines)
{
  std::vector<std::string> result;
  for (auto i : defines) {
    result.push_back(i);
  }
  std::sort(result.begin(), result.end());
  Defines = result;
}

namespace std {

template <>
struct hash<LanguageData>
{
  std::size_t operator()(const LanguageData& in) const
  {
    using std::hash;
    size_t result =
      hash<std::string>()(in.Language) ^ hash<std::string>()(in.Flags);
    for (auto i : in.IncludePathList) {
      result = result ^ (hash<std::string>()(i.first) ^
                         (i.second ? std::numeric_limits<size_t>::max() : 0));
    }
    for (auto i : in.Defines) {
      result = result ^ hash<std::string>()(i);
    }
    result =
      result ^ (in.IsGenerated ? std::numeric_limits<size_t>::max() : 0);
    return result;
  }
};

} // namespace std

static Json::Value DumpSourceFileGroup(const LanguageData& data,
                                       const std::vector<std::string>& files,
                                       const std::string& baseDir)
{
  Json::Value result = Json::objectValue;

  if (!data.Language.empty()) {
    result[LANGUAGE_KEY] = data.Language;
    if (!data.Flags.empty()) {
      result[COMPILE_FLAGS_KEY] = data.Flags;
    }
    if (!data.IncludePathList.empty()) {
      Json::Value includes = Json::arrayValue;
      for (auto i : data.IncludePathList) {
        Json::Value tmp = Json::objectValue;
        tmp[PATH_KEY] = i.first;
        if (i.second) {
          tmp[IS_SYSTEM_KEY] = i.second;
        }
        includes.append(tmp);
      }
      result[INCLUDE_PATH_KEY] = includes;
    }
    if (!data.Defines.empty()) {
      result[DEFINES_KEY] = fromStringList(data.Defines);
    }
  }

  result[IS_GENERATED_KEY] = data.IsGenerated;

  Json::Value sourcesValue = Json::arrayValue;
  for (auto i : files) {
    const std::string relPath =
      cmSystemTools::RelativePath(baseDir.c_str(), i.c_str());
    sourcesValue.append(relPath.size() < i.size() ? relPath : i);
  }

  result[SOURCES_KEY] = sourcesValue;
  return result;
}

static Json::Value DumpSourceFilesList(
  cmGeneratorTarget* target, const std::string& config,
  const std::map<std::string, LanguageData>& languageDataMap)
{
  // Collect sourcefile groups:

  std::vector<cmSourceFile*> files;
  target->GetSourceFiles(files, config);

  std::unordered_map<LanguageData, std::vector<std::string> > fileGroups;
  for (cmSourceFile* file : files) {
    LanguageData fileData;
    fileData.Language = file->GetLanguage();
    if (!fileData.Language.empty()) {
      const LanguageData& ld = languageDataMap.at(fileData.Language);
      cmLocalGenerator* lg = target->GetLocalGenerator();

      std::string compileFlags = ld.Flags;
      lg->AppendFlags(compileFlags, file->GetProperty("COMPILE_FLAGS"));
      fileData.Flags = compileFlags;

      fileData.IncludePathList = ld.IncludePathList;

      std::set<std::string> defines;
      lg->AppendDefines(defines, file->GetProperty("COMPILE_DEFINITIONS"));
      const std::string defPropName =
        "COMPILE_DEFINITIONS_" + cmSystemTools::UpperCase(config);
      lg->AppendDefines(defines, file->GetProperty(defPropName));
      defines.insert(ld.Defines.begin(), ld.Defines.end());

      fileData.SetDefines(defines);
    }

    fileData.IsGenerated = file->GetPropertyAsBool("GENERATED");
    std::vector<std::string>& groupFileList = fileGroups[fileData];
    groupFileList.push_back(file->GetFullPath());
  }

  const std::string baseDir = target->Makefile->GetCurrentSourceDirectory();
  Json::Value result = Json::arrayValue;
  for (auto it = fileGroups.begin(); it != fileGroups.end(); ++it) {
    Json::Value group =
      DumpSourceFileGroup(it->first, it->second, baseDir);
    if (!group.isNull())
      result.append(group);
  }

  return result;
}

static Json::Value DumpTarget(cmGeneratorTarget* target,
                              const std::string& config)
{
  cmLocalGenerator* lg = target->GetLocalGenerator();
  const cmState* state = lg->GetState();

  const cmState::TargetType type = target->GetType();
  const std::string typeName = state->GetTargetTypeName(type);

  Json::Value ttl = Json::arrayValue;
  ttl.append("EXECUTABLE");
  ttl.append("STATIC_LIBRARY");
  ttl.append("SHARED_LIBRARY");
  ttl.append("MODULE_LIBRARY");
  ttl.append("OBJECT_LIBRARY");
  ttl.append("UTILITY");
  ttl.append("INTERFACE_LIBRARY");
  ttl.append("UNKNOWN_LIBRARY");

  if (hasString(ttl, typeName)) {
    return Json::Value();
  }

  Json::Value result = Json::objectValue;
  result[NAME_KEY] = target->GetName();

  result[TYPE_KEY] = typeName;
  result[FULL_NAME_KEY] = target->GetFullName(config);
  result[SOURCE_DIRECTORY_KEY] = lg->GetCurrentSourceDirectory();
  result[BUILD_DIRECTORY_KEY] = lg->GetCurrentBinaryDirectory();

  if (target->HaveWellDefinedOutputFiles()) {
    Json::Value artifacts = Json::arrayValue;
    artifacts.append(target->GetFullPath(config, false));
    if (target->IsDLLPlatform()) {
      artifacts.append(target->GetFullPath(config, true));
      const cmGeneratorTarget::OutputInfo* output =
        target->GetOutputInfo(config);
      if (output && !output->PdbDir.empty()) {
        artifacts.append(output->PdbDir + '/' + target->GetPDBName(config));
      }
    }
    result[ARTIFACTS_KEY] = artifacts;

    result[LINKER_LANGUAGE_KEY] = target->GetLinkerLanguage(config);

    std::string linkLibs;
    std::string linkFlags;
    std::string linkLanguageFlags;
    std::string frameworkPath;
    std::string linkPath;
    lg->GetTargetFlags(config, linkLibs, linkLanguageFlags, linkFlags,
                       frameworkPath, linkPath, target, false);

    linkLibs = cmSystemTools::TrimWhitespace(linkLibs);
    linkFlags = cmSystemTools::TrimWhitespace(linkFlags);
    linkLanguageFlags = cmSystemTools::TrimWhitespace(linkLanguageFlags);
    frameworkPath = cmSystemTools::TrimWhitespace(frameworkPath);
    linkPath = cmSystemTools::TrimWhitespace(linkPath);

    if (!cmSystemTools::TrimWhitespace(linkLibs).empty()) {
      result[LINK_LIBRARIES_KEY] = linkLibs;
    }
    if (!cmSystemTools::TrimWhitespace(linkFlags).empty()) {
      result[LINK_FLAGS_KEY] = linkFlags;
    }
    if (!cmSystemTools::TrimWhitespace(linkLanguageFlags).empty()) {
      result[LINK_LANGUAGE_FLAGS_KEY] = linkLanguageFlags;
    }
    if (!frameworkPath.empty()) {
      result[FRAMEWORK_PATH_KEY] = frameworkPath;
    }
    if (!linkPath.empty()) {
      result[LINK_PATH_KEY] = linkPath;
    }
    const std::string sysroot =
      lg->GetMakefile()->GetSafeDefinition("CMAKE_SYSROOT");
    if (!sysroot.empty()) {
      result[SYSROOT_KEY] = sysroot;
    }
  }

  std::set<std::string> languages;
  target->GetLanguages(languages, config);
  std::map<std::string, LanguageData> languageDataMap;
  for (auto lang : languages) {
    LanguageData& ld = languageDataMap[lang];
    ld.Language = lang;
    lg->GetTargetCompileFlags(target, config, lang, ld.Flags);
    std::set<std::string> defines;
    lg->GetTargetDefines(target, config, lang, defines);
    ld.SetDefines(defines);
    std::vector<std::string> includePathList;
    lg->GetIncludeDirectories(includePathList, target, lang, config, true);
    for (auto i : includePathList) {
      ld.IncludePathList.push_back(
            std::make_pair(i, target->IsSystemIncludeDirectory(i, config)));
    }
  }

  Json::Value sourceGroupsValue =
      DumpSourceFilesList(target, config, languageDataMap);
  if (!sourceGroupsValue.empty()) {
    result[FILE_GROUPS_KEY] = sourceGroupsValue;
  }

  return result;
}

static Json::Value DumpTargetsList(
  const std::vector<cmLocalGenerator*>& generators, const std::string& config)
{
  Json::Value result = Json::arrayValue;

  std::vector<cmGeneratorTarget*> targetList;
  for (const auto& lgIt : generators) {
    auto list = lgIt->GetGeneratorTargets();
    targetList.insert(targetList.end(), list.begin(), list.end());
  }
  std::sort(targetList.begin(), targetList.end());

  for (cmGeneratorTarget* target : targetList) {
    Json::Value tmp = DumpTarget(target, config);
    if (!tmp.isNull()) {
      result.append(tmp);
    }
  }

  return result;
}

static Json::Value DumpProjectList(const cmake* cm,
                                   const std::string config)
{
  Json::Value result = Json::arrayValue;

  auto globalGen = cm->GetGlobalGenerator();

  for (const auto& projectIt : globalGen->GetProjectMap()) {
    Json::Value pObj = Json::objectValue;
    pObj[NAME_KEY] = projectIt.first;

    assert(projectIt.second.size() >
           0); // All Projects must have at least one local generator
    const cmLocalGenerator* lg = projectIt.second.at(0);

    // Project structure information:
    const cmMakefile* mf = lg->GetMakefile();
    pObj[SOURCE_DIRECTORY_KEY] = mf->GetCurrentSourceDirectory();
    pObj[BUILD_DIRECTORY_KEY] = mf->GetCurrentBinaryDirectory();
    pObj[TARGETS_KEY] = DumpTargetsList(projectIt.second, config);

    result.append(pObj);
  }

  return result;
}

static Json::Value DumpConfiguration(const cmake *cm, const std::string& config)
{
  Json::Value result = Json::objectValue;
  result[NAME_KEY] = config;

  result[PROJECTS_KEY] = DumpProjectList(cm, config);

  return result;
}

static Json::Value DumpConfigurationsList(const cmake* cm)
{
  Json::Value result = Json::arrayValue;

  for (const std::string& c : getConfigurations(cm)) {
    result.append(DumpConfiguration(cm, c));
  }

  return result;
}

cmServerResponse cmServerProtocol1_0::ProcessCodeModel(
  const cmServerRequest& request)
{
  if (this->m_State != COMPUTED) {
    return request.ReportError("No build system was generated yet.");
  }

  Json::Value result = Json::objectValue;
  result[CONFIGURATIONS_KEY] = DumpConfigurationsList(this->CMakeInstance());
  return request.Reply(result);
}

cmServerResponse cmServerProtocol1_0::ProcessCompute(
  const cmServerRequest& request)
{
  if (this->m_State > CONFIGURED) {
    return request.ReportError("This build system was already generated.");
  }
  if (this->m_State < CONFIGURED) {
    return request.ReportError("This project was not configured yet.");
  }

  cmake* cm = this->CMakeInstance();
  int ret = cm->Generate();

  if (ret < 0) {
    return request.ReportError("Failed to compute build system.");
  } else {
    m_State = COMPUTED;
    return request.Reply(Json::Value());
  }
}

cmServerResponse cmServerProtocol1_0::ProcessConfigure(
  const cmServerRequest& request)
{
  if (this->m_State != ACTIVE) {
    return request.ReportError("This instance was already configured.");
  }

  // Make sure the types of cacheArguments matches (if given):
  std::vector<std::string> cacheArgs;
  cacheArgs = toStringList(request.Data[CACHE_ARGUMENTS_KEY]);

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

cmServerResponse cmServerProtocol1_0::ProcessSetGlobalSettings(
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

  cmake* cm = this->CMakeInstance();
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

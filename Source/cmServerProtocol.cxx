/*============================================================================
  CMake - Server Protocol Implementations
  Copyright 2015 Stephen Kelly <steveire@gmail.com>
  Copyright 2015 Tobias Hunger <tobias.hunger@gmail.com>

  Distributed under the OSI-approved BSD License (the "License");
  see accompanying file Copyright.txt for details.

  This software is distributed WITHOUT ANY WARRANTY; without even the
  implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
  See the License for more information.
============================================================================*/

#include "cmServerProtocol.h"

#include "cmServer.h"
#include "cmVersionMacros.h"
#include "cmGlobalGenerator.h"
#include "cmLocalGenerator.h"
#include "cmMakefile.h"
#include "cmSourceFile.h"
#include "cmServerDiff.h"
#include "cmServerParser.h"
#include "cmCommand.h"
#include "cmServerCompleter.h"
#include "cmServerVocabulary.h"

#if defined(CMAKE_BUILD_WITH_CMAKE)
# include "cm_jsoncpp_value.h"
# include "cm_jsoncpp_reader.h"
#endif

static std::string INVALID_LINE_ERROR(const std::string &key)
{
    return std::string("\"") + key + std::string("\" must be a positive integer.");
}

static std::string KEY_IS_MANDATORY_ERROR(const std::string &key)
{
    return std::string("\"") + key + std::string("\" may not be unset.");
}

static std::string NOT_INITIALIZED_ERROR()
{
    return std::string("Not initialized yet.");
}

void getUnreachable(Json::Value& unreachable,
                    DifferentialFileContent const& diff,
                    std::map<long, long> const& nx)
{
  auto& chunks = diff.Chunks;

  auto chunkIt = chunks.begin();

  for (auto it = nx.begin(); it != nx.end(); ++it)
    {
    chunkIt = std::lower_bound(chunkIt, chunks.end(), it->first,
                       [](const Chunk& lhs, long rhs)
      {
        return lhs.OrigStart < rhs;
      });
    if (chunkIt == chunks.end() || chunkIt->OrigStart != it->first)
      {
      --chunkIt;
      }
    auto theLine = chunkIt->NewStart;
    while (chunkIt->NumCommon + chunkIt->NumAdded == 0) // Should be NumRemoved?
      {
      ++chunkIt;
      }
    assert(theLine == chunkIt->NewStart);
    if (chunkIt->OrigStart > it->first)
      {
      continue;
      }

    long offset = chunkIt->NewStart - chunkIt->OrigStart;

    Json::Value elem = Json::objectValue;
    elem[BEGIN_KEY] = (int)(it->first + offset);
    elem[END_KEY] = (int)(it->second + offset);
    unreachable.append(elem);
    }
}

cmServerRequest::cmServerRequest(cmMetadataServer* server, const std::string &t, const std::string &c, const Json::Value &d)
  : Type(t), Cookie(c), Data(d), Server(server)
{

}

void cmServerRequest::ReportProgress(int min, int current, int max, const std::string& message) const
{
  Server->WriteProgress(*this, min, current, max, message);
}

cmServerResponse cmServerRequest::Reply(const Json::Value& data) const
{
  cmServerResponse response(*this);
  response.setData(data);
  return response;
}

cmServerResponse cmServerRequest::ReportError(const std::string& message) const
{
  cmServerResponse response(*this);
  response.setError(message);
  return response;
}

cmServerResponse::cmServerResponse(const cmServerRequest &request)
  : Type(request.Type), Cookie(request.Cookie)
{

}

void cmServerResponse::setData(const Json::Value &data)
{
  assert(mPayload == UNKNOWN);
  mPayload = DATA;
  mData = data;
}

void cmServerResponse::setError(const std::string &message)
{
  assert(mPayload == UNKNOWN);
  mPayload = ERROR;
  mErrorMessage = message;
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
    return mErrorMessage;
  else
    return std::string();
}

Json::Value cmServerResponse::Data() const
{
  assert(mPayload != UNKNOWN);
  return mData;
}

cmServerProtocol::~cmServerProtocol() { }

cmServerProtocol0_1::cmServerProtocol0_1()
  : CMakeInstance(0)
{

  }

cmServerProtocol0_1::~cmServerProtocol0_1()
{
  delete this->CMakeInstance;
}

std::pair<int, int> cmServerProtocol0_1::protocolVersion() const
{
  return std::make_pair(0, 1);
}

void cmServerProtocol0_1::activate()
{
  assert(!CMakeInstance);
  this->CMakeInstance = new cmake;
  this->CMakeInstance->SetWorkingMode(cmake::SNAPSHOT_RECORD_MODE);
  mState = ACTIVE;
}

const cmServerResponse cmServerProtocol0_1::process(const cmServerRequest &request)
{
  assert(mState >= ACTIVE);
  if (request.Type == "initialize")
    {
    return ProcessInitialize(request);
    }
  if (request.Type == "version")
    {
    return ProcessVersion(request);
    }
  if (request.Type == "generators")
    {
    return ProcessGenerator(request);
    }
  if (request.Type == "buildsystem")
    {
    return ProcessBuildSystem(request);
    }
  if (request.Type == "targetInfo")
    {
    return ProcessTargetInfo(request);
    }
  if (request.Type == "fileInfo")
    {
    return ProcessFileInfo(request);
    }
  if (request.Type == "content")
    {
    return ProcessContent(request);
    }
  if (request.Type == "parse")
    {
    return ProcessParse(request);
    }
  if (request.Type == "contextualHelp")
    {
    return ProcessContextualHelp(request);
    }
  if (request.Type == "contentDiff")
    {
    return ProcessContentDiff(request);
    }
  if (request.Type == "codeComplete")
    {
    return ProcessCodeComplete(request);
    }
  if (request.Type == "contextWriters")
    {
    return ProcessContextWriters(request);
    }

  return request.ReportError("Unknown command!");
}

cmServerResponse cmServerProtocol0_1::ProcessInitialize(const cmServerRequest &request)
{
  if (mState == INITIALIZED)
    {
    return request.ReportError("Already initialized.");
    }

  std::string buildDir = request.Data[BUILD_DIRECTORY_KEY].asString();
  if (buildDir.empty())
    {
    return request.ReportError(KEY_IS_MANDATORY_ERROR(BUILD_DIRECTORY_KEY));
    }

  std::set<std::string> emptySet;
  if(!this->CMakeInstance->GetState()->LoadCache(buildDir.c_str(),
                                                 true, emptySet, emptySet))
    {
    return request.ReportError("Failed to load cache in build directory.");
    }

  const char* genName =
      this->CMakeInstance->GetState()
          ->GetInitializedCacheValue("CMAKE_GENERATOR");
  if (!genName)
    {
    return request.ReportError("No CMAKE_GENERATOR value found in cache.");
    }

  const char* sourceDir =
      this->CMakeInstance->GetState()
          ->GetInitializedCacheValue("CMAKE_HOME_DIRECTORY");
  if (!sourceDir)
    {
    return request.ReportError("No CMAKE_HOME_DIRECTORY value found in cache.");
    }

  this->CMakeInstance->SetHomeDirectory(sourceDir);
  this->CMakeInstance->SetHomeOutputDirectory(buildDir);
  this->CMakeInstance->SetGlobalGenerator(
    this->CMakeInstance->CreateGlobalGenerator(genName));

  this->CMakeInstance->LoadCache();
  this->CMakeInstance->SetSuppressDevWarnings(true);
  this->CMakeInstance->SetWarnUninitialized(false);
  this->CMakeInstance->SetWarnUnused(false);
  this->CMakeInstance->PreLoadCMakeFiles();

  request.ReportProgress(0, 0, 3, "initialized");

  // First not? But some other mode that aborts after ActualConfigure
  // and creates snapshots?
  this->CMakeInstance->Configure();
  mState = CONFIGURED;

  request.ReportProgress(0, 1, 3, "configured");

  if (!this->CMakeInstance->GetGlobalGenerator()->Compute())
    {
    return request.ReportError("Failed to run generator.");
    }
  mState = COMPUTED;
  request.ReportProgress(0, 2, 3, "computed");

  cmState* state = this->CMakeInstance->GetState();

  auto snps = state->TraceSnapshots();
  for (auto is = snps.begin(); is != snps.end(); ++is)
    {
    this->Snapshots[is->first] = is->second;
    }

  auto srcDir = this->CMakeInstance->GetState()->GetSourceDirectory();

  mState = INITIALIZED;
  request.ReportProgress(0, 3, 3, "done");

  Json::Value obj = Json::objectValue;
  obj[SOURCE_DIRECTORY_KEY] = srcDir;
  obj[BUILD_DIRECTORY_KEY] = this->CMakeInstance->GetState()->GetBinaryDirectory();
  obj[PROJECT_NAME_KEY] = this->CMakeInstance->GetGlobalGenerator()
      ->GetLocalGenerators()[0]->GetProjectName();

  return request.Reply(obj);
}

cmServerResponse cmServerProtocol0_1::ProcessGenerator(const cmServerRequest &request)
{
  assert(this->CMakeInstance);
  std::vector<cmDocumentationEntry> entries;
  this->CMakeInstance->GetGeneratorDocumentation(entries);
  Json::Value obj = Json::arrayValue;
  for (const cmDocumentationEntry &e : entries)
    {
    Json::Value gen = Json::objectValue;
    gen[GENERATOR_KEY] = e.Name;
    gen[DESCRIPTION_KEY] = e.Brief;
    obj.append(gen);
    }
  return request.Reply(obj);
}

cmServerResponse cmServerProtocol0_1::ProcessVersion(const cmServerRequest &request)
{
  Json::Value obj = std::string(CMake_VERSION);
  return request.Reply(obj);
}

cmServerResponse cmServerProtocol0_1::ProcessBuildSystem(const cmServerRequest &request)
{
  if (mState < INITIALIZED)
    {
    return request.ReportError(NOT_INITIALIZED_ERROR());
    }

  Json::Value root = Json::objectValue;

  auto mf = this->CMakeInstance->GetGlobalGenerator()->GetMakefiles()[0];
  auto lg = this->CMakeInstance->GetGlobalGenerator()->GetLocalGenerators()[0];

  Json::Value& configs = root[CONFIGURATION_LIST_KEY] = Json::arrayValue;

  std::vector<std::string> configsVec;
  mf->GetConfigurations(configsVec);
  for (auto const& config : configsVec)
    {
    configs.append(config);
    }

  Json::Value& globalTargets = root[GLOBAL_TARGET_LIST_KEY] = Json::arrayValue;
  Json::Value& targets = root[TARGET_LIST_KEY] = Json::arrayValue;
  auto gens = this->CMakeInstance->GetGlobalGenerator()->GetLocalGenerators();

  auto firstMf =
      this->CMakeInstance->GetGlobalGenerator()->GetMakefiles()[0];
  auto firstTgts = firstMf->GetTargets();
  for (auto const& tgt : firstTgts)
    {
    if (tgt.second.GetType() == cmState::GLOBAL_TARGET)
      {
      globalTargets.append(tgt.second.GetName());
      }
    }

  for (auto const& gen : gens)
    {
    for (auto const& tgt : gen->GetGeneratorTargets())
      {
      if (tgt->IsImported())
        {
        continue;
        }
      if (tgt->GetType() == cmState::GLOBAL_TARGET)
        {
        continue;
        }
      Json::Value target = Json::objectValue;
      target[TARGET_NAME_KEY] = tgt->GetName();
      target[TARGET_TYPE_KEY] = cmState::GetTargetTypeName(tgt->GetType());

      if (tgt->GetType() <= cmState::UTILITY)
        {
        auto lfbt = tgt->GetBacktrace();
        Json::Value bt = Json::arrayValue;
        for (auto const& lbtF : lfbt.FrameContexts())
          {
          Json::Value fff = Json::objectValue;
          fff[PATH_KEY] = lbtF.FilePath;
          fff[LINE_KEY] = (int)lbtF.Line;
          bt.append(fff);
          }
        target[BACKTRACE_KEY] = bt;
        if (tgt->GetType() < cmState::OBJECT_LIBRARY)
          {
//          std::string fp = (*ittgt)->GetFullPath(config, false, true);
//          targetValue["targetFile"] = fp;
          }
        }
      // Should be list?
      target[PROJECT_NAME_KEY] = lg->GetProjectName();
      targets.append(target);
      }
    }

  return request.Reply(root);
}

cmServerResponse cmServerProtocol0_1::ProcessTargetInfo(const cmServerRequest &request)
{
    if (mState < INITIALIZED)
    {
    return request.ReportError(NOT_INITIALIZED_ERROR());
    }

  std::string tgtName = request.Data[TARGET_NAME_KEY].asString();
  if (tgtName.empty())
    {
    return request.ReportError(KEY_IS_MANDATORY_ERROR(TARGET_NAME_KEY));
    }
  std::string config = request.Data[CONFIGURATION_KEY].asString();
  const char* language = nullptr;
  if (request.Data.isMember(LANGUAGE_KEY))
    {
    language = request.Data[LANGUAGE_KEY].asCString();
    }

  Json::Value obj = Json::objectValue;

  auto tgt =
      this->CMakeInstance->GetGlobalGenerator()->FindGeneratorTarget(tgtName);

  if (!tgt)
    {
    return request.ReportError("Failed to find target.");
    }

  obj[TARGET_NAME_KEY] = tgt->GetName();

  if (tgt->GetType() != cmState::GLOBAL_TARGET
      && tgt->GetType() != cmState::UTILITY
      && tgt->GetType() != cmState::OBJECT_LIBRARY)
    {
    obj[BUILD_DIRECTORY_KEY] = tgt->GetLocation(config);
    if (tgt->HasImportLibrary())
      {
      obj[BUILD_IMPORT_LIBRARY_KEY] = tgt->GetFullPath(config, true);
      }
    }

  std::vector<const cmSourceFile*> files;

  tgt->GetObjectSources(files, config);

  Json::Value& object_sources = obj[OBJECT_SOURCE_LIST_KEY] = Json::arrayValue;
  Json::Value& generated_object_sources = obj[GENERATED_OBJECT_SOURCE_LIST_KEY] = Json::arrayValue;
  for (auto const& sf : files)
    {
    std::string filePath = sf->GetFullPath();
    if (sf->GetProperty("GENERATED"))
      {
      generated_object_sources.append(filePath);
      }
    else
      {
      object_sources.append(filePath);
      }
    }

  files.clear();

  tgt->GetHeaderSources(files, config);

  Json::Value& header_sources = obj[HEADER_SOURCE_LIST_KEY] = Json::arrayValue;
  Json::Value& generated_header_sources = obj[GENERATED_HEADER_SOURCE_LIST_KEY] = Json::arrayValue;
  for (auto const& sf : files)
    {
    std::string filePath = sf->GetFullPath();
    if (sf->GetProperty("GENERATED"))
      {
      generated_header_sources.append(filePath);
      }
    else
      {
      header_sources.append(filePath);
      }
    }

  Json::Value& target_defines = obj[COMPILE_DEFINITION_LIST_KEY] = Json::arrayValue;

  std::string lang = language ? language : "C";

  std::vector<std::string> cdefs;
  tgt->GetCompileDefinitions(cdefs, config, lang);
  for (auto const& cdef : cdefs)
    {
    target_defines.append(cdef);
    }

  Json::Value& target_features = obj[COMPILE_FEATURES_LIST_KEY] = Json::arrayValue;

  std::vector<std::string> features;
  tgt->GetCompileFeatures(cdefs, config);
  for (auto const& feature : features)
    {
    target_features.append(feature);
    }

  Json::Value& target_options = obj[COMPILE_OPTIONS_LIST_KEY] = Json::arrayValue;

  std::vector<std::string> options;
  tgt->GetCompileOptions(cdefs, config, lang);
  for (auto const& option : options)
    {
    target_options.append(option);
    }

  Json::Value& target_includes = obj[INCLUDE_DIRECTORY_LIST_KEY] = Json::arrayValue;

  std::vector<std::string> dirs;
  tgt->GetLocalGenerator()->GetIncludeDirectories(dirs, tgt, lang, config);
  for (auto const& dir : dirs)
    {
    target_includes.append(dir);
    }

  return request.Reply(obj);
}

cmServerResponse cmServerProtocol0_1::ProcessFileInfo(const cmServerRequest &request)
{
  if (mState < INITIALIZED)
    {
    return request.ReportError(NOT_INITIALIZED_ERROR());
    }

  const std::string tgtName = request.Data[TARGET_NAME_KEY].asString();
  const std::string config = request.Data[CONFIGURATION_KEY].asString();
  const std::string file_path = request.Data[PATH_KEY].asString();

  if (tgtName.empty())
    {
    return request.ReportError(KEY_IS_MANDATORY_ERROR(TARGET_NAME_KEY));
    }

  if (file_path.empty())
    {
    return request.ReportError(KEY_IS_MANDATORY_ERROR(PATH_KEY));
    }

  auto tgt =
      this->CMakeInstance->GetGlobalGenerator()->FindGeneratorTarget(tgtName);

  if (!tgt)
    {
    return request.ReportError("Target not found.");
    }

  Json::Value obj = Json::objectValue;

  std::vector<const cmSourceFile*> files;
  tgt->GetObjectSources(files, config);

  const cmSourceFile* file = 0;
  for (auto const& sf : files)
    {
    if (sf->GetFullPath() == file_path)
      {
      file = sf;
      break;
      }
    }

  if (!file)
    {
    return request.ReportError("File not found.");
    }

  // TODO: Get the includes/defines/flags for the file for this target.
  // There does not seem to be suitable API for that yet.

  return request.Reply(obj);
}

cmServerResponse cmServerProtocol0_1::ProcessContent(const cmServerRequest &request)
{
  if (mState < INITIALIZED)
    {
    return request.ReportError(NOT_INITIALIZED_ERROR());
    }

  const std::string filePath = request.Data[PATH_KEY].asString();
  const long fileLine = request.Data[LINE_KEY].asInt();
  const DifferentialFileContent diff = cmServerDiff::GetDiff(request.Data);
  const std::string matcher = request.Data[MATCHER_KEY].asString();

  if (filePath.empty())
    {
    return request.ReportError(KEY_IS_MANDATORY_ERROR(PATH_KEY));
    }
  if (fileLine <= 0)
    {
    return request.ReportError(INVALID_LINE_ERROR(LINE_KEY));
    }

  if (this->IsNotExecuted(filePath, fileLine))
    {
    return request.Reply(UNEXECUTED_VALUE);
    }

  auto res = this->GetSnapshotAndStartLine(filePath, fileLine, diff);
  if (res.second < 0)
    {
    return request.Reply(UNEXECUTED_VALUE);
    }

  auto desired =
      this->GetDesiredSnapshot(diff.EditorLines, res.second, res.first, fileLine);
  cmState::Snapshot contentSnp = desired.first;
  if (!contentSnp.IsValid())
    {
    return request.Reply(UNEXECUTED_VALUE);
    }

  return request.Reply(GenerateContent(contentSnp, matcher));
}

cmServerResponse cmServerProtocol0_1::ProcessParse(const cmServerRequest &request)
{
  const std::string file_path = request.Data[PATH_KEY].asString();
  DifferentialFileContent diff = cmServerDiff::GetDiff(request.Data);

  if (file_path.empty())
    {
    return request.ReportError(KEY_IS_MANDATORY_ERROR(PATH_KEY));
    }

  Json::Value obj = Json::objectValue;

  cmServerParser p(this->CMakeInstance->GetState(),
                   file_path, cmSystemTools::GetCMakeRoot());
  cmServerParser::ParseResult result = p.Parse(diff);
  if (result.IsError)
    {
    return request.ReportError(result.ErrorMessage);
    }
  obj[TOKENS_KEY] = result.Result;

  auto& unreachable = obj[UNREACHABLE_KEY] = Json::arrayValue;

  auto nx = this->CMakeInstance->GetState()->GetNotExecuted(file_path);

  getUnreachable(unreachable, diff, nx);

  return request.Reply(obj);
}

cmServerResponse cmServerProtocol0_1::ProcessContextualHelp(const cmServerRequest &request)
{
  if (mState < INITIALIZED)
    {
    return request.ReportError(NOT_INITIALIZED_ERROR());
    }

  const std::string filePath = request.Data[PATH_KEY].asString();
  const long fileLine = request.Data[LINE_KEY].asInt();
  const long fileColumn = request.Data[COLUMN_KEY].asInt();
  const std::string fileContent = request.Data[CONTENTS_KEY].asString();

  if (fileLine <= 0)
    {
    return request.ReportError(INVALID_LINE_ERROR(LINE_KEY));
    }

  std::string content;
  {
  std::stringstream ss(fileContent);

  long desiredLines = fileLine;
  for (std::string line;
       std::getline(ss, line, '\n') && desiredLines > 0;
       --desiredLines)
    {
    content += line + "\n";
    }
  }

  cmListFile listFile;

  if (!listFile.ParseString(
        content.c_str(),
        filePath.c_str(),
        this->CMakeInstance->GetGlobalGenerator()->GetMakefiles()[0]))
    {
    return request.ReportError("Failed to parse.");
    }

  const size_t numberFunctions = listFile.Functions.size();
  size_t funcIndex = 0;
  for( ; funcIndex < numberFunctions; ++funcIndex)
    {
    if (listFile.Functions[funcIndex].Line > fileLine)
      {
      Json::Value obj = Json::objectValue;
      obj[NO_CONTEXT_KEY] = true;
      return request.Reply(obj);
      }

    const long closeParenLine = listFile.Functions[funcIndex].CloseParenLine;

    if (listFile.Functions[funcIndex].Line <= fileLine
        && closeParenLine >= fileLine)
      {
      auto args = listFile.Functions[funcIndex].Arguments;
      const size_t numberArgs = args.size();
      size_t argIndex = 0;

      for( ; argIndex < numberArgs; ++argIndex)
        {
        if (args[argIndex].Delim == cmListFileArgument::Bracket)
          {
          continue;
          }

        const bool lastArg = (argIndex == numberArgs - 1);

        if (lastArg
            || (argIndex != numberArgs
                && (args[argIndex + 1].Line > fileLine
                    || args[argIndex + 1].Column > fileColumn)))
          {
          if (args[argIndex].Line > fileLine ||
              args[argIndex].Column > fileColumn)
            {
            return request.Reply(GenerateContextualHelp("command",
                                                        listFile.Functions[funcIndex].Name));
            }
          if (args[argIndex].Delim == cmListFileArgument::Unquoted)
            {
            auto endPos = args[argIndex].Column + args[argIndex].Value.size();
            if (args[argIndex].Line == fileLine
                && args[argIndex].Column <= fileColumn
                && (long)endPos >= fileColumn)
              {
              auto inPos = fileColumn - args[argIndex].Column;
              auto closePos = args[argIndex].Value.find('}', inPos);
              auto openPos = args[argIndex].Value.rfind('{', inPos);
              if (openPos != std::string::npos)
                {
                if (openPos > 0 && args[argIndex].Value[openPos - 1] == '$')
                  {
                  auto endRel = closePos == std::string::npos
                        ? closePos - openPos - 1 : inPos - openPos - 1;
                  std::string relevant =
                      args[argIndex].Value.substr(openPos + 1, endRel);
                  Json::Value help = GenerateContextualHelp("variable", relevant);
                  if (!help.isNull())
                    {
                    return request.Reply(help);
                    }
                  }
                }
              Json::Value help = this->EmitTypedIdentifier(listFile.Functions[funcIndex].Name,
                                                           args, argIndex);
              if (!help.isNull())
                {
                return request.Reply(help);
                }
              }
            break;
            }

          long fileLineDiff = fileLine - args[argIndex].Line;

          long fileColumnDiff = fileColumn - args[argIndex].Column;

          bool breakOut = false;

          size_t argPos = 0;
          while (fileLineDiff != 0)
            {
            argPos = args[argIndex].Value.find('\n', argPos);
            if (argPos == std::string::npos)
              {
              breakOut = true;
              break;
              }
            ++argPos;
            fileColumnDiff = 0;
            --fileLineDiff;
            }
          if (breakOut)
            {
            break;
            }

          assert(fileLineDiff == 0);

          size_t sentinal = args[argIndex].Value.find('\n', argPos);
          if (sentinal == std::string::npos)
            {
            sentinal = args[argIndex].Value.size() - argPos;
            if ((long)sentinal < fileColumn)
              {
              break;
              }
            Json::Value help = this->EmitTypedIdentifier(listFile.Functions[funcIndex].Name,
                                       args, argIndex);
            if (!help.isNull())
              {
              return request.Reply(help);
              }
            }

          if (sentinal < argPos)
            {
            // In between args?
            break;
            }

          long inPos = fileColumnDiff;

          std::string relevant =
              args[argIndex].Value.substr(argPos, sentinal - argPos);

          auto closePos = relevant.find('}', inPos);
          auto openPos = relevant.rfind('{', inPos);
          if (openPos != std::string::npos)
            {
            if (openPos > 0 && relevant[openPos - 1] == '$')
              {
              auto endRel = closePos == std::string::npos
                    ? closePos - openPos - 1 : inPos - openPos - 1;
              relevant = relevant.substr(openPos + 1, endRel);
              Json::Value help = GenerateContextualHelp("variable", relevant);
              if (!help.isNull())
                return request.Reply(help);
              else
                break;
              }
            }
          break;
          }
        }

      return request.Reply(GenerateContextualHelp("command", listFile.Functions[funcIndex].Name));
      }
    }
  return request.Reply(Json::objectValue);
}

cmServerResponse cmServerProtocol0_1::ProcessContentDiff(const cmServerRequest &request)
{
  if (mState < INITIALIZED)
    {
    return request.ReportError(NOT_INITIALIZED_ERROR());
    }

  const std::string filePath1 = request.Data[PATH1_KEY].asString();
  const long fileLine1 = request.Data[LINE1_KEY].asInt();
  const std::string filePath2 = request.Data[PATH2_KEY].asString();
  const long fileLine2 = request.Data[LINE2_KEY].asInt();
  const std::pair<DifferentialFileContent, DifferentialFileContent> diffs
          = cmServerDiff::GetDiffs(request.Data);

  if (fileLine1 <= 0)
    {
    return request.ReportError(INVALID_LINE_ERROR(LINE1_KEY));
    }
  if (fileLine2 <= 0)
    {
    return request.ReportError(INVALID_LINE_ERROR(LINE2_KEY));
    }

  if (this->IsNotExecuted(filePath1, fileLine1)
      || this->IsNotExecuted(filePath2, fileLine2))
    {
    return request.Reply(UNEXECUTED_VALUE);
    }

  auto res1 = GetSnapshotAndStartLine(filePath1, fileLine1, diffs.first);
  if (res1.second < 0)
    {
    return request.Reply(UNEXECUTED_VALUE);
    }

  auto res2 = GetSnapshotAndStartLine(filePath2, fileLine2, diffs.second);
  if (res2.second < 0)
    {
    return request.Reply(UNEXECUTED_VALUE);
    }

  auto desired1 =
      GetDesiredSnapshot(diffs.first.EditorLines, res1.second, res1.first, fileLine1);
  cmState::Snapshot contentSnp1 = desired1.first;
  if (!contentSnp1.IsValid())
    {
    return request.Reply(UNEXECUTED_VALUE);
    }

  auto desired2 =
      GetDesiredSnapshot(diffs.second.EditorLines, res2.second, res2.first, fileLine2);
  cmState::Snapshot contentSnp2 = desired2.first;
  if (!contentSnp2.IsValid())
    {
    return request.Reply(UNEXECUTED_VALUE);
    }

  Json::Value obj = Json::objectValue;

  std::vector<std::string> keys1 = contentSnp1.ClosureKeys();
  std::vector<std::string> keys2 = contentSnp2.ClosureKeys();

  auto& addedDefs = obj[ADDED_DEFINITIONS_LIST_KEY] = Json::arrayValue;
  auto& removedDefs = obj[REMOVED_DEFINITIONS_LIST_KEY] = Json::arrayValue;

  for(auto key : keys2)
    {
    auto d1 = contentSnp1.GetDefinition(key);
    d1 = d1 ? d1 : "";
    auto d2 = contentSnp2.GetDefinition(key);
    d2 = d2 ? d2 : "";
    if (std::find(keys1.begin(), keys1.end(), key) != keys1.end()
        && !strcmp(d1, d2))
      continue;
    Json::Value def = Json::objectValue;
    def[KEY_KEY] = key;
    def[VALUE_KEY] = contentSnp2.GetDefinition(key);
    addedDefs.append(def);
    }

  for(auto key : keys1)
    {
    auto d1 = contentSnp1.GetDefinition(key);
    d1 = d1 ? d1 : "";
    auto d2 = contentSnp2.GetDefinition(key);
    d2 = d2 ? d2 : "";
    if (!strcmp(d1, d2))
      continue;
    Json::Value def = Json::objectValue;
    def[KEY_KEY] = key;
    def[VALUE_KEY] = contentSnp1.GetDefinition(key);
    removedDefs.append(def);
    }

  return request.Reply(obj);
}

cmServerResponse cmServerProtocol0_1::ProcessCodeComplete(const cmServerRequest &request)
{
  if (mState < INITIALIZED)
    {
    return request.ReportError(NOT_INITIALIZED_ERROR());
    }

  const std::string filePath = request.Data[PATH_KEY].asString();
  const long fileLine = request.Data[LINE_KEY].asInt();
  const long fileColumn = request.Data[COLUMN_KEY].asInt();
  const DifferentialFileContent diff = cmServerDiff::GetDiff(request.Data);

  if (fileLine <= 0)
    {
    return request.ReportError(INVALID_LINE_ERROR(LINE_KEY));
    }

  auto res = GetSnapshotAndStartLine(filePath, fileLine, diff);
  if (res.second < 0)
    {
    return request.Reply(NO_COMPLETION_VALUE);
    }

  auto desired =
      GetDesiredSnapshot(diff.EditorLines, res.second, res.first, fileLine, true);
  cmState::Snapshot completionSnp = desired.first;
  if (!completionSnp.IsValid())
    {
    return request.Reply(NO_COMPLETION_VALUE);
    }

  auto prParseStart = diff.EditorLines.begin() + res.second - 1;
  auto prParseEnd = diff.EditorLines.begin() + fileLine - 1;
  auto newToParse = std::distance(prParseStart, prParseEnd) + 1;

  auto theLine = *prParseEnd;

  std::string completionPrefix;

  auto columnData = theLine.substr(0, fileColumn);
  auto strt = columnData.find_first_not_of(' ');
  if (strt != std::string::npos)
    {
    completionPrefix = columnData.substr(strt);
    }

  cmServerCompleter completer(this->CMakeInstance, completionSnp);

  auto result = completer.Complete(completionSnp,
                       desired.second, completionPrefix,
                       newToParse, fileColumn);

  return request.Reply(result);
}

cmServerResponse cmServerProtocol0_1::ProcessContextWriters(const cmServerRequest &request)
{
  if (mState < INITIALIZED)
    {
    return request.ReportError(NOT_INITIALIZED_ERROR());
    }

  const std::string filePath = request.Data[PATH_KEY].asString();
  const long fileLine = request.Data[LINE_KEY].asInt();
  const long fileColumn = request.Data[COLUMN_KEY].asInt();
  const DifferentialFileContent diff = cmServerDiff::GetDiff(request.Data);

  if (fileLine <= 0)
    {
    return request.ReportError(INVALID_LINE_ERROR(LINE_KEY));
    }

  auto res = GetSnapshotAndStartLine(filePath, fileLine, diff);
  if (res.second < 0)
    {
    return request.Reply(NO_CONTEXT_VALUE);
    }

  auto desired =
      GetDesiredSnapshot(diff.EditorLines, res.second, res.first, fileLine, true);
  cmState::Snapshot completionSnp = desired.first;
  if (!completionSnp.IsValid())
    {
    return request.Reply(NO_CONTEXT_VALUE);
    }

  auto prParseStart = diff.EditorLines.begin() + res.second - 1;
  auto prParseEnd = diff.EditorLines.begin() + fileLine - 1;
  auto newToParse = std::distance(prParseStart, prParseEnd) + 1;

  auto theLine = *prParseEnd;

  std::string completionPrefix;

  auto columnData = theLine.substr(0, fileColumn);

  auto strt = columnData.find_first_not_of(' ');
  if (strt != std::string::npos)
    {
    completionPrefix = columnData.substr(strt);
    }

  cmServerCompleter completer(this->CMakeInstance, completionSnp, true);

  auto result = completer.Complete(completionSnp,
                       desired.second, completionPrefix,
                       newToParse, fileColumn);

  if (!result.isMember(CONTEXT_ORIGIN_KEY))
    {
    return request.Reply(NO_CONTEXT_VALUE);
    }

  if (!result[CONTEXT_ORIGIN_KEY].isMember(MATCHER_KEY))
    {
    return request.Reply(NO_CONTEXT_VALUE);
    }

  auto varName = result[CONTEXT_ORIGIN_KEY][MATCHER_KEY].asString();

  auto snps = this->CMakeInstance->GetState()->GetWriters(completionSnp, varName);

  if (snps.empty())
    {
    return request.Reply(NO_CONTEXT_VALUE);
    }

  cmState::Snapshot snp = snps.front();

  cmListFileContext lfc;
  lfc.FilePath = snp.GetExecutionListFile();
  lfc.Line = snp.GetStartingPoint();
  auto it = this->Snapshots.lower_bound(lfc);

  if (it == this->Snapshots.end())
    {
    return request.Reply(NO_CONTEXT_VALUE);
    }

  if (it->second.empty())
    {
    return request.Reply(NO_CONTEXT_VALUE);
    }

  ++it;

  Json::Value obj = Json::objectValue;
  obj[DEFINITION_MATCH_KEY] = varName;
  obj[DEFINITION_ORIGIN_KEY] = (int)it->first.Line - 1;
  return request.Reply(obj);
}

std::pair<cmState::Snapshot, long>
cmServerProtocol0_1::GetSnapshotAndStartLine(std::string filePath,
                                             long fileLine,
                                             DifferentialFileContent diff)
{
  assert(fileLine > 0);

  const auto& chunks = diff.Chunks;

  auto it = std::lower_bound(chunks.begin(), chunks.end(), fileLine,
                   [](Chunk const& lhs, long rhs) {
    return lhs.NewStart < rhs;
  });
  if (it == chunks.end() || it->NewStart != fileLine)
    {
    --it;
    }
  auto theLine = it->NewStart;
  while (it->NumCommon + it->NumAdded == 0)
    {
    ++it;
    }
  assert(theLine == it->NewStart);

  // it is the chunk which contains the line request.

  auto searchStart = 0;
  bool isNotCommon = it->NumAdded != 0 || it->NumRemoved != 0;
  if (isNotCommon)
    {
    if (it != chunks.begin())
      {
      auto it2 = it;
      --it2;
      searchStart = it2->OrigStart + it2->NumCommon;
      }
    else
      {
      searchStart = 1;
      }
    }
  else
    {
    auto newLinesDistance = fileLine - it->NewStart;
    searchStart = it->OrigStart + newLinesDistance;
    }

  auto ctx = this->GetSnapshotContext(filePath, searchStart);

  auto mostRecentSnapshotLine = ctx.second;

  // We might still have commands we can't execute in the dirty set. We
  // will skip over them.

  auto itToExecFrom = std::lower_bound(chunks.begin(), chunks.end(),
                                       mostRecentSnapshotLine,
                   [](Chunk const& lhs, long rhs) {
    return lhs.OrigStart < rhs;
  });
  if (itToExecFrom == chunks.end() || itToExecFrom->OrigStart != mostRecentSnapshotLine)
    {
    --itToExecFrom;
    }
  isNotCommon = itToExecFrom->NumAdded != 0 || itToExecFrom->NumRemoved != 0;
  long startFrom = 0;
  if (isNotCommon)
    {
    startFrom = -1;
    }
  else
    {
    auto oldLinesDistance = mostRecentSnapshotLine - itToExecFrom->OrigStart;

    startFrom = itToExecFrom->NewStart + oldLinesDistance;
    }

  return std::make_pair(ctx.first, startFrom);
}

std::pair<cmState::Snapshot, cmListFileFunction>
cmServerProtocol0_1::GetDesiredSnapshot(
    std::vector<std::string> const& editorLines, long startLine,
    cmState::Snapshot snp, long fileLine, bool completionMode)
{
  auto prParseStart = editorLines.begin() + startLine - 1;
  assert((long)editorLines.size() >= fileLine);
  auto prParseEnd = editorLines.begin() + fileLine - 1;
  if (completionMode)
    {
    ++prParseEnd;
    }

  auto newString = cmJoin(cmMakeRange(prParseStart, prParseEnd), "\n");

  cmGlobalGenerator* gg = this->CMakeInstance->GetGlobalGenerator();

  cmMakefile mf(gg, snp);

  cmListFile listFile;

  if (!listFile.ParseString(
        newString.c_str(),
        snp.GetExecutionListFile().c_str(),
        &mf))
    {
    std::cout << "STRING PARSE ERROR" << std::endl;
    return std::make_pair(cmState::Snapshot(), cmListFileFunction());
    }

  auto newToParse = fileLine - startLine + 1;
  assert(newToParse >= 0);
  return mf.ReadCommands(listFile.Functions, newToParse);
}

std::pair<cmState::Snapshot, long>
cmServerProtocol0_1::GetSnapshotContext(std::string filePath, long fileLine)
{
  cmListFileContext lfc;
  lfc.FilePath = filePath;
  lfc.Line = fileLine;

  auto it = this->Snapshots.lower_bound(lfc);

  // Do some checks before any of this? ie, if the prev, then popped is
  // macro or function, then do it, otherwise don't?
  // Also some logic to know whether to go prev?

  if (it != this->Snapshots.end())
    {
    cmState::Snapshot snp = it->second.back();

    if (snp.GetExecutionListFile() == filePath &&
        snp.GetStartingPoint() == fileLine)
      {
      return std::make_pair(this->CMakeInstance->GetState()->PopArbitrary(snp), fileLine);
      }
    }

  assert(it != this->Snapshots.begin());

  --it;
  cmState::Snapshot snp = it->second.back();

  {
  // Do this conditionally, depending on whether we need the state from inside
  // a macro or included file.
  cmListFileContext lfc2;
  lfc2.FilePath = it->second.front().GetExecutionListFile();
  lfc2.Line = it->second.front().GetStartingPoint();
//  if (lfc2 == it->first)
    {
    snp = this->CMakeInstance->GetState()->PopArbitrary(snp);
    }
  }

  long startingPoint = it->first.Line;
  return std::make_pair(snp, startingPoint);
}

bool cmServerProtocol0_1::IsNotExecuted(std::string filePath, long fileLine)
{
  auto nx = this->CMakeInstance->GetState()->GetNotExecuted(filePath);
  for (auto it = nx.begin(); it != nx.end(); ++it)
    {
    if (fileLine >= it->first && fileLine < it->second)
      {
      return true;
      }
    }
  return false;
}

Json::Value cmServerProtocol0_1::GenerateContent(cmState::Snapshot snp, std::string matcher)
{
  Json::Value obj = Json::objectValue;
  std::vector<std::string> keys = snp.ClosureKeys();
  for (const auto& p: keys)
    {
    if (p.find(matcher) == 0)
      obj[p] = snp.GetDefinition(p);
    }

  return obj;
}

Json::Value cmServerProtocol0_1::GenerateContextualHelp(const std::string &context,
                                                        const std::string &help_key)
{
  std::string pdir = cmSystemTools::GetCMakeRoot();
  pdir += "/Help/" + context + "/";

  std::string relevant = cmSystemTools::HelpFileName(help_key);
  std::string helpFile = pdir + relevant + ".rst";
  if(!cmSystemTools::FileExists(helpFile.c_str(), true))
    {
    return Json::nullValue;
    }
  Json::Value obj = Json::objectValue;

  Json::Value& contextualHelp =
      obj[CONTEXTUAL_HELP_KEY] = Json::objectValue;

  contextualHelp[CONTEXT_KEY] = context;
  contextualHelp[HELPKEY_KEY] = relevant;

  return obj;
}

Json::Value cmServerProtocol0_1::EmitTypedIdentifier(const std::string &commandName,
                                                     std::vector<cmListFileArgument> args,
                                                     size_t argIndex)
{
  cmCommand* proto =
      this->CMakeInstance->GetState()->GetCommand(commandName);
  if(!proto)
    {
    return Json::nullValue;
    }

  std::vector<std::string> argStrings;
  for (auto arg: args)
    {
    argStrings.push_back(arg.Value);
    }

  auto contextType = proto->GetContextForParameter(argStrings, argIndex);

  auto value = args[argIndex].Value;

  std::string context;
  switch (contextType)
    {
  case cmCommand::TargetPropertyParameter:
    context = "propertyTarget";
    break;
  case cmCommand::DirectoryPropertyParameter:
    context = "propertyDirectory";
    break;
  case cmCommand::VariableIdentifierParameter:
    context = "variable";
    break;
  case cmCommand::PolicyParameter:
    context = "policy";
    break;
  case cmCommand::ModuleNameParameter:
    context = "module";
    break;
  case cmCommand::PackageNameParameter:
    context = "module";
    value = "Find" + value;
    break;
  default:
    break;
    }

  if (context.empty())
    {
    return false;
    }

  return this->GenerateContextualHelp(context, value);
}

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

// #include "cmState.h"
#include "cmListFileCache.h"

#if defined(CMAKE_BUILD_WITH_CMAKE)
# include "cm_jsoncpp_writer.h"
#endif

class cmake;
class cmMetadataServer;

struct DifferentialFileContent;

class cmServerRequest {
public:
  cmServerRequest(const std::string &t, const std::string &c, const Json::Value &d);

  const std::string Type;
  const std::string Cookie;
  const Json::Value Data;
};

class cmServerResponse {
public:
  explicit cmServerResponse(const cmServerRequest &request);
  static cmServerResponse errorResponse(const cmServerRequest &request, const std::string &message);
  static cmServerResponse dataResponse(const cmServerRequest &request, const Json::Value &data);

  void setData(const Json::Value &data);
  void setError(const std::string &message);

  bool IsComplete() const;
  bool IsError() const;
  std::string ErrorMessage() const;
  Json::Value Data() const;

  const std::string Type;
  const std::string Cookie;

private:
  enum PayLoad { UNKNOWN, ERROR, DATA };
  PayLoad mPayload = UNKNOWN;
  std::string mErrorMessage;
  Json::Value mData;
};

class cmServerProtocol
{
public:
  virtual ~cmServerProtocol();

  virtual std::pair<int, int> protocolVersion() const = 0;
  virtual const cmServerResponse process(const cmServerRequest &request) = 0;
};

struct OrderFileThenLine
{
  bool operator()(cmListFileContext const& l,
                  cmListFileContext const& r) const
  {
    std::pair<std::string, long> lhs(l.FilePath, l.Line);
    std::pair<std::string, long> rhs(r.FilePath, r.Line);
    const bool res = lhs < rhs;
    return res;
  }
};

class cmServerProtocol0_1 : public cmServerProtocol
{
public:
  cmServerProtocol0_1(cmMetadataServer* server, std::string buildDir);
  ~cmServerProtocol0_1() override;

  std::pair<int, int> protocolVersion() const override;
  const cmServerResponse process(const cmServerRequest &request) override;

private:
  cmServerResponse ProcessHandshake(const cmServerRequest &request);
  cmServerResponse ProcessVersion(const cmServerRequest &request);
  cmServerResponse ProcessBuildSystem(const cmServerRequest &request);
  cmServerResponse ProcessTargetInfo(const cmServerRequest &request);
  cmServerResponse ProcessFileInfo(const cmServerRequest &request);
  cmServerResponse ProcessContent(const cmServerRequest &request);
  cmServerResponse ProcessParse(const cmServerRequest &request);
  cmServerResponse ProcessContextualHelp(const cmServerRequest &request);

  std::pair<cmState::Snapshot, long>
  GetSnapshotAndStartLine(std::string filePath,
                          long fileLine,
                          DifferentialFileContent diff);
  std::pair<cmState::Snapshot, cmListFileFunction>
  GetDesiredSnapshot(std::vector<std::string> const& editorLines,
                     long startLine, cmState::Snapshot snp,
                     long fileLine, bool completionMode = false);
  std::pair<cmState::Snapshot, long>
  GetSnapshotContext(std::string filePath, long fileLine);

  bool IsNotExecuted(std::string filePath, long fileLine);
  Json::Value EmitTypedIdentifier(std::string const& commandName,
                                  std::vector<cmListFileArgument> args,
                                  size_t argIndex);
  Json::Value GenerateContent(cmState::Snapshot snp, std::string matcher);

  Json::Value GenerateContextualHelp(std::string const& context,
                      std::string const& help_key);
#if 0
  void processRequest(const std::string& json);

private:
  void ProcessHandshake(const std::string& protocolVersion);
  void ProcessVersion();
  void ProcessBuildsystem();
  void ProcessTargetInfo(std::string tgtName,
                         std::string config,
                         const char* language);
  void ProcessFileInfo(std::string tgtName,
                       std::string config,
                       std::string file_path);
  void ProcessContent(std::string filePath, long fileLine,
                      DifferentialFileContent diff, std::string matcher);
  void ProcessParse(std::string file_path, DifferentialFileContent diff);
  void ProcessContextualHelp(std::string filePath,
                             long fileLine, long fileColumn,
                             std::string fileContent);
  void ProcessContentDiff(std::string filePath1, long fileLine1,
                          std::string filePath2, long fileLine2,
                          std::pair<DifferentialFileContent,
                                    DifferentialFileContent> diffs);
  void ProcessCodeComplete(std::string filePath,
                           long fileLine, long fileColumn,
                           DifferentialFileContent diff);
  void ProcessContextWriters(std::string filePath,
                           long fileLine, long fileColumn,
                           DifferentialFileContent diff);

private:



  void writeContent(cmState::Snapshot snp, std::string matcher);

#endif

private:
  cmMetadataServer* Server;
  cmake *CMakeInstance;
  std::string m_buildDir;
  std::map<cmListFileContext, std::vector<cmState::Snapshot>, OrderFileThenLine> Snapshots;
};

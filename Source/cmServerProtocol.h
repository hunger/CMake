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

class DifferentialFileContent;

class cmServerRequest {
public:
  void ReportProgress(int min, int current, int max, const std::string &message) const;

  const std::string Type;
  const std::string Cookie;
  const Json::Value Data;

private:
  cmServerRequest(cmMetadataServer* server, const std::string &t,
                  const std::string &c, const Json::Value &d);

  cmMetadataServer* Server;

  friend class cmMetadataServer;
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
  cmServerProtocol0_1();
  ~cmServerProtocol0_1() override;

  std::pair<int, int> protocolVersion() const override;
  const cmServerResponse process(const cmServerRequest &request) override;

private:
  cmServerResponse ProcessInitialize(const cmServerRequest &request);
  cmServerResponse ProcessVersion(const cmServerRequest &request);
  cmServerResponse ProcessBuildSystem(const cmServerRequest &request);
  cmServerResponse ProcessTargetInfo(const cmServerRequest &request);
  cmServerResponse ProcessFileInfo(const cmServerRequest &request);
  cmServerResponse ProcessContent(const cmServerRequest &request);
  cmServerResponse ProcessParse(const cmServerRequest &request);
  cmServerResponse ProcessContextualHelp(const cmServerRequest &request);
  cmServerResponse ProcessContentDiff(const cmServerRequest &request);
  cmServerResponse ProcessCodeComplete(const cmServerRequest &request);
  cmServerResponse ProcessContextWriters(const cmServerRequest &request);

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

  cmake *CMakeInstance;
  std::map<cmListFileContext, std::vector<cmState::Snapshot>, OrderFileThenLine> Snapshots;
};

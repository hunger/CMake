/*============================================================================
  CMake - Cross Platform Makefile Generator
  Copyright 2000-2009 Kitware, Inc., Insight Software Consortium

  Distributed under the OSI-approved BSD License (the "License");
  see accompanying file Copyright.txt for details.

  This software is distributed WITHOUT ANY WARRANTY; without even the
  implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
  See the License for more information.
============================================================================*/

#ifndef cmMessenger_h
#define cmMessenger_h

#include "cmake.h"
#include "cmListFileCache.h"
#include "cmState.h"

class cmMessenger
{
public:
  cmMessenger(cmState* state);
  void IssueMessage(cmake::MessageType t, std::string const& text,
        cmListFileBacktrace const& backtrace = cmListFileBacktrace(),
        bool force = false) const;
  void IssueMessage(cmake::MessageType t, std::string const& text,
        cmListFileContext const& lfc,
        bool force = false) const;
  void IssueMessage(cmake::MessageType t, std::string const& text,
        cmState::Snapshot snp,
        bool force = false) const;

  bool GetSuppressDevWarnings() const;
  bool GetSuppressDeprecatedWarnings() const;
  bool GetDevWarningsAsErrors() const;
  bool GetDeprecatedWarningsAsErrors() const;

private:
  bool IsMessageTypeVisible(cmake::MessageType t) const;
  cmake::MessageType ConvertMessageType(cmake::MessageType t) const;

  cmState* State;
};

#endif

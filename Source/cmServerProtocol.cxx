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

#include "cmServer.h"
#include "cmake.h"

#if defined(CMAKE_BUILD_WITH_CMAKE)
#include "cm_jsoncpp_reader.h"
#include "cm_jsoncpp_value.h"
#endif

namespace {
// Vocabulary:

char COOKIE_KEY[] = "cookie";
char TYPE_KEY[] = "type";

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
  return request.ReportError("Unknown command!");
}

bool cmServerProtocol0_1::IsExperimental() const
{
  return true;
}

/*============================================================================
  CMake - Cross Platform Makefile Generator
  Copyright 2015 Stephen Kelly <steveire@gmail.com>
  Copyright 2016 Tobias Hunger <tobias.hunger@qt.io>

  Distributed under the OSI-approved BSD License (the "License");
  see accompanying file Copyright.txt for details.

  This software is distributed WITHOUT ANY WARRANTY; without even the
  implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
  See the License for more information.
============================================================================*/

#include "cmServer.h"

#include "cmServerProtocol.h"
#include "cmVersionMacros.h"
#include "cmake.h"

#if defined(CMAKE_BUILD_WITH_CMAKE)
#include "cm_jsoncpp_reader.h"
#include "cm_jsoncpp_value.h"
#endif

const char TYPE_KEY[] = "type";
const char COOKIE_KEY[] = "cookie";
const char REPLY_TO_KEY[] = "inReplyTo";
const char ERROR_MESSAGE_KEY[] = "errorMessage";

const char ERROR_TYPE[] = "error";
const char REPLY_TYPE[] = "reply";
const char PROGRESS_TYPE[] = "progress";

typedef struct
{
  uv_write_t req;
  uv_buf_t buf;
} write_req_t;

void alloc_buffer(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf)
{
  (void)handle;
  *buf = uv_buf_init((char*)malloc(suggested_size), suggested_size);
}

void free_write_req(uv_write_t* req)
{
  write_req_t* wr = (write_req_t*)req;
  free(wr->buf.base);
  free(wr);
}

void on_stdout_write(uv_write_t* req, int status)
{
  (void)status;
  auto server = reinterpret_cast<cmServer*>(req->data);
  free_write_req(req);
  server->PopOne();
}

void write_data(uv_stream_t* dest, std::string content, uv_write_cb cb)
{
  write_req_t* req = (write_req_t*)malloc(sizeof(write_req_t));
  req->req.data = dest->data;
  req->buf = uv_buf_init((char*)malloc(content.size()), content.size());
  memcpy(req->buf.base, content.c_str(), content.size());
  uv_write((uv_write_t*)req, (uv_stream_t*)dest, &req->buf, 1, cb);
}

void read_stdin(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf)
{
  if (nread > 0) {
    auto server = reinterpret_cast<cmServer*>(stream->data);
    std::string result = std::string(buf->base, buf->base + nread);
    server->handleData(result);
  }

  if (buf->base)
    free(buf->base);
}

cmServer::cmServer()
{
  m_Loop = uv_default_loop();

  if (uv_guess_handle(1) == UV_TTY) {
    uv_tty_init(m_Loop, &m_StdinTty, 0, 1);
    uv_tty_set_mode(&m_StdinTty, UV_TTY_MODE_NORMAL);
    m_StdinTty.data = this;

    uv_tty_init(m_Loop, &m_StdoutTty, 1, 0);
    uv_tty_set_mode(&m_StdoutTty, UV_TTY_MODE_NORMAL);
    m_StdoutTty.data = this;
  } else {
    uv_pipe_init(m_Loop, &m_StdinPipe, 0);
    uv_pipe_open(&m_StdinPipe, 0);
    m_StdinPipe.data = this;

    uv_pipe_init(m_Loop, &m_StdoutPipe, 0);
    uv_pipe_open(&m_StdoutPipe, 1);
    m_StdoutPipe.data = this;
  }

  this->m_Writing = false;
}

cmServer::~cmServer()
{
  uv_close((uv_handle_t*)&m_StdinPipe, NULL);
  uv_close((uv_handle_t*)&m_StdoutPipe, NULL);
  uv_loop_close(m_Loop);
}

void cmServer::PopOne()
{
  this->m_Writing = false;
  if (m_Queue.empty()) {
    return;
  }

  Json::Reader reader;
  Json::Value value;
  const std::string input = m_Queue.front();
  m_Queue.erase(m_Queue.begin());

  if (!reader.parse(input, value)) {
    WriteParseError("Failed to parse JSON input.");
    return;
  }

  const cmServerRequest request(this, value[TYPE_KEY].asString(),
                                value[COOKIE_KEY].asString(), value);

  if (request.Type == "") {
    cmServerResponse response(request);
    response.SetError("No type given in request.");
    WriteResponse(response);
    return;
  }

  WriteResponse(m_Protocol ? m_Protocol->Process(request)
                           : SetProtocolVersion(request));
}

void cmServer::handleData(const std::string& data)
{
#ifdef _WIN32
  const char LINE_SEP[] = "\r\n";
#else
  const char LINE_SEP[] = "\n";
#endif

  m_DataBuffer += data;

  for (;;) {
    auto needle = m_DataBuffer.find(LINE_SEP);

    if (needle == std::string::npos) {
      return;
    }
    std::string line = m_DataBuffer.substr(0, needle);
    m_DataBuffer.erase(m_DataBuffer.begin(),
                       m_DataBuffer.begin() + needle + sizeof(LINE_SEP) - 1);
    if (line == "[== CMake MetaMagic ==[") {
      m_JsonData.clear();
      continue;
    }
    if (line == "]== CMake MetaMagic ==]") {
      m_Queue.push_back(m_JsonData);
      m_JsonData.clear();
      if (!this->m_Writing) {
        this->PopOne();
      }
    } else {
      m_JsonData += line;
      m_JsonData += "\n";
    }
  }
}

void cmServer::RegisterProtocol(cmServerProtocol* protocol)
{
  auto version = protocol->ProtocolVersion();
  assert(version.first >= 0);
  assert(version.second >= 0);
  auto it =
    std::find_if(m_SupportedProtocols.begin(), m_SupportedProtocols.end(),
                 [version](cmServerProtocol* p) {
                   return p->ProtocolVersion() == version;
                 });
  if (it == m_SupportedProtocols.end())
    m_SupportedProtocols.push_back(protocol);
}

cmServerResponse cmServer::SetProtocolVersion(const cmServerRequest& request)
{
  if (request.Type != "handshake")
    return request.ReportError("Waiting for type \"handshake\".");

  Json::Value requestedProtocolVersion = request.Data["protocolVersion"];
  if (requestedProtocolVersion.isNull())
    return request.ReportError(
      "\"protocolVersion\" is required for \"handshake\".");

  if (!requestedProtocolVersion.isObject())
    return request.ReportError("\"protocolVersion\" must be a JSON object.");

  Json::Value majorValue = requestedProtocolVersion["major"];
  if (!majorValue.isInt())
    return request.ReportError("\"major\" must be set and an integer.");

  Json::Value minorValue = requestedProtocolVersion["minor"];
  if (!minorValue.isNull() && !minorValue.isInt())
    return request.ReportError("\"minor\" must be unset or an integer.");

  const int major = majorValue.asInt();
  const int minor = minorValue.isNull() ? -1 : minorValue.asInt();
  if (major < 0)
    return request.ReportError("\"major\" must be >= 0.");
  if (!minorValue.isNull() && minor < 0)
    return request.ReportError("\"minor\" must be >= 0 when set.");

  m_Protocol = FindMatchingProtocol(m_SupportedProtocols, major, minor);
  if (m_Protocol) {
    m_Protocol->Activate();
    return request.Reply(Json::objectValue);
  } else {
    return request.ReportError("Protocol version not supported.");
  }
}

void cmServer::Serve()
{
  // Register supported protocols:
  RegisterProtocol(new cmServerProtocol0_1);

  assert(!m_SupportedProtocols.empty());
  assert(!m_Protocol);

  Json::Value hello = Json::objectValue;
  hello[TYPE_KEY] = "hello";

  Json::Value& protocolVersions = hello["supportedProtocolVersions"] =
    Json::arrayValue;

  for (auto const& proto : m_SupportedProtocols) {
    auto version = proto->ProtocolVersion();
    Json::Value tmp = Json::objectValue;
    tmp["major"] = version.first;
    tmp["minor"] = version.second;
    protocolVersions.append(tmp);
  }

  WriteJsonObject(hello);

  if (uv_guess_handle(1) == UV_TTY) {
    uv_read_start((uv_stream_t*)&m_StdinTty, alloc_buffer, read_stdin);
  } else {
    uv_read_start((uv_stream_t*)&m_StdinPipe, alloc_buffer, read_stdin);
  }

  uv_run(m_Loop, UV_RUN_DEFAULT);
}

void cmServer::WriteJsonObject(const Json::Value& jsonValue)
{
  Json::FastWriter writer;

  std::string result = "\n[== CMake MetaMagic ==[\n";
  result += writer.write(jsonValue);
  result += "]== CMake MetaMagic ==]\n";

  this->m_Writing = true;
  if (uv_guess_handle(1) == UV_TTY) {
    write_data((uv_stream_t*)&m_StdoutTty, result, on_stdout_write);
  } else {
    write_data((uv_stream_t*)&m_StdoutPipe, result, on_stdout_write);
  }
}

cmServerProtocol* cmServer::FindMatchingProtocol(
  const std::vector<cmServerProtocol*>& protocols, int major, int minor)
{
  cmServerProtocol* bestMatch = nullptr;
  for (auto protocol : protocols) {
    auto version = protocol->ProtocolVersion();
    if (major != version.first)
      continue;
    if (minor == version.second)
      return protocol;
    if (!bestMatch || bestMatch->ProtocolVersion().second < version.second)
      bestMatch = protocol;
  }
  return minor < 0 ? bestMatch : nullptr;
}

void cmServer::WriteProgress(const cmServerRequest& request, int min,
                             int current, int max, const std::string& message)
{
  assert(min <= current && current <= max);
  assert(message.length() != 0);

  Json::Value obj = Json::objectValue;
  obj[TYPE_KEY] = PROGRESS_TYPE;
  obj[REPLY_TO_KEY] = request.Type;
  obj[COOKIE_KEY] = request.Cookie;
  obj["progressMessage"] = message;
  obj["progressMinimum"] = min;
  obj["progressMaximum"] = max;
  obj["progressCurrent"] = current;

  WriteJsonObject(obj);
}

void cmServer::WriteParseError(const std::string& message)
{
  Json::Value obj = Json::objectValue;
  obj[TYPE_KEY] = ERROR_TYPE;
  obj[ERROR_MESSAGE_KEY] = message;
  obj[REPLY_TO_KEY] = "";
  obj[COOKIE_KEY] = "";

  WriteJsonObject(obj);
}

void cmServer::WriteResponse(const cmServerResponse& response)
{
  assert(response.IsComplete());

  Json::Value obj = response.Data();
  obj[COOKIE_KEY] = response.Cookie;
  obj[TYPE_KEY] = response.IsError() ? ERROR_TYPE : REPLY_TYPE;
  obj[REPLY_TO_KEY] = response.Type;
  if (response.IsError()) {
    obj[ERROR_MESSAGE_KEY] = response.ErrorMessage();
  }

  WriteJsonObject(obj);
}

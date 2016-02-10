/*============================================================================
  CMake - Cross Platform Makefile Generator
  Copyright 2015 Stephen Kelly <steveire@gmail.com>

  Distributed under the OSI-approved BSD License (the "License");
  see accompanying file Copyright.txt for details.

  This software is distributed WITHOUT ANY WARRANTY; without even the
  implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
  See the License for more information.
============================================================================*/

#include "cmServer.h"

#include "cmServerProtocol.h"

#include "cmVersionMacros.h"

#if defined(CMAKE_BUILD_WITH_CMAKE)
# include "cm_jsoncpp_value.h"
# include "cm_jsoncpp_reader.h"
#endif

typedef struct {
  uv_write_t req;
  uv_buf_t buf;
} write_req_t;

void alloc_buffer(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf) {
  (void)handle;
  *buf = uv_buf_init((char*) malloc(suggested_size), suggested_size);
}

void free_write_req(uv_write_t *req) {
  write_req_t *wr = (write_req_t*) req;
  free(wr->buf.base);
  free(wr);
}

void on_stdout_write(uv_write_t *req, int status) {
  (void)status;
  auto server = reinterpret_cast<cmMetadataServer*>(req->data);
  free_write_req(req);
  server->PopOne();
}

void write_data(uv_stream_t *dest, std::string content, uv_write_cb cb) {
  write_req_t *req = (write_req_t*) malloc(sizeof(write_req_t));
  req->req.data = dest->data;
  req->buf = uv_buf_init((char*) malloc(content.size()), content.size());
  memcpy(req->buf.base, content.c_str(), content.size());
  uv_write((uv_write_t*) req, (uv_stream_t*)dest, &req->buf, 1, cb);
}

void read_stdin(uv_stream_t *stream, ssize_t nread, const uv_buf_t *buf) {
  if (nread > 0) {
    auto server = reinterpret_cast<cmMetadataServer*>(stream->data);
    std::string result = std::string(buf->base, buf->base + nread);
    server->handleData(result);
  }

  if (buf->base)
    free(buf->base);
}

cmMetadataServer::cmMetadataServer()
  : Protocol(0)
{
  mLoop = uv_default_loop();

  uv_pipe_init(mLoop, &mStdin_pipe, 0);
  uv_pipe_open(&mStdin_pipe, 0);
  mStdin_pipe.data = this;

  uv_pipe_init(mLoop, &mStdout_pipe, 0);
  uv_pipe_open(&mStdout_pipe, 1);
  mStdout_pipe.data = this;

  this->Writing = false;
}

cmMetadataServer::~cmMetadataServer() {
  uv_close((uv_handle_t *)&mStdin_pipe, NULL);
  uv_close((uv_handle_t *)&mStdout_pipe, NULL);
  uv_loop_close(mLoop);
  delete Protocol;
}

void cmMetadataServer::PopOne()
{
  this->Writing = false;
  if (mQueue.empty())
    {
    return;
    }

  Json::Reader reader;
  Json::Value value;
  const std::string input = mQueue.front();
  mQueue.erase(mQueue.begin());

  if (!reader.parse(input, value))
    {
    WriteParseError("Failed to parse JSON input.");
    return;
    }

  const cmServerRequest request(this, value["type"].asString(), value["cookie"].asString(), value);

  if (request.Type == "")
    {
    cmServerResponse response(request);
    response.setError("No type given in request.");
    WriteResponse(response);
    return;
    }

  WriteResponse(Protocol ? Protocol->process(request) : SetProtocolVersion(request));
}

void cmMetadataServer::handleData(const std::string &data)
{
  mDataBuffer += data;

  for ( ; ; )
    {
    auto needle = mDataBuffer.find('\n');

    if (needle == std::string::npos)
      {
      return;
      }
    std::string line = mDataBuffer.substr(0, needle);
    mDataBuffer.erase(mDataBuffer.begin(), mDataBuffer.begin() + needle + 1);
    if (line == "[== CMake MetaMagic ==[")
      {
      mJsonData.clear();
      continue;
      }
    mJsonData += line;
    mJsonData += "\n";
    if (line == "]== CMake MetaMagic ==]")
      {
      mQueue.push_back(mJsonData);
      mJsonData.clear();
      if (!this->Writing)
        {
        this->PopOne();
        }
      }
  }
}

cmServerResponse cmMetadataServer::SetProtocolVersion(const cmServerRequest& request)
{
  if (request.Type != "handshake")
    return cmServerResponse::errorResponse(request, "Waiting for type \"handshake\".");

  Json::Value requestedProtocolVersion = request.Data["protocolVersion"];
  if (requestedProtocolVersion.isNull())
    return cmServerResponse::errorResponse(request, "\"protocolVersion\" is required for \"handshake\".");

  Json::Value majorValue = requestedProtocolVersion["major"];
  if (!majorValue.isInt())
    return cmServerResponse::errorResponse(request, "\"major\" must be an integer.");

  Json::Value minorValue = requestedProtocolVersion["minor"];
  if (!minorValue.isNull() && !minorValue.isInt())
    return cmServerResponse::errorResponse(request, "\"minor\" must be unset or an integer.");

  const int major = majorValue.asInt();
  const int minor = minorValue.isNull() ? -1 : minorValue.asInt();
  if (major < 0)
    return cmServerResponse::errorResponse(request, "\"major\" must be >= 0.");
  if (!minorValue.isNull() && minor < 0)
    return cmServerResponse::errorResponse(request, "\"minor\" must be >= 0.");

  Protocol = findMatchingProtocol(major, minor);
  if (Protocol)
    return cmServerResponse::dataResponse(request, Json::objectValue);
  else
    return cmServerResponse::errorResponse(request, "Protocol version not supported.");
}

void cmMetadataServer::ServeMetadata()
{
  // Register supported protocols:
  SupportedProtocols.push_back(new cmServerProtocol0_1());

  assert(!SupportedProtocols.empty());
  assert(!Protocol);

  Json::Value hello = Json::objectValue;
  hello["type"] = "hello";

  Json::Value& protocolVersions = hello["supportedProtocolVersions"] = Json::arrayValue;

  for (auto const &proto : SupportedProtocols)
    {
    auto version = proto->protocolVersion();
    Json::Value tmp = Json::objectValue;
    tmp["major"] = version.first;
    tmp["minor"] = version.second;
    protocolVersions.append(tmp);
    }

  WriteJsonObject(hello);

  uv_read_start((uv_stream_t*)&mStdin_pipe, alloc_buffer, read_stdin);

  uv_run(mLoop, UV_RUN_DEFAULT);
}

void cmMetadataServer::WriteJsonObject(const Json::Value& jsonValue)
{
  Json::FastWriter writer;

  std::string result = "\n[== CMake MetaMagic ==[\n";
  result += writer.write(jsonValue);
  result += "]== CMake MetaMagic ==]\n";

  this->Writing = true;
  write_data((uv_stream_t *)&mStdout_pipe, result, on_stdout_write);
}

cmServerProtocol* cmMetadataServer::findMatchingProtocol(int major, int minor) const
{
  for (auto protocol : SupportedProtocols)
    {
    auto version = protocol->protocolVersion();
    if (major == version.first && (minor < 0 || minor == version.second))
      return protocol;
    }
  return 0;
}

void cmMetadataServer::WriteProgress(const cmServerRequest &request,
                                     int min, int current, int max, const std::string& message)
{
  assert(min <= current && current <= max);
  assert(message.length() != 0);

  Json::Value obj = Json::objectValue;
  obj["type"] = "progress";
  obj["inResponseTo"] = request.Type;
  obj["cookie"] = request.Cookie;
  obj["progressMessage"] = message;
  obj["progressMinimum"] = min;
  obj["progressMaximum"] = max;
  obj["progressCurrent"] = current;

  WriteJsonObject(obj);
}

void cmMetadataServer::WriteParseError(const std::string &message)
{
  Json::Value obj = Json::objectValue;
  obj["type"] = "error";
  obj["errorMessage"] = message;
  obj["inResponseTo"] = "";
  obj["cookie"] = "";

  WriteJsonObject(obj);
}

void cmMetadataServer::WriteProgress(const std::string &progress)
{
  Json::Value obj = Json::objectValue;
  obj["type"] = "progress";
  obj["progress"] = progress;
  WriteJsonObject(obj);
}

void cmMetadataServer::WriteResponse(const cmServerResponse &response)
{
  assert(response.IsComplete());

  Json::Value result = response.Data();
  result["cookie"] = response.Cookie;
  result["type"] = response.IsError() ? "error" : "reply";
  result["inResponseTo"] = response.Type;
  if (response.IsError())
    {
    result["errorMessage"] = response.ErrorMessage();
    }

  WriteJsonObject(result);
}

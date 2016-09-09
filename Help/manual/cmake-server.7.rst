.. cmake-manual-description: CMake Server

cmake-server(7)
***************

.. only:: html

   .. contents::

Introduction
============

:manual:`cmake(1)` is capable of providing semantic information about
CMake code it executes to generate a buildsystem.  If executed with
the ``-E server`` command line options, it starts in a long running mode
and allows a client to request the available information via a JSON protocol.

The protocol is designed to be useful to IDEs, refactoring tools, and
other tools which have a need to understand the buildsystem in entirity.

A single :manual:`cmake-buildsystem(7)` may describe buildsystem contents
and build properties which differ based on
:manual:`generation-time context <cmake-generator-expressions(7)>`
including:

* The Platform (eg, Windows, APPLE, Linux).
* The build configuration (eg, Debug, Release, Coverage).
* The Compiler (eg, MSVC, GCC, Clang) and compiler version.
* The language of the source files compiled.
* Available compile features (eg CXX variadic templates).
* CMake policies.

The protocol aims to provide information to tooling to satisfy several
needs:

#. Provide a complete and easily parsed source of all information relevant
   to the tooling as it relates to the source code.  There should be no need
   for tooling to parse generated buildsystems to access include directories
   or compile defintions for example.
#. Semantic information about the CMake buildsystem itself.
#. Provide a stable interface for reading the information in the CMake cache.
#. Information for determining when cmake needs to be re-run as a result of
   file changes.


Operation
=========

Start :manual:`cmake(1)` in the server command mode, supplying the path to
the build directory to process::

  cmake -E server

The server will start up and reply with an hello message on stdout::
  [== CMake Server ==[
  {"supportedProtocolVersions":[{"major":0,"minor":1}],"type":"hello"}
  ]== CMake Server ==]

Messages sent to and from the process are wrapped in magic strings::

  [== CMake Server ==[
  {
    ... some JSON message ...
  }
  ]== CMake Server ==]

The server is now ready to accept further requests via stdin.


Protocol API
============


General Message Layout
----------------------

All messages need to have a "type" value, which identifies the type of
message that is passed back or forth. E.g. the initial message sent by the
server is of type "hello". Messages without a type will generate an response
of type "error".

All requests sent to the server may contain a "cookie" value. This value
will he handed back unchanged in all responses triggered by the request.

All responses will contain a value "inReplyTo", which may be empty in
case of parse errors, but will contain the type of the request message
in all other cases.


Type "reply"
^^^^^^^^^^^^

This type is used by the server to reply to requests.

The message may -- depending on the type of the original request --
contain values.

Example::
  [== CMake Server ==[
  {"cookie":"zimtstern","inReplyTo":"handshake","type":"reply"}
  ]== CMake Server ==]


Type "error"
^^^^^^^^^^^^

This type is used to return an error condition to the client. It will
contain an "errorMessage".

Example::
  [== CMake Server ==[
  {"cookie":"","errorMessage":"Protocol version not supported.","inReplyTo":"handshake","type":"error"}
  ]== CMake Server ==]


Type "progress"
^^^^^^^^^^^^^^^

When the server is busy for a long time, it is polite to send back replies of
type "progress" to the client. These will contain a "progressMessage" with a
string describing the action currently taking place as well as
"progressMinimum", "progressMaximum" and "progressCurrent" with integer values
describing the range of progess.

Messages of type "progress" will be followed by more "progress" messages or with
a message of type "reply" or "error" that complete the request.

"progress" messages may not be emitted after the "reply" or "error" message for
the request that triggered the responses was delivered.


Type "message"
^^^^^^^^^^^^^^

A message is triggered when the server processes a request and produces some
form of output that should be displayed to the user. A Message has a "message"
with the actual text to display as well as a "title" with a suggested dialog
box title.

Example::
  [== CMake Server ==[
  {"cookie":"","message":"Something happened.","title":"Title Text","inReplyTo":"handshake","type":"message"}
  ]== CMake Server ==]


Specific Message Types
----------------------


Type "hello"
^^^^^^^^^^^^

The initial message send by the cmake server on startup is of type "hello".
This is the only message ever sent by the server that is not of type "reply",
"progress" or "error".

It will contain "supportedProtocolVersions" with an array of server protocol
versions supported by the cmake server. These are JSON objects with "major" and
"minor" keys containing non-negative integer values.

Example::
  [== CMake Server ==[
  {"supportedProtocolVersions":[{"major":0,"minor":1}],"type":"hello"}
  ]== CMake Server ==]


Type "handshake"
^^^^^^^^^^^^^^^^

The first request that the client may send to the server is of type "handshake".

This request needs to pass one of the "supportedProtocolVersions" of the "hello"
type response received earlier back to the server in the "protocolVersion" field.

Each protocol version may request additional attributes to be present.

Protocol version 1.0 requires the following attributes to be set:

  * "sourceDirectory" with a path to the sources
  * "buildDirectory" with a path to the build directory
  * "generator" with the generator name
  * "extraGenerator" (optional!) with the extra generator to be used.

Example::
  [== CMake Server ==[
  {"cookie":"zimtstern","type":"handshake","protocolVersion":{"major":0},
   "sourceDirectory":"/home/code/cmake", "buildDirectory":"/tmp/testbuild",
   "generator":"Ninja"}
  ]== CMake Server ==]

which will result in a response type "reply"::
  [== CMake Server ==[
  {"cookie":"zimtstern","inReplyTo":"handshake","type":"reply"}
  ]== CMake Server ==]

indicating that the server is ready for action.

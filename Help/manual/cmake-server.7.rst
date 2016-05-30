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
---------

Start :manual:`cmake(1)` in the server command mode, supplying the path to
the build directory to process::

  cmake -E server

The server will start up and reply with an hello message on stdout::
  [== CMake MetaMagic ==[
  {"supportedProtocolVersions":[{"major":0,"minor":1}],"type":"hello"}
  ]== CMake MetaMagic ==]

Messages sent to and from the process are wrapped in magic strings::

  [== CMake MetaMagic ==[
  {
    ... some JSON message ...
  }
  ]== CMake MetaMagic ==]

The server is now ready to accept further requests via stdin.


Protocol API
------------


General
^^^^^^^

All messages need to have a "type" value, which identifies the type of
message that is passed back or forth. E.g. the initial message sent by the
server is of type "hello". Messages without a type will generate an response
of type "error".

All requests sent to the server may contain a "cookie" value. This value
will he handed back unchanged in all responses triggered by the request.

All responses will contain a value "inReplyTo", which may be empty in
case of parse errors, but will contain the type of the request message
in all other cases.

All requests can contain a "debug" object to enable different settings
that help with debugging cmake server-mode. These settings include:

* "dumpToFile", followed by a string value containing a path to dump
  the response into.

* "prettyPrint", to enable pretty printing of the response.

* "showStats", to enable simple statistics on run time and response size.


Type "reply"
^^^^^^^^^^^^

This type is used by the server to reply to requests.

The message may -- depending on the type of the original request --
contain values.

Example::
  [== CMake MetaMagic ==[
  {"cookie":"zimtstern","inReplyTo":"handshake","type":"reply"}
  ]== CMake MetaMagic ==]


Type "error"
^^^^^^^^^^^^

This type is used to return an error condition to the client. It will
contain an "errorMessage".

Example::
  [== CMake MetaMagic ==[
  {"cookie":"","errorMessage":"Protocol version not supported.","inReplyTo":"handshake","type":"error"}
  ]== CMake MetaMagic ==]


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


Type "hello"
^^^^^^^^^^^^

The initial message send by the cmake server on startup is of type "hello".
This is the only message ever sent by the server that is not of type "reply",
"progress" or "error".

It will contain "supportedProtocolVersions" with an array of server protocol
versions supported by the cmake server. These are JSON objects with "major" and
"minor" keys containing non-negative integer values.

Example::
  [== CMake MetaMagic ==[
  {"supportedProtocolVersions":[{"major":0,"minor":1}],"type":"hello"}
  ]== CMake MetaMagic ==]


Type "handshake"
^^^^^^^^^^^^^^^^

The first request that the client may send to the server is of type "handshake".

This request needs to pass one of the "supportedProtocolVersions" of the "hello"
type response received earlier back to the server in the "protocolVersion" field.

Example::
  [== CMake MetaMagic ==[
  {"cookie":"zimtstern","type":"handshake","protocolVersion":{"major":0}}
  ]== CMake MetaMagic ==]

which will result in a response type "reply"::
  [== CMake MetaMagic ==[
  {"cookie":"zimtstern","inReplyTo":"handshake","type":"reply"}
  ]== CMake MetaMagic ==]

indicating that the server is ready for action.


Type "globalSettings"
^^^^^^^^^^^^^^^^^^^^^

This request can be sent after the initial handshake. It will return a
JSON structure with information on cmake state.

Example::
  [== CMake MetaMagic ==[
  {"type":"globalSettings"}
  ]== CMake MetaMagic ==]

which will result in a response type "reply"::
  [== CMake MetaMagic ==[
  {"inReplyTo":"globalSettings",
   "debugOutput":false,"currentGenerator":"","warnUnused":false,"warnUnusedCli":true,
   "checkSystemVars":false,"buildDirectory":"","warnUninitialized":false,
   "traceExpand":false,"generators":
   ["Unix Makefiles","Ninja","Watcom WMake","CodeBlocks - Ninja",
    "CodeBlocks - Unix Makefiles","CodeLite - Ninja",
    "CodeLite - Unix Makefiles","Eclipse CDT4 - Ninja",
    "Eclipse CDT4 - Unix Makefiles","KDevelop3",
    "KDevelop3 - Unix Makefiles","Kate - Ninja",
    "Kate - Unix Makefiles","Sublime Text 2 - Ninja",
    "Sublime Text 2 - Unix Makefiles"],
   "version":{"major":3,"patchLevel":20160601,"minor":5,"string":"3.5.20160601"},
   "sourceDirectory":"","trace":false,"cookie":"","type":"reply"}
  ]== CMake MetaMagic ==]


Type "setGlobalSettings"
^^^^^^^^^^^^^^^^^^^^^^^^

This request can be sent to change the global settings attributes. Unknown or read-only
attributes are going to be ignored. All other settings will be changed.

The server will respond with an empty reply message or an error.

Example::
  [== CMake MetaMagic ==[
  {"type":"setGlobalSettings","debugOutput":true}
  ]== CMake MetaMagic ==]

CMake will reply to this with::
  [== CMake MetaMagic ==[
  {"inReplyTo":"setGlobalSettings","type":"reply"}
  ]== CMake MetaMagic ==]


Type "configure"
^^^^^^^^^^^^^^^^

This request will configure a project for build.

To configure a build directory already containing cmake files, it is enough to
set "buildDirectory" via "setGlobalSettings". To create a fresh build directory
you also need to set "currentGenerator" and "sourceDirectory" via "setGlobalSettings"
in addition to "buildDirectory".

You may a list of strings to "configure" via the "cacheArguments" key. These
strings will be interpreted similar to command line arguments related to
cache handling that are passed to the cmake command line client.

Example::
  [== CMake MetaMagic ==[
  {"type":"configure", "cacheArguments":["-Dsomething=else"]}
  ]== CMake MetaMagic ==]

CMake will reply like this (after reporting progress for some time)::
  [== CMake MetaMagic ==[
  {"cookie":"","inReplyTo":"configure","type":"reply"}
  ]== CMake MetaMagic ==]

.. cmake-manual-description: CMake Metadata Daemon

cmake-metadata-daemon(7)
************************

.. only:: html

   .. contents::

Introduction
============

:manual:`cmake(1)` is capable of providing semantic information about
CMake code it executes to generate a buildsystem.  If executed with
the ``-E daemon`` command line options, it starts in a long running mode
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

Start :manual:`cmake(1)` in the daemon command mode, supplying the path to
the build directory to process::

  cmake -E daemon

The daemon will start up and reply with an hello message on stdout::
  [== CMake MetaMagic ==[
  {"supportedProtocolVersions":[{"major":0,"minor":1}],"type":"hello"}
  ]== CMake MetaMagic ==]

Messages sent to and from the process are wrapped in magic strings::

  [== CMake MetaMagic ==[
  {
    ... some JSON message ...
  }
  ]== CMake MetaMagic ==]

The daemon is now ready to accept further requests via stdin.


Protocol API
------------


General
^^^^^^^

All messages need to have a "type" value, which identifies the type of
message that is passed back or forth. E.g. the initial message sent by the
daemon is of type "hello". Messages without a type will generate an response
of type "error".

All requests sent to the daemon may contain a "cookie" value. This value
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

When the daemon is busy for a long time, it is polite to send back replies of
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

The initial message send by the cmake-daemon on startup is of type "hello".
This is the only message ever sent by the server that is not of type "reply",
"progress" or "error".

It will contain "supportedProtocolVersions" with an array of server protocol
versions supported by the cmake daemon. These are JSON objects with "major" and
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

indicating that the daemon is ready for action.

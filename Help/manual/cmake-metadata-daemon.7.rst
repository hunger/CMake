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

All requests can contain a "debug" object to enable different settings
that help with debugging cmake daemon-mode. These settings include:

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


Type "reset"
^^^^^^^^^^^^

The reset command will reset the cmake daemon to the state it had right
after the handshake message was sent.

The server will respond with an empty reply.

Example::
  [== Make MetaMagic ==[
  {"type":"reset"}
  ]== CMake MetaMagic ==]

which will result in a response type "reply"::
  [== CMake MetaMagic ==[
  {"inReplyTo":"reset","type":"reply"}
  ]== CMake MetaMagic ==]


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

The daemon will respond with an empty reply message or an error.

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


Type "generate"
^^^^^^^^^^^^^^^

This requist will generate build system files in the build directory and
is only available after a project was successfully "configure"d.

Example::
  [== CMake MetaMagic ==[
  {"type":"generate"}
  ]== CMake MetaMagic ==]

CMake will reply (after reporting progress information)::
  [== CMake MetaMagic ==[
  {"cookie":"","inReplyTo":"generate","type":"reply"}
  ]== CMake MetaMagic ==]


Type "project"
^^^^^^^^^^^^^^

The "project" request can be used after a project was "generate"d successfully.

It will list the complete project structure as it is known to cmake.

The reply will contain a key "projects", which will contain a list of
project objects, one for each (sub-)project defined in the cmake build system.

Each project object can have the following keys:
# "name", containing the (sub-)projects name.
# "sourceDirectory", containing the current source directory
# "buildDirectory", containing the current build directory.
# "configurations", containing a list of configuration objects.

Configuration objects are used to destinquish between different
configurations the build directory might have enabled. While most generators
only support one configuration, others support several.

Each configuration object can have the following keys:
# "name", containing the name of the configuration. The namy may be empty.
# "targets", containing a list of target objects, one for each build
  target.

Target objects define individual build targets for a certain configuration.

Each target object can have the following keys:
# "name", containing the name of the target.
# "type", defining the type of build target this is. Possible values are
  "STATIC_LIBRARY", "MODULE_LIBRARY", "SHARED_LIBRARY", "OBJECT_LIBRARY",
  "EXECUTABLE", "UTILITY", "INTERFACE_LIBRARY" and "UNKNOWN_LIBRARY".
# "fullName", containing the full name of the build result (incl. extensions, etc.).
# "sourceDirectory", containing the current source directory.
# "buildDirectory", containing the current build directory.
# "artifactDirectory", the directory the build result will end up in.
# "linkerLanguage", the language of the linker used to produce the artifact.
# "linkLibraries", with a list of libraries to link to.
# "linkFlags", with a list of flags to pass to the linker.
# "linkLanguageFlags", with the flags for a compiler using the linkerLanguage.
# "frameworkPath", with the framework path (on Apple computers).
# "linkPath", with the link path.
# "sysroot", with the sysroot path.
# "fileGroups", which will indirectly contain the source files making up the target.

FileGroups are used to group sources using similar settings together.

Each fileGroup object may contain the following keys:
# "language", containing the programming language used by all files in the group.
# "compileFlags", with a string containing all the flags passed to the compiler
  when building any of the files in this group.
# "includePath", with a list of include paths. Each include path is an object
  containing a "path" with the actual include path and "isSystem" with a bool
  value informing whether this is a normal include or a system include.
# "defines", with a list of defines in the form "SOMEVALUE" or "SOMEVALUE=42".
# "sources", with a list of source files.

All file paths in the fileGroup are either absolute or relative to the
sourceDirectory of the target.

Example::
  [== CMake MetaMagic ==[
  {"type":"project"}
  ]== CMake MetaMagic ==]

CMake will reply::
  [== CMake MetaMagic ==[
  {
    "cookie":"",
    "type":"reply",
    "inReplyTo":"project",

    "projects":
    [
      {
        "name":"CMAKE_FORM",
        "sourceDirectory":"/home/code/src/cmake/Source/CursesDialog/form"
        "buildDirectory":"/tmp/cmake-build-test/Source/CursesDialog/form",
        "configurations":
        [
          {
            "name":"",
            "targets":
            [
              {
                "artifactDirectory":"/tmp/cmake/Source/CursesDialog/form",
                "fileGroups":
                [
                  {
                    "compileFlags":"  -std=gnu11",
                    "defines":
                    [
                      "SOMETHING=1",
                      "LIBARCHIVE_STATIC"
                    ],
                    "includePath":
                    [
                      { "path":"/tmp/cmake-build-test/Utilities" },
                      { "isSystem": true, "path":"/usr/include/something" },
                      ...
                    ]
                    "lanugage":"C",
                    "sources":
                    [
                      "fld_arg.c",
                      ...
                      "fty_regex.c"
                    ]
                  }
                ],
                "fullName":"libcmForm.a",
                "linkerLanguage":"C",
                "name":"cmForm",
                "type":"STATIC_LIBRARY"
              }
            ]
          }
        ],
      },
      ...
    ]
  }
  ]== CMake MetaMagic ==]

The output can be tailored to the specific needs via parameter passed when
requesting "project" information.

You can have a "depth" key, which accepts "project", "configuration" and
"target" as string values. These cause the output to be trimmed at the
appropriate depth of the output tree.

You can also set "configurations" to an array of strings with configuration
names to list. This will cause any configuration that is not listed to be
trimmed from the output.

Generated files can be included in the listing by setting "includeGeneratedFiles"
to "true". This setting defaults to "false", so generated files are not
listed by default.

Finally you can limit the target types that are going to be listed. This is
done by providing a list of target types as an array of strings to the
"targetTypes" key.


Type "buildSystem"
^^^^^^^^^^^^^^^^^^

The "buildSystem" requests will report files used by CMake as part
of the build system itself.

This request is only available after a project was successfully
"configure"d.

Example::
  [== CMake MetaMagic ==[
  {"type":"buildSystem"}
  ]== CMake MetaMagic ==]

CMake will reply with the following information::
  [== CMake MetaMagic ==[
  {"buildFiles":
    [
      {"isCMake":true,"isTemporary":false,"sources":["/usr/lib/cmake/...", ... ]},
      {"isCMake":false,"isTemporary":false,"sources":["CMakeLists.txt", ...]},
      {"isCMake":false,"isTemporary":true,"sources":["/tmp/build/CMakeFiles/...", ...]}
    ],
    "cmakeRootDirectory":"/usr/lib/cmake",
    "sourceDirectory":"/home/code/src/cmake",
    "cookie":"",
    "inReplyTo":"buildSystem",
    "type":"reply"
  }
  ]== CMake MetaMagic ==]

All file names are either relative to the top level source directory or
absolute.

The list of files which "isCMake" set to true are part of the cmake installation.

The list of files witch "isTemporary" set to true are part of the build directory
and will not survive the build directory getting cleaned out.

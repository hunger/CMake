#!/usr/bin/env python2

import sys, subprocess, json

termwidth = 150

print_communication = True

def ordered(obj):
  if isinstance(obj, dict):
    return sorted((k, ordered(v)) for k, v in obj.items())
  if isinstance(obj, list):
    return sorted(ordered(x) for x in obj)
  else:
    return obj

def col_print(title, array):
  print
  print
  print title

  indentwidth = 4
  indent = " " * indentwidth

  if not array:
    print(indent + "<None>")
    return

  padwidth = 2

  maxitemwidth = len(max(array, key=len))

  numCols = max(1, int((termwidth - indentwidth + padwidth) / (maxitemwidth + padwidth)))

  numRows = len(array) // numCols + 1

  pad = " " * padwidth

  for index in range(numRows):
    print(indent + pad.join(item.ljust(maxitemwidth) for item in array[index::numRows]))

def waitForRawMessage(cmakeCommand):
  stdoutdata = ""
  payload = ""
  while not cmakeCommand.poll():
    stdoutdataLine = cmakeCommand.stdout.readline()
    if stdoutdataLine:
      stdoutdata += stdoutdataLine
    else:
      break
    begin = stdoutdata.find("[== CMake MetaMagic ==[\n")
    end = stdoutdata.find("]== CMake MetaMagic ==]")

    if (begin != -1 and end != -1):
      begin += len("[== CMake MetaMagic ==[\n")
      payload = stdoutdata[begin:end]
      if print_communication:
        print "\nDAEMON>", json.loads(payload), "\n"
      return json.loads(payload)

def writeRawData(cmakeCommand, content):
  payload = """
[== CMake MetaMagic ==[
%s
]== CMake MetaMagic ==]
""" % content
  if print_communication:
    print "\nCLIENT>", content, "\n"
  cmakeCommand.stdin.write(payload)

def writePayload(cmakeCommand, obj):
  writeRawData(cmakeCommand, json.dumps(obj))

def initProc(cmakeCommand):
  cmakeCommand = subprocess.Popen([cmakeCommand, "-E", "daemon"],
                                  stdin=subprocess.PIPE,
                                  stdout=subprocess.PIPE)

  packet = waitForRawMessage(cmakeCommand)
  if packet == None:
    print "Not in daemon mode"
    sys.exit(1)

  if packet['type'] != 'hello':
    print "No hello message"
    sys.exit(1)

  return cmakeCommand

def waitForMessage(cmakeCommand, expected):
  data = ordered(expected)
  packet = ordered(waitForRawMessage(cmakeCommand))

  if packet != data:
    sys.exit(-1)

def waitForReply(cmakeCommand, originalType, cookie):
  packet = waitForRawMessage(cmakeCommand)
  if packet['cookie'] != cookie or packet['type'] != 'reply' or packet['inReplyTo'] != originalType:
    sys.exit(1)

def waitForError(cmakeCommand, originalType, cookie, message):
  packet = waitForRawMessage(cmakeCommand)
  if packet['cookie'] != cookie or packet['type'] != 'error' or packet['inReplyTo'] != originalType or packet['errorMessage'] != message:
    sys.exit(1)

def waitForProgress(cmakeCommand, originalType, cookie, current, message):
  packet = waitForRawMessage(cmakeCommand)
  if packet['cookie'] != cookie or packet['type'] != 'progress' or packet['inReplyTo'] != originalType or packet['progressCurrent'] != current or packet['progressMessage'] != message:
    sys.exit(1)

def handshake(cmakeCommand, major, minor):
  version = { 'major': major }
  if minor >= 0:
    version['minor'] = minor

  writePayload(cmakeCommand, { 'type': 'handshake', 'protocolVersion': version, 'cookie': 'TEST_HANDSHAKE' })
  waitForReply(cmakeCommand, 'handshake', 'TEST_HANDSHAKE')

def initialize(cmakeCommand, sourceDir, buildDir):
  writePayload(cmakeCommand, { 'type': 'initialize', 'buildDirectory': buildDir, 'cookie': 'TEST_INIT' })
  waitForProgress(cmakeCommand, 'initialize', 'TEST_INIT', 0, 'initialized')
  waitForProgress(cmakeCommand, 'initialize', 'TEST_INIT', 1, 'configured')
  waitForProgress(cmakeCommand, 'initialize', 'TEST_INIT', 2, 'computed')
  waitForProgress(cmakeCommand, 'initialize', 'TEST_INIT', 3, 'done')
  waitForMessage(cmakeCommand, {"binary_dir":buildDir,"cookie":"TEST_INIT","inReplyTo":"initialize","project_name":"CMake","source_dir":sourceDir,"type":"reply"})


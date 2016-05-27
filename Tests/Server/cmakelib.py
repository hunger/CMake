#!/usr/bin/env python

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
  print(title)

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
      stdoutdata += stdoutdataLine.decode('utf-8')
    else:
      break
    begin = stdoutdata.find("[== CMake MetaMagic ==[\n")
    end = stdoutdata.find("]== CMake MetaMagic ==]")

    if (begin != -1 and end != -1):
      begin += len("[== CMake MetaMagic ==[\n")
      payload = stdoutdata[begin:end]
      if print_communication:
        print("\nSERVER>", json.loads(payload), "\n")
      return json.loads(payload)

def writeRawData(cmakeCommand, content):
  payload = """
[== CMake MetaMagic ==[
%s
]== CMake MetaMagic ==]
""" % content
  if print_communication:
    print("\nCLIENT>", content, "\n")
  cmakeCommand.stdin.write(payload.encode('utf-8'))
  cmakeCommand.stdin.flush()

def writePayload(cmakeCommand, obj):
  writeRawData(cmakeCommand, json.dumps(obj))

def initProc(cmakeCommand):
  cmakeCommand = subprocess.Popen([cmakeCommand, "-E", "server", "--experimental"],
                                  stdin=subprocess.PIPE,
                                  stdout=subprocess.PIPE)

  packet = waitForRawMessage(cmakeCommand)
  if packet == None:
    print("Not in server mode")
    sys.exit(1)

  if packet['type'] != 'hello':
    print("No hello message")
    sys.exit(1)

  return cmakeCommand

def waitForMessage(cmakeCommand, expected):
  data = ordered(expected)
  packet = ordered(waitForRawMessage(cmakeCommand))

  if packet != data:
    sys.exit(-1)
  return packet

def waitForReply(cmakeCommand, originalType, cookie):
  packet = waitForRawMessage(cmakeCommand)
  if packet['cookie'] != cookie or packet['type'] != 'reply' or packet['inReplyTo'] != originalType:
    sys.exit(1)
  return packet

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

def validateGlobalSettings(cmakeCommand, cmakeCommandPath):
  packet = waitForReply(cmakeCommand, 'globalSettings', '')

  # validate version:
  cmakeoutput = subprocess.check_output([ cmakeCommandPath, "--version" ], universal_newlines=True)
  cmakeVersion = cmakeoutput.splitlines()[0][14:]

  version = packet['version']
  versionString = version['string']
  vs = str(version['major']) + '.' + str(version['minor']) + '.' + str(version['patchLevel'])
  if (versionString != vs and not versionString.startswith(vs + '-')):
    sys.exit(1)
  if (versionString != cmakeVersion):
    sys.exit(1)

  # validate generators:
  generators = packet['generators']

  cmakeoutput = subprocess.check_output([ cmakeCommandPath, "--help" ], universal_newlines=True)
  index = cmakeoutput.index('\nGenerators\n\n')
  cmakeGenerators = []
  for line in cmakeoutput[index + 12:].splitlines():
    if not line.startswith('  '):
      continue
    equalPos = line.find('=')
    tmp = ''
    if (equalPos > 0):
      tmp = line[2:equalPos].strip()
    else:
      tmp = line.strip()
    if len(tmp) > 0:
      cmakeGenerators.append(tmp)

  generators.sort()
  cmakeGenerators.sort()

  if (generators != cmakeGenerators):
    sys.exit(1)

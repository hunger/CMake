#!/usr/bin/env python2

import sys, subprocess, json

termwidth = 150

print_communication = False

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

def waitForMessage(process):
  stdoutdata = ""
  payload = ""
  while not process.poll():
    stdoutdataLine = process.stdout.readline()
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

def writeRawData(process, content):
  payload = """
[== CMake MetaMagic ==[
%s
]== CMake MetaMagic ==]
""" % content
  if print_communication:
    print "\nCLIENT>", payload, "\n"
  process.stdin.write(payload)

def writePayload(process, obj):
  writeRawData(process, json.dumps(obj))

def initProc(cmakeCommand):

  cmakeProcess = subprocess.Popen([cmakeCommand, "-E", "daemon"],
                                  stdin=subprocess.PIPE,
                                  stdout=subprocess.PIPE)

  packet = waitForMessage(cmakeProcess)
  if packet == None:
    print "FAIL: Not in daemon mode"
    sys.exit(1)

  if packet["type"] != "hello":
    print "FAIL: No hello message"
    sys.exit(1)

  return cmakeProcess

#!/usr/bin/env python

import sys, cmakelib, json

debug = True

cmakeCommand = sys.argv[1]
testFile = sys.argv[2]
sourceDir = sys.argv[3]
buildDir = sys.argv[4]

print("SourceDir: ", sourceDir, " -- BuildDir: ", buildDir)

proc = cmakelib.initProc(cmakeCommand)

with open(testFile) as f:
    testText = f.read()
    testText = testText.replace('%BUILDDIR%', buildDir)
    testText = testText.replace('%SOURCEDIR%', sourceDir)
    testData = json.loads(testText)

buildDir = sys.argv[3]
sourceDir = sys.argv[4]

for obj in testData:
    if 'sendRaw' in obj:
        data = obj['sendRaw']
        if debug: print("Sending raw:", data)
        cmakelib.writeRawData(proc, data)
    elif 'send' in obj:
        data = obj['send']
        if debug: print("Sending:", json.dumps(data))
        cmakelib.writePayload(proc, data)
    elif 'recv' in obj:
        data = obj['recv']
        if debug: print("Waiting for:", json.dumps(data))
        cmakelib.waitForMessage(proc, data)
    elif 'reply' in obj:
        data = obj['reply']
        if debug: print("Waiting for reply:", json.dumps(data))
        originalType = ""
        cookie = ""
        if 'cookie' in data: cookie = data['cookie']
        if 'type' in data: originalType = data['type']
        cmakelib.waitForReply(proc, originalType, cookie)
    elif 'error' in obj:
        data = obj['error']
        if debug: print("Waiting for error:", json.dumps(data))
        originalType = ""
        cookie = ""
        message = ""
        if 'cookie' in data: cookie = data['cookie']
        if 'type' in data: originalType = data['type']
        if 'message' in data: message = data['message']
        cmakelib.waitForError(proc, originalType, cookie, message)
    elif 'progress' in obj:
        data = obj['progress']
        if debug: print("Waiting for progress:", json.dumps(data))
        originalType = ''
        cookie = ""
        current = 0
        message = ""
        if 'cookie' in data: cookie = data['cookie']
        if 'type' in data: originalType = data['type']
        if 'current' in data: current = data['current']
        if 'message' in data: message = data['message']
        cmakelib.waitForProgress(proc, originalType, cookie, current, message)
    elif 'handshake' in obj:
        data = obj['handshake']
        if debug: print("Doing handshake:", json.dumps(data))
        major = -1
        minor = -1
        if 'major' in data: major = data['major']
        if 'minor' in data: minor = data['minor']
        cmakelib.handshake(proc, major, minor)
    elif 'validateGlobalSettings' in obj:
        cmakelib.validateGlobalSettings(proc, cmakeCommand)
    elif 'message' in obj:
        print("MESSAGE:", obj["message"])
    else:
        print("Unknown command:", json.dumps(obj))
        sys.exit(2)

    print("Completed")

sys.exit(0)

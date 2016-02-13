#!/usr/bin/env python2

import sys, cmakelib, json

debug = False

def ordered(obj):
    if isinstance(obj, dict):
        return sorted((k, ordered(v)) for k, v in obj.items())
    if isinstance(obj, list):
        return sorted(ordered(x) for x in obj)
    else:
        return obj

cmakeCommand = sys.argv[1]

proc = cmakelib.initProc(cmakeCommand)

with open(sys.argv[2]) as f:
    testdata = json.loads(f.read())

for obj in testdata:
    if "sendRaw" in obj:
        data = obj["sendRaw"]
        if debug: print "Sending raw:", data
        cmakelib.writeRawData(proc, data)
    elif "send" in obj:
        data = obj["send"]
        if debug: print "Sending:", json.dumps(data)
        cmakelib.writePayload(proc, data)
    elif "recv" in obj:
        data = ordered(obj["recv"])
        if debug: print "Waiting for:", json.dumps(data)
        packet = ordered(cmakelib.waitForMessage(proc))

        if packet != data:
             print "NOT EQUAL\n", packet, "\n\n", data
             sys.exit(-1)
        else:
             if debug: print "Got expected response\n"
    elif "reply" in obj:
        data = obj["reply"]
        if debug: print "Waiting for reply:", json.dumps(data)
        originalType = ""
        cookie = ""
        if 'cookie' in data: cookie = data['cookie']
        if 'type' in data: originalType = data['type']
        cmakelib.waitForReply(proc, originalType, cookie)
    elif "error" in obj:
        data = obj["error"]
        if debug: print "Waiting for error:", json.dumps(data)
        originalType = ""
        cookie = ""
        message = ""
        if 'cookie' in data: cookie = data['cookie']
        if 'type' in data: originalType = data['type']
        if 'message' in data: message = data['message']
        cmakelib.waitForError(proc, originalType, cookie, message)
    elif "handshake" in obj:
        data = obj['handshake']
        if debug: print "Doing handshake:" .json.dumps(data)
        major = data['major']
        minor = -1
        if 'minor' in data:
             minor = data['minor']
        cmakelib.handshake(proc, major, minor)
    elif "message" in obj:
        print "MESSAGE:", obj["message"]
    else:
        print "Unknown command:", json.dumps(obj)
        sys.exit(2)

sys.exit(0)

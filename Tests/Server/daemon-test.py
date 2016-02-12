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
    elif "message" in obj:
        print "MESSAGE:", obj["message"]
    else:
        print "Unknown command:", json.dumps(obj)
        sys.exit(2)

sys.exit(0)

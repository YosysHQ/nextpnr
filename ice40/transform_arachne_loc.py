#!/usr/bin/env python3
import json
import sys
import re

with open(sys.argv[1]) as f:
    data = json.load(f)
    
for mod, moddata in data["modules"].items():
    if "cells" in moddata:
        for cell, celldata in moddata["cells"].items():
            pos = re.split('[,/]', celldata["attributes"]["loc"])
            pos = [int(_) for _ in pos]
            if celldata["type"] == "ICESTORM_LC":
                celldata["attributes"]["BEL"] = "X%d/Y%d/lc%d" % (pos[0], pos[1], pos[2])
            elif celldata["type"] == "SB_IO":
                celldata["attributes"]["BEL"] = "X%d/Y%d/io%d" % (pos[0], pos[1], pos[2])
            elif "RAM" in celldata["type"]:
                celldata["attributes"]["BEL"] = "X%d/Y%d/ram" % (pos[0], pos[1])
            elif celldata["type"] == "SB_GB":
                celldata["attributes"]["BEL"] = "X%d/Y%d/gb" % (pos[0], pos[1])
            else:
                assert False
print(json.dumps(data, sort_keys=True, indent=4))
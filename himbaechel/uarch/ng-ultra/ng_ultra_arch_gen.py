import json
from os import path
import sys
sys.path.append(path.join(path.dirname(__file__), "../.."))
from himbaechel_dbgen.chip import *

def create_tile(ch: Chip):
    tt = ch.create_tile_type("TILE")

def create_mesh(ch: Chip):
    tt = ch.create_tile_type("MESH")

def create_cgb(ch: Chip):
    tt = ch.create_tile_type("CGB")

def main():
    ch = Chip("ng-ultra", "NG-ULTRA", 93, 50)
    ch.strs.read_constids(path.join(path.dirname(__file__), "constids.inc"))
    pkg = ch.create_package("FF-1760")

    create_tile(ch)
    create_mesh(ch)
    create_cgb(ch)

    with open("/home/lofty/prjbeyond/database/NG-ULTRA/tilegrid.json") as f:
        tilegrid = json.load(f)
        for name, data in tilegrid.items():
            ch.set_tile_type(data["x"], data["y"], data["type"])

    ch.write_bba(sys.argv[1])

if __name__ == '__main__':
    main()

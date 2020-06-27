#!/usr/bin/env python3
import argparse
import json
import sys

parser = argparse.ArgumentParser(description="import MachXO2 routing and bels from Project Trellis")
parser.add_argument("device", type=str, help="target device")
parser.add_argument("-p", "--constids", type=str, help="path to constids.inc")
parser.add_argument("-g", "--gfxh", type=str, help="path to gfx.h (unused)")
parser.add_argument("-L", "--libdir", type=str, action="append", help="extra Python library path")
args = parser.parse_args()

sys.path += args.libdir
import pytrellis
import database


class BinaryBlobAssembler:
    def l(self, name, ltype = None, export = False):
        if ltype is None:
            print("label %s" % (name,))
        else:
            print("label %s %s" % (name, ltype))

    def r(self, name, comment):
        if comment is None:
            print("ref %s" % (name,))
        else:
            print("ref %s %s" % (name, comment))

    def s(self, s, comment):
        assert "|" not in s
        print("str |%s| %s" % (s, comment))

    def u8(self, v, comment):
        if comment is None:
            print("u8 %d" % (v,))
        else:
            print("u8 %d %s" % (v, comment))

    def u16(self, v, comment):
        if comment is None:
            print("u16 %d" % (v,))
        else:
            print("u16 %d %s" % (v, comment))

    def u32(self, v, comment):
        if comment is None:
            print("u32 %d" % (v,))
        else:
            print("u32 %d %s" % (v, comment))

    def pre(self, s):
        print("pre %s" % s)

    def post(self, s):
        print("post %s" % s)

    def push(self, name):
        print("push %s" % name)

    def pop(self):
        print("pop")


dev_names = {"1200": "LCMXO2-1200HC"}

def main():
    global max_row, max_col, const_id_count

    pytrellis.load_database(database.get_db_root())

    # Dummy
    dev_name = "1200"

    bba = BinaryBlobAssembler()
    bba.pre('#include "nextpnr.h"')
    bba.pre('#include "embed.h"')
    bba.pre('NEXTPNR_NAMESPACE_BEGIN')
    bba.post('EmbeddedFile chipdb_file_%s("machxo2/chipdb-%s.bin", chipdb_blob_%s);' % (dev_name, dev_name, dev_name))
    bba.post('NEXTPNR_NAMESPACE_END')
    bba.push("chipdb_blob_%s" % args.device)
    bba.u8(0, None)
    bba.pop()

if __name__ == "__main__":
    main()

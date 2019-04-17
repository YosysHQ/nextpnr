# Generic Architecture Example

This contains a simple, artificial, example of the nextpnr generic API.

 - simple.py procedurally generates a simple FPGA architecture with IO at the edges,
   logic slices in all other tiles, and interconnect only between adjacent tiles
 
 - simple_timing.py annotates cells with timing data (this is a separate script that must be run after packing)

 - write_fasm.py uses the nextpnr Python API to write a FASM file for a design

 - bitstream.py uses write_fasm.py to create a FASM ("FPGA assembly") file for the place-and-routed design

 - Run simple.sh to build an example design on the FPGA above
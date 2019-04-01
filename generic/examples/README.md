# Generic Architecture Example

This contains a simple, artificial, example of the nextpnr generic API.

 - simple.py procedurally generates a simple FPGA architecture with IO at the edges,
   logic slices in all other tiles, and interconnect only between adjacent tiles

 - report.py stores design information after place-and-route to blinky.txt in place
   of real bitstream generation

 - Run blinky.sh to build an example design on the FPGA above
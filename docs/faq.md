FAQ
===

Nextpnr and other tools
-----------------------

### Which tool chain should I use and why?

 * If you wish to do new **research** into FPGA architectures, place and route
   algorithms or other similar topics, we suggest you look at using
   [Verilog to Routing](https://verilogtorouting.org).

 * If you are developing FPGA code in **Verilog** for a **Lattice iCE40** and
   need an open source toolchain, we suggest you use nextpnr.

 * If you are developing FPGA code in **Verilog** for a **Lattice iCE40** with
   the **existing Arachne-PNR toolchain**, we suggest you start thinking about
   migrating to nextpnr.

 * If you are developing Verilog FPGA code targeted at the Lattice ECP5 and
   need an open source toolchain, you may consider the **extremely
   experimental** ECP5 support in nextpnr.

 * If you are developing FPGA code in **VHDL** you will need to use either a
   version of [Yosys with Verific support]() or the vendor provided tools due
   to the lack of open source VHDL support in Yosys.

### Why didn't you just improve [Arachne-PNR](https://github.com/cseed/arachne-pnr)?

[Arachne-PNR](https://github.com/cseed/arachne-pnr) was originally developed as
part of [Project IceStorm](http://www.clifford.at/icestorm/) to demonstrate it
was possible to create an open source place and route tool for the iCE40 FPGAs
that actually produced valid bitstreams.

For it's original purpose it has served the community extremely well. However,
it was never designed to support multiple different FPGA devices, nor more
complicated timing driven routing used by most commercial place and route
tools.

It felt like extending Arachne-PNR was not going to be the best path forward, so
[SymbioticEDA](https://www.symbioticeda.com/) decided to invest in an
experiment around creating a replacement. nextpnr is the result of that
experiment and we believe well on it's way to being a direct replacement for
Arachne-PNR (and hence why it is called *next*pnr).

### Arachne-PNR does X better!

If you have a use case which prevents you from switching to nextpnr from
Arachne, we want to hear about it! Please create an issue following the
[Arachne-PNR regression template]() and we will do our best to solve the problem!

We want nextpnr to be a suitable replacement for anyone who is currently a user
of Arachne.

### Why are you not just contributing to [Verilog to Routing](https://verilogtorouting.org)?

We believe that [Verilog to Routing](https://verilogtorouting.org) is a great
tool and many of the nextpnr developers have made (and continue to make)
contributions to the project.

VtR is an extremely flexible tool but focuses on research around FPGA
architecture and algorithm development. If your goal is research, then we very
much encourage you to look into VtR further!

nextpnr takes a different approach by focusing on users developing FPGA code
for current FPGAs.

We also believe that support for real architectures will enable interesting new
research. nextpnr (like all place and route systems). depends heavily on
research groups like the VtR developers to investigate and push forward FPGA
algorithms in new and exciting ways.

#### What is VPR?

VPR is the "place and route" tool from Verilog To Routing. It has a similar
role in an FPGA development flow as nextpnr.

### What about [SymbiFlow](http://symbiflow.github.io)?

We expect that as nextpnr matures, it will become a key part of the
[SymbiFlow](http://github.com/SymbiFlow). For now, while still in a more
experimental state SymbioticEDA will continue to host the project.

For the moment SymbiFlow is continuing to concentrate on extending Verilog to
Routing tool to work with real world architectures.

### Who is working on this project?

nextpnr was
[started as an experiment by SymbioticEDA](https://www.symbioticeda.com/) but
hopes to grow beyond being both just an experiment and developed by a single
company. Like Linux grew from Linus Torvalds experiment in creating his own
operating system to something contributed too by many different companies, are
hope is the same will happen here.

The project has already accepted a number of contributions from people not
employed by SymbioticEDA and now with the public release encourages the
community to contribute too.


### What is [Project Trellis](https://github.com/SymbiFlow/prjtrellis)?

[Project Trellis](https://github.com/SymbiFlow/prjtrellis) is the effort to
document the bitstream format for the Lattice ECP5 series of FPGAs. It also
includes tooling around bitstream creation.

Project Trellis is used by nextpnr to enable support for creation of bitstreams
for these parts.

### What is [Project X-Ray](https://github.com/SymbiFlow/prjxray)?

[Project X-Ray](https://github.com/SymbiFlow/prjxray) is the effort to document
the bitstream format for the Xilinx Series 7 series of FPGAs. It also includes
tooling around bitstream generation for these parts.

While nextpnr currently does **not** support these Xilinx parts, we expect it
will soon by using Project X Ray in a similar manner to Project Trellis.

### What is [Project IceStorm](http://www.clifford.at/icestorm/)?

[Project IceStorm](http://www.clifford.at/icestorm/) was both a project to
document the bitstream for the Lattice iCE40 series of parts **and** a full
flow including Yosys and Arachne-PNR for converting Verilog into a bitstream for
these parts.

As the open source community now has support for multiple different FPGA parts,
in the nextpnr documentation we generally use Project IceStorm to mean the
tools that fulfil the same role as Project Trellis or Project X-Ray.

# nextpnr-nexus notes

### Constraints

Currently the following PDC constraint styles are supported for IO constraints:

```
ldc_set_location -site {G13} [get_ports gsrn]
ldc_set_port -iobuf {IO_TYPE=LVCMOS33} [get_ports {led[0]}]
```

Timing constraints are currently ignored, but should be expected to be supported soon.

### Command Line

A full device name is specified on the command line. It should be postfixed with 'ES' if using an engineering sample device to ensure correct use of the ES IDCODE.

```
--device LIFCL-40-9BG400CES
```

# iCE40 Architecture Documentation

## PCF format reference

PCF files contain physical constraints and are specified using the `--pcf` argument. Each (non blank) line contains a command; lines beginning with `#` are comments.

Two commands are supported: `set_io` and `set_frequency`.

    set_io [-nowarn] [-pullup yes|no] [-pullup_resistor 3P3K|6P8K|10K|100K] port pin

Constrains named port `port` to package pin `pin`. `-nowarn` disables the warning if `port` does not exist. `-pullup yes` can be used to enable the built in pullup for all iCE40 devices. `-pullup_resistor` sets the pullup strength, and is available on iCE40 UltraPlus only.

    set_frequency net frequency

Adds a clock constraint to a named net (any alias for the net can be used). `frequency` is in MHz.

_Note that this is a non-standard extension, not supported by the vendor toolchain. It allows specifying clock constraints without needing the Python API._
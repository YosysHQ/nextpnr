# zybo board
set_property PACKAGE_PIN K17 [get_ports clk]
set_property PACKAGE_PIN K18 [get_ports rst]
set_property PACKAGE_PIN M14 [get_ports io_led[4]]
set_property PACKAGE_PIN M15 [get_ports io_led[5]]
set_property PACKAGE_PIN G14 [get_ports io_led[6]]
set_property PACKAGE_PIN D18 [get_ports io_led[7]]

set_property IOSTANDARD LVCMOS33 [get_ports clk]
set_property IOSTANDARD LVCMOS33 [get_ports rst]
set_property IOSTANDARD LVCMOS33 [get_ports io_led[4]]
set_property IOSTANDARD LVCMOS33 [get_ports io_led[5]]
set_property IOSTANDARD LVCMOS33 [get_ports io_led[6]]
set_property IOSTANDARD LVCMOS33 [get_ports io_led[7]]

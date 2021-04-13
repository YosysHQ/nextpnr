library ieee;
use ieee.std_logic_1164.all;

-- We don't have VHDL primitives yet, so declare them in examples for now.
package components is

component OSCH
  generic (
    NOM_FREQ : string := "2.08"
  );
  port(
    STDBY : in std_logic;
    OSC : out std_logic;
    SEDSTDBY : out std_logic
  );
end component;

end components;

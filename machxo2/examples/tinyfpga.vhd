library ieee ;
context ieee.ieee_std_context;

use work.components.all;

entity top is
  port (
    pin1: out std_logic
  );

  attribute LOC: string;
  attribute LOC of pin1: signal is "13";
end;

architecture arch of top is
  signal clk: std_logic;
  signal led_timer: unsigned(23 downto 0) := (others=>'0');
begin

  internal_oscillator_inst: OSCH
  generic map (
    NOM_FREQ => "16.63"
  )
  port map (
    STDBY => '0',
    OSC => clk
  );

  process(clk)
  begin
    if rising_edge(clk) then
      led_timer <= led_timer + 1;
    end if;
  end process;

  pin1 <= led_timer(led_timer'left);

end;

library IEEE;
use IEEE.STD_LOGIC_1164.ALL;

entity testbench is
end entity;
architecture rtl of testbench is
    signal clk : STD_LOGIC;
    signal led : STD_LOGIC_VECTOR(3 downto 0);
begin
    process begin
        clk <= '0';
        wait for 4 ns;
        clk <= '1';
        wait for 4 ns;
    end process;

    uut: entity work.name port map(clki_PAD_PAD => clk, led0_OUTBUF_OUT => led(0), led1_OUTBUF_OUT => led(1), led2_OUTBUF_OUT => led(2), led3_OUTBUF_OUT => led(3));

process
begin
report std_logic'image(led(3)) & std_logic'image(led(2)) & std_logic'image(led(1)) & std_logic'image(led(0));
wait on led;
end process;

end rtl;

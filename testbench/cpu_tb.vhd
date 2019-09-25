library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library work;
use work.rom_pkg.all;

entity cpu_tb is
end cpu_tb;

architecture behaviour of cpu_tb
is
	constant clk_period : time := 100 ns;
	signal clk : std_logic;

	signal reset_n : std_logic;
	signal pc :std_logic_vector(31 downto 0);
	signal pc_q :std_logic_vector(31 downto 0);
	signal pc_req : std_logic;
	signal pc_next : std_logic;

	signal ls_addr : std_logic_vector(31 downto 0);
	signal ls_byte : std_logic;
	signal ls_halfword : std_logic;
	signal ls_req : std_logic;
	signal ls_wr : std_logic;
	signal ls_ack : std_logic;


	signal ram_addr : std_logic_vector(31 downto 2);
	signal from_ram : std_logic_vector(31 downto 0);
	signal to_ram : std_logic_vector(31 downto 0);
	signal ram_wr : std_logic;
	signal ram_bytesel : std_logic_vector(3 downto 0);
	signal ram_req : std_logic;
	signal ram_ack : std_logic;

	type tbstates is (RESET,INIT,MAIN,LOAD);
	signal tbstate : tbstates:=RESET;

	signal romin : fromROM;
	signal romout : toROM;

begin

	rom : entity work.helloworld_rom
	port map(
		clk => clk,
		from_soc => romout,
		to_soc => romin
	);

	romout.MemAAddr<=ram_addr(15 downto 2);
	romout.MemAWrite<=to_ram;
	romout.MemAWriteEnable<=ram_wr and ram_req;
	romout.MemAByteSel<=ram_bytesel;
	from_ram<=romin.MemARead;

	cpu : entity work.eightthirtytwo_cpu_comb
	port map
	(
		clk => clk,
		reset_n => reset_n,

		-- cpu fetch interface

		addr => ram_addr,
		d => from_ram,
		q => to_ram,
		bytesel => ram_bytesel,
		wr => ram_wr,
		req => ram_req,
		ack => ram_ack
	);

  -- Clock process definition
  clk_process: process
  begin
    clk <= '0';
    wait for clk_period/2;
    clk <= '1';
    wait for clk_period/2;
  end process;

	process(clk,ls_ack)
	begin

		if rising_edge(clk) then

			pc_req<='0';
			reset_n<='1';
			pc_next<='0';
			ram_ack<='0';

			ls_byte<='0';
			ls_halfword<='0';
			ls_wr<='0';
			ls_req<='0';

			if ram_req='1' then
				ram_ack<='1';
			end if;

			case tbstate is
				when RESET =>
					reset_n<='0';
					tbstate<=INIT;
				when INIT =>
					pc<=(others => '0');
					pc_req<='1';
					tbstate<=MAIN;
				when MAIN =>
					if pc_q=X"00000010" then
						tbstate<=LOAD;
					else
						pc_next<='1';
						tbstate<=MAIN;
					end if;

				when LOAD =>
					ls_addr<=X"00000013";
					ls_byte<='1';
					ls_wr<='0';
					ls_req<='1' and not ls_ack;
					if ls_ack='1' then
						tbstate<=MAIN;
					end if;

			end case;

		end if;

	end process;

end behaviour;

Driving Party: Racing in Italy (ver EAA), MAME set "gticlub2ea".
Also known as: GTI Club 2, GTI Club: Corso Italiano.
Note: MAME: DIP switch SW:3 must be ON (IN2 bit 0x02 = 0), otherwise the game asks for a password. The BIOS passes IN2 to the game in the boot word (r31), see runtime hw_boot_param().
Note: The TEST MODE calibration flow is the same as Thrill Drive 2 (same script).

Put these files in this folder:

  941a02.chd        SHA1 dd180ad92dd344b38f160e31833077e342cee38d (internal CHD SHA1, see 'chdman info')
  941eaa_nvram.u39  SHA1 92e0ce01049308f459985d466fbfcfac82f34a47

Also needed: roms/kviper/ (941b01.u25, ds2430.u3).
Check with: make check GAME=gticlub2ea

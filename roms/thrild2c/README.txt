Thrill Drive 2 (ver EAA), MAME set "thrild2c".
Note: European version (older than EBB). MAME marks the only known CF dump as bad (blue screens), and the NVRAM has never been dumped: the game starts from an empty NVRAM.

Put these files in this folder:

  a41c02.chd        SHA1 ab3020e8709768c0fd2467573e92b679a05944e5 (internal CHD SHA1, see 'chdman info')
  941eaa_nvram.u39  no known dump: optional, the game starts with an empty NVRAM

Also needed: roms/kviper/ (941b01.u25, ds2430.u3).
Check with: make check GAME=thrild2c

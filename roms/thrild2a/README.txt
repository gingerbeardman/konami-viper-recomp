Thrill Drive 2 (ver AAA), MAME set "thrild2a".
Note: Asian version; same CF card as ver JAA, the region comes from the NVRAM.
Note: MAME uses the GTI Club 2 input layout (K-type steering wheel) for this version; the analog mapping and the calibration script still have to be verified.

Put these files in this folder:

  a41a02.chd        SHA1 bbb71e23bddfa07dfa30b6565a35befd82b055b8 (internal CHD SHA1, see 'chdman info')
                    (shared with roms/thrild2j/: one copy in either folder is enough)
  a41aaa_nvram.u39  SHA1 768bcd46a6ad20948f60f5e0ecd2f7b9c2901061

Also needed: roms/kviper/ (941b01.u25, ds2430.u3).
Check with: make check GAME=thrild2a

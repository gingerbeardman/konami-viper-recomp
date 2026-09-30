GTI Club: Corso Italiano (ver JAB), MAME set "gticlub2".
Also known as: GTI Club 2, Driving Party: Racing in Italy.
Note: Japanese version. The NVRAM declares a cabinet with a handbrake and a K-type force-feedback wheel (MOTOR TYPE: K-TYPE).
Note: The calibration is TD2's script plus the motor-driven steering test (about 50 s, needs RT_FFB_WHEEL, set automatically by the first-run calibration) and the HAND BRAKE step.

Put these files in this folder:

  941b02.chd        SHA1 943bc9b1ea7273a8382b94c8a75010dfe296df14 (internal CHD SHA1, see 'chdman info')
  941jab_nvram.u39  SHA1 2753dda42cdd81af22dc6780678f1ddeb3c62013

Also needed: roms/kviper/ (941b01.u25, ds2430.u3).
Check with: make check GAME=gticlub2

Dolphin 2606 ARM integer conformance reproducer (2026-10-05)

Build: bash wii/diagnostics/arm_integer/build.sh
Outputs: build/wii/arm-integer-conformance/op{0,1}-r{0,5}/probe.dol
op0 = addme; op1 = subfme. Uses ordinary libogc SDK PPC750 hard-float.

Probe receives x in r3, carry bit encoded in r4. mtxer establishes CA;
ori r6,r6,1 gives the analyzer an instruction before the dead destination overwrite.
addme/subfme writes DEST and mr returns the result in r3. No game source changes.
Five x values (0,1,7,ffffffff,80000000) times CA0/1 = ten cases per passing run.
Expected uint32 results: addme x-1+CA; subfme ~x-1+CA.
This tests result values, not outgoing XER flags or record/overflow variants.

Verified visually through CUA with installed /Applications/Dolphin.app2606,
arm64 CPUCore4 EmulationSpeed0. Default integer JIT unless specified:
 addme r0,r3: run.4BPbSY => BindForWrite line412 discarded-value assertion.
 addme r5,r3: run.kjwQcH => END PASS failures=0, all ten values OK.
 addme r0,r3 + JitIntegerOff=True: run.gMHik8 => END PASS failures=0.
 subfme r0,r3: run.nc5SMx => same BindForWrite412 assertion.
 subfme r5,r3: run.PsjrOw => END PASS failures=0, all ten values OK.
 subfme r0,r3 + JitIntegerOff=True: run.bmWMbX => END PASS failures=0.
Artifacts and launch settings retained in build/wii/runs/<run>/.
Probe hashes in build/wii/arm-integer-conformance/sha256.txt.
Disassembly in each op/destination build directory.

Source-level explanation:
Dolphin2606 JitArm64_Integer.cpp shared subfex/subfmex handler1220 and
addex/addmex handler1497 pass will_read=(d==a || d==b) to BindToRegister.
For mex instructions the encoded RB field is reserved zero and is not an input;
the handler uses literal -1 instead. Destination r0 therefore spuriously asks to
read old r0. PPCAnalyst correctly declares only RA input and RD output and can
discard old r0 before this write. This disagrees with the ARM handler request.
The two-line proposed correction is d==a || (!mex && d==b).

The adjacent dolphin-2606-mex-read.patch remains runtime-UNVALIDATED.
It is applied in build/tools/dolphin-2606-fix-checkout for a separate emulator
build. Installed Dolphin and the pristine source archive were not modified.
Do not claim a validated fix until both probes and the game checkpoint pass.

This reproduces the same assertion independently with minimal arithmetic.
Baseline Viper ELF has relevant addme r0,r10 at80054350 and addme r0,r30 at800559d4,
but the original Viper assertion guest PC was not captured. Thus the exact link
to the original failing block remains strongly supported, not directly proven.
No assertions were ignored. Each owned test was verified, kill -9 stopped,
and all Dolphin processes verified absent before starting another test.
Final owned PID14517 stopped and no Dolphin remained.

Separate custom emulator validation (2606 source, mex-read patch):
 build/tools/dolphin-2606-fix-build/Binaries/Dolphin.app
 ARM64 CPUCore4, normal integer JIT, Metal, EmulationSpeed0.
 addme r0 run.yoaQlc: visually END PASS failures=0, all ten cases OK.
 subfme r0 run.68Q9CN: visually END PASS failures=0, all ten cases OK.
 No assertions ignored in either probe. Both force-stopped before next run.
 Full baseline game run.uAsdly launched; checkpoint/RAM validation pending.
 Build uses SDK libbz2 instead of incompatible legacy framework and disables
 Vulkan (Metal remains available). These are local build configuration changes.

Game validation completed: run.uAsdly SCRIPTED END PASS, hash9730789c.
Full 16MiB RAM matches Intel baseline byte-for-byte. Normal integer JIT;
no assertions ignored. This validates the proposed fix within tested scope,
not every PowerPC instruction variant or the complete game.

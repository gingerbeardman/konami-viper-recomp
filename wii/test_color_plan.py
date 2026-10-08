"""Compare all decoded fields with bit definitions extracted from the reference."""
from pathlib import Path
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'runtime/voodoo/voodoo_regs.h').read_text()
section = source.split('class reg_fbz_colorpath', 1)[1].split('class reg_fbz_mode', 1)[0]
fields = re.findall(r'constexpr u32 (\w+)\(\) const\s*\{ return BIT\(m_value, (\d+), (\d+)\); \}', section)
assert len(fields) == 21
code = ['#include "wii/voodoo_color_plan.h"', '#include <assert.h>', '#include <stdio.h>',
        'int main(void){uint32_t r=1;for(unsigned n=0;n<1000000;n++){r^=r<<13;r^=r>>17;r^=r<<5;'
        'WiiVoodooColorPlan p=wii_voodoo_color_plan(r);const uint8_t *a=(const uint8_t*)&p;']
for index, (name, shift, width) in enumerate(fields):
    code.append(f'assert(a[{index}]==((r>>{shift})&{(1 << int(width)) - 1})); /* {name} */')
code.append('}puts("colour plan PASS 1000000 states, all21 reference fields");}')
with tempfile.TemporaryDirectory(prefix='viper-color-plan-') as directory:
    src = Path(directory) / 'test.c'
    binary = Path(directory) / 'test'
    src.write_text('\n'.join(code))
    subprocess.run(['clang', '-std=c11', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', '-I', str(root), str(src), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)

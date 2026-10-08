"""Extract the unchanged renderer fallback as an independent probe oracle."""
from pathlib import Path
import sys
root=Path(__file__).resolve().parent
source=(root/'gx_renderer.c').read_text()
anchor='#else\n        unsigned end=0;\n        for(int unit=1;unit>=0;unit--)if(tmu_pipeline.unit[unit].use){'
if source.count(anchor)!=1:raise SystemExit('Expected one unchanged TMU prefix reference loop')
start=source.index(anchor)+len('#else\n')
end=source.index('        if(end!=tmu_pipeline.fbi_first)',start)
body=source[start:end]
needle='if(!end)unsupported(v,cmd,"GX planned TMU stage budget");'
if body.count(needle)!=1:raise SystemExit('Expected one TMU budget check')
body=body.replace('tmu_pipeline.','p->').replace(needle,'if(!end)return 0;')
output=Path(sys.argv[1]);output.parent.mkdir(parents=True,exist_ok=True)
output.write_text('/* Generated from actual unchanged gx_renderer.c fallback loop. */\nstatic unsigned prefix_source_original(const WiiTMUPipelinePlan *p){\n'+body+'    return end;\n}\n')

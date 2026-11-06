"""Package only model sources and final artifacts, after checking their version."""
from pathlib import Path
import hashlib
import json
import struct
import zipfile

HERE=Path(__file__).resolve().parent
params=json.loads((HERE/'parameters.json').read_text(encoding='utf-8'))
report=json.loads((HERE/'exports'/'validation.json').read_text(encoding='utf-8'))
assert report['parameters_mm']==params, 'Regenerate CAD after changing parameters'
for name,part in report['parts'].items():
    raw=(HERE/'exports'/f'{name}.stl').read_bytes()
    triangles=struct.unpack_from('<I',raw,80)[0]
    assert len(raw)==84+triangles*50 and triangles==part['triangles']
    assert part['valid_solid'] and part['watertight_stl'] and part['solids']==1
for field in ('hinge_housing_collision_mm3','component_printed_part_intersections_mm3','printed_assembly_intersections_mm3'):
    assert all(abs(v)<1e-5 for v in report[field].values())
def include(path):
    return path.is_file() and '__pycache__' not in path.parts and path.suffix not in ('.blend1','.pyc','.zip') and path.name!='package_manifest.json'
files=sorted(path for path in HERE.rglob('*') if include(path))
manifest={str(path.relative_to(HERE)).replace('\\','/'):{'bytes':path.stat().st_size,'sha256':hashlib.sha256(path.read_bytes()).hexdigest()} for path in files}
manifest_path=HERE/'package_manifest.json'
manifest_path.write_text(json.dumps(manifest,indent=2),encoding='utf-8')
target=HERE.parent/'LapTimerPrototype_PETG_v1.zip'
with zipfile.ZipFile(target,'w',compression=zipfile.ZIP_DEFLATED,compresslevel=6) as archive:
    for path in files+[manifest_path]: archive.write(path,str(Path(HERE.name)/path.relative_to(HERE)))
with zipfile.ZipFile(target) as archive:
    assert archive.testzip() is None
print(f'{target}: {target.stat().st_size} bytes, {len(files)+1} files; validated')

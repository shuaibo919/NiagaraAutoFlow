import argparse
import hashlib
import json
import pathlib
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--engine-root', required=True, type=pathlib.Path)
ENGINE = parser.parse_args().engine_root.resolve()
version = json.loads((ENGINE / 'Engine/Build/Build.version').read_text(encoding='utf-8-sig'))
if (version['MajorVersion'], version['MinorVersion'], version['PatchVersion']) != (5, 8, 4):
    raise SystemExit('This patch recorder targets UE 5.8.4 only.')
PLUGIN = pathlib.Path(__file__).resolve().parents[1]
FILES = [
    'Engine/Plugins/FX/Niagara/Source/NiagaraEditor/Public/NiagaraScriptAuthoringUtilities.h',
    'Engine/Plugins/FX/Niagara/Source/NiagaraEditor/Private/NiagaraScriptAuthoringUtilities.cpp',
    'Engine/Plugins/FX/Niagara/Source/NiagaraEditor/Private/NiagaraFluidScriptAuthoring.inl',
]
out = PLUGIN / 'EnginePatches'
out.mkdir(exist_ok=True)
patch = out / 'UE5.8.4-NiagaraScriptAuthoring.diff'
parts = []
hashes = {}
for relative in FILES:
    content = (ENGINE / relative).read_text(encoding='utf-8').replace('\r\n', '\n')
    assert content.endswith('\n')
    lines = content.splitlines(keepends=True)
    hashes[relative] = hashlib.sha256(content.encode()).hexdigest()
    parts.extend([
        f'diff --git a/{relative} b/{relative}\n',
        'new file mode 100644\n',
        '--- /dev/null\n',
        f'+++ b/{relative}\n',
        f'@@ -0,0 +1,{len(lines)} @@\n',
        ''.join('+' + line for line in lines),
    ])
patch.write_text(''.join(parts), encoding='utf-8', newline='\n')
manifest = {
    'base_tag': '5.8.4-release',
    'base_commit': 'f45729c0b6f097038e72b52d366a62baeca3275b',
    'changes': 'Three added NiagaraEditor source files, including the typed data-interface and simulation-stage graph bridge; no original engine files modified.',
    'sha256_lf_utf8': hashes,
}
(out / 'UE5.8.4-NiagaraScriptAuthoring.manifest.json').write_text(
    json.dumps(manifest, indent=2) + '\n', encoding='utf-8', newline='\n')
subprocess.run(['git', 'apply', '--reverse', '--check', str(patch)], cwd=ENGINE, check=True)
with tempfile.TemporaryDirectory(prefix='naf-engine-patch-') as directory:
    subprocess.run(['git', 'apply', '--check', str(patch)], cwd=directory, check=True)
    subprocess.run(['git', 'apply', str(patch)], cwd=directory, check=True)
    for relative in FILES:
        actual = pathlib.Path(directory, relative).read_text(encoding='utf-8').replace('\r\n', '\n')
        assert hashlib.sha256(actual.encode()).hexdigest() == hashes[relative], relative
print('Patch reverse-check, clean apply and SHA-256 reconstruction passed (3 engine files).')

"""
Full regen of src/game/recomp/gen: seed, then translate until nothing seedable is missing.
usage: py -3 -m tools.recomp.regen <xbe>
"""
import subprocess
import sys
import tempfile
from pathlib import Path

xbe = sys.argv[1]
py = sys.executable


def run(*args):
    r = subprocess.run([py, "-m"] + list(args), capture_output=True, text=True, encoding="utf-8")
    if r.returncode:
        sys.exit(f"{' '.join(args)} failed:\n{r.stdout}\n{r.stderr}")
    return r.stdout


with tempfile.TemporaryDirectory(prefix="doa2u-regen-") as tmp:
    seed_list = str(Path(tmp) / "seed_list.tmp")
    for tool, arg in (("tools.recomp.seed_data_pointers", xbe),
                      ("tools.recomp.seed_immediates", xbe),
                      ("tools.recomp.seed_symbol_cache", "tools/recomp/doa2u_cxbx_symbols.ini")):
        print(run(tool, arg, seed_list).strip())
        print(run("tools.recomp.seed_missing_functions", seed_list).strip())

    for i in range(10):
        run("tools.recomp", xbe, "--all", "--split", "1000")
        out = run("tools.recomp.find_missing", seed_list)
        print(f"pass {i + 1}: {out.splitlines()[0]}")
        if "seedable=0" in out:
            break
        print(run("tools.recomp.seed_missing_functions", seed_list).strip())
    else:
        sys.exit("Regeneration did not converge after 10 passes")

print("Regeneration complete (no postprocessing required).")

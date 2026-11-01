# config/game.toml: xbe_symbols' output, then config/symbols.toml over it, then config/hooks.toml.
# Run from the repository root after xbe_symbols has written game.toml.
import re

cfg = open("config/game.toml", encoding="utf8").read().split("\n# ---- Tiger Woods")[0]
for line in open("config/symbols.toml", encoding="utf8"):
    m = re.match(r"(\w+)\s*=\s*(0x[0-9A-Fa-f]+)", line)
    if not m:
        continue
    row = f"{m[1]:<52} = {m[2]}"
    cfg, n = re.subn(rf"^{m[1]}\s*=.*$", row, cfg, flags=re.M)
    if not n:
        cfg = cfg.replace("[symbols]\n", f"[symbols]\n{row}\n", 1)
open("config/game.toml", "w", encoding="utf8", newline="\n").write(cfg.rstrip() + "\n" + open("config/hooks.toml", encoding="utf8").read())

import re, pathlib
root = pathlib.Path(__file__).resolve().parent.parent
h = (root / "cpp/MSCore/msc_contract.hpp").read_text()
cpp = {re.sub(r'(?<!^)(?=[A-Z])', '_', m.group(1)).lower(): m.group(2) for m in re.finditer(r'kCls(\w+)\s*=\s*"([^"]+)"', h)}
lua = (root / "mods/MSCore/Scripts/mscore_contract.lua").read_text()
blk = re.search(r'C\.classes\s*=\s*\{(.*?)\n\}', lua, re.S).group(1)
lm = dict(re.findall(r'(\w+)\s*=\s*"([^"]+)"', blk))
assert cpp == lm, (cpp, lm)
print("class contract ok", len(cpp))

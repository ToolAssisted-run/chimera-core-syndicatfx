#!/usr/bin/env python3
"""GetGameProperties, held to docs/game-cores.md ("The export").

The table must be JSON with a "properties" list, and every entry:
  - a unique name, a domain the core exposes (the run prints the sizes as
    domainSize[...]=N), an offset and a type the spec allows;
  - string and bytes with a length (and a known encoding for a string);
  - an array's count and stride, and every element inside its domain;
  - a bit field on an integer type, inside the value;
  - values only on integer types, naming numbers;
and no two entries may describe the same bit (bit fields may share a byte,
nothing may share a bit). The Game State block must be described byte for
byte: it is the properties, packed.

usage: check-properties.py <properties.json> <digest output with domainSize lines>
"""
import json
import re
import sys

INT = {"u8": 1, "s8": 1, "u16": 2, "s16": 2, "u32": 4, "s32": 4, "u64": 8, "s64": 8}
SIZES = dict(INT, f32=4, f64=8, bool=1)
ENCODINGS = {"ascii", "latin1", "utf8", "utf16le"}
FIELDS = {"name", "domain", "offset", "type", "length", "encoding", "count", "stride", "endian",
          "bit", "bits", "group", "values", "writable", "description"}

table = json.load(open(sys.argv[1]))
domains = {m.group(1): int(m.group(2)) for m in re.finditer(r"^domainSize\[(.+)\]=(\d+)$", open(sys.argv[2]).read(), re.M)}
props = table.get("properties")
if not isinstance(props, list) or not props:
    sys.exit("no properties list")


def fail(name, why):
    sys.exit(f"{name}: {why}")


names = set()
owner = {}  # (domain, byte, bit) -> name
elements = 0
kinds = {}
for p in props:
    name = p.get("name")
    if not isinstance(name, str) or not name:
        sys.exit(f"an entry without a name: {p}")
    if name in names:
        fail(name, "named twice")
    names.add(name)
    if set(p) - FIELDS:
        fail(name, f"unknown fields {sorted(set(p) - FIELDS)}")
    dom, off, typ = p.get("domain"), p.get("offset"), p.get("type")
    if dom not in domains:
        fail(name, f"domain {dom!r} is not one the core exposes ({sorted(domains)})")
    if not isinstance(off, int) or off < 0:
        fail(name, f"offset {off!r}")
    if typ in ("string", "bytes"):
        size = p.get("length")
        if not isinstance(size, int) or size < 1:
            fail(name, "a string or bytes needs a length")
        if typ == "string" and p.get("encoding", "ascii") not in ENCODINGS:
            fail(name, f"encoding {p.get('encoding')!r}")
    elif typ in SIZES:
        size = SIZES[typ]
        if "length" in p or "encoding" in p:
            fail(name, "length/encoding on a " + typ)
    else:
        fail(name, f"type {typ!r} is not one the spec allows")
    if p.get("endian", "little") not in ("little", "big"):
        fail(name, f"endian {p.get('endian')!r}")
    count, stride = p.get("count", 1), p.get("stride", size)
    if not isinstance(count, int) or count < 1 or not isinstance(stride, int) or stride < size:
        fail(name, f"count {count!r} / stride {stride!r} (element size {size})")
    bits_of_value = list(range(size * 8))
    if "bit" in p or "bits" in p:
        if typ not in INT:
            fail(name, "a bit field on a " + typ)
        bit, nbits = p.get("bit", 0), p.get("bits")
        if not isinstance(bit, int) or not isinstance(nbits, int) or bit < 0 or nbits < 1 or bit + nbits > size * 8:
            fail(name, f"bit {bit!r} bits {nbits!r} do not fit a {typ}")
        bits_of_value = list(range(bit, bit + nbits))
    if "values" in p:
        v = p["values"]
        if typ not in INT or not isinstance(v, dict) or not v or not all(
                isinstance(k, str) and re.fullmatch(r"-?\d+", k) and isinstance(t, str) for k, t in v.items()):
            fail(name, f"values must name numbers of an integer type: {v}")
    for key, kind in (("group", str), ("description", str), ("writable", bool)):
        if key in p and not isinstance(p[key], kind):
            fail(name, f"{key} is not a {kind.__name__}")
    big = p.get("endian") == "big"
    for i in range(count):
        base = off + i * stride
        if base + size > domains[dom]:
            fail(name, f"element {i} ends at {base + size}, past {dom} ({domains[dom]} bytes)")
        for b in bits_of_value:
            byte = base + (size - 1 - b // 8 if big else b // 8)
            key = (dom, byte, b % 8)
            if key in owner:
                fail(name, f"shares {dom}:{byte} bit {b % 8} with {owner[key]}")
            owner[key] = name
    elements += count
    kind = "bit field" if "bits" in p else typ
    kinds[kind] = kinds.get(kind, 0) + 1

gaps = [b for b in range(domains["Game State"]) if any((("Game State", b, k) not in owner) for k in range(8))]
if gaps:
    sys.exit(f"Game State has bytes no property describes: {gaps[:8]}")
for must in ("Steps", "Turns", "Mission", "Level.Seed"):
    if must not in names:
        sys.exit(f"missing {must}")
if set(table) - {"properties"}:
    sys.exit(f"unknown top-level fields {sorted(set(table) - {'properties'})}")
arrays = sum(1 for p in props if p.get("count", 1) > 1)
print(f"{len(props)} properties ({arrays} arrays, {elements} values; "
      + ", ".join(f"{n} {k}" for k, n in sorted(kinds.items()))
      + f") over {len(domains)} domains, no bit described twice; Game State described byte for byte ({domains['Game State']} bytes)")

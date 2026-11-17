#!/usr/bin/env python3
"""Compile real GeoNames China cities, Chinese aliases and a fixed-size sample."""
import argparse
import csv
import hashlib
import io
import json
from pathlib import Path
import re
import unicodedata
import zipfile


def rows(path, name):
    with zipfile.ZipFile(path) as archive, archive.open(name) as source:
        yield from csv.reader(io.TextIOWrapper(source, encoding="utf-8"), delimiter="\t")


def normal(value):
    value = unicodedata.normalize("NFKC", value).casefold().strip()
    if value.endswith("市"):
        value = value[:-1]
    return value


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--input", required=True, type=Path)
    p.add_argument("--output", required=True, type=Path)
    p.add_argument("--sample-size", type=int, choices=(350, 1000), default=350)
    args = p.parse_args()
    args.output.mkdir(parents=True, exist_ok=False)
    provinces = {r[10]: {"id": int(r[0]), "name": r[1]} for r in rows(args.input / "CN.zip", "CN.txt")
                 if len(r) >= 19 and r[6:9] == ["A", "ADM1", "CN"]}
    cities = {}
    for r in rows(args.input / "cities500.zip", "cities500.txt"):
        if len(r) < 19 or r[8] != "CN":
            continue
        cities[int(r[0])] = {"id": str(r[0]), "geoname_id": int(r[0]), "name": r[1],
                             "latitude": float(r[4]), "longitude": float(r[5]),
                             "province_code": r[10], "province": provinces.get(r[10], {}).get("name"),
                             "population": int(r[14] or 0), "feature": r[7], "modified": r[18],
                             "aliases": sorted(set([r[1], r[2], *r[3].split(",")]) - {""})}
    province_ids = {p["id"]: p for p in provinces.values()}
    chinese = {}
    for r in rows(args.input / "CN-alternates.zip", "CN.txt"):
        if len(r) < 5 or not r[1].isdigit():
            continue
        identity = int(r[1])
        if identity not in cities and identity not in province_ids:
            continue
        if r[2] in ("zh", "zh-CN", "zh-Hans") and re.search(r"[\u3400-\u9fff]", r[3]):
            score = (r[4] == "1", r[2] == "zh-Hans", len(r[3]))
            chinese.setdefault(identity, []).append((score, r[3]))
            if identity in cities:
                cities[identity]["aliases"] = sorted(set(cities[identity]["aliases"] + [r[3]]))
    for city in cities.values():
        names = chinese.get(city["geoname_id"], [])
        city["chinese_name"] = max(names)[1] if names else None
        province = provinces.get(city["province_code"])
        if province:
            aliases = chinese.get(province["id"], [])
            city["province_chinese"] = max(aliases)[1] if aliases else None
    ordered = sorted(cities.values(), key=lambda c: (-c["population"], c["id"]))
    changsha = cities[1815577]
    selected = [changsha] + [c for c in ordered if c["id"] != changsha["id"]][:args.sample_size - 1]
    provenance = {"source": "https://download.geonames.org/export/dump/", "license": "CC-BY-4.0",
                  "attribution": "GeoNames, https://www.geonames.org/; filtered and compiled for this test",
                  "sha256": {n: hashlib.sha256((args.input / n).read_bytes()).hexdigest()
                             for n in ("cities500.zip", "CN.zip", "CN-alternates.zip")}}
    for name, values in (("cities-" + str(args.sample_size) + ".json", selected), ("china-city-catalog.json", ordered)):
        with (args.output / name).open("x", encoding="utf-8") as f:
            json.dump({"provenance": provenance, "cities": values}, f, ensure_ascii=False, indent=2)
    aliases = {}
    for city in ordered:
        for alias in city["aliases"]:
            aliases.setdefault(normal(alias), set()).add(city["id"])
    ambiguous = [(name, sorted(ids)) for name, ids in aliases.items()
                 if re.fullmatch(r"[\u3400-\u9fff]{2,5}", name) and len(ids) > 1]
    report = {"city_count": len(ordered), "sample_city_count": len(selected),
              "chinese_named_count": sum(bool(c["chinese_name"]) for c in ordered),
              "sample_chinese_named_count": sum(bool(c["chinese_name"]) for c in selected),
              "ambiguous_chinese_alias_count": len(ambiguous), "ambiguity_examples": ambiguous[:10],
              "sample_unique_coordinates": len({(c["latitude"], c["longitude"]) for c in selected}),
              "chinese_names_without_language_tag_are_not_assumed_simplified": True}
    (args.output / "city-catalog-report.json").write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, ensure_ascii=False))


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""Evaluate downloaded offline datasets against public RIPE reference metadata."""
import argparse
import collections
import hashlib
import ipaddress
import json
import math
import re
import statistics
from pathlib import Path

from location_candidates import CityCatalog, IpDatabase, normal, public_ip, trusted_client_ip


def distance(a, b, c, d):
    lat1, lat2 = math.radians(a), math.radians(c)
    x = math.sin((lat2 - lat1) / 2) ** 2 + math.cos(lat1) * math.cos(lat2) * math.sin(math.radians(d - b) / 2) ** 2
    return 6371.0088 * 2 * math.asin(min(1, math.sqrt(x)))


def reference_exclusion(item, coordinates):
    """Conservative public-reference screen, not a national boundary dataset."""
    country = item.get("country_code", item.get("country"))
    if country != "CN":
        return "reference_country_not_CN"
    if normal(item.get("city") or "") in ("hong kong", "hongkong", "香港", "macao", "macau", "澳门"):
        return "reference_city_HK_MO"
    lon, lat = coordinates
    if 113.8 <= lon <= 114.5 and 22.1 <= lat <= 22.56:
        return "HK_or_border_uncertain_coordinates"
    if 113.45 <= lon <= 113.65 and 22.05 <= lat <= 22.3:
        return "MO_or_border_uncertain_coordinates"
    if 119.8 <= lon <= 122.1 and 21.8 <= lat <= 25.5:
        return "TW_or_border_uncertain_coordinates"
    if not (73 <= lon <= 135.2 and 18 <= lat <= 54):
        return "outside_mainland_candidate_envelope"
    return None


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--catalog", required=True, type=Path)
    p.add_argument("--database", required=True, type=Path)
    p.add_argument("--anchors", required=True, type=Path)
    p.add_argument("--probes", required=True, type=Path)
    p.add_argument("--output", required=True, type=Path)
    args = p.parse_args()
    if args.output.exists():
        p.error("Evidence must use a new output path")
    catalog = CityCatalog(args.catalog)
    ambiguity = []
    for alias, entries in catalog.by_alias.items():
        if not re.fullmatch(r"[\u3400-\u9fff]{2,8}", alias):
            continue
        if len(entries) > 1 and len({c["province_code"] for c in entries.values()}) > 1:
            for city in entries.values():
                province = city.get("province_chinese") or city["province_code"]
                narrowed = catalog.resolve(alias, province)
                if narrowed["status"] == "found":
                    ambiguity.append({"query": alias, "without_province": catalog.resolve(alias)["status"],
                                      "province_code": city["province_code"], "with_province": narrowed["status"],
                                      "province_query": province,
                                      "selected": narrowed["candidates"][0]["id"]})
                    break
        if len(ambiguity) >= 5:
            break
    unknown = catalog.resolve("不存在的测试城市weather43")
    assert ambiguity and all(x["without_province"] == "ambiguous" and x["with_province"] == "found" for x in ambiguity)
    assert unknown == {"status": "unknown", "candidates": []}
    header_cases = [
        ("111.13.100.92", "8.8.8.8", [], "111.13.100.92"),
        ("172.26.0.5", "8.8.8.8, 111.13.100.92", ["172.26.0.5/32"], "111.13.100.92"),
        ("172.26.0.5", "bad-ip", ["172.26.0.5/32"], None),
        ("::ffff:111.13.100.92", "8.8.8.8", [], "111.13.100.92"),
    ]
    for peer, forwarded, trusted, expected in header_cases:
        assert trusted_client_ip(peer, forwarded, trusted) == expected
    database = IpDatabase(args.database)
    boundary_cases = ["127.0.0.1", "192.168.168.114", "100.64.0.1", "198.51.100.5", "::1", "fe80::1", "fc00::1",
                      "2001:db8::1", "not-an-ip", "224.0.0.1", "ff02::1", "255.255.255.255", "0.0.0.0"]
    boundaries = {value: database.lookup(value) for value in boundary_cases}
    assert all(value["status"] == "unknown" and value["city"] is None for value in boundaries.values())
    references, excluded, seen = [], [], set()
    for category, path in (("anchor", args.anchors), ("probe", args.probes)):
        payload = json.loads(path.read_text())
        for item in payload["results"]:
            coordinates = (item.get("geometry") or {}).get("coordinates")
            if not coordinates or len(coordinates) != 2:
                continue
            if category == "probe" and item.get("is_public") is False:
                continue
            if category == "anchor" and item.get("is_disabled"):
                continue
            for version, field in ((4, "ip_v4" if category == "anchor" else "address_v4"),
                                   (6, "ip_v6" if category == "anchor" else "address_v6")):
                address = item.get(field)
                if not address or public_ip(address) is None:
                    continue
                result = database.lookup(address)
                row = {"reference_type": category, "reference_id": item["id"], "ip_version": version,
                       "ip": address, "reference_coordinates": coordinates, "reference_city": item.get("city"),
                       "reference_country": item.get("country_code", item.get("country")),
                       "lookup": result}
                if result.get("latitude") is not None and result.get("longitude") is not None:
                    row["distance_km"] = distance(coordinates[1], coordinates[0], result["latitude"], result["longitude"])
                exclusion = reference_exclusion(item, coordinates)
                if (category, version, str(public_ip(address))) in seen:
                    exclusion = exclusion or "duplicate_address_in_reference_category"
                seen.add((category, version, str(public_ip(address))))
                if exclusion:
                    excluded.append(dict(row, exclusion_reason=exclusion))
                else:
                    references.append(row)
    summary = {}
    for category in ("anchor", "probe"):
        for version in (4, 6):
            rows = [r for r in references if r["reference_type"] == category and r["ip_version"] == version]
            found = [r for r in rows if r["lookup"]["status"] == "estimated"]
            summary[category + "_ipv" + str(version)] = {
                "samples": len(rows), "city_results": len(found),
                "country_cn": sum(r["lookup"].get("country") == "CN" for r in rows),
                "within_50km": sum(r.get("distance_km", math.inf) <= 50 for r in rows),
                "within_100km": sum(r.get("distance_km", math.inf) <= 100 for r in rows),
                "median_km_for_found": statistics.median(r["distance_km"] for r in found) if found else None}
    report = {"scope": "offline_candidate_evaluation", "database_sha256": hashlib.sha256(args.database.read_bytes()).hexdigest(),
              "database_metadata": {"ip_version": database.reader.metadata().ip_version,
                                    "build_epoch": database.reader.metadata().build_epoch,
                                    "database_type": database.reader.metadata().database_type},
              "catalog_count": len(catalog.cities), "ambiguity_cases": ambiguity, "unknown_city": unknown,
              "untrusted_header_and_proxy_chain_cases_passed": len(header_cases), "non_public_ip_boundaries": boundaries,
              "reference_summary": summary, "samples": references,
              "excluded_references": excluded,
              "reference_screen": "CN tag plus conservative HK/MO/TW/border boxes; not a national polygon survey",
              "limitations": ["RIPE coordinates are operator-published references, not a field survey",
                              "Probe coordinates may be rounded; anchors and probes are reported separately",
                              "CN query returned Hong Kong anchors; these and uncertain border references are excluded",
                              "Sample is not random and does not estimate nationwide or consumer-network accuracy",
                              "IP city guesses remain correctable by an explicit manual city selection",
                              "IPv4/IPv6 database support is not a guarantee of city coverage"]}
    database.close()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("x", encoding="utf-8") as output:
        json.dump(report, output, ensure_ascii=False, indent=2)
    print(json.dumps({"reference_summary": summary, "ambiguity_tests": len(ambiguity),
                      "boundary_tests": len(boundaries), "database_metadata": report["database_metadata"]}))


if __name__ == "__main__":
    main()

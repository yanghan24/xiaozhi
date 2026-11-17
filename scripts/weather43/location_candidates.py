"""Offline candidate evaluation helpers; no online geolocation fallback."""
import ipaddress
import json
from pathlib import Path
import unicodedata


def normal(value):
    value = unicodedata.normalize("NFKC", value).casefold().strip()
    for suffix in ("省", "市"):
        if value.endswith(suffix):
            value = value[:-len(suffix)]
    return value


class CityCatalog:
    def __init__(self, path):
        self.cities = json.loads(Path(path).read_text(encoding="utf-8"))["cities"]
        self.by_alias = {}
        for city in self.cities:
            for alias in city["aliases"]:
                self.by_alias.setdefault(normal(alias), {})[city["id"]] = city

    def resolve(self, query, province=None):
        candidates = list(self.by_alias.get(normal(query), {}).values())
        if province:
            candidates = [c for c in candidates if normal(province) in {
                normal(c.get("province") or ""), normal(c.get("province_chinese") or ""), c["province_code"]}]
        return {"status": "found" if len(candidates) == 1 else ("ambiguous" if candidates else "unknown"),
                "candidates": [{k: c.get(k) for k in ("id", "name", "chinese_name", "province", "province_chinese",
                                                      "latitude", "longitude")} for c in candidates]}


def public_ip(value):
    try:
        address = ipaddress.ip_address(value)
        address = address.ipv4_mapped if isinstance(address, ipaddress.IPv6Address) and address.ipv4_mapped else address
        usable = (address.is_global and not address.is_multicast and not address.is_reserved and
                  not address.is_unspecified and not address.is_loopback and not address.is_link_local)
        return str(address) if usable else None
    except ValueError:
        return None


def trusted_client_ip(peer, forwarded, trusted):
    """Walk from the observed peer backwards; ignore headers from untrusted peers."""
    try:
        address = ipaddress.ip_address(peer)
        networks = [ipaddress.ip_network(value) for value in trusted]
        if not any(address in network for network in networks):
            return public_ip(peer)
        chain = (forwarded or "").split(",")
        if len(chain) > 16:
            return None
        for value in reversed(chain):
            address = ipaddress.ip_address(value.strip())
            if not any(address in network for network in networks):
                return public_ip(str(address))
        return None
    except ValueError:
        return None


class IpDatabase:
    def __init__(self, path):
        import maxminddb
        self.reader = maxminddb.open_database(str(path), mode=maxminddb.MODE_MMAP)

    def lookup(self, value):
        normalized = public_ip(value)
        if normalized is None:
            return {"status": "unknown", "reason": "non_public_or_invalid", "city": None}
        result = self.reader.get(normalized)
        if not result:
            return {"status": "unknown", "reason": "not_in_database", "city": None}
        country = result.get("country", {}).get("iso_code")
        if country != "CN":
            return {"status": "outside_scope", "country": country, "city": None}
        names = result.get("city", {}).get("names", {})
        region = (result.get("subdivisions") or [{}])[0]
        location = result.get("location", {})
        return {"status": "estimated" if names and "latitude" in location else "unknown",
                "country": country, "city": names.get("zh-CN") or names.get("en"),
                "english_city": names.get("en"), "region": region.get("names", {}).get("en"),
                "latitude": location.get("latitude"), "longitude": location.get("longitude"),
                "ip_version": ipaddress.ip_address(normalized).version, "source": "DB-IP City Lite",
                "accuracy": "coarse_ip_estimate"}

    def close(self):
        self.reader.close()

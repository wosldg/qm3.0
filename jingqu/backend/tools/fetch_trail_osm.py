#!/usr/bin/env python3
"""离线构建工具：从 Overpass 拉取森林公园全域 OSM 步道，纠偏后生成 data/trail_osm.json。

仅用于构建期，不进入后端运行时依赖。
- OSM 原始坐标为 WGS-84，入库前统一转换为 GCJ-02（高德底图坐标系）。
- 按 OSM node id 去重，共用节点自然形成岔路口。
- 丢弃 access=private / area=yes 的面要素；highway=track 在查询阶段已排除。
"""

import json
import math
import time
import urllib.parse
import urllib.request
from datetime import date

BBOX = (29.30, 110.43, 29.42, 110.55)  # south, west, north, east
QUERY = (
    "[out:json][timeout:300];\n"
    'way["highway"~"^(path|footway|steps)$"]'
    "(%s,%s,%s,%s);\n"
    "out body geom;\n"
) % BBOX

MIRRORS = [
    "https://overpass-api.de/api/interpreter",
    "https://kumi.systems/api/interpreter",
    "https://overpass.osm.jp/api/interpreter",
    "https://overpass.openstreetmap.ru/api/interpreter",
    "https://overpass.private.coffee/api/interpreter",
]

OUT_PATH = "backend/data/trail_osm.json"

PI = math.pi
A = 6378245.0
EE = 0.00669342162296594323


def _out_of_china(lng, lat):
    return not (73.66 < lng < 135.05 and 3.86 < lat < 53.55)


def _t_lat(x, y):
    r = -100.0 + 2.0 * x + 3.0 * y + 0.2 * y * y + 0.1 * x * y + 0.2 * math.sqrt(abs(x))
    r += (20.0 * math.sin(6.0 * x * PI) + 20.0 * math.sin(2.0 * x * PI)) * 2.0 / 3.0
    r += (20.0 * math.sin(y * PI) + 40.0 * math.sin(y / 3.0 * PI)) * 2.0 / 3.0
    r += (160.0 * math.sin(y / 12.0 * PI) + 320 * math.sin(y * PI / 30.0)) * 2.0 / 3.0
    return r


def _t_lng(x, y):
    r = 300.0 + x + 2.0 * y + 0.1 * x * x + 0.1 * x * y + 0.1 * math.sqrt(abs(x))
    r += (20.0 * math.sin(6.0 * x * PI) + 20.0 * math.sin(2.0 * x * PI)) * 2.0 / 3.0
    r += (20.0 * math.sin(x * PI) + 40.0 * math.sin(x / 3.0 * PI)) * 2.0 / 3.0
    r += (150.0 * math.sin(x / 12.0 * PI) + 300.0 * math.sin(x / 30.0 * PI)) * 2.0 / 3.0
    return r


def wgs84_to_gcj02(lng, lat):
    if _out_of_china(lng, lat):
        return lng, lat
    dlat = _t_lat(lng - 105.0, lat - 35.0)
    dlng = _t_lng(lng - 105.0, lat - 35.0)
    radlat = lat / 180.0 * PI
    magic = 1 - EE * math.sin(radlat) ** 2
    sqrtmagic = math.sqrt(magic)
    dlat = (dlat * 180.0) / ((A * (1 - EE)) / (magic * sqrtmagic) * PI)
    dlng = (dlng * 180.0) / (A / sqrtmagic * math.cos(radlat) * PI)
    return lng + dlng, lat + dlat


def fetch():
    body = urllib.parse.urlencode({"data": QUERY}).encode()
    last_error = None
    for attempt in range(1, 4):
        for mirror in MIRRORS:
            try:
                print("fetch attempt %d mirror %s" % (attempt, mirror), flush=True)
                req = urllib.request.Request(
                    mirror,
                    data=body,
                    headers={"User-Agent": "scenicflow-trail-import/1.0"},
                )
                with urllib.request.urlopen(req, timeout=300) as resp:
                    payload = json.loads(resp.read().decode("utf-8"))
                if payload.get("elements"):
                    return payload
                last_error = "empty elements from %s" % mirror
            except Exception as exc:  # noqa: BLE001 - offline tool, report and retry
                last_error = "%s: %s" % (mirror, exc)
                print("  failed: %s" % last_error, flush=True)
        if attempt < 3:
            print("  backoff 20s", flush=True)
            time.sleep(20)
    raise RuntimeError("Overpass unavailable: %s" % last_error)


def _round(value):
    return round(float(value), 7)


def convert(payload):
    nodes = {}   # osm node id -> {"code", "lng", "lat"}
    edges = {}   # (from_code, to_code) -> {"from", "to", "oneWay"}

    for way in payload["elements"]:
        if way.get("type") != "way":
            continue
        tags = way.get("tags", {})
        if tags.get("access") == "private" or tags.get("area") == "yes":
            continue
        geometry = way.get("geometry") or []
        node_ids = way.get("nodes") or []
        if len(geometry) < 2:
            continue

        oneway_tag = str(tags.get("oneway", "")).lower()
        one_way = 1 if oneway_tag in ("yes", "1", "true") else 0
        if tags.get("highway") == "steps":
            one_way = 0   # 台阶一律按双向处理

        for index, point in enumerate(geometry):
            raw_id = node_ids[index] if index < len(node_ids) else None
            if raw_id is None:
                raw_id = "coord/%.7f,%.7f" % (point["lat"], point["lon"])
            code = "osm/%s" % raw_id
            if code not in nodes:
                lng, lat = wgs84_to_gcj02(point["lon"], point["lat"])
                nodes[code] = {"code": code, "lng": _round(lng), "lat": _round(lat)}

        for index in range(len(geometry) - 1):
            a_code = "osm/%s" % (node_ids[index] if index < len(node_ids) else
                                 "coord/%.7f,%.7f" % (geometry[index]["lat"], geometry[index]["lon"]))
            b_code = "osm/%s" % (node_ids[index + 1] if index + 1 < len(node_ids) else
                                 "coord/%.7f,%.7f" % (geometry[index + 1]["lat"], geometry[index + 1]["lon"]))
            if a_code == b_code:
                continue
            key = tuple(sorted((a_code, b_code)))
            if key in edges:
                continue
            edges[key] = {"from": a_code, "to": b_code, "oneWay": one_way}

    return list(nodes.values()), list(edges.values())


def main():
    payload = fetch()
    nodes, edges = convert(payload)
    result = {
        "meta": {
            "source": "OpenStreetMap",
            "license": "ODbL",
            "generated": date.today().isoformat(),
            "bbox": [BBOX[1], BBOX[0], BBOX[3], BBOX[2]],
        },
        "nodes": nodes,
        "edges": edges,
    }
    with open(OUT_PATH, "w", encoding="utf-8") as handle:
        json.dump(result, handle, ensure_ascii=False, separators=(",", ":"))
    print("wrote %s: %d nodes / %d edges" % (OUT_PATH, len(nodes), len(edges)))


if __name__ == "__main__":
    main()

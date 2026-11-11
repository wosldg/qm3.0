#!/usr/bin/env python3
"""离线构建工具：从现有 trail_node / trail_edge 抽取成熟游览路线轨迹。

产物 backend/data/tracks.json，供后端 seedTracks() 导入 route_track / track_point。
- 每条路线 = 两关键节点之间的最短路（遵循已建步道，不走野路）
- Douglas-Peucker 抽稀到 8 m，关键节点（出入口/岔路口/观景台）强制保留
- 为岔路口生成方向提示（直行/左转/右转 + 下一关键点 + 剩余距离）
"""

import argparse
import heapq
import json
import math
import sqlite3
from datetime import date

DB_PATH = "backend/data/scenicflow.db"
OUT_PATH = "backend/data/tracks.json"
EARTH_R = 6371000.0
DP_EPS_M = 8.0

# name, start anchor, end anchor, description
TRACKS = [
    ("东门入园 · 金鞭溪经典线", "东门入口", "千里相会",
     "东门入园后经观景环线岔路口进入金鞭溪主步道，沿途溪流与峰林景观。"),
    ("东门入园 · 大氧吧广场线", "东门入口", "大氧吧广场",
     "从东门沿主步道步行至大氧吧广场，坡度平缓，适合亲子与老年游客。"),
    ("金鞭溪入口 · 溪谷观景线", "金鞭溪入口", "溪边观景台",
     "金鞭溪入口沿溪谷步道至溪边观景台，亲水路段，注意台阶湿滑。"),
    ("东门入园 · 黄石寨观景线", "东门入口", "黄石寨观景台",
     "东门经主步道转入黄石寨方向，登顶后可俯瞰峰林全景。"),
]


def haversine(lng1, lat1, lng2, lat2):
    phi1 = math.radians(lat1)
    phi2 = math.radians(lat2)
    dphi = math.radians(lat2 - lat1)
    dlmb = math.radians(lng2 - lng1)
    h = math.sin(dphi / 2) ** 2 + math.cos(phi1) * math.cos(phi2) * math.sin(dlmb / 2) ** 2
    return 2.0 * EARTH_R * math.asin(min(1.0, math.sqrt(h)))


def meters_per_degree(lat):
    return 111320.0, 111320.0 * math.cos(math.radians(lat))


def point_segment_distance(p, a, b):
    """局部等距投影下的点到线段距离（米），用于抽稀判定。"""
    lat = (a[1] + b[1]) / 2.0
    mx, my = meters_per_degree(lat)
    ax, ay = a[0] * mx, a[1] * my
    bx, by = b[0] * mx, b[1] * my
    px, py = p[0] * mx, p[1] * my
    dx, dy = bx - ax, by - ay
    length_sq = dx * dx + dy * dy
    if length_sq <= 1e-9:
        t = 0.0
    else:
        t = ((px - ax) * dx + (py - ay) * dy) / length_sq
        t = max(0.0, min(1.0, t))
    qx, qy = ax + t * dx, ay + t * dy
    return math.hypot(px - qx, py - qy)


def bearing(lng1, lat1, lng2, lat2):
    phi1 = math.radians(lat1)
    phi2 = math.radians(lat2)
    dl = math.radians(lng2 - lng1)
    y = math.sin(dl) * math.cos(phi2)
    x = math.cos(phi1) * math.sin(phi2) - math.sin(phi1) * math.cos(phi2) * math.cos(dl)
    return (math.degrees(math.atan2(y, x)) + 360.0) % 360.0


class Graph:
    def __init__(self, conn):
        self.nodes = {}
        for row in conn.execute(
            "SELECT id,node_code,name,node_type,longitude,latitude,note,image_url"
            " FROM trail_node WHERE status=1"
        ):
            nid, code, name, ntype, lng, lat, note, image = row
            self.nodes[nid] = {
                "id": nid, "code": code, "name": name or "", "type": ntype,
                "lng": lng, "lat": lat, "note": note or "", "image": image or "",
            }
        self.adj = {}
        for row in conn.execute(
            "SELECT id,start_node_id,end_node_id,one_way,source FROM trail_edge WHERE status=1"
        ):
            eid, start, end, one_way, source = row
            if start not in self.nodes or end not in self.nodes:
                continue
            length = haversine(self.nodes[start]["lng"], self.nodes[start]["lat"],
                               self.nodes[end]["lng"], self.nodes[end]["lat"])
            # 手工骨架是直线插值的近似几何，给 1.6 倍权重，让选线优先走 OSM 精细轨迹，
            # 只有在 OSM 无覆盖（如东门至观景环线岔路口）时才回退到手工主干。
            weight = length * (1.6 if source == "manual" else 1.0)
            self.adj.setdefault(start, []).append((end, weight))
            if not one_way:
                self.adj.setdefault(end, []).append((start, weight))

    def shortest_path(self, source, target):
        dist = {source: 0.0}
        prev = {}
        heap = [(0.0, source)]
        while heap:
            current_dist, node = heapq.heappop(heap)
            if node == target:
                break
            if current_dist > dist.get(node, math.inf):
                continue
            for neighbor, length in self.adj.get(node, []):
                candidate = current_dist + length
                if candidate < dist.get(neighbor, math.inf):
                    dist[neighbor] = candidate
                    prev[neighbor] = node
                    heapq.heappush(heap, (candidate, neighbor))
        if target not in dist:
            return None
        path = [target]
        while path[-1] != source:
            path.append(prev[path[-1]])
        path.reverse()
        return path


def douglas_peucker(points, start_index, end_index):
    """返回保留的点在 points 中的索引序列（含两端）。"""
    if end_index - start_index <= 1:
        return [start_index, end_index]
    a = (points[start_index][0], points[start_index][1])
    b = (points[end_index][0], points[end_index][1])
    max_distance = -1.0
    max_index = start_index
    for i in range(start_index + 1, end_index):
        d = point_segment_distance((points[i][0], points[i][1]), a, b)
        if d > max_distance:
            max_distance = d
            max_index = i
    if max_distance > DP_EPS_M:
        left = douglas_peucker(points, start_index, max_index)
        right = douglas_peucker(points, max_index, end_index)
        return left[:-1] + right
    return [start_index, end_index]


def build_track(graph, name, start_name, end_name, description):
    by_name = {}
    for node in graph.nodes.values():
        if node["name"]:
            by_name.setdefault(node["name"], []).append(node)
    if start_name not in by_name or end_name not in by_name:
        return None, f"anchor missing: {start_name} or {end_name}"
    start = by_name[start_name][0]["id"]
    end = by_name[end_name][0]["id"]
    path = graph.shortest_path(start, end)
    if not path:
        return None, f"no path between {start_name} and {end_name}"

    coords = [(graph.nodes[nid]["lng"], graph.nodes[nid]["lat"]) for nid in path]
    # 关键点索引：端点与非普通步道点，抽稀时必须保留
    key_indices = [0]
    for index, nid in enumerate(path):
        if graph.nodes[nid]["type"] != "PATH" and index not in (0, len(path) - 1):
            key_indices.append(index)
    key_indices.append(len(path) - 1)

    kept_indices = []
    for i in range(len(key_indices) - 1):
        piece = douglas_peucker(coords, key_indices[i], key_indices[i + 1])
        kept_indices.extend(piece if not kept_indices else piece[1:])

    total_length = 0.0
    cumulative = {}
    for i, nid in enumerate(path):
        if i > 0:
            prev = path[i - 1]
            total_length += haversine(graph.nodes[prev]["lng"], graph.nodes[prev]["lat"],
                                      graph.nodes[nid]["lng"], graph.nodes[nid]["lat"])
        cumulative[i] = total_length

    points = []
    for seq, index in enumerate(kept_indices):
        node = graph.nodes[path[index]]
        point_type = node["type"] if node["type"] != "PATH" else "PATH"
        hint = ""
        if point_type == "JUNCTION":
            incoming = path[max(0, index - 1)]
            outgoing = path[min(len(path) - 1, index + 1)]
            prev_node = graph.nodes[incoming]
            next_node = graph.nodes[outgoing]
            angle = (bearing(prev_node["lng"], prev_node["lat"], node["lng"], node["lat"]) -
                     bearing(node["lng"], node["lat"], next_node["lng"], next_node["lat"]))
            angle = (angle + 180.0) % 360.0 - 180.0
            if abs(angle) < 25.0:
                turn = "直行"
            elif angle < 0:
                turn = "左转"
            else:
                turn = "右转"
            # 下一个命名的关键点，用于提示文案
            target_name = ""
            target_index = index
            for j in range(index + 1, len(path)):
                candidate = graph.nodes[path[j]]
                if candidate["type"] != "PATH" and candidate["name"]:
                    target_name = candidate["name"]
                    target_index = j
                    break
            remain = cumulative[len(path) - 1] - cumulative[index]
            if target_name:
                remain = cumulative[target_index] - cumulative[index]
            remain_text = f"{remain / 1000.0:.1f}km" if remain >= 1000 else f"{int(round(remain))}m"
            suffix = f" → {target_name} {remain_text}" if target_name else f" {remain_text}"
            hint = f"{turn}{suffix}"
        points.append({
            "seq": seq,
            "longitude": round(node["lng"], 7),
            "latitude": round(node["lat"], 7),
            "pointType": point_type,
            "name": node["name"],
            "directionHint": hint,
            "imageUrl": node["image"],
        })

    return {
        "name": name,
        "description": description,
        "totalLengthM": round(total_length, 1),
        "estimatedMinutes": max(1, int(round(total_length / 66.7))),
        "pointCount": len(points),
        "points": points,
    }, None


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--names", action="store_true", help="列出可用于起终点的关键节点")
    args = parser.parse_args()

    conn = sqlite3.connect(DB_PATH)
    graph = Graph(conn)

    if args.names:
        for node in graph.nodes.values():
            if node["type"] != "PATH":
                print(f"{node['type']:9s} {node['name']}")
        return

    result = []
    for name, start, end, description in TRACKS:
        track, error = build_track(graph, name, start, end, description)
        if error:
            print(f"skip {name}: {error}")
            continue
        result.append(track)
        print(f"built {name}: {track['totalLengthM']} m / {track['pointCount']} pts / "
              f"{sum(1 for p in track['points'] if p['pointType'] == 'JUNCTION')} junctions")

    if not result:
        raise SystemExit("no track built; check anchor names with --names")

    payload = {
        "meta": {"source": "trail graph", "generated": date.today().isoformat()},
        "tracks": result,
    }
    with open(OUT_PATH, "w", encoding="utf-8") as handle:
        json.dump(payload, handle, ensure_ascii=False, separators=(",", ":"))
    print(f"wrote {OUT_PATH}: {len(result)} tracks")


if __name__ == "__main__":
    main()

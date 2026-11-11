// 步道几何纯函数：距离、线段投影、步道吸附、路线进度投影。
// 全部使用 GCJ-02 经纬度，可直接喂给高德地图。
import type { TrailEdge, TrailGeoPoint, TrailNode } from '@/api'

const EARTH_RADIUS_M = 6371000
const DEG_TO_RAD = Math.PI / 180
const METERS_PER_DEG_LAT = 111000
const METERS_PER_DEG_LNG_AT = (lat: number) => 111000 * Math.cos(lat * DEG_TO_RAD)

/** haversine 球面距离（米） */
export function haversine(lng1: number, lat1: number, lng2: number, lat2: number): number {
  const phi1 = lat1 * DEG_TO_RAD
  const phi2 = lat2 * DEG_TO_RAD
  const dPhi = (lat2 - lat1) * DEG_TO_RAD
  const dLambda = (lng2 - lng1) * DEG_TO_RAD
  const h =
    Math.sin(dPhi / 2) ** 2 + Math.cos(phi1) * Math.cos(phi2) * Math.sin(dLambda / 2) ** 2
  return 2 * EARTH_RADIUS_M * Math.asin(Math.min(1, Math.sqrt(h)))
}

export interface SegmentProjection {
  /** 投影点在 A→B 上的参数（0..1） */
  t: number
  longitude: number
  latitude: number
  /** 点到线段的距离（米） */
  distanceM: number
}

/**
 * 点 P 到线段 AB 的最近点。局部投影到米制平面上计算，
 * 距离用 haversine 复核，避免经纬度在投影时的尺度失真。
 */
export function pointToSegment(
  pLng: number,
  pLat: number,
  aLng: number,
  aLat: number,
  bLng: number,
  bLat: number,
): SegmentProjection {
  const metersPerLng = METERS_PER_DEG_LNG_AT(pLat)
  const ax = (aLng - pLng) * metersPerLng
  const ay = (aLat - pLat) * METERS_PER_DEG_LAT
  const bx = (bLng - pLng) * metersPerLng
  const by = (bLat - pLat) * METERS_PER_DEG_LAT

  const dx = bx - ax
  const dy = by - ay
  const lengthSq = dx * dx + dy * dy
  let t = 0
  if (lengthSq > 0) {
    t = -(ax * dx + ay * dy) / lengthSq
    t = Math.max(0, Math.min(1, t))
  }

  const longitude = aLng + (bLng - aLng) * t
  const latitude = aLat + (bLat - aLat) * t
  return { t, longitude, latitude, distanceM: haversine(pLng, pLat, longitude, latitude) }
}

export interface SnapResult {
  /** 定位点到步道的距离（米） */
  distanceM: number
  /** 吸附后的坐标 */
  longitude: number
  latitude: number
  edgeId: number
  /** true 表示吸附到了 ENTRANCE / JUNCTION 节点坐标（锚点校准） */
  anchored: boolean
}

/** 岔路口 / 出入口锚点校准半径：进入该范围直接贴到节点上，避免岔路口抖动 */
const ANCHOR_RADIUS_M = 50

/**
 * 在整张步道网上找最近点。
 * - 距 ENTRANCE / JUNCTION 节点 50m 内 → 直接吸附到节点坐标
 * - 否则吸附到最近线段的垂足（是否真的采用由页面按 30/50m 分区决定）
 */
export function snapToTrail(
  lng: number,
  lat: number,
  edges: TrailEdge[],
  nodeMap: Map<number, TrailNode>,
): SnapResult | null {
  let best: SnapResult | null = null

  for (const edge of edges) {
    const a = nodeMap.get(edge.startNodeId)
    const b = nodeMap.get(edge.endNodeId)
    if (!a || !b) continue
    const projection = pointToSegment(lng, lat, a.longitude, a.latitude, b.longitude, b.latitude)
    if (!best || projection.distanceM < best.distanceM) {
      best = {
        distanceM: projection.distanceM,
        longitude: projection.longitude,
        latitude: projection.latitude,
        edgeId: edge.edgeId,
        anchored: false,
      }
    }
  }

  if (best) {
    for (const node of nodeMap.values()) {
      if (node.nodeType !== 'ENTRANCE' && node.nodeType !== 'JUNCTION') continue
      const d = haversine(lng, lat, node.longitude, node.latitude)
      if (d <= ANCHOR_RADIUS_M && d < best.distanceM) {
        best = {
          distanceM: d,
          longitude: node.longitude,
          latitude: node.latitude,
          edgeId: best.edgeId,
          anchored: true,
        }
      }
    }
  }
  return best
}

export interface PathProjection {
  /** 沿折线累计已走距离（米） */
  alongM: number
  /** 到折线的垂直距离（米） */
  distanceM: number
  segmentIndex: number
  t: number
  /** 折线上的投影点坐标（吸附目标） */
  longitude: number
  latitude: number
}

/** 把定位点投影到路线折线上，用于计算已走 / 剩余距离 */
export function projectOntoPath(
  lng: number,
  lat: number,
  path: TrailGeoPoint[],
): PathProjection | null {
  if (path.length < 2) return null

  let cumulative = 0
  let best: PathProjection | null = null
  for (let i = 0; i + 1 < path.length; i += 1) {
    const a = path[i]
    const b = path[i + 1]
    const projection = pointToSegment(lng, lat, a.longitude, a.latitude, b.longitude, b.latitude)
    const segmentLength = haversine(a.longitude, a.latitude, b.longitude, b.latitude)
    if (!best || projection.distanceM < best.distanceM) {
      best = {
        alongM: cumulative + segmentLength * projection.t,
        distanceM: projection.distanceM,
        segmentIndex: i,
        t: projection.t,
        longitude: projection.longitude,
        latitude: projection.latitude,
      }
    }
    cumulative += segmentLength
  }
  return best
}

/** 折线总长度（米） */
export function pathLength(path: TrailGeoPoint[]): number {
  let total = 0
  for (let i = 0; i + 1 < path.length; i += 1) {
    total += haversine(
      path[i].longitude,
      path[i].latitude,
      path[i + 1].longitude,
      path[i + 1].latitude,
    )
  }
  return total
}

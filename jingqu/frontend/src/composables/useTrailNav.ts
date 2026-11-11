// 步道导航核心：给定一条路线（节点序列 + 岔路提示），负责
// 定位吸附（三分区）、行进进度、岔路提示滞回状态机、到达检测。
// 与路线来源无关：成熟轨迹 / 自由规划 / 多途经点拼接路线都复用这套逻辑。
import { computed, ref, type Ref } from 'vue'
import { ElMessage } from 'element-plus'
import type { TrailGeoPoint, TrailHint, TrailNetwork, TrailNode, TrailRouteResult } from '@/api'
import { haversine, pointToSegment, snapToTrail, type PathProjection } from '@/utils/trailGeo'
import type { TrailPosition } from './useTrailLocation'

/** 吸附三分区阈值 */
const SNAP_FULL_M = 30
const SNAP_EDGE_M = 50
/** 岔路提示滞回区间：< 100m 触发，> 120m 复位 */
const HINT_TRIGGER_M = 100
const HINT_RESET_M = 120

export interface UseTrailNavOptions {
  /** 当前导航路线；null = 未导航 */
  route: Ref<TrailRouteResult | null>
  /** 全路网（未导航时的全网吸附回退）；不加载全路网的页面传 ref(null) 即可 */
  network: Ref<TrailNetwork | null>
  /** 到达终点回调（页面通常在这里停止定位） */
  onArrived?: () => void
}

export function useTrailNav({ route, network, onArrived }: UseTrailNavOptions) {
  const snappedPoint = ref<TrailGeoPoint | null>(null)
  const rawPoint = ref<TrailGeoPoint | null>(null)
  const guidance = ref('')
  const guidanceLevel = ref<'warning' | 'error'>('warning')
  const activeHint = ref<TrailHint | null>(null)
  const remainM = ref<number | null>(null)
  const walkedM = ref(0)
  const arrived = ref(false)
  const junctionStates = new Map<number, 'ARMED' | 'TRIGGERED'>()

  const nodeMap = computed(() => {
    const map = new Map<number, TrailNode>()
    network.value?.nodes.forEach((node) => map.set(node.nodeId, node))
    return map
  })

  const remainText = computed(() => `${Math.max(0, Math.round(remainM.value ?? 0))} m`)
  const remainMinutes = computed(() => Math.max(0, Math.round((remainM.value ?? 0) / 66.7)))
  const progressPercent = computed(() => {
    if (!route.value || route.value.totalDistanceM <= 0) return 0
    return Math.min(100, Math.round((walkedM.value / route.value.totalDistanceM) * 100))
  })

  function resetNavigation() {
    walkedM.value = 0
    remainM.value = null
    arrived.value = false
    guidance.value = ''
    activeHint.value = null
    junctionStates.clear()
    for (const node of route.value?.nodes ?? []) {
      if (node.nodeType === 'JUNCTION') junctionStates.set(node.nodeId, 'ARMED')
    }
  }

  function setGuidance(message: string, level: 'warning' | 'error') {
    guidance.value = message
    guidanceLevel.value = level
  }

  // 往返/近距重叠路段：只在当前进度前后开窗投影，避免最近点跳到对向腿
  const WINDOW_BACK_M = 150
  const WINDOW_AHEAD_M = 400
  let cachedPath: TrailGeoPoint[] | null = null
  let segStartM: number[] = []
  let segLengthM: number[] = []

  function ensureSegments(path: TrailGeoPoint[]) {
    if (cachedPath === path) return
    cachedPath = path
    segStartM = []
    segLengthM = []
    let cumulative = 0
    for (let i = 0; i + 1 < path.length; i += 1) {
      const a = path[i]
      const b = path[i + 1]
      const len = haversine(a.longitude, a.latitude, b.longitude, b.latitude)
      segStartM.push(cumulative)
      segLengthM.push(len)
      cumulative += len
    }
  }

  function projectNearProgress(
    lng: number,
    lat: number,
    path: TrailGeoPoint[],
  ): PathProjection | null {
    if (path.length < 2) return null
    ensureSegments(path)
    const lo = Math.max(0, walkedM.value - WINDOW_BACK_M)
    const hi = walkedM.value + WINDOW_AHEAD_M
    let best: PathProjection | null = null
    for (let i = 0; i < segStartM.length; i += 1) {
      if (segStartM[i] + segLengthM[i] < lo || segStartM[i] > hi) continue
      const a = path[i]
      const b = path[i + 1]
      const proj = pointToSegment(lng, lat, a.longitude, a.latitude, b.longitude, b.latitude)
      if (!best || proj.distanceM < best.distanceM) {
        best = {
          alongM: segStartM[i] + segLengthM[i] * proj.t,
          distanceM: proj.distanceM,
          segmentIndex: i,
          t: proj.t,
          longitude: proj.longitude,
          latitude: proj.latitude,
        }
      }
    }
    return best
  }

  /**
   * 两级吸附：
   * - 导航中只投影当前路线，天然不会吸到别的路。
   * - 未导航且已加载全路网时，才回退到全网吸附。
   */
  function handlePosition(p: TrailPosition) {
    const current = route.value

    if (current && current.nodes.length >= 2) {
      const projection = projectNearProgress(p.longitude, p.latitude, current.nodes)
      if (projection) {
        if (projection.distanceM <= SNAP_FULL_M) {
          snappedPoint.value = { longitude: projection.longitude, latitude: projection.latitude }
          rawPoint.value = null
          guidance.value = ''
          updateProgress(projection)
          updateJunctionHints()
          return
        }
        if (projection.distanceM <= SNAP_EDGE_M) {
          snappedPoint.value = { longitude: projection.longitude, latitude: projection.latitude }
          rawPoint.value = { longitude: p.longitude, latitude: p.latitude }
          setGuidance('您正在路线边缘，请向路线中心线靠拢', 'warning')
          updateProgress(projection)
          updateJunctionHints()
          return
        }

        const net = network.value
        const globalSnap = net
          ? snapToTrail(p.longitude, p.latitude, net.edges, nodeMap.value)
          : null
        if (net && globalSnap && globalSnap.distanceM <= SNAP_FULL_M) {
          snappedPoint.value = { longitude: globalSnap.longitude, latitude: globalSnap.latitude }
          rawPoint.value = { longitude: p.longitude, latitude: p.latitude }
          setGuidance('偏离当前路线，您在另一条步道上', 'warning')
        } else {
          snappedPoint.value = { longitude: p.longitude, latitude: p.latitude }
          rawPoint.value = null
          setGuidance('您已偏离游览路线，请返回', 'error')
        }
        updateJunctionHints()
        return
      }
    }

    // 未导航：仅在已加载全路网时做全网吸附
    const net = network.value
    if (!net) return
    const snap = snapToTrail(p.longitude, p.latitude, net.edges, nodeMap.value)
    if (!snap) return

    if (snap.distanceM <= SNAP_FULL_M) {
      snappedPoint.value = { longitude: snap.longitude, latitude: snap.latitude }
      rawPoint.value = null
      guidance.value = ''
    } else if (snap.distanceM <= SNAP_EDGE_M) {
      snappedPoint.value = { longitude: snap.longitude, latitude: snap.latitude }
      rawPoint.value = { longitude: p.longitude, latitude: p.latitude }
      setGuidance('您正在步道边缘，请向步道中心线靠拢', 'warning')
    } else {
      snappedPoint.value = { longitude: p.longitude, latitude: p.latitude }
      rawPoint.value = null
      setGuidance('已偏离正规游览步道，请返回', 'error')
    }

    updateProgress()
    updateJunctionHints()
  }

  function updateProgress(projection?: PathProjection) {
    const current = route.value
    const point = snappedPoint.value
    if (!current || !point) return
    const projected =
      projection ?? projectNearProgress(point.longitude, point.latitude, current.nodes)
    if (!projected) return

    // 往返段几何重叠时最近投影可能跳到对向腿，进度只增不减避免来回跳
    walkedM.value = Math.max(walkedM.value, Math.round(projected.alongM))
    const remaining = Math.max(0, current.totalDistanceM - walkedM.value)
    const rounded = Math.round(remaining)
    // 1 秒平滑 + 单调递减，避免数字来回跳动
    remainM.value = remainM.value === null
      ? rounded
      : Math.min(remainM.value, Math.round(remainM.value * 0.7 + rounded * 0.3))

    if (remaining <= 15) {
      if (!arrived.value) {
        arrived.value = true
        ElMessage.success('已到达终点')
        onArrived?.()
      }
      guidance.value = ''
    }
  }

  function updateJunctionHints() {
    const current = route.value
    const point = snappedPoint.value
    if (!current || !point) return

    for (const hint of current.hints) {
      const node = current.nodes.find((n) => n.nodeId === hint.nodeId)
      if (!node) continue
      const distance = haversine(point.longitude, point.latitude, node.longitude, node.latitude)
      const state = junctionStates.get(hint.nodeId) ?? 'ARMED'
      if (distance < HINT_TRIGGER_M && state === 'ARMED') {
        activeHint.value = hint
        junctionStates.set(hint.nodeId, 'TRIGGERED')
      } else if (distance > HINT_RESET_M && state === 'TRIGGERED') {
        junctionStates.set(hint.nodeId, 'ARMED')
      }
    }
  }

  return {
    snappedPoint,
    rawPoint,
    guidance,
    guidanceLevel,
    activeHint,
    remainM,
    remainText,
    remainMinutes,
    walkedM,
    arrived,
    progressPercent,
    resetNavigation,
    handlePosition,
  }
}

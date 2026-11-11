// 定位通道：真实定位（高德 Geolocation）+ 模拟定位（演示主路径）。
// 两路输出统一为 { longitude, latitude, accuracy, source, timestamp }，
// 并在出口做"单次跳点丢弃 + 连续两次接受"的滤波，避免 Marker 被噪声带跑。
import { onUnmounted, ref } from 'vue'
import type { TrailGeoPoint } from '@/api'
import { haversine } from '@/utils/trailGeo'

export interface TrailPosition {
  longitude: number
  latitude: number
  accuracy: number
  source: 'real' | 'sim'
  timestamp: number
}

/** 真实定位刷新间隔 */
const REAL_INTERVAL_MS = 3000
/** 模拟定位：1 秒 1 个点 */
const SIM_INTERVAL_MS = 1000
/** 模拟步行速度 1.2 m/s */
const SIM_SPEED_MPS = 1.2
const METERS_PER_DEG_LAT = 111000
const metersPerDegLng = (lat: number) => 111000 * Math.cos((lat * Math.PI) / 180)

/** 抖动注入：每 20 秒一次，横向偏移 40~50 米（落在"步道边缘"区间，
 *  用于验证吸附与滤波，同时保证 Marker 始终贴在步道上），持续 3 帧 */
const JITTER_INTERVAL_TICKS = 20
const JITTER_MIN_M = 40
const JITTER_MAX_M = 50
const JITTER_FRAMES = 3

/** 跳点判定：3 秒内位移超过 30 米必然不是步行（3 秒正常步幅约 3.6 米） */
const JUMP_DISTANCE_M = 30
const JUMP_WINDOW_MS = 3000
/** 连续两次跳变则接受新位置并重置基准，防止 Marker 永久冻结 */
const JUMP_ACCEPT_ATTEMPTS = 2
/** PC 浏览器 IP 定位精度通常以公里计，超过该值视为不可用 */
const UNUSABLE_ACCURACY_M = 200

export function useTrailLocation() {
  const position = ref<TrailPosition | null>(null)
  const source = ref<'idle' | 'real' | 'sim'>('idle')
  const notice = ref('')

  let realTimer: number | null = null
  let simTimer: number | null = null
  let lastValid: TrailPosition | null = null
  let jumpAttempts = 0
  let finishHandler: (() => void) | null = null

  function resetFilter() {
    lastValid = null
    jumpAttempts = 0
  }

  /** 滤波后写入 position；返回是否被采用 */
  function accept(raw: TrailPosition): boolean {
    if (!lastValid) {
      lastValid = raw
      position.value = raw
      return true
    }
    const elapsed = raw.timestamp - lastValid.timestamp
    const moved = haversine(lastValid.longitude, lastValid.latitude, raw.longitude, raw.latitude)
    if (moved > JUMP_DISTANCE_M && elapsed < JUMP_WINDOW_MS) {
      jumpAttempts += 1
      if (jumpAttempts < JUMP_ACCEPT_ATTEMPTS) return false
    }
    jumpAttempts = 0
    lastValid = raw
    position.value = raw
    return true
  }

  function stopTimers() {
    if (realTimer !== null) {
      window.clearInterval(realTimer)
      realTimer = null
    }
    if (simTimer !== null) {
      window.clearInterval(simTimer)
      simTimer = null
    }
  }

  function stop() {
    stopTimers()
    source.value = 'idle'
  }

  /** 真实定位；返回是否成功启动 */
  function startReal(): boolean {
    stop()
    resetFilter()
    notice.value = ''

    if (!window.AMap?.Geolocation) {
      notice.value = '当前浏览器不支持高德定位，已切换演示模式'
      return false
    }

    const geolocation = new window.AMap.Geolocation({
      enableHighAccuracy: true,
      timeout: 10000,
      GeoLocationFirst: true,
    })

    let fallbackDone = false
    const fallback = (message: string) => {
      if (fallbackDone) return
      fallbackDone = true
      stopTimers()
      source.value = 'idle'
      notice.value = message
    }

    const poll = () => {
      geolocation.getCurrentPosition((status: string, result: any) => {
        if (status !== 'complete' || !result?.position) {
          fallback('当前设备定位不可用，已切换演示模式')
          return
        }
        const accuracy = Number(result.accuracy ?? UNUSABLE_ACCURACY_M + 1)
        if (accuracy > UNUSABLE_ACCURACY_M) {
          fallback('当前定位精度不足，已切换演示模式')
          return
        }
        const lngLat = result.position
        accept({
          longitude: Number(lngLat.getLng ? lngLat.getLng() : lngLat.lng),
          latitude: Number(lngLat.getLat ? lngLat.getLat() : lngLat.lat),
          accuracy,
          source: 'real',
          timestamp: Date.now(),
        })
      })
    }

    source.value = 'real'
    poll()
    realTimer = window.setInterval(poll, REAL_INTERVAL_MS)
    notice.value = '正在使用设备定位，请保持在开阔地带'
    return true
  }

  function samplePath(path: TrailGeoPoint[], cumulative: number[], distance: number) {
    let index = 0
    while (index + 2 < path.length && cumulative[index + 1] < distance) index += 1
    const segmentLength = cumulative[index + 1] - cumulative[index]
    const t = segmentLength > 0 ? (distance - cumulative[index]) / segmentLength : 0
    const a = path[index]
    const b = path[index + 1]
    return {
      longitude: a.longitude + (b.longitude - a.longitude) * t,
      latitude: a.latitude + (b.latitude - a.latitude) * t,
      segmentIndex: index,
    }
  }

  /** 模拟定位：沿路线折线按 speed（默认 1.2 m/s）前进，每 20 秒注入一次横向抖动 */
  function startSim(path: TrailGeoPoint[], options?: { jitter?: boolean; speed?: number }): boolean {
    stop()
    resetFilter()
    notice.value = '演示模式：模拟沿步道步行'
    if (path.length < 2) return false

    const cumulative: number[] = [0]
    for (let i = 0; i + 1 < path.length; i += 1) {
      cumulative.push(
        cumulative[i] +
          haversine(
            path[i].longitude,
            path[i].latitude,
            path[i + 1].longitude,
            path[i + 1].latitude,
          ),
      )
    }
    const total = cumulative[cumulative.length - 1]
    const speed = options?.speed ?? SIM_SPEED_MPS
    let travelled = 0
    let tick = 0
    let jitterLeft = 0
    let jitterLng = 0
    let jitterLat = 0

    source.value = 'sim'
    const emit = (raw: TrailPosition) => accept(raw)

    const step = () => {
      travelled = Math.min(total, travelled + (speed * SIM_INTERVAL_MS) / 1000)
      const point = samplePath(path, cumulative, travelled)
      tick += 1

      if (options?.jitter !== false && tick % JITTER_INTERVAL_TICKS === 0) {
        // 垂直于行进方向注入 40~80 米横向偏移
        const a = path[point.segmentIndex]
        const b = path[point.segmentIndex + 1]
        const dx = (b.longitude - a.longitude) * metersPerDegLng(point.latitude)
        const dy = (b.latitude - a.latitude) * METERS_PER_DEG_LAT
        const norm = Math.hypot(dx, dy) || 1
        const magnitude = JITTER_MIN_M + Math.random() * (JITTER_MAX_M - JITTER_MIN_M)
        const sign = Math.random() < 0.5 ? -1 : 1
        jitterLng = (sign * -dy / norm) * magnitude / metersPerDegLng(point.latitude)
        jitterLat = (sign * dx / norm) * magnitude / METERS_PER_DEG_LAT
        jitterLeft = JITTER_FRAMES
      }

      let longitude = point.longitude
      let latitude = point.latitude
      if (jitterLeft > 0) {
        longitude += jitterLng
        latitude += jitterLat
        jitterLeft -= 1
      }

      emit({ longitude, latitude, accuracy: 15, source: 'sim', timestamp: Date.now() })

      if (travelled >= total) {
        stopTimers()
        source.value = 'idle'
        if (finishHandler) finishHandler()
      }
    }

    emit({
      longitude: path[0].longitude,
      latitude: path[0].latitude,
      accuracy: 15,
      source: 'sim',
      timestamp: Date.now(),
    })
    simTimer = window.setInterval(step, SIM_INTERVAL_MS)
    return true
  }

  function onArrive(handler: () => void) {
    finishHandler = handler
  }

  onUnmounted(() => {
    stopTimers()
  })

  return { position, source, notice, startReal, startSim, stop, onArrive }
}

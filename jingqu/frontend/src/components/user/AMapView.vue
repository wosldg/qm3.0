<template>
  <div class="amap-wrap" :style="{ height }">
    <div ref="mapRef" class="amap"></div>
    <div v-if="loadFailed" class="map-fallback">
      <div class="fallback-content">
        <div class="fallback-icon">🗺️</div>
        <div class="fallback-text">高德地图加载失败</div>
        <div class="fallback-hint">请检查网络连接后刷新页面</div>
        <button class="retry-btn" @click="retryLoad">重新加载</button>
      </div>
    </div>
  </div>
</template>

<script setup lang="ts">
import { ref, onMounted, onUnmounted, watch } from 'vue'
import type { MapMarker } from '@/api'
import { getLevelColor } from '@/utils/level'

// 高德地图通用组件：底图 + 点位标记 + 路线折线
// 地图中心 / 缩放按需求固定在武陵源景区内部
const MAP_CENTER: [number, number] = [110.466, 29.346]
const MAP_ZOOM = 15

const props = withDefaults(
  defineProps<{
    points: MapMarker[]
    routePath?: MapMarker[]
    lineColor?: string
    focusId?: string
    height?: string
  }>(),
  { routePath: () => [], lineColor: '#67C23A', focusId: '', height: '420px' },
)

const emit = defineEmits<{ 'marker-click': [marker: MapMarker] }>()

const mapRef = ref<HTMLDivElement>()
const loadFailed = ref(false)

let map: any = null
let markers: any[] = []
let polyline: any = null
let infoWindow: any = null

// 类型配色：闸机红、停车场蓝、景点绿
function markerColor(m: MapMarker): string {
  if (m.type === 'gate') return '#F56C6C'
  if (m.type === 'parking') return '#409EFF'
  return '#67C23A'
}

function typeText(m: MapMarker): string {
  if (m.type === 'gate') return '闸机'
  if (m.type === 'parking') return '停车场'
  return '景点'
}

// 弹窗 HTML：名称、类型、人数、拥挤度 / 剩余车位
function infoHtml(m: MapMarker): string {
  const rows = [`<div><b>${m.name}</b></div>`, `<div>类型：${typeText(m)}</div>`]
  if (m.visitors !== undefined) rows.push(`<div>当前人数：${m.visitors} 人</div>`)
  if (m.levelText) {
    rows.push(
      `<div>拥挤度：<span style="color:#fff;background:${getLevelColor(m.level || 'NORMAL')};` +
        `padding:1px 6px;border-radius:3px;">${m.levelText}</span></div>`,
    )
  }
  if (m.freeSpaces !== undefined) rows.push(`<div>剩余车位：${m.freeSpaces} 个</div>`)
  return `<div style="font-size:13px;line-height:1.7;color:#303133;min-width:150px;">${rows.join('')}</div>`
}

function renderMarkers() {
  if (!map) return
  markers.forEach((m) => map.remove(m))
  markers = []

  for (const p of props.points) {
    if (!p.longitude || !p.latitude) continue
    const color = markerColor(p)
    const marker = new window.AMap.Marker({
      position: [p.longitude, p.latitude],
      title: p.name,
      anchor: 'center',
      content:
        `<div style="width:14px;height:14px;border-radius:50%;background:${color};` +
        `border:2px solid #fff;box-shadow:0 1px 4px rgba(0,0,0,.45);"></div>`,
    })
    marker.on('click', () => {
      if (!infoWindow) infoWindow = new window.AMap.InfoWindow({ offset: new window.AMap.Pixel(0, -12) })
      infoWindow.setContent(infoHtml(p))
      infoWindow.open(map, [p.longitude, p.latitude])
      emit('marker-click', p)
    })
    map.add(marker)
    markers.push(marker)
  }
}

// 路线折线：先清旧线再画新线，并自动缩放适配整条路线
function renderRoute() {
  console.log('[AMapView] renderRoute called, path length:', props.routePath?.length)
  if (!map) return
  if (polyline) {
    map.remove(polyline)
    polyline = null
  }
  const path = props.routePath
  console.log('[AMapView] routePath type:', typeof props.routePath, Array.isArray(props.routePath))
  console.log('[AMapView] routePath[0]:', props.routePath?.[0])
  const filtered = (props.routePath || []).filter((p) => p && p.longitude && p.latitude)
  console.log('[AMapView] filtered count:', filtered.length, 'from', (props.routePath || []).length)
  if (filtered.length > 0) {
    console.log('[AMapView] filtered[0]:', filtered[0])
  }
  const mapped = filtered.map((p) => [p.longitude, p.latitude])
  console.log('[AMapView] polyline path:', mapped.length, 'points')
  console.log('[AMapView] first 5 coords:', mapped.slice(0, 5))
  console.log('[AMapView] coords 12-15:', mapped.slice(12, 15))
  console.log('[AMapView] coords 99-103:', mapped.slice(99, 103))
  if (mapped.length < 2) return
  const lons = mapped.map((p) => p[0])
  const lats = mapped.map((p) => p[1])
  console.log('[AMapView] lng range:', Math.min(...lons).toFixed(8), '~', Math.max(...lons).toFixed(8), 'delta:', (Math.max(...lons) - Math.min(...lons)).toFixed(8))
  console.log('[AMapView] lat range:', Math.min(...lats).toFixed(8), '~', Math.max(...lats).toFixed(8), 'delta:', (Math.max(...lats) - Math.min(...lats)).toFixed(8))

  polyline = new window.AMap.Polyline({
    path: mapped,
    strokeColor: props.lineColor,
    strokeWeight: 6,
    strokeOpacity: 0.85,
    lineJoin: 'round',
    showDir: true,
  })
  map.add(polyline)
  map.setFitView([polyline], false, [60, 60, 60, 60], 16)
}

// 路线列表点击某个景点后，地图定位到该标记点
function focusMarker(id: string) {
  if (!map || !id) return
  const p = props.points.find((x) => x.id === id)
  if (!p || !p.longitude) return
  map.setZoomAndCenter(16, [p.longitude, p.latitude])
}

function initMap() {
  if (!window.AMap || !mapRef.value) {
    loadFailed.value = true
    return
  }
  map = new window.AMap.Map(mapRef.value, {
    zoom: MAP_ZOOM,
    center: MAP_CENTER,
    viewMode: '2D',
    resizeEnable: true,
  })
  map.addControl(new window.AMap.Scale())
  map.addControl(new window.AMap.ToolBar({ position: 'RB' }))
  renderMarkers()
  renderRoute()
}

onMounted(() => {
  // 高德脚本在 index.html 中同步加载，网络慢时轮询等待
  // 最多等待 3 秒，超时后显示降级提示
  let tries = 0
  const timer = window.setInterval(() => {
    if (window.AMap) {
      window.clearInterval(timer)
      initMap()
    } else if (++tries > 30) {
      window.clearInterval(timer)
      loadFailed.value = true
    }
  }, 100)
})

// 暴露方法供父组件调用以手动触发地图初始化
defineExpose({ initMap })

// 重新加载地图
function retryLoad() {
  loadFailed.value = false
  if (map) {
    map.destroy()
    map = null
  }
  markers = []
  polyline = null
  initMap()
}

// 监听 props 变化，确保地图初始化后也能渲染路线
watch(() => props.points, (newVal) => {
  if (map && newVal?.length) renderMarkers()
}, { deep: true })

onUnmounted(() => {
  if (map) map.destroy()
  map = null
})

watch(() => props.points, renderMarkers, { deep: true })
watch(() => props.routePath, renderRoute, { deep: true })
watch(() => props.lineColor, renderRoute)
watch(() => props.focusId, focusMarker)
</script>

<style scoped>
.amap-wrap {
  position: relative;
  width: 100%;
}

.amap {
  width: 100%;
  height: 100%;
}

.map-fallback {
  position: absolute;
  inset: 0;
  display: flex;
  align-items: center;
  justify-content: center;
  background: #f5f7fa;
}

.fallback-content {
  text-align: center;
  padding: 20px;
}

.fallback-icon {
  font-size: 48px;
  margin-bottom: 12px;
}

.fallback-text {
  font-size: 16px;
  font-weight: 600;
  color: #303133;
  margin-bottom: 8px;
}

.fallback-hint {
  font-size: 13px;
  color: #909399;
  margin-bottom: 16px;
}

.retry-btn {
  padding: 8px 24px;
  background: #409eff;
  color: #fff;
  border: none;
  border-radius: 4px;
  font-size: 14px;
  cursor: pointer;
  transition: background 0.2s;
}

.retry-btn:hover {
  background: #66b1ff;
}
</style>

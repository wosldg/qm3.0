<template>
  <div class="trail-map-wrap" :style="{ height }">
    <div ref="mapRef" class="trail-map"></div>
    <button v-if="position" class="locate-btn" type="button" @click="locateMe">定位到我</button>
    <div class="map-attribution">© OpenStreetMap contributors</div>
    <div v-if="loadFailed" class="map-fallback">高德地图加载失败，请检查网络后刷新</div>
  </div>
</template>

<script setup lang="ts">
import { ref, onMounted, onUnmounted, watch } from 'vue'
import type { TrailEdge, TrailGeoPoint, TrailNetwork, TrailNode } from '@/api'

const MAP_CENTER: [number, number] = [110.466, 29.346]
const MAP_ZOOM = 15

/** 一条合并后的折线链：把度为 1/2 的节点串起来，一条链只画一次，
 *  避免上千条边各画两层导致数千个对象。 */
interface TrailChain {
  path: [number, number][]
  source: 'manual' | 'osm'
}

const props = withDefaults(
  defineProps<{
    network?: TrailNetwork | null
    routePath?: TrailGeoPoint[]
    /** 吸附后的定位点（蓝色） */
    position?: TrailGeoPoint | null
    /** 未吸附的真实位置（灰色半透明，仅偏离时显示） */
    rawPosition?: TrailGeoPoint | null
    height?: string
  }>(),
  {
    network: null,
    routePath: () => [],
    position: null,
    rawPosition: null,
    height: '460px',
  },
)

const emit = defineEmits<{ 'map-ready': []; 'node-click': [node: TrailNode] }>()

const mapRef = ref<HTMLDivElement>()
const loadFailed = ref(false)

let map: any = null
let infoWindow: any = null
let trailPolylines: any[] = []
let nodeMarkers: any[] = []
let routePolyline: any = null
let positionMarker: any = null
let rawPositionMarker: any = null
let fittedNetwork = false
let mergedChains: TrailChain[] = []
let renderedBucket = -1

function nodeColor(node: TrailNode): string {
  if (node.nodeType === 'ENTRANCE') return '#F56C6C'
  if (node.nodeType === 'JUNCTION') return '#E6A23C'
  return '#8B5CF6'
}

function markerContent(node: TrailNode): string {
  const color = nodeColor(node)
  const shape =
    node.nodeType === 'JUNCTION'
      ? `<svg width="18" height="18" viewBox="0 0 24 24"><path d="M12 3l9 18H3z" fill="${color}" stroke="#fff" stroke-width="2"/></svg>`
      : `<svg width="${node.nodeType === 'ENTRANCE' ? 18 : 14}" height="${
          node.nodeType === 'ENTRANCE' ? 18 : 14
        }" viewBox="0 0 24 24"><circle cx="12" cy="12" r="9" fill="${color}" stroke="#fff" stroke-width="3"/></svg>`
  return (
    `<div style="display:flex;flex-direction:column;align-items:center;gap:2px;">${shape}` +
    `<span style="font-size:11px;line-height:16px;color:#fff;background:${color};` +
    `padding:0 5px;border-radius:8px;white-space:nowrap;box-shadow:0 1px 4px rgba(0,0,0,.35);">` +
    `${node.name}</span></div>`
  )
}

function infoHtml(node: TrailNode): string {
  const typeText =
    node.nodeType === 'ENTRANCE'
      ? '步道出入口'
      : node.nodeType === 'JUNCTION'
        ? '岔路口'
        : '观景点'
  const rows = [
    `<div><b>${node.name}</b></div>`,
    `<div>类型：${typeText}</div>`,
    node.note ? `<div>${node.note}</div>` : '',
  ]
  if (node.imageUrl) {
    rows.push(
      `<div style="margin-top:4px;"><img src="${node.imageUrl}" style="width:150px;border-radius:4px;" /></div>`,
    )
  }
  return `<div style="font-size:13px;line-height:1.7;color:#303133;min-width:150px;">${rows.join('')}</div>`
}

/** 链路合并：度为 2 的节点只做串联，度为 1/3+ 的节点视为链条端点（岔路口处断开）。 */
function buildChains(network: TrailNetwork): TrailChain[] {
  const nodeMap = new Map<number, TrailNode>()
  network.nodes.forEach((node) => nodeMap.set(node.nodeId, node))
  const edgeById = new Map<number, TrailEdge>()
  network.edges.forEach((edge) => edgeById.set(edge.edgeId, edge))

  interface Link {
    edgeId: number
    other: number
  }
  const adjacency = new Map<number, Link[]>()
  const link = (id: number, item: Link) => {
    const list = adjacency.get(id)
    if (list) list.push(item)
    else adjacency.set(id, [item])
  }
  for (const edge of network.edges) {
    const a = nodeMap.get(edge.startNodeId)
    const b = nodeMap.get(edge.endNodeId)
    if (!a || !b) continue
    link(a.nodeId, { edgeId: edge.edgeId, other: b.nodeId })
    link(b.nodeId, { edgeId: edge.edgeId, other: a.nodeId })
  }

  const sourceOf = (edgeId: number): 'manual' | 'osm' =>
    edgeById.get(edgeId)?.source === 'osm' ? 'osm' : 'manual'
  const positionOf = (nodeId: number): [number, number] | null => {
    const node = nodeMap.get(nodeId)
    return node ? [node.longitude, node.latitude] : null
  }

  const usedEdges = new Set<number>()
  const chains: TrailChain[] = []

  const walk = (startId: number, firstEdgeId: number, firstOther: number) => {
    const start = positionOf(startId)
    if (!start) return
    const path: [number, number][] = [start]
    const source = sourceOf(firstEdgeId)
    let currentEdge = firstEdgeId
    let currentOther = firstOther

    while (!usedEdges.has(currentEdge)) {
      usedEdges.add(currentEdge)
      const point = positionOf(currentOther)
      if (!point) break
      path.push(point)

      const options = adjacency.get(currentOther) ?? []
      // 岔路口断开，让每条支路各自成链
      if (options.length > 2) break
      const next = options.find((item) => !usedEdges.has(item.edgeId))
      if (!next) break
      // 主干与小路线宽不同，换来源时断开
      if (sourceOf(next.edgeId) !== source) break
      currentEdge = next.edgeId
      currentOther = next.other
    }

    if (path.length >= 2) chains.push({ path, source })
  }

  // 从端点 / 岔路口出发，保证所有非环边都被覆盖
  for (const [nodeId, links] of adjacency) {
    if (links.length === 2) continue
    for (const item of links) {
      if (usedEdges.has(item.edgeId)) continue
      walk(nodeId, item.edgeId, item.other)
    }
  }
  // 剩余的是闭环，任取一条未用边起手
  for (const edge of network.edges) {
    if (usedEdges.has(edge.edgeId)) continue
    if (!nodeMap.get(edge.startNodeId) || !nodeMap.get(edge.endNodeId)) continue
    walk(edge.startNodeId, edge.edgeId, edge.endNodeId)
  }

  return chains
}

/** 按 zoom 抽稀：z≤14 每 4 点取 1，z15~16 每 2 点取 1，z≥17 全量。 */
function zoomBucket(zoom: number): number {
  if (zoom <= 14) return 0
  if (zoom <= 16) return 1
  return 2
}

function decimate(path: [number, number][], bucket: number): [number, number][] {
  if (bucket >= 2 || path.length <= 2) return path
  const stride = bucket === 0 ? 4 : 2
  const result: [number, number][] = [path[0]]
  for (let i = 1; i + 1 < path.length; i += 1) {
    if (i % stride === 0) result.push(path[i])
  }
  result.push(path[path.length - 1])
  return result
}

/** 绘制合并后的链路：主干双层粗线，OSM 小路细线 */
function renderChains(bucket: number) {
  if (!map) return
  trailPolylines.forEach((line) => map.remove(line))
  trailPolylines = []
  renderedBucket = bucket

  for (const chain of mergedChains) {
    const path = decimate(chain.path, bucket)
    if (chain.source === 'osm') {
      const line = new window.AMap.Polyline({
        path,
        strokeColor: '#0d9488',
        strokeWeight: 3,
        strokeOpacity: 0.75,
        lineJoin: 'round',
        lineCap: 'round',
        bubble: true,
      })
      map.add(line)
      trailPolylines.push(line)
    } else {
      const casing = new window.AMap.Polyline({
        path,
        strokeColor: '#ffffff',
        strokeWeight: 8,
        strokeOpacity: 0.9,
        lineJoin: 'round',
        lineCap: 'round',
        bubble: true,
      })
      const main = new window.AMap.Polyline({
        path,
        strokeColor: '#0d9488',
        strokeWeight: 5,
        strokeOpacity: 0.9,
        lineJoin: 'round',
        lineCap: 'round',
        bubble: true,
      })
      map.add(casing)
      map.add(main)
      trailPolylines.push(casing, main)
    }
  }
}

function renderMarkers() {
  nodeMarkers.forEach((marker) => map.remove(marker))
  nodeMarkers = []
  const network = props.network
  if (!network || !map) return

  for (const node of network.nodes) {
    if (node.nodeType === 'PATH') continue
    if (!node.longitude || !node.latitude) continue
    const marker = new window.AMap.Marker({
      position: [node.longitude, node.latitude],
      anchor: 'bottom-center',
      title: node.name,
      content: markerContent(node),
    })
    marker.on('click', () => {
      if (!infoWindow) {
        infoWindow = new window.AMap.InfoWindow({ offset: new window.AMap.Pixel(0, -18) })
      }
      infoWindow.setContent(infoHtml(node))
      infoWindow.open(map, [node.longitude, node.latitude])
      emit('node-click', node)
    })
    map.add(marker)
    nodeMarkers.push(marker)
  }
}

function renderNetwork() {
  if (!map) return
  mergedChains = props.network ? buildChains(props.network) : []
  renderChains(zoomBucket(map.getZoom()))
  renderMarkers()

  if (!fittedNetwork && trailPolylines.length > 0) {
    // 全域路网跨度约 13 km，首屏必须覆盖整个导入范围
    map.setFitView(trailPolylines, false, [30, 30, 30, 30], 17)
    fittedNetwork = true
  }
}

/** 规划路线高亮，画在步道之上 */
function renderRoute() {
  if (!map) return
  if (routePolyline) {
    map.remove(routePolyline)
    routePolyline = null
  }
  const path = props.routePath.map((p) => [p.longitude, p.latitude])
  if (path.length < 2) return
  routePolyline = new window.AMap.Polyline({
    path,
    strokeColor: '#E6A23C',
    strokeWeight: 8,
    strokeOpacity: 0.95,
    lineJoin: 'round',
    lineCap: 'round',
    showDir: true,
    zIndex: 120,
  })
  map.add(routePolyline)
  map.setFitView([routePolyline], false, [50, 50, 50, 50], 17)
}

function renderPosition() {
  if (!map) return
  if (positionMarker) {
    map.remove(positionMarker)
    positionMarker = null
  }
  if (rawPositionMarker) {
    map.remove(rawPositionMarker)
    rawPositionMarker = null
  }

  if (props.position) {
    positionMarker = new window.AMap.Marker({
      position: [props.position.longitude, props.position.latitude],
      anchor: 'center',
      zIndex: 200,
      content:
        '<div style="width:16px;height:16px;border-radius:50%;background:#409EFF;' +
        'border:3px solid #fff;box-shadow:0 0 0 4px rgba(64,158,255,.25),0 1px 6px rgba(0,0,0,.4);"></div>',
    })
    map.add(positionMarker)
  }

  if (props.rawPosition) {
    rawPositionMarker = new window.AMap.Marker({
      position: [props.rawPosition.longitude, props.rawPosition.latitude],
      anchor: 'center',
      zIndex: 190,
      content:
        '<div style="width:12px;height:12px;border-radius:50%;background:rgba(144,147,153,.75);' +
        'border:2px solid #fff;"></div>',
    })
    map.add(rawPositionMarker)
  }
}

function locateMe() {
  if (!map || !props.position) return
  map.setZoomAndCenter(17, [props.position.longitude, props.position.latitude])
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
  // 缩放结束后按抽稀档位重绘，避免上千条折线全量绘制掉帧
  map.on('zoomend', () => {
    const bucket = zoomBucket(map.getZoom())
    if (bucket !== renderedBucket) renderChains(bucket)
  })
  renderNetwork()
  renderRoute()
  renderPosition()
  emit('map-ready')
}

onMounted(() => {
  // 高德脚本在 index.html 中同步加载，网络慢时轮询等待
  let tries = 0
  const timer = window.setInterval(() => {
    if (window.AMap) {
      window.clearInterval(timer)
      initMap()
    } else if (++tries > 50) {
      window.clearInterval(timer)
      loadFailed.value = true
    }
  }, 100)
})

onUnmounted(() => {
  if (map) map.destroy()
  map = null
})

watch(() => props.network, renderNetwork, { deep: false })
watch(() => props.routePath, renderRoute, { deep: false })
watch(() => props.position, renderPosition, { deep: false })
watch(() => props.rawPosition, renderPosition, { deep: false })
</script>

<style scoped>
.trail-map-wrap {
  position: relative;
  width: 100%;
}

.trail-map {
  width: 100%;
  height: 100%;
}

.locate-btn {
  position: absolute;
  left: 12px;
  bottom: 12px;
  z-index: 10;
  padding: 5px 10px;
  font-size: 12px;
  color: #0d9488;
  background: #fff;
  border: 1px solid #0d9488;
  border-radius: 4px;
  cursor: pointer;
  box-shadow: 0 1px 4px rgba(0, 0, 0, 0.2);
}

.map-attribution {
  position: absolute;
  right: 8px;
  bottom: 6px;
  z-index: 8;
  font-size: 11px;
  color: rgba(255, 255, 255, 0.85);
  text-shadow: 0 1px 2px rgba(0, 0, 0, 0.8);
  pointer-events: none;
}

.map-fallback {
  position: absolute;
  inset: 0;
  display: flex;
  align-items: center;
  justify-content: center;
  color: #909399;
  font-size: 13px;
  background: #f5f7fa;
}
</style>

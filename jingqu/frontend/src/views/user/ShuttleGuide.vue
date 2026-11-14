<template>
  <div class="page">
    <!-- 导航模式：站点选择器 -->
    <el-card v-if="isNavigating" shadow="never" class="card nav-setup-card">
      <div class="nav-header">
        <button class="back-btn" @click="exitNavigation">&larr; 返回</button>
        <span class="nav-title">导航到乘车站点</span>
      </div>
      
      <div class="station-selector">
        <label class="selector-label">目标站点：</label>
        <el-select v-model="selectedStation" class="station-select" placeholder="选择站点" @change="onStationChange">
          <el-option
            v-for="station in stationList"
            :key="station.name"
            :label="station.name"
            :value="station.name"
          >
            <span>{{ station.name }}</span>
            <span class="distance-hint">{{ station.distanceM }}m</span>
          </el-option>
        </el-select>
        <button class="nav-start-btn" :disabled="!selectedStation" @click="startNavigation">
          开始导航
        </button>
      </div>

      <div v-if="currentPosition" class="position-info">
        <span class="info-icon">&bull;</span>
        当前位置：({{ currentPosition.longitude.toFixed(5) }}, {{ currentPosition.latitude.toFixed(5) }})
      </div>
    </el-card>

    <!-- 地图 -->
    <el-card shadow="never" class="card map-card">
      <AMapView
        :points="navMarkers"
        :route-path="navRoutePath"
        :line-color="'#409EFF'"
        height="400px"
      />
      <div class="map-legend" v-if="!isNavigating">
        <span><i class="dot station"></i>站点</span>
        <span class="hint">点击线路查看详情</span>
      </div>
    </el-card>

    <!-- 导航中：路线信息 -->
    <el-card v-if="isNavigating && navRoutePath.length > 0" shadow="never" class="card nav-info-card">
      <div class="nav-info">
        <div class="nav-info-header">
          <span class="nav-info-title">前往 {{ selectedStation }}</span>
          <button class="nav-stop-btn" @click="exitNavigation">结束导航</button>
        </div>
        <div class="nav-info-stats">
          <span class="stat">距离约 {{ navDistanceText }}</span>
        </div>
        <div class="nav-hint">
          <span class="hint-icon">&#128205;</span>
          沿步道路线前往站点，到达站点后等候摆渡车
        </div>
      </div>
    </el-card>

    <!-- 线路列表（非导航模式） -->
    <template v-if="!isNavigating">
      <!-- 导航入口 -->
      <el-card shadow="never" class="card nav-entry-card" @click="enterNavigation">
        <div class="nav-entry-content">
          <span class="nav-entry-icon">&#128205;</span>
          <div class="nav-entry-text">
            <span class="nav-entry-title">导航到乘车站点</span>
            <span class="nav-entry-desc">通过步道步行导航到最近的车站点</span>
          </div>
          <span class="nav-entry-arrow">&rarr;</span>
        </div>
      </el-card>

      <el-card v-for="r in routes" :key="r.routeName" shadow="never" class="card"
        :class="{ active: activeName === r.routeName }"
        @click="activeName = r.routeName">
        <div class="route-head">
          <span class="route-name">&#128419; {{ r.routeName }}</span>
          <QueueBadge :minutes="r.waitingMinutes" prefix="候车" :tone="'normal'" />
        </div>
        <div class="stations">
          <template v-for="(st, i) in r.stations" :key="i">
            <span v-if="i > 0" class="arrow">&rarr;</span>
            <span class="station">{{ st }}</span>
          </template>
        </div>
        <div class="route-meta">全程约 {{ r.totalDistanceM }} 米</div>
      </el-card>
      <el-empty v-if="!routes.length" description="暂无摆渡线路信息" />
    </template>
  </div>
</template>

<script setup lang="ts">
import { ref, computed, onMounted, onUnmounted } from 'vue'
import AMapView from '@/components/user/AMapView.vue'
import QueueBadge from '@/components/user/QueueBadge.vue'
import { getUserShuttleRoutes, getUserPois } from '@/api'
import { getTrailNetwork, planTrailRoute } from '@/api/trail'
import { haversine } from '@/utils/trailGeo'
import { usePolling } from '@/composables/usePolling'
import type { ShuttleRouteGroup, MapMarker, Poi } from '@/api'

const routes = ref<ShuttleRouteGroup[]>([])
const activeName = ref<string>('')

const lineColors = ['#409EFF', '#67C23A', '#E6A23C', '#F56C6C', '#909399', '#722ED1', '#13C2C2', '#EE0A24']

const mapMarkers = computed<MapMarker[]>(() => {
  const set = new Set<string>()
  const markers: MapMarker[] = []
  for (const r of routes.value) {
    const path = (r as any).path as any[] | undefined
    if (!path) continue
    for (const pt of path) {
      const key = `${pt.latitude},${pt.longitude}`
      if (!set.has(key) && pt.latitude !== '0' && pt.longitude !== '0') {
        set.add(key)
        markers.push({
          id: `st_${key}`,
          name: '',
          type: 'spot' as const,
          longitude: parseFloat(pt.longitude),
          latitude: parseFloat(pt.latitude),
        })
      }
    }
  }
  return markers
})

const activePath = computed<MapMarker[]>(() => {
  const route = routes.value.find(r => r.routeName === activeName.value)
  if (!route) return []
  const path = (route as any).path as any[] | undefined
  if (!path) return []
  return path
    .filter((pt: any) => pt.latitude !== '0' && pt.longitude !== '0')
    .map((pt: any) => ({
      id: '',
      name: '',
      type: 'spot' as const,
      longitude: parseFloat(pt.longitude),
      latitude: parseFloat(pt.latitude),
    }))
})

const activeColor = computed(() => {
  if (!activeName.value) return '#409EFF'
  const idx = routes.value.findIndex(r => r.routeName === activeName.value)
  return lineColors[idx % lineColors.length] || '#409EFF'
})

// ==================== 导航功能 ====================

// 站点 POI 数据
const stationPoIs = ref<Poi[]>([])
const isNavigating = ref(false)
const selectedStation = ref('')
const navRoutePath = ref<MapMarker[]>([])
const navDistance = ref(0)
const currentPosition = ref<{ longitude: number; latitude: number } | null>(null)
let watchId: number | null = null

// 站点列表（带距离）
const stationList = computed(() => {
  if (!currentPosition.value || stationPoIs.value.length === 0) return []
  return stationPoIs.value
    .filter(poi => poi.category === 'STATION')
    .map(poi => ({
      name: poi.name,
      longitude: poi.longitude,
      latitude: poi.latitude,
      distanceM: Math.round(haversine(
        currentPosition.value!.longitude,
        currentPosition.value!.latitude,
        poi.longitude,
        poi.latitude
      ))
    }))
    .sort((a, b) => a.distanceM - b.distanceM)
})

// 自动选择最近站点
const nearestStation = computed(() => stationList.value[0]?.name || '')

// 导航标记点
const navMarkers = computed<MapMarker[]>(() => {
  const markers: MapMarker[] = []
  
  // 添加当前位置标记
  if (currentPosition.value) {
    markers.push({
      id: 'current_position',
      name: '当前位置',
      type: 'current' as const,
      longitude: currentPosition.value.longitude,
      latitude: currentPosition.value.latitude,
    })
  }
  
  // 添加所有站点标记
  for (const station of stationList.value) {
    markers.push({
      id: `station_${station.name}`,
      name: station.name,
      type: 'spot' as const,
      longitude: station.longitude,
      latitude: station.latitude,
    })
  }
  
  return markers
})

// 导航距离文本
const navDistanceText = computed(() => {
  if (navDistance.value === 0) return '计算中...'
  if (navDistance.value < 1000) return `${navDistance.value} 米`
  return `${(navDistance.value / 1000).toFixed(1)} 公里`
})

// 启动浏览器定位
function startLocation() {
  if (!navigator.geolocation) {
    console.warn('浏览器不支持地理定位')
    return
  }
  
  watchId = navigator.geolocation.watchPosition(
    (position) => {
      currentPosition.value = {
        longitude: position.coords.longitude,
        latitude: position.coords.latitude
      }
    },
    (error) => {
      console.warn('定位失败:', error)
      // 定位失败时使用默认位置（景区中心）
      currentPosition.value = {
        longitude: 110.45,
        latitude: 29.34
      }
    },
    {
      enableHighAccuracy: true,
      timeout: 10000,
      maximumAge: 30000
    }
  )
}

function enterNavigation() {
  isNavigating.value = true
  selectedStation.value = nearestStation.value
}

function exitNavigation() {
  isNavigating.value = false
  selectedStation.value = ''
  navRoutePath.value = []
  navDistance.value = 0
}

async function startNavigation() {
  if (!selectedStation.value || !currentPosition.value) {
    alert('请先获取位置信息')
    return
  }
  
  try {
    // 获取目标站点坐标
    const targetStation = stationList.value.find(s => s.name === selectedStation.value)
    if (!targetStation) {
      alert('未找到目标站点')
      return
    }
    
    // 获取步道网络
    const networkRes = await getTrailNetwork()
    const network = networkRes.data
    
    if (!network || !network.nodes || network.nodes.length === 0) {
      alert('步道网络数据不可用，将显示直线引导')
      // 显示直线引导
      navRoutePath.value = [
        {
          id: 'start',
          name: '起点',
          type: 'spot' as const,
          longitude: currentPosition.value.longitude,
          latitude: currentPosition.value.latitude
        },
        {
          id: 'end',
          name: selectedStation.value,
          type: 'spot' as const,
          longitude: targetStation.longitude,
          latitude: targetStation.latitude
        }
      ]
      navDistance.value = haversine(
        currentPosition.value.longitude,
        currentPosition.value.latitude,
        targetStation.longitude,
        targetStation.latitude
      )
      return
    }
    
    // 规划路线
    const routeRes = await planTrailRoute({
      start: {
        longitude: currentPosition.value.longitude,
        latitude: currentPosition.value.latitude
      },
      end: {
        longitude: targetStation.longitude,
        latitude: targetStation.latitude
      }
    })
    
    if (!routeRes.data || !routeRes.data.nodes || routeRes.data.nodes.length < 2) {
      alert('无法规划路线，将显示直线引导')
      navRoutePath.value = [
        {
          id: 'start',
          name: '起点',
          type: 'spot' as const,
          longitude: currentPosition.value.longitude,
          latitude: currentPosition.value.latitude
        },
        {
          id: 'end',
          name: selectedStation.value,
          type: 'spot' as const,
          longitude: targetStation.longitude,
          latitude: targetStation.latitude
        }
      ]
      navDistance.value = haversine(
        currentPosition.value.longitude,
        currentPosition.value.latitude,
        targetStation.longitude,
        targetStation.latitude
      )
      return
    }
    
    // 转换路线数据
    navRoutePath.value = routeRes.data.nodes.map((node, index) => ({
      id: `node_${index}`,
      name: index === 0 ? '起点' : (index === routeRes.data.nodes.length - 1 ? selectedStation.value : ''),
      type: 'spot' as const,
      longitude: node.longitude,
      latitude: node.latitude
    }))
    
    // 计算路线总距离
    let totalDistance = 0
    for (let i = 1; i < routeRes.data.nodes.length; i++) {
      const prev = routeRes.data.nodes[i - 1]
      const curr = routeRes.data.nodes[i]
      totalDistance += haversine(prev.longitude, prev.latitude, curr.longitude, curr.latitude)
    }
    navDistance.value = Math.round(totalDistance)
    
  } catch (error) {
    console.error('导航规划失败:', error)
    alert('导航规划失败，将显示直线引导')
    
    const targetStation = stationList.value.find(s => s.name === selectedStation.value)
    if (!targetStation || !currentPosition.value) return
    
    navRoutePath.value = [
      {
        id: 'start',
        name: '起点',
        type: 'spot' as const,
        longitude: currentPosition.value.longitude,
        latitude: currentPosition.value.latitude
      },
      {
        id: 'end',
        name: selectedStation.value,
        type: 'spot' as const,
        longitude: targetStation.longitude,
        latitude: targetStation.latitude
      }
    ]
    navDistance.value = haversine(
      currentPosition.value.longitude,
      currentPosition.value.latitude,
      targetStation.longitude,
      targetStation.latitude
    )
  }
}

function onStationChange() {
  // 站点变化时重新规划路线
  if (selectedStation.value && currentPosition.value) {
    startNavigation()
  }
}

async function fetchData() {
  try {
    const res = await getUserShuttleRoutes()
    console.log('[ShuttleGuide] API response:', res)
    routes.value = res.data || []
    
    // 同时获取站点 POI 数据
    const poiRes = await getUserPois()
    stationPoIs.value = poiRes.data || []
    console.log('[ShuttleGuide] station POIs:', stationPoIs.value.length)
  } catch (error) {
    console.error('获取数据失败:', error)
  }
}

onMounted(() => {
  fetchData()
  startLocation()
})

onUnmounted(() => {
  if (watchId !== null) {
    navigator.geolocation.clearWatch(watchId)
  }
})

// 使用 usePolling 定时刷新（它内部也会调用一次 fetchData）
usePolling(fetchData, 30000)
</script>

<style scoped>
.page {
  display: flex;
  flex-direction: column;
  gap: 12px;
}

.card {
  border-radius: 6px;
  cursor: pointer;
  transition: box-shadow 0.2s;
}

.card:hover {
  box-shadow: 0 2px 12px rgba(0,0,0,0.1);
}

.card.active {
  border: 2px solid #409EFF;
}

.map-card {
  cursor: default;
}

.map-card:hover {
  box-shadow: 0 2px 12px rgba(0,0,0,0.1);
}

.card :deep(.el-card__body) {
  padding: 14px 16px;
}

.route-head {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 10px;
}

.route-name {
  font-size: 16px;
  font-weight: 600;
  color: #303133;
}

.stations {
  display: flex;
  flex-wrap: wrap;
  align-items: center;
  gap: 6px;
  margin-top: 12px;
  padding: 10px;
  background: #f5f7fa;
  border-radius: 6px;
}

.station {
  font-size: 13px;
  color: #303133;
  background: #fff;
  border: 1px solid #e4e7ed;
  border-radius: 4px;
  padding: 3px 8px;
}

.arrow {
  color: #c0c4cc;
  font-size: 12px;
}

.route-meta {
  margin-top: 8px;
  font-size: 12px;
  color: #909399;
}

.map-legend {
  display: flex;
  align-items: center;
  gap: 14px;
  padding: 8px 4px 2px;
  font-size: 12px;
  color: #606266;
}

.map-legend .dot {
  display: inline-block;
  width: 9px;
  height: 9px;
  border-radius: 50%;
  margin-right: 4px;
  background: #E6A23C;
}

.map-legend .hint {
  margin-left: auto;
  color: #909399;
}

/* 导航入口卡片样式 */
.nav-entry-card {
  cursor: pointer;
}

.nav-entry-content {
  display: flex;
  align-items: center;
  gap: 12px;
  padding: 8px 0;
}

.nav-entry-icon {
  font-size: 24px;
}

.nav-entry-text {
  flex: 1;
  display: flex;
  flex-direction: column;
  gap: 2px;
}

.nav-entry-title {
  font-size: 15px;
  font-weight: 600;
  color: #303133;
}

.nav-entry-desc {
  font-size: 12px;
  color: #909399;
}

.nav-entry-arrow {
  font-size: 18px;
  color: #c0c4cc;
}

/* 导航相关样式 */
.nav-setup-card {
  cursor: default;
}

.nav-header {
  display: flex;
  align-items: center;
  gap: 12px;
  margin-bottom: 16px;
}

.back-btn {
  padding: 6px 12px;
  font-size: 14px;
  color: #409EFF;
  background: #fff;
  border: 1px solid #409EFF;
  border-radius: 4px;
  cursor: pointer;
  transition: all 0.2s;
}

.back-btn:hover {
  background: #409EFF;
  color: #fff;
}

.nav-title {
  font-size: 16px;
  font-weight: 600;
  color: #303133;
}

.station-selector {
  display: flex;
  align-items: center;
  gap: 12px;
  flex-wrap: wrap;
}

.selector-label {
  font-size: 14px;
  color: #606266;
  white-space: nowrap;
}

.station-select {
  flex: 1;
  min-width: 200px;
}

.distance-hint {
  float: right;
  color: #909399;
  font-size: 12px;
}

.nav-start-btn {
  padding: 8px 20px;
  font-size: 14px;
  color: #fff;
  background: #409EFF;
  border: none;
  border-radius: 4px;
  cursor: pointer;
  transition: background 0.2s;
}

.nav-start-btn:hover {
  background: #66b1ff;
}

.position-info {
  margin-top: 12px;
  font-size: 12px;
  color: #909399;
}

.info-icon {
  color: #409EFF;
}

.nav-info-card {
  cursor: default;
}

.nav-info {
  padding: 8px 0;
}

.nav-info-header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  margin-bottom: 12px;
}

.nav-info-title {
  font-size: 16px;
  font-weight: 600;
  color: #303133;
}

.nav-stop-btn {
  padding: 6px 16px;
  font-size: 13px;
  color: #F56C6C;
  background: #fff;
  border: 1px solid #F56C6C;
  border-radius: 4px;
  cursor: pointer;
  transition: all 0.2s;
}

.nav-stop-btn:hover {
  background: #F56C6C;
  color: #fff;
}

.nav-info-stats {
  display: flex;
  gap: 16px;
  margin-bottom: 8px;
  font-size: 13px;
  color: #606266;
}

.stat {
  display: flex;
  align-items: center;
  gap: 4px;
}

.nav-hint {
  display: flex;
  align-items: center;
  gap: 8px;
  padding: 10px 12px;
  background: #ecf5ff;
  border-left: 3px solid #409EFF;
  border-radius: 4px;
  font-size: 13px;
  color: #409EFF;
}

.hint-icon {
  font-size: 16px;
}
</style>
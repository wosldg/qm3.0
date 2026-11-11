<template>
  <div class="page">
    <!-- 地图 -->
    <el-card shadow="never" class="card map-card">
      <AMapView :points="mapMarkers" :route-path="activePath" :line-color="activeColor" height="400px" />
      <div class="map-legend">
        <span><i class="dot station"></i>站点</span>
        <span class="hint">点击线路查看详情</span>
      </div>
    </el-card>

    <!-- 线路列表 -->
    <el-card v-for="r in routes" :key="r.routeName" shadow="never" class="card"
      :class="{ active: activeName === r.routeName }"
      @click="activeName = r.routeName">
      <div class="route-head">
        <span class="route-name">🚌 {{ r.routeName }}</span>
        <QueueBadge :minutes="r.waitingMinutes" prefix="候车" :tone="'normal'" />
      </div>
      <div class="stations">
        <template v-for="(st, i) in r.stations" :key="i">
          <span v-if="i > 0" class="arrow">→</span>
          <span class="station">{{ st }}</span>
        </template>
      </div>
      <div class="route-meta">全程约 {{ r.totalDistanceM }} 米</div>
    </el-card>
    <el-empty v-if="!routes.length" description="暂无摆渡线路信息" />
  </div>
</template>

<script setup lang="ts">
import { ref, computed } from 'vue'
import AMapView from '@/components/user/AMapView.vue'
import QueueBadge from '@/components/user/QueueBadge.vue'
import { getUserShuttleRoutes } from '@/api'
import type { ShuttleRouteGroup, MapMarker } from '@/api'
import { usePolling } from '@/composables/usePolling'

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

async function fetchData() {
  const res = await getUserShuttleRoutes()
  routes.value = res.data
}

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
</style>
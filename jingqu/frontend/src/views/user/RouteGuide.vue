<template>
  <div class="page">
    <!-- 路线类型切换 -->
    <el-radio-group v-model="routeType" size="small" class="switch">
      <el-radio-button label="avoid">🌿 避峰路线</el-radio-button>
      <el-radio-button label="classic">🏯 经典路线</el-radio-button>
    </el-radio-group>

    <!-- 门的切换 -->
    <div v-if="gateList.length" class="gate-switch">
      <el-button
        v-for="g in gateList"
        :key="g"
        size="small"
        :type="selectedGate === g ? 'primary' : ''"
        @click="selectedGate = g"
      >
        {{ g }}
      </el-button>
    </div>

    <el-card shadow="never" class="card">
      <AMapView
        ref="mapViewRef"
        :points="markers"
        :route-path="currentRoute?.steps || []"
        :line-color="lineColor"
        :focus-id="focusId"
        height="340px"
        @marker-click="focusSpot"
      />
    </el-card>

    <el-card shadow="never" class="card">
      <div class="route-head">
        <span class="route-name">{{ currentRoute?.name || '路线推荐' }}</span>
        <span v-if="currentRoute" class="route-est">约 {{ currentRoute.estimatedMinutes }} 分钟</span>
      </div>
      <p v-if="currentRoute?.message" class="route-message">{{ currentRoute.message }}</p>

      <template v-if="currentRoute?.steps && currentRoute.steps.length">
        <div class="step-label">
          途径 {{ currentRoute.steps.length }} 个景点 · 步行约 {{ currentRoute.totalDistanceM }} 米
        </div>
        <div
          v-for="s in currentRoute.steps"
          :key="s.spotId"
          class="step-row"
          :class="{ active: focusId === s.spotId, isGate: s.isGate }"
          @click="focusSpot(s.spotId)"
        >
          <span class="step-no" :class="{ gate: s.isGate }">{{ s.order }}</span>
          <span class="step-name">{{ s.name }}</span>
          <LevelTag v-if="!s.isGate" :level="s.level" />
        </div>
      </template>
      <el-skeleton v-else :rows="3" animated />
    </el-card>
  </div>
</template>

<script setup lang="ts">
import { ref, computed, nextTick } from 'vue'
import AMapView from '@/components/user/AMapView.vue'
import LevelTag from '@/components/LevelTag.vue'
import { getUserRecommendRoutes } from '@/api'
import type { MapMarker } from '@/api'
import { usePolling } from '@/composables/usePolling'

const mapViewRef = ref<InstanceType<typeof AMapView> | null>(null)
const routeType = ref<'avoid' | 'classic'>('avoid')

// All route data
const allRoutes = ref<any[]>([])
const gateList = ref<string[]>([])
const selectedGate = ref('')
const focusId = ref('')

// Current route based on type and selected gate
const currentRoute = computed(() => {
  if (!allRoutes.value.length) return null
  if (!selectedGate.value) return allRoutes.value[0]
  const found = allRoutes.value.find((r: any) => r.name.includes(selectedGate.value))
  return found || allRoutes.value[0]
})

const lineColor = computed(() => (routeType.value === 'avoid' ? '#67C23A' : '#909399'))

const markers = computed<MapMarker[]>(() =>
  (currentRoute.value?.steps || [])
    .filter((s: any) => s.longitude && s.latitude)
    .map((s: any) => ({
      id: s.spotId,
      name: s.name,
      type: s.isGate ? 'gate' as const : 'spot' as const,
      longitude: s.longitude,
      latitude: s.latitude,
      visitors: s.currentVisitors,
      level: s.level,
      levelText: s.levelText,
    })),
)

function focusSpot(id: string) {
  if (focusId.value === id) {
    focusId.value = ''
    nextTick(() => (focusId.value = id))
    return
  }
  focusId.value = id
}

function extractGates(routes: any[]) {
  const gates = new Set<string>()
  routes.forEach((r: any) => {
    const match = r.name?.match(/(.+?)出发/)
    if (match?.[1]) gates.add(match[1])
  })
  return Array.from(gates)
}

let fetching = false
async function fetchData() {
  if (fetching) return
  fetching = true
  try {
    const [av, cl] = await Promise.allSettled([
      getUserRecommendRoutes('avoid'),
      getUserRecommendRoutes('classic'),
    ])

    const routes: any[] = []

    if (av.status === 'fulfilled' && av.value?.data?.routes) {
      routes.push(...av.value.data.routes)
    }

    if (cl.status === 'fulfilled' && cl.value?.data?.routes) {
      routes.push(...cl.value.data.routes)
    }

    allRoutes.value = routes
    if (routes.length) {
      gateList.value = extractGates(routes)
      if (!selectedGate.value) {
        selectedGate.value = gateList.value[0]
      }
    }
  } catch (err) {
    console.error('[RouteGuide] fetchData error:', err)
  } finally {
    fetching = false
  }
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
}

.card :deep(.el-card__body) {
  padding: 8px;
}

.route-head {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 4px;
}

.route-name {
  font-size: 16px;
  font-weight: 600;
  color: #303133;
}

.route-est {
  font-size: 12px;
  color: #409eff;
}

.route-message {
  margin: 10px 4px 0;
  font-size: 13px;
  color: #e6a23c;
}

.step-label {
  font-size: 12px;
  color: #909399;
  margin: 8px 4px 6px;
}

.step-row {
  display: flex;
  align-items: center;
  gap: 10px;
  padding: 8px 4px;
  border-bottom: 1px solid #f5f7fa;
  cursor: pointer;
}

.step-row.active {
  background: #ecf5ff;
  border-radius: 4px;
}

.step-row:last-of-type {
  border-bottom: none;
}

.step-row.isGate {
  background: #fdf6ec;
  border-radius: 4px;
  margin: 4px 0;
}

.step-no {
  width: 20px;
  height: 20px;
  border-radius: 50%;
  background: #409eff;
  color: #fff;
  font-size: 12px;
  display: flex;
  align-items: center;
  justify-content: center;
  flex-shrink: 0;
}

.step-no.gate {
  background: #e6a23c;
}

.step-name {
  flex: 1;
  font-size: 14px;
  color: #303133;
}

.gate-switch {
  margin: 8px 0;
}
</style>
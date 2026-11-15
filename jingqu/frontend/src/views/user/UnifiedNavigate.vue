<script setup lang="ts">
import { ref, computed, watch, onMounted } from 'vue'
import { useRouter } from 'vue-router'
import { ElMessage } from 'element-plus'
import axios from 'axios'
import AMapView from '@/components/user/AMapView.vue'
import TrailMap from '@/components/user/TrailMap.vue'
import LevelTag from '@/components/LevelTag.vue'
import {
  getUserRecommendRoutes,
  getPlanSpots,
  planMultiRoute,
  getTrailNetwork,
  type PlanMode,
  type PlanRouteResult,
  type PlanSpot,
  type RecommendData,
  type MapMarker,
  type TrailNetwork,
  type TrailRouteResult,
  type TrailHint,
  type TrailNode,
  type TrailRouteNode,
  type TrailGeoPoint,
} from '@/api'
import { useTrailLocation } from '@/composables/useTrailLocation'
import { useTrailNav } from '@/composables/useTrailNav'
import { haversine, snapToTrail } from '@/utils/trailGeo'

const router = useRouter()

// ==================== 导航模式切换 ====================
type NavMode = 'recommend' | 'peak'
const navMode = ref<NavMode>('recommend')

// ==================== 路线推荐模式 ====================
const routeType = ref<'avoid' | 'classic'>('avoid')
const avoidRoute = ref<RecommendData | null>(null)
const classicRoute = ref<RecommendData | null>(null)
const focusId = ref('')
const recommendNetwork = ref<TrailNetwork | null>(null)
const refiningRoute = ref(false)

const selectedGate = ref('')

const currentRoute = computed<RecommendData | null>(() => {
  const base = (routeType.value === 'avoid' ? avoidRoute.value : classicRoute.value) as any
  if (!base) return null
  const routes = base.routes as any[] | undefined
  if (routes?.length && selectedGate.value) {
    const found = routes.find((r) => String(r.name || '').includes(selectedGate.value))
    if (found) return found as RecommendData
  }
  return base as RecommendData
})

/** 可选的门（后端返回 gates 数组） */
const gateOptions = computed<string[]>(() => {
  const base = (routeType.value === 'avoid' ? avoidRoute.value : classicRoute.value) as any
  return (base?.gates as string[]) || []
})

const lineColor = computed(() => (routeType.value === 'avoid' ? '#67C23A' : '#909399'))

/** 细化后的路径点（用于地图显示） */
const refinedPath = ref<MapMarker[]>([])

/** 加载路网并细化路径 */
async function loadAndRefineRoute() {
  const steps = currentRoute.value?.steps || []
  console.log('[refine] steps:', steps.length, 'currentRoute:', currentRoute.value?.name)
  if (steps.length < 2) {
    refinedPath.value = []
    console.log('[refine] EARLY: steps < 2')
    return
  }

  refiningRoute.value = true
  try {
    // 收集景点坐标（保底路径）
    const spotCoords: TrailGeoPoint[] = []
    for (const step of steps) {
      if (step.longitude && step.latitude) {
        spotCoords.push({ longitude: step.longitude, latitude: step.latitude })
      }
    }
    console.log('[refine] spotCoords:', spotCoords.length)

    // 尝试用步道网络细化路径
    let success = false
    try {
      // 加载路网（如果尚未加载）
      if (!recommendNetwork.value) {
        console.log('[refine] loading trail network...')
        const res = await getTrailNetwork()
        if (res.code === 200 && res.data) {
          recommendNetwork.value = res.data
          console.log('[refine] network loaded:', res.data.nodes.length, 'nodes,', res.data.edges.length, 'edges')
        }
      }

      if (recommendNetwork.value) {
        const nodes = recommendNetwork.value.nodes

        // 将景点坐标吸附到最近步道节点
        const snappedNodeIds: number[] = []
        for (const step of steps) {
          if (!step.longitude || !step.latitude) continue
          let bestNode: TrailNode | null = null
          let bestDist = Infinity
          for (const node of nodes) {
            const d = haversine(step.longitude, step.latitude, node.longitude, node.latitude)
            if (d < bestDist) {
              bestDist = d
              bestNode = node
            }
          }
          if (bestNode) snappedNodeIds.push(bestNode.nodeId)
        }
        console.log('[refine] snappedNodeIds:', snappedNodeIds.length, 'nodes')

        if (snappedNodeIds.length >= 2) {
          // 用后端接口把相邻景点在真实步道路网上展开
          const allPathPoints: TrailGeoPoint[] = []
          for (let i = 0; i < snappedNodeIds.length - 1; i++) {
            const from = snappedNodeIds[i]
            const to = snappedNodeIds[i + 1]
            if (from === to) {
              console.log('[refine] skip same:', from)
              continue
            }
            const fromNode = nodes.find((n) => n.nodeId === from)
            const toNode = nodes.find((n) => n.nodeId === to)
            if (!fromNode || !toNode) continue
            console.log('[refine] planning segment', i + 1, ':', fromNode.name, '->', toNode.name)
            try {
              const res = await axios.post('/api/trail/route', {
                start: { longitude: fromNode.longitude, latitude: fromNode.latitude },
                end: { longitude: toNode.longitude, latitude: toNode.latitude },
              })
              const axiosRes = res.data
              console.log('[refine] segment', i + 1, 'response:', axiosRes.code, axiosRes.data?.nodes?.length, 'nodes')
              if (axiosRes.code === 200 && axiosRes.data?.nodes) {
                for (const n of axiosRes.data.nodes) {
                  allPathPoints.push({ longitude: n.longitude, latitude: n.latitude })
                }
              }
            } catch (e: any) {
              console.log('[refine] segment', i + 1, 'FAILED:', e.message)
            }
          }

          if (allPathPoints.length >= 2) {
            refinedPath.value = allPathPoints.map((p, i) => ({
              id: `path-${i}`,
              name: '',
              type: 'spot' as const,
              ...p,
            }))
            console.log('[refine] trail route DONE:', refinedPath.value.length, 'points')
            success = true
          }
        }
      }
    } catch (e: any) {
      console.log('[refine] trail route ERROR:', e.message)
    }

    // 如果步道路由失败，使用景点坐标直连
    if (!success) {
      console.log('[refine] FALLBACK: using spot coords directly')
      refinedPath.value = spotCoords.map((p, i) => ({
        id: `path-${i}`,
        name: '',
        type: 'spot' as const,
        ...p,
      }))
      console.log('[refine] Fallback path:', refinedPath.value.length, 'points')
    }
  } finally {
    refiningRoute.value = false
  }
}

const recommendMarkers = computed<MapMarker[]>(() =>
  (currentRoute.value?.steps || [])
    .filter((s) => s.longitude && s.latitude)
    .map((s) => ({
      id: s.spotId,
      name: s.name,
      type: 'spot' as const,
      longitude: s.longitude as number,
      latitude: s.latitude as number,
      visitors: s.currentVisitors,
      level: s.level,
      levelText: s.levelText,
    })),
)

function focusSpot(id: string) {
  if (focusId.value === id) {
    focusId.value = ''
    setTimeout(() => (focusId.value = id), 0)
    return
  }
  focusId.value = id
}

async function fetchRoutes() {
  const [av, cl] = await Promise.allSettled([
    getUserRecommendRoutes('avoid'),
    getUserRecommendRoutes('classic'),
  ])
  if (av.status === 'fulfilled') avoidRoute.value = av.value?.data ?? null
  if (cl.status === 'fulfilled') classicRoute.value = cl.value?.data ?? null

  // 初始化默认门
  if (!selectedGate.value) {
    const gates = gateOptions.value
    if (gates.length) selectedGate.value = gates[0]
  }

  // 加载路网并细化路径
  await loadAndRefineRoute()
}

// 路线类型切换时重新细化（首次加载由 fetchRoutes 末尾统一触发，避免重复规划）
watch(routeType, () => loadAndRefineRoute())
// 切换出发门时重新细化路线
watch(selectedGate, () => loadAndRefineRoute())

// ==================== 模式切换 ====================
function onModeChange() {
  if (navMode.value === 'recommend') {
    fetchRoutes()
  } else if (navMode.value === 'peak') {
    loadPeakSpots()
  }
}

// ==================== 避峰导航模式 ====================
const MAX_SPOTS = 8
const peakSpots = ref<PlanSpot[]>([])
const loadingSpots = ref(false)
const entryGateId = ref('')
const exitGateId = ref('')
const checkedSpotIds = ref<string[]>([])
const peakMode = ref<PlanMode>('avoid_peak')
const planResult = ref<PlanRouteResult | null>(null)
const planning = ref(false)

const emptyNetwork = ref<TrailNetwork | null>(null)

const mergedRoute = computed<TrailRouteResult | null>(() => {
  const result = planResult.value
  console.log('[mergedRoute] planResult:', !!result, 'keys:', result ? Object.keys(result) : null, 'segments:', result?.segments?.length)
  if (!result || result.segments.length === 0) return null
  const nodes: TrailRouteNode[] = []
  const hints: TrailHint[] = []
  let offset = 0
  for (const seg of result.segments) {
    for (let i = 0; i < seg.nodes.length; i += 1) {
      if (i === 0 && nodes.length > 0) continue
      nodes.push(seg.nodes[i])
    }
    for (const hint of seg.hints) {
      hints.push({ ...hint, distanceFromStartM: hint.distanceFromStartM + offset })
    }
    offset += seg.totalDistanceM
  }
  return {
    nodes,
    hints,
    totalDistanceM: Math.round(offset * 10) / 10,
    walkMinutes: result.totalWalkMinutes,
    startSnapM: 0,
    endSnapM: 0,
  }
})

const { position: peakPosition, source: peakSource, notice: peakNotice, startReal: startPeakReal, startSim: simPeakSim, stop: stopPeakSim, onArrive: onPeakArrive } = useTrailLocation()
const {
  snappedPoint: peakSnappedPoint,
  rawPoint: peakRawPoint,
  guidance: peakGuidance,
  guidanceLevel: peakGuidanceLevel,
  activeHint: peakActiveHint,
  remainM: peakRemainM,
  remainText: peakRemainText,
  remainMinutes: peakRemainMinutes,
  walkedM: peakWalkedM,
  arrived: peakArrived,
  progressPercent: peakProgressPercent,
  resetNavigation: resetPeakNav,
  handlePosition: handlePeakPosition,
} = useTrailNav({
  route: mergedRoute,
  network: emptyNetwork,
  onArrived: () => stopPeakSim(),
})

const gates = computed(() => peakSpots.value.filter((s) => s.isGate))
const attractions = computed(() => peakSpots.value.filter((s) => !s.isGate))

watch(peakPosition, (p) => {
  if (p) handlePeakPosition(p)
})

watch(planResult, (newVal, oldVal) => {
  console.log('[watch-planResult] old:', !!oldVal, 'new:', !!newVal, 'segments:', newVal?.segments?.length)
  if (newVal) {
    console.log('[watch-planResult] newVal keys:', Object.keys(newVal))
  }
  resetPeakNav()
  stopPeakSim()
})

onPeakArrive(() => {
  if (!peakArrived.value && mergedRoute.value) {
    peakArrived.value = true
    ElMessage.success('已到达终点')
  }
})

async function loadPeakSpots() {
  loadingSpots.value = true
  try {
    const res = await getPlanSpots()
    if (res.code === 200 && res.data) {
      peakSpots.value = res.data.spots
      // 初始不自动选中，由用户手动选择
      checkedSpotIds.value = []
      const defaultGate = gates.value[0]
      if (defaultGate) {
        entryGateId.value = defaultGate.spotId
        exitGateId.value = defaultGate.spotId
      }
    } else {
      ElMessage.error(res.message || '加载景点失败')
    }
  } finally {
    loadingSpots.value = false
  }
}

function levelTagType(level: string): 'success' | 'warning' | 'danger' | 'info' {
  if (level === 'EMPTY' || level === 'NORMAL') return 'success'
  if (level === 'MODERATE') return 'warning'
  return 'danger'
}

async function doPeakPlan(overrideIds?: string[]) {
  const spotIds = overrideIds ?? checkedSpotIds.value
  if (spotIds.length < 1) {
    ElMessage.warning('请至少选择 1 个景点')
    return
  }
  if (spotIds.length > MAX_SPOTS) {
    ElMessage.warning(`最多选择 ${MAX_SPOTS} 个景点`)
    return
  }
  if (!entryGateId.value || !exitGateId.value) {
    ElMessage.warning('请选择入口门和出口门')
    return
  }
  console.log('[peak-plan] spotIds:', spotIds, 'entryGateId:', entryGateId.value, 'exitGateId:', exitGateId.value, 'mode:', peakMode.value)
  planning.value = true
  try {
    const res = await planMultiRoute({
      spotIds,
      mode: peakMode.value,
      entryGateId: entryGateId.value,
      exitGateId: exitGateId.value,
    })
    console.log('[peak-plan] response:', JSON.stringify(res, null, 2))
    console.log('[peak-plan] segments count:', res.data?.segments?.length)
    console.log('[peak-plan] res.data keys:', Object.keys(res.data || {}))
    if (res.code === 200 && res.data) {
      planResult.value = res.data
      console.log('[peak-plan] planResult set, segments:', planResult.value?.segments?.length)
      console.log('[peak-plan] planResult keys:', Object.keys(planResult.value || {}))
    } else {
      ElMessage.warning(res.message || '规划失败')
    }
  } catch (e: any) {
    console.error('[peak-plan] ERROR:', e.message, e)
    ElMessage.error('规划请求失败: ' + (e.message || '未知错误'))
  } finally {
    planning.value = false
  }
}

const dragIndex = ref<number | null>(null)

function onDragStart(index: number) {
  dragIndex.value = index
}

function onDrop(index: number) {
  if (dragIndex.value === null || dragIndex.value === index) {
    dragIndex.value = null
    return
  }
  const ordered = planResult.value?.orderedSpots ?? []
  const ids = ordered.map((s) => s.spotId)
  const [moved] = ids.splice(dragIndex.value, 1)
  ids.splice(index, 0, moved)
  dragIndex.value = null
  doPeakPlan(ids)
}

function startPeakSim() {
  const current = mergedRoute.value
  if (!current || current.nodes.length < 2) {
    ElMessage.warning('请先规划路线')
    return
  }
  resetPeakNav()
  simPeakSim(current.nodes, { jitter: true, speed: 60 })
}

function togglePeakSim() {
  if (peakSource.value === 'sim') {
    stopPeakSim()
    return
  }
  startPeakSim()
}

function usePeakReal() {
  resetPeakNav()
  if (!startPeakReal()) {
    ElMessage.warning(peakNotice.value || '真实定位不可用')
    if (mergedRoute.value) startPeakSim()
  }
}

// ==================== 初始化 ====================
async function initMode() {
  if (navMode.value === 'recommend') {
    await fetchRoutes()
    // 首次加载后需要细化路径
    setTimeout(() => loadAndRefineRoute(), 100)
  } else if (navMode.value === 'peak') {
    await loadPeakSpots()
  }
}

// 支持从外部传入导航模式（通过路由 query）
onMounted(async () => {
  const mode = router.currentRoute.value.query.mode
  if (mode && ['recommend', 'peak'].includes(mode as string)) {
    navMode.value = mode as NavMode
  }
  await initMode()
})

watch(navMode, () => initMode())
</script>

<template>
  <div class="page-shell page-stack">
    <!-- 模式切换 -->
    <el-card shadow="never">
      <div class="mode-switch">
        <el-radio-group v-model="navMode" size="default">
          <el-radio-button value="recommend">🌿 路线推荐</el-radio-button>
          <el-radio-button value="peak">⛰️ 避峰导航</el-radio-button>
        </el-radio-group>
      </div>
    </el-card>

    <!-- ==================== 路线推荐模式 ==================== -->
    <template v-if="navMode === 'recommend'">
      <el-card shadow="never">
        <AMapView
          :points="recommendMarkers"
          :route-path="refinedPath"
          :line-color="lineColor"
          :focus-id="focusId"
          height="340px"
        />
        <div v-if="refiningRoute" class="route-loading">正在细化路线…</div>
      </el-card>

      <el-card shadow="never" class="card">
        <div class="route-head">
          <span class="route-name">{{ currentRoute?.name || '路线推荐' }}</span>
          <span v-if="currentRoute" class="route-est">约 {{ currentRoute.estimatedMinutes }} 分钟</span>
        </div>
        <el-radio-group v-model="routeType" size="small" class="switch">
          <el-radio-button value="avoid">🌿 避峰路线</el-radio-button>
          <el-radio-button value="classic">🏯 经典路线</el-radio-button>
        </el-radio-group>

        <el-radio-group
          v-if="gateOptions.length"
          v-model="selectedGate"
          size="small"
          class="switch gate-switch"
        >
          <el-radio-button v-for="g in gateOptions" :key="g" :value="g">{{ g }}</el-radio-button>
        </el-radio-group>

        <p v-if="currentRoute?.message" class="route-message">{{ currentRoute.message }}</p>

        <template v-if="currentRoute?.steps?.length">
          <div class="step-label">
            途径 {{ currentRoute.steps?.length }} 个景点 · 步行约 {{ currentRoute.totalDistanceM }} 米
          </div>
          <div
            v-for="s in currentRoute.steps || []"
            :key="s.spotId"
            class="step-row"
            :class="{ active: focusId === s.spotId }"
            @click="focusSpot(s.spotId)"
          >
            <span class="step-no">{{ s.order }}</span>
            <span class="step-name">{{ s.name }}</span>
            <LevelTag :level="s.level" />
          </div>
        </template>
        <el-skeleton v-else-if="!currentRoute" :rows="3" animated />
        <el-empty v-else description="暂无可用路线" :image-size="60" />
      </el-card>
    </template>

    <!-- ==================== 避峰导航模式 ==================== -->
    <template v-else-if="navMode === 'peak'">
      <el-card shadow="never">
        <div class="page-toolbar">
          <div>
            <div class="page-title">避峰多景点导航</div>
            <div class="page-desc">
              勾选想去的景点，按实时拥挤度自动安排错峰顺序；也可切换距离最短或自定义顺序。
            </div>
          </div>
        </div>
      </el-card>

      <div class="main-grid">
        <div class="map-column">
          <TrailMap
            :route-path="mergedRoute?.nodes ?? []"
            :position="peakSnappedPoint"
            :raw-position="peakRawPoint"
            height="100%"
            class="map-card"
          />
        </div>

        <div class="control-column">
          <el-card shadow="never" class="control-card">
            <div class="panel-title">起止门</div>
            <div class="gate-row">
              <el-select v-model="entryGateId" class="full" placeholder="从哪个门进">
                <el-option
                  v-for="gate in gates"
                  :key="gate.spotId"
                  :label="`进：${gate.name}`"
                  :value="gate.spotId"
                />
              </el-select>
              <el-select v-model="exitGateId" class="full" placeholder="从哪个门出">
                <el-option
                  v-for="gate in gates"
                  :key="gate.spotId"
                  :label="`出：${gate.name}`"
                  :value="gate.spotId"
                />
              </el-select>
            </div>
          </el-card>

          <el-card shadow="never" class="control-card">
            <div class="panel-title">选择景点（{{ checkedSpotIds.length }}/{{ MAX_SPOTS }}）</div>
            <el-checkbox-group v-model="checkedSpotIds" :max="MAX_SPOTS">
              <div v-for="spot in attractions" :key="spot.spotId" class="spot-check">
                <el-checkbox :value="spot.spotId">
                  <span class="spot-name">{{ spot.name }}</span>
                  <el-tag :type="levelTagType(spot.level)" size="small">{{ spot.levelText }}</el-tag>
                </el-checkbox>
              </div>
            </el-checkbox-group>
          </el-card>

          <el-card shadow="never" class="control-card">
            <div class="panel-title">排序模式</div>
            <el-radio-group v-model="peakMode">
              <el-radio-button value="avoid_peak">避峰优先</el-radio-button>
              <el-radio-button value="shortest">距离最短</el-radio-button>
              <el-radio-button value="custom">自定义顺序</el-radio-button>
            </el-radio-group>
            <el-button
              type="primary"
              class="plan-btn"
              :loading="planning"
              :disabled="checkedSpotIds.length < 1"
              @click="doPeakPlan()"
            >
              开始规划
            </el-button>
          </el-card>

          <el-card v-if="planResult" shadow="never" class="control-card">
            <div class="panel-title">
              游览顺序
              <span v-if="peakMode === 'custom'" class="muted-text">（可拖拽调整）</span>
            </div>
            <div class="order-list">
              <div class="order-item gate">{{ planResult.entryGate.name }}（进）</div>
              <div
                v-for="(spot, index) in planResult.orderedSpots"
                :key="spot.spotId"
                class="order-item"
                :class="{ draggable: peakMode === 'custom' }"
                :draggable="peakMode === 'custom'"
                @dragstart="onDragStart(index)"
                @dragover.prevent
                @drop="onDrop(index)"
              >
                <span class="order-no">{{ index + 1 }}</span>
                <div class="order-body">
                  <div class="order-name">
                    {{ spot.name }}
                    <el-tag :type="levelTagType(spot.level)" size="small">{{ spot.levelText }}</el-tag>
                  </div>
                  <div class="order-sub">建议 {{ spot.suggestedArrival }} 到达</div>
                  <div v-if="spot.peakTip" class="peak-tip">{{ spot.peakTip }}</div>
                </div>
              </div>
              <div class="order-item gate">
                {{ planResult.exitGate.name }}（出{{ planResult.roundTrip ? ' · 原路返回' : '' }}）
              </div>
            </div>
            <el-divider />
            <div class="stat-grid">
              <div class="stat">
                <div class="stat-value">{{ (planResult.totalDistanceM / 1000).toFixed(1) }} km</div>
                <div class="stat-label">总距离</div>
              </div>
              <div class="stat">
                <div class="stat-value">{{ planResult.totalWalkMinutes }} 分钟</div>
                <div class="stat-label">纯步行</div>
              </div>
              <div class="stat">
                <div class="stat-value">{{ planResult.totalDwellMinutes }} 分钟</div>
                <div class="stat-label">停留</div>
              </div>
              <div class="stat">
                <div class="stat-value">{{ planResult.estimatedFinish }}</div>
                <div class="stat-label">预计结束</div>
              </div>
            </div>
          </el-card>

          <el-card shadow="never" class="control-card">
            <div class="panel-title">导航控制</div>
            <div class="button-stack">
              <el-button type="primary" :disabled="!mergedRoute" @click="togglePeakSim">
                {{ peakSource === 'sim' ? '停止演示' : '开始演示' }}
              </el-button>
              <el-button :disabled="!mergedRoute" @click="usePeakReal">使用真实定位</el-button>
            </div>
            <el-alert v-if="peakNotice" :title="peakNotice" type="info" :closable="false" class="nav-alert" />
            <el-alert
              v-if="peakGuidance"
              :title="peakGuidance"
              :type="peakGuidanceLevel === 'error' ? 'error' : 'warning'"
              :closable="false"
              class="nav-alert"
            />
          </el-card>

          <el-card v-if="peakActiveHint" shadow="never" class="control-card hint-card">
            <div class="panel-title">前方岔路</div>
            <div class="hint-text">{{ peakActiveHint.directionHint }}</div>
            <img
              v-if="peakActiveHint.imageUrl"
              :src="peakActiveHint.imageUrl"
              alt="岔路口实景"
              class="hint-image"
            />
          </el-card>

          <el-card v-if="mergedRoute && peakRemainM !== null" shadow="never" class="control-card">
            <div class="panel-title">实时状态</div>
            <div class="status-line">
              <span>剩余</span>
              <strong>{{ peakRemainText }}（约 {{ peakRemainMinutes }} 分钟）</strong>
            </div>
            <el-progress :percentage="peakProgressPercent" :stroke-width="10" class="progress" />
          </el-card>
        </div>
      </div>
    </template>
  </div>
</template>

<style scoped>
.page-shell {
  display: flex;
  flex-direction: column;
  gap: 12px;
}

.page-stack {
  display: flex;
  flex-direction: column;
  gap: 12px;
}

.card {
  border-radius: 6px;
}

.card :deep(.el-card__body) {
  padding: 12px;
}

.mode-switch {
  display: flex;
  justify-content: center;
  padding: 8px;
}

.route-loading {
  text-align: center;
  padding: 8px;
  color: #409eff;
  font-size: 13px;
  background: #ecf5ff;
  border-radius: 4px;
  margin: 8px 0;
}

.switch {
  margin: 10px 4px 0;
  display: flex;
  justify-content: center;
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

.step-name {
  flex: 1;
  font-size: 14px;
  color: #303133;
}

.main-grid {
  display: grid;
  grid-template-columns: 3fr 1fr;
  gap: 12px;
  min-height: 0;
}

.map-column {
  min-height: 640px;
}

.map-card {
  height: 100%;
}

.map-card :deep(.el-card__body) {
  height: 100%;
  box-sizing: border-box;
}

.control-column {
  display: flex;
  flex-direction: column;
  gap: 12px;
}

.control-card :deep(.el-card__body) {
  display: flex;
  flex-direction: column;
  gap: 10px;
}

.panel-title {
  font-weight: 600;
  font-size: 14px;
}

.gate-row {
  display: flex;
  flex-direction: column;
  gap: 8px;
}

.full {
  width: 100%;
}

.spot-check {
  padding: 2px 0;
}

.spot-name {
  margin-right: 6px;
}

.plan-btn {
  width: 100%;
}

.order-list {
  display: flex;
  flex-direction: column;
  gap: 6px;
}

.order-item {
  display: flex;
  align-items: flex-start;
  gap: 8px;
  padding: 6px 8px;
  border-radius: 6px;
  background: #f5f7fa;
  font-size: 13px;
}

.order-item.draggable {
  cursor: grab;
  border: 1px dashed #c0c4cc;
  background: #fff;
}

.order-item.gate {
  color: #0d9488;
  font-weight: 600;
}

.order-no {
  flex-shrink: 0;
  width: 20px;
  height: 20px;
  border-radius: 50%;
  background: #0d9488;
  color: #fff;
  font-size: 12px;
  display: flex;
  align-items: center;
  justify-content: center;
}

.order-body {
  flex: 1;
  min-width: 0;
}

.order-name {
  font-weight: 600;
  display: flex;
  align-items: center;
  gap: 6px;
}

.order-sub {
  font-size: 12px;
  color: #909399;
}

.peak-tip {
  font-size: 12px;
  color: #e6a23c;
}

.stat-grid {
  display: flex;
  gap: 18px;
  flex-wrap: wrap;
}

.stat-value {
  font-size: 17px;
  font-weight: 600;
}

.stat-label {
  font-size: 12px;
  color: #909399;
}

.button-stack {
  display: flex;
  flex-direction: column;
  gap: 8px;
}

.button-stack .el-button + .el-button {
  margin-left: 0;
}

.nav-alert {
  margin-top: 4px;
}

.muted-text {
  color: #909399;
  font-size: 12px;
  font-weight: 400;
}

.status-line {
  display: flex;
  justify-content: space-between;
  align-items: baseline;
}

.progress {
  margin-top: 4px;
}

.hint-card .hint-text {
  font-size: 14px;
  color: #e6a23c;
  font-weight: 600;
}

.hint-image {
  width: 100%;
  border-radius: 6px;
}

.track-desc {
  font-size: 13px;
  color: #606266;
  margin-top: 6px;
  line-height: 1.5;
}
</style>
<script setup lang="ts">
import { computed, onMounted, ref, watch } from 'vue'
import { ElMessage } from 'element-plus'
import {
  getPlanSpots,
  planMultiRoute,
  type PlanMode,
  type PlanRouteResult,
  type PlanSpot,
  type TrailHint,
  type TrailRouteNode,
  type TrailRouteResult,
  type TrailNetwork,
} from '@/api'
import TrailMap from '@/components/user/TrailMap.vue'
import { useTrailLocation } from '@/composables/useTrailLocation'
import { useTrailNav } from '@/composables/useTrailNav'

const MAX_SPOTS = 8

const spots = ref<PlanSpot[]>([])
const loadingSpots = ref(false)
const entryGateId = ref('')
const exitGateId = ref('')
const checkedSpotIds = ref<string[]>([])
const mode = ref<PlanMode>('avoid_peak')
const planResult = ref<PlanRouteResult | null>(null)
const planning = ref(false)

// 避峰导航不加载全路网：吸附只针对当前规划路线
const emptyNetwork = ref<TrailNetwork | null>(null)

/** 把各段拼成一整条导航路线：节点去重衔接，岔路提示加上沿程偏移 */
const mergedRoute = computed<TrailRouteResult | null>(() => {
  const result = planResult.value
  if (!result || result.segments.length === 0) return null
  const nodes: TrailRouteNode[] = []
  const hints: TrailHint[] = []
  let offset = 0
  for (const seg of result.segments) {
    for (let i = 0; i < seg.nodes.length; i += 1) {
      if (i === 0 && nodes.length > 0) continue // 段首与上一段尾重合
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

const { position, source, notice, startReal, startSim, stop, onArrive } = useTrailLocation()
const {
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
} = useTrailNav({
  route: mergedRoute,
  network: emptyNetwork,
  onArrived: () => stop(),
})

const gates = computed(() => spots.value.filter((s) => s.isGate))
const attractions = computed(() => spots.value.filter((s) => !s.isGate))

watch(position, (p) => {
  if (p) handlePosition(p)
})

// 规划结果变化时重置导航进度并停止定位
watch(planResult, () => {
  resetNavigation()
  stop()
})

onArrive(() => {
  if (!arrived.value && mergedRoute.value) {
    arrived.value = true
    ElMessage.success('已到达终点')
  }
})

async function loadSpots() {
  loadingSpots.value = true
  try {
    const res = await getPlanSpots()
    if (res.code === 200 && res.data) {
      spots.value = res.data.spots
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

async function doPlan(overrideIds?: string[]) {
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
  planning.value = true
  try {
    const res = await planMultiRoute({
      spotIds,
      mode: mode.value,
      entryGateId: entryGateId.value,
      exitGateId: exitGateId.value,
    })
    if (res.code === 200 && res.data) {
      planResult.value = res.data
    } else {
      ElMessage.warning(res.message || '规划失败')
    }
  } finally {
    planning.value = false
  }
}

// ==================== custom 模式拖拽重排 ====================
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
  // 重排后按 custom 模式重新规划，刷新段路径与时间估计
  doPlan(ids)
}

function startSimWithRoute() {
  const current = mergedRoute.value
  if (!current || current.nodes.length < 2) {
    ElMessage.warning('请先规划路线')
    return
  }
  resetNavigation()
  startSim(current.nodes, { jitter: true, speed: 60 })
}

function toggleSim() {
  if (source.value === 'sim') {
    stop()
    return
  }
  startSimWithRoute()
}

function useRealLocation() {
  resetNavigation()
  if (!startReal()) {
    ElMessage.warning(notice.value || '真实定位不可用')
    if (mergedRoute.value) startSimWithRoute()
  }
}

onMounted(loadSpots)
</script>

<template>
  <div class="page-shell page-stack">
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
          :position="snappedPoint"
          :raw-position="rawPoint"
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
          <el-radio-group v-model="mode">
            <el-radio-button value="avoid_peak">避峰优先</el-radio-button>
            <el-radio-button value="shortest">距离最短</el-radio-button>
            <el-radio-button value="custom">自定义顺序</el-radio-button>
          </el-radio-group>
          <el-button
            type="primary"
            class="plan-btn"
            :loading="planning"
            :disabled="checkedSpotIds.length < 1"
            @click="doPlan()"
          >
            开始规划
          </el-button>
        </el-card>

        <el-card v-if="planResult" shadow="never" class="control-card">
          <div class="panel-title">
            游览顺序
            <span v-if="mode === 'custom'" class="muted-text">（可拖拽调整）</span>
          </div>
          <div class="order-list">
            <div class="order-item gate">{{ planResult.entryGate.name }}（进）</div>
            <div
              v-for="(spot, index) in planResult.orderedSpots"
              :key="spot.spotId"
              class="order-item"
              :class="{ draggable: mode === 'custom' }"
              :draggable="mode === 'custom'"
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
            <el-button type="primary" :disabled="!mergedRoute" @click="toggleSim">
              {{ source === 'sim' ? '停止演示' : '开始演示' }}
            </el-button>
            <el-button :disabled="!mergedRoute" @click="useRealLocation">使用真实定位</el-button>
          </div>
          <el-alert v-if="notice" :title="notice" type="info" :closable="false" class="nav-alert" />
          <el-alert
            v-if="guidance"
            :title="guidance"
            :type="guidanceLevel === 'error' ? 'error' : 'warning'"
            :closable="false"
            class="nav-alert"
          />
        </el-card>

        <el-card v-if="activeHint" shadow="never" class="control-card hint-card">
          <div class="panel-title">前方岔路</div>
          <div class="hint-text">{{ activeHint.directionHint }}</div>
          <img
            v-if="activeHint.imageUrl"
            :src="activeHint.imageUrl"
            alt="岔路口实景"
            class="hint-image"
          />
        </el-card>

        <el-card v-if="mergedRoute && remainM !== null" shadow="never" class="control-card">
          <div class="panel-title">实时状态</div>
          <div class="status-line">
            <span>剩余</span>
            <strong>{{ remainText }}（约 {{ remainMinutes }} 分钟）</strong>
          </div>
          <el-progress :percentage="progressPercent" :stroke-width="10" class="progress" />
        </el-card>
      </div>
    </div>
  </div>
</template>

<style scoped>
.main-grid {
  display: grid;
  grid-template-columns: 2fr 1fr;
  gap: 12px;
  min-height: 0;
}

.map-column {
  min-height: 480px;
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
</style>

export interface ApiResult<T = any> {
  code: number
  message: string
  data: T
}

export type CongestionLevel = 'EMPTY' | 'NORMAL' | 'MODERATE' | 'CROWDED' | 'OVERLOADED'

export interface Spot {
  spotId: string
  name: string
  locationX: number
  locationY: number
  capacity: number
  dailyCapacity: number
  currentVisitors: number
  totalIn: number
  arrivalRate: number
  departureRate: number
  level: CongestionLevel
}

export interface ParkingLot {
  parkingId: string
  name: string
  totalSpaces: number
  occupiedSpaces: number
  freeSpaces: number
}

export interface Shuttle {
  shuttleId: string
  name: string
  capacity: number
  currentStation: string
  passengerCount: number
  status: 'RUNNING' | 'IDLE' | 'MAINTENANCE'
}

export interface SystemStatus {
  totalCapacity: number
  currentVisitors: number
  crowdedSpotCount: number
  freeSpaces: number
  shuttleOnline: number
  shuttleTotal: number
}

export interface LoginRequest {
  username: string
  password: string
}

export interface LoginResponse {
  token: string
  role: 'admin' | 'viewer'
  username: string
}

export interface FlowPoint {
  timestamp: number
  visitors: number
}

// ==================== 事件 / 告警 ====================

export type EventType =
  | 'CONGESTION'
  | 'OVERLOAD'
  | 'PARKING_SHORTAGE'
  | 'CAPACITY_SHORTAGE'
  | 'DATA_ANOMALY'
  | 'SYSTEM_ERROR'

export type EventLevel = 'INFO' | 'WARNING' | 'HIGH' | 'CRITICAL'

export type EventStatus = 'ACTIVE' | 'ACKNOWLEDGED' | 'RESOLVED' | 'CLOSED' | 'IGNORED'

export interface EventItem {
  eventId: number
  type: EventType
  level: EventLevel
  spotId?: string
  spotName?: string
  description: string
  status: EventStatus
  suggestion?: string
  createdAt: string
  updatedAt?: string
}

// ==================== 调度建议 ====================

export type SuggestionType =
  | 'DIVERSION'
  | 'SHUTTLE_DISPATCH'
  | 'PARKING_GUIDE'
  | 'ENTRANCE_LIMIT'
  | 'RESOURCE_WARNING'

export type SuggestionStatus = 'PENDING' | 'EXECUTED' | 'REJECTED'

export interface Suggestion {
  suggestionId: number
  type: SuggestionType
  content: string
  detail?: string
  spotId?: string
  status: SuggestionStatus
  createdAt: string
}

// ==================== 规则配置 ====================

export type RuleMetric = 'visitor_ratio' | 'parking_free' | 'shuttle_load' | 'arrival_rate'

export type RuleOperator = '>' | '>=' | '<' | '<='

export interface Rule {
  ruleId: number
  spotId: string // '*' 表示适用于全部景点
  metric: RuleMetric
  operator: RuleOperator
  threshold: number
  action: string
  level: EventLevel
  priority: number
  enabled: boolean
}

// ==================== 用户管理 ====================

export interface User {
  userId: number
  username: string
  role: 'admin' | 'viewer'
  createdAt: string
}

export interface UserCreateRequest {
  username: string
  password: string
  role: 'admin' | 'viewer'
}

// ==================== 数据统计 ====================

export interface FlowStatPoint {
  date: string
  visitors: number
}

export interface CongestionStat {
  spotName: string
  crowdedMinutes: number
}

export interface ResourceStatPoint {
  time: string
  utilization: number
}

export interface StatisticsData {
  flow: FlowStatPoint[]
  congestion: CongestionStat[]
  resource: ResourceStatPoint[]
  suggestionTypeCount: { type: SuggestionType; count: number }[]
}

// ==================== 用户端（/api/user/*） ====================

/** 用户端景点简报：只给结论，不给运营数字 */
export interface UserSpotBrief {
  spotId: string
  name: string
  level: CongestionLevel
  levelText: string
  queueMinutes: number
  openTime: string
  closeTime: string
  locationX?: number
  locationY?: number
  latitude?: number
  longitude?: number
  currentVisitors?: number
}

export interface OverviewConclusion {
  level: CongestionLevel
  text: string
  currentVisitors: number
  totalCapacity: number
  ratio: number
  crowdedSpotCount: number
}

export interface PeakAdvice {
  isPeak: boolean
  peakStart?: number
  peakEnd?: number
  message: string
}

export interface OverviewData {
  conclusion: OverviewConclusion
  peakAdvice: PeakAdvice
  spots: UserSpotBrief[]
  updatedAt: string
}

export interface RankingsData {
  type: 'avoid' | 'crowded'
  list: UserSpotBrief[]
}

export interface ForecastPoint {
  hour: number
  predictedVisitors: number
  level: CongestionLevel
  levelText: string
}

export interface ForecastData {
  date: string
  points: ForecastPoint[]
  peakStart: number
  peakEnd: number
}

/** 地图标记点（用户端底图 / 路线页共用，坐标直接用于高德地图） */
export interface MapMarker {
  id: string
  name: string
  type: 'gate' | 'parking' | 'spot'
  longitude: number
  latitude: number
  visitors?: number
  level?: CongestionLevel
  levelText?: string
  freeSpaces?: number
}

export interface UserParking {
  parkingId: string
  name: string
  freeSpaces: number
  distanceM: number
  latitude: number
  longitude: number
  statusText: string
}

export interface ShuttleRouteGroup {
  routeName: string
  stations: string[]
  waitingMinutes: number
  totalDistanceM: number
}

export type PoiCategory = 'TOILET' | 'PARKING' | 'STATION' | 'SERVICE' | 'MEDICAL' | 'GATE'

export interface Poi {
  poiId: string
  name: string
  category: PoiCategory
  categoryText: string
  locationX: number
  locationY: number
  latitude: number
  longitude: number
  description?: string
}

export interface RouteStep extends UserSpotBrief {
  order: number
}

export interface RecommendData {
  type: 'avoid' | 'classic'
  name: string
  steps: RouteStep[]
  totalDistanceM: number
  estimatedMinutes: number
  message?: string
}

export interface SpotTrendPoint {
  hour: number
  visitors: number
}

export interface SpotDetailPoi {
  poiId: string
  name: string
  category: PoiCategory
  categoryText: string
  distanceM: number
  latitude: number
  longitude: number
}

export interface SpotDetailData extends UserSpotBrief {
  currentVisitors: number
  capacity: number
  trend: SpotTrendPoint[]
  pois: SpotDetailPoi[]
}

/** 底图点位选中事件（PoiMap 组件 → 页面） */
export interface PoiMapSelect {
  type: 'spot' | 'poi'
  id: string
  name: string
  latitude?: number
  longitude?: number
  distanceM?: number
}

// ==================== 步道导航（/api/trail/*） ====================

export type TrailNodeType = 'ENTRANCE' | 'JUNCTION' | 'VIEWPOINT' | 'PATH'

/** 步道节点：坐标统一为 GCJ-02，可直接用于高德地图 */
export interface TrailNode {
  nodeId: number
  nodeCode: string
  name: string
  longitude: number
  latitude: number
  nodeType: TrailNodeType
  note?: string
  imageUrl?: string
  /** manual = 手工主干，osm = OpenStreetMap 导入 */
  source?: 'manual' | 'osm'
}

/** 步道连通段：长度为后端按坐标实时计算，不在表里存 */
export interface TrailEdge {
  edgeId: number
  startNodeId: number
  endNodeId: number
  oneWay: number
  directionHint?: string
  source?: 'manual' | 'osm'
}

export interface TrailNetwork {
  nodes: TrailNode[]
  edges: TrailEdge[]
}

export interface TrailGeoPoint {
  longitude: number
  latitude: number
}

export interface TrailRouteRequest {
  start: TrailGeoPoint
  end: TrailGeoPoint
}

export interface TrailRouteNode extends TrailGeoPoint {
  nodeId: number
  name: string
  nodeType: TrailNodeType
}

/** 岔路口引导提示 */
export interface TrailHint {
  nodeId: number
  name: string
  nodeType: TrailNodeType
  directionHint: string
  imageUrl?: string
  distanceFromStartM: number
}

export interface TrailRouteResult {
  nodes: TrailRouteNode[]
  totalDistanceM: number
  walkMinutes: number
  startSnapM: number
  endSnapM: number
  hints: TrailHint[]
}

// ==================== 成熟游览路线（轨迹版） ====================

export interface TrackSummary {
  trackId: number
  name: string
  description?: string
  totalLengthM: number
  estimatedMinutes: number
  pointCount: number
}

export interface TrackPoint extends TrailGeoPoint {
  seq: number
  pointType: TrailNodeType
  name?: string
  directionHint?: string
  imageUrl?: string
}

export interface TrackDetail {
  trackId: number
  name: string
  description?: string
  totalLengthM: number
  estimatedMinutes: number
  points: TrackPoint[]
}

export interface TrackListResult {
  tracks: TrackSummary[]
}

// ==================== 避峰多途经点导航（/api/plan/*） ====================

/** 可选景点（含门标记，供选点面板展示） */
export interface PlanSpot {
  spotId: string
  name: string
  longitude: number
  latitude: number
  currentVisitors: number
  capacity: number
  ratio: number
  level: CongestionLevel
  levelText: string
  /** true = 景区大门，可作为入口/出口，不参与中途点勾选 */
  isGate: boolean
}

export interface PlanSpotListResult {
  spots: PlanSpot[]
}

export type PlanMode = 'avoid_peak' | 'shortest' | 'custom'

export interface PlanRouteRequest {
  /** 中途景点 1~8 个；custom 模式下数组顺序即游览顺序 */
  spotIds: string[]
  mode: PlanMode
  /** 入口门，默认东门 spot_01 */
  entryGateId?: string
  /** 出口门，可等于入口 = 原路返回 */
  exitGateId?: string
}

/** 排序后的中途景点（含错峰提示） */
export interface PlanOrderedSpot {
  spotId: string
  name: string
  longitude: number
  latitude: number
  order: number
  ratio: number
  level: CongestionLevel
  levelText: string
  /** 预计到达时的预测占比（短时外推） */
  predictedRatio: number
  /** 建议到达时间 HH:mm */
  suggestedArrival: string
  /** 避峰模式下的错峰提示文案 */
  peakTip?: string
}

/** 相邻两点间的一段步道（结构同 TrailRouteResult，多 from/to） */
export interface PlanSegment extends TrailRouteResult {
  fromSpotId: string
  toSpotId: string
}

export interface PlanRouteResult {
  mode: PlanMode
  entryGate: PlanSpot
  exitGate: PlanSpot
  /** true = 入口即出口（原路返回） */
  roundTrip: boolean
  orderedSpots: PlanOrderedSpot[]
  segments: PlanSegment[]
  totalDistanceM: number
  totalWalkMinutes: number
  totalDwellMinutes: number
  /** 预计结束时间 HH:mm */
  estimatedFinish: string
}


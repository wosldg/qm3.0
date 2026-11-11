import http from './http'
import type { ApiResult, TrailNetwork, TrailRouteRequest, TrailRouteResult } from './types'

/** 景区自建步道路网导航（后端 TrailController） */
export const getTrailNetwork = (): Promise<ApiResult<TrailNetwork>> =>
  http.get('/trail/nodes').then((r) => r.data)

/** 起终点坐标 → 步道网最短路径（不依赖高德公开步行规划） */
export const planTrailRoute = (payload: TrailRouteRequest): Promise<ApiResult<TrailRouteResult>> => {
  console.log('[trail.ts] planTrailRoute called with:', JSON.stringify(payload))
  return http.post('/trail/route', payload).then((r) => r.data)
}

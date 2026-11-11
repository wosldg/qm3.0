import http from './http'
import type {
  ApiResult,
  PlanRouteRequest,
  PlanRouteResult,
  PlanSpotListResult,
} from './types'

/** 避峰多途经点导航：可选景点列表（含门标记与实时拥挤度） */
export const getPlanSpots = (): Promise<ApiResult<PlanSpotListResult>> =>
  http.get('/plan/spots').then((r) => {
    console.log('[plan.ts] getPlanSpots response:', r.data)
    return r.data
  })

/** 多途经点路线规划：避峰/最短/自定义三模式，门作起止点 */
export const planMultiRoute = (payload: PlanRouteRequest): Promise<ApiResult<PlanRouteResult>> => {
  console.log('[plan.ts] planMultiRoute called with:', payload)
  return http.post('/plan/route', payload).then((r) => {
    console.log('[plan.ts] planMultiRoute response:', r.data)
    return r.data
  })
}

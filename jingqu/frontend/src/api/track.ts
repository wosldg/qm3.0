import http from './http'
import type { ApiResult, TrackDetail, TrackListResult } from './types'

/** 成熟游览路线（后端 TrackController） */
export const getTrackList = (): Promise<ApiResult<TrackListResult>> =>
  http.get('/track/list').then((r) => r.data)

export const getTrackDetail = (trackId: number): Promise<ApiResult<TrackDetail>> =>
  http.get(`/track/${trackId}`).then((r) => r.data)

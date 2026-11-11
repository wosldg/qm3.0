# 景区客流调度系统 (ScenicFlow)

基于 Vue.js + Drogon(C++) 的景区客流实时监控与调度系统。

## 技术栈

- **前端**: Vue 3 + TypeScript + Vite + Element Plus + 高德地图
- **后端**: C++17 + Drogon Web 框架 + SQLite
- **构建**: CMake + MSVC (Windows)

## 项目结构

```
jingqu/
├── frontend/          # 前端项目 (Vue 3)
├── backend/           # 后端项目 (Drogon C++)
├── data/              # 演示数据库
└── logs/              # 运行日志
```

## 快速开始

### 环境要求

- Windows 10/11
- Visual Studio Build Tools (MSVC x64)
- Node.js 18+
- CMake 3.20+
- vcpkg (用于安装 Drogon 依赖)

### 后端启动

```bash
cd jingqu\backend

# 1. 配置 vcpkg (首次使用)
# 确保 vcpkg 已安装并使用 x64-windows triplet
# vcpkg install drogon:x64-windows sqlite3:x64-windows

# 2. 构建
mkdir build && cd build
cmake .. -DCMAKE_TOOLCHAIN_FILE=<vcpkg_path>/scripts/buildsystems/vcpkg.cmake
cmake --build . --config Release

# 3. 运行 (参数为资源目录路径)
.\Release\jingqu.exe .
```

后端默认监听 `http://0.0.0.0:8848`

### 前端启动

```bash
cd jingqu\frontend

# 1. 安装依赖 (首次使用)
npm install

# 2. 开发模式
npm run dev

# 3. 构建生产版本
npm run build
```

前端默认访问 `http://localhost:5173`，通过 Vite 代理转发 API 请求到后端。

### 登录信息

| 角色 | 用户名 | 密码 |
|------|--------|------|
| 管理员 | admin | admin123 |
| 游客 | tourist | tourist123 |

## API 文档

后端 API 基础路径: `http://localhost:8848`

### 管理端接口 (`/api/*`)

| 接口 | 方法 | 说明 |
|------|------|------|
| `/api/spots` | GET | 获取景点列表 |
| `/api/spots/{id}/flow` | GET | 获取景点流量历史 |
| `/api/parkings` | GET | 获取停车场列表 |
| `/api/shuttles` | GET | 获取摆渡车列表 |
| `/api/events` | GET | 获取告警事件 |
| `/api/events/{id}` | PUT | 更新事件状态 |
| `/api/scheduling` | GET | 获取调度建议 |
| `/api/rules` | GET/POST/PUT/DELETE | 规则配置 CRUD |
| `/api/users` | GET/POST/DELETE | 用户管理 |
| `/api/statistics` | GET | 数据统计 |
| `/api/track/*` | GET | 成熟游览路线 |
| `/api/trail/*` | GET | 步道导航 |
| `/api/plan/*` | GET | 避峰多途经点导航 |

### 用户端接口 (`/api/user/*`)

| 接口 | 方法 | 说明 |
|------|------|------|
| `/api/user/overview` | GET | 景区概览 |
| `/api/user/rankings` | GET | 拥挤度排行 |
| `/api/user/forecast` | GET | 客流预测 |
| `/api/user/parking` | GET | 停车场信息 |
| `/api/user/shuttle-routes` | GET | 摆渡车路线 |
| `/api/user/pois` | GET | 兴趣点列表 |
| `/api/user/recommend-routes` | GET | 路线推荐 |
| `/api/user/spots/{id}` | GET | 景点详情 |

### 认证接口

| 接口 | 方法 | 说明 |
|------|------|------|
| `/api/auth/login` | POST | 登录 |
| `/api/auth/register` | POST | 注册 |

## 注意事项

1. 后端需要使用 MSVC 编译，不支持 MinGW
2. vcpkg 需使用 `x64-windows` triplet
3. 演示数据库 `backend/data/scenicflow.db` 已预置数据
4. 导航功能依赖高德地图 API，需配置 API Key
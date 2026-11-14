# 接入指南 - 队友开发环境配置

## 1. 克隆仓库

```bash
git clone https://github.com/wosldg/qm3.0.git
cd qm3.0\jingqu
```

## 2. 配置后端开发环境

### 2.1 安装 Visual Studio Build Tools

1. 下载 [Visual Studio Build Tools 2022](https://visualstudio.microsoft.com/visual-cpp-build-tools/)
2. 安装时勾选 **"使用 C++ 的桌面开发"**
3. 确保安装以下组件：
   - MSVC v143 - VS 2022 C++ x64/x86 生成工具
   - Windows 10/11 SDK
   - C++ CMake 工具 for Windows

### 2.2 安装 vcpkg

```powershell
# 克隆 vcpkg
cd d:\C++work
git clone https://github.com/microsoft/vcpkg.git
cd vcpkg
.\bootstrap-vcpkg.bat

# 安装依赖 (x64-windows)
.\vcpkg install drogon:x64-windows sqlite3:x64-windows
```

### 2.3 配置环境变量 (可选)

将 vcpkg 路径加入 CMAKE_TOOLCHAIN_FILE：

```powershell
# 添加到系统环境变量
$env:CMAKE_TOOLCHAIN_FILE = "d:\C++work\vcpkg\scripts\buildsystems\vcpkg.cmake"
```

### 2.4 构建后端

```powershell
cd d:\C++work\qmwork2.0\jingqu\backend

# 创建构建目录
mkdir build
cd build

# 配置 (替换为你的 vcpkg 路径)
cmake .. -DCMAKE_TOOLCHAIN_FILE=d:/C++work/vcpkg/scripts/buildsystems/vcpkg.cmake

# 编译
cmake --build . --config Release

# 运行
.\Release\jingqu.exe .
```

运行成功后，后端监听 `http://0.0.0.0:8848`

## 3. 配置前端开发环境

### 3.1 安装 Node.js

下载并安装 [Node.js 18+](https://nodejs.org/)

### 3.2 安装依赖

```powershell
cd d:\C++work\qmwork2.0\jingqu\frontend
npm install
```

### 3.3 启动开发服务器

```powershell
npm run dev
```

前端默认访问 `http://localhost:5173`

### 3.4 API 代理配置

Vite 已配置代理，开发模式下 API 请求会自动转发到 `http://localhost:8848`

如后端端口不同，修改 `frontend/vite.config.ts`：

```typescript
server: {
  proxy: {
    '/api': {
      target: 'http://localhost:8848',  // 修改为你的后端地址
      changeOrigin: true,
    }
  }
}
```

## 4. 常见问题

### Q1: CMake 找不到 Drogon

- 确认 vcpkg 已正确安装 Drogon: `vcpkg list | findstr drogon`
- 确认 CMAKE_TOOLCHAIN_FILE 路径正确
- 尝试重新安装: `vcpkg install drogon:x64-windows`

### Q2: MSVC 编译报错

- 确认使用 x64 原生命令提示符 (x64 Native Tools Command Prompt)
- 确认安装了 Windows SDK
- 清理后重新构建: `Remove-Item build -Recurse -Force; mkdir build`

### Q3: 前端请求 404

- 确认后端已启动在 8848 端口
- 检查 `vite.config.ts` 中的代理配置
- 清除浏览器缓存

### Q4: 数据库加载失败

- 确认 `backend/data/scenicflow.db` 文件存在
- 检查文件权限
- 如需重置数据，删除 `.db` 文件后重启后端（会自动初始化）

## 5. 开发建议

### 后端开发

- 修改代码后需重新编译: `cmake --build . --config Release`
- 日志输出到 `logs/` 目录
- 使用 `app.json` 配置日志级别、数据库路径等

### 前端开发

- 使用 TypeScript，注意类型定义在 `src/api/types.ts`
- Mock 数据开关: 设置环境变量 `VITE_USE_MOCK=true` 可切换本地 Mock
- API 调用统一使用 `src/api/index.ts` 导出的函数

### 代码提交

- 不要提交 `node_modules/`, `build/`, `dist/`, `logs/`
- 不要提交 `.db-wal`, `.db-shm` 等 SQLite 临时文件
- 测试文档和设计文档已排除在 git 之外

## 6. 联系方式

有问题请联系项目负责人。
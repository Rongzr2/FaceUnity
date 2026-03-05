# FaceUnity — 实时摄像头美颜/滤镜桌面应用

## 一、项目概述

基于 **Qt 6 + OpenGL 3.3 Core + WinRT MediaCapture** 构建的实时视频美颜处理桌面应用。
摄像头画面经 GPU 多 Pass 滤镜链实时处理后上屏，支持美颜磨皮、瘦脸液化、画质锐化、绿幕抠图、LUT 风格滤镜、品牌水印、滚动字幕等 7 大特效的自由组合与参数调节。

### 技术栈

| 层级 | 技术 |
|------|------|
| 构建 | qmake / MSVC / C++17 (`/await`) |
| UI 框架 | Qt 6.7 (Widgets + OpenGLWidgets) |
| 图形渲染 | OpenGL 3.3 Core Profile / GLSL 330 |
| 视频采集 | WinRT `MediaCapture` + `MediaFrameReader` API |
| 系统互操作 | C++/WinRT、COM (IMemoryBufferByteAccess) |
| 链接库 | d3d11, dxgi, d3dcompiler, windowsapp 等 |

### 文件结构

```
FaceUnity/
├── main.cpp              # 程序入口
├── mainwindow.h/cpp      # 主窗口 UI、控件布局、信号槽连接
├── CameraCapture.h/cpp   # 摄像头采集模块 (WinRT)
├── VideoRenderWidget.h   # OpenGL 渲染窗口 + 滤镜管线
├── FilterContext.h        # 滤镜基类 AbstractFilter
├── Filters.h             # 7 种具体滤镜实现
├── Logger.h              # 线程安全日志系统
└── FaceUnity.pro         # qmake 工程文件
```

---

## 二、核心架构

### 2.1 整体数据流

```
摄像头 (WinRT MediaFrameReader)
       │  SoftwareBitmap → BGRA8 像素
       ▼
  CameraCapture (CPU 像素缓冲区, std::mutex 保护)
       │  getLatestFrame() 拷贝到渲染线程
       ▼
  VideoRenderWidget::paintGL()
       │  上传 → QOpenGLTexture (rawTexture)
       │
       ▼  ┌────────────────────────────────┐
  Ping-Pong FBO 滤镜链：                    │
  │ rawTexture ──▶ Filter1 ──▶ FBO_A       │
  │ FBO_A.tex  ──▶ Filter2 ──▶ FBO_B       │
  │ FBO_B.tex  ──▶ Filter3 ──▶ FBO_A       │
  │ ...                                     │
  └────────────────────────────────────────┘
       │  最终纹理
       ▼
  PassthroughFilter → 默认帧缓冲 → 屏幕
```

### 2.2 线程模型

```
┌──────────────────┐   ┌────────────────────┐   ┌──────────────────┐
│  Qt 主线程 (UI)   │   │  Camera Worker 线程  │   │  Qt 渲染线程      │
│                  │   │  (WinRT Apartment)  │   │  (OpenGL Context) │
│ - 控件事件       │   │ - MediaCapture 初始化│   │ - paintGL()       │
│ - 信号槽连接     │   │ - FrameArrived 回调  │   │ - 纹理上传        │
│                  │   │ - 像素数据写入 buffer │   │ - FBO 多 Pass     │
└──────────────────┘   └────────────────────┘   └──────────────────┘
          │                      │                       │
          └──────── m_mutex ─────┴──── m_filterMutex ────┘
                   (数据同步)           (滤镜链同步)
```

**关键同步机制**：
- `m_mutex`：保护 `m_pixelBuffer`，Camera Worker 写入、渲染线程读出
- `m_filterMutex`：保护 `m_activeFilters` 容器，UI 线程热插拔、渲染线程遍历
- `m_mutexStop` + `m_cvStop`：Camera Worker 的优雅停止机制（条件变量等待）
- `std::atomic<bool> m_running / m_isDirty / m_hasNewFrame`：无锁标记位

---

## 三、核心模块详解

### 3.1 CameraCapture — 摄像头采集

**文件**: `CameraCapture.h/cpp`

#### 设计要点

1. **WinRT 异步 API 桥接**
   - 在独立 `std::thread` 中调用 `winrt::init_apartment(multi_threaded)` 初始化 COM 环境
   - 使用 `.get()` 将 WinRT 异步操作（`IAsyncOperation`）阻塞转同步
   - `onFrameArrived` 回调在 WinRT 线程池中触发，通过 `std::mutex` 与渲染线程同步

2. **三级降级策略**
   ```
   Strategy::Level1 → SharedReadOnly + 尝试设置 1080p/720p 格式
   Strategy::Level2 → SharedReadOnly + 不设置格式（使用默认）
   Strategy::Level3 → ExclusiveControl（独占模式）
   ```
   当高级策略失败时自动降级，增强不同摄像头硬件的兼容性。

3. **帧数据处理流程**
   ```
   MediaFrameReference → VideoMediaFrame
       ├── SoftwareBitmap (优先，CPU 路径)
       └── Direct3DSurface → CreateCopyFromSurfaceAsync (降级)
   → Convert to BGRA8
   → LockBuffer → IMemoryBufferByteAccess::GetBuffer
   → 逐行 memcpy 到 m_pixelBuffer (处理 Stride 对齐)
   ```

4. **生命周期管理**
   - `stop()` 通过 `condition_variable::notify_all()` 唤醒 Worker 线程
   - `CleanupResources()` 分阶段释放：解绑事件 → StopAsync (1s 超时) → Close → 置空
   - 每步 try-catch 包裹，确保析构不抛异常

#### 面试高频问题

> **Q: 为什么摄像头采集要放在独立线程而不用 Qt 的 QCamera？**
> A: WinRT MediaFrameReader 比 QCamera 提供更底层的帧控制（格式选择、零拷贝 Direct3D Surface 路径），且 WinRT API 需要独立的 COM Apartment。独立线程隔离了 WinRT 事件循环，避免阻塞 Qt 主线程。

> **Q: onFrameArrived 回调的线程安全如何保证？**
> A: 回调运行在 WinRT 线程池中，通过 `std::mutex m_mutex` 保护 `m_pixelBuffer` 的写入。渲染线程通过 `getLatestFrame()` 在同一把锁下读出数据，实现生产者-消费者模型。

---

### 3.2 VideoRenderWidget — OpenGL 渲染管线

**文件**: `VideoRenderWidget.h`

#### Ping-Pong FBO 架构

核心思路：使用两个 FBO (`m_fboA`, `m_fboB`) 交替作为输入/输出，构成任意长度的后处理链。

```cpp
GLuint currentInput = m_rawTexture->textureId();
QOpenGLFramebufferObject* currFBO = m_fboA;
QOpenGLFramebufferObject* nextFBO = m_fboB;

for (auto filter : m_activeFilters) {
    currFBO->bind();                                    // 输出到 currFBO
    filter->process(currentInput, w, h, time, flip);    // 从 currentInput 采样
    currFBO->release();
    currentInput = currFBO->texture();                  // 输出变输入
    std::swap(currFBO, nextFBO);                        // 交换 Ping-Pong
}
// 最终 currentInput → PassthroughFilter → 默认帧缓冲
```

**优势**：
- O(1) 显存占用（无论多少滤镜，始终只有 2 个 FBO）
- 滤镜的增删不影响管线结构，热插拔友好

#### Y 轴翻转处理

摄像头像素数据是 Top-Down 排列（行 0 在图像顶部），但 OpenGL 纹理坐标系 (0,0) 在左下角。解决方案：

- **AbstractFilter** 维护两套 VAO/VBO：标准 Quad（UV 正常）和 Flipped Quad（UV Y 轴翻转）
- 第一个滤镜处理 `rawTexture` 时使用 Flipped Quad（`yFlip=true`），后续 FBO 纹理使用标准 Quad
- Shader 中通过 `isInputFlipped` uniform 让水印/字幕等依赖绝对位置的特效正确计算坐标

#### 面试高频问题

> **Q: 为什么选择 Ping-Pong FBO 而不是 MRT (Multiple Render Targets)？**
> A: 后处理链的每个 Pass 输入依赖上一个 Pass 输出，是串行依赖关系，MRT 适合同时输出多个 G-Buffer 的场景（如延迟渲染）。Ping-Pong 更匹配这种链式处理。

> **Q: 滤镜的动态插拔如何保证线程安全？**
> A: UI 线程通过 `toggleFilter()` 修改 `m_activeFilters`，渲染线程在 `paintGL()` 中遍历它。两者通过 `m_filterMutex` 互斥。滤镜对象本身在 `initializeGL()` 中全部创建完毕，active 列表只增删指针。

---

### 3.3 滤镜系统 — 策略模式 + 模板方法

**文件**: `FilterContext.h`（基类）、`Filters.h`（具体滤镜）

#### 类层次

```
AbstractFilter (模板方法基类)
 ├── PassthroughFilter    — 直通 (最终上屏用)
 ├── BeautySmoothFilter   — 磨皮 (双边滤波)
 ├── FaceSlimFilter       — 瘦脸 (径向网格变形)
 ├── SharpenFilter        — 锐化 (反锐化掩模)
 ├── GreenScreenFilter    — 绿幕抠图 (色度键)
 ├── LutFilter            — LUT 调色 (512x512 查找表)
 ├── WatermarkFilter      — 品牌水印 (Alpha 混合)
 └── SubtitleFilter       — 滚动字幕 (时间驱动位移)
```

#### 模板方法模式

```cpp
class AbstractFilter {
public:
    virtual bool init() {
        initializeOpenGLFunctions();  // 固定步骤
        initQuad();                    // 固定步骤：创建 VAO/VBO
        return initShaders();          // 子类实现：编译链接 Shader
    }

    virtual void process(GLuint inputTex, int w, int h, float time, bool yFlip) {
        m_program->bind();
        // ... 绑定纹理、设置通用 Uniform ...
        onSetUniforms();               // 子类实现：设置特有 Uniform
        if (yFlip) renderQuadFlipped();
        else renderQuad();
        m_program->release();
    }

protected:
    virtual bool initShaders() = 0;    // 纯虚 — 子类必须实现
    virtual void onSetUniforms() {}    // 钩子 — 子类可选覆盖
};
```

#### 各滤镜算法原理

| 滤镜 | 算法 | 核心 Shader 思路 |
|------|------|------------------|
| **美颜磨皮** | 双边滤波 (Bilateral Filter) | 9x9 采样核，同时考虑**空间距离权重** (Gaussian) 和**颜色差异权重**，仅对肤色区域强效模糊，保留边缘。肤色检测基于 RGB 比值规则 |
| **瘦脸液化** | 径向网格变形 (Radial Warp) | 以面部中心为圆心，半径内的像素向圆心收缩。`percent = 1 - ((radius - dist) / radius) * strength`，UV 坐标重映射实现"液化"效果 |
| **锐化** | 反锐化掩模 (Unsharp Mask) | 拉普拉斯算子：`result = center*5 - (top+bottom+left+right)`。与原图按 `amount` 参数混合 |
| **绿幕抠图** | 色度键 (Chroma Key) | 当 `G > (R+B)/2 + threshold` 时判定为绿色背景，输出透明像素 |
| **LUT 调色** | 3D 颜色查找表 | CPU 算法生成 512x512 LUT 纹理（64 个 64x64 方块，Blue 通道索引方块，R/G 索引方块内位置）。Shader 中通过 RGB 值三线性插值查表。支持 6 种预设风格（清凉/粉嫩/怀旧/黑白/赛博朋克） |
| **水印叠加** | Alpha 混合 | QPainter 动态生成文字纹理，Shader 中根据屏幕 UV 计算水印区域，`mix(base, wm, wm.a * opacity)` |
| **滚动字幕** | 时间驱动 UV 位移 | `textUV.x = TexCoord.x - time * speed * 0.2`，配合 `GL_REPEAT` 包裹模式实现无限滚动 |

#### LUT 滤镜深入解析

LUT (Look-Up Table) 是专业调色的核心技术。本项目采用 **算法生成** 而非加载外部 `.cube`/`.png` 文件：

```
512x512 纹理 = 8x8 个方块，每个方块 64x64 像素

编码方式：
  方块坐标 (bx, by) → Blue 通道 = (by * 8 + bx) / 63
  方块内坐标 (ix, iy) → Red = ix/63, Green = iy/63

查询方式 (Shader)：
  1. 用输入像素的 B 值计算方块索引
  2. 用 R, G 值计算方块内 UV
  3. 采样得到映射后的颜色
  4. 相邻 B 方块线性插值消除色阶
```

预设风格通过不同的色彩变换函数生成：
- **怀旧 (Vintage)**：经典 Sepia 矩阵变换 `[0.393, 0.769, 0.189; ...]`
- **黑白 (B&W)**：加权灰度 + S 型对比度曲线
- **赛博朋克**：暗部偏青、亮部偏洋红的分离调色

---

### 3.4 Logger — 线程安全日志

**文件**: `Logger.h`

- **单例模式** (`static Logger inst`)
- **流式宏接口**：`LOG_INFO("msg" << var)` → `std::stringstream` 拼接 → `log()`
- **双模式输出**：Debug 输出到控制台，Release 追加到文件 (带 `flush`)
- **智能类型适配**：通过 SFINAE 模板 `operator<<` 自动检测 `toStdString()` 方法，无缝支持 `QString` 输出，无需引入 Qt 头文件

```cpp
// SFINAE: 如果 T 有 toStdString()，自动转换输出
template<typename T>
auto operator<<(std::ostream& os, const T& t) -> decltype(t.toStdString(), os) {
    return os << t.toStdString();
}
```

---

## 四、设计模式总结

| 模式 | 应用位置 | 说明 |
|------|----------|------|
| **模板方法** | `AbstractFilter::init()` / `process()` | 固定流程，子类实现 `initShaders()` 和 `onSetUniforms()` |
| **策略模式** | `m_activeFilters` 动态组合 | 滤镜作为可替换策略，运行时自由增删 |
| **单例** | `Logger::instance()` | Meyer's Singleton，线程安全 |
| **生产者-消费者** | Camera Worker ↔ 渲染线程 | mutex 保护的共享缓冲区 |
| **观察者** | Qt 信号槽、WinRT 事件 | UI 控件变化通知渲染组件 |
| **优雅降级** | 三级 Camera 启动策略 | 硬件不支持时逐级降低要求 |

---

## 五、关键技术难点与解决方案

### 5.1 摄像头资源生命周期

**问题**：窗口关闭时，`paintGL()` 可能仍在访问已销毁的 Camera 对象。

**解决**：
```cpp
MainWindow::~MainWindow() {
    m_glWidget->setSource(nullptr);  // 1. 停止渲染定时器，断开信号
    m_camera->stop();                // 2. 停止采集线程
    delete m_camera;                 // 3. 释放资源
}
```
严格按照 **停渲染 → 停采集 → 释放** 的顺序，避免悬挂指针。

### 5.2 WinRT COM 线程模型

**问题**：WinRT API 必须在初始化过 COM Apartment 的线程中调用。

**解决**：Camera Worker 线程入口调用 `winrt::init_apartment(multi_threaded)`，所有 WinRT 异步操作均在此线程内执行。设备枚举 `getCameras()` 也在临时线程中完成。

### 5.3 OpenGL 纹理 Y 轴翻转

**问题**：摄像头像素 Top-Down 排列 vs OpenGL 纹理坐标 Bottom-Up，导致画面和水印/字幕位置均倒转。

**解决**：双 VAO 方案——为每个 Filter 准备标准 Quad 和 Flipped Quad，第一个处理摄像头原始纹理的 Filter 使用 Flipped Quad 修正 Y 轴。同时通过 `isInputFlipped` uniform 让 Shader 中的绝对坐标计算（水印位置、字幕条带）也能正确工作。

### 5.4 LUT 纹理跨线程生成

**问题**：UI 线程切换 LUT 预设时需要生成 `QOpenGLTexture`，但 OpenGL 上下文绑定在渲染线程。

**解决**：在 `setLutPreset()` 中显式调用 `makeCurrent()` / `doneCurrent()` 临时获取 GL 上下文，确保纹理创建在正确的上下文中执行。

---

## 六、性能分析

| 指标 | 说明 |
|------|------|
| 渲染帧率 | ~60fps (16ms 定时器驱动) |
| 显存占用 | rawTexture + 2 FBO + 各滤镜纹理 (LUT 512x512 + 水印 300x128 + 字幕) |
| CPU→GPU 带宽 | 每帧 1 次 `setData()` 上传 (1280x720x4 ≈ 3.5MB/帧) |
| 磨皮 Shader 复杂度 | 9x9=81 次纹理采样/像素（最重的 Pass） |

**可优化方向**：
- 双边滤波可拆为水平+垂直两趟 Pass，从 O(N^2) 降到 O(2N)
- PBO (Pixel Buffer Object) 异步上传替代同步 `setData()`
- 摄像头 GPU 零拷贝路径：Direct3D Surface → DXGI 共享纹理 → OpenGL 互操作

---

## 七、面试常见追问

### 图形渲染类

**Q: FBO 是什么？为什么后处理需要 FBO？**
A: Framebuffer Object 是 OpenGL 的离屏渲染目标。后处理链中每个 Pass 的输出不能直接上屏，需要先渲染到 FBO 附带的纹理上，作为下一个 Pass 的输入。

**Q: 为什么用 OpenGL 3.3 Core Profile 而不用更高版本？**
A: 3.3 Core 是移除了所有固定管线功能的最低版本，提供了完整的可编程管线能力（VAO/VBO、Shader、FBO），同时兼容绝大多数 GPU。

**Q: Shader 中 `uniform` 和 `in/out` 的区别？**
A: `uniform` 是 CPU 传给 GPU 的全局常量（如分辨率、强度参数），在一次 Draw Call 中对所有顶点/片元相同。`in/out` 是着色器阶段之间传递的插值变量（如纹理坐标）。

### 多线程类

**Q: `std::mutex` vs `std::atomic` 的使用场景？**
A: `atomic` 适合单个变量的无锁读写（如 `m_isDirty` 标志位）。`mutex` 适合保护复合操作（如同时读写 `m_pixelBuffer` 的多个字段、或遍历 `m_activeFilters` 容器）。

**Q: 条件变量 `m_cvStop` 的作用？**
A: Camera Worker 线程启动成功后进入 `m_cvStop.wait()` 休眠，避免忙等。`stop()` 调用 `notify_all()` 唤醒线程使其退出，实现优雅停止。

### C++ 语言类

**Q: `ComPtr<T>` 是什么？为什么不用裸指针？**
A: `Microsoft::WRL::ComPtr` 是 COM 对象的智能指针，自动管理 `AddRef/Release` 引用计数，防止 COM 对象泄漏。

**Q: Logger 中 SFINAE 模板的原理？**
A: `decltype(t.toStdString(), os)` 利用逗号表达式——如果 `T` 没有 `toStdString()` 方法，`decltype` 推导失败（SFINAE），编译器不会选择此重载。这让 `LOG_INFO` 宏能同时支持 `std::string` 和 `QString` 而不引入 Qt 依赖。

---

## 八、构建与运行

```bash
# 环境要求
# - Windows 10/11
# - Qt 6.7+ (MSVC 64-bit)
# - Windows SDK (WinRT 支持)

# 使用 Qt Creator 打开 FaceUnity.pro 构建即可
# 或命令行：
qmake FaceUnity.pro
nmake
```

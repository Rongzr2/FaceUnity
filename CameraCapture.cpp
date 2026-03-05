#include "CameraCapture.h"
#include <windows.h>
#include <inspectable.h>
#include <algorithm>
#include <thread>
#include <chrono>

using namespace winrt;
using namespace winrt::Windows::Media::Capture;
using namespace winrt::Windows::Media::Capture::Frames;
using namespace winrt::Windows::Graphics::Imaging;
using namespace winrt::Windows::Media::MediaProperties;
using namespace winrt::Windows::Devices::Enumeration;

extern "C" {
struct __declspec(uuid("5b0d3235-4dba-4d44-865e-8f1d0e4fd04d")) IMemoryBufferByteAccess : ::IUnknown
{
    virtual HRESULT __stdcall GetBuffer(uint8_t** value, uint32_t* capacity) = 0;
};
struct __declspec(uuid("A9B3D012-3DF2-4EE3-B8D1-8695F457D3C1")) IDirect3DDxgiInterfaceAccess : ::IUnknown
{
    virtual HRESULT __stdcall GetInterface(const GUID & iid, void** p) = 0;
};
}

CameraCapture::CameraCapture() {
}

CameraCapture::~CameraCapture() {
    stop();
}

std::vector<std::pair<QString, QString>> CameraCapture::getCameras() {
    std::vector<std::pair<QString, QString>> list;
    std::thread t([&]() {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
        try {
            auto devices = DeviceInformation::FindAllAsync(DeviceClass::VideoCapture).get();
            for (auto const& dev : devices) {
                list.push_back({ QString::fromStdWString(dev.Name().c_str()), QString::fromStdWString(dev.Id().c_str()) });
            }
        } catch (...) { LOG_ERROR("Enum Cameras Failed"); }
    });
    if (t.joinable()) t.join();
    return list;
}

void CameraCapture::configure(const QString& deviceId, ID3D11Device* device) {
    m_targetDeviceId = deviceId;
    m_d3dDevice = device;
}

bool CameraCapture::start() {
    if (m_running) return true;

    // [关键修复] 移除了 !m_d3dDevice 的强制检查
    // 允许在 device 为 nullptr 的情况下启动（即 CPU 模式）
    if (m_targetDeviceId.isEmpty()) {
        LOG_WARN("Camera Start Ignored: Invalid DeviceId");
        return false;
    }

    stop();

    m_running = true;
    m_workerThread = std::thread(&CameraCapture::workerThreadFunc, this, m_targetDeviceId);
    return true;
}

// 辅助函数：安全清理资源，绝不抛出异常
void CleanupResources(MediaCapture& mc, MediaFrameReader& reader, winrt::event_token& token) {
    if (reader) {
        try {
            if (token) {
                reader.FrameArrived(token);
                token = winrt::event_token{};
            }
        } catch (...) {}

        try {
            auto op = reader.StopAsync();
            if(op.wait_for(std::chrono::milliseconds(1000)) == winrt::Windows::Foundation::AsyncStatus::Completed) {
                // op.get();
            }
        } catch (...) {
            LOG_WARN("Reader StopAsync failed/timeout");
        }

        try { reader.Close(); } catch (...) {}
        reader = nullptr;
    }

    if (mc) {
        try { mc.Close(); } catch (...) {}
        mc = nullptr;
    }
}

void CameraCapture::stop() {
    {
        std::lock_guard<std::mutex> lock(m_mutexStop);
        if (!m_running) return;
        m_running = false;
        m_cvStop.notify_all();
    }

    if (m_workerThread.joinable()) {
        try {
            m_workerThread.join();
        } catch (const std::system_error& e) {
            LOG_ERROR("CameraCapture: Join failed: " << e.what());
        } catch (...) {
            LOG_ERROR("CameraCapture: Join failed with unknown error.");
        }
    }

    LOG_INFO("CameraCapture stopped successfully.");
}

void CameraCapture::workerThreadFunc(QString deviceId) {
    // 1. 初始化 COM
    try {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
    } catch (...) {
        LOG_ERROR("Failed to init WinRT apartment");
        return;
    }

    // 2. 线程主体防护
    try {
        LOG_INFO("Worker Thread Started: " << deviceId.toStdString());

        enum class Strategy { Level1, Level2, Level3 };
        Strategy strategies[] = { Strategy::Level1, Strategy::Level2, Strategy::Level3 };

        bool success = false;

        // 外层循环：策略重试
        for (auto strategy : strategies) {
            {
                std::lock_guard<std::mutex> lock(m_mutexStop);
                if (!m_running) break;
            }

            // 清理旧状态
            m_mediaCapture = nullptr;
            m_frameReader = nullptr;
            m_frameArrivedToken = winrt::event_token{};

            try {
                MediaCaptureInitializationSettings settings;
                settings.VideoDeviceId(deviceId.toStdWString());
                settings.StreamingCaptureMode(StreamingCaptureMode::Video);
                settings.MemoryPreference(MediaCaptureMemoryPreference::Auto); // CPU会自动选择

                if (strategy == Strategy::Level3) settings.SharingMode(MediaCaptureSharingMode::ExclusiveControl);
                else settings.SharingMode(MediaCaptureSharingMode::SharedReadOnly);

                if (!m_running) break;

                // --- 阶段 1: Initialize ---
                m_mediaCapture = MediaCapture();
                m_mediaCapture.Failed({ this, &CameraCapture::onCaptureFailed });

                m_mediaCapture.InitializeAsync(settings).get();

                if (!m_running) throw std::runtime_error("Stop requested during initialization");

                // --- 阶段 2: Find Source ---
                auto frameSources = m_mediaCapture.FrameSources();
                MediaFrameSource bestSource = nullptr;
                for (auto const& kv : frameSources) {
                    if (kv.Value().Info().SourceKind() == MediaFrameSourceKind::Color) {
                        bestSource = kv.Value(); break;
                    }
                }
                if (!bestSource && frameSources.Size() > 0) bestSource = frameSources.First().Current().Value();
                if (!bestSource) throw std::runtime_error("No FrameSource found");

                // --- 阶段 3: Set Format (Strategy 1 Only) ---
                if (strategy == Strategy::Level1) {
                    auto formats = bestSource.SupportedFormats();
                    MediaFrameFormat targetFormat = nullptr;

                    // 简单查找逻辑：优先 1080p，其次 720p，且必须是 NV12 或 YUY2 或 BGRA8
                    for (auto const& fmt : formats) {
                        // 这里可以根据需要添加更复杂的格式筛选
                        if (fmt.VideoFormat().Width() == 1920 && fmt.VideoFormat().Height() == 1080) {
                            targetFormat = fmt;
                            break;
                        }
                    }
                    if(!targetFormat) {
                        for (auto const& fmt : formats) {
                            if (fmt.VideoFormat().Width() == 1280 && fmt.VideoFormat().Height() == 720) {
                                targetFormat = fmt;
                                break;
                            }
                        }
                    }

                    if (targetFormat) {
                        if (!m_running) break;
                        bestSource.SetFormatAsync(targetFormat).get();
                    }
                }

                // --- 阶段 4: Create Reader ---
                if (!m_running) break;
                m_frameReader = m_mediaCapture.CreateFrameReaderAsync(bestSource).get();
                m_frameArrivedToken = m_frameReader.FrameArrived({ this, &CameraCapture::onFrameArrived });

                // --- 阶段 5: Start ---
                if (!m_running) break;
                auto status = m_frameReader.StartAsync().get();

                if (status == MediaFrameReaderStartStatus::Success) {
                    LOG_INFO("Camera Started Successfully (Strategy " << (int)strategy << ")");
                    success = true;
                    break;
                } else {
                    LOG_WARN("Start failed status: " << (int)status);
                    CleanupResources(m_mediaCapture, m_frameReader, m_frameArrivedToken);
                }

            } catch (winrt::hresult_error const& ex) {
                LOG_WARN("Strategy WinRT Error: 0x" << std::hex << ex.code() << ": " << winrt::to_string(ex.message()));
                CleanupResources(m_mediaCapture, m_frameReader, m_frameArrivedToken);
                if (!m_running) break;
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
            } catch (std::exception const& ex) {
                LOG_WARN("Strategy Std Error: " << ex.what());
                CleanupResources(m_mediaCapture, m_frameReader, m_frameArrivedToken);
                if (!m_running) break;
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
            } catch (...) {
                LOG_WARN("Strategy Unknown Error");
                CleanupResources(m_mediaCapture, m_frameReader, m_frameArrivedToken);
                if (!m_running) break;
            }
        }

        if (success && m_running) {
            std::unique_lock<std::mutex> lock(m_mutexStop);
            m_cvStop.wait(lock, [this] { return !m_running; });
        }

        CleanupResources(m_mediaCapture, m_frameReader, m_frameArrivedToken);

    } catch (...) {
        LOG_ERROR("CRITICAL: WorkerThread crashed with unhandled exception.");
        try { CleanupResources(m_mediaCapture, m_frameReader, m_frameArrivedToken); } catch(...) {}
    }

    LOG_INFO("Worker Thread Exited Gracefully.");
}

void CameraCapture::onCaptureFailed(MediaCapture const&, MediaCaptureFailedEventArgs const& args) {
    LOG_ERROR("CAMERA ERROR EVENT: " << QString::fromStdWString(args.Message().c_str()).toStdString() << " Code: " << args.Code());
}

void CameraCapture::onFrameArrived(MediaFrameReader const& sender, MediaFrameArrivedEventArgs const&) {
    try {
        auto frame = sender.TryAcquireLatestFrame();
        if (!frame) return;

        // 这里仅实现 CPU 路径，因为 m_d3dDevice 可能为空
        bool enableGpuPath = false;

        // ... GPU Path 省略或不执行 ...

        m_useGpuPath = false;
        auto bitmap = frame.VideoMediaFrame().SoftwareBitmap();
        if (!bitmap) {
            auto surface = frame.VideoMediaFrame().Direct3DSurface();
            if (surface) bitmap = SoftwareBitmap::CreateCopyFromSurfaceAsync(surface).get();
        }

        if (!bitmap) return;

        if (bitmap.BitmapPixelFormat() != BitmapPixelFormat::Bgra8 ||
            bitmap.BitmapAlphaMode() == BitmapAlphaMode::Premultiplied) {
            bitmap = SoftwareBitmap::Convert(bitmap, BitmapPixelFormat::Bgra8, BitmapAlphaMode::Premultiplied);
        }

        auto buffer = bitmap.LockBuffer(BitmapBufferAccessMode::Read);
        auto ref = buffer.CreateReference();
        auto byteAccess = ref.as<IMemoryBufferByteAccess>();

        uint8_t* data = nullptr;
        uint32_t capacity = 0;
        if (SUCCEEDED(byteAccess->GetBuffer(&data, &capacity))) {
            std::lock_guard<std::mutex> lock(m_mutex);

            int w = bitmap.PixelWidth();
            int h = bitmap.PixelHeight();

            if (m_pixelBuffer.size() != w * h * 4) {
                m_pixelBuffer.resize(w * h * 4);
                m_width = w;
                m_height = h;
            }

            auto plane = buffer.GetPlaneDescription(0);
            for (int y = 0; y < h; ++y) {
                memcpy(m_pixelBuffer.data() + y * w * 4, data + plane.StartIndex + y * plane.Stride, w * 4);
            }
            m_isDirty = true;
            m_hasNewFrame = true;
        }
    } catch (...) {}
}

Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> CameraCapture::getTexture(ID3D11DeviceContext* ctx) {
    std::lock_guard<std::mutex> lock(m_mutex);

    // 如果没有 D3D 设备，直接返回 nullptr
    if(!m_d3dDevice) return nullptr;

    if (m_useGpuPath) {
        if (m_gpuTexture) {
            if (m_isDirty) {
                D3D11_TEXTURE2D_DESC desc;
                m_gpuTexture->GetDesc(&desc);

                D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
                srvDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
                if (desc.Format == DXGI_FORMAT_NV12) {
                    srvDesc.Format = DXGI_FORMAT_R8_UNORM;
                } else {
                    srvDesc.Format = desc.Format;
                }

                srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
                srvDesc.Texture2D.MipLevels = 1;

                m_d3dDevice->CreateShaderResourceView(m_gpuTexture.Get(), &srvDesc, &m_gpuSrv);
                m_isDirty = false;
            }
            return m_gpuSrv;
        }
    } else {
        if (!m_isDirty && m_cpuSrv) return m_cpuSrv;
        if (m_pixelBuffer.empty()) return nullptr;

        bool recreate = (!m_cpuTexture);
        if (m_cpuTexture) {
            D3D11_TEXTURE2D_DESC desc;
            m_cpuTexture->GetDesc(&desc);
            if (desc.Width != (UINT)m_width || desc.Height != (UINT)m_height) recreate = true;
        }

        if (recreate) {
            D3D11_TEXTURE2D_DESC desc = {};
            desc.Width = m_width;
            desc.Height = m_height;
            desc.MipLevels = 1;
            desc.ArraySize = 1;
            desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
            desc.SampleDesc.Count = 1;
            desc.Usage = D3D11_USAGE_DEFAULT;
            desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

            D3D11_SUBRESOURCE_DATA data = {};
            data.pSysMem = m_pixelBuffer.data();
            data.SysMemPitch = m_width * 4;

            m_cpuSrv.Reset();
            m_cpuTexture.Reset();
            if (m_d3dDevice) {
                m_d3dDevice->CreateTexture2D(&desc, &data, &m_cpuTexture);
                if (m_cpuTexture) {
                    m_d3dDevice->CreateShaderResourceView(m_cpuTexture.Get(), nullptr, &m_cpuSrv);
                }
            }
        } else if (ctx && m_cpuTexture && m_isDirty) {
            ctx->UpdateSubresource(m_cpuTexture.Get(), 0, nullptr, m_pixelBuffer.data(), m_width * 4, 0);
        }
        m_isDirty = false;
        return m_cpuSrv;
    }
    return nullptr;
}

void CameraCapture::getSize(int &w, int &h)
{
    w = m_width;
    h = m_height;
}

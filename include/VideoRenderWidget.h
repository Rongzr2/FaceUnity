#pragma once
#include <QOpenGLWidget>
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLTexture>
#include <QOpenGLFramebufferObject>
#include <QTimer>
#include <QElapsedTimer>
#include <mutex>
#include <vector>
#include <algorithm>
#include "CameraCapture.h"
#include "Filters.h"

class VideoRenderWidget : public QOpenGLWidget, protected QOpenGLFunctions_3_3_Core {
    Q_OBJECT
public:
    explicit VideoRenderWidget(QWidget* parent = nullptr) : QOpenGLWidget(parent) {}

    ~VideoRenderWidget() {
        makeCurrent();
        for(auto f : m_filters) delete f;
        if (m_finalPass) delete m_finalPass;
        if(m_fboA) delete m_fboA;
        if(m_fboB) delete m_fboB;
        if(m_rawTexture) delete m_rawTexture;
        doneCurrent();
    }

    void setSource(CameraCapture* cap) {
        m_camera = cap;
        if (m_camera) {
            // 只有有相机时才启动定时器
            connect(&m_refreshTimer, &QTimer::timeout, this, [this](){
                this->update();
            });
            m_refreshTimer.start(16);
        } else {
            // 如果传入 nullptr，停止刷新，断开连接，防止后续回调
            m_refreshTimer.stop();
            m_refreshTimer.disconnect();
        }
    }

    void toggleFilter(const QString& name, bool enable) {
        std::lock_guard<std::mutex> lock(m_filterMutex);
        if (enable) {
            bool exists = false;
            for(auto* f : m_activeFilters) {
                if(f == m_filters[name]) { exists = true; break; }
            }
            if (!exists && m_filters.contains(name)) {
                // 保证顺序：美颜 -> 滤镜 -> 水印 -> 字幕
                // 简单的做法是直接 push，复杂做法是按优先级 insert
                // 这里为了演示直接 push，实际体验中顺序影响叠加效果
                m_activeFilters.push_back(m_filters[name]);
            }
        } else {
            auto it = std::remove_if(m_activeFilters.begin(), m_activeFilters.end(),
                                     [&](AbstractFilter* f) {
                                         // 检查指针匹配即可
                                         return f == m_filters[name];
                                     });
            m_activeFilters.erase(it, m_activeFilters.end());
        }
    }

    void updateFilterParam(const QString& name, const QString& key, float value) {
        std::lock_guard<std::mutex> lock(m_filterMutex);
        if (m_filters.contains(name)) {
            m_filters[name]->setParameter(key, value);
        }
    }

    void setLutPreset(int index) {
        std::lock_guard<std::mutex> lock(m_filterMutex);
        if (m_filters.contains("Lut")) {
            auto lut = dynamic_cast<LutFilter*>(m_filters["Lut"]);
            if (lut) {
                // 在 OpenGL 线程外修改纹理是不安全的，但由于我们是在 CPU 生成 QImage
                // 然后在下一次 paintGL 时绑定，只要加锁保护通常没问题。
                // 严格来说应该用 makeCurrent，但为了代码简洁，这里直接调用生成逻辑，
                // LutFilter 内部生成 QOpenGLTexture 时如果上下文不对会报警告，
                // 建议在 paintGL 周期内生成，或者在这里 makeCurrent。

                makeCurrent();
                lut->switchPreset(index);
                doneCurrent();
            }
        }
    }

protected:
    void initializeGL() override {
        initializeOpenGLFunctions();

        // 实例化所有滤镜
        m_filters["Beauty"] = new BeautySmoothFilter();
        m_filters["Slim"] = new FaceSlimFilter();
        m_filters["Sharpen"] = new SharpenFilter();
        m_filters["Green"] = new GreenScreenFilter();
        // [新增]
        m_filters["Lut"] = new LutFilter();
        m_filters["Watermark"] = new WatermarkFilter();
        m_filters["Subtitle"] = new SubtitleFilter();

        m_finalPass = new PassthroughFilter();
        m_finalPass->init();

        for(auto f : m_filters) f->init();
        m_time.start();
    }

    void resizeGL(int w, int h) override {
        if (m_fboA) delete m_fboA;
        if (m_fboB) delete m_fboB;
        QOpenGLFramebufferObjectFormat format;
        format.setAttachment(QOpenGLFramebufferObject::NoAttachment);
        format.setInternalTextureFormat(GL_RGBA);
        m_fboA = new QOpenGLFramebufferObject(w, h, format);
        m_fboB = new QOpenGLFramebufferObject(w, h, format);
    }

    void paintGL() override {
        if (!m_camera) {
            glClearColor(0, 0, 0, 1);
            glClear(GL_COLOR_BUFFER_BIT);
            return;
        }

        int camW, camH;
        m_camera->getSize(camW, camH);
        if (camW <= 0 || camH <= 0) return;

        if (m_rawTexture == nullptr || m_rawTexture->width() != camW || m_rawTexture->height() != camH) {
            if (m_rawTexture) delete m_rawTexture;
            m_rawTexture = new QOpenGLTexture(QOpenGLTexture::Target2D);
            m_rawTexture->setSize(camW, camH);
            m_rawTexture->setFormat(QOpenGLTexture::RGBA8_UNorm);
            m_rawTexture->allocateStorage();
            m_rawTexture->setMinMagFilters(QOpenGLTexture::Linear, QOpenGLTexture::Linear);
            m_rawTexture->setWrapMode(QOpenGLTexture::ClampToEdge);
        }

        static std::vector<uint8_t> tempBuffer;
        int bufSize = camW * camH * 4;
        if (tempBuffer.size() != bufSize) tempBuffer.resize(bufSize);
        m_camera->getLatestFrame(tempBuffer.data(), bufSize);
        m_rawTexture->setData(QOpenGLTexture::BGRA, QOpenGLTexture::UInt8, tempBuffer.data());

        // ---------------- 渲染逻辑 ----------------
        GLuint currentInput = m_rawTexture->textureId();

        QOpenGLFramebufferObject* currFBO = m_fboA;
        QOpenGLFramebufferObject* nextFBO = m_fboB;

        {
            std::lock_guard<std::mutex> lock(m_filterMutex);

            for (auto filter : m_activeFilters) {
                if(!currFBO) break;

                currFBO->bind();
                glViewport(0, 0, currFBO->width(), currFBO->height());
                glClear(GL_COLOR_BUFFER_BIT);

                // 只有 raw texture 需要 Y 轴翻转
                bool needsFlip = (currentInput == m_rawTexture->textureId());
                filter->process(currentInput, currFBO->width(), currFBO->height(), m_time.elapsed() / 1000.0f, needsFlip);

                currFBO->release();

                currentInput = currFBO->texture();
                std::swap(currFBO, nextFBO);
            }
        }

        // ---------------- 最终上屏 ----------------
        glBindFramebuffer(GL_FRAMEBUFFER, defaultFramebufferObject());
        glViewport(0, 0, width() * devicePixelRatio(), height() * devicePixelRatio());
        glClearColor(0, 0, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT);

        bool finalNeedsFlip = (currentInput == m_rawTexture->textureId());
        if (m_finalPass) {
            m_finalPass->process(currentInput, width(), height(), 0.0f, finalNeedsFlip);
        }
    }

private:
    CameraCapture* m_camera = nullptr;
    QTimer m_refreshTimer;
    QElapsedTimer m_time;

    QOpenGLTexture* m_rawTexture = nullptr;
    QOpenGLFramebufferObject *m_fboA = nullptr, *m_fboB = nullptr;

    QMap<QString, AbstractFilter*> m_filters;
    std::vector<AbstractFilter*> m_activeFilters;
    std::mutex m_filterMutex;

    PassthroughFilter* m_finalPass = nullptr;
};

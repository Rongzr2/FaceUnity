#pragma once
#include "FilterContext.h"
#include <cmath>
#include <QImage>
#include <QPainter>
#include <QFontMetrics>

// ================== 1. 基础直通滤镜 (PassThrough) ==================
class PassthroughFilter : public AbstractFilter {
protected:
    bool initShaders() override {
        m_program = new QOpenGLShaderProgram();
        m_program->addShaderFromSourceCode(QOpenGLShader::Vertex,
                                           "#version 330 core\n"
                                           "layout(location = 0) in vec2 aPos;\n"
                                           "layout(location = 1) in vec2 aTexCoord;\n"
                                           "out vec2 TexCoord;\n"
                                           "void main() { gl_Position = vec4(aPos, 0.0, 1.0); TexCoord = aTexCoord; }");
        m_program->addShaderFromSourceCode(QOpenGLShader::Fragment,
                                           "#version 330 core\n"
                                           "in vec2 TexCoord;\n"
                                           "uniform sampler2D inputTexture;\n"
                                           "out vec4 FragColor;\n"
                                           "void main() { FragColor = texture(inputTexture, TexCoord); }");
        return m_program->link();
    }
};

// ================== 2. 企业级磨皮 (Skin Smoothing) ==================
class BeautySmoothFilter : public AbstractFilter {
public:
    BeautySmoothFilter() {
        m_params["intensity"] = 0.5f;
        m_params["radius"] = 0.0f;
    }

protected:
    bool initShaders() override {
        m_program = new QOpenGLShaderProgram();
        m_program->addShaderFromSourceCode(QOpenGLShader::Vertex,
                                           "#version 330 core\n"
                                           "layout(location = 0) in vec2 aPos;\n"
                                           "layout(location = 1) in vec2 aTexCoord;\n"
                                           "out vec2 TexCoord;\n"
                                           "void main() { gl_Position = vec4(aPos, 0.0, 1.0); TexCoord = aTexCoord; }");

        m_program->addShaderFromSourceCode(QOpenGLShader::Fragment,
                                           "#version 330 core\n"
                                           "in vec2 TexCoord;\n"
                                           "uniform sampler2D inputTexture;\n"
                                           "uniform vec2 resolution;\n"
                                           "uniform float intensity;\n"
                                           "out vec4 FragColor;\n"
                                           "\n"
                                           "bool isSkin(vec3 color) {\n"
                                           "    float r = color.r; float g = color.g; float b = color.b;\n"
                                           "    return (r > 0.37 && g > 0.21 && b > 0.14 && r > g && r > b && (max(max(r, g), b) - min(min(r, g), b)) > 0.058);\n"
                                           "}\n"
                                           "\n"
                                           "void main() {\n"
                                           "    vec4 centerColor = texture(inputTexture, TexCoord);\n"
                                           "    if (intensity <= 0.01) {\n"
                                           "        FragColor = centerColor;\n"
                                           "        return;\n"
                                           "    }\n"
                                           "    float skinFactor = isSkin(centerColor.rgb) ? 1.0 : 0.0;\n"
                                           "    if (skinFactor < 0.5) skinFactor = 0.1;\n"
                                           "    float blurRadius = 3.0 + (intensity * 5.0);\n"
                                           "    vec2 texelSize = 1.0 / resolution;\n"
                                           "    vec3 sumColor = vec3(0.0);\n"
                                           "    float sumWeight = 0.0;\n"
                                           "    for(int x = -4; x <= 4; x++) {\n"
                                           "        for(int y = -4; y <= 4; y++) {\n"
                                           "            vec2 offset = vec2(float(x), float(y)) * blurRadius * 0.5;\n"
                                           "            vec2 uv = TexCoord + offset * texelSize;\n"
                                           "            vec3 sampleColor = texture(inputTexture, uv).rgb;\n"
                                           "            float distSq = float(x*x + y*y);\n"
                                           "            float spatialWeight = exp(-distSq / (2.0 * 4.0));\n"
                                           "            vec3 diff = sampleColor - centerColor.rgb;\n"
                                           "            float colorDistSq = dot(diff, diff);\n"
                                           "            float rangeSigma = 0.05 + (intensity * 0.2);\n"
                                           "            float colorWeight = exp(-colorDistSq / (2.0 * rangeSigma * rangeSigma));\n"
                                           "            float totalWeight = spatialWeight * colorWeight;\n"
                                           "            sumColor += sampleColor * totalWeight;\n"
                                           "            sumWeight += totalWeight;\n"
                                           "        }\n"
                                           "    }\n"
                                           "    vec3 smoothResult = sumColor / sumWeight;\n"
                                           "    float finalMix = clamp(intensity * skinFactor, 0.0, 1.0);\n"
                                           "    FragColor = vec4(mix(centerColor.rgb, smoothResult, finalMix), centerColor.a);\n"
                                           "}");
        return m_program->link();
    }

    void onSetUniforms() override {
        m_program->setUniformValue("intensity", m_params["intensity"].toFloat());
    }
};

// ================== 3. 瘦脸 (Slimming) ==================
class FaceSlimFilter : public AbstractFilter {
public:
    FaceSlimFilter() {
        m_params["strength"] = 0.0f;
        m_params["centerX"] = 0.5f;
        m_params["centerY"] = 0.5f;
        m_params["radius"] = 0.2f;
    }

protected:
    bool initShaders() override {
        m_program = new QOpenGLShaderProgram();
        m_program->addShaderFromSourceCode(QOpenGLShader::Vertex,
                                           "#version 330 core\n"
                                           "layout(location = 0) in vec2 aPos;\n"
                                           "layout(location = 1) in vec2 aTexCoord;\n"
                                           "out vec2 TexCoord;\n"
                                           "void main() { gl_Position = vec4(aPos, 0.0, 1.0); TexCoord = aTexCoord; }");

        m_program->addShaderFromSourceCode(QOpenGLShader::Fragment,
                                           "#version 330 core\n"
                                           "in vec2 TexCoord;\n"
                                           "uniform sampler2D inputTexture;\n"
                                           "uniform float strength;\n"
                                           "uniform vec2 center;\n"
                                           "uniform float radius;\n"
                                           "out vec4 FragColor;\n"
                                           "vec2 warp(vec2 uv, vec2 center, float radius, float strength) {\n"
                                           "    vec2 delta = uv - center;\n"
                                           "    float dist = length(delta);\n"
                                           "    if (dist < radius) {\n"
                                           "        float percent = 1.0 - ((radius - dist) / radius) * strength;\n"
                                           "        percent = clamp(percent, 0.0, 1.0);\n"
                                           "        return center + delta * percent;\n"
                                           "    }\n"
                                           "    return uv;\n"
                                           "}\n"
                                           "void main() {\n"
                                           "    vec2 newUV = warp(TexCoord, center, radius, strength * 0.5);\n"
                                           "    FragColor = texture(inputTexture, newUV);\n"
                                           "}");
        return m_program->link();
    }

    void onSetUniforms() override {
        m_program->setUniformValue("strength", m_params["strength"].toFloat());
        m_program->setUniformValue("center", QVector2D(m_params["centerX"].toFloat(), m_params["centerY"].toFloat()));
        m_program->setUniformValue("radius", m_params["radius"].toFloat());
    }
};

// ================== 4. 锐化 (Sharpen) ==================
class SharpenFilter : public AbstractFilter {
public:
    SharpenFilter() { m_params["amount"] = 0.0f; }

protected:
    bool initShaders() override {
        m_program = new QOpenGLShaderProgram();
        m_program->addShaderFromSourceCode(QOpenGLShader::Vertex,
                                           "#version 330 core\n"
                                           "layout(location = 0) in vec2 aPos;\n"
                                           "layout(location = 1) in vec2 aTexCoord;\n"
                                           "out vec2 TexCoord;\n"
                                           "void main() { gl_Position = vec4(aPos, 0.0, 1.0); TexCoord = aTexCoord; }");

        m_program->addShaderFromSourceCode(QOpenGLShader::Fragment,
                                           "#version 330 core\n"
                                           "in vec2 TexCoord;\n"
                                           "uniform sampler2D inputTexture;\n"
                                           "uniform vec2 resolution;\n"
                                           "uniform float amount;\n"
                                           "out vec4 FragColor;\n"
                                           "void main() {\n"
                                           "    vec2 step = 1.0 / resolution;\n"
                                           "    vec4 center = texture(inputTexture, TexCoord);\n"
                                           "    vec4 top = texture(inputTexture, TexCoord + vec2(0, -step.y));\n"
                                           "    vec4 bottom = texture(inputTexture, TexCoord + vec2(0, step.y));\n"
                                           "    vec4 left = texture(inputTexture, TexCoord + vec2(-step.x, 0));\n"
                                           "    vec4 right = texture(inputTexture, TexCoord + vec2(step.x, 0));\n"
                                           "    vec4 result = center * 5.0 - (top + bottom + left + right);\n"
                                           "    FragColor = mix(center, result, amount);\n"
                                           "}");
        return m_program->link();
    }
    void onSetUniforms() override {
        m_program->setUniformValue("amount", m_params["amount"].toFloat());
    }
};

// ================== 5. 绿幕抠图 (Chroma Key) ==================
class GreenScreenFilter : public AbstractFilter {
public:
    GreenScreenFilter() { m_params["threshold"] = 0.4f; }

protected:
    bool initShaders() override {
        m_program = new QOpenGLShaderProgram();
        m_program->addShaderFromSourceCode(QOpenGLShader::Vertex,
                                           "#version 330 core\n"
                                           "layout(location = 0) in vec2 aPos;\n"
                                           "layout(location = 1) in vec2 aTexCoord;\n"
                                           "out vec2 TexCoord;\n"
                                           "void main() { gl_Position = vec4(aPos, 0.0, 1.0); TexCoord = aTexCoord; }");
        m_program->addShaderFromSourceCode(QOpenGLShader::Fragment,
                                           "#version 330 core\n"
                                           "in vec2 TexCoord;\n"
                                           "uniform sampler2D inputTexture;\n"
                                           "uniform float threshold;\n"
                                           "out vec4 FragColor;\n"
                                           "void main() {\n"
                                           "    vec4 color = texture(inputTexture, TexCoord);\n"
                                           "    float g = color.g;\n"
                                           "    float rb = (color.r + color.b) / 2.0;\n"
                                           "    if (g > rb + threshold) {\n"
                                           "        FragColor = vec4(0.0, 0.0, 0.0, 0.0);\n"
                                           "    } else {\n"
                                           "        FragColor = color;\n"
                                           "    }\n"
                                           "}");
        return m_program->link();
    }
    void onSetUniforms() override {
        m_program->setUniformValue("threshold", m_params["threshold"].toFloat());
    }
};

// ================== 6. LUT 滤镜 (多风格支持版) ==================
class LutFilter : public AbstractFilter {
public:
    enum Preset {
        Origin = 0,     // 原图
        Cool,           // 清凉
        Pink,           // 粉嫩
        Vintage,        // 怀旧
        BlackWhite,     // 黑白
        Cyberpunk       // 赛博
    };

    LutFilter() {
        m_params["intensity"] = 1.0f;
        // 默认生成原图 LUT
        switchPreset(Origin);
    }

    ~LutFilter() {
        if (m_lutTexture) delete m_lutTexture;
    }

    // 切换预设
    void switchPreset(int presetIndex) {
        if (m_currentPreset == presetIndex && m_lutTexture) return;
        m_currentPreset = presetIndex;
        generateLut(static_cast<Preset>(presetIndex));
    }

protected:
    // 算法生成 LUT 纹理 (512x512)
    // 这种方式不需要依赖外部 .png 文件，直接由 CPU 计算出色彩映射表
    void generateLut(Preset preset) {
        if (m_lutTexture) {
            delete m_lutTexture;
            m_lutTexture = nullptr;
        }

        QImage img(512, 512, QImage::Format_RGB32);

        for (int y = 0; y < 512; ++y) {
            for (int x = 0; x < 512; ++x) {
                // 1. 计算当前像素对应的 RGB 基准值 (0.0 - 1.0)
                // LUT 映射逻辑：将 512x512 拆分为 64个 64x64 的方块
                int blueBlockX = x / 64;             // 0-7
                int blueBlockY = y / 64;             // 0-7
                int blueIdx = blueBlockY * 8 + blueBlockX; // 0-63 (B通道)

                int inBlockX = x % 64; // 0-63 (R通道)
                int inBlockY = y % 64; // 0-63 (G通道)

                float r = inBlockX / 63.0f;
                float g = inBlockY / 63.0f;
                float b = blueIdx / 63.0f;

                // 2. 根据预设应用色彩算法
                float nr = r, ng = g, nb = b;

                switch (preset) {
                case Origin:
                    // 原图：不做改变
                    break;

                case Cool: // 清凉：提亮，加蓝，减红
                    nr = r * 0.9f;
                    ng = g * 1.05f;
                    nb = b * 1.2f;
                    // 稍微提亮
                    nr = pow(nr, 0.9f);
                    ng = pow(ng, 0.9f);
                    nb = pow(nb, 0.9f);
                    break;

                case Pink: // 粉嫩：加红，加蓝(紫)，减绿
                    nr = r * 1.1f;
                    ng = g * 0.95f;
                    nb = b * 1.05f;
                    // 柔和对比度
                    nr = 0.5f + (nr - 0.5f) * 0.9f;
                    ng = 0.5f + (ng - 0.5f) * 0.9f;
                    nb = 0.5f + (nb - 0.5f) * 0.9f;
                    break;

                case Vintage: // 怀旧：经典的棕褐色变换
                    nr = r * 0.393f + g * 0.769f + b * 0.189f;
                    ng = r * 0.349f + g * 0.686f + b * 0.168f;
                    nb = r * 0.272f + g * 0.534f + b * 0.131f;
                    break;

                case BlackWhite: // 黑白：高对比度
                {
                    float gray = r * 0.299f + g * 0.587f + b * 0.114f;
                    // S型曲线增加对比度
                    gray = (gray > 0.5f) ? (1.0f - 2.0f * (1.0f - gray) * (1.0f - gray))
                                         : (2.0f * gray * gray);
                    nr = ng = nb = gray;
                }
                break;

                case Cyberpunk: // 赛博：暗部偏青，亮部偏洋红
                    nr = r * 1.2f; // 整体偏红
                    ng = g * 0.8f; // 减绿
                    nb = b * 1.3f; // 强蓝
                    // 暗部加蓝
                    if (r+g+b < 1.0f) nb += 0.2f;
                    break;
                }

                // 3. 钳制范围
                nr = qBound(0.0f, nr, 1.0f);
                ng = qBound(0.0f, ng, 1.0f);
                nb = qBound(0.0f, nb, 1.0f);

                img.setPixelColor(x, y, QColor::fromRgbF(nr, ng, nb));
            }
        }

        m_lutTexture = new QOpenGLTexture(img);
        m_lutTexture->setMinMagFilters(QOpenGLTexture::Linear, QOpenGLTexture::Linear);
        m_lutTexture->setWrapMode(QOpenGLTexture::ClampToEdge);
    }

    bool initShaders() override {
        // 如果纹理未生成（比如直接调用的 init），先生成默认
        if (!m_lutTexture) generateLut(Origin);

        m_program = new QOpenGLShaderProgram();
        m_program->addShaderFromSourceCode(QOpenGLShader::Vertex,
                                           "#version 330 core\n"
                                           "layout(location = 0) in vec2 aPos;\n"
                                           "layout(location = 1) in vec2 aTexCoord;\n"
                                           "out vec2 TexCoord;\n"
                                           "void main() { gl_Position = vec4(aPos, 0.0, 1.0); TexCoord = aTexCoord; }");

        m_program->addShaderFromSourceCode(QOpenGLShader::Fragment,
                                           "#version 330 core\n"
                                           "in vec2 TexCoord;\n"
                                           "uniform sampler2D inputTexture;\n"
                                           "uniform sampler2D lutTexture;\n"
                                           "uniform float intensity;\n"
                                           "out vec4 FragColor;\n"
                                           "\n"
                                           "vec4 lookup(vec4 textureColor) {\n"
                                           "    float blueColor = textureColor.b * 63.0;\n"
                                           "    vec2 quad1;\n"
                                           "    quad1.y = floor(floor(blueColor) / 8.0);\n"
                                           "    quad1.x = floor(blueColor) - (quad1.y * 8.0);\n"
                                           "    vec2 quad2;\n"
                                           "    quad2.y = floor(ceil(blueColor) / 8.0);\n"
                                           "    quad2.x = ceil(blueColor) - (quad2.y * 8.0);\n"
                                           "    vec2 texPos1;\n"
                                           "    texPos1.x = (quad1.x * 0.125) + 0.5/512.0 + ((0.125 - 1.0/512.0) * textureColor.r);\n"
                                           "    texPos1.y = (quad1.y * 0.125) + 0.5/512.0 + ((0.125 - 1.0/512.0) * textureColor.g);\n"
                                           "    vec2 texPos2;\n"
                                           "    texPos2.x = (quad2.x * 0.125) + 0.5/512.0 + ((0.125 - 1.0/512.0) * textureColor.r);\n"
                                           "    texPos2.y = (quad2.y * 0.125) + 0.5/512.0 + ((0.125 - 1.0/512.0) * textureColor.g);\n"
                                           "    vec4 newColor1 = texture(lutTexture, texPos1);\n"
                                           "    vec4 newColor2 = texture(lutTexture, texPos2);\n"
                                           "    vec4 newColor = mix(newColor1, newColor2, fract(blueColor));\n"
                                           "    return newColor;\n"
                                           "}\n"
                                           "\n"
                                           "void main() {\n"
                                           "    vec4 px = texture(inputTexture, TexCoord);\n"
                                           "    vec4 lutColor = lookup(px);\n"
                                           "    FragColor = mix(px, vec4(lutColor.rgb, px.a), intensity);\n"
                                           "}");
        return m_program->link();
    }

    void onSetUniforms() override {
        if (m_lutTexture) {
            glActiveTexture(GL_TEXTURE1);
            m_lutTexture->bind();
            m_program->setUniformValue("lutTexture", 1);
        }
        m_program->setUniformValue("intensity", m_params["intensity"].toFloat());
    }

private:
    QOpenGLTexture* m_lutTexture = nullptr;
    int m_currentPreset = -1;
};

// ================== 7. 水印叠加 (Watermark) ==================
class WatermarkFilter : public AbstractFilter {
public:
    WatermarkFilter() {
        m_params["scale"] = 0.3f;
        m_params["x"] = 0.05f;
        m_params["y"] = 0.05f;
        m_params["opacity"] = 0.8f;
    }

    ~WatermarkFilter() { if (m_wmTexture) delete m_wmTexture; }

    void createDefaultWatermark() {
        if (m_wmTexture) return;
        QImage img(300, 128, QImage::Format_ARGB32);
        img.fill(Qt::transparent);
        QPainter p(&img);
        p.setRenderHint(QPainter::Antialiasing);
        p.setFont(QFont("Arial", 40, QFont::Bold));
        p.setPen(Qt::white);
        p.drawText(img.rect(), Qt::AlignCenter, "Rzr牛逼");
        p.setPen(QColor(0,0,0,128));
        p.drawText(img.rect().translated(2,2), Qt::AlignCenter, "Rzr牛逼");

        m_wmTexture = new QOpenGLTexture(img);
        m_wmTexture->setMinMagFilters(QOpenGLTexture::LinearMipMapLinear, QOpenGLTexture::Linear);
    }

protected:
    bool initShaders() override {
        createDefaultWatermark();
        m_program = new QOpenGLShaderProgram();
        m_program->addShaderFromSourceCode(QOpenGLShader::Vertex,
                                           "#version 330 core\n"
                                           "layout(location = 0) in vec2 aPos;\n"
                                           "layout(location = 1) in vec2 aTexCoord;\n"
                                           "out vec2 TexCoord;\n"
                                           "void main() { gl_Position = vec4(aPos, 0.0, 1.0); TexCoord = aTexCoord; }");

        m_program->addShaderFromSourceCode(QOpenGLShader::Fragment,
                                           "#version 330 core\n"
                                           "in vec2 TexCoord;\n"
                                           "uniform sampler2D inputTexture;\n"
                                           "uniform sampler2D wmTexture;\n"
                                           "uniform vec2 position;\n" // (0.05, 0.05)
                                           "uniform float scale;\n"
                                           "uniform float opacity;\n"
                                           "uniform float imgAspect;\n"
                                           "uniform float wmAspect;\n"
                                           "uniform bool isInputFlipped;\n" // [新增]
                                           "out vec4 FragColor;\n"
                                           "void main() {\n"
                                           "    vec4 base = texture(inputTexture, TexCoord);\n"
                                           "    \n"
                                           "    // [关键修复] 坐标归一化：构造一个 screenUV，(0,0) 永远在屏幕视觉左上角\n"
                                           "    vec2 screenUV = TexCoord;\n"
                                           "    if (!isInputFlipped) {\n"
                                           "        // 如果是 FBO 输入(标准GL坐标)，原点在左下，需要翻转 Y 轴才变成左上系\n"
                                           "        screenUV.y = 1.0 - screenUV.y;\n"
                                           "    } else {\n"
                                           "        // 如果是摄像头输入(Flipped Quad)，(0,0) 已经是视觉左上角了，无需变动\n"
                                           "    }\n"
                                           "    \n"
                                           "    // 计算水印自身的 UV\n"
                                           "    float realScaleX = scale;\n"
                                           "    float realScaleY = scale * (imgAspect / wmAspect);\n"
                                           "    \n"
                                           "    // 使用归一化的 screenUV 进行计算，这样 position 永远是相对于左上角\n"
                                           "    vec2 wmUV = (screenUV - position) / vec2(realScaleX, realScaleY);\n"
                                           "    \n"
                                           "    if (wmUV.x >= 0.0 && wmUV.x <= 1.0 && wmUV.y >= 0.0 && wmUV.y <= 1.0) {\n"
                                           "        // 采样水印纹理。通常 QImage 生成的纹理 (0,0) 在左上，但在 GL 中也是 flipped。\n"
                                           "        // 如果水印倒了，把下面这行改为 wmUV.y = 1.0 - wmUV.y;\n"
                                           "        // 通常 QOpenGLTexture 默认是正的，这里用标准采样:\n"
                                           "        vec4 wmColor = texture(wmTexture, wmUV);\n"
                                           "        FragColor = mix(base, vec4(wmColor.rgb, 1.0), wmColor.a * opacity);\n"
                                           "    } else {\n"
                                           "        FragColor = base;\n"
                                           "    }\n"
                                           "}");
        return m_program->link();
    }

    void onSetUniforms() override {
        if (m_wmTexture) {
            glActiveTexture(GL_TEXTURE2);
            m_wmTexture->bind();
            m_program->setUniformValue("wmTexture", 2);
            m_program->setUniformValue("imgAspect", 16.0f/9.0f);
            m_program->setUniformValue("wmAspect", (float)m_wmTexture->width() / m_wmTexture->height());
        }
        m_program->setUniformValue("scale", m_params["scale"].toFloat());
        m_program->setUniformValue("opacity", m_params["opacity"].toFloat());
        m_program->setUniformValue("position", QVector2D(m_params["x"].toFloat(), m_params["y"].toFloat()));
    }
private:
    QOpenGLTexture* m_wmTexture = nullptr;
};

// ================== 8. 滚动字幕 (Marquee) [修复版] ==================
class SubtitleFilter : public AbstractFilter {
public:
    SubtitleFilter() {
        m_params["speed"] = 1.0f;
        setText("Rzr牛逼");
    }

    ~SubtitleFilter() { if (m_textTexture) delete m_textTexture; }

    void setText(const QString& text) {
        QFont font("SimHei", 40);
        QFontMetrics fm(font);
        int textW = fm.horizontalAdvance(text);
        int textH = fm.height();
        int padding = 50;

        QImage img(textW + padding * 2, textH + 20, QImage::Format_ARGB32);
        // [修复 1] 使用完全透明背景，去掉黑色底
        img.fill(Qt::transparent);

        QPainter p(&img);
        p.setRenderHint(QPainter::Antialiasing);
        p.setFont(font);
        p.setPen(Qt::white);
        p.drawText(img.rect(), Qt::AlignCenter, text);

        if (m_textTexture) delete m_textTexture;
        m_textTexture = new QOpenGLTexture(img);
        m_textTexture->setMinMagFilters(QOpenGLTexture::Linear, QOpenGLTexture::Linear);
        m_textTexture->setWrapMode(QOpenGLTexture::Repeat);
    }

protected:
    bool initShaders() override {
        m_program = new QOpenGLShaderProgram();
        m_program->addShaderFromSourceCode(QOpenGLShader::Vertex,
                                           "#version 330 core\n"
                                           "layout(location = 0) in vec2 aPos;\n"
                                           "layout(location = 1) in vec2 aTexCoord;\n"
                                           "out vec2 TexCoord;\n"
                                           "void main() { gl_Position = vec4(aPos, 0.0, 1.0); TexCoord = aTexCoord; }");

        m_program->addShaderFromSourceCode(QOpenGLShader::Fragment,
                                           "#version 330 core\n"
                                           "in vec2 TexCoord;\n"
                                           "uniform sampler2D inputTexture;\n"
                                           "uniform sampler2D textTexture;\n"
                                           "uniform float time;\n"
                                           "uniform float speed;\n"
                                           "uniform bool isInputFlipped;\n" // [新增]
                                           "out vec4 FragColor;\n"
                                           "void main() {\n"
                                           "    vec4 base = texture(inputTexture, TexCoord);\n"
                                           "    \n"
                                           "    // [关键修复] 坐标归一化：获取距离屏幕顶部的 Y 值 (0.0 为顶，1.0 为底)\n"
                                           "    float topDownY = TexCoord.y;\n"
                                           "    if (!isInputFlipped) {\n"
                                           "        topDownY = 1.0 - topDownY;\n"
                                           "    }\n"
                                           "    \n"
                                           "    // 字幕区域：屏幕顶部 15% (0.0 ~ 0.15)\n"
                                           "    if (topDownY < 0.15) {\n"
                                           "        vec2 textUV;\n"
                                           "        textUV.x = TexCoord.x - (time * speed * 0.2);\n"
                                           "        \n"
                                           "        // [修改这里]：去掉 '1.0 - '，直接正向映射\n"
                                           "        // 原代码: textUV.y = 1.0 - (topDownY / 0.15);\n"
                                           "        // 修改后: \n"
                                           "        textUV.y = topDownY / 0.15;\n"
                                           "        \n"
                                           "        vec4 textColor = texture(textTexture, textUV);\n"
                                           "        FragColor = mix(base, vec4(textColor.rgb, 1.0), textColor.a);\n"
                                           "    } else {\n"
                                           "        FragColor = base;\n"
                                           "    }\n"
                                           "}");
        return m_program->link();
    }

    void onSetUniforms() override {
        if (m_textTexture) {
            glActiveTexture(GL_TEXTURE3);
            m_textTexture->bind();
            m_program->setUniformValue("textTexture", 3);
        }
        m_program->setUniformValue("speed", m_params["speed"].toFloat());
    }
private:
    QOpenGLTexture* m_textTexture = nullptr;
};

#pragma once
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLShaderProgram>
#include <QOpenGLFramebufferObject>
#include <vector>
#include <memory>
#include <QMap>
#include <QVariant>

class AbstractFilter : protected QOpenGLFunctions_3_3_Core {
public:
    AbstractFilter() = default;
    virtual ~AbstractFilter() {
        if (m_program) delete m_program;
        if (m_vao) glDeleteVertexArrays(1, &m_vao);
        if (m_vbo) glDeleteBuffers(1, &m_vbo);
        if (m_vaoFlip) glDeleteVertexArrays(1, &m_vaoFlip); // [新增]
        if (m_vboFlip) glDeleteBuffers(1, &m_vboFlip);     // [新增]
    }

    virtual bool init() {
        initializeOpenGLFunctions();
        initQuad(); // 初始化两个 Quad
        return initShaders();
    }

    // [修改] 增加 yFlip 参数
    virtual void process(GLuint inputTexId, int width, int height, float time = 0.0f, bool yFlip = false) {
        if (!m_program) return;
        m_program->bind();
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, inputTexId);
        m_program->setUniformValue("inputTexture", 0);
        m_program->setUniformValue("resolution", QVector2D(width, height));
        m_program->setUniformValue("time", time);

        // [新增] 告诉 Shader 当前输入纹理是否是翻转模式 (摄像头源)
        m_program->setUniformValue("isInputFlipped", yFlip);

        onSetUniforms();

        if (yFlip) renderQuadFlipped();
        else renderQuad();

        m_program->release();
    }

    virtual void setParameter(const QString& key, const QVariant& value) {
        m_params[key] = value;
    }

protected:
    virtual bool initShaders() = 0;
    virtual void onSetUniforms() {}

    void renderQuad() {
        glBindVertexArray(m_vao);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        glBindVertexArray(0);
    }

    // [新增] 绘制翻转的 Quad
    void renderQuadFlipped() {
        glBindVertexArray(m_vaoFlip);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        glBindVertexArray(0);
    }

    void initQuad() {
        // 1. 标准 Quad (UV: 0,0 在左下) -> 用于 FBO 到 FBO/屏幕
        float vertices[] = {
            -1.0f, -1.0f, 0.0f, 0.0f,
            1.0f, -1.0f, 1.0f, 0.0f,
            -1.0f,  1.0f, 0.0f, 1.0f,
            1.0f,  1.0f, 1.0f, 1.0f
        };
        glGenVertexArrays(1, &m_vao);
        glGenBuffers(1, &m_vbo);
        glBindVertexArray(m_vao);
        glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
        glEnableVertexAttribArray(1);

        // 2. [关键修复] 翻转 Quad (UV: 0,1 在左下) -> 专用于摄像头输入
        // 这样可以将 Top-Down 的像素数据映射回正确的视觉方向
        float verticesFlip[] = {
            -1.0f, -1.0f, 0.0f, 1.0f, // 左下 -> UV(0,1) Top
            1.0f, -1.0f, 1.0f, 1.0f, // 右下 -> UV(1,1)
            -1.0f,  1.0f, 0.0f, 0.0f, // 左上 -> UV(0,0) Bottom
            1.0f,  1.0f, 1.0f, 0.0f  // 右上 -> UV(1,0)
        };
        glGenVertexArrays(1, &m_vaoFlip);
        glGenBuffers(1, &m_vboFlip);
        glBindVertexArray(m_vaoFlip);
        glBindBuffer(GL_ARRAY_BUFFER, m_vboFlip);
        glBufferData(GL_ARRAY_BUFFER, sizeof(verticesFlip), verticesFlip, GL_STATIC_DRAW);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
        glEnableVertexAttribArray(1);

        glBindVertexArray(0);
    }

    QOpenGLShaderProgram* m_program = nullptr;
    QMap<QString, QVariant> m_params;
    GLuint m_vao = 0, m_vbo = 0;
    GLuint m_vaoFlip = 0, m_vboFlip = 0; // [新增]
};

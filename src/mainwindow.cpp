#include "mainwindow.h"
#include <QMessageBox>
#include <QGroupBox>
#include <QComboBox>

MainWindow::MainWindow(QWidget *parent)
    : QWidget(parent)
{
    setupUI();

    m_camera = new CameraCapture();
    auto devices = CameraCapture::getCameras();

    if (devices.empty()) {
        QMessageBox::warning(this, "Error", "No camera detected!");
    } else {
        m_camera->configure(devices[0].second, nullptr);
        if (m_camera->start()) {
            m_glWidget->setSource(m_camera);
        } else {
            QMessageBox::critical(this, "Error", "Failed to start camera!");
        }
    }
}

MainWindow::~MainWindow() {
    // 1. 先告诉 GL 窗口相机要没了，停止渲染循环
    if (m_glWidget) {
        m_glWidget->setSource(nullptr);
        // 强制处理完当前的事件队列，确保 pending 的 paintGL 不会执行（可选，但推荐）
        // QCoreApplication::processEvents();
    }

    // 2. 现在可以安全地删除相机对象了
    if (m_camera) {
        m_camera->stop();
        delete m_camera;
        m_camera = nullptr;
    }
}

void MainWindow::setupUI() {
    auto mainLayout = new QHBoxLayout(this);
    this->setWindowTitle("Rzr的小demo");
    this->resize(1280, 800);

    // 左侧：OpenGL 渲染窗口
    m_glWidget = new VideoRenderWidget(this);
    m_glWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    mainLayout->addWidget(m_glWidget, 4);

    // 右侧：控制面板
    auto controlPanel = new QWidget();
    controlPanel->setFixedWidth(300);
    auto ctrlLayout = new QVBoxLayout(controlPanel);

    // === 原有功能 ===
    ctrlLayout->addWidget(createControlGroup("美颜 (磨皮)", "Beauty", "intensity"));
    ctrlLayout->addWidget(createControlGroup("瘦脸 (液化)", "Slim", "strength"));
    ctrlLayout->addWidget(createControlGroup("画质增强 (锐化)", "Sharpen", "amount"));
    ctrlLayout->addWidget(createControlGroup("绿幕抠图", "Green", "threshold"));

    // === 新增功能 ===
    // 1. 滤镜 (LUT) - 调节强度
    QGroupBox* lutGroup = new QGroupBox("多风格滤镜 (LUT)");
    QVBoxLayout* lutLayout = new QVBoxLayout(lutGroup);

    QCheckBox* lutChk = new QCheckBox("启用滤镜");
    QComboBox* lutCombo = new QComboBox();
    lutCombo->addItem("原图 (Origin)", 0);
    lutCombo->addItem("清凉 (Cool)", 1);
    lutCombo->addItem("粉嫩 (Pink)", 2);
    lutCombo->addItem("怀旧 (Vintage)", 3);
    lutCombo->addItem("黑白 (B&W)", 4);
    lutCombo->addItem("赛博朋克 (Cyberpunk)", 5);

    QSlider* lutSlider = new QSlider(Qt::Horizontal);
    lutSlider->setRange(0, 100);
    lutSlider->setValue(100); // 滤镜默认强度 100%

    // 逻辑连接
    connect(lutChk, &QCheckBox::toggled, [=](bool checked){
        m_glWidget->toggleFilter("Lut", checked);
        if(checked) {
            // 重新发送参数
            m_glWidget->setLutPreset(lutCombo->currentData().toInt());
            m_glWidget->updateFilterParam("Lut", "intensity", lutSlider->value() / 100.0f);
        }
    });

    connect(lutCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), [=](int index){
        // 获取 item data 中的 enum 值
        int preset = lutCombo->itemData(index).toInt();
        m_glWidget->setLutPreset(preset);
    });

    connect(lutSlider, &QSlider::valueChanged, [=](int val){
        m_glWidget->updateFilterParam("Lut", "intensity", val / 100.0f);
    });

    lutLayout->addWidget(lutChk);
    lutLayout->addWidget(new QLabel("风格选择:"));
    lutLayout->addWidget(lutCombo);
    lutLayout->addWidget(new QLabel("强度调节:"));
    lutLayout->addWidget(lutSlider);

    ctrlLayout->addWidget(lutGroup);

    // 2. 水印 - 调节不透明度 (也可以扩展 X/Y 调节)
    ctrlLayout->addWidget(createControlGroup("品牌水印", "Watermark", "opacity"));

    // 3. 滚动字幕 - 调节速度
    ctrlLayout->addWidget(createControlGroup("滚动字幕", "Subtitle", "speed"));

    ctrlLayout->addStretch();
    mainLayout->addWidget(controlPanel, 1);
}

QGroupBox* MainWindow::createControlGroup(const QString& title, const QString& filterName, const QString& paramName) {
    auto gb = new QGroupBox(title);
    auto layout = new QVBoxLayout(gb);

    auto chk = new QCheckBox("启用功能");
    auto slider = new QSlider(Qt::Horizontal);
    slider->setRange(0, 100);

    // 不同滤镜默认值不同
    if (filterName == "Watermark") slider->setValue(80);
    else if (filterName == "Subtitle") slider->setValue(50);
    else slider->setValue(0);

    // 启用/禁用 (Hot-plug)
    connect(chk, &QCheckBox::toggled, [=](bool checked){
        m_glWidget->toggleFilter(filterName, checked);
        // 激活时同步参数
        if(checked) {
            float fVal = slider->value() / 100.0f;
            // 速度参数放大一点
            if (filterName == "Subtitle") fVal *= 2.0f;
            m_glWidget->updateFilterParam(filterName, paramName, fVal);
        }
    });

    // 调整参数
    connect(slider, &QSlider::valueChanged, [=](int val){
        float fVal = val / 100.0f;
        if (filterName == "Subtitle") fVal *= 2.0f;
        m_glWidget->updateFilterParam(filterName, paramName, fVal);
    });

    layout->addWidget(chk);
    layout->addWidget(new QLabel(paramName + ":"));
    layout->addWidget(slider);
    return gb;
}

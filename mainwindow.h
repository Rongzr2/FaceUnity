#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QWidget>
#include <QGroupBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QCheckBox>
#include <QSlider>
#include <QLabel>
#include "VideoRenderWidget.h"
#include "CameraCapture.h"

class MainWindow : public QWidget {
    Q_OBJECT
public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private:
    void setupUI();
    QGroupBox* createControlGroup(const QString& title, const QString& filterName, const QString& paramName);

    VideoRenderWidget* m_glWidget;
    CameraCapture* m_camera;
};

#endif // MAINWINDOW_H


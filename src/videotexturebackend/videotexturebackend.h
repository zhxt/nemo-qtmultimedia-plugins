/*
 * Copyright (c) 2014 - 2019 Jolla Ltd.
 * Copyright (c) 2020 Open Mobile Platform LLC.
 *
 * You may use this file under the terms of the BSD license as follows:
 *
 * "Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in
 *     the documentation and/or other materials provided with the
 *     distribution.
 *   * Neither the name of Nemo Mobile nor the names of its contributors
 *     may be used to endorse or promote products derived from this
 *     software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE."
 */
#ifndef VIDEOTEXTUREBACKEND_H
#define VIDEOTEXTUREBACKEND_H

#include <private/qdeclarativevideooutput_backend_p.h>
#include <private/qdeclarativevideooutput_p.h>

#include <QGuiApplication>
#include <QMediaObject>
#include <QMediaService>
#include <QMutex>
#include <QMutexLocker>
#include <QResizeEvent>
#include <QQuickWindow>
#include <QSGGeometry>
#include <QSGGeometryNode>
#include <QSGMaterial>
#include <QSGTexture>
#include <QOpenGLContext>
#include <QThread>
#include <QRunnable>

#include <qpa/qplatformnativeinterface.h>
#include <private/qgstreamerelementcontrol_p.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <gst/interfaces/nemovideotexture.h>
#include <gst/video/gstvideometa.h>

#include "texturevideobuffer.h"

namespace NemoVideoBackend {
struct FilterInfo {
    FilterInfo() { }   // QVector requires default constructor to be present
    FilterInfo(QAbstractVideoFilter *f) : filter(f) { }
    QAbstractVideoFilter *filter = nullptr;
    QVideoFilterRunnable *runnable = nullptr;
};


class GStreamerVideoTexture : public QSGDynamicTexture
{
    Q_OBJECT
public:
    GStreamerVideoTexture(GstElement *sink, EGLDisplay display);
    ~GStreamerVideoTexture();

    bool isActive() const;

    int textureId() const override;
    QSize textureSize() const override;
    void setTextureSize(const QSize &size);
    bool hasAlphaChannel() const override;
    bool hasMipmaps() const override;

    QRectF normalizedTextureSubRect() const override;

    void bind() override;
    bool updateTexture() override;

    void invalidateTexture();
    void invalidated();

    QVector<FilterInfo> swapVideoFilters(QVector<FilterInfo> &filters);

public slots:
    void releaseTexture();

protected:
    void createVideoFilterRunnables();
    void callVideoFilterRunnables();

private:
    GstElement *m_sink;
    EGLDisplay m_display;
    QRectF m_subRect;
    QSize m_textureSize;
    GLuint m_textureId;
    bool m_updated;

    // to get pixels from each video frame
    QPointer<TextureVideoBuffer> m_videoBuffer;
    QVector<FilterInfo> m_filters;
    mutable QMutex m_filtersMutex;
};

class GStreamerVideoMaterial : public QSGMaterial
{
public:
    GStreamerVideoMaterial(GStreamerVideoTexture *texture);

    QSGMaterialShader *createShader() const override;
    QSGMaterialType *type() const override;
    int compare(const QSGMaterial *other) const override;

    void setTexture(GStreamerVideoTexture *texture);

private:
    friend class GStreamerVideoMaterialShader;
    friend class GStreamerVideoNode;

    GStreamerVideoTexture *m_texture;
};

class GStreamerVideoNode : public QSGGeometryNode
{
public:
    GStreamerVideoNode(GStreamerVideoTexture *texture);
    ~GStreamerVideoNode();

    void setBoundingRect(const QRectF &rect, int orientation, bool horizontalMirror, bool verticalMirror);
    void preprocess() override;

private:
    GStreamerVideoMaterial m_material;
    QSGGeometry m_geometry;
};

class ImplicitSizeVideoOutput : public QDeclarativeVideoOutput
{
public:
    using QQuickItem::setImplicitSize;
};

class NemoVideoTextureBackend : public QObject, public QDeclarativeVideoBackend
{
    Q_OBJECT
public:
    explicit NemoVideoTextureBackend(QDeclarativeVideoOutput *parent);
    virtual ~NemoVideoTextureBackend();

    bool init(QMediaService *service) override;
    void releaseSource() override;
    void releaseControl() override;
    void itemChange(QQuickItem::ItemChange change, const QQuickItem::ItemChangeData &changeData) override;
    QSize nativeSize() const override;
    void updateGeometry() override;
    QSGNode *updatePaintNode(QSGNode *oldNode, QQuickItem::UpdatePaintNodeData *data) override;
    QAbstractVideoSurface *videoSurface() const override;

    void appendFilter(QAbstractVideoFilter *filter) override;
    void clearFilters() override;

    // The viewport, adjusted for the pixel aspect ratio
    QRectF adjustedViewport() const override;

    bool event(QEvent *event) override;

signals:
    void nativeSizeChanged();

protected:
    void syncFilters();

private slots:
    void orientationChanged();
    void sourceChanged();
    void cameraStateChanged(QCamera::State newState);

private:
    static void frame_ready(GstElement *sink, int frame, void *data);
    static GstPadProbeReturn probe(GstPad *pad, GstPadProbeInfo *info, void *data);


    QMutex m_mutex;
    QPointer<QGStreamerElementControl> m_control;
    GstElement* m_sink;
    EGLDisplay m_display;
    std::unique_ptr<GStreamerVideoTexture, void(*)(QObject*)> m_texture;
    QCamera *m_camera;
    QSize m_nativeSize;
    QSize m_textureSize;
    QSize m_implicitSize;
    gulong m_signalId;
    gulong m_probeId;
    int m_orientation;
    int m_textureOrientation;
    bool m_mirror;
    bool m_active;
    bool m_geometryChanged;
    bool m_frameChanged;

    // to keep track of added video filters locally, to avoid doing
    //   q->filters() and dealing with QQmlListProperty
    QVector<FilterInfo> m_filters;
};

class NemoVideoTextureBackendPlugin : public QObject, public QDeclarativeVideoBackendFactoryInterface
{
    Q_OBJECT
    Q_INTERFACES(QDeclarativeVideoBackendFactoryInterface)
    Q_PLUGIN_METADATA(IID "org.qt-project.qt.declarativevideobackendfactory/5.2" FILE "videotexturebackend.json")

public:
    NemoVideoTextureBackendPlugin();

    QDeclarativeVideoBackend *create(QDeclarativeVideoOutput *parent) override;
};

} //namespace NemoVideoTextureBackend
#endif // VIDEOTEXTUREBACKEND_H

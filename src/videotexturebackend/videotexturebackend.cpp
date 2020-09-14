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

#include "videotexturebackend.h"
#include <gst/interfaces/nemoeglimagememory.h>

#include <QLoggingCategory>
#include <QElapsedTimer>


namespace NemoVideoBackend {

namespace {
Q_LOGGING_CATEGORY(Timing, "org.sailfishos.multimedia.egltexture.times", QtWarningMsg)
}

GStreamerVideoTexture::GStreamerVideoTexture(EGLDisplay display)
    : m_buffer(nullptr)
    , m_display(display)
    , m_subRect(0, 0, 1, 1)
    , m_textureId(0)
    , m_buffersInvalidated(false)
{
}

GStreamerVideoTexture::~GStreamerVideoTexture()
{
    for (const FilterInfo &info : m_filters) {
        delete info.runnable;
    }

    destroyCachedTextures();

    if (m_buffer) {
        gst_buffer_unref(m_buffer);
    }
}

int GStreamerVideoTexture::textureId() const
{
    return m_textureId;
}

QSize GStreamerVideoTexture::textureSize() const
{
    return m_textureSize;
}

void GStreamerVideoTexture::setTextureSize(const QSize &size)
{
    m_textureSize = size;
}

bool GStreamerVideoTexture::hasAlphaChannel() const
{
    return false;
}

bool GStreamerVideoTexture::hasMipmaps() const
{
    return false;
}

QRectF GStreamerVideoTexture::normalizedTextureSubRect() const
{
    return m_subRect;
}

void GStreamerVideoTexture::bind()
{
    glBindTexture(GL_TEXTURE_EXTERNAL_OES, m_textureId);
}

bool GStreamerVideoTexture::updateTexture()
{
    static const PFNGLEGLIMAGETARGETTEXTURE2DOESPROC glEGLImageTargetTexture2DOES
            = reinterpret_cast<PFNGLEGLIMAGETARGETTEXTURE2DOESPROC>(eglGetProcAddress("glEGLImageTargetTexture2DOES"));

    if (m_buffersInvalidated) {
        m_buffersInvalidated = false;
        destroyCachedTextures();
    } else if (!m_bufferChanged) {
        return false;
    }

    m_bufferChanged = false;

    m_textureId = 0;

    if (!m_buffer || gst_buffer_n_memory(m_buffer) == 0) {
        return true;
    }

    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;
    if (GstMeta *meta = gst_buffer_get_meta (m_buffer, GST_VIDEO_CROP_META_API_TYPE)) {
        GstVideoCropMeta *crop = (GstVideoCropMeta *) meta;
        left = crop->x;
        top = crop->y;
        right = crop->width + left;
        bottom = crop->height + top;
    }

    // All the calculations below are based on ideas from Android GLConsumer.
    // The difference between us and Android is we rely on texture coordinates
    // while Android relies on a matxrix for cropping
    qreal x = 0.0, y = 0.0, width = 1.0, height = 1.0;

    // This value is taken from Android GLConsumer
    qreal shrinkAmount = 1.0;
    qreal croppedWidth = right - left;
    qreal croppedHeight = bottom - top;
    if (croppedWidth > 0 && croppedWidth < m_textureSize.width()) {
        x = (left + shrinkAmount) / m_textureSize.width();
        width = ((croppedWidth) - (2.0f * shrinkAmount)) / m_textureSize.width();
    }

    if (croppedHeight > 0 && croppedHeight < m_textureSize.height()) {
        y = (top + shrinkAmount) / m_textureSize.height();
        height = (croppedHeight - (2.0f * shrinkAmount)) / m_textureSize.height();
    }

    m_subRect = QRectF(x, y, width, height);

    GstMemory *memory = gst_buffer_peek_memory(m_buffer, 0);

    for (CachedTexture &texture : m_textures) {
        if (texture.memory == memory) {
            m_textureId = texture.textureId;
            glBindTexture(GL_TEXTURE_EXTERNAL_OES, m_textureId);
            QElapsedTimer timer;
            timer.start();
            glEGLImageTargetTexture2DOES(GL_TEXTURE_EXTERNAL_OES, texture.image);
            qCDebug(Timing) << m_textureId << "bound in" << timer.elapsed();
            break;
        }
    }

    if (m_textureId == 0) {
        if (EGLImageKHR image = nemo_gst_egl_image_memory_create_image(memory, m_display, nullptr)) {
            glGenTextures(1, &m_textureId);
            glBindTexture(GL_TEXTURE_EXTERNAL_OES, m_textureId);
            glTexParameterf(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameterf(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            QElapsedTimer timer;
            timer.start();
            glEGLImageTargetTexture2DOES(GL_TEXTURE_EXTERNAL_OES, image);
            qCDebug(Timing) << m_textureId << "initial bind in" << timer.elapsed();
            CachedTexture texture = { gst_memory_ref(memory), image, m_textureId};
            m_textures.push_back(texture);
        } else {
            return true;
        }
    }

    // if we have video filters attached to owning VideoOutput,
    // render video frame into a framebuffer to be able to get its pixels;
    // if no filters, don't do this, it affects performance.

    if (!m_filters.isEmpty()) {
        if (!m_videoBuffer) {
            // create only once
            m_videoBuffer.reset(new TextureVideoBuffer());
        }
        // update texture size and ID for every frame
        m_videoBuffer->setTextureSize(m_textureSize);
        m_videoBuffer->setTextureId(m_textureId);
        m_videoBuffer->updateFrame();  // renders frame image to FBO

        callVideoFilterRunnables();
    }

    return true;
}

void GStreamerVideoTexture::destroyCachedTextures()
{
    static const PFNEGLDESTROYIMAGEKHRPROC eglDestroyImageKHR
            = reinterpret_cast<PFNEGLDESTROYIMAGEKHRPROC>(eglGetProcAddress("eglDestroyImageKHR"));

    for (CachedTexture &texture : m_textures) {
        glDeleteTextures(1, &texture.textureId);

        eglDestroyImageKHR(m_display, texture.image);

        gst_memory_unref(texture.memory);
    }
    m_textures.clear();
}


void GStreamerVideoTexture::setBuffer(GstBuffer *buffer)
{
    if (m_buffer != buffer) {
        m_bufferChanged = true;

        if (m_buffer) {
            gst_buffer_unref(m_buffer);
        }
        m_buffer = gst_buffer_ref(buffer);
    }
}

void GStreamerVideoTexture::invalidateBuffers()
{
    m_buffersInvalidated = true;
}

void GStreamerVideoTexture::resetTextures()
{
    m_textureId = 0;
    destroyCachedTextures();

    m_bufferChanged = true;
}

void GStreamerVideoTexture::syncFilters(QVector<FilterInfo> &filters)
{
    QVector<FilterInfo> existingFilters = m_filters;
    m_filters.clear();

    for (auto it = filters.begin(); it != filters.end();) {
        if (it->create) {
            it->create = true;
            it->destroy = false;
            it->created = true;

            m_filters.append(FilterInfo(it->filter, it->filter->createFilterRunnable()));
            ++it;
        } else if (!it->destroy) {
            auto existing = std::find_if(existingFilters.begin(), existingFilters.end(), [it](const FilterInfo &info) {
               return it->filter == info.filter;
            });
            if (existing != existingFilters.end()) {
                m_filters.append(*existing);
                existingFilters.erase(existing);
            }
            ++it;
        } else {
            it = filters.erase(it);
        }
    }

    for (const FilterInfo &info : existingFilters) {
        delete info.runnable;
    }
}

void GStreamerVideoTexture::callVideoFilterRunnables()
{
    if(m_filters.isEmpty())
        return;

    // create video frame and its format descriptor: construct
    //   video frame from video buffer
    QVideoFrame vframe(m_videoBuffer.data(), m_textureSize, QVideoFrame::Format_BGRA32);
    QVideoSurfaceFormat surfaceFormat(m_textureSize, vframe.pixelFormat(),
                                      m_videoBuffer->handleType());

    bool frameWasFiltered = false;
    // pass frame to each filter

    for (int i = 0; i < m_filters.size(); ++i) {
        const FilterInfo &finfo = m_filters.at(i);
        if (Q_UNLIKELY(!finfo.runnable)) {
            continue;
        }

        QVideoFilterRunnable::RunFlags runFlag = 0;
        // the only flag we can set for QVideoFilterRunnable is a marker
        //    that this filter is a last filter in chain
        if (i == m_filters.size() - 1) {
            runFlag |= QVideoFilterRunnable::LastInChain;
        }

        // actually call filter runnable here
        QVideoFrame newFrame = finfo.runnable->run(&vframe, surfaceFormat, runFlag);
        if (newFrame != vframe) {
            frameWasFiltered = true;
            vframe = newFrame;
        }
    }

    // TODO: if frame data has changed, write it back to video buffer.
    if (frameWasFiltered) {
        qWarning() << "call_video_filters(): filters have changed a frame!";
        qWarning() << "  But we don't support changing video frames in filter now.";
    }
}


class GStreamerVideoMaterialShader : public QSGMaterialShader
{
public:
    static QSGMaterialType type;

    void updateState(const RenderState &state, QSGMaterial *newEffect, QSGMaterial *oldEffect);
    char const *const *attributeNames() const;

protected:
    void initialize();

    const char *vertexShader() const;
    const char *fragmentShader() const;

private:
    int id_matrix;
    int id_subrect;
    int id_opacity;
    int id_texture;
};

void GStreamerVideoMaterialShader::updateState(
        const RenderState &state, QSGMaterial *newEffect, QSGMaterial *oldEffect)
{
    GStreamerVideoMaterial *material = static_cast<GStreamerVideoMaterial *>(newEffect);

    if (state.isMatrixDirty()) {
        program()->setUniformValue(id_matrix, state.combinedMatrix());
    }

    if (state.isOpacityDirty()) {
        program()->setUniformValue(id_opacity, state.opacity());
    }

    if (!oldEffect) {
        program()->setUniformValue(id_texture, 0);
    }

    const QRectF subRect = material->m_texture->normalizedTextureSubRect();
    program()->setUniformValue(
                id_subrect, QVector4D(subRect.x(), subRect.y(), subRect.width(), subRect.height()));

    glActiveTexture(GL_TEXTURE0);
    material->m_texture->bind();
}

char const *const *GStreamerVideoMaterialShader::attributeNames() const
{
    static char const *const attr[] = { "position", "texcoord", 0 };
    return attr;
}

void GStreamerVideoMaterialShader::initialize()
{
    id_matrix = program()->uniformLocation("matrix");
    id_subrect = program()->uniformLocation("subrect");
    id_opacity = program()->uniformLocation("opacity");
    id_texture = program()->uniformLocation("texture");
}

QSGMaterialType GStreamerVideoMaterialShader::type;

const char *GStreamerVideoMaterialShader::vertexShader() const
{
    return  "\n uniform highp mat4 matrix;"
            "\n uniform highp vec4 subrect;"
            "\n attribute highp vec4 position;"
            "\n attribute highp vec2 texcoord;"
            "\n varying highp vec2 frag_tx;"
            "\n void main(void)"
            "\n {"
            "\n     gl_Position = matrix * position;"
            "\n     frag_tx = (texcoord * subrect.zw) + subrect.xy;"
            "\n }";
}

const char *GStreamerVideoMaterialShader::fragmentShader() const
{
    return  "\n #extension GL_OES_EGL_image_external : require"
            "\n uniform samplerExternalOES texture;"
            "\n uniform lowp float opacity;"
            "\n varying highp vec2 frag_tx;"
            "\n void main(void)"
            "\n {"
            "\n     gl_FragColor = opacity * texture2D(texture, frag_tx.st);"
            "\n }";
}

GStreamerVideoMaterial::GStreamerVideoMaterial(GStreamerVideoTexture *texture)
    : m_texture(texture)
{
}

QSGMaterialShader *GStreamerVideoMaterial::createShader() const
{
    return new GStreamerVideoMaterialShader;
}

QSGMaterialType *GStreamerVideoMaterial::type() const
{
    return &GStreamerVideoMaterialShader::type;
}

int GStreamerVideoMaterial::compare(const QSGMaterial *other) const
{
    return m_texture - static_cast<const GStreamerVideoMaterial *>(other)->m_texture;
}

GStreamerVideoNode::GStreamerVideoNode(GStreamerVideoTexture *texture)
    : m_material(texture)
    , m_geometry(QSGGeometry::defaultAttributes_TexturedPoint2D(), 4)
{
    setGeometry(&m_geometry);
    setMaterial(&m_material);
    setFlag(UsePreprocess);
}

GStreamerVideoNode::~GStreamerVideoNode()
{
    delete m_material.m_texture;
}

void GStreamerVideoNode::preprocess()
{
    GStreamerVideoTexture *t = m_material.m_texture;
    if (t && t->updateTexture())
        markDirty(QSGNode::DirtyMaterial);
}

void GStreamerVideoNode::setBoundingRect(
        const QRectF &rect, int orientation, bool horizontalMirror, bool verticalMirror)
{
    // Texture vertices clock wise from top left: or tl, tr, br, lf
    // Vertex order is tl, bl, tr, br. So unrotated the texture indexes are [0, 3, 1, 2] and
    // by shifting the array left and wrapping we rotate the image in 90 degree increments.
    const float tx[] = { 0, 1, 1, 0 };
    const float ty[] = { 0, 0, 1, 1 };

    // Texture coordinates are 0, or 1 so flip by subtracting the cooridinate from 1 and
    // taking the absolute value. 1 - 0 = 1, 1 - 1 = 0.  The absolute of 0 take the coordinate
    // gives back the original value. 0 - 0 = 0, 0 - 1 = -1.
    const float hm = horizontalMirror ? 1 : 0;
    const float vm = verticalMirror ? 1 : 0;

    const int offset = orientation / 90;
    QSGGeometry::TexturedPoint2D vertices[] = {
        { rect.left() , rect.top()   , qAbs(hm - tx[(0 + offset) % 4]), qAbs(vm - ty[(0 + offset) % 4]) },
        { rect.left() , rect.bottom(), qAbs(hm - tx[(3 + offset) % 4]), qAbs(vm - ty[(3 + offset) % 4]) },
        { rect.right(), rect.top()   , qAbs(hm - tx[(1 + offset) % 4]), qAbs(vm - ty[(1 + offset) % 4]) },
        { rect.right(), rect.bottom(), qAbs(hm - tx[(2 + offset) % 4]), qAbs(vm - ty[(2 + offset) % 4]) }
    };

    memcpy(m_geometry.vertexDataAsTexturedPoint2D(), vertices, sizeof(vertices));
}

NemoVideoTextureBackend::NemoVideoTextureBackend(QDeclarativeVideoOutput *parent)
    : QDeclarativeVideoBackend(parent)
    , m_sink(nullptr)
    , m_queuedBuffer(nullptr)
    , m_currentBuffer(nullptr)
    , m_display(0)
    , m_camera(nullptr)
    , m_probeId(0)
    , m_showFrameId(0)
    , m_buffersInvalidatedId(0)
    , m_orientation(0)
    , m_textureOrientation(0)
    , m_mirror(false)
    , m_geometryChanged(false)
    , m_filtersChanged(false)
    , m_buffersInvalidated(false)
{
    connect(this, &NemoVideoTextureBackend::requestUpdate, q, &QQuickItem::update, Qt::QueuedConnection);

    if (QPlatformNativeInterface *nativeInterface = QGuiApplication::platformNativeInterface()) {
        m_display = nativeInterface->nativeResourceForIntegration("egldisplay");
    }
    if (!m_display) {
        m_display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    }

    if ((m_sink = gst_element_factory_make("droideglsink", NULL))) {
        // Take ownership of the element or it will be destroyed when any bin it was added to is.
        gst_object_ref_sink(GST_OBJECT(m_sink));

        g_object_set(G_OBJECT(m_sink), "egl-display", m_display, NULL);

        m_showFrameId = g_signal_connect(G_OBJECT(m_sink), "show-frame", G_CALLBACK(show_frame), this);
        m_buffersInvalidatedId = g_signal_connect(
                    G_OBJECT(m_sink), "buffers-invalidated", G_CALLBACK(buffers_invalidated), this);

        m_probeId = gst_pad_add_probe(
                    gst_element_get_static_pad(m_sink, "sink"),
                    GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM,
                    probe,
                    this,
                    NULL);
    }
}

NemoVideoTextureBackend::~NemoVideoTextureBackend()
{
    releaseControl();

    if (m_sink) {
        g_signal_handler_disconnect(G_OBJECT(m_sink), m_showFrameId);
        g_signal_handler_disconnect(G_OBJECT(m_sink), m_buffersInvalidatedId);

        gst_pad_remove_probe(gst_element_get_static_pad(m_sink, "sink"), m_probeId);
        gst_object_unref(GST_OBJECT(m_sink));
        m_sink = 0;
    }

    if (m_queuedBuffer) {
        gst_buffer_unref(m_queuedBuffer);
    }
    if (m_currentBuffer) {
        gst_buffer_unref(m_currentBuffer);
    }
}

void NemoVideoTextureBackend::orientationChanged()
{
    const int orientation = q->orientation();
    if (m_orientation != orientation) {
        {
            QMutexLocker locker(&m_mutex);
            m_orientation = orientation;
            m_geometryChanged = true;
        }
        q->update();
    }
}


void NemoVideoTextureBackend::sourceChanged()
{
    QMutexLocker locker(&m_mutex);
    if (m_camera) {
        disconnect(m_camera, SIGNAL(stateChanged(QCamera::State)), this, SLOT(cameraStateChanged(QCamera::State)));
        m_camera = nullptr;
    }

    QObject *source = q->source();
    if (source) {
        // Check if the mediaObject of the source is a QCamera
        const QMetaObject *metaObject = source->metaObject();
        int mediaObjectPropertyIndex = metaObject->indexOfProperty("mediaObject");
        int deviceIdPropertyIndex = metaObject->indexOfProperty("deviceId");

        if (mediaObjectPropertyIndex != -1 && deviceIdPropertyIndex != -1) { // Camera source
            QCamera *camera = qobject_cast<QCamera*>(source->property("mediaObject").value<QObject*>());
            // Watch its state
            if (camera) {
                m_camera = camera;
                connect(m_camera, SIGNAL(stateChanged(QCamera::State)), this, SLOT(cameraStateChanged(QCamera::State)));
            }
        }
    }
}

void NemoVideoTextureBackend::cameraStateChanged(QCamera::State newState)
{
    // Check the camera position when we get into the Active state
    if (newState != QCamera::ActiveState) {
        return;
    }
    QMutexLocker locker(&m_mutex);
    bool mirror = false;

    if (m_camera) {
        QCameraInfo info(*m_camera);
        mirror = (info.position() == QCamera::FrontFace);
    }

    if (m_mirror != mirror) {
        m_mirror = mirror;
        m_geometryChanged = true;
        locker.unlock();
        q->update();
    }
}

bool NemoVideoTextureBackend::init(QMediaService *service)
{
    if (!m_sink) {
        return false;
    }

    QMediaControl *control = service->requestControl(QGStreamerVideoSinkControl_iid);
    if (control) {
        m_control = qobject_cast<QGStreamerElementControl *>(control);
        if (!m_control) {
            service->releaseControl(control);
            return false;
        }
    } else {
        return false;
    }

    m_service = service;
    m_control->setElement(m_sink);

    m_orientation = q->orientation();

    connect(this, SIGNAL(nativeSizeChanged()), q, SLOT(_q_updateNativeSize()));
    connect(q, SIGNAL(orientationChanged()), this, SLOT(orientationChanged()));
    connect(q, SIGNAL(sourceChanged()), this, SLOT(sourceChanged()));

    return true;
}

void NemoVideoTextureBackend::releaseSource()
{
}

void NemoVideoTextureBackend::releaseControl()
{
    if (m_service && m_control) {
        m_service->releaseControl(m_control);
        m_control.clear();
    }
}

void NemoVideoTextureBackend::itemChange(QQuickItem::ItemChange, const QQuickItem::ItemChangeData &)
{
}

QSize NemoVideoTextureBackend::nativeSize() const
{
    return m_nativeSize;
}

void NemoVideoTextureBackend::updateGeometry()
{
    QMutexLocker locker(&m_mutex);
    m_geometryChanged = true;
}

QSGNode *NemoVideoTextureBackend::updatePaintNode(QSGNode *oldNode, QQuickItem::UpdatePaintNodeData *)
{
    GStreamerVideoNode *node = static_cast<GStreamerVideoNode *>(oldNode);

    QMutexLocker locker(&m_mutex);

    if (!m_queuedBuffer) {
        GstBuffer *currentBuffer = m_currentBuffer;
        m_currentBuffer = nullptr;

        if (m_filtersChanged) {
            m_filtersChanged = false;

            for (auto it = m_filters.begin(); it != m_filters.end();) {
                if (it->destroy) {
                    it = m_filters.erase(it);
                } else {
                    ++it;
                }
            }
        }

        locker.unlock();

        if (currentBuffer) {
            gst_buffer_unref(currentBuffer);
        }

        delete node;
        return nullptr;
    }

    if (!node) {
        node = new GStreamerVideoNode(new GStreamerVideoTexture(m_display));

        m_geometryChanged = true;
        m_filtersChanged = !m_filters.isEmpty();

        static const bool noRetainTextures = qEnvironmentVariableIntValue("QTMULTIMEDIA_VIDEO_TEXTURE_BACKEND_NO_RETAIN_TEXTURES") != 0;

        if (noRetainTextures) {
            QObject::connect(q->window(), &QQuickWindow::afterRendering, node->texture(), &GStreamerVideoTexture::resetTextures, Qt::DirectConnection);
        }
    }

    GStreamerVideoTexture * const texture = node->texture();

    texture->setTextureSize(m_textureSize);
    node->markDirty(QSGNode::DirtyMaterial);

    if (m_buffersInvalidated) {
        m_buffersInvalidated = false;
        texture->invalidateBuffers();
    }

    GstBuffer *bufferToRelease = nullptr;
    if (m_currentBuffer != m_queuedBuffer) {
        bufferToRelease = m_currentBuffer;

        m_currentBuffer = gst_buffer_ref(m_queuedBuffer);
    }

    if (m_filtersChanged) {
        m_filtersChanged = false;
        texture->syncFilters(m_filters);
    }

    if (m_geometryChanged) {
        const QRectF br = q->boundingRect();

        QRectF rect(QPointF(0, 0), QSizeF(m_nativeSize).scaled(br.size(), Qt::KeepAspectRatio));
        rect.moveCenter(br.center());

        int orientation = (m_orientation - m_textureOrientation) % 360;
        if (orientation < 0)
            orientation += 360;

        node->setBoundingRect(
                    rect,
                    orientation,
                    m_mirror && (m_textureOrientation % 180) == 0,
                    m_mirror && (m_textureOrientation % 180) != 0);
        node->markDirty(QSGNode::DirtyGeometry);
        m_geometryChanged = false;
    }

    locker.unlock();

    texture->setBuffer(m_currentBuffer);

    if (bufferToRelease) {
        gst_buffer_unref(bufferToRelease);
    }

    return node;
}

QAbstractVideoSurface *NemoVideoTextureBackend::videoSurface() const
{
    return nullptr;
}

void NemoVideoTextureBackend::appendFilter(QAbstractVideoFilter *filter)
{
    m_filtersChanged = true;
    for (auto it = m_filters.begin(); it != m_filters.end(); ++it) {
        FilterInfo &info = *it;

        if (info.filter == filter) {
            // Pointers make lousy unique ids as they can be recycled after an object is destroyed.
            // So is removed or moved we'll flag it and delay removing until it has been synced.
            info.create = info.destroy;
            std::rotate(it, it + 1, m_filters.end());
            return;
        }
    }
    m_filters.append(FilterInfo(filter));
}

void NemoVideoTextureBackend::clearFilters()
{
    m_filtersChanged = true;
    for (auto it = m_filters.begin(); it != m_filters.end();) {
        if (it->created) {
            it->destroy = true;
            ++it;
        } else {
            it = m_filters.erase(it);
        }
    }
}

// The viewport, adjusted for the pixel aspect ratio
QRectF NemoVideoTextureBackend::adjustedViewport() const
{
    const QRectF br = q->boundingRect();

    QRectF rect(QPointF(0, 0), QSizeF(m_nativeSize).scaled(br.size(), Qt::KeepAspectRatio));
    rect.moveCenter(br.center());

    return rect;
}

bool NemoVideoTextureBackend::event(QEvent *event)
{
    if (event->type() == QEvent::Resize) {
        QSize nativeSize = static_cast<QResizeEvent *>(event)->size();
        if (nativeSize.isValid()) {
            {
                QMutexLocker locker(&m_mutex);
                m_nativeSize = nativeSize;
                if ((m_orientation % 180) != 0) {
                    m_nativeSize.transpose();
                }
            }
            static_cast<ImplicitSizeVideoOutput *>(q)->setImplicitSize(
                        nativeSize.width(), nativeSize.height());
        }
        q->update();
        emit nativeSizeChanged();
        return true;
    } else {
        return QObject::event(event);
    }
}

void NemoVideoTextureBackend::show_frame(GstVideoSink *, GstBuffer *buffer, void *data)
{
    NemoVideoTextureBackend *instance = static_cast<NemoVideoTextureBackend *>(data);

    QMutexLocker locker(&instance->m_mutex);

    GstBuffer * const bufferToRelease = instance->m_queuedBuffer;
    instance->m_queuedBuffer = buffer ? gst_buffer_ref(buffer) : nullptr;

    locker.unlock();

    if (bufferToRelease) {
        gst_buffer_unref(bufferToRelease);
    }

    instance->requestUpdate();
}

void NemoVideoTextureBackend::buffers_invalidated(GstVideoSink *, void *data)
{
    NemoVideoTextureBackend *instance = static_cast<NemoVideoTextureBackend *>(data);

    {
        QMutexLocker locker(&instance->m_mutex);

        instance->m_buffersInvalidated = true;
    }

    instance->requestUpdate();
}


GstPadProbeReturn NemoVideoTextureBackend::probe(GstPad *, GstPadProbeInfo *info, void *data)
{
    NemoVideoTextureBackend * const instance = static_cast<NemoVideoTextureBackend *>(data);
    GstEvent * const event = gst_pad_probe_info_get_event(info);
    if (!event) {
        return GST_PAD_PROBE_OK;
    }

    QMutexLocker locker(&instance->m_mutex);

    QSize implicitSize = instance->m_implicitSize;
    int orientation = instance->m_textureOrientation;
    bool geometryChanged = false;

    if (GST_EVENT_TYPE(event) == GST_EVENT_CAPS) {
        GstCaps *caps;
        gst_event_parse_caps(event, &caps);

        QSize textureSize;

        const GstStructure *structure = gst_caps_get_structure(caps, 0);
        gst_structure_get_int(structure, "width", &textureSize.rwidth());
        gst_structure_get_int(structure, "height", &textureSize.rheight());

        implicitSize = textureSize;
        gint numerator = 0;
        gint denominator = 0;
        if (gst_structure_get_fraction(structure, "pixel-aspect-ratio", &numerator, &denominator)
                && denominator > 0) {
            implicitSize.setWidth(implicitSize.width() * numerator / denominator);
        }

        instance->m_textureSize = textureSize;
        geometryChanged = true;
    } else if (GST_EVENT_TYPE(event) == GST_EVENT_TAG) {
        GstTagList *tags;
        gst_event_parse_tag(event, &tags);

        gchar *orientationTag = 0;
        if (!gst_tag_list_get_string(tags, GST_TAG_IMAGE_ORIENTATION, &orientationTag)) {
            // No orientation in tags, ignore.
        } else if (qstrcmp(orientationTag, "rotate-90") == 0) {
            orientation = 90;
        } else if (qstrcmp(orientationTag, "rotate-180") == 0) {
            orientation = 180;
        } else if (qstrcmp(orientationTag, "rotate-270") == 0) {
            orientation = 270;
        } else {
            orientation = 0;
        }

        g_free(orientationTag);
    } else if (GST_EVENT_TYPE(event) == GST_EVENT_STREAM_START) {
        orientation = 0;
    }

    if (instance->m_textureOrientation != orientation || instance->m_implicitSize != implicitSize) {
        instance->m_implicitSize = implicitSize;
        instance->m_textureOrientation= orientation;
        instance->m_geometryChanged = true;

        if (orientation % 180 != 0) {
            implicitSize.transpose();
        }

        QCoreApplication::postEvent(instance, new QResizeEvent(implicitSize, implicitSize));
    } else if (geometryChanged) {
        instance->m_geometryChanged = true;
        QCoreApplication::postEvent(instance, new QEvent(QEvent::UpdateRequest));
    }

    return GST_PAD_PROBE_OK;
}

NemoVideoTextureBackendPlugin::NemoVideoTextureBackendPlugin()
{
    gst_init(0, 0);
}

QDeclarativeVideoBackend *NemoVideoTextureBackendPlugin::create(QDeclarativeVideoOutput *parent)
{
    return new NemoVideoTextureBackend(parent);
}

} //namespace NemoVideoBackend

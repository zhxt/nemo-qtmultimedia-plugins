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

#ifndef H_TEXTURE_VIDEO_BUFFER
#define H_TEXTURE_VIDEO_BUFFER

#include <QtGlobal>
#include <QVariant>
#include <QImage>
#include <QAbstractVideoBuffer>
#include <QMutex>

#include <memory>

#include <GLES2/gl2.h>    // for GLuint

QT_FORWARD_DECLARE_CLASS(QOpenGLFramebufferObject)
QT_FORWARD_DECLARE_CLASS(QOpenGLShaderProgram)

namespace NemoVideoBackend {

/**
 * @brief The TextureVideoBuffer class
 * Acts much like QMemoryVideoBuffer, storing pixels data in
 * QImage, which is constructed from QOpenGLFrameBufferObject,
 * where texture with textureId is rendered to. It assumes
 * EGLImage is already bound to passed texture.
 */
class TextureVideoBuffer: public QObject, public QAbstractVideoBuffer
{
    Q_OBJECT
public:
    explicit TextureVideoBuffer();
    virtual ~TextureVideoBuffer();

    void release() override;
    MapMode mapMode() const override;
    uchar *map(MapMode mode, int *numBytes, int *bytesPerLine) override;
    void unmap() override;

    QVariant handle() const override;

    void setTextureSize(const QSize &size);
    void setTextureId(GLuint textureId);

    QImage toImage() const;

public Q_SLOTS:
    void updateFrame();

private Q_SLOTS:
    void createGLResources();
    void deleteGLResources();

    void renderFrameToFbo();

protected:
    uchar *realMap(MapMode mode, int *numBytes, int *bytesPerLine);
    void realUnmap();

    void realSetTextureSize(const QSize &size);
    void realSetTextureId(GLuint textureId);

    void realUpdateFrame();
    void realCreateGLResources();
    void realDeleteGLResources();
    void realRenderFrameToFbo();

private:
    bool     m_textureUpdated = false;
    MapMode  m_mapMode = QAbstractVideoBuffer::NotMapped;
    GLuint   m_textureId = 0;

    std::unique_ptr<QOpenGLFramebufferObject> m_fbo;
    std::unique_ptr<QOpenGLShaderProgram> m_program;

    mutable QImage m_image;
    QSize    m_size;
    QMutex   m_mutex;
};

} //namespace
#endif

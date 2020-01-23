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

#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QOpenGLFramebufferObject>
#include <QOpenGLShaderProgram>

#include "texturevideobuffer.h"

namespace NemoVideoBackend {

static const char *c_vertexShaderCode =
        "attribute highp vec4 vertexCoordsArray; \n" \
        "attribute highp vec2 textureCoordArray; \n" \
        "uniform   highp mat4 texMatrix; \n" \
        "varying   highp vec2 textureCoords; \n" \
        "void main(void) \n" \
        "{ \n" \
        "    gl_Position = vertexCoordsArray; \n" \
        "    textureCoords = (texMatrix * vec4(textureCoordArray, 0.0, 1.0)).xy; \n" \
        "}\n";

static const char *c_fragmentShaderCode =
        "#extension GL_OES_EGL_image_external : require \n" \
        "varying highp vec2         textureCoords; \n" \
        "uniform samplerExternalOES frameTexture; \n" \
        "void main() \n" \
        "{ \n" \
        "    gl_FragColor = texture2D(frameTexture, textureCoords); \n" \
        "}\n";

TextureVideoBuffer::TextureVideoBuffer():
    QAbstractVideoBuffer(QAbstractVideoBuffer::GLTextureHandle)
{
}

TextureVideoBuffer::~TextureVideoBuffer()
{
    realDeleteGLResources();
}

void TextureVideoBuffer::release()
{
    // Quote from: [qtmultimedia.git/src/multimedia/video/qabstractvideobuffer.cpp]
    // QVideoFrame calls QAbstractVideoBuffer::release when the buffer is not used
    // any more and can be destroyed or returned to the buffer pool.
    // The default implementation deletes the buffer instance.
    //
    // Qt sources do:
    //     delete this;
    // We do not want to shoot ourselves. Do nothing
}

QAbstractVideoBuffer::MapMode TextureVideoBuffer::mapMode() const
{
    return m_mapMode;
}

uchar *TextureVideoBuffer::map(MapMode mode, int *numBytes, int *bytesPerLine)
{
    QMutexLocker locker(&m_mutex);
    return realMap(mode, numBytes, bytesPerLine);
}

uchar *TextureVideoBuffer::realMap(MapMode mode, int *numBytes, int *bytesPerLine)
{
    if (m_mapMode == NotMapped && mode == ReadOnly) {
        realUpdateFrame();
        m_mapMode = mode;
        // call toImage() only if image was not created yet by call
        //     TextureVideoBuffer::toImage(), for example
        if (m_image.isNull())
            m_image = m_fbo->toImage();

        if (numBytes)
            *numBytes = m_image.byteCount();

        if (bytesPerLine)
            *bytesPerLine = m_image.bytesPerLine();

        return m_image.bits();
    }
    return nullptr;
}

void TextureVideoBuffer::unmap()
{
    QMutexLocker locker(&m_mutex);
    realUnmap();
}

void TextureVideoBuffer::realUnmap()
{
    m_image = QImage();
    m_mapMode = NotMapped;
}

QVariant TextureVideoBuffer::handle() const
{
    return m_textureId;
}

void TextureVideoBuffer::setTextureSize(const QSize &size)
{
    QMutexLocker locker(&m_mutex);
    return realSetTextureSize(size);
}


void TextureVideoBuffer::realSetTextureSize(const QSize &size)
{
    m_size = size;
}

void TextureVideoBuffer::setTextureId(GLuint textureId)
{
    QMutexLocker locker(&m_mutex);
    realSetTextureId(textureId);
}

void TextureVideoBuffer::realSetTextureId(GLuint textureId)
{
    m_textureId = textureId;
    m_textureUpdated = false;
}

/**
 * @brief TextureVideoBuffer::toImage
 * Better to call this after updateFrame() was called
 * @return internal image, if rendered. Null image otherwise
 */
QImage TextureVideoBuffer::toImage() const
{
    if (m_textureUpdated) {
        m_image = m_fbo->toImage();
    }
    return m_image;
}


void TextureVideoBuffer::updateFrame()
{
    QMutexLocker locker(&m_mutex);
    realUpdateFrame();
}

void TextureVideoBuffer::realUpdateFrame()
{
    if (!m_textureUpdated) {
        // update the video texture (called from the render thread)
        realRenderFrameToFbo();
        m_textureUpdated = true;
    }
}

void TextureVideoBuffer::createGLResources()
{
    QMutexLocker locker(&m_mutex);
    realCreateGLResources();
}

void TextureVideoBuffer::realCreateGLResources()
{
    // check opengl context: Returns the last context which called
    //  makeCurrent in the current thread, or 0, if no context is current.
    QOpenGLContext *context = QOpenGLContext::currentContext();
    if (!context) {
        qWarning() << Q_FUNC_INFO << " There is no current OpenGL context!";
        qWarning() << Q_FUNC_INFO << " This should be called from QML render thread!";
        return;
    }

    // Delete FBO if the texture size changed (to recreate it later on)
    if (m_fbo && (m_fbo->size() != m_size)) {
        m_fbo.reset(nullptr);
    }

    // create framebuffer object if not exists
    if (!m_fbo) {
        m_fbo.reset(new QOpenGLFramebufferObject(m_size));
        QObject::connect(context, &QOpenGLContext::aboutToBeDestroyed,
                         this, &TextureVideoBuffer::deleteGLResources,
                         Qt::UniqueConnection);
    }

    // init shader programs only once
    if (m_program) {
        return;
    }
    m_program.reset(new QOpenGLShaderProgram());

    QOpenGLShader *vertexShader = new QOpenGLShader(QOpenGLShader::Vertex, m_program.get());
    vertexShader->compileSourceCode(c_vertexShaderCode);
    m_program->addShader(vertexShader);

    QOpenGLShader *fragmentShader = new QOpenGLShader(QOpenGLShader::Fragment, m_program.get());
    fragmentShader->compileSourceCode(c_fragmentShaderCode);
    m_program->addShader(fragmentShader);

    m_program->bindAttributeLocation("vertexCoordsArray", 0);
    m_program->bindAttributeLocation("textureCoordArray", 1);
    m_program->link();
}

void TextureVideoBuffer::deleteGLResources()
{
    QMutexLocker locker(&m_mutex);
    realDeleteGLResources();
}

void TextureVideoBuffer::realDeleteGLResources()
{
    // This should be called in owning GStreamerVideoTexture::releaseTexture()
    //  which is called in slot conected to QQuickWindow::afterRendering(),
    //  so rendering should be complete and it is safe to delete resources
    if (m_mapMode != NotMapped) {
        realUnmap();
    }
    // delete framefuffer object
    m_fbo.reset(nullptr);
    // delete shader program
    m_program.reset(nullptr);
}

void TextureVideoBuffer::renderFrameToFbo()
{
    QMutexLocker locker(&m_mutex);
    realRenderFrameToFbo();
}

void TextureVideoBuffer::realRenderFrameToFbo()
{
    if (!m_size.isValid()) {
        return;
    }

    realCreateGLResources();

    glBindTexture(GL_TEXTURE_EXTERNAL_OES, m_textureId);

    // save current render states
    GLboolean stencilTestEnabled;
    GLboolean depthTestEnabled;
    GLboolean scissorTestEnabled;
    GLboolean blendEnabled;
    glGetBooleanv(GL_STENCIL_TEST, &stencilTestEnabled);
    glGetBooleanv(GL_DEPTH_TEST, &depthTestEnabled);
    glGetBooleanv(GL_SCISSOR_TEST, &scissorTestEnabled);
    glGetBooleanv(GL_BLEND, &blendEnabled);

    if (stencilTestEnabled) glDisable(GL_STENCIL_TEST);
    if (depthTestEnabled) glDisable(GL_DEPTH_TEST);
    if (scissorTestEnabled) glDisable(GL_SCISSOR_TEST);
    if (blendEnabled) glDisable(GL_BLEND);

    m_fbo->bind();

    glViewport(0, 0, m_size.width(), m_size.height());

    m_program->bind();
    m_program->enableAttributeArray(0);
    m_program->enableAttributeArray(1);
    m_program->setUniformValue("frameTexture", GLuint(0));
    m_program->setUniformValue("texMatrix", QMatrix4x4());

    static const GLfloat g_vertex_data[] = {
        -1.0f, 1.0f,  1.0f, 1.0f,
        1.0f, -1.0f,  -1.0f, -1.0f
    };
    static const GLfloat g_texture_data[] = {
        0.0f, 0.0f,  1.0f, 0.0f,
        1.0f, 1.0f,  0.0f, 1.0f
    };

    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, g_vertex_data);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, g_texture_data);

    // draw primitive
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);

    m_program->disableAttributeArray(0);
    m_program->disableAttributeArray(1);

    glBindTexture(GL_TEXTURE_EXTERNAL_OES, 0);
    // Switch rendering back to the default, windowing
    //   system provided framebuffer.
    m_fbo->release();

    // restore render states
    if (stencilTestEnabled) glEnable(GL_STENCIL_TEST);
    if (depthTestEnabled) glEnable(GL_DEPTH_TEST);
    if (scissorTestEnabled) glEnable(GL_SCISSOR_TEST);
    if (blendEnabled) glEnable(GL_BLEND);
}
} //namespace NemoVideoBackend

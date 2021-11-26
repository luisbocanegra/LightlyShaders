/*
 *   Copyright © 2015 Robert Metsäranta <therealestrob@gmail.com>
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *   General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; see the file COPYING.  if not, write to
 *   the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 *   Boston, MA 02110-1301, USA.
 */

#include "dbus.h"
#include "lightlyshaders.h"
#include <QPainter>
#include <QImage>
#include <QFile>
#include <QTextStream>
#include <QStandardPaths>
#include <kwinglplatform.h>
#include <kwinglutils.h>
#include <kwindowsystem.h>
#include <QMatrix4x4>
#include <KConfigGroup>
#include <QtDBus/QDBusConnection>

KWIN_EFFECT_FACTORY_SUPPORTED_ENABLED(  LightlyShadersFactory,
                                        LightlyShadersEffect,
                                        "lightlyshaders.json",
                                        return LightlyShadersEffect::supported();,
                                        return LightlyShadersEffect::enabledByDefault();)


static void convertFromGLImage(QImage &img, int w, int h)
{
    // from QtOpenGL/qgl.cpp
    // SPDX-FileCopyrightText: 2010 Nokia Corporation and /or its subsidiary(-ies)
    // see https://github.com/qt/qtbase/blob/dev/src/opengl/qgl.cpp
    if (QSysInfo::ByteOrder == QSysInfo::BigEndian) {
        // OpenGL gives RGBA; Qt wants ARGB
        uint *p = reinterpret_cast<uint *>(img.bits());
        uint *end = p + w * h;
        while (p < end) {
            uint a = *p << 24;
            *p = (*p >> 8) | a;
            p++;
        }
    } else {
        // OpenGL gives ABGR (i.e. RGBA backwards); Qt wants ARGB
        for (int y = 0; y < h; y++) {
            uint *q = reinterpret_cast<uint *>(img.scanLine(y));
            for (int x = 0; x < w; ++x) {
                const uint pixel = *q;
                *q = ((pixel << 16) & 0xff0000) | ((pixel >> 16) & 0xff)
                     | (pixel & 0xff00ff00);

                q++;
            }
        }

    }
    img = img.mirrored();
}

LightlyShadersEffect::LightlyShadersEffect() : KWin::Effect(), m_shader(0)
{
    new KWin::EffectAdaptor(this);
    QDBusConnection::sessionBus().registerObject("/LightlyShaders", this);
    for (int i = 0; i < NTex; ++i)
    {
        m_tex[i] = 0;
        m_rect[i] = 0;
        m_dark_rect[i] = 0;
    }
    reconfigure(ReconfigureAll);

    QString shadersDir(QStringLiteral("kwin/shaders/1.10/"));
#ifdef KWIN_HAVE_OPENGLES
    const qint64 coreVersionNumber = kVersionNumber(3, 0);
#else
    const qint64 version = KWin::kVersionNumber(1, 40);
#endif
    if (KWin::GLPlatform::instance()->glslVersion() >= version)
        shadersDir = QStringLiteral("kwin/shaders/1.40/");

    const QString shader = QStandardPaths::locate(QStandardPaths::GenericDataLocation, shadersDir + QStringLiteral("lightlyshaders.frag"));

    QFile file(shader);
    if (file.open(QFile::ReadOnly))
    {
        QByteArray frag = file.readAll();
        m_shader = KWin::ShaderManager::instance()->generateCustomShader(KWin::ShaderTrait::MapTexture, QByteArray(), frag);
        file.close();
        //qDebug() << frag;
        //qDebug() << "shader valid: " << m_shader->isValid();
        if (m_shader->isValid())
        {
            m_applyEffect = NULL;
            const int sampler = m_shader->uniformLocation("sampler");
            const int corner = m_shader->uniformLocation("corner");
            KWin::ShaderManager::instance()->pushShader(m_shader);
            m_shader->setUniform(corner, 1);
            m_shader->setUniform(sampler, 0);
            KWin::ShaderManager::instance()->popShader();

            for (int i = 0; i < KWindowSystem::windows().count(); ++i)
                if (KWin::EffectWindow *win = KWin::effects->findWindow(KWindowSystem::windows().at(i)))
                    windowAdded(win);
            connect(KWin::effects, &KWin::EffectsHandler::windowAdded, this, &LightlyShadersEffect::windowAdded);
            connect(KWin::effects, &KWin::EffectsHandler::windowClosed, this, [this](){m_managed.removeOne(dynamic_cast<KWin::EffectWindow *>(sender()));});
            connect(KWin::effects, &KWin::EffectsHandler::windowMaximizedStateChanged, this, &LightlyShadersEffect::windowMaximizedStateChanged);
        }
        else
            qDebug() << "LightlyShaders: no valid shaders found! LightlyShaders will not work.";
    }
    else
    {
        qDebug() << "LightlyShaders: no shaders found! Exiting...";
        deleteLater();
    }
}

LightlyShadersEffect::~LightlyShadersEffect()
{
    if (m_shader)
        delete m_shader;
    for (int i = 0; i < NTex; ++i)
    {
        if (m_tex[i])
            delete m_tex[i];
        if (m_rect[i])
            delete m_rect[i];
        if (m_dark_rect[i])
            delete m_dark_rect[i];
    }
}

void
LightlyShadersEffect::windowAdded(KWin::EffectWindow *w)
{
    if (m_managed.contains(w)
            || w->windowType() == NET::OnScreenDisplay
            || w->windowType() == NET::Dock
            || w->windowType() == NET::Menu
            || w->windowType() == NET::DropdownMenu)
        return;
//    qDebug() << w->windowRole() << w->windowType() << w->windowClass();
    if (!w->hasDecoration() && (w->windowClass().contains("plasma", Qt::CaseInsensitive)
            || w->windowClass().contains("krunner", Qt::CaseInsensitive)
            || w->windowClass().contains("latte-dock", Qt::CaseInsensitive)
            || w->windowClass().contains("lattedock", Qt::CaseInsensitive)))
        return;

    if (w->windowClass().contains("plasma", Qt::CaseInsensitive) && !w->isNormalWindow() && !w->isDialog() && !w->isModal())
        return;

    if (!w->isPaintingEnabled() || (w->isDesktop()) || w->isPopupMenu())
        return;
    m_managed << w;
}

void 
LightlyShadersEffect::windowMaximizedStateChanged(KWin::EffectWindow *w, bool horizontal, bool vertical) 
{
    if (!m_disabled_for_maximized) return;

    if ((horizontal == true) && (vertical == true))
        m_applyEffect = w;
    else
        m_applyEffect = NULL;
}

void
LightlyShadersEffect::genMasks()
{
    for (int i = 0; i < NTex; ++i)
        if (m_tex[i])
            delete m_tex[i];

    QImage img((m_size+1)*2, (m_size+1)*2, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    QPainter p(&img);
    p.fillRect(img.rect(), Qt::black);
    p.setCompositionMode(QPainter::CompositionMode_DestinationOut);
    p.setPen(Qt::NoPen);
    p.setBrush(Qt::black);
    p.setRenderHint(QPainter::Antialiasing);
    p.drawEllipse(QRect(1,1, m_size*2, m_size*2));
    p.end();

    m_tex[TopLeft] = new KWin::GLTexture(img.copy(0, 0, (m_size+1), (m_size+1)));
    m_tex[TopRight] = new KWin::GLTexture(img.copy((m_size+1), 0, (m_size+1), (m_size+1)));
    m_tex[BottomRight] = new KWin::GLTexture(img.copy((m_size+1), (m_size+1), (m_size+1), (m_size+1)));
    m_tex[BottomLeft] = new KWin::GLTexture(img.copy(0, (m_size+1), (m_size+1), (m_size+1)));
}

void
LightlyShadersEffect::genRect()
{
    for (int i = 0; i < NTex; ++i) {
        if (m_rect[i])
            delete m_rect[i];
        if (m_dark_rect[i])
            delete m_dark_rect[i];
    }

    m_rSize = m_size+1;

    QImage img(m_rSize*2, m_rSize*2, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    QPainter p(&img);
    QRect r(img.rect());
    p.setPen(Qt::NoPen);
    p.setRenderHint(QPainter::Antialiasing);
    r.adjust(1, 1, -1, -1);
    p.setBrush(QColor(255, 255, 255, m_alpha));
    p.drawEllipse(r);
    p.setCompositionMode(QPainter::CompositionMode_DestinationOut);
    p.setBrush(Qt::black);
    r.adjust(1, 1, -1, -1);
    p.drawEllipse(r);
    p.end();

    m_rect[TopLeft] = new KWin::GLTexture(img.copy(0, 0, m_rSize, m_rSize));
    m_rect[TopRight] = new KWin::GLTexture(img.copy(m_rSize, 0, m_rSize, m_rSize));
    m_rect[BottomRight] = new KWin::GLTexture(img.copy(m_rSize, m_rSize, m_rSize, m_rSize));
    m_rect[BottomLeft] = new KWin::GLTexture(img.copy(0, m_rSize, m_rSize, m_rSize));

    m_rSize = m_size+2;

    QImage img2(m_rSize*2, m_rSize*2, QImage::Format_ARGB32_Premultiplied);
    img2.fill(Qt::transparent);
    QPainter p2(&img2);
    QRect r2(img2.rect());
    p2.setPen(Qt::NoPen);
    p2.setRenderHint(QPainter::Antialiasing);
    r2.adjust(1, 1, -1, -1);
    if(m_dark_theme) 
        p2.setBrush(QColor(0, 0, 0, 255));
    else 
        p2.setBrush(QColor(0, 0, 0, m_alpha));
    p2.drawEllipse(r2);
    p2.setCompositionMode(QPainter::CompositionMode_DestinationOut);
    p2.setBrush(Qt::black);
    r2.adjust(1, 1, -1, -1);
    p2.drawEllipse(r2);
    p2.end();

    m_dark_rect[TopLeft] = new KWin::GLTexture(img2.copy(0, 0, m_rSize, m_rSize));
    m_dark_rect[TopRight] = new KWin::GLTexture(img2.copy(m_rSize, 0, m_rSize, m_rSize));
    m_dark_rect[BottomRight] = new KWin::GLTexture(img2.copy(m_rSize, m_rSize, m_rSize, m_rSize));
    m_dark_rect[BottomLeft] = new KWin::GLTexture(img2.copy(0, m_rSize, m_rSize, m_rSize));
}

void
LightlyShadersEffect::setRoundness(const int r)
{
    m_size = r;
    m_corner = QSize(m_size+1, m_size+1);
    genMasks();
    genRect();
}

void
LightlyShadersEffect::reconfigure(ReconfigureFlags flags)
{
    Q_UNUSED(flags)
    KConfigGroup conf = KSharedConfig::openConfig("lightlyshaders.conf")->group("General");
    m_alpha = int(conf.readEntry("alpha", 15)*2.55);
    m_outline = conf.readEntry("outline", false);
    m_dark_theme = conf.readEntry("dark_theme", false);
    m_disabled_for_maximized = conf.readEntry("disabled_for_maximized", false);
    setRoundness(conf.readEntry("roundness", 5));
}

void
LightlyShadersEffect::prePaintWindow(KWin::EffectWindow *w, KWin::WindowPrePaintData &data, std::chrono::milliseconds time)
{
    if (!m_shader->isValid()
            || !m_managed.contains(w)
            || !w->isPaintingEnabled()
            || KWin::effects->hasActiveFullScreenEffect()
            || w->isDesktop()
            || (w == m_applyEffect))
    {
        KWin::effects->prePaintWindow(w, data, time);
        return;
    }
    
    //QRegion outerRect(w->expandedGeometry());

    const QRect geo(w->geometry());
    const QRect rect[NTex] =
    {
        QRect(geo.topLeft(), m_corner),
        QRect(geo.topRight()-QPoint(m_size, 0), m_corner),
        QRect(geo.bottomRight()-QPoint(m_size, m_size), m_corner),
        QRect(geo.bottomLeft()-QPoint(0, m_size), m_corner)
    };
    for (int i = 0; i < NTex; ++i)
    {
        data.paint += rect[i];
        data.clip -= rect[i];
    }
    QRegion outerRect(QRegion(geo.adjusted(-2, -2, 2, 2))-geo.adjusted(2, 2, -2, -2));

    data.paint += outerRect;
    data.clip -=outerRect;
    KWin::effects->prePaintWindow(w, data, time);
}

static bool hasShadow(KWin::EffectWindow *w)
{
    if(w->expandedGeometry().size() != w->frameGeometry().size())
        return true;
    return false;
}

void
LightlyShadersEffect::paintWindow(KWin::EffectWindow *w, int mask, QRegion region, KWin::WindowPaintData &data)
{
    if (!m_shader->isValid()
            || !m_managed.contains(w)
            || !w->isPaintingEnabled()
            || KWin::effects->hasActiveFullScreenEffect()
            || w->isDesktop()
//            || (mask & (PAINT_WINDOW_TRANSFORMED|PAINT_SCREEN_WITH_TRANSFORMED_WINDOWS))
            || !hasShadow(w)
            || (w == m_applyEffect))
    {
        KWin::effects->paintWindow(w, mask, region, data);
        return;
    }

    bool use_outline = m_outline;
    if(mask & PAINT_WINDOW_TRANSFORMED) {
        use_outline = false;
    }
    
    //map the corners
    const QRect geo(w->frameGeometry());
    const QSize size(m_size+2, m_size+2);
    const QRect big_rect[NTex] =
    {
        QRect(geo.topLeft()-QPoint(2,2), size),
        QRect(geo.topRight()-QPoint(m_size-1, 2), size),
        QRect(geo.bottomRight()-QPoint(m_size-1, m_size-1), size),
        QRect(geo.bottomLeft()-QPoint(2, m_size-1), size)
    };

    //copy the empty corner regions
    QList<KWin::GLTexture> empty_corners_tex = getTexRegions(big_rect);
    
    //paint the actual window
    KWin::effects->paintWindow(w, mask, region, data);

    //get samples with shadow
    QList<KWin::GLTexture> shadow_corners_tex = getTexRegions(big_rect);

    //generate shadow texture
    QList<KWin::GLTexture> shadow_tex = createShadowTexture(empty_corners_tex, shadow_corners_tex);

    const QRect rect[NTex] =
    {
        QRect(geo.topLeft()-QPoint(1,1), m_corner),
        QRect(geo.topRight()-QPoint(m_size-1, 1), m_corner),
        QRect(geo.bottomRight()-QPoint(m_size-1, m_size-1), m_corner),
        QRect(geo.bottomLeft()-QPoint(1, m_size-1), m_corner)
    };

    //Draw rounded corners with shadows    
    glEnable(GL_BLEND);
    const int mvpMatrixLocation = m_shader->uniformLocation("modelViewProjectionMatrix");
    KWin::ShaderManager *sm = KWin::ShaderManager::instance();
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    sm->pushShader(m_shader);
    for (int i = 0; i < NTex; ++i)
    {
        QMatrix4x4 mvp = data.screenProjectionMatrix();
        mvp.translate(rect[i].x(), rect[i].y());
        m_shader->setUniform(mvpMatrixLocation, mvp);
        glActiveTexture(GL_TEXTURE1);
        m_tex[i]->bind();
        glActiveTexture(GL_TEXTURE0);
        shadow_tex[i].bind();
        shadow_tex[i].render(region, rect[i]);
        shadow_tex[i].unbind();
        m_tex[i]->unbind();
    }
    sm->popShader();

    // outline
    if (use_outline && data.brightness() == 1.0 && data.crossFadeProgress() == 1.0)
    {
        const float o(data.opacity());

        KWin::GLShader *shader = KWin::ShaderManager::instance()->pushShader(KWin::ShaderTrait::MapTexture|KWin::ShaderTrait::UniformColor|KWin::ShaderTrait::Modulate);
        glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

        //Inner corners
        shader->setUniform(KWin::GLShader::ModulationConstant, QVector4D(o, o, o, o));

        for (int i = 0; i < NTex; ++i)
        {
            QMatrix4x4 modelViewProjection = data.screenProjectionMatrix();
            modelViewProjection.translate(rect[i].x(), rect[i].y());
            shader->setUniform("modelViewProjectionMatrix", modelViewProjection);
            m_rect[i]->bind();
            m_rect[i]->render(region, rect[i]);
            m_rect[i]->unbind();
        }

        KWin::ShaderManager::instance()->popShader();

        //Outer corners
        shader = KWin::ShaderManager::instance()->pushShader(KWin::ShaderTrait::MapTexture|KWin::ShaderTrait::UniformColor|KWin::ShaderTrait::Modulate);
        shader->setUniform(KWin::GLShader::ModulationConstant, QVector4D(o, o, o, o));
        for (int i = 0; i < NTex; ++i)
        {
            QMatrix4x4 modelViewProjection = data.screenProjectionMatrix();
            modelViewProjection.translate(big_rect[i].x(), big_rect[i].y());
            shader->setUniform("modelViewProjectionMatrix", modelViewProjection);
            m_dark_rect[i]->bind();
            m_dark_rect[i]->render(region, big_rect[i]);
            m_dark_rect[i]->unbind();
        
        }
        KWin::ShaderManager::instance()->popShader();
        
        QRegion reg = geo;
        QMatrix4x4 mvp = data.screenProjectionMatrix();

        //Outline
        shader = KWin::ShaderManager::instance()->pushShader(KWin::ShaderTrait::UniformColor);
        shader->setUniform("modelViewProjectionMatrix", mvp);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        reg -= QRegion(geo.adjusted(1, 1, -1, -1));
        for (int i = 0; i < NTex; ++i)
            reg -= rect[i];
        fillRegion(reg, QColor(255, 255, 255, m_alpha*data.opacity()));
        KWin::ShaderManager::instance()->popShader();

        //Borderline
        shader = KWin::ShaderManager::instance()->pushShader(KWin::ShaderTrait::UniformColor);
        shader->setUniform("modelViewProjectionMatrix", mvp);
        glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        reg = QRegion(geo.adjusted(-1, -1, 1, 1));
        reg -= geo;
        for (int i = 0; i < NTex; ++i)
            reg -= rect[i];
        if(m_dark_theme)
            fillRegion(reg, QColor(0, 0, 0, 255*data.opacity()));
        else
            fillRegion(reg, QColor(0, 0, 0, m_alpha*data.opacity()));
        KWin::ShaderManager::instance()->popShader();
    }

    glDisable(GL_BLEND);
}

void
LightlyShadersEffect::fillRegion(const QRegion &reg, const QColor &c)
{
    KWin::GLVertexBuffer *vbo = KWin::GLVertexBuffer::streamingBuffer();
    vbo->reset();
    vbo->setUseColor(true);
    vbo->setColor(c);
    QVector<float> verts;
    foreach (const QRect & r, reg.rects())
    {
        verts << r.x() + r.width() << r.y();
        verts << r.x() << r.y();
        verts << r.x() << r.y() + r.height();
        verts << r.x() << r.y() + r.height();
        verts << r.x() + r.width() << r.y() + r.height();
        verts << r.x() + r.width() << r.y();
    }
    vbo->setData(verts.count() / 2, 2, verts.data(), NULL);
    vbo->render(GL_TRIANGLES);
}

QList<KWin::GLTexture> LightlyShadersEffect::getTexRegions(const QRect* rect)
{
    QList<KWin::GLTexture> sample_tex;
    const QRect s(KWin::effects->virtualScreenGeometry());

    for (int i = 0; i < NTex; ++i)
    {
        KWin::GLTexture t = KWin::GLTexture(GL_RGBA8, rect[i].size());
        t.bind();
        glCopyTexSubImage2D(
            GL_TEXTURE_2D, 
            0, 
            0, 
            0, 
            rect[i].x(), 
            s.height() - rect[i].y() - rect[i].height(), 
            rect[i].width(), 
            rect[i].height()
        );
        t.unbind();

        sample_tex.append(t);
    }

    return sample_tex;
}

QList<KWin::GLTexture> LightlyShadersEffect::createShadowTexture(QList<KWin::GLTexture> orig_tex, QList<KWin::GLTexture> shadow_tex)
{
    QList<KWin::GLTexture> res;

    for (int k = 0; k < NTex; ++k)
    {
        QImage orig_tex_img = toImage(orig_tex[k]);
        QImage shadow_tex_img = toImage(shadow_tex[k]);

        QImage tex_img, orig1_img, orig2_img, shadow1_img, shadow2_img;

        switch(k)
        {
            case TopLeft:
                tex_img = orig_tex_img.copy(1,1,m_size+1,m_size+1);
                orig1_img = orig_tex_img.copy(0,0,m_size+2,1);
                orig2_img = orig_tex_img.copy(0,0,1,m_size+2);
                shadow1_img = shadow_tex_img.copy(0,0,m_size+2,1);
                shadow2_img = shadow_tex_img.copy(0,0,1,m_size+2);
                break;
            case TopRight:
                tex_img = orig_tex_img.copy(0,1,m_size+1,m_size+1);
                orig1_img = orig_tex_img.copy(0,0,m_size+2,1);
                orig2_img = orig_tex_img.copy(m_size+1,0,1,m_size+2);
                shadow1_img = shadow_tex_img.copy(0,0,m_size+2,1);
                shadow2_img = shadow_tex_img.copy(m_size+1,0,1,m_size+2);
                break;
            case BottomRight:
                tex_img = orig_tex_img.copy(0,0,m_size+1,m_size+1);
                orig1_img = orig_tex_img.copy(m_size+1,0,1,m_size+2);
                orig2_img = orig_tex_img.copy(0,m_size+1,m_size+2,1);
                shadow1_img = shadow_tex_img.copy(m_size+1,0,1,m_size+2);
                shadow2_img = shadow_tex_img.copy(0,m_size+1,m_size+2,1);
                break;
            case BottomLeft:
                tex_img = orig_tex_img.copy(1,0,m_size+1,m_size+1);
                orig1_img = orig_tex_img.copy(0,0,1,m_size+2);
                orig2_img = orig_tex_img.copy(0,m_size+1,m_size+2,1);
                shadow1_img = shadow_tex_img.copy(0,0,1,m_size+2);
                shadow2_img = shadow_tex_img.copy(0,m_size+1,m_size+2,1);
                break;
        }
        
        //interpolate the shadows
        //get deltas
        int x, y, d, dR[m_size+1][2], dG[m_size+1][2], dB[m_size+1][2];
        int shadow1_img_width = shadow1_img.width(), shadow2_img_width = shadow2_img.width();
        
        uint *orig1_line = reinterpret_cast<uint*>(orig1_img.bits());
        uint *shadow1_line = reinterpret_cast<uint*>(shadow1_img.bits());
        uint *orig2_line = reinterpret_cast<uint*>(orig2_img.bits());
        uint *shadow2_line = reinterpret_cast<uint*>(shadow2_img.bits());
        
        for (int i = 0; i < m_size+1; ++i)
        {
            if(shadow1_img_width>1) {
                x = i;
                y = 0;
                d = 0; //horizontal
            } else {
                x = 0;
                y = i;
                d = 1; //vertical

                orig1_line = reinterpret_cast<uint*>(orig1_img.scanLine(y));
                shadow1_line = reinterpret_cast<uint*>(shadow1_img.scanLine(y));
            }
 
            QRgb *orig1_pixel = (QRgb*)(orig1_line+x);
            QRgb *shadow1_pixel = (QRgb*)(shadow1_line+x);
            dR[i][d] = qRed(*orig1_pixel) - qRed(*shadow1_pixel);
            dG[i][d] = qGreen(*orig1_pixel) - qGreen(*shadow1_pixel);
            dB[i][d] = qBlue(*orig1_pixel) - qBlue(*shadow1_pixel);

            if(shadow2_img_width>1) {
                x = i;
                y = 0;
                d = 0; //horizontal
            } else {
                x = 0;
                y = i;
                d = 1; //vertical

                orig2_line = reinterpret_cast<uint*>(orig2_img.scanLine(y));
                shadow2_line = reinterpret_cast<uint*>(shadow2_img.scanLine(y));
            }
            
            QRgb* orig2_pixel = (QRgb*)(orig2_line+x);
            QRgb* shadow2_pixel = (QRgb*)(shadow2_line+x);
            dR[i][d] = qRed(*orig2_pixel) - qRed(*shadow2_pixel);
            dG[i][d] = qGreen(*orig2_pixel) - qGreen(*shadow2_pixel);
            dB[i][d] = qBlue(*orig2_pixel) - qBlue(*shadow2_pixel);
        }

        //Generate texture and push it to resulting list
        QImage img(m_size+1, m_size+1, QImage::Format_ARGB32_Premultiplied);
        for (int j = 0; j < m_size+1; ++j)
        {
            uint *img_line = reinterpret_cast<uint*>(img.scanLine(j));
            const uint *tex_img_line = reinterpret_cast<const uint*>(tex_img.constScanLine(j));
            for (int i = 0; i < m_size+1; ++i)
            {
                QRgb *tex_pixel = (QRgb *)(tex_img_line+i);
                
                int diffR, diffG, diffB;

                diffR = std::max(dR[i][0], dR[j][1]);
                diffG = std::max(dG[i][0], dG[j][1]);
                diffB = std::max(dB[i][0], dB[j][1]);
                
                int red = normalize(qRed(*tex_pixel)-diffR);
                int green = normalize(qGreen(*tex_pixel)-diffG);
                int blue = normalize(qBlue(*tex_pixel)-diffB);
                
                *(img_line+i) = qRgb(red, green, blue);
            }
        }
        KWin::GLTexture t(img);        
        res.append(t);
    }

    return res;
}

int
LightlyShadersEffect::normalize(int color_val)
{

    if(color_val<0) color_val = 0;
    else if(color_val>255) color_val = 255;

    return color_val;
}

QImage
LightlyShadersEffect::toImage(KWin::GLTexture texture)
{
    int width = texture.width(), height = texture.height();
    QImage img(width, height, QImage::Format_ARGB32_Premultiplied);
    texture.bind();
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                  static_cast<GLvoid *>(img.bits()));
    texture.unbind();
    convertFromGLImage(img, width, height);
    return img;
}

bool
LightlyShadersEffect::enabledByDefault()
{
    return supported();
}

bool LightlyShadersEffect::supported()
{
    return KWin::effects->isOpenGLCompositing() && KWin::GLRenderTarget::supported();
}

#include "lightlyshaders.moc"

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


LightlyShadersEffect::LightlyShadersEffect() : KWin::DeformEffect(), m_shader(0)
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

    const QString fragmentshader = QStandardPaths::locate(QStandardPaths::GenericDataLocation, shadersDir + QStringLiteral("lightlyshaders.frag"));
//    m_shader = KWin::ShaderManager::instance()->loadFragmentShader(KWin::ShaderManager::GenericShader, fragmentshader);
    QFile file(fragmentshader);
    if (file.open(QFile::ReadOnly))
    {
        QByteArray frag = file.readAll();
        m_shader = KWin::ShaderManager::instance()->generateCustomShader(KWin::ShaderTrait::MapTexture, QByteArray(), frag);
        file.close();
//        qDebug() << frag;
//        qDebug() << "shader valid: " << m_shader->isValid();
        if (m_shader->isValid())
        {
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
            || w->windowType() == NET::WindowType::OnScreenDisplay
            || w->windowType() == NET::WindowType::Dock)
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
LightlyShadersEffect::genMasks()
{
    for (int i = 0; i < NTex; ++i)
        if (m_tex[i])
            delete m_tex[i];

    QImage img(m_size*2, m_size*2, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    QPainter p(&img);
    p.fillRect(img.rect(), Qt::black);
    p.setCompositionMode(QPainter::CompositionMode_DestinationOut);
    p.setPen(Qt::NoPen);
    p.setBrush(Qt::black);
    p.setRenderHint(QPainter::Antialiasing);
    p.drawEllipse(img.rect());
    p.end();

    m_tex[TopLeft] = new KWin::GLTexture(img.copy(0, 0, m_size, m_size));
    m_tex[TopRight] = new KWin::GLTexture(img.copy(m_size, 0, m_size, m_size));
    m_tex[BottomRight] = new KWin::GLTexture(img.copy(m_size, m_size, m_size, m_size));
    m_tex[BottomLeft] = new KWin::GLTexture(img.copy(0, m_size, m_size, m_size));
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
    m_corner = QSize(m_size, m_size);
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
    setRoundness(conf.readEntry("roundness", 5));
}

void
LightlyShadersEffect::prePaintWindow(KWin::EffectWindow *w, KWin::WindowPrePaintData &data, std::chrono::milliseconds time)
{
    if (!m_shader->isValid()
            || !m_managed.contains(w)
            || !w->isPaintingEnabled()
            || KWin::effects->hasActiveFullScreenEffect()
            || w->isDesktop())
    {
        KWin::effects->prePaintWindow(w, data, time);
        unredirect(w);
        return;
    }
    QRegion outerRect(w->expandedGeometry());
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
            || (mask & (PAINT_WINDOW_TRANSFORMED|PAINT_SCREEN_WITH_TRANSFORMED_WINDOWS))
            || !hasShadow(w))
    {
        KWin::effects->paintWindow(w, mask, region, data);
        unredirect(w);
        return;
    }

    //paint the shadow
    redirect(w);

    QRect frame_geo = w->frameGeometry();

    QRegion top_left_reg(frame_geo.x(),frame_geo.y(),m_rSize,m_rSize);
    QRegion top_left_ellipse(frame_geo.x(),frame_geo.y()+m_rSize,m_rSize*2,m_rSize*2,QRegion::Ellipse);
    top_left_reg = top_left_reg.subtracted(top_left_ellipse);

    QRegion top_right_reg(frame_geo.x()+ frame_geo.width()-m_rSize,frame_geo.y(),m_rSize,m_rSize);
    QRegion top_right_ellipse(frame_geo.x()+ frame_geo.width()-2*m_rSize,frame_geo.y()+m_rSize,m_rSize*2,m_rSize*2,QRegion::Ellipse);
    top_right_reg = top_right_reg.subtracted(top_right_ellipse);

    QRegion bottom_right_reg(frame_geo.x()+frame_geo.width()-m_rSize,frame_geo.y()+frame_geo.height()-m_rSize,m_rSize,m_rSize);
    QRegion bottom_right_ellipse(frame_geo.x()+ frame_geo.width()-2*m_rSize,frame_geo.y()+frame_geo.height()-2*m_rSize,m_rSize*2,m_rSize*2,QRegion::Ellipse);
    bottom_right_reg = bottom_right_reg.subtracted(bottom_right_ellipse);

    QRegion bottom_left_reg(frame_geo.x(),frame_geo.y()+frame_geo.height()-m_rSize,m_rSize,m_rSize);
    QRegion bottom_left_ellipse(frame_geo.x(),frame_geo.y()+frame_geo.height()-2*m_rSize,m_rSize*2,m_rSize*2,QRegion::Ellipse);
    bottom_left_reg = bottom_left_reg.subtracted(bottom_left_ellipse);

    QRegion shadow_region(w->expandedGeometry());
    QRegion exclude(w->frameGeometry());
    exclude -= top_left_reg;
    exclude -= top_right_reg;
    exclude -= bottom_right_reg;
    exclude -= bottom_left_reg;
    shadow_region -= exclude;
    m_deform = true;
    KWin::effects->paintWindow(w, PAINT_WINDOW_TRANSFORMED, shadow_region, data);
    m_deform = false;

    //map the corners
    const QRect geo(w->frameGeometry());
    const QRect rect[NTex] =
    {
        QRect(geo.topLeft(), m_corner),
        QRect(geo.topRight()-QPoint(m_size-1, 0), m_corner),
        QRect(geo.bottomRight()-QPoint(m_size-1, m_size-1), m_corner),
        QRect(geo.bottomLeft()-QPoint(0, m_size-1), m_corner)
    };

    //copy the corner regions
    QList<KWin::GLTexture> tex;
    const QRect s(KWin::effects->virtualScreenGeometry());
    for (int i = 0; i < NTex; ++i)
    {
        KWin::GLTexture t = KWin::GLTexture(GL_RGBA8, rect[i].size());
        t.bind();
        glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, rect[i].x(), s.height() - rect[i].y() - rect[i].height(), rect[i].width(), rect[i].height());
        t.unbind();
        tex.append(t);
    }

    //paint the actual window
    KWin::effects->paintWindow(w, mask, w->frameGeometry(), data);

    //'shape' the corners
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    const int mvpMatrixLocation = m_shader->uniformLocation("modelViewProjectionMatrix");
    KWin::ShaderManager *sm = KWin::ShaderManager::instance();
    sm->pushShader(m_shader);
    for (int i = 0; i < NTex; ++i)
    {
        QMatrix4x4 mvp = data.screenProjectionMatrix();
        mvp.translate(rect[i].x(), rect[i].y());
        m_shader->setUniform(mvpMatrixLocation, mvp);
        glActiveTexture(GL_TEXTURE1);
        m_tex[3-i]->bind();
        glActiveTexture(GL_TEXTURE0);
        tex[i].bind();
        tex[i].render(region, rect[i]);
        tex[i].unbind();
        m_tex[3-i]->unbind();
    }
    sm->popShader();

    // outline
    if (m_outline && data.brightness() == 1.0 && data.crossFadeProgress() == 1.0)
    {
        const QRect rrect[NTex] =
        {
            rect[0].adjusted(-1, -1, 0, 0),
            rect[1].adjusted(0, -1, 1, 0),
            rect[2].adjusted(0, 0, 1, 1),
            rect[3].adjusted(-1, 0, 0, 1)
        };
        const float o(data.opacity());

        KWin::GLShader *shader = KWin::ShaderManager::instance()->pushShader(KWin::ShaderTrait::MapTexture|KWin::ShaderTrait::UniformColor|KWin::ShaderTrait::Modulate);
        glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

        //Inner corners
        shader->setUniform(KWin::GLShader::ModulationConstant, QVector4D(o, o, o, o));

        for (int i = 0; i < NTex; ++i)
        {
            QMatrix4x4 modelViewProjection = data.screenProjectionMatrix();
            modelViewProjection.translate(rrect[i].x(), rrect[i].y());
            shader->setUniform("modelViewProjectionMatrix", modelViewProjection);
            m_rect[i]->bind();
            m_rect[i]->render(region, rrect[i]);
            m_rect[i]->unbind();
        }

        KWin::ShaderManager::instance()->popShader();

        //Outer corners
        const QRect nrect[NTex] =
        {
            rect[0].adjusted(-2, -2, 0, 0),
            rect[1].adjusted(0, -2, 2, 0),
            rect[2].adjusted(0, 0, 2, 2),
            rect[3].adjusted(-2, 0, 0, 2)
        };
        shader = KWin::ShaderManager::instance()->pushShader(KWin::ShaderTrait::MapTexture|KWin::ShaderTrait::UniformColor|KWin::ShaderTrait::Modulate);
        shader->setUniform(KWin::GLShader::ModulationConstant, QVector4D(o, o, o, o));
        for (int i = 0; i < NTex; ++i)
        {
            QMatrix4x4 modelViewProjection = data.screenProjectionMatrix();
            modelViewProjection.translate(nrect[i].x(), nrect[i].y());
            shader->setUniform("modelViewProjectionMatrix", modelViewProjection);
            m_dark_rect[i]->bind();
            m_dark_rect[i]->render(region, nrect[i]);
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
        for (int i = 0; i < 4; ++i)
            reg -= rrect[i];
        fillRegion(reg, QColor(255, 255, 255, m_alpha*data.opacity()));
        KWin::ShaderManager::instance()->popShader();

        //Borderline
        shader = KWin::ShaderManager::instance()->pushShader(KWin::ShaderTrait::UniformColor);
        shader->setUniform("modelViewProjectionMatrix", mvp);
        glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        reg = QRegion(geo.adjusted(-1, -1, 1, 1));
        reg -= geo;
        for (int i = 0; i < 4; ++i)
            reg -= rrect[i];
        if(m_dark_theme)
            fillRegion(reg, QColor(0, 0, 0, 255*data.opacity()));
        else
            fillRegion(reg, QColor(0, 0, 0, m_alpha*data.opacity()));
        KWin::ShaderManager::instance()->popShader();
    }

    glDisable(GL_BLEND);
}

void
LightlyShadersEffect::deform(KWin::EffectWindow *w, int mask, KWin::WindowPaintData &data, KWin::WindowQuadList &quads)
{
    if(!m_deform) {
        return;
    }

    QRect expanded_geo = w->expandedGeometry();
    QRect frame_geo = w->frameGeometry();

    quads = quads.splitAtY(frame_geo.height()/2-m_rSize);
    quads = quads.splitAtY(frame_geo.height()/2+m_rSize);
    quads = quads.splitAtX(frame_geo.width()/2-m_rSize);
    quads = quads.splitAtX(frame_geo.width()/2+m_rSize);

    for (int j = 0; j < 4; ++j) {
        quads[0][j].move(quads[0][j].x()+m_rSize, quads[0][j].y()+m_rSize);
        quads[2][j].move(quads[2][j].x()-m_rSize, quads[2][j].y()+m_rSize);
        quads[6][j].move(quads[6][j].x()+m_rSize, quads[6][j].y()-m_rSize);
        quads[8][j].move(quads[8][j].x()-m_rSize, quads[8][j].y()-m_rSize);
    }

    KWin::WindowQuadList new_quads;
    for (int i = 0; i < 9; ++i)
    {
        if (i==1) continue;
        if (i>=3 && i<=5) continue;
        if (i==7) continue;
        new_quads.append(quads[i]);
    }
    quads = new_quads;
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

bool
LightlyShadersEffect::enabledByDefault()
{
    return supported();
}

bool LightlyShadersEffect::supported()
{
    return DeformEffect::supported() && KWin::GLRenderTarget::supported();
}

#include "lightlyshaders.moc"

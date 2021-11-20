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
    //qDebug() << "prePaintWindow called";
    if (!m_shader->isValid()
            || !m_managed.contains(w)
            || !w->isPaintingEnabled()
            || KWin::effects->hasActiveFullScreenEffect()
            || w->isDesktop())
    {
        KWin::effects->prePaintWindow(w, data, time);
        return;
    }
    /*const QRect geo(w->frameGeometry());
    const QRect rect[NTex] =
    {
        QRect(geo.topLeft(), m_corner),
        QRect(geo.topRight()-QPoint(m_size-1, 0), m_corner),
        QRect(geo.bottomRight()-QPoint(m_size-1, m_size-1), m_corner),
        QRect(geo.bottomLeft()-QPoint(0, m_size-1), m_corner)
    };
    for (int i = 0; i < NTex; ++i)
    {
        data.paint += rect[i];
        data.clip -= rect[i];
    }*/
    //QRegion outerRect(QRegion(geo.adjusted(-1, -1, 1, 1))-geo.adjusted(1, 1, -1, -1));
    //outerRect += QRegion(geo.x()+m_size, geo.y(), geo.width()-m_size*2, 1);
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
//            || (mask & (PAINT_WINDOW_TRANSFORMED|PAINT_SCREEN_WITH_TRANSFORMED_WINDOWS))
            || !hasShadow(w))
    {
        KWin::effects->paintWindow(w, mask, region, data);
        return;
    }

    KConfigGroup conf = KSharedConfig::openConfig("lightlyshaders.conf")->group("General");
    m_outline = conf.readEntry("outline", false);
    if(mask & PAINT_WINDOW_TRANSFORMED) {
        m_outline = false;
    }

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
        tex.append(t);
    }

    //map shadow source regions
    int a = 0;
    if(w->windowClass().contains("chrome", Qt::CaseInsensitive))
    {
        a = 1;
    }
    const QRect shadow_rect[8] =
    {
        QRect(geo.topLeft()-QPoint(1+a,1+a), QSize(m_size,1)),
        QRect(geo.topLeft()-QPoint(1+a,1+a), QSize(1,m_size)),
        QRect(geo.topRight()-QPoint(m_size+a,1+a), QSize(m_size,1)),
        QRect(geo.topRight()-QPoint(-1-a, 1+a), QSize(1,m_size)),
        QRect(geo.bottomRight()-QPoint(-1-a,m_size+a), QSize(1,m_size)),
        QRect(geo.bottomRight()-QPoint(m_size+a, -1-a), QSize(m_size,1)),
        QRect(geo.bottomLeft()-QPoint(1+a,m_size+a), QSize(1,m_size)),
        QRect(geo.bottomLeft()-QPoint(1+a, -1-a), QSize(m_size,1))
    };

    //get samples without shadow
    QList<KWin::GLTexture> orig_sample_tex = getSamples(shadow_rect);

    //paint the actual window
    KWin::effects->paintWindow(w, mask, region, data);

    //get samples with shadow
    QList<KWin::GLTexture> shadow_sample_tex = getSamples(shadow_rect);
    
    //'shape' the corners
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
        m_tex[3-i]->bind();
        glActiveTexture(GL_TEXTURE0);
        tex[i].bind();
        tex[i].render(region, rect[i]);
        tex[i].unbind();
        m_tex[3-i]->unbind();
    }
    sm->popShader();

    //get original texture under cut out corners
    QList<KWin::GLTexture> orig_tex;
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
        orig_tex.append(t);
    }

    //generate shadow texture
    QList<KWin::GLTexture> shadow_tex = createShadowTexture(orig_sample_tex, shadow_sample_tex, orig_tex);

    //Draw shadows
    sm->pushShader(m_shader);
    for (int i = 0; i < NTex; ++i)
    {
        QMatrix4x4 mvp = data.screenProjectionMatrix();
        switch(i)
        {
            case 0:
                mvp.translate(rect[i].x()-a, rect[i].y()-a);
                break;
            case 1:
                mvp.translate(rect[i].x()+a, rect[i].y()-a);
                break;
            case 2:
                mvp.translate(rect[i].x()+a, rect[i].y()+a);
                break;
            case 3:
                mvp.translate(rect[i].x()-a, rect[i].y()+a);
                break;
        }
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

QList<KWin::GLTexture> LightlyShadersEffect::getSamples(const QRect* rect)
{
    QList<KWin::GLTexture> sample_tex;
    const QRect s(KWin::effects->virtualScreenGeometry());

    for (int i = 0; i < 8; ++i)
    {
        KWin::GLTexture sample = KWin::GLTexture(GL_RGBA8, rect[i].size());
        sample.bind();
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
        sample.unbind();

        sample_tex.append(sample);
    }

    return sample_tex;
}

QList<KWin::GLTexture> LightlyShadersEffect::createShadowTexture(QList<KWin::GLTexture> orig_sample_tex, QList<KWin::GLTexture> shadow_sample_tex, QList<KWin::GLTexture> orig_tex)
{
    QList<KWin::GLTexture> res;

    for (int k = 0; k < 4; ++k)
    {

        QImage shadow1_img = toImage(shadow_sample_tex[k*2]);
        QImage shadow2_img = toImage(shadow_sample_tex[k*2+1]);

        QImage orig1_img = toImage(orig_sample_tex[k*2]);
        QImage orig2_img = toImage(orig_sample_tex[k*2+1]);

        QImage tex_img = toImage(orig_tex[k]);
        
        //interpolate the shadows
        //get deltas
        int x, y, dR[m_size][2], dG[m_size][2], dB[m_size][2];
        for (int i = 0; i < m_size; ++i)
        {
            int d, n = i;
            if(k*2 == 2) {
                n = m_size-1-i;
            }
            if(shadow1_img.width()>1) {
                x = n;
                y = 0;
                d = 0; //horizontal
            } else {
                x = 0;
                y = n;
                d = 1; //vertical
            }

            QRgb orig1_pixel = orig1_img.pixel(x,y);
            QRgb shadow1_pixel = shadow1_img.pixel(x,y);
            dR[i][d] = qRed(orig1_pixel) - qRed(shadow1_pixel);
            dG[i][d] = qGreen(orig1_pixel) - qGreen(shadow1_pixel);
            dB[i][d] = qBlue(orig1_pixel) - qBlue(shadow1_pixel);

            //qDebug() << "k: " << k << ", dR: " << dR << ", dG: " << dG << ", dB: " << dB;

            n = i;
            if(k*2+1 == 1 || k*2+1 == 5) {
                n = m_size-1-i;
            }
            if(shadow2_img.width()>1) {
                x = n;
                y = 0;
                d = 0; //horizontal
            } else {
                x = 0;
                y = n;
                d = 1; //vertical
            }
            QRgb orig2_pixel = orig2_img.pixel(x,y);
            QRgb shadow2_pixel = shadow2_img.pixel(x,y);
            dR[i][d] = qRed(orig2_pixel) - qRed(shadow2_pixel);
            dG[i][d] = qGreen(orig2_pixel) - qGreen(shadow2_pixel);
            dB[i][d] = qBlue(orig2_pixel) - qBlue(shadow2_pixel);
        }

        //Generate texture and push it to resulting list
        QImage img(m_size, m_size, QImage::Format_ARGB32_Premultiplied);
        for (int i = 0; i < m_size; ++i)
        {
            for (int j = 0; j < m_size; ++j)
            {
                QRgb tex_pixel = tex_img.pixel(i,m_size-1-j);
                //qDebug() << "Pixel: " << qRed(tex_img.pixel(i,m_size-1-j)) << " " << qGreen(tex_img.pixel(i,m_size-1-j)) << " " << qBlue(tex_img.pixel(i,m_size-1-j));
                int diffR, diffG, diffB;

                int m = i, n = j;
                if(k == 1) {
                    m = m_size-1-i;
                    n = m_size-1-j;
                }
                if(k == 2) {
                    m = m_size-1-i;
                    n = m_size-1-j;
                }
                if(k == 3) {
                    n = m_size-1-j;
                }

                diffR = std::max(dR[m][0], dR[n][1]);
                diffG = std::max(dG[m][0], dG[n][1]);
                diffB = std::max(dB[m][0], dB[n][1]);
                
                QColor color(normalise(qRed(tex_pixel)-diffR), normalise(qGreen(tex_pixel)-diffG), normalise(qBlue(tex_pixel)-diffB), 255);

                img.setPixelColor(i, j, color);
            }
        }
        KWin::GLTexture t(img);        
        res.append(t);
    }

    /*for (int i = 1; i <= m_size; ++i)
    {
        for (int j = 1; j <= m_size; ++j)
        {
            QColor color1prev(line1_img.color(i-1));
            QColor color1(line1_img.color(i));
            QColor color2(line2_img.color(j));

            //qDebug() << "Pixel: " << qRed(line1_img.pixel(0,0)) << " " << qGreen(line1_img.pixel(0,0)) << " " << qBlue(line1_img.pixel(0,0)) << " " << qAlpha(line1_img.pixel(0,0));

            int red = color2.red() + (color1prev.red() - color1.red());
            int green = color2.green() + (color1prev.green() - color1.green());
            int blue = color2.blue() + (color1prev.blue() - color1.blue());
            int alpha = color2.alpha() + (color1prev.alpha() - color1.alpha());

            if(red<0) red = 0;
            if(red>255) red = 255;
            if(green<0) green = 0;
            if(green>255) green = 255;
            if(blue<0) blue = 0;
            if(blue>255) blue = 255;
            if(alpha<0) alpha = 0;
            if(alpha>255) alpha = 255;

            res.setPixelColor(QPoint(i-1,j-1), QColor(0,255,0,255));
            //res.setPixelColor(QPoint(i-1,j-1), QColor(red,green,blue,alpha));
            //res.setPixelColor(QPoint(i-1,j-1), color2);
        }
    }*/

    return res;
}

int
LightlyShadersEffect::normalise(int color_val)
{

    if(color_val<0) color_val = 0;
    if(color_val>255) color_val = 255;

    return color_val;
}

QImage
LightlyShadersEffect::toImage(KWin::GLTexture texture)
{
    QImage img(texture.width(), texture.height(), QImage::Format_RGBA8888_Premultiplied);
    texture.bind();
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_INT_8_8_8_8_REV,
                  static_cast<GLvoid *>(img.bits()));
    texture.unbind();

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

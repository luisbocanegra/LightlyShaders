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
#include <QPainterPath>
#include <QImage>
#include <QFile>
#include <QTextStream>
#include <QStandardPaths>
#include <kwinglplatform.h>
#include <kwinglutils.h>
#include <QMatrix4x4>
#include <KConfigGroup>
#include <QtDBus/QDBusConnection>

namespace KWin {

#ifdef KWIN_PLUGIN_FACTORY_NAME
KWIN_EFFECT_FACTORY_SUPPORTED_ENABLED(  LightlyShadersEffect,
                                        "lightlyshaders.json",
                                        return LightlyShadersEffect::supported();,
                                        return LightlyShadersEffect::enabledByDefault();)
#else
KWIN_EFFECT_FACTORY_SUPPORTED_ENABLED(  LightlyShadersFactory,
                                        LightlyShadersEffect,
                                        "lightlyshaders.json",
                                        return LightlyShadersEffect::supported();,
                                        return LightlyShadersEffect::enabledByDefault();)
#endif

LightlyShadersEffect::LightlyShadersEffect() : Effect(), m_shader(0)
{
    new EffectAdaptor(this);
    QDBusConnection::sessionBus().registerObject("/LightlyShaders", this);
    for (int i = 0; i < NTex; ++i)
    {
        m_tex[i] = 0;
        m_rect[i] = 0;
        m_dark_rect[i] = 0;
    }
    reconfigure(ReconfigureAll);

    QString shadersDir(QStringLiteral("kwin/shaders/1.10/"));
    const qint64 version = kVersionNumber(1, 40);
    if (GLPlatform::instance()->glslVersion() >= version)
        shadersDir = QStringLiteral("kwin/shaders/1.40/");

    const QString shader = QStandardPaths::locate(QStandardPaths::GenericDataLocation, shadersDir + QStringLiteral("lightlyshaders.frag"));

    QFile file(shader);
    if (file.open(QFile::ReadOnly))
    {
        QByteArray frag = file.readAll();
        m_shader = ShaderManager::instance()->generateCustomShader(ShaderTrait::MapTexture, QByteArray(), frag);
        file.close();
        //qDebug() << frag;
        //qDebug() << "shader valid: " << m_shader->isValid();
        if (m_shader->isValid())
        {
            m_applyEffect = NULL;
            const int background_sampler = m_shader->uniformLocation("background_sampler");
            const int shadow_sampler = m_shader->uniformLocation("shadow_sampler");
            const int radius_sampler = m_shader->uniformLocation("radius_sampler");
            const int corner_number = m_shader->uniformLocation("corner_number");
            const int sampler_size = m_shader->uniformLocation("sampler_size");
            ShaderManager::instance()->pushShader(m_shader);
            m_shader->setUniform(sampler_size, 4);
            m_shader->setUniform(corner_number, 3);
            m_shader->setUniform(radius_sampler, 2);
            m_shader->setUniform(shadow_sampler, 1);
            m_shader->setUniform(background_sampler, 0);
            ShaderManager::instance()->popShader();

            const auto stackingOrder = effects->stackingOrder();
            for (EffectWindow *window : stackingOrder) {
                windowAdded(window);
            }

            connect(effects, &EffectsHandler::windowAdded, this, &LightlyShadersEffect::windowAdded);
            connect(effects, &EffectsHandler::windowClosed, this, &LightlyShadersEffect::windowClosed);
            connect(effects, &EffectsHandler::windowMaximizedStateChanged, this, &LightlyShadersEffect::windowMaximizedStateChanged);
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
    m_clip.clear();
}

void
LightlyShadersEffect::windowClosed(EffectWindow *w)
{
    m_managed.removeOne(w);
    m_clip.remove(w);
}

void
LightlyShadersEffect::windowAdded(EffectWindow *w)
{
    if (m_managed.contains(w)
            || w->windowType() == NET::OnScreenDisplay
            || w->windowType() == NET::Dock
            || w->windowType() == NET::Menu
            || w->windowType() == NET::DropdownMenu
            || w->windowType() == NET::Tooltip
            || w->windowType() == NET::ComboBox)
        return;
//    qDebug() << w->windowRole() << w->windowType() << w->windowClass();
    if (!w->hasDecoration() && (w->windowClass().contains("plasma", Qt::CaseInsensitive)
            || w->windowClass().contains("krunner", Qt::CaseInsensitive)
            || w->windowClass().contains("latte-dock", Qt::CaseInsensitive)
            || w->windowClass().contains("lattedock", Qt::CaseInsensitive)))
        return;

    if (w->windowClass().contains("plasma", Qt::CaseInsensitive) && !w->isNormalWindow() && !w->isDialog() && !w->isModal())
        return;

    if (w->isDesktop() || w->isPopupMenu() || w->isTooltip() || w->isSpecialWindow())
        return;
    m_managed << w;
}

void 
LightlyShadersEffect::windowMaximizedStateChanged(EffectWindow *w, bool horizontal, bool vertical) 
{
    if (!m_disabled_for_maximized) return;

    if ((horizontal == true) && (vertical == true))
        m_applyEffect = w;
    else
        m_applyEffect = NULL;
}

void
LightlyShadersEffect::drawSquircle(QPainter *p, float size, int translate)
{
    QPainterPath squircle;
    float squircleSize = size * 2 * (float(m_squircle_ratio)/24.0 * 0.25 + 0.8); //0.8 .. 1.05
    float squircleEdge = (size * 2) - squircleSize;

    squircle.moveTo(size, 0);
    squircle.cubicTo(QPointF(squircleSize, 0), QPointF(size * 2, squircleEdge), QPointF(size * 2, size));
    squircle.cubicTo(QPointF(size * 2, squircleSize), QPointF(squircleSize, size * 2), QPointF(size, size * 2));
    squircle.cubicTo(QPointF(squircleEdge, size * 2), QPointF(0, squircleSize), QPointF(0, size));
    squircle.cubicTo(QPointF(0, squircleEdge), QPointF(squircleEdge, 0), QPointF(size, 0));

    squircle.translate(translate,translate);

    p->drawPolygon(squircle.toFillPolygon());
}

QImage
LightlyShadersEffect::genMaskImg(int size, bool mask, bool outer_rect)
{
    QImage img(size*2, size*2, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    QPainter p(&img);
    QRect r(img.rect());

    if(mask) {
        p.fillRect(img.rect(), Qt::black);
        p.setCompositionMode(QPainter::CompositionMode_DestinationOut);
        p.setPen(Qt::NoPen);
        p.setBrush(Qt::black);
        p.setRenderHint(QPainter::Antialiasing);
        if (m_corners_type == SquircledCorners) {
            drawSquircle(&p, (size-1.5), 2);
        } else {
            p.drawEllipse(r.adjusted(2,2,-1,-1));
        }
    } else {
        p.setPen(Qt::NoPen);
        p.setRenderHint(QPainter::Antialiasing);
        r.adjust(1, 1, -1, -1);
        if(outer_rect) {
            if(m_dark_theme) 
                p.setBrush(QColor(0, 0, 0, 240));
            else 
                p.setBrush(QColor(0, 0, 0, m_alpha));
        } else {
            p.setBrush(QColor(255, 255, 255, m_alpha));
        }
        if (m_corners_type == SquircledCorners) {
            drawSquircle(&p, (size-1), 1);
        } else {
            p.drawEllipse(r);
        }
        p.setCompositionMode(QPainter::CompositionMode_DestinationOut);
        p.setBrush(Qt::black);
        r.adjust(1, 1, -1, -1);
        if (m_corners_type == SquircledCorners) {
            drawSquircle(&p, (size-2), 2);
        } else {
            p.drawEllipse(r);
        }
    }
    p.end();

    return img;
}

void
LightlyShadersEffect::genMasks()
{
    for (int i = 0; i < NTex; ++i)
        if (m_tex[i])
            delete m_tex[i];

    int size = m_size + 1;

    QImage img = genMaskImg(size, true, false);
    
    m_tex[TopLeft] = new GLTexture(img.copy(0, 0, size, size), GL_TEXTURE_RECTANGLE);
    m_tex[TopRight] = new GLTexture(img.copy(size, 0, size, size), GL_TEXTURE_RECTANGLE);
    m_tex[BottomRight] = new GLTexture(img.copy(size, size, size, size), GL_TEXTURE_RECTANGLE);
    m_tex[BottomLeft] = new GLTexture(img.copy(0, size, size, size), GL_TEXTURE_RECTANGLE);
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

    int size = m_size+1;
    QImage img = genMaskImg(size, false, false);

    m_rect[TopLeft] = new GLTexture(img.copy(0, 0, size, size));
    m_rect[TopRight] = new GLTexture(img.copy(size, 0, size, size));
    m_rect[BottomRight] = new GLTexture(img.copy(size, size, size, size));
    m_rect[BottomLeft] = new GLTexture(img.copy(0, size, size, size));

    size += 1;
    QImage img2 = genMaskImg(size, false, true);

    m_dark_rect[TopLeft] = new GLTexture(img2.copy(0, 0, size, size));
    m_dark_rect[TopRight] = new GLTexture(img2.copy(size, 0, size, size));
    m_dark_rect[BottomRight] = new GLTexture(img2.copy(size, size, size, size));
    m_dark_rect[BottomLeft] = new GLTexture(img2.copy(0, size, size, size));
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
    m_corners_type = conf.readEntry("corners_type", int(RoundedCorners));
    m_squircle_ratio = int(conf.readEntry("squircle_ratio", 12));
    setRoundness(conf.readEntry("roundness", 5));
}

void
LightlyShadersEffect::prePaintWindow(EffectWindow *w, WindowPrePaintData &data, std::chrono::milliseconds time)
{
    if (!m_shader->isValid()
            || !w->isOnCurrentDesktop()
            || !m_managed.contains(w)
            || !w->isPaintingEnabled()
            || effects->hasActiveFullScreenEffect()
            || w->isDesktop()
            || w->isSpecialWindow()
            || (w == m_applyEffect))
    {
        effects->prePaintWindow(w, data, time);
        return;
    }

    const QRect geo(w->geometry());
    const QRect rect[NTex] =
    {
        QRect(geo.topLeft()-QPoint(1,1), m_corner),
        QRect(geo.topRight()-QPoint(m_size-1, 1), m_corner),
        QRect(geo.bottomRight()-QPoint(m_size-1, m_size-1), m_corner),
        QRect(geo.bottomLeft()-QPoint(1, m_size-1), m_corner)
    };
    QRegion repaintRegion(QRegion(geo.adjusted(-2, -2, 2, 2))-geo.adjusted(2, 2, -2, -2));

    for (int i = 0; i < NTex; ++i)
    {
        repaintRegion += rect[i];
    }

    m_clip[w] = QRegion();

    const auto stackingOrder = effects->stackingOrder();
    bool bottom_w = true;
    for (EffectWindow *window : stackingOrder) {
        if(!window->isOnCurrentDesktop()
            || window->isMinimized()
            || window->windowClass().contains("latte-dock", Qt::CaseInsensitive)
            || window->windowClass().contains("lattedock", Qt::CaseInsensitive)
            || window->windowClass().contains("peek", Qt::CaseInsensitive)
            || (bottom_w && window != w)
        ) continue;

        bottom_w = false;
        if(!bottom_w && window != w)
            m_clip[w] += window->geometry().adjusted(m_size, m_size, -m_size, -m_size);
    }

    repaintRegion -= m_clip[w];
    data.clip -= repaintRegion;
    data.paint += repaintRegion;

    effects->prePaintWindow(w, data, time);
}

static bool hasShadow(EffectWindow *w)
{
    if(w->expandedGeometry().size() != w->frameGeometry().size())
        return true;
    return false;
}

void
LightlyShadersEffect::paintWindow(EffectWindow *w, int mask, QRegion region, WindowPaintData &data)
{
    if (!m_shader->isValid()
            || !w->isOnCurrentDesktop()
            || !m_managed.contains(w)
            || !w->isPaintingEnabled()
            || effects->hasActiveFullScreenEffect()
            || w->isDesktop()
            || w->isSpecialWindow()
//            || (mask & (PAINT_WINDOW_TRANSFORMED|PAINT_SCREEN_WITH_TRANSFORMED_WINDOWS))
            || !hasShadow(w)
            || (w == m_applyEffect))
    {
        effects->paintWindow(w, mask, region, data);
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
    QList<GLTexture> empty_corners_tex = getTexRegions(w, big_rect);
    
    //paint the actual window
    effects->paintWindow(w, mask, region, data);

    //get samples with shadow
    QList<GLTexture> shadow_corners_tex = getTexRegions(w, big_rect);

    //Draw rounded corners with shadows    
    glEnable(GL_BLEND);
    const int mvpMatrixLocation = m_shader->uniformLocation("modelViewProjectionMatrix");
    const int cornerNumberLocation = m_shader->uniformLocation("corner_number");
    const int samplerSizeLocation = m_shader->uniformLocation("sampler_size");
    ShaderManager *sm = ShaderManager::instance();
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    sm->pushShader(m_shader);
    for (int i = 0; i < NTex; ++i)
    {
        if(m_clip[w].contains(big_rect[i])) {
            continue;
        }

        QMatrix4x4 mvp = data.screenProjectionMatrix();
        QVector2D samplerSize = QVector2D(big_rect[i].width(), big_rect[i].height());
        mvp.translate(big_rect[i].x(), big_rect[i].y());
        m_shader->setUniform(mvpMatrixLocation, mvp);
        m_shader->setUniform(samplerSizeLocation, samplerSize);
        m_shader->setUniform(cornerNumberLocation, i);
        glActiveTexture(GL_TEXTURE2);
        m_tex[i]->bind();
        glActiveTexture(GL_TEXTURE1);
        shadow_corners_tex[i].bind();
        glActiveTexture(GL_TEXTURE0);
        empty_corners_tex[i].bind();
        empty_corners_tex[i].render(region, big_rect[i]);
        empty_corners_tex[i].unbind();
        shadow_corners_tex[i].unbind();
        m_tex[i]->unbind();
    }
    sm->popShader();

    // outline
    if (use_outline && data.brightness() == 1.0 && data.crossFadeProgress() == 1.0)
    {
        const float o(data.opacity());

        glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

        //Outer corners
        GLShader *shader = ShaderManager::instance()->pushShader(ShaderTrait::MapTexture|ShaderTrait::UniformColor|ShaderTrait::Modulate);
        shader->setUniform(GLShader::ModulationConstant, QVector4D(o, o, o, o));
        for (int i = 0; i < NTex; ++i)
        {
            if(m_clip[w].contains(big_rect[i])) {
                continue;
            }

            QMatrix4x4 modelViewProjection = data.screenProjectionMatrix();
            modelViewProjection.translate(big_rect[i].x(), big_rect[i].y());
            shader->setUniform("modelViewProjectionMatrix", modelViewProjection);
            m_dark_rect[i]->bind();
            m_dark_rect[i]->render(region, big_rect[i]);
            m_dark_rect[i]->unbind();
        
        }
        ShaderManager::instance()->popShader();

        //Inner corners
        const QRect rect[NTex] =
        {
            QRect(geo.topLeft()-QPoint(1,1), m_corner),
            QRect(geo.topRight()-QPoint(m_size-1, 1), m_corner),
            QRect(geo.bottomRight()-QPoint(m_size-1, m_size-1), m_corner),
            QRect(geo.bottomLeft()-QPoint(1, m_size-1), m_corner)
        };

        shader = ShaderManager::instance()->pushShader(ShaderTrait::MapTexture|ShaderTrait::UniformColor|ShaderTrait::Modulate);
        shader->setUniform(GLShader::ModulationConstant, QVector4D(o, o, o, o));

        for (int i = 0; i < NTex; ++i)
        {
            if(m_clip[w].contains(rect[i])) {
                continue;
            }

            QMatrix4x4 modelViewProjection = data.screenProjectionMatrix();
            modelViewProjection.translate(rect[i].x(), rect[i].y());
            shader->setUniform("modelViewProjectionMatrix", modelViewProjection);
            m_rect[i]->bind();
            m_rect[i]->render(region, rect[i]);
            m_rect[i]->unbind();
        }
        ShaderManager::instance()->popShader();
        
        QRegion reg = geo;
        QMatrix4x4 mvp = data.screenProjectionMatrix();

        //Outline
        shader = ShaderManager::instance()->pushShader(ShaderTrait::UniformColor);
        shader->setUniform("modelViewProjectionMatrix", mvp);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        reg -= QRegion(geo.adjusted(1, 1, -1, -1));
        reg -= m_clip[w];
        for (int i = 0; i < NTex; ++i)
            reg -= rect[i];
        fillRegion(reg, QColor(255, 255, 255, m_alpha*data.opacity()));
        ShaderManager::instance()->popShader();

        //Borderline
        shader = ShaderManager::instance()->pushShader(ShaderTrait::UniformColor);
        shader->setUniform("modelViewProjectionMatrix", mvp);
        glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        reg = QRegion(geo.adjusted(-1, -1, 1, 1));
        reg -= geo;
        reg -= m_clip[w];
        for (int i = 0; i < NTex; ++i)
            reg -= rect[i];
        if(m_dark_theme)
            fillRegion(reg, QColor(0, 0, 0, 255*data.opacity()));
        else
            fillRegion(reg, QColor(0, 0, 0, m_alpha*data.opacity()));
        ShaderManager::instance()->popShader();
    }

    glDisable(GL_BLEND);
}

void
LightlyShadersEffect::fillRegion(const QRegion &reg, const QColor &c)
{
    GLVertexBuffer *vbo = GLVertexBuffer::streamingBuffer();
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

QList<GLTexture> LightlyShadersEffect::getTexRegions(EffectWindow *w, const QRect* rect)
{
    QList<GLTexture> sample_tex;
    const QRect s(effects->virtualScreenGeometry());

    for (int i = 0; i < NTex; ++i)
    {
        if(m_clip[w].contains(rect[i])) {
            sample_tex.append(GLTexture(GL_TEXTURE_RECTANGLE));
            continue;
        }

        QImage img(rect[i].width(), rect[i].height(), QImage::Format_ARGB32_Premultiplied);
        GLTexture t = GLTexture(img, GL_TEXTURE_RECTANGLE);
        t.bind();
        glCopyTexSubImage2D(
            GL_TEXTURE_RECTANGLE, 
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

bool
LightlyShadersEffect::enabledByDefault()
{
    return supported();
}

bool LightlyShadersEffect::supported()
{
    return effects->isOpenGLCompositing() && GLRenderTarget::supported();
}

#include "lightlyshaders.moc"


} // namespace KWin

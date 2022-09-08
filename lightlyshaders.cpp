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
#include <QRegularExpression>
#include <QBitmap>


namespace KWin {

#ifndef KWIN_PLUGIN_FACTORY_NAME
KWIN_EFFECT_FACTORY_SUPPORTED_ENABLED(LightlyShadersFactory, LightlyShadersEffect, "lightlyshaders.json", return LightlyShadersEffect::supported();, return LightlyShadersEffect::enabledByDefault();)
#else
KWIN_EFFECT_FACTORY_SUPPORTED_ENABLED(LightlyShadersEffect, "lightlyshaders.json", return LightlyShadersEffect::supported();, return LightlyShadersEffect::enabledByDefault();)
#endif


LightlyShadersEffect::LightlyShadersEffect() : Effect(), m_shader(0)
{
    const auto screens = effects->screens();
    for(EffectScreen *s : screens)
    {
        if (effects->waylandDisplay() == nullptr) {
            s = nullptr;
        }
        for (int i = 0; i < NTex; ++i)
        {
            m_screens[s].tex[i] = 0;
            m_screens[s].rect[i] = 0;
            m_screens[s].darkRect[i] = 0;
            m_screens[s].mask[i] = 0;
        }
        if (effects->waylandDisplay() == nullptr) {
            break;
        }
    }
    reconfigure(ReconfigureAll);

    QString shadersDir(QStringLiteral("kwin/shaders/1.40/"));

    const QString shader = QStandardPaths::locate(QStandardPaths::GenericDataLocation, shadersDir + QStringLiteral("lightlyshaders.frag"));

    QFile file_shader(shader);

    if (!file_shader.open(QFile::ReadOnly) )
    {
        qDebug() << "LightlyShaders: no shaders found! Exiting...";
        deleteLater();
        return;
    }

    QByteArray frag = file_shader.readAll();
    m_shader = ShaderManager::instance()->generateCustomShader(ShaderTrait::MapTexture, QByteArray(), frag);
    file_shader.close();

    if (m_shader->isValid())
    {
        const int sampler = m_shader->uniformLocation("sampler");
        const int radius_top_left_sampler = m_shader->uniformLocation("radius_top_left_sampler");
        const int radius_top_right_sampler = m_shader->uniformLocation("radius_top_right_sampler");
        const int radius_bottom_right_sampler = m_shader->uniformLocation("radius_bottom_right_sampler");
        const int radius_bottom_left_sampler = m_shader->uniformLocation("radius_bottom_left_sampler");
        const int expanded_size = m_shader->uniformLocation("expanded_size");
        const int frame_size = m_shader->uniformLocation("frame_size");
        const int csd_shadow_offset = m_shader->uniformLocation("csd_shadow_offset");
        const int radius = m_shader->uniformLocation("radius");
        const int shadow_sample_offset = m_shader->uniformLocation("shadow_sample_offset");
        ShaderManager::instance()->pushShader(m_shader);
        m_shader->setUniform(shadow_sample_offset, 9);
        m_shader->setUniform(radius, 8);
        m_shader->setUniform(csd_shadow_offset, 7);
        m_shader->setUniform(frame_size, 6);
        m_shader->setUniform(expanded_size, 5);
        m_shader->setUniform(radius_bottom_left_sampler, 4);
        m_shader->setUniform(radius_bottom_right_sampler, 3);
        m_shader->setUniform(radius_top_right_sampler, 2);
        m_shader->setUniform(radius_top_left_sampler, 1);
        m_shader->setUniform(sampler, 0);
        ShaderManager::instance()->popShader();

        const auto stackingOrder = effects->stackingOrder();
        for (EffectWindow *window : stackingOrder) {
            windowAdded(window);
        }

        connect(effects, &EffectsHandler::windowAdded, this, &LightlyShadersEffect::windowAdded);
        connect(effects, &EffectsHandler::windowDeleted, this, &LightlyShadersEffect::windowDeleted);
        connect(effects, &EffectsHandler::windowMaximizedStateChanged, this, &LightlyShadersEffect::windowMaximizedStateChanged);
    }
    else
        qDebug() << "LightlyShaders: no valid shaders found! LightlyShaders will not work.";
}

LightlyShadersEffect::~LightlyShadersEffect()
{
    if (m_shader)
        delete m_shader;

    const auto screens = effects->screens();
    for(EffectScreen *s : screens)
    {
        if (effects->waylandDisplay() == nullptr) {
            s = nullptr;
        }
        for (int i = 0; i < NTex; ++i)
        {
            if (m_screens[s].tex[i])
                delete m_screens[s].tex[i];
            if (m_screens[s].rect[i])
                delete m_screens[s].rect[i];
            if (m_screens[s].darkRect[i])
                delete m_screens[s].darkRect[i];
            if (m_screens[s].mask[i])
                delete m_screens[s].mask[i];
        }
        if (effects->waylandDisplay() == nullptr) {
            break;
        }
    }

    m_windows.clear();
}

void
LightlyShadersEffect::windowDeleted(EffectWindow *w)
{
    m_windows.remove(w);
}

static bool hasShadow(EffectWindow *w)
{
    if(w->expandedGeometry().size() != w->frameGeometry().size())
        return true;
    return false;
}

void
LightlyShadersEffect::windowAdded(EffectWindow *w)
{
    m_windows[w].isManaged = false;

    if (w->windowType() == NET::OnScreenDisplay
            || w->windowType() == NET::Dock
            || w->windowType() == NET::Menu
            || w->windowType() == NET::DropdownMenu
            || w->windowType() == NET::Tooltip
            || w->windowType() == NET::ComboBox
            || w->windowType() == NET::Splash)
        return;
//    qDebug() << w->windowRole() << w->windowType() << w->windowClass();
    if (!w->hasDecoration() && (w->windowClass().contains("plasma", Qt::CaseInsensitive)
            || w->windowClass().contains("krunner", Qt::CaseInsensitive)
            || w->windowClass().contains("latte-dock", Qt::CaseInsensitive)
            || w->windowClass().contains("lattedock", Qt::CaseInsensitive)
            || w->windowClass().contains("plank", Qt::CaseInsensitive)
            || w->windowClass().contains("cairo-dock", Qt::CaseInsensitive)
            || w->windowClass().contains("albert", Qt::CaseInsensitive)
            || w->windowClass().contains("ulauncher", Qt::CaseInsensitive)
            || w->windowClass().contains("ksplash", Qt::CaseInsensitive)
            || w->windowClass().contains("ksmserver", Qt::CaseInsensitive)
            || (w->windowClass().contains("reaper", Qt::CaseInsensitive) && !hasShadow(w))))
        return;

    if(w->windowClass().contains("jetbrains", Qt::CaseInsensitive) && w->caption().contains(QRegularExpression ("win[0-9]+")))
        return;

    if (w->windowClass().contains("plasma", Qt::CaseInsensitive) && !w->isNormalWindow() && !w->isDialog() && !w->isModal())
        return;

    if (w->isDesktop()
            || w->isFullScreen()
            || w->isPopupMenu()
            || w->isTooltip() 
            || w->isSpecialWindow()
            || w->isDropdownMenu()
            || w->isPopupWindow()
            || w->isLockScreen()
            || w->isSplash())
        return;

    m_windows[w].isManaged = true;
    m_windows[w].skipEffect = false;

    QRect maximized_area = effects->clientArea(MaximizeArea, w);
    if (maximized_area == w->frameGeometry() && m_disabledForMaximized)
        m_windows[w].skipEffect = true;
}

void 
LightlyShadersEffect::windowMaximizedStateChanged(EffectWindow *w, bool horizontal, bool vertical) 
{
    if (!m_disabledForMaximized) return;

    if ((horizontal == true) && (vertical == true)) {
        m_windows[w].skipEffect = true;
    } else {
        m_windows[w].skipEffect = false;
    }
}

QPainterPath
LightlyShadersEffect::drawSquircle(float size, int translate)
{
    QPainterPath squircle;
    float squircleSize = size * 2 * (float(m_squircleRatio)/24.0 * 0.25 + 0.8); //0.8 .. 1.05
    float squircleEdge = (size * 2) - squircleSize;

    squircle.moveTo(size, 0);
    squircle.cubicTo(QPointF(squircleSize, 0), QPointF(size * 2, squircleEdge), QPointF(size * 2, size));
    squircle.cubicTo(QPointF(size * 2, squircleSize), QPointF(squircleSize, size * 2), QPointF(size, size * 2));
    squircle.cubicTo(QPointF(squircleEdge, size * 2), QPointF(0, squircleSize), QPointF(0, size));
    squircle.cubicTo(QPointF(0, squircleEdge), QPointF(squircleEdge, 0), QPointF(size, 0));

    squircle.translate(translate,translate);

    return squircle;
}

QImage
LightlyShadersEffect::genMaskImg(int size, bool mask, bool outer_rect)
{
    QImage img(size*2, size*2, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    QPainter p(&img);
    QRect r(img.rect());
    int offset_decremented = m_shadowOffset*m_zoom-1;

    if(mask) {
        p.fillRect(img.rect(), Qt::black);
        p.setCompositionMode(QPainter::CompositionMode_DestinationOut);
        p.setPen(Qt::NoPen);
        p.setBrush(Qt::black);
        p.setRenderHint(QPainter::Antialiasing);
        if (m_cornersType == SquircledCorners) {
            const QPainterPath squircle1 = drawSquircle((size-m_shadowOffset*m_zoom), m_shadowOffset*m_zoom);
            p.drawPolygon(squircle1.toFillPolygon());
        } else {
            p.drawEllipse(r.adjusted(m_shadowOffset*m_zoom,m_shadowOffset*m_zoom,-m_shadowOffset*m_zoom,-m_shadowOffset*m_zoom));
        }
    } else {
        p.setPen(Qt::NoPen);
        p.setRenderHint(QPainter::Antialiasing);
        r.adjust(offset_decremented, offset_decremented, -offset_decremented, -offset_decremented);
        if(outer_rect) {
            if(m_darkTheme) 
                p.setBrush(QColor(0, 0, 0, 240));
            else 
                p.setBrush(QColor(0, 0, 0, m_alpha));
        } else {
            p.setBrush(QColor(255, 255, 255, m_alpha));
        }
        if (m_cornersType == SquircledCorners) {
            const QPainterPath squircle2 = drawSquircle((size-offset_decremented), offset_decremented);
            p.drawPolygon(squircle2.toFillPolygon());
        } else {
            p.drawEllipse(r);
        }
        p.setCompositionMode(QPainter::CompositionMode_DestinationOut);
        p.setBrush(Qt::black);
        r.adjust(1, 1, -1, -1);
        if (m_cornersType == SquircledCorners) {
            const QPainterPath squircle3 = drawSquircle((size-m_shadowOffset*m_zoom), m_shadowOffset*m_zoom);
            p.drawPolygon(squircle3.toFillPolygon());
        } else {
            p.drawEllipse(r);
        }
    }
    p.end();

    return img;
}

void
LightlyShadersEffect::genMasks(EffectScreen *s)
{
    for (int i = 0; i < NTex; ++i) {
        if (m_screens[s].tex[i])
            delete m_screens[s].tex[i];
        if (m_screens[s].mask[i])
            delete m_screens[s].mask[i];
    }

    int size = m_screens[s].sizeScaled + m_shadowOffset*m_zoom;

    QImage img = genMaskImg(size, true, false);
    
    m_screens[s].tex[TopLeft] = new GLTexture(img.copy(0, 0, size, size), GL_TEXTURE_2D);
    m_screens[s].tex[TopRight] = new GLTexture(img.copy(size, 0, size, size), GL_TEXTURE_2D);
    m_screens[s].tex[BottomRight] = new GLTexture(img.copy(size, size, size, size), GL_TEXTURE_2D);
    m_screens[s].tex[BottomLeft] = new GLTexture(img.copy(0, size, size, size), GL_TEXTURE_2D);

    m_screens[s].mask[TopLeft] = new QRegion(QBitmap::fromImage(img.copy(0, 0, size, size).createMaskFromColor(QColor( Qt::black ).rgb(), Qt::MaskOutColor), Qt::DiffuseAlphaDither));
    m_screens[s].mask[TopRight] = new QRegion(QBitmap::fromImage(img.copy(size, 0, size, size).createMaskFromColor(QColor( Qt::black ).rgb(), Qt::MaskOutColor), Qt::DiffuseAlphaDither));
    m_screens[s].mask[BottomRight] = new QRegion(QBitmap::fromImage(img.copy(size, size, size, size).createMaskFromColor(QColor( Qt::black ).rgb(), Qt::MaskOutColor), Qt::DiffuseAlphaDither));
    m_screens[s].mask[BottomLeft] = new QRegion(QBitmap::fromImage(img.copy(0, size, size, size).createMaskFromColor(QColor( Qt::black ).rgb(), Qt::MaskOutColor), Qt::DiffuseAlphaDither));
    
}

void
LightlyShadersEffect::genRect(EffectScreen *s)
{
    for (int i = 0; i < NTex; ++i) {
        if (m_screens[s].rect[i])
            delete m_screens[s].rect[i];
        if (m_screens[s].darkRect[i])
            delete m_screens[s].darkRect[i];
    }

    int size = m_screens[s].sizeScaled + (m_shadowOffset*m_zoom-1);

    QImage img = genMaskImg(size, false, false);

    m_screens[s].rect[TopLeft] = new GLTexture(img.copy(0, 0, size, size));
    m_screens[s].rect[TopRight] = new GLTexture(img.copy(size, 0, size, size));
    m_screens[s].rect[BottomRight] = new GLTexture(img.copy(size, size, size, size));
    m_screens[s].rect[BottomLeft] = new GLTexture(img.copy(0, size, size, size));

    size = m_screens[s].sizeScaled + m_shadowOffset*m_zoom;
    QImage img2 = genMaskImg(size, false, true);

    m_screens[s].darkRect[TopLeft] = new GLTexture(img2.copy(0, 0, size, size));
    m_screens[s].darkRect[TopRight] = new GLTexture(img2.copy(size, 0, size, size));
    m_screens[s].darkRect[BottomRight] = new GLTexture(img2.copy(size, size, size, size));
    m_screens[s].darkRect[BottomLeft] = new GLTexture(img2.copy(0, size, size, size));
}

void
LightlyShadersEffect::setRoundness(const int r, EffectScreen *s)
{
    m_size = r;
    m_screens[s].sizeScaled = r*m_screens[s].scale;
    m_corner = QSize(m_size+(m_shadowOffset*m_zoom-1), m_size+(m_shadowOffset*m_zoom-1));
    genMasks(s);
    genRect(s);
}

void
LightlyShadersEffect::reconfigure(ReconfigureFlags flags)
{
    Q_UNUSED(flags)
    KConfigGroup conf = KSharedConfig::openConfig("lightlyshaders.conf")->group("General");
    m_alpha = int(conf.readEntry("alpha", 15)*2.55);
    m_outline = conf.readEntry("outline", false);
    m_darkTheme = conf.readEntry("dark_theme", false);
    m_disabledForMaximized = conf.readEntry("disabled_for_maximized", false);
    m_cornersType = conf.readEntry("corners_type", int(RoundedCorners));
    m_squircleRatio = int(conf.readEntry("squircle_ratio", 12));
    m_shadowOffset = int(conf.readEntry("shadow_offset", 2));
    m_roundness = int(conf.readEntry("roundness", 5));
    if(m_shadowOffset>=m_roundness) {
        m_shadowOffset = m_roundness-1;
    }

    const auto screens = effects->screens();
    for(EffectScreen *s : screens)
    {
        if (effects->waylandDisplay() == nullptr) {
            s = nullptr;
        }
        setRoundness(m_roundness, s);

        if (effects->waylandDisplay() == nullptr) {
            break;
        }
    }
}

void
LightlyShadersEffect::paintScreen(int mask, const QRegion &region, ScreenPaintData &data)
{
    EffectScreen *s = data.screen();
    if (effects->waylandDisplay() == nullptr) {
        s = nullptr;
    }

    bool set_roundness = false;

#if KWIN_EFFECT_API_VERSION < 234
    qreal scale = GLRenderTarget::virtualScreenScale();
#else
    qreal scale = effects->renderTargetScale();
#endif
    if(scale != m_screens[s].scale) {
        m_screens[s].scale = scale;
        set_roundness = true;
    }

    qreal zoom = data.xScale();
    if(zoom != m_zoom) {
        m_zoom = zoom;
        set_roundness = true;
    }

    if(set_roundness) {
        setRoundness(m_roundness*m_zoom, s);
        //qDebug() << "Set roundness";
    } 

    m_xTranslation = data.xTranslation();
    m_yTranslation = data.yTranslation();

    effects->paintScreen(mask, region, data);
}

void
LightlyShadersEffect::prePaintWindow(EffectWindow *w, WindowPrePaintData &data, std::chrono::milliseconds time)
{
    if (!isValidWindow(w))
    {
        effects->prePaintWindow(w, data, time);
        return;
    }

    EffectScreen *s = w->screen();
    if (effects->waylandDisplay() == nullptr) {
        s = nullptr;
    } 

    const QRect geo(w->frameGeometry());
    for (int corner = 0; corner < NTex; ++corner)
    {
        QRegion reg = QRegion(m_screens[s].mask[corner]->boundingRect());
        switch(corner) {
            case TopLeft:
                reg.translate(geo.x()-m_shadowOffset, geo.y()-m_shadowOffset);
                break;
            case TopRight:
                reg.translate(geo.x() + geo.width() - m_size, geo.y()-m_shadowOffset);
                break;
            case BottomRight:
                reg.translate(geo.x() + geo.width() - m_size-1, geo.y()+geo.height()-m_size-1); 
                break;
            case BottomLeft:
                reg.translate(geo.x()-m_shadowOffset+1, geo.y()+geo.height()-m_size-1);
                break;
            default:
                break;
        }
        
    #if KWIN_EFFECT_API_VERSION < 234
        data.clip -= reg;
    #else
        data.opaque -= reg;
    #endif
    }

    QRegion blur_region = w->data(WindowBlurBehindRole).value<QRegion>();
    if(!blur_region.isEmpty() || w->windowClass().contains("konsole", Qt::CaseInsensitive)) {   
        if(w->windowClass().contains("konsole", Qt::CaseInsensitive)) {
            blur_region = QRegion(0,0,geo.width(),geo.height());    
        }

        QRegion top_left = *m_screens[s].mask[TopLeft];
        top_left.translate(0-m_shadowOffset+1, -(geo.height() - w->contentsRect().height())-m_shadowOffset+1);  
        blur_region = blur_region.subtracted(top_left);  
        
        QRegion top_right = *m_screens[s].mask[TopRight];
        top_right.translate(geo.width() - m_size-1, -(geo.height() - w->contentsRect().height())-m_shadowOffset+1);   
        blur_region = blur_region.subtracted(top_right);  

        QRegion bottom_right = *m_screens[s].mask[BottomRight];
        bottom_right.translate(geo.width() - m_size-1, w->contentsRect().height()-m_size-1);    
        blur_region = blur_region.subtracted(bottom_right);     
        
        QRegion bottom_left = *m_screens[s].mask[BottomLeft];
        bottom_left.translate(0-m_shadowOffset+1, w->contentsRect().height()-m_size-1);
        blur_region = blur_region.subtracted(bottom_left);
        
        w->setData(WindowBlurBehindRole, blur_region);
    }

    effects->prePaintWindow(w, data, time);
    /*return;

    if(m_windows[w].hasFadeInAnimation) {
        if (m_windows[w].animationTime == std::chrono::milliseconds(0)) {
            m_windows[w].animationTime = time;
        } else if((time - m_windows[w].animationTime) > std::chrono::milliseconds(150)) { // hardcode fade in animation duration as 150 milliseconds
            m_windows[w].hasFadeInAnimation = false;
        }
    }

    if (!isValidWindow(w))
    {
        effects->prePaintWindow(w, data, time);
        return;
    }

    const QRect s_geo(effects->virtualScreenGeometry());
    const QRegion exp_geo(w->expandedGeometry());
    const QRegion shadow = exp_geo - geo;

    if(shadow.intersects(data.paint) && geo.width() != s_geo.width()) {
        m_windows[w].updateDiffTex = true;
        //qDebug() << "shadow repainted";
    }

    int offset_decremented = m_shadowOffset-1;

    const QRect rect[NTex] =
    {
        QRect(geo.topLeft()-QPoint(offset_decremented,offset_decremented), m_corner),
        QRect(geo.topRight()-QPoint(m_size-offset_decremented, offset_decremented), m_corner),
        QRect(geo.bottomRight()-QPoint(m_size-offset_decremented, m_size-offset_decremented), m_corner),
        QRect(geo.bottomLeft()-QPoint(offset_decremented, m_size-offset_decremented), m_corner)
    };
    QRegion repaintRegion(QRegion(
        geo.adjusted(
            -m_shadowOffset, 
            -m_shadowOffset, 
            m_shadowOffset, 
            m_shadowOffset))
        -geo.adjusted(
            m_shadowOffset, 
            m_shadowOffset, 
            -m_shadowOffset, 
            -m_shadowOffset)
    );

    for (int i = 0; i < NTex; ++i)
    {
        repaintRegion += rect[i];
    }

    QRegion clip = QRegion();
    QRegion clip_scaled = QRegion();

    QSize size(m_size + m_shadowOffset, m_size + m_shadowOffset);

    const auto stackingOrder = effects->stackingOrder();
    bool bottom_w = true;
    for (EffectWindow *window : stackingOrder) {
        if(!window->isVisible()
            || window->isDeleted()
            || window->opacity() != 1.0
            || window->isUserMove()
            || window->isUserResize()
            || window->windowClass().contains("latte-dock", Qt::CaseInsensitive)
            || window->windowClass().contains("lattedock", Qt::CaseInsensitive)
            || window->windowClass().contains("plank", Qt::CaseInsensitive)
            || window->windowClass().contains("cairo-dock", Qt::CaseInsensitive)
            || window->windowClass().contains("peek", Qt::CaseInsensitive)
        ) continue;

        const void *addedGrab = window->data(WindowAddedGrabRole).value<void *>();
        if ((addedGrab && m_windows[window].hasFadeInAnimation != false) || !m_windows.contains(window)) continue;

        if(window != w) {
            QRect w_geo = window->frameGeometry();

            const QRect w_rect[NTex] =
            {
                QRect(w_geo.topLeft()-QPoint(m_shadowOffset,m_shadowOffset), size),
                QRect(w_geo.topRight()-QPoint(m_size, m_shadowOffset), size),
                QRect(w_geo.bottomRight()-QPoint(m_size, m_size), size),
                QRect(w_geo.bottomLeft()-QPoint(m_shadowOffset, m_size), size)
            };

            if (bottom_w)
            {

                for (int i = 0; i < NTex; ++i)
                {
                    if(geo.intersects(w_rect[i]) && !geo.contains(w_rect[i]) ) {
                        clip -= w_rect[i];
                        clip_scaled -= scale(w_rect[i], m_screens[s].scale*m_zoom);
                        repaintRegion += w_rect[i];
                    }
                }
                continue;
            }

            clip += w_geo;
            clip_scaled += scale(w_geo, m_screens[s].scale);

            for (int i = 0; i < NTex; ++i)
            {
                clip -= w_rect[i];
                clip_scaled -= scale(w_rect[i], m_screens[s].scale*m_zoom);
            }
        } else {
            bottom_w = false;
        }
    }

    repaintRegion -= clip;

    for (int i = 0; i < NTex; ++i)
    {
        if(clip.intersects(rect[i]) && !clip.contains(rect[i])) {
            clip -= rect[i];
            clip_scaled -= scale(rect[i], m_screens[s].scale*m_zoom);
            repaintRegion += rect[i];
        }
    }

    if(m_windows[w].clip != clip_scaled) {
        m_windows[w].clip = clip_scaled;
        m_windows[w].updateDiffTex = true;
    }

    if(w->isUserMove() || w->isUserResize()) {
        repaintRegion += exp_geo;
    }

#if KWIN_EFFECT_API_VERSION < 234
    data.clip -= repaintRegion;
#else
    data.opaque -= repaintRegion;
#endif
    data.paint += repaintRegion;

    if(shadow.isEmpty()) {
        data.paint = infiniteRegion();
        GLTexture tex = GLTexture(GL_TEXTURE_2D);
        m_windows[w].diffTextures[s] = QList<GLTexture>();
        m_windows[w].updateDiffTex = false;
        for (int i = 0; i < NTex; ++i)
        {
            m_windows[w].diffTextures[s].append(tex);
        }
    }

    /*QRegion blur_region = w->data(WindowBlurBehindRole).value<QRegion>();
    if(!blur_region.isEmpty() || w->windowClass().contains("konsole", Qt::CaseInsensitive)) {   
        if(w->windowClass().contains("konsole", Qt::CaseInsensitive)) {
            blur_region = QRegion(geo);    
        }

        QRegion top_left = *m_screens[s].mask[TopLeft];
        top_left.translate(0-m_shadowOffset, -(geo.height() - w->contentsRect().height())-m_shadowOffset);  
        blur_region = blur_region.subtracted(top_left);  
        
        QRegion top_right = *m_screens[s].mask[TopRight];
        top_right.translate(geo.width() - m_size, -(geo.height() - w->contentsRect().height())-m_shadowOffset);   
        blur_region = blur_region.subtracted(top_right);  

        QRegion bottom_right = *m_screens[s].mask[BottomRight];
        bottom_right.translate(geo.width() - m_size, w->contentsRect().height()-m_size);    
        blur_region = blur_region.subtracted(bottom_right);     
        
        QRegion bottom_left = *m_screens[s].mask[BottomLeft];
        bottom_left.translate(0-m_shadowOffset, w->contentsRect().height()-m_size);
        blur_region = blur_region.subtracted(bottom_left);
        
        w->setData(WindowBlurBehindRole, blur_region);

        /*if(w->windowClass().contains("konsole", Qt::CaseInsensitive)) {
            blur_region = QRegion(geo);
            KWindowEffects::enableBlurBehind(w->internalWindow() != nullptr ? w->internalWindow()->winId() : w->windowId(), true, blur_region);
        }*
    }*

    effects->prePaintWindow(w, data, time);*/
}

bool
LightlyShadersEffect::isValidWindow(EffectWindow *w, int mask)
{
#if KWIN_EFFECT_API_VERSION < 234
    const QRect screen = GLRenderTarget::virtualScreenGeometry();
#else
    const QRect screen = effects->renderTargetRect();
#endif
    if (!m_shader->isValid()
            //|| (!w->isOnCurrentDesktop() && !(mask & PAINT_WINDOW_TRANSFORMED))
            //|| w->isMinimized()
            || !m_windows[w].isManaged
        /*#if KWIN_EFFECT_API_VERSION < 234
            || !w->isPaintingEnabled()
        #endif*/
            //|| effects->hasActiveFullScreenEffect()
            || w->isFullScreen()
            || w->isDesktop()
            || w->isSpecialWindow()
            || m_windows[w].skipEffect
            || (!screen.intersects(w->frameGeometry()) && !(mask & PAINT_WINDOW_TRANSFORMED))
        )
    {
        return false;
    }
    return true;
}

void
LightlyShadersEffect::drawWindow(EffectWindow *w, int mask, const QRegion &region, WindowPaintData &data)
{    
    if (!isValidWindow(w, mask) /*|| (mask & (PAINT_WINDOW_TRANSFORMED|PAINT_SCREEN_WITH_TRANSFORMED_WINDOWS))*/)
    {
        effects->drawWindow(w, mask, region, data);
        return;
    }

    EffectScreen *s = w->screen();
    if (effects->waylandDisplay() == nullptr) {
        s = nullptr;
    }

#if KWIN_EFFECT_API_VERSION < 234
    const QRect screen = scale(GLRenderTarget::virtualScreenGeometry(), m_screens[s].scale);
#else
    const QRect screen = scale(effects->renderTargetRect(), m_screens[s].scale);
#endif
    qreal xTranslation = screen.x() - m_xTranslation*m_screens[s].scale;
    qreal yTranslation = effects->virtualScreenSize().height() - screen.height() - screen.y() + m_yTranslation*m_screens[s].scale;

    //qDebug() << m_xTranslation;
    
    //map the corners
    QRect geo(w->frameGeometry());
    geo.translate(data.xTranslation(), data.yTranslation());
    const QRect geo_scaled = scale(geo, m_screens[s].scale*m_zoom);
    const QSize size_scaled(m_screens[s].sizeScaled+m_shadowOffset*m_zoom, m_screens[s].sizeScaled+m_shadowOffset*m_zoom);
    const QRect big_rect_scaled[NTex] =
    {
        QRect(geo_scaled.topLeft()-QPoint(m_shadowOffset*m_zoom,m_shadowOffset*m_zoom), size_scaled),
        QRect(geo_scaled.topRight()-QPoint(m_screens[s].sizeScaled-1, m_shadowOffset*m_zoom), size_scaled),
        QRect(geo_scaled.bottomRight()-QPoint(m_screens[s].sizeScaled-1, m_screens[s].sizeScaled-1), size_scaled),
        QRect(geo_scaled.bottomLeft()-QPoint(m_shadowOffset*m_zoom, m_screens[s].sizeScaled-1), size_scaled)
    };

    //check if one of the corners is out of screen
    QRect screen_scaled = scale(screen, m_zoom);
    bool out_of_screen = (
        (
            !screen_scaled.contains(big_rect_scaled[TopLeft], true)
            && screen_scaled.intersects(big_rect_scaled[TopLeft])
        ) ||
        (
            !screen_scaled.contains(big_rect_scaled[TopRight], true)
            && screen_scaled.intersects(big_rect_scaled[TopRight])
        ) ||
        (
            !screen_scaled.contains(big_rect_scaled[BottomRight], true)
            && screen_scaled.intersects(big_rect_scaled[BottomRight])
        ) ||
        (
            !screen_scaled.contains(big_rect_scaled[BottomLeft], true)
            &&screen_scaled.intersects(big_rect_scaled[BottomLeft])
        )
    ) && !(mask & PAINT_WINDOW_TRANSFORMED);

    if(w->isDeleted() && out_of_screen) {
        effects->drawWindow(w, mask, region, data);
        return;
    }

    if(data.opacity() == 1.0) {
        for (int corner = 0; corner < NTex; ++corner)
        {       
            QRegion reg = *m_screens[s].mask[corner];
            switch(corner) {
                case TopLeft:
                    reg.translate(geo.x()-m_shadowOffset, geo.y()-m_shadowOffset);
                    break;
                case TopRight:
                    reg.translate(geo.x() + geo.width() - m_size, geo.y()-m_shadowOffset);
                    break;
                case BottomRight:
                    reg.translate(geo.x() + geo.width() - m_size, geo.y()+geo.height()-m_size); 
                    break;
                case BottomLeft:
                    reg.translate(geo.x()-m_shadowOffset, geo.y()+geo.height()-m_size);
                    break;
            }

            //We need to trigger blending in scene_opengl.cpp in order to have antialiased corners
            if(region.intersects(reg)) {
                data.setOpacity(0.9999999);
                break;
            }
        }
    }

    const QRect s_geo(effects->virtualScreenGeometry());

    //Draw rounded corners with shadows   
    const int mvpMatrixLocation = m_shader->uniformLocation("modelViewProjectionMatrix");
    const int frameSizeLocation = m_shader->uniformLocation("frame_size");
    const int expandedSizeLocation = m_shader->uniformLocation("expanded_size");
    const int csdShadowOffsetLocation = m_shader->uniformLocation("csd_shadow_offset");
    const int radiusLocation = m_shader->uniformLocation("radius");
    const int shadowOffsetLocation = m_shader->uniformLocation("shadow_sample_offset");
    ShaderManager *sm = ShaderManager::instance();
    sm->pushShader(m_shader);

    data.shader = m_shader;

    QMatrix4x4 mvp = data.screenProjectionMatrix();
    mvp.scale(1.0/(m_screens[s].scale*m_zoom));
    //mvp.translate(big_rect_scaled[0].x(), big_rect_scaled[0].y());
    m_shader->setUniform(mvpMatrixLocation, mvp);
    m_shader->setUniform(frameSizeLocation, QVector2D(w->frameGeometry().width(), w->frameGeometry().height()));
    m_shader->setUniform(expandedSizeLocation, QVector2D(w->expandedGeometry().width(), w->expandedGeometry().height()));
    m_shader->setUniform(csdShadowOffsetLocation, QVector2D(w->frameGeometry().x() - w->expandedGeometry().x(), w->frameGeometry().y()-w->expandedGeometry().y()));
    m_shader->setUniform(radiusLocation, m_screens[s].sizeScaled);
    m_shader->setUniform(shadowOffsetLocation, m_shadowOffset);
    glActiveTexture(GL_TEXTURE4);
    m_screens[s].tex[BottomLeft]->bind();
    glActiveTexture(GL_TEXTURE3);
    m_screens[s].tex[BottomRight]->bind();
    glActiveTexture(GL_TEXTURE2);
    m_screens[s].tex[TopRight]->bind();
    glActiveTexture(GL_TEXTURE1);
    m_screens[s].tex[TopLeft]->bind();
    glActiveTexture(GL_TEXTURE0);
    effects->drawWindow(w, mask, region, data);
    m_screens[s].tex[TopLeft]->unbind();
    m_screens[s].tex[TopRight]->unbind();
    m_screens[s].tex[BottomRight]->unbind();
    m_screens[s].tex[BottomLeft]->unbind();
    sm->popShader();

}

QRect
LightlyShadersEffect::scale(const QRect rect, qreal scaleFactor)
{
    return QRect(
        rect.x()*scaleFactor,
        rect.y()*scaleFactor,
        rect.width()*scaleFactor,
        rect.height()*scaleFactor
    );
}

bool
LightlyShadersEffect::enabledByDefault()
{
    return supported();
}

bool
LightlyShadersEffect::supported()
{
    const qint64 version = kVersionNumber(1, 40);
#if KWIN_EFFECT_API_VERSION < 234
    return effects->isOpenGLCompositing() && GLRenderTarget::supported() && GLPlatform::instance()->glslVersion() >= version;
#else
    return effects->isOpenGLCompositing() && GLFramebuffer::supported() && GLPlatform::instance()->glslVersion() >= version;
#endif
}

#include "lightlyshaders.moc"


} // namespace KWin

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
#include <KDecoration2/Decoration>


namespace KWin {

#ifndef KWIN_PLUGIN_FACTORY_NAME
KWIN_EFFECT_FACTORY_SUPPORTED_ENABLED(LightlyShadersFactory, LightlyShadersEffect, "lightlyshaders.json", return LightlyShadersEffect::supported();, return LightlyShadersEffect::enabledByDefault();)
#else
KWIN_EFFECT_FACTORY_SUPPORTED_ENABLED(LightlyShadersEffect, "lightlyshaders.json", return LightlyShadersEffect::supported();, return LightlyShadersEffect::enabledByDefault();)
#endif


LightlyShadersEffect::LightlyShadersEffect() : Effect()
{
    const auto screens = effects->screens();
    for(EffectScreen *s : screens)
    {
        if (effects->waylandDisplay() == nullptr) {
            s = nullptr;
        }
        for (int i = 0; i < NTex; ++i)
        {
            m_screens[s].maskRegion[i] = 0;
        }
        m_screens[s].maskTex = 0;
        m_screens[s].lightOutlineTex = 0;
        m_screens[s].darkOutlineTex = 0;
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
    m_shader = std::unique_ptr<GLShader>(ShaderManager::instance()->generateCustomShader(ShaderTrait::MapTexture, QByteArray(), frag));
    file_shader.close();

    if (m_shader->isValid())
    {
        const int sampler = m_shader->uniformLocation("sampler");
        const int mask_sampler = m_shader->uniformLocation("mask_sampler");
        const int light_outline_sampler = m_shader->uniformLocation("light_outline_sampler");
        const int dark_outline_sampler = m_shader->uniformLocation("dark_outline_sampler");
        const int expanded_size = m_shader->uniformLocation("expanded_size");
        const int frame_size = m_shader->uniformLocation("frame_size");
        const int csd_shadow_offset = m_shader->uniformLocation("csd_shadow_offset");
        const int radius = m_shader->uniformLocation("radius");
        const int shadow_sample_offset = m_shader->uniformLocation("shadow_sample_offset");
        const int content_size = m_shader->uniformLocation("content_size");
        const int is_wayland = m_shader->uniformLocation("is_wayland");
        const int has_decoration = m_shader->uniformLocation("has_decoration");
        const int shadow_tex_size = m_shader->uniformLocation("shadow_tex_size");
        const int outline_strength = m_shader->uniformLocation("outline_strength");
        const int draw_outline = m_shader->uniformLocation("draw_outline");
        const int dark_theme = m_shader->uniformLocation("dark_theme");
        const int scale = m_shader->uniformLocation("scale");
        ShaderManager::instance()->pushShader(m_shader.get());
        m_shader->setUniform(scale, 15);
        m_shader->setUniform(dark_theme, 14);
        m_shader->setUniform(draw_outline, 13);
        m_shader->setUniform(outline_strength, 12);
        m_shader->setUniform(shadow_tex_size, 11);
        m_shader->setUniform(has_decoration, 11);
        m_shader->setUniform(is_wayland, 10);
        m_shader->setUniform(content_size, 9);
        m_shader->setUniform(shadow_sample_offset, 8);
        m_shader->setUniform(radius, 7);
        m_shader->setUniform(csd_shadow_offset, 6);
        m_shader->setUniform(frame_size, 5);
        m_shader->setUniform(expanded_size, 4);
        m_shader->setUniform(dark_outline_sampler, 3);
        m_shader->setUniform(light_outline_sampler, 2);
        m_shader->setUniform(mask_sampler, 1);
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
    const auto screens = effects->screens();
    for(EffectScreen *s : screens)
    {
        if (effects->waylandDisplay() == nullptr) {
            s = nullptr;
        }
        for (int i = 0; i < NTex; ++i)
        {
            if (m_screens[s].maskRegion[i])
                delete m_screens[s].maskRegion[i];
        }
        if (m_screens[s].maskTex)
            delete m_screens[s].maskTex;
        if (m_screens[s].lightOutlineTex)
            delete m_screens[s].lightOutlineTex;
        if (m_screens[s].darkOutlineTex)
            delete m_screens[s].darkOutlineTex;
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

    QRectF maximized_area = effects->clientArea(MaximizeArea, w);
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
    int offset_decremented;
    if(outer_rect) {
        offset_decremented = m_shadowOffset-1;
    } else {
        offset_decremented = m_shadowOffset;
    }

    if(mask) {
        p.fillRect(img.rect(), Qt::black);
        p.setCompositionMode(QPainter::CompositionMode_DestinationOut);
        p.setPen(Qt::NoPen);
        p.setBrush(Qt::black);
        p.setRenderHint(QPainter::Antialiasing);
        if (m_cornersType == SquircledCorners) {
            const QPainterPath squircle1 = drawSquircle((size-m_shadowOffset), m_shadowOffset);
            p.drawPolygon(squircle1.toFillPolygon());
        } else {
            p.drawEllipse(r.adjusted(m_shadowOffset,m_shadowOffset,-m_shadowOffset,-m_shadowOffset));
        }
    } else {
        p.setPen(Qt::NoPen);
        p.setRenderHint(QPainter::Antialiasing);
        r.adjust(offset_decremented, offset_decremented, -offset_decremented, -offset_decremented);
        if(outer_rect) {
            p.setBrush(QColor(0, 0, 0, 255));
        } else {
            p.setBrush(QColor(255, 255, 255, 255));
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
            const QPainterPath squircle3 = drawSquircle((size-(offset_decremented+1)), (offset_decremented+1));
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
        if (m_screens[s].maskRegion[i])
            delete m_screens[s].maskRegion[i];
    }
    if (m_screens[s].maskTex)
        delete m_screens[s].maskTex;

    int size = m_screens[s].sizeScaled + m_shadowOffset;
    QImage img = genMaskImg(size, true, false);
    
    m_screens[s].maskTex = new GLTexture(img, GL_TEXTURE_2D);


    size = m_size + m_shadowOffset;
    img = genMaskImg(size, true, false);

    m_screens[s].maskRegion[TopLeft] = new QRegion(QBitmap::fromImage(img.copy(0, 0, size, size).createMaskFromColor(QColor( Qt::black ).rgb(), Qt::MaskOutColor), Qt::DiffuseAlphaDither));
    m_screens[s].maskRegion[TopRight] = new QRegion(QBitmap::fromImage(img.copy(size, 0, size, size).createMaskFromColor(QColor( Qt::black ).rgb(), Qt::MaskOutColor), Qt::DiffuseAlphaDither));
    m_screens[s].maskRegion[BottomRight] = new QRegion(QBitmap::fromImage(img.copy(size, size, size, size).createMaskFromColor(QColor( Qt::black ).rgb(), Qt::MaskOutColor), Qt::DiffuseAlphaDither));
    m_screens[s].maskRegion[BottomLeft] = new QRegion(QBitmap::fromImage(img.copy(0, size, size, size).createMaskFromColor(QColor( Qt::black ).rgb(), Qt::MaskOutColor), Qt::DiffuseAlphaDither));
    
}

void
LightlyShadersEffect::genRect(EffectScreen *s)
{
    if (m_screens[s].lightOutlineTex)
        delete m_screens[s].lightOutlineTex;
    if (m_screens[s].darkOutlineTex)
        delete m_screens[s].darkOutlineTex;

    int size = m_screens[s].sizeScaled + m_shadowOffset;

    QImage img = genMaskImg(size, false, false);

    m_screens[s].lightOutlineTex = new GLTexture(img, GL_TEXTURE_2D);

    QImage img2 = genMaskImg(size, false, true);

    m_screens[s].darkOutlineTex = new GLTexture(img2, GL_TEXTURE_2D);
}

void
LightlyShadersEffect::setRoundness(const int r, EffectScreen *s)
{
    m_size = r;
    m_screens[s].sizeScaled = r*m_screens[s].scale;
    m_corner = QSize(m_size+(m_shadowOffset-1), m_size+(m_shadowOffset-1));
    genMasks(s);
    genRect(s);
}

void
LightlyShadersEffect::reconfigure(ReconfigureFlags flags)
{
    Q_UNUSED(flags)
    KConfigGroup conf = KSharedConfig::openConfig("lightlyshaders.conf")->group("General");
    m_alpha = int(conf.readEntry("alpha", 15));
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

    if(set_roundness) {
        setRoundness(m_roundness, s);
        //qDebug() << "Set roundness";
    } 

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

    const QRectF geo(w->frameGeometry());
    for (int corner = 0; corner < NTex; ++corner)
    {
        QRegion reg = QRegion(scale(m_screens[s].maskRegion[corner]->boundingRect(), m_screens[s].scale).toRect());
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
    // force blur on these windows
    bool window_should_blur = w->windowClass().contains("konsole", Qt::CaseInsensitive) || w->windowClass().contains("conky-window", Qt::CaseInsensitive) || w->windowClass().contains("alacritty", Qt::CaseInsensitive) || w->windowClass().contains("firefox", Qt::CaseInsensitive);
    if(!blur_region.isEmpty() || window_should_blur) {
        if(window_should_blur) {
            blur_region = QRegion(0,0,geo.width(),geo.height());
        }

        QRegion top_left = *m_screens[s].maskRegion[TopLeft];
        top_left.translate(0-m_shadowOffset+1, -(geo.height() - w->contentsRect().height())-m_shadowOffset+1);  
        blur_region = blur_region.subtracted(top_left);  
        
        QRegion top_right = *m_screens[s].maskRegion[TopRight];
        top_right.translate(geo.width() - m_size-1, -(geo.height() - w->contentsRect().height())-m_shadowOffset+1);   
        blur_region = blur_region.subtracted(top_right);  

        QRegion bottom_right = *m_screens[s].maskRegion[BottomRight];
        bottom_right.translate(geo.width() - m_size-1, w->contentsRect().height()-m_size-1);    
        blur_region = blur_region.subtracted(bottom_right);     
        
        QRegion bottom_left = *m_screens[s].maskRegion[BottomLeft];
        bottom_left.translate(0-m_shadowOffset+1, w->contentsRect().height()-m_size-1);
        blur_region = blur_region.subtracted(bottom_left);
        
        w->setData(WindowBlurBehindRole, blur_region);
    }

    if(w->decoration() != nullptr && w->decoration()->shadow() != nullptr ) {
        const QImage shadow = w->decoration()->shadow()->shadow();
        if(shadow.width() != m_windows[w].shadowTexSize.x() || shadow.height() != m_windows[w].shadowTexSize.y()) {
            m_windows[w].shadowTexSize.setX(shadow.width());
            m_windows[w].shadowTexSize.setY(shadow.height());
        }
    }

    if(!w->isDeleted() && m_windows[w].hasDecoration != (w->decoration() != nullptr)) {
        m_windows[w].hasDecoration = w->decoration() != nullptr;
    }

    effects->prePaintWindow(w, data, time);
}

bool
LightlyShadersEffect::isValidWindow(EffectWindow *w, int mask)
{
#if KWIN_EFFECT_API_VERSION < 234
    const QRectF screen = QRectF(GLRenderTarget::virtualScreenGeometry());
#else
    const QRectF screen = QRectF(effects->renderTargetRect());
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

    QRectF geo(w->frameGeometry());
    QRectF exp_geo(w->expandedGeometry());
    QRectF contents_geo(w->contentsRect());
    //geo.translate(data.xTranslation(), data.yTranslation());
    const QRectF geo_scaled = scale(geo, m_screens[s].scale);
    const QRectF contents_geo_scaled = scale(contents_geo, m_screens[s].scale);
    const QRectF exp_geo_scaled = scale(exp_geo, m_screens[s].scale);

    if(data.opacity() == 1.0) {
        for (int corner = 0; corner < NTex; ++corner)
        {       
            QRegion reg = *m_screens[s].maskRegion[corner];
            switch(corner) {
                case TopLeft:
                    reg.translate(geo.x() - m_shadowOffset, geo.y() - m_shadowOffset);
                    break;
                case TopRight:
                    reg.translate(geo.x() + geo.width() - m_size, geo.y() - m_shadowOffset);
                    break;
                case BottomRight:
                    reg.translate(geo.x() + geo.width() - m_size, geo.y() + geo.height() - m_size); 
                    break;
                case BottomLeft:
                    reg.translate(geo.x() - m_shadowOffset, geo.y() + geo.height() - m_size);
                    break;
            }

            //We need to trigger blending in scene_opengl.cpp in order to have antialiased corners
            if(region.intersects(reg)) {
                data.setOpacity(0.9999999);
                break;
            }
        }
    }

    //Draw rounded corners with shadows   
    //const int mvpMatrixLocation = m_shader->uniformLocation("modelViewProjectionMatrix");
    const int frameSizeLocation = m_shader->uniformLocation("frame_size");
    const int expandedSizeLocation = m_shader->uniformLocation("expanded_size");
    const int csdShadowOffsetLocation = m_shader->uniformLocation("csd_shadow_offset");
    const int radiusLocation = m_shader->uniformLocation("radius");
    const int shadowOffsetLocation = m_shader->uniformLocation("shadow_sample_offset");
    const int contentSizeLocation = m_shader->uniformLocation("content_size");
    const int isWaylandLocation = m_shader->uniformLocation("is_wayland");
    const int hasDecorationLocation = m_shader->uniformLocation("has_decoration");
    const int shadowTexSizeLocation = m_shader->uniformLocation("shadow_tex_size");
    const int outlineStrengthLocation = m_shader->uniformLocation("outline_strength");
    const int drawOutlineLocation = m_shader->uniformLocation("draw_outline");
    const int darkThemeLocation = m_shader->uniformLocation("dark_theme");
    const int scaleLocation = m_shader->uniformLocation("scale");
    ShaderManager *sm = ShaderManager::instance();
    sm->pushShader(m_shader.get());

    data.shader = m_shader.get();

    bool is_wayland = effects->waylandDisplay() != nullptr;
    //qDebug() << geo_scaled.width() << geo_scaled.height();
    m_shader->setUniform(frameSizeLocation, QVector2D(geo_scaled.width(), geo_scaled.height()));
    m_shader->setUniform(expandedSizeLocation, QVector2D(exp_geo_scaled.width(), exp_geo_scaled.height()));
    m_shader->setUniform(csdShadowOffsetLocation, QVector3D(geo_scaled.x() - exp_geo_scaled.x(), geo_scaled.y()-exp_geo_scaled.y(), exp_geo_scaled.height() - geo_scaled.height() - geo_scaled.y() + exp_geo_scaled.y() ));
    m_shader->setUniform(radiusLocation, m_screens[s].sizeScaled);
    m_shader->setUniform(shadowOffsetLocation, m_shadowOffset);
    m_shader->setUniform(contentSizeLocation, QVector2D(contents_geo_scaled.width(), contents_geo_scaled.height()));
    m_shader->setUniform(isWaylandLocation, is_wayland);
    m_shader->setUniform(hasDecorationLocation, m_windows[w].hasDecoration);
    m_shader->setUniform(shadowTexSizeLocation, m_windows[w].shadowTexSize);
    m_shader->setUniform(outlineStrengthLocation, float(m_alpha)/100);
    m_shader->setUniform(drawOutlineLocation, m_outline);
    m_shader->setUniform(darkThemeLocation, m_darkTheme);
    m_shader->setUniform(scaleLocation, float(m_screens[s].scale));
    glActiveTexture(GL_TEXTURE3);
    m_screens[s].darkOutlineTex->bind();
    glActiveTexture(GL_TEXTURE2);
    m_screens[s].lightOutlineTex->bind();
    glActiveTexture(GL_TEXTURE1);
    m_screens[s].maskTex->bind();
    glActiveTexture(GL_TEXTURE0);
    effects->drawWindow(w, mask, region, data);
    m_screens[s].maskTex->unbind();
    m_screens[s].lightOutlineTex->unbind();
    m_screens[s].darkOutlineTex->unbind();
    sm->popShader();
}

QRectF
LightlyShadersEffect::scale(const QRectF rect, qreal scaleFactor)
{
    return QRectF(
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

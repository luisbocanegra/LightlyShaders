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

#ifndef LIGHTLYSHADERS_H
#define LIGHTLYSHADERS_H

#include <kwinoffscreeneffect.h>

namespace KWin {

class GLTexture;

class Q_DECL_EXPORT LightlyShadersEffect : public OffscreenEffect
{
    Q_OBJECT
public:
    LightlyShadersEffect();
    ~LightlyShadersEffect();

    static bool supported();
    static bool enabledByDefault();
    
    void setRoundness(const int r, EffectScreen *s);
    void reconfigure(ReconfigureFlags flags) override;
    void paintScreen(int mask, const QRegion &region, ScreenPaintData &data) override;
    void prePaintWindow(EffectWindow* w, WindowPrePaintData& data, std::chrono::milliseconds time) override;
    void drawWindow(EffectWindow* w, int mask, const QRegion& region, WindowPaintData& data) override;
    virtual int requestedEffectChainPosition() const override { return 99; }

    enum { RoundedCorners = 0, SquircledCorners };

protected Q_SLOTS:
    void windowAdded(EffectWindow *window);
    void windowDeleted(EffectWindow *window);
    void windowMaximizedStateChanged(EffectWindow *window, bool horizontal, bool vertical);

private:
    enum { TopLeft = 0, TopRight, BottomRight, BottomLeft, NTex };
    enum { Top = 0, Bottom, NShad };

    struct LSWindowStruct
    {
        bool skipEffect;
        bool isManaged;
        bool hasDecoration = false;
        QVector2D shadowTexSize = QVector2D(0,0);
    };

    struct LSScreenStruct
    {
        qreal scale=1.0;
        int sizeScaled;
        GLTexture *maskTex;
        GLTexture *lightOutlineTex;
        GLTexture *darkOutlineTex;
        QRegion *maskRegion[NTex];
    };

    void genMasks(EffectScreen *s);
    void genRect(EffectScreen *s);

    bool isValidWindow(EffectWindow *w, int mask=0);

    void fillRegion(const QRegion &reg, const QColor &c);
    QPainterPath drawSquircle(float size, int translate);
    QImage genMaskImg(int size, bool mask, bool outer_rect);
    QRectF scale(const QRectF rect, qreal scaleFactor);

    int m_size, m_alpha, m_cornersType, m_squircleRatio, m_roundness, m_shadowOffset;
    bool m_outline, m_darkTheme, m_disabledForMaximized;
    std::unique_ptr<GLShader> m_shader;
    QSize m_corner;

    QMap<EffectScreen *, LSScreenStruct> m_screens;
    QMap<EffectWindow *, LSWindowStruct> m_windows;
};

} // namespace KWin

#endif //LIGHTLYSHADERS_H


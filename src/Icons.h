#pragma once
#include <QColor>
#include <QIcon>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>

// Tiny vector glyph icons drawn with QPainter. They do not depend on any font, so they can never
// render as an empty box, and they are recoloured to follow the active theme.
namespace Icons {

enum Glyph { Close, ChevronRight, ChevronDown };

inline QIcon glyph(Glyph g, const QColor& color, int size = 16)
{
    const qreal dpr = 2.0;
    QPixmap pm(int(size * dpr), int(size * dpr));
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    QPen pen(color, 1.7, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    p.setPen(pen);
    const qreal s = size, m = s * 0.24;
    switch (g) {
    case Close:
        p.drawLine(QPointF(m, m), QPointF(s - m, s - m));
        p.drawLine(QPointF(s - m, m), QPointF(m, s - m));
        break;
    case ChevronRight: {
        QPainterPath path;
        path.moveTo(s * 0.38, s * 0.24);
        path.lineTo(s * 0.64, s * 0.5);
        path.lineTo(s * 0.38, s * 0.76);
        p.drawPath(path);
        break;
    }
    case ChevronDown: {
        QPainterPath path;
        path.moveTo(s * 0.24, s * 0.38);
        path.lineTo(s * 0.5, s * 0.64);
        path.lineTo(s * 0.76, s * 0.38);
        p.drawPath(path);
        break;
    }
    }
    p.end();
    return QIcon(pm);
}

// Small filled circle, used as a status marker in tab titles.
inline QIcon dot(const QColor& color, int size = 16)
{
    const qreal dpr = 2.0;
    QPixmap pm(int(size * dpr), int(size * dpr));
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(color);
    const qreal d = size * 0.42;
    p.drawEllipse(QRectF((size - d) / 2, (size - d) / 2, d, d));
    p.end();
    return QIcon(pm);
}

} // namespace Icons

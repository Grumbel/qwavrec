// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "waveformwidget.h"

#include <QPainter>
#include <QPixmap>
#include <QPaintEvent>
#include <QResizeEvent>
#include <QPainterPath>
#include <QMouseEvent>
#include <QContextMenuEvent>
#include <QtMath>

WaveformWidget::WaveformWidget(QWidget *parent)
    : QWidget(parent)
{
    setMinimumHeight(64);
    setMinimumWidth(200);
    setCursor(Qt::PointingHandCursor);
    setMouseTracking(true);
    setContextMenuPolicy(Qt::DefaultContextMenu);
}

void WaveformWidget::setPeaks(const QVector<float> &peaks)
{
    if (m_peaks == peaks)
        return;
    m_peaks = peaks;
    invalidateBodyCache();
    update();
}

void WaveformWidget::setWaveformDensity(const QImage &image)
{
    if (m_waveDensity == image)
        return;
    m_waveDensity = image;
    invalidateBodyCache();
    update();
}

void WaveformWidget::setSpectrogram(const QImage &image)
{
    m_spectrogram = image;
    invalidateBodyCache();
    update();
}

void WaveformWidget::setDisplayMode(DisplayMode mode)
{
    if (m_mode == mode)
        return;
    m_mode = mode;
    invalidateBodyCache();
    update();
}

void WaveformWidget::clear()
{
    m_peaks.clear();
    m_waveDensity = QImage();
    m_spectrogram = QImage();
    m_playbackPos = 0.0;
    clearSelection();
    invalidateBodyCache();
    update();
}

int WaveformWidget::contentWidth() const
{
    return qMax(1, rect().adjusted(1, 1, -1, -1).width());
}

void WaveformWidget::invalidateBodyCache()
{
    m_bodyCacheDirty = true;
}

void WaveformWidget::ensureBodyCache()
{
    if (!m_bodyCacheDirty)
        return;
    const QRect r = rect();
    if (r.width() <= 0 || r.height() <= 0) {
        m_bodyCache = QPixmap();
        m_bodyCacheDirty = false;
        return;
    }
    m_bodyCache = QPixmap(r.size());
    m_bodyCache.fill(QColor(18, 22, 28));
    QPainter p(&m_bodyCache);
    p.setRenderHint(QPainter::Antialiasing, m_mode == DisplayMode::Waveform && m_peaks.size() < 800);
    p.setPen(QColor(40, 48, 58));
    p.drawRect(0, 0, r.width() - 1, r.height() - 1);
    const QRect inner = QRect(1, 1, r.width() - 2, r.height() - 2);
    const bool useSpec = (m_mode == DisplayMode::Spectrogram && !m_spectrogram.isNull());
    if (useSpec) {
        paintSpectrogram(p, inner);
    } else if (!m_waveDensity.isNull()) {
        paintWaveform(p, inner);
    } else if (!m_peaks.isEmpty()) {
        paintWaveform(p, inner);
    } else {
        p.setPen(QColor(90, 100, 110));
        p.drawText(inner, Qt::AlignCenter,
                   m_mode == DisplayMode::Spectrogram ? tr("No spectrogram") : tr("No waveform"));
    }
    m_bodyCacheDirty = false;
}

void WaveformWidget::setPlaybackPosition(qreal pos)
{
    const qreal p = qBound(0.0, pos, 1.0);
    if (qFuzzyCompare(p + 1.0, m_playbackPos + 1.0))
        return;
    m_playbackPos = p;
    update();
}

void WaveformWidget::setSelection(qreal a, qreal b)
{
    a = qBound(0.0, a, 1.0);
    b = qBound(0.0, b, 1.0);
    if (a > b)
        qSwap(a, b);
    m_selStart = a;
    m_selEnd = b;
    update();
    emit selectionChanged(m_selStart, m_selEnd);
}

void WaveformWidget::clearSelection()
{
    if (!hasSelection() && m_selStart == 0.0 && m_selEnd == 0.0)
        return;
    m_selStart = 0.0;
    m_selEnd = 0.0;
    update();
    emit selectionChanged(0.0, 0.0);
}

qreal WaveformWidget::posFromX(qreal x) const
{
    const QRect r = rect().adjusted(1, 1, -1, -1);
    if (r.width() <= 0)
        return 0.0;
    return qBound(0.0, (x - r.left()) / r.width(), 1.0);
}

qreal WaveformWidget::xFromPos(qreal pos) const
{
    const QRect r = rect().adjusted(1, 1, -1, -1);
    return r.left() + pos * r.width();
}

WaveformWidget::Hit WaveformWidget::hitTest(qreal x) const
{
    if (!hasSelection())
        return Hit::None;
    const qreal xa = xFromPos(m_selStart);
    const qreal xb = xFromPos(m_selEnd);
    if (qAbs(x - xa) <= kEdgePx)
        return Hit::EdgeA;
    if (qAbs(x - xb) <= kEdgePx)
        return Hit::EdgeB;
    if (x > xa && x < xb)
        return Hit::Interior;
    return Hit::None;
}

void WaveformWidget::setHover(Hit h)
{
    if (m_hover == h)
        return;
    m_hover = h;
    if (h == Hit::EdgeA || h == Hit::EdgeB)
        setCursor(Qt::SizeHorCursor);
    else
        setCursor(Qt::PointingHandCursor);
    update();
}

void WaveformWidget::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && (!m_peaks.isEmpty() || !m_waveDensity.isNull() || !m_spectrogram.isNull())) {
        const qreal x = event->position().x();
        const qreal p = posFromX(x);
        const Hit hit = hitTest(x);
        m_moved = false;
        m_dragAnchor = p;

        if (hit == Hit::EdgeA) {
            m_drag = Drag::EdgeA;
        } else if (hit == Hit::EdgeB) {
            m_drag = Drag::EdgeB;
        } else {
            // New selection only when not near an edge
            m_drag = Drag::NewSelection;
            m_selStart = p;
            m_selEnd = p;
            update();
        }
    }
    QWidget::mousePressEvent(event);
}

void WaveformWidget::mouseMoveEvent(QMouseEvent *event)
{
    const qreal x = event->position().x();
    const qreal p = posFromX(x);

    if (m_drag == Drag::None) {
        if (!m_peaks.isEmpty() || !m_waveDensity.isNull() || !m_spectrogram.isNull())
            setHover(hitTest(x));
        QWidget::mouseMoveEvent(event);
        return;
    }

    if (m_drag == Drag::NewSelection) {
        if (qAbs(p - m_dragAnchor) > 0.005)
            m_moved = true;
        m_selStart = qMin(m_dragAnchor, p);
        m_selEnd = qMax(m_dragAnchor, p);
        update();
        emit selectionChanged(m_selStart, m_selEnd);
    } else if (m_drag == Drag::EdgeA) {
        m_moved = true;
        m_selStart = qBound(0.0, p, m_selEnd - 1e-4);
        update();
        emit selectionChanged(m_selStart, m_selEnd);
    } else if (m_drag == Drag::EdgeB) {
        m_moved = true;
        m_selEnd = qBound(m_selStart + 1e-4, p, 1.0);
        update();
        emit selectionChanged(m_selStart, m_selEnd);
    }
    QWidget::mouseMoveEvent(event);
}

void WaveformWidget::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && m_drag != Drag::None) {
        const Drag kind = m_drag;
        m_drag = Drag::None;
        if (kind == Drag::NewSelection && !m_moved) {
            // Click without drag → seek, keep selection if any
            clearSelection();
            emit seekRequested(posFromX(event->position().x()));
        } else if (kind == Drag::NewSelection || kind == Drag::EdgeA || kind == Drag::EdgeB) {
            if (hasSelection())
                emit selectionChanged(m_selStart, m_selEnd);
            else
                emit selectionChanged(0.0, 0.0);
        }
        setHover(hitTest(event->position().x()));
    }
    QWidget::mouseReleaseEvent(event);
}

void WaveformWidget::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        clearSelection();
    }
    QWidget::mouseDoubleClickEvent(event);
}

void WaveformWidget::leaveEvent(QEvent *event)
{
    setHover(Hit::None);
    QWidget::leaveEvent(event);
}

void WaveformWidget::contextMenuEvent(QContextMenuEvent *event)
{
    emit contextMenuRequested(event->globalPos());
    event->accept();
}

QSize WaveformWidget::sizeHint() const
{
    return QSize(400, 96);
}

QSize WaveformWidget::minimumSizeHint() const
{
    return QSize(200, 64);
}

void WaveformWidget::resizeEvent(QResizeEvent *event)
{
    const int oldW = event->oldSize().width();
    const int newW = event->size().width();
    invalidateBodyCache();
    QWidget::resizeEvent(event);
    if (newW > 0 && newW != oldW)
        emit contentWidthChanged(contentWidth());
}

void WaveformWidget::paintEvent(QPaintEvent *)
{
    ensureBodyCache();
    QPainter p(this);
    if (!m_bodyCache.isNull())
        p.drawPixmap(0, 0, m_bodyCache);
    else
        p.fillRect(rect(), QColor(18, 22, 28));
    const QRect r = rect().adjusted(1, 1, -1, -1);
    paintOverlay(p, r);
}

void WaveformWidget::paintWaveform(QPainter &p, const QRect &r)
{
    // Prefer intensity-graded density image (oscilloscope-style) when available.
    if (!m_waveDensity.isNull()) {
        p.setRenderHint(QPainter::SmoothPixmapTransform, true);
        p.drawImage(r, m_waveDensity);
        // Zero line for orientation
        const qreal mid = r.center().y();
        p.setPen(QPen(QColor(20, 50, 30), 1, Qt::DotLine));
        p.drawLine(r.left(), int(mid), r.right(), int(mid));
        return;
    }

    const int n = m_peaks.size();
    if (n <= 0 || r.width() <= 0)
        return;

    const qreal mid = r.center().y();
    const qreal amp = r.height() / 2.0 - 3.0;

    // Dense peaks: one vertical span per bin (matches pixel columns when bins ≈ width).
    // Cheap and sharp on large maximized windows; AA path only for coarse peak sets.
    if (n >= r.width() / 2 || n >= 800) {
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 160, 70, 140));
        const qreal step = r.width() / static_cast<qreal>(n);
        for (int i = 0; i < n; ++i) {
            const qreal v = qBound(0.0, qreal(m_peaks.at(i)), 1.0);
            if (v <= 1e-4)
                continue;
            const qreal x = r.left() + i * step;
            const qreal h = amp * v;
            const int w = qMax(1, int(step + 0.5));
            p.drawRect(QRectF(x, mid - h, w, h * 2.0));
        }
        p.setPen(QPen(QColor(20, 50, 30), 1, Qt::DotLine));
        p.drawLine(r.left(), int(mid), r.right(), int(mid));
        return;
    }

    const qreal step = r.width() / static_cast<qreal>(qMax(1, n));
    QPainterPath path;
    path.moveTo(r.left(), mid);
    for (int i = 0; i < n; ++i) {
        const qreal v = qBound(0.0, qreal(m_peaks.at(i)), 1.0);
        const qreal x = r.left() + (i + 0.5) * step;
        path.lineTo(x, mid - amp * v);
    }
    for (int i = n - 1; i >= 0; --i) {
        const qreal v = qBound(0.0, qreal(m_peaks.at(i)), 1.0);
        const qreal x = r.left() + (i + 0.5) * step;
        path.lineTo(x, mid + amp * v);
    }
    path.closeSubpath();

    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0, 160, 70, 120));
    p.drawPath(path);
    p.setPen(QPen(QColor(80, 230, 120), 1.2));
    p.setBrush(Qt::NoBrush);
    p.drawPath(path);

    p.setPen(QPen(QColor(20, 50, 30), 1, Qt::DotLine));
    p.drawLine(r.left(), int(mid), r.right(), int(mid));
}

void WaveformWidget::paintSpectrogram(QPainter &p, const QRect &r)
{
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    p.drawImage(r, m_spectrogram);
}

void WaveformWidget::paintOverlay(QPainter &p, const QRect &r)
{
    if (hasSelection()) {
        const int x0 = int(xFromPos(m_selStart));
        const int x1 = int(xFromPos(m_selEnd));
        p.fillRect(QRect(x0, r.top(), qMax(1, x1 - x0), r.height()),
                    QColor(180, 160, 40, 70));
    }

    if (hasSelection()) {
        auto drawEdge = [&](qreal pos, bool hot) {
            const qreal x = xFromPos(pos);
            p.setPen(QPen(hot ? QColor(255, 220, 60) : QColor(220, 200, 50), hot ? 3.0 : 2.0));
            p.drawLine(QPointF(x, r.top()), QPointF(x, r.bottom()));
            p.setBrush(hot ? QColor(255, 220, 60) : QColor(220, 200, 50));
            p.setPen(Qt::NoPen);
            const qreal gy = r.center().y();
            p.drawRect(QRectF(x - 3, gy - 10, 6, 20));
        };
        const bool hotA = (m_hover == Hit::EdgeA || m_drag == Drag::EdgeA);
        const bool hotB = (m_hover == Hit::EdgeB || m_drag == Drag::EdgeB);
        drawEdge(m_selStart, hotA);
        drawEdge(m_selEnd, hotB);
        p.setPen(QColor(240, 220, 80));
        p.drawText(QPointF(xFromPos(m_selStart) + 4, r.top() + 14), QStringLiteral("A"));
        p.drawText(QPointF(xFromPos(m_selEnd) - 12, r.top() + 14), QStringLiteral("B"));
    }

    if (m_playbackPos >= 0.0) {
        const qreal x = xFromPos(m_playbackPos);
        p.setPen(QPen(QColor(230, 230, 230), 1.5));
        p.drawLine(QPointF(x, r.top()), QPointF(x, r.bottom()));
    }
}

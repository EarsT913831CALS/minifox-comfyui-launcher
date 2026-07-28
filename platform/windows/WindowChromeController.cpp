#include "WindowChromeController.h"

#include <QGuiApplication>
#include <QScreen>
#include <QWindow>

#ifdef Q_OS_WIN
#include <dwmapi.h>
#include <qt_windows.h>
#endif

WindowChromeController::WindowChromeController(QObject *parent)
    : QObject(parent)
{
    if (QCoreApplication::instance()) {
        QCoreApplication::instance()->installNativeEventFilter(this);
    }
}

WindowChromeController::~WindowChromeController()
{
    if (QCoreApplication::instance()) {
        QCoreApplication::instance()->removeNativeEventFilter(this);
    }
}

void WindowChromeController::attach(QObject *windowObject)
{
    QWindow *window = qobject_cast<QWindow *>(windowObject);
    if (!window || window == m_window) {
        return;
    }
    m_window = window;
    m_hwnd = static_cast<quintptr>(window->winId());
    applyNativeStyle();
    initializeGeometry();
}

void WindowChromeController::showSystemMenu(int globalX, int globalY)
{
#ifdef Q_OS_WIN
    if (!m_hwnd) {
        return;
    }
    const HWND hwnd = reinterpret_cast<HWND>(m_hwnd);
    HMENU menu = GetSystemMenu(hwnd, FALSE);
    if (!menu) {
        return;
    }
    const bool maximized = m_maximized;
    EnableMenuItem(menu, SC_RESTORE, MF_BYCOMMAND | (maximized ? MF_ENABLED : MF_GRAYED));
    EnableMenuItem(menu, SC_MAXIMIZE, MF_BYCOMMAND | (maximized ? MF_GRAYED : MF_ENABLED));
    const UINT command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON,
                                        globalX, globalY, 0, hwnd, nullptr);
    if (command == SC_MAXIMIZE || command == SC_RESTORE) {
        toggleMaximized();
    } else if (command) {
        PostMessageW(hwnd, WM_SYSCOMMAND, command, 0);
    }
#else
    Q_UNUSED(globalX)
    Q_UNUSED(globalY)
#endif
}

void WindowChromeController::toggleMaximized()
{
    if (!m_window || !m_window->screen()) {
        return;
    }
    if (m_maximized) {
        const QRect restore = m_restoreGeometry;
        setMaximized(false);
        if (restore.isValid()) {
            m_window->setGeometry(restore);
        } else {
            initializeGeometry();
        }
        return;
    }

    m_restoreGeometry = m_window->geometry();
    setMaximized(true);
    applyAspectMaximizedGeometry();
}

qreal WindowChromeController::aspectRatio() const
{
    return m_aspectRatio;
}

void WindowChromeController::setAspectRatio(qreal ratio)
{
    ratio = qBound(1.2, ratio, 2.4);
    if (qFuzzyCompare(ratio, m_aspectRatio)) {
        return;
    }
    m_aspectRatio = ratio;
    emit aspectRatioChanged();

    if (!m_window || !m_window->screen()) {
        return;
    }
    if (m_restoreGeometry.isValid()) {
        // Re-fit the saved normal geometry to the new aspect. Restoring a
        // stale rect (restore button or caption drag) would otherwise bring
        // back the old ratio and leave the window out of sync with the
        // design surface.
        const QRect available = m_window->screen()->availableGeometry();
        int restoreWidth = m_restoreGeometry.width();
        int restoreHeight = qRound(restoreWidth / m_aspectRatio);
        if (restoreHeight > available.height()) {
            restoreHeight = available.height();
            restoreWidth = qRound(restoreHeight * m_aspectRatio);
        }
        if (restoreWidth > available.width()) {
            restoreWidth = available.width();
            restoreHeight = qRound(restoreWidth / m_aspectRatio);
        }
        const QPoint center = m_restoreGeometry.center();
        const int rx = qBound(available.left(),
                              center.x() - restoreWidth / 2,
                              available.right() - restoreWidth + 1);
        const int ry = qBound(available.top(),
                              center.y() - restoreHeight / 2,
                              available.bottom() - restoreHeight + 1);
        m_restoreGeometry = QRect(rx, ry, restoreWidth, restoreHeight);
    }
    if (m_maximized) {
        applyAspectMaximizedGeometry();
        return;
    }
    if (m_window->visibility() == QWindow::FullScreen) {
        return;
    }
    const QRect available = m_window->screen()->availableGeometry();
    const QPoint center = m_window->geometry().center();
    int width = m_window->width();
    int height = qRound(width / m_aspectRatio);
    if (height > available.height()) {
        height = available.height();
        width = qRound(height * m_aspectRatio);
    }
    const int x = qBound(available.left(),
                         center.x() - width / 2,
                         available.right() - width + 1);
    const int y = qBound(available.top(),
                         center.y() - height / 2,
                         available.bottom() - height + 1);
    m_window->setGeometry(x, y, width, height);
}

bool WindowChromeController::maximized() const
{
    return m_maximized;
}

bool WindowChromeController::nativeEventFilter(const QByteArray &eventType,
                                               void *message,
                                               qintptr *result)
{
    Q_UNUSED(eventType)
#ifdef Q_OS_WIN
    MSG *nativeMessage = static_cast<MSG *>(message);
    if (!nativeMessage || reinterpret_cast<quintptr>(nativeMessage->hwnd) != m_hwnd || !m_window) {
        return false;
    }

    const HWND hwnd = nativeMessage->hwnd;
    switch (nativeMessage->message) {
    case WM_NCCALCSIZE:
        if (nativeMessage->wParam) {
            *result = 0;
            return true;
        }
        break;
    case WM_NCHITTEST: {
        RECT rect{};
        GetWindowRect(hwnd, &rect);
        const int x = static_cast<short>(LOWORD(nativeMessage->lParam));
        const int y = static_cast<short>(HIWORD(nativeMessage->lParam));
        const bool maximized = m_maximized || IsZoomed(hwnd);
        const HitRegion region = hitTest(
            QRect(rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top),
            QPoint(x, y), m_window->devicePixelRatio(), maximized, m_aspectRatio);
        switch (region) {
        case HitRegion::Caption: *result = HTCAPTION; return true;
        case HitRegion::MaximizeButton: *result = HTCLIENT; return true;
        case HitRegion::Left: *result = HTLEFT; return true;
        case HitRegion::Right: *result = HTRIGHT; return true;
        case HitRegion::Top: *result = HTTOP; return true;
        case HitRegion::Bottom: *result = HTBOTTOM; return true;
        case HitRegion::TopLeft: *result = HTTOPLEFT; return true;
        case HitRegion::TopRight: *result = HTTOPRIGHT; return true;
        case HitRegion::BottomLeft: *result = HTBOTTOMLEFT; return true;
        case HitRegion::BottomRight: *result = HTBOTTOMRIGHT; return true;
        case HitRegion::Client: break;
        }
        break;
    }
    case WM_NCLBUTTONDOWN:
        if (nativeMessage->wParam == HTCAPTION && m_maximized) {
            // Swallow the press and track the drag ourselves. Passing it to
            // DefWindowProc would start the system move loop with the
            // maximized rect, leaving no chance to restore under the cursor.
            POINT anchor{};
            GetCursorPos(&anchor);
            m_captionDragAnchor = QPoint(anchor.x, anchor.y);
            m_pendingCaptionDrag = true;
            m_captionDragging = false;
            SetCapture(hwnd);
            // Default processing would also activate the window; do it
            // explicitly since the press is consumed here.
            m_window->requestActivate();
            // Mouse messages may be posted; Qt passes result == nullptr
            // for those and they cannot carry a return value.
            if (result) {
                *result = 0;
            }
            return true;
        }
        break;
    case WM_MOUSEMOVE:
        if (m_pendingCaptionDrag && m_window) {
            POINT pt{};
            GetCursorPos(&pt);
            if (!m_captionDragging) {
                const int dragX = GetSystemMetrics(SM_CXDRAG);
                const int dragY = GetSystemMetrics(SM_CYDRAG);
                if (qAbs(pt.x - m_captionDragAnchor.x()) <= dragX
                    && qAbs(pt.y - m_captionDragAnchor.y()) <= dragY) {
                    if (result) {
                        *result = 0;
                        return true;
                    }
                    return false;
                }
                // The press turned into a real drag: restore the window
                // under the cursor, then keep following the pointer with
                // the restored geometry.
                const QRect restore = m_restoreGeometry;
                setMaximized(false);
                const qreal dpr = m_window->devicePixelRatio();
                const QPointF cursorLogical(pt.x / dpr, pt.y / dpr);
                if (restore.isValid() && m_window->screen()) {
                    const QRect target = restoreRectUnderCursor(
                        restore, m_window->geometry(), cursorLogical,
                        m_window->screen()->availableGeometry());
                    m_window->setGeometry(target);
                    m_captionGrabOffset = cursorLogical - target.topLeft();
                } else {
                    m_captionGrabOffset = QPointF();
                }
                m_captionDragging = true;
            }
            if (!m_captionGrabOffset.isNull()) {
                const qreal dpr = m_window->devicePixelRatio();
                const QPointF cursorLogical(pt.x / dpr, pt.y / dpr);
                const QPointF topLeft = cursorLogical - m_captionGrabOffset;
                m_window->setPosition(qRound(topLeft.x()), qRound(topLeft.y()));
            }
            if (result) {
                *result = 0;
                return true;
            }
        }
        break;
    case WM_LBUTTONUP:
        if (m_pendingCaptionDrag || m_captionDragging) {
            endCaptionDrag();
            ReleaseCapture();
            if (result) {
                *result = 0;
                return true;
            }
        }
        break;
    case WM_CAPTURECHANGED:
        endCaptionDrag();
        break;
    case WM_NCLBUTTONDBLCLK:
        if (nativeMessage->wParam == HTCAPTION) {
            QMetaObject::invokeMethod(this, &WindowChromeController::toggleMaximized,
                                      Qt::QueuedConnection);
            if (result) {
                *result = 0;
            }
            return true;
        }
        break;
    case WM_SYSCOMMAND: {
        const UINT command = static_cast<UINT>(nativeMessage->wParam) & 0xFFF0;
        const bool minimized = m_window->visibility() == QWindow::Minimized;
        if (command == SC_MAXIMIZE) {
            QMetaObject::invokeMethod(this, [this] {
                if (!m_window) {
                    return;
                }
                if (m_window->visibility() == QWindow::Minimized) {
                    m_window->showNormal();
                }
                if (!m_maximized) {
                    toggleMaximized();
                }
            }, Qt::QueuedConnection);
            if (result) {
                *result = 0;
            }
            return true;
        }
        if (command == SC_RESTORE && !minimized && m_maximized) {
            QMetaObject::invokeMethod(this, &WindowChromeController::toggleMaximized,
                                      Qt::QueuedConnection);
            if (result) {
                *result = 0;
            }
            return true;
        }
        // A restore requested while minimized (clicking the taskbar button
        // posts exactly this) must reach DefWindowProc so Windows actually
        // un-minimizes the window. Swallowing it left the window stuck.
        break;
    }
    case WM_ENTERSIZEMOVE:
        if (m_maximized) {
            // Reached only for keyboard-initiated moves (mouse drags are
            // handled above before the system move loop starts). Keep the
            // saved geometry so a later restore still returns to it.
            setMaximized(false);
        }
        break;
    case WM_WINDOWPOSCHANGING: {
        if (m_maximized) {
            break;
        }
        auto *pos = reinterpret_cast<WINDOWPOS *>(nativeMessage->lParam);
        if (!pos || (pos->flags & SWP_NOSIZE) != 0 || pos->cx <= 0 || pos->cy <= 0) {
            break;
        }
        RECT current{};
        GetWindowRect(hwnd, &current);
        if (pos->cx == current.right - current.left
            && pos->cy == current.bottom - current.top) {
            break; // a pure move, nothing to constrain
        }
        // External resizes (aero snap, DPI changes, Qt-internal clamps)
        // never pass through WM_SIZING. Enforce the design aspect here so
        // no code path can leave the window at a free-form ratio.
        const QSize enforced = enforceAspectSize(QSize(pos->cx, pos->cy),
                                                 m_aspectRatio);
        pos->cx = enforced.width();
        pos->cy = enforced.height();
        break; // modified in place; default processing continues
    }
    case WM_SIZE:
        if (nativeMessage->wParam == SIZE_MAXIMIZED) {
            // A native zoom (snap-to-top, tablet shake, ...) bypasses the
            // custom maximize. Convert it back to the aspect-aware one.
            QMetaObject::invokeMethod(this, [this] {
                if (!m_window) {
                    return;
                }
                if (m_window->visibility() == QWindow::Maximized) {
                    m_window->showNormal();
                }
                setMaximized(true);
                applyAspectMaximizedGeometry();
            }, Qt::QueuedConnection);
        }
        break;
    case WM_SIZING: {
        auto *nativeRect = reinterpret_cast<RECT *>(nativeMessage->lParam);
        HitRegion edge = HitRegion::Client;
        switch (nativeMessage->wParam) {
        case WMSZ_LEFT: edge = HitRegion::Left; break;
        case WMSZ_RIGHT: edge = HitRegion::Right; break;
        case WMSZ_TOP: edge = HitRegion::Top; break;
        case WMSZ_BOTTOM: edge = HitRegion::Bottom; break;
        case WMSZ_TOPLEFT: edge = HitRegion::TopLeft; break;
        case WMSZ_TOPRIGHT: edge = HitRegion::TopRight; break;
        case WMSZ_BOTTOMLEFT: edge = HitRegion::BottomLeft; break;
        case WMSZ_BOTTOMRIGHT: edge = HitRegion::BottomRight; break;
        default: break;
        }
        if (edge != HitRegion::Client) {
            const QRect constrained = constrainResizeRect(
                QRect(nativeRect->left, nativeRect->top,
                      nativeRect->right - nativeRect->left,
                      nativeRect->bottom - nativeRect->top),
                edge, m_aspectRatio);
            nativeRect->left = constrained.x();
            nativeRect->top = constrained.y();
            nativeRect->right = constrained.x() + constrained.width();
            nativeRect->bottom = constrained.y() + constrained.height();
            *result = TRUE;
            return true;
        }
        break;
    }
    case WM_GETMINMAXINFO: {
        auto *info = reinterpret_cast<MINMAXINFO *>(nativeMessage->lParam);
        const HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
        MONITORINFO monitorInfo{sizeof(MONITORINFO)};
        if (GetMonitorInfoW(monitor, &monitorInfo)) {
            const RECT work = monitorInfo.rcWork;
            const RECT area = monitorInfo.rcMonitor;
            info->ptMaxPosition.x = work.left - area.left;
            info->ptMaxPosition.y = work.top - area.top;
            info->ptMaxSize.x = work.right - work.left;
            info->ptMaxSize.y = work.bottom - work.top;
        }
        return false;
    }
    default:
        break;
    }
#else
    Q_UNUSED(message)
    Q_UNUSED(result)
#endif
    return false;
}

WindowChromeController::HitRegion WindowChromeController::hitTest(
    const QRect &windowRect,
    const QPoint &globalPoint,
    qreal scale,
    bool maximized,
    qreal contentAspectRatio)
{
    const int border = qRound(12 * scale);
    const int x = globalPoint.x();
    const int y = globalPoint.y();

    if (!maximized) {
        const bool left = x >= windowRect.left() && x < windowRect.left() + border;
        const bool right = x <= windowRect.right() && x > windowRect.right() - border;
        const bool top = y >= windowRect.top() && y < windowRect.top() + border;
        const bool bottom = y <= windowRect.bottom() && y > windowRect.bottom() - border;
        if (top && left) return HitRegion::TopLeft;
        if (top && right) return HitRegion::TopRight;
        if (bottom && left) return HitRegion::BottomLeft;
        if (bottom && right) return HitRegion::BottomRight;
        // Fixed-aspect resizing is available from the four corners only.
        // Side handles visually promise one-axis resizing, which this window
        // intentionally does not support.
    }

    constexpr qreal designWidth = 1440.0;
    const qreal designHeight = designWidth / qBound(1.2, contentAspectRatio, 2.4);
    const qreal contentScale = qMin(windowRect.width() / (designWidth * scale),
                                    windowRect.height() / (designHeight * scale));
    const int contentWidth = qRound(designWidth * scale * contentScale);
    const int contentHeight = qRound(designHeight * scale * contentScale);
    const QRect contentRect(windowRect.x() + (windowRect.width() - contentWidth) / 2,
                            windowRect.y() + (windowRect.height() - contentHeight) / 2,
                            contentWidth, contentHeight);
    const int titleHeight = qRound(48 * scale * contentScale);
    const int buttonWidth = qRound(46 * scale * contentScale);

    if (y >= contentRect.top() && y < contentRect.top() + titleHeight) {
        const int fromRight = contentRect.right() + 1 - x;
        if (fromRight > buttonWidth && fromRight <= buttonWidth * 2) {
            // Keep the maximize region in the QML client area. Calling
            // showMaximized while processing HTMAXBUTTON non-client messages
            // caused a repeatable access violation in the static Qt build.
            return HitRegion::Client;
        }
        if (fromRight > buttonWidth * 3) {
            return HitRegion::Caption;
        }
    }
    return HitRegion::Client;
}

QRect WindowChromeController::restoreRectUnderCursor(const QRect &restoreGeometry,
                                                     const QRect &currentGeometry,
                                                     const QPointF &cursorPoint,
                                                     const QRect &availableRect)
{
    if (!restoreGeometry.isValid()) {
        return {};
    }
    // Keep the cursor at the same relative horizontal position it had on
    // the maximized caption, so the restored window follows the pointer
    // the way native maximized windows do.
    qreal relativeX = 0.5;
    if (currentGeometry.width() > 0) {
        relativeX = qBound(0.0,
                           (cursorPoint.x() - currentGeometry.x())
                               / static_cast<qreal>(currentGeometry.width()),
                           1.0);
    }
    const qreal grabY = qBound(0.0,
                               cursorPoint.y() - currentGeometry.y(),
                               static_cast<qreal>(qMax(0, currentGeometry.height())));
    int x = qRound(cursorPoint.x() - relativeX * restoreGeometry.width());
    int y = qRound(cursorPoint.y() - grabY);
    if (availableRect.isValid()) {
        if (restoreGeometry.width() <= availableRect.width()) {
            x = qBound(availableRect.left(), x,
                       availableRect.right() - restoreGeometry.width() + 1);
        } else {
            x = availableRect.left();
        }
        if (restoreGeometry.height() <= availableRect.height()) {
            y = qBound(availableRect.top(), y,
                       availableRect.bottom() - restoreGeometry.height() + 1);
        } else {
            y = availableRect.top();
        }
    }
    return QRect(x, y, restoreGeometry.width(), restoreGeometry.height());
}

QSize WindowChromeController::enforceAspectSize(const QSize &proposed,
                                                qreal aspectRatio)
{
    aspectRatio = qBound(1.2, aspectRatio, 2.4);
    const int width = qMax(1, proposed.width());
    const int height = qMax(1, proposed.height());
    if (qAbs(static_cast<qreal>(width) / height - aspectRatio) < 0.01) {
        return QSize(width, height);
    }
    // Width-driven, matching the corner-resize correction.
    return QSize(width, qMax(1, qRound(width / aspectRatio)));
}

QRect WindowChromeController::constrainResizeRect(const QRect &proposedRect,
                                                  HitRegion resizeEdge,
                                                  qreal aspectRatio)
{
    aspectRatio = qBound(1.2, aspectRatio, 2.4);
    int x = proposedRect.x();
    int y = proposedRect.y();
    const int width = qMax(1, proposedRect.width());
    const int height = qMax(1, proposedRect.height());

    const bool fromLeft = resizeEdge == HitRegion::Left
                          || resizeEdge == HitRegion::TopLeft
                          || resizeEdge == HitRegion::BottomLeft;
    const bool fromRight = resizeEdge == HitRegion::Right
                           || resizeEdge == HitRegion::TopRight
                           || resizeEdge == HitRegion::BottomRight;
    const bool fromTop = resizeEdge == HitRegion::Top
                         || resizeEdge == HitRegion::TopLeft
                         || resizeEdge == HitRegion::TopRight;
    const bool fromBottom = resizeEdge == HitRegion::Bottom
                            || resizeEdge == HitRegion::BottomLeft
                            || resizeEdge == HitRegion::BottomRight;

    int targetWidth = width;
    int targetHeight = height;
    if ((fromLeft || fromRight) && !fromTop && !fromBottom) {
        targetHeight = qRound(width / aspectRatio);
    } else if ((fromTop || fromBottom) && !fromLeft && !fromRight) {
        targetWidth = qRound(height * aspectRatio);
    } else {
        // A corner drag is always width-driven. Switching between width- and
        // height-driven correction on adjacent mouse events makes the frame
        // oscillate and forces redundant Qt Quick relayouts.
        targetHeight = qRound(width / aspectRatio);
    }

    if (fromLeft) {
        x += width - targetWidth;
    } else if (!fromRight) {
        x += (width - targetWidth) / 2;
    }
    if (fromTop) {
        y += height - targetHeight;
    } else if (!fromBottom) {
        y += (height - targetHeight) / 2;
    }
    return QRect(x, y, targetWidth, targetHeight);
}

QRect WindowChromeController::largestAspectRect(const QRect &availableRect,
                                                qreal aspectRatio)
{
    if (!availableRect.isValid()) {
        return {};
    }
    aspectRatio = qBound(1.2, aspectRatio, 2.4);
    int width = availableRect.width();
    int height = qRound(width / aspectRatio);
    if (height > availableRect.height()) {
        height = availableRect.height();
        width = qRound(height * aspectRatio);
    }
    return QRect(availableRect.x() + (availableRect.width() - width) / 2,
                 availableRect.y() + (availableRect.height() - height) / 2,
                 width, height);
}

void WindowChromeController::applyNativeStyle()
{
#ifdef Q_OS_WIN
    if (!m_hwnd) {
        return;
    }
    const HWND hwnd = reinterpret_cast<HWND>(m_hwnd);
    LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
    style |= WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SYSMENU;
    SetWindowLongPtrW(hwnd, GWL_STYLE, style);

    const BOOL enabled = TRUE;
    DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &enabled, sizeof(enabled));
    const DWM_WINDOW_CORNER_PREFERENCE corners = DWMWCP_ROUND;
    DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &corners, sizeof(corners));
    SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                 SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
#endif
}

void WindowChromeController::initializeGeometry()
{
    if (!m_window || !m_window->screen()) {
        return;
    }
    const QRect available = m_window->screen()->availableGeometry();
    constexpr int designWidth = 1440;
    const int designHeight = qRound(designWidth / m_aspectRatio);
    constexpr int baseMinimumWidth = 1024;
    constexpr int baseMinimumHeight = 700;
    const int minimumWidth = qMax(baseMinimumWidth,
                                  qRound(baseMinimumHeight * m_aspectRatio));
    const int minimumHeight = qMax(baseMinimumHeight,
                                   qRound(minimumWidth / m_aspectRatio));
    const qreal minimumScale = qMax(static_cast<qreal>(minimumWidth) / designWidth,
                                    static_cast<qreal>(minimumHeight) / designHeight);
    const qreal maximumScale = qMin(static_cast<qreal>(available.width()) / designWidth,
                                    static_cast<qreal>(available.height()) / designHeight);
    if (maximumScale < minimumScale) {
        m_window->setGeometry(largestAspectRect(available, m_aspectRatio));
        return;
    }
    const qreal preferredScale = (minimumScale + maximumScale) / 2.0;
    const int preferredWidth = qRound(designWidth * preferredScale);
    const int preferredHeight = qRound(designHeight * preferredScale);
    m_window->resize(preferredWidth, preferredHeight);
    m_window->setPosition(
        available.x() + (available.width() - preferredWidth) / 2,
        available.y() + (available.height() - preferredHeight) / 2);
}

void WindowChromeController::setMaximized(bool maximized)
{
    if (m_maximized == maximized) {
        return;
    }
    m_maximized = maximized;
    emit maximizedChanged();
}

void WindowChromeController::applyAspectMaximizedGeometry()
{
    if (!m_window || !m_window->screen()) {
        return;
    }
    m_window->setGeometry(
        largestAspectRect(m_window->screen()->availableGeometry(), m_aspectRatio));
}

void WindowChromeController::endCaptionDrag()
{
    m_pendingCaptionDrag = false;
    m_captionDragging = false;
    m_captionGrabOffset = QPointF();
}

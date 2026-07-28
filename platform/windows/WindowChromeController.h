#pragma once

#include <QAbstractNativeEventFilter>
#include <QObject>
#include <QPointer>
#include <QPoint>
#include <QRect>

class QWindow;

class WindowChromeController final : public QObject, public QAbstractNativeEventFilter
{
    Q_OBJECT
    Q_PROPERTY(qreal aspectRatio READ aspectRatio WRITE setAspectRatio NOTIFY aspectRatioChanged)
    Q_PROPERTY(bool maximized READ maximized NOTIFY maximizedChanged)

public:
    enum class HitRegion {
        Client,
        Caption,
        MaximizeButton,
        Left,
        Right,
        Top,
        Bottom,
        TopLeft,
        TopRight,
        BottomLeft,
        BottomRight
    };

    explicit WindowChromeController(QObject *parent = nullptr);
    ~WindowChromeController() override;

    Q_INVOKABLE void attach(QObject *windowObject);
    Q_INVOKABLE void showSystemMenu(int globalX, int globalY);
    Q_INVOKABLE void toggleMaximized();
    qreal aspectRatio() const;
    void setAspectRatio(qreal ratio);
    bool maximized() const;

    static HitRegion hitTest(const QRect &windowRect,
                             const QPoint &globalPoint,
                             qreal scale,
                             bool maximized,
                             qreal contentAspectRatio = 1.6);
    static QRect restoreRectUnderCursor(const QRect &restoreGeometry,
                                        const QRect &currentGeometry,
                                        const QPointF &cursorPoint,
                                        const QRect &availableRect);
    static QSize enforceAspectSize(const QSize &proposed,
                                   qreal aspectRatio = 1.6);
    static QRect constrainResizeRect(const QRect &proposedRect,
                                     HitRegion resizeEdge,
                                     qreal aspectRatio = 1.6);
    static QRect largestAspectRect(const QRect &availableRect,
                                   qreal aspectRatio = 1.6);
    bool nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result) override;

signals:
    void aspectRatioChanged();
    void maximizedChanged();

private:
    void applyNativeStyle();
    void initializeGeometry();
    void setMaximized(bool maximized);
    void applyAspectMaximizedGeometry();
    void endCaptionDrag();

    QPointer<QWindow> m_window;
    quintptr m_hwnd = 0;
    qreal m_aspectRatio = 1.6;
    bool m_maximized = false;
    QRect m_restoreGeometry;
    // Caption drag tracking used to restore a maximized window once the
    // user actually starts dragging it, matching the behaviour of native
    // maximized windows (Chrome, Explorer, ...).
    bool m_pendingCaptionDrag = false;
    bool m_captionDragging = false;
    QPoint m_captionDragAnchor;
    QPointF m_captionGrabOffset;
};

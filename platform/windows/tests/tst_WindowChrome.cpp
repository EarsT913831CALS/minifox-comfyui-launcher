#include "WindowChromeController.h"

#include <QTest>

class WindowChromeTest final : public QObject
{
    Q_OBJECT

private slots:
    void hitTestScales_data();
    void hitTestScales();
    void maximizedWindowHasNoResizeBorder();
    void maximizeButtonStaysInClientArea();
    void resizeKeepsDesignAspect_data();
    void resizeKeepsDesignAspect();
    void supportedAspectRatios();
    void cornerResizeUsesStableWidthDriver();
    void sideEdgesDoNotOfferOneAxisResize();
    void largestAspectRectFitsAndCenters_data();
    void largestAspectRectFitsAndCenters();
    void restoreRectFollowsCursor();
    void enforceAspectSizeKeepsAndFixesRatio();
};

void WindowChromeTest::hitTestScales_data()
{
    QTest::addColumn<double>("scale");
    QTest::newRow("100-percent") << 1.0;
    QTest::newRow("150-percent") << 1.5;
    QTest::newRow("200-percent") << 2.0;
}

void WindowChromeTest::hitTestScales()
{
    QFETCH(double, scale);
    const QRect window(100, 100, qRound(1440 * scale), qRound(900 * scale));
    QCOMPARE(WindowChromeController::hitTest(window, window.topLeft(), scale, false),
             WindowChromeController::HitRegion::TopLeft);
    QCOMPARE(WindowChromeController::hitTest(
                 window, QPoint(window.center().x(), window.top() + qRound(24 * scale)),
                 scale, false),
             WindowChromeController::HitRegion::Caption);
    QCOMPARE(WindowChromeController::hitTest(
                 window, QPoint(window.right() - qRound(69 * scale),
                                window.top() + qRound(24 * scale)),
                 scale, false),
             WindowChromeController::HitRegion::Client);
    QCOMPARE(WindowChromeController::hitTest(
                 window, QPoint(window.right() - qRound(23 * scale),
                                window.top() + qRound(24 * scale)),
                 scale, false),
             WindowChromeController::HitRegion::Client);
}

void WindowChromeTest::maximizedWindowHasNoResizeBorder()
{
    const QRect window(0, 0, 1920, 1080);
    QCOMPARE(WindowChromeController::hitTest(window, QPoint(0, 500), 1.0, true),
             WindowChromeController::HitRegion::Client);
}

void WindowChromeTest::maximizeButtonStaysInClientArea()
{
    const QRect window(0, 0, 1920, 1040);
    const int contentWidth = qRound(1440.0 * (1040.0 / 900.0));
    const int contentRight = (window.width() + contentWidth) / 2 - 1;
    QCOMPARE(WindowChromeController::hitTest(
                 window, QPoint(contentRight - 69, 24), 1.0, true),
             WindowChromeController::HitRegion::Client);
    QCOMPARE(WindowChromeController::hitTest(
                 window, QPoint(window.right() - 69, 24), 1.0, true),
             WindowChromeController::HitRegion::Client);
}

void WindowChromeTest::resizeKeepsDesignAspect_data()
{
    QTest::addColumn<int>("edge");
    QTest::newRow("left") << static_cast<int>(WindowChromeController::HitRegion::Left);
    QTest::newRow("right") << static_cast<int>(WindowChromeController::HitRegion::Right);
    QTest::newRow("top") << static_cast<int>(WindowChromeController::HitRegion::Top);
    QTest::newRow("bottom") << static_cast<int>(WindowChromeController::HitRegion::Bottom);
    QTest::newRow("top-left") << static_cast<int>(WindowChromeController::HitRegion::TopLeft);
    QTest::newRow("top-right") << static_cast<int>(WindowChromeController::HitRegion::TopRight);
    QTest::newRow("bottom-left") << static_cast<int>(WindowChromeController::HitRegion::BottomLeft);
    QTest::newRow("bottom-right") << static_cast<int>(WindowChromeController::HitRegion::BottomRight);
}

void WindowChromeTest::resizeKeepsDesignAspect()
{
    QFETCH(int, edge);
    const QRect constrained = WindowChromeController::constrainResizeRect(
        QRect(100, 100, 1377, 823),
        static_cast<WindowChromeController::HitRegion>(edge));
    const qreal ratio = static_cast<qreal>(constrained.width()) / constrained.height();
    QVERIFY2(qAbs(ratio - 1.6) < 0.002, "The resized window must remain 16:10");
}

void WindowChromeTest::supportedAspectRatios()
{
    const QList<qreal> ratios {16.0 / 10.0, 16.0 / 9.0, 3.0 / 2.0, 4.0 / 3.0};
    for (const qreal expected : ratios) {
        const QRect constrained = WindowChromeController::constrainResizeRect(
            QRect(100, 100, 1377, 823),
            WindowChromeController::HitRegion::BottomRight,
            expected);
        const qreal actual =
            static_cast<qreal>(constrained.width()) / constrained.height();
        QVERIFY2(qAbs(actual - expected) < 0.002,
                 "The resize result must use the selected aspect ratio");
    }
}

void WindowChromeTest::cornerResizeUsesStableWidthDriver()
{
    const QRect proposed(100, 100, 1377, 823);
    const QRect constrained = WindowChromeController::constrainResizeRect(
        proposed, WindowChromeController::HitRegion::BottomRight, 1.6);
    QCOMPARE(constrained.width(), proposed.width());
    QCOMPARE(constrained.height(), qRound(proposed.width() / 1.6));
}

void WindowChromeTest::sideEdgesDoNotOfferOneAxisResize()
{
    const QRect window(100, 100, 1440, 900);
    QCOMPARE(WindowChromeController::hitTest(
                 window, QPoint(window.left(), window.center().y()), 1.0, false),
             WindowChromeController::HitRegion::Client);
    QCOMPARE(WindowChromeController::hitTest(
                 window, QPoint(window.right(), window.center().y()), 1.0, false),
             WindowChromeController::HitRegion::Client);
}

void WindowChromeTest::largestAspectRectFitsAndCenters_data()
{
    QTest::addColumn<QRect>("available");
    QTest::addColumn<double>("ratio");
    QTest::newRow("wide-work-area") << QRect(0, 0, 1920, 1040) << 1.6;
    QTest::newRow("tall-work-area") << QRect(100, 50, 1280, 1024) << (16.0 / 9.0);
    QTest::newRow("four-three") << QRect(-1920, 0, 1920, 1080) << (4.0 / 3.0);
}

void WindowChromeTest::largestAspectRectFitsAndCenters()
{
    QFETCH(QRect, available);
    QFETCH(double, ratio);
    const QRect result =
        WindowChromeController::largestAspectRect(available, ratio);
    QVERIFY(available.contains(result));
    QVERIFY(qAbs(static_cast<double>(result.width()) / result.height() - ratio) < 0.002);
    QVERIFY(qAbs(result.center().x() - available.center().x()) <= 1);
    QVERIFY(qAbs(result.center().y() - available.center().y()) <= 1);
}

void WindowChromeTest::restoreRectFollowsCursor()
{
    const QRect available(0, 0, 1920, 1080);
    const QRect maximized(120, 40, 1680, 1000);
    const QRect restore(200, 150, 1440, 900);

    // A cursor in the middle of the caption keeps its relative position.
    const QRect centered = WindowChromeController::restoreRectUnderCursor(
        restore, maximized, QPointF(960, 60), available);
    QCOMPARE(centered.size(), restore.size());
    QVERIFY(centered.contains(QPoint(960, 60)));

    // A cursor near the left edge keeps its relative position too.
    const QRect left = WindowChromeController::restoreRectUnderCursor(
        restore, maximized, QPointF(220, 60), available);
    QCOMPARE(left.size(), restore.size());
    QVERIFY(left.contains(QPoint(220, 60)));

    // A restore rect that would leave the work area is clamped inside it.
    const QRect clamped = WindowChromeController::restoreRectUnderCursor(
        restore, maximized, QPointF(1900, 60), available);
    QVERIFY(available.contains(clamped));

    // Without a valid restore geometry there is nothing to restore to.
    QCOMPARE(WindowChromeController::restoreRectUnderCursor(
                 QRect(), maximized, QPointF(960, 60), available),
             QRect());
}

void WindowChromeTest::enforceAspectSizeKeepsAndFixesRatio()
{
    // An already-correct size is returned unchanged.
    QCOMPARE(WindowChromeController::enforceAspectSize(QSize(1600, 1000), 1.6),
             QSize(1600, 1000));

    // A snapped half-screen size is corrected width-driven.
    const QSize snapped = WindowChromeController::enforceAspectSize(
        QSize(960, 1080), 16.0 / 9.0);
    QCOMPARE(snapped.width(), 960);
    QCOMPARE(snapped.height(), qRound(960 / (16.0 / 9.0)));

    // The supported ratios all survive enforcement.
    const QList<qreal> ratios {16.0 / 10.0, 16.0 / 9.0, 3.0 / 2.0, 4.0 / 3.0};
    for (const qreal ratio : ratios) {
        const QSize enforced = WindowChromeController::enforceAspectSize(
            QSize(1200, 1200), ratio);
        QVERIFY2(qAbs(static_cast<qreal>(enforced.width()) / enforced.height()
                      - ratio) < 0.002,
                 "Enforced size must use the selected aspect ratio");
    }
}

QTEST_GUILESS_MAIN(WindowChromeTest)
#include "tst_WindowChrome.moc"

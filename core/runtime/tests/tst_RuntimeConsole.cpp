// Console log parsing and the console view.
#include "tst_Runtime.h"

void RuntimeTest::progressFollowedByErrorKeepsError()
{
    LogModel model;
    model.appendStandardError(QByteArrayLiteral("\r 50%|#####     | 4/8 [00:01<00:01, 2.47it/s]"));
    model.appendStandardError(QByteArrayLiteral("RuntimeError: CUDA out of memory\n"));
    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(model.data(model.index(0, 0), LogModel::TextRole).toString(),
             QStringLiteral("RuntimeError: CUDA out of memory"));

    LogModel unterminated;
    unterminated.appendStandardError(
        QByteArrayLiteral("\r 75%|#######   | 6/8 [00:02<00:01, 2.35it/s]Traceback (most recent call last):"));
    unterminated.flush();
    QCOMPARE(unterminated.rowCount(), 1);
    QCOMPARE(unterminated.data(unterminated.index(0, 0), LogModel::TextRole).toString(),
             QStringLiteral("Traceback (most recent call last):"));
}

void RuntimeTest::tqdmProgressIsSeparatedFromConsoleLog()
{
    LogModel model;
    QSignalSpy progressSpy(&model, &LogModel::progressChanged);
    model.appendStandardError(
        QByteArrayLiteral("\rLoading UNet: 50%|#####     | 1/2 [00:10<00:10, 0.10it/s]"));
    QCOMPARE(model.rowCount(), 0);
    QVERIFY(!model.progressActive());

    model.appendStandardError(
        QByteArrayLiteral("\r  0%|          | 0/8 [00:00<?, ?it/s, Model Initializing ...]"));
    QCOMPARE(model.rowCount(), 0);
    QVERIFY(model.progressActive());
    QCOMPARE(model.progressPercent(), 0);
    QCOMPARE(model.progressCurrent(), 0);
    QCOMPARE(model.progressTotal(), 8);
    QVERIFY(model.progressLabel().isEmpty());

    QByteArray damagedProgressFrame = QByteArrayLiteral("\r 25%|");
    damagedProgressFrame.append(char(0xff));
    damagedProgressFrame.append(
        QByteArrayLiteral("         | 2/8 [00:00<00:02, 2.42it/s]"));
    model.appendStandardError(damagedProgressFrame);
    QCOMPARE(model.rowCount(), 0);
    QCOMPARE(model.progressPercent(), 25);
    QCOMPARE(model.progressCurrent(), 2);

    model.appendStandardError(
        QByteArrayLiteral("\r 50%|#####     | 4/8 [00:01<00:01, 2.47it/s]"));
    QCOMPARE(model.rowCount(), 0);
    QCOMPARE(model.progressPercent(), 50);
    QCOMPARE(model.progressCurrent(), 4);
    QCOMPARE(model.progressRemaining(), QStringLiteral("00:01"));
    QCOMPARE(model.progressRate(), QStringLiteral("2.47it/s"));
    model.appendStandardError(
        QByteArrayLiteral("\r采样步骤 50%-: 62.5%|######    | 5/8 [00:01<00:00, 3.00it/s]"));
    QCOMPARE(model.rowCount(), 0);
    QCOMPARE(model.progressPercent(), 63);
    QCOMPARE(model.progressCurrent(), 5);
    QCOMPARE(model.progressLabel(), QStringLiteral("采样步骤 50%-"));

    QVERIFY(!model.progressLabel().isEmpty());

    model.appendStandardError(
        QByteArrayLiteral("\rKSampler: 75%|#######5  | 6/8 [00:02<00:01, 2.35steps/s]"));
    QCOMPARE(model.rowCount(), 0);
    QCOMPARE(model.progressPercent(), 75);
    QCOMPARE(model.progressCurrent(), 6);
    QCOMPARE(model.progressTotal(), 8);
    QCOMPARE(model.progressLabel(), QStringLiteral("KSampler"));
    QCOMPARE(model.progressRate(), QStringLiteral("2.35steps/s"));

    model.appendStandardError(
        QByteArrayLiteral("\r100%|##########| 8/8 [00:03<00:00, 2.41it/s]\n"));
    QCOMPARE(model.rowCount(), 0);
    QCOMPARE(model.progressPercent(), 100);
    QCOMPARE(model.progressCurrent(), 8);
    QVERIFY(progressSpy.count() >= 3);
    QTRY_VERIFY_WITH_TIMEOUT(!model.progressActive(), 2000);
}

void RuntimeTest::carriageReturnLineEndingsRemainNormalLogLines()
{
    LogModel model;

    model.appendStandardOutput(QByteArrayLiteral("first line\r"));
    QCOMPARE(model.rowCount(), 1);
    model.appendStandardOutput(QByteArrayLiteral("\nsecond line\r\n"));
    QCOMPARE(model.rowCount(), 2);
    QCOMPARE(model.data(model.index(0, 0), LogModel::TextRole).toString(),
             QStringLiteral("first line"));
    QCOMPARE(model.data(model.index(1, 0), LogModel::TextRole).toString(),
             QStringLiteral("second line"));

    model.appendStandardOutput(QByteArrayLiteral("Memory 50% (1/2)\n"));
    QCOMPARE(model.rowCount(), 3);
    QVERIFY(!model.progressActive());
}

void RuntimeTest::consoleDecodesSplitUtf8AndLocalText()
{
    LogModel model;
    const QString utf8Text = QStringLiteral("模型加载完成");
    const QByteArray utf8 = utf8Text.toUtf8();

    model.appendStandardOutput(utf8.first(2));
    QCOMPARE(model.rowCount(), 0);
    model.appendStandardOutput(utf8.sliced(2) + '\n');
    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(model.data(model.index(0, 0), LogModel::TextRole).toString(), utf8Text);

    model.clear();
    const QString localText = QStringLiteral("本地编码输出");
    const QByteArray localBytes = localText.toLocal8Bit();
    const qsizetype split = qMax<qsizetype>(1, localBytes.size() / 2);
    model.appendStandardError(localBytes.first(split));
    model.appendStandardError(localBytes.sliced(split) + '\n');
    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(model.data(model.index(0, 0), LogModel::TextRole).toString(),
             QString::fromLocal8Bit(localBytes));
}

void RuntimeTest::consoleDisplayTextSupportsDocumentSelection()
{
    LogModel model;
    model.appendStandardOutput(QByteArrayLiteral("first line\n"));
    model.appendStandardError(QByteArrayLiteral("[INFO] second line\n"));
    model.appendStandardError(QByteArrayLiteral("[WARNING] third line\n"));
    model.appendStandardError(QByteArrayLiteral("RuntimeError: fourth line\n"));
    model.appendSystemMessage(QStringLiteral("fifth line"));

    const QString withTimestamps = model.displayText(true);
    QVERIFY(withTimestamps.contains(
        QRegularExpression(QStringLiteral(R"(^1 \d{2}:\d{2}:\d{2}\.\d{3} {5}first line$)"),
                           QRegularExpression::MultilineOption)));
    QVERIFY(withTimestamps.contains(
        QRegularExpression(QStringLiteral(R"(^2 \d{2}:\d{2}:\d{2}\.\d{3} {5}\[INFO\] second line$)"),
                           QRegularExpression::MultilineOption)));
    QVERIFY(withTimestamps.contains(
        QRegularExpression(QStringLiteral(R"(^3 \d{2}:\d{2}:\d{2}\.\d{3} {5}\[WARNING\] third line$)"),
                           QRegularExpression::MultilineOption)));
    QVERIFY(withTimestamps.contains(
        QRegularExpression(QStringLiteral(R"(^4 \d{2}:\d{2}:\d{2}\.\d{3} ERR RuntimeError: fourth line$)"),
                           QRegularExpression::MultilineOption)));
    QVERIFY(withTimestamps.contains(
        QRegularExpression(QStringLiteral(R"(^5 \d{2}:\d{2}:\d{2}\.\d{3} SYS fifth line$)"),
                           QRegularExpression::MultilineOption)));

    QCOMPARE(model.displayText(false, true),
             QStringLiteral("first line\n[INFO] second line\n[WARNING] third line\n"
                            "RuntimeError: fourth line\nfifth line"));

    const QString styled = model.displayStyledText(
        false, false, QStringLiteral("#777777"), QStringLiteral("#ffffff"),
        QStringLiteral("#ffff00"), QStringLiteral("#00aaff"),
        QStringLiteral("#00ff00"), QStringLiteral("#ff0000"));
    QVERIFY(!styled.contains(QStringLiteral(">ERR</font><font color=\"#00ff00\">[INFO]")));
    QVERIFY(styled.contains(QStringLiteral("<font color=\"#00ff00\">[INFO]</font>")));
    QVERIFY(styled.contains(QStringLiteral("<font color=\"#ffff00\">[WARNING]</font>")));
    QVERIFY(styled.contains(QStringLiteral("<font color=\"#ff0000\">ERR</font>")));
    QVERIFY(styled.contains(QStringLiteral("<font color=\"#00aaff\">SYS</font>")));
    QVERIFY(styled.contains(QStringLiteral("first line")));
    QVERIFY(styled.startsWith(QStringLiteral("<table width=\"100%\"")));
    QVERIFY(styled.contains(QStringLiteral("<td width=\"48\"")));
    QVERIFY(styled.contains(QStringLiteral("<td width=\"8\"")));
    QVERIFY(styled.contains(QStringLiteral("<td width=\"32\"")));

    const QString timestampedRange = model.displayStyledTextRange(
        1, true, false, QStringLiteral("#777777"), QStringLiteral("#ffffff"),
        QStringLiteral("#ffff00"), QStringLiteral("#00aaff"),
        QStringLiteral("#00ff00"), QStringLiteral("#ff0000"));
    QVERIFY(timestampedRange.contains(QStringLiteral("<td width=\"48\"")));
    QVERIFY(timestampedRange.contains(QStringLiteral("<td width=\"104\"")));
    QVERIFY(timestampedRange.contains(QStringLiteral("<td width=\"32\"")));
}

void RuntimeTest::consoleViewRefreshesWhenLogCountChanges()
{
    LogModel logModel;
    QQmlEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("testLogModel"), &logModel);

    QQmlComponent component(&engine);
    component.setData(R"(
        import QtQuick
        import QtQuick.Window
        import Minifox.Runtime

        Window {
            id: root
            width: 960
            height: 640
            visible: true

            property QtObject fakeSettings: QtObject {
                property string consoleFontFamily: "Consolas"
                property real consoleFontSize: 11
                property bool consoleWordWrap: true
                property bool showTimestamps: true
                property string consoleTheme: "dark"
            }
            property QtObject fakeRuntime: QtObject {
                property var logModel: testLogModel
            }
            property QtObject fakeContext: QtObject {
                property var settings: root.fakeSettings
                property var runtime: root.fakeRuntime
            }

            ConsoleView {
                objectName: "consoleView"
                anchors.fill: parent
                appContext: root.fakeContext
            }
        }
    )", QUrl(QStringLiteral("inmemory:/ConsoleRefreshProbe.qml")));

    QTRY_VERIFY_WITH_TIMEOUT(component.status() != QQmlComponent::Loading, 5000);
    QVERIFY2(component.status() == QQmlComponent::Ready, qPrintable(component.errorString()));
    QScopedPointer<QObject> root(component.create());
    QVERIFY2(root, qPrintable(component.errorString()));
    auto *window = qobject_cast<QQuickWindow *>(root.data());
    QVERIFY(window);
    QTRY_VERIFY_WITH_TIMEOUT(window->isVisible(), 1000);

    QObject *consoleView = root->findChild<QObject *>(QStringLiteral("consoleView"));
    QObject *consoleScroll = root->findChild<QObject *>(QStringLiteral("consoleScroll"));
    QObject *consoleText = root->findChild<QObject *>(QStringLiteral("consoleText"));
    QObject *resumeTailButton =
        root->findChild<QObject *>(QStringLiteral("resumeTailButton"));
    QVERIFY(consoleView);
    QVERIFY(consoleScroll);
    QVERIFY(consoleText);
    QVERIFY(resumeTailButton);
    QCOMPARE(resumeTailButton->property("visible").toBool(), false);
    QCOMPARE(consoleText->property("persistentSelection").toBool(), false);
    QCOMPARE(consoleText->property("selectedTextColor").value<QColor>(),
             consoleText->property("color").value<QColor>());
    QCOMPARE(consoleText->property("font").value<QFont>().weight(), QFont::Normal);
    QObject *viewport = consoleScroll->property("contentItem").value<QObject *>();
    QVERIFY(viewport);
    auto *consoleScrollItem = qobject_cast<QQuickItem *>(consoleScroll);
    QVERIFY(consoleScrollItem);
    auto *quickDocument =
        consoleText->property("textDocument").value<QQuickTextDocument *>();
    QVERIFY(quickDocument);

    for (int line = 1; line <= 79; ++line) {
        logModel.appendSystemMessage(QStringLiteral("refresh-probe-line-%1").arg(line));
    }
    logModel.appendSystemMessage(QStringLiteral("[DONE] Security scan"));

    QTRY_VERIFY_WITH_TIMEOUT(
        quickDocument->textDocument()->toPlainText().contains(
            QStringLiteral("[DONE] Security scan")),
        2000);

    QTRY_VERIFY_WITH_TIMEOUT(
        viewport->property("contentHeight").toReal() > viewport->property("height").toReal(),
        2000);
    QTRY_VERIFY_WITH_TIMEOUT(
        qAbs(viewport->property("contentY").toReal()
             - (viewport->property("contentHeight").toReal() - viewport->property("height").toReal()))
            < 2.0,
        2000);
    const qreal initialTailPosition = viewport->property("contentY").toReal();

    // Exercise the actual QQuickWindow event route. Calling handleWheelInput()
    // directly is insufficient because ScrollView/TextArea may consume a real
    // wheel event before a sibling handler sees it.
    const QPointF wheelPosition =
        consoleScrollItem->mapToScene(consoleScrollItem->boundingRect().center());
    QWheelEvent wheelUp(
        wheelPosition,
        window->mapToGlobal(wheelPosition.toPoint()),
        QPoint(),
        QPoint(0, 120),
        Qt::NoButton,
        Qt::NoModifier,
        Qt::NoScrollPhase,
        false);
    QCoreApplication::sendEvent(window, &wheelUp);
    QTRY_VERIFY_WITH_TIMEOUT(consoleView->property("manualNavigation").toBool(), 1000);
    QCOMPARE(consoleView->property("followTail").toBool(), false);
    QTRY_VERIFY_WITH_TIMEOUT(
        !viewport->property("moving").toBool() && !viewport->property("flicking").toBool(),
        1000);

    const qreal wheelInspectionPosition = viewport->property("contentY").toReal();
    QVERIFY2(wheelInspectionPosition < initialTailPosition,
             "The real upward wheel event did not move the ScrollView away from the tail.");
    logModel.appendSystemMessage(QStringLiteral("wheel-routing-probe"));
    QTRY_VERIFY_WITH_TIMEOUT(
        quickDocument->textDocument()->toPlainText().contains(
            QStringLiteral("wheel-routing-probe")),
        2000);
    QCOMPARE(consoleView->property("followTail").toBool(), false);
    QCOMPARE(consoleView->property("manualNavigation").toBool(), true);
    QCOMPARE(resumeTailButton->property("visible").toBool(), true);
    QVERIFY2(qAbs(viewport->property("contentY").toReal() - wheelInspectionPosition) < 2.0,
             "A real wheel event did not stop automatic tail following.");

    // Reaching the live edge, including with another real wheel event, must not
    // restore following. Recovery is an explicit action on the floating button.
    const qreal postWheelMaximum = std::max(
        0.0,
        viewport->property("contentHeight").toReal() - viewport->property("height").toReal());
    QVERIFY(viewport->setProperty("contentY", postWheelMaximum));
    QCoreApplication::processEvents();
    QCOMPARE(consoleView->property("followTail").toBool(), false);
    QCOMPARE(consoleView->property("manualNavigation").toBool(), true);
    QCOMPARE(resumeTailButton->property("visible").toBool(), true);

    QWheelEvent wheelDown(
        wheelPosition,
        window->mapToGlobal(wheelPosition.toPoint()),
        QPoint(),
        QPoint(0, -120),
        Qt::NoButton,
        Qt::NoModifier,
        Qt::NoScrollPhase,
        false);
    QCoreApplication::sendEvent(window, &wheelDown);
    QTest::qWait(100);
    QCOMPARE(consoleView->property("followTail").toBool(), false);
    QCOMPARE(consoleView->property("manualNavigation").toBool(), true);
    QCOMPARE(resumeTailButton->property("visible").toBool(), true);

    auto *resumeTailButtonItem = qobject_cast<QQuickItem *>(resumeTailButton);
    QVERIFY(resumeTailButtonItem);
    const QPointF resumeButtonPosition =
        resumeTailButtonItem->mapToScene(resumeTailButtonItem->boundingRect().center());
    QTest::mouseClick(
        window,
        Qt::LeftButton,
        Qt::NoModifier,
        resumeButtonPosition.toPoint());
    QTRY_VERIFY_WITH_TIMEOUT(consoleView->property("followTail").toBool(), 1000);
    QCOMPARE(consoleView->property("manualNavigation").toBool(), false);
    QCOMPARE(resumeTailButton->property("visible").toBool(), false);
    QTRY_VERIFY_WITH_TIMEOUT(
        qAbs(viewport->property("contentY").toReal()
             - (viewport->property("contentHeight").toReal()
                - viewport->property("height").toReal()))
            < 2.0,
        1000);

    // Pointer movement can be reported without any wheel delta. It must not
    // disable automatic following.
    QVERIFY(QMetaObject::invokeMethod(
        consoleView,
        "handleWheelInput",
        Q_ARG(QVariant, QVariant(0)),
        Q_ARG(QVariant, QVariant(0))));
    QCOMPARE(consoleView->property("followTail").toBool(), true);
    QCOMPARE(consoleView->property("manualNavigation").toBool(), false);

    // Background logs must use their own QTextCursor. Reusing TextArea's active
    // cursor changes the live selection and can leak its character format into
    // preceding rows.
    const int requestedSelectionStart =
        quickDocument->textDocument()->toPlainText().indexOf(QStringLiteral("[DONE]"));
    QVERIFY(requestedSelectionStart >= 0);
    const int requestedSelectionEnd =
        requestedSelectionStart + QStringLiteral("[DONE]").size();
    QVERIFY(QMetaObject::invokeMethod(
        consoleText,
        "select",
        Q_ARG(int, requestedSelectionStart),
        Q_ARG(int, requestedSelectionEnd)));
    QTRY_VERIFY_WITH_TIMEOUT(consoleView->property("selectionActive").toBool(), 1000);
    QCOMPARE(consoleView->property("followTail").toBool(), true);
    QCOMPARE(consoleView->property("manualNavigation").toBool(), false);
    const int selectionStart = consoleText->property("selectionStart").toInt();
    const int selectionEnd = consoleText->property("selectionEnd").toInt();
    const int cursorPosition = consoleText->property("cursorPosition").toInt();
    const QString selectedText = consoleText->property("selectedText").toString();
    QCOMPARE(selectionStart, requestedSelectionStart);
    QCOMPARE(selectionEnd, requestedSelectionEnd);

    for (int line = 81; line <= 90; ++line) {
        logModel.appendSystemMessage(QStringLiteral("refresh-probe-line-%1").arg(line));
    }
    QTRY_VERIFY_WITH_TIMEOUT(
        quickDocument->textDocument()->toPlainText().contains(
            QStringLiteral("refresh-probe-line-90")),
        2000);
    QCOMPARE(consoleView->property("renderedLogCount").toInt(), 91);
    QCOMPARE(consoleText->property("selectionStart").toInt(), selectionStart);
    QCOMPARE(consoleText->property("selectionEnd").toInt(), selectionEnd);
    QCOMPARE(consoleText->property("cursorPosition").toInt(), cursorPosition);
    QCOMPARE(consoleText->property("selectedText").toString(), selectedText);
    QVERIFY(consoleView->property("selectionActive").toBool());

    for (QTextBlock block = quickDocument->textDocument()->begin();
         block.isValid();
         block = block.next()) {
        for (QTextBlock::Iterator fragmentIterator = block.begin();
             !fragmentIterator.atEnd();
             ++fragmentIterator) {
            const QTextFragment fragment = fragmentIterator.fragment();
            if (fragment.isValid()) {
                QVERIFY2(fragment.charFormat().fontWeight() <= QFont::Normal,
                         qPrintable(fragment.text()));
            }
        }
    }

    QVERIFY(QMetaObject::invokeMethod(consoleText, "deselect"));
    QTRY_VERIFY_WITH_TIMEOUT(!consoleView->property("selectionActive").toBool(), 1000);
    QTRY_VERIFY_WITH_TIMEOUT(
        qAbs(viewport->property("contentY").toReal()
             - (viewport->property("contentHeight").toReal() - viewport->property("height").toReal()))
            < 2.0,
        2000);

    // Real startup output arrives in multiple batches. Growing the document moves
    // the scrollbar away from its former end before the queued follow-tail update;
    // that programmatic movement must not be mistaken for a user scroll.
    for (int line = 91; line <= 170; ++line) {
        logModel.appendSystemMessage(QStringLiteral("refresh-probe-line-%1").arg(line));
    }

    QTRY_VERIFY_WITH_TIMEOUT(
        quickDocument->textDocument()->toPlainText().contains(
            QStringLiteral("refresh-probe-line-170")),
        2000);
    QTRY_VERIFY_WITH_TIMEOUT(
        qAbs(viewport->property("contentY").toReal()
             - (viewport->property("contentHeight").toReal() - viewport->property("height").toReal()))
            < 2.0,
        2000);

    QVERIFY(QMetaObject::invokeMethod(consoleView, "beginUserNavigation"));
    const qreal inspectionPosition = viewport->property("contentY").toReal() / 2.0;
    QVERIFY(viewport->setProperty("contentY", inspectionPosition));
    QCOMPARE(consoleView->property("followTail").toBool(), false);
    QCOMPARE(consoleView->property("manualNavigation").toBool(), true);
    QCOMPARE(resumeTailButton->property("visible").toBool(), true);
    QSignalSpy contentYChanges(viewport, SIGNAL(contentYChanged()));
    QVERIFY(contentYChanges.isValid());

    // While the user moves toward the latest row, background output must never
    // restore an earlier anchor or fight over contentY.
    for (int line = 171; line <= 180; ++line) {
        const qreal maximumY = std::max(
            0.0,
            viewport->property("contentHeight").toReal()
                - viewport->property("height").toReal());
        const qreal currentY = viewport->property("contentY").toReal();
        const qreal requestedY = std::min(maximumY - 4.0, currentY + 12.0);
        QVERIFY(viewport->setProperty("contentY", requestedY));
        QCoreApplication::processEvents();
        contentYChanges.clear();
        logModel.appendSystemMessage(QStringLiteral("refresh-probe-line-%1").arg(line));
        QTRY_VERIFY_WITH_TIMEOUT(
            quickDocument->textDocument()->toPlainText().contains(
                QStringLiteral("refresh-probe-line-%1").arg(line)),
            2000);
        QVERIFY2(qAbs(viewport->property("contentY").toReal() - requestedY) < 2.0,
                 qPrintable(QStringLiteral("line %1 moved from %2 to %3")
                                .arg(line)
                                .arg(requestedY)
                                .arg(viewport->property("contentY").toReal())));
        QVERIFY2(contentYChanges.isEmpty(),
                 qPrintable(QStringLiteral("background append changed contentY on line %1")
                                .arg(line)));
    }
    QCOMPARE(consoleView->property("manualNavigation").toBool(), true);

    const qreal latestPosition = std::max(
        0.0,
        viewport->property("contentHeight").toReal() - viewport->property("height").toReal());
    QVERIFY(viewport->setProperty("contentY", latestPosition));
    QCoreApplication::processEvents();
    QCOMPARE(consoleView->property("followTail").toBool(), false);
    QCOMPARE(consoleView->property("manualNavigation").toBool(), true);
    QCOMPARE(resumeTailButton->property("visible").toBool(), true);
    QVERIFY(QMetaObject::invokeMethod(consoleView, "resumeTailFollowing"));
    QCOMPARE(consoleView->property("followTail").toBool(), true);
    QCOMPARE(consoleView->property("manualNavigation").toBool(), false);
    QCOMPARE(resumeTailButton->property("visible").toBool(), false);

    logModel.appendSystemMessage(QStringLiteral("refresh-probe-line-181"));
    QTRY_VERIFY_WITH_TIMEOUT(
        quickDocument->textDocument()->toPlainText().contains(
            QStringLiteral("refresh-probe-line-181")),
        2000);
    QTRY_VERIFY_WITH_TIMEOUT(
        qAbs(viewport->property("contentY").toReal()
             - (viewport->property("contentHeight").toReal() - viewport->property("height").toReal()))
            < 2.0,
        2000);
}

void RuntimeTest::consoleHistoryIsBounded()
{
    LogModel model;
    QSignalSpy trimmedSpy(&model, &LogModel::historyTrimmed);
    for (int line = 0; line <= LogModel::MaximumEntryCount; ++line) {
        model.appendSystemMessage(QStringLiteral("line-%1").arg(line));
    }

    QVERIFY(model.rowCount() <= LogModel::MaximumEntryCount);
    QCOMPARE(trimmedSpy.count(), 1);
    QCOMPARE(model.data(model.index(0, 0), LogModel::TextRole).toString(),
             QStringLiteral("line-5000"));

    model.clear();
    model.appendStandardOutput(
        QByteArray(LogModel::MaximumPartialCharacters + 1, 'x'));
    QCOMPARE(model.rowCount(), 1);
    model.flush();
    QCOMPARE(model.rowCount(), 2);

    model.clear();
    QByteArray oversizedLine(LogModel::MaximumPartialCharacters + 128, 'y');
    oversizedLine.append('\n');
    model.appendStandardOutput(oversizedLine);
    QCOMPARE(model.rowCount(), 1);
    const QString storedLine = model.data(
        model.index(0, 0), LogModel::TextRole).toString();
    QVERIFY(storedLine.size() <= LogModel::MaximumPartialCharacters + 16);
    QVERIFY(storedLine.endsWith(QStringLiteral("[truncated]")));

    model.clear();
    model.appendSystemMessage(
        QString(LogModel::MaximumPartialCharacters + 128, QLatin1Char('z')));
    QCOMPARE(model.rowCount(), 1);
    QVERIFY(model.data(model.index(0, 0), LogModel::TextRole)
                .toString().endsWith(QStringLiteral("[truncated]")));
}

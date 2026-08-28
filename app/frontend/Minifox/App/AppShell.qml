pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Minifox.ApplicationSettings
import Minifox.Configuration
import Minifox.Runtime
import Minifox.Shared

Control {
    id: root

    required property var appContext
    property int currentPage: 0
    readonly property bool compactNavigation: width < 1000
    onCurrentPageChanged: root.appContext.configuration.savePendingChanges()

    padding: 0
    background: Rectangle {
        color: Theme.pageFill
    }
    font.family: Theme.uiFontFamily
    font.pointSize: Theme.bodySize
    palette.window: Theme.surface
    palette.windowText: Theme.foreground
    palette.base: Theme.surfaceRaised
    palette.alternateBase: Theme.surfaceSubtle
    palette.text: Theme.foreground
    palette.button: Theme.surfaceRaised
    palette.buttonText: Theme.foreground
    palette.accent: Theme.accent
    palette.highlight: Theme.accent
    palette.highlightedText: Theme.accentForeground
    palette.brightText: Theme.error
    palette.light: Theme.surfaceRaised
    palette.midlight: Theme.surfaceSubtle
    palette.mid: Theme.outline
    palette.dark: Theme.outline
    palette.shadow: Theme.shadow
    palette.toolTipBase: Theme.surfaceRaised
    palette.toolTipText: Theme.foreground
    palette.placeholderText: Theme.foregroundSecondary
    palette.link: Theme.info
    palette.linkVisited: Theme.info

    function pageComponent(index) {
        switch (index) {
        case 0:
            return dashboardPageComponent;
        case 1:
            return configurationPageComponent;
        case 2:
            return runtimePageComponent;
        case 3:
            return versionPageComponent;
        case 4:
            return settingsPageComponent;
        default:
            return dashboardPageComponent;
        }
    }

    Component {
        id: dashboardPageComponent

        DashboardPage {
            appContext: root.appContext
            onOpenConfiguration: root.currentPage = 1
            onOpenRuntime: root.currentPage = 2
            onLaunchRequested: {
                root.appContext.runtime.start();
                root.currentPage = 2;
            }
        }
    }

    Component {
        id: configurationPageComponent

        ConfigurationPage {
            appContext: root.appContext
            onLaunchRequested: {
                root.appContext.runtime.start();
                root.currentPage = 2;
            }
        }
    }

    Component {
        id: runtimePageComponent

        RuntimePage {
            appContext: root.appContext
        }
    }

    Component {
        id: versionPageComponent

        VersionPage {
            appContext: root.appContext
        }
    }

    Component {
        id: settingsPageComponent

        SettingsPage {
            appContext: root.appContext
        }
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        NavigationRail {
            currentIndex: root.currentPage
            compact: root.compactNavigation
            Layout.fillHeight: true
            Layout.preferredWidth: root.compactNavigation ? 72 : 108
            onPageSelected: index => root.currentPage = index
        }

        Loader {
            Layout.fillWidth: true
            Layout.fillHeight: true
            sourceComponent: root.pageComponent(root.currentPage)
        }
    }
}

// SPDX-FileCopyrightText: 2026 Lukas Dobler
// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Auc.DataTool

// Checks GitHub Releases for a newer version of AUCDataTool and downloads/installs it.
Dialog {
    id: root
    required property UpdateChecker updater
    title: qsTr("Software update")
    modal: true
    standardButtons: Dialog.Close
    width: Math.min(520, parent ? parent.width - 40 : 520)
    anchors.centerIn: parent

    function formatBytes(b) {
        if (b >= 1024 * 1024) return (b / (1024 * 1024)).toFixed(1) + " MB"
        if (b >= 1024) return (b / 1024).toFixed(0) + " KB"
        return b + " B"
    }

    onAboutToShow: {
        if (!updater.checked && !updater.checking) updater.checkForUpdates(true)
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 10

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Label {
                text: updater.updateAvailable
                      ? qsTr("AUCDataTool %1 is available").arg(updater.latestVersion)
                      : qsTr("AUCDataTool %1").arg(updater.currentVersion)
                font.bold: true
                font.pixelSize: 15
                Layout.fillWidth: true
            }
            Label {
                visible: updater.updateAvailable && updater.publishedDate !== ""
                text: updater.publishedDate
                opacity: 0.65
                font.pixelSize: 12
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            BusyIndicator {
                visible: updater.checking
                running: visible
                Layout.preferredWidth: 20
                Layout.preferredHeight: 20
            }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: updater.statusMessage !== ""
                      ? updater.statusMessage
                      : qsTr("Installed version: %1").arg(updater.currentVersion)
                color: updater.error ? "#dc2626" : palette.windowText
            }
        }

        // Release notes
        Frame {
            visible: updater.updateAvailable && updater.releaseNotes !== ""
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(200, Math.max(80, notesArea.contentHeight + 20))
            padding: 4
            ScrollView {
                anchors.fill: parent
                clip: true
                TextArea {
                    id: notesArea
                    readOnly: true
                    selectByMouse: true
                    wrapMode: Text.WordWrap
                    textFormat: Text.MarkdownText
                    text: updater.releaseNotes
                    background: null
                    onLinkActivated: (link) => Qt.openUrlExternally(link)
                }
            }
        }

        // Platform update hint
        Label {
            visible: updater.updateAvailable && updater.installHint !== "" && !updater.downloading && updater.downloadedFile === ""
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            font.pixelSize: 12
            opacity: 0.75
            text: updater.installHint
        }

        // Download progress
        ColumnLayout {
            visible: updater.downloading
            Layout.fillWidth: true
            spacing: 4
            ProgressBar {
                Layout.fillWidth: true
                indeterminate: updater.downloadTotalBytes <= 0
                value: updater.downloadProgress
            }
            RowLayout {
                Layout.fillWidth: true
                Label {
                    font.pixelSize: 12
                    opacity: 0.75
                    text: updater.downloadTotalBytes > 0
                          ? qsTr("%1 / %2").arg(root.formatBytes(updater.downloadBytes)).arg(root.formatBytes(updater.downloadTotalBytes))
                          : root.formatBytes(updater.downloadBytes)
                    Layout.fillWidth: true
                }
                Button {
                    text: qsTr("Cancel")
                    onClicked: updater.cancelDownload()
                }
            }
        }

        // Linux / downloaded package install command
        ColumnLayout {
            visible: updater.downloadedFile !== "" && !updater.downloading
            Layout.fillWidth: true
            spacing: 6
            RowLayout {
                visible: updater.installCommand !== ""
                Layout.fillWidth: true
                spacing: 6
                TextField {
                    Layout.fillWidth: true
                    readOnly: true
                    selectByMouse: true
                    text: updater.installCommand
                    font.family: "monospace"
                    font.pixelSize: 12
                }
                Button {
                    id: copyButton
                    property bool copied: false
                    text: copied ? qsTr("Copied") : qsTr("Copy")
                    onClicked: {
                        updater.copyInstallCommand()
                        copied = true
                        copiedReset.restart()
                    }
                    Timer {
                        id: copiedReset
                        interval: 2000
                        onTriggered: copyButton.copied = false
                    }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                Button {
                    text: qsTr("Open folder")
                    onClicked: updater.openDownloadedFolder()
                }
                Item { Layout.fillWidth: true }
            }
        }

        // Actions
        Flow {
            Layout.fillWidth: true
            spacing: 8
            Button {
                visible: updater.updateAvailable
                enabled: !updater.downloading && !updater.checking
                highlighted: true
                text: updater.assetSize > 0
                      ? qsTr("%1 (%2)").arg(updater.installActionLabel).arg(root.formatBytes(updater.assetSize))
                      : updater.installActionLabel
                onClicked: updater.downloadAndInstall()
            }
            Button {
                text: qsTr("Release page")
                onClicked: updater.openReleasePage()
            }
            Button {
                text: qsTr("Check now")
                enabled: !updater.checking && !updater.downloading
                onClicked: updater.checkForUpdates(true)
            }
            Button {
                visible: updater.updateAvailable && updater.skippedVersion !== updater.latestVersion
                flat: true
                text: qsTr("Skip %1").arg(updater.latestVersion)
                onClicked: {
                    updater.skipLatestVersion()
                    root.close()
                }
            }
        }

        CheckBox {
            text: qsTr("Check for updates automatically on startup")
            checked: updater.autoCheck
            onToggled: updater.autoCheck = checked
        }
    }
}

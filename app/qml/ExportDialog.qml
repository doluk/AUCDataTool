// SPDX-FileCopyrightText: 2026 Lukas Dobler
// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

// Export of the processed data (with all current processing options) for other programs.
Dialog {
    id: root
    property var controller
    title: qsTr("Export data")
    modal: true
    standardButtons: Dialog.Cancel
    width: Math.min(460, parent ? parent.width - 40 : 460)
    anchors.centerIn: parent

    readonly property var formats: [
        { name: qsTr("CSV (spreadsheet, Excel)"), folder: false, suffix: "csv",
          filter: qsTr("CSV (*.csv)"), info: qsTr("One file: radius column, one column per scan.") },
        { name: qsTr("Origin ASCII"), folder: false, suffix: "dat",
          filter: qsTr("Origin ASCII (*.dat *.txt)"),
          info: qsTr("Tab-separated with Long Name / Units / Comments header rows for Origin's ASCII import.") },
        { name: qsTr("Beckman XL ASCII"), folder: true,
          info: qsTr("One file per scan (e.g. 2A280/A00012.RA2), readable by UltraScan, SEDFIT and the XL software.") },
        { name: qsTr("UltraScan III (openAUC .auc)"), folder: true,
          info: qsTr("One .auc file per cell/channel/wavelength (run.RA.2.A.280.auc) for UltraScan's import.") }
    ]

    onAboutToShow: {
        fromField.text = controller.wavelengthMin.toFixed(1)
        toField.text = controller.wavelengthMax.toFixed(1)
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 8

        Label { text: qsTr("Format") }
        ComboBox {
            id: formatBox
            Layout.fillWidth: true
            model: root.formats.map(f => f.name)
        }
        Label {
            text: root.formats[formatBox.currentIndex].info
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
            opacity: 0.7
            font.pixelSize: 12
        }

        CheckBox {
            id: allWl
            visible: root.controller.wavelengthCount > 1
            text: qsTr("All wavelengths in a range (one data set each)")
        }
        GridLayout {
            visible: allWl.visible && allWl.checked
            columns: 6
            Layout.fillWidth: true
            Label { text: qsTr("from") }
            TextField {
                id: fromField
                validator: DoubleValidator { locale: "C" }
                Layout.fillWidth: true
            }
            Label { text: qsTr("to") }
            TextField {
                id: toField
                validator: DoubleValidator { locale: "C" }
                Layout.fillWidth: true
            }
            Label { text: qsTr("every") }
            SpinBox {
                id: stepBox
                from: 1
                to: 100
                value: 1
                editable: true
            }
        }
        Label {
            text: allWl.visible && allWl.checked
                  ? qsTr("Each wavelength is processed like the current view (reference, scan selection, noise, corrections).")
                  : qsTr("Exports the current view: %1").arg(root.controller.runInfo)
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
            opacity: 0.7
            font.pixelSize: 12
        }

        Button {
            text: root.formats[formatBox.currentIndex].folder ? qsTr("Choose folder and export…") : qsTr("Choose file and export…")
            highlighted: true
            Layout.alignment: Qt.AlignRight
            onClicked: {
                const f = root.formats[formatBox.currentIndex]
                if (f.folder) {
                    folderDialog.open()
                } else {
                    fileDialog.defaultSuffix = f.suffix
                    fileDialog.nameFilters = [f.filter]
                    fileDialog.open()
                }
            }
        }
    }

    function run(url) {
        controller.exportData(formatBox.currentIndex, url, allWl.visible && allWl.checked,
                              Number(fromField.text), Number(toField.text), stepBox.value)
        root.close()
    }

    FileDialog {
        id: fileDialog
        title: qsTr("Export to file")
        fileMode: FileDialog.SaveFile
        onAccepted: root.run(selectedFile)
    }
    FolderDialog {
        id: folderDialog
        title: qsTr("Export to folder")
        onAccepted: root.run(selectedFolder)
    }
}

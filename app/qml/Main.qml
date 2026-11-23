// SPDX-FileCopyrightText: 2026 Lukas Dobler
// SPDX-License-Identifier: LGPL-3.0-or-later
import QtCore
import QtQuick
import QtQuick.Controls
// Qualified: an unqualified import would replace the controls of this file with the
// Material ones on every platform (tall indigo bars, a light theme in dark mode).
import QtQuick.Controls.Material as Mat
import QtQuick.Layouts
import QtQuick.Dialogs
import Auc.DataTool

ApplicationWindow {
    id: win
    width: 1400
    height: 860
    visible: true
    title: ctrl.currentIndex >= 0 && ctrl.channels.length > ctrl.currentIndex
           ? ctrl.channels[ctrl.currentIndex].run + " · " + ctrl.channels[ctrl.currentIndex].title + " – AUCDataTool"
           : "AUCDataTool"

    // Dark scheme: the Windows theme gives check boxes (and radio buttons) an accent-coloured
    // base, so an unchecked box looks checked. Set on the window, the base propagates to
    // every control; light: reset to the theme's.
    readonly property bool darkScheme: Application.styleHints.colorScheme === Qt.ColorScheme.Dark
    palette.base: darkScheme ? "#2a2a2a" : undefined

    // Exposed for main.cpp (command-line files, benchmark).
    property alias controller: ctrl
    property alias mainPlot: scanView.plot
    /// 0 scans, 1 run conditions, 2 3D surface, 3 scan timing (main.cpp --view)
    property alias viewIndex: viewTabs.currentIndex
    property real lastProcessMs: 0
    readonly property bool isAndroid: Qt.platform.os === "android"
    /// Phone layout (e.g. 412 × 915 dp portrait, 915 × 412 landscape): plots use the full
    /// window, channel list and options open as drawers, actions sit in a menu.
    readonly property bool compact: width < 1000 || height < 520
    /// Phone in landscape: tabs move into the toolbar and the status bar is hidden.
    readonly property bool landscapePhone: compact && width > height
    onCompactChanged: { channelDrawer.close(); optionsDrawer.close() }

    AppController {
        id: ctrl
        scanPlot: scanView.plot
        integralPlot: integralView.plot
        spectrumPlot: spectrumView.plot
        speedPlot: runView.speedPlot
        temperaturePlot: runView.temperaturePlot
        omega2tPlot: runView.omega2tPlot
        timingPlot: timingView.timingPlot
        transitionPlot: timingView.transitionPlot
        sweepPlot: timingView.sweepPlot
        sweepSpeedPlot: timingView.sweepSpeedPlot
        surfaceActive: viewTabs.currentIndex === 2
        onProcessed: (ms) => win.lastProcessMs = ms
        // --set surfaceActive=true on the command line opens the tab.
        onSurfaceSettingsChanged: if (surfaceActive && surfaceSupported) viewTabs.currentIndex = 2
        onCurrentIndexChanged: channelDrawer.close()
        // Scan timing exists only for absorbance/intensity data (not interference, fluorescence).
        onDatasetChanged: if (!hasScanTiming && viewTabs.currentIndex === 3) viewTabs.currentIndex = 0
    }

    // Grabs the visible graph area (scan/spectrum/integral plots or the 3D surface).
    function grabGraph(callback) {
        const item = viewStack.currentIndex === 2 && surfaceLoader.item ? surfaceLoader.item
                   : viewStack.currentIndex === 1 ? runView
                   : viewStack.currentIndex === 3 ? timingView : plotArea
        item.grabToImage(callback, Qt.size(item.width * 2, item.height * 2))
    }

    FileDialog {
        id: fileDialog
        title: qsTr("Open data files")
        fileMode: FileDialog.OpenFiles
        nameFilters: win.isAndroid ? [] : [qsTr("AUC data (*.auc *.mwrs *.mw *.mw? *.ra? *.ri? *.ip? *.wa? *.wi? *.fi?)"), qsTr("openAUC (*.auc)"),
                      qsTr("Multi-wavelength (*.mwrs *.mw *.mw?)"), qsTr("Beckman XL (*.ra? *.ri? *.ip? *.wa? *.wi? *.fi?)"),
                      qsTr("All files (*)")]  // Android: the picker maps filters to MIME types, the AUC extensions have none
        onAccepted: {
            if (selectedFiles.length > 0) win.rememberFolder(win.parentFolder(selectedFiles[0]))
            ctrl.openFiles(selectedFiles)
        }
    }
    FolderDialog {
        id: folderDialog
        property bool live: false
        title: live ? qsTr("Watch folder (live)") : qsTr("Open folder")
        onAccepted: {
            win.rememberFolder(selectedFolder)
            ctrl.openFolder(selectedFolder, live)
        }
    }

    // Open dialogs start where data was last opened from (kept across sessions), else in
    // the home folder. Not on Android: its pickers return content:// URIs and keep their own.
    Settings {
        id: dialogSettings
        category: "dialogs"
        property string lastFolder: ""
    }
    readonly property url homeFolder: StandardPaths.writableLocation(StandardPaths.HomeLocation)
    function parentFolder(url) {
        const s = url.toString().replace(/\/+$/, "")
        const i = s.lastIndexOf("/")
        return i > 0 ? s.substring(0, i) : s
    }
    function rememberFolder(url) { if (!win.isAndroid) dialogSettings.lastFolder = url.toString() }
    function openFilesDialog() {
        if (!win.isAndroid)
            fileDialog.currentFolder = dialogSettings.lastFolder !== "" ? dialogSettings.lastFolder : win.homeFolder
        fileDialog.open()
    }
    function openFolderDialog(live) {
        folderDialog.live = live
        if (!win.isAndroid) {
            // Show the last folder selected inside its parent, so a sibling run is one click away.
            const last = dialogSettings.lastFolder
            folderDialog.currentFolder = last !== "" ? win.parentFolder(last) : win.homeFolder
            if (last !== "") folderDialog.selectedFolder = last
        }
        folderDialog.open()
    }
    ExportDialog {
        id: exportDialog
        controller: ctrl
    }
    FileDialog {
        id: saveGraphDialog
        title: qsTr("Save graph")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "png"
        nameFilters: [qsTr("PNG image (*.png)"), qsTr("PDF document (*.pdf)"), qsTr("JPEG image (*.jpg)")]
        onAccepted: {
            const url = selectedFile
            win.grabGraph(r => ctrl.saveImage(r.image, url, ctrl.runInfo))
        }
    }

    Shortcut { sequences: [StandardKey.Open]; onActivated: win.openFilesDialog() }
    Shortcut { sequences: [StandardKey.Print]; enabled: ctrl.canPrint && ctrl.scanCount > 0; onActivated: win.grabGraph(r => ctrl.printImage(r.image, ctrl.runInfo)) }
    Shortcut { sequence: "Esc"; onActivated: scanView.plot.selectedCurve = -1 }
    Shortcut { sequence: "F1"; onActivated: scanView.plot.autoscale() }  // same key as the LabVIEW viewer
    Shortcut { sequences: ["Ctrl+Right", "PgUp"]; onActivated: ctrl.stepWavelength(1) }
    Shortcut { sequences: ["Ctrl+Left", "PgDown"]; onActivated: ctrl.stepWavelength(-1) }

    function startWatch() { win.openFolderDialog(true) }

    Menu {
        id: actionsMenu
        MenuItem { text: qsTr("Open files…"); onTriggered: win.openFilesDialog() }
        MenuItem { text: qsTr("Open folder…"); onTriggered: win.openFolderDialog(false) }
        MenuItem {
            text: ctrl.live ? qsTr("Stop live mode") : qsTr("Watch folder…")
            onTriggered: ctrl.live ? ctrl.stopLive() : win.startWatch()
        }
        MenuSeparator {}
        MenuItem {
            text: ctrl.exporting ? qsTr("Exporting…") : qsTr("Export…")
            enabled: scanView.plot.hasData && !ctrl.exporting
            onTriggered: exportDialog.open()
        }
        MenuItem { text: qsTr("Save graph…"); enabled: ctrl.scanCount > 0; onTriggered: saveGraphDialog.open() }
        MenuItem {
            text: qsTr("Print…")
            visible: ctrl.canPrint
            height: visible ? implicitHeight : 0
            enabled: ctrl.scanCount > 0
            onTriggered: win.grabGraph(r => ctrl.printImage(r.image, ctrl.runInfo))
        }
        MenuSeparator {}
        MenuItem { text: qsTr("Close all channels"); enabled: ctrl.channels.length > 0; onTriggered: ctrl.closeAll() }
    }

    header: ToolBar {
      ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // Phone: channels · current channel · fit · options · menu
        RowLayout {
            visible: win.compact
            spacing: 0
            Layout.fillWidth: true
            IconButton { glyph: "menu"; onClicked: channelDrawer.open() }
            ColumnLayout {
                spacing: 0
                Layout.fillWidth: true
                Label {
                    id: compactTitle
                    text: ctrl.currentIndex >= 0 && ctrl.channels.length > ctrl.currentIndex
                          ? ctrl.channels[ctrl.currentIndex].title : "AUCDataTool"
                    font.bold: true
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }
                Label {
                    visible: text !== ""
                    text: ctrl.live ? qsTr("● Live – %1").arg(ctrl.runInfo) : ctrl.runInfo
                    color: compactTitle.color  // toolbar text colour; "● Live" marks live mode
                    font.pixelSize: 11
                    opacity: 0.8
                    elide: Text.ElideLeft
                    Layout.fillWidth: true
                }
            }
            Item {
                id: landscapeTabSlot
                visible: win.landscapePhone
                Layout.preferredWidth: 300
                Layout.fillHeight: true
            }
            BusyIndicator {
                visible: win.landscapePhone && ctrl.busy
                running: visible
                Layout.preferredWidth: 32
                Layout.preferredHeight: 32
            }
            IconButton { glyph: "fit"; enabled: scanView.plot.hasData; onClicked: scanView.plot.autoscale() }
            IconButton { glyph: "tune"; onClicked: optionsDrawer.open() }
            IconButton { id: moreButton; glyph: "more"; onClicked: actionsMenu.popup(moreButton, moreButton.width - actionsMenu.width, moreButton.height) }
        }

        // Desktop: actions in framed groups (data in · results out · view) …
        RowLayout {
            visible: !win.compact
            Layout.fillWidth: true
            Layout.leftMargin: 6
            Layout.rightMargin: 6
            Layout.topMargin: 4
            Layout.bottomMargin: 4
            spacing: 8
            ActionGroup {
                ToolButton { text: qsTr("Open files…"); onClicked: win.openFilesDialog() }
                ToolSeparator {}
                ToolButton { text: qsTr("Open folder…"); onClicked: win.openFolderDialog(false) }
                ToolSeparator {}
                ToolButton {
                    text: ctrl.live ? qsTr("● Live") : qsTr("Watch folder…")
                    checkable: true
                    checked: ctrl.live
                    palette.buttonText: ctrl.live ? "#dc2626" : win.palette.buttonText
                    ToolTip.visible: hovered
                    ToolTip.text: ctrl.live ? qsTr("Watching %1 – click to stop").arg(ctrl.liveFolder)
                                            : qsTr("Open a folder and add/update files while the run is acquiring")
                    onClicked: {
                        if (ctrl.live) ctrl.stopLive()
                        else { checked = false; win.startWatch() }
                    }
                }
            }
            ActionGroup {
                ToolButton {
                    text: ctrl.exporting ? qsTr("Exporting…") : qsTr("Export…")
                    enabled: scanView.plot.hasData && !ctrl.exporting
                    onClicked: exportDialog.open()
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("CSV, Origin, Beckman XL or UltraScan (.auc), one or many wavelengths")
                }
                ToolSeparator { visible: ctrl.canPrint }
                ToolButton {
                    text: qsTr("Print…")
                    visible: ctrl.canPrint
                    enabled: ctrl.scanCount > 0
                    onClicked: win.grabGraph(r => ctrl.printImage(r.image, ctrl.runInfo))
                }
                ToolSeparator {}
                ToolButton { text: qsTr("Save graph…"); enabled: ctrl.scanCount > 0; onClicked: saveGraphDialog.open() }
            }
            ActionGroup {
                ToolButton {
                    text: qsTr("Autoscale")
                    enabled: scanView.plot.hasData
                    onClicked: scanView.plot.autoscale()
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Fit the axes to the data (F1, double-click)")
                }
            }
            Item { Layout.fillWidth: true }
        }

        // … and the views as tabs on their own row, with the current channel's run info.
        RowLayout {
            visible: !win.compact
            Layout.fillWidth: true
            Layout.leftMargin: 6
            spacing: 8
            Item {
                id: wideTabSlot
                implicitWidth: viewTabs.implicitWidth
                implicitHeight: viewTabs.implicitHeight
            }
            Item { Layout.fillWidth: true }
            Label {
                text: ctrl.runInfo
                elide: Text.ElideLeft
                Layout.maximumWidth: win.width * 0.55
                rightPadding: 10
                opacity: 0.75
            }
        }

        Item {
            id: compactTabSlot
            visible: win.compact && !win.landscapePhone
            Layout.fillWidth: true
            implicitHeight: viewTabs.implicitHeight
        }
      }
    }

    // One instance, placed in the toolbar row (desktop) or as a full-width row (phone).
    TabBar {
        id: viewTabs
        parent: !win.compact ? wideTabSlot : win.landscapePhone ? landscapeTabSlot : compactTabSlot
        anchors.fill: parent
        // Inside the toolbar, Material (Android) would hand the toolbar's white text to the tabs.
        Mat.Material.foreground: win.palette.windowText
        readonly property int tabCount: 2 + (ctrl.surfaceSupported ? 1 : 0) + (ctrl.hasScanTiming ? 1 : 0)
        // Phone: equal tabs over the slot (not viewTabs.width, which depends on the tabs).
        readonly property real compactTabWidth: (win.landscapePhone ? landscapeTabSlot.width : win.width) / tabCount
        TabButton { text: qsTr("Scans"); width: win.compact ? viewTabs.compactTabWidth : implicitWidth }
        TabButton { text: win.compact ? qsTr("Run") : qsTr("Run conditions"); width: win.compact ? viewTabs.compactTabWidth : implicitWidth }
        TabButton {
            text: win.compact ? qsTr("3D") : qsTr("3D surface")
            visible: ctrl.surfaceSupported
            width: !visible ? 0 : win.compact ? viewTabs.compactTabWidth : implicitWidth
        }
        TabButton {
            text: win.compact ? qsTr("Timing") : qsTr("Scan timing")
            visible: ctrl.hasScanTiming
            width: !visible ? 0 : win.compact ? viewTabs.compactTabWidth : implicitWidth
        }
    }

    // Phone: channel list and options as drawers (opened from the toolbar; no edge swipe,
    // which would collide with panning the plot and the system back gesture).
    Drawer {
        id: channelDrawer
        edge: Qt.LeftEdge
        width: Math.min(win.width * 0.85, 380)
        height: win.height
        dragMargin: 0
    }
    Drawer {
        id: optionsDrawer
        edge: Qt.RightEdge
        Mat.Material.elevation: 0  // Qt 6.8 Material: the elevation layer leaves a right-edge drawer transparent
        width: Math.min(win.width * 0.92, 420)
        height: win.height
        dragMargin: 0
    }

    SplitView {
        anchors.fill: parent
        // Room for the edge tab of a floating sidebar, so it does not cover the axis labels.
        anchors.leftMargin: leftFloat.active ? leftFloat.tabWidth + 2 : 0
        anchors.rightMargin: rightFloat.active ? rightFloat.tabWidth + 2 : 0
        orientation: Qt.Horizontal

        Item {
            id: channelPane
            visible: !win.compact && layoutSettings.leftPinned
            SplitView.preferredWidth: 270
            SplitView.minimumWidth: 160
            FileList {
                parent: win.compact ? channelDrawer.contentItem
                      : layoutSettings.leftPinned ? channelPane : leftFloat.holder
                anchors.fill: parent
                controller: ctrl
                showPin: !win.compact
                pinned: layoutSettings.leftPinned
                onPinToggled: win.togglePinned(true)
            }
        }

        StackLayout {
            id: viewStack
            SplitView.fillWidth: true
            currentIndex: viewTabs.currentIndex

        SplitView {
            id: plotArea
            orientation: Qt.Vertical

            PlotView {
                id: scanView
                curvesSelectable: true
                SplitView.fillHeight: true
                SplitView.minimumHeight: 200
                xLabel: ctrl.xLabel
                yLabel: ctrl.yLabel
                placeholder: ctrl.processingError !== "" ? ctrl.processingError
                             : win.compact ? qsTr("Open files or a data folder from the menu (top right).\nDrag: pan · Pinch: zoom · Long press + drag: zoom box\nDouble tap: autoscale · Tap: select curve")
                             : qsTr("Open .auc/.mwrs/.mw/XL files or a data folder.\nWheel: zoom · Drag: pan · Right-drag/Ctrl+drag: zoom box · Double-click/F1: autoscale · Click: select curve\nCtrl+←/→: previous/next wavelength")
                markers: {
                    var m = []
                    if (ctrl.offsetMode === 1)
                        m.push({ value: ctrl.offsetR1, color: "#d97706", label: qsTr("offset"), key: "offR1" })
                    if (ctrl.offsetMode === 2) {
                        m.push({ value: ctrl.offsetR1, color: "#d97706", label: qsTr("baseline"), key: "offR1" })
                        m.push({ value: ctrl.offsetR2, color: "#d97706", label: "", key: "offR2" })
                    }
                    if (!ctrl.dataIsAbsorbance && ctrl.displayMode === 1 && ctrl.referenceMode === 2) {
                        m.push({ value: ctrl.refR1, color: "#7c3aed", label: qsTr("I₀"), key: "refR1" })
                        m.push({ value: ctrl.refR2, color: "#7c3aed", label: "", key: "refR2" })
                    }
                    if (ctrl.integrate) {
                        m.push({ value: ctrl.intR1, color: "#1d4ed8", label: qsTr("∫"), key: "intR1" })
                        m.push({ value: ctrl.intR2, color: "#1d4ed8", label: "", key: "intR2" })
                    }
                    if (ctrl.isInterference && ctrl.fringeJumps) {
                        const set = ctrl.fringeR2 > ctrl.fringeR1
                        const ra = set ? ctrl.fringeR1 : (ctrl.fringeInfo.r1 ?? 0)
                        const rb = set ? ctrl.fringeR2 : (ctrl.fringeInfo.r2 ?? 0)
                        if (rb > ra) {
                            m.push({ value: ra, color: "#0891b2", label: qsTr("meniscus"), key: "fringeR1" })
                            m.push({ value: rb, color: "#9333ea", label: qsTr("bottom"), key: "fringeR2" })
                        }
                        const align = ctrl.fringeAlign > 0 ? ctrl.fringeAlign : (ctrl.fringeInfo.alignment ?? 0)
                        if (align > 0)
                            m.push({ value: align, color: "#e11d48", label: qsTr("alignment"), key: "fringeAlign" })
                    }
                    if (ctrl.showSpectrum && ctrl.hasSpectra)
                        m.push({ value: ctrl.spectrumRadius, color: "#059669", label: qsTr("spectrum"), key: "specR" })
                    return m
                }
                onMarkerMoved: (key, value) => {
                    if (key === "specR") ctrl.spectrumRadius = value
                    else if (key === "offR1") ctrl.offsetR1 = value
                    else if (key === "offR2") ctrl.offsetR2 = value
                    else if (key === "refR1") ctrl.refR1 = value
                    else if (key === "refR2") ctrl.refR2 = value
                    else if (key === "intR1") ctrl.intR1 = value
                    else if (key === "intR2") ctrl.intR2 = value
                    else if (key === "fringeR1" || key === "fringeR2") {
                        // Dragging an automatic limit fixes both at their current positions.
                        const set = ctrl.fringeR2 > ctrl.fringeR1
                        const ra = key === "fringeR1" ? value : (set ? ctrl.fringeR1 : (ctrl.fringeInfo.r1 ?? 0))
                        const rb = key === "fringeR2" ? value : (set ? ctrl.fringeR2 : (ctrl.fringeInfo.r2 ?? 0))
                        ctrl.fringeR1 = Math.min(ra, rb)
                        ctrl.fringeR2 = Math.max(ra, rb)
                    }
                    else if (key === "fringeAlign") ctrl.fringeAlign = value
                }

                ColorBar {
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    anchors.rightMargin: 26
                    anchors.bottomMargin: 54
                    visible: scanView.plot.hasData
                    colormap: ctrl.colormap
                    firstLabel: qsTr("first scan")
                    lastLabel: qsTr("last scan")
                }
            }

            PlotView {
                id: spectrumView
                visible: ctrl.showSpectrum && ctrl.hasSpectra
                curvesSelectable: true
                SplitView.preferredHeight: 260
                SplitView.minimumHeight: 140
                xLabel: qsTr("Wavelength (nm)")
                yLabel: ctrl.yLabel
                placeholder: qsTr("Spectra at r = %1 cm (reading all scans…)").arg(ctrl.spectrumRadius.toFixed(3))
                markers: ctrl.mwa
                         ? [{ value: ctrl.mwaFrom, color: "#c2410c", label: qsTr("MWA"), key: "mwaFrom" },
                            { value: ctrl.mwaTo, color: "#c2410c", label: "", key: "mwaTo" }]
                         : [{ value: ctrl.wavelength, color: "#c2410c", label: ctrl.wavelength.toFixed(0) + " nm", key: "wl" }]
                onMarkerMoved: (key, value) => {
                    if (key === "wl") ctrl.wavelengthIndex = ctrl.wavelengthIndexOf(value)
                    else if (key === "mwaFrom") ctrl.mwaFrom = value
                    else if (key === "mwaTo") ctrl.mwaTo = value
                }
                Label {
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.rightMargin: 24
                    anchors.topMargin: 16
                    visible: spectrumView.plot.hasData
                    text: qsTr("r = %1 ± %2 cm").arg(ctrl.spectrumRadius.toFixed(3)).arg(ctrl.spectrumWidth.toFixed(3))
                    color: "#059669"
                    font.pixelSize: 12
                }
            }

            PlotView {
                id: integralView
                visible: ctrl.integrate
                SplitView.preferredHeight: 230
                SplitView.minimumHeight: 140
                xLabel: qsTr("Time (min)")
                yLabel: ctrl.radialWeight ? qsTr("∫A·r dr (OD·cm²)") : qsTr("∫A dr (OD·cm)")
                placeholder: qsTr("Radial integral per scan")
            }
        }

        RunConditionsView {
            id: runView
            controller: ctrl
        }

        Loader {
            id: surfaceLoader
            // Created on first use: Qt Quick 3D starts only when the tab is opened.
            active: ctrl.surfaceSupported && viewTabs.currentIndex === 2
            Component.onCompleted: if (ctrl.surfaceSupported) setSource(Qt.resolvedUrl("SurfaceView.qml"), { controller: ctrl })
        }

        ScanTimingView {
            id: timingView
            controller: ctrl
        }
        }

        Item {
            id: optionsPane
            visible: !win.compact && layoutSettings.rightPinned
            SplitView.preferredWidth: 300
            SplitView.minimumWidth: 240
            ColumnLayout {
                parent: win.compact ? optionsDrawer.contentItem
                      : layoutSettings.rightPinned ? optionsPane : rightFloat.holder
                anchors.fill: parent
                anchors.margins: win.compact ? 12 : 0
                spacing: 0
                RowLayout {
                    visible: !win.compact
                    Layout.fillWidth: true
                    spacing: 0
                    Label {
                        text: qsTr("Options")
                        font.bold: true
                        padding: 8
                        Layout.fillWidth: true
                    }
                    PinButton {
                        pinned: layoutSettings.rightPinned
                        onClicked: win.togglePinned(false)
                    }
                }
                ScrollView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    contentWidth: availableWidth
                    OptionsPanel {
                        width: parent.width
                        controller: ctrl
                        plot: scanView.plot
                        compact: win.compact
                    }
                }
            }
        }
    }

    // Sidebars: pinned beside the plots, or floating - hidden until the mouse reaches the
    // window edge, then sliding in over the plots and hiding again when the mouse leaves.
    Settings {
        id: layoutSettings
        category: "layout"
        property bool leftPinned: false   // the channel list is needed less often
        property bool rightPinned: true
    }
    function togglePinned(left) {
        if (left) {
            layoutSettings.leftPinned = !layoutSettings.leftPinned
            if (!layoutSettings.leftPinned) leftFloat.reveal()  // stays under the mouse until it leaves
        } else {
            layoutSettings.rightPinned = !layoutSettings.rightPinned
            if (!layoutSettings.rightPinned) rightFloat.reveal()
        }
    }

    // Framed row of tool buttons, so they read as buttons rather than toolbar text.
    component ActionGroup: Rectangle {
        default property alias buttons: actionRow.data
        implicitWidth: actionRow.implicitWidth + 2
        implicitHeight: actionRow.implicitHeight + 2
        radius: 4
        color: win.palette.button
        border.color: win.palette.mid
        RowLayout {
            id: actionRow
            anchors.centerIn: parent
            spacing: 0
        }
    }

    component FloatingSidebar: Item {
        id: fs
        property bool leftEdge: true
        property bool active: false
        property string title
        property real panelWidth: 300
        property bool shown: false
        readonly property alias holder: holder
        /// Width of the edge tab: the rotated title plus padding (the plots leave room for it).
        readonly property real tabWidth: Math.ceil(tabLabel.implicitHeight) + 10
        function reveal() { shown = true; hideTimer.restart() }

        anchors.fill: parent
        visible: active
        z: 20

        // A popup (combo box list, menu, dialog) is open, or the user is typing in the
        // panel: it stays even with the mouse outside.
        function busy() {
            for (const c of Overlay.overlay.children)
                if (c.visible && c.width > 0 && c.height > 0) return true
            const focus = win.activeFocusItem
            if (focus instanceof TextInput)
                for (let it = focus; it; it = it.parent)
                    if (it === holder) return true
            return false
        }
        Timer {
            id: hideTimer
            interval: 700
            onTriggered: {
                if (panelHover.hovered || edgeHover.hovered) return
                if (fs.busy()) { restart(); return }
                fs.shown = false
            }
        }

        // Hot zone along the window edge, and a small tab showing that the sidebar is there.
        Item {
            width: 6
            height: parent.height
            x: fs.leftEdge ? 0 : parent.width - width
            HoverHandler {
                id: edgeHover
                onHoveredChanged: if (hovered) fs.reveal()
            }
        }
        Rectangle {
            visible: !fs.shown
            width: fs.tabWidth
            height: tabLabel.implicitWidth + 24
            radius: 4
            x: fs.leftEdge ? 0 : parent.width - width
            y: (parent.height - height) / 2
            color: win.palette.button
            border.color: win.palette.mid
            Label {
                id: tabLabel
                anchors.centerIn: parent
                rotation: fs.leftEdge ? 90 : -90
                text: fs.title
                font.pixelSize: 11
                opacity: 0.8
            }
            HoverHandler { onHoveredChanged: if (hovered) fs.reveal() }
            TapHandler { onTapped: fs.reveal() }
        }

        Rectangle {
            width: Math.min(fs.panelWidth, fs.width * 0.8)
            height: parent.height
            x: fs.leftEdge ? (fs.shown ? 0 : -width - 12) : (fs.shown ? fs.width - width : fs.width + 12)
            Behavior on x { NumberAnimation { duration: 150; easing.type: Easing.OutCubic } }
            color: win.palette.window
            border.color: win.palette.mid
            // Soft shadow on the inner edge.
            Rectangle {
                width: 10
                height: parent.height
                x: fs.leftEdge ? parent.width : -width
                gradient: Gradient {
                    orientation: Gradient.Horizontal
                    GradientStop { position: 0; color: fs.leftEdge ? Qt.rgba(0, 0, 0, 0.18) : "transparent" }
                    GradientStop { position: 1; color: fs.leftEdge ? "transparent" : Qt.rgba(0, 0, 0, 0.18) }
                }
            }
            Item {
                id: holder
                anchors.fill: parent
                anchors.margins: 1
            }
            HoverHandler {
                id: panelHover
                onHoveredChanged: if (hovered) hideTimer.stop(); else hideTimer.restart()
            }
        }
    }

    FloatingSidebar {
        id: leftFloat
        leftEdge: true
        title: qsTr("Channels")
        panelWidth: 300
        active: !win.compact && !layoutSettings.leftPinned
    }
    FloatingSidebar {
        id: rightFloat
        leftEdge: false
        title: qsTr("Options")
        panelWidth: 320
        active: !win.compact && !layoutSettings.rightPinned
    }

    footer: ToolBar {
        visible: !win.landscapePhone
        topPadding: 1
        bottomPadding: 1
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 8
            anchors.rightMargin: 8
            BusyIndicator { running: ctrl.busy; Layout.preferredHeight: 18; Layout.preferredWidth: 18 }
            Label { text: ctrl.status; elide: Text.ElideRight; Layout.fillWidth: true }
            Label {
                visible: scanView.plot.hasData && !win.compact
                text: qsTr("%1 scans · %2 vertices · processed in %3 ms")
                      .arg(scanView.plot.curveCount)
                      .arg(scanView.plot.vertexCount.toLocaleString(Qt.locale(), "f", 0))
                      .arg(win.lastProcessMs.toFixed(1))
                opacity: 0.7
            }
        }
    }
}

// Low-latency viewer for a USB capture device (Genki ShadowCast): the camera
// feed goes straight to an AVCaptureVideoPreviewLayer (no decode/encode or
// frame queue in between) and its audio to an AVCaptureAudioPreviewOutput.
// Build the app: sh wii/build_shadowcast_view.sh -> build/ShadowCast Viewer.app
// (device-name substring as the first argument, default "ShadowCast").
// Window size and position, aspect and capture size are remembered. The
// ShadowCast offers every size at 60 fps but delivers only 30 above 1280 x 720,
// so the menu shows the real rate; 720p (60 fps; the Wii's 480p loses nothing)
// is the default.
// View menu: 16:9 (Cmd-1) or 4:3 (Cmd-2), Smooth (Cmd-3) or Sharp (Cmd-4)
// scaling, Full Screen. Audio menu: a volume slider, Volume Up (Cmd-=) and
// Down (Cmd--) in 10% steps, Mute (Cmd-M); the volume is remembered. Resolution
// menu: every capture size the device offers (at its best frame rate). Keys without
// Cmd also work: A switch aspect, F full screen, M mute, + and - volume, Q quit.
// The capture frame is always 16:9; the Wii's own aspect setting decides what
// the picture should be: 16:9 shows it as is, 4:3 squeezes it. The signal
// cannot tell them apart, so the choice is remembered (default 16:9).
import AVFoundation
import AppKit

let wanted = CommandLine.arguments.count > 1 ? CommandLine.arguments[1] : "ShadowCast"

func device(_ type: AVMediaType) -> AVCaptureDevice? {
    let kinds: [AVCaptureDevice.DeviceType] = type == .video ? [.external, .builtInWideAngleCamera] : [.microphone, .external]
    let found = AVCaptureDevice.DiscoverySession(deviceTypes: kinds, mediaType: type, position: .unspecified).devices
    return found.first { $0.localizedName.localizedCaseInsensitiveContains(wanted) }
}

final class View: NSView {
    let preview: AVCaptureVideoPreviewLayer
    var onKey: (String) -> Void = { _ in }
    init(session: AVCaptureSession) {
        preview = AVCaptureVideoPreviewLayer(session: session)
        super.init(frame: NSRect(x: 0, y: 0, width: 960, height: 720))
        wantsLayer = true
        layer = CALayer()
        layer!.backgroundColor = NSColor.black.cgColor
        preview.videoGravity = .resize
        preview.frame = bounds
        preview.autoresizingMask = [.layerWidthSizable, .layerHeightSizable]
        layer!.addSublayer(preview)
    }
    required init?(coder: NSCoder) { fatalError() }
    override var acceptsFirstResponder: Bool { true }
    override func keyDown(with event: NSEvent) { onKey(event.charactersIgnoringModifiers?.lowercased() ?? "") }
}

final class App: NSObject, NSApplicationDelegate, NSWindowDelegate {
    let session = AVCaptureSession()
    /* Audio in its own session: with an audio input in the video session,
     * the preview is timed by the capture device's audio clock, which drifts
     * against the display, so the picture falls further behind over time. */
    let audioSession = AVCaptureSession()
    var video: AVCaptureDevice!
    var formatItems: [NSMenuItem] = []
    let audioOut = AVCaptureAudioPreviewOutput()
    /* Real-time display: keep App Nap and timer throttling away, which
     * otherwise slow a window that is not frontmost or seems idle. */
    var activity: NSObjectProtocol?
    var window: NSWindow!

    func applicationDidFinishLaunching(_ note: Notification) {
        activity = ProcessInfo.processInfo.beginActivity(options: [.userInitiated, .latencyCritical, .idleDisplaySleepDisabled],
                                                         reason: "Live capture display")
        guard let found = device(.video) else {
            print("No video device matching \"\(wanted)\""); exit(1)
        }
        video = found
        session.beginConfiguration()
        session.sessionPreset = .high
        if let input = try? AVCaptureDeviceInput(device: video), session.canAddInput(input) { session.addInput(input) }
        let saved = UserDefaults.standard.string(forKey: "format")
        let fallback = formats().first { label($0) == "1280 x 720 @ 60 fps" }
        applyFormat(formats().first { label($0) == saved } ?? fallback ?? formats().first!)
        session.commitConfiguration()
        if let mic = device(.audio), let input = try? AVCaptureDeviceInput(device: mic) {
            audioSession.beginConfiguration()
            if audioSession.canAddInput(input) { audioSession.addInput(input) }
            if audioSession.canAddOutput(audioOut) { audioSession.addOutput(audioOut) }
            audioSession.commitConfiguration()
        }

        let view = View(session: session)
        self.view = view
        view.frame = NSRect(x: 0, y: 0, width: 1280, height: 720)
        view.onKey = { [weak self] key in self?.key(key, view) }
        window = NSWindow(contentRect: view.frame, styleMask: [.titled, .closable, .resizable, .miniaturizable], backing: .buffered, defer: false)
        window.contentView = view
        buildMenu()
        setScaling(UserDefaults.standard.string(forKey: "scaling") ?? "Smooth")
        applyVolume()
        /* Frame saved by hand (any display, including a secondary one left
         * of the main): applied after the window is on screen, where AppKit's
         * autosave would first constrain it to the main display. */
        let savedFrame = UserDefaults.standard.string(forKey: "frame").map(NSRectFromString)
        setAspect(UserDefaults.standard.string(forKey: "aspect") ?? "16:9", resize: savedFrame == nil)
        if savedFrame == nil { window.center() }
        window.makeKeyAndOrderFront(nil)
        if let savedFrame, NSScreen.screens.contains(where: { $0.frame.intersects(savedFrame) }) { window.setFrame(savedFrame, display: true) }
        window.delegate = self
        window.makeFirstResponder(view)
        NSApp.activate(ignoringOtherApps: true)
        DispatchQueue.global(qos: .userInteractive).async { self.startVideo(); self.audioSession.startRunning() }
    }

    /* startRunning resets the device to the session preset's frame rate
     * (25 fps here) unless the device is locked for configuration across it. */
    func startVideo() {
        let locked = (try? video.lockForConfiguration()) != nil
        session.startRunning()
        if locked { video.unlockForConfiguration() }
    }

    var aspect = "16:9"
    var aspectItems: [String: NSMenuItem] = [:]
    var scalingItems: [String: NSMenuItem] = [:]
    weak var view: View?
    @objc func chooseScaling(_ sender: NSMenuItem) { setScaling(sender.title) }
    /* Smooth: bilinear stretch. Sharp: nearest neighbour, crisp pixels. */
    func setScaling(_ mode: String) {
        let filter: CALayerContentsFilter = mode == "Sharp" ? .nearest : .linear
        view?.preview.magnificationFilter = filter
        view?.preview.minificationFilter = filter
        UserDefaults.standard.set(mode, forKey: "scaling")
        for (title, item) in scalingItems { item.state = title == mode ? .on : .off }
        updateTitle()
    }
    @objc func chooseAspect(_ sender: NSMenuItem) { setAspect(sender.title, resize: true) }

    /* AVCaptureAudioPreviewOutput plays at 0...1 (no boost past the
     * device's own level); mute keeps the volume to come back to. */
    var volume: Float = UserDefaults.standard.object(forKey: "volume") == nil ? 1 : UserDefaults.standard.float(forKey: "volume")
    var muted = false
    var muteItem: NSMenuItem?
    var volumeSlider: NSSlider?
    func setVolume(_ v: Float) {
        volume = min(max((v * 100).rounded() / 100, 0), 1)
        muted = false
        UserDefaults.standard.set(volume, forKey: "volume")
        applyVolume()
    }
    func applyVolume() {
        audioOut.volume = muted ? 0 : volume
        muteItem?.state = muted ? .on : .off
        volumeSlider?.floatValue = volume * 100
        updateTitle()
    }
    @objc func toggleMute(_ sender: Any?) { muted.toggle(); applyVolume() }
    @objc func volumeUp(_ sender: Any?) { setVolume(volume + 0.1) }
    @objc func volumeDown(_ sender: Any?) { setVolume(volume - 0.1) }
    @objc func slideVolume(_ sender: NSSlider) { setVolume(sender.floatValue / 100) }

    func buildMenu() {
        let bar = NSMenu()
        let appItem = NSMenuItem(); bar.addItem(appItem)
        let appMenu = NSMenu()
        appMenu.addItem(withTitle: "Quit", action: #selector(NSApplication.terminate(_:)), keyEquivalent: "q")
        appItem.submenu = appMenu
        let viewItem = NSMenuItem(); bar.addItem(viewItem)
        let viewMenu = NSMenu(title: "View")
        for (title, key) in [("16:9", "1"), ("4:3", "2")] {
            let item = viewMenu.addItem(withTitle: title, action: #selector(chooseAspect(_:)), keyEquivalent: key)
            item.target = self
            aspectItems[title] = item
        }
        viewMenu.addItem(.separator())
        for (title, key) in [("Smooth", "3"), ("Sharp", "4")] {
            let item = viewMenu.addItem(withTitle: title, action: #selector(chooseScaling(_:)), keyEquivalent: key)
            item.target = self
            scalingItems[title] = item
        }
        viewMenu.addItem(.separator())
        viewMenu.addItem(withTitle: "Enter Full Screen", action: #selector(NSWindow.toggleFullScreen(_:)), keyEquivalent: "f")
        viewMenu.addItem(.separator())
        viewMenu.addItem(withTitle: "Restart Capture", action: #selector(restartCapture(_:)), keyEquivalent: "r").target = self
        viewItem.submenu = viewMenu
        let audioItem = NSMenuItem(); bar.addItem(audioItem)
        let audioMenu = NSMenu(title: "Audio")
        let sliderView = NSView(frame: NSRect(x: 0, y: 0, width: 220, height: 28))
        let slider = NSSlider(value: Double(volume * 100), minValue: 0, maxValue: 100, target: self, action: #selector(slideVolume(_:)))
        slider.frame = NSRect(x: 20, y: 4, width: 184, height: 20)
        slider.isContinuous = true
        sliderView.addSubview(slider)
        volumeSlider = slider
        let sliderItem = NSMenuItem(); sliderItem.view = sliderView
        audioMenu.addItem(sliderItem)
        audioMenu.addItem(withTitle: "Volume Up", action: #selector(volumeUp(_:)), keyEquivalent: "=").target = self
        audioMenu.addItem(withTitle: "Volume Down", action: #selector(volumeDown(_:)), keyEquivalent: "-").target = self
        audioMenu.addItem(.separator())
        let mute = audioMenu.addItem(withTitle: "Mute", action: #selector(toggleMute(_:)), keyEquivalent: "m")
        mute.target = self
        muteItem = mute
        audioItem.submenu = audioMenu
        let resItem = NSMenuItem(); bar.addItem(resItem)
        let resMenu = NSMenu(title: "Resolution")
        for f in formats() {
            let item = resMenu.addItem(withTitle: label(f), action: #selector(chooseFormat(_:)), keyEquivalent: "")
            item.target = self
            item.state = f == video.activeFormat ? .on : .off
            formatItems.append(item)
        }
        resItem.submenu = resMenu
        NSApp.mainMenu = bar
    }

    /* Each size once, at its highest frame rate, largest first. */
    func formats() -> [AVCaptureDevice.Format] {
        var best: [String: AVCaptureDevice.Format] = [:]
        for f in video.formats {
            let d = CMVideoFormatDescriptionGetDimensions(f.formatDescription)
            let key = "\(d.width)x\(d.height)"
            if let b = best[key], maxRate(b) >= maxRate(f) { continue }
            best[key] = f
        }
        return best.values.sorted {
            let a = CMVideoFormatDescriptionGetDimensions($0.formatDescription), b = CMVideoFormatDescriptionGetDimensions($1.formatDescription)
            return (a.width * a.height, maxRate($0)) > (b.width * b.height, maxRate($1))
        }
    }
    func maxRate(_ f: AVCaptureDevice.Format) -> Double { f.videoSupportedFrameRateRanges.map(\.maxFrameRate).max() ?? 0 }
    /* The rate frames actually arrive at: measured, 30 fps above 1280 x 720. */
    func realRate(_ f: AVCaptureDevice.Format) -> Double {
        let d = CMVideoFormatDescriptionGetDimensions(f.formatDescription)
        return d.width * d.height > 1280 * 720 ? min(maxRate(f), 30) : maxRate(f)
    }
    func label(_ f: AVCaptureDevice.Format) -> String {
        let d = CMVideoFormatDescriptionGetDimensions(f.formatDescription)
        return "\(d.width) x \(d.height) @ \(Int(realRate(f).rounded())) fps"
    }
    func applyFormat(_ f: AVCaptureDevice.Format) {
        guard (try? video.lockForConfiguration()) != nil else { return }
        video.activeFormat = f
        if let range = f.videoSupportedFrameRateRanges.max(by: { $0.maxFrameRate < $1.maxFrameRate }) {
            video.activeVideoMinFrameDuration = range.minFrameDuration
            video.activeVideoMaxFrameDuration = range.minFrameDuration
        }
        video.unlockForConfiguration()
        UserDefaults.standard.set(label(f), forKey: "format")
        for item in formatItems { item.state = item.title == label(f) ? .on : .off }
        updateTitle()
    }
    @objc func chooseFormat(_ sender: NSMenuItem) {
        if let f = formats().first(where: { label($0) == sender.title }) { applyFormat(f) }
    }

    /* "ShadowCast — 16:9 — 1920 x 1080 @ 60 fps — Smooth — Volume 80%" */
    func updateTitle() {
        guard let window else { return }
        let scaling = UserDefaults.standard.string(forKey: "scaling") ?? "Smooth"
        let sound = muted ? "Muted" : "Volume \(Int((volume * 100).rounded()))%"
        window.title = [video.localizedName, aspect, label(video.activeFormat), scaling, sound].joined(separator: " — ")
    }

    /* Stop and start the session: drops anything queued in the capture or
     * audio pipeline without relaunching. */
    @objc func restartCapture(_ sender: Any?) {
        DispatchQueue.global(qos: .userInteractive).async {
            self.session.stopRunning(); self.audioSession.stopRunning()
            self.startVideo(); self.audioSession.startRunning()
        }
    }

    func setAspect(_ a: String, resize: Bool) {
        aspect = a
        for (title, item) in aspectItems { item.state = title == a ? .on : .off }
        UserDefaults.standard.set(a, forKey: "aspect")
        let ratio = a == "4:3" ? NSSize(width: 4, height: 3) : NSSize(width: 16, height: 9)
        window.contentAspectRatio = ratio
        if resize && !window.styleMask.contains(.fullScreen) {
            let w = window.contentLayoutRect.width
            window.setContentSize(NSSize(width: w, height: (w * ratio.height / ratio.width).rounded()))
        }
        updateTitle()
    }

    func key(_ key: String, _ view: View) {
        switch key {
        case "f": window.toggleFullScreen(nil)
        case "a": setAspect(aspect == "16:9" ? "4:3" : "16:9", resize: true)
        case "m": toggleMute(nil)
        case "=", "+": volumeUp(nil)
        case "-", "_": volumeDown(nil)
        case "q": NSApp.terminate(nil)
        default: break
        }
    }

    func saveFrame() {
        if !window.styleMask.contains(.fullScreen) { UserDefaults.standard.set(NSStringFromRect(window.frame), forKey: "frame") }
    }
    func windowDidMove(_ note: Notification) { saveFrame() }
    func windowDidEndLiveResize(_ note: Notification) { saveFrame() }

    func applicationShouldTerminateAfterLastWindowClosed(_ app: NSApplication) -> Bool { true }
}

let app = NSApplication.shared
app.setActivationPolicy(.regular)
let delegate = App()
app.delegate = delegate
app.run()

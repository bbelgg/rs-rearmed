// RS Re-ARMed Installer — a native macOS app that builds and installs the game
// following the steps in docs/mac/INSTALL.md, with a progress bar and live log.
//
// Build:  tools/installer/build.sh
// Requires macOS 12+, Apple Silicon.
//
// When run from inside the repository it builds in place. When run standalone
// (e.g. a binary GitHub release) it downloads the source of its release into a build folder the user
// can choose (default ~/Library/Caches/rs-rearmed-build), builds there, and
// optionally cleans up afterwards.

import AppKit

let repoWebURL = "https://github.com/bbelgg/rs-rearmed"
// RS_SOURCE_URL: another archive (a test of the standalone path before the release is published).
func sourceArchiveURL(_ tag: String) -> String {
    ProcessInfo.processInfo.environment["RS_SOURCE_URL"] ?? "\(repoWebURL)/archive/refs/tags/\(tag).tar.gz"
}
// What the source this installer was built from adds up to: every file's SHA-256 and every link's target, by
// path. tools/installer/build.sh runs this same line (it reads it from here) over `git archive HEAD` and
// builds the result in as rsSourceDigest; downloadSource runs it over what it downloaded.
let sourceDigestCommand = #"export LC_ALL=C; { find . -type l -exec sh -c 'printf "%s -> %s\n" "$1" "$(readlink "$1")"' _ {} \; | sort; find . -type f -print0 | sort -z | xargs -0 shasum -a 256; } | shasum -a 256 | cut -d' ' -f1"#
let releasesURL = "https://api.github.com/repos/bbelgg/rs-rearmed/releases/latest"
// No spaces: the recompiler's build scripts pass lists of paths as space-separated strings.
let buildFolderName = "rs-rearmed-build"
let oldBuildFolderName = "RS Re-ARMed Build"  // 1.0.0-1.0.2; a saved path ending in it gets the new name
let defaultBuildBase = NSHomeDirectory() + "/Library/Caches/" + buildFolderName
let buildBaseDefaultsKey = "BuildFolder"
let launcherApp = NSHomeDirectory() + "/Applications/RS Re-ARMed.app"  // made by tools/play-native/install.sh
let nativeHome = NSHomeDirectory() + "/Library/Application Support/RS Re-ARMed"  // install.sh's folder
// The renderer (install.sh's RS_RENDERER, written to <nativeHome>/renderer): "metal" or "opengl".
let rendererDefaultsKey = "Renderer"
let rendererChoices: [(title: String, value: String)] = [("Metal (recommended)", "metal"), ("OpenGL", "opengl")]
let logsDir = NSHomeDirectory() + "/Library/Logs/RS Re-ARMed Installer"  // kept when build files are deleted
// The game to build from: the Mac game, or the Windows game, Learn & Play, for Macs without the Mac game
// (win-x86/install/lp_install.py gets the game and its DLLs, builds and installs the app).
let gameDefaultsKey = "Game"
let gameMac = "Rocksmith 2014 for Mac"
let gameLP = "Rocksmith 2014 Learn & Play"
let gameChoices: [(title: String, value: String)] = [
    (gameMac, "mac"), ("Learn & Play (the Windows game)", "learnplay")]
// Standard library Python and macOS's own tools only (no venv, no Rosetta).
let lpInstall = "python3 -I win-x86/install/lp_install.py"
let gameFilesTool = "python3 -I tools/play-native/gamefiles.py"
// The offline unpacker's seed (win-x86/unpack): in the release's app bundle, never in git (build.sh).
func bundledSeed() -> [String: String] {
    guard let path = Bundle.main.path(forResource: "seed", ofType: "bin") else { return [:] }
    return ["LP_SEED": path]
}
// RS_XCODE_SELECT: a stand-in, to test the walk through Apple's installer.
let xcodeSelect = ProcessInfo.processInfo.environment["RS_XCODE_SELECT"] ?? "/usr/bin/xcode-select"
let xcodeBuild = ProcessInfo.processInfo.environment["RS_XCODEBUILD"] ?? "/usr/bin/xcodebuild"
// What a macOS privacy question looks like from here: a file that can't be read, with the reason macOS gives.
let deniedByMacOS = "Operation not permitted"
let deniedHelp = "macOS is not letting the installer use that folder. In System Settings, open Privacy & Security, "
    + "then Files & Folders, and turn on the folder or Removable Volumes for RS Re-ARMed Installer. Then click Try Again."
// Seconds without any output before the box says macOS may be waiting for an answer.
let quietAfter = 120.0
let steamBundleID = "com.valvesoftware.steam"
let steamDownloadPage = "https://store.steampowered.com/about/"
let lpDLLDir = "build/win-x86/dlls"  // in the source folder
let lpImage = "build/win-x86/image/Rocksmith2014.exe"  // the code image (lp_image.py), in the source folder
let lpWork = "build/win-x86/lp"  // lp_install.py build's folder; its translated libraries stay as a cache
let lpApp = NSHomeDirectory() + "/Applications/RS Re-ARMed Learn & Play.app"  // lp_install.py app-install
let lpHome = nativeHome + "/learn-play"  // lp_install.py app-install's folder (settings, saves, game files)
// Each edition's own copy of the game files (tools/play-native/gamefiles.py), which the game reads instead of
// Steam's folders; "Refresh Game Files" syncs it again. The copy's tool is installed next to it.
func gameCopy(_ game: String) -> String { (game == "learnplay" ? lpHome : nativeHome) + "/game" }
func gameCopyTool(_ game: String) -> String { (game == "learnplay" ? lpHome : nativeHome) + "/gamefiles.py" }
// The built-in mods switched off (Built-in Mods…), which the edition's launcher reads at every start.
func settingsFile(_ game: String) -> String { (game == "learnplay" ? lpHome : nativeHome) + "/settings.env" }
func gameCopyExists(_ game: String) -> Bool {
    (FileManager.default.fileExists(atPath: gameCopy(game) + "/.rearmed/source.json") || gameCopyElsewhere(game) != nil)
        && FileManager.default.fileExists(atPath: gameCopyTool(game))
}
// A game on another drive: the copy is on that drive too and gameCopy is a link to it (gamefiles.py).
func gameCopyElsewhere(_ game: String) -> String? {
    guard let t = try? FileManager.default.destinationOfSymbolicLink(atPath: gameCopy(game)) else { return nil }
    return t.hasPrefix("/") ? t : ((gameCopy(game) as NSString).deletingLastPathComponent as NSString).appendingPathComponent(t)
}
// The name of that drive when it is not connected (the link leads nowhere), else nil.
func gameCopyDriveMissing(_ game: String) -> String? {
    guard let t = gameCopyElsewhere(game), !FileManager.default.fileExists(atPath: t), t.hasPrefix("/Volumes/")
    else { return nil }
    let name = String(t.dropFirst("/Volumes/".count).split(separator: "/").first ?? "")
    let mounted = (try? URL(fileURLWithPath: "/Volumes/" + name).resourceValues(forKeys: [.isVolumeKey]))?.isVolume ?? false
    return name.isEmpty || mounted ? nil : name
}
// For the messages: where the copy is when it is on another drive.
func gameCopyElsewhereNote(_ game: String) -> String {
    guard let t = gameCopyElsewhere(game) else { return "" }
    return " The game files themselves are on the drive with the game, in \(t) (the folder above leads there): " +
        "to uninstall, delete that folder too."
}

// Where the copy is kept when the game is on another drive (gamefiles.py's RS_GAME_COPY_DIR): on that drive as
// clones, or on this Mac as a real copy. An install or a refresh moves a copy that is in the other place.
let gameCopyPlaceEnv = "RS_GAME_COPY_DIR"
let gameCopyPlaces: [(title: String, value: String)] = [
    ("On the game's drive (no extra space)", "drive"),
    ("On this Mac (plays without that drive)", "home")]
let gameCopyPlaceDefaultsKey = "gameCopyPlace"
// A folder of the user's own choosing there (launchctl setenv): the installer leaves it alone.
func gameCopyPlaceFixed() -> Bool {
    let v = (ProcessInfo.processInfo.environment[gameCopyPlaceEnv] ?? "").trimmingCharacters(in: .whitespaces).lowercased()
    return !v.isEmpty && !gameCopyPlaces.contains(where: { $0.value == v })
}
// Where the edition's copy is now, else what was chosen last time, else the game's drive.
func gameCopyPlaceNow(_ game: String) -> String {
    if gameCopyElsewhere(game) != nil { return "drive" }
    if FileManager.default.fileExists(atPath: gameCopy(game) + "/.rearmed/source.json") { return "home" }
    let saved = UserDefaults.standard.string(forKey: gameCopyPlaceDefaultsKey)
    return gameCopyPlaces.contains(where: { $0.value == saved }) ? saved! : "drive"
}

// What macOS asks the game, for the last window of the install: both editions the same.
func macOSQuestions(_ game: String) -> String {
    let drive = gameCopyElsewhere(game) == nil ? "" : "The game files are on another drive, so macOS asks too whether "
        + "the game may use that disk: click Allow, or the game can't start. "
    return drive + "When the game first listens for your guitar, macOS asks for microphone access: click Allow, or "
        + "there is no guitar sound. Clicked Don't Allow? Turn it on in System Settings, Privacy & Security, under "
        + "Microphone or Files & Folders."
}

// A word for bash, quoted.
func shq(_ s: String) -> String { "'" + s.replacingOccurrences(of: "'", with: "'\\''") + "'" }

// MARK: - Progress lines

// "progress F LABEL" lines on a command's stdout (build-m4.sh, warm-shaders.sh): F is the fraction
// of the current step. Parsed apart from the UI, so `--parse-progress FILE` can check it.
struct ProgressLine {
    let fraction: Double
    let label: String

    static func parse(_ line: Substring) -> ProgressLine? {
        let parts = line.split(separator: " ", maxSplits: 2)
        guard parts.count == 3, parts[0] == "progress", let f = Double(parts[1]) else { return nil }
        return ProgressLine(fraction: min(max(f, 0), 1), label: String(parts[2]))
    }

    // The log line: "→ Linking the app…", "→ Preparing shaders… 120 of 3880".
    var logText: String { label.contains(where: { $0.isNumber }) ? "→ \(label)" : "→ \(label)…" }

    // What the step label shows: the label without the step's own title ("Preparing shaders… 120
    // of 3880" in the step "Preparing shaders" -> "120 of 3880").
    func detail(inStep title: String) -> String {
        let prefix = title + "… "
        return label.hasPrefix(prefix) ? String(label.dropFirst(prefix.count)) : label
    }
}

// Which progress lines go to the log: a new label, and for a counting label ("… 120 of 3880") also
// every further tenth of the step, rather than every count.
struct ProgressLog {
    private var lastKey = ""
    private var lastTenth = -1

    mutating func shouldLog(_ p: ProgressLine) -> Bool {
        let key = String(p.label.filter { !$0.isNumber })
        let tenth = Int(p.fraction * 10)
        if key != lastKey {
            lastKey = key
            lastTenth = tenth
            return true
        }
        if key != p.label && tenth > lastTenth {
            lastTenth = tenth
            return true
        }
        return false
    }

    mutating func reset() { self = ProgressLog() }
}

// `RS Re-ARMed Installer --parse-progress FILE [STEP]`: feeds FILE (a command's captured output) through
// the parsing above and prints what the step label and the log would show; exit 1 if the fractions go
// backwards or do not end at 1. A check of the step's parsing without the UI, e.g. on the output of
// tools/play-native/warm-shaders.sh.
func parseProgressFile(_ path: String, step: String) -> Int32 {
    guard let text = try? String(contentsOfFile: path, encoding: .utf8) else {
        print("parse-progress: cannot read \(path)")
        return 2
    }
    var log = ProgressLog()
    var last = -1.0, lines = 0, logged = 0, backwards = 0, detail = ""
    for line in text.split(separator: "\n") {
        guard let p = ProgressLine.parse(line) else { continue }
        lines += 1
        if p.fraction < last { backwards += 1 }
        last = p.fraction
        detail = p.detail(inStep: step)
        if log.shouldLog(p) {
            logged += 1
            print("log: \(p.logText)")
        }
    }
    print("parse-progress: \(lines) progress lines, \(logged) logged, last \(last), last label shown: \(detail)")
    let ok = lines > 0 && backwards == 0 && last == 1.0
    print(ok ? "parse-progress: ok" : "parse-progress: FAILED (\(backwards) steps backwards, last fraction \(last))")
    return ok ? 0 : 1
}

// MARK: - Build runner

@MainActor
final class BuildRunner {
    var currentStep = 0
    var totalSteps = 0
    var stepTitle = ""
    var progress: Double = 0
    // Within the current step: what a command reports with "progress F LABEL" lines (build-m4.sh),
    // and when the step started, so a long step visibly moves.
    var stepFraction: Double = 0
    var stepDetail = ""
    var stepStart = Date()
    var progressLog = ProgressLog()
    // The shader warm-up step compiled everything (the first Metal start is quick then).
    var shadersReady = false
    var isRunning = false
    var isFinished = false
    var failure: Error?
    var failed = false
    var log = ""

    var repoRoot = ""
    var gameDir = ""
    var buildBase = defaultBuildBase  // standalone only: holds the source, its build/ and the logs
    var embeddedInRepo = false
    var cleanupAfterInstall = false
    var renderer = "metal"  // rendererChoices' value
    var game = "mac"        // gameChoices' value
    var gameCopyPlace = ""  // gameCopyPlaces' value for the run's commands; empty: the environment's own
    var refreshOnly = false // the run only refreshes the game's own copy of the game files (Refresh Game Files)
    // What the user has to do now, in the box above the log: "guide" lines of lp_install.py and gamefiles.py,
    // the same words in both editions and in a terminal, or the installer's own. Empty code: nothing.
    var guideCode = ""
    var guideTitle = ""
    var guideText = ""
    var guideSeen = 0  // counts new messages, so the window can ask for attention once each
    var lastActivity = Date()  // a command's last output: long silence may mean macOS is asking something
    var stopRequested = false
    var lastError = ""  // the last "error: ..." line of a command: what the failure alert says
    var notes: [String] = []  // "note: ..." lines of a command: said again when the run is done
    var lpStopped = ""  // Learn & Play: why the run stopped before the build (no code image yet), "" if it did not
    var lpUnpinned = false  // Learn & Play: the code image is a capture's, which can differ from the pin
    var lpGameFolder = ""  // the Windows game folder lp_install.py found
    var logFileURL: URL?
    // The command shell() is running, so Skip and Quit can stop it (with what it started).
    var currentProcess: Process?
    var lastOutput = ""  // of the last shell(), also when it failed
    var shadersSkipped = false

    private var logFileHandle: FileHandle?

    // Stops the running command and every process it started (warm-shaders.sh runs the game's
    // executable, which would otherwise keep running after the installer quits).
    func stopCurrentCommand() {
        guard let p = currentProcess, p.isRunning else { return }
        terminateTree(p.processIdentifier)
    }

    func appendLog(_ text: String) {
        let line = text.hasSuffix("\n") ? text : text + "\n"
        log += line
        if let data = line.data(using: .utf8) {
            logFileHandle?.write(data)
        }
    }

    func setGuide(_ code: String, _ title: String, _ text: String) {
        if code != guideCode { guideSeen += 1 }
        guideCode = code
        guideTitle = title
        guideText = text
        if !code.isEmpty { appendLog("→ \(title)") }
    }

    // A command's output: "progress F LABEL" lines move the bar within the step and set the step
    // label (ProgressLog picks the ones that go to the log), "guide" lines fill the box above the log, the
    // rest is logged as is.
    func appendOutput(_ text: String) {
        lastActivity = Date()
        if guideCode == "quiet" { setGuide("", "", "") }
        var rest: [Substring] = []
        for line in text.split(separator: "\n", omittingEmptySubsequences: false) {
            if line.hasPrefix("note: ") {
                notes.append(String(line.dropFirst("note: ".count)))
                rest.append(line)
            } else if line.hasPrefix("guide\t") {
                let f = line.split(separator: "\t", maxSplits: 3).map(String.init)
                if f.count == 4 { setGuide(f[1], f[2], f[3]) } else { setGuide("", "", "") }
            } else if let p = ProgressLine.parse(line) {
                stepFraction = p.fraction
                progress = (Double(currentStep - 1) + stepFraction) / Double(max(totalSteps, 1))
                stepDetail = p.detail(inStep: stepTitle)
                if progressLog.shouldLog(p) {
                    rest.append(Substring(p.logText))
                }
            } else {
                if line.hasPrefix("error: ") {
                    let text = line.dropFirst("error: ".count)
                    lastError = text.prefix(1).uppercased() + text.dropFirst()
                }
                rest.append(line)
            }
        }
        if !rest.isEmpty { appendLog(rest.joined(separator: "\n")) }
    }

    func openLogFile() {
        // ~/Library/Logs: outside the build folder, so the log survives "Delete build files" and shows in Console.
        let dir = URL(fileURLWithPath: logsDir)
        try? FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true)
        let formatter = DateFormatter()
        formatter.dateFormat = "yyyy-MM-dd_HHmmss"
        let name = "install-\(formatter.string(from: Date())).log"
        let url = dir.appendingPathComponent(name)
        FileManager.default.createFile(atPath: url.path, contents: nil)
        logFileHandle = try? FileHandle(forWritingTo: url)
        logFileURL = url
        appendLog("RS Re-ARMed Installer v\(rsVersion) — \(Date())")
        appendLog("Repository: \(repoRoot.isEmpty ? "(will download)" : repoRoot)")
        if !embeddedInRepo { appendLog("Build folder: \(buildBase)") }
        appendLog("Game: \(gameChoices.first(where: { $0.value == game })?.title ?? game)")
        appendLog("Game directory: \(gameDir.isEmpty ? "(Steam's download folder)" : gameDir)")
        appendLog("Embedded in repo: \(embeddedInRepo)")
        appendLog("Log file: \(url.path)\n")
    }

    func closeLogFile() {
        try? logFileHandle?.close()
        logFileHandle = nil
    }

    @discardableResult
    func shell(_ command: String, workDir: String? = nil, env: [String: String]? = nil) async throws -> String {
        appendLog("$ \(command)")
        let process = Process()
        process.executableURL = URL(fileURLWithPath: "/bin/bash")
        process.arguments = ["-c", command]
        process.currentDirectoryURL = URL(fileURLWithPath: workDir ?? repoRoot)

        var environment = ProcessInfo.processInfo.environment
        // macOS's own tools and the Xcode command line tools only (launchd's PATH, what an app opened from
        // the Finder gets), whatever else the user's shell has: CMake and Ninja come from the build's venv.
        environment["PATH"] = "/usr/bin:/bin:/usr/sbin:/sbin"
        if !gameCopyPlace.isEmpty { environment[gameCopyPlaceEnv] = gameCopyPlace }
        if let env = env {
            for (k, v) in env { environment[k] = v }
        }
        process.environment = environment

        let pipe = Pipe()
        process.standardOutput = pipe
        process.standardError = pipe

        let handle = pipe.fileHandleForReading
        var output = ""
        var pending = Data()  // bytes after the last newline: a read can end mid-line or mid-character

        let runner = self
        // Complete lines only (all of it when `flush`), decoded leniently. Only the reader below calls it.
        let take: (Data, Bool) -> String? = { data, flush in
            pending.append(data)
            let end = flush ? pending.endIndex : pending.lastIndex(of: UInt8(ascii: "\n")).map { $0 + 1 }
            guard let end = end, end > pending.startIndex else { return nil }
            let str = String(decoding: pending[pending.startIndex..<end], as: UTF8.self)
            pending = Data(pending[end...])
            output += str
            return str
        }

        try process.run()
        currentProcess = process
        defer { currentProcess = nil }

        // One reader, to the end of the output: nothing can arrive after the result is returned.
        let status: Int32 = await withCheckedContinuation { (cont: CheckedContinuation<Int32, Never>) in
            DispatchQueue.global().async {
                while true {
                    let data = handle.availableData
                    guard !data.isEmpty else { break }
                    if let str = take(data, false) {
                        DispatchQueue.main.async { runner.appendOutput(str.hasSuffix("\n") ? String(str.dropLast()) : str) }
                    }
                }
                if let str = take(Data(), true), !str.isEmpty {
                    DispatchQueue.main.async { runner.appendOutput(str.hasSuffix("\n") ? String(str.dropLast()) : str) }
                }
                process.waitUntilExit()
                DispatchQueue.main.async { cont.resume(returning: process.terminationStatus) }
            }
        }

        lastOutput = output
        if status != 0 {
            throw InstallerError.commandFailed(command, status)
        }
        return output
    }

    // A command's exit status (its output is logged as with shell()).
    func status(_ command: String, env: [String: String]? = nil) async -> Int32 {
        await capture(command, env: env).status
    }

    // A command's exit status and what it printed.
    func capture(_ command: String, env: [String: String]? = nil) async -> (status: Int32, text: String) {
        lastOutput = ""
        do {
            let out = try await shell(command, env: env)
            return (0, out.trimmingCharacters(in: .whitespacesAndNewlines))
        } catch InstallerError.commandFailed(_, let code) {
            return (code, lastOutput.trimmingCharacters(in: .whitespacesAndNewlines))
        } catch {
            return (-1, error.localizedDescription)
        }
    }

    // A command's exit status, not logged: for checks made every few seconds.
    func quietly(_ path: String, _ args: [String]) async -> Bool {
        await withCheckedContinuation { (cont: CheckedContinuation<Bool, Never>) in
            DispatchQueue.global().async {
                let p = Process()
                p.executableURL = URL(fileURLWithPath: path)
                p.arguments = args
                p.standardOutput = FileHandle.nullDevice
                p.standardError = FileHandle.nullDevice
                guard (try? p.run()) != nil else { return cont.resume(returning: false) }
                p.waitUntilExit()
                cont.resume(returning: p.terminationStatus == 0)
            }
        }
    }

    // The guide's Try Again: lp_install.py game-download starts over (SIGUSR1). The command is started with
    // exec, so this is its own process.
    func tryAgain() {
        if let p = currentProcess, p.isRunning { kill(p.processIdentifier, SIGUSR1) }
    }

    // The guide's Stop: the step that waits for the user ends, and the run with it.
    func stop() {
        stopRequested = true
        stopCurrentCommand()
    }

    func checkStopped() throws {
        if stopRequested { throw InstallerError.userCancelled }
    }

    func run() async {
        isRunning = true
        isFinished = false
        failed = false
        failure = nil
        log = ""
        stopRequested = false
        lastError = ""
        notes = []
        setGuide("", "", "")
        openLogFile()

        // Apple's tools first, before anything runs that needs them: everything else waits behind their questions.
        var steps: [(String, (BuildRunner) async throws -> Void)] = [("Checking Apple's tools", Steps.ensureXcodeTools)]

        if refreshOnly {
            steps.append(("Copying the game files", Steps.refreshGameFiles))
        } else if !embeddedInRepo {
            steps.append(("Downloading source", Steps.downloadSource))
        }
        if refreshOnly {
            // nothing else
        } else if game == "learnplay" {
            // The game and its DLLs, the code image (from the seed, else a harvest), then the build and the app.
            // The step titles match the "progress" labels of lp_install.py.
            steps += [
                ("Checking prerequisites",       Steps.checkPrereqsWindows),
                ("Downloading the Windows game", Steps.windowsGame),
                ("Downloading your DLC",         Steps.windowsDLC),
                ("Copying the game files",       Steps.windowsCopy),
                ("Getting Microsoft libraries",  Steps.windowsLibraries),
                ("Code image",                   Steps.windowsCodeImage),
                ("Building the game",            Steps.windowsBuild),
                ("Installing",                   Steps.windowsInstall),
            ]
        } else {
            steps += [
                ("Checking prerequisites",       Steps.checkPrereqs),
                ("Setting up Python environment", Steps.setupVenv),
                ("Decrypting the game",           Steps.obtainGameImage),
                ("Building shims",                Steps.buildShims),
                ("Building the app",              Steps.buildApp),
                ("Packaging",                     Steps.packageApp),
            ]
        }
        // The Metal shaders only for Metal: an OpenGL install needs none (after a later switch to Metal
        // the game compiles them as it uses them).
        if (renderer == "metal" || game == "learnplay") && !refreshOnly {
            steps.append(("Preparing shaders", Steps.prepareShaders))
        }
        if !embeddedInRepo && cleanupAfterInstall && game == "mac" && !refreshOnly {
            steps.append(("Cleaning up", Steps.cleanup))
        }

        totalSteps = steps.count
        currentStep = 0

        do {
            for (i, (title, action)) in steps.enumerated() {
                currentStep = i + 1
                stepTitle = title
                stepFraction = 0
                stepDetail = ""
                progressLog.reset()
                stepStart = Date()
                progress = Double(i) / Double(steps.count)
                appendLog("\n══════════════════════════════════════════")
                appendLog("Step \(i + 1)/\(steps.count): \(title)")
                appendLog("══════════════════════════════════════════\n")
                try await action(self)
            }
            progress = 1.0
            stepTitle = "Done"
            isFinished = true
            appendLog(refreshOnly ? "\n✓ Game files refreshed." : "\n✓ Installation complete.")
        } catch {
            failed = true
            failure = stopRequested ? InstallerError.userCancelled : error
            appendLog(stopRequested ? "\nStopped." : "\n✗ FAILED: \(error.localizedDescription)")
        }

        setGuide("", "", "")
        isRunning = false
        closeLogFile()
    }
}

enum InstallerError: LocalizedError {
    case commandFailed(String, Int32)
    case missingPrerequisite(String)
    case userCancelled
    case fileNotFound(String)
    case downloadFailed(String)
    case unsupportedGame(String)
    case driveNotConnected(String)

    var errorDescription: String? {
        switch self {
        case .commandFailed(let cmd, let code):
            if cmd.contains("curl ") || cmd.contains("pip install") {
                return "A download failed. Check that this Mac is on the internet."
            }
            return "A build command stopped with status \(code): \(cmd)"
        case .missingPrerequisite(let what):
            return what
        case .userCancelled:
            return "Cancelled by user."
        case .fileNotFound(let path):
            return "File not found: \(path)"
        case .downloadFailed(let what):
            return "Download failed: \(what)"
        case .unsupportedGame(let what):
            return "Unsupported game: \(what)"
        case .driveNotConnected(let name):
            return "The drive with the game files (\(name)) isn't connected. Connect it and try again."
        }
    }
}

// MARK: - Build steps

@MainActor
enum Steps {

    static func downloadSource(_ r: BuildRunner) async throws {
        let srcDir = r.buildBase + "/rs-rearmed"
        let stamp = srcDir + "/.installer-version"
        let fm = FileManager.default
        try fm.createDirectory(atPath: r.buildBase, withIntermediateDirectories: true)

        // The installer builds the source of its own release, not whatever is on main.
        let tag = "v\(rsVersion)"
        if (try? String(contentsOfFile: stamp, encoding: .utf8))?
            .trimmingCharacters(in: .whitespacesAndNewlines) == tag,
           fm.fileExists(atPath: srcDir + "/CMakeLists.txt") {
            r.appendLog("Source of \(tag) already downloaded.")
            r.repoRoot = srcDir
            return
        }

        // Unpack next to the old source, then swap; build/ (venv, decrypted game image) carries over.
        let archive = r.buildBase + "/source.tar.gz"
        let newDir = r.buildBase + "/rs-rearmed.new"
        try? fm.removeItem(atPath: newDir)
        try fm.createDirectory(atPath: newDir, withIntermediateDirectories: true)
        r.appendLog("Downloading the source of \(tag)…")
        try await r.shell("curl -fsSL --retry 3 --retry-all-errors -o \(shq(archive)) \(shq(sourceArchiveURL(tag)))", workDir: r.buildBase)
        // GitHub can answer 200 with an HTML page (e.g. a release that is not public yet).
        do {
            try await r.shell("gzip -t \(shq(archive))", workDir: r.buildBase)
        } catch {
            throw InstallerError.downloadFailed(
                "no source archive of \(tag) at \(repoWebURL); is the \(tag) release published?")
        }
        try await r.shell("tar -xzf \(shq(archive)) -C \(shq(newDir)) --strip-components 1", workDir: r.buildBase)
        try? fm.removeItem(atPath: archive)
        guard fm.fileExists(atPath: newDir + "/CMakeLists.txt") else {
            throw InstallerError.fileNotFound("CMakeLists.txt in the downloaded source")
        }
        // The scripts in it are run next, so it has to be the source this installer was made from, file for
        // file: a tag can be moved, and an archive can be whole and still be something else.
        if rsSourceDigest.isEmpty {
            r.appendLog("This installer was built without its source's digest: the download is not checked.")
        } else {
            let got = try await r.shell(sourceDigestCommand, workDir: newDir)
                .trimmingCharacters(in: .whitespacesAndNewlines)
            guard got == rsSourceDigest else {
                try? fm.removeItem(atPath: newDir)
                throw InstallerError.downloadFailed(
                    "the source of \(tag) at \(repoWebURL) is not the source this installer was made from "
                    + "(\(got.prefix(12)), not \(rsSourceDigest.prefix(12))). Nothing of it was run. "
                    + "Download the installer again from \(repoWebURL)/releases")
            }
            r.appendLog("The source is the one this installer was made from (\(got.prefix(12))).")
        }

        if fm.fileExists(atPath: srcDir + "/build") {
            try fm.moveItem(atPath: srcDir + "/build", toPath: newDir + "/build")
        }
        if fm.fileExists(atPath: srcDir) {
            try fm.removeItem(atPath: srcDir)
        }
        try fm.moveItem(atPath: newDir, toPath: srcDir)
        try (tag + "\n").write(toFile: stamp, atomically: true, encoding: .utf8)

        r.repoRoot = srcDir
        r.appendLog("Using source at \(srcDir)")
    }

    // Apple's command line tools (clang and the python3 everything here runs): macOS's own installer does it, so
    // the user only clicks Install in its window, and the installer goes on when they are there. Both editions.
    static func ensureXcodeTools(_ r: BuildRunner) async throws {
        if !(await r.quietly(xcodeSelect, ["-p"])) {
            r.appendLog("Asking macOS to install the Xcode command line tools…")
            _ = await r.quietly(xcodeSelect, ["--install"])
            r.setGuide("xcode", "Install Apple's command line tools",
                       "RS Re-ARMed needs Apple's free command line tools to build the game. A window from macOS has "
                       + "opened (it may be behind this one): click Install, then Agree. It takes a few minutes, and "
                       + "the installer goes on by itself. No window? Click Open Apple's Installer.")
            while !(await r.quietly(xcodeSelect, ["-p"])) {
                try r.checkStopped()
                try await Task.sleep(nanoseconds: 3_000_000_000)
            }
            r.setGuide("", "", "")
        }
        r.appendLog("  ✓ Xcode CLI tools")
        try await finishXcode(r)
    }

    // With Xcode itself selected, every clang and python3 goes through it, and an Xcode that has not been set
    // up (a new one, or just updated) stops them with macOS's question for an administrator's password, behind
    // whatever is on the screen. So it is asked here, up front, with words for it.
    static func finishXcode(_ r: BuildRunner) async throws {
        let dir = (try? await r.shell("\(shq(xcodeSelect)) -p"))?.trimmingCharacters(in: .whitespacesAndNewlines) ?? ""
        guard let app = dir.range(of: ".app/") else { return }  // the command line tools alone: nothing to set up
        func ready() async -> Bool {
            let license = await r.quietly(xcodeBuild, ["-license", "check"])
            let components = await r.quietly(xcodeBuild, ["-checkFirstLaunchStatus"])
            return license && components
        }
        if await ready() { return }
        r.appendLog("Xcode isn't set up yet: opening it.")
        NSWorkspace.shared.open(URL(fileURLWithPath: String(dir[..<app.upperBound])))
        r.setGuide("xcode-setup", "Finish setting up Xcode",
                   "Xcode is on this Mac and the build goes through it, but it hasn't installed its own components "
                   + "yet. Xcode is opening: if it shows a license, click Agree; when it asks to install components, "
                   + "click Install and type your Mac's password. The password goes to macOS, not to this installer. "
                   + "When that is done, you can quit Xcode: the installer goes on by itself.")
        while !(await ready()) {
            try r.checkStopped()
            try await Task.sleep(nanoseconds: 3_000_000_000)
        }
        r.setGuide("", "", "")
    }

    // The tools that are there must also run: a full Xcode whose license nobody accepted makes every clang
    // fail.
    static func checkTools(_ r: BuildRunner) async throws {
        guard await r.status("clang --version >/dev/null && python3 --version") == 0 else {
            throw InstallerError.missingPrerequisite(
                "Apple's command line tools are there but don't run. If you have Xcode, open it once and accept "
                + "its license, then click Build & Install.")
        }
    }

    static func checkPrereqs(_ r: BuildRunner) async throws {
        let arch = try await r.shell("uname -m")
        guard arch.trimmingCharacters(in: .whitespacesAndNewlines) == "arm64" else {
            throw InstallerError.missingPrerequisite("Apple Silicon Mac required")
        }
        r.appendLog("  ✓ Apple Silicon")
        // CMake and Ninja are not prerequisites: setupVenv installs them into the build's venv with pip.
        try await checkTools(r)
        try await askForStartupRoom(r)  // the same recompiler, the same memory

        // The game: the folder chosen, else any Steam library; else the walk through Steam, as Learn & Play
        // has, until Steam has installed it.
        let folder = r.gameDir.isEmpty ? "" : " --folder \(shq(r.gameDir))"
        let out = try await r.shell("exec \(gameFilesTool) --progress-lines mac-game\(folder)")
        if let line = out.split(separator: "\n").last(where: { $0.hasPrefix("game-folder ") }) {
            r.gameDir = String(line.dropFirst("game-folder ".count))
        }
        guard FileManager.default.fileExists(atPath: r.gameDir + "/Rocksmith2014.app") else {
            throw InstallerError.fileNotFound("Rocksmith2014.app not found in \(r.gameDir)")
        }
        r.appendLog("  ✓ Game found at \(r.gameDir)")
    }

    // The Mac build's venv: requirements.txt, with the CMake and Ninja the shims and the game code are
    // built with. Learn & Play's build has a list of its own: Capstone, and Unicorn and pefile for the unpacker.
    static func setupVenv(_ r: BuildRunner) async throws { try await setupVenv(r, packages: "-r requirements.txt") }
    static func setupVenvWindows(_ r: BuildRunner) async throws { try await setupVenv(r, packages: "-r win-x86/install/requirements.txt") }

    static func setupVenv(_ r: BuildRunner, packages: String) async throws {
        let venvPy = r.repoRoot + "/build/venv/bin/python"

        if FileManager.default.fileExists(atPath: venvPy) {
            r.appendLog("Virtual environment already exists, updating…")
        } else {
            try await r.shell("python3 -m venv build/venv")
        }
        // On a drive without extended attributes (exFAT, FAT) macOS writes a "._" file next to every file it
        // creates, and Python reads ._distutils-precedence.pth as a .pth file and fails to start.
        let removeAppleDouble = "find build/venv -name '._*' -delete"
        try await r.shell(removeAppleDouble)
        try await r.shell("build/venv/bin/pip install \(packages)")
        try await r.shell(removeAppleDouble)
        if packages == "-r requirements.txt" {
            try await r.shell("build/venv/bin/cmake --version && build/venv/bin/ninja --version")
        }
    }

    // The recompiler's input, from the Steam binary on disk: nothing runs, and no Steam, Rosetta or account is
    // involved (tools/steamless/decrypt.py checks it against the pin).
    static func obtainGameImage(_ r: BuildRunner) async throws {
        // Every run, as it takes seconds: an image kept from an earlier version may not be this version's.
        try await r.shell("python3 -I tools/steamless/decrypt.py --game \(shq(r.gameDir))")
    }

    static func buildShims(_ r: BuildRunner) async throws {
        // The venv's CMake and Ninja (setupVenv); CMake looks for ninja on PATH, so name it.
        r.appendLog("Configuring CMake…")
        try await r.shell("build/venv/bin/cmake -S . -B build -G Ninja -DCMAKE_MAKE_PROGRAM=\"$PWD/build/venv/bin/ninja\"")

        r.appendLog("Building shims (Bink/FFmpeg, Cg, OpenGL, game libraries)…")
        try await r.shell("build/venv/bin/cmake --build build")

        r.appendLog("Building Steam shim…")
        try await r.shell("make -C shims/steam")

        r.appendLog("Building network guard…")
        try await r.shell("tools/ws8/netguard/build.sh")
    }

    static func buildApp(_ r: BuildRunner) async throws {
        let rcPy = r.repoRoot + "/build/venv/bin/python"
        let rcImage = r.repoRoot + "/build/steamless/Rocksmith2014_x64_decrypted"

        let env = [
            "RC_PY": rcPy,
            "RC_IMAGE": rcImage,
            "RS_GAME_DIR": r.gameDir,
        ]

        r.appendLog("Recompiling game to ARM64 — this will take a while…")
        try await r.shell("tools/ws7/recomp/app/build-m4.sh build/m4", env: env)
    }

    static func packageApp(_ r: BuildRunner) async throws {
        let builtApp = r.repoRoot + "/build/m4/app/Rocksmith2014.app"
        guard FileManager.default.fileExists(atPath: builtApp + "/Contents/MacOS/Rocksmith2014") else {
            throw InstallerError.fileNotFound(builtApp)
        }

        // install.sh refuses to run while any Rocksmith2014 process is up: ask to quit it rather than fail at the
        // last step.
        while (try? await r.shell("pgrep -x Rocksmith2014")) != nil {
            let choice = showAlert(
                title: "Quit Rocksmith 2014",
                message: """
                    The app is built. Quit Rocksmith 2014 so the installer can install it, then click Continue.
                    """,
                buttons: ["Continue", "Cancel"]
            )
            if choice == "Cancel" { throw InstallerError.userCancelled }
        }

        // The same install as a manual build: the game goes into ~/Library/Application Support/RS Re-ARMed/game
        // with the install's own copy of the game files (clones on the same volume; the game finds its files next
        // to its .app) and ~/Applications/RS Re-ARMed.app is the launcher.
        // The shader warm-up is a step of its own (prepareShaders).
        try await r.shell("tools/play-native/install.sh \(shq(builtApp))",
                          env: ["RS_GAME_DIR": r.gameDir, "RS_WARM_SHADERS": "0", "RS_RENDERER": r.renderer])
        guard FileManager.default.fileExists(atPath: launcherApp + "/Contents/MacOS/launch") else {
            throw InstallerError.fileNotFound(launcherApp)
        }

        r.appendLog("\n══════════════════════════════════════════")
        r.appendLog("Installed: \(launcherApp)")
        r.appendLog("Game, its own copy of the game files (game/) and saves: ~/Library/Application Support/RS Re-ARMed/")
        r.appendLog("Renderer: \(r.renderer == "metal" ? "Metal" : "OpenGL") (the file \"renderer\" there; "
            + "write metal or opengl into it to switch)")
        r.appendLog("To uninstall, delete both (keep the folder learn-play in there if you have Learn & Play).")
        r.appendLog("══════════════════════════════════════════")
    }

    // The installed game compiles its Metal shaders once (warm-shaders.sh runs its executable with
    // --warm-shaders: no window), so it does not have to as it plays; "progress" lines
    // with "Preparing shaders… N of M" move the bar. Never fails the install: without it the game
    // compiles them as it uses them.
    static func prepareShaders(_ r: BuildRunner) async throws {
        guard r.lpStopped.isEmpty else { return }
        let lp = r.game == "learnplay"
        let later = "the game compiles them while it plays."
        r.appendLog(lp
            ? "Compiling the game's Metal shaders, so its first play doesn't stall on them. "
                + "The first time this takes under a minute; after an update usually seconds."
            : "Compiling the game's Metal shaders, so its first play doesn't stall on them. "
                + "The first time this takes a few minutes; after an update usually seconds.")
        r.shadersSkipped = false
        r.shadersReady = false  // Build Again: not what the run before found
        do {
            let out = try await r.shell("tools/play-native/warm-shaders.sh"
                + (r.game == "learnplay" ? " \(shq(lpApp))" : ""))
            r.shadersReady = out.contains("shader libraries ready")
        } catch {
            // Skipped (Skip stops it) or failed: not a failed install.
            r.appendLog(r.shadersSkipped
                ? "Shader preparation skipped: \(later)"
                : "Shader preparation stopped (\(error.localizedDescription)); \(later)")
        }
    }

    static func cleanup(_ r: BuildRunner) async throws {
        let srcDir = r.buildBase + "/rs-rearmed"
        r.appendLog("Removing build files at \(srcDir)…")
        try? FileManager.default.removeItem(atPath: srcDir)
        r.appendLog("Done. Freed disk space from the downloaded source and build artifacts.")
    }

    // MARK: Learn & Play (the Windows game)

    // Natively on Apple Silicon with what macOS ships: no Rosetta, no Homebrew, no steamcmd.
    static func checkPrereqsWindows(_ r: BuildRunner) async throws {
        let arch = try await r.shell("uname -m")
        guard arch.trimmingCharacters(in: .whitespacesAndNewlines) == "arm64" else {
            throw InstallerError.missingPrerequisite("Apple Silicon Mac required")
        }
        r.appendLog("  ✓ Apple Silicon")
        try await checkTools(r)
        guard FileManager.default.fileExists(atPath: r.repoRoot + "/win-x86/install/lp_install.py") else {
            throw InstallerError.fileNotFound("win-x86/install/lp_install.py: this version has no Learn & Play support")
        }
        try await askForStartupRoom(r)
    }

    // The startup disk holds swap whatever folder the build uses: asked up front, not half an hour in, when
    // macOS runs out of it. Only freeing space helps; the estimate is an estimate, so Go On Anyway is there.
    static func askForStartupRoom(_ r: BuildRunner) async throws {
        while true {
            let check = await r.capture("\(lpInstall) startup-space \(lpWork)")
            if check.status == 0 { return }
            let choice = showAlert(
                title: "The startup disk is short of space",
                message: """
                    \(check.text)

                    Free some space and click Check Again, or the build can stop with the disk full. A folder on \
                    another disk doesn't help with this: macOS uses the startup disk for it.
                    """,
                buttons: ["Check Again", "Go On Anyway", "Cancel"]
            )
            if choice == "Cancel" { throw InstallerError.userCancelled }
            if choice == "Go On Anyway" { return }
        }
    }

    // Room for Steam's download, or a folder on another disk. Steam puts the whole game on the startup disk at
    // once; checked before every download, since a Steam update can undo a redirection.
    static func askForRoom(_ r: BuildRunner) async throws {
        var note = ""
        while true {
            let check = await r.capture("\(lpInstall) game-space --with-build \(lpWork)")
            if check.status == 0 { return }
            let choice = showAlert(
                title: "Not enough space for the download",
                message: """
                    Steam downloads the Windows game (about 6 GB) at once, and it needs room for the build too.
                    \(check.text).

                    Choose a folder on another disk, such as an external drive, and Steam downloads it there \
                    instead (if macOS asks whether Steam may use that disk, click Allow). Or free some space \
                    and click Check Again.\(note)
                    """,
                buttons: ["Choose Folder…", "Check Again", "Cancel"]
            )
            if choice == "Cancel" { throw InstallerError.userCancelled }
            note = ""
            guard choice == "Choose Folder…" else { continue }
            let panel = NSOpenPanel()
            panel.canChooseFiles = false
            panel.canChooseDirectories = true
            panel.canCreateDirectories = true
            panel.directoryURL = URL(fileURLWithPath: "/Volumes")
            panel.message = "Choose a folder for Steam's download of the Windows game"
            if panel.runModal() == .OK, let url = panel.url {
                let moved = await r.capture("\(lpInstall) game-redirect \(shq(url.path))")
                if moved.status != 0 { note = "\n\nThat folder didn't work: \(moved.text)" }
            }
        }
    }

    // The game from Steam, with nothing to paste: lp_install.py game-download opens Steam and gives the running
    // Steam the download command, says what the user has to do, if anything, in the box above the log, and goes
    // on by itself. A folder the user picked (e.g. copied from a Windows PC) is only checked.
    static func windowsGame(_ r: BuildRunner) async throws {
        let folder = r.gameDir.isEmpty ? "" : " " + shq(r.gameDir)
        switch await r.status("\(lpInstall) game-locate\(folder)") {
        case 0:
            return
        case 2:
            throw InstallerError.unsupportedGame(
                "Steam's folder holds a different build of the Windows game from the one RS Re-ARMed is made for "
                + "(the log says which), so it can't be used.")
        default:
            if !r.gameDir.isEmpty { throw InstallerError.fileNotFound("Rocksmith2014.exe in \(r.gameDir)") }
        }

        while true {
            try await askForRoom(r)
            r.appendLog("Your Mac game and saves are not touched.")
            let status = await r.status("exec \(lpInstall) --progress-lines game-download")
            try r.checkStopped()
            switch status {
            case 0: return
            case 2:
                throw InstallerError.unsupportedGame(
                    "Steam downloaded a different build of the Windows game from the one RS Re-ARMed is made for "
                    + "(the log says which), so it can't be used.")
            case 4: continue  // no room after all: a Steam update can drop the link to another disk
            default: throw InstallerError.downloadFailed("the Windows game: \(r.lastError)")
            }
        }
    }

    // Every DLC the signed-in Steam account owns, one after another, the same way as the game (gamefiles.py
    // dlc-fetch). Never stops the install: what could not be fetched is said at the end, and Refresh Game Files
    // tries again. A game folder the user gave comes without Steam.
    static func windowsDLC(_ r: BuildRunner) async throws {
        guard r.gameDir.isEmpty else { return }
        _ = await r.status("exec \(gameFilesTool) --progress-lines dlc-fetch \(shq(lpHome + "/dlc-fetched.json"))")
        try r.checkStopped()
    }

    // The app's own copy of the game files (lp_install.py game-copy: the game and the PC DLC archives of the Mac
    // game's dlc folder; clones on the same volume, else a real copy with progress), which the app then reads.
    static func windowsCopy(_ r: BuildRunner) async throws {
        let folder = r.gameDir.isEmpty ? "" : " " + shq(r.gameDir)
        let found = try await r.shell("\(lpInstall) game-locate --path-only\(folder)")
            .trimmingCharacters(in: .whitespacesAndNewlines)
        try await r.shell("\(lpInstall) --progress-lines game-copy \(shq(found))")
        r.lpGameFolder = gameCopy("learnplay")
    }

    // Refresh Game Files: the edition's copy synced with the folders it was made from (new DLC, a Steam update),
    // with the copy's own tool (installed next to it, so the build folder is not needed).
    static func refreshGameFiles(_ r: BuildRunner) async throws {
        let tool = gameCopyTool(r.game)
        guard FileManager.default.fileExists(atPath: tool) else { throw InstallerError.fileNotFound(tool) }
        if let drive = gameCopyDriveMissing(r.game) {
            r.appendLog("\(gameCopy(r.game)) leads to the drive \(drive), which isn't connected.")
            throw InstallerError.driveNotConnected(drive)
        }
        if r.game == "learnplay" {  // new DLC from Steam first, as the install gets it
            _ = await r.status("exec python3 -I \(shq(tool)) --progress-lines dlc-fetch --wait 30 "
                + shq(lpHome + "/dlc-fetched.json"))
            try r.checkStopped()
        }
        r.appendLog("Syncing \(gameCopy(r.game)) with the game folder it was copied from. Only new and changed "
            + "files are copied; your settings and saves are not touched.")
        try await r.shell("python3 -I \(shq(tool)) --progress-lines refresh \(shq(gameCopy(r.game)))",
                          workDir: NSHomeDirectory())
        // The copy's tool of an install from before the choice keeps the copy where it is.
        if r.gameCopyPlace == "home", gameCopyElsewhere(r.game) != nil {
            r.appendLog("The game files are still on the other drive: this install is from before they could be "
                + "moved. Click Install to move them.")
        }
    }

    // msvcr100, msvcp100, d3dx9_42 and D3DCompiler_42: from Steam's Common Redistributables, else from
    // Microsoft's redistributables (the only download outside Steam), each checked by SHA-256.
    static func windowsLibraries(_ r: BuildRunner) async throws {
        try await r.shell("\(lpInstall) --progress-lines redist \(lpDLLDir)")
    }

    // The code image: kept from an earlier run when it is the pinned one; else with nothing from the user, the exe
    // unpacked offline with the seed the release's app bundle has; else (a build of the repository without one) an
    // earlier capture's, or one from a harvest folder (harvester v2, run once on a Windows PC), and without that the
    // run stops here (lpStopped), as learn-play.sh does.
    static func windowsCodeImage(_ r: BuildRunner) async throws {
        r.lpStopped = ""
        r.lpUnpinned = false
        let earlier = FileManager.default.fileExists(atPath: r.repoRoot + "/" + lpImage)
        if earlier, await r.status("\(lpInstall) image-check \(lpImage)") == 0 {
            r.appendLog("Code image from an earlier run: \(lpImage)")
            return
        }
        if await r.status("\(lpInstall) seed-check", env: bundledSeed()) == 0 {
            try await setupVenvWindows(r)
            try await r.shell("\(lpInstall) --progress-lines game-image \(shq(gameCopy("learnplay"))) \(lpImage) "
                + "--py build/venv/bin/python --report build/win-x86/image/report.txt", env: bundledSeed())
            return
        }
        if earlier {
            r.appendLog("Code image from an earlier capture: \(lpImage)")
            r.lpUnpinned = true
            return
        }
        let choice = showAlert(
            title: "The game's code from a Windows PC",
            message: """
                The Windows game and the libraries it needs are ready. Its code has to be captured once from \
                the game running on a Windows PC with the harvester (win-x86/harvest: a prebuilt exe and a \
                one-click script, nothing to install; `lp_install.py harvest-export DIR` makes the folder \
                to copy there).

                If you have the harvest folder it makes, choose it and the installer builds and installs the \
                game (about half an hour).
                """,
            buttons: ["Choose Harvest Folder…", "Stop Here"]
        )
        let panel = NSOpenPanel()
        panel.canChooseFiles = false
        panel.canChooseDirectories = true
        panel.message = "Choose the harvest folder (harvester v2)"
        guard choice == "Choose Harvest Folder…", panel.runModal() == .OK, let url = panel.url else {
            r.lpStopped = "no harvest folder was chosen"
            r.appendLog("Stopped before the code image: \(r.lpStopped).")
            return
        }
        try await setupVenvWindows(r)  // lp_image.py and the translator need capstone
        try await r.shell("mkdir -p build/win-x86/image && build/venv/bin/python win-x86/image/lp_image.py "
            + "\(shq(url.path)) \(lpImage) --report build/win-x86/image/report.txt")
        r.lpUnpinned = true
    }

    // Translate, compile and link (lp_install.py build: resumable, each translation keyed by its inputs).
    static func windowsBuild(_ r: BuildRunner) async throws {
        guard r.lpStopped.isEmpty else { return }
        if !FileManager.default.fileExists(atPath: r.repoRoot + "/build/venv/bin/python") { try await setupVenvWindows(r) }
        if r.lpGameFolder.isEmpty { r.lpGameFolder = gameCopy("learnplay") }  // the copy (windowsCopy)
        r.appendLog(try await r.shell("\(lpInstall) build-estimate"))
        try await r.shell("\(lpInstall) --progress-lines build \(lpWork) --image \(lpImage)"
            + "\(r.lpUnpinned ? " --unpinned" : "") --dlls \(lpDLLDir) "
            + "--game \(shq(r.lpGameFolder)) --py build/venv/bin/python")
    }

    // The app in ~/Applications and its folder for settings and saves; the build's intermediates are deleted
    // (the translated libraries stay, so a rebuild after an update is quick).
    static func windowsInstall(_ r: BuildRunner) async throws {
        guard r.lpStopped.isEmpty else { return }
        if r.lpGameFolder.isEmpty { r.lpGameFolder = gameCopy("learnplay") }
        try await r.shell("\(lpInstall) app-install \(lpWork) --game \(shq(r.lpGameFolder))")
        _ = await r.status("\(lpInstall) build-clean \(lpWork)")  // the app is installed: not worth failing over
    }
}

// MARK: - Process helpers

// SIGTERM to pid and all its descendants, children first.
func terminateTree(_ pid: pid_t) {
    let pgrep = Process()
    pgrep.executableURL = URL(fileURLWithPath: "/usr/bin/pgrep")
    pgrep.arguments = ["-P", String(pid)]
    let pipe = Pipe()
    pgrep.standardOutput = pipe
    pgrep.standardError = FileHandle.nullDevice
    if (try? pgrep.run()) != nil {
        let data = pipe.fileHandleForReading.readDataToEndOfFile()
        pgrep.waitUntilExit()
        for line in String(decoding: data, as: UTF8.self).split(separator: "\n") {
            if let child = pid_t(line.trimmingCharacters(in: .whitespaces)) { terminateTree(child) }
        }
    }
    kill(pid, SIGTERM)
}

// MARK: - Alert helper

@MainActor
func showAlert(title: String, message: String, buttons: [String]) -> String {
    let alert = NSAlert()
    alert.messageText = title
    alert.informativeText = message
    alert.alertStyle = .informational
    for b in buttons { alert.addButton(withTitle: b) }
    let response = alert.runModal()
    let idx = response.rawValue - NSApplication.ModalResponse.alertFirstButtonReturn.rawValue
    return idx >= 0 && idx < buttons.count ? buttons[idx] : buttons.last!
}

// MARK: - Repo detection

func detectRepoRoot() -> String? {
    // Walk up from the executable.
    var dir = (CommandLine.arguments[0] as NSString).deletingLastPathComponent
    for _ in 0..<10 {
        if FileManager.default.fileExists(atPath: dir + "/CMakeLists.txt") &&
           FileManager.default.fileExists(atPath: dir + "/tools/play-native/install.sh") {
            return dir
        }
        dir = (dir as NSString).deletingLastPathComponent
    }
    // Fallback: cwd.
    let cwd = FileManager.default.currentDirectoryPath
    if FileManager.default.fileExists(atPath: cwd + "/CMakeLists.txt") &&
       FileManager.default.fileExists(atPath: cwd + "/tools/play-native/install.sh") {
        return cwd
    }
    return nil
}

// MARK: - AppKit UI

@MainActor
class InstallerWindow: NSWindow {
    let runner = BuildRunner()
    let repoDetected: String?

    let gameDirField = NSTextField()
    let browseButton = NSButton(title: "Browse…", target: nil, action: nil)
    let buildDirField = NSTextField()
    let buildBrowseButton = NSButton(title: "Browse…", target: nil, action: nil)
    let startButton = NSButton(title: "Build & Install", target: nil, action: nil)
    let progressBar = NSProgressIndicator()
    let stepLabel = NSTextField(labelWithString: "Ready")
    let progressLabel = NSTextField(labelWithString: "")
    let logView = NSTextView()
    var shownLog = ""   // what logView has of the runner's log (updateUI)
    // What the user has to do now, between the progress and the log (runner.guideCode).
    let guideBox = NSBox()
    let guideTitle = NSTextField(labelWithString: "")
    let guideText = NSTextField(wrappingLabelWithString: "")
    let guideAction = NSButton(title: "", target: nil, action: nil)
    let guideStop = NSButton(title: "Stop", target: nil, action: nil)
    var guideClosed: NSLayoutConstraint!
    var startAtEdge: NSLayoutConstraint!
    var startBesideClose: NSLayoutConstraint!
    var guideShown = 0
    let showLogButton = NSButton(title: "Show Log File", target: nil, action: nil)
    let closeButton = NSButton(title: "Close", target: nil, action: nil)
    let updateButton = NSButton(title: "", target: nil, action: nil)
    var updateURL = ""
    // How the last run ended, in the box above the log until the next one starts: what the alerts said before,
    // in the window, with what to do next as its buttons and Close beside Build Again.
    struct RunResult {
        var good: Bool
        var title: String
        var text: String
        var primary = ""    // the box's buttons ("" : none)
        var secondary = ""
        var reveal = ""     // what Show in Finder shows
    }
    var result: RunResult?
    let refreshButton = NSButton(title: "Refresh Game Files", target: nil, action: nil)
    let cleanupCheck = NSButton(checkboxWithTitle: "Delete build files after install (saves ~5 GB)", target: nil, action: nil)
    let rendererPopup = NSPopUpButton(frame: .zero, pullsDown: false)
    let gameCopyPlacePopup = NSPopUpButton(frame: .zero, pullsDown: false)
    let modsButton = NSButton(title: "Built-in Mods…", target: nil, action: nil)
    var modsSheet: BuiltInModsSheet?  // while it is shown
    let gamePopup = NSPopUpButton(frame: .zero, pullsDown: false)
    let gameDirLabel = NSTextField(labelWithString: "Rocksmith 2014 game folder:")
    var macGameDir = ""  // the Mac game folder field, kept while Learn & Play is chosen
    var lpGameDir = ""   // the Windows game folder field (empty: Steam's download folder)

    var selectedGame: String { gameChoices[max(0, gamePopup.indexOfSelectedItem)].value }

    init() {
        repoDetected = detectRepoRoot()

        let rect = NSRect(x: 0, y: 0, width: 720, height: 620)
        super.init(contentRect: rect, styleMask: [.titled, .closable, .miniaturizable, .resizable],
                   backing: .buffered, defer: false)
        self.title = "RS Re-ARMed Installer v\(rsVersion)"
        self.minSize = NSSize(width: 600, height: 450)
        self.center()

        if let repo = repoDetected {
            runner.repoRoot = repo
            runner.embeddedInRepo = true
        }

        setupUI()
    }

    private func setupUI() {
        let content = NSView(frame: self.contentRect(forFrameRect: self.frame))
        self.contentView = content
        content.wantsLayer = true

        // The game folder the current install's copy comes from (install.sh's game-source; earlier versions' game-dir
        // named it, now the copy), else Steam's default library.
        let installedGameDir = ["/game-source", "/game-dir"].lazy.compactMap {
            (try? String(contentsOfFile: nativeHome + $0, encoding: .utf8))?
                .trimmingCharacters(in: .whitespacesAndNewlines)
        }.first { !$0.isEmpty && $0 != gameCopy("mac") }
        var isDir: ObjCBool = false
        let defaultGameDir = installedGameDir.flatMap {
            FileManager.default.fileExists(atPath: $0, isDirectory: &isDir) && isDir.boolValue ? $0 : nil
        } ?? NSHomeDirectory() + "/Library/Application Support/Steam/steamapps/common/Rocksmith2014"
        runner.gameDir = defaultGameDir

        let titleLabel = NSTextField(labelWithString: "RS Re-ARMed Installer")
        titleLabel.font = .boldSystemFont(ofSize: 20)
        titleLabel.translatesAutoresizingMaskIntoConstraints = false
        content.addSubview(titleLabel)

        let versionLabel = NSTextField(labelWithString: "v\(rsVersion)")
        versionLabel.font = .systemFont(ofSize: 12)
        versionLabel.textColor = .tertiaryLabelColor
        versionLabel.translatesAutoresizingMaskIntoConstraints = false
        content.addSubview(versionLabel)

        // A newer release: said here, where it does not stand in the way of this one (AppDelegate asks GitHub).
        updateButton.bezelStyle = .inline
        updateButton.isHidden = true
        updateButton.target = self
        updateButton.action = #selector(openUpdate)
        updateButton.translatesAutoresizingMaskIntoConstraints = false
        content.addSubview(updateButton)

        let subtitleLabel = NSTextField(labelWithString: "Builds Rocksmith 2014 natively for Apple Silicon from your Steam copy.")
        subtitleLabel.font = .systemFont(ofSize: 12)
        subtitleLabel.textColor = .secondaryLabelColor
        subtitleLabel.translatesAutoresizingMaskIntoConstraints = false
        content.addSubview(subtitleLabel)


        // Game: the Mac game; else (no Mac game installed, or chosen last time) Learn & Play.
        let gameLabel = NSTextField(labelWithString: "Game:")
        gameLabel.font = .systemFont(ofSize: 12, weight: .medium)
        gameLabel.translatesAutoresizingMaskIntoConstraints = false
        content.addSubview(gameLabel)

        gamePopup.addItems(withTitles: gameChoices.map { $0.title })
        let macGameThere = FileManager.default.fileExists(atPath: defaultGameDir + "/Rocksmith2014.app")
        let savedGame = UserDefaults.standard.string(forKey: gameDefaultsKey) ?? (macGameThere ? "mac" : "learnplay")
        gamePopup.selectItem(at: gameChoices.firstIndex(where: { $0.value == savedGame }) ?? 0)
        gamePopup.font = .systemFont(ofSize: 12)
        gamePopup.target = self
        gamePopup.action = #selector(gameChanged)
        gamePopup.translatesAutoresizingMaskIntoConstraints = false
        content.addSubview(gamePopup)
        macGameDir = defaultGameDir

        // Game directory
        gameDirLabel.font = .systemFont(ofSize: 12, weight: .medium)
        gameDirLabel.translatesAutoresizingMaskIntoConstraints = false
        content.addSubview(gameDirLabel)

        gameDirField.stringValue = defaultGameDir
        gameDirField.isEditable = true
        gameDirField.isBordered = true
        gameDirField.isBezeled = true
        gameDirField.bezelStyle = .roundedBezel
        gameDirField.font = .systemFont(ofSize: 12)
        gameDirField.translatesAutoresizingMaskIntoConstraints = false
        gameDirField.lineBreakMode = .byTruncatingMiddle
        content.addSubview(gameDirField)

        browseButton.bezelStyle = .rounded
        browseButton.target = self
        browseButton.action = #selector(browseGameDir)
        browseButton.translatesAutoresizingMaskIntoConstraints = false
        content.addSubview(browseButton)

        // Build folder (only when running standalone; in a checkout the build goes into its build/)
        let buildDirLabel = NSTextField(labelWithString: "Build folder:")
        buildDirLabel.font = .systemFont(ofSize: 12, weight: .medium)
        buildDirLabel.translatesAutoresizingMaskIntoConstraints = false

        if !runner.embeddedInRepo {
            if let saved = UserDefaults.standard.string(forKey: buildBaseDefaultsKey), !saved.isEmpty {
                runner.buildBase = buildBase(in: saved)
            }
            buildDirField.stringValue = runner.buildBase
            buildDirField.isEditable = true
            buildDirField.isBordered = true
            buildDirField.isBezeled = true
            buildDirField.bezelStyle = .roundedBezel
            buildDirField.font = .systemFont(ofSize: 12)
            buildDirField.translatesAutoresizingMaskIntoConstraints = false
            buildDirField.lineBreakMode = .byTruncatingMiddle

            buildBrowseButton.bezelStyle = .rounded
            buildBrowseButton.target = self
            buildBrowseButton.action = #selector(browseBuildDir)
            buildBrowseButton.translatesAutoresizingMaskIntoConstraints = false

            content.addSubview(buildDirLabel)
            content.addSubview(buildDirField)
            content.addSubview(buildBrowseButton)
        }

        // Renderer: what the current install uses (the game may have fallen back to OpenGL, or the user
        // switched with the file); else the one chosen last time; else Metal.
        let rendererLabel = NSTextField(labelWithString: "Renderer:")
        rendererLabel.font = .systemFont(ofSize: 12, weight: .medium)
        rendererLabel.translatesAutoresizingMaskIntoConstraints = false
        content.addSubview(rendererLabel)

        rendererPopup.addItems(withTitles: rendererChoices.map { $0.title })
        let installed = (try? String(contentsOfFile: nativeHome + "/renderer", encoding: .utf8))?
            .trimmingCharacters(in: .whitespacesAndNewlines).lowercased()
        let current = installed == "gl" ? "opengl" : installed
        let saved = rendererChoices.contains(where: { $0.value == current })
            ? current : UserDefaults.standard.string(forKey: rendererDefaultsKey)
        rendererPopup.selectItem(at: rendererChoices.firstIndex(where: { $0.value == saved }) ?? 0)
        rendererPopup.font = .systemFont(ofSize: 12)
        rendererPopup.translatesAutoresizingMaskIntoConstraints = false
        content.addSubview(rendererPopup)

        let rendererHint = NSTextField(labelWithString: "Switch later without reinstalling (README: Renderer)")
        rendererHint.font = .systemFont(ofSize: 11)
        rendererHint.textColor = .secondaryLabelColor
        rendererHint.toolTip = "The file \"renderer\" in ~/Library/Application Support/RS Re-ARMed: metal or opengl"
        rendererHint.lineBreakMode = .byTruncatingTail
        rendererHint.setContentCompressionResistancePriority(.defaultLow, for: .horizontal)
        rendererHint.translatesAutoresizingMaskIntoConstraints = false
        content.addSubview(rendererHint)

        // Game files: where the copy is kept when the game is on another drive (applyGame selects where it is now).
        let placeLabel = NSTextField(labelWithString: "Game files:")
        placeLabel.font = .systemFont(ofSize: 12, weight: .medium)
        placeLabel.translatesAutoresizingMaskIntoConstraints = false
        content.addSubview(placeLabel)

        gameCopyPlacePopup.addItems(withTitles: gameCopyPlaces.map { $0.title })
        gameCopyPlacePopup.font = .systemFont(ofSize: 12)
        gameCopyPlacePopup.toolTip = "Only for a game on another drive than your home folder. On the game's drive the "
            + "copy takes no extra space, but the game needs that drive connected and macOS asks whether it may use "
            + "it. On this Mac it is a full copy. Install or Refresh Game Files moves a copy that is in the other place."
        gameCopyPlacePopup.translatesAutoresizingMaskIntoConstraints = false
        content.addSubview(gameCopyPlacePopup)

        let placeHint = NSTextField(labelWithString: "For a game on another drive")
        placeHint.font = .systemFont(ofSize: 11)
        placeHint.textColor = .secondaryLabelColor
        placeHint.toolTip = gameCopyPlacePopup.toolTip
        placeHint.lineBreakMode = .byTruncatingTail
        placeHint.setContentCompressionResistancePriority(.defaultLow, for: .horizontal)
        placeHint.translatesAutoresizingMaskIntoConstraints = false
        content.addSubview(placeHint)

        // Built-in mods to switch off, for the game chosen above (the sheet says which).
        modsButton.bezelStyle = .rounded
        modsButton.target = self
        modsButton.action = #selector(showBuiltInMods)
        modsButton.toolTip = "Switch off built-in mods, such as custom songs or the intro skip. Takes effect the next "
            + "time the game starts; nothing to reinstall."
        modsButton.translatesAutoresizingMaskIntoConstraints = false
        content.addSubview(modsButton)

        // Source info
        let sourceLabel = NSTextField(labelWithString: "Source:")
        sourceLabel.font = .systemFont(ofSize: 12, weight: .medium)
        sourceLabel.translatesAutoresizingMaskIntoConstraints = false
        content.addSubview(sourceLabel)

        let sourceText: String
        if let repo = repoDetected {
            sourceText = repo
        } else {
            sourceText = "Will download the v\(rsVersion) source from \(repoWebURL) into the build folder"
        }
        let sourceField = NSTextField(labelWithString: sourceText)
        sourceField.font = .systemFont(ofSize: 12)
        sourceField.textColor = .secondaryLabelColor
        sourceField.translatesAutoresizingMaskIntoConstraints = false
        content.addSubview(sourceField)

        // Cleanup checkbox (only when running standalone)
        cleanupCheck.translatesAutoresizingMaskIntoConstraints = false
        cleanupCheck.isHidden = runner.embeddedInRepo
        cleanupCheck.state = .off
        content.addSubview(cleanupCheck)

        // Progress
        progressBar.isIndeterminate = false
        progressBar.minValue = 0
        progressBar.maxValue = 1
        progressBar.doubleValue = 0
        progressBar.translatesAutoresizingMaskIntoConstraints = false
        content.addSubview(progressBar)

        stepLabel.font = .systemFont(ofSize: 13, weight: .medium)
        stepLabel.lineBreakMode = .byTruncatingTail
        stepLabel.setContentCompressionResistancePriority(.defaultLow, for: .horizontal)
        stepLabel.translatesAutoresizingMaskIntoConstraints = false
        content.addSubview(stepLabel)

        progressLabel.font = .systemFont(ofSize: 11)
        progressLabel.textColor = .secondaryLabelColor
        progressLabel.translatesAutoresizingMaskIntoConstraints = false
        content.addSubview(progressLabel)

        // Log view
        let scrollView = NSScrollView()
        scrollView.hasVerticalScroller = true
        scrollView.hasHorizontalScroller = false
        scrollView.autohidesScrollers = true
        scrollView.borderType = .bezelBorder
        scrollView.translatesAutoresizingMaskIntoConstraints = false
        content.addSubview(scrollView)

        logView.isEditable = false
        logView.isSelectable = true
        logView.font = NSFont.monospacedSystemFont(ofSize: 11, weight: .regular)
        logView.backgroundColor = NSColor.textBackgroundColor
        logView.textContainerInset = NSSize(width: 4, height: 4)
        logView.isVerticallyResizable = true
        logView.isHorizontallyResizable = false
        logView.autoresizingMask = [.width]
        logView.textContainer?.widthTracksTextView = true
        scrollView.documentView = logView

        // What to do now
        guideBox.boxType = .custom
        guideBox.titlePosition = .noTitle
        guideBox.cornerRadius = 8
        guideBox.borderWidth = 1
        guideBox.borderColor = .controlAccentColor
        guideBox.fillColor = NSColor.controlAccentColor.withAlphaComponent(0.1)
        guideBox.contentViewMargins = NSSize(width: 12, height: 10)
        guideBox.translatesAutoresizingMaskIntoConstraints = false
        guideBox.isHidden = true
        content.addSubview(guideBox)
        guideTitle.font = .boldSystemFont(ofSize: 14)
        guideText.font = .systemFont(ofSize: 13)
        guideText.isSelectable = true
        guideAction.bezelStyle = .rounded
        guideAction.target = self
        guideAction.action = #selector(guideActionClicked)
        guideStop.bezelStyle = .rounded
        guideStop.target = self
        guideStop.action = #selector(guideStopClicked)
        let inside = guideBox.contentView!
        for v in [guideTitle, guideText, guideAction, guideStop] as [NSView] {
            v.translatesAutoresizingMaskIntoConstraints = false
            inside.addSubview(v)
        }
        guideText.setContentHuggingPriority(.required, for: .vertical)
        guideClosed = guideBox.heightAnchor.constraint(equalToConstant: 0)
        guideClosed.isActive = true
        // As small as its text allows: the rest of the window is the log's.
        let guideSmall = guideBox.heightAnchor.constraint(equalToConstant: 0)
        guideSmall.priority = .defaultLow
        guideSmall.isActive = true
        NSLayoutConstraint.activate([
            guideTitle.topAnchor.constraint(equalTo: inside.topAnchor),
            guideTitle.leadingAnchor.constraint(equalTo: inside.leadingAnchor),
            guideText.topAnchor.constraint(equalTo: guideTitle.bottomAnchor, constant: 4),
            guideText.leadingAnchor.constraint(equalTo: inside.leadingAnchor),
            guideText.trailingAnchor.constraint(equalTo: guideStop.leadingAnchor, constant: -12),
            guideText.bottomAnchor.constraint(lessThanOrEqualTo: inside.bottomAnchor),
            guideAction.topAnchor.constraint(equalTo: inside.topAnchor),
            guideAction.trailingAnchor.constraint(equalTo: inside.trailingAnchor),
            guideStop.topAnchor.constraint(equalTo: guideAction.bottomAnchor, constant: 6),
            guideStop.trailingAnchor.constraint(equalTo: inside.trailingAnchor),
            guideStop.widthAnchor.constraint(equalTo: guideAction.widthAnchor),
            guideAction.widthAnchor.constraint(greaterThanOrEqualToConstant: 130),
            inside.bottomAnchor.constraint(greaterThanOrEqualTo: guideStop.bottomAnchor),
        ])

        // Buttons
        startButton.bezelStyle = .rounded
        startButton.keyEquivalent = "\r"
        startButton.target = self
        startButton.action = #selector(startBuild)
        startButton.translatesAutoresizingMaskIntoConstraints = false
        content.addSubview(startButton)

        showLogButton.bezelStyle = .rounded
        showLogButton.isHidden = true
        showLogButton.target = self
        showLogButton.action = #selector(revealLog)
        showLogButton.translatesAutoresizingMaskIntoConstraints = false

        refreshButton.bezelStyle = .rounded
        refreshButton.isHidden = true
        refreshButton.target = self
        refreshButton.action = #selector(refreshGameFiles)
        refreshButton.toolTip = "Copy new DLC, custom songs and game updates from Steam into RS Re-ARMed's own copy of the game files"
        refreshButton.translatesAutoresizingMaskIntoConstraints = false
        let leftButtons = NSStackView(views: [showLogButton, refreshButton])
        leftButtons.orientation = .horizontal
        leftButtons.spacing = 8
        leftButtons.translatesAutoresizingMaskIntoConstraints = false
        content.addSubview(leftButtons)

        // Close, once a run has ended: the window's own close button does the same, but the last thing an
        // installer shows should have a way out where the eye is. Escape presses it.
        closeButton.bezelStyle = .rounded
        closeButton.isHidden = true
        closeButton.keyEquivalent = "\u{1b}"
        closeButton.target = self
        closeButton.action = #selector(closeInstaller)
        closeButton.translatesAutoresizingMaskIntoConstraints = false
        content.addSubview(closeButton)

        // The cleanup checkbox goes between the source field and the progress bar.
        let checkTopAnchor = runner.embeddedInRepo
            ? sourceField.bottomAnchor
            : cleanupCheck.bottomAnchor

        let sourceTopAnchor = runner.embeddedInRepo ? gameDirField.bottomAnchor : buildDirField.bottomAnchor
        if !runner.embeddedInRepo {
            NSLayoutConstraint.activate([
                buildDirLabel.topAnchor.constraint(equalTo: gameDirField.bottomAnchor, constant: 12),
                buildDirLabel.leadingAnchor.constraint(equalTo: content.leadingAnchor, constant: 20),

                buildDirField.topAnchor.constraint(equalTo: buildDirLabel.bottomAnchor, constant: 4),
                buildDirField.leadingAnchor.constraint(equalTo: content.leadingAnchor, constant: 20),
                buildDirField.trailingAnchor.constraint(equalTo: buildBrowseButton.leadingAnchor, constant: -8),

                buildBrowseButton.centerYAnchor.constraint(equalTo: buildDirField.centerYAnchor),
                buildBrowseButton.trailingAnchor.constraint(equalTo: content.trailingAnchor, constant: -20),
                buildBrowseButton.widthAnchor.constraint(equalToConstant: 90),
            ])
        }

        // Build & Install is at the window's edge until Close takes that place.
        startAtEdge = startButton.trailingAnchor.constraint(equalTo: content.trailingAnchor, constant: -20)
        startBesideClose = startButton.trailingAnchor.constraint(equalTo: closeButton.leadingAnchor, constant: -8)

        NSLayoutConstraint.activate([
            titleLabel.topAnchor.constraint(equalTo: content.topAnchor, constant: 20),
            titleLabel.leadingAnchor.constraint(equalTo: content.leadingAnchor, constant: 20),

            versionLabel.lastBaselineAnchor.constraint(equalTo: titleLabel.lastBaselineAnchor),
            versionLabel.leadingAnchor.constraint(equalTo: titleLabel.trailingAnchor, constant: 8),

            updateButton.centerYAnchor.constraint(equalTo: titleLabel.centerYAnchor),
            updateButton.trailingAnchor.constraint(equalTo: content.trailingAnchor, constant: -20),
            updateButton.leadingAnchor.constraint(greaterThanOrEqualTo: versionLabel.trailingAnchor, constant: 12),

            subtitleLabel.topAnchor.constraint(equalTo: titleLabel.bottomAnchor, constant: 4),
            subtitleLabel.leadingAnchor.constraint(equalTo: content.leadingAnchor, constant: 20),

            gameLabel.centerYAnchor.constraint(equalTo: gamePopup.centerYAnchor),
            gameLabel.leadingAnchor.constraint(equalTo: content.leadingAnchor, constant: 20),

            gamePopup.topAnchor.constraint(equalTo: subtitleLabel.bottomAnchor, constant: 14),
            gamePopup.leadingAnchor.constraint(equalTo: gameLabel.trailingAnchor, constant: 8),

            gameDirLabel.topAnchor.constraint(equalTo: gamePopup.bottomAnchor, constant: 12),
            gameDirLabel.leadingAnchor.constraint(equalTo: content.leadingAnchor, constant: 20),

            gameDirField.topAnchor.constraint(equalTo: gameDirLabel.bottomAnchor, constant: 4),
            gameDirField.leadingAnchor.constraint(equalTo: content.leadingAnchor, constant: 20),
            gameDirField.trailingAnchor.constraint(equalTo: browseButton.leadingAnchor, constant: -8),

            browseButton.centerYAnchor.constraint(equalTo: gameDirField.centerYAnchor),
            browseButton.trailingAnchor.constraint(equalTo: content.trailingAnchor, constant: -20),
            browseButton.widthAnchor.constraint(equalToConstant: 90),

            rendererLabel.centerYAnchor.constraint(equalTo: rendererPopup.centerYAnchor),
            rendererLabel.leadingAnchor.constraint(equalTo: content.leadingAnchor, constant: 20),

            rendererPopup.topAnchor.constraint(equalTo: sourceTopAnchor, constant: 12),
            rendererPopup.leadingAnchor.constraint(equalTo: rendererLabel.trailingAnchor, constant: 8),

            rendererHint.centerYAnchor.constraint(equalTo: rendererPopup.centerYAnchor),
            rendererHint.leadingAnchor.constraint(equalTo: rendererPopup.trailingAnchor, constant: 10),
            rendererHint.trailingAnchor.constraint(lessThanOrEqualTo: modsButton.leadingAnchor, constant: -10),

            modsButton.centerYAnchor.constraint(equalTo: rendererPopup.centerYAnchor),
            modsButton.trailingAnchor.constraint(equalTo: content.trailingAnchor, constant: -20),
            modsButton.widthAnchor.constraint(equalToConstant: 140),

            placeLabel.centerYAnchor.constraint(equalTo: gameCopyPlacePopup.centerYAnchor),
            placeLabel.leadingAnchor.constraint(equalTo: content.leadingAnchor, constant: 20),

            gameCopyPlacePopup.topAnchor.constraint(equalTo: rendererPopup.bottomAnchor, constant: 10),
            gameCopyPlacePopup.leadingAnchor.constraint(equalTo: placeLabel.trailingAnchor, constant: 8),

            placeHint.centerYAnchor.constraint(equalTo: gameCopyPlacePopup.centerYAnchor),
            placeHint.leadingAnchor.constraint(equalTo: gameCopyPlacePopup.trailingAnchor, constant: 10),
            placeHint.trailingAnchor.constraint(lessThanOrEqualTo: content.trailingAnchor, constant: -20),

            sourceLabel.topAnchor.constraint(equalTo: gameCopyPlacePopup.bottomAnchor, constant: 12),
            sourceLabel.leadingAnchor.constraint(equalTo: content.leadingAnchor, constant: 20),

            sourceField.topAnchor.constraint(equalTo: sourceLabel.bottomAnchor, constant: 2),
            sourceField.leadingAnchor.constraint(equalTo: content.leadingAnchor, constant: 20),
            sourceField.trailingAnchor.constraint(equalTo: content.trailingAnchor, constant: -20),

            cleanupCheck.topAnchor.constraint(equalTo: sourceField.bottomAnchor, constant: 8),
            cleanupCheck.leadingAnchor.constraint(equalTo: content.leadingAnchor, constant: 20),

            progressBar.topAnchor.constraint(equalTo: checkTopAnchor, constant: 12),
            progressBar.leadingAnchor.constraint(equalTo: content.leadingAnchor, constant: 20),
            progressBar.trailingAnchor.constraint(equalTo: content.trailingAnchor, constant: -20),

            stepLabel.topAnchor.constraint(equalTo: progressBar.bottomAnchor, constant: 6),
            stepLabel.leadingAnchor.constraint(equalTo: content.leadingAnchor, constant: 20),

            progressLabel.centerYAnchor.constraint(equalTo: stepLabel.centerYAnchor),
            progressLabel.trailingAnchor.constraint(equalTo: content.trailingAnchor, constant: -20),
            stepLabel.trailingAnchor.constraint(lessThanOrEqualTo: progressLabel.leadingAnchor, constant: -12),

            guideBox.topAnchor.constraint(equalTo: stepLabel.bottomAnchor, constant: 10),
            guideBox.leadingAnchor.constraint(equalTo: content.leadingAnchor, constant: 20),
            guideBox.trailingAnchor.constraint(equalTo: content.trailingAnchor, constant: -20),

            scrollView.topAnchor.constraint(equalTo: guideBox.bottomAnchor, constant: 10),
            scrollView.heightAnchor.constraint(greaterThanOrEqualToConstant: 60),
            scrollView.leadingAnchor.constraint(equalTo: content.leadingAnchor, constant: 20),
            scrollView.trailingAnchor.constraint(equalTo: content.trailingAnchor, constant: -20),
            scrollView.bottomAnchor.constraint(equalTo: startButton.topAnchor, constant: -12),

            closeButton.bottomAnchor.constraint(equalTo: content.bottomAnchor, constant: -16),
            closeButton.trailingAnchor.constraint(equalTo: content.trailingAnchor, constant: -20),
            closeButton.widthAnchor.constraint(equalToConstant: 100),

            startButton.bottomAnchor.constraint(equalTo: content.bottomAnchor, constant: -16),
            startAtEdge,
            startButton.widthAnchor.constraint(equalToConstant: 140),

            // (a stack: the one that is hidden leaves no gap)
            leftButtons.bottomAnchor.constraint(equalTo: content.bottomAnchor, constant: -16),
            leftButtons.leadingAnchor.constraint(equalTo: content.leadingAnchor, constant: 20),
            showLogButton.widthAnchor.constraint(equalToConstant: 140),
            refreshButton.widthAnchor.constraint(equalToConstant: 160),
            startButton.leadingAnchor.constraint(greaterThanOrEqualTo: leftButtons.trailingAnchor, constant: 8),
        ])

        applyGame()

        Timer.scheduledTimer(withTimeInterval: 0.1, repeats: true) { [weak self] _ in
            DispatchQueue.main.async { self?.updateUI() }
        }
    }

    // The game folder field follows the game: the Mac game's folder, or the Windows game's (empty: the
    // folder Steam's console downloads into).
    private func applyGame() {
        let lp = selectedGame == "learnplay"
        gameDirLabel.stringValue = lp ? "Windows game folder (leave empty to download it with Steam):"
                                      : "Rocksmith 2014 game folder:"
        gameDirField.placeholderString = lp ? "Steam's download folder" : nil
        gameDirField.stringValue = lp ? lpGameDir : macGameDir
        cleanupCheck.isHidden = runner.embeddedInRepo || lp
        let place = gameCopyPlaceNow(selectedGame)
        gameCopyPlacePopup.selectItem(at: gameCopyPlaces.firstIndex(where: { $0.value == place }) ?? 0)
        if !runner.isRunning && !runner.isFinished && !runner.failed { runner.log = whatHappens(lp) }
    }

    // In the log's place until a run starts: what the installer is about to do, and what is left to the user.
    private func whatHappens(_ lp: Bool) -> String {
        let game = lp ? gameLP : gameMac
        return """
            Click Build & Install. The installer does the rest and tells you, in a box above this text, whenever \
            it needs you: \(lp ? "about half an hour" : "about 20 minutes") in all, and you can use the Mac meanwhile.

            What you need: \(lp ? "Steam on this Mac, signed in with the account that owns \(game)" : "\(game) installed in Steam"), \
            and an internet connection. If anything is missing, the installer says what to do when it gets there.

            """
    }

    private func guideButton(_ code: String) -> String {
        switch code {
        case "steam-missing": return "Get Steam"
        case "steam-starting", "sign-in", "wrong-account", "steam-offline", "install-game", "installing-game":
            return "Open Steam"
        case "download-stalled", "download-failed", "paste": return "Try Again"
        case "xcode": return "Open Apple's Installer"
        case "xcode-setup": return "Open Xcode"
        default: return ""
        }
    }

    @objc func guideActionClicked() {
        if !runner.isRunning, let r = result {
            switch r.primary {
            case "Try Again": if runner.refreshOnly { refreshGameFiles() } else { startBuild() }
            case "Show in Finder": NSWorkspace.shared.activateFileViewerSelecting([URL(fileURLWithPath: r.reveal)])
            default: startGame()
            }
            return
        }
        switch runner.guideCode {
        case "steam-missing":
            if let url = URL(string: steamDownloadPage) { NSWorkspace.shared.open(url) }
        case "xcode":
            Task { _ = await runner.quietly(xcodeSelect, ["--install"]) }
        case "xcode-setup":
            if let app = NSWorkspace.shared.urlForApplication(withBundleIdentifier: "com.apple.dt.Xcode") {
                NSWorkspace.shared.open(app)
            }
        case "download-stalled", "download-failed", "paste":
            runner.tryAgain()
        default:
            if let app = NSWorkspace.shared.urlForApplication(withBundleIdentifier: steamBundleID) {
                NSWorkspace.shared.openApplication(at: app, configuration: NSWorkspace.OpenConfiguration())
            }
        }
    }

    @objc func guideStopClicked() {
        if !runner.isRunning, let r = result {
            if r.secondary == "Show Log File" { revealLog() }
            else { NSWorkspace.shared.activateFileViewerSelecting([URL(fileURLWithPath: r.reveal)]) }
            return
        }
        runner.stop()
    }

    @objc func closeInstaller() { performClose(nil) }

    @objc func openUpdate() {
        if let url = URL(string: updateURL) { NSWorkspace.shared.open(url) }
    }

    // A newer release than this installer: a button in the title row that opens its page.
    func showUpdate(tag: String, url: String) {
        updateURL = url
        updateButton.title = "Version \(tag.hasPrefix("v") ? String(tag.dropFirst()) : tag) is available: get it"
        updateButton.toolTip = "You are running v\(rsVersion). Opens the download page of \(tag)."
        updateButton.isHidden = false
    }

    private func boxColor(_ c: NSColor) {
        guideBox.borderColor = c
        guideBox.fillColor = c.withAlphaComponent(0.1)
    }

    private func updateGuide() {
        // Nothing moved for a while and nobody has said why: it may be a system window waiting for an answer.
        if runner.isRunning && runner.guideCode.isEmpty && Date().timeIntervalSince(runner.lastActivity) > quietAfter {
            runner.setGuide("quiet", "Is macOS asking something?",
                            "Nothing has changed for a while. Look for a window from macOS asking for permission or "
                            + "your password, which may be behind this one or another app: answer it and this goes "
                            + "on. If there is none, this step is simply taking its time.")
        }
        // After a run the same box says how it ended.
        if !runner.isRunning, let r = result {
            guideBox.isHidden = false
            guideClosed.isActive = false
            boxColor(r.good ? .systemGreen : .systemOrange)
            guideTitle.stringValue = r.title
            guideText.stringValue = r.text
            guideAction.title = r.primary
            guideAction.isHidden = r.primary.isEmpty
            guideStop.title = r.secondary
            guideStop.isHidden = r.secondary.isEmpty
            return
        }
        boxColor(.controlAccentColor)
        guideStop.title = "Stop"
        guideStop.isHidden = false
        let show = runner.isRunning && !runner.guideCode.isEmpty
        guideBox.isHidden = !show
        guideClosed.isActive = !show
        guard show else { return }
        guideTitle.stringValue = runner.guideTitle
        guideText.stringValue = runner.guideText
        let action = guideButton(runner.guideCode)
        guideAction.title = action
        guideAction.isHidden = action.isEmpty
        // A new message that asks for something: the Dock icon bounces if the installer is in the background.
        if guideShown != runner.guideSeen {
            guideShown = runner.guideSeen
            if !action.isEmpty { NSApp.requestUserAttention(.informationalRequest) }
        }
    }

    @objc func gameChanged() {
        // Keep what was typed for the other game.
        if selectedGame == "learnplay" { macGameDir = gameDirField.stringValue } else { lpGameDir = gameDirField.stringValue }
        // A run that ended was the other game's: its result, its log and what its buttons did (Build Again, Start
        // the Game) go with it, and the window is as it opens, for the game now chosen.
        if !runner.isRunning && runner.game != selectedGame {
            result = nil
            runner.isFinished = false
            runner.failed = false
            runner.failure = nil
            runner.refreshOnly = false
            runner.lpStopped = ""
            runner.progress = 0
            runner.setGuide("", "", "")
            runner.game = selectedGame
        }
        applyGame()
        updateUI()
    }

    private func updateUI() {
        progressBar.doubleValue = runner.progress
        let lp = runner.game == "learnplay"
        stepLabel.stringValue = runner.isRunning
            ? "Step \(runner.currentStep)/\(runner.totalSteps): \(runner.stepTitle)"
            : runner.isFinished && runner.refreshOnly ? "✓ Game files refreshed: \(gameCopy(runner.game))"
            : runner.isFinished ? (!lp ? "✓ Installed: ~/Applications/RS Re-ARMed.app"
                                   : runner.lpStopped.isEmpty ? "✓ Installed: ~/Applications/RS Re-ARMed Learn & Play.app"
                                   : "Stopped before the code image (\(runner.lpStopped))")
            : runner.failure is InstallerError && runner.stopRequested ? "Stopped. Build & Install goes on from here."
            : runner.failed ? (runner.refreshOnly ? "✗ Refresh failed (see the log)" : "✗ Build failed (see the log)")
            : "Ready"
        if runner.isRunning && !runner.stepDetail.isEmpty {
            // (a detail with its own numbers in brackets: the bar shows the rest)
            stepLabel.stringValue += " — \(runner.stepDetail)"
                + (runner.stepDetail.hasSuffix(")") ? "" : " (\(Int(runner.stepFraction * 100))%)")
        }
        if runner.isRunning {
            let t = Int(Date().timeIntervalSince(runner.stepStart))
            progressLabel.stringValue = String(format: "%d%% · %d:%02d", Int(runner.progress * 100), t / 60, t % 60)
        } else {
            progressLabel.stringValue = ""
        }
        // While the shaders are prepared the button skips that (optional) step.
        let canSkip = runner.isRunning && runner.stepTitle == "Preparing shaders" && !runner.shadersSkipped
        startButton.isEnabled = !runner.isRunning || canSkip
        startButton.title = canSkip ? "Skip" : runner.isFinished ? "Build Again" : "Build & Install"
        updateGuide()
        // Once a run has ended: Close at the edge, Return on what the box offers (Start the Game, Try Again).
        let ended = !runner.isRunning && result != nil
        closeButton.isHidden = !ended
        if startBesideClose.isActive != ended {
            startAtEdge.isActive = !ended
            startBesideClose.isActive = ended
        }
        let boxDefault = ended && !(result?.primary.isEmpty ?? true)
        guideAction.keyEquivalent = boxDefault ? "\r" : ""
        startButton.keyEquivalent = boxDefault ? "" : "\r"
        refreshButton.isHidden = runner.isRunning || !gameCopyExists(selectedGame)
        gamePopup.isEnabled = !runner.isRunning
        browseButton.isEnabled = !runner.isRunning
        gameDirField.isEditable = !runner.isRunning
        buildBrowseButton.isEnabled = !runner.isRunning
        buildDirField.isEditable = !runner.isRunning
        cleanupCheck.isEnabled = !runner.isRunning
        rendererPopup.isEnabled = !runner.isRunning && selectedGame == "mac"
        gameCopyPlacePopup.isEnabled = !runner.isRunning && !gameCopyPlaceFixed()
        showLogButton.isHidden = runner.logFileURL == nil

        // Ten times a second: only what the log grew by is added (a build's log is thousands of lines, and
        // setting all of it lays all of it out again); a log that is another one (a new build) is set whole.
        let logText = runner.log
        if logText != shownLog {
            if !shownLog.isEmpty, logText.hasPrefix(shownLog), let storage = logView.textStorage {
                let more = String(logText[logText.index(logText.startIndex, offsetBy: shownLog.count)...])
                storage.append(NSAttributedString(string: more, attributes: [
                    .font: logView.font ?? NSFont.monospacedSystemFont(ofSize: 11, weight: .regular),
                    .foregroundColor: logView.textColor ?? NSColor.textColor]))
            } else {
                logView.string = logText
            }
            shownLog = logText
            logView.scrollToEndOfDocument(nil)
        }
    }

    @objc func showBuiltInMods() {
        guard modsSheet == nil else { return }
        let s = BuiltInModsSheet(game: selectedGame)
        modsSheet = s
        beginSheet(s.sheet) { [weak self] _ in self?.modsSheet = nil }
    }

    @objc func browseGameDir() {
        let panel = NSOpenPanel()
        panel.canChooseFiles = false
        panel.canChooseDirectories = true
        panel.allowsMultipleSelection = false
        panel.message = selectedGame == "learnplay"
            ? "Select the Windows game folder (with Rocksmith2014.exe), e.g. one copied from a Windows PC"
            : "Select the Rocksmith 2014 game folder"
        if !gameDirField.stringValue.isEmpty { panel.directoryURL = URL(fileURLWithPath: gameDirField.stringValue) }
        if panel.runModal() == .OK, let url = panel.url {
            gameDirField.stringValue = url.path
        }
    }

    @objc func browseBuildDir() {
        let panel = NSOpenPanel()
        panel.canChooseFiles = false
        panel.canChooseDirectories = true
        panel.canCreateDirectories = true
        panel.allowsMultipleSelection = false
        panel.message = "Choose where to put the build files. A folder named \"\(buildFolderName)\" is made inside it."
        let current = URL(fileURLWithPath: buildDirField.stringValue)
        panel.directoryURL = current.lastPathComponent == buildFolderName ? current.deletingLastPathComponent() : current
        if panel.runModal() == .OK, let url = panel.url {
            buildDirField.stringValue = buildBase(in: url.path)
        }
    }

    // Always our own subfolder, so the source never lands in (and cleanup never touches) an unrelated folder.
    private func buildBase(in folder: String) -> String {
        let path = (folder.trimmingCharacters(in: .whitespaces) as NSString).expandingTildeInPath
        guard !path.isEmpty else { return defaultBuildBase }
        var std = (path as NSString).standardizingPath
        if (std as NSString).lastPathComponent == oldBuildFolderName {
            std = (std as NSString).deletingLastPathComponent
        }
        return (std as NSString).lastPathComponent == buildFolderName
            ? std : (std as NSString).appendingPathComponent(buildFolderName)
    }

    @objc func startBuild() {
        if runner.isRunning {
            if runner.stepTitle == "Preparing shaders" {
                runner.shadersSkipped = true
                runner.stopCurrentCommand()
            }
            return
        }
        result = nil
        runner.refreshOnly = false
        runner.game = selectedGame
        UserDefaults.standard.set(runner.game, forKey: gameDefaultsKey)
        runner.gameDir = gameDirField.stringValue.trimmingCharacters(in: .whitespaces)
        if !runner.embeddedInRepo {
            runner.buildBase = buildBase(in: buildDirField.stringValue)
            buildDirField.stringValue = runner.buildBase
            if runner.buildBase == defaultBuildBase {
                UserDefaults.standard.removeObject(forKey: buildBaseDefaultsKey)
            } else {
                UserDefaults.standard.set(runner.buildBase, forKey: buildBaseDefaultsKey)
            }
        }
        // The recompiler's build scripts can't handle spaces or shell characters in their own paths (the
        // game folder may have them).
        let buildPath = runner.embeddedInRepo ? runner.repoRoot : runner.buildBase
        if buildPath.contains(where: { $0.isWhitespace || "$`\"'&();<>|\\*?#~".contains($0) }) {
            _ = showAlert(
                title: "Choose a folder without spaces",
                message: """
                    The build can't run in a folder whose path contains spaces or characters such as $ & ' ( ):
                    \(buildPath)

                    \(runner.embeddedInRepo ? "Move the repository" : "Choose another build folder").
                    """,
                buttons: ["OK"]
            )
            return
        }
        runner.cleanupAfterInstall = cleanupCheck.state == .on
        runner.renderer = rendererChoices[max(0, rendererPopup.indexOfSelectedItem)].value
        UserDefaults.standard.set(runner.renderer, forKey: rendererDefaultsKey)
        takeGameCopyPlace()
        Task { @MainActor in
            await runner.run()
            updateUI()
            finished()
        }
    }

    // The Game files choice, for the run's commands (not when the environment names a folder of its own).
    private func takeGameCopyPlace() {
        runner.gameCopyPlace = gameCopyPlaceFixed() ? "" : gameCopyPlaces[max(0, gameCopyPlacePopup.indexOfSelectedItem)].value
        if !runner.gameCopyPlace.isEmpty {
            UserDefaults.standard.set(runner.gameCopyPlace, forKey: gameCopyPlaceDefaultsKey)
        }
    }

    // Refresh Game Files: the chosen game's own copy synced with Steam's folders again.
    @objc func refreshGameFiles() {
        guard !runner.isRunning else { return }
        result = nil
        runner.refreshOnly = true
        runner.game = selectedGame
        runner.lpStopped = ""
        takeGameCopyPlace()
        Task { @MainActor in
            await runner.run()
            updateUI()
            finished()
        }
    }

    // Say plainly what happened and where things are, instead of leaving it to the log: in the box above the
    // log (result), with what one needs to know before the first start; the rest, where everything is and how
    // to uninstall, is the log's last lines.
    private func finished() {
        let logPath = runner.logFileURL.map { ($0.path as NSString).abbreviatingWithTildeInPath } ?? logsDir
        let notes = runner.notes.isEmpty ? "" : "\n\n" + runner.notes.joined(separator: "\n")
        let shaders = { (ready: Bool) in ready ? "" : " The game's Metal shaders are not prepared: the first play "
            + "stalls while it compiles them. Build & Install again prepares them." }
        var where_ = ""
        if runner.failed, let error = runner.failure {
            if error is CancellationError { return }
            if case InstallerError.userCancelled = error { return }
            let why = runner.log.contains(deniedByMacOS) && !runner.refreshOnly ? deniedHelp
                : runner.lastError.isEmpty ? error.localizedDescription : runner.lastError
            result = runner.refreshOnly
                ? RunResult(good: false, title: "The game files could not be refreshed",
                            text: "\(why)\n\nFiles copied so far are kept, and Try Again continues from there.",
                            primary: "Try Again", secondary: "Show Log File")
                : RunResult(good: false, title: "\(runner.stepTitle) didn't work",
                            text: "\(why)\n\nTry Again goes on from where it stopped: what is done is kept. If it "
                                + "stops at the same place again, the log says why; please attach it when you ask "
                                + "for help.",
                            primary: "Try Again", secondary: "Show Log File")
            where_ = "The full log is in \(logPath)."
        } else if !runner.isFinished {
            return
        } else if runner.refreshOnly {
            result = RunResult(good: true, title: "Game files refreshed",
                               text: "\((gameCopy(runner.game) as NSString).abbreviatingWithTildeInPath) now matches "
                                   + "the game folder it is copied from: new DLC, custom songs and updates are in, "
                                   + "and files the game folder no longer has are gone. Your settings and saves "
                                   + "were not touched.\(notes)")
            where_ = "The installer log is in \(logPath)."
        } else if runner.game == "learnplay" && !runner.lpStopped.isEmpty {
            let dlls = runner.repoRoot + "/" + lpDLLDir
            result = RunResult(good: false, title: "Stopped before the build",
                               text: "The Windows game and the libraries it needs are in place; this copy of the "
                                   + "installer has no seed.bin (the released installer has one), so the build needs "
                                   + "the game's code, captured once on a Windows PC (\(runner.lpStopped)). Run the "
                                   + "installer again with the harvest folder; what is done is kept.",
                               primary: "Show in Finder", reveal: dlls)
            where_ = "The libraries are in \((dlls as NSString).abbreviatingWithTildeInPath). The installer log is "
                + "in \(logPath)."
        } else if runner.game == "learnplay" {
            result = RunResult(good: true, title: "RS Re-ARMed Learn & Play is installed",
                               text: "Start it any time from Applications in your home folder, or with Launchpad or "
                                   + "Spotlight. It runs offline.\n\n\(macOSQuestions("learnplay"))"
                                   + "\(shaders(runner.shadersReady))\(notes)",
                               primary: "Start the Game", secondary: "Show in Finder", reveal: lpApp)
            where_ = """
                The app is ~/Applications/RS Re-ARMed Learn & Play.app. Its own copy of the game files, your \
                settings, saves and logs are in ~/Library/Application Support/RS Re-ARMed/learn-play: the game no \
                longer needs Steam's download folder. After new DLC in Steam, click Refresh Game Files. To \
                uninstall, delete the app and that folder.\(gameCopyElsewhereNote("learnplay"))
                The installer log is in \(logPath).
                """
        } else {
            result = RunResult(good: true, title: "RS Re-ARMed is installed",
                               text: "Start it any time from Applications in your home folder, or with Launchpad or "
                                   + "Spotlight.\n\n\(macOSQuestions("mac"))"
                                   + "\(shaders(runner.renderer != "metal" || runner.shadersReady))\(notes)",
                               primary: "Start the Game", secondary: "Show in Finder", reveal: launcherApp)
            where_ = """
                The app is ~/Applications/RS Re-ARMed.app. The game build, its own copy of the game files and your \
                saves are in ~/Library/Application Support/RS Re-ARMed/: Steam updates or moving the Steam game do \
                not change it. After new DLC or custom songs, click Refresh Game Files. To uninstall, delete both \
                (keep the folder learn-play in there if you have Learn & Play).\(gameCopyElsewhereNote("mac"))
                The installer log is in \(logPath).
                """
        }
        runner.appendLog("\n" + where_)
        updateUI()
        // The installer may be behind other windows by now: the Dock icon says it is done.
        NSApp.requestUserAttention(.informationalRequest)
    }

    // The window after a run that did not happen (RS_INSTALLER_PREVIEW): for looking at it.
    func preview(_ what: String) {
        runner.game = selectedGame
        switch what {
        case "installed":
            runner.isFinished = true
            runner.shadersReady = true
            runner.progress = 1
        case "failed":
            runner.failed = true
            runner.failure = InstallerError.commandFailed("ninja", 1)
            runner.stepTitle = "Building the game"
            runner.lastError = "The build stopped: no space left on the disk."
        case "update":
            showUpdate(tag: "v99.0.0", url: repoWebURL + "/releases")
            return
        default:
            return
        }
        runner.log = "(a preview: nothing was run)\n"
        finished()
    }

    @objc func startGame() {
        let app = runner.game == "learnplay" ? lpApp : launcherApp
        NSWorkspace.shared.openApplication(at: URL(fileURLWithPath: app),
                                           configuration: NSWorkspace.OpenConfiguration())
    }

    @objc func revealLog() {
        if let url = runner.logFileURL {
            NSWorkspace.shared.activateFileViewerSelecting([url])
        }
    }
}

// MARK: - Built-in mods

// The built-in mods a player can switch off: KEY=0 lines in each edition's settings.env, which its launcher reads
// at every start (tools/play-native/launch.sh; Learn & Play: win-x86/app/lp_settings.c), so a change needs no
// reinstall. The keys are switches of docs/mac/INSTALL.md and docs/windows/INSTALL.md, "Settings and switches",
// and the same list as both launchers' (tools/play-native/test_settings.py checks it); nothing that touches the
// offline layer can be set this way. No file is every mod on, as before there was one.
struct BuiltInMod {
    let key: String
    let title: String
    let detail: String
    var lpDetail: String? = nil  // Learn & Play's, where it has less
}
let builtInMods: [BuiltInMod] = [
    BuiltInMod(key: "RS_MODS_BYPASS", title: "Custom songs (CDLC)",
               detail: "Songs without the game's signature load, as with RSBypass. Off: custom songs are not listed."),
    BuiltInMod(key: "RS_MODS_FIXES", title: "Faster loading and other fixes (rs-mac-fixes)",
               detail: "Large song libraries load faster; more keyboard keys, the End key in menus, more open files.",
               lpDetail: "Large song libraries load faster; more open files."),
    BuiltInMod(key: "RS_MODS_INTRO", title: "Skip the intro screens",
               detail: "The logos and the intro video at start-up."),
    BuiltInMod(key: "RS_MODS_AUTOSAVE", title: "Skip the Auto Save warning",
               detail: "The notice after choosing a profile."),
    BuiltInMod(key: "RS_MODS_OVERLAY", title: "Menu additions",
               detail: "DIRECT CONNECT for audio interfaces, ENUMERATE in place of SHOP, EXIT GAME in place of UPLAY."),
    BuiltInMod(key: "RS_MODS_MULTIPLAYER", title: "Two players on any input",
               detail: "Off: Player 2 needs a Real Tone cable, as in the original game."),
    BuiltInMod(key: "RS_REARMED", title: "Re-ARMed title logo",
               detail: "Off: the game's own logo."),
]

// The mods an edition's settings.env switches off, read as the launchers read it: whitespace ignored, # a comment,
// KEY=0 or KEY=1 of the keys above, the first of a key counts.
func modsSwitchedOff(_ game: String) -> Set<String> {
    guard let text = try? String(contentsOfFile: settingsFile(game), encoding: .utf8) else { return [] }
    var seen = Set<String>(), off = Set<String>()
    for raw in text.components(separatedBy: "\n") {
        let line = raw.filter { !$0.isWhitespace }
        guard !line.hasPrefix("#"), let eq = line.firstIndex(of: "=") else { continue }
        let key = String(line[..<eq]), value = String(line[line.index(after: eq)...])
        guard builtInMods.contains(where: { $0.key == key }), value == "0" || value == "1", !seen.contains(key)
        else { continue }
        seen.insert(key)
        if value == "0" { off.insert(key) }
    }
    return off
}

// The installer writes the whole file: the mods switched off, or no file when none is.
func saveModsSwitchedOff(_ game: String, _ off: Set<String>) throws {
    let path = settingsFile(game)
    if off.isEmpty {
        if FileManager.default.fileExists(atPath: path) { try FileManager.default.removeItem(atPath: path) }
        return
    }
    try FileManager.default.createDirectory(atPath: (path as NSString).deletingLastPathComponent,
                                            withIntermediateDirectories: true)
    let docs = game == "learnplay" ? "docs/windows/INSTALL.md" : "docs/mac/INSTALL.md"
    let text = """
        # The built-in mods switched off, written by the RS Re-ARMed Installer (Built-in Mods…) and read at every
        # start of the game. KEY=0 is off; the switches are in \(docs), "Settings and switches".

        """ + builtInMods.filter { off.contains($0.key) }.map { "\($0.key)=0\n" }.joined()
    try text.write(toFile: path, atomically: true, encoding: .utf8)
}

// A value `launchctl setenv` gives every app started from now on: it wins over the file.
func launchctlValue(_ key: String) -> String? {
    let p = Process()
    p.executableURL = URL(fileURLWithPath: "/bin/launchctl")
    p.arguments = ["getenv", key]
    let out = Pipe()
    p.standardOutput = out
    p.standardError = FileHandle.nullDevice
    guard (try? p.run()) != nil else { return nil }
    let data = out.fileHandleForReading.readDataToEndOfFile()
    p.waitUntilExit()
    let value = String(data: data, encoding: .utf8)?.trimmingCharacters(in: .whitespacesAndNewlines) ?? ""
    return value.isEmpty ? nil : value
}

// The sheet: one checkbox per mod for the game chosen in the window, every one on unless the file says off.
@MainActor
final class BuiltInModsSheet: NSObject {
    let game: String
    let sheet = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 520, height: 200), styleMask: [.titled],
                         backing: .buffered, defer: false)
    private var checks: [(key: String, box: NSButton)] = []

    init(game: String) {
        self.game = game
        super.init()
        let lp = game == "learnplay"
        let width: CGFloat = 480  // inside the margins
        let off = modsSwitchedOff(game)

        let stack = NSStackView()
        stack.orientation = .vertical
        stack.alignment = .leading
        stack.spacing = 0
        stack.edgeInsets = NSEdgeInsets(top: 20, left: 20, bottom: 20, right: 20)
        stack.translatesAutoresizingMaskIntoConstraints = false

        func label(_ text: String, size: CGFloat, indent: CGFloat = 0, color: NSColor = .secondaryLabelColor) -> NSView {
            let l = NSTextField(wrappingLabelWithString: text)
            l.font = .systemFont(ofSize: size)
            l.textColor = color
            l.isSelectable = false
            l.translatesAutoresizingMaskIntoConstraints = false
            l.widthAnchor.constraint(equalToConstant: width - indent).isActive = true
            guard indent > 0 else { return l }
            let row = NSStackView(views: [l])
            row.edgeInsets = NSEdgeInsets(top: 0, left: indent, bottom: 0, right: 0)
            return row
        }

        let title = NSTextField(labelWithString: "Built-in mods: \(lp ? gameLP : gameMac)")
        title.font = .boldSystemFont(ofSize: 14)
        stack.addArrangedSubview(title)
        stack.setCustomSpacing(6, after: title)
        let intro = label("Uncheck what you don't want. A change takes effect the next time the game starts, with "
                          + "nothing to reinstall.", size: 12)
        stack.addArrangedSubview(intro)
        stack.setCustomSpacing(16, after: intro)

        for mod in builtInMods {
            let box = NSButton(checkboxWithTitle: mod.title, target: nil, action: nil)
            box.state = off.contains(mod.key) ? .off : .on
            box.toolTip = "\(mod.key) in \((settingsFile(game) as NSString).abbreviatingWithTildeInPath)"
            stack.addArrangedSubview(box)
            stack.setCustomSpacing(2, after: box)
            let detail = label((lp ? mod.lpDetail : nil) ?? mod.detail, size: 11, indent: 20)
            stack.addArrangedSubview(detail)
            stack.setCustomSpacing(10, after: detail)
            checks.append((mod.key, box))
        }

        // A switch set with launchctl setenv (the earlier way, docs' "Settings and switches") wins over these.
        let overridden = builtInMods.compactMap { m in launchctlValue(m.key).map { "\(m.key)=\($0)" } }
        if !overridden.isEmpty {
            let note = label("Set with launchctl setenv, which wins over this page until you log out: "
                             + overridden.joined(separator: ", ") + ". launchctl unsetenv clears it.",
                             size: 11, color: .systemOrange)
            stack.addArrangedSubview(note)
            stack.setCustomSpacing(10, after: note)
        }
        stack.setCustomSpacing(16, after: stack.arrangedSubviews.last!)

        let restore = NSButton(title: "Turn All On", target: self, action: #selector(allOn))
        let cancel = NSButton(title: "Cancel", target: self, action: #selector(cancelled))
        cancel.keyEquivalent = "\u{1b}"
        let save = NSButton(title: "Save", target: self, action: #selector(saved))
        save.keyEquivalent = "\r"
        for b in [restore, cancel, save] { b.bezelStyle = .rounded }
        let buttons = NSStackView()
        buttons.orientation = .horizontal
        buttons.addView(restore, in: .leading)
        buttons.addView(cancel, in: .trailing)
        buttons.addView(save, in: .trailing)
        buttons.translatesAutoresizingMaskIntoConstraints = false
        stack.addArrangedSubview(buttons)
        buttons.widthAnchor.constraint(equalToConstant: width).isActive = true
        save.widthAnchor.constraint(greaterThanOrEqualToConstant: 90).isActive = true
        cancel.widthAnchor.constraint(equalTo: save.widthAnchor).isActive = true

        let content = NSView()
        content.addSubview(stack)
        NSLayoutConstraint.activate([
            stack.topAnchor.constraint(equalTo: content.topAnchor),
            stack.bottomAnchor.constraint(equalTo: content.bottomAnchor),
            stack.leadingAnchor.constraint(equalTo: content.leadingAnchor),
            stack.trailingAnchor.constraint(equalTo: content.trailingAnchor),
        ])
        sheet.contentView = content
        content.layoutSubtreeIfNeeded()
        sheet.setContentSize(content.fittingSize)
    }

    @objc func allOn() { for c in checks { c.box.state = .on } }

    @objc func cancelled() { sheet.sheetParent?.endSheet(sheet, returnCode: .cancel) }

    @objc func saved() {
        let off = Set(checks.filter { $0.box.state == .off }.map { $0.key })
        do {
            try saveModsSwitchedOff(game, off)
        } catch {
            _ = showAlert(title: "The settings could not be saved",
                          message: "\((settingsFile(game) as NSString).abbreviatingWithTildeInPath): "
                              + error.localizedDescription,
                          buttons: ["OK"])
            return
        }
        sheet.sheetParent?.endSheet(sheet, returnCode: .OK)
    }
}

// MARK: - Update check

func checkForUpdate() async -> (tag: String, url: String)? {
    guard let url = URL(string: releasesURL) else { return nil }
    var request = URLRequest(url: url, timeoutInterval: 10)
    request.setValue("application/vnd.github+json", forHTTPHeaderField: "Accept")
    guard let (data, response) = try? await URLSession.shared.data(for: request),
          let http = response as? HTTPURLResponse, http.statusCode == 200 else { return nil }
    guard let json = try? JSONSerialization.jsonObject(with: data) as? [String: Any],
          let tag = json["tag_name"] as? String,
          let htmlURL = json["html_url"] as? String else { return nil }
    let remote = tag.hasPrefix("v") ? String(tag.dropFirst()) : tag
    guard remote != rsVersion else { return nil }
    let remoteParts = remote.split(separator: ".").compactMap { Int($0) }
    let localParts = rsVersion.split(separator: ".").compactMap { Int($0) }
    for i in 0..<max(remoteParts.count, localParts.count) {
        let r = i < remoteParts.count ? remoteParts[i] : 0
        let l = i < localParts.count ? localParts[i] : 0
        if r > l { return (tag, htmlURL) }
        if r < l { return nil }
    }
    return nil
}

// MARK: - App delegate & entry point

class AppDelegate: NSObject, NSApplicationDelegate, NSWindowDelegate {
    var window: InstallerWindow!

    func applicationDidFinishLaunching(_ notification: Notification) {
        // A second copy of the app would build in the same folders: go to the one that runs.
        if let id = Bundle.main.bundleIdentifier, let other = NSRunningApplication.runningApplications(
            withBundleIdentifier: id).first(where: { $0 != NSRunningApplication.current }) {
            other.activate(options: [])
            NSApp.terminate(nil)
            return
        }
        NSApp.mainMenu = makeMainMenu()
        window = InstallerWindow()
        // Ours to keep: by default a closed window releases itself, and the quit that follows reads this
        // property for the runner (applicationShouldTerminate) and crashes on the freed window.
        window.isReleasedWhenClosed = false
        window.delegate = self
        window.makeKeyAndOrderFront(nil)

        // RS_INSTALLER_PREVIEW=installed|failed|update: the window as it is after such a run, with nothing run
        // (docs/mac/TESTING.md, Installer tests). No update check then.
        if let preview = ProcessInfo.processInfo.environment["RS_INSTALLER_PREVIEW"] {
            window.preview(preview)
            return
        }
        Task {
            if let update = await checkForUpdate() {
                // Asked once, since this installer builds its own version, not the newest; and it stays in the
                // window's title row for whoever chose Later.
                window.showUpdate(tag: update.tag, url: update.url)
                let choice = showAlert(
                    title: "Update Available",
                    message: "A newer version (\(update.tag)) is available. You are running v\(rsVersion), and "
                        + "this installer installs v\(rsVersion).",
                    buttons: ["Open Download Page", "Later"]
                )
                if choice == "Open Download Page", let url = URL(string: update.url) {
                    NSWorkspace.shared.open(url)
                }
            }
        }
    }

    func applicationShouldTerminateAfterLastWindowClosed(_ sender: NSApplication) -> Bool { true }

    // Quitting mid-build stops the running command too: an orphaned warm-up (the game's executable)
    // would keep the launcher and install.sh saying "Rocksmith 2014 is already running".
    func applicationShouldTerminate(_ sender: NSApplication) -> NSApplication.TerminateReply {
        guard let runner = window?.runner, runner.isRunning else { return .terminateNow }
        guard confirmQuit() else { return .terminateCancel }
        runner.stopCurrentCommand()
        return .terminateNow
    }

    // Closing the window quits, so it asks first too: a closed window can't be got back.
    func windowShouldClose(_ sender: NSWindow) -> Bool {
        guard let runner = window?.runner, runner.isRunning else { return true }
        guard confirmQuit() else { return false }
        runner.stopCurrentCommand()
        exit(0)
    }

    @MainActor private func confirmQuit() -> Bool {
        showAlert(
            title: "Stop the installation?",
            message: "RS Re-ARMed is still being built and installed. Quitting stops it; what is done is kept, "
                + "and Build & Install goes on from there later.",
            buttons: ["Continue", "Quit"]  // Return keeps it running
        ) == "Quit"
    }
}

// Main menu: without one there is no ⌘Q, and ⌘C/⌘A do nothing in the log and text fields.
@MainActor
func makeMainMenu() -> NSMenu {
    let main = NSMenu()

    let appMenu = NSMenu()
    appMenu.addItem(withTitle: "Built-in Mods…", action: #selector(InstallerWindow.showBuiltInMods), keyEquivalent: ",")
    appMenu.addItem(.separator())
    appMenu.addItem(withTitle: "Hide RS Re-ARMed Installer", action: #selector(NSApplication.hide(_:)), keyEquivalent: "h")
    appMenu.addItem(.separator())
    appMenu.addItem(withTitle: "Quit RS Re-ARMed Installer", action: #selector(NSApplication.terminate(_:)), keyEquivalent: "q")
    let appItem = NSMenuItem()
    appItem.submenu = appMenu
    main.addItem(appItem)

    let editMenu = NSMenu(title: "Edit")
    editMenu.addItem(withTitle: "Cut", action: #selector(NSText.cut(_:)), keyEquivalent: "x")
    editMenu.addItem(withTitle: "Copy", action: #selector(NSText.copy(_:)), keyEquivalent: "c")
    editMenu.addItem(withTitle: "Paste", action: #selector(NSText.paste(_:)), keyEquivalent: "v")
    editMenu.addItem(withTitle: "Select All", action: #selector(NSText.selectAll(_:)), keyEquivalent: "a")
    let editItem = NSMenuItem(title: "Edit", action: nil, keyEquivalent: "")
    editItem.submenu = editMenu
    main.addItem(editItem)

    let windowMenu = NSMenu(title: "Window")
    windowMenu.addItem(withTitle: "Minimize", action: #selector(NSWindow.performMiniaturize(_:)), keyEquivalent: "m")
    windowMenu.addItem(withTitle: "Close", action: #selector(NSWindow.performClose(_:)), keyEquivalent: "w")
    let windowItem = NSMenuItem(title: "Window", action: nil, keyEquivalent: "")
    windowItem.submenu = windowMenu
    main.addItem(windowItem)

    return main
}

if let i = CommandLine.arguments.firstIndex(of: "--parse-progress"), i + 1 < CommandLine.arguments.count {
    let a = CommandLine.arguments
    exit(parseProgressFile(a[i + 1], step: i + 2 < a.count ? a[i + 2] : "Preparing shaders"))
}

let delegate = AppDelegate()
let app = NSApplication.shared
app.delegate = delegate
app.setActivationPolicy(.regular)
app.activate(ignoringOtherApps: true)
app.run()

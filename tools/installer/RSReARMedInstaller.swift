// RS Re-ARMed Installer — a native macOS app that builds and installs the game
// following the steps in docs/BUILDING.md, with a progress bar and live log.
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
func sourceArchiveURL(_ tag: String) -> String { "\(repoWebURL)/archive/refs/tags/\(tag).tar.gz" }
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
    var logFileURL: URL?
    // The command shell() is running, so Skip and Quit can stop it (with what it started).
    var currentProcess: Process?
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

    // A command's output: "progress F LABEL" lines move the bar within the step and set the step
    // label (ProgressLog picks the ones that go to the log), the rest is logged as is.
    func appendOutput(_ text: String) {
        var rest: [Substring] = []
        for line in text.split(separator: "\n", omittingEmptySubsequences: false) {
            if let p = ProgressLine.parse(line) {
                stepFraction = p.fraction
                progress = (Double(currentStep - 1) + stepFraction) / Double(max(totalSteps, 1))
                stepDetail = p.detail(inStep: stepTitle)
                if progressLog.shouldLog(p) {
                    rest.append(Substring(p.logText))
                }
            } else {
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
        appendLog("Game directory: \(gameDir)")
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
        // Opened from the Finder, the app gets launchd's PATH (/usr/bin:/bin:/usr/sbin:/sbin), which
        // lacks Homebrew's cmake and ninja; add its folders as a login shell would.
        var path = (environment["PATH"] ?? "/usr/bin:/bin:/usr/sbin:/sbin").split(separator: ":").map(String.init)
        for dir in ["/opt/homebrew/bin", "/opt/homebrew/sbin", "/usr/local/bin"] where !path.contains(dir) {
            path.append(dir)
        }
        environment["PATH"] = path.joined(separator: ":")
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
        let outputLock = NSLock()

        let runner = self
        // Complete lines only (all of it when `flush`), decoded leniently.
        let take: (Data, Bool) -> String? = { data, flush in
            outputLock.lock()
            defer { outputLock.unlock() }
            pending.append(data)
            let end = flush ? pending.endIndex : pending.lastIndex(of: UInt8(ascii: "\n")).map { $0 + 1 }
            guard let end = end, end > pending.startIndex else { return nil }
            let str = String(decoding: pending[pending.startIndex..<end], as: UTF8.self)
            pending = Data(pending[end...])
            output += str
            return str
        }
        handle.readabilityHandler = { fh in
            let data = fh.availableData
            guard !data.isEmpty, let str = take(data, false) else { return }
            DispatchQueue.main.async {
                runner.appendOutput(str.hasSuffix("\n") ? String(str.dropLast()) : str)
            }
        }

        try process.run()
        currentProcess = process
        defer { currentProcess = nil }

        let status: Int32 = await withCheckedContinuation { (cont: CheckedContinuation<Int32, Never>) in
            DispatchQueue.global().async {
                process.waitUntilExit()
                handle.readabilityHandler = nil
                let rest = handle.readDataToEndOfFile()
                if let str = take(rest, true), !str.isEmpty {
                    DispatchQueue.main.async {
                        runner.appendOutput(str.hasSuffix("\n") ? String(str.dropLast()) : str)
                    }
                }
                cont.resume(returning: process.terminationStatus)
            }
        }

        if status != 0 {
            throw InstallerError.commandFailed(command, status)
        }
        return output
    }

    func run() async {
        isRunning = true
        isFinished = false
        failed = false
        failure = nil
        log = ""
        openLogFile()

        var steps: [(String, (BuildRunner) async throws -> Void)] = []

        if !embeddedInRepo {
            steps.append(("Downloading source", Steps.downloadSource))
        }
        steps += [
            ("Checking prerequisites",       Steps.checkPrereqs),
            ("Setting up Python environment", Steps.setupVenv),
            ("Obtaining game image",          Steps.obtainGameImage),
            ("Building shims",                Steps.buildShims),
            ("Building the app",              Steps.buildApp),
            ("Packaging",                     Steps.packageApp),
        ]
        // The Metal shaders only for Metal: an OpenGL install needs none (a later switch to Metal
        // prepares them on its first start).
        if renderer == "metal" {
            steps.append(("Preparing shaders", Steps.prepareShaders))
        }
        if !embeddedInRepo && cleanupAfterInstall {
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
            appendLog("\n✓ Installation complete.")
        } catch {
            failed = true
            failure = error
            appendLog("\n✗ FAILED: \(error.localizedDescription)")
        }

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

    var errorDescription: String? {
        switch self {
        case .commandFailed(let cmd, let code):
            return "Command exited with status \(code): \(cmd)"
        case .missingPrerequisite(let what):
            return "Missing prerequisite: \(what)"
        case .userCancelled:
            return "Cancelled by user."
        case .fileNotFound(let path):
            return "File not found: \(path)"
        case .downloadFailed(let what):
            return "Download failed: \(what)"
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
        try await r.shell("curl -fsSL --retry 3 -o \"\(archive)\" \"\(sourceArchiveURL(tag))\"", workDir: r.buildBase)
        // GitHub can answer 200 with an HTML page (e.g. a release that is not public yet).
        do {
            try await r.shell("gzip -t \"\(archive)\"", workDir: r.buildBase)
        } catch {
            throw InstallerError.downloadFailed(
                "no source archive of \(tag) at \(repoWebURL); is the \(tag) release published?")
        }
        try await r.shell("tar -xzf \"\(archive)\" -C \"\(newDir)\" --strip-components 1", workDir: r.buildBase)
        try? fm.removeItem(atPath: archive)
        guard fm.fileExists(atPath: newDir + "/CMakeLists.txt") else {
            throw InstallerError.fileNotFound("CMakeLists.txt in the downloaded source")
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

    static func checkPrereqs(_ r: BuildRunner) async throws {
        func require(_ cmd: String, name: String, hint: String) async throws {
            do {
                try await r.shell("command -v \(cmd)")
                r.appendLog("  ✓ \(name)")
            } catch {
                throw InstallerError.missingPrerequisite("\(name) — \(hint)")
            }
        }

        let arch = try await r.shell("uname -m")
        guard arch.trimmingCharacters(in: .whitespacesAndNewlines) == "arm64" else {
            throw InstallerError.missingPrerequisite("Apple Silicon Mac required")
        }
        r.appendLog("  ✓ Apple Silicon")

        try await require("xcode-select", name: "Xcode CLI tools", hint: "run: xcode-select --install")
        try await r.shell("xcode-select -p")

        try await require("cmake", name: "CMake", hint: "run: brew install cmake")
        try await require("ninja", name: "Ninja", hint: "run: brew install ninja")
        try await require("python3", name: "Python 3", hint: "install Xcode CLI tools or Homebrew python")

        let gameApp = r.gameDir + "/Rocksmith2014.app"
        guard FileManager.default.fileExists(atPath: gameApp) else {
            throw InstallerError.fileNotFound("Rocksmith2014.app not found in \(r.gameDir)")
        }
        r.appendLog("  ✓ Game found at \(r.gameDir)")
    }

    static func setupVenv(_ r: BuildRunner) async throws {
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
        try await r.shell("build/venv/bin/pip install -r requirements.txt")
        try await r.shell(removeAppleDouble)
    }

    static func obtainGameImage(_ r: BuildRunner) async throws {
        let imagePath = r.repoRoot + "/build/steamless/Rocksmith2014_x64_decrypted"

        if FileManager.default.fileExists(atPath: imagePath) {
            r.appendLog("Game image already exists at \(imagePath), skipping decryption.")
            return
        }

        r.appendLog("The game binary needs to be decrypted from a running copy of Rocksmith 2014.")
        let env = ["RS_GAME_DIR": r.gameDir]

        // The game has to start after the setup step (which makes it load the dump dylib), so it
        // must not be running yet.
        let gameBin = r.gameDir + "/Rocksmith2014.app/Contents/MacOS/Rocksmith2014"
        while (try? await r.shell("pgrep -f \"^\(gameBin)( |$)\"")) != nil {
            let choice = showAlert(
                title: "Quit Rocksmith 2014",
                message: """
                    Rocksmith 2014 is running. Quit it first: the installer prepares the game, \
                    then asks you to start it again through Steam.
                    """,
                buttons: ["Try Again", "Cancel"]
            )
            if choice == "Cancel" { throw InstallerError.userCancelled }
        }

        let choice = showAlert(
            title: "Decrypt the game",
            message: """
                The installer now prepares your Steam copy of Rocksmith 2014 to save its decrypted \
                code, then asks you to start the game through Steam once. Your Steam copy is put back \
                as it was afterwards.

                If you already have a decrypted binary, click Skip and place it at:
                \(imagePath)
                """,
            buttons: ["Continue", "Skip", "Cancel"]
        )
        if choice == "Cancel" { throw InstallerError.userCancelled }
        if choice == "Skip" {
            guard FileManager.default.fileExists(atPath: imagePath) else {
                throw InstallerError.fileNotFound(imagePath)
            }
            return
        }

        do {
            try await r.shell("./tools/steamless/dump-decrypted.sh setup", env: env)
        } catch {
            _ = try? await r.shell("./tools/steamless/dump-decrypted.sh restore", env: env)
            throw error
        }

        let launch = showAlert(
            title: "Start Rocksmith 2014",
            message: """
                Start Rocksmith 2014 through Steam now. The installer continues by itself once the \
                game has been decrypted, usually within a minute of the game starting. You can quit \
                the game then.
                """,
            buttons: ["OK", "Cancel"]
        )
        if launch == "Cancel" {
            _ = try? await r.shell("./tools/steamless/dump-decrypted.sh restore", env: env)
            throw InstallerError.userCancelled
        }

        // wait restores the Steam binary however it ends, before patch-binary reads it.
        try await r.shell("./tools/steamless/dump-decrypted.sh wait", env: env)
        try await r.shell("python3 tools/steamless/patch-binary.py", env: env)

        guard FileManager.default.fileExists(atPath: imagePath) else {
            throw InstallerError.fileNotFound("Decrypted binary was not created: \(imagePath)")
        }
        r.appendLog("The game is decrypted: quit Rocksmith 2014 now. The build goes on without it.")
    }

    static func buildShims(_ r: BuildRunner) async throws {
        r.appendLog("Configuring CMake…")
        try await r.shell("cmake -S . -B build -G Ninja")

        r.appendLog("Building shims (Bink/FFmpeg, Cg, OpenGL, game libraries)…")
        try await r.shell("cmake --build build")

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

        // install.sh refuses to run while any Rocksmith2014 process is up, which is often still the Steam
        // copy started for the decryption step: ask to quit it rather than fail at the last step.
        while (try? await r.shell("pgrep -x Rocksmith2014")) != nil {
            let choice = showAlert(
                title: "Quit Rocksmith 2014",
                message: """
                    The app is built. Quit Rocksmith 2014 (still running from the decryption step, or \
                    the native game) so the installer can install it, then click Continue.
                    """,
                buttons: ["Continue", "Cancel"]
            )
            if choice == "Cancel" { throw InstallerError.userCancelled }
        }

        // The same install as a manual build: the game goes into ~/Library/Application Support/RS Re-ARMed/game
        // (the launcher mirrors the game folder there, since the game finds its files next to its .app) and
        // ~/Applications/RS Re-ARMed.app is the launcher.
        // The shader warm-up is a step of its own (prepareShaders).
        try await r.shell("tools/play-native/install.sh \"\(builtApp)\"",
                          env: ["RS_GAME_DIR": r.gameDir, "RS_WARM_SHADERS": "0", "RS_RENDERER": r.renderer])
        guard FileManager.default.fileExists(atPath: launcherApp + "/Contents/MacOS/launch") else {
            throw InstallerError.fileNotFound(launcherApp)
        }

        r.appendLog("\n══════════════════════════════════════════")
        r.appendLog("Installed: \(launcherApp)")
        r.appendLog("Game and saves: ~/Library/Application Support/RS Re-ARMed/")
        r.appendLog("Renderer: \(r.renderer == "metal" ? "Metal" : "OpenGL") (the file \"renderer\" there; "
            + "write metal or opengl into it to switch)")
        r.appendLog("To uninstall, delete both.")
        r.appendLog("══════════════════════════════════════════")
    }

    // The installed game compiles its Metal shaders once (warm-shaders.sh runs its executable with
    // --warm-shaders: no window), so its first start with Metal does not have to; "progress" lines
    // with "Preparing shaders… N of M" move the bar. Never fails the install: without it the game
    // prepares them on its first Metal start.
    static func prepareShaders(_ r: BuildRunner) async throws {
        r.appendLog("Compiling the game's Metal shaders, so its first start with Metal is quick. "
            + "The first time this takes a few minutes; after an update usually seconds.")
        r.shadersSkipped = false
        do {
            let out = try await r.shell("tools/play-native/warm-shaders.sh")
            r.shadersReady = out.contains("shader libraries ready")
        } catch {
            // Skipped (Skip stops it) or failed: the game prepares the rest on its first Metal start.
            r.appendLog(r.shadersSkipped
                ? "Shader preparation skipped: the game prepares the rest on its first start with Metal."
                : "Shader preparation stopped (\(error.localizedDescription)); the game prepares them on its "
                    + "first start with Metal.")
        }
    }

    static func cleanup(_ r: BuildRunner) async throws {
        let srcDir = r.buildBase + "/rs-rearmed"
        r.appendLog("Removing build files at \(srcDir)…")
        try? FileManager.default.removeItem(atPath: srcDir)
        r.appendLog("Done. Freed disk space from the downloaded source and build artifacts.")
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
    let showLogButton = NSButton(title: "Show Log File", target: nil, action: nil)
    let playButton = NSButton(title: "Start RS Re-ARMed", target: nil, action: nil)
    let cleanupCheck = NSButton(checkboxWithTitle: "Delete build files after install (saves ~5 GB)", target: nil, action: nil)
    let rendererPopup = NSPopUpButton(frame: .zero, pullsDown: false)

    init() {
        repoDetected = detectRepoRoot()

        let rect = NSRect(x: 0, y: 0, width: 720, height: 590)
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

        // The game folder the current install uses (install.sh's game-dir), else Steam's default library.
        let installedGameDir = (try? String(contentsOfFile: nativeHome + "/game-dir", encoding: .utf8))?
            .trimmingCharacters(in: .whitespacesAndNewlines)
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

        let subtitleLabel = NSTextField(labelWithString: "Builds Rocksmith 2014 natively for Apple Silicon from your Steam copy.")
        subtitleLabel.font = .systemFont(ofSize: 12)
        subtitleLabel.textColor = .secondaryLabelColor
        subtitleLabel.translatesAutoresizingMaskIntoConstraints = false
        content.addSubview(subtitleLabel)


        // Game directory
        let gameDirLabel = NSTextField(labelWithString: "Rocksmith 2014 game folder:")
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

        let rendererHint = NSTextField(labelWithString: "Change it later without reinstalling: the file \"renderer\" in ~/Library/Application Support/RS Re-ARMed")
        rendererHint.font = .systemFont(ofSize: 11)
        rendererHint.textColor = .secondaryLabelColor
        rendererHint.lineBreakMode = .byTruncatingTail
        rendererHint.setContentCompressionResistancePriority(.defaultLow, for: .horizontal)
        rendererHint.translatesAutoresizingMaskIntoConstraints = false
        content.addSubview(rendererHint)

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
        content.addSubview(showLogButton)

        playButton.bezelStyle = .rounded
        playButton.isHidden = true
        playButton.target = self
        playButton.action = #selector(startGame)
        playButton.translatesAutoresizingMaskIntoConstraints = false
        content.addSubview(playButton)

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

        NSLayoutConstraint.activate([
            titleLabel.topAnchor.constraint(equalTo: content.topAnchor, constant: 20),
            titleLabel.leadingAnchor.constraint(equalTo: content.leadingAnchor, constant: 20),

            versionLabel.lastBaselineAnchor.constraint(equalTo: titleLabel.lastBaselineAnchor),
            versionLabel.leadingAnchor.constraint(equalTo: titleLabel.trailingAnchor, constant: 8),

            subtitleLabel.topAnchor.constraint(equalTo: titleLabel.bottomAnchor, constant: 4),
            subtitleLabel.leadingAnchor.constraint(equalTo: content.leadingAnchor, constant: 20),

            gameDirLabel.topAnchor.constraint(equalTo: subtitleLabel.bottomAnchor, constant: 16),
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
            rendererHint.trailingAnchor.constraint(lessThanOrEqualTo: content.trailingAnchor, constant: -20),

            sourceLabel.topAnchor.constraint(equalTo: rendererPopup.bottomAnchor, constant: 12),
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

            scrollView.topAnchor.constraint(equalTo: stepLabel.bottomAnchor, constant: 10),
            scrollView.leadingAnchor.constraint(equalTo: content.leadingAnchor, constant: 20),
            scrollView.trailingAnchor.constraint(equalTo: content.trailingAnchor, constant: -20),
            scrollView.bottomAnchor.constraint(equalTo: startButton.topAnchor, constant: -12),

            startButton.bottomAnchor.constraint(equalTo: content.bottomAnchor, constant: -16),
            startButton.trailingAnchor.constraint(equalTo: content.trailingAnchor, constant: -20),
            startButton.widthAnchor.constraint(equalToConstant: 140),

            showLogButton.bottomAnchor.constraint(equalTo: content.bottomAnchor, constant: -16),
            showLogButton.leadingAnchor.constraint(equalTo: content.leadingAnchor, constant: 20),
            showLogButton.widthAnchor.constraint(equalToConstant: 140),

            playButton.bottomAnchor.constraint(equalTo: content.bottomAnchor, constant: -16),
            playButton.trailingAnchor.constraint(equalTo: startButton.leadingAnchor, constant: -8),
            playButton.widthAnchor.constraint(equalToConstant: 160),
        ])

        Timer.scheduledTimer(withTimeInterval: 0.1, repeats: true) { [weak self] _ in
            DispatchQueue.main.async { self?.updateUI() }
        }
    }

    private func updateUI() {
        progressBar.doubleValue = runner.progress
        stepLabel.stringValue = runner.isRunning
            ? "Step \(runner.currentStep)/\(runner.totalSteps): \(runner.stepTitle)"
            : runner.isFinished ? "✓ Installed: ~/Applications/RS Re-ARMed.app"
            : runner.failed ? "✗ Build failed (see the log)" : "Ready"
        if runner.isRunning && !runner.stepDetail.isEmpty {
            stepLabel.stringValue += " — \(runner.stepDetail) (\(Int(runner.stepFraction * 100))%)"
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
        playButton.isHidden = !runner.isFinished || runner.isRunning
        if runner.isFinished && !runner.isRunning { playButton.keyEquivalent = "\r"; startButton.keyEquivalent = "" }
        browseButton.isEnabled = !runner.isRunning
        gameDirField.isEditable = !runner.isRunning
        buildBrowseButton.isEnabled = !runner.isRunning
        buildDirField.isEditable = !runner.isRunning
        cleanupCheck.isEnabled = !runner.isRunning
        rendererPopup.isEnabled = !runner.isRunning
        showLogButton.isHidden = runner.logFileURL == nil

        let logText = runner.log
        if logView.string != logText {
            logView.string = logText
            logView.scrollToEndOfDocument(nil)
        }
    }

    @objc func browseGameDir() {
        let panel = NSOpenPanel()
        panel.canChooseFiles = false
        panel.canChooseDirectories = true
        panel.allowsMultipleSelection = false
        panel.message = "Select the Rocksmith 2014 game folder"
        panel.directoryURL = URL(fileURLWithPath: gameDirField.stringValue)
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
        runner.gameDir = gameDirField.stringValue
        if !runner.embeddedInRepo {
            runner.buildBase = buildBase(in: buildDirField.stringValue)
            buildDirField.stringValue = runner.buildBase
            if runner.buildBase == defaultBuildBase {
                UserDefaults.standard.removeObject(forKey: buildBaseDefaultsKey)
            } else {
                UserDefaults.standard.set(runner.buildBase, forKey: buildBaseDefaultsKey)
            }
        }
        // The recompiler's build scripts can't handle spaces in their own paths (the game folder may
        // have them).
        let buildPath = runner.embeddedInRepo ? runner.repoRoot : runner.buildBase
        if buildPath.contains(where: { $0.isWhitespace }) {
            _ = showAlert(
                title: "Choose a folder without spaces",
                message: """
                    The build can't run in a folder whose path contains spaces:
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
        Task { @MainActor in
            await runner.run()
            updateUI()
            finished()
        }
    }

    // Say plainly what happened and where things are, instead of leaving it to the log.
    private func finished() {
        let logPath = runner.logFileURL.map { ($0.path as NSString).abbreviatingWithTildeInPath } ?? logsDir
        if runner.isFinished {
            let choice = showAlert(
                title: "RS Re-ARMed is installed",
                message: """
                    Start it any time from Applications in your home folder (~/Applications/RS Re-ARMed.app), \
                    or with Launchpad or Spotlight.

                    The game build and your saves are in ~/Library/Application Support/RS Re-ARMed/. \
                    To uninstall, delete both.
                    \(runner.renderer != "metal" || runner.shadersReady ? "" : """

                    The game's Metal shaders are not prepared yet: its first start with Metal shows \
                    "Preparing shaders" for a few minutes.
                    """)

                    The installer log is in \(logPath).
                    """,
                buttons: ["Start RS Re-ARMed", "Show in Finder", "Close"]
            )
            if choice == "Start RS Re-ARMed" { startGame() }
            if choice == "Show in Finder" {
                NSWorkspace.shared.activateFileViewerSelecting([URL(fileURLWithPath: launcherApp)])
            }
        } else if runner.failed, let error = runner.failure, !(error is CancellationError) {
            if case InstallerError.userCancelled = error { return }
            let choice = showAlert(
                title: "The build failed",
                message: """
                    \(error.localizedDescription)

                    The full log is in \(logPath). Nothing was installed; fix the problem and click \
                    Build & Install again.
                    """,
                buttons: ["Show Log File", "Close"]
            )
            if choice == "Show Log File" { revealLog() }
        }
    }

    @objc func startGame() {
        NSWorkspace.shared.openApplication(at: URL(fileURLWithPath: launcherApp),
                                           configuration: NSWorkspace.OpenConfiguration())
    }

    @objc func revealLog() {
        if let url = runner.logFileURL {
            NSWorkspace.shared.activateFileViewerSelecting([url])
        }
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

class AppDelegate: NSObject, NSApplicationDelegate {
    var window: InstallerWindow!

    func applicationDidFinishLaunching(_ notification: Notification) {
        NSApp.mainMenu = makeMainMenu()
        window = InstallerWindow()
        window.makeKeyAndOrderFront(nil)

        Task {
            if let update = await checkForUpdate() {
                let choice = showAlert(
                    title: "Update Available",
                    message: "A newer version (\(update.tag)) is available. You are running v\(rsVersion).",
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
        let choice = showAlert(
            title: "Stop the installation?",
            message: "RS Re-ARMed is still being built and installed. Quitting stops it; click Build & "
                + "Install again later to finish.",
            buttons: ["Quit", "Continue"]
        )
        guard choice == "Quit" else { return .terminateCancel }
        runner.stopCurrentCommand()
        return .terminateNow
    }
}

// Main menu: without one there is no ⌘Q, and ⌘C/⌘A do nothing in the log and text fields.
@MainActor
func makeMainMenu() -> NSMenu {
    let main = NSMenu()

    let appMenu = NSMenu()
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

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

// MARK: - Build runner

@MainActor
final class BuildRunner {
    var currentStep = 0
    var totalSteps = 0
    var stepTitle = ""
    var progress: Double = 0
    var isRunning = false
    var isFinished = false
    var failed = false
    var log = ""

    var repoRoot = ""
    var gameDir = ""
    var buildBase = defaultBuildBase  // standalone only: holds the source, its build/ and the logs
    var embeddedInRepo = false
    var cleanupAfterInstall = false
    var logFileURL: URL?

    private var logFileHandle: FileHandle?

    func appendLog(_ text: String) {
        let line = text.hasSuffix("\n") ? text : text + "\n"
        log += line
        if let data = line.data(using: .utf8) {
            logFileHandle?.write(data)
        }
    }

    func openLogFile() {
        // When running standalone, logs go into the build folder (repo may not exist yet).
        let logsDir: URL
        if embeddedInRepo {
            logsDir = URL(fileURLWithPath: repoRoot).appendingPathComponent("build/installer-logs")
        } else {
            logsDir = URL(fileURLWithPath: buildBase).appendingPathComponent("logs")
        }
        try? FileManager.default.createDirectory(at: logsDir, withIntermediateDirectories: true)
        let formatter = DateFormatter()
        formatter.dateFormat = "yyyy-MM-dd_HHmmss"
        let name = "install-\(formatter.string(from: Date())).log"
        let url = logsDir.appendingPathComponent(name)
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
        let outputLock = NSLock()

        let runner = self
        handle.readabilityHandler = { fh in
            let data = fh.availableData
            guard !data.isEmpty, let str = String(data: data, encoding: .utf8) else { return }
            outputLock.lock()
            output += str
            outputLock.unlock()
            DispatchQueue.main.async {
                runner.appendLog(str.hasSuffix("\n") ? String(str.dropLast()) : str)
            }
        }

        try process.run()

        let status: Int32 = await withCheckedContinuation { (cont: CheckedContinuation<Int32, Never>) in
            DispatchQueue.global().async {
                process.waitUntilExit()
                handle.readabilityHandler = nil
                let rest = handle.readDataToEndOfFile()
                if !rest.isEmpty, let str = String(data: rest, encoding: .utf8) {
                    outputLock.lock()
                    output += str
                    outputLock.unlock()
                    DispatchQueue.main.async {
                        runner.appendLog(str.hasSuffix("\n") ? String(str.dropLast()) : str)
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
        if !embeddedInRepo && cleanupAfterInstall {
            steps.append(("Cleaning up", Steps.cleanup))
        }

        totalSteps = steps.count
        currentStep = 0

        do {
            for (i, (title, action)) in steps.enumerated() {
                currentStep = i + 1
                stepTitle = title
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
        try await r.shell("build/venv/bin/pip install -r requirements.txt")
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

        let installDir = NSHomeDirectory() + "/Applications"
        let appBundle = installDir + "/RS Re-ARMed.app"
        let fm = FileManager.default

        try fm.createDirectory(atPath: installDir, withIntermediateDirectories: true)

        if fm.fileExists(atPath: appBundle) {
            r.appendLog("Removing previous installation…")
            try fm.removeItem(atPath: appBundle)
        }

        r.appendLog("Copying built app to \(appBundle)…")
        try fm.copyItem(atPath: builtApp, toPath: appBundle)

        // Bundle libnetguard inside Contents/Frameworks/
        let fwDir = appBundle + "/Contents/Frameworks"
        try fm.createDirectory(atPath: fwDir, withIntermediateDirectories: true)
        let netguardSrc = r.repoRoot + "/build/ws8/libnetguard.dylib"
        guard fm.fileExists(atPath: netguardSrc) else {
            throw InstallerError.fileNotFound(netguardSrc)
        }
        try fm.copyItem(atPath: netguardSrc, toPath: fwDir + "/libnetguard.dylib")
        r.appendLog("  Bundled libnetguard.dylib")

        // Copy game icon from the Steam install
        let gameApp = r.gameDir + "/Rocksmith2014.app"
        let iconSrc = gameApp + "/Contents/Resources/RS2014.icns"
        let iconDst = appBundle + "/Contents/Resources/RS2014.icns"
        if fm.fileExists(atPath: iconSrc) && !fm.fileExists(atPath: iconDst) {
            try? fm.copyItem(atPath: iconSrc, toPath: iconDst)
            r.appendLog("  Copied game icon")
        }
        let enLproj = gameApp + "/Contents/Resources/en.lproj"
        let enDst = appBundle + "/Contents/Resources/en.lproj"
        if fm.fileExists(atPath: enLproj) && !fm.fileExists(atPath: enDst) {
            try? fm.copyItem(atPath: enLproj, toPath: enDst)
        }

        // Ensure Info.plist has the icon key and display name
        let plist = appBundle + "/Contents/Info.plist"
        if fm.fileExists(atPath: plist) {
            _ = try? await r.shell("""
                /usr/libexec/PlistBuddy -c 'Print :CFBundleIconFile' "\(plist)" 2>/dev/null || \
                plutil -insert CFBundleIconFile -string RS2014.icns "\(plist)"
                """)
            _ = try? await r.shell("""
                plutil -replace CFBundleName -string 'RS Re-ARMed' "\(plist)" 2>/dev/null
                plutil -replace CFBundleDisplayName -string 'RS Re-ARMed' "\(plist)" 2>/dev/null
                """)
        }

        // Replace the binary with a launcher wrapper; rename the real binary.
        let macosDir = appBundle + "/Contents/MacOS"
        let realBinary = macosDir + "/Rocksmith2014"
        let wrappedBinary = macosDir + "/Rocksmith2014-bin"

        try fm.moveItem(atPath: realBinary, toPath: wrappedBinary)

        let launcherScript = #"""
            #!/bin/bash
            # RS Re-ARMed self-contained launcher.
            # Everything lives inside this .app bundle — delete it to uninstall.
            set -u
            BUNDLE="$(cd "$(dirname "$0")/.." && pwd)"
            BINARY="$BUNDLE/MacOS/Rocksmith2014-bin"
            NETGUARD="$BUNDLE/Frameworks/libnetguard.dylib"

            GAME_DIR="${RS_GAME_DIR:-$HOME/Library/Application Support/Steam/steamapps/common/Rocksmith2014}"
            DATA_DIR="$HOME/Library/Application Support/RS Re-ARMed"

            fail() {
                /usr/bin/osascript -e "display alert \"RS Re-ARMed\" message \"$1\" as critical" >/dev/null 2>&1
                echo "launch: $1" >&2; exit 1
            }

            [[ -x "$BINARY" ]]   || fail "Game binary missing inside the app bundle."
            [[ -f "$NETGUARD" ]] || fail "libnetguard.dylib missing inside the app bundle."
            [[ -d "$GAME_DIR" ]] || fail "Rocksmith 2014 is not installed at $GAME_DIR."
            if pgrep -x Rocksmith2014 >/dev/null || pgrep -x Rocksmith2014-bin >/dev/null; then
                fail "Rocksmith 2014 is already running."
            fi

            mkdir -p "$DATA_DIR/steam" "$DATA_DIR/logs"
            logs="$DATA_DIR/logs"

            # Mirror game assets via symlinks.
            stage="$BUNDLE/Resources/game"
            mkdir -p "$stage"
            for e in "$GAME_DIR"/*; do
                n=$(basename "$e")
                case $n in Rocksmith2014.app|Rocksmith.ini) continue ;; esac
                [[ -e "$stage/$n" || -L "$stage/$n" ]] || ln -s "$e" "$stage/$n"
            done
            for l in "$stage"/*; do
                [[ -L $l && ! -e $l ]] && rm -f "$l"
            done
            if [[ -f "$GAME_DIR/static.psarc.rearmed" && "${RS_REARMED:-1}" != 0 ]]; then
                ln -sfn "$GAME_DIR/static.psarc.rearmed" "$stage/static.psarc"
            else
                ln -sfn "$GAME_DIR/static.psarc" "$stage/static.psarc"
            fi
            if [[ -f "$GAME_DIR/Rocksmith.ini" ]]; then
                awk '{ sub(/\r$/, "") }
                     /^\[/ { sec = $0 }
                     sec == "[Net]" && /^AutoLogin=/ { next }
                     sec == "[Renderer.MacOS]" && /^DisableBrowser=/ { print "DisableBrowser=1"; next }
                     { print }
                     $0 == "[Net]" { print "AutoLogin=0" }' "$GAME_DIR/Rocksmith.ini" > "$stage/Rocksmith.ini.new" &&
                    mv -f "$stage/Rocksmith.ini.new" "$stage/Rocksmith.ini"
            fi

            # Seed offline Steam cloud on first launch.
            for src in "$HOME/Library/Application Support/Steam/userdata"/*/221680/remote; do
                [[ -d $src && "${RS_NATIVE_SEED:-1}" != 0 ]] || continue
                acct=$(basename "$(dirname "$(dirname "$src")")")
                dst=$DATA_DIR/steam/$acct/221680/remote
                [[ -d $dst ]] && continue
                mkdir -p "$dst.tmp" && rsync -a --exclude crd "$src/" "$dst.tmp/" && mv "$dst.tmp" "$dst" &&
                    echo "seeded $dst from $src (without crd)" >> "$logs/seed.log"
            done
            find "$DATA_DIR/steam" -name crd -type f -delete 2>/dev/null

            [[ -f "$logs/last-run.log" ]] && mv -f "$logs/last-run.log" "$logs/previous-run.log"

            export RS_STEAM_MODE=stub
            export RS_STEAM_DATA_DIR="$DATA_DIR/steam"
            export RS_NETGUARD_LOG="$logs/netguard.log"
            export DYLD_INSERT_LIBRARIES="$NETGUARD"
            cd "$stage"
            exec "$BINARY" "$@" > "$logs/last-run.log" 2>&1
            """#
        try launcherScript.write(toFile: realBinary, atomically: true, encoding: .utf8)
        try fm.setAttributes([.posixPermissions: 0o755], ofItemAtPath: realBinary)

        r.appendLog("Signing the app bundle…")
        _ = try? await r.shell(#"codesign --force --deep -s - "\#(appBundle)" 2>/dev/null"#)

        _ = try? await r.shell("""
            lsregister=/System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister
            [[ -x $lsregister ]] && "$lsregister" -f "\(appBundle)" 2>/dev/null
            touch "\(appBundle)"
            """)

        r.appendLog("\n══════════════════════════════════════════")
        r.appendLog("Installed: \(appBundle)")
        r.appendLog("To uninstall, just delete RS Re-ARMed.app.")
        r.appendLog("Saves are in ~/Library/Application Support/RS Re-ARMed/")
        r.appendLog("══════════════════════════════════════════")
    }

    static func cleanup(_ r: BuildRunner) async throws {
        let srcDir = r.buildBase + "/rs-rearmed"
        r.appendLog("Removing build files at \(srcDir)…")
        try? FileManager.default.removeItem(atPath: srcDir)
        r.appendLog("Done. Freed disk space from the downloaded source and build artifacts.")
    }
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
    let cleanupCheck = NSButton(checkboxWithTitle: "Delete build files after install (saves ~5 GB)", target: nil, action: nil)

    init() {
        repoDetected = detectRepoRoot()

        let rect = NSRect(x: 0, y: 0, width: 720, height: 560)
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

        let defaultGameDir = NSHomeDirectory() + "/Library/Application Support/Steam/steamapps/common/Rocksmith2014"
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

            sourceLabel.topAnchor.constraint(equalTo: sourceTopAnchor, constant: 12),
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
        ])

        Timer.scheduledTimer(withTimeInterval: 0.1, repeats: true) { [weak self] _ in
            DispatchQueue.main.async { self?.updateUI() }
        }
    }

    private func updateUI() {
        progressBar.doubleValue = runner.progress
        stepLabel.stringValue = runner.isRunning
            ? "Step \(runner.currentStep)/\(runner.totalSteps): \(runner.stepTitle)"
            : runner.isFinished ? "✓ Installation complete" : runner.failed ? "✗ Build failed" : "Ready"
        progressLabel.stringValue = runner.isRunning ? "\(Int(runner.progress * 100))%" : ""
        startButton.isEnabled = !runner.isRunning
        startButton.title = runner.isFinished ? "Rebuild & Install" : "Build & Install"
        browseButton.isEnabled = !runner.isRunning
        gameDirField.isEditable = !runner.isRunning
        buildBrowseButton.isEnabled = !runner.isRunning
        buildDirField.isEditable = !runner.isRunning
        cleanupCheck.isEnabled = !runner.isRunning
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
        Task { @MainActor in
            await runner.run()
        }
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

let delegate = AppDelegate()
let app = NSApplication.shared
app.delegate = delegate
app.setActivationPolicy(.regular)
app.activate(ignoringOtherApps: true)
app.run()

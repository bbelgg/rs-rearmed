// The signature of a release's installer, which the installer checks before it replaces itself with a newer one
// (tools/installer/RSReARMedInstaller.swift, Self-update): Ed25519, over the bytes of the zip.
//
//   swift tools/release/sign.swift keygen KEYFILE        a new private key in KEYFILE (never over an old one);
//                                                        prints its public key
//   swift tools/release/sign.swift public KEYFILE        the public key of KEYFILE
//   swift tools/release/sign.swift sign KEYFILE FILE     the signature of FILE, on stdout
//   swift tools/release/sign.swift verify PUBLIC FILE SIGFILE    exit 0 when SIGFILE is PUBLIC's signature of FILE
//
// Keys and signatures are base64, one line. The private key is the maintainer's, outside the repository
// (package.sh reads its path from RS_UPDATE_KEY); the public key is updatePublicKey in the installer's source.
// Whoever has the private key can make an installer that every installed copy accepts as an update: keep it
// where the seed is kept, and never in a commit, a log or a message.

import CryptoKit
import Foundation

func die(_ what: String) -> Never {
    FileHandle.standardError.write(("error: " + what + "\n").data(using: .utf8)!)
    exit(2)
}

func privateKey(_ path: String) -> Curve25519.Signing.PrivateKey {
    guard let text = try? String(contentsOfFile: path, encoding: .utf8),
          let raw = Data(base64Encoded: text.trimmingCharacters(in: .whitespacesAndNewlines)),
          let key = try? Curve25519.Signing.PrivateKey(rawRepresentation: raw) else {
        die("\(path) is not a private key of this tool")
    }
    return key
}

func bytes(_ path: String) -> Data {
    guard let data = FileManager.default.contents(atPath: path) else { die("cannot read \(path)") }
    return data
}

let a = Array(CommandLine.arguments.dropFirst())
switch (a.first ?? "", a.count) {
case ("keygen", 2):
    let key = Curve25519.Signing.PrivateKey()
    // Created, never opened: a key that is there is the one the released installers trust.
    let fd = open(a[1], O_WRONLY | O_CREAT | O_EXCL, 0o600)
    if fd < 0 { die("\(a[1]): \(String(cString: strerror(errno))) (a key that exists is never written over)") }
    let line = (key.rawRepresentation.base64EncodedString() + "\n").data(using: .utf8)!
    let wrote = line.withUnsafeBytes { write(fd, $0.baseAddress, $0.count) }
    close(fd)
    if wrote != line.count { die("could not write \(a[1])") }
    print(key.publicKey.rawRepresentation.base64EncodedString())
case ("public", 2):
    print(privateKey(a[1]).publicKey.rawRepresentation.base64EncodedString())
case ("sign", 3):
    guard let sig = try? privateKey(a[1]).signature(for: bytes(a[2])) else { die("could not sign \(a[2])") }
    print(sig.base64EncodedString())
case ("verify", 4):
    guard let raw = Data(base64Encoded: a[1]), let key = try? Curve25519.Signing.PublicKey(rawRepresentation: raw) else {
        die("\(a[1]) is not a public key")
    }
    guard let text = try? String(contentsOfFile: a[3], encoding: .utf8),
          let sig = Data(base64Encoded: text.trimmingCharacters(in: .whitespacesAndNewlines)) else {
        die("\(a[3]) is not a signature")
    }
    exit(key.isValidSignature(sig, for: bytes(a[2])) ? 0 : 1)
default:
    die("usage: sign.swift keygen KEYFILE | public KEYFILE | sign KEYFILE FILE | verify PUBLIC FILE SIGFILE")
}

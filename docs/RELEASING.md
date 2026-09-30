# Releasing PCASynth

Every push builds and tests PCASynth on Linux and macOS (see
`.github/workflows/build.yml`). The macOS job produces two downloads (the
`PCASynth-macOS` artifact):
- `PCASynth-<version>-macOS.zip`: the VST3, the Standalone app, and the
  manual;
- `PCASynth-<version>-macOS.pkg`: an installer for the VST3, plus the
  Standalone app if chosen.

Without signing secrets, these builds are **ad-hoc signed**. They work, but
macOS Gatekeeper warns about them, and users must clear the quarantine flag
(see the README). A public release should be **signed with a Developer ID
and notarized** by Apple. The workflow does this when the secrets below
exist.

## 1. One-time setup: certificates and secrets

You need an Apple Developer Program membership (paid). The account holder
creates the certificates.

1. **Create two certificates** in Xcode (Settings → Accounts → Manage
   Certificates → +) or at developer.apple.com → Certificates:
   - **Developer ID Application**: signs the plug-in and the app.
   - **Developer ID Installer**: signs the `.pkg`.
2. **Export both** from Keychain Access into one `.p12` file:
   - select both certificates, each with its private key;
   - choose File → Export Items;
   - set a password.
3. **Create an app-specific password** for notarization at
   [appleid.apple.com](https://appleid.apple.com) (Sign-In and Security →
   App-Specific Passwords).
4. **Find your Team ID**: developer.apple.com → Membership (10 characters).
5. **Add repository secrets** (GitHub → Settings → Secrets and variables →
   Actions → New repository secret):

| Secret | Value |
|---|---|
| `MACOS_CERTS_P12` | The `.p12` file, base64-encoded: `base64 -i certs.p12 \| pbcopy` |
| `MACOS_CERTS_PASSWORD` | The `.p12` export password |
| `MACOS_SIGN_APP` | The certificate's full name, e.g. `Developer ID Application: Matthew Crump (ABCDE12345)` |
| `MACOS_SIGN_INSTALLER` | e.g. `Developer ID Installer: Matthew Crump (ABCDE12345)` |
| `APPLE_ID` | The Apple ID email used for notarization |
| `APPLE_TEAM_ID` | The Team ID |
| `APPLE_APP_PASSWORD` | The app-specific password |

The exact certificate names are listed by `security find-identity -v` on the
Mac that holds them.

What the secrets turn on:
- **The signing secrets** (the first four): every build signs with your
  Developer ID and the hardened runtime.
- **All seven**: builds of tags are also **notarized** and **stapled**. This
  adds a few minutes, so ordinary pushes skip it.

## 2. Making a release

1. Make sure the branch you are releasing has green CI.
2. Set the version:
   - `project(PCASynth VERSION x.y.z)` in `CMakeLists.txt` (the plugin
     reports this to hosts);
   - a `## [x.y.z] - date` section at the top of `CHANGELOG.md` (the release
     notes come from it);
   - the version line at the top of `docs/manual.md`.
3. Commit, then tag and push the tag:

   ```sh
   git tag v0.8.0
   git push origin v0.8.0
   ```

4. The workflow builds, tests, signs and notarizes. The `release` job then
   creates a GitHub release named "PCASynth x.y.z" with the zip and the pkg,
   using that version's changelog section as the notes. Versions 0.x are
   marked as pre-releases.

## 3. Checking a signed release

On a Mac, after downloading:

```sh
# The installer: signed, notarized (stapled ticket)
pkgutil --check-signature PCASynth-0.8.0-macOS.pkg
spctl --assess --type install --verbose PCASynth-0.8.0-macOS.pkg

# The bundles from the zip
codesign --verify --deep --strict --verbose=2 PCASynth.vst3 PCASynth.app
spctl --assess --type execute --verbose PCASynth.app
xcrun stapler validate PCASynth.vst3
```

`spctl` should report `accepted` with `source=Notarized Developer ID`.

## 4. Before tagging: a manual check in a DAW

CI runs the unit tests, the headless plugin checks and pluginval at the
strictest level. Before a release, also check by hand, in Live (or another
VST3 host):
- [ ] The plug-in scans and opens; the window resizes.
- [ ] Factory presets load from the preset menu; prev/next step through them.
- [ ] A saved user preset loads in a new project.
- [ ] A project saves and reopens with the same sound, including a trained
      space.
- [ ] Training on a folder of WAVs works while the transport plays.
- [ ] Random walk synced to tempo follows tempo changes.
- [ ] MPE on the Osmose (or another MPE controller): per-note bend,
      pressure and slide; notes do not leak into each other.
- [ ] CPU in the DAW's meter is in line with `pcs-bench` (manual §12).
- [ ] Offline bounce matches real-time playback.

## Building a release locally

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"
cmake --build build --parallel
ctest --test-dir build
MACOS_SIGN_APP="Developer ID Application: …" MACOS_SIGN_INSTALLER="Developer ID Installer: …" \
APPLE_ID=… APPLE_TEAM_ID=… APPLE_APP_PASSWORD=… \
  scripts/package_macos.sh build/plugin/PCASynth_artefacts/Release dist
```

Leave out the variables for an ad-hoc signed build.

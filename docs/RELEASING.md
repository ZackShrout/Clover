# Releasing Clover

Clover's `Cross-platform release` GitHub Actions workflow builds distributable
applications on native hosted runners:

- Windows x64: portable ZIP and NSIS installer
- macOS: universal Apple silicon and Intel DMG
- Linux x86-64: AppImage

Commercial ROMs, firmware, and Nintendo assets are never included.

## Continuous cross-platform builds

Every push to `main` builds and tests all three packages. Open the workflow run
on GitHub and download its artifacts to test them. These continuous artifacts
are not added to the repository's Releases page.

The workflow can also be started without a push from **Actions →
Cross-platform release → Run workflow**.

## Publish a beta

1. Make sure `main` is green.
2. Choose a semantic prerelease version, for example `0.1.0-beta.1`.
3. Create and push an annotated tag:

   ```bash
   git tag -a v0.1.0-beta.1 -m "Clover 0.1.0 beta 1"
   git push origin v0.1.0-beta.1
   ```

4. Watch the `Cross-platform release` workflow.
5. Test the three packages from the draft-independent GitHub prerelease.

Tags containing a hyphen are marked as prereleases. A tag such as `v0.1.0`
is published as a normal release. The workflow will not publish unless every
platform build and test succeeds.

Each release includes `SHA256SUMS.txt`. GitHub also provides source archives
for the exact tag; these are the corresponding sources and relinkable build
inputs for Clover's LGPL-covered SPC DSP module.

## macOS signing and notarization

Without signing secrets, CI applies an ad-hoc signature so the bundle remains
structurally valid, but Gatekeeper will warn testers. For normal
double-clickable distribution, join the Apple Developer Program and add these
GitHub Actions repository secrets:

- `MACOS_CERTIFICATE`: base64-encoded Developer ID Application `.p12`
- `MACOS_CERTIFICATE_PASSWORD`: password used when exporting the `.p12`
- `MACOS_SIGNING_IDENTITY`: full identity, such as
  `Developer ID Application: BunnySoft (...)`
- `APPLE_ID`: Apple ID used for notarization
- `APPLE_APP_PASSWORD`: app-specific password for that Apple ID
- `APPLE_TEAM_ID`: Apple Developer team identifier

Encode the certificate on macOS with:

```bash
base64 -i DeveloperIDApplication.p12 | pbcopy
```

The credentials are imported only into a temporary CI keychain. Tagged builds
are submitted with `notarytool`, and the successful notarization ticket is
stapled to the DMG.

## Windows signing

The Windows beta includes both a portable ZIP and an installer with an
uninstaller and Start Menu integration. Both can be unsigned initially.
Windows SmartScreen may warn testers until Clover has a trusted Authenticode
certificate and reputation. Signing can be added without changing the
application package layout.

## Linux notes

The AppImage targets x86-64 desktop Linux. ARM Linux devices require a separate
artifact. If a desktop refuses to launch an AppImage directly, mark it
executable in the file properties or run:

```bash
chmod +x Clover-*-linux-x86_64.AppImage
```

## Website downloads

A website can link to the GitHub Releases page immediately. For a particular
version, link directly to that release's three assets. Avoid using
`releases/latest` for beta buttons because GitHub's latest stable release and
latest prerelease are different concepts.

## Version locations

The application version is declared in the top-level `project(...)` call in
`CMakeLists.txt` and as `version-semver` in `vcpkg.json`. Update both before
the corresponding release series. Artifact names use the Git tag, while
application metadata uses the CMake project version.

## Dependency and license policy

`vcpkg.json` pins Clover's dependency names, and the release workflow pins the
vcpkg revision used to resolve SDL3 and SQLite. Windows releases additionally
require Visual Studio 2022's clang-cl 19.1.5 and the MSVC 14.44 platform
toolset; configuration and binary-fingerprint checks stop the build if the
hosted runner drifts from that combination. Distributable packages include:

- `THIRD_PARTY_NOTICES.md`
- `LICENSES/LGPL-2.1.txt`
- `LICENSES/SDL3.txt`
- `LICENSES/Snes9x.txt`

Clover is distributed free of charge and must remain non-commercial while it
contains Snes9x-derived DSP material under the current upstream terms.

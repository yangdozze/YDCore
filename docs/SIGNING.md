# GLOBUS — Windows code-signing guide

## Current status

**The published GLOBUS artifacts are UNSIGNED development builds.** Windows
SmartScreen therefore shows *"Windows protected your PC — Unknown publisher"*
when running the installer. This is expected for unsigned software and is not
bypassed, disabled or worked around by this project. The CI pipeline is
signing-ready: the moment valid credentials are added as encrypted repository
secrets, every build is signed and verified automatically.

## What removes the warning (and what does not)

A publicly trusted **Authenticode** signature (SHA-256 digest, RFC 3161
timestamp) replaces "Unknown publisher" with the verified publisher name.
Note honestly: brand-new signed files can still trigger *reputation-based*
SmartScreen prompts until Microsoft has seen enough downloads — an OV
certificate builds reputation over time; EV certificates and Azure Trusted
Signing typically start with better standing. **No metadata trick, manifest
change, icon, or version resource affects trust.** Never disable SmartScreen,
edit user registry settings, use a self-signed certificate in production, or
fabricate certificate identities.

"Ninth Parallel Audio" is currently a fictional brand. A certificate authority
will only put a name on a certificate after legally validating it. Until such
an entity exists, the verified publisher will be one of:

- a **verified individual** (the developer's legal name, OV individual cert),
- a **verified organization** (a registered company),
- **SignPath Foundation** (for accepted open-source projects — the publisher
  shown is the foundation, with the project named in the certificate's OU).

## Supported signing routes

### 1. Azure Trusted Signing (integrated in CI — preferred)

1. Create an Azure *Trusted Signing* account + certificate profile
   (identity validation performed by Microsoft; individual or organization).
2. Create an Entra service principal with the *Trusted Signing Certificate
   Profile Signer* role.
3. Add these **encrypted GitHub secrets** (Settings → Secrets → Actions):
   `AZURE_TENANT_ID`, `AZURE_CLIENT_ID`, `AZURE_CLIENT_SECRET`,
   `ACS_ENDPOINT` (e.g. `https://eus.codesigning.azure.net`),
   `ACS_ACCOUNT`, `ACS_PROFILE`.
4. Push to `main`. The workflow detects the secrets and automatically:
   - signs the PE module inside `GLOBUS.vst3` and `GLOBUS.exe`,
   - verifies them (`signtool verify /pa /all /v`),
   - builds the installer from the **signed** binaries (Inno's uninstaller
     inherits the signed setup executable; for a signed uninstaller stub use
     Inno's `SignTool=` directive with the same profile),
   - signs and verifies `GLOBUS-Installer-Windows-x64.exe`
     (RFC 3161 timestamp: `http://timestamp.acs.microsoft.com`),
   - runs the silent install/uninstall test against the signed installer,
   - publishes with release notes stating the signed status.

### 2. OV/EV Authenticode certificate (manual/CI signtool)

Buy an OV or EV certificate from a trusted CA (identity validation required;
EV requires hardware/cloud key storage). Then sign in this exact order:

```bat
signtool sign /fd SHA256 /tr http://<CA-timestamp-url> /td SHA256 ^
  "GLOBUS.vst3\Contents\x86_64-win\GLOBUS.vst3"
signtool sign /fd SHA256 /tr http://<CA-timestamp-url> /td SHA256 GLOBUS.exe
iscc installer\GLOBUS-Installer.iss
signtool sign /fd SHA256 /tr http://<CA-timestamp-url> /td SHA256 ^
  installer\Output\GLOBUS-Installer-Windows-x64.exe
signtool verify /pa /all /v installer\Output\GLOBUS-Installer-Windows-x64.exe
```

Keys belong in an HSM/cloud KMS or GitHub encrypted secrets — **never in the
repository**.

### 3. SignPath Foundation (open source)

Apply at signpath.org with this repository (AGPL-3.0 qualifies). If accepted,
integrate their GitHub Actions connector using the provided project/signing
policy slugs stored as secrets; the publisher shown to users is SignPath
Foundation.

## Verification checklist (performed by CI when signing is active)

- `signtool verify /pa /all /v <file>` succeeds for the VST3 module, the
  standalone exe and the installer.
- Certificate chain terminates in a trusted root.
- Timestamp present and valid (signature outlives certificate expiry).
- The publisher name matches the validated identity.
- Installer test confirms the *installed* files are byte-identical to the
  signed build outputs (SHA-256).

Record for each release: file SHA-256 (published as `SHA256SUMS.txt`), signer
identity, signtool output, timestamp authority, SmartScreen behaviour on a
clean Windows 10/11 VM.

## Never commit

Private keys, PFX files, passwords, Azure credentials, client secrets,
signing tokens or SignPath secrets. Use GitHub encrypted secrets or OIDC
federation only. This repository contains none of these by design.

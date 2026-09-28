# SARA Windows code signing

SARA branch builds may be produced as unsigned development artifacts for internal testing.
A version-tag release must not be published as a normal SARA installer unless the installer
and packaged executables are Authenticode signed with a publicly trusted identity.

## Why this matters

Unsigned or low-reputation Windows executables may be blocked or warned on by browsers,
Microsoft Defender SmartScreen, and other endpoint-security products. Renaming, wrapping,
or hiding the executable does not solve the trust problem and is not the release strategy.

The SARA workflow therefore distinguishes:

- `SARA-Setup-1.0.15` — produced only when Artifact Signing is configured and the
  installer passes Authenticode verification.
- `SARA-Setup-1.0.15-UNSIGNED-DEVELOPMENT` — branch-only development artifact.
  It includes a SHA-256/status file and is not a production release.
- version tags such as `v1.0.15` — rejected if trusted signing configuration is absent.

## Microsoft Artifact Signing prerequisites

Use a Public Trust Artifact Signing certificate profile for broadly distributed Windows
software. Complete Microsoft identity validation before configuring GitHub Actions.

The workflow already uses GitHub OIDC and requires these repository secrets:

- `AZURE_CLIENT_ID`
- `AZURE_TENANT_ID`
- `AZURE_SUBSCRIPTION_ID`
- `ARTIFACT_SIGNING_ENDPOINT`
- `ARTIFACT_SIGNING_ACCOUNT`
- `ARTIFACT_SIGNING_PROFILE`

The Microsoft Entra application or managed identity used by GitHub must have a federated
credential for this repository/workflow and must be assigned the Artifact Signing
Certificate Profile Signer role for the certificate profile.

## Expected signed-build flow

1. GitHub authenticates to Azure using OIDC.
2. `azure/artifact-signing-action@v2` signs packaged SARA executables.
3. The workflow verifies those executable signatures with `Get-AuthenticodeSignature`.
4. Inno Setup builds `SARA-Setup-1.0.15.exe`.
5. Artifact Signing signs the installer.
6. The workflow verifies the installer Authenticode signature.
7. The final signed installer SHA-256 and signer status are recorded.
8. Only then is the normal `SARA-Setup-1.0.15` artifact published.

## Internal development builds

The unsigned-development artifact is useful for controlled recovery testing, but it is
expected to have no trusted publisher identity. Always compare its SHA-256 with the
included `SARA-Setup-1.0.15-SHA256.txt` file before internal testing.

Do not disable browser protection or Microsoft Defender as a distribution strategy.
The production fix is trusted Authenticode signing.

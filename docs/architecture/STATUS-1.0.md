# Sentinel 1.0 Development Status

Sentinel 1.0 is an integrated development release of the local-first Windows evidence and investigation workstation.

## Operational in this build
- Native Windows GUI
- Encrypted case metadata and evidence containers
- DPAPI-protected workstation master key and per-case AES-256-GCM keys
- SHA-256 evidence integrity and hash-linked audit verification
- Simulation Lab with copyable transcript, scrolling, human-paced response delay, Enter-to-send, model discovery and OpenAI-compatible local model support
- Persistent model, persona, scenario, age-state and response-delay settings
- Persona & Policy page with independent age-knowledge state
- Policy evaluation of generated suggestions
- Provider-independent IMessageAdapter architecture and local test adapter
- Human/supervisor approval queue with action hashes
- Simulation transcript preservation into encrypted case evidence
- Agency Server configuration and offline encrypted-sync work queue foundation
- Windows product/version metadata
- Authenticode signing pipeline support
- Per-user installer/uninstaller scripts

## Requires external infrastructure before operational deployment
- Real agency server transport/authentication
- Real third-party messaging provider adapters
- Production identity/role directory
- Trusted Authenticode signing identity and publisher reputation
- Agency policy package signing/distribution backend
- Managed updater/release service

## Safety boundary
Model output is advisory. Sentinel does not autonomously send messages to an external service. Outbound messaging architecture requires an operator/supervisor-approved action path.

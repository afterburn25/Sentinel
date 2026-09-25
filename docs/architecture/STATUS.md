# Build status

## Complete bootstrap pieces
- project skeleton
- GitHub Actions Windows/MSVC CI build
- vcpkg SQLite dependency manifest
- packaged build artifact + SHA-256 manifest generation
- domain UUIDs
- Windows cryptographic primitives
- SQLite migrations
- case repository baseline
- audit chaining baseline
- SEV1 authenticated container baseline
- SEV1 decrypt/authentication path
- Windows CI tamper test for ciphertext modification
- Windows CI DPAPI round-trip test
- DPAPI-protected workstation master key manager
- AES-GCM-wrapped per-case DEKs with case/version-bound AAD
- Windows CI case-key persistence/reopen test

## Security correction completed
SEV1 now finalizes all authenticated header fields, including `ciphertextSize`, before the header is supplied as AES-GCM associated data. The same exact header bytes are used during decryption/authentication. This prevents an encrypt/decrypt AAD mismatch.

## Current explicit limitations
- case fields are still plaintext in the bootstrap migration
- case-key manager and wrapped per-case DEKs are not complete
- SEV1 payload buffering is limited to 256 MiB until the chunked format is implemented
- evidence DB persistence/recovery state machine is not complete
- WinUI 3 shell is not included yet

## Next implementation slice
1. Encrypted case field codec and migration from plaintext bootstrap schema.
2. Transactional secure-case creation that couples case row + wrapped DEK.
3. Streaming/chunked SEV payload encryption.
4. Evidence database commit + staging recovery state machine.
5. Database/container cross-verification.
6. Expanded tamper and crash-recovery tests.
7. WinUI 3 shell after the secure core passes tests.

# Security Policy — Nezha Cyber (哪吒网络安全)

## Authorized Use Only

**Nezha Cyber (哪吒网络安全)** is an open-source hardware security, RF/wireless research, and educational toolkit released under AGPL-3.0. All wireless, RF, BLE, IR, and network diagnostic features must be used **exclusively** on devices, networks, and spectrum you own or have explicit written authorization to test.

## Supported Versions

Security fixes and firmware updates are provided on the latest `main` branch and the most recent tagged release:

| Version / Branch | Supported |
| :--- | :--- |
| `main` (latest) | Yes |
| Latest tagged release (`v*`) | Yes |
| Older releases / archived branches | No |

## Reporting a Vulnerability

If you discover a security vulnerability in **Nezha Cyber** (such as WebUI authentication bypass, memory corruption, arbitrary code execution, or unsafe defaults):

1. **Do not** open a public GitHub Issue with exploit details before a fix is available.
2. Report privately via **GitHub Security Advisories** (`Security → Advisories → Report a vulnerability`) or contact the maintainer **钟智强 (ctkqiang)** directly via GitHub.
3. Include:
   - Affected board target (`lilygo-t-deck`, `m5stack-cplus1_1`, etc.) and commit SHA
   - Clear reproduction steps or proof-of-concept
   - Impact assessment and suggested remediation (if available)

We aim to acknowledge reports within **72 hours** and coordinate responsible disclosure once a patch is verified.

# Third-party notices

FM1 DRUM OS is a direct GPL-3.0-only adaptation of SLOOP2.6/Felucca. Original source headers and copyrights remain intact. Upstream: https://github.com/isod89/sloop-fm1 and the Felucca work credited to Leo Kuroshita (@kurogedelic), Hügelton Instruments. The product-facing name/splash does not remove those legal attributions.

| Component | License / retained notice |
|---|---|
| Firmware/editor code derived from SLOOP/Felucca | GPL-3.0-only, root `LICENSE`, original source headers |
| MSFA-derived table provenance in `gen_tables.py` | Apache-2.0, `docs/licenses/Apache-2.0-msfa.txt`; Google Inc., Pascal Gauthier and the Felucca C-port credits retained |
| Terminus bitmap font | SIL OFL1.1, `firmware/assets/fonts/Terminus-LICENSE.txt` |
| Fukiai icon atlas, Hügelton Instruments | MIT, `docs/licenses/Fukiai-MIT.txt`; original notice retained even though the old web font/site is not included |
| Barlow Condensed / IBM Plex Mono | OFL notices in `drum-os-editor/public/licenses/`; exact @fontsource versions locked |
| Final55-sample embedded bank | Owner-confirmed distribution permission, `docs/AUDIO_NOTICE.md`; no CC0 claim, original WAVs excluded |
| JieLi AC79 SDK/SPL/package resources | External dependency, SDK Apache notice in `docs/licenses/AC79-SDK-LICENSE.txt`; SDK/toolchain binaries not included in this repository |

The MSFA notice mentions historical files not shipped in this drum-only subset; its table provenance remains relevant. The SDK/compiler remain separately obtained dependencies under their original terms. The package identity/encryption constants in build tools are public format constants, not account credentials. npm dependencies retain their respective package licenses and pinned integrity entries; no node_modules or vendor SDK/compiler is checked in.

External build tools: Python/Pillow (Pillow MIT-CMU), TypeScript and Playwright (Apache-2.0), Vite/tooling (MIT and other licenses recorded in package-lock). They are installed separately, not distributed binaries. Lightning CSS (MPL-2.0) is a development dependency; font files retain OFL notices.

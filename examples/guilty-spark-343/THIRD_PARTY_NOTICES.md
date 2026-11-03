# References and third-party notices

Guilty Spark 343 is authored inside the independent Reclaimer project. The C desktop application, native HTTP host, state adapters and automatic importer are newly implemented. The advanced browser workspace includes a modified MIT-licensed GobboNet front-end. Compatibility does not imply endorsement or complete Ollama server parity.

- GobboNet: https://github.com/ElodineOfficial/GobboNet, MIT, Copyright (c) 2026 Elodine. The earlier independent importer used observed data shapes from snapshot `f1b9f50a96f7caf9f2d9bafeb63286d3eb4441ab`. Native version 0.9 includes the browser front-end from the operator's GobboNet-ai2go checkout at `781eb43418297afbb2a75c5a4ff81a0b17c2b95d`, with integration and branding changes. `native/web/UPSTREAM.json` records original file hashes; `native/web/LICENSE` retains the complete MIT notice. GobboNet Go/PowerShell gateway, launchers, services and runtime are not bundled.
- Ollama API references and source evidence: [existing reconstruction record](../ollama-compatible/EVIDENCE.md).
- llama.cpp is a separately supplied MIT-licensed runtime. Retain the license shipped with your runtime distribution. The locally tested release and checksum are recorded in the existing reconstruction evidence; binaries are not committed.
- Model weights are separately supplied, remain read-only, and retain their original licenses. This repository does not redistribute them.
- The native C desktop application vendors cJSON v1.7.19 under MIT, with the complete notice in `native/vendor/LICENSE.cJSON.txt`. A native Windows package also includes the selected llama.cpp runtime's MIT license and LLVM OpenMP notice. See the native README for pinned versions and checksums.

For the GobboNet reference, the MIT notice is retained here:

Copyright (c) 2026 Elodine

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

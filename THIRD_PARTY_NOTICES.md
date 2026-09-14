# Third-Party Notices

This file reproduces the notices for third-party code incorporated into
distributed s3g-dsp binaries. Full license text is retained where
required by the applicable redistribution terms.

## CLAP

The CLAP API headers are incorporated into the distributed plugin binaries and
standalone applications that embed CLAP processors. CMake fetches the pinned
headers when `S3G_CLAP_INCLUDE_DIR` is not supplied.

- Project: <https://github.com/free-audio/clap>
- License: MIT
- Copyright: Copyright (c) 2021 Alexandre BIQUE

CLAP license notice:

```text
MIT License

Copyright (c) 2021 Alexandre BIQUE

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

## VSTGUI

VSTGUI is statically linked into portable s3g-dsp plugin editors. CMake fetches
the pinned source when `S3G_ENABLE_PORTABLE_CLAP_GUI=ON` on macOS or Windows.

- Project: <https://github.com/steinbergmedia/vstgui>
- License: BSD-style 3-clause license
- Copyright: Copyright (c) 2022 Steinberg Media Technologies

VSTGUI license notice:

```text
Copyright (c) 2022, Steinberg Media Technologies, All Rights Reserved

Redistribution and use in source and binary forms, with or without modification,
are permitted provided that the following conditions are met:

- Redistributions of source code must retain the above copyright notice,
  this list of conditions and the following disclaimer.
- Redistributions in binary form must reproduce the above copyright notice,
  this list of conditions and the following disclaimer in the documentation
  and/or other materials provided with the distribution.
- Neither the name of the Steinberg Media Technologies nor the names of its
  contributors may be used to endorse or promote products derived from this
  software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```

## IBM Plex Mono (Tracker)

Tracker bundles IBM Plex Mono Regular, Medium and SemiBold for its grid and
reference typography. The fonts are loaded privately on macOS and Windows.

- Copyright: Copyright © 2017 IBM Corp. with Reserved Font Name "Plex"
- License: SIL Open Font License 1.1
- Full notice: `tracker/resources/fonts/OFL.txt` in source;
  `Resources/Fonts/OFL.txt` in the Windows Tracker package.

## Source Code Pro

Source Code Pro Regular is retained as a portable UI font candidate.

- Project: <https://github.com/adobe-fonts/source-code-pro>
- Version: 2.042R-u/1.062R-i/1.026R-vf (upright font version 2.042)
- Font: `assets/fonts/SourceCodePro-Regular.ttf`
- SHA-256: `74bd80d3e42a08517cd7e1108ba3d86f2da29ac0f3065be95e0357956ab9db37`
- License: SIL Open Font License 1.1
- Full notice: `assets/fonts/SourceCodePro-LICENSE.md`

## dr_wav

The Windows Sample Player statically incorporates `dr_wav` for sample-file
decoding. No separate runtime library is distributed or required.

- Project: <https://github.com/mackron/dr_libs>
- Component: `dr_wav` 0.14.6
- Commit: `dfe8377631000664666519fdb83da193fd8037f4`
- License selection: Public Domain

## Fira Code

Fira Code Regular is bundled with the portable plugin editors so macOS and
Windows use the same static font data and the same default slashed-zero glyph.

- Project: <https://github.com/tonsky/FiraCode>
- Version: 6.2
- Font: `assets/fonts/FiraCode-Regular.ttf`
- SHA-256: `5992ab9640e2df491b2f609467b1de60e8bc39b2c28db184342a0592d98f6117`
- License: SIL Open Font License 1.1
- Full notice: `assets/fonts/FiraCode-LICENSE.txt`

## WORLD

WORLD is statically linked into the distributed Ambi Vox Encoder CLAP plugin.
It provides speech analysis and synthesis for the plugin's vocal WAV and
voicebank alias paths. Source builds can omit it with `S3G_ENABLE_WORLD=OFF`.

- Project: <https://github.com/mmorise/World>
- License: BSD-style 3-clause license
- Copyright: Copyright (c) 2010 M. Morise

WORLD license notice:

```text
WORLD: High-quality speech analysis,
manipulation and synthesis system
developed by M. Morise
http://www.kisc.meiji.ac.jp/~mmorise/world/english/

Copyright (c) 2010  M. Morise

All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

- Redistributions of source code must retain the above copyright
  notice, this list of conditions and the following disclaimer.
- Redistributions in binary form must reproduce the above
  copyright notice, this list of conditions and the following
  disclaimer in the documentation and/or other materials provided
  with the distribution.
- Neither the name of the M. Morise nor the names of its
  contributors may be used to endorse or promote products derived
  from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND
CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,
INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.
```

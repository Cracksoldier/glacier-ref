# Third-party notices

Glacier Ref's own source code is licensed under the MIT license (see
`LICENSE`). The components below keep their own licenses; the MIT label does
not relicense them.

## Source code included in this repository

### miniz 3.1.2

- Location: `third_party/miniz/`
- Upstream: https://github.com/richgel999/miniz
- License: MIT

```
Copyright 2013-2014 RAD Game Tools and Valve Software
Copyright 2010-2014 Rich Geldreich and Tenacious Software LLC

All Rights Reserved.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
```

## Libraries linked at build time and shipped in binary packages

### Qt 6

Glacier Ref links Qt dynamically (Core, Gui, Widgets, Svg, Concurrent) and uses
Qt runtime plugins, including Qt Image Formats. The open-source edition of Qt
is available under the GNU Lesser General Public License v3 (LGPLv3); see
https://www.qt.io/licensing/open-source-lgpl-obligations.

Binary packages (milestone M5) will list the exact Qt modules, plugins, and
their bundled third-party components (for example libwebp, libtiff, libjpeg,
libpng, zlib, FreeType, HarfBuzz) based on the staged release files, together
with their license texts and source links.

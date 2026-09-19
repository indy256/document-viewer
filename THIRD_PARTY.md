# Third-party notices

Document Viewer's own source files are available under the MIT license in `LICENSE`.

DjVu decoding uses [DjVuLibre](https://djvu.sourceforge.net/), licensed under
GNU GPL version 2 or later. This dependency is linked into the viewer, so the
combined application's distribution is subject to GPL-2.0-or-later. The original
MIT notices remain applicable to the viewer source.

The CMake install tree includes `licenses/DjVuLibre-COPYING.txt`, the exact
decoder source in `sources/djvulibre`, and viewer source in `sources/document-viewer`.
DjVuLibre is pinned to upstream release 3.5.30.1, commit
`af7431e5b024cc24eb04f8609c271ae68542fd5e` from
`https://git.code.sf.net/p/djvu/djvulibre-git`. It is built without source patches
using `cmake/DjVuLibre.cmake`.

To rebuild from the installed sources, follow the viewer README with Qt and a
C++17 toolchain installed. Set
`-DFETCHCONTENT_SOURCE_DIR_DJVULIBRE=/absolute/path/to/sources/djvulibre`
when configuring to use the included decoder source. PDFium is downloaded and
verified as described in the README. The viewer's source repository is
<https://github.com/indy256/document-viewer>.

Qt and PDFium retain their own licenses. Qt deployment includes its runtime
components; PDFium's license and third-party notices are installed under
`licenses/`. DjVuLibre's source files contain its copyright and attribution notices.

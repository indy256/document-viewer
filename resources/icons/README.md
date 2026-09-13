# Document Viewer icon

`DocumentViewer-master.png` is the original artwork from the built-in image
generation tool. `DocumentViewer.png` is the 512-pixel runtime/AppImage icon;
`DocumentViewer.ico` contains Windows sizes from 16 to 256 pixels;
`DocumentViewer.icns` contains macOS sizes from 16 to 1024 pixels.

Regenerate the desktop formats on Windows:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build-icons.ps1
```

Generation prompt:

> Use case: logo-brand. Asset type: desktop application icon for a lightweight PDF and EPUB document reader named Document Viewer. Create ONE finished square app icon, front-facing, centered, no mockup or surrounding layout. An elegant white document with a clearly folded upper corner and two broad, understated reading lines, with a small blue page behind it suggesting multiple documents. Set on a deep slate-blue rounded-square tile, with restrained soft blue gradient, subtle depth and beautifully crisp edges. Calm, professional native desktop utility aesthetic. Bold simple silhouette, excellent legibility at 16, 32 and 64 pixels; generous but modest internal padding. The tile should fill about 90% of the square canvas. Transparent alpha background outside the rounded tile; do not draw a checkerboard. No letters, no words, no PDF text, no magnifying glass, no tiny decorative details, no heavy shadows outside the tile. Produce a high-resolution 1024 by 1024 PNG suitable as the master for Windows ICO, macOS ICNS and Linux PNG.

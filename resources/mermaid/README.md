# Mermaid, vendored

`mermaid.min.js` is Mermaid 11.4.1 (`dist/mermaid.min.js` from the npm package
`mermaid@11.4.1`, integrity `sha512-Mb01JT/x6CKDWaxigwfZYuYmDZ6xtrNwNlidKZwkSrDaY9n90tdrJTV5Umk+wP1fZscGptmKFXHsXMDEVZ+Q6A==`),
SHA-256 `a43bc1afd446f9c4cc66ac5dd45d02e8d65e26fc5344ec0ef787f88d6ddb6f9e`, under the
MIT licence in `LICENSE`. Coeus compiles it in as a Qt resource and hands it to
a headless Chromium-family browser to draw ```` ```mermaid ```` blocks in reading
mode (D-022). It never runs inside Coeus itself.

To update: `npm pack mermaid@<version>`, take `package/dist/mermaid.min.js` and
`package/LICENSE`, record the version, integrity and SHA-256 here, and raise
`MermaidRenderer::kVersion` so cached diagrams are drawn again.

// Minimal static file server with the two response headers browsers
// require before they'll allow SharedArrayBuffer — which the pthreads
// Wasm build (engine_patch/) needs for its Lazy-SMP search threads.
//
// Usage:  node serve.js [port]      (default port 8080)
// Then open http://localhost:8080

const http = require('http');
const fs = require('fs');
const path = require('path');

const PORT = process.argv[2] || 8080;
const ROOT = __dirname;

const MIME = {
  '.html': 'text/html', '.js': 'text/javascript', '.css': 'text/css',
  '.wasm': 'application/wasm', '.json': 'application/json',
};

http.createServer((req, res) => {
  let filePath = path.join(ROOT, decodeURIComponent(req.url.split('?')[0]));
  if (filePath.endsWith('/')) filePath = path.join(filePath, 'index.html');

  fs.readFile(filePath, (err, data) => {
    if (err) {
      res.writeHead(404, { 'Content-Type': 'text/plain' });
      res.end('Not found: ' + req.url);
      return;
    }
    res.writeHead(200, {
      'Content-Type': MIME[path.extname(filePath)] || 'application/octet-stream',
      'Cross-Origin-Opener-Policy': 'same-origin',
      'Cross-Origin-Embedder-Policy': 'require-corp',
    });
    res.end(data);
  });
}).listen(PORT, () => console.log(`Serving ${ROOT} at http://localhost:${PORT}`));

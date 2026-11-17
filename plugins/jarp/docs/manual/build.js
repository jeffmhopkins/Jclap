// Renders manual.html to a PDF with Chromium. build.sh runs it twice: once with markers to find
// where each heading lands, then for real with those pages in the contents.
// node build.js <marker|final> out.pdf [toc.json]
const { chromium } = require('playwright');
const fs = require('fs');
(async () => {
  const [mode, out, tocFile] = process.argv.slice(2);
  const b = await chromium.launch();
  const p = await b.newPage();
  const toc = tocFile ? fs.readFileSync(tocFile, 'utf8') : '{}';
  await p.addInitScript(`window.TOC_PAGES = ${toc};`);
  await p.goto('file://' + __dirname + '/manual.html', { waitUntil: 'networkidle' });
  await p.evaluate(() => document.fonts.ready);
  if (mode === 'marker') {
    await p.evaluate(() => {
      const add = (node, key) => {
        node.style.position = 'relative';
        const s = document.createElement('span');
        s.textContent = '@@' + key + '@@';
        s.style.cssText = 'position:absolute;left:0;top:0;font-size:2px;color:#fff;';
        node.appendChild(s);
      };
      document.querySelectorAll('section.chapter').forEach(sec => {
        const t = sec.dataset.title;
        add(sec.querySelector('.chap-head'), t);
        sec.querySelectorAll('h2').forEach(h => add(h, t + '/' + h.childNodes[h.childNodes.length - 1].textContent.trim()));
      });
    });
  }
  await p.pdf({ path: out, preferCSSPageSize: true, printBackground: true });
  await b.close();
})();

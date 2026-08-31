const puppeteer = require('puppeteer');
(async () => {
    const browser = await puppeteer.launch({ args: ['--no-sandbox', '--disable-setuid-sandbox', '--use-gl=swiftshader'] });
    const page = await browser.newPage();
    page.on('console', msg => console.log('BROWSER CONSOLE:', msg.text()));
    await page.goto('http://localhost:8086/index.html');
    await new Promise(r => setTimeout(r, 2000));
    await page.screenshot({path: 'screenshot.png'});
    await browser.close();
})();

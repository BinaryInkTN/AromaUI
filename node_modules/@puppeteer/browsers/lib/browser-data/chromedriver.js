/**
 * @license
 * Copyright 2023 Google Inc.
 * SPDX-License-Identifier: Apache-2.0
 */
import path from 'node:path';
import { compareVersions } from './chrome.js';
import { BrowserPlatform } from './types.js';
function folder(platform, buildId) {
    switch (platform) {
        case BrowserPlatform.LINUX_ARM:
            if (buildId && compareVersions(buildId, '153.0.8001.0') < 0) {
                return 'linux64';
            }
            return 'linux-arm64';
        case BrowserPlatform.LINUX:
            return 'linux64';
        case BrowserPlatform.MAC_ARM:
            return 'mac-arm64';
        case BrowserPlatform.MAC:
            return 'mac-x64';
        case BrowserPlatform.WIN32:
            return 'win32';
        case BrowserPlatform.WIN64:
            return 'win64';
    }
}
export function resolveDownloadUrl(platform, buildId, baseUrl = 'https://storage.googleapis.com/chrome-for-testing-public') {
    return `${baseUrl}/${resolveDownloadPath(platform, buildId).join('/')}`;
}
export function resolveDownloadPath(platform, buildId) {
    return [
        buildId,
        folder(platform, buildId),
        `chromedriver-${folder(platform, buildId)}.zip`,
    ];
}
export function relativeExecutablePath(platform, buildId) {
    switch (platform) {
        case BrowserPlatform.MAC:
        case BrowserPlatform.MAC_ARM:
            return path.join('chromedriver-' + folder(platform, buildId), 'chromedriver');
        case BrowserPlatform.LINUX_ARM:
        case BrowserPlatform.LINUX:
            return path.join('chromedriver-' + folder(platform, buildId), 'chromedriver');
        case BrowserPlatform.WIN32:
        case BrowserPlatform.WIN64:
            return path.join('chromedriver-' + folder(platform, buildId), 'chromedriver.exe');
    }
}
export { resolveBuildId, compareVersions } from './chrome.js';
//# sourceMappingURL=chromedriver.js.map
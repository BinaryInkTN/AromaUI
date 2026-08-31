/**
 * @license
 * Copyright 2023 Google Inc.
 * SPDX-License-Identifier: Apache-2.0
 */
import type { ChromeHeadlessShellSettings, ChromeSettings, Configuration, FirefoxSettings } from 'puppeteer-core';
/**
 * @internal
 */
export declare function getBrowserSetting(browser: 'chrome' | 'chrome-headless-shell' | 'firefox', configuration: Configuration, defaultConfig?: ChromeSettings | ChromeHeadlessShellSettings | FirefoxSettings): ChromeSettings | ChromeHeadlessShellSettings | FirefoxSettings;
/**
 * @internal
 */
export declare const getConfiguration: () => Promise<Configuration>;
//# sourceMappingURL=getConfiguration.d.ts.map
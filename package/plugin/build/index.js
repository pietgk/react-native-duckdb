"use strict";
Object.defineProperty(exports, "__esModule", { value: true });
const config_plugins_1 = require("@expo/config-plugins");
const withAndroid_1 = require("./withAndroid");
const withIos_1 = require("./withIos");
const withDuckDB = (config, props = {}) => {
    config = (0, withAndroid_1.withAndroidExtensions)(config, props);
    config = (0, withIos_1.withIosExtensions)(config, props);
    return config;
};
// package.json sits outside rootDir, so it cannot be imported here.
// eslint-disable-next-line @typescript-eslint/no-require-imports
const pkg = require('../../package.json');
exports.default = (0, config_plugins_1.createRunOncePlugin)(withDuckDB, pkg.name, pkg.version);

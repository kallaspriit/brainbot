// Converts the UI fonts and icons to LVGL C fonts.
//
//     node tools/convert-fonts.mjs
//
// Text fonts come from firmware/fonts/source/ (committed, with licences). The Material Symbols icon font is 15 MB, so
// it is downloaded on demand into tools/.cache/ (git-ignored), pinned to one commit of google/material-design-icons.
// Output goes to firmware/main/ui/fonts/: one .c file per font and size, Fonts.hpp declaring them, and Icons.hpp with
// the icons as UTF-8 strings. Generated files are committed; rerun this after changing anything below.
//
// Needs Node.js; lv_font_conv is run through npx at a pinned version.

import { execFileSync } from "node:child_process";
import { createHash } from "node:crypto";
import { existsSync, mkdirSync, readFileSync, readdirSync, rmSync, writeFileSync } from "node:fs";
import { dirname, join, relative } from "node:path";
import { fileURLToPath } from "node:url";

const LV_FONT_CONV = "lv_font_conv@1.5.3";

// npx is a .cmd script on Windows, which can't be run without a shell; run npm's npx-cli.js with node instead
const npxCommand =
    process.platform === "win32"
        ? [process.execPath, join(dirname(process.execPath), "node_modules", "npm", "bin", "npx-cli.js")]
        : ["npx"];

const repositoryDirectory = join(dirname(fileURLToPath(import.meta.url)), "..");
const sourceDirectory = join(repositoryDirectory, "firmware", "fonts", "source");
const outputDirectory = join(repositoryDirectory, "firmware", "main", "ui", "fonts");
const cacheDirectory = join(repositoryDirectory, "tools", ".cache");

// Material Symbols Rounded, variable font; lv_font_conv renders its default instance (weight 400, outlined)
const MATERIAL_SYMBOLS_COMMIT = "737e3324305806514d7909874fa1818ae1808232";
const MATERIAL_SYMBOLS_SHA256 = "95b24392bb49efd1bc3e92cff4e2452ad094461bab7c97e7d8723fab97e330ca";
const MATERIAL_SYMBOLS_URL = `https://github.com/google/material-design-icons/raw/${MATERIAL_SYMBOLS_COMMIT}/variablefont/MaterialSymbolsRounded%5BFILL%2CGRAD%2Copsz%2Cwght%5D.ttf`;

// Text: printable ASCII plus the symbols the UI uses (° ± ² µ · × – — • … → ≈ Ω)
const TEXT_RANGES = "0x20-0x7E,0xB0,0xB1,0xB2,0xB5,0xB7,0xD7,0x2013,0x2014,0x2022,0x2026,0x2192,0x2248,0x3A9";

const TEXT_FONTS = [
    { name: "inter_regular", file: "Inter-Regular.ttf", sizes: [16, 20, 24] },
    { name: "inter_semibold", file: "Inter-SemiBold.ttf", sizes: [20, 24, 32] },
    { name: "mono_medium", file: "JetBrainsMono-Medium.ttf", sizes: [16, 24, 32, 48] },
];

const ICON_SIZES = [24, 32, 48];

// Icons used by the UI: constant name in Icons.hpp → Material Symbols name and code point
const ICONS = {
    HOME: ["home", 0xe9b2],
    BACK: ["arrow_back", 0xe5c4],
    OVERVIEW: ["dashboard", 0xe871],
    VELOCITY: ["speed", 0xe9e4],
    POSITION: ["my_location", 0xe55c],
    MOVE: ["open_with", 0xe89f],
    WARNING: ["warning", 0xf083],
    FAULT: ["error", 0xf8b6],
    PARAMETERS: ["tune", 0xe429],
    TOOLS: ["build", 0xf8cd],
    SETTINGS: ["settings", 0xe8b8],
    TORQUE: ["rotate_right", 0xe41a],
    START: ["play_arrow", 0xe037],
    STOP: ["stop", 0xe047],
    STOP_CIRCLE: ["stop_circle", 0xef71],
    REFRESH: ["refresh", 0xe5d5],
    CHECK: ["check", 0xe668],
    CLOSE: ["close", 0xe5cd],
    ADD: ["add", 0xe145],
    REMOVE: ["remove", 0xe15b],
    CAN_BUS: ["lan", 0xeb2f],
    VOLTAGE: ["bolt", 0xea0b],
    TEMPERATURE: ["thermostat", 0xf076],
    DISCONNECTED: ["link_off", 0xe16f],
    CABLE: ["cable", 0xefe6],
    SEARCH: ["search", 0xef7a],
    INFO: ["info", 0xe88e],
    RESET: ["restart_alt", 0xf053],
    POWER: ["power_settings_new", 0xf8c7],
    SYNC: ["sync", 0xe627],
    CURRENT: ["electric_bolt", 0xec1c],
    CANCEL: ["cancel", 0xe888],
    CHEVRON_RIGHT: ["chevron_right", 0xe5cc],
    EXPAND: ["expand_more", 0xe5cf],
    CHIP: ["memory", 0xe322],
    MONITORING: ["monitoring", 0xf190],
    LOCK: ["lock", 0xe897],
    LATENCY: ["timer", 0xe425],
    LIGHT: ["lightbulb", 0xe0f0],
};

/** Downloads the icon font once, into the cache. */
async function getMaterialSymbolsPath() {
    const path = join(cacheDirectory, `MaterialSymbolsRounded-${MATERIAL_SYMBOLS_COMMIT.slice(0, 8)}.ttf`);

    if (existsSync(path)) {
        return path;
    }

    console.log("Downloading Material Symbols Rounded…");
    const response = await fetch(MATERIAL_SYMBOLS_URL);

    if (!response.ok) {
        throw new Error(`Download failed: ${response.status} ${response.statusText}`);
    }

    const data = Buffer.from(await response.arrayBuffer());
    const sha256 = createHash("sha256").update(data).digest("hex");

    if (sha256 !== MATERIAL_SYMBOLS_SHA256) {
        throw new Error(`Material Symbols download has sha256 ${sha256}, expected ${MATERIAL_SYMBOLS_SHA256}`);
    }

    mkdirSync(cacheDirectory, { recursive: true });
    writeFileSync(path, data);

    return path;
}

/** Makes a path relative to the repository with forward slashes, so generated files don't depend on the machine. */
function toRepositoryPath(path) {
    return relative(repositoryDirectory, path).replaceAll("\\", "/");
}

/** Runs lv_font_conv for one font and size. */
function convertFont({ fontPath, ranges, size, fontName }) {
    const outputPath = toRepositoryPath(join(outputDirectory, `${fontName}.c`));
    execFileSync(
        npxCommand[0],
        [
            ...npxCommand.slice(1),
            "-y",
            LV_FONT_CONV,
            "--font", toRepositoryPath(fontPath),
            "-r", ranges,
            "--size", String(size),
            "--bpp", "4",
            "--format", "lvgl",
            "--no-compress",
            "--lv-include", "lvgl.h",
            "--lv-font-name", fontName,
            "-o", outputPath,
        ],
        { stdio: "inherit", cwd: repositoryDirectory },
    );

    console.log(`  ${fontName}`);
}

/** Encodes a code point as a C string literal of its UTF-8 bytes. */
function toUtf8Literal(codePoint) {
    const bytes = Buffer.from(String.fromCodePoint(codePoint), "utf8");

    return `"${[...bytes].map((byte) => `\\x${byte.toString(16).toUpperCase().padStart(2, "0")}`).join("")}"`;
}

/** Writes Fonts.hpp declaring every generated font. */
function writeFontsHeader(fontNames) {
    const lines = [
        "// Generated by tools/convert-fonts.mjs. Do not edit.",
        "",
        "#pragma once",
        "",
        '#include "lvgl.h"',
        "",
        ...fontNames.map((fontName) => `LV_FONT_DECLARE(${fontName})`),
        "",
    ];

    writeFileSync(join(outputDirectory, "Fonts.hpp"), lines.join("\n"));
}

/** Writes Icons.hpp with the icons as UTF-8 strings for the icon fonts. */
function writeIconsHeader() {
    const names = Object.keys(ICONS);
    const nameWidth = Math.max(...names.map((name) => name.length));
    const lines = [
        "// Generated by tools/convert-fonts.mjs. Do not edit.",
        "//",
        "// Material Symbols Rounded icons as UTF-8 strings. Show them in a label using one of the icons_* fonts.",
        "",
        "#pragma once",
        "",
        "namespace Icons {",
        ...names.map((name) => {
            const [symbolName, codePoint] = ICONS[name];

            return `    constexpr const char* ${name.padEnd(nameWidth)} = ${toUtf8Literal(codePoint)}; // ${symbolName}`;
        }),
        "} // namespace Icons",
        "",
    ];

    writeFileSync(join(outputDirectory, "Icons.hpp"), lines.join("\n"));
}

async function main() {
    // Start clean so removed fonts don't linger
    if (existsSync(outputDirectory)) {
        for (const file of readdirSync(outputDirectory)) {
            rmSync(join(outputDirectory, file));
        }
    }

    mkdirSync(outputDirectory, { recursive: true });
    const fontNames = [];

    console.log("Text fonts:");

    for (const font of TEXT_FONTS) {
        for (const size of font.sizes) {
            const fontName = `${font.name}_${size}`;
            convertFont({ fontPath: join(sourceDirectory, font.file), ranges: TEXT_RANGES, size, fontName });
            fontNames.push(fontName);
        }
    }

    console.log("Icon fonts:");
    const materialSymbolsPath = await getMaterialSymbolsPath();
    // The space is included so icons can be separated in one label
    const iconRanges = ["0x20", ...Object.values(ICONS).map(([, codePoint]) => `0x${codePoint.toString(16)}`)].join(",");

    for (const size of ICON_SIZES) {
        const fontName = `icons_${size}`;
        convertFont({ fontPath: materialSymbolsPath, ranges: iconRanges, size, fontName });
        fontNames.push(fontName);
    }

    writeFontsHeader(fontNames);
    writeIconsHeader();

    const totalBytes = readdirSync(outputDirectory)
        .filter((file) => file.endsWith(".c"))
        .reduce((sum, file) => sum + readFileSync(join(outputDirectory, file)).length, 0);
    console.log(`Done: ${fontNames.length} fonts, ${Math.round(totalBytes / 1024)} KB of C source in firmware/main/ui/fonts/`);
}

main().catch((error) => {
    console.error(error);
    process.exit(1);
});

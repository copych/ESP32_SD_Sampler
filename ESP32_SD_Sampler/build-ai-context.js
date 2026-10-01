#!/usr/bin/env node
'use strict';

/**
 * AI project-context builder
 *
 * Usage:
 *   node build-ai-context.js
 *   node build-ai-context.js --output .ai-context
 *   node build-ai-context.js --chunk-kb 1500
 *   node build-ai-context.js --max-file-kb 500
 *   node build-ai-context.js --include-all-text
 *
 * Output:
 *   .ai-context/
 *     AI_CONTEXT.md
 *     AI_CONTEXT_001.md
 *     AI_CONTEXT_002.md
 *     ...
 *     AI_CONTEXT_INDEX.md
 *
 * The combined AI_CONTEXT.md is convenient when it is reasonably sized.
 * Chunk files are useful when ChatGPT upload limits or context size matter.
 */

const fs = require('fs');
const path = require('path');
const crypto = require('crypto');

let PROJECT_ROOT = process.cwd();

const DEFAULT_CONFIG = {
    rootDir: null,
    outputDir: '.ai-context',

    // Maximum approximate size of each chunk.
    chunkSizeBytes: 1500 * 1024,

    // Individual source files larger than this are skipped.
    maxFileSizeBytes: 750 * 1024,

    // Generate one combined file in addition to chunks.
    writeCombinedFile: true,

    // Include files without a known extension when they appear textual.
    includeAllTextFiles: false,

    // Include empty files in the manifest, but not in context bodies.
    includeEmptyFiles: false,

    // Very long lines often indicate generated/minified files.
    maxReasonableLineLength: 20_000,

    // Extensions generally useful for software development context.
    includedExtensions: new Set([
        // C / C++
        '.c',
        '.cc',
        '.cpp',
        '.cxx',
        '.h',
        '.hh',
        '.hpp',
        '.hxx',
        '.ino',
        '.ipp',
        '.tpp',

        // Web / backend
        '.php',
        '.phtml',
        '.js',
        '.mjs',
        '.cjs',
        '.jsx',
        '.ts',
        '.tsx',
        '.css',
        '.scss',
        '.sass',
        '.less',
        '.html',
        '.htm',
        '.vue',
        '.svelte',

        // Python / scripting
        '.py',
        '.pyw',
        '.ps1',
        '.psm1',
        '.bat',
        '.cmd',
        '.sh',
        '.bash',

        // Java / Kotlin / C#
        '.java',
        '.kt',
        '.kts',
        '.cs',
        '.fs',
        '.fsx',

        // Rust / Go
        '.rs',
        '.go',

        // SQL / data
        '.sql',
        '.graphql',
        '.gql',
        '.xml',
        '.xsd',
        '.xsl',
        '.xslt',
        '.json',
        '.jsonc',
        '.yaml',
        '.yml',
        '.toml',
        '.ini',
        '.cfg',
        '.conf',
        '.properties',
        '.env.example',

        // Documentation
        '.md',
        '.markdown',
        '.txt',
        '.rst',
        '.adoc',

        // Build / configuration
        '.cmake',
        '.gradle',
        '.mak',
        '.mk',
        '.dockerfile',

        // Templates
        '.twig',
        '.blade.php',
        '.mustache',
        '.hbs',
        '.ejs'
    ]),

    includedExactNames: new Set([
        'Dockerfile',
        'Makefile',
        'CMakeLists.txt',
        'Kconfig',
        'Kconfig.projbuild',
        'platformio.ini',
        'sdkconfig.defaults',
        'composer.json',
        'package.json',
        'package-lock.json',
        'npm-shrinkwrap.json',
        'tsconfig.json',
        'jsconfig.json',
        'vite.config.js',
        'webpack.config.js',
        'rollup.config.js',
        'eslint.config.js',
        '.gitignore',
        '.gitattributes',
        '.editorconfig',
        'README',
        'LICENSE'
    ]),

    excludedDirectories: new Set([
        '.work', // local builds, test compiler/cache and original-source backup
		'.work (1)', // weird
        '.git',
        '.svn',
        '.hg',
        '.idea',
        '.vscode',
		
		'tools',
		'tests',

        'node_modules',
        'vendor',
        'bower_components',

        'dist',
        'build',
        'out',
        'target',
        'bin',
        'obj',
        'coverage',
        '.next',
        '.nuxt',
        '.cache',
        '.parcel-cache',
        '.pytest_cache',
        '__pycache__',

        '.pio',
        '.pioenvs',
        '.pioenv',
        'Debug',
        'Release',

        '.ai-context'
    ]),

    excludedExactFiles: new Set([
        '.env',
        '.env.local',
        '.env.development',
        '.env.production',
        '.env.test',

        'id_rsa',
        'id_ed25519',

        'AI_CONTEXT.md',
        'AI_CONTEXT_INDEX.md'
    ]),

    excludedSuffixes: [
        '.min.js',
        '.min.css',
        '.map',
        '.lock',
        '.log',
        '.tmp',
        '.bak',
        '.swp',
        '.swo',
        '.orig',
        '.rej'
    ],

    excludedExtensions: new Set([
        // Images
        '.png',
        '.jpg',
        '.jpeg',
        '.gif',
        '.bmp',
        '.webp',
        '.ico',
        '.svgz',

        // Audio/video
        '.mp3',
        '.wav',
        '.flac',
        '.ogg',
        '.aac',
        '.m4a',
        '.mp4',
        '.mkv',
        '.avi',
        '.mov',
        '.webm',

        // Fonts
        '.ttf',
        '.otf',
        '.woff',
        '.woff2',
        '.eot',

        // Archives
        '.zip',
        '.7z',
        '.rar',
        '.tar',
        '.gz',
        '.bz2',
        '.xz',

        // Executables / compiled files
        '.exe',
        '.dll',
        '.so',
        '.dylib',
        '.obj',
        '.o',
        '.a',
        '.lib',
        '.class',
        '.jar',
        '.pyc',
        '.pdb',
        '.elf',
        '.bin',
        '.hex',

        // Databases
        '.db',
        '.sqlite',
        '.sqlite3',
        '.mdb',

        // Documents not suitable as plain source context
        '.pdf',
        '.doc',
        '.docx',
        '.xls',
        '.xlsx',
        '.ppt',
        '.pptx'
    ])
};

function parseArguments() {
    const config = {
        ...DEFAULT_CONFIG,
        includedExtensions: new Set(DEFAULT_CONFIG.includedExtensions),
        includedExactNames: new Set(DEFAULT_CONFIG.includedExactNames),
        excludedDirectories: new Set(DEFAULT_CONFIG.excludedDirectories),
        excludedExactFiles: new Set(DEFAULT_CONFIG.excludedExactFiles),
        excludedSuffixes: [...DEFAULT_CONFIG.excludedSuffixes],
        excludedExtensions: new Set(DEFAULT_CONFIG.excludedExtensions)
    };

    const args = process.argv.slice(2);

    for (let i = 0; i < args.length; i++) {
        const arg = args[i];

        switch (arg) {
            case '--output':
                config.outputDir = requireArgument(args, ++i, arg);
                break;

            case '--chunk-kb':
                config.chunkSizeBytes =
                    parsePositiveInteger(requireArgument(args, ++i, arg), arg) * 1024;
                break;

            case '--max-file-kb':
                config.maxFileSizeBytes =
                    parsePositiveInteger(requireArgument(args, ++i, arg), arg) * 1024;
                break;

            case '--include-all-text':
                config.includeAllTextFiles = true;
                break;

            case '--include-empty':
                config.includeEmptyFiles = true;
                break;

            case '--no-combined':
                config.writeCombinedFile = false;
                break;

			case '--root':
				config.rootDir = requireArgument(args, ++i, arg);
				break;

            case '--help':
            case '-h':
                printHelp();
                process.exit(0);
                break;

            default:
                throw new Error(`Unknown argument: ${arg}`);
        }
    }

    // Always exclude the selected output directory.
	if (config.rootDir) {
		PROJECT_ROOT = path.resolve(config.rootDir);
	} else {
		PROJECT_ROOT = path.resolve(PROJECT_ROOT);
	}

	const outputBaseName = path.basename(
		path.resolve(PROJECT_ROOT, config.outputDir)
	);

	config.excludedDirectories.add(outputBaseName);

	return config;
}

function requireArgument(args, index, optionName) {
    if (index >= args.length) {
        throw new Error(`Missing value for ${optionName}`);
    }

    return args[index];
}

function parsePositiveInteger(value, optionName) {
    const parsed = Number.parseInt(value, 10);

    if (!Number.isFinite(parsed) || parsed <= 0) {
        throw new Error(`${optionName} must be a positive integer`);
    }

    return parsed;
}

function printHelp() {
    console.log(`
AI context builder

Usage:
  node build-ai-context.js [options]

Options:
  --root <directory>     Project root, including a UNC path
  --output <directory>   Output directory. Default: .ai-context
  --chunk-kb <number>   Maximum approximate chunk size in KB. Default: 1500
  --max-file-kb <number>
                         Maximum individual source file size. Default: 750
  --include-all-text     Include unknown extensions when content is textual
  --include-empty        Include empty files in generated context
  --no-combined          Do not generate the combined AI_CONTEXT.md
  --help, -h             Show this help
`);
}

function normalizeRelativePath(absolutePath) {
    return path
        .relative(PROJECT_ROOT, absolutePath)
        .split(path.sep)
        .join('/');
}

function shouldExcludeDirectory(name, config) {
    if (config.excludedDirectories.has(name)) {
        return true;
    }

    // Common temporary and IDE-generated directories.
    return (
        name.startsWith('.tmp') ||
        name.startsWith('.cache') ||
        name.endsWith('.egg-info')
    );
}

function shouldExcludeFile(fileName, config) {
    if (config.excludedExactFiles.has(fileName)) {
        return true;
    }

    const lowerName = fileName.toLowerCase();
    const extension = path.extname(lowerName);

    if (config.excludedExtensions.has(extension)) {
        return true;
    }

    return config.excludedSuffixes.some(
        suffix => lowerName.endsWith(suffix.toLowerCase())
    );
}

function isExplicitlyIncluded(fileName, config) {
    if (config.includedExactNames.has(fileName)) {
        return true;
    }

    const lowerName = fileName.toLowerCase();

    // Handles compound extensions such as .blade.php and .env.example.
    for (const extension of config.includedExtensions) {
        if (lowerName.endsWith(extension.toLowerCase())) {
            return true;
        }
    }

    return false;
}

function isProbablyBinary(buffer) {
    if (buffer.length === 0) {
        return false;
    }

    const sampleLength = Math.min(buffer.length, 8192);
    let suspiciousBytes = 0;

    for (let i = 0; i < sampleLength; i++) {
        const byte = buffer[i];

        if (byte === 0) {
            return true;
        }

        const isControlCharacter =
            byte < 7 ||
            (byte > 13 && byte < 32) ||
            byte === 127;

        if (isControlCharacter) {
            suspiciousBytes++;
        }
    }

    return suspiciousBytes / sampleLength > 0.05;
}

function normalizeText(buffer) {
    let text = buffer.toString('utf8');

    // Remove UTF-8 BOM.
    if (text.charCodeAt(0) === 0xFEFF) {
        text = text.slice(1);
    }

    // Keep generated output stable across Windows/Linux.
    return text
        .replace(/\r\n/g, '\n')
        .replace(/\r/g, '\n');
}

function detectGeneratedOrMinified(text, config) {
    const lines = text.split('\n');

    if (lines.length === 0) {
        return null;
    }

    let longestLine = 0;

    for (const line of lines) {
        if (line.length > longestLine) {
            longestLine = line.length;
        }
    }

    if (
        longestLine > config.maxReasonableLineLength &&
        lines.length < 50
    ) {
        return `probably minified/generated; longest line is ${longestLine} characters`;
    }

    const firstPart = text.slice(0, 2000).toLowerCase();

    const generatedMarkers = [
        'this file is auto-generated',
        'this file is autogenerated',
        'automatically generated file',
        'generated by the protocol buffer compiler',
        'code generated by',
        'do not edit this file',
        '@generated'
    ];

    const marker = generatedMarkers.find(item => firstPart.includes(item));

    if (marker) {
        return `generated-file marker detected: "${marker}"`;
    }

    return null;
}

function scanDirectory(directoryPath, config, result) {
    let entries;

    try {
        entries = fs.readdirSync(directoryPath, {
            withFileTypes: true
        });
    } catch (error) {
        result.skipped.push({
            path: normalizeRelativePath(directoryPath),
            reason: `cannot read directory: ${error.message}`
        });
        return;
    }

    entries.sort((a, b) =>
        a.name.localeCompare(b.name, 'en', {
            sensitivity: 'base',
            numeric: true
        })
    );

    for (const entry of entries) {
        const absolutePath = path.join(directoryPath, entry.name);

        if (entry.isSymbolicLink()) {
            result.skipped.push({
                path: normalizeRelativePath(absolutePath),
                reason: 'symbolic link'
            });
            continue;
        }

        if (entry.isDirectory()) {
            if (shouldExcludeDirectory(entry.name, config)) {
                result.excludedDirectoryCount++;
                continue;
            }

            scanDirectory(absolutePath, config, result);
            continue;
        }

        if (!entry.isFile()) {
            continue;
        }

        processFile(absolutePath, entry.name, config, result);
    }
}

function processFile(absolutePath, fileName, config, result) {
    const relativePath = normalizeRelativePath(absolutePath);

    if (shouldExcludeFile(fileName, config)) {
        return;
    }

    let stats;

    try {
        stats = fs.statSync(absolutePath);
    } catch (error) {
        result.skipped.push({
            path: relativePath,
            reason: `cannot stat file: ${error.message}`
        });
        return;
    }

    if (stats.size > config.maxFileSizeBytes) {
        result.skipped.push({
            path: relativePath,
            reason: `file exceeds limit: ${formatBytes(stats.size)}`
        });
        return;
    }

    const explicitlyIncluded = isExplicitlyIncluded(fileName, config);

    if (!explicitlyIncluded && !config.includeAllTextFiles) {
        return;
    }

    let buffer;

    try {
        buffer = fs.readFileSync(absolutePath);
    } catch (error) {
        result.skipped.push({
            path: relativePath,
            reason: `cannot read file: ${error.message}`
        });
        return;
    }

    if (isProbablyBinary(buffer)) {
        result.skipped.push({
            path: relativePath,
            reason: 'binary content detected'
        });
        return;
    }

    const text = normalizeText(buffer);

    if (!explicitlyIncluded && config.includeAllTextFiles && !text.trim()) {
        return;
    }

    const generatedReason = detectGeneratedOrMinified(text, config);

    if (generatedReason) {
        result.skipped.push({
            path: relativePath,
            reason: generatedReason
        });
        return;
    }

    if (!text.length && !config.includeEmptyFiles) {
        result.empty.push(relativePath);
        return;
    }

    const hash = crypto
        .createHash('sha256')
        .update(buffer)
        .digest('hex')
        .slice(0, 16);

    result.files.push({
        absolutePath,
        relativePath,
        size: stats.size,
        lines: countLines(text),
        hash,
        text
    });
}

function countLines(text) {
    if (!text.length) {
        return 0;
    }

    return text.split('\n').length;
}

function languageForFile(filePath) {
    const fileName = path.basename(filePath);
    const lowerName = fileName.toLowerCase();
    const extension = path.extname(lowerName);

    const mappings = {
        '.c': 'c',
        '.cc': 'cpp',
        '.cpp': 'cpp',
        '.cxx': 'cpp',
        '.h': 'cpp',
        '.hh': 'cpp',
        '.hpp': 'cpp',
        '.hxx': 'cpp',
        '.ino': 'cpp',
        '.ipp': 'cpp',
        '.tpp': 'cpp',
        '.php': 'php',
        '.phtml': 'php',
        '.js': 'javascript',
        '.mjs': 'javascript',
        '.cjs': 'javascript',
        '.jsx': 'jsx',
        '.ts': 'typescript',
        '.tsx': 'tsx',
        '.py': 'python',
        '.pyw': 'python',
        '.ps1': 'powershell',
        '.psm1': 'powershell',
        '.bat': 'batch',
        '.cmd': 'batch',
        '.sh': 'bash',
        '.bash': 'bash',
        '.sql': 'sql',
        '.html': 'html',
        '.htm': 'html',
        '.css': 'css',
        '.scss': 'scss',
        '.sass': 'sass',
        '.less': 'less',
        '.xml': 'xml',
        '.xsd': 'xml',
        '.xsl': 'xml',
        '.xslt': 'xml',
        '.json': 'json',
        '.jsonc': 'jsonc',
        '.yaml': 'yaml',
        '.yml': 'yaml',
        '.toml': 'toml',
        '.ini': 'ini',
        '.cfg': 'ini',
        '.conf': 'text',
        '.properties': 'properties',
        '.md': 'markdown',
        '.markdown': 'markdown',
        '.rst': 'rst',
        '.adoc': 'asciidoc',
        '.java': 'java',
        '.kt': 'kotlin',
        '.kts': 'kotlin',
        '.cs': 'csharp',
        '.fs': 'fsharp',
        '.fsx': 'fsharp',
        '.rs': 'rust',
        '.go': 'go',
        '.graphql': 'graphql',
        '.gql': 'graphql',
        '.vue': 'vue',
        '.svelte': 'svelte',
        '.cmake': 'cmake',
        '.gradle': 'gradle',
        '.mk': 'makefile',
        '.mak': 'makefile',
        '.twig': 'twig',
        '.mustache': 'mustache',
        '.hbs': 'handlebars',
        '.ejs': 'ejs'
    };

    if (mappings[extension]) {
        return mappings[extension];
    }

    if (fileName === 'Dockerfile') {
        return 'dockerfile';
    }

    if (fileName === 'Makefile') {
        return 'makefile';
    }

    if (fileName === 'CMakeLists.txt') {
        return 'cmake';
    }

    return 'text';
}

function chooseFence(text) {
    let fenceLength = 3;

    const matches = text.match(/`+/g);

    if (matches) {
        for (const match of matches) {
            fenceLength = Math.max(fenceLength, match.length + 1);
        }
    }

    return '`'.repeat(fenceLength);
}

function renderFileSection(file) {
    const fence = chooseFence(file.text);
    const language = languageForFile(file.relativePath);

    return [
        `## File: \`${file.relativePath}\``,
        '',
        `- Size: ${formatBytes(file.size)}`,
        `- Lines: ${file.lines}`,
        `- SHA-256: \`${file.hash}\``,
        '',
        `${fence}${language}`,
        file.text,
        fence,
        ''
    ].join('\n');
}

function renderProjectHeader(files, generatedAt) {
    const totalBytes = files.reduce((sum, file) => sum + file.size, 0);
    const totalLines = files.reduce((sum, file) => sum + file.lines, 0);

    return [
        '# AI Project Context',
        '',
        `- Project: \`${path.basename(PROJECT_ROOT)}\``,
        `- Root: \`${PROJECT_ROOT}\``,
        `- Generated: ${generatedAt}`,
        `- Included files: ${files.length}`,
        `- Source size: ${formatBytes(totalBytes)}`,
        `- Source lines: ${totalLines.toLocaleString('en-US')}`,
        `- Approximate tokens: ${estimateTokens(totalBytes).toLocaleString('en-US')}`,
        '',
        '> Treat file paths as authoritative. Later occurrences of a file override earlier context.',
        ''
    ].join('\n');
}

function renderTree(files) {
    const root = {};

    for (const file of files) {
        const parts = file.relativePath.split('/');
        let node = root;

        for (let i = 0; i < parts.length; i++) {
            const part = parts[i];

            if (i === parts.length - 1) {
                node[part] = null;
            } else {
                node[part] ??= {};
                node = node[part];
            }
        }
    }

    const lines = [];

    function walk(node, prefix = '') {
        const names = Object.keys(node).sort((a, b) =>
            a.localeCompare(b, 'en', {
                sensitivity: 'base',
                numeric: true
            })
        );

        names.forEach((name, index) => {
            const isLast = index === names.length - 1;
            const child = node[name];
            const connector = isLast ? '└── ' : '├── ';

            lines.push(`${prefix}${connector}${name}`);

            if (child !== null) {
                walk(child, `${prefix}${isLast ? '    ' : '│   '}`);
            }
        });
    }

    lines.push(path.basename(PROJECT_ROOT));
    walk(root);

    return [
        '# Project File Tree',
        '',
        '```text',
        ...lines,
        '```',
        ''
    ].join('\n');
}

function renderManifest(files) {
    const lines = [
        '# File Manifest',
        '',
        '| File | Size | Lines | SHA-256 |',
        '|---|---:|---:|---|'
    ];

    for (const file of files) {
        const escapedPath = file.relativePath.replace(/\|/g, '\\|');

        lines.push(
            `| \`${escapedPath}\` | ${formatBytes(file.size)} | ` +
            `${file.lines} | \`${file.hash}\` |`
        );
    }

    lines.push('');

    return lines.join('\n');
}

function renderSkippedFiles(result) {
    if (result.skipped.length === 0 && result.empty.length === 0) {
        return '';
    }

    const lines = [
        '# Files Not Included',
        ''
    ];

    if (result.skipped.length > 0) {
        lines.push('## Skipped', '');

        for (const item of result.skipped) {
            lines.push(`- \`${item.path}\`: ${item.reason}`);
        }

        lines.push('');
    }

    if (result.empty.length > 0) {
        lines.push('## Empty files', '');

        for (const filePath of result.empty) {
            lines.push(`- \`${filePath}\``);
        }

        lines.push('');
    }

    return lines.join('\n');
}

function splitIntoChunks(fileSections, header, config) {
    const chunks = [];
    let current = `${header}\n`;
    let currentFiles = [];

    for (const section of fileSections) {
        const sectionText =
            typeof section === 'string'
                ? section
                : section.text;

        const relativePath =
            typeof section === 'string'
                ? '(unknown)'
                : section.relativePath;

        const sectionSize = Buffer.byteLength(sectionText, 'utf8');
        const currentSize = Buffer.byteLength(current, 'utf8');

        if (
            currentFiles.length > 0 &&
            currentSize + sectionSize > config.chunkSizeBytes
        ) {
            chunks.push({
                body: current,
                files: currentFiles
            });

            current = `${header}\n`;
            currentFiles = [];
        }

        current += `${sectionText}\n`;
        currentFiles.push(relativePath);
    }

    if (currentFiles.length > 0) {
        chunks.push({
            body: current,
            files: currentFiles
        });
    }

    return chunks;
}

function cleanOutputDirectory(outputPath) {
    fs.mkdirSync(outputPath, {
        recursive: true
    });

    const entries = fs.readdirSync(outputPath, {
        withFileTypes: true
    });

    for (const entry of entries) {
        if (!entry.isFile()) {
            continue;
        }

        if (
            /^AI_CONTEXT(?:_\d+|_INDEX)?\.md$/i.test(entry.name)
        ) {
            fs.unlinkSync(path.join(outputPath, entry.name));
        }
    }
}

function writeUtf8(filePath, content) {
    fs.writeFileSync(filePath, content, {
        encoding: 'utf8'
    });
}

function formatBytes(bytes) {
    if (bytes < 1024) {
        return `${bytes} B`;
    }

    if (bytes < 1024 * 1024) {
        return `${(bytes / 1024).toFixed(1)} KB`;
    }

    return `${(bytes / (1024 * 1024)).toFixed(2)} MB`;
}

function estimateTokens(bytes) {
    // Crude but useful estimate for mixed source code and documentation.
    return Math.ceil(bytes / 3.5);
}

function main() {
    const config = parseArguments();
    const outputPath = path.resolve(PROJECT_ROOT, config.outputDir);

    const result = {
        files: [],
        skipped: [],
        empty: [],
        excludedDirectoryCount: 0
    };

    console.log(`Scanning: ${PROJECT_ROOT}`);

    scanDirectory(PROJECT_ROOT, config, result);

    result.files.sort((a, b) =>
        a.relativePath.localeCompare(b.relativePath, 'en', {
            sensitivity: 'base',
            numeric: true
        })
    );

    if (result.files.length === 0) {
        throw new Error('No source files matched the configured rules');
    }

    const generatedAt = new Date().toISOString();
    const projectHeader = renderProjectHeader(result.files, generatedAt);
    const tree = renderTree(result.files);
    const manifest = renderManifest(result.files);
    const skipped = renderSkippedFiles(result);

    const chunkHeader = [
        projectHeader,
        '> This is one chunk of a larger project context.',
        ''
    ].join('\n');

    const sections = result.files.map(file => ({
		relativePath: file.relativePath,
		text: renderFileSection(file)
	}));

	const chunks = splitIntoChunks(
		sections,
		chunkHeader,
		config
	);

cleanOutputDirectory(outputPath);

const chunkRecords = [];
let combinedSize = 0;

if (chunks.length === 1) {
    /*
     * A single chunk would duplicate AI_CONTEXT.md, so only write the
     * combined context file.
     */
    const combinedBody = [
        projectHeader,
        tree,
        manifest,
        skipped,
        ...result.files.map(renderFileSection)
    ].join('\n');

    const combinedPath = path.join(outputPath, 'AI_CONTEXT.md');

    writeUtf8(combinedPath, combinedBody);

    combinedSize = Buffer.byteLength(combinedBody, 'utf8');
} else {
    /*
     * For larger projects, write only numbered chunks. Writing a combined
     * file as well would duplicate the complete source set.
     */
    chunks.forEach((chunk, index) => {
        const number = String(index + 1).padStart(3, '0');
        const fileName = `AI_CONTEXT_${number}.md`;
        const filePath = path.join(outputPath, fileName);

        const chunkPreamble = [
            `# Context Chunk ${index + 1} of ${chunks.length}`,
            '',
            `Files in this chunk: ${chunk.files.length}`,
            '',
            '---',
            ''
        ].join('\n');

        const body = `${chunkPreamble}${chunk.body}`;

        writeUtf8(filePath, body);

        chunkRecords.push({
            fileName,
            fileCount: chunk.files.length,
            size: Buffer.byteLength(body, 'utf8'),
            firstFile: chunk.files[0],
            lastFile: chunk.files[chunk.files.length - 1]
        });
    });
}

const indexLines = [
    '# AI Context Index',
    '',
    projectHeader,
    tree,
    manifest,
    '# Generated Context Files',
    ''
];

if (chunks.length === 1) {
    indexLines.push(
        '| File | Included files | Size |',
        '|---|---:|---:|',
        `| \`AI_CONTEXT.md\` | ${result.files.length} | ${formatBytes(combinedSize)} |`,
        '',
        '## Upload instructions',
        '',
        'Upload these files:',
        '',
        '1. `AI_CONTEXT_INDEX.md`',
        '2. `AI_CONTEXT.md`',
        '',
        'Do not upload numbered chunk files; none are required.',
        ''
    );
} else {
    indexLines.push(
        '| File | Included files | Size | Range |',
        '|---|---:|---:|---|'
    );

    for (const chunk of chunkRecords) {
        indexLines.push(
            `| \`${chunk.fileName}\` | ${chunk.fileCount} | ` +
            `${formatBytes(chunk.size)} | ` +
            `\`${chunk.firstFile}\` → \`${chunk.lastFile}\` |`
        );
    }

    indexLines.push(
        '',
        '## Upload instructions',
        '',
        'Upload these files:',
        '',
        '1. `AI_CONTEXT_INDEX.md`',
        `2. All numbered context chunks from \`AI_CONTEXT_001.md\` through ` +
        `\`AI_CONTEXT_${String(chunks.length).padStart(3, '0')}.md\``,
        '',
        'Do not upload `AI_CONTEXT.md`; the numbered chunks contain the complete source set.',
        ''
    );
}

if (skipped) {
    indexLines.push(skipped);
}

writeUtf8(
    path.join(outputPath, 'AI_CONTEXT_INDEX.md'),
    indexLines.join('\n')
);

    const sourceBytes = result.files.reduce(
        (sum, file) => sum + file.size,
        0
    );

    console.log('');
    console.log('AI context generated successfully');
    console.log(`Included files : ${result.files.length}`);
    console.log(`Source size    : ${formatBytes(sourceBytes)}`);
    console.log(`Source lines   : ${result.files.reduce((sum, file) => sum + file.lines, 0)}`);
    console.log(`Estimated tokens: ${estimateTokens(sourceBytes).toLocaleString('en-US')}`);
    console.log(`Skipped files  : ${result.skipped.length}`);
	console.log(`Context parts   : ${chunks.length}`);
	console.log(`Output          : ${outputPath}`);
	console.log('');

	if (chunks.length === 1) {
		console.log('Upload:');
		console.log('  AI_CONTEXT_INDEX.md');
		console.log('  AI_CONTEXT.md');
	} else {
		console.log('Upload:');
		console.log('  AI_CONTEXT_INDEX.md');
		console.log(
			`  AI_CONTEXT_001.md through ` +
			`AI_CONTEXT_${String(chunks.length).padStart(3, '0')}.md`
		);
	}
}

try {
    main();
} catch (error) {
    console.error('');
    console.error(`ERROR: ${error.message}`);
    process.exitCode = 1;
}

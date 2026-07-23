# MVX BASIC for VSCode

Language support for MVX Pick/MultiValue BASIC.

- Syntax highlighting for the full MVX dialect, including dynamic-array
  system variables (`@AM`, `@VM`, ...), numeric labels, and docblock
  `@tags` inside comments
- Comment toggling (`*`), auto-indent for `IF/END`, `LOOP/REPEAT`,
  `BEGIN CASE`, bracket pairs
- Snippets: `docblock`, `if`, `ifelse`, `for`, `loop`, `case`, `read`,
  `open`, `subroutine`
- The `$mvx` problem matcher turns compiler errors (`item:line:
  message`) into clickable diagnostics

## Install (local)

```sh
ln -s "$(pwd)/editors/vscode/mvx-basic" ~/.vscode/extensions/mvx-basic-0.1.0
```

then reload VSCode. Files with the `.b` extension, and files inside a
`BP/` directory, open as MVX BASIC. For other locations add to your
settings:

```json
"files.associations": { "**/BP*/*": "mvx-basic" }
```

## Compile task

`.vscode/tasks.json` in your project:

```json
{
  "version": "2.0.0",
  "tasks": [
    {
      "label": "mvx: compile current file",
      "type": "shell",
      "command": "/Users/gordon/Source/mvx-lang/build/bin/mvx",
      "args": ["-c", "${file}", "-o", "/tmp/${fileBasename}.o"],
      "group": { "kind": "build", "isDefault": true },
      "problemMatcher": "$mvx"
    }
  ]
}
```

Cmd+Shift+B compiles the open program; errors appear in Problems and
jump to the offending line.

# nimx

Nim Script Loader written in C++

Compile with:

```
g++ -std=c++17 -O2 -o nimx nimx.cpp
```

(needs `g++`, not plain `gcc` — it links libstdc++ automatically, which `nimx.cpp` needs for `<filesystem>`, `<string>`, etc.)

Use with e.g. this she-bang:

```
#!/usr/local/bin/nimx
```

* Better than using restricted NimScript - you can use the full Nim command set
* No complicated she-bang line with unnecessary arguments
* Caches compiled files in `~/.nimx` - not clobbering your script folder
* Handles filenames Nim itself wouldn't accept as a module name (dashes, dots, etc. - e.g. `pw6-project-create`) by compiling a sanitized stand-in name instead of the script's literal filename
* Delegates actual change-detection to `nim r` itself, so editing an `include`d or `import`ed file correctly triggers a recompile - not just edits to the top-level script

## How it works

1. Resolves the script to its real, canonical path.
2. Derives a valid Nim identifier from the filename (non-identifier characters become `_`) plus a short hash of the full path, so two scripts with the same basename in different folders don't collide.
3. Builds a "shadow" directory under `~/.nimx/<name>_env/` that symlinks every sibling file and folder from the script's real directory into it - so relative `include`/`import` paths (like `include/procs.nim`) still resolve correctly, since Nim resolves those relative to the compiled file's own directory. This shadow directory is rebuilt from scratch on every run, so it can't go stale if you add or remove sibling files.
4. Inside that shadow directory, writes an actual *copy* of the script's contents under the sanitized name (a symlink doesn't work here - Nim resolves symlinks back to the real path when deriving the module name, which would just reintroduce the "invalid module name" error for a dashed filename).
5. Runs `nim r --nimcache:~/.nimx ...` on that copy, passing your script's own arguments through untouched via `execvp` (no shell involved, so no manual quote-escaping for arguments with spaces).

Because step 5 hands off to `nim r`, the actual compiled objects/binaries also land in `~/.nimx` (not next to your script), and whether anything needs recompiling at all is decided by Nim's own dependency hashing rather than a hand-rolled mtime check - so it's correct even when only an included file changed.

Note: this version no longer pipes the script through stdin to work around dash-containing filenames (as earlier versions did) - the shadow-directory + sanitized-copy approach above handles that instead, while additionally fixing relative includes, which stdin-piping did not.

I'd recommend you also checkout these and see what you like best:

* https://github.com/flaviut/nimrun
* https://github.com/Jeff-Ciesielski/nimr
* https://github.com/PMunch/nimcr

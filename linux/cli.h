// linux/cli.h -- command-line interface shared by mctde-launcher (window + CLI) and
// mctde-launcher-cli (CLI only, no Qt dependency).
#pragma once

#include <string>

// Folder the running executable lives in (the PTDE DATA folder, when deployed).
std::string ExeDir();
// Run a command line: `[--dir <DATA>] <command> ...`. Returns the process exit code.
int RunCli(int argc, char** argv);

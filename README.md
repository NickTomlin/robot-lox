# robot-lox

An entirely robot written bytecode VM interpreter for the [Lox language](https://craftinginterpreters.com). 

This was produced to show that all the artisanal hand-typing I did in [my edition](https://github.com/NickTomlin/clox) and most human endeavour after the year of our Claude 2026 is worthless. Enjoy.

## The language

Dynamically typed. C-like syntax.

```lox
class Animal {
    init(name) { this.name = name; }
    speak() { print this.name + " makes a sound."; }
}

class Dog < Animal {
    speak() { print this.name + " barks."; }
}

var d = Dog("Rex");
d.speak();  // Rex barks.
```

Features: variables, closures, classes with single inheritance, `for`/`while`, first-class functions. Falsy values: `nil` and `false`. Everything else is truthy.

## Build

```sh
make          # builds build/lox
make test     # runs golden tests
make clean
```

Requires gcc and a POSIX shell.

## Usage

```sh
./build/lox script.lox   # run a file
./build/lox              # REPL
```

Exit codes: 0 success · 65 compile error · 70 runtime error.

## Tests

Golden tests live in `tests/golden/`. Each test is a directory containing `input.lox` and `expected_stdout.txt` (and optionally `expected_stderr.txt`). Add a new directory to add a new test.

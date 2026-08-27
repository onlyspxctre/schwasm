# schwasm

Schwasm is a lightweight assembler for the **G-CPU**. It parses an `.asm` assembly file and generates `.mif` files to be used with the **G-CPU**. This is heavily based on the example assembler built for a simple RALU shown in `splexer` example source code. Currently supports Linux and Windows (macOS is untested).

## Features

Schwasm is designed according to the [**G-CPU Specification**](https://mil.ufl.edu/3701/labs/gcpu/G-CPU_Complete_Docs_5Apr2024.pdf), and strives for functional parity with the official, proprietary **G-CPU IDE**. The assembly language syntax itself is an in-house syntax created by Professor Eric M. Schwartz.

- Direct, extended, and immediate addressing into the **4kx8 ROM** and **4kx8 RAM**.
- Both decimal and hexadecimal (`$`) integer literals are supported.
- Error reporting on line and column number
- Depends entirely on in-house dependencies:
  - `splexer` (lexer/tokenizer)
  - `sptl.h` (header-only stdlib)

### Supported Instructions


| Mnemonic | Description                 |
| -------- | --------------------------- |
| TAB      | Transfer register A to B    |
| TBA      | Transfer register B to A    |
| LDAA     | Load to register A (8-bit)  |
| LDAB     | Load to register B (8-bit)  |
| LDX      | Load to register X (16-bit) |
| LDY      | Load to register Y (16-bit) |
| STAA     | Store from register A       |
| STAB     | Store from register B       |
| SUM_BA   | Add register B to A         |
| SUM_AB   | Add register A to B         |
| AND_BA   | AND register B into A       |
| AND_AB   | AND register A into B       |
| OR_BA    | OR register B into A        |
| OR_AB    | OR register A into B        |
| COMA     | Complement register A       |
| COMB     | Complement register B       |
| SHFA_L   | Shift register A left       |
| SHFA_R   | Shift register A right      |
| SHFB_L   | Shift register B left       |
| SHFB_R   | Shift register B right      |
| INX      | Increment register X        |
| INY      | Increment register Y        |
| BEQ      | Branch if A == 0            |
| BNE      | Branch if A != 0            |
| BN       | Branch if A < 0             |
| BP       | Branch if A > 0             |


### Preprocessor Directives


| Directive | Description                           |
| --------- | ------------------------------------- |
| ORG       | Set addr for next instruction(s)      |
| DC.B      | Define constant byte(s)               |
| DS.B      | Define storage (reserve bytes in RAM) |


## Building

To compile, simply run:

```bash
make
```

To build an optimized, statically linked release binary, run:

```bash
make RELEASE=y
```

To target Windows, run:

```bash
make WINDOWS=y
```

To clean everything up:

```bash
make clean depsclean
```

## Usage

```bash
Usage: ./schwasm <input.asm>
```

The assembler reads assembly source and writes a `.mif`-format memory file to stdout.

## License

This project is licensed under the [MIT License](LICENSE).

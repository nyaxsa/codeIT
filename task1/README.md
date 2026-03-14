# C++ File Management System (fstream)

Menu-driven console program demonstrating practical file handling in C++ using `fstream`.

## Features

- Create/clear a text file
- Write (overwrite) data to a file
- Append data to a file
- Read and display file contents (with line numbers)
- Handles invalid menu input and file open/read/write errors

## Build & Run (macOS / Linux)

```bash
g++ -std=c++17 -O2 -Wall -Wextra -pedantic main.cpp -o file_manager
./file_manager
```

## How to Use

- Choose an option (1-5)
- Enter a filename like `notes.txt`
- For write/append, type lines of text; enter `END` on its own line to finish



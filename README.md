# McGuffin — Files

## Part 1: web crawler

| File | What it does |
|---|---|
| `main.cpp` | Runs the crawler: takes the seed url (argument or prompt) and an optional output folder, then prints every url found. |
| `url.h` | Splits a url into its parts (scheme, host, port, path, query, fragment) and checks for links that are always skipped. |
| `urlStandard.h` / `urlStandard.cpp` | Normalizes every link into one standard key, so the same page is never stored twice, and finds each host's site with libpsl. |
| `dnsResolver.h` / `dnsResolver.cpp` | Looks up a host's IPv4 and IPv6 addresses with the Asio resolver and caches them. |
| `fetch.h` / `fetch.cpp` | Downloads a page over `tcp_stream` (http) or `ssl_stream` over `tcp_stream` (https), follows redirects, checks certificates, and reports what happened. |
| `extractor.h` / `extractor.cpp` | Reads a page with readlib's `readline(stream, '<')` and returns the links to other pages, plus `<base>`, meta refresh and a hashable body. |
| `crawler.h` / `crawler.cpp` | Schedules fetches, builds the link graph (pages, weighted edges, merges), saves it as JSON with a log, and returns the list of fetch urls. |
| `read.h` | readlib, copied from https://github.com/ShakaUVM/read; used by the extractor. |
| `cacert.pem` | Bundled trusted certificates, used when the system certificate store isn't available (needed on Windows). |
| `data/` | Default output folder for each crawl's `.json` and `.log` files (not committed). |
| `graph.cpp` | Runs the BFS and DFS crawls, adds each page's links, and finds shortest distances from the start url. |
| `graph.h` | Declares `WebsiteGraph`, `GraphNode` and the `LinkFetcher` callback the crawler provides. |
| `graph_demo.cpp` | Test program for `WebsiteGraph` with a fake site; has its own `main`. |

## Build files

| File | What it does |
|---|---|
| `CMakeLists.txt` | Builds `McGuffin` (C++23); on Windows it also copies the vcpkg DLLs and `cacert.pem` next to the exe. |
| `vcpkg.json` | Library list for vcpkg: Boost (Asio, Beast, JSON), OpenSSL and libpsl. |

## Get the code from GitHub

- **CLion:** File → New → Project from Version Control, then enter `https://github.com/gdurant1/McGuffin.git`.
- **Terminal:** `git clone https://github.com/gdurant1/McGuffin.git`

Then follow one of the two sections below.

## Build and run (CLion, Windows)

1. Install vcpkg at `C:\vcpkg` (in PowerShell):
   `git clone https://github.com/microsoft/vcpkg.git C:\vcpkg` then `C:\vcpkg\bootstrap-vcpkg.bat`
2. In Settings → Build, Execution, Deployment → CMake, set the CMake options to:
   `-DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake -DVCPKG_TARGET_TRIPLET=x64-mingw-dynamic -DVCPKG_HOST_TRIPLET=x64-mingw-dynamic`
3. Reload CMake. The first load builds the libraries and takes about 30 minutes.
4. In Run → Edit Configurations → McGuffin, set the working directory to `$ProjectFileDir$`. Program arguments are the seed url, for example `https://site.com/`, and an optional output folder.
5. Build and run. The urls print when the crawl ends; progress goes to `data/<seed>.log`.

## Build and run (GitHub Codespaces or Linux)

1. On the repo page, click Code → Codespaces → Create codespace on main. On Linux, open a terminal in the cloned folder instead.
2. Install vcpkg and its tools (one time):
   ```
   sudo apt-get update && sudo apt-get install -y build-essential cmake ninja-build pkg-config curl zip unzip tar
   git clone https://github.com/microsoft/vcpkg.git ~/vcpkg && ~/vcpkg/bootstrap-vcpkg.sh
   ```
3. Build (the first build compiles the libraries and takes a while):
   ```
   cmake -B build -S . -G Ninja -DCMAKE_TOOLCHAIN_FILE=$HOME/vcpkg/scripts/buildsystems/vcpkg.cmake
   cmake --build build
   ```
4. Run from the project folder so `data/` and `cacert.pem` are found:
   ```
   ./build/McGuffin https://site.com/
   ```
   To get a crawl file out of a Codespace, right-click it under `data/` in the file panel and choose Download.

Running the same seed again loads the saved `.json` and adds to it.

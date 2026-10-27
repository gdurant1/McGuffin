# McGuffin

## Files

| File | What it does |
|---|---|
| `main.cpp` | Runs the program: takes the seed url (argument or prompt) and an optional output folder, crawls, prints every url found, then runs BFS and DFS and lists the most-linked pages. |
| `url.h` | Splits a url into its parts (scheme, host, port, path, query, fragment) and checks for links that are always skipped. |
| `urlStandard.h` / `urlStandard.cpp` | Normalizes every link into one standard key, so the same page is never stored twice, and finds each host's site with libpsl. |
| `dnsResolver.h` / `dnsResolver.cpp` | Looks up a host's IPv4 and IPv6 addresses with the Asio resolver and caches them. |
| `fetch.h` / `fetch.cpp` | Downloads a page over `tcp_stream` (http) or `ssl_stream` over `tcp_stream` (https), follows redirects, checks certificates, and reports what happened. |
| `extractor.h` / `extractor.cpp` | Reads a page with readlib's `readline(stream, '<')` and returns the links to other pages, plus `<base>`, meta refresh and a hashable body. |
| `crawler.h` / `crawler.cpp` | Schedules fetches, builds the link graph (pages, weighted edges, merges), saves it as JSON with a log, and returns the list of fetch urls. |
| `graph.h` | Declares `WebsiteGraph`, `GraphNode` (with link weights, incoming weight and status) and the `LinkFetcher` callback. |
| `graph.cpp` | Loads the finished crawl into `WebsiteGraph`, runs BFS and DFS over it, and finds shortest distances from the start url; also keeps the original fetch-as-you-go `crawlBFS` and `crawlDFS`. |
| `graph_demo.cpp` | Test program for `WebsiteGraph` with a fake site; has its own `main` and must be built with the crawler files. |
| `bridges.h` | Reads `data/<seed>_graph.json` and sends the graph to BRIDGES (assignment 110), colored by distance from the seed. |
| `bridges/bridges-cxx/` | The BRIDGES C++ library, kept as a git submodule (see "Get the code from GitHub"). |
| `read.h` | readlib, copied from https://github.com/ShakaUVM/read; used by the extractor. |
| `cacert.pem` | Bundled trusted certificates, used when the system certificate store isn't available (needed on Windows). |
| `CMakeLists.txt` | Builds `McGuffin` (C++23); on Windows it also copies the vcpkg DLLs and `cacert.pem` next to the exe. |
| `vcpkg.json` | Library list for vcpkg: Boost (Asio, Beast, JSON), OpenSSL, libpsl, and curl and RapidJSON for BRIDGES. |
| `data/` | Default output folder for each crawl's `.json` and `.log` files (not committed). |

## Why some nodes have no lines

The graph page (`web/index.html`) shows only part of a crawl so it stays fast and readable: the fetched and failed pages, the most-linked unfetched pages, and the heaviest links, up to a set limit. Navigation links (nav, header, footer, aside) are hidden on purpose. A dot with no lines still has links in the crawl data; they just weren't drawn. Usually they were low-weight links cut by the limit, menu links, or links to pages that aren't shown. Failed pages were never downloaded, so they have no outgoing links at all.

## Get the code from GitHub

- **CLion:** File → New → Project from Version Control, then enter `https://github.com/gdurant1/McGuffin.git`.
- **Terminal:** `git clone --recursive https://github.com/gdurant1/McGuffin.git`

The BRIDGES library is a submodule. If `bridges/bridges-cxx` is empty after cloning (including in CLion or a Codespace), run this in the project folder:
```
git submodule update --init
```

Then follow one of the two sections below.

## Build and run (CLion, Windows)

1. Install vcpkg at `C:\vcpkg` (in PowerShell):
   `git clone https://github.com/microsoft/vcpkg.git C:\vcpkg` then `C:\vcpkg\bootstrap-vcpkg.bat`
2. In Settings → Build, Execution, Deployment → CMake, set the CMake options to:
   `-DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake -DVCPKG_TARGET_TRIPLET=x64-mingw-dynamic -DVCPKG_HOST_TRIPLET=x64-mingw-dynamic`
3. Reload CMake. The first load builds the libraries and takes about 30 minutes.
4. In Run → Edit Configurations → McGuffin, set the working directory to `$ProjectFileDir$`. Program arguments are the seed url, for example `https://site.com/`, and an optional output folder.
5. Build and run. The urls, BFS/DFS counts and most-linked pages print when the crawl ends; progress goes to `data/<seed>.log`.

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

## View the graph on BRIDGES

After the BFS, the program sends the graph to BRIDGES and prints the link. The visualization is at:

http://assignments.bridgesuncc.org/assignments/110/gTayona

Each run replaces what's shown there with the latest crawl.

## View the graph (web folder)

`web/index.html` shows a crawl as an interactive graph. It opens with a made-up sample; there are two ways to show a real crawl.

**Option A: serve the project (easiest in Codespaces).** The page lists every `.json` in `data/` in a menu next to the buttons.

1. Run a crawl first so there is a `data/<seed>.json` file.
2. From the project folder (not from inside `web/`, so both `web/` and `data/` are reachable), start a server:
   ```
   python3 -m http.server 8000
   ```
   On Windows, use `py -m http.server 8000` if `python3` isn't found.
3. Open the page:
   - **Codespaces:** in the **Ports** tab, click the globe icon next to port 8000, then add `/web/` to the end of the address.
   - **Local:** go to `http://localhost:8000/web/`.
4. Pick a crawl from the **Load from data/** menu. To open one directly, add `?file=` to the address, for example `/web/?file=books.toscrape.com.json`.
5. Stop the server with Ctrl+C when you're done.

**Option B: open a file from your computer.** Open `web/index.html` in a browser (no server needed) and click **Open crawl .json**. This picker only sees files on your own computer, so in Codespaces download the crawl first: right-click it under `data/` and choose Download.
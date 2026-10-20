# McGuffin Part 1 — Files

| File | What it does |
|---|---|
| `url.h` | Splits a url into its parts (scheme, host, port, path, query, fragment) and checks for links that are always skipped. |
| `urlStandard.h` / `urlStandard.cpp` | Normalizes every link into one standard key, so the same page is never stored twice, and finds each host's site with libpsl. |
| `dnsResolver.h` / `dnsResolver.cpp` | Looks up a host's IPv4 and IPv6 addresses with the Asio resolver and caches them. |
| `fetch.h` / `fetch.cpp` | Downloads a page over `tcp_stream` (http) or `ssl_stream` over `tcp_stream` (https), follows redirects, checks certificates, and reports what happened. |
| `extractor.h` / `extractor.cpp` | Reads a page with readlib's `readline(stream, '<')` and returns the links to other pages, plus `<base>`, meta refresh and a hashable body. |
| `crawler.h` / `crawler.cpp` | Schedules fetches, builds the link graph (pages, weighted edges, merges), saves it as JSON with a log, and returns the list of fetch urls. |
| `read.h` | readlib, copied from https://github.com/ShakaUVM/read; used by the extractor. |
| `cacert.pem` | Bundled trusted certificates, used when the system certificate store isn't available (needed on Windows). |
| `data/` | Default output folder for each crawl's `.json` and `.log` files. |

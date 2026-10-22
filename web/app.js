// mcguffin graph viewer: loads a crawl .json, draws it, sends waves and animates path searches

const settings = {
    maxUnfetched: 100,  // most-linked unfetched pages to include
    maxLinks: 4000,     // heaviest links drawn at the start; the rest appear when a search uses them
    maxSteps: 400,      // longest search animation, in steps (big searches color several links per step)
    warnPages: 2000,    // more pages than this shows a "may be slow" warning before a run
    warnLinks: 20000,   // more links than this shows the same warning
    skipNavigation: true
};
// line color by weight tier: light yellow (weight 1) to dark red (257+)
const tierColors = ["#f6c343", "#f2ae3b", "#ee9835", "#e9802f", "#e1672a", "#d34f26", "#c03b23", "#a62a20", "#88191c", "#6a0f17"];
// node color by hops from the start: light purple (1) to dark purple (6+)
const hopColors = ["#c9c1f7", "#ab9ff0", "#8d7de6", "#6f5bd9", "#5241bf", "#372a8f"];
const waveColors = ["#e34948", "#eb6834", "#eda100", "#1baf7a", "#2a78d6", "#6250d6", "#e87ba4"];

// colors come from style.css so light and dark themes both work
function css(name) {
    return getComputedStyle(document.documentElement).getPropertyValue(name).trim();
}

let nodes = [];
let allLinks = [];              // every link between included pages
let shownLinks = [];            // links that have a line on screen
let nodeById = new Map();
let pickMode = null;            // "start" or "end" while picking
let startId = null;
let endId = null;
let focusId = null;             // node pushed to the center by right-click
let timers = [];
let lastTap = { id: null, time: 0 };
let simulation = null;
let crawlInfo = {};
let hops = new Map();           // page id -> links from the start, for the current run

const svg = d3.select("#graph");
const zoomLayer = svg.append("g");
const linkLayer = zoomLayer.append("g");
const nodeLayer = zoomLayer.append("g");
const tooltip = document.getElementById("tooltip");

// weight to tier 0-9 by doubling ranges: 1, 2, 3-4, 5-8, ... 257+
function tierFor(weight) {
    let tier = 0;
    let limit = 1;
    while (weight > limit && tier < 9) {
        limit *= 2;
        tier++;
    }
    return tier;
}

function linkColor(link) {
    return tierColors[tierFor(link.weight)];
}

function linkWidth(link) {
    return Math.min(4, 0.8 + link.weight * 0.3);
}

function nodeColor(node) {
    const names = { fetched: "--fetched", failed: "--failed", unfetched: "--unfetched", queued: "--unfetched" };
    return css(names[node.status] || "--other");
}

function nodeSize(node) {
    return Math.min(22, 4 + Math.sqrt(node.incoming) * 0.9);
}

// small made-up site so the page shows something before a file is opened
function makeSample() {
    const pages = [];
    const edges = [];
    const sections = ["docs", "blog", "shop", "about", "help"];
    pages.push({ id: 0, key: "site.com/", status: "fetched", depth: 0, hostId: 0 });
    sections.forEach((s, i) => pages.push({ id: i + 1, key: "site.com/" + s, status: "fetched", depth: 1, hostId: 0 }));
    for (let i = 0; i < 30; i++) {
        pages.push({ id: 6 + i, key: "site.com/" + sections[i % 5] + "/page-" + i, status: i % 9 === 4 ? "failed" : i > 24 ? "unfetched" : "fetched", depth: 2, hostId: 0 });
    }
    pages.push({ id: 36, key: "site.com/archive", status: "fetched", depth: 2, hostId: 0 });
    for (let s = 1; s <= 5; s++) {
        edges.push({ from: 0, to: s, weight: 2 });
        edges.push({ from: s, to: 0, weight: 1 });
    }
    for (let i = 0; i < 30; i++) {
        const id = 6 + i;
        edges.push({ from: 1 + (i % 5), to: id, weight: 1 + (i % 3) });
        if (i > 0) edges.push({ from: id, to: id - 1, weight: 1 });
        if (i % 4 === 0) edges.push({ from: id, to: 6 + ((i * 7) % 30), weight: 4 + i * 8 });
    }
    edges.push({ from: 36, to: 20, weight: 1 });
    return { crawl: { seed: "https://site.com/", runs: 1 }, hosts: [{ id: 0, site: "site.com" }], pages, edges, sample: true };
}

// pick the pages and links to include, the same way bridgesView does
function buildGraph(raw) {
    const pages = new Map();
    for (const p of raw.pages || []) {
        if (p.id >= 0 && p.status !== "nonHtml") {
            pages.set(p.id, { id: p.id, key: p.key, status: p.status, depth: p.depth ?? 0, incoming: 0 });
        }
    }
    const edges = [];
    for (const e of raw.edges || []) {
        if (pages.has(e.from) && pages.has(e.to) && !(settings.skipNavigation && e.navigationByLocation)) {
            pages.get(e.to).incoming += e.weight;
            edges.push({ from: e.from, to: e.to, weight: e.weight || 1 });
        }
    }
    const chosen = new Set();
    const unfetched = [];
    for (const p of pages.values()) {
        if (p.status === "unfetched" || p.status === "queued") {
            unfetched.push(p);
        }
        else {
            chosen.add(p.id);
        }
    }
    unfetched.sort((a, b) => b.incoming - a.incoming).slice(0, settings.maxUnfetched).forEach(p => chosen.add(p.id));

    nodes = [...chosen].map(id => pages.get(id));
    nodeById = new Map(nodes.map(n => [n.id, n]));
    for (const n of nodes) {
        n.out = [];
        n.in = [];
        n.drawnDegree = 0;
        n.revealed = false;
    }
    allLinks = edges.filter(e => chosen.has(e.from) && chosen.has(e.to)).sort((a, b) => b.weight - a.weight)
        .map((e, i) => ({ from: e.from, to: e.to, weight: e.weight, drawn: i < settings.maxLinks, revealed: false, element: null }));
    for (const l of allLinks) {
        nodeById.get(l.from).out.push(l);
        nodeById.get(l.to).in.push(l);
        if (l.drawn) {
            nodeById.get(l.from).drawnDegree++;
            nodeById.get(l.to).drawnDegree++;
        }
    }
}

// summary strip: totals for the whole crawl file, plus what is on screen
function renderStats(raw) {
    const counts = { fetched: 0, failed: 0, unfetched: 0 };
    for (const p of raw.pages || []) {
        const status = p.status === "queued" ? "unfetched" : p.status;
        if (counts[status] !== undefined) counts[status]++;
    }
    const sites = new Set((raw.hosts || []).map(h => h.site));
    const onScreen = nodes.filter(isVisible).length;
    const items = [
        ["Pages", (raw.pages || []).length, "nodes saved"],
        ["Fetched", counts.fetched, "downloaded"],
        ["Unfetched", counts.unfetched, "found, not downloaded"],
        ["Failed", counts.failed, "errors after retries"],
        ["Links", (raw.edges || []).length, "in the crawl file"],
        ["On screen", onScreen, shownLinks.length + " lines drawn"],
        ["Sites", sites.size, "hosts: " + (raw.hosts || []).length],
        ["Runs", (raw.crawl && raw.crawl.runs) || 1, (raw.crawl && raw.crawl.savedAt) ? "saved " + raw.crawl.savedAt : ""]
    ];
    document.getElementById("stats").innerHTML = items.map(([label, value, note]) =>
        `<div class="stat"><span class="label">${label}</span><span class="value">${Number(value).toLocaleString()}</span><span class="note">${note}</span></div>`).join("");
}

// a page is drawn if it has a line, was reached by a search, or is the start or end
function isVisible(node) {
    return node.drawnDegree > 0 || node.revealed || node.id === startId || node.id === endId;
}

function updateVisibility() {
    nodes.forEach(n => d3.select(n.element).classed("lone", !isVisible(n)));
}

// make a line for a link that has none yet (used when a search reaches a hidden link)
function ensureLine(link) {
    if (link.element) return;
    const from = nodeById.get(link.from);
    const to = nodeById.get(link.to);
    link.element = linkLayer.append("line")
        .attr("stroke", linkColor(link)).attr("stroke-width", linkWidth(link)).attr("stroke-opacity", 0.85)
        .attr("x1", from.x).attr("y1", from.y).attr("x2", to.x).attr("y2", to.y).node();
    shownLinks.push(link);
}

// show a hidden node next to the page that led to it
function revealNode(node, near) {
    if (!isVisible(node) && near) {
        node.x = near.x + (Math.random() - 0.5) * 40;
        node.y = near.y + (Math.random() - 0.5) * 40;
        node.vx = 0;
        node.vy = 0;
    }
    node.revealed = true;
    d3.select(node.element).classed("lone", false).classed("found", true);
}

function revealLink(link) {
    const from = nodeById.get(link.from);
    const to = nodeById.get(link.to);
    revealNode(from, to);
    revealNode(to, from);
    link.revealed = true;
    ensureLine(link);
    link.element.classList.add("found");
}

// discovery mode: only start and end show at first; the rest appears as the algorithm finds it
function enterDiscovery() {
    exitDiscovery();
    if (!document.getElementById("hideUntilFound").checked) return;
    document.getElementById("graph").classList.add("discover");
    for (const id of [startId, endId]) {
        if (id !== null) nodeById.get(id).element.classList.add("found");
    }
}

// back to the full graph (used by clear, a new run and loading a file)
function exitDiscovery() {
    document.getElementById("graph").classList.remove("discover");
    document.querySelectorAll("#graph .found").forEach(element => element.classList.remove("found"));
}

// remove lines and nodes that a search revealed
function hideRevealed() {
    shownLinks = shownLinks.filter(l => {
        if (l.drawn) return true;
        l.element.remove();
        l.element = null;
        l.revealed = false;
        return false;
    });
    nodes.forEach(n => { n.revealed = false; });
    updateVisibility();
}

// draw nodes and the starting links with a force layout
function render() {
    if (simulation) simulation.stop();
    linkLayer.selectAll("*").remove();
    nodeLayer.selectAll("*").remove();
    shownLinks = [];
    allLinks.filter(l => l.drawn).forEach(ensureLine);

    const circles = nodeLayer.selectAll("circle").data(nodes).join("circle")
        .attr("r", nodeSize)
        .attr("fill", nodeColor)
        .attr("stroke", css("--surface"))
        .attr("stroke-width", 1.2)
        .on("pointerenter", (event, n) => { showTooltip(event, n); highlight(n.id); })
        .on("pointermove", (event, n) => showTooltip(event, n))
        .on("pointerleave", () => { tooltip.hidden = true; highlight(focusId); })
        .on("click", (event, n) => onNodeTap(n))
        .on("contextmenu", (event, n) => { event.preventDefault(); event.stopPropagation(); toggleFocus(n.id); })
        .call(d3.drag()
            .on("start", (event, n) => { if (!event.active) simulation.alphaTarget(0.2).restart(); n.fx = n.x; n.fy = n.y; })
            .on("drag", (event, n) => { n.fx = event.x; n.fy = event.y; })
            .on("end", (event, n) => { if (!event.active) simulation.alphaTarget(0); if (n.id !== focusId) { n.fx = null; n.fy = null; } }));
    nodes.forEach((n, i) => { n.element = circles.nodes()[i]; });
    updateVisibility();

    const box = document.getElementById("graphBox").getBoundingClientRect();
    const big = nodes.length > 400;
    simulation = d3.forceSimulation(nodes)
        .force("link", d3.forceLink(allLinks.filter(l => l.drawn).map(l => ({ source: l.from, target: l.to })))
            .id(n => n.id).distance(big ? 30 : 50).strength(big ? 0.05 : 0.2))
        .force("charge", d3.forceManyBody().strength(big ? -25 : -120).distanceMax(500))
        .force("center", d3.forceCenter(box.width / 2, box.height / 2))
        .force("collide", d3.forceCollide(n => nodeSize(n) + 1))
        .on("tick", () => {
            for (const l of shownLinks) {
                const from = nodeById.get(l.from);
                const to = nodeById.get(l.to);
                l.element.setAttribute("x1", from.x);
                l.element.setAttribute("y1", from.y);
                l.element.setAttribute("x2", to.x);
                l.element.setAttribute("y2", to.y);
            }
            for (const n of nodes) {
                n.element.setAttribute("cx", n.x);
                n.element.setAttribute("cy", n.y);
            }
        });

    const options = document.getElementById("pageList");
    options.innerHTML = "";
    for (const n of nodes) {
        const option = document.createElement("option");
        option.value = n.key;
        options.appendChild(option);
    }
}

function showTooltip(event, n) {
    const box = document.getElementById("graphBox").getBoundingClientRect();
    const away = hops.has(n.id) ? "\n" + hops.get(n.id) + " links from start" : "";
    tooltip.textContent = n.key + "\n" + n.status + ", depth " + n.depth + ", " + n.incoming + " incoming links" + away + "\nright-click to focus";
    tooltip.hidden = false;
    tooltip.style.left = Math.min(event.clientX - box.left + 12, box.width - tooltip.offsetWidth - 6) + "px";
    tooltip.style.top = (event.clientY - box.top + 12) + "px";
}

// brighten one node's lines and neighbors, dim the rest; null clears it
function highlight(id) {
    const graph = document.getElementById("graph");
    graph.classList.toggle("dim", id !== null);
    nodes.forEach(n => n.element && n.element.classList.remove("hot"));
    shownLinks.forEach(l => l.element.classList.remove("hot"));
    if (id === null) return;
    nodeById.get(id).element.classList.add("hot");
    for (const l of shownLinks) {
        if (l.from === id || l.to === id) {
            l.element.classList.add("hot");
            nodeById.get(l.from).element.classList.add("hot");
            nodeById.get(l.to).element.classList.add("hot");
        }
    }
}

// right-click: the node moves to the center and nearby nodes are pushed away; again releases it
function toggleFocus(id) {
    if (focusId !== null) {
        const old = nodeById.get(focusId);
        old.fx = null;
        old.fy = null;
    }
    if (id === null || id === focusId) {
        focusId = null;
        simulation.force("focus", null).alpha(0.3).restart();
        highlight(null);
        setStatus("");
        return;
    }
    focusId = id;
    const node = nodeById.get(id);
    node.fx = node.x;
    node.fy = node.y;
    simulation.force("focus", focusForce).alpha(0.6).restart();
    svg.transition().duration(500).call(zoom.translateTo, node.x, node.y);
    highlight(id);
    setStatus("Focused on " + node.key + ". Right-click it again, right-click the background or press Esc to release.");
}

// push nodes out of a circle around the focused node; its neighbors may sit closer
function focusForce(alpha) {
    const center = nodeById.get(focusId);
    if (!center) return;
    const near = new Set();
    for (const l of shownLinks) {
        if (l.from === focusId) near.add(l.to);
        if (l.to === focusId) near.add(l.from);
    }
    for (const n of nodes) {
        if (n === center) continue;
        const dx = n.x - center.x;
        const dy = n.y - center.y;
        const distance = Math.hypot(dx, dy) || 1;
        const target = near.has(n.id) ? 110 : 240;
        if (distance < target) {
            const push = (target - distance) / distance * alpha * 0.5;
            n.vx += dx * push;
            n.vy += dy * push;
        }
    }
}

// picking: the tap sets start or end; otherwise two taps within 350 ms send a wave (works on phones too)
function onNodeTap(n) {
    if (pickMode !== null) {
        setEndpoint(pickMode, n.id);
        lastTap = { id: null, time: 0 };
        return;
    }
    const now = Date.now();
    if (lastTap.id === n.id && now - lastTap.time < 350) {
        sendWave(n.id);
        lastTap = { id: null, time: 0 };
        return;
    }
    lastTap = { id: n.id, time: now };
}

// turn pick mode on for "start" or "end" (pressing the same button again turns it off)
function startPicking(mode) {
    pickMode = pickMode === mode ? null : mode;
    document.getElementById("graphBox").classList.toggle("picking", pickMode !== null);
    document.getElementById("selectStart").classList.toggle("active", pickMode === "start");
    document.getElementById("selectEnd").classList.toggle("active", pickMode === "end");
    document.getElementById("pickNote").textContent = pickMode === null ? "" : "Click the " + pickMode + " node on the graph (Esc cancels).";
}

function setEndpoint(mode, id) {
    if (mode === "start") {
        startId = id;
        document.getElementById("startLabel").textContent = nodeById.get(id).key;
    }
    else {
        endId = id;
        document.getElementById("endLabel").textContent = nodeById.get(id).key;
    }
    startPicking(null);
    updateVisibility();
    markEndpoints();
}

// dark ring on the start node, accent ring on the end node
function markEndpoints() {
    nodes.forEach(n => {
        const ring = n.id === startId ? css("--fg") : n.id === endId ? css("--accent") : css("--surface");
        d3.select(n.element).attr("stroke", ring).attr("stroke-width", n.id === startId || n.id === endId ? 3.5 : 1.2);
    });
}

// table headings change between a path and a spanning tree
function setTableHead(labels) {
    document.getElementById("pathHead").innerHTML = labels.map((label, i) =>
        `<th class="${i === 1 ? "" : "num"}">${label}</th>`).join("");
}

// one row per page on the path, from start to end
function showPathTable(path) {
    const body = document.getElementById("pathBody");
    body.innerHTML = "";
    setTableHead(["#", "Page", "Link weight", "Page weight"]);
    document.getElementById("tableNote").textContent = "Link weight = number of links from the page above. Page weight = all incoming links to the page.";
    if (path.length === 0) {
        document.getElementById("pathSummary").textContent = "No path between start and end.";
        return;
    }
    const rows = [{ id: startId, weight: null }];
    let current = startId;
    for (const link of path) {
        const next = link.from === current ? link.to : link.from;
        rows.push({ id: next, weight: link.weight });
        current = next;
    }
    rows.forEach((row, i) => {
        const node = nodeById.get(row.id);
        const tr = document.createElement("tr");
        if (i === 0 || i === rows.length - 1) tr.className = "ends";
        const cells = [i, node.key, row.weight === null ? "start" : row.weight, node.incoming];
        cells.forEach((value, c) => {
            const td = document.createElement("td");
            td.className = c === 1 ? "page" : "num";
            td.textContent = value;
            tr.appendChild(td);
        });
        body.appendChild(tr);
    });
    const total = path.reduce((sum, link) => sum + link.weight, 0);
    document.getElementById("pathSummary").textContent = rows.length + " pages, " + path.length + " links, total link weight " + total + ".";
}

// neighbors of a node; searches use every link, the wave only lines on screen
function neighbors(id, onScreenOnly) {
    const twoWay = document.getElementById("twoWay").checked;
    const node = nodeById.get(id);
    const result = [];
    node.out.forEach(l => { if (!onScreenOnly || l.element) result.push({ id: l.to, link: l }); });
    if (twoWay) {
        node.in.forEach(l => { if (!onScreenOnly || l.element) result.push({ id: l.from, link: l }); });
    }
    return result;
}

function later(ms, action) {
    timers.push(setTimeout(action, ms));
}

// put every color back and cancel running animations
function clearAnimation() {
    timers.forEach(clearTimeout);
    timers = [];
    shownLinks.forEach(l => d3.select(l.element).interrupt().attr("stroke", linkColor(l)).attr("stroke-opacity", 0.85).attr("stroke-width", linkWidth(l)));
    nodes.forEach(n => d3.select(n.element).interrupt().attr("fill", nodeColor(n)));
    markEndpoints();
}

// wave: bfs levels from the node over the lines on screen; each level flashes a color, then fades back
function sendWave(id) {
    clearAnimation();
    const level = new Map([[id, 0]]);
    let frontier = [id];
    let depth = 0;
    const stepMs = 260;
    flashNode(id, 0, stepMs);
    while (frontier.length > 0) {
        const next = [];
        for (const current of frontier) {
            for (const { id: neighbor, link } of neighbors(current, true)) {
                if (!level.has(neighbor)) {
                    level.set(neighbor, depth + 1);
                    next.push(neighbor);
                }
                if (level.get(neighbor) === depth + 1) {
                    flashLink(link, depth + 1, stepMs);
                    flashNode(neighbor, depth + 1, stepMs);
                }
            }
        }
        frontier = next;
        depth++;
    }
    setStatus("Wave from " + nodeById.get(id).key + " reached " + (level.size - 1) + " pages in " + Math.max(0, depth - 1) + " steps.");
}

function flashLink(link, step, stepMs) {
    const color = waveColors[(step - 1) % waveColors.length];
    d3.select(link.element).transition().delay(step * stepMs).duration(120)
        .attr("stroke", color).attr("stroke-opacity", 1)
        .transition().delay(stepMs * 2).duration(500)
        .attr("stroke", linkColor(link)).attr("stroke-opacity", 0.85);
}

function flashNode(id, step, stepMs) {
    const node = nodeById.get(id);
    const color = waveColors[Math.max(0, step - 1) % waveColors.length];
    d3.select(node.element).transition().delay(step * stepMs).duration(120)
        .attr("fill", color)
        .transition().delay(stepMs * 2).duration(500)
        .attr("fill", nodeColor(node));
}

// shortest number of links from the start to every page it can reach
function computeHops(start, twoWay) {
    const result = new Map([[start, 0]]);
    const queue = [start];
    for (let head = 0; head < queue.length; head++) {
        const node = nodeById.get(queue[head]);
        const next = node.out.map(l => l.to);
        if (twoWay) node.in.forEach(l => next.push(l.from));
        for (const id of next) {
            if (!result.has(id)) {
                result.set(id, result.get(queue[head]) + 1);
                queue.push(id);
            }
        }
    }
    return result;
}

// purple by hops; the start keeps its own color, pages the start can't reach use the fallback
function hopFill(id, fallback) {
    if (!hops.has(id)) return fallback;
    const away = hops.get(id);
    return away === 0 ? nodeColor(nodeById.get(id)) : hopColors[Math.min(away, hopColors.length) - 1];
}

// a found line keeps its weight color, drawn thicker and solid
function showFoundLink(link) {
    revealLink(link);
    d3.select(link.element).attr("stroke", linkColor(link)).attr("stroke-width", linkWidth(link) + 1.2).attr("stroke-opacity", 1);
}

// each search returns the links it explored (in order) and the path it found
function searchBfs(start, end) {
    const explored = [];
    const parent = new Map([[start, null]]);
    const queue = [start];
    let head = 0;
    while (head < queue.length) {
        const current = queue[head++];
        if (current === end) break;
        for (const { id, link } of neighbors(current, false)) {
            if (!parent.has(id)) {
                parent.set(id, { from: current, link });
                explored.push(link);
                queue.push(id);
            }
        }
    }
    return { explored, path: tracePath(parent, end), visited: parent.size };
}

function searchDfs(start, end) {
    const explored = [];
    const parent = new Map([[start, null]]);
    const stack = [start];
    const done = new Set();
    while (stack.length > 0) {
        const current = stack.pop();
        if (done.has(current)) continue;
        done.add(current);
        if (current === end) break;
        const list = neighbors(current, false);
        for (let i = list.length - 1; i >= 0; i--) {
            const { id, link } = list[i];
            if (!done.has(id)) {
                parent.set(id, { from: current, link });
                explored.push(link);
                stack.push(id);
            }
        }
    }
    return { explored, path: done.has(end) ? tracePath(parent, end) : [], visited: done.size };
}

// stronger links are shorter: cost = 1 / weight
function searchDijkstra(start, end) {
    const explored = [];
    const cost = new Map([[start, 0]]);
    const parent = new Map([[start, null]]);
    const done = new Set();
    const open = [{ id: start, cost: 0 }];
    while (open.length > 0) {
        let best = 0;
        for (let i = 1; i < open.length; i++) {
            if (open[i].cost < open[best].cost) best = i;
        }
        const { id: current } = open.splice(best, 1)[0];
        if (done.has(current)) continue;
        done.add(current);
        if (current === end) break;
        for (const { id, link } of neighbors(current, false)) {
            const next = cost.get(current) + 1 / link.weight;
            if (!done.has(id) && (!cost.has(id) || next < cost.get(id))) {
                cost.set(id, next);
                parent.set(id, { from: current, link });
                explored.push(link);
                open.push({ id, cost: next });
            }
        }
    }
    return { explored, path: done.has(end) ? tracePath(parent, end) : [], visited: done.size };
}

// follow parents back from the end; empty if the end was never reached
function tracePath(parent, end) {
    if (!parent.has(end)) return [];
    const path = [];
    let step = parent.get(end);
    while (step) {
        path.unshift(step.link);
        step = parent.get(step.from);
    }
    return path;
}

// one row per tree link, strongest first, with a running total
function showTreeTable(tree, treeCount) {
    const body = document.getElementById("pathBody");
    body.innerHTML = "";
    setTableHead(["#", "Link", "Weight", "Total"]);
    document.getElementById("tableNote").textContent = "Weight = links in both directions added together. Total = weight of the tree so far.";
    let total = 0;
    tree.forEach((edge, i) => {
        total += edge.weight;
        const tr = document.createElement("tr");
        const cells = [i + 1, nodeById.get(edge.a).key + "  \u2194  " + nodeById.get(edge.b).key, edge.weight, total];
        cells.forEach((value, c) => {
            const td = document.createElement("td");
            td.className = c === 1 ? "page" : "num";
            td.textContent = value;
            tr.appendChild(td);
        });
        body.appendChild(tr);
    });
    document.getElementById("pathSummary").textContent = tree.length + " links in the tree, total weight " + total + ". "
        + (treeCount > 1 ? treeCount + " separate trees (pages that can't reach each other)." : "One tree connects every page.");
}

// links as two-way connections: a->b and b->a become one edge with their weights added
function undirectedEdges() {
    const byPair = new Map();
    for (const l of allLinks) {
        const a = Math.min(l.from, l.to);
        const b = Math.max(l.from, l.to);
        let edge = byPair.get(a + "-" + b);
        if (!edge) {
            edge = { a, b, weight: 0, links: [] };
            byPair.set(a + "-" + b, edge);
        }
        edge.weight += l.weight;
        edge.links.push(l);
    }
    return [...byPair.values()];
}

// kruskal: strongest edges first; an edge that would close a loop is rejected
function treeKruskal() {
    const edges = undirectedEdges().sort((x, y) => y.weight - x.weight);
    const parent = new Map(nodes.map(n => [n.id, n.id]));
    const find = id => {
        while (parent.get(id) !== id) {
            parent.set(id, parent.get(parent.get(id)));
            id = parent.get(id);
        }
        return id;
    };
    const events = [];
    const tree = [];
    for (const edge of edges) {
        if (tree.length === nodes.length - 1) break;
        const rootA = find(edge.a);
        const rootB = find(edge.b);
        if (rootA === rootB) {
            events.push({ edge, accepted: false });
            continue;
        }
        parent.set(rootA, rootB);
        tree.push(edge);
        events.push({ edge, accepted: true });
    }
    return { events, tree, treeCount: nodes.length - tree.length };
}

// prim: grow from the start node, always taking the strongest edge that reaches a new page
function treePrim(start) {
    const adjacent = new Map(nodes.map(n => [n.id, []]));
    for (const edge of undirectedEdges()) {
        adjacent.get(edge.a).push(edge);
        adjacent.get(edge.b).push(edge);
    }
    const heap = [];
    const push = edge => {
        heap.push(edge);
        let i = heap.length - 1;
        while (i > 0 && heap[(i - 1) >> 1].weight < heap[i].weight) {
            [heap[i], heap[(i - 1) >> 1]] = [heap[(i - 1) >> 1], heap[i]];
            i = (i - 1) >> 1;
        }
    };
    const pop = () => {
        const top = heap[0];
        const last = heap.pop();
        if (heap.length > 0) {
            heap[0] = last;
            let i = 0;
            while (true) {
                let big = i;
                for (const child of [2 * i + 1, 2 * i + 2]) {
                    if (child < heap.length && heap[child].weight > heap[big].weight) big = child;
                }
                if (big === i) break;
                [heap[i], heap[big]] = [heap[big], heap[i]];
                i = big;
            }
        }
        return top;
    };
    const inTree = new Set([start]);
    adjacent.get(start).forEach(push);
    const events = [];
    const tree = [];
    while (heap.length > 0) {
        const edge = pop();
        const next = !inTree.has(edge.a) ? edge.a : !inTree.has(edge.b) ? edge.b : null;
        if (next === null) {
            events.push({ edge, accepted: false });
            continue;
        }
        inTree.add(next);
        tree.push(edge);
        events.push({ edge, accepted: true });
        adjacent.get(next).forEach(e => { if (!inTree.has(e.a) || !inTree.has(e.b)) push(e); });
    }
    return { events, tree, treeCount: 1, reached: inTree.size };
}

// accepted edges turn green as they join the tree; rejected ones flash gray
function runTree(algorithm) {
    if (algorithm === "prim" && startId === null) {
        setResult("Prim grows from the start node: press Select Start and pick one first.");
        return;
    }
    clearAnimation();
    hideRevealed();
    enterDiscovery();
    hops = startId === null ? new Map() : computeHops(startId, true);
    const result = algorithm === "prim" ? treePrim(startId) : treeKruskal();
    const stepMs = Number(document.getElementById("speed").value);
    const perStep = Math.max(1, Math.ceil(result.events.length / settings.maxSteps));
    const steps = Math.ceil(result.events.length / perStep);

    for (let s = 0; s < steps; s++) {
        later(s * stepMs, () => {
            for (const { edge, accepted } of result.events.slice(s * perStep, (s + 1) * perStep)) {
                if (accepted) {
                    for (const link of edge.links) {
                        showFoundLink(link);
                        d3.select(link.element).raise();
                    }
                    d3.select(nodeById.get(edge.a).element).attr("fill", hopFill(edge.a, css("--path")));
                    d3.select(nodeById.get(edge.b).element).attr("fill", hopFill(edge.b, css("--path")));
                }
                else {
                    for (const link of edge.links.filter(l => l.element)) {
                        link.element.classList.add("flash");
                        d3.select(link.element).interrupt().attr("stroke", css("--unfetched")).attr("stroke-opacity", 1)
                            .transition().duration(500).attr("stroke", linkColor(link)).attr("stroke-opacity", 0.85)
                            .on("end", () => link.element && link.element.classList.remove("flash"));
                    }
                }
            }
        });
    }
    later(steps * stepMs + 200, () => {
        markEndpoints();
        showTreeTable(result.tree, result.treeCount);
        renderStats(crawlInfo);
        const total = result.tree.reduce((sum, edge) => sum + edge.weight, 0);
        let text = "Strongest-link tree: " + result.tree.length + " links, total weight " + total + ".";
        if (algorithm === "prim" && result.reached < nodes.length) {
            text += " It reaches " + result.reached + " of " + nodes.length + " pages; the rest aren't connected to the start.";
        }
        setResult(text);
    });
    setResult("Running " + document.getElementById("algorithm").selectedOptions[0].text + "...");
}

// big graphs: show a warning with an estimate and wait for "Run anyway"
function confirmLarge(action) {
    const box = document.getElementById("warning");
    if (nodes.length <= settings.warnPages && allLinks.length <= settings.warnLinks) {
        box.hidden = true;
        action();
        return;
    }
    const stepMs = Number(document.getElementById("speed").value);
    const seconds = Math.ceil(settings.maxSteps * stepMs / 1000 + allLinks.length / 20000);
    document.getElementById("warningText").textContent = "This may be slow (about " + seconds + " seconds): "
        + nodes.length.toLocaleString() + " pages and " + allLinks.length.toLocaleString() + " links.";
    box.hidden = false;
    document.getElementById("runAnyway").onclick = () => { box.hidden = true; action(); };
    document.getElementById("cancelRun").onclick = () => { box.hidden = true; };
}

function runSelected() {
    const algorithm = document.getElementById("algorithm").value;
    if (algorithm === "kruskal" || algorithm === "prim") {
        confirmLarge(() => runTree(algorithm));
    }
    else {
        confirmLarge(runSearch);
    }
}

// color explored links step by step (hidden ones appear as they are reached), then draw the path
function runSearch() {
    if (startId === null || endId === null) {
        setResult("Pick a start and an end node first.");
        return;
    }
    clearAnimation();
    hideRevealed();
    enterDiscovery();
    hops = computeHops(startId, document.getElementById("twoWay").checked);
    const algorithm = document.getElementById("algorithm").value;
    const search = { bfs: searchBfs, dfs: searchDfs, dijkstra: searchDijkstra }[algorithm];
    const result = search(startId, endId);
    const stepMs = Number(document.getElementById("speed").value);
    const perStep = Math.max(1, Math.ceil(result.explored.length / settings.maxSteps));
    const steps = Math.ceil(result.explored.length / perStep);

    for (let s = 0; s < steps; s++) {
        later(s * stepMs, () => {
            for (const link of result.explored.slice(s * perStep, (s + 1) * perStep)) {
                showFoundLink(link);
                d3.select(nodeById.get(link.from).element).attr("fill", hopFill(link.from, css("--explored")));
                d3.select(nodeById.get(link.to).element).attr("fill", hopFill(link.to, css("--explored")));
            }
        });
    }
    later(steps * stepMs + 200, () => {
        result.path.forEach(link => {
            revealLink(link);
            d3.select(link.element).raise().attr("stroke", css("--path")).attr("stroke-width", 4).attr("stroke-opacity", 1);
            d3.select(nodeById.get(link.from).element).attr("fill", css("--path"));
            d3.select(nodeById.get(link.to).element).attr("fill", css("--path"));
        });
        markEndpoints();
        showPathTable(result.path);
        renderStats(crawlInfo);
        if (result.path.length === 0) {
            setResult("No path found. Visited " + result.visited + " pages. Try \"Follow links both ways\".");
        }
        else {
            setResult("Path found: " + result.path.length + " links. Visited " + result.visited + " pages, explored " + result.explored.length + " links.");
        }
    });
    setResult("Running " + document.getElementById("algorithm").selectedOptions[0].text + "...");
}

function setStatus(text) {
    document.getElementById("status").textContent = text;
}

function setResult(text) {
    document.getElementById("result").textContent = text;
}

// load a crawl file picked by the user
function loadFile(file) {
    document.getElementById("fileNote").textContent = "Reading " + file.name + "...";
    file.text().then(text => {
        const raw = JSON.parse(text);
        if (!Array.isArray(raw.pages)) throw new Error("no pages list");
        show(raw, file.name);
    }).catch(error => {
        document.getElementById("fileNote").textContent = "Couldn't open " + file.name + ": " + error.message;
    });
}

function show(raw, fileName) {
    clearAnimation();
    exitDiscovery();
    hops = new Map();
    focusId = null;
    pickMode = null;
    startId = null;
    endId = null;
    buildGraph(raw);
    crawlInfo = raw;
    document.getElementById("startLabel").textContent = "none";
    document.getElementById("endLabel").textContent = "none";
    showPathTable([]);
    document.getElementById("pathSummary").textContent = "Run a search to list the pages between start and end.";
    const isSample = raw.sample === true;
    document.getElementById("badge").hidden = !isSample;
    document.getElementById("seedLine").textContent = "Seed: " + ((raw.crawl && raw.crawl.seed) || "unknown") + (isSample ? "" : "  ·  " + fileName);
    document.getElementById("fileNote").textContent = isSample ? "Showing a made-up sample. Open a crawl file from your data folder."
        : "Loaded " + fileName + ".";
    render();
    renderStats(raw);
    setStatus("");
}

// when the project is served over http: load a crawl straight from data/
function loadFromServer(name) {
    document.getElementById("fileNote").textContent = "Loading " + name + "...";
    fetch("../data/" + encodeURIComponent(name)).then(response => {
        if (!response.ok) throw new Error("HTTP " + response.status);
        return response.json();
    }).then(raw => {
        if (!Array.isArray(raw.pages)) throw new Error("no pages list");
        show(raw, name);
    }).catch(error => {
        document.getElementById("fileNote").textContent = "Couldn't load " + name + ": " + error.message;
    });
}

// read the server's listing of data/ and put each .json in the menu; does nothing without a server
function listServerFiles() {
    fetch("../data/").then(response => response.ok ? response.text() : Promise.reject()).then(html => {
        const names = [...new DOMParser().parseFromString(html, "text/html").querySelectorAll("a")]
            .map(a => decodeURIComponent(a.getAttribute("href"))).filter(name => name.endsWith(".json"));
        if (names.length === 0) return;
        const menu = document.getElementById("dataFiles");
        menu.replaceChildren(new Option("Load from data/...", ""), ...names.map(name => new Option(name, name)));
        menu.hidden = false;
    }).catch(() => {});
}

// controls
document.getElementById("fileInput").addEventListener("change", e => { if (e.target.files[0]) loadFile(e.target.files[0]); });
document.getElementById("dataFiles").addEventListener("change", e => { if (e.target.value) loadFromServer(e.target.value); });
document.getElementById("sampleBtn").addEventListener("click", () => show(makeSample(), "sample"));
document.getElementById("selectStart").addEventListener("click", () => startPicking("start"));
document.getElementById("selectEnd").addEventListener("click", () => startPicking("end"));
document.addEventListener("keydown", e => {
    if (e.key !== "Escape") return;
    if (pickMode !== null) startPicking(null);
    else if (focusId !== null) toggleFocus(null);
});
document.getElementById("search").addEventListener("change", e => {
    const found = nodes.find(n => n.key === e.target.value);
    if (found && pickMode !== null) setEndpoint(pickMode, found.id);
    else if (found) document.getElementById("pickNote").textContent = "Press Select Start or Select end first, then pick the page again.";
});
document.getElementById("speed").addEventListener("input", e => { document.getElementById("speedLabel").textContent = e.target.value + " ms"; });
document.getElementById("run").addEventListener("click", runSelected);
document.getElementById("clear").addEventListener("click", () => {
    clearAnimation();
    hideRevealed();
    exitDiscovery();
    hops = new Map();
    setResult("");
    showPathTable([]);
    renderStats(crawlInfo);
    document.getElementById("pathSummary").textContent = "Run a search to list the pages between start and end.";
});

const zoom = d3.zoom().scaleExtent([0.1, 8]).on("zoom", event => zoomLayer.attr("transform", event.transform));
svg.call(zoom).on("dblclick.zoom", null);
svg.on("contextmenu", event => { event.preventDefault(); if (focusId !== null) toggleFocus(null); });
document.getElementById("resetView").addEventListener("click", () => {
    if (focusId !== null) toggleFocus(null);
    svg.transition().duration(300).call(zoom.transform, d3.zoomIdentity);
});

show(makeSample(), "sample");
listServerFiles();
const wantedFile = new URLSearchParams(location.search).get("file");
if (wantedFile) loadFromServer(wantedFile);
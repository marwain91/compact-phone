/* Minimal Markdown → HTML converter for Compact Phone docs.
 *
 * Scope: documentation-friendly subset of CommonMark. Supports headings,
 * paragraphs, bold / italic / inline code, fenced code blocks, ordered +
 * unordered lists (with nesting via leading spaces), blockquotes, links,
 * images, horizontal rules, and pipe tables. Deliberately not a full
 * CommonMark implementation — we keep this small and reviewable rather
 * than vendoring a large library.
 *
 * Usage: window.renderMarkdown(text) → HTML string.
 *
 * Escapes raw HTML in source so .md files can't accidentally inject markup.
 */
(function () {
    const RE = {
        fence:    /^```([\w-]*)\s*$/,
        heading:  /^(#{1,6})\s+(.*)$/,
        hr:       /^(?:[-*_]\s*){3,}$/,
        olist:    /^(\s*)(\d+)\.\s+(.*)$/,
        ulist:    /^(\s*)[-*+]\s+(.*)$/,
        quote:    /^>\s?(.*)$/,
        tableSep: /^\s*\|?\s*:?-+:?\s*(\|\s*:?-+:?\s*)+\|?\s*$/,
        tableRow: /^\s*\|(.+)\|\s*$/,
    };

    function escapeHtml(s) {
        return s.replace(/[&<>"']/g, c => ({
            "&": "&amp;", "<": "&lt;", ">": "&gt;",
            '"': "&quot;", "'": "&#39;"
        }[c]));
    }

    function renderInline(s) {
        // Escape first, then re-introduce known markup.
        s = escapeHtml(s);
        // Inline code: `code`
        s = s.replace(/`([^`]+)`/g, (_, c) => `<code>${c}</code>`);
        // Images: ![alt](url)
        s = s.replace(/!\[([^\]]*)\]\(([^)\s]+)\)/g,
            (_, alt, url) => `<img alt="${alt}" src="${url}">`);
        // Links: [text](url)
        s = s.replace(/\[([^\]]+)\]\(([^)\s]+)\)/g,
            (_, txt, url) => `<a href="${url}">${txt}</a>`);
        // Bold: **text**
        s = s.replace(/\*\*([^*]+)\*\*/g, "<strong>$1</strong>");
        // Italic: *text* or _text_ (single underscores must not be inside words)
        s = s.replace(/(?:^|[\s(])\*([^*]+)\*/g, m => {
            const lead = m.startsWith("*") ? "" : m[0];
            const inner = m.slice(lead.length + 1, -1);
            return `${lead}<em>${inner}</em>`;
        });
        // Line breaks: two trailing spaces or trailing backslash
        s = s.replace(/[ \\]{2,}\n/g, "<br>\n");
        return s;
    }

    function parseTable(lines, i) {
        // lines[i] is the header row, lines[i+1] is the separator.
        const split = row => row.replace(/^\s*\|/, "").replace(/\|\s*$/, "")
                                .split("|").map(c => c.trim());
        const header = split(lines[i]);
        const out = ["<table><thead><tr>"];
        header.forEach(h => out.push(`<th>${renderInline(h)}</th>`));
        out.push("</tr></thead><tbody>");
        let j = i + 2;
        while (j < lines.length && RE.tableRow.test(lines[j])) {
            const cells = split(lines[j]);
            out.push("<tr>");
            cells.forEach(c => out.push(`<td>${renderInline(c)}</td>`));
            out.push("</tr>");
            j++;
        }
        out.push("</tbody></table>");
        return { html: out.join(""), next: j };
    }

    function parseList(lines, i, ordered) {
        // Collect items at the same indent level; nested items recurse.
        const pattern = ordered ? RE.olist : RE.ulist;
        const m0 = lines[i].match(pattern);
        const baseIndent = m0[1].length;
        const items = [];
        let j = i;
        while (j < lines.length) {
            const m = lines[j].match(pattern);
            if (!m || m[1].length !== baseIndent) {
                // Allow blank lines to interleave but exit on dedent or content
                // that no longer matches our list level.
                if (lines[j].trim() === "" && j + 1 < lines.length) {
                    const next = lines[j + 1].match(pattern);
                    if (next && next[1].length === baseIndent) { j++; continue; }
                }
                break;
            }
            const text = ordered ? m[3] : m[2];
            const itemLines = [text];
            j++;
            // Pick up any indented continuation lines belonging to this item.
            while (j < lines.length) {
                const continuationIndent = lines[j].match(/^(\s*)/)[1].length;
                if (lines[j].trim() === "") { itemLines.push(""); j++; continue; }
                if (continuationIndent > baseIndent && !lines[j].match(pattern)) {
                    itemLines.push(lines[j].slice(baseIndent + 2));
                    j++;
                } else if (continuationIndent > baseIndent && lines[j].match(pattern)) {
                    // Nested list — fall through to nested render below.
                    break;
                } else {
                    break;
                }
            }
            items.push(itemLines);
        }
        const tag = ordered ? "ol" : "ul";
        const html = [`<${tag}>`];
        for (const itemLines of items) {
            // Recurse into the item body so nested lists / paragraphs work.
            const inner = render(itemLines.join("\n")).trim();
            // Strip surrounding <p>...</p> for single-paragraph items.
            const stripped = inner.replace(/^<p>([\s\S]*)<\/p>$/, "$1");
            html.push(`<li>${stripped}</li>`);
        }
        html.push(`</${tag}>`);
        return { html: html.join(""), next: j };
    }

    function render(text) {
        const lines = text.replace(/\r\n?/g, "\n").split("\n");
        const out = [];
        let i = 0;

        while (i < lines.length) {
            const line = lines[i];

            // Skip blank lines between blocks
            if (line.trim() === "") { i++; continue; }

            // Fenced code block
            const fence = line.match(RE.fence);
            if (fence) {
                const lang = fence[1];
                const buf = [];
                i++;
                while (i < lines.length && !RE.fence.test(lines[i])) {
                    buf.push(lines[i]);
                    i++;
                }
                i++; // closing fence
                const cls = lang ? ` class="lang-${lang}"` : "";
                out.push(`<pre><code${cls}>${escapeHtml(buf.join("\n"))}</code></pre>`);
                continue;
            }

            // Heading
            const h = line.match(RE.heading);
            if (h) {
                const level = h[1].length;
                out.push(`<h${level}>${renderInline(h[2])}</h${level}>`);
                i++;
                continue;
            }

            // Horizontal rule
            if (RE.hr.test(line)) { out.push("<hr>"); i++; continue; }

            // Table (header + separator)
            if (RE.tableRow.test(line)
                && i + 1 < lines.length
                && RE.tableSep.test(lines[i + 1])) {
                const t = parseTable(lines, i);
                out.push(t.html);
                i = t.next;
                continue;
            }

            // Blockquote
            if (RE.quote.test(line)) {
                const buf = [];
                while (i < lines.length && RE.quote.test(lines[i])) {
                    buf.push(lines[i].replace(RE.quote, "$1"));
                    i++;
                }
                out.push(`<blockquote>${render(buf.join("\n"))}</blockquote>`);
                continue;
            }

            // Lists
            if (RE.olist.test(line) || RE.ulist.test(line)) {
                const ordered = RE.olist.test(line);
                const l = parseList(lines, i, ordered);
                out.push(l.html);
                i = l.next;
                continue;
            }

            // Paragraph — accumulate consecutive non-blank, non-block lines.
            const buf = [];
            while (i < lines.length && lines[i].trim() !== ""
                   && !RE.heading.test(lines[i])
                   && !RE.fence.test(lines[i])
                   && !RE.hr.test(lines[i])
                   && !RE.quote.test(lines[i])
                   && !RE.olist.test(lines[i])
                   && !RE.ulist.test(lines[i])
                   && !(RE.tableRow.test(lines[i])
                        && i + 1 < lines.length
                        && RE.tableSep.test(lines[i + 1]))) {
                buf.push(lines[i]);
                i++;
            }
            if (buf.length) {
                out.push(`<p>${renderInline(buf.join("\n"))}</p>`);
            }
        }

        return out.join("\n");
    }

    window.renderMarkdown = render;
})();

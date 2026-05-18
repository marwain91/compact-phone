/* Docs SPA: hash-routes between Markdown pages, renders the sidebar from
 * the PAGES table declared inline in docs/index.html. Each .md file lives
 * in /docs/pages/<slug>.md. Empty file → "Coming soon" placeholder.
 *
 * The Markdown source is local, version-controlled, and never user-supplied;
 * markdown.js escapes inline HTML before rendering. We use replaceChildren()
 * + a DocumentFragment-style helper rather than `.innerHTML =` for clarity.
 */
(function () {
    const navList = document.getElementById("docs-nav-list");
    const content = document.getElementById("docs-content");

    function setHtml(el, html) {
        // Markdown output is pre-escaped by renderMarkdown().
        const tpl = document.createElement("template");
        tpl.innerHTML = html;
        el.replaceChildren(tpl.content);
    }

    function buildNav() {
        const html = [];
        let lastSection = undefined;
        for (const p of window.PAGES || []) {
            if (p.section && p.section !== lastSection) {
                html.push(`<li class="docs-nav-section">${p.section}</li>`);
                lastSection = p.section;
            } else if (!p.section && lastSection !== null) {
                lastSection = null;
            }
            html.push(`<li><a href="#/${p.slug}" data-slug="${p.slug}">${p.title}</a></li>`);
        }
        setHtml(navList, html.join(""));
    }

    function setActive(slug) {
        navList.querySelectorAll("a").forEach(a => {
            a.classList.toggle("active", a.dataset.slug === slug);
        });
    }

    function pageTitle(slug) {
        const p = (window.PAGES || []).find(x => x.slug === slug);
        return p ? p.title : slug;
    }

    async function load(slug) {
        setActive(slug);
        setHtml(content, `<div class="docs-loading">Loading…</div>`);
        try {
            const r = await fetch(`/docs/pages/${slug}.md`, { cache: "no-cache" });
            if (!r.ok) throw new Error(`HTTP ${r.status}`);
            const text = await r.text();
            if (!text.trim()) {
                setHtml(content, `<h1>${pageTitle(slug)}</h1>
                    <p><em>This page is coming soon — content will arrive once the corresponding feature is finalized.</em></p>`);
                return;
            }
            setHtml(content, window.renderMarkdown(text));
            document.title = `${pageTitle(slug)} · Compact Phone Docs`;
            window.scrollTo({ top: 0, behavior: "instant" });
        } catch (e) {
            setHtml(content, `<h1>Not found</h1>
                <p>Couldn't load <code>${slug}.md</code>. ${e.message}</p>
                <p><a href="#/${window.DEFAULT_SLUG}">Back to introduction</a></p>`);
        }
    }

    function currentSlug() {
        const m = window.location.hash.match(/^#\/([\w-]+)/);
        return m ? m[1] : window.DEFAULT_SLUG;
    }

    buildNav();
    load(currentSlug());
    window.addEventListener("hashchange", () => load(currentSlug()));
})();

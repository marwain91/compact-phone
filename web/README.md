# Compact Phone — documentation source

This directory holds the user-facing documentation that the public site at
`https://compactphone.app/docs/` serves.

The landing page itself (`index.html`, `styles.css`, `icon.svg`) and the
deploy pipeline live in the separate
[MyWebs site repo](https://github.com/marwain91/my-webs). MyWebs pulls
`web/docs/` from this repo during its own deploy.

## Layout

```
web/
├── README.md           you are here
└── docs/
    ├── index.html      SPA shell (sidebar + content)
    ├── docs.js         hash-router + page loader
    ├── markdown.js     tiny vanilla MD → HTML renderer
    └── pages/
        ├── introduction.md
        ├── installation.md      empty → "coming soon" placeholder
        ├── getting-started.md
        ├── configuration.md
        ├── provisioning.md
        ├── troubleshooting.md
        └── faq.md
```

## Authoring a doc page

1. Drop a new file in `web/docs/pages/<slug>.md`.
2. Add an entry to the `PAGES` table at the top of `web/docs/index.html`:
   ```js
   { slug: "my-page", title: "My page", section: "Using" }
   ```
3. Commit + PR + squash-merge. MyWebs picks up the change on its next deploy.

Markdown features supported: headings, paragraphs, bold/italic/inline code,
fenced code blocks, ordered + unordered lists (nested), blockquotes, links,
images, hr, pipe tables. Raw HTML in `.md` files is escaped — keep markup
in Markdown syntax.

Empty `.md` files render as a "coming soon" placeholder so the navigation
surface stays stable while content is being written.

## Preview locally

```bash
make docs-list           # see what pages exist
```

For a styled preview, set up a temporary site root that includes the
landing page's `styles.css` and `icon.svg`. The docs SPA references both
at the host root (`/styles.css`, `/icon.svg`); they live in the MyWebs repo.

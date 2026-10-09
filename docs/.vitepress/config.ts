import { defineConfig } from "vitepress";

export default defineConfig({
  title: "openbv",
  description: "Babo Violent 2 on gasm: the original game, from its GPL source, on macOS, Linux and Windows.",
  cleanUrls: true,
  lastUpdated: true,
  srcExclude: ["README.md", "AGENTS.md"],
  sitemap: { hostname: "https://openbv.emdzej.pl" },
  // /play/ holds gasm's browser runtime and the background's module (scripts/vendor-web.sh, build-bg.sh).
  ignoreDeadLinks: [/^\/play\//],

  head: [
    ["link", { rel: "icon", href: "/favicon.svg", type: "image/svg+xml" }],
    ["meta", { name: "theme-color", content: "#0a5fd6" }],
    ["meta", { property: "og:title", content: "openbv — Babo Violent 2 on gasm" }],
    ["meta", { property: "og:description", content: "The original Babo Violent 2, ported from its GPL source to gasm: macOS, Linux and Windows from one WebAssembly file." }],
    ["meta", { property: "og:image", content: "https://openbv.emdzej.pl/screenshots/in-game.webp" }],
    ["meta", { property: "og:url", content: "https://openbv.emdzej.pl/" }],
  ],

  themeConfig: {
    siteTitle: "openbv",
    logo: "/favicon.svg",

    nav: [
      { text: "Guide", link: "/guide/", activeMatch: "/guide/" },
      { text: "Internals", link: "/internals/", activeMatch: "/internals/" },
      { text: "Status", link: "/status" },
      { text: "Download", link: "https://github.com/emdzej/openbv/releases" },
    ],

    sidebar: {
      "/guide/": [
        {
          text: "Guide",
          items: [
            { text: "Getting started", link: "/guide/" },
            { text: "Controls", link: "/guide/controls" },
            { text: "Hosting a game", link: "/guide/hosting" },
            { text: "Building from source", link: "/guide/build" },
          ],
        },
      ],
      "/internals/": [
        {
          text: "Internals",
          items: [
            { text: "Architecture", link: "/internals/" },
            { text: "Networking", link: "/internals/networking" },
          ],
        },
      ],
    },

    socialLinks: [{ icon: "github", link: "https://github.com/emdzej/openbv" }],

    editLink: {
      pattern: "https://github.com/emdzej/openbv/edit/main/docs/:path",
      text: "Edit this page on GitHub",
    },

    search: { provider: "local" },

    footer: {
      message:
        "openbv is released under the GPL-3.0, like the Babo Violent 2 source it is built from (© 2012 bitHeads inc., released by Daivuk). The game's art and sound are RndLabs'. It runs on <a href=\"https://gasm.emdzej.pl\">gasm</a>. <a href=\"/credits\">Credits</a>",
    },
  },
});

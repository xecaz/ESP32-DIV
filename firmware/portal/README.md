# Captive Portal template

Drop the contents of this folder onto the root of the ESP32-DIV's SD card
so the path is `/portal/` on the card itself:

```
<SD root>/
  portal/
    index.html     # the landing / sign-in page
    style.css      # styling shared by index.html + post.html
    logo.svg       # optional brand mark; falls back to hidden if absent
    post.html      # shown after credentials are submitted (optional)
```

The firmware (`radio::captive`) looks for `/portal/index.html` every time
the Captive Portal feature starts. If present, it serves the folder
verbatim for any HTTP GET — that means you can drop extra assets (fonts,
images, JS) alongside and reference them with absolute URLs like
`/hero.png` or `/script.js`. If `/portal/` is missing, the built-in
inline form is used instead.

## What gets captured

Form field names are **`u`** (username/email) and **`p`** (password);
keep those names in your custom HTML or the firmware won't pick up the
submission. Example minimum form:

```html
<form action="/submit" method="POST">
  <input name="u" required>
  <input name="p" type="password" required>
  <button>Continue</button>
</form>
```

Every submission is appended to the in-device log visible on the Captive
Portal screen (scrollable, cleartext, with a relative timestamp). Up to
64 submissions per session are kept in RAM; stopping the portal (LEFT on
the screen) discards them, so off-load anything you need via USB MSC in
a later firmware revision.

## Content-Type dispatch

Extensions the firmware auto-maps:

| Extension           | Type                       |
|---------------------|----------------------------|
| .html, .htm         | text/html                  |
| .css                | text/css                   |
| .js                 | application/javascript     |
| .json               | application/json           |
| .png                | image/png                  |
| .jpg, .jpeg         | image/jpeg                 |
| .gif                | image/gif                  |
| .svg                | image/svg+xml              |
| .ico                | image/x-icon               |
| .webp               | image/webp                 |
| .woff2              | font/woff2                 |
| (anything else)     | text/plain                 |

## Legal / ethical note

This is a research tool. Only run the portal against networks and
devices you own or have explicit written permission to test. Cloning a
corporate or public Wi-Fi sign-in page to harvest credentials from
unsuspecting users is a crime in most jurisdictions.

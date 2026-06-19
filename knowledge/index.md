# Knowledge hub

Everything authoritative about the badge and its plugins lives in the vendored
submodules under `vendor/` and stays current when you update them
(`git submodule update --remote vendor/<name>`). Start here.

## Plugin SDK & how-to (vendor/cdc-badge-plugins)

- SDK crate & safe wrappers: [`vendor/cdc-badge-plugins/sdk/cdc-badge-plugin/src/`](../vendor/cdc-badge-plugins/sdk/cdc-badge-plugin/src/)
- Getting started: [`vendor/cdc-badge-plugins/docs/getting_started.md`](../vendor/cdc-badge-plugins/docs/getting_started.md)
- Manifest (`meta.json`) schema: [`vendor/cdc-badge-plugins/docs/manifest_schema.md`](../vendor/cdc-badge-plugins/docs/manifest_schema.md)
- Example plugins to read: [`vendor/cdc-badge-plugins/examples/`](../vendor/cdc-badge-plugins/examples/)
- Plugin templates: [`vendor/cdc-badge-plugins/sdk/plugin_template_rust/`](../vendor/cdc-badge-plugins/sdk/plugin_template_rust/)
- Browser installer (webflasher): [`vendor/cdc-badge-plugins/webflasher/`](../vendor/cdc-badge-plugins/webflasher/)

## Host API (the contract between plugin and firmware)

- Canonical header (single source of truth - read, never edit):
  [`vendor/cdc-badge-plugins/sdk/host_api.h`](../vendor/cdc-badge-plugins/sdk/host_api.h)
- Online reference: <https://krim404.github.io/cdc-badge-os/dev/host-api/> (Doxygen: <https://krim404.github.io/cdc-badge-os/api/host__api_8h.html>)

## Firmware docs & specifications (vendor/cdc-badge-os)

- Developer docs: [`vendor/cdc-badge-os/website/src/content/docs/dev/`](../vendor/cdc-badge-os/website/src/content/docs/dev/)
  (plugin-sdk, host-api, architecture, and `proto/serial-commands.md`)
- Feature specifications: [`vendor/cdc-badge-os/specs/`](../vendor/cdc-badge-os/specs/)
- Upload tool used by `badge flash`: [`vendor/cdc-badge-os/tools/upload.py`](../vendor/cdc-badge-os/tools/upload.py)

## Keeping it current

```sh
git submodule update --remote vendor/cdc-badge-os vendor/cdc-badge-plugins
git add vendor && git commit -m "Update vendored knowledge"
```

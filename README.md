# MagicGate Patch

An experimental RAM-only PSP plugin intended to reduce memory-card delays associated with a MagicGate-related firmware function.
It is based on the corresponding patch in the 5.50 iR Shell plugin.

The plugin has been tested on PSP-1000 and PSP-2000 models running ARK-5 6.61 firmware with a MicroSD card adapter.

I left out these systems as i do not have the ability to verify them:
- PSP-3000
- PSP Go
- PSP E1000 (Street)

## Compatibility

- Firmware **6.61** only.
- PSP-1000, PSP-2000.
- Tested with ARK-5. Other custom firmwares are untested.
- PSP-3000, PSP Go, PSP E1000 (Street) and PS Vita are not supported.

The plugin checks the underlying firmware version, model, loaded module and expected instructions before patching. If a check fails, it does not apply the patch.

## Installation

1. Copy `magicgate-patch.prx` to `ms0:/SEPLUGINS/`.
2. Add the following line to `ms0:/SEPLUGINS/PLUGINS.TXT`, preserving existing entries:

   ```text
   vsh, ms0:/SEPLUGINS/magicgate-patch.prx, on
   ```

3. Fully reboot the PSP. Check `ms0:/SEPLUGINS/magicgate-test.log` for `Patch installed in RAM` or a refusal message.

See the [ARK-5 plugin documentation](https://github.com/PSP-Arkfive/ARK-5/wiki/Plugin-Management) for Plugin Manager and manual configuration instructions.

The `vsh` entry loads the plugin in the XMB, not games. It may also load too late to affect checks performed early during startup.

## Disabling

Disable or remove the entry using ARK's Plugin Manager or edit `PLUGINS.TXT`, then reboot.
The plugin does not write to flash.
The RAM patch is removed on reboot and reapplied only if the plugin is enabled.

## Building

Install the PSP toolchain and PSPSDK, and ensure `psp-config` and the PSP compiler tools are on `PATH`. From this directory, run:

```sh
make
```

The output is `magicgate-patch.prx`.

## How it works

The original 5.50 patch replaces a function in `sceMgr_Driver` with a stub returning `0x80504002`, an error in the MagicGate facility.
This plugin applies the equivalent replacement to the matching 6.61 function in memory after verifying its instructions.

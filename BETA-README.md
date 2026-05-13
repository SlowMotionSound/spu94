# SPU-94 Beta

Bit-faithful PS1 SPU reverb plugin for your DAW.

## Supported Formats and Platforms

| Format | Linux | macOS | Windows |
|--------|-------|-------|---------|
| VST3   | Yes   | Yes   | Yes     |
| AU     | --    | Yes   | --      |
| LV2    | Yes   | --    | --      |
| CLAP   | Yes   | Yes   | Yes     |

The standalone build is an internal development tool and is not included in beta
distributions. Plugin formats receive audio from your DAW's host buffer.

## Installation

### macOS

**Installer (.pkg):** Run the `.pkg` installer. It places binaries in the
standard system scan paths:

- AU: `/Library/Audio/Plug-Ins/Components/`
- VST3: `/Library/Audio/Plug-Ins/VST3/`
- CLAP: `/Library/Audio/Plug-Ins/CLAP/`

AU must be installed to the system path (not `~/Library`). Logic ignores
user-path AU in some configurations.

**Drag-install (.dmg):** Mount the `.dmg` and copy each plugin bundle to the
paths listed above. After copying, remove the quarantine attribute from each
bundle (see "Bypassing Unsigned Binary Warnings" below).

### Windows

Run the Inno Setup installer. It places binaries in:

- VST3: `C:\Program Files\Common Files\VST3\`
- CLAP: `C:\Program Files\Common Files\CLAP\`

### Linux

Extract the tarball and run `install.sh`. It places binaries in:

- VST3: `~/.vst3/`
- LV2: `~/.lv2/`
- CLAP: `~/.clap/`

## Bypassing Unsigned Binary Warnings

SPU-94 beta builds are not code-signed. Your operating system will warn you
before running them. This is normal for unsigned software from a small developer.

### macOS (Gatekeeper)

**Method 1 -- Right-click Open:**
Right-click the `.pkg` installer (or a plugin bundle if using drag-install),
select **Open**, then confirm **Open** in the dialog that appears.

**Method 2 -- System Settings:**
If the installer or bundle was blocked on first launch, open
**System Settings > Privacy & Security**, scroll down to the blocked item,
and click **Open Anyway**.

**Method 3 -- Remove quarantine attribute (drag-install only):**
After copying plugin bundles from the `.dmg`, run these commands in Terminal:

```
xattr -cr /Library/Audio/Plug-Ins/Components/SPU-94.component
xattr -cr /Library/Audio/Plug-Ins/VST3/SPU-94.vst3
xattr -cr /Library/Audio/Plug-Ins/CLAP/SPU-94.clap
```

### Windows (SmartScreen)

When the "Windows protected your PC" dialog appears, click **More info**, then
click **Run anyway**. This is a one-time approval per installer executable.

## Resetting Plugin Cache

After installing an updated beta build, your DAW may still load the previous
version from its plugin cache. Use these per-DAW instructions to force a
rescan.

### Reaper

Open **Preferences > VST** and click **Clear cache and re-scan VST paths for
all plugins**.

Alternatively, delete the cache files under your Reaper resource path:

- `reaper-vstplugins64.ini`
- `reaper-auplugins64.ini`
- `reaper-clapplugins64.ini`
- `reaper-lv2plugins64.ini`

### Ardour

Open **Window > Plugin Manager** and click **Re-scan** at the bottom of the
window.

### Logic Pro

Open **Settings > Plug-In Manager** and click **Full Audio Unit Reset**.

You can also manually delete the AU cache files:

- `~/Library/Caches/AudioUnitCache/com.apple.audiounits.cache`
- `~/Library/Caches/AudioUnitCache/com.apple.audiounits.sandboxed.cache`

### Ableton Live

Open **Preferences > Plug-Ins** and **Alt+click** (Option+click on macOS)
the **Rescan** button for a full rescan. A normal click performs an incremental
scan that may not pick up the updated build.

### FL Studio

Open **Options > Manage plugins** and click **Rescan and verify installed
plugins**. Make sure the **Rescan previously verified plugins** checkbox is
enabled so FL Studio re-validates the updated binary.

### Bitwig

Open **Dashboard > Settings > Locations** and click **Rescan** next to your
plugin directories.

## Pro Tools

SPU-94 does not ship in AAX format. Pro Tools users can load the VST3 build
using a wrapper host such as Blue Cat PatchWork.

## Known Issues

- **Standalone system volume bug (macOS):** The standalone JUCE wrapper may
  change the macOS system volume on launch or close. This does not affect any
  plugin format (VST3, AU, LV2, CLAP). The standalone is an internal
  development tool and is not included in beta distributions.

- **LV2 GUI state in Ardour:** When the plugin window is closed and reopened in
  Ardour (LV2 format), the parameter display may reset to default positions.
  Audio processing is unaffected -- the engine state is preserved in the
  processor. This is a known GUI synchronization issue under investigation.

## Feedback

When reporting an issue, please include:

- **DAW name and version** (e.g., Reaper 7.30, Logic 11.1.2)
- **OS and version** (e.g., Ubuntu 24.04, macOS 15.4, Windows 11 24H2)
- **Plugin format used** (VST3, AU, LV2, or CLAP)
- **Steps to reproduce** the issue, as specifically as possible

Thank you for testing SPU-94.

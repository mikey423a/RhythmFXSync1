# Getting the Windows VST3 (no coding)

GitHub builds the plugin for you on a Windows machine. You just download the result.

1. In **GitHub Desktop**: *Repository > Show in Explorer*, replace the repository's files with the contents of this folder
   (keep the hidden `.github` folder - it contains the build recipe), then **Commit to main** and **Push origin**.
2. On github.com open your repository, click the **Actions** tab, and click the newest run called **Build**.
   Wait for the green tick on the Windows jobs (about 10-15 minutes).
3. Scroll to **Artifacts** at the bottom of that run. Download **RhythmFXSync-windows-latest-guiON**
   (this has the new Studio / Perform pages). If that one is red or missing, download **...-guiOFF** instead - same plugin, fewer pages.
4. Unzip it. Inside `VST3` is a folder named **RhythmFX Sync.vst3**. Copy that whole folder into
   `C:\Program Files\Common Files\VST3\` (Windows will ask for admin permission).
5. Open your DAW and rescan plug-ins. Put RhythmFX Sync on a guitar track as an insert.

The `Standalone` folder holds a regular `.exe` you can double-click to try it without a DAW
(it starts with audio input muted to prevent feedback - change that under Options > Audio/MIDI Settings, and use headphones).

If a run shows a red cross, click it, open the failed step, copy the last ~30 lines of red text and send them to me.
